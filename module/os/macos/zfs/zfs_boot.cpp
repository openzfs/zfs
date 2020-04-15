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
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 */
/*
 * ZFS boot utils
 *
 * While loading the kext, check if early boot and zfs-boot
 * kernel flag.
 * Allocate pool_list (and lock).
 * Register matching notification zfs_boot_probe_disk to check
 * IOMediaBSDClient devices as they are published (or matched?),
 * passing pool_list (automatically calls handler for all
 * existing devices).
 * Dispatch zfs_boot_import_thread on zfs_boot_taskq.
 *
 * In notification handler zfs_boot_probe_disk:
 * Check provider IOMedia for:
 *  1 Leaf node and whole disk.
 *  2 Leaf node and type ZFS.
 *  3 Leaf node and type FreeBSD-ZFS.
 * Check IOMedia meets minimum size or bail.
 * Allocate char* buffer.
 * Call vdev_disk_read_rootlabel.
 * XXX Alternately:
 * Alloc and prep IOMemoryDescriptor.
 * Open IOMedia device (read-only).
 * Try to read vdev label from device.
 * Close IOMedia device.
 * Release IOMemoryDescriptor (data is in buffer).
 * XXX
 * If label was read, try to generate a config from label.
 * Check pool name matches zfs-boot or bail.
 * Check pool status.
 * Update this vdev's path and set status.
 * Set other vdevs to missing status.
 * Check-in config in thread-safe manner:
 * Take pool_list lock.
 * If config not found, insert new config, or update existing.
 * Unlock pool_list.
 * If found config is complete, wake import thread.
 *
 * In vdev_disk_read_rootlabel:
 * Use vdev_disk_physio to read label.
 * If label was read, try to unpack.
 * Return label or failure.
 *
 * In vdev_disk_physio:
 * Open device (read-only) using vnop/VOP.
 * Try to read vdev label from device.
 * Close device using vnop/VOP.
 *
 * In zfs_boot_import_thread:
 * Loop checking for work and sleeping on lock between loops.
 * Take pool_list lock and check for work.
 * Attempt to import root pool using spa_import_rootpool.
 * If successful, remove notification handler (waits for
 * all tasks).
 * Empty and deallocate pool_list (and lock).
 */

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/IOMemoryDescriptor.h>

extern "C" {
#include <sys/taskq.h>
#include <sys/param.h>
#include <sys/uuid.h>
#include <sys/nvpair.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_disk_os.h>
#include <sys/spa_impl.h>
#include <sys/spa.h>

#include <sys/zfs_context.h>
#include <sys/mount.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_rlock.h>
#include <sys/dataset_kstats.h>
#include <sys/zvol_impl.h>

int dsl_dsobj_to_dsname(char *pname, uint64_t obj, char *buf);

} /* extern "C" */

static taskq_t *zfs_boot_taskq;

#include <sys/zvolIO.h>
#include <sys/ZFSPool.h>
#include <sys/ZFSDatasetScheme.h>
#include <sys/zfs_boot.h>

#if defined(DEBUG) || defined(ZFS_DEBUG)
#define	DSTATIC
#else
#define	DSTATIC	static
#endif

#ifndef verify
#define	verify(EX) (void)((EX) || \
	(printf("%s, %s, %d, %s\n", #EX, __FILE__, __LINE__, __func__), 0))
#endif  /* verify */

/* Most of this is only built when configured with --enable-boot */

/* block size is 512 B, count is 512 M blocks */
#define	ZFS_BOOT_DEV_BSIZE	(UInt64)(1<<9)
#define	ZFS_BOOT_DEV_BCOUNT	(UInt64)(2<<29)
#define	ZFS_BOOT_DATASET_NAME_KEY	"zfs_dataset_name"
#define	ZFS_BOOT_DATASET_UUID_KEY	"zfs_dataset_uuid"
#define	ZFS_BOOT_DATASET_RDONLY_KEY	"zfs_dataset_rdonly"
#define	ZFS_MOUNTROOT_RETRIES	50
#define	ZFS_BOOTLOG_DELAY	100

/*
 * C functions for boot-time vdev discovery
 */

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
	uint64_t		complete;
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
	uint64_t		pool_guid;
	char			*pool_name;
	OSSet			*new_disks;
	OSSet			*disks;
	kmutex_t		lock;
	kcondvar_t		cv;
	IOService		*zfs_hl;
	IONotifier		*notifier;
	_Atomic UInt64		terminating;
} pool_list_t;

#define	ZFS_BOOT_ACTIVE		0x1
#define	ZFS_BOOT_TERMINATING	0x2
#define	ZFS_BOOT_INVALID	0x99

#define	ZFS_BOOT_PREALLOC_SET	5

#if 0
static ZFSBootDevice *bootdev = 0;
#endif
static pool_list_t *zfs_boot_pool_list = 0;

DSTATIC char *
zfs_boot_get_devid(const char *path)
{
	/*
	 * XXX Unavailable interface
	 *
	 * If we implement one in spl, it could
	 * simplify import when device paths
	 * have changed (e.g. USB pools).
	 *
	 * Could use ldi DeviceTree path, or
	 * IOService path if not in DTPlane.
	 */
	return (NULL);
}

/*
 * Go through and fix up any path and/or devid information for the given vdev
 * configuration.
 *
 * Copied from libzfs_import.c
 */
DSTATIC int
zfs_boot_fix_paths(nvlist_t *nv, name_entry_t *names)
{
	nvlist_t **child;
	uint_t c, children;
	uint64_t guid;
	name_entry_t *ne, *best;
	char *path, *devid;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (zfs_boot_fix_paths(child[c], names) != 0)
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

			/* Prefer paths with more vdev labels. */
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

	if ((devid = zfs_boot_get_devid(best->ne_name)) == NULL) {
		(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
	} else {
		if (nvlist_add_string(nv, ZPOOL_CONFIG_DEVID, devid) != 0) {
			spa_strfree(devid);
			return (-1);
		}
		spa_strfree(devid);
	}

	return (0);
}

/*
 * Add the given configuration to the list of known devices.
 *
 * Copied from libzfs_import.c
 * diffs: kmem_alloc, kmem_free with size
 */
DSTATIC int
zfs_boot_add_config(pool_list_t *pl, const char *path,
    int order, int num_labels, nvlist_t *config)
{
	uint64_t pool_guid, vdev_guid, top_guid, txg, state;
	pool_entry_t *pe;
	vdev_entry_t *ve;
	config_entry_t *ce;
	name_entry_t *ne;

	dprintf("%s %p [%s] %d %d %p\n", __func__,
	    pl, path, order, num_labels, config);

	/*
	 * If this is a hot spare not currently in use or level 2 cache
	 * device, add it to the list of names to translate, but don't do
	 * anything else.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &state) == 0 &&
	    (state == POOL_STATE_SPARE || state == POOL_STATE_L2CACHE) &&
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid) == 0) {
		if ((ne = (name_entry_t *)kmem_alloc(
		    sizeof (name_entry_t), KM_SLEEP)) == NULL) {
			return (-1);
		}
		memset(ne, 0, sizeof (name_entry_t));

		if ((ne->ne_name = spa_strdup(path)) == NULL) {
			kmem_free(ne, sizeof (name_entry_t));
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
		nvlist_free(config);
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
		if ((pe = (pool_entry_t *)kmem_alloc(
		    sizeof (pool_entry_t), KM_SLEEP)) == NULL) {
			nvlist_free(config);
			return (-1);
		}
		memset(pe, 0, sizeof (pool_entry_t));
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
		if ((ve = (vdev_entry_t *)kmem_alloc(
		    sizeof (vdev_entry_t), KM_SLEEP)) == NULL) {
			nvlist_free(config);
			return (-1);
		}
		memset(ve, 0, sizeof (vdev_entry_t));
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
		if ((ce = (config_entry_t *)kmem_alloc(
		    sizeof (config_entry_t), KM_SLEEP)) == NULL) {
			nvlist_free(config);
			return (-1);
		}
		memset(ce, 0, sizeof (config_entry_t));
		ce->ce_txg = txg;
		ce->ce_config = config;
		ce->ce_next = ve->ve_configs;
		ve->ve_configs = ce;
	} else {
		nvlist_free(config);
	}

	/*
	 * At this point we've successfully added our config to the list of
	 * known configs.  The last thing to do is add the vdev guid -> path
	 * mappings so that we can fix up the configuration as necessary before
	 * doing the import.
	 */
	if ((ne = (name_entry_t *)kmem_alloc(
	    sizeof (name_entry_t), KM_SLEEP)) == NULL) {
		return (-1);
	}
	memset(ne, 0, sizeof (name_entry_t));

	if ((ne->ne_name = spa_strdup(path)) == NULL) {
		kmem_free(ne, sizeof (name_entry_t));
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
 * libzfs_import used the libzfs handle and a zfs
 * command to issue tryimport in-kernel via ioctl.
 * This should leave config as-is, and return nvl.
 * Since zfs_boot is already in-kernel, duplicate
 * config into nvl, and call spa_tryimport on it.
 */
DSTATIC nvlist_t *
zfs_boot_refresh_config(nvlist_t *config)
{
	nvlist_t *nvl = 0;

	/* tryimport does not free config, and returns new nvl or null */
	nvl = spa_tryimport(config);
	return (nvl);
}

/*
 * Determine if the vdev id is a hole in the namespace.
 */
DSTATIC boolean_t
zfs_boot_vdev_is_hole(uint64_t *hole_array, uint_t holes, uint_t id)
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
DSTATIC nvlist_t *
zfs_boot_get_configs(pool_list_t *pl, boolean_t active_ok)
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
#if 0
	boolean_t isactive;
#endif
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
				 *	pool txg (if available)
				 *	comment (if available)
				 *	pool state
				 *	hostid (if available)
				 *	hostname (if available)
				 */
				uint64_t state, version, pool_txg;
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
				if (nvlist_lookup_uint64(tmp,
				    ZPOOL_CONFIG_POOL_TXG, &pool_txg) == 0)
					fnvlist_add_uint64(config,
					    ZPOOL_CONFIG_POOL_TXG, pool_txg);

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

				newchild = (nvlist_t **)kmem_alloc((id + 1) *
				    sizeof (nvlist_t *), KM_SLEEP);
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				kmem_free(child, children *
				    sizeof (nvlist_t *));
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

				newchild = (nvlist_t **)kmem_alloc((max_id) *
				    sizeof (nvlist_t *), KM_SLEEP);
				if (newchild == NULL)
					goto nomem;

				for (c = 0; c < children; c++)
					newchild[c] = child[c];

				kmem_free(child, children *
				    sizeof (nvlist_t *));
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
				    !zfs_boot_vdev_is_hole(hole_array, holes,
				    c))
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
		kmem_free(child, children * sizeof (nvlist_t *));
		children = 0;
		child = NULL;

		/*
		 * Go through and fix up any paths and/or devids based on our
		 * known list of vdev GUID -> path mappings.
		 */
		if (zfs_boot_fix_paths(nvroot, pl->names) != 0) {
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

#if 0
/*
 * For root-pool import, no pools are active yet.
 * Pool name and guid were looked up from the config and only used here.
 * (Later we lookup the pool name for a separate test).
 */
		/*
		 * Determine if this pool is currently active, in which case we
		 * can't actually import it.
		 */
		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &name) == 0);
		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &guid) == 0);

		if (zfs_boot_pool_active(name, guid, &isactive) != 0)
			goto error;

		if (isactive) {
			nvlist_free(config);
			config = NULL;
			continue;
		}
#endif

		if ((nvl = zfs_boot_refresh_config(config)) == NULL) {
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
				if (zfs_boot_fix_paths(spares[i], pl->names) !=
				    0)
					goto nomem;
			}
		}

		/*
		 * Update the paths for l2cache devices.
		 */
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0) {
			for (i = 0; i < nl2cache; i++) {
				if (zfs_boot_fix_paths(l2cache[i], pl->names) !=
				    0)
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
#ifdef DEBUG
	printf("zfs_boot_get_configs failed to allocate memory\n");
#endif
	if (config) nvlist_free(config);
	if (ret) nvlist_free(ret);
	for (c = 0; c < children; c++)
		nvlist_free(child[c]);
	if (children > 0) {
		kmem_free(child, children * sizeof (nvlist_t *));
	}
	/*
	 * libzfs_import simply calls free(child), we need to
	 * pass kmem_free the size of the array. Array is
	 * allocated above as (children * sizeof nvlist_t*).
	 */

	return (NULL);
}

/*
 * Return the offset of the given label.
 */
DSTATIC uint64_t
zfs_boot_label_offset(uint64_t size, int l)
{
	ASSERT(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t) == 0);
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

/*
 * Given an IOMedia, read the label information and return an nvlist
 * describing the configuration, if there is one.  The number of valid
 * labels found will be returned in num_labels when non-NULL.
 */
DSTATIC int
zfs_boot_read_label(IOService *zfs_hl, IOMedia *media,
    nvlist_t **config, int *num_labels)
{
	IOMemoryDescriptor *buffer = NULL;
	uint64_t mediaSize;
	uint64_t nread = 0;
	vdev_label_t *label;
	nvlist_t *expected_config = NULL;
	uint64_t expected_guid = 0, size, labelsize;
	int l, count = 0;
	IOReturn ret;

	*config = NULL;

	/* Verify IOMedia pointer and device size */
	if (!media || (mediaSize = media->getSize()) == 0) {
		dprintf("%s couldn't get media or size\n", __func__);
		return (-1);
	}

	/* Determine vdev label size and aligned vdev size */
	labelsize = sizeof (vdev_label_t);
	size = P2ALIGN_TYPED(mediaSize, labelsize, uint64_t);

	/* Allocate a buffer to read labels into */
	label = (vdev_label_t *)kmem_alloc(labelsize, KM_SLEEP);
	if (!label) {
		dprintf("%s couldn't allocate label for read\n", __func__);
		return (-1);
	}

	/* Allocate a memory descriptor with the label pointer */
	buffer = IOMemoryDescriptor::withAddress((void*)label, labelsize,
	    kIODirectionIn);

	/* Verify buffer was allocated */
	if (!buffer || (buffer->getLength() != labelsize)) {
		dprintf("%s couldn't allocate buffer for read\n", __func__);
		goto error;
	}

	/* Open the device for reads */
	if (false == media->IOMedia::open(zfs_hl, 0,
	    kIOStorageAccessReader)) {
		dprintf("%s media open failed\n", __func__);
		goto error;
	}

	/* Read all four vdev labels */
	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t state, guid, txg;

		/* Zero the label buffer */
		memset(label, 0, labelsize);

		/* Prepare the buffer for IO */
		buffer->prepare(kIODirectionIn);

		/* Read a label from the specified offset */
		ret = media->IOMedia::read(zfs_hl,
		    zfs_boot_label_offset(size, l),
		    buffer, 0, &nread);

		/* Call the buffer completion */
		buffer->complete();

		/* Skip failed reads, try next label */
		if (ret != kIOReturnSuccess) {
			dprintf("%s media->read failed\n", __func__);
			continue;
		}

		/* Skip incomplete reads, try next label */
		if (nread < labelsize) {
			dprintf("%s nread %llu / %llu\n",
			    __func__, nread, labelsize);
			continue;
		}

		/* Skip invalid labels that can't be unpacked */
		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), config, 0) != 0)
			continue;

		/* Verify GUID */
		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid == 0) {
			dprintf("%s nvlist_lookup guid failed %llu\n",
			    __func__, guid);
			nvlist_free(*config);
			continue;
		}

		/* Verify vdev state */
		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			dprintf("%s nvlist_lookup state failed %llu\n",
			    __func__, state);
			nvlist_free(*config);
			continue;
		}

		/* Verify txg number */
		if (state != POOL_STATE_SPARE && state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			dprintf("%s nvlist_lookup txg failed %llu\n",
			    __func__, txg);
			nvlist_free(*config);
			continue;
		}

		/* Increment count for first match, or if guid matches */
		if (expected_guid) {
			if (expected_guid == guid)
				count++;

			nvlist_free(*config);
		} else {
			expected_config = *config;
			expected_guid = guid;
			count++;
		}
	}

	/* Close IOMedia */
	media->close(zfs_hl);

	/* Copy out the config and number of labels */
	if (num_labels != NULL)
		*num_labels = count;

	kmem_free(label, labelsize);
	buffer->release();
	*config = expected_config;

	return (0);


error:
	/* Clean up */
	if (buffer) {
		buffer->release();
		buffer = 0;
	}
	if (label) {
		kmem_free(label, labelsize);
		label = 0;
	}

	return (-1);
}

DSTATIC bool
zfs_boot_probe_media(void* target, void* refCon,
    IOService* newService, __unused IONotifier* notifier)
{
	IOMedia *media = 0;
	OSObject *isLeaf = 0;
	OSString *ospath = 0;
	uint64_t mediaSize = 0;
	pool_list_t *pools = (pool_list_t *)refCon;

	/* Verify pool list can be cast */
	if (!pools) {
		dprintf("%s invalid refCon\n", __func__);
		return (false);
	}
	/* Should never happen */
	if (!newService) {
		printf("%s %s\n", "zfs_boot_probe_media",
		    "called with null newService");
		return (false);
	}

	/* Abort early */
	if (pools->terminating != ZFS_BOOT_ACTIVE) {
		dprintf("%s terminating 1\n", __func__);
		return (false);
	}

	/* Validate pool name */
	if (!pools->pool_name || strlen(pools->pool_name) == 0) {
		dprintf("%s no pool name specified\n", __func__);
		return (false);
	}

	/* Get the parent IOMedia device */
	media = OSDynamicCast(IOMedia, newService->getProvider());

	if (!media) {
		dprintf("%s couldn't be cast as IOMedia\n",
		    __func__);
		return (false);
	}

	isLeaf = media->getProperty(kIOMediaLeafKey);
	if (!isLeaf) {
		dprintf("%s skipping non-leaf\n", __func__);
		goto out;
	}

	mediaSize = media->getSize();
	if (mediaSize < SPA_MINDEVSIZE) {
		dprintf("%s skipping device with size %llu\n",
		    __func__, mediaSize);
		goto out;
	}

	ospath = OSDynamicCast(OSString, media->getProperty(
	    kIOBSDNameKey, gIOServicePlane,
	    kIORegistryIterateRecursively));
	if (!ospath || (ospath->getLength() == 0)) {
		dprintf("%s skipping device with no bsd disk node\n",
		    __func__);
		goto out;
	}

	/* Abort early */
	if (pools->terminating != ZFS_BOOT_ACTIVE) {
		dprintf("%s terminating 2\n", __func__);
		goto out;
	}

	/* Take pool_list lock */
	mutex_enter(&pools->lock);

	/* Abort early */
	if (pools->terminating != ZFS_BOOT_ACTIVE) {
		dprintf("%s terminating 3\n", __func__);
		/* Unlock the pool list lock */
		mutex_exit(&pools->lock);
		goto out;
	}

	/* Add this IOMedia to the disk set */
	pools->disks->setObject(media);

	/* Unlock the pool list lock */
	mutex_exit(&pools->lock);

	/* Wakeup zfs_boot_import_thread */
	cv_signal(&pools->cv);

out:
	media = 0;
	return (true);
}

DSTATIC bool
zfs_boot_probe_disk(pool_list_t *pools, IOMedia *media)
{
	OSString *ospath, *uuid;
	char *path = 0, *pname;
	const char prefix[] = "/private/var/run/disk/by-id/media-";
	uint64_t this_guid;
	int num_labels, err, len = 0;
	nvlist_t *config;
	boolean_t matched = B_FALSE;

	dprintf("%s: with %s media\n", __func__,
	    (media ? "valid" : "missing"));
	ASSERT3U(media, !=, NULL);

	/* Verify pool list can be cast */
	if (!pools) {
		dprintf("%s missing pool_list\n", __func__);
		return (false);
	}

	/* Abort early */
	if (pools->terminating != ZFS_BOOT_ACTIVE) {
		dprintf("%s terminating 1\n", __func__);
		return (false);
	}

	/* Validate pool name */
	if (!pools->pool_name || strlen(pools->pool_name) == 0) {
		dprintf("%s no pool name specified\n", __func__);
		return (false);
	}

	/* Try to get a UUID from the media */
	uuid = OSDynamicCast(OSString, media->getProperty(kIOMediaUUIDKey));
	if (uuid && uuid->getLength() != 0) {
		/* Allocate room for prefix, UUID, and null terminator */
		len = (strlen(prefix) + uuid->getLength()) + 1;

		path = (char *)kmem_alloc(len, KM_SLEEP);
		if (!path) {
			dprintf("%s couldn't allocate path\n", __func__);
			return (false);
		}

		snprintf(path, len, "%s%s", prefix, uuid->getCStringNoCopy());
		uuid = 0;
	} else {
		/* Get the BSD name as a C string */
		ospath = OSDynamicCast(OSString, media->getProperty(
		    kIOBSDNameKey, gIOServicePlane,
		    kIORegistryIterateRecursively));
		if (!ospath || (ospath->getLength() == 0)) {
			dprintf("%s skipping device with no bsd disk node\n",
			    __func__);
			return (false);
		}

		/* Allocate room for "/dev/" + "diskNsN" + '\0' */
		len = (strlen("/dev/") + ospath->getLength() + 1);
		path = (char *)kmem_alloc(len, KM_SLEEP);
		if (!path) {
			dprintf("%s couldn't allocate path\n", __func__);
			return (false);
		}

		/* "/dev/" is 5 characters, plus null character */
		snprintf(path, len, "/dev/%s", ospath->getCStringNoCopy());
		ospath = 0;
	}
	dprintf("%s path [%s]\n", __func__, (path ? path : ""));

	/* Read vdev labels, if any */
	err = zfs_boot_read_label(pools->zfs_hl, media,
	    &config, &num_labels);

	/* Skip disks with no labels */
	if (err != 0 || num_labels == 0 || !config) {
		goto out;
	}

	/* Lookup pool name */
	if (pools->pool_name != NULL &&
	    (nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &pname) == 0)) {
		/* Compare with pool_name */
		if (strlen(pools->pool_name) == strlen(pname) &&
		    strncmp(pools->pool_name, pname,
		    strlen(pname)) == 0) {
			printf("%s matched pool %s\n",
			    __func__, pname);
			matched = B_TRUE;
		}
	/* Compare with pool_guid */
	} else if (pools->pool_guid != 0) {
		matched = nvlist_lookup_uint64(config,
		    ZPOOL_CONFIG_POOL_GUID,
		    &this_guid) == 0 &&
		    pools->pool_guid == this_guid;
	}

	/* Skip non-matches */
	if (!matched) {
		nvlist_free(config);
		config = NULL;
		goto out;
	}

	/*
	 * Add this config to the pool list.
	 * Always assigns order 1 since all disks are
	 * referenced by /private/var/run/disk/by-id/ paths.
	 */
	dprintf("%s: add_config %s\n", __func__, path);
	if (zfs_boot_add_config(pools, path, 1,
	    num_labels, config) != 0) {
		printf("%s couldn't add config to pool list\n",
		    __func__);
	}

out:
	/* Clean up */
	if (path && len > 0) {
		kmem_free(path, len);
	}
	return (true);
}

DSTATIC void
zfs_boot_free()
{
	pool_entry_t *pe, *penext;
	vdev_entry_t *ve, *venext;
	config_entry_t *ce, *cenext;
	name_entry_t *ne, *nenext;
	pool_list_t *pools = zfs_boot_pool_list;

	/* Verify pool list can be cast */
	if (!pools) {
		dprintf("%s: no pool_list to clear\n", __func__);
		return;
	}

	/* Clear global ptr */
	zfs_boot_pool_list = 0;

	pools->terminating = ZFS_BOOT_TERMINATING;

	/* Remove IONotifier (waits for tasks to complete) */
	if (pools->notifier) {
		pools->notifier->remove();
		pools->notifier = 0;
	}

	/* Release the lock */
	mutex_destroy(&pools->lock);

	/* Release the disk set */
	if (pools->disks) {
		pools->disks->flushCollection();
		pools->disks->release();
		pools->disks = 0;
	}

	/* Clear the zfs IOService handle */
	if (pools->zfs_hl) {
		pools->zfs_hl = 0;
	}

	/* Free the pool_name string */
	if (pools->pool_name) {
		kmem_free(pools->pool_name, strlen(pools->pool_name) + 1);
		pools->pool_name = 0;
	}

	/* Clear the pool config list */
	for (pe = pools->pools; pe != NULL; pe = penext) {
		/* Clear the vdev list */
		penext = pe->pe_next;
		for (ve = pe->pe_vdevs; ve != NULL; ve = venext) {
			/* Clear the vdev config list */
			venext = ve->ve_next;
			for (ce = ve->ve_configs; ce != NULL; ce = cenext) {
				cenext = ce->ce_next;
				if (ce->ce_config)
					nvlist_free(ce->ce_config);
				kmem_free(ce, sizeof (config_entry_t));
			}
			kmem_free(ve, sizeof (vdev_entry_t));
		}
		kmem_free(pe, sizeof (pool_entry_t));
	}
	pools->pools = 0;

	/* Clear the vdev name list */
	for (ne = pools->names; ne != NULL; ne = nenext) {
		nenext = ne->ne_next;
		if (ne->ne_name)
			spa_strfree(ne->ne_name);
		kmem_free(ne, sizeof (name_entry_t));
	}
	pools->names = 0;

	/* Finally, free the pool list struct */
	kmem_free(pools, sizeof (pool_list_t));
	pools = 0;
}

void
zfs_boot_fini()
{
	pool_list_t *pools = zfs_boot_pool_list;

	if (!pools) {
		printf("%s no pool_list to clear\n", __func__);
		return;
	}

	/* Set terminating flag */
	if (false == OSCompareAndSwap64(ZFS_BOOT_ACTIVE,
	    ZFS_BOOT_TERMINATING, &(pools->terminating))) {
		printf("%s already terminating? %llu\n",
		    __func__, pools->terminating);
	}

	/* Wakeup zfs_boot_import_thread */
	cv_signal(&pools->cv);

	taskq_wait(zfs_boot_taskq);
	taskq_destroy(zfs_boot_taskq);

	/* Clean up */
	pools = 0;
}

#define	kBootUUIDKey		"boot-uuid"
#define	kBootUUIDMediaKey	"boot-uuid-media"

DSTATIC int
zfs_boot_publish_bootfs(IOService *zfs_hl, pool_list_t *pools)
{
	ZFSDataset *dataset = NULL;
	IOMedia *media;
	IOService *resourceService = NULL;
	OSDictionary *properties = NULL;
	spa_t *spa = NULL;
	char *zfs_bootfs = NULL;
	uint64_t bootfs = 0ULL;
	int error, len = ZFS_MAX_DATASET_NAME_LEN;

	dprintf("%s\n", __func__);
	if (!zfs_hl || !pools) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

#if 0
	ZFSPool *pool_proxy = NULL;
	if (bootdev) {
		dprintf("%s bootdev already set\n", __func__);
		return (EBUSY);
	}
#endif

	zfs_bootfs = (char *)kmem_alloc(len, KM_SLEEP);
	if (!zfs_bootfs) {
		printf("%s string alloc failed\n", __func__);
		return (ENOMEM);
	}
	zfs_bootfs[0] = '\0';

	mutex_enter(&spa_namespace_lock);
	spa = spa_next(NULL);
	if (spa) {
		bootfs = spa_bootfs(spa);
	}
	if (bootfs == 0) {
		mutex_exit(&spa_namespace_lock);
		dprintf("%s no bootfs, nothing to do\n", __func__);
		kmem_free(zfs_bootfs, len);
		return (0);
	}

#if 0
	/* Get pool proxy */
	if (!spa->spa_iokit_proxy ||
	    (pool_proxy = spa->spa_iokit_proxy->proxy) == NULL) {
		mutex_exit(&spa_namespace_lock);
		dprintf("%s no spa_pool_proxy\n", __func__);
		kmem_free(zfs_bootfs, len);
		return (0);
	}
#endif

	error = dsl_dsobj_to_dsname(spa_name(spa),
	    spa_bootfs(spa), zfs_bootfs);
	mutex_exit(&spa_namespace_lock);

	if (error != 0) {
		dprintf("%s bootfs to name failed\n", __func__);
		kmem_free(zfs_bootfs, len);
		return (ENODEV);
	}

	printf("%s: publishing bootfs [%s]\n", __func__, zfs_bootfs);

	/* Create prop dict for the proxy, with 6 or more keys */
	if ((properties = OSDictionary::withCapacity(6)) == NULL) {
		dprintf("%s prop dict allocation failed\n", __func__);
		kmem_free(zfs_bootfs, len);
		return (ENOMEM);
	}

	/* Set Content Hint and Content */
	do {
		const OSSymbol *partUUID;

		/* ZFS (BF01) partition type */
		if ((partUUID = OSSymbol::withCString(
		    "6A898CC3-1DD2-11B2-99A6-080020736631")) == NULL) {
			dprintf("%s couldn't make partUUID\n", __func__);
			break;
			// kmem_free(zfs_bootfs, len);
			// return (ENOMEM);
		}

		/* Assign ZFS partiton UUID to both */
		if (properties->setObject(kIOMediaContentKey,
		    partUUID) == false ||
		    properties->setObject(kIOMediaContentHintKey,
		    partUUID) == false) {
			dprintf("%s content hint failed\n", __func__);
			// kmem_free(zfs_bootfs, len);
			// return (ENOMEM);
		}
		partUUID->release();
	} while (0);

	/* XXX Set dataset name, rdonly, and UUID */
	do {
		OSString *nameStr;
		OSString *uuidStr;
		char uuid_cstr[UUID_PRINTABLE_STRING_LENGTH];
		uuid_t uuid;

		memset(uuid, 0, sizeof (uuid_t));
		memset(uuid_cstr, 0, UUID_PRINTABLE_STRING_LENGTH);

		zfs_vfs_uuid_gen(zfs_bootfs, uuid);
		zfs_vfs_uuid_unparse(uuid, uuid_cstr);

		nameStr = OSString::withCString(zfs_bootfs);
		uuidStr = OSString::withCString(uuid_cstr);

		if (properties->setObject(ZFS_BOOT_DATASET_NAME_KEY,
		    nameStr) == false ||
		    properties->setObject(ZFS_BOOT_DATASET_UUID_KEY,
		    uuidStr) == false ||
		    properties->setObject(ZFS_BOOT_DATASET_RDONLY_KEY,
		    kOSBooleanFalse) == false) {
			dprintf("ZFSBootDevice::%s couldn't setup"
			    "property dict\n", __func__);
			nameStr->release();
			uuidStr->release();
			kmem_free(zfs_bootfs, len);
			return (ENOMEM);
		}
		nameStr->release();
		uuidStr->release();
	} while (0);

	/* Create proxy device */
	error = zfs_osx_proxy_create(zfs_bootfs);
	if (error == 0) {
		dataset = zfs_osx_proxy_get(zfs_bootfs);
	}
	/* Done with this string */
	kmem_free(zfs_bootfs, len);
	zfs_bootfs = 0;

	if (!dataset) {
		printf("%s: couldn't create proxy device\n",
		    __func__);
		return (ENXIO);
	}

	media = OSDynamicCast(IOMedia, dataset);
	if (!media) {
		printf("%s: couldn't cast proxy media\n",
		    __func__);
		dataset->release();
		return (ENXIO);
	}

#if 0
	bootdev = new ZFSBootDevice;

	if (!bootdev) {
		printf("%s: couldn't create boot device\n", __func__);
		return (ENOMEM);
	}

	if (bootdev->init(properties) == false) {
		printf("%s init failed\n", __func__);
		properties->release();
		bootdev->release();
		bootdev = 0;
		return (ENXIO);
	}
	properties->release();
	properties = 0;

	if (bootdev->attach(pool_proxy) == false) {
		printf("%s attach failed\n", __func__);
		bootdev->release();
		bootdev = 0;
		return (ENXIO);
	}

	/* Technically should start but this doesn't do much */
	if (bootdev->start(pool_proxy) == false) {
		printf("%s start failed\n", __func__);
		bootdev->detach(pool_proxy);
		bootdev->release();
		bootdev = 0;
		return (ENXIO);
	}

	/* Get matching started */
	bootdev->registerService(kIOServiceAsynchronous);
	// bootdev->registerService(kIOServiceSynchronous);

	do {
		if (bootdev->getClient() != 0) {
			media = OSDynamicCast(IOMedia,
			    bootdev->getClient()->getClient());
			if (media) {
				media->retain();
				break;
			}
		}

		/* Sleep until media is available */
		/*
		 * XXX Should use waitForServiceMatching or IONotifier
		 */
		IOSleep(200);
	} while (!media);

	if (!media) {
		/* XXX currently unreachable */
		printf("%s couldn't get bootdev media\n", __func__);
		return (ENXIO);
	}
#endif

	resourceService = IOService::getResourceService();
	if (!resourceService) {
		dprintf("%s missing resource service\n", __func__);
		/* Handle error */
		media->release();
		return (ENXIO);
	}

#if 1
	/* XXX publish an IOMedia as the BootUUIDMedia resource */
	/* uses same method as AppleFileSystemDriver */

	/* Set IOMedia UUID */
	/* XXX skip (moved get uuid below) */
	// media->setProperty(kIOMediaUUIDKey, uuid);
	/* Publish this IOMedia as the boot-uuid-media */
	IOService::publishResource(kBootUUIDMediaKey, media);

	/* Drop retain from earlier */
	media->release();
	/* Remove boot-uuid key so AppleFileSystem stops matching */
	resourceService->removeProperty(kBootUUIDKey);
#else
	OSString *uuid = 0;
	/* Get the current boot-uuid string */
	uuid = OSDynamicCast(OSString,
	    resourceService->getProperty(kBootUUIDKey, gIOServicePlane));
	if (!uuid) {
		dprintf("%s missing boot-uuid IOResource\n", __func__);
		/* Handle error */
		return (ENXIO);
	}
	printf("%s: got boot-uuid %s\n", __func__, uuid->getCStringNoCopy());

	/* XXX Or use below and let AppleFileSystemDriver match it */
	/* Leaves the Apple_Boot content hint (at least for now) */
	media->setProperty(kIOMediaContentHintKey, "Apple_Boot");
	media->setProperty(kIOMediaUUIDKey, uuid);
	/* Register for notifications (not matching) */
	media->registerService(kIOServiceAsynchronous);
	/* Drop retain from earlier */
	media->release();
#endif

	printf("%s done\n", __func__);
	return (0);
}

DSTATIC void
zfs_boot_import_thread(void *arg)
{
	nvlist_t *configs, *nv, *newnv;
	nvpair_t *elem;
	IOService *zfs_hl = 0;
	OSSet *disks, *new_set = 0;
	OSCollectionIterator *iter = 0;
	OSObject *next;
	IOMedia *media;
	pool_list_t *pools = (pool_list_t *)arg;
	uint64_t pool_state;
	boolean_t pool_imported = B_FALSE;
	int error = EINVAL;

	/* Verify pool list coult be cast */
	ASSERT3U(pools, !=, 0);
	if (!pools) {
		printf("%s %p %s\n", "zfs_boot_import_thread",
		    arg, "couldn't be cast as pool_list_t*");
		return;
	}

	/* Abort early */
	if (pools->terminating != ZFS_BOOT_ACTIVE) {
		dprintf("%s terminating 1\n", __func__);
		goto out_unlocked;
	}

	new_set = OSSet::withCapacity(1);
	/* To swap with pools->disks while locked */
	if (!new_set) {
		dprintf("%s couldn't allocate new_set\n", __func__);
		goto out_unlocked;
	}

	/* Take pool list lock */
	mutex_enter(&pools->lock);

	zfs_hl = pools->zfs_hl;

	/* Check for work, then sleep on the lock */
	do {
		/* Abort early */
		if (pools->terminating != ZFS_BOOT_ACTIVE) {
			dprintf("%s terminating 2\n", __func__);
			goto out_locked;
		}

		/* Check for work */
		if (pools->disks->getCount() == 0) {
			dprintf("%s no disks to check\n", __func__);
			goto next_locked;
		}

		/* Swap full set with a new empty one */
		ASSERT3U(new_set, !=, 0);
		disks = pools->disks;
		pools->disks = new_set;
		new_set = 0;

		/* Release pool list lock */
		mutex_exit(&pools->lock);

		/* Create an iterator over the objects in the set */
		iter = OSCollectionIterator::withCollection(disks);

		/* couldn't be initialized */
		if (!iter) {
			dprintf("%s %s %d %s\n", "zfs_boot_import_thread",
			    "couldn't get iterator from collection",
			    disks->getCount(), "disks skipped");

			/* Merge disks back into pools->disks */
			mutex_enter(&pools->lock);
			pools->disks->merge(disks);
			mutex_exit(&pools->lock);

			/* Swap 'disks' back to new_set */
			disks->flushCollection();
			new_set = disks;
			disks = 0;

			continue;
		}

		/* Iterate over all disks */
		while ((next = iter->getNextObject()) != NULL) {
			/* Cast each IOMedia object */
			media = OSDynamicCast(IOMedia, next);

			if (!iter->isValid()) {
				/* Oh gosh, need to start over */
				iter->reset();
				continue;
			}

			if (!media) {
				dprintf("%s couldn't cast IOMedia\n",
				    __func__);
				continue;
			}

			/* Check this IOMedia device for a vdev label */
			if (!zfs_boot_probe_disk(pools, media)) {
				dprintf("%s couldn't probe disk\n",
				    __func__);
				continue;
			}
		}

		/* Clean up */
		media = 0;
		iter->release();
		iter = 0;

		/* Swap 'disks' back to new_set */
		disks->flushCollection();
		new_set = disks;
		disks = 0;

		/* Abort early */
		if (pools->terminating != ZFS_BOOT_ACTIVE) {
			dprintf("%s terminating 3\n", __func__);
			goto out_unlocked;
		}

		mutex_enter(&pools->lock);
		/* Check for work */
		if (pools->disks->getCount() != 0) {
			dprintf("%s more disks available, looping\n", __func__);
			continue;
		}
		/* Release pool list lock */
		mutex_exit(&pools->lock);

		/* Generate a list of pool configs to import */
		configs = zfs_boot_get_configs(pools, B_TRUE);

		/* Abort early */
		if (pools->terminating != ZFS_BOOT_ACTIVE) {
			dprintf("%s terminating 4\n", __func__);
			goto out_unlocked;
		}

		/* Iterate over the nvlists (stored as nvpairs in nvlist) */
		elem = NULL;
		while ((elem = nvlist_next_nvpair(configs,
		    elem)) != NULL) {
			/* Cast the nvpair back to nvlist */
			nv = NULL;
			verify(nvpair_value_nvlist(elem, &nv) == 0);

			/* Check vdev state */
			verify(nvlist_lookup_uint64(nv,
			    ZPOOL_CONFIG_POOL_STATE,
			    &pool_state) == 0);
			if (pool_state == POOL_STATE_DESTROYED) {
				dprintf("%s skipping destroyed pool\n",
				    __func__);
				continue;
			}

			/* Abort early */
			if (pools->terminating != ZFS_BOOT_ACTIVE) {
				dprintf("%s terminating 5\n", __func__);
				goto out_unlocked;
			}

			/* Try import */
			newnv = spa_tryimport(nv);
			nvlist_free(nv);
			nv = 0;
			if (newnv) {
				dprintf("%s newnv: %p\n", __func__, newnv);

				/* Stop probing disks */
				if (pools->notifier)
					pools->notifier->disable();

				/* Do import */
				pool_imported = (spa_import(pools->pool_name,
				    newnv, 0, 0) == 0);
				nvlist_free(newnv);
				newnv = 0;
				// pool_imported = spa_import_rootpool(nv);
			} else {
				dprintf("%s no newnv returned\n", __func__);
			}

			dprintf("%s spa_import returned %d\n", __func__,
			    pool_imported);

			if (pool_imported) {
				/* Get bootfs and publish IOMedia */
				error = zfs_boot_publish_bootfs(zfs_hl, pools);
				if (error != 0) {
					dprintf("%s publish bootfs error %d\n",
					    __func__, error);
				}

				goto out_unlocked;
			} else {
				/* Resume notifications */
				if (pools->notifier)
					pools->notifier->enable(true);
			}
		}

		/* Retake pool list lock */
		mutex_enter(&pools->lock);

next_locked:
		/* Check for work */
		if (pools->disks->getCount() != 0) {
			continue;
		}

		/* Abort early */
		if (pools->terminating != ZFS_BOOT_ACTIVE) {
			dprintf("%s terminating 6\n", __func__);
			goto out_locked;
		}

		dprintf("%s sleeping on lock\n", __func__);
		/* Sleep on lock, thread is resumed with lock held */
		cv_timedwait_sig(&pools->cv, &pools->lock,
		    ddi_get_lbolt() + hz);

	/* Loop forever */
	} while (true);

out_locked:
	/* Unlock pool list lock */
	mutex_exit(&pools->lock);

out_unlocked:
	/* Cleanup new_set */
	if (new_set) {
		new_set->flushCollection();
		new_set->release();
		new_set = 0;
	}

	/* Teardown pool list, lock, etc */
	zfs_boot_free();

	return;	/* taskq_dispatch */
#if 0
	thread_exit();	/* thread_create */
#endif
}

DSTATIC bool
zfs_boot_check_mountroot(char **pool_name, uint64_t *pool_guid)
{
	/*
	 * Check if the kext is loading during early boot
	 * and/or check if root is mounted (IORegistry?)
	 * Use PE Boot Args to determine the root pool name.
	 */
	char *zfs_boot;
	char *split;
	uint64_t len;
	bool result = false;
	uint64_t uptime =   0;


	if (!pool_name || !pool_guid) {
		dprintf("%s %s\n", __func__,
		    "invalid pool_name or pool_guid ptr");
		return (false);
	}

	/* XXX Ugly hack to determine if this is early boot */
	/*
	 * Could just check if boot-uuid (or rd= or rootdev=)
	 * are set, and abort otherwise
	 * IOResource "boot-uuid" only published before root is
	 * mounted, or "boot-uuid-media" once discovered
	 */
	clock_get_uptime(&uptime); /* uptime since boot in nanoseconds */
	dprintf("%s uptime: %llu\n", __func__, uptime);

	/* 3 billion nanoseconds ~= 3 seconds */
	// if (uptime >= 3LLU<<30) {
	/* 60 billion nanoseconds ~= 60 seconds */
	if (uptime >= 7LLU<<33) {
		dprintf("%s %s\n", __func__, "Already booted");
		/*
		 * Start the getrootdir() from working, the vfs_start() call
		 * isn't called until first mount, which is too late for
		 * spa_async_dispatch().
		 */
		return (false);
	} else {
		dprintf("%s %s\n", __func__, "Boot time");
	}

	zfs_boot = (char *)kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (!zfs_boot) {
		dprintf("%s couldn't allocate zfs_boot\n", __func__);
		return (false);
	}

	result = PE_parse_boot_argn("zfs_boot", zfs_boot,
	    ZFS_MAX_DATASET_NAME_LEN);
	// dprintf( "Raw zfs_boot: [%llu] {%s}\n",
	//    (uint64_t)strlen(zfs_boot), zfs_boot);

	result = (result && (zfs_boot != 0) && strlen(zfs_boot) > 0);

	if (!result) {
		result = PE_parse_boot_argn("rd", zfs_boot,
		    MAXPATHLEN);
		result = (result && (zfs_boot != 0) &&
		    strlen(zfs_boot) > 0 &&
		    strncmp(zfs_boot, "zfs:", 4));
		// dprintf("Raw rd: [%llu] {%s}\n",
		//    (uint64_t)strlen(zfs_boot), zfs_boot );
	}
	if (!result) {
		result = PE_parse_boot_argn("rootdev", zfs_boot,
		    MAXPATHLEN);
		result = (result && (zfs_boot != 0) &&
		    strlen(zfs_boot) > 0 &&
		    strncmp(zfs_boot, "zfs:", 4));
		// dprintf("Raw rootdev: [%llu] {%s}\n",
		//    (uint64_t)strlen(zfs_boot), zfs_boot );
	}

	/*
	 * XXX To Do - parse zpool_guid boot arg
	 */
	*pool_guid = 0;

	if (result) {
		/* Check for first slash in zfs_boot */
		split = strchr(zfs_boot, '/');
		if (split) {
			/* copy pool name up to first slash */
			len = (split - zfs_boot);
		} else {
			/* or copy whole string */
			len = strlen(zfs_boot);
		}

		*pool_name = (char *)kmem_alloc(len+1, KM_SLEEP);
		snprintf(*pool_name, len+1, "%s", zfs_boot);

		dprintf("Got zfs_boot: [%llu] {%s}->{%s}\n",
		    *pool_guid, zfs_boot, *pool_name);
	} else {
		dprintf("%s\n", "No zfs_boot\n");
		pool_name = 0;
	}

	kmem_free(zfs_boot, ZFS_MAX_DATASET_NAME_LEN);
	zfs_boot = 0;
	return (result);
}

bool
zfs_boot_init(IOService *zfs_hl)
{
	IONotifier *notifier = 0;
	pool_list_t *pools = 0;
	char *pool_name = 0;
	uint64_t pool_guid = 0;

	zfs_boot_pool_list = 0;

	if (!zfs_hl) {
		dprintf("%s: No zfs_hl provided\n", __func__);
		return (false);
	}

	if (!zfs_boot_check_mountroot(&pool_name, &pool_guid) ||
	    (!pool_name && pool_guid == 0)) {
		/*
		 * kext is not being loaded during early-boot,
		 * or no pool is specified for import.
		 */
		dprintf("%s: check failed\n", __func__);
		return (true);
	}

	pools = (pool_list_t *)kmem_alloc(sizeof (pool_list_t),
	    KM_SLEEP);
	if (!pools) {
		goto error;
	}
	memset(pools, 0, sizeof (pool_list_t));

	if ((pools->disks = OSSet::withCapacity(
	    ZFS_BOOT_PREALLOC_SET)) == NULL) {
		/* Fail if memory couldn't be allocated */
		goto error;
	}

	/* create the zfs_boot taskq */

	zfs_boot_taskq =
	    taskq_create("zfs_boot_taskq", 100, defclsyspri,
		max_ncpus, INT_MAX,
		TASKQ_PREPOPULATE | TASKQ_THREADS_CPU_PCT);

	VERIFY(zfs_boot_taskq);

	/* create the lock and cv early, before notifier */
	mutex_init(&pools->lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&pools->cv, NULL, CV_DEFAULT, NULL);

	pools->pools = 0;
	pools->names = 0;
	pools->pool_guid = pool_guid;
	pools->pool_name = pool_name;
	pools->zfs_hl = zfs_hl;
	/* and finally hit the _Atomic to spread the above */
	pools->terminating = ZFS_BOOT_ACTIVE;

	notifier = IOService::addMatchingNotification(
	    gIOFirstPublishNotification, IOService::serviceMatching(
	    "IOMediaBSDClient"), zfs_boot_probe_media,
	    zfs_hl, pools, 0);

	if (!notifier) {
		/* Fail if memory couldn't be allocated */
		goto error;
	}
	pools->notifier = notifier;

	/* Finally, start the import thread */
	VERIFY3U(taskq_dispatch(zfs_boot_taskq, zfs_boot_import_thread,
		(void*)pools, TQ_SLEEP), !=, 0);

#if 0
/* Alternate method of scheduling the import thread */
	(void) thread_create(NULL, 0, zfs_boot_import_thread,
	    pools, 0, &p0,
	    TS_RUN, minclsyspri);
#endif

	zfs_boot_pool_list = pools;

	return (true);

error:
	if (pools) {
		if (pools->disks) {
			pools->disks->flushCollection();
			pools->disks->release();
			pools->disks = 0;
		}
		kmem_free(pools, sizeof (pool_list_t));
		pools = 0;
	}
	return (false);
}

/* Include these functions in all builds */

/*
 * zfs_boot_update_bootinfo_vdev_leaf
 * Inputs: spa: valid pool spa pointer. vd: valid vdev pointer.
 * Return: 0 on success, positive integer errno on failure.
 * Callers: zfs_boot_update_bootinfo_vdev
 *
 * called by bootinfo_vdev with each leaf vdev.
 */
DSTATIC int
zfs_boot_update_bootinfo_vdev_leaf(OSArray *array, vdev_t *vd)
{
	OSDictionary *dict;
	OSString *dev_str;
	OSNumber *dev_size;
	vdev_disk_t *dvd;
	struct io_bootinfo *info;
	int error;

	/* Validate inputs */
	if (!array || !vd) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	/* Should be called with leaf vdev */
	if (!vd->vdev_ops->vdev_op_leaf) {
		dprintf("%s not a leaf vdev\n", __func__);
		return (EINVAL);
	}

	/* Skip hole vdevs */
	if (vd->vdev_ishole) {
		dprintf("%s skipping hole in namespace\n", __func__);
		return (0);
	}

	/* No info available if missing */
	if (strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_MISSING) == 0) {
		dprintf("%s skipping missing vdev\n", __func__);
		return (0);
	}

	/* Must be a disk, not a file */
	if (strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_DISK) != 0) {
		dprintf("%s skipping non-disk vdev\n", __func__);
		return (0);
	}

	/* Skip obviously non-bootable vdevs */
	if (vd->vdev_islog ||
	    vd->vdev_isl2cache || vd->vdev_isspare) {
		dprintf("%s skipping non-bootable\n", __func__);
		return (0);
	}

	/* Get vdev type-specific data */
	dvd = (vdev_disk_t *)vd->vdev_tsd;
	if (!dvd || !dvd->vd_lh) {
		dprintf("%s missing dvd or ldi handle\n", __func__);
		return (0);
	}

	/* Allocate an ldi io_bootinfo struct */
	info = (struct io_bootinfo *)kmem_alloc(
	    sizeof (struct io_bootinfo), KM_SLEEP);
	if (!info) {
		dprintf("%s info alloc failed\n", __func__);
		return (ENOMEM);
	}
	memset(info, 0, sizeof (struct io_bootinfo));

	/* Ask the vdev handle to fill in the info */
	error = ldi_ioctl(dvd->vd_lh, DKIOCGETBOOTINFO,
	    (intptr_t)info, 0, 0, 0);
	if (error != 0) {
		dprintf("%s ioctl error %d\n", __func__, error);
		kmem_free(info, sizeof (struct io_bootinfo));
		return (EIO);
	}

	/* Allocate dictionary to hold the keys */
	if ((dict = OSDictionary::withCapacity(2)) == NULL) {
		dprintf("%s dictionary alloc failed\n", __func__);
		kmem_free(info, sizeof (struct io_bootinfo));
		return (ENOMEM);
	}

	/* Keys are path (string) and size (number) */
	dev_str = OSString::withCString(info->dev_path);
	dev_size = OSNumber::withNumber(info->dev_size,
	    (8 * sizeof (info->dev_size)));
	kmem_free(info, sizeof (struct io_bootinfo));
	info = 0;

	/* Add keys to dictionary or bail */
	if (!dev_str || !dev_size ||
	    dict->setObject(kIOBootDevicePathKey,
	    dev_str) == false ||
	    dict->setObject(kIOBootDeviceSizeKey,
	    dev_size) == false) {
		dprintf("%s dictionary setup failed\n", __func__);
		if (dev_str) dev_str->release();
		if (dev_size) dev_size->release();
		dict->release();
		dict = 0;
		return (ENOMEM);
	}
	dev_str->release();
	dev_str = 0;
	dev_size->release();
	dev_size = 0;

	/* Add dict to array */
	if (array->setObject(dict) == false) {
		dprintf("%s couldn't set bootinfo\n", __func__);
		dict->release();
		dict = 0;
		return (ENOMEM);
	}
	dict->release();
	dict = 0;

	return (0);
}

/*
 * zfs_boot_update_bootinfo_vdev
 * Inputs: spa: valid pool spa pointer. vd: valid vdev pointer.
 * Return: 0 on success, positive integer errno on failure.
 * Callers: zfs_boot_update_bootinfo
 *
 * called by bootinfo with root vdev, and recursively calls
 * itself while iterating over children (vdevs only have a
 * few levels of nesting at most).
 */
DSTATIC int
zfs_boot_update_bootinfo_vdev(OSArray *array, vdev_t *vd)
{
	int c, error;

	/* Validate inputs */
	if (!array || !vd) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	/* Skip obviously non-bootable vdevs */
	if (vd->vdev_islog ||
	    vd->vdev_isl2cache || vd->vdev_isspare) {
		dprintf("%s skipping non-bootable\n", __func__);
		return (0);
	}

	/* Process leaf vdevs */
	if (vd->vdev_ops->vdev_op_leaf) {
		error = zfs_boot_update_bootinfo_vdev_leaf(array, vd);
		if (error)
			dprintf("%s bootinfo_vdev_leaf error %d\n",
			    __func__, error);
		return (error);
	}

	/* Iterate over child vdevs */
	for (c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c] == NULL) {
			dprintf("%s hole in vdev namespace\n", __func__);
			continue;
		}

		/* Recursion */
		error = zfs_boot_update_bootinfo_vdev(array,
		    vd->vdev_child[c]);
		if (error != 0) {
			dprintf("%s bootinfo_vdev_leaf error %d\n",
			    __func__, error);
			return (error);
		}
	}

	return (0);
}

extern "C" {

/*
 * zfs_boot_update_bootinfo
 * Inputs: spa: valid pool spa pointer.
 * Return: 0 on success, positive integer errno on failure.
 * Callers: spa_open_common, spa_vdev_add, spa_vdev_remove,
 * spa_vdev_attach, spa_vdev_detach.
 *
 * Called from spa.c on changes to the vdev layout. This
 * information is assigned to the pool proxy so all zvols
 * and datasets will retrieve the property through IOKit
 * since it is retrieved via recursion.
 * (see bless-105/Misc/BLCreateBooterInformationDictionary.c).
 * If IOBootDevice property is needed for each dataset and
 * zvol, we can revisit this and assign/update on all of
 * these (already implemented a prototype that worked fine).
 *
 * Note: bootinfo is only collected for data vdevs.
 * XXX We only want boot helpers there, unless there is a
 * compelling argument for log, cache, or spares having
 * boot helpers.
 */
int
zfs_boot_update_bootinfo(spa_t *spa)
{
	ZFSPool *pool_proxy;
	OSArray *array;
	int error;

	if (!spa) {
		dprintf("%s missing spa\n", __func__);
		return (EINVAL);
	}

	/* XXX Could count vdevs first? */
	if ((array = OSArray::withCapacity(1)) == NULL) {
		dprintf("%s allocation failed\n", __func__);
		return (ENOMEM);
	}

	/* Grab necessary locks */
	mutex_enter(&spa_namespace_lock);
	spa_open_ref(spa, FTAG);

	/* Get pool proxy */
	if (!spa->spa_iokit_proxy ||
	    (pool_proxy = spa->spa_iokit_proxy->proxy) == NULL) {
		spa_close(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		dprintf("%s no spa_pool_proxy\n", __func__);
		return (0);
	}
	/* Avoid it disappearing from under us */
	pool_proxy->retain();

	/* Don't need to hold this throughout */
	mutex_exit(&spa_namespace_lock);

	/* vdev state lock only requires an spa open ref */
	spa_vdev_state_enter(spa, SCL_NONE);

	/* Iterate over all vdevs */
	if ((error = zfs_boot_update_bootinfo_vdev(array,
	    spa->spa_root_vdev)) != 0) {
		dprintf("%s bootinfo_vdev error %d\n",
		    __func__, error);

		/* Drop locks */
		(void) spa_vdev_state_exit(spa, NULL, 0);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		array->release();
		pool_proxy->release();
		return (error);
	}

	/* Release locks, passing NULL vd (no change) */
	error = spa_vdev_state_exit(spa, NULL, 0);
	if (error != 0) {
		dprintf("%s spa_vdev_state_exit error %d\n",
		    __func__, error);
	}

	/* setProperty adds a retain */
	pool_proxy->setProperty(kIOBootDeviceKey, array);
	pool_proxy->release();
	array->release();

	/* Drop locks */
	mutex_enter(&spa_namespace_lock);
	spa_close(spa, FTAG);
	mutex_exit(&spa_namespace_lock);
	return (0);
}

} /* extern "C" */

#if 0
#ifdef ZFS_BOOT
/* Remainder only needed for boot */

#define	DPRINTF_FUNC()	dprintf("%s\n", __func__)

#pragma mark - ZFSBootDevice

OSDefineMetaClassAndStructors(ZFSBootDevice, IOBlockStorageDevice);
char ZFSBootDevice::vendorString[4] = "ZFS";
char ZFSBootDevice::revisionString[4] = "0.1";
char ZFSBootDevice::infoString[12] = "ZFS dataset";

#if 0
int
zfs_boot_get_path(char *path, int len)
{
	OSString *disk = 0;

	if (!path || len == 0) {
		dprintf("%s: invalid argument\n", __func__);
		return (-1);
	}

	if (bootdev) {
		disk = OSDynamicCast(OSString,
		    bootdev->getProperty(kIOBSDNameKey, gIOServicePlane,
		    kIORegistryIterateRecursively));
	}

	if (disk) {
		snprintf(path, len, "/dev/%s", disk->getCStringNoCopy());
		return (0);
	}

	return (-1);
}
#endif

bool
ZFSBootDevice::init(OSDictionary *properties)
{
	/* Allocate dictionaries and symbols */
	OSDictionary *pdict = OSDictionary::withCapacity(2);
	OSDictionary *ddict = OSDictionary::withCapacity(4);
	const OSSymbol *virtualSymbol = OSSymbol::withCString(
	    kIOPropertyPhysicalInterconnectTypeVirtual);
	const OSSymbol *ramSymbol = OSSymbol::withCString(
	    kIOPropertyInterconnectRAMKey);
	const OSSymbol *ssdSymbol = OSSymbol::withCString(
	    kIOPropertyMediumTypeSolidStateKey);
	OSNumber *physSize = OSNumber::withNumber((uint32_t)4096, 32);
	OSNumber *logSize = OSNumber::withNumber((uint32_t)512, 32);
	const OSSymbol *vendorSymbol = 0;
	const OSSymbol *revisionSymbol = 0;
	const OSSymbol *blankSymbol = 0;
	OSBoolean *rdonly = 0;
	OSString *str = 0;
	const char *cstr = 0;
	bool ret = false;

	DPRINTF_FUNC();

	/* Validate allocations */
	if (!pdict || !ddict || !virtualSymbol || !ramSymbol ||
	    !ssdSymbol || !physSize || !logSize) {
		dprintf("ZFSBootDevice::%s allocation failed\n", __func__);
		goto error;
	}

	/* Init class statics every time an instance inits */
	/* Shared across instances, but doesn't hurt to reprint */
	snprintf(vendorString, strlen("ZFS")+1, "ZFS");
	snprintf(revisionString, strlen("0.1")+1, "0.1");
	snprintf(infoString, strlen("ZFS dataset")+1, "ZFS dataset");

	/* For IORegistry keys, cache OSSymbols for class statics */
	/* Leverages OSSymbol cahce pool to reuse across instances */
	vendorSymbol = OSSymbol::withCString(vendorString);
	revisionSymbol = OSSymbol::withCString(revisionString);
	blankSymbol = OSSymbol::withCString("");
	if (!vendorSymbol || !revisionSymbol || !blankSymbol) {
		dprintf("ZFSBootDevice::%s class symbols failed\n", __func__);
		goto error;
	}

	/* Call super init */
	if (IOBlockStorageDevice::init(properties) == false) {
		dprintf("ZFSBootDevice::%s device init failed\n", __func__);
		goto error;
	}

	/* Set class private vars */
	productString = NULL;
	isReadOnly = false; // XXX should really be true initially

	/* Set Protocol Characteristics */
	if (pdict->setObject(kIOPropertyPhysicalInterconnectLocationKey,
	    ramSymbol) == false ||
	    pdict->setObject(kIOPropertyPhysicalInterconnectTypeKey,
	    virtualSymbol) == false) {
		dprintf("%s pdict set properties failed\n", __func__);
		goto error;
	}
	setProperty(kIOPropertyProtocolCharacteristicsKey, pdict);

	/* Set Device Characteristics */
	if (ddict->setObject(kIOPropertyVendorNameKey,
	    vendorSymbol) == false ||
	    ddict->setObject(kIOPropertyProductRevisionLevelKey,
	    revisionSymbol) == false ||
	    ddict->setObject(kIOPropertyProductSerialNumberKey,
	    blankSymbol) == false ||
	    ddict->setObject(kIOPropertyPhysicalBlockSizeKey,
	    physSize) == false ||
	    ddict->setObject(kIOPropertyLogicalBlockSizeKey,
	    logSize) == false ||
	    ddict->setObject(kIOPropertyMediumTypeKey,
	    ssdSymbol) == false) {
		dprintf("%s ddict set properties failed\n", __func__);
		goto error;
	}
	setProperty(kIOPropertyDeviceCharacteristicsKey, ddict);

	/* Check for passed in readonly status */
	if (properties && (rdonly = OSDynamicCast(OSBoolean,
	    properties->getObject(ZFS_BOOT_DATASET_RDONLY_KEY))) != NULL) {
		/* Got the boolean */
		isReadOnly = rdonly->getValue();
		dprintf("ZFSBootDevice %s set %s\n", __func__,
		    (isReadOnly ? "readonly" : "readwrite"));
	}

	/* Check for passed in dataset UUID */
	if (properties && (str = OSDynamicCast(OSString,
	    properties->getObject(ZFS_BOOT_DATASET_UUID_KEY))) != NULL &&
	    (cstr = str->getCStringNoCopy()) != NULL) {
		/* Got the string, try to set UUID */
		str->retain();
		if (ddict->setObject("Dataset UUID", str) == false) {
			dprintf("ZFSBootDevice::%s failed UUID [%s]\n",
			    __func__, cstr);
			str->release();
			goto error;
		}
		dprintf("ZFSBootDevice::%s set UUID [%s]\n",
		    __func__, cstr);
		str->release();
	}

	/* Check for passed in dataset name */
	if (properties && (str = OSDynamicCast(OSString,
	    properties->getObject(ZFS_BOOT_DATASET_NAME_KEY))) != NULL &&
	    (cstr = str->getCStringNoCopy()) != NULL) {
		/* Got the string, try to set name */
		str->retain();
		if (setDatasetName(cstr) == false) {
			/* Unlikely */
			dprintf("ZFSBootDevice %s couldn't setup dataset"
			    " name property [%s]\n", __func__, cstr);
			str->release();
			goto error;
		}

		dprintf("ZFSBootDevice %s set dataset name [%s]\n",
		    __func__, cstr);
		str->release();
	} else {
		if (setDatasetName("invalid") == false) {
			dprintf("ZFSBootDevice::%s setDatasetName failed\n",
			    __func__);
			goto error;
		}
		dprintf("ZFSBootDevice %s set name [invalid]\n", __func__);
	}

	/* Success */
	ret = true;

error:
	if (pdict) pdict->release();
	if (ddict) ddict->release();
	if (virtualSymbol) virtualSymbol->release();
	if (ramSymbol) ramSymbol->release();
	if (ssdSymbol) ssdSymbol->release();
	if (physSize) physSize->release();
	if (logSize) logSize->release();
	if (vendorSymbol) vendorSymbol->release();
	if (revisionSymbol) revisionSymbol->release();
	if (blankSymbol) blankSymbol->release();
	return (ret);
}

void
ZFSBootDevice::free()
{
	char *pstring = (char *)productString;
	productString = 0;

	if (pstring) kmem_free(pstring, strlen(pstring) + 1);

	IOBlockStorageDevice::free();
}

#if 0
bool
ZFSBootDevice::attach(IOService *provider)
{
	DPRINTF_FUNC();
	// return (IOMedia::attach(provider));
	return (IOBlockStorageDevice::attach(provider));
}

void
ZFSBootDevice::detach(IOService *provider)
{
	DPRINTF_FUNC();
	// IOMedia::detach(provider);
	IOBlockStorageDevice::detach(provider);
}

bool
ZFSBootDevice::start(IOService *provider)
{
	DPRINTF_FUNC();
	// return (IOMedia::start(provider));
	return (IOBlockStorageDevice::start(provider));
}

void
ZFSBootDevice::stop(IOService *provider)
{
	DPRINTF_FUNC();
	// IOMedia::stop(provider);
	IOBlockStorageDevice::stop(provider);
}

IOService*
ZFSBootDevice::probe(IOService *provider, SInt32 *score)
{
	DPRINTF_FUNC();
	// return (IOMedia::probe(provider, score));
	return (IOBlockStorageDevice::probe(provider, score));
}
#endif

IOReturn
ZFSBootDevice::doSynchronizeCache(void)
{
	dprintf("ZFSBootDevice %s\n", __func__);
	return (kIOReturnSuccess);
}

IOReturn
ZFSBootDevice::doAsyncReadWrite(IOMemoryDescriptor *buffer,
    UInt64 block, UInt64 nblks,
    IOStorageAttributes *attributes,
    IOStorageCompletion *completion)
{
	char zero[ZFS_BOOT_DEV_BSIZE];
	size_t len, cur, off = 0;

	DPRINTF_FUNC();

	if (!buffer) {
		IOStorage::complete(completion, kIOReturnError, 0);
		return (kIOReturnSuccess);
	}

	/* Read vs. write */
	if (buffer->getDirection() == kIODirectionIn) {
		/* Zero the read buffer */
		memset(zero, 0, ZFS_BOOT_DEV_BSIZE);
		len = buffer->getLength();
		while (len > 0) {
			cur = (len > ZFS_BOOT_DEV_BSIZE ?
			    ZFS_BOOT_DEV_BSIZE : len);
			buffer->writeBytes(/* offset */ off,
			    /* buf */ zero, /* length */ cur);
			off += cur;
			len -= cur;
		}
		// dprintf("%s: read: %llu %llu\n",
		//    __func__, block, nblks);
		IOStorage::complete(completion, kIOReturnSuccess,
			    buffer->getLength());
		return (kIOReturnSuccess);
	}

	if (buffer->getDirection() != kIODirectionOut) {
		dprintf("%s invalid direction %d\n", __func__,
		    buffer->getDirection());
		IOStorage::complete(completion, kIOReturnError, 0);
		return (kIOReturnSuccess);
	}

	/*
	 * XXX For now this just returns error for all writes.
	 * If it turns out that mountroot/bdevvp try to
	 * verify writable status by reading a block and writing
	 * it back to disk, lie and say it succeeded.
	 */
	dprintf("%s: write: %llu %llu\n", __func__, block, nblks);
	IOStorage::complete(completion, kIOReturnError, 0);
	return (kIOReturnSuccess);
}

IOReturn
ZFSBootDevice::doEjectMedia()
{
	DPRINTF_FUNC();
	/* XXX Called at shutdown, maybe return success? */
	return (kIOReturnError);
}

IOReturn
ZFSBootDevice::doFormatMedia(UInt64 byteCapacity)
{
	DPRINTF_FUNC();
	/* XXX shouldn't need it */
	return (kIOReturnError);
	// return (kIOReturnSuccess);
}

UInt32
ZFSBootDevice::doGetFormatCapacities(UInt64 *capacities,
    UInt32 capacitiesMaxCount) const
{
	DPRINTF_FUNC();
	if (capacities && capacitiesMaxCount > 0) {
		capacities[0] = (ZFS_BOOT_DEV_BSIZE * ZFS_BOOT_DEV_BCOUNT);
		dprintf("ZFSBootDevice %s: capacity %llu\n",
		    __func__, capacities[0]);
	}

	/* Always inform caller of capacity count */
	return (1);
}

/* Assign dataset name from null-terminated string */
bool
ZFSBootDevice::setDatasetName(const char *dsname)
{
	OSDictionary *dict;
	OSString *dsstr;
	char *newname, *oldname;
	size_t len;

	DPRINTF_FUNC();

	/* Validate arguments */
	if (!dsname || (len = strnlen(dsname,
	    ZFS_MAX_DATASET_NAME_LEN)) == 0) {
		dprintf("%s: missing argument\n", __func__);
		return (false);
	}

	/* Truncate too-long names (shouldn't happen) */
	if (len == ZFS_MAX_DATASET_NAME_LEN &&
	    dsname[ZFS_MAX_DATASET_NAME_LEN] != '\0') {
		dprintf("%s: dsname too long [%s]\n",
		    __func__, dsname);
		/* XXX Just truncate the name */
		len--;
	}

	/* Allocate room for name plus null char */
	newname = (char *)kmem_alloc(len+1, KM_SLEEP);
	if (!newname) {
		dprintf("ZFSBootDevice::%s string alloc failed\n", __func__);
		return (false);
	}
	snprintf(newname, len+1, "%s", dsname);
	newname[len] = '\0'; /* just in case */

	/* Save an OSString copy for IORegistry */
	dsstr = OSString::withCString(newname);
	if (!dsstr) {
		dprintf("ZFSBootDevice::%s OSString failed\n", __func__);
		kmem_free(newname, len+1);
		return (false);
	}

	/* Swap into class private var */
	oldname = productString;
	productString = newname;
	newname = 0;
	if (oldname) {
		kmem_free(oldname, strlen(oldname)+1);
		oldname = 0;
	}

	/* Get and clone device characteristics prop dict */
	if ((dict = OSDynamicCast(OSDictionary,
	    getProperty(kIOPropertyDeviceCharacteristicsKey))) == NULL ||
	    (dict = OSDictionary::withDictionary(dict)) == NULL) {
		dprintf("%s couldn't clone prop dict\n", __func__);
		/* Should only happen during initialization */
	}

	if (dict) {
		/* Copy string, add to dictionary, and replace prop dict */
		if (dict->setObject(kIOPropertyProductNameKey,
		    dsstr) == false ||
		    setProperty(kIOPropertyDeviceCharacteristicsKey,
		    dict) == false) {
			dprintf("%s couldn't set name\n", __func__);
			dsstr->release();
			dict->release();
			return (false);
		}
		dict->release();
		dict = 0;
	}

	/* Finally, set the IORegistryEntry/IOService name */
	setName(dsstr->getCStringNoCopy());
	dsstr->release();

	return (true);
}

/* Returns full dataset name from instance private var */
char *
ZFSBootDevice::getProductString()
{
	dprintf("ZFSBootDevice %s [%s]\n", productString);
	/* Return class private string */
	return (productString);
}

/* Returns readonly status from instance private var */
IOReturn
ZFSBootDevice::reportWriteProtection(bool *isWriteProtected)
{
	DPRINTF_FUNC();
	if (isWriteProtected) *isWriteProtected = isReadOnly;
	return (kIOReturnSuccess);
}

/* These return class static string for all instances */
char *
ZFSBootDevice::getVendorString()
{
	dprintf("ZFSBootDevice %s [%s]\n", vendorString);
	/* Return class static string */
	return (vendorString);
}
char *
ZFSBootDevice::getRevisionString()
{
	dprintf("ZFSBootDevice %s [%s]\n", revisionString);
	/* Return class static string */
	return (revisionString);
}
char *
ZFSBootDevice::getAdditionalDeviceInfoString()
{
	dprintf("ZFSBootDevice %s [%s]\n", infoString);
	/* Return class static string */
	return (infoString);
}

/* Always return media present and unchanged */
IOReturn
ZFSBootDevice::reportMediaState(bool *mediaPresent,
    bool *changedState)
{
	DPRINTF_FUNC();
	if (mediaPresent) *mediaPresent = true;
	if (changedState) *changedState = false;
	return (kIOReturnSuccess);
}

/* Always report nonremovable and nonejectable */
IOReturn
ZFSBootDevice::reportRemovability(bool *isRemoveable)
{
	DPRINTF_FUNC();
	if (isRemoveable) *isRemoveable = false;
	return (kIOReturnSuccess);
}
IOReturn
ZFSBootDevice::reportEjectability(bool *isEjectable)
{
	DPRINTF_FUNC();
	if (isEjectable) *isEjectable = false;
	return (kIOReturnSuccess);
}

/* Always report 512b blocksize */
IOReturn
ZFSBootDevice::reportBlockSize(UInt64 *blockSize)
{
	DPRINTF_FUNC();
	if (!blockSize)
		return (kIOReturnError);

	*blockSize = ZFS_BOOT_DEV_BSIZE;
	return (kIOReturnSuccess);
}

/* XXX Calculate from dev_bcount, should get size from objset */
/* XXX Can issue message kIOMessageMediaParametersHaveChanged to update */
IOReturn
ZFSBootDevice::reportMaxValidBlock(UInt64 *maxBlock)
{
	DPRINTF_FUNC();
	if (!maxBlock)
		return (kIOReturnError);

	// *maxBlock = 0;
	*maxBlock = ZFS_BOOT_DEV_BCOUNT - 1;
	dprintf("ZFSBootDevice %s: maxBlock %llu\n", __func__, *maxBlock);

	return (kIOReturnSuccess);
}
#endif	/* ZFS_BOOT */
#endif	/* 0 */
