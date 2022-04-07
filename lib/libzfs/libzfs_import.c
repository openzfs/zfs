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

#include <errno.h>
#include <libintl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/vdev_impl.h>
#include <libzfs.h>
#include "libzfs_impl.h"
#include <libzutil.h>
#include <sys/arc_impl.h>

/*
 * Returns true if the named pool matches the given GUID.
 */
static int
pool_active(libzfs_handle_t *hdl, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	zpool_handle_t *zhp;

	if (zpool_open_silent(hdl, name, &zhp) != 0)
		return (-1);

	if (zhp == NULL) {
		*isactive = B_FALSE;
		return (0);
	}

	uint64_t theguid = fnvlist_lookup_uint64(zhp->zpool_config,
	    ZPOOL_CONFIG_POOL_GUID);

	zpool_close(zhp);

	*isactive = (theguid == guid);
	return (0);
}

static nvlist_t *
refresh_config(libzfs_handle_t *hdl, nvlist_t *config)
{
	nvlist_t *nvl;
	zfs_cmd_t zc = {"\0"};
	int err, dstbuf_size;

	zcmd_write_conf_nvlist(hdl, &zc, config);

	dstbuf_size = MAX(CONFIG_BUF_MINSIZE, zc.zc_nvlist_conf_size * 32);

	zcmd_alloc_dst_nvlist(hdl, &zc, dstbuf_size);

	while ((err = zfs_ioctl(hdl, ZFS_IOC_POOL_TRYIMPORT,
	    &zc)) != 0 && errno == ENOMEM)
		zcmd_expand_dst_nvlist(hdl, &zc);

	if (err) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	if (zcmd_read_dst_nvlist(hdl, &zc, &nvl) != 0) {
		zcmd_free_nvlists(&zc);
		return (NULL);
	}

	zcmd_free_nvlists(&zc);
	return (nvl);
}

static nvlist_t *
refresh_config_libzfs(void *handle, nvlist_t *tryconfig)
{
	return (refresh_config((libzfs_handle_t *)handle, tryconfig));
}

static int
pool_active_libzfs(void *handle, const char *name, uint64_t guid,
    boolean_t *isactive)
{
	return (pool_active((libzfs_handle_t *)handle, name, guid, isactive));
}

const pool_config_ops_t libzfs_config_ops = {
	.pco_refresh_config = refresh_config_libzfs,
	.pco_pool_active = pool_active_libzfs,
};

/*
 * Return the offset of the given label.
 */
static uint64_t
label_offset(uint64_t size, int l)
{
	ASSERT(P2PHASE_TYPED(size, sizeof (vdev_label_t), uint64_t) == 0);
	return (l * sizeof (vdev_label_t) + (l < VDEV_LABELS / 2 ?
	    0 : size - VDEV_LABELS * sizeof (vdev_label_t)));
}

/*
 * Given a file descriptor, clear (zero) the label information.  This function
 * is used in the appliance stack as part of the ZFS sysevent module and
 * to implement the "zpool labelclear" command.
 */
int
zpool_clear_label(int fd)
{
	struct stat64 statbuf;
	int l;
	vdev_label_t *label;
	uint64_t size;
	boolean_t labels_cleared = B_FALSE, clear_l2arc_header = B_FALSE,
	    header_cleared = B_FALSE;

	if (fstat64_blk(fd, &statbuf) == -1)
		return (0);

	size = P2ALIGN_TYPED(statbuf.st_size, sizeof (vdev_label_t), uint64_t);

	if ((label = calloc(1, sizeof (vdev_label_t))) == NULL)
		return (-1);

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t state, guid, l2cache;
		nvlist_t *config;

		if (pread64(fd, label, sizeof (vdev_label_t),
		    label_offset(size, l)) != sizeof (vdev_label_t)) {
			continue;
		}

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), &config, 0) != 0) {
			continue;
		}

		/* Skip labels which do not have a valid guid. */
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID,
		    &guid) != 0 || guid == 0) {
			nvlist_free(config);
			continue;
		}

		/* Skip labels which are not in a known valid state. */
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(config);
			continue;
		}

		/* If the device is a cache device clear the header. */
		if (!clear_l2arc_header) {
			if (nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_POOL_STATE, &l2cache) == 0 &&
			    l2cache == POOL_STATE_L2CACHE) {
				clear_l2arc_header = B_TRUE;
			}
		}

		nvlist_free(config);

		/*
		 * A valid label was found, overwrite this label's nvlist
		 * and uberblocks with zeros on disk.  This is done to prevent
		 * system utilities, like blkid, from incorrectly detecting a
		 * partial label.  The leading pad space is left untouched.
		 */
		memset(label, 0, sizeof (vdev_label_t));
		size_t label_size = sizeof (vdev_label_t) - (2 * VDEV_PAD_SIZE);

		if (pwrite64(fd, label, label_size, label_offset(size, l) +
		    (2 * VDEV_PAD_SIZE)) == label_size)
			labels_cleared = B_TRUE;
	}

	if (clear_l2arc_header) {
		_Static_assert(sizeof (*label) >= sizeof (l2arc_dev_hdr_phys_t),
		    "label < l2arc_dev_hdr_phys_t");
		memset(label, 0, sizeof (l2arc_dev_hdr_phys_t));
		if (pwrite64(fd, label, sizeof (l2arc_dev_hdr_phys_t),
		    VDEV_LABEL_START_SIZE) == sizeof (l2arc_dev_hdr_phys_t))
			header_cleared = B_TRUE;
	}

	free(label);

	if (!labels_cleared || (clear_l2arc_header && !header_cleared))
		return (-1);

	return (0);
}

static boolean_t
find_guid(nvlist_t *nv, uint64_t guid)
{
	nvlist_t **child;
	uint_t c, children;

	if (fnvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID) == guid)
		return (B_TRUE);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_guid(child[c], guid))
				return (B_TRUE);
	}

	return (B_FALSE);
}

typedef struct aux_cbdata {
	const char	*cb_type;
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
} aux_cbdata_t;

static int
find_aux(zpool_handle_t *zhp, void *data)
{
	aux_cbdata_t *cbp = data;
	nvlist_t **list;
	uint_t count;

	nvlist_t *nvroot = fnvlist_lookup_nvlist(zhp->zpool_config,
	    ZPOOL_CONFIG_VDEV_TREE);

	if (nvlist_lookup_nvlist_array(nvroot, cbp->cb_type,
	    &list, &count) == 0) {
		for (uint_t i = 0; i < count; i++) {
			uint64_t guid = fnvlist_lookup_uint64(list[i],
			    ZPOOL_CONFIG_GUID);
			if (guid == cbp->cb_guid) {
				cbp->cb_zhp = zhp;
				return (1);
			}
		}
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Determines if the pool is in use.  If so, it returns true and the state of
 * the pool as well as the name of the pool.  Name string is allocated and
 * must be freed by the caller.
 */
int
zpool_in_use(libzfs_handle_t *hdl, int fd, pool_state_t *state, char **namestr,
    boolean_t *inuse)
{
	nvlist_t *config;
	char *name = NULL;
	boolean_t ret;
	uint64_t guid = 0, vdev_guid;
	zpool_handle_t *zhp;
	nvlist_t *pool_config;
	uint64_t stateval, isspare;
	aux_cbdata_t cb = { 0 };
	boolean_t isactive;

	*inuse = B_FALSE;

	if (zpool_read_label(fd, &config, NULL) != 0) {
		(void) no_memory(hdl);
		return (-1);
	}

	if (config == NULL)
		return (0);

	stateval = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE);
	vdev_guid = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID);

	if (stateval != POOL_STATE_SPARE && stateval != POOL_STATE_L2CACHE) {
		name = fnvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME);
		guid = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID);
	}

	switch (stateval) {
	case POOL_STATE_EXPORTED:
		/*
		 * A pool with an exported state may in fact be imported
		 * read-only, so check the in-core state to see if it's
		 * active and imported read-only.  If it is, set
		 * its state to active.
		 */
		if (pool_active(hdl, name, guid, &isactive) == 0 && isactive &&
		    (zhp = zpool_open_canfail(hdl, name)) != NULL) {
			if (zpool_get_prop_int(zhp, ZPOOL_PROP_READONLY, NULL))
				stateval = POOL_STATE_ACTIVE;

			/*
			 * All we needed the zpool handle for is the
			 * readonly prop check.
			 */
			zpool_close(zhp);
		}

		ret = B_TRUE;
		break;

	case POOL_STATE_ACTIVE:
		/*
		 * For an active pool, we have to determine if it's really part
		 * of a currently active pool (in which case the pool will exist
		 * and the guid will be the same), or whether it's part of an
		 * active pool that was disconnected without being explicitly
		 * exported.
		 */
		if (pool_active(hdl, name, guid, &isactive) != 0) {
			nvlist_free(config);
			return (-1);
		}

		if (isactive) {
			/*
			 * Because the device may have been removed while
			 * offlined, we only report it as active if the vdev is
			 * still present in the config.  Otherwise, pretend like
			 * it's not in use.
			 */
			if ((zhp = zpool_open_canfail(hdl, name)) != NULL &&
			    (pool_config = zpool_get_config(zhp, NULL))
			    != NULL) {
				nvlist_t *nvroot = fnvlist_lookup_nvlist(
				    pool_config, ZPOOL_CONFIG_VDEV_TREE);
				ret = find_guid(nvroot, vdev_guid);
			} else {
				ret = B_FALSE;
			}

			/*
			 * If this is an active spare within another pool, we
			 * treat it like an unused hot spare.  This allows the
			 * user to create a pool with a hot spare that currently
			 * in use within another pool.  Since we return B_TRUE,
			 * libdiskmgt will continue to prevent generic consumers
			 * from using the device.
			 */
			if (ret && nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_IS_SPARE, &isspare) == 0 && isspare)
				stateval = POOL_STATE_SPARE;

			if (zhp != NULL)
				zpool_close(zhp);
		} else {
			stateval = POOL_STATE_POTENTIALLY_ACTIVE;
			ret = B_TRUE;
		}
		break;

	case POOL_STATE_SPARE:
		/*
		 * For a hot spare, it can be either definitively in use, or
		 * potentially active.  To determine if it's in use, we iterate
		 * over all pools in the system and search for one with a spare
		 * with a matching guid.
		 *
		 * Due to the shared nature of spares, we don't actually report
		 * the potentially active case as in use.  This means the user
		 * can freely create pools on the hot spares of exported pools,
		 * but to do otherwise makes the resulting code complicated, and
		 * we end up having to deal with this case anyway.
		 */
		cb.cb_zhp = NULL;
		cb.cb_guid = vdev_guid;
		cb.cb_type = ZPOOL_CONFIG_SPARES;
		if (zpool_iter(hdl, find_aux, &cb) == 1) {
			name = (char *)zpool_get_name(cb.cb_zhp);
			ret = B_TRUE;
		} else {
			ret = B_FALSE;
		}
		break;

	case POOL_STATE_L2CACHE:

		/*
		 * Check if any pool is currently using this l2cache device.
		 */
		cb.cb_zhp = NULL;
		cb.cb_guid = vdev_guid;
		cb.cb_type = ZPOOL_CONFIG_L2CACHE;
		if (zpool_iter(hdl, find_aux, &cb) == 1) {
			name = (char *)zpool_get_name(cb.cb_zhp);
			ret = B_TRUE;
		} else {
			ret = B_FALSE;
		}
		break;

	default:
		ret = B_FALSE;
	}


	if (ret) {
		*namestr = zfs_strdup(hdl, name);
		*state = (pool_state_t)stateval;
	}

	if (cb.cb_zhp)
		zpool_close(cb.cb_zhp);

	nvlist_free(config);
	*inuse = ret;
	return (0);
}
