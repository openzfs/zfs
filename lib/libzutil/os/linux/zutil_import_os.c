/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright 2015 RackTop Systems.
 * Copyright (c) 2016, Intel Corporation.
 */

/*
 * Pool import support functions.
 *
 * Used by zpool, ztest, zdb, and zhack to locate importable configs. Since
 * these commands are expected to run in the global zone, we can assume
 * that the devices are all readable when called.
 *
 * To import a pool, we rely on reading the configuration information from the
 * ZFS label of each device.  If we successfully read the label, then we
 * organize the configuration information in the following hierarchy:
 *
 *	pool guid -> toplevel vdev guid -> label txg
 *
 * Duplicate entries matching this same tuple will be discarded.  Once we have
 * examined every device, we pick the best label txg config for each toplevel
 * vdev.  We then arrange these toplevel vdevs into a complete pool config, and
 * update any paths that have changed.  Finally, we attempt to import the pool
 * using our derived config, and record the results.
 */

#include <ctype.h>
#include <devid.h>
#include <dirent.h>
#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dktp/fdisk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/vdev_impl.h>

#include <thread_pool.h>
#include <libzutil.h>
#include <libnvpair.h>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <sched.h>
#endif
#include <blkid/blkid.h>

#define	IMPORT_ORDER_PREFERRED_1	1
#define	IMPORT_ORDER_PREFERRED_2	2
#define	IMPORT_ORDER_SCAN_OFFSET	10
#define	IMPORT_ORDER_DEFAULT		100
#define	DEFAULT_IMPORT_PATH_SIZE	9

#define	DEV_BYID_PATH	"/dev/disk/by-id/"

#define	EZFS_BADPATH	"must be an absolute path"

/*
 * Intermediate structures used to gather configuration information.
 */
typedef struct config_entry {
	uint64_t		ce_txg;
	nvlist_t		*ce_config;
	struct config_entry	*ce_next;
} config_entry_t;

typedef struct vdev_entry {
	uint64_t		ve_guid;
	config_entry_t		*ve_configs;
	struct vdev_entry	*ve_next;
} vdev_entry_t;

typedef struct pool_entry {
	uint64_t		pe_guid;
	vdev_entry_t		*pe_vdevs;
	struct pool_entry	*pe_next;
} pool_entry_t;

typedef struct name_entry {
	char			*ne_name;
	uint64_t		ne_guid;
	uint64_t		ne_order;
	uint64_t		ne_num_labels;
	struct name_entry	*ne_next;
} name_entry_t;

typedef struct pool_list {
	pool_entry_t		*pools;
	name_entry_t		*names;
} pool_list_t;

typedef struct rdsk_node {
	char *rn_name;			/* Full path to device */
	int rn_order;			/* Preferred order (low to high) */
	int rn_num_labels;		/* Number of valid labels */
	uint64_t rn_vdev_guid;		/* Expected vdev guid when set */
	libpc_handle_t *rn_hdl;
	nvlist_t *rn_config;		/* Label config */
	avl_tree_t *rn_avl;
	avl_node_t rn_node;
	pthread_mutex_t *rn_lock;
	boolean_t rn_labelpaths;
} rdsk_node_t;

/*
 * Go through and fix up any path and/or devid information for the given vdev
 * configuration.
 */
static int
fix_paths(libpc_handle_t *hdl, nvlist_t *nv, name_entry_t *names)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	name_entry_t *ne, *best;
	char *path;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (fix_paths(hdl, child[c], names) != 0)
				return (-1);
		return (0);
	}

	/*
	 * This is a leaf (file or disk) vdev.  In either case, go through
	 * the name list and see if we find a matching guid.  If so, replace
	 * the path and see if we can calculate a new devid.
	 *
	 * There may be multiple names associated with a particular guid, in
	 * which case we have overlapping partitions or multiple paths to the
	 * same disk.  In this case we prefer to use the path name which
	 * matches the ZPOOL_CONFIG_PATH.  If no matching entry is found we
	 * use the lowest order device which corresponds to the first match
	 * while traversing the ZPOOL_IMPORT_PATH search path.
	 */
	verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0);
	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		path = NULL;

	best = NULL;
	for (ne = names; ne != NULL; ne = ne->ne_next) {
		if (ne->ne_guid == guid) {
			if (path == NULL) {
				best = ne;
				break;
			}

			if ((strlen(path) == strlen(ne->ne_name)) &&
			    strncmp(path, ne->ne_name, strlen(path)) == 0) {
				best = ne;
				break;
			}

			if (best == NULL) {
				best = ne;
				continue;
			}

			/* Prefer paths with move vdev labels. */
			if (ne->ne_num_labels > best->ne_num_labels) {
				best = ne;
				continue;
			}

			/* Prefer paths earlier in the search order. */
			if (ne->ne_num_labels == best->ne_num_labels &&
			    ne->ne_order < best->ne_order) {
				best = ne;
				continue;
			}
		}
	}

	if (best == NULL)
		return (0);

	if (nvlist_add_string(nv, ZPOOL_CONFIG_PATH, best->ne_name) != 0)
		return (-1);

	/* Linux only - update ZPOOL_CONFIG_DEVID and ZPOOL_CONFIG_PHYS_PATH */
	update_vdev_config_dev_strs(nv);

	return (0);
}

/*
 * Add the given configuration to the list of known devices.
 */
static int
add_config(libpc_handle_t *hdl, pool_list_t *pl, const char *path,
    int order, int num_labels, nvlist_t *config)
{
	uint64_t pool_guid, vdev_guid, top_guid, txg, state;
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	name_entry_t *ne;

	/*
	 * If this is a hot spare not currently in use or level 2 cache
	 * device, add it to the list of names to translate, but don't do
	 * anything else.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &state) == 0 &&
	    (state == POOL_STATE_SPARE || state == POOL_STATE_L2CACHE) &&
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid) == 0) {
		if ((ne = zutil_alloc(hdl, sizeof (name_entry_t))) == NULL)
			return (-1);

		if ((ne->ne_name = zutil_strdup(hdl, path)) == NULL) {
			free(ne);
			return (-1);
		}
		ne->ne_guid = vdev_guid;
		ne->ne_order = order;
		ne->ne_num_labels = num_labels;
		ne->ne_next = pl->names;
		pl->names = ne;

		return (0);
	}

	/*
	 * If we have a valid config but cannot read any of these fields, then
	 * it means we have a half-initialized label.  In vdev_label_init()
	 * we write a label with txg == 0 so that we can identify the device
	 * in case the user refers to the same disk later on.  If we fail to
	 * create the pool, we'll be left with a label in this state
	 * which should not be considered part of a valid pool.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pool_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
	    &vdev_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_TOP_GUID,
	    &top_guid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &txg) != 0 || txg == 0) {
		return (0);
	}

	/*
	 * First, see if we know about this pool.  If not, then add it to the
	 * list of known pools.
	 */
	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		if (pe->pe_guid == pool_guid)
			break;
	}

	if (pe == NULL) {
		if ((pe = zutil_alloc(hdl, sizeof (pool_entry_t))) == NULL) {
			return (-1);
		}
		pe->pe_guid = pool_guid;
		pe->pe_next = pl->pools;
		pl->pools = pe;
	}

	/*
	 * Second, see if we know about this toplevel vdev.  Add it if its
	 * missing.
	 */
	for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {
		if (ve->ve_guid == top_guid)
			break;
	}

	if (ve == NULL) {
		if ((ve = zutil_alloc(hdl, sizeof (vdev_entry_t))) == NULL) {
			return (-1);
		}
		ve->ve_guid = top_guid;
		ve->ve_next = pe->pe_vdevs;
		pe->pe_vdevs = ve;
	}

	/*
	 * Third, see if we have a config with a matching transaction group.  If
	 * so, then we do nothing.  Otherwise, add it to the list of known
	 * configs.
	 */
	for (ce = ve->ve_configs; ce != NULL; ce = ce->ce_next) {
		if (ce->ce_txg == txg)
			break;
	}

	if (ce == NULL) {
		if ((ce = zutil_alloc(hdl, sizeof (config_entry_t))) == NULL) {
			return (-1);
		}
		ce->ce_txg = txg;
		ce->ce_config = fnvlist_dup(config);
		ce->ce_next = ve->ve_configs;
		ve->ve_configs = ce;
	}

	/*
	 * At this point we've successfully added our config to the list of
	 * known configs.  The last thing to do is add the vdev guid -> path
	 * mappings so that we can fix up the configuration as necessary before
	 * doing the import.
	 */
	if ((ne = zutil_alloc(hdl, sizeof (name_entry_t))) == NULL)
		return (-1);

	if ((ne->ne_name = zutil_strdup(hdl, path)) == NULL) {
		free(ne);
		return (-1);
	}

	ne->ne_guid = vdev_guid;
	ne->ne_order = order;
	ne->ne_num_labels = num_labels;
	ne->ne_next = pl->names;
	pl->names = ne;

	return (0);
}

/*
 * Determine if the vdev id is a hole in the namespace.
 */
static boolean_t
vdev_is_hole(uint64_t *hole_array, uint_t holes, uint_t id)
{
	int c;

	for (c = 0; c < holes; c++) {

		/* Top-level is a hole */
		if (hole_array[c] == id)
			return (B_TRUE);
	}
	return (B_FALSE);
}


/*
 * Convert our list of pools into the definitive set of configurations.  We
 * start by picking the best config for each toplevel vdev.  Once that's done,
 * we assemble the toplevel vdevs into a full config for the pool.  We make a
 * pass to fix up any incorrect paths, and then add it to the main list to
 * return to the user.
 */
static nvlist_t *
get_configs(libpc_handle_t *hdl, pool_list_t *pl, boolean_t active_ok,
    nvlist_t *policy)
{
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	nvlist_t *ret = NULL, *config = NULL, *tmp = NULL, *nvtop, *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t i, nspares, nl2cache;
	boolean_t config_seen;
	uint64_t best_txg;
	char *name, *hostname = NULL;
	uint64_t guid;
	uint_t children = 0;
	nvlist_t **child = NULL;
	uint_t holes;
	uint64_t *hole_array, max_id;
	uint_t c;
	boolean_t isactive;
	uint64_t hostid;
	nvlist_t *nvl;
	boolean_t valid_top_config = B_FALSE;

	if (nvlist_alloc(&ret, 0, 0) != 0)
		goto nomem;

	for (pe = pl->pools; pe != NULL; pe = pe->pe_next) {
		uint64_t id, max_txg = 0;

		if (nvlist_alloc(&config, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		config_seen = B_FALSE;

		/*
		 * Iterate over all toplevel vdevs.  Grab the pool configuration
		 * from the first one we find, and then go through the rest and
		 * add them as necessary to the 'vdevs' member of the config.
		 */
		for (ve = pe->pe_vdevs; ve != NULL; ve = ve->ve_next) {

			/*
			 * Determine the best configuration for this vdev by
			 * selecting the config with the latest transaction
			 * group.
			 */
			best_txg = 0;
			for (ce = ve->ve_configs; ce != NULL;
			    ce = ce->ce_next) {

				if (ce->ce_txg > best_txg) {
					tmp = ce->ce_config;
					best_txg = ce->ce_txg;
				}
			}

			/*
			 * We rely on the fact that the max txg for the
			 * pool will contain the most up-to-date information
			 * about the valid top-levels in the vdev namespace.
			 */
			if (best_txg > max_txg) {
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_VDEV_CHILDREN,
				    DATA_TYPE_UINT64);
				(void) nvlist_remove(config,
				    ZPOOL_CONFIG_HOLE_ARRAY,
				    DATA_TYPE_UINT64_ARRAY);

				max_txg = best_txg;
				hole_array = NULL;
				holes = 0;
				max_id = 0;
				valid_top_config = B_FALSE;

				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VDEV_CHILDREN, &max_id) == 0) {
					verify(nvlist_add_uint64(config,
					    ZPOOL_CONFIG_VDEV_CHILDREN,
					    max_id) == 0);
					valid_top_config = B_TRUE;
				}

				if (nvlist_lookup_uint64_array(tmp,
				    ZPOOL_CONFIG_HOLE_ARRAY, &hole_array,
				    &holes) == 0) {
					verify(nvlist_add_uint64_array(config,
					    ZPOOL_CONFIG_HOLE_ARRAY,
					    hole_array, holes) == 0);
				}
			}

			if (!config_seen) {
				/*
				 * Copy the relevant pieces of data to the pool
				 * configuration:
				 *
				 *	version
				 *	pool guid
				 *	name
				 *	comment (if available)
				 *	pool state
				 *	hostid (if available)
				 *	hostname (if available)
				 */
				uint64_t state, version;
				char *comment = NULL;

				version = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_VERSION);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_VERSION, version);
				guid = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_GUID);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, guid);
				name = fnvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_POOL_NAME);
				fnvlist_add_string(config,
				    ZPOOL_CONFIG_POOL_NAME, name);

				if (nvlist_lookup_string(tmp,
				    ZPOOL_CONFIG_COMMENT, &comment) == 0)
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_COMMENT, comment);

				state = fnvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_STATE);
				fnvlist_add_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, state);

				hostid = 0;
				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
					fnvlist_add_uint64(config,
					    ZPOOL_CONFIG_HOSTID, hostid);
					hostname = fnvlist_lookup_string(tmp,
					    ZPOOL_CONFIG_HOSTNAME);
					fnvlist_add_string(config,
					    ZPOOL_CONFIG_HOSTNAME, hostname);
				}

				config_seen = B_TRUE;
			}

			/*
			 * Add this top-level vdev to the child array.
			 */
			verify(nvlist_lookup_nvlist(tmp,
			    ZPOOL_CONFIG_VDEV_TREE, &nvtop) == 0);
			verify(nvlist_lookup_uint64(nvtop, ZPOOL_CONFIG_ID,
			    &id) == 0);

			if (id >= children) {
				nvlist_t **newchild;

				newchild = zutil_alloc(hdl, (id + 1) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = id + 1;
			}
			if (nvlist_dup(nvtop, &child[id], 0) != 0)
				goto nomem;

		}

		/*
		 * If we have information about all the top-levels then
		 * clean up the nvlist which we've constructed. This
		 * means removing any extraneous devices that are
		 * beyond the valid range or adding devices to the end
		 * of our array which appear to be missing.
		 */
		if (valid_top_config) {
			if (max_id < children) {
				for (c = max_id; c < children; c++)
					nvlist_free(child[c]);
				children = max_id;
			} else if (max_id > children) {
				nvlist_t **newchild;

				newchild = zutil_alloc(hdl, (max_id) *
				    sizeof (nvlist_t *));
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				free(child);
				child = newchild;
				children = max_id;
			}
		}

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		/*
		 * The vdev namespace may contain holes as a result of
		 * device removal. We must add them back into the vdev
		 * tree before we process any missing devices.
		 */
		if (holes > 0) {
			ASSERT(valid_top_config);

			for (c = 0; c < children; c++) {
				nvlist_t *holey;

				if (child[c] != NULL ||
				    !vdev_is_hole(hole_array, holes, c))
					continue;

				if (nvlist_alloc(&holey, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;

				/*
				 * Holes in the namespace are treated as
				 * "hole" top-level vdevs and have a
				 * special flag set on them.
				 */
				if (nvlist_add_string(holey,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_HOLE) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(holey,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(holey);
					goto nomem;
				}
				child[c] = holey;
			}
		}

		/*
		 * Look for any missing top-level vdevs.  If this is the case,
		 * create a faked up 'missing' vdev as a placeholder.  We cannot
		 * simply compress the child array, because the kernel performs
		 * certain checks to make sure the vdev IDs match their location
		 * in the configuration.
		 */
		for (c = 0; c < children; c++) {
			if (child[c] == NULL) {
				nvlist_t *missing;
				if (nvlist_alloc(&missing, NV_UNIQUE_NAME,
				    0) != 0)
					goto nomem;
				if (nvlist_add_string(missing,
				    ZPOOL_CONFIG_TYPE,
				    VDEV_TYPE_MISSING) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_ID, c) != 0 ||
				    nvlist_add_uint64(missing,
				    ZPOOL_CONFIG_GUID, 0ULL) != 0) {
					nvlist_free(missing);
					goto nomem;
				}
				child[c] = missing;
			}
		}

		/*
		 * Put all of this pool's top-level vdevs into a root vdev.
		 */
		if (nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) != 0)
			goto nomem;
		if (nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_ROOT) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) != 0 ||
		    nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, guid) != 0 ||
		    nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
		    child, children) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		for (c = 0; c < children; c++)
			nvlist_free(child[c]);
		free(child);
		children = 0;
		child = NULL;

		/*
		 * Go through and fix up any paths and/or devids based on our
		 * known list of vdev GUID -> path mappings.
		 */
		if (fix_paths(hdl, nvroot, pl->names) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}

		/*
		 * Add the root vdev to this pool's configuration.
		 */
		if (nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    nvroot) != 0) {
			nvlist_free(nvroot);
			goto nomem;
		}
		nvlist_free(nvroot);

		/*
		 * zdb uses this path to report on active pools that were
		 * imported or created using -R.
		 */
		if (active_ok)
			goto add_pool;

		/*
		 * Determine if this pool is currently active, in which case we
		 * can't actually import it.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		if (zutil_pool_active(hdl, name, guid, &isactive) != 0)
			goto error;

		if (isactive) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		if (policy != NULL) {
			if (nvlist_add_nvlist(config, ZPOOL_LOAD_POLICY,
			    policy) != 0)
				goto nomem;
		}

		if ((nvl = zutil_refresh_config(hdl, config)) == NULL) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		nvlist_free(config);
		config = nvl;

		/*
		 * Go through and update the paths for spares, now that we have
		 * them.
		 */
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0) {
			for (i = 0; i < nspares; i++) {
				if (fix_paths(hdl, spares[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Update the paths for l2cache devices.
		 */
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0) {
			for (i = 0; i < nl2cache; i++) {
				if (fix_paths(hdl, l2cache[i], pl->names) != 0)
					goto nomem;
			}
		}

		/*
		 * Restore the original information read from the actual label.
		 */
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTID,
		    DATA_TYPE_UINT64);
		(void) nvlist_remove(config, ZPOOL_CONFIG_HOSTNAME,
		    DATA_TYPE_STRING);
		if (hostid != 0) {
			verify(nvlist_add_uint64(config, ZPOOL_CONFIG_HOSTID,
			    hostid) == 0);
			verify(nvlist_add_string(config, ZPOOL_CONFIG_HOSTNAME,
			    hostname) == 0);
		}

add_pool:
		/*
		 * Add this pool to the list of configs.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);

		if (nvlist_add_nvlist(ret, name, config) != 0)
			goto nomem;

		nvlist_free(config);
		config = NULL;
	}

	return (ret);

nomem:
	(void) zutil_no_memory(hdl);
error:
	nvlist_free(config);
	nvlist_free(ret);
	for (c = 0; c < children; c++)
		nvlist_free(child[c]);
	free(child);

	return (NULL);
}


/*
 * Sorted by full path and then vdev guid to allow for multiple entries with
 * the same full path name.  This is required because it's possible to
 * have multiple block devices with labels that refer to the same
 * ZPOOL_CONFIG_PATH yet have different vdev guids.  In this case both
 * entries need to be added to the cache.  Scenarios where this can occur
 * include overwritten pool labels, devices which are visible from multiple
 * hosts and multipath devices.
 */
static int
slice_cache_compare(const void *arg1, const void *arg2)
{
	const char  *nm1 = ((rdsk_node_t *)arg1)->rn_name;
	const char  *nm2 = ((rdsk_node_t *)arg2)->rn_name;
	uint64_t guid1 = ((rdsk_node_t *)arg1)->rn_vdev_guid;
	uint64_t guid2 = ((rdsk_node_t *)arg2)->rn_vdev_guid;
	int rv;

	rv = AVL_ISIGN(strcmp(nm1, nm2));
	if (rv)
		return (rv);

	return (AVL_CMP(guid1, guid2));
}

static boolean_t
is_watchdog_dev(char *dev)
{
	/* For 'watchdog' dev */
	if (strcmp(dev, "watchdog") == 0)
		return (B_TRUE);

	/* For 'watchdog<digit><whatever> */
	if (strstr(dev, "watchdog") == dev && isdigit(dev[8]))
		return (B_TRUE);

	return (B_FALSE);
}

static int
label_paths_impl(libpc_handle_t *hdl, nvlist_t *nvroot, uint64_t pool_guid,
    uint64_t vdev_guid, char **path, char **devid)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	char *val;
	int error;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			error  = label_paths_impl(hdl, child[c],
			    pool_guid, vdev_guid, path, devid);
			if (error)
				return (error);
		}
		return (0);
	}

	if (nvroot == NULL)
		return (0);

	error = nvlist_lookup_uint64(nvroot, ZPOOL_CONFIG_GUID, &guid);
	if ((error != 0) || (guid != vdev_guid))
		return (0);

	error = nvlist_lookup_string(nvroot, ZPOOL_CONFIG_PATH, &val);
	if (error == 0)
		*path = val;

	error = nvlist_lookup_string(nvroot, ZPOOL_CONFIG_DEVID, &val);
	if (error == 0)
		*devid = val;

	return (0);
}

/*
 * Given a disk label fetch the ZPOOL_CONFIG_PATH and ZPOOL_CONFIG_DEVID
 * and store these strings as config_path and devid_path respectively.
 * The returned pointers are only valid as long as label remains valid.
 */
static int
label_paths(libpc_handle_t *hdl, nvlist_t *label, char **path, char **devid)
{
	nvlist_t *nvroot;
	uint64_t pool_guid;
	uint64_t vdev_guid;

	*path = NULL;
	*devid = NULL;

	if (nvlist_lookup_nvlist(label, ZPOOL_CONFIG_VDEV_TREE, &nvroot) ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_GUID, &pool_guid) ||
	    nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID, &vdev_guid))
		return (ENOENT);

	return (label_paths_impl(hdl, nvroot, pool_guid, vdev_guid, path,
	    devid));
}

static void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	libpc_handle_t *hdl = rn->rn_hdl;
	struct stat64 statbuf;
	nvlist_t *config;
	char *bname, *dupname;
	uint64_t vdev_guid = 0;
	int error;
	int num_labels = 0;
	int fd;

	/*
	 * Skip devices with well known prefixes there can be side effects
	 * when opening devices which need to be avoided.
	 *
	 * hpet     - High Precision Event Timer
	 * watchdog - Watchdog must be closed in a special way.
	 */
	dupname = zutil_strdup(hdl, rn->rn_name);
	bname = basename(dupname);
	error = ((strcmp(bname, "hpet") == 0) || is_watchdog_dev(bname));
	free(dupname);
	if (error)
		return;

	/*
	 * Ignore failed stats.  We only want regular files and block devices.
	 */
	if (stat64(rn->rn_name, &statbuf) != 0 ||
	    (!S_ISREG(statbuf.st_mode) && !S_ISBLK(statbuf.st_mode)))
		return;

	/*
	 * Preferentially open using O_DIRECT to bypass the block device
	 * cache which may be stale for multipath devices.  An EINVAL errno
	 * indicates O_DIRECT is unsupported so fallback to just O_RDONLY.
	 */
	fd = open(rn->rn_name, O_RDONLY | O_DIRECT);
	if ((fd < 0) && (errno == EINVAL))
		fd = open(rn->rn_name, O_RDONLY);
#ifdef notyet
	if ((fd < 0) && (errno == EACCES))
		hdl->lpc_open_access_error = B_TRUE;
#endif
	if (fd < 0)
		return;

	/*
	 * This file is too small to hold a zpool
	 */
	if (S_ISREG(statbuf.st_mode) && statbuf.st_size < SPA_MINDEVSIZE) {
		(void) close(fd);
		return;
	}

	error = zpool_read_label(fd, &config, &num_labels);
	if (error != 0) {
		(void) close(fd);
		return;
	}

	if (num_labels == 0) {
		(void) close(fd);
		nvlist_free(config);
		return;
	}

	/*
	 * Check that the vdev is for the expected guid.  Additional entries
	 * are speculatively added based on the paths stored in the labels.
	 * Entries with valid paths but incorrect guids must be removed.
	 */
	error = nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid);
	if (error || (rn->rn_vdev_guid && rn->rn_vdev_guid != vdev_guid)) {
		(void) close(fd);
		nvlist_free(config);
		return;
	}

	(void) close(fd);

	rn->rn_config = config;
	rn->rn_num_labels = num_labels;

	/*
	 * Add additional entries for paths described by this label.
	 */
	if (rn->rn_labelpaths) {
		char *path = NULL;
		char *devid = NULL;
		rdsk_node_t *slice;
		avl_index_t where;
		int error;

		if (label_paths(rn->rn_hdl, rn->rn_config, &path, &devid))
			return;

		/*
		 * Allow devlinks to stabilize so all paths are available.
		 */
		zpool_label_disk_wait(rn->rn_name, DISK_LABEL_WAIT);

		if (path != NULL) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
			slice->rn_name = zutil_strdup(hdl, path);
			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_1;
			slice->rn_labelpaths = B_FALSE;
			pthread_mutex_lock(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
			pthread_mutex_unlock(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				pthread_mutex_unlock(rn->rn_lock);
				zpool_open_func(slice);
			}
		}

		if (devid != NULL) {
			slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
			error = asprintf(&slice->rn_name, "%s%s",
			    DEV_BYID_PATH, devid);
			if (error == -1) {
				free(slice);
				return;
			}

			slice->rn_vdev_guid = vdev_guid;
			slice->rn_avl = rn->rn_avl;
			slice->rn_hdl = hdl;
			slice->rn_order = IMPORT_ORDER_PREFERRED_2;
			slice->rn_labelpaths = B_FALSE;
			pthread_mutex_lock(rn->rn_lock);
			if (avl_find(rn->rn_avl, slice, &where)) {
				pthread_mutex_unlock(rn->rn_lock);
				free(slice->rn_name);
				free(slice);
			} else {
				avl_insert(rn->rn_avl, slice, where);
				pthread_mutex_unlock(rn->rn_lock);
				zpool_open_func(slice);
			}
		}
	}
}

static void
zpool_find_import_scan_add_slice(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t *cache, const char *path, const char *name, int order)
{
	avl_index_t where;
	rdsk_node_t *slice;

	slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
	if (asprintf(&slice->rn_name, "%s/%s", path, name) == -1) {
		free(slice);
		return;
	}
	slice->rn_vdev_guid = 0;
	slice->rn_lock = lock;
	slice->rn_avl = cache;
	slice->rn_hdl = hdl;
	slice->rn_order = order + IMPORT_ORDER_SCAN_OFFSET;
	slice->rn_labelpaths = B_FALSE;

	pthread_mutex_lock(lock);
	if (avl_find(cache, slice, &where)) {
		free(slice->rn_name);
		free(slice);
	} else {
		avl_insert(cache, slice, where);
	}
	pthread_mutex_unlock(lock);
}

static int
zpool_find_import_scan_dir(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t *cache, const char *dir, int order)
{
	int error;
	char path[MAXPATHLEN];
	struct dirent64 *dp;
	DIR *dirp;

	if (realpath(dir, path) == NULL) {
		error = errno;
		if (error == ENOENT)
			return (0);

		zutil_error_aux(hdl, strerror(error));
		(void) zutil_error_fmt(hdl, EZFS_BADPATH, dgettext(
		    TEXT_DOMAIN, "cannot resolve path '%s'"), dir);
		return (error);
	}

	dirp = opendir(path);
	if (dirp == NULL) {
		error = errno;
		zutil_error_aux(hdl, strerror(error));
		(void) zutil_error_fmt(hdl, EZFS_BADPATH,
		    dgettext(TEXT_DOMAIN, "cannot open '%s'"), path);
		return (error);
	}

	while ((dp = readdir64(dirp)) != NULL) {
		const char *name = dp->d_name;
		if (name[0] == '.' &&
		    (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;

		zpool_find_import_scan_add_slice(hdl, lock, cache, path, name,
		    order);
	}

	(void) closedir(dirp);
	return (0);
}

static int
zpool_find_import_scan_path(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t *cache, const char *dir, int order)
{
	int error = 0;
	char path[MAXPATHLEN];
	char *d, *b;
	char *dpath, *name;

	/*
	 * Separate the directory part and last part of the
	 * path. We do this so that we can get the realpath of
	 * the directory. We don't get the realpath on the
	 * whole path because if it's a symlink, we want the
	 * path of the symlink not where it points to.
	 */
	d = zutil_strdup(hdl, dir);
	b = zutil_strdup(hdl, dir);
	dpath = dirname(d);
	name = basename(b);

	if (realpath(dpath, path) == NULL) {
		error = errno;
		if (error == ENOENT) {
			error = 0;
			goto out;
		}

		zutil_error_aux(hdl, strerror(error));
		(void) zutil_error_fmt(hdl, EZFS_BADPATH, dgettext(
		    TEXT_DOMAIN, "cannot resolve path '%s'"), dir);
		goto out;
	}

	zpool_find_import_scan_add_slice(hdl, lock, cache, path, name, order);

out:
	free(b);
	free(d);
	return (error);
}

/*
 * Scan a list of directories for zfs devices.
 */
static int
zpool_find_import_scan(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache, char **dir, int dirs)
{
	avl_tree_t *cache;
	rdsk_node_t *slice;
	void *cookie;
	int i, error;

	*slice_cache = NULL;
	cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	for (i = 0; i < dirs; i++) {
		struct stat sbuf;

		if (stat(dir[i], &sbuf) != 0) {
			error = errno;
			if (error == ENOENT)
				continue;

			zutil_error_aux(hdl, strerror(error));
			(void) zutil_error_fmt(hdl, EZFS_BADPATH, dgettext(
			    TEXT_DOMAIN, "cannot resolve path '%s'"), dir[i]);
			goto error;
		}

		/*
		 * If dir[i] is a directory, we walk through it and add all
		 * the entry to the cache. If it's not a directory, we just
		 * add it to the cache.
		 */
		if (S_ISDIR(sbuf.st_mode)) {
			if ((error = zpool_find_import_scan_dir(hdl, lock,
			    cache, dir[i], i)) != 0)
				goto error;
		} else {
			if ((error = zpool_find_import_scan_path(hdl, lock,
			    cache, dir[i], i)) != 0)
				goto error;
		}
	}

	*slice_cache = cache;
	return (0);

error:
	cookie = NULL;
	while ((slice = avl_destroy_nodes(cache, &cookie)) != NULL) {
		free(slice->rn_name);
		free(slice);
	}
	free(cache);

	return (error);
}

static char *
zpool_default_import_path[DEFAULT_IMPORT_PATH_SIZE] = {
	"/dev/disk/by-vdev",	/* Custom rules, use first if they exist */
	"/dev/mapper",		/* Use multipath devices before components */
	"/dev/disk/by-partlabel", /* Single unique entry set by user */
	"/dev/disk/by-partuuid", /* Generated partition uuid */
	"/dev/disk/by-label",	/* Custom persistent labels */
	"/dev/disk/by-uuid",	/* Single unique entry and persistent */
	"/dev/disk/by-id",	/* May be multiple entries and persistent */
	"/dev/disk/by-path",	/* Encodes physical location and persistent */
	"/dev"			/* UNSAFE device names will change */
};

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = DEFAULT_IMPORT_PATH_SIZE;
	return ((const char * const *)zpool_default_import_path);
}

/*
 * Given a full path to a device determine if that device appears in the
 * import search path.  If it does return the first match and store the
 * index in the passed 'order' variable, otherwise return an error.
 */
static int
zfs_path_order(char *name, int *order)
{
	int i = 0, error = ENOENT;
	char *dir, *env, *envdup;

	env = getenv("ZPOOL_IMPORT_PATH");
	if (env) {
		envdup = strdup(env);
		dir = strtok(envdup, ":");
		while (dir) {
			if (strncmp(name, dir, strlen(dir)) == 0) {
				*order = i;
				error = 0;
				break;
			}
			dir = strtok(NULL, ":");
			i++;
		}
		free(envdup);
	} else {
		for (i = 0; i < DEFAULT_IMPORT_PATH_SIZE; i++) {
			if (strncmp(name, zpool_default_import_path[i],
			    strlen(zpool_default_import_path[i])) == 0) {
				*order = i;
				error = 0;
				break;
			}
		}
	}

	return (error);
}

/*
 * Use libblkid to quickly enumerate all known zfs devices.
 */
static int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	rdsk_node_t *slice;
	blkid_cache cache;
	blkid_dev_iterate iter;
	blkid_dev dev;
	avl_index_t where;
	int error;

	*slice_cache = NULL;

	error = blkid_get_cache(&cache, NULL);
	if (error != 0)
		return (error);

	error = blkid_probe_all_new(cache);
	if (error != 0) {
		blkid_put_cache(cache);
		return (error);
	}

	iter = blkid_dev_iterate_begin(cache);
	if (iter == NULL) {
		blkid_put_cache(cache);
		return (EINVAL);
	}

	error = blkid_dev_set_search(iter, "TYPE", "zfs_member");
	if (error != 0) {
		blkid_dev_iterate_end(iter);
		blkid_put_cache(cache);
		return (error);
	}

	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	while (blkid_dev_next(iter, &dev) == 0) {
		slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
		slice->rn_name = zutil_strdup(hdl, blkid_dev_devname(dev));
		slice->rn_vdev_guid = 0;
		slice->rn_lock = lock;
		slice->rn_avl = *slice_cache;
		slice->rn_hdl = hdl;
		slice->rn_labelpaths = B_TRUE;

		error = zfs_path_order(slice->rn_name, &slice->rn_order);
		if (error == 0)
			slice->rn_order += IMPORT_ORDER_SCAN_OFFSET;
		else
			slice->rn_order = IMPORT_ORDER_DEFAULT;

		pthread_mutex_lock(lock);
		if (avl_find(*slice_cache, slice, &where)) {
			free(slice->rn_name);
			free(slice);
		} else {
			avl_insert(*slice_cache, slice, where);
		}
		pthread_mutex_unlock(lock);
	}

	blkid_dev_iterate_end(iter);
	blkid_put_cache(cache);

	return (0);
}

/*
 * Given a list of directories to search, find all pools stored on disk.  This
 * includes partial pools which are not available to import.  If no args are
 * given (argc is 0), then the default directory (/dev/dsk) is searched.
 * poolname or guid (but not both) are provided by the caller when trying
 * to import a specific pool.
 */
nvlist_t *
zpool_find_import_impl(libpc_handle_t *hdl, importargs_t *iarg)
{
	nvlist_t *ret = NULL;
	pool_list_t pools = { 0 };
	pool_entry_t *pe, *penext;
	vdev_entry_t *ve, *venext;
	config_entry_t *ce, *cenext;
	name_entry_t *ne, *nenext;
	pthread_mutex_t lock;
	avl_tree_t *cache;
	rdsk_node_t *slice;
	void *cookie;
	tpool_t *t;

	verify(iarg->poolname == NULL || iarg->guid == 0);
	pthread_mutex_init(&lock, NULL);

	/*
	 * Locate pool member vdevs using libblkid or by directory scanning.
	 * On success a newly allocated AVL tree which is populated with an
	 * entry for each discovered vdev will be returned as the cache.
	 * It's the callers responsibility to consume and destroy this tree.
	 */
	if (iarg->scan || iarg->paths != 0) {
		int dirs = iarg->paths;
		char **dir = iarg->path;

		if (dirs == 0) {
			dir = zpool_default_import_path;
			dirs = DEFAULT_IMPORT_PATH_SIZE;
		}

		if (zpool_find_import_scan(hdl, &lock, &cache, dir,  dirs) != 0)
			return (NULL);
	} else {
		if (zpool_find_import_blkid(hdl, &lock, &cache) != 0)
			return (NULL);
	}

	/*
	 * Create a thread pool to parallelize the process of reading and
	 * validating labels, a large number of threads can be used due to
	 * minimal contention.
	 */
	t = tpool_create(1, 2 * sysconf(_SC_NPROCESSORS_ONLN), 0, NULL);
	for (slice = avl_first(cache); slice;
	    (slice = avl_walk(cache, slice, AVL_AFTER)))
		(void) tpool_dispatch(t, zpool_open_func, slice);

	tpool_wait(t);
	tpool_destroy(t);

	/*
	 * Process the cache, filtering out any entries which are not
	 * for the specified pool then adding matching label configs.
	 */
	cookie = NULL;
	while ((slice = avl_destroy_nodes(cache, &cookie)) != NULL) {
		if (slice->rn_config != NULL) {
			nvlist_t *config = slice->rn_config;
			boolean_t matched = B_TRUE;
			boolean_t aux = B_FALSE;
			int fd;

			/*
			 * Check if it's a spare or l2cache device. If it is,
			 * we need to skip the name and guid check since they
			 * don't exist on aux device label.
			 */
			if (iarg->poolname != NULL || iarg->guid != 0) {
				uint64_t state;
				aux = nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_POOL_STATE, &state) == 0 &&
				    (state == POOL_STATE_SPARE ||
				    state == POOL_STATE_L2CACHE);
			}

			if (iarg->poolname != NULL && !aux) {
				char *pname;

				matched = nvlist_lookup_string(config,
				    ZPOOL_CONFIG_POOL_NAME, &pname) == 0 &&
				    strcmp(iarg->poolname, pname) == 0;
			} else if (iarg->guid != 0 && !aux) {
				uint64_t this_guid;

				matched = nvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_POOL_GUID, &this_guid) == 0 &&
				    iarg->guid == this_guid;
			}
			if (matched) {
				/*
				 * Verify all remaining entries can be opened
				 * exclusively. This will prune all underlying
				 * multipath devices which otherwise could
				 * result in the vdev appearing as UNAVAIL.
				 *
				 * Under zdb, this step isn't required and
				 * would prevent a zdb -e of active pools with
				 * no cachefile.
				 */
				fd = open(slice->rn_name, O_RDONLY | O_EXCL);
				if (fd >= 0 || iarg->can_be_active) {
					if (fd >= 0)
						close(fd);
					add_config(hdl, &pools,
					    slice->rn_name, slice->rn_order,
					    slice->rn_num_labels, config);
				}
			}
			nvlist_free(config);
		}
		free(slice->rn_name);
		free(slice);
	}
	avl_destroy(cache);
	free(cache);
	pthread_mutex_destroy(&lock);

	ret = get_configs(hdl, &pools, iarg->can_be_active, iarg->policy);

	for (pe = pools.pools; pe != NULL; pe = penext) {
		penext = pe->pe_next;
		for (ve = pe->pe_vdevs; ve != NULL; ve = venext) {
			venext = ve->ve_next;
			for (ce = ve->ve_configs; ce != NULL; ce = cenext) {
				cenext = ce->ce_next;
				nvlist_free(ce->ce_config);
				free(ce);
			}
			free(ve);
		}
		free(pe);
	}

	for (ne = pools.names; ne != NULL; ne = nenext) {
		nenext = ne->ne_next;
		free(ne->ne_name);
		free(ne);
	}

	return (ret);
}
