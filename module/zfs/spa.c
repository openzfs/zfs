/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2024 by Delphix. All rights reserved.
 * Copyright (c) 2018, Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright 2018 Joyent, Inc.
 * Copyright (c) 2017, 2019, Datto Inc. All rights reserved.
 * Copyright 2017 Joyent, Inc.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 * Copyright (c) 2023 Hewlett Packard Enterprise Development LP.
 */

/*
 * SPA: Storage Pool Allocator
 *
 * This file contains all the routines used when modifying on-disk SPA state.
 * This includes opening, importing, destroying, exporting a pool, and syncing a
 * pool.
 */

#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/brt.h>
#include <sys/ddt.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_removal.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/vdev_indirect_births.h>
#include <sys/vdev_initialize.h>
#include <sys/vdev_rebuild.h>
#include <sys/vdev_trim.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_draid.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/mmp.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/bpobj.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_objset.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/fs/zfs.h>
#include <sys/arc.h>
#include <sys/callb.h>
#include <sys/systeminfo.h>
#include <sys/zfs_ioctl.h>
#include <sys/dsl_scan.h>
#include <sys/zfeature.h>
#include <sys/dsl_destroy.h>
#include <sys/zvol.h>

#ifdef	_KERNEL
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/callb.h>
#include <sys/zone.h>
#include <sys/vmsystm.h>
#endif	/* _KERNEL */

#include "zfs_prop.h"
#include "zfs_comutil.h"
#include <cityhash.h>

/*
 * spa_thread() existed on Illumos as a parent thread for the various worker
 * threads that actually run the pool, as a way to both reference the entire
 * pool work as a single object, and to share properties like scheduling
 * options. It has not yet been adapted to Linux or FreeBSD. This define is
 * used to mark related parts of the code to make things easier for the reader,
 * and to compile this code out. It can be removed when someone implements it,
 * moves it to some Illumos-specific place, or removes it entirely.
 */
#undef HAVE_SPA_THREAD

/*
 * The "System Duty Cycle" scheduling class is an Illumos feature to help
 * prevent CPU-intensive kernel threads from affecting latency on interactive
 * threads. It doesn't exist on Linux or FreeBSD, so the supporting code is
 * gated behind a define. On Illumos SDC depends on spa_thread(), but
 * spa_thread() also has other uses, so this is a separate define.
 */
#undef HAVE_SYSDC

/*
 * The interval, in seconds, at which failed configuration cache file writes
 * should be retried.
 */
int zfs_ccw_retry_interval = 300;

typedef enum zti_modes {
	ZTI_MODE_FIXED,			/* value is # of threads (min 1) */
	ZTI_MODE_SCALE,			/* Taskqs scale with CPUs. */
	ZTI_MODE_SYNC,			/* sync thread assigned */
	ZTI_MODE_NULL,			/* don't create a taskq */
	ZTI_NMODES
} zti_modes_t;

#define	ZTI_P(n, q)	{ ZTI_MODE_FIXED, (n), (q) }
#define	ZTI_PCT(n)	{ ZTI_MODE_ONLINE_PERCENT, (n), 1 }
#define	ZTI_SCALE	{ ZTI_MODE_SCALE, 0, 1 }
#define	ZTI_SYNC	{ ZTI_MODE_SYNC, 0, 1 }
#define	ZTI_NULL	{ ZTI_MODE_NULL, 0, 0 }

#define	ZTI_N(n)	ZTI_P(n, 1)
#define	ZTI_ONE		ZTI_N(1)

typedef struct zio_taskq_info {
	zti_modes_t zti_mode;
	uint_t zti_value;
	uint_t zti_count;
} zio_taskq_info_t;

static const char *const zio_taskq_types[ZIO_TASKQ_TYPES] = {
	"iss", "iss_h", "int", "int_h"
};

/*
 * This table defines the taskq settings for each ZFS I/O type. When
 * initializing a pool, we use this table to create an appropriately sized
 * taskq. Some operations are low volume and therefore have a small, static
 * number of threads assigned to their taskqs using the ZTI_N(#) or ZTI_ONE
 * macros. Other operations process a large amount of data; the ZTI_SCALE
 * macro causes us to create a taskq oriented for throughput. Some operations
 * are so high frequency and short-lived that the taskq itself can become a
 * point of lock contention. The ZTI_P(#, #) macro indicates that we need an
 * additional degree of parallelism specified by the number of threads per-
 * taskq and the number of taskqs; when dispatching an event in this case, the
 * particular taskq is chosen at random. ZTI_SCALE uses a number of taskqs
 * that scales with the number of CPUs.
 *
 * The different taskq priorities are to handle the different contexts (issue
 * and interrupt) and then to reserve threads for ZIO_PRIORITY_NOW I/Os that
 * need to be handled with minimum delay.
 */
static zio_taskq_info_t zio_taskqs[ZIO_TYPES][ZIO_TASKQ_TYPES] = {
	/* ISSUE	ISSUE_HIGH	INTR		INTR_HIGH */
	{ ZTI_ONE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* NULL */
	{ ZTI_N(8),	ZTI_NULL,	ZTI_SCALE,	ZTI_NULL }, /* READ */
	{ ZTI_SYNC,	ZTI_N(5),	ZTI_SCALE,	ZTI_N(5) }, /* WRITE */
	{ ZTI_SCALE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* FREE */
	{ ZTI_ONE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* CLAIM */
	{ ZTI_ONE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* FLUSH */
	{ ZTI_N(4),	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* TRIM */
};

static void spa_sync_version(void *arg, dmu_tx_t *tx);
static void spa_sync_props(void *arg, dmu_tx_t *tx);
static boolean_t spa_has_active_shared_spare(spa_t *spa);
static int spa_load_impl(spa_t *spa, spa_import_type_t type,
    const char **ereport);
static void spa_vdev_resilver_done(spa_t *spa);

/*
 * Percentage of all CPUs that can be used by the metaslab preload taskq.
 */
static uint_t metaslab_preload_pct = 50;

static uint_t	zio_taskq_batch_pct = 80;	  /* 1 thread per cpu in pset */
static uint_t	zio_taskq_batch_tpq;		  /* threads per taskq */

#ifdef HAVE_SYSDC
static const boolean_t	zio_taskq_sysdc = B_TRUE; /* use SDC scheduling class */
static const uint_t	zio_taskq_basedc = 80;	  /* base duty cycle */
#endif

#ifdef HAVE_SPA_THREAD
static const boolean_t spa_create_process = B_TRUE; /* no process => no sysdc */
#endif

static uint_t	zio_taskq_wr_iss_ncpus = 0;

/*
 * Report any spa_load_verify errors found, but do not fail spa_load.
 * This is used by zdb to analyze non-idle pools.
 */
boolean_t	spa_load_verify_dryrun = B_FALSE;

/*
 * Allow read spacemaps in case of readonly import (spa_mode == SPA_MODE_READ).
 * This is used by zdb for spacemaps verification.
 */
boolean_t	spa_mode_readable_spacemaps = B_FALSE;

/*
 * This (illegal) pool name is used when temporarily importing a spa_t in order
 * to get the vdev stats associated with the imported devices.
 */
#define	TRYIMPORT_NAME	"$import"

/*
 * For debugging purposes: print out vdev tree during pool import.
 */
static int		spa_load_print_vdev_tree = B_FALSE;

/*
 * A non-zero value for zfs_max_missing_tvds means that we allow importing
 * pools with missing top-level vdevs. This is strictly intended for advanced
 * pool recovery cases since missing data is almost inevitable. Pools with
 * missing devices can only be imported read-only for safety reasons, and their
 * fail-mode will be automatically set to "continue".
 *
 * With 1 missing vdev we should be able to import the pool and mount all
 * datasets. User data that was not modified after the missing device has been
 * added should be recoverable. This means that snapshots created prior to the
 * addition of that device should be completely intact.
 *
 * With 2 missing vdevs, some datasets may fail to mount since there are
 * dataset statistics that are stored as regular metadata. Some data might be
 * recoverable if those vdevs were added recently.
 *
 * With 3 or more missing vdevs, the pool is severely damaged and MOS entries
 * may be missing entirely. Chances of data recovery are very low. Note that
 * there are also risks of performing an inadvertent rewind as we might be
 * missing all the vdevs with the latest uberblocks.
 */
uint64_t	zfs_max_missing_tvds = 0;

/*
 * The parameters below are similar to zfs_max_missing_tvds but are only
 * intended for a preliminary open of the pool with an untrusted config which
 * might be incomplete or out-dated.
 *
 * We are more tolerant for pools opened from a cachefile since we could have
 * an out-dated cachefile where a device removal was not registered.
 * We could have set the limit arbitrarily high but in the case where devices
 * are really missing we would want to return the proper error codes; we chose
 * SPA_DVAS_PER_BP - 1 so that some copies of the MOS would still be available
 * and we get a chance to retrieve the trusted config.
 */
uint64_t	zfs_max_missing_tvds_cachefile = SPA_DVAS_PER_BP - 1;

/*
 * In the case where config was assembled by scanning device paths (/dev/dsks
 * by default) we are less tolerant since all the existing devices should have
 * been detected and we want spa_load to return the right error codes.
 */
uint64_t	zfs_max_missing_tvds_scan = 0;

/*
 * Debugging aid that pauses spa_sync() towards the end.
 */
static const boolean_t	zfs_pause_spa_sync = B_FALSE;

/*
 * Variables to indicate the livelist condense zthr func should wait at certain
 * points for the livelist to be removed - used to test condense/destroy races
 */
static int zfs_livelist_condense_zthr_pause = 0;
static int zfs_livelist_condense_sync_pause = 0;

/*
 * Variables to track whether or not condense cancellation has been
 * triggered in testing.
 */
static int zfs_livelist_condense_sync_cancel = 0;
static int zfs_livelist_condense_zthr_cancel = 0;

/*
 * Variable to track whether or not extra ALLOC blkptrs were added to a
 * livelist entry while it was being condensed (caused by the way we track
 * remapped blkptrs in dbuf_remap_impl)
 */
static int zfs_livelist_condense_new_alloc = 0;

/*
 * ==========================================================================
 * SPA properties routines
 * ==========================================================================
 */

/*
 * Add a (source=src, propname=propval) list to an nvlist.
 */
static void
spa_prop_add_list(nvlist_t *nvl, zpool_prop_t prop, const char *strval,
    uint64_t intval, zprop_source_t src)
{
	const char *propname = zpool_prop_to_name(prop);
	nvlist_t *propval;

	propval = fnvlist_alloc();
	fnvlist_add_uint64(propval, ZPROP_SOURCE, src);

	if (strval != NULL)
		fnvlist_add_string(propval, ZPROP_VALUE, strval);
	else
		fnvlist_add_uint64(propval, ZPROP_VALUE, intval);

	fnvlist_add_nvlist(nvl, propname, propval);
	nvlist_free(propval);
}

/*
 * Add a user property (source=src, propname=propval) to an nvlist.
 */
static void
spa_prop_add_user(nvlist_t *nvl, const char *propname, char *strval,
    zprop_source_t src)
{
	nvlist_t *propval;

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_SOURCE, src) == 0);
	VERIFY(nvlist_add_string(propval, ZPROP_VALUE, strval) == 0);
	VERIFY(nvlist_add_nvlist(nvl, propname, propval) == 0);
	nvlist_free(propval);
}

/*
 * Get property values from the spa configuration.
 */
static void
spa_prop_get_config(spa_t *spa, nvlist_t **nvp)
{
	vdev_t *rvd = spa->spa_root_vdev;
	dsl_pool_t *pool = spa->spa_dsl_pool;
	uint64_t size, alloc, cap, version;
	const zprop_source_t src = ZPROP_SRC_NONE;
	spa_config_dirent_t *dp;
	metaslab_class_t *mc = spa_normal_class(spa);

	ASSERT(MUTEX_HELD(&spa->spa_props_lock));

	if (rvd != NULL) {
		alloc = metaslab_class_get_alloc(mc);
		alloc += metaslab_class_get_alloc(spa_special_class(spa));
		alloc += metaslab_class_get_alloc(spa_dedup_class(spa));
		alloc += metaslab_class_get_alloc(spa_embedded_log_class(spa));

		size = metaslab_class_get_space(mc);
		size += metaslab_class_get_space(spa_special_class(spa));
		size += metaslab_class_get_space(spa_dedup_class(spa));
		size += metaslab_class_get_space(spa_embedded_log_class(spa));

		spa_prop_add_list(*nvp, ZPOOL_PROP_NAME, spa_name(spa), 0, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_SIZE, NULL, size, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_ALLOCATED, NULL, alloc, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_FREE, NULL,
		    size - alloc, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_CHECKPOINT, NULL,
		    spa->spa_checkpoint_info.sci_dspace, src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_FRAGMENTATION, NULL,
		    metaslab_class_fragmentation(mc), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_EXPANDSZ, NULL,
		    metaslab_class_expandable_space(mc), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_READONLY, NULL,
		    (spa_mode(spa) == SPA_MODE_READ), src);

		cap = (size == 0) ? 0 : (alloc * 100 / size);
		spa_prop_add_list(*nvp, ZPOOL_PROP_CAPACITY, NULL, cap, src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_DEDUPRATIO, NULL,
		    ddt_get_pool_dedup_ratio(spa), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_BCLONEUSED, NULL,
		    brt_get_used(spa), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_BCLONESAVED, NULL,
		    brt_get_saved(spa), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_BCLONERATIO, NULL,
		    brt_get_ratio(spa), src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_HEALTH, NULL,
		    rvd->vdev_state, src);

		version = spa_version(spa);
		if (version == zpool_prop_default_numeric(ZPOOL_PROP_VERSION)) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_VERSION, NULL,
			    version, ZPROP_SRC_DEFAULT);
		} else {
			spa_prop_add_list(*nvp, ZPOOL_PROP_VERSION, NULL,
			    version, ZPROP_SRC_LOCAL);
		}
		spa_prop_add_list(*nvp, ZPOOL_PROP_LOAD_GUID,
		    NULL, spa_load_guid(spa), src);
	}

	if (pool != NULL) {
		/*
		 * The $FREE directory was introduced in SPA_VERSION_DEADLISTS,
		 * when opening pools before this version freedir will be NULL.
		 */
		if (pool->dp_free_dir != NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_FREEING, NULL,
			    dsl_dir_phys(pool->dp_free_dir)->dd_used_bytes,
			    src);
		} else {
			spa_prop_add_list(*nvp, ZPOOL_PROP_FREEING,
			    NULL, 0, src);
		}

		if (pool->dp_leak_dir != NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_LEAKED, NULL,
			    dsl_dir_phys(pool->dp_leak_dir)->dd_used_bytes,
			    src);
		} else {
			spa_prop_add_list(*nvp, ZPOOL_PROP_LEAKED,
			    NULL, 0, src);
		}
	}

	spa_prop_add_list(*nvp, ZPOOL_PROP_GUID, NULL, spa_guid(spa), src);

	if (spa->spa_comment != NULL) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_COMMENT, spa->spa_comment,
		    0, ZPROP_SRC_LOCAL);
	}

	if (spa->spa_compatibility != NULL) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_COMPATIBILITY,
		    spa->spa_compatibility, 0, ZPROP_SRC_LOCAL);
	}

	if (spa->spa_root != NULL)
		spa_prop_add_list(*nvp, ZPOOL_PROP_ALTROOT, spa->spa_root,
		    0, ZPROP_SRC_LOCAL);

	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_BLOCKS)) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXBLOCKSIZE, NULL,
		    MIN(zfs_max_recordsize, SPA_MAXBLOCKSIZE), ZPROP_SRC_NONE);
	} else {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXBLOCKSIZE, NULL,
		    SPA_OLD_MAXBLOCKSIZE, ZPROP_SRC_NONE);
	}

	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_DNODE)) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXDNODESIZE, NULL,
		    DNODE_MAX_SIZE, ZPROP_SRC_NONE);
	} else {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXDNODESIZE, NULL,
		    DNODE_MIN_SIZE, ZPROP_SRC_NONE);
	}

	if ((dp = list_head(&spa->spa_config_list)) != NULL) {
		if (dp->scd_path == NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_CACHEFILE,
			    "none", 0, ZPROP_SRC_LOCAL);
		} else if (strcmp(dp->scd_path, spa_config_path) != 0) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_CACHEFILE,
			    dp->scd_path, 0, ZPROP_SRC_LOCAL);
		}
	}
}

/*
 * Get zpool property values.
 */
int
spa_prop_get(spa_t *spa, nvlist_t **nvp)
{
	objset_t *mos = spa->spa_meta_objset;
	zap_cursor_t zc;
	zap_attribute_t za;
	dsl_pool_t *dp;
	int err;

	err = nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP);
	if (err)
		return (err);

	dp = spa_get_dsl(spa);
	dsl_pool_config_enter(dp, FTAG);
	mutex_enter(&spa->spa_props_lock);

	/*
	 * Get properties from the spa config.
	 */
	spa_prop_get_config(spa, nvp);

	/* If no pool property object, no more prop to get. */
	if (mos == NULL || spa->spa_pool_props_object == 0)
		goto out;

	/*
	 * Get properties from the MOS pool property object.
	 */
	for (zap_cursor_init(&zc, mos, spa->spa_pool_props_object);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t intval = 0;
		char *strval = NULL;
		zprop_source_t src = ZPROP_SRC_DEFAULT;
		zpool_prop_t prop;

		if ((prop = zpool_name_to_prop(za.za_name)) ==
		    ZPOOL_PROP_INVAL && !zfs_prop_user(za.za_name))
			continue;

		switch (za.za_integer_length) {
		case 8:
			/* integer property */
			if (za.za_first_integer !=
			    zpool_prop_default_numeric(prop))
				src = ZPROP_SRC_LOCAL;

			if (prop == ZPOOL_PROP_BOOTFS) {
				dsl_dataset_t *ds = NULL;

				err = dsl_dataset_hold_obj(dp,
				    za.za_first_integer, FTAG, &ds);
				if (err != 0)
					break;

				strval = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN,
				    KM_SLEEP);
				dsl_dataset_name(ds, strval);
				dsl_dataset_rele(ds, FTAG);
			} else {
				strval = NULL;
				intval = za.za_first_integer;
			}

			spa_prop_add_list(*nvp, prop, strval, intval, src);

			if (strval != NULL)
				kmem_free(strval, ZFS_MAX_DATASET_NAME_LEN);

			break;

		case 1:
			/* string property */
			strval = kmem_alloc(za.za_num_integers, KM_SLEEP);
			err = zap_lookup(mos, spa->spa_pool_props_object,
			    za.za_name, 1, za.za_num_integers, strval);
			if (err) {
				kmem_free(strval, za.za_num_integers);
				break;
			}
			if (prop != ZPOOL_PROP_INVAL) {
				spa_prop_add_list(*nvp, prop, strval, 0, src);
			} else {
				src = ZPROP_SRC_LOCAL;
				spa_prop_add_user(*nvp, za.za_name, strval,
				    src);
			}
			kmem_free(strval, za.za_num_integers);
			break;

		default:
			break;
		}
	}
	zap_cursor_fini(&zc);
out:
	mutex_exit(&spa->spa_props_lock);
	dsl_pool_config_exit(dp, FTAG);
	if (err && err != ENOENT) {
		nvlist_free(*nvp);
		*nvp = NULL;
		return (err);
	}

	return (0);
}

/*
 * Validate the given pool properties nvlist and modify the list
 * for the property values to be set.
 */
static int
spa_prop_validate(spa_t *spa, nvlist_t *props)
{
	nvpair_t *elem;
	int error = 0, reset_bootfs = 0;
	uint64_t objnum = 0;
	boolean_t has_feature = B_FALSE;

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		uint64_t intval;
		const char *strval, *slash, *check, *fname;
		const char *propname = nvpair_name(elem);
		zpool_prop_t prop = zpool_name_to_prop(propname);

		switch (prop) {
		case ZPOOL_PROP_INVAL:
			/*
			 * Sanitize the input.
			 */
			if (zfs_prop_user(propname)) {
				if (strlen(propname) >= ZAP_MAXNAMELEN) {
					error = SET_ERROR(ENAMETOOLONG);
					break;
				}

				if (strlen(fnvpair_value_string(elem)) >=
				    ZAP_MAXVALUELEN) {
					error = SET_ERROR(E2BIG);
					break;
				}
			} else if (zpool_prop_feature(propname)) {
				if (nvpair_type(elem) != DATA_TYPE_UINT64) {
					error = SET_ERROR(EINVAL);
					break;
				}

				if (nvpair_value_uint64(elem, &intval) != 0) {
					error = SET_ERROR(EINVAL);
					break;
				}

				if (intval != 0) {
					error = SET_ERROR(EINVAL);
					break;
				}

				fname = strchr(propname, '@') + 1;
				if (zfeature_lookup_name(fname, NULL) != 0) {
					error = SET_ERROR(EINVAL);
					break;
				}

				has_feature = B_TRUE;
			} else {
				error = SET_ERROR(EINVAL);
				break;
			}
			break;

		case ZPOOL_PROP_VERSION:
			error = nvpair_value_uint64(elem, &intval);
			if (!error &&
			    (intval < spa_version(spa) ||
			    intval > SPA_VERSION_BEFORE_FEATURES ||
			    has_feature))
				error = SET_ERROR(EINVAL);
			break;

		case ZPOOL_PROP_DELEGATION:
		case ZPOOL_PROP_AUTOREPLACE:
		case ZPOOL_PROP_LISTSNAPS:
		case ZPOOL_PROP_AUTOEXPAND:
		case ZPOOL_PROP_AUTOTRIM:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && intval > 1)
				error = SET_ERROR(EINVAL);
			break;

		case ZPOOL_PROP_MULTIHOST:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && intval > 1)
				error = SET_ERROR(EINVAL);

			if (!error) {
				uint32_t hostid = zone_get_hostid(NULL);
				if (hostid)
					spa->spa_hostid = hostid;
				else
					error = SET_ERROR(ENOTSUP);
			}

			break;

		case ZPOOL_PROP_BOOTFS:
			/*
			 * If the pool version is less than SPA_VERSION_BOOTFS,
			 * or the pool is still being created (version == 0),
			 * the bootfs property cannot be set.
			 */
			if (spa_version(spa) < SPA_VERSION_BOOTFS) {
				error = SET_ERROR(ENOTSUP);
				break;
			}

			/*
			 * Make sure the vdev config is bootable
			 */
			if (!vdev_is_bootable(spa->spa_root_vdev)) {
				error = SET_ERROR(ENOTSUP);
				break;
			}

			reset_bootfs = 1;

			error = nvpair_value_string(elem, &strval);

			if (!error) {
				objset_t *os;

				if (strval == NULL || strval[0] == '\0') {
					objnum = zpool_prop_default_numeric(
					    ZPOOL_PROP_BOOTFS);
					break;
				}

				error = dmu_objset_hold(strval, FTAG, &os);
				if (error != 0)
					break;

				/* Must be ZPL. */
				if (dmu_objset_type(os) != DMU_OST_ZFS) {
					error = SET_ERROR(ENOTSUP);
				} else {
					objnum = dmu_objset_id(os);
				}
				dmu_objset_rele(os, FTAG);
			}
			break;

		case ZPOOL_PROP_FAILUREMODE:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && intval > ZIO_FAILURE_MODE_PANIC)
				error = SET_ERROR(EINVAL);

			/*
			 * This is a special case which only occurs when
			 * the pool has completely failed. This allows
			 * the user to change the in-core failmode property
			 * without syncing it out to disk (I/Os might
			 * currently be blocked). We do this by returning
			 * EIO to the caller (spa_prop_set) to trick it
			 * into thinking we encountered a property validation
			 * error.
			 */
			if (!error && spa_suspended(spa)) {
				spa->spa_failmode = intval;
				error = SET_ERROR(EIO);
			}
			break;

		case ZPOOL_PROP_CACHEFILE:
			if ((error = nvpair_value_string(elem, &strval)) != 0)
				break;

			if (strval[0] == '\0')
				break;

			if (strcmp(strval, "none") == 0)
				break;

			if (strval[0] != '/') {
				error = SET_ERROR(EINVAL);
				break;
			}

			slash = strrchr(strval, '/');
			ASSERT(slash != NULL);

			if (slash[1] == '\0' || strcmp(slash, "/.") == 0 ||
			    strcmp(slash, "/..") == 0)
				error = SET_ERROR(EINVAL);
			break;

		case ZPOOL_PROP_COMMENT:
			if ((error = nvpair_value_string(elem, &strval)) != 0)
				break;
			for (check = strval; *check != '\0'; check++) {
				if (!isprint(*check)) {
					error = SET_ERROR(EINVAL);
					break;
				}
			}
			if (strlen(strval) > ZPROP_MAX_COMMENT)
				error = SET_ERROR(E2BIG);
			break;

		default:
			break;
		}

		if (error)
			break;
	}

	(void) nvlist_remove_all(props,
	    zpool_prop_to_name(ZPOOL_PROP_DEDUPDITTO));

	if (!error && reset_bootfs) {
		error = nvlist_remove(props,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), DATA_TYPE_STRING);

		if (!error) {
			error = nvlist_add_uint64(props,
			    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), objnum);
		}
	}

	return (error);
}

void
spa_configfile_set(spa_t *spa, nvlist_t *nvp, boolean_t need_sync)
{
	const char *cachefile;
	spa_config_dirent_t *dp;

	if (nvlist_lookup_string(nvp, zpool_prop_to_name(ZPOOL_PROP_CACHEFILE),
	    &cachefile) != 0)
		return;

	dp = kmem_alloc(sizeof (spa_config_dirent_t),
	    KM_SLEEP);

	if (cachefile[0] == '\0')
		dp->scd_path = spa_strdup(spa_config_path);
	else if (strcmp(cachefile, "none") == 0)
		dp->scd_path = NULL;
	else
		dp->scd_path = spa_strdup(cachefile);

	list_insert_head(&spa->spa_config_list, dp);
	if (need_sync)
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
}

int
spa_prop_set(spa_t *spa, nvlist_t *nvp)
{
	int error;
	nvpair_t *elem = NULL;
	boolean_t need_sync = B_FALSE;

	if ((error = spa_prop_validate(spa, nvp)) != 0)
		return (error);

	while ((elem = nvlist_next_nvpair(nvp, elem)) != NULL) {
		zpool_prop_t prop = zpool_name_to_prop(nvpair_name(elem));

		if (prop == ZPOOL_PROP_CACHEFILE ||
		    prop == ZPOOL_PROP_ALTROOT ||
		    prop == ZPOOL_PROP_READONLY)
			continue;

		if (prop == ZPOOL_PROP_INVAL &&
		    zfs_prop_user(nvpair_name(elem))) {
			need_sync = B_TRUE;
			break;
		}

		if (prop == ZPOOL_PROP_VERSION || prop == ZPOOL_PROP_INVAL) {
			uint64_t ver = 0;

			if (prop == ZPOOL_PROP_VERSION) {
				VERIFY(nvpair_value_uint64(elem, &ver) == 0);
			} else {
				ASSERT(zpool_prop_feature(nvpair_name(elem)));
				ver = SPA_VERSION_FEATURES;
				need_sync = B_TRUE;
			}

			/* Save time if the version is already set. */
			if (ver == spa_version(spa))
				continue;

			/*
			 * In addition to the pool directory object, we might
			 * create the pool properties object, the features for
			 * read object, the features for write object, or the
			 * feature descriptions object.
			 */
			error = dsl_sync_task(spa->spa_name, NULL,
			    spa_sync_version, &ver,
			    6, ZFS_SPACE_CHECK_RESERVED);
			if (error)
				return (error);
			continue;
		}

		need_sync = B_TRUE;
		break;
	}

	if (need_sync) {
		return (dsl_sync_task(spa->spa_name, NULL, spa_sync_props,
		    nvp, 6, ZFS_SPACE_CHECK_RESERVED));
	}

	return (0);
}

/*
 * If the bootfs property value is dsobj, clear it.
 */
void
spa_prop_clear_bootfs(spa_t *spa, uint64_t dsobj, dmu_tx_t *tx)
{
	if (spa->spa_bootfs == dsobj && spa->spa_pool_props_object != 0) {
		VERIFY(zap_remove(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), tx) == 0);
		spa->spa_bootfs = 0;
	}
}

static int
spa_change_guid_check(void *arg, dmu_tx_t *tx)
{
	uint64_t *newguid __maybe_unused = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t vdev_state;

	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		int error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (SET_ERROR(error));
	}

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	vdev_state = rvd->vdev_state;
	spa_config_exit(spa, SCL_STATE, FTAG);

	if (vdev_state != VDEV_STATE_HEALTHY)
		return (SET_ERROR(ENXIO));

	ASSERT3U(spa_guid(spa), !=, *newguid);

	return (0);
}

static void
spa_change_guid_sync(void *arg, dmu_tx_t *tx)
{
	uint64_t *newguid = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	uint64_t oldguid;
	vdev_t *rvd = spa->spa_root_vdev;

	oldguid = spa_guid(spa);

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	rvd->vdev_guid = *newguid;
	rvd->vdev_guid_sum += (*newguid - oldguid);
	vdev_config_dirty(rvd);
	spa_config_exit(spa, SCL_STATE, FTAG);

	spa_history_log_internal(spa, "guid change", tx, "old=%llu new=%llu",
	    (u_longlong_t)oldguid, (u_longlong_t)*newguid);
}

/*
 * Change the GUID for the pool.  This is done so that we can later
 * re-import a pool built from a clone of our own vdevs.  We will modify
 * the root vdev's guid, our own pool guid, and then mark all of our
 * vdevs dirty.  Note that we must make sure that all our vdevs are
 * online when we do this, or else any vdevs that weren't present
 * would be orphaned from our pool.  We are also going to issue a
 * sysevent to update any watchers.
 */
int
spa_change_guid(spa_t *spa)
{
	int error;
	uint64_t guid;

	mutex_enter(&spa->spa_vdev_top_lock);
	mutex_enter(&spa_namespace_lock);
	guid = spa_generate_guid(NULL);

	error = dsl_sync_task(spa->spa_name, spa_change_guid_check,
	    spa_change_guid_sync, &guid, 5, ZFS_SPACE_CHECK_RESERVED);

	if (error == 0) {
		/*
		 * Clear the kobj flag from all the vdevs to allow
		 * vdev_cache_process_kobj_evt() to post events to all the
		 * vdevs since GUID is updated.
		 */
		vdev_clear_kobj_evt(spa->spa_root_vdev);
		for (int i = 0; i < spa->spa_l2cache.sav_count; i++)
			vdev_clear_kobj_evt(spa->spa_l2cache.sav_vdevs[i]);

		spa_write_cachefile(spa, B_FALSE, B_TRUE, B_TRUE);
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_REGUID);
	}

	mutex_exit(&spa_namespace_lock);
	mutex_exit(&spa->spa_vdev_top_lock);

	return (error);
}

/*
 * ==========================================================================
 * SPA state manipulation (open/create/destroy/import/export)
 * ==========================================================================
 */

static int
spa_error_entry_compare(const void *a, const void *b)
{
	const spa_error_entry_t *sa = (const spa_error_entry_t *)a;
	const spa_error_entry_t *sb = (const spa_error_entry_t *)b;
	int ret;

	ret = memcmp(&sa->se_bookmark, &sb->se_bookmark,
	    sizeof (zbookmark_phys_t));

	return (TREE_ISIGN(ret));
}

/*
 * Utility function which retrieves copies of the current logs and
 * re-initializes them in the process.
 */
void
spa_get_errlists(spa_t *spa, avl_tree_t *last, avl_tree_t *scrub)
{
	ASSERT(MUTEX_HELD(&spa->spa_errlist_lock));

	memcpy(last, &spa->spa_errlist_last, sizeof (avl_tree_t));
	memcpy(scrub, &spa->spa_errlist_scrub, sizeof (avl_tree_t));

	avl_create(&spa->spa_errlist_scrub,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_last,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
}

static void
spa_taskqs_init(spa_t *spa, zio_type_t t, zio_taskq_type_t q)
{
	const zio_taskq_info_t *ztip = &zio_taskqs[t][q];
	enum zti_modes mode = ztip->zti_mode;
	uint_t value = ztip->zti_value;
	uint_t count = ztip->zti_count;
	spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];
	uint_t cpus, flags = TASKQ_DYNAMIC;

	switch (mode) {
	case ZTI_MODE_FIXED:
		ASSERT3U(value, >, 0);
		break;

	case ZTI_MODE_SYNC:

		/*
		 * Create one wr_iss taskq for every 'zio_taskq_wr_iss_ncpus',
		 * not to exceed the number of spa allocators.
		 */
		if (zio_taskq_wr_iss_ncpus == 0) {
			count = MAX(boot_ncpus / spa->spa_alloc_count, 1);
		} else {
			count = MAX(1,
			    boot_ncpus / MAX(1, zio_taskq_wr_iss_ncpus));
		}
		count = MAX(count, (zio_taskq_batch_pct + 99) / 100);
		count = MIN(count, spa->spa_alloc_count);

		/*
		 * zio_taskq_batch_pct is unbounded and may exceed 100%, but no
		 * single taskq may have more threads than 100% of online cpus.
		 */
		value = (zio_taskq_batch_pct + count / 2) / count;
		value = MIN(value, 100);
		flags |= TASKQ_THREADS_CPU_PCT;
		break;

	case ZTI_MODE_SCALE:
		flags |= TASKQ_THREADS_CPU_PCT;
		/*
		 * We want more taskqs to reduce lock contention, but we want
		 * less for better request ordering and CPU utilization.
		 */
		cpus = MAX(1, boot_ncpus * zio_taskq_batch_pct / 100);
		if (zio_taskq_batch_tpq > 0) {
			count = MAX(1, (cpus + zio_taskq_batch_tpq / 2) /
			    zio_taskq_batch_tpq);
		} else {
			/*
			 * Prefer 6 threads per taskq, but no more taskqs
			 * than threads in them on large systems. For 80%:
			 *
			 *                 taskq   taskq   total
			 * cpus    taskqs  percent threads threads
			 * ------- ------- ------- ------- -------
			 * 1       1       80%     1       1
			 * 2       1       80%     1       1
			 * 4       1       80%     3       3
			 * 8       2       40%     3       6
			 * 16      3       27%     4       12
			 * 32      5       16%     5       25
			 * 64      7       11%     7       49
			 * 128     10      8%      10      100
			 * 256     14      6%      15      210
			 */
			count = 1 + cpus / 6;
			while (count * count > cpus)
				count--;
		}
		/* Limit each taskq within 100% to not trigger assertion. */
		count = MAX(count, (zio_taskq_batch_pct + 99) / 100);
		value = (zio_taskq_batch_pct + count / 2) / count;
		break;

	case ZTI_MODE_NULL:
		tqs->stqs_count = 0;
		tqs->stqs_taskq = NULL;
		return;

	default:
		panic("unrecognized mode for %s_%s taskq (%u:%u) in "
		    "spa_taskqs_init()",
		    zio_type_name[t], zio_taskq_types[q], mode, value);
		break;
	}

	ASSERT3U(count, >, 0);
	tqs->stqs_count = count;
	tqs->stqs_taskq = kmem_alloc(count * sizeof (taskq_t *), KM_SLEEP);

	for (uint_t i = 0; i < count; i++) {
		taskq_t *tq;
		char name[32];

		if (count > 1)
			(void) snprintf(name, sizeof (name), "%s_%s_%u",
			    zio_type_name[t], zio_taskq_types[q], i);
		else
			(void) snprintf(name, sizeof (name), "%s_%s",
			    zio_type_name[t], zio_taskq_types[q]);

#ifdef HAVE_SYSDC
		if (zio_taskq_sysdc && spa->spa_proc != &p0) {
			(void) zio_taskq_basedc;
			tq = taskq_create_sysdc(name, value, 50, INT_MAX,
			    spa->spa_proc, zio_taskq_basedc, flags);
		} else {
#endif
			pri_t pri = maxclsyspri;
			/*
			 * The write issue taskq can be extremely CPU
			 * intensive.  Run it at slightly less important
			 * priority than the other taskqs.
			 *
			 * Under Linux and FreeBSD this means incrementing
			 * the priority value as opposed to platforms like
			 * illumos where it should be decremented.
			 *
			 * On FreeBSD, if priorities divided by four (RQ_PPQ)
			 * are equal then a difference between them is
			 * insignificant.
			 */
			if (t == ZIO_TYPE_WRITE && q == ZIO_TASKQ_ISSUE) {
#if defined(__linux__)
				pri++;
#elif defined(__FreeBSD__)
				pri += 4;
#else
#error "unknown OS"
#endif
			}
			tq = taskq_create_proc(name, value, pri, 50,
			    INT_MAX, spa->spa_proc, flags);
#ifdef HAVE_SYSDC
		}
#endif

		tqs->stqs_taskq[i] = tq;
	}
}

static void
spa_taskqs_fini(spa_t *spa, zio_type_t t, zio_taskq_type_t q)
{
	spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];

	if (tqs->stqs_taskq == NULL) {
		ASSERT3U(tqs->stqs_count, ==, 0);
		return;
	}

	for (uint_t i = 0; i < tqs->stqs_count; i++) {
		ASSERT3P(tqs->stqs_taskq[i], !=, NULL);
		taskq_destroy(tqs->stqs_taskq[i]);
	}

	kmem_free(tqs->stqs_taskq, tqs->stqs_count * sizeof (taskq_t *));
	tqs->stqs_taskq = NULL;
}

#ifdef _KERNEL
/*
 * The READ and WRITE rows of zio_taskqs are configurable at module load time
 * by setting zio_taskq_read or zio_taskq_write.
 *
 * Example (the defaults for READ and WRITE)
 *   zio_taskq_read='fixed,1,8 null scale null'
 *   zio_taskq_write='sync fixed,1,5 scale fixed,1,5'
 *
 * Each sets the entire row at a time.
 *
 * 'fixed' is parameterised: fixed,Q,T where Q is number of taskqs, T is number
 * of threads per taskq.
 *
 * 'null' can only be set on the high-priority queues (queue selection for
 * high-priority queues will fall back to the regular queue if the high-pri
 * is NULL.
 */
static const char *const modes[ZTI_NMODES] = {
	"fixed", "scale", "sync", "null"
};

/* Parse the incoming config string. Modifies cfg */
static int
spa_taskq_param_set(zio_type_t t, char *cfg)
{
	int err = 0;

	zio_taskq_info_t row[ZIO_TASKQ_TYPES] = {{0}};

	char *next = cfg, *tok, *c;

	/*
	 * Parse out each element from the string and fill `row`. The entire
	 * row has to be set at once, so any errors are flagged by just
	 * breaking out of this loop early.
	 */
	uint_t q;
	for (q = 0; q < ZIO_TASKQ_TYPES; q++) {
		/* `next` is the start of the config */
		if (next == NULL)
			break;

		/* Eat up leading space */
		while (isspace(*next))
			next++;
		if (*next == '\0')
			break;

		/* Mode ends at space or end of string */
		tok = next;
		next = strchr(tok, ' ');
		if (next != NULL) *next++ = '\0';

		/* Parameters start after a comma */
		c = strchr(tok, ',');
		if (c != NULL) *c++ = '\0';

		/* Match mode string */
		uint_t mode;
		for (mode = 0; mode < ZTI_NMODES; mode++)
			if (strcmp(tok, modes[mode]) == 0)
				break;
		if (mode == ZTI_NMODES)
			break;

		/* Invalid canary */
		row[q].zti_mode = ZTI_NMODES;

		/* Per-mode setup */
		switch (mode) {

		/*
		 * FIXED is parameterised: number of queues, and number of
		 * threads per queue.
		 */
		case ZTI_MODE_FIXED: {
			/* No parameters? */
			if (c == NULL || *c == '\0')
				break;

			/* Find next parameter */
			tok = c;
			c = strchr(tok, ',');
			if (c == NULL)
				break;

			/* Take digits and convert */
			unsigned long long nq;
			if (!(isdigit(*tok)))
				break;
			err = ddi_strtoull(tok, &tok, 10, &nq);
			/* Must succeed and also end at the next param sep */
			if (err != 0 || tok != c)
				break;

			/* Move past the comma */
			tok++;
			/* Need another number */
			if (!(isdigit(*tok)))
				break;
			/* Remember start to make sure we moved */
			c = tok;

			/* Take digits */
			unsigned long long ntpq;
			err = ddi_strtoull(tok, &tok, 10, &ntpq);
			/* Must succeed, and moved forward */
			if (err != 0 || tok == c || *tok != '\0')
				break;

			/*
			 * sanity; zero queues/threads make no sense, and
			 * 16K is almost certainly more than anyone will ever
			 * need and avoids silly numbers like UINT32_MAX
			 */
			if (nq == 0 || nq >= 16384 ||
			    ntpq == 0 || ntpq >= 16384)
				break;

			const zio_taskq_info_t zti = ZTI_P(ntpq, nq);
			row[q] = zti;
			break;
		}

		case ZTI_MODE_SCALE: {
			const zio_taskq_info_t zti = ZTI_SCALE;
			row[q] = zti;
			break;
		}

		case ZTI_MODE_SYNC: {
			const zio_taskq_info_t zti = ZTI_SYNC;
			row[q] = zti;
			break;
		}

		case ZTI_MODE_NULL: {
			/*
			 * Can only null the high-priority queues; the general-
			 * purpose ones have to exist.
			 */
			if (q != ZIO_TASKQ_ISSUE_HIGH &&
			    q != ZIO_TASKQ_INTERRUPT_HIGH)
				break;

			const zio_taskq_info_t zti = ZTI_NULL;
			row[q] = zti;
			break;
		}

		default:
			break;
		}

		/* Ensure we set a mode */
		if (row[q].zti_mode == ZTI_NMODES)
			break;
	}

	/* Didn't get a full row, fail */
	if (q < ZIO_TASKQ_TYPES)
		return (SET_ERROR(EINVAL));

	/* Eat trailing space */
	if (next != NULL)
		while (isspace(*next))
			next++;

	/* If there's anything left over then fail */
	if (next != NULL && *next != '\0')
		return (SET_ERROR(EINVAL));

	/* Success! Copy it into the real config */
	for (q = 0; q < ZIO_TASKQ_TYPES; q++)
		zio_taskqs[t][q] = row[q];

	return (0);
}

static int
spa_taskq_param_get(zio_type_t t, char *buf, boolean_t add_newline)
{
	int pos = 0;

	/* Build paramater string from live config */
	const char *sep = "";
	for (uint_t q = 0; q < ZIO_TASKQ_TYPES; q++) {
		const zio_taskq_info_t *zti = &zio_taskqs[t][q];
		if (zti->zti_mode == ZTI_MODE_FIXED)
			pos += sprintf(&buf[pos], "%s%s,%u,%u", sep,
			    modes[zti->zti_mode], zti->zti_count,
			    zti->zti_value);
		else
			pos += sprintf(&buf[pos], "%s%s", sep,
			    modes[zti->zti_mode]);
		sep = " ";
	}

	if (add_newline)
		buf[pos++] = '\n';
	buf[pos] = '\0';

	return (pos);
}

#ifdef __linux__
static int
spa_taskq_read_param_set(const char *val, zfs_kernel_param_t *kp)
{
	char *cfg = kmem_strdup(val);
	int err = spa_taskq_param_set(ZIO_TYPE_READ, cfg);
	kmem_free(cfg, strlen(val)+1);
	return (-err);
}
static int
spa_taskq_read_param_get(char *buf, zfs_kernel_param_t *kp)
{
	return (spa_taskq_param_get(ZIO_TYPE_READ, buf, TRUE));
}

static int
spa_taskq_write_param_set(const char *val, zfs_kernel_param_t *kp)
{
	char *cfg = kmem_strdup(val);
	int err = spa_taskq_param_set(ZIO_TYPE_WRITE, cfg);
	kmem_free(cfg, strlen(val)+1);
	return (-err);
}
static int
spa_taskq_write_param_get(char *buf, zfs_kernel_param_t *kp)
{
	return (spa_taskq_param_get(ZIO_TYPE_WRITE, buf, TRUE));
}
#else
/*
 * On FreeBSD load-time parameters can be set up before malloc() is available,
 * so we have to do all the parsing work on the stack.
 */
#define	SPA_TASKQ_PARAM_MAX	(128)

static int
spa_taskq_read_param(ZFS_MODULE_PARAM_ARGS)
{
	char buf[SPA_TASKQ_PARAM_MAX];
	int err;

	(void) spa_taskq_param_get(ZIO_TYPE_READ, buf, FALSE);
	err = sysctl_handle_string(oidp, buf, sizeof (buf), req);
	if (err || req->newptr == NULL)
		return (err);
	return (spa_taskq_param_set(ZIO_TYPE_READ, buf));
}

static int
spa_taskq_write_param(ZFS_MODULE_PARAM_ARGS)
{
	char buf[SPA_TASKQ_PARAM_MAX];
	int err;

	(void) spa_taskq_param_get(ZIO_TYPE_WRITE, buf, FALSE);
	err = sysctl_handle_string(oidp, buf, sizeof (buf), req);
	if (err || req->newptr == NULL)
		return (err);
	return (spa_taskq_param_set(ZIO_TYPE_WRITE, buf));
}
#endif
#endif /* _KERNEL */

/*
 * Dispatch a task to the appropriate taskq for the ZFS I/O type and priority.
 * Note that a type may have multiple discrete taskqs to avoid lock contention
 * on the taskq itself.
 */
static taskq_t *
spa_taskq_dispatch_select(spa_t *spa, zio_type_t t, zio_taskq_type_t q,
    zio_t *zio)
{
	spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];
	taskq_t *tq;

	ASSERT3P(tqs->stqs_taskq, !=, NULL);
	ASSERT3U(tqs->stqs_count, !=, 0);

	if ((t == ZIO_TYPE_WRITE) && (q == ZIO_TASKQ_ISSUE) &&
	    (zio != NULL) && (zio->io_wr_iss_tq != NULL)) {
		/* dispatch to assigned write issue taskq */
		tq = zio->io_wr_iss_tq;
		return (tq);
	}

	if (tqs->stqs_count == 1) {
		tq = tqs->stqs_taskq[0];
	} else {
		tq = tqs->stqs_taskq[((uint64_t)gethrtime()) % tqs->stqs_count];
	}
	return (tq);
}

void
spa_taskq_dispatch_ent(spa_t *spa, zio_type_t t, zio_taskq_type_t q,
    task_func_t *func, void *arg, uint_t flags, taskq_ent_t *ent,
    zio_t *zio)
{
	taskq_t *tq = spa_taskq_dispatch_select(spa, t, q, zio);
	taskq_dispatch_ent(tq, func, arg, flags, ent);
}

/*
 * Same as spa_taskq_dispatch_ent() but block on the task until completion.
 */
void
spa_taskq_dispatch_sync(spa_t *spa, zio_type_t t, zio_taskq_type_t q,
    task_func_t *func, void *arg, uint_t flags)
{
	taskq_t *tq = spa_taskq_dispatch_select(spa, t, q, NULL);
	taskqid_t id = taskq_dispatch(tq, func, arg, flags);
	if (id)
		taskq_wait_id(tq, id);
}

static void
spa_create_zio_taskqs(spa_t *spa)
{
	for (int t = 0; t < ZIO_TYPES; t++) {
		for (int q = 0; q < ZIO_TASKQ_TYPES; q++) {
			spa_taskqs_init(spa, t, q);
		}
	}
}

#if defined(_KERNEL) && defined(HAVE_SPA_THREAD)
static void
spa_thread(void *arg)
{
	psetid_t zio_taskq_psrset_bind = PS_NONE;
	callb_cpr_t cprinfo;

	spa_t *spa = arg;
	user_t *pu = PTOU(curproc);

	CALLB_CPR_INIT(&cprinfo, &spa->spa_proc_lock, callb_generic_cpr,
	    spa->spa_name);

	ASSERT(curproc != &p0);
	(void) snprintf(pu->u_psargs, sizeof (pu->u_psargs),
	    "zpool-%s", spa->spa_name);
	(void) strlcpy(pu->u_comm, pu->u_psargs, sizeof (pu->u_comm));

	/* bind this thread to the requested psrset */
	if (zio_taskq_psrset_bind != PS_NONE) {
		pool_lock();
		mutex_enter(&cpu_lock);
		mutex_enter(&pidlock);
		mutex_enter(&curproc->p_lock);

		if (cpupart_bind_thread(curthread, zio_taskq_psrset_bind,
		    0, NULL, NULL) == 0)  {
			curthread->t_bind_pset = zio_taskq_psrset_bind;
		} else {
			cmn_err(CE_WARN,
			    "Couldn't bind process for zfs pool \"%s\" to "
			    "pset %d\n", spa->spa_name, zio_taskq_psrset_bind);
		}

		mutex_exit(&curproc->p_lock);
		mutex_exit(&pidlock);
		mutex_exit(&cpu_lock);
		pool_unlock();
	}

#ifdef HAVE_SYSDC
	if (zio_taskq_sysdc) {
		sysdc_thread_enter(curthread, 100, 0);
	}
#endif

	spa->spa_proc = curproc;
	spa->spa_did = curthread->t_did;

	spa_create_zio_taskqs(spa);

	mutex_enter(&spa->spa_proc_lock);
	ASSERT(spa->spa_proc_state == SPA_PROC_CREATED);

	spa->spa_proc_state = SPA_PROC_ACTIVE;
	cv_broadcast(&spa->spa_proc_cv);

	CALLB_CPR_SAFE_BEGIN(&cprinfo);
	while (spa->spa_proc_state == SPA_PROC_ACTIVE)
		cv_wait(&spa->spa_proc_cv, &spa->spa_proc_lock);
	CALLB_CPR_SAFE_END(&cprinfo, &spa->spa_proc_lock);

	ASSERT(spa->spa_proc_state == SPA_PROC_DEACTIVATE);
	spa->spa_proc_state = SPA_PROC_GONE;
	spa->spa_proc = &p0;
	cv_broadcast(&spa->spa_proc_cv);
	CALLB_CPR_EXIT(&cprinfo);	/* drops spa_proc_lock */

	mutex_enter(&curproc->p_lock);
	lwp_exit();
}
#endif

extern metaslab_ops_t *metaslab_allocator(spa_t *spa);

/*
 * Activate an uninitialized pool.
 */
static void
spa_activate(spa_t *spa, spa_mode_t mode)
{
	metaslab_ops_t *msp = metaslab_allocator(spa);
	ASSERT(spa->spa_state == POOL_STATE_UNINITIALIZED);

	spa->spa_state = POOL_STATE_ACTIVE;
	spa->spa_mode = mode;
	spa->spa_read_spacemaps = spa_mode_readable_spacemaps;

	spa->spa_normal_class = metaslab_class_create(spa, msp);
	spa->spa_log_class = metaslab_class_create(spa, msp);
	spa->spa_embedded_log_class = metaslab_class_create(spa, msp);
	spa->spa_special_class = metaslab_class_create(spa, msp);
	spa->spa_dedup_class = metaslab_class_create(spa, msp);

	/* Try to create a covering process */
	mutex_enter(&spa->spa_proc_lock);
	ASSERT(spa->spa_proc_state == SPA_PROC_NONE);
	ASSERT(spa->spa_proc == &p0);
	spa->spa_did = 0;

#ifdef HAVE_SPA_THREAD
	/* Only create a process if we're going to be around a while. */
	if (spa_create_process && strcmp(spa->spa_name, TRYIMPORT_NAME) != 0) {
		if (newproc(spa_thread, (caddr_t)spa, syscid, maxclsyspri,
		    NULL, 0) == 0) {
			spa->spa_proc_state = SPA_PROC_CREATED;
			while (spa->spa_proc_state == SPA_PROC_CREATED) {
				cv_wait(&spa->spa_proc_cv,
				    &spa->spa_proc_lock);
			}
			ASSERT(spa->spa_proc_state == SPA_PROC_ACTIVE);
			ASSERT(spa->spa_proc != &p0);
			ASSERT(spa->spa_did != 0);
		} else {
#ifdef _KERNEL
			cmn_err(CE_WARN,
			    "Couldn't create process for zfs pool \"%s\"\n",
			    spa->spa_name);
#endif
		}
	}
#endif /* HAVE_SPA_THREAD */
	mutex_exit(&spa->spa_proc_lock);

	/* If we didn't create a process, we need to create our taskqs. */
	if (spa->spa_proc == &p0) {
		spa_create_zio_taskqs(spa);
	}

	for (size_t i = 0; i < TXG_SIZE; i++) {
		spa->spa_txg_zio[i] = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL);
	}

	list_create(&spa->spa_config_dirty_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_config_dirty_node));
	list_create(&spa->spa_evicting_os_list, sizeof (objset_t),
	    offsetof(objset_t, os_evicting_node));
	list_create(&spa->spa_state_dirty_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_state_dirty_node));

	txg_list_create(&spa->spa_vdev_txg_list, spa,
	    offsetof(struct vdev, vdev_txg_node));

	avl_create(&spa->spa_errlist_scrub,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_last,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_healed,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));

	spa_activate_os(spa);

	spa_keystore_init(&spa->spa_keystore);

	/*
	 * This taskq is used to perform zvol-minor-related tasks
	 * asynchronously. This has several advantages, including easy
	 * resolution of various deadlocks.
	 *
	 * The taskq must be single threaded to ensure tasks are always
	 * processed in the order in which they were dispatched.
	 *
	 * A taskq per pool allows one to keep the pools independent.
	 * This way if one pool is suspended, it will not impact another.
	 *
	 * The preferred location to dispatch a zvol minor task is a sync
	 * task. In this context, there is easy access to the spa_t and minimal
	 * error handling is required because the sync task must succeed.
	 */
	spa->spa_zvol_taskq = taskq_create("z_zvol", 1, defclsyspri,
	    1, INT_MAX, 0);

	/*
	 * The taskq to preload metaslabs.
	 */
	spa->spa_metaslab_taskq = taskq_create("z_metaslab",
	    metaslab_preload_pct, maxclsyspri, 1, INT_MAX,
	    TASKQ_DYNAMIC | TASKQ_THREADS_CPU_PCT);

	/*
	 * Taskq dedicated to prefetcher threads: this is used to prevent the
	 * pool traverse code from monopolizing the global (and limited)
	 * system_taskq by inappropriately scheduling long running tasks on it.
	 */
	spa->spa_prefetch_taskq = taskq_create("z_prefetch", 100,
	    defclsyspri, 1, INT_MAX, TASKQ_DYNAMIC | TASKQ_THREADS_CPU_PCT);

	/*
	 * The taskq to upgrade datasets in this pool. Currently used by
	 * feature SPA_FEATURE_USEROBJ_ACCOUNTING/SPA_FEATURE_PROJECT_QUOTA.
	 */
	spa->spa_upgrade_taskq = taskq_create("z_upgrade", 100,
	    defclsyspri, 1, INT_MAX, TASKQ_DYNAMIC | TASKQ_THREADS_CPU_PCT);
}

/*
 * Opposite of spa_activate().
 */
static void
spa_deactivate(spa_t *spa)
{
	ASSERT(spa->spa_sync_on == B_FALSE);
	ASSERT(spa->spa_dsl_pool == NULL);
	ASSERT(spa->spa_root_vdev == NULL);
	ASSERT(spa->spa_async_zio_root == NULL);
	ASSERT(spa->spa_state != POOL_STATE_UNINITIALIZED);

	spa_evicting_os_wait(spa);

	if (spa->spa_zvol_taskq) {
		taskq_destroy(spa->spa_zvol_taskq);
		spa->spa_zvol_taskq = NULL;
	}

	if (spa->spa_metaslab_taskq) {
		taskq_destroy(spa->spa_metaslab_taskq);
		spa->spa_metaslab_taskq = NULL;
	}

	if (spa->spa_prefetch_taskq) {
		taskq_destroy(spa->spa_prefetch_taskq);
		spa->spa_prefetch_taskq = NULL;
	}

	if (spa->spa_upgrade_taskq) {
		taskq_destroy(spa->spa_upgrade_taskq);
		spa->spa_upgrade_taskq = NULL;
	}

	txg_list_destroy(&spa->spa_vdev_txg_list);

	list_destroy(&spa->spa_config_dirty_list);
	list_destroy(&spa->spa_evicting_os_list);
	list_destroy(&spa->spa_state_dirty_list);

	taskq_cancel_id(system_delay_taskq, spa->spa_deadman_tqid);

	for (int t = 0; t < ZIO_TYPES; t++) {
		for (int q = 0; q < ZIO_TASKQ_TYPES; q++) {
			spa_taskqs_fini(spa, t, q);
		}
	}

	for (size_t i = 0; i < TXG_SIZE; i++) {
		ASSERT3P(spa->spa_txg_zio[i], !=, NULL);
		VERIFY0(zio_wait(spa->spa_txg_zio[i]));
		spa->spa_txg_zio[i] = NULL;
	}

	metaslab_class_destroy(spa->spa_normal_class);
	spa->spa_normal_class = NULL;

	metaslab_class_destroy(spa->spa_log_class);
	spa->spa_log_class = NULL;

	metaslab_class_destroy(spa->spa_embedded_log_class);
	spa->spa_embedded_log_class = NULL;

	metaslab_class_destroy(spa->spa_special_class);
	spa->spa_special_class = NULL;

	metaslab_class_destroy(spa->spa_dedup_class);
	spa->spa_dedup_class = NULL;

	/*
	 * If this was part of an import or the open otherwise failed, we may
	 * still have errors left in the queues.  Empty them just in case.
	 */
	spa_errlog_drain(spa);
	avl_destroy(&spa->spa_errlist_scrub);
	avl_destroy(&spa->spa_errlist_last);
	avl_destroy(&spa->spa_errlist_healed);

	spa_keystore_fini(&spa->spa_keystore);

	spa->spa_state = POOL_STATE_UNINITIALIZED;

	mutex_enter(&spa->spa_proc_lock);
	if (spa->spa_proc_state != SPA_PROC_NONE) {
		ASSERT(spa->spa_proc_state == SPA_PROC_ACTIVE);
		spa->spa_proc_state = SPA_PROC_DEACTIVATE;
		cv_broadcast(&spa->spa_proc_cv);
		while (spa->spa_proc_state == SPA_PROC_DEACTIVATE) {
			ASSERT(spa->spa_proc != &p0);
			cv_wait(&spa->spa_proc_cv, &spa->spa_proc_lock);
		}
		ASSERT(spa->spa_proc_state == SPA_PROC_GONE);
		spa->spa_proc_state = SPA_PROC_NONE;
	}
	ASSERT(spa->spa_proc == &p0);
	mutex_exit(&spa->spa_proc_lock);

	/*
	 * We want to make sure spa_thread() has actually exited the ZFS
	 * module, so that the module can't be unloaded out from underneath
	 * it.
	 */
	if (spa->spa_did != 0) {
		thread_join(spa->spa_did);
		spa->spa_did = 0;
	}

	spa_deactivate_os(spa);

}

/*
 * Verify a pool configuration, and construct the vdev tree appropriately.  This
 * will create all the necessary vdevs in the appropriate layout, with each vdev
 * in the CLOSED state.  This will prep the pool before open/creation/import.
 * All vdev validation is done by the vdev_alloc() routine.
 */
int
spa_config_parse(spa_t *spa, vdev_t **vdp, nvlist_t *nv, vdev_t *parent,
    uint_t id, int atype)
{
	nvlist_t **child;
	uint_t children;
	int error;

	if ((error = vdev_alloc(spa, vdp, nv, parent, id, atype)) != 0)
		return (error);

	if ((*vdp)->vdev_ops->vdev_op_leaf)
		return (0);

	error = nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children);

	if (error == ENOENT)
		return (0);

	if (error) {
		vdev_free(*vdp);
		*vdp = NULL;
		return (SET_ERROR(EINVAL));
	}

	for (int c = 0; c < children; c++) {
		vdev_t *vd;
		if ((error = spa_config_parse(spa, &vd, child[c], *vdp, c,
		    atype)) != 0) {
			vdev_free(*vdp);
			*vdp = NULL;
			return (error);
		}
	}

	ASSERT(*vdp != NULL);

	return (0);
}

static boolean_t
spa_should_flush_logs_on_unload(spa_t *spa)
{
	if (!spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP))
		return (B_FALSE);

	if (!spa_writeable(spa))
		return (B_FALSE);

	if (!spa->spa_sync_on)
		return (B_FALSE);

	if (spa_state(spa) != POOL_STATE_EXPORTED)
		return (B_FALSE);

	if (zfs_keep_log_spacemaps_at_export)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Opens a transaction that will set the flag that will instruct
 * spa_sync to attempt to flush all the metaslabs for that txg.
 */
static void
spa_unload_log_sm_flush_all(spa_t *spa)
{
	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

	ASSERT3U(spa->spa_log_flushall_txg, ==, 0);
	spa->spa_log_flushall_txg = dmu_tx_get_txg(tx);

	dmu_tx_commit(tx);
	txg_wait_synced(spa_get_dsl(spa), spa->spa_log_flushall_txg);
}

static void
spa_unload_log_sm_metadata(spa_t *spa)
{
	void *cookie = NULL;
	spa_log_sm_t *sls;
	log_summary_entry_t *e;

	while ((sls = avl_destroy_nodes(&spa->spa_sm_logs_by_txg,
	    &cookie)) != NULL) {
		VERIFY0(sls->sls_mscount);
		kmem_free(sls, sizeof (spa_log_sm_t));
	}

	while ((e = list_remove_head(&spa->spa_log_summary)) != NULL) {
		VERIFY0(e->lse_mscount);
		kmem_free(e, sizeof (log_summary_entry_t));
	}

	spa->spa_unflushed_stats.sus_nblocks = 0;
	spa->spa_unflushed_stats.sus_memused = 0;
	spa->spa_unflushed_stats.sus_blocklimit = 0;
}

static void
spa_destroy_aux_threads(spa_t *spa)
{
	if (spa->spa_condense_zthr != NULL) {
		zthr_destroy(spa->spa_condense_zthr);
		spa->spa_condense_zthr = NULL;
	}
	if (spa->spa_checkpoint_discard_zthr != NULL) {
		zthr_destroy(spa->spa_checkpoint_discard_zthr);
		spa->spa_checkpoint_discard_zthr = NULL;
	}
	if (spa->spa_livelist_delete_zthr != NULL) {
		zthr_destroy(spa->spa_livelist_delete_zthr);
		spa->spa_livelist_delete_zthr = NULL;
	}
	if (spa->spa_livelist_condense_zthr != NULL) {
		zthr_destroy(spa->spa_livelist_condense_zthr);
		spa->spa_livelist_condense_zthr = NULL;
	}
	if (spa->spa_raidz_expand_zthr != NULL) {
		zthr_destroy(spa->spa_raidz_expand_zthr);
		spa->spa_raidz_expand_zthr = NULL;
	}
}

/*
 * Opposite of spa_load().
 */
static void
spa_unload(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa_state(spa) != POOL_STATE_UNINITIALIZED);

	spa_import_progress_remove(spa_guid(spa));
	spa_load_note(spa, "UNLOADING");

	spa_wake_waiters(spa);

	/*
	 * If we have set the spa_final_txg, we have already performed the
	 * tasks below in spa_export_common(). We should not redo it here since
	 * we delay the final TXGs beyond what spa_final_txg is set at.
	 */
	if (spa->spa_final_txg == UINT64_MAX) {
		/*
		 * If the log space map feature is enabled and the pool is
		 * getting exported (but not destroyed), we want to spend some
		 * time flushing as many metaslabs as we can in an attempt to
		 * destroy log space maps and save import time.
		 */
		if (spa_should_flush_logs_on_unload(spa))
			spa_unload_log_sm_flush_all(spa);

		/*
		 * Stop async tasks.
		 */
		spa_async_suspend(spa);

		if (spa->spa_root_vdev) {
			vdev_t *root_vdev = spa->spa_root_vdev;
			vdev_initialize_stop_all(root_vdev,
			    VDEV_INITIALIZE_ACTIVE);
			vdev_trim_stop_all(root_vdev, VDEV_TRIM_ACTIVE);
			vdev_autotrim_stop_all(spa);
			vdev_rebuild_stop_all(spa);
		}
	}

	/*
	 * Stop syncing.
	 */
	if (spa->spa_sync_on) {
		txg_sync_stop(spa->spa_dsl_pool);
		spa->spa_sync_on = B_FALSE;
	}

	/*
	 * This ensures that there is no async metaslab prefetching
	 * while we attempt to unload the spa.
	 */
	taskq_wait(spa->spa_metaslab_taskq);

	if (spa->spa_mmp.mmp_thread)
		mmp_thread_stop(spa);

	/*
	 * Wait for any outstanding async I/O to complete.
	 */
	if (spa->spa_async_zio_root != NULL) {
		for (int i = 0; i < max_ncpus; i++)
			(void) zio_wait(spa->spa_async_zio_root[i]);
		kmem_free(spa->spa_async_zio_root, max_ncpus * sizeof (void *));
		spa->spa_async_zio_root = NULL;
	}

	if (spa->spa_vdev_removal != NULL) {
		spa_vdev_removal_destroy(spa->spa_vdev_removal);
		spa->spa_vdev_removal = NULL;
	}

	spa_destroy_aux_threads(spa);

	spa_condense_fini(spa);

	bpobj_close(&spa->spa_deferred_bpobj);

	spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);

	/*
	 * Close all vdevs.
	 */
	if (spa->spa_root_vdev)
		vdev_free(spa->spa_root_vdev);
	ASSERT(spa->spa_root_vdev == NULL);

	/*
	 * Close the dsl pool.
	 */
	if (spa->spa_dsl_pool) {
		dsl_pool_close(spa->spa_dsl_pool);
		spa->spa_dsl_pool = NULL;
		spa->spa_meta_objset = NULL;
	}

	ddt_unload(spa);
	brt_unload(spa);
	spa_unload_log_sm_metadata(spa);

	/*
	 * Drop and purge level 2 cache
	 */
	spa_l2cache_drop(spa);

	if (spa->spa_spares.sav_vdevs) {
		for (int i = 0; i < spa->spa_spares.sav_count; i++)
			vdev_free(spa->spa_spares.sav_vdevs[i]);
		kmem_free(spa->spa_spares.sav_vdevs,
		    spa->spa_spares.sav_count * sizeof (void *));
		spa->spa_spares.sav_vdevs = NULL;
	}
	if (spa->spa_spares.sav_config) {
		nvlist_free(spa->spa_spares.sav_config);
		spa->spa_spares.sav_config = NULL;
	}
	spa->spa_spares.sav_count = 0;

	if (spa->spa_l2cache.sav_vdevs) {
		for (int i = 0; i < spa->spa_l2cache.sav_count; i++) {
			vdev_clear_stats(spa->spa_l2cache.sav_vdevs[i]);
			vdev_free(spa->spa_l2cache.sav_vdevs[i]);
		}
		kmem_free(spa->spa_l2cache.sav_vdevs,
		    spa->spa_l2cache.sav_count * sizeof (void *));
		spa->spa_l2cache.sav_vdevs = NULL;
	}
	if (spa->spa_l2cache.sav_config) {
		nvlist_free(spa->spa_l2cache.sav_config);
		spa->spa_l2cache.sav_config = NULL;
	}
	spa->spa_l2cache.sav_count = 0;

	spa->spa_async_suspended = 0;

	spa->spa_indirect_vdevs_loaded = B_FALSE;

	if (spa->spa_comment != NULL) {
		spa_strfree(spa->spa_comment);
		spa->spa_comment = NULL;
	}
	if (spa->spa_compatibility != NULL) {
		spa_strfree(spa->spa_compatibility);
		spa->spa_compatibility = NULL;
	}

	spa->spa_raidz_expand = NULL;

	spa_config_exit(spa, SCL_ALL, spa);
}

/*
 * Load (or re-load) the current list of vdevs describing the active spares for
 * this pool.  When this is called, we have some form of basic information in
 * 'spa_spares.sav_config'.  We parse this into vdevs, try to open them, and
 * then re-generate a more complete list including status information.
 */
void
spa_load_spares(spa_t *spa)
{
	nvlist_t **spares;
	uint_t nspares;
	int i;
	vdev_t *vd, *tvd;

#ifndef _KERNEL
	/*
	 * zdb opens both the current state of the pool and the
	 * checkpointed state (if present), with a different spa_t.
	 *
	 * As spare vdevs are shared among open pools, we skip loading
	 * them when we load the checkpointed state of the pool.
	 */
	if (!spa_writeable(spa))
		return;
#endif

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/*
	 * First, close and free any existing spare vdevs.
	 */
	if (spa->spa_spares.sav_vdevs) {
		for (i = 0; i < spa->spa_spares.sav_count; i++) {
			vd = spa->spa_spares.sav_vdevs[i];

			/* Undo the call to spa_activate() below */
			if ((tvd = spa_lookup_by_guid(spa, vd->vdev_guid,
			    B_FALSE)) != NULL && tvd->vdev_isspare)
				spa_spare_remove(tvd);
			vdev_close(vd);
			vdev_free(vd);
		}

		kmem_free(spa->spa_spares.sav_vdevs,
		    spa->spa_spares.sav_count * sizeof (void *));
	}

	if (spa->spa_spares.sav_config == NULL)
		nspares = 0;
	else
		VERIFY0(nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, &spares, &nspares));

	spa->spa_spares.sav_count = (int)nspares;
	spa->spa_spares.sav_vdevs = NULL;

	if (nspares == 0)
		return;

	/*
	 * Construct the array of vdevs, opening them to get status in the
	 * process.   For each spare, there is potentially two different vdev_t
	 * structures associated with it: one in the list of spares (used only
	 * for basic validation purposes) and one in the active vdev
	 * configuration (if it's spared in).  During this phase we open and
	 * validate each vdev on the spare list.  If the vdev also exists in the
	 * active configuration, then we also mark this vdev as an active spare.
	 */
	spa->spa_spares.sav_vdevs = kmem_zalloc(nspares * sizeof (void *),
	    KM_SLEEP);
	for (i = 0; i < spa->spa_spares.sav_count; i++) {
		VERIFY(spa_config_parse(spa, &vd, spares[i], NULL, 0,
		    VDEV_ALLOC_SPARE) == 0);
		ASSERT(vd != NULL);

		spa->spa_spares.sav_vdevs[i] = vd;

		if ((tvd = spa_lookup_by_guid(spa, vd->vdev_guid,
		    B_FALSE)) != NULL) {
			if (!tvd->vdev_isspare)
				spa_spare_add(tvd);

			/*
			 * We only mark the spare active if we were successfully
			 * able to load the vdev.  Otherwise, importing a pool
			 * with a bad active spare would result in strange
			 * behavior, because multiple pool would think the spare
			 * is actively in use.
			 *
			 * There is a vulnerability here to an equally bizarre
			 * circumstance, where a dead active spare is later
			 * brought back to life (onlined or otherwise).  Given
			 * the rarity of this scenario, and the extra complexity
			 * it adds, we ignore the possibility.
			 */
			if (!vdev_is_dead(tvd))
				spa_spare_activate(tvd);
		}

		vd->vdev_top = vd;
		vd->vdev_aux = &spa->spa_spares;

		if (vdev_open(vd) != 0)
			continue;

		if (vdev_validate_aux(vd) == 0)
			spa_spare_add(vd);
	}

	/*
	 * Recompute the stashed list of spares, with status information
	 * this time.
	 */
	fnvlist_remove(spa->spa_spares.sav_config, ZPOOL_CONFIG_SPARES);

	spares = kmem_alloc(spa->spa_spares.sav_count * sizeof (void *),
	    KM_SLEEP);
	for (i = 0; i < spa->spa_spares.sav_count; i++)
		spares[i] = vdev_config_generate(spa,
		    spa->spa_spares.sav_vdevs[i], B_TRUE, VDEV_CONFIG_SPARE);
	fnvlist_add_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, (const nvlist_t * const *)spares,
	    spa->spa_spares.sav_count);
	for (i = 0; i < spa->spa_spares.sav_count; i++)
		nvlist_free(spares[i]);
	kmem_free(spares, spa->spa_spares.sav_count * sizeof (void *));
}

/*
 * Load (or re-load) the current list of vdevs describing the active l2cache for
 * this pool.  When this is called, we have some form of basic information in
 * 'spa_l2cache.sav_config'.  We parse this into vdevs, try to open them, and
 * then re-generate a more complete list including status information.
 * Devices which are already active have their details maintained, and are
 * not re-opened.
 */
void
spa_load_l2cache(spa_t *spa)
{
	nvlist_t **l2cache = NULL;
	uint_t nl2cache;
	int i, j, oldnvdevs;
	uint64_t guid;
	vdev_t *vd, **oldvdevs, **newvdevs;
	spa_aux_vdev_t *sav = &spa->spa_l2cache;

#ifndef _KERNEL
	/*
	 * zdb opens both the current state of the pool and the
	 * checkpointed state (if present), with a different spa_t.
	 *
	 * As L2 caches are part of the ARC which is shared among open
	 * pools, we skip loading them when we load the checkpointed
	 * state of the pool.
	 */
	if (!spa_writeable(spa))
		return;
#endif

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	oldvdevs = sav->sav_vdevs;
	oldnvdevs = sav->sav_count;
	sav->sav_vdevs = NULL;
	sav->sav_count = 0;

	if (sav->sav_config == NULL) {
		nl2cache = 0;
		newvdevs = NULL;
		goto out;
	}

	VERIFY0(nvlist_lookup_nvlist_array(sav->sav_config,
	    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache));
	newvdevs = kmem_alloc(nl2cache * sizeof (void *), KM_SLEEP);

	/*
	 * Process new nvlist of vdevs.
	 */
	for (i = 0; i < nl2cache; i++) {
		guid = fnvlist_lookup_uint64(l2cache[i], ZPOOL_CONFIG_GUID);

		newvdevs[i] = NULL;
		for (j = 0; j < oldnvdevs; j++) {
			vd = oldvdevs[j];
			if (vd != NULL && guid == vd->vdev_guid) {
				/*
				 * Retain previous vdev for add/remove ops.
				 */
				newvdevs[i] = vd;
				oldvdevs[j] = NULL;
				break;
			}
		}

		if (newvdevs[i] == NULL) {
			/*
			 * Create new vdev
			 */
			VERIFY(spa_config_parse(spa, &vd, l2cache[i], NULL, 0,
			    VDEV_ALLOC_L2CACHE) == 0);
			ASSERT(vd != NULL);
			newvdevs[i] = vd;

			/*
			 * Commit this vdev as an l2cache device,
			 * even if it fails to open.
			 */
			spa_l2cache_add(vd);

			vd->vdev_top = vd;
			vd->vdev_aux = sav;

			spa_l2cache_activate(vd);

			if (vdev_open(vd) != 0)
				continue;

			(void) vdev_validate_aux(vd);

			if (!vdev_is_dead(vd))
				l2arc_add_vdev(spa, vd);

			/*
			 * Upon cache device addition to a pool or pool
			 * creation with a cache device or if the header
			 * of the device is invalid we issue an async
			 * TRIM command for the whole device which will
			 * execute if l2arc_trim_ahead > 0.
			 */
			spa_async_request(spa, SPA_ASYNC_L2CACHE_TRIM);
		}
	}

	sav->sav_vdevs = newvdevs;
	sav->sav_count = (int)nl2cache;

	/*
	 * Recompute the stashed list of l2cache devices, with status
	 * information this time.
	 */
	fnvlist_remove(sav->sav_config, ZPOOL_CONFIG_L2CACHE);

	if (sav->sav_count > 0)
		l2cache = kmem_alloc(sav->sav_count * sizeof (void *),
		    KM_SLEEP);
	for (i = 0; i < sav->sav_count; i++)
		l2cache[i] = vdev_config_generate(spa,
		    sav->sav_vdevs[i], B_TRUE, VDEV_CONFIG_L2CACHE);
	fnvlist_add_nvlist_array(sav->sav_config, ZPOOL_CONFIG_L2CACHE,
	    (const nvlist_t * const *)l2cache, sav->sav_count);

out:
	/*
	 * Purge vdevs that were dropped
	 */
	if (oldvdevs) {
		for (i = 0; i < oldnvdevs; i++) {
			uint64_t pool;

			vd = oldvdevs[i];
			if (vd != NULL) {
				ASSERT(vd->vdev_isl2cache);

				if (spa_l2cache_exists(vd->vdev_guid, &pool) &&
				    pool != 0ULL && l2arc_vdev_present(vd))
					l2arc_remove_vdev(vd);
				vdev_clear_stats(vd);
				vdev_free(vd);
			}
		}

		kmem_free(oldvdevs, oldnvdevs * sizeof (void *));
	}

	for (i = 0; i < sav->sav_count; i++)
		nvlist_free(l2cache[i]);
	if (sav->sav_count)
		kmem_free(l2cache, sav->sav_count * sizeof (void *));
}

static int
load_nvlist(spa_t *spa, uint64_t obj, nvlist_t **value)
{
	dmu_buf_t *db;
	char *packed = NULL;
	size_t nvsize = 0;
	int error;
	*value = NULL;

	error = dmu_bonus_hold(spa->spa_meta_objset, obj, FTAG, &db);
	if (error)
		return (error);

	nvsize = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	packed = vmem_alloc(nvsize, KM_SLEEP);
	error = dmu_read(spa->spa_meta_objset, obj, 0, nvsize, packed,
	    DMU_READ_PREFETCH);
	if (error == 0)
		error = nvlist_unpack(packed, nvsize, value, 0);
	vmem_free(packed, nvsize);

	return (error);
}

/*
 * Concrete top-level vdevs that are not missing and are not logs. At every
 * spa_sync we write new uberblocks to at least SPA_SYNC_MIN_VDEVS core tvds.
 */
static uint64_t
spa_healthy_core_tvds(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t tvds = 0;

	for (uint64_t i = 0; i < rvd->vdev_children; i++) {
		vdev_t *vd = rvd->vdev_child[i];
		if (vd->vdev_islog)
			continue;
		if (vdev_is_concrete(vd) && !vdev_is_dead(vd))
			tvds++;
	}

	return (tvds);
}

/*
 * Checks to see if the given vdev could not be opened, in which case we post a
 * sysevent to notify the autoreplace code that the device has been removed.
 */
static void
spa_check_removed(vdev_t *vd)
{
	for (uint64_t c = 0; c < vd->vdev_children; c++)
		spa_check_removed(vd->vdev_child[c]);

	if (vd->vdev_ops->vdev_op_leaf && vdev_is_dead(vd) &&
	    vdev_is_concrete(vd)) {
		zfs_post_autoreplace(vd->vdev_spa, vd);
		spa_event_notify(vd->vdev_spa, vd, NULL, ESC_ZFS_VDEV_CHECK);
	}
}

static int
spa_check_for_missing_logs(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * If we're doing a normal import, then build up any additional
	 * diagnostic information about missing log devices.
	 * We'll pass this up to the user for further processing.
	 */
	if (!(spa->spa_import_flags & ZFS_IMPORT_MISSING_LOG)) {
		nvlist_t **child, *nv;
		uint64_t idx = 0;

		child = kmem_alloc(rvd->vdev_children * sizeof (nvlist_t *),
		    KM_SLEEP);
		nv = fnvlist_alloc();

		for (uint64_t c = 0; c < rvd->vdev_children; c++) {
			vdev_t *tvd = rvd->vdev_child[c];

			/*
			 * We consider a device as missing only if it failed
			 * to open (i.e. offline or faulted is not considered
			 * as missing).
			 */
			if (tvd->vdev_islog &&
			    tvd->vdev_state == VDEV_STATE_CANT_OPEN) {
				child[idx++] = vdev_config_generate(spa, tvd,
				    B_FALSE, VDEV_CONFIG_MISSING);
			}
		}

		if (idx > 0) {
			fnvlist_add_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
			    (const nvlist_t * const *)child, idx);
			fnvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_MISSING_DEVICES, nv);

			for (uint64_t i = 0; i < idx; i++)
				nvlist_free(child[i]);
		}
		nvlist_free(nv);
		kmem_free(child, rvd->vdev_children * sizeof (char **));

		if (idx > 0) {
			spa_load_failed(spa, "some log devices are missing");
			vdev_dbgmsg_print_tree(rvd, 2);
			return (SET_ERROR(ENXIO));
		}
	} else {
		for (uint64_t c = 0; c < rvd->vdev_children; c++) {
			vdev_t *tvd = rvd->vdev_child[c];

			if (tvd->vdev_islog &&
			    tvd->vdev_state == VDEV_STATE_CANT_OPEN) {
				spa_set_log_state(spa, SPA_LOG_CLEAR);
				spa_load_note(spa, "some log devices are "
				    "missing, ZIL is dropped.");
				vdev_dbgmsg_print_tree(rvd, 2);
				break;
			}
		}
	}

	return (0);
}

/*
 * Check for missing log devices
 */
static boolean_t
spa_check_logs(spa_t *spa)
{
	boolean_t rv = B_FALSE;
	dsl_pool_t *dp = spa_get_dsl(spa);

	switch (spa->spa_log_state) {
	default:
		break;
	case SPA_LOG_MISSING:
		/* need to recheck in case slog has been restored */
	case SPA_LOG_UNKNOWN:
		rv = (dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
		    zil_check_log_chain, NULL, DS_FIND_CHILDREN) != 0);
		if (rv)
			spa_set_log_state(spa, SPA_LOG_MISSING);
		break;
	}
	return (rv);
}

/*
 * Passivate any log vdevs (note, does not apply to embedded log metaslabs).
 */
static boolean_t
spa_passivate_log(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	boolean_t slog_found = B_FALSE;

	ASSERT(spa_config_held(spa, SCL_ALLOC, RW_WRITER));

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (tvd->vdev_islog) {
			ASSERT3P(tvd->vdev_log_mg, ==, NULL);
			metaslab_group_passivate(tvd->vdev_mg);
			slog_found = B_TRUE;
		}
	}

	return (slog_found);
}

/*
 * Activate any log vdevs (note, does not apply to embedded log metaslabs).
 */
static void
spa_activate_log(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(spa_config_held(spa, SCL_ALLOC, RW_WRITER));

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (tvd->vdev_islog) {
			ASSERT3P(tvd->vdev_log_mg, ==, NULL);
			metaslab_group_activate(tvd->vdev_mg);
		}
	}
}

int
spa_reset_logs(spa_t *spa)
{
	int error;

	error = dmu_objset_find(spa_name(spa), zil_reset,
	    NULL, DS_FIND_CHILDREN);
	if (error == 0) {
		/*
		 * We successfully offlined the log device, sync out the
		 * current txg so that the "stubby" block can be removed
		 * by zil_sync().
		 */
		txg_wait_synced(spa->spa_dsl_pool, 0);
	}
	return (error);
}

static void
spa_aux_check_removed(spa_aux_vdev_t *sav)
{
	for (int i = 0; i < sav->sav_count; i++)
		spa_check_removed(sav->sav_vdevs[i]);
}

void
spa_claim_notify(zio_t *zio)
{
	spa_t *spa = zio->io_spa;

	if (zio->io_error)
		return;

	mutex_enter(&spa->spa_props_lock);	/* any mutex will do */
	if (spa->spa_claim_max_txg < BP_GET_LOGICAL_BIRTH(zio->io_bp))
		spa->spa_claim_max_txg = BP_GET_LOGICAL_BIRTH(zio->io_bp);
	mutex_exit(&spa->spa_props_lock);
}

typedef struct spa_load_error {
	boolean_t	sle_verify_data;
	uint64_t	sle_meta_count;
	uint64_t	sle_data_count;
} spa_load_error_t;

static void
spa_load_verify_done(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	spa_load_error_t *sle = zio->io_private;
	dmu_object_type_t type = BP_GET_TYPE(bp);
	int error = zio->io_error;
	spa_t *spa = zio->io_spa;

	abd_free(zio->io_abd);
	if (error) {
		if ((BP_GET_LEVEL(bp) != 0 || DMU_OT_IS_METADATA(type)) &&
		    type != DMU_OT_INTENT_LOG)
			atomic_inc_64(&sle->sle_meta_count);
		else
			atomic_inc_64(&sle->sle_data_count);
	}

	mutex_enter(&spa->spa_scrub_lock);
	spa->spa_load_verify_bytes -= BP_GET_PSIZE(bp);
	cv_broadcast(&spa->spa_scrub_io_cv);
	mutex_exit(&spa->spa_scrub_lock);
}

/*
 * Maximum number of inflight bytes is the log2 fraction of the arc size.
 * By default, we set it to 1/16th of the arc.
 */
static uint_t spa_load_verify_shift = 4;
static int spa_load_verify_metadata = B_TRUE;
static int spa_load_verify_data = B_TRUE;

static int
spa_load_verify_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	zio_t *rio = arg;
	spa_load_error_t *sle = rio->io_private;

	(void) zilog, (void) dnp;

	/*
	 * Note: normally this routine will not be called if
	 * spa_load_verify_metadata is not set.  However, it may be useful
	 * to manually set the flag after the traversal has begun.
	 */
	if (!spa_load_verify_metadata)
		return (0);

	/*
	 * Sanity check the block pointer in order to detect obvious damage
	 * before using the contents in subsequent checks or in zio_read().
	 * When damaged consider it to be a metadata error since we cannot
	 * trust the BP_GET_TYPE and BP_GET_LEVEL values.
	 */
	if (!zfs_blkptr_verify(spa, bp, BLK_CONFIG_NEEDED, BLK_VERIFY_LOG)) {
		atomic_inc_64(&sle->sle_meta_count);
		return (0);
	}

	if (zb->zb_level == ZB_DNODE_LEVEL || BP_IS_HOLE(bp) ||
	    BP_IS_EMBEDDED(bp) || BP_IS_REDACTED(bp))
		return (0);

	if (!BP_IS_METADATA(bp) &&
	    (!spa_load_verify_data || !sle->sle_verify_data))
		return (0);

	uint64_t maxinflight_bytes =
	    arc_target_bytes() >> spa_load_verify_shift;
	size_t size = BP_GET_PSIZE(bp);

	mutex_enter(&spa->spa_scrub_lock);
	while (spa->spa_load_verify_bytes >= maxinflight_bytes)
		cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
	spa->spa_load_verify_bytes += size;
	mutex_exit(&spa->spa_scrub_lock);

	zio_nowait(zio_read(rio, spa, bp, abd_alloc_for_io(size, B_FALSE), size,
	    spa_load_verify_done, rio->io_private, ZIO_PRIORITY_SCRUB,
	    ZIO_FLAG_SPECULATIVE | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SCRUB | ZIO_FLAG_RAW, zb));
	return (0);
}

static int
verify_dataset_name_len(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	(void) dp, (void) arg;

	if (dsl_dataset_namelen(ds) >= ZFS_MAX_DATASET_NAME_LEN)
		return (SET_ERROR(ENAMETOOLONG));

	return (0);
}

static int
spa_load_verify(spa_t *spa)
{
	zio_t *rio;
	spa_load_error_t sle = { 0 };
	zpool_load_policy_t policy;
	boolean_t verify_ok = B_FALSE;
	int error = 0;

	zpool_get_load_policy(spa->spa_config, &policy);

	if (policy.zlp_rewind & ZPOOL_NEVER_REWIND ||
	    policy.zlp_maxmeta == UINT64_MAX)
		return (0);

	dsl_pool_config_enter(spa->spa_dsl_pool, FTAG);
	error = dmu_objset_find_dp(spa->spa_dsl_pool,
	    spa->spa_dsl_pool->dp_root_dir_obj, verify_dataset_name_len, NULL,
	    DS_FIND_CHILDREN);
	dsl_pool_config_exit(spa->spa_dsl_pool, FTAG);
	if (error != 0)
		return (error);

	/*
	 * Verify data only if we are rewinding or error limit was set.
	 * Otherwise nothing except dbgmsg care about it to waste time.
	 */
	sle.sle_verify_data = (policy.zlp_rewind & ZPOOL_REWIND_MASK) ||
	    (policy.zlp_maxdata < UINT64_MAX);

	rio = zio_root(spa, NULL, &sle,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE);

	if (spa_load_verify_metadata) {
		if (spa->spa_extreme_rewind) {
			spa_load_note(spa, "performing a complete scan of the "
			    "pool since extreme rewind is on. This may take "
			    "a very long time.\n  (spa_load_verify_data=%u, "
			    "spa_load_verify_metadata=%u)",
			    spa_load_verify_data, spa_load_verify_metadata);
		}

		error = traverse_pool(spa, spa->spa_verify_min_txg,
		    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA |
		    TRAVERSE_NO_DECRYPT, spa_load_verify_cb, rio);
	}

	(void) zio_wait(rio);
	ASSERT0(spa->spa_load_verify_bytes);

	spa->spa_load_meta_errors = sle.sle_meta_count;
	spa->spa_load_data_errors = sle.sle_data_count;

	if (sle.sle_meta_count != 0 || sle.sle_data_count != 0) {
		spa_load_note(spa, "spa_load_verify found %llu metadata errors "
		    "and %llu data errors", (u_longlong_t)sle.sle_meta_count,
		    (u_longlong_t)sle.sle_data_count);
	}

	if (spa_load_verify_dryrun ||
	    (!error && sle.sle_meta_count <= policy.zlp_maxmeta &&
	    sle.sle_data_count <= policy.zlp_maxdata)) {
		int64_t loss = 0;

		verify_ok = B_TRUE;
		spa->spa_load_txg = spa->spa_uberblock.ub_txg;
		spa->spa_load_txg_ts = spa->spa_uberblock.ub_timestamp;

		loss = spa->spa_last_ubsync_txg_ts - spa->spa_load_txg_ts;
		fnvlist_add_uint64(spa->spa_load_info, ZPOOL_CONFIG_LOAD_TIME,
		    spa->spa_load_txg_ts);
		fnvlist_add_int64(spa->spa_load_info, ZPOOL_CONFIG_REWIND_TIME,
		    loss);
		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_LOAD_META_ERRORS, sle.sle_meta_count);
		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_LOAD_DATA_ERRORS, sle.sle_data_count);
	} else {
		spa->spa_load_max_txg = spa->spa_uberblock.ub_txg;
	}

	if (spa_load_verify_dryrun)
		return (0);

	if (error) {
		if (error != ENXIO && error != EIO)
			error = SET_ERROR(EIO);
		return (error);
	}

	return (verify_ok ? 0 : EIO);
}

/*
 * Find a value in the pool props object.
 */
static void
spa_prop_find(spa_t *spa, zpool_prop_t prop, uint64_t *val)
{
	(void) zap_lookup(spa->spa_meta_objset, spa->spa_pool_props_object,
	    zpool_prop_to_name(prop), sizeof (uint64_t), 1, val);
}

/*
 * Find a value in the pool directory object.
 */
static int
spa_dir_prop(spa_t *spa, const char *name, uint64_t *val, boolean_t log_enoent)
{
	int error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    name, sizeof (uint64_t), 1, val);

	if (error != 0 && (error != ENOENT || log_enoent)) {
		spa_load_failed(spa, "couldn't get '%s' value in MOS directory "
		    "[error=%d]", name, error);
	}

	return (error);
}

static int
spa_vdev_err(vdev_t *vdev, vdev_aux_t aux, int err)
{
	vdev_set_state(vdev, B_TRUE, VDEV_STATE_CANT_OPEN, aux);
	return (SET_ERROR(err));
}

boolean_t
spa_livelist_delete_check(spa_t *spa)
{
	return (spa->spa_livelists_to_delete != 0);
}

static boolean_t
spa_livelist_delete_cb_check(void *arg, zthr_t *z)
{
	(void) z;
	spa_t *spa = arg;
	return (spa_livelist_delete_check(spa));
}

static int
delete_blkptr_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	spa_t *spa = arg;
	zio_free(spa, tx->tx_txg, bp);
	dsl_dir_diduse_space(tx->tx_pool->dp_free_dir, DD_USED_HEAD,
	    -bp_get_dsize_sync(spa, bp),
	    -BP_GET_PSIZE(bp), -BP_GET_UCSIZE(bp), tx);
	return (0);
}

static int
dsl_get_next_livelist_obj(objset_t *os, uint64_t zap_obj, uint64_t *llp)
{
	int err;
	zap_cursor_t zc;
	zap_attribute_t za;
	zap_cursor_init(&zc, os, zap_obj);
	err = zap_cursor_retrieve(&zc, &za);
	zap_cursor_fini(&zc);
	if (err == 0)
		*llp = za.za_first_integer;
	return (err);
}

/*
 * Components of livelist deletion that must be performed in syncing
 * context: freeing block pointers and updating the pool-wide data
 * structures to indicate how much work is left to do
 */
typedef struct sublist_delete_arg {
	spa_t *spa;
	dsl_deadlist_t *ll;
	uint64_t key;
	bplist_t *to_free;
} sublist_delete_arg_t;

static void
sublist_delete_sync(void *arg, dmu_tx_t *tx)
{
	sublist_delete_arg_t *sda = arg;
	spa_t *spa = sda->spa;
	dsl_deadlist_t *ll = sda->ll;
	uint64_t key = sda->key;
	bplist_t *to_free = sda->to_free;

	bplist_iterate(to_free, delete_blkptr_cb, spa, tx);
	dsl_deadlist_remove_entry(ll, key, tx);
}

typedef struct livelist_delete_arg {
	spa_t *spa;
	uint64_t ll_obj;
	uint64_t zap_obj;
} livelist_delete_arg_t;

static void
livelist_delete_sync(void *arg, dmu_tx_t *tx)
{
	livelist_delete_arg_t *lda = arg;
	spa_t *spa = lda->spa;
	uint64_t ll_obj = lda->ll_obj;
	uint64_t zap_obj = lda->zap_obj;
	objset_t *mos = spa->spa_meta_objset;
	uint64_t count;

	/* free the livelist and decrement the feature count */
	VERIFY0(zap_remove_int(mos, zap_obj, ll_obj, tx));
	dsl_deadlist_free(mos, ll_obj, tx);
	spa_feature_decr(spa, SPA_FEATURE_LIVELIST, tx);
	VERIFY0(zap_count(mos, zap_obj, &count));
	if (count == 0) {
		/* no more livelists to delete */
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_DELETED_CLONES, tx));
		VERIFY0(zap_destroy(mos, zap_obj, tx));
		spa->spa_livelists_to_delete = 0;
		spa_notify_waiters(spa);
	}
}

/*
 * Load in the value for the livelist to be removed and open it. Then,
 * load its first sublist and determine which block pointers should actually
 * be freed. Then, call a synctask which performs the actual frees and updates
 * the pool-wide livelist data.
 */
static void
spa_livelist_delete_cb(void *arg, zthr_t *z)
{
	spa_t *spa = arg;
	uint64_t ll_obj = 0, count;
	objset_t *mos = spa->spa_meta_objset;
	uint64_t zap_obj = spa->spa_livelists_to_delete;
	/*
	 * Determine the next livelist to delete. This function should only
	 * be called if there is at least one deleted clone.
	 */
	VERIFY0(dsl_get_next_livelist_obj(mos, zap_obj, &ll_obj));
	VERIFY0(zap_count(mos, ll_obj, &count));
	if (count > 0) {
		dsl_deadlist_t *ll;
		dsl_deadlist_entry_t *dle;
		bplist_t to_free;
		ll = kmem_zalloc(sizeof (dsl_deadlist_t), KM_SLEEP);
		dsl_deadlist_open(ll, mos, ll_obj);
		dle = dsl_deadlist_first(ll);
		ASSERT3P(dle, !=, NULL);
		bplist_create(&to_free);
		int err = dsl_process_sub_livelist(&dle->dle_bpobj, &to_free,
		    z, NULL);
		if (err == 0) {
			sublist_delete_arg_t sync_arg = {
			    .spa = spa,
			    .ll = ll,
			    .key = dle->dle_mintxg,
			    .to_free = &to_free
			};
			zfs_dbgmsg("deleting sublist (id %llu) from"
			    " livelist %llu, %lld remaining",
			    (u_longlong_t)dle->dle_bpobj.bpo_object,
			    (u_longlong_t)ll_obj, (longlong_t)count - 1);
			VERIFY0(dsl_sync_task(spa_name(spa), NULL,
			    sublist_delete_sync, &sync_arg, 0,
			    ZFS_SPACE_CHECK_DESTROY));
		} else {
			VERIFY3U(err, ==, EINTR);
		}
		bplist_clear(&to_free);
		bplist_destroy(&to_free);
		dsl_deadlist_close(ll);
		kmem_free(ll, sizeof (dsl_deadlist_t));
	} else {
		livelist_delete_arg_t sync_arg = {
		    .spa = spa,
		    .ll_obj = ll_obj,
		    .zap_obj = zap_obj
		};
		zfs_dbgmsg("deletion of livelist %llu completed",
		    (u_longlong_t)ll_obj);
		VERIFY0(dsl_sync_task(spa_name(spa), NULL, livelist_delete_sync,
		    &sync_arg, 0, ZFS_SPACE_CHECK_DESTROY));
	}
}

static void
spa_start_livelist_destroy_thread(spa_t *spa)
{
	ASSERT3P(spa->spa_livelist_delete_zthr, ==, NULL);
	spa->spa_livelist_delete_zthr =
	    zthr_create("z_livelist_destroy",
	    spa_livelist_delete_cb_check, spa_livelist_delete_cb, spa,
	    minclsyspri);
}

typedef struct livelist_new_arg {
	bplist_t *allocs;
	bplist_t *frees;
} livelist_new_arg_t;

static int
livelist_track_new_cb(void *arg, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	ASSERT(tx == NULL);
	livelist_new_arg_t *lna = arg;
	if (bp_freed) {
		bplist_append(lna->frees, bp);
	} else {
		bplist_append(lna->allocs, bp);
		zfs_livelist_condense_new_alloc++;
	}
	return (0);
}

typedef struct livelist_condense_arg {
	spa_t *spa;
	bplist_t to_keep;
	uint64_t first_size;
	uint64_t next_size;
} livelist_condense_arg_t;

static void
spa_livelist_condense_sync(void *arg, dmu_tx_t *tx)
{
	livelist_condense_arg_t *lca = arg;
	spa_t *spa = lca->spa;
	bplist_t new_frees;
	dsl_dataset_t *ds = spa->spa_to_condense.ds;

	/* Have we been cancelled? */
	if (spa->spa_to_condense.cancelled) {
		zfs_livelist_condense_sync_cancel++;
		goto out;
	}

	dsl_deadlist_entry_t *first = spa->spa_to_condense.first;
	dsl_deadlist_entry_t *next = spa->spa_to_condense.next;
	dsl_deadlist_t *ll = &ds->ds_dir->dd_livelist;

	/*
	 * It's possible that the livelist was changed while the zthr was
	 * running. Therefore, we need to check for new blkptrs in the two
	 * entries being condensed and continue to track them in the livelist.
	 * Because of the way we handle remapped blkptrs (see dbuf_remap_impl),
	 * it's possible that the newly added blkptrs are FREEs or ALLOCs so
	 * we need to sort them into two different bplists.
	 */
	uint64_t first_obj = first->dle_bpobj.bpo_object;
	uint64_t next_obj = next->dle_bpobj.bpo_object;
	uint64_t cur_first_size = first->dle_bpobj.bpo_phys->bpo_num_blkptrs;
	uint64_t cur_next_size = next->dle_bpobj.bpo_phys->bpo_num_blkptrs;

	bplist_create(&new_frees);
	livelist_new_arg_t new_bps = {
	    .allocs = &lca->to_keep,
	    .frees = &new_frees,
	};

	if (cur_first_size > lca->first_size) {
		VERIFY0(livelist_bpobj_iterate_from_nofree(&first->dle_bpobj,
		    livelist_track_new_cb, &new_bps, lca->first_size));
	}
	if (cur_next_size > lca->next_size) {
		VERIFY0(livelist_bpobj_iterate_from_nofree(&next->dle_bpobj,
		    livelist_track_new_cb, &new_bps, lca->next_size));
	}

	dsl_deadlist_clear_entry(first, ll, tx);
	ASSERT(bpobj_is_empty(&first->dle_bpobj));
	dsl_deadlist_remove_entry(ll, next->dle_mintxg, tx);

	bplist_iterate(&lca->to_keep, dsl_deadlist_insert_alloc_cb, ll, tx);
	bplist_iterate(&new_frees, dsl_deadlist_insert_free_cb, ll, tx);
	bplist_destroy(&new_frees);

	char dsname[ZFS_MAX_DATASET_NAME_LEN];
	dsl_dataset_name(ds, dsname);
	zfs_dbgmsg("txg %llu condensing livelist of %s (id %llu), bpobj %llu "
	    "(%llu blkptrs) and bpobj %llu (%llu blkptrs) -> bpobj %llu "
	    "(%llu blkptrs)", (u_longlong_t)tx->tx_txg, dsname,
	    (u_longlong_t)ds->ds_object, (u_longlong_t)first_obj,
	    (u_longlong_t)cur_first_size, (u_longlong_t)next_obj,
	    (u_longlong_t)cur_next_size,
	    (u_longlong_t)first->dle_bpobj.bpo_object,
	    (u_longlong_t)first->dle_bpobj.bpo_phys->bpo_num_blkptrs);
out:
	dmu_buf_rele(ds->ds_dbuf, spa);
	spa->spa_to_condense.ds = NULL;
	bplist_clear(&lca->to_keep);
	bplist_destroy(&lca->to_keep);
	kmem_free(lca, sizeof (livelist_condense_arg_t));
	spa->spa_to_condense.syncing = B_FALSE;
}

static void
spa_livelist_condense_cb(void *arg, zthr_t *t)
{
	while (zfs_livelist_condense_zthr_pause &&
	    !(zthr_has_waiters(t) || zthr_iscancelled(t)))
		delay(1);

	spa_t *spa = arg;
	dsl_deadlist_entry_t *first = spa->spa_to_condense.first;
	dsl_deadlist_entry_t *next = spa->spa_to_condense.next;
	uint64_t first_size, next_size;

	livelist_condense_arg_t *lca =
	    kmem_alloc(sizeof (livelist_condense_arg_t), KM_SLEEP);
	bplist_create(&lca->to_keep);

	/*
	 * Process the livelists (matching FREEs and ALLOCs) in open context
	 * so we have minimal work in syncing context to condense.
	 *
	 * We save bpobj sizes (first_size and next_size) to use later in
	 * syncing context to determine if entries were added to these sublists
	 * while in open context. This is possible because the clone is still
	 * active and open for normal writes and we want to make sure the new,
	 * unprocessed blockpointers are inserted into the livelist normally.
	 *
	 * Note that dsl_process_sub_livelist() both stores the size number of
	 * blockpointers and iterates over them while the bpobj's lock held, so
	 * the sizes returned to us are consistent which what was actually
	 * processed.
	 */
	int err = dsl_process_sub_livelist(&first->dle_bpobj, &lca->to_keep, t,
	    &first_size);
	if (err == 0)
		err = dsl_process_sub_livelist(&next->dle_bpobj, &lca->to_keep,
		    t, &next_size);

	if (err == 0) {
		while (zfs_livelist_condense_sync_pause &&
		    !(zthr_has_waiters(t) || zthr_iscancelled(t)))
			delay(1);

		dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
		dmu_tx_mark_netfree(tx);
		dmu_tx_hold_space(tx, 1);
		err = dmu_tx_assign(tx, TXG_NOWAIT | TXG_NOTHROTTLE);
		if (err == 0) {
			/*
			 * Prevent the condense zthr restarting before
			 * the synctask completes.
			 */
			spa->spa_to_condense.syncing = B_TRUE;
			lca->spa = spa;
			lca->first_size = first_size;
			lca->next_size = next_size;
			dsl_sync_task_nowait(spa_get_dsl(spa),
			    spa_livelist_condense_sync, lca, tx);
			dmu_tx_commit(tx);
			return;
		}
	}
	/*
	 * Condensing can not continue: either it was externally stopped or
	 * we were unable to assign to a tx because the pool has run out of
	 * space. In the second case, we'll just end up trying to condense
	 * again in a later txg.
	 */
	ASSERT(err != 0);
	bplist_clear(&lca->to_keep);
	bplist_destroy(&lca->to_keep);
	kmem_free(lca, sizeof (livelist_condense_arg_t));
	dmu_buf_rele(spa->spa_to_condense.ds->ds_dbuf, spa);
	spa->spa_to_condense.ds = NULL;
	if (err == EINTR)
		zfs_livelist_condense_zthr_cancel++;
}

/*
 * Check that there is something to condense but that a condense is not
 * already in progress and that condensing has not been cancelled.
 */
static boolean_t
spa_livelist_condense_cb_check(void *arg, zthr_t *z)
{
	(void) z;
	spa_t *spa = arg;
	if ((spa->spa_to_condense.ds != NULL) &&
	    (spa->spa_to_condense.syncing == B_FALSE) &&
	    (spa->spa_to_condense.cancelled == B_FALSE)) {
		return (B_TRUE);
	}
	return (B_FALSE);
}

static void
spa_start_livelist_condensing_thread(spa_t *spa)
{
	spa->spa_to_condense.ds = NULL;
	spa->spa_to_condense.first = NULL;
	spa->spa_to_condense.next = NULL;
	spa->spa_to_condense.syncing = B_FALSE;
	spa->spa_to_condense.cancelled = B_FALSE;

	ASSERT3P(spa->spa_livelist_condense_zthr, ==, NULL);
	spa->spa_livelist_condense_zthr =
	    zthr_create("z_livelist_condense",
	    spa_livelist_condense_cb_check,
	    spa_livelist_condense_cb, spa, minclsyspri);
}

static void
spa_spawn_aux_threads(spa_t *spa)
{
	ASSERT(spa_writeable(spa));

	spa_start_raidz_expansion_thread(spa);
	spa_start_indirect_condensing_thread(spa);
	spa_start_livelist_destroy_thread(spa);
	spa_start_livelist_condensing_thread(spa);

	ASSERT3P(spa->spa_checkpoint_discard_zthr, ==, NULL);
	spa->spa_checkpoint_discard_zthr =
	    zthr_create("z_checkpoint_discard",
	    spa_checkpoint_discard_thread_check,
	    spa_checkpoint_discard_thread, spa, minclsyspri);
}

/*
 * Fix up config after a partly-completed split.  This is done with the
 * ZPOOL_CONFIG_SPLIT nvlist.  Both the splitting pool and the split-off
 * pool have that entry in their config, but only the splitting one contains
 * a list of all the guids of the vdevs that are being split off.
 *
 * This function determines what to do with that list: either rejoin
 * all the disks to the pool, or complete the splitting process.  To attempt
 * the rejoin, each disk that is offlined is marked online again, and
 * we do a reopen() call.  If the vdev label for every disk that was
 * marked online indicates it was successfully split off (VDEV_AUX_SPLIT_POOL)
 * then we call vdev_split() on each disk, and complete the split.
 *
 * Otherwise we leave the config alone, with all the vdevs in place in
 * the original pool.
 */
static void
spa_try_repair(spa_t *spa, nvlist_t *config)
{
	uint_t extracted;
	uint64_t *glist;
	uint_t i, gcount;
	nvlist_t *nvl;
	vdev_t **vd;
	boolean_t attempt_reopen;

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_SPLIT, &nvl) != 0)
		return;

	/* check that the config is complete */
	if (nvlist_lookup_uint64_array(nvl, ZPOOL_CONFIG_SPLIT_LIST,
	    &glist, &gcount) != 0)
		return;

	vd = kmem_zalloc(gcount * sizeof (vdev_t *), KM_SLEEP);

	/* attempt to online all the vdevs & validate */
	attempt_reopen = B_TRUE;
	for (i = 0; i < gcount; i++) {
		if (glist[i] == 0)	/* vdev is hole */
			continue;

		vd[i] = spa_lookup_by_guid(spa, glist[i], B_FALSE);
		if (vd[i] == NULL) {
			/*
			 * Don't bother attempting to reopen the disks;
			 * just do the split.
			 */
			attempt_reopen = B_FALSE;
		} else {
			/* attempt to re-online it */
			vd[i]->vdev_offline = B_FALSE;
		}
	}

	if (attempt_reopen) {
		vdev_reopen(spa->spa_root_vdev);

		/* check each device to see what state it's in */
		for (extracted = 0, i = 0; i < gcount; i++) {
			if (vd[i] != NULL &&
			    vd[i]->vdev_stat.vs_aux != VDEV_AUX_SPLIT_POOL)
				break;
			++extracted;
		}
	}

	/*
	 * If every disk has been moved to the new pool, or if we never
	 * even attempted to look at them, then we split them off for
	 * good.
	 */
	if (!attempt_reopen || gcount == extracted) {
		for (i = 0; i < gcount; i++)
			if (vd[i] != NULL)
				vdev_split(vd[i]);
		vdev_reopen(spa->spa_root_vdev);
	}

	kmem_free(vd, gcount * sizeof (vdev_t *));
}

static int
spa_load(spa_t *spa, spa_load_state_t state, spa_import_type_t type)
{
	const char *ereport = FM_EREPORT_ZFS_POOL;
	int error;

	spa->spa_load_state = state;
	(void) spa_import_progress_set_state(spa_guid(spa),
	    spa_load_state(spa));
	spa_import_progress_set_notes(spa, "spa_load()");

	gethrestime(&spa->spa_loaded_ts);
	error = spa_load_impl(spa, type, &ereport);

	/*
	 * Don't count references from objsets that are already closed
	 * and are making their way through the eviction process.
	 */
	spa_evicting_os_wait(spa);
	spa->spa_minref = zfs_refcount_count(&spa->spa_refcount);
	if (error) {
		if (error != EEXIST) {
			spa->spa_loaded_ts.tv_sec = 0;
			spa->spa_loaded_ts.tv_nsec = 0;
		}
		if (error != EBADF) {
			(void) zfs_ereport_post(ereport, spa,
			    NULL, NULL, NULL, 0);
		}
	}
	spa->spa_load_state = error ? SPA_LOAD_ERROR : SPA_LOAD_NONE;
	spa->spa_ena = 0;

	(void) spa_import_progress_set_state(spa_guid(spa),
	    spa_load_state(spa));

	return (error);
}

#ifdef ZFS_DEBUG
/*
 * Count the number of per-vdev ZAPs associated with all of the vdevs in the
 * vdev tree rooted in the given vd, and ensure that each ZAP is present in the
 * spa's per-vdev ZAP list.
 */
static uint64_t
vdev_count_verify_zaps(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	uint64_t total = 0;

	if (spa_feature_is_active(vd->vdev_spa, SPA_FEATURE_AVZ_V2) &&
	    vd->vdev_root_zap != 0) {
		total++;
		ASSERT0(zap_lookup_int(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, vd->vdev_root_zap));
	}
	if (vd->vdev_top_zap != 0) {
		total++;
		ASSERT0(zap_lookup_int(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, vd->vdev_top_zap));
	}
	if (vd->vdev_leaf_zap != 0) {
		total++;
		ASSERT0(zap_lookup_int(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, vd->vdev_leaf_zap));
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		total += vdev_count_verify_zaps(vd->vdev_child[i]);
	}

	return (total);
}
#else
#define	vdev_count_verify_zaps(vd) ((void) sizeof (vd), 0)
#endif

/*
 * Determine whether the activity check is required.
 */
static boolean_t
spa_activity_check_required(spa_t *spa, uberblock_t *ub, nvlist_t *label,
    nvlist_t *config)
{
	uint64_t state = 0;
	uint64_t hostid = 0;
	uint64_t tryconfig_txg = 0;
	uint64_t tryconfig_timestamp = 0;
	uint16_t tryconfig_mmp_seq = 0;
	nvlist_t *nvinfo;

	if (nvlist_exists(config, ZPOOL_CONFIG_LOAD_INFO)) {
		nvinfo = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO);
		(void) nvlist_lookup_uint64(nvinfo, ZPOOL_CONFIG_MMP_TXG,
		    &tryconfig_txg);
		(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_TIMESTAMP,
		    &tryconfig_timestamp);
		(void) nvlist_lookup_uint16(nvinfo, ZPOOL_CONFIG_MMP_SEQ,
		    &tryconfig_mmp_seq);
	}

	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE, &state);

	/*
	 * Disable the MMP activity check - This is used by zdb which
	 * is intended to be used on potentially active pools.
	 */
	if (spa->spa_import_flags & ZFS_IMPORT_SKIP_MMP)
		return (B_FALSE);

	/*
	 * Skip the activity check when the MMP feature is disabled.
	 */
	if (ub->ub_mmp_magic == MMP_MAGIC && ub->ub_mmp_delay == 0)
		return (B_FALSE);

	/*
	 * If the tryconfig_ values are nonzero, they are the results of an
	 * earlier tryimport.  If they all match the uberblock we just found,
	 * then the pool has not changed and we return false so we do not test
	 * a second time.
	 */
	if (tryconfig_txg && tryconfig_txg == ub->ub_txg &&
	    tryconfig_timestamp && tryconfig_timestamp == ub->ub_timestamp &&
	    tryconfig_mmp_seq && tryconfig_mmp_seq ==
	    (MMP_SEQ_VALID(ub) ? MMP_SEQ(ub) : 0))
		return (B_FALSE);

	/*
	 * Allow the activity check to be skipped when importing the pool
	 * on the same host which last imported it.  Since the hostid from
	 * configuration may be stale use the one read from the label.
	 */
	if (nvlist_exists(label, ZPOOL_CONFIG_HOSTID))
		hostid = fnvlist_lookup_uint64(label, ZPOOL_CONFIG_HOSTID);

	if (hostid == spa_get_hostid(spa))
		return (B_FALSE);

	/*
	 * Skip the activity test when the pool was cleanly exported.
	 */
	if (state != POOL_STATE_ACTIVE)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Nanoseconds the activity check must watch for changes on-disk.
 */
static uint64_t
spa_activity_check_duration(spa_t *spa, uberblock_t *ub)
{
	uint64_t import_intervals = MAX(zfs_multihost_import_intervals, 1);
	uint64_t multihost_interval = MSEC2NSEC(
	    MMP_INTERVAL_OK(zfs_multihost_interval));
	uint64_t import_delay = MAX(NANOSEC, import_intervals *
	    multihost_interval);

	/*
	 * Local tunables determine a minimum duration except for the case
	 * where we know when the remote host will suspend the pool if MMP
	 * writes do not land.
	 *
	 * See Big Theory comment at the top of mmp.c for the reasoning behind
	 * these cases and times.
	 */

	ASSERT(MMP_IMPORT_SAFETY_FACTOR >= 100);

	if (MMP_INTERVAL_VALID(ub) && MMP_FAIL_INT_VALID(ub) &&
	    MMP_FAIL_INT(ub) > 0) {

		/* MMP on remote host will suspend pool after failed writes */
		import_delay = MMP_FAIL_INT(ub) * MSEC2NSEC(MMP_INTERVAL(ub)) *
		    MMP_IMPORT_SAFETY_FACTOR / 100;

		zfs_dbgmsg("fail_intvals>0 import_delay=%llu ub_mmp "
		    "mmp_fails=%llu ub_mmp mmp_interval=%llu "
		    "import_intervals=%llu", (u_longlong_t)import_delay,
		    (u_longlong_t)MMP_FAIL_INT(ub),
		    (u_longlong_t)MMP_INTERVAL(ub),
		    (u_longlong_t)import_intervals);

	} else if (MMP_INTERVAL_VALID(ub) && MMP_FAIL_INT_VALID(ub) &&
	    MMP_FAIL_INT(ub) == 0) {

		/* MMP on remote host will never suspend pool */
		import_delay = MAX(import_delay, (MSEC2NSEC(MMP_INTERVAL(ub)) +
		    ub->ub_mmp_delay) * import_intervals);

		zfs_dbgmsg("fail_intvals=0 import_delay=%llu ub_mmp "
		    "mmp_interval=%llu ub_mmp_delay=%llu "
		    "import_intervals=%llu", (u_longlong_t)import_delay,
		    (u_longlong_t)MMP_INTERVAL(ub),
		    (u_longlong_t)ub->ub_mmp_delay,
		    (u_longlong_t)import_intervals);

	} else if (MMP_VALID(ub)) {
		/*
		 * zfs-0.7 compatibility case
		 */

		import_delay = MAX(import_delay, (multihost_interval +
		    ub->ub_mmp_delay) * import_intervals);

		zfs_dbgmsg("import_delay=%llu ub_mmp_delay=%llu "
		    "import_intervals=%llu leaves=%u",
		    (u_longlong_t)import_delay,
		    (u_longlong_t)ub->ub_mmp_delay,
		    (u_longlong_t)import_intervals,
		    vdev_count_leaves(spa));
	} else {
		/* Using local tunings is the only reasonable option */
		zfs_dbgmsg("pool last imported on non-MMP aware "
		    "host using import_delay=%llu multihost_interval=%llu "
		    "import_intervals=%llu", (u_longlong_t)import_delay,
		    (u_longlong_t)multihost_interval,
		    (u_longlong_t)import_intervals);
	}

	return (import_delay);
}

/*
 * Perform the import activity check.  If the user canceled the import or
 * we detected activity then fail.
 */
static int
spa_activity_check(spa_t *spa, uberblock_t *ub, nvlist_t *config)
{
	uint64_t txg = ub->ub_txg;
	uint64_t timestamp = ub->ub_timestamp;
	uint64_t mmp_config = ub->ub_mmp_config;
	uint16_t mmp_seq = MMP_SEQ_VALID(ub) ? MMP_SEQ(ub) : 0;
	uint64_t import_delay;
	hrtime_t import_expire, now;
	nvlist_t *mmp_label = NULL;
	vdev_t *rvd = spa->spa_root_vdev;
	kcondvar_t cv;
	kmutex_t mtx;
	int error = 0;

	cv_init(&cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_enter(&mtx);

	/*
	 * If ZPOOL_CONFIG_MMP_TXG is present an activity check was performed
	 * during the earlier tryimport.  If the txg recorded there is 0 then
	 * the pool is known to be active on another host.
	 *
	 * Otherwise, the pool might be in use on another host.  Check for
	 * changes in the uberblocks on disk if necessary.
	 */
	if (nvlist_exists(config, ZPOOL_CONFIG_LOAD_INFO)) {
		nvlist_t *nvinfo = fnvlist_lookup_nvlist(config,
		    ZPOOL_CONFIG_LOAD_INFO);

		if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_TXG) &&
		    fnvlist_lookup_uint64(nvinfo, ZPOOL_CONFIG_MMP_TXG) == 0) {
			vdev_uberblock_load(rvd, ub, &mmp_label);
			error = SET_ERROR(EREMOTEIO);
			goto out;
		}
	}

	import_delay = spa_activity_check_duration(spa, ub);

	/* Add a small random factor in case of simultaneous imports (0-25%) */
	import_delay += import_delay * random_in_range(250) / 1000;

	import_expire = gethrtime() + import_delay;

	spa_import_progress_set_notes(spa, "Checking MMP activity, waiting "
	    "%llu ms", (u_longlong_t)NSEC2MSEC(import_delay));

	int interations = 0;
	while ((now = gethrtime()) < import_expire) {
		if (interations++ % 30 == 0) {
			spa_import_progress_set_notes(spa, "Checking MMP "
			    "activity, %llu ms remaining",
			    (u_longlong_t)NSEC2MSEC(import_expire - now));
		}

		(void) spa_import_progress_set_mmp_check(spa_guid(spa),
		    NSEC2SEC(import_expire - gethrtime()));

		vdev_uberblock_load(rvd, ub, &mmp_label);

		if (txg != ub->ub_txg || timestamp != ub->ub_timestamp ||
		    mmp_seq != (MMP_SEQ_VALID(ub) ? MMP_SEQ(ub) : 0)) {
			zfs_dbgmsg("multihost activity detected "
			    "txg %llu ub_txg  %llu "
			    "timestamp %llu ub_timestamp  %llu "
			    "mmp_config %#llx ub_mmp_config %#llx",
			    (u_longlong_t)txg, (u_longlong_t)ub->ub_txg,
			    (u_longlong_t)timestamp,
			    (u_longlong_t)ub->ub_timestamp,
			    (u_longlong_t)mmp_config,
			    (u_longlong_t)ub->ub_mmp_config);

			error = SET_ERROR(EREMOTEIO);
			break;
		}

		if (mmp_label) {
			nvlist_free(mmp_label);
			mmp_label = NULL;
		}

		error = cv_timedwait_sig(&cv, &mtx, ddi_get_lbolt() + hz);
		if (error != -1) {
			error = SET_ERROR(EINTR);
			break;
		}
		error = 0;
	}

out:
	mutex_exit(&mtx);
	mutex_destroy(&mtx);
	cv_destroy(&cv);

	/*
	 * If the pool is determined to be active store the status in the
	 * spa->spa_load_info nvlist.  If the remote hostname or hostid are
	 * available from configuration read from disk store them as well.
	 * This allows 'zpool import' to generate a more useful message.
	 *
	 * ZPOOL_CONFIG_MMP_STATE    - observed pool status (mandatory)
	 * ZPOOL_CONFIG_MMP_HOSTNAME - hostname from the active pool
	 * ZPOOL_CONFIG_MMP_HOSTID   - hostid from the active pool
	 */
	if (error == EREMOTEIO) {
		const char *hostname = "<unknown>";
		uint64_t hostid = 0;

		if (mmp_label) {
			if (nvlist_exists(mmp_label, ZPOOL_CONFIG_HOSTNAME)) {
				hostname = fnvlist_lookup_string(mmp_label,
				    ZPOOL_CONFIG_HOSTNAME);
				fnvlist_add_string(spa->spa_load_info,
				    ZPOOL_CONFIG_MMP_HOSTNAME, hostname);
			}

			if (nvlist_exists(mmp_label, ZPOOL_CONFIG_HOSTID)) {
				hostid = fnvlist_lookup_uint64(mmp_label,
				    ZPOOL_CONFIG_HOSTID);
				fnvlist_add_uint64(spa->spa_load_info,
				    ZPOOL_CONFIG_MMP_HOSTID, hostid);
			}
		}

		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_MMP_STATE, MMP_STATE_ACTIVE);
		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_MMP_TXG, 0);

		error = spa_vdev_err(rvd, VDEV_AUX_ACTIVE, EREMOTEIO);
	}

	if (mmp_label)
		nvlist_free(mmp_label);

	return (error);
}

static int
spa_verify_host(spa_t *spa, nvlist_t *mos_config)
{
	uint64_t hostid;
	const char *hostname;
	uint64_t myhostid = 0;

	if (!spa_is_root(spa) && nvlist_lookup_uint64(mos_config,
	    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
		hostname = fnvlist_lookup_string(mos_config,
		    ZPOOL_CONFIG_HOSTNAME);

		myhostid = zone_get_hostid(NULL);

		if (hostid != 0 && myhostid != 0 && hostid != myhostid) {
			cmn_err(CE_WARN, "pool '%s' could not be "
			    "loaded as it was last accessed by "
			    "another system (host: %s hostid: 0x%llx). "
			    "See: https://openzfs.github.io/openzfs-docs/msg/"
			    "ZFS-8000-EY",
			    spa_name(spa), hostname, (u_longlong_t)hostid);
			spa_load_failed(spa, "hostid verification failed: pool "
			    "last accessed by host: %s (hostid: 0x%llx)",
			    hostname, (u_longlong_t)hostid);
			return (SET_ERROR(EBADF));
		}
	}

	return (0);
}

static int
spa_ld_parse_config(spa_t *spa, spa_import_type_t type)
{
	int error = 0;
	nvlist_t *nvtree, *nvl, *config = spa->spa_config;
	int parse;
	vdev_t *rvd;
	uint64_t pool_guid;
	const char *comment;
	const char *compatibility;

	/*
	 * Versioning wasn't explicitly added to the label until later, so if
	 * it's not present treat it as the initial version.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &spa->spa_ubsync.ub_version) != 0)
		spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pool_guid)) {
		spa_load_failed(spa, "invalid config provided: '%s' missing",
		    ZPOOL_CONFIG_POOL_GUID);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If we are doing an import, ensure that the pool is not already
	 * imported by checking if its pool guid already exists in the
	 * spa namespace.
	 *
	 * The only case that we allow an already imported pool to be
	 * imported again, is when the pool is checkpointed and we want to
	 * look at its checkpointed state from userland tools like zdb.
	 */
#ifdef _KERNEL
	if ((spa->spa_load_state == SPA_LOAD_IMPORT ||
	    spa->spa_load_state == SPA_LOAD_TRYIMPORT) &&
	    spa_guid_exists(pool_guid, 0)) {
#else
	if ((spa->spa_load_state == SPA_LOAD_IMPORT ||
	    spa->spa_load_state == SPA_LOAD_TRYIMPORT) &&
	    spa_guid_exists(pool_guid, 0) &&
	    !spa_importing_readonly_checkpoint(spa)) {
#endif
		spa_load_failed(spa, "a pool with guid %llu is already open",
		    (u_longlong_t)pool_guid);
		return (SET_ERROR(EEXIST));
	}

	spa->spa_config_guid = pool_guid;

	nvlist_free(spa->spa_load_info);
	spa->spa_load_info = fnvlist_alloc();

	ASSERT(spa->spa_comment == NULL);
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMMENT, &comment) == 0)
		spa->spa_comment = spa_strdup(comment);

	ASSERT(spa->spa_compatibility == NULL);
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMPATIBILITY,
	    &compatibility) == 0)
		spa->spa_compatibility = spa_strdup(compatibility);

	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &spa->spa_config_txg);

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_SPLIT, &nvl) == 0)
		spa->spa_config_splitting = fnvlist_dup(nvl);

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvtree)) {
		spa_load_failed(spa, "invalid config provided: '%s' missing",
		    ZPOOL_CONFIG_VDEV_TREE);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Create "The Godfather" zio to hold all async IOs
	 */
	spa->spa_async_zio_root = kmem_alloc(max_ncpus * sizeof (void *),
	    KM_SLEEP);
	for (int i = 0; i < max_ncpus; i++) {
		spa->spa_async_zio_root[i] = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
		    ZIO_FLAG_GODFATHER);
	}

	/*
	 * Parse the configuration into a vdev tree.  We explicitly set the
	 * value that will be returned by spa_version() since parsing the
	 * configuration requires knowing the version number.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	parse = (type == SPA_IMPORT_EXISTING ?
	    VDEV_ALLOC_LOAD : VDEV_ALLOC_SPLIT);
	error = spa_config_parse(spa, &rvd, nvtree, NULL, 0, parse);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_load_failed(spa, "unable to parse config [error=%d]",
		    error);
		return (error);
	}

	ASSERT(spa->spa_root_vdev == rvd);
	ASSERT3U(spa->spa_min_ashift, >=, SPA_MINBLOCKSHIFT);
	ASSERT3U(spa->spa_max_ashift, <=, SPA_MAXBLOCKSHIFT);

	if (type != SPA_IMPORT_ASSEMBLE) {
		ASSERT(spa_guid(spa) == pool_guid);
	}

	return (0);
}

/*
 * Recursively open all vdevs in the vdev tree. This function is called twice:
 * first with the untrusted config, then with the trusted config.
 */
static int
spa_ld_open_vdevs(spa_t *spa)
{
	int error = 0;

	/*
	 * spa_missing_tvds_allowed defines how many top-level vdevs can be
	 * missing/unopenable for the root vdev to be still considered openable.
	 */
	if (spa->spa_trust_config) {
		spa->spa_missing_tvds_allowed = zfs_max_missing_tvds;
	} else if (spa->spa_config_source == SPA_CONFIG_SRC_CACHEFILE) {
		spa->spa_missing_tvds_allowed = zfs_max_missing_tvds_cachefile;
	} else if (spa->spa_config_source == SPA_CONFIG_SRC_SCAN) {
		spa->spa_missing_tvds_allowed = zfs_max_missing_tvds_scan;
	} else {
		spa->spa_missing_tvds_allowed = 0;
	}

	spa->spa_missing_tvds_allowed =
	    MAX(zfs_max_missing_tvds, spa->spa_missing_tvds_allowed);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = vdev_open(spa->spa_root_vdev);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (spa->spa_missing_tvds != 0) {
		spa_load_note(spa, "vdev tree has %lld missing top-level "
		    "vdevs.", (u_longlong_t)spa->spa_missing_tvds);
		if (spa->spa_trust_config && (spa->spa_mode & SPA_MODE_WRITE)) {
			/*
			 * Although theoretically we could allow users to open
			 * incomplete pools in RW mode, we'd need to add a lot
			 * of extra logic (e.g. adjust pool space to account
			 * for missing vdevs).
			 * This limitation also prevents users from accidentally
			 * opening the pool in RW mode during data recovery and
			 * damaging it further.
			 */
			spa_load_note(spa, "pools with missing top-level "
			    "vdevs can only be opened in read-only mode.");
			error = SET_ERROR(ENXIO);
		} else {
			spa_load_note(spa, "current settings allow for maximum "
			    "%lld missing top-level vdevs at this stage.",
			    (u_longlong_t)spa->spa_missing_tvds_allowed);
		}
	}
	if (error != 0) {
		spa_load_failed(spa, "unable to open vdev tree [error=%d]",
		    error);
	}
	if (spa->spa_missing_tvds != 0 || error != 0)
		vdev_dbgmsg_print_tree(spa->spa_root_vdev, 2);

	return (error);
}

/*
 * We need to validate the vdev labels against the configuration that
 * we have in hand. This function is called twice: first with an untrusted
 * config, then with a trusted config. The validation is more strict when the
 * config is trusted.
 */
static int
spa_ld_validate_vdevs(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = vdev_validate(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_load_failed(spa, "vdev_validate failed [error=%d]", error);
		return (error);
	}

	if (rvd->vdev_state <= VDEV_STATE_CANT_OPEN) {
		spa_load_failed(spa, "cannot open vdev tree after invalidating "
		    "some vdevs");
		vdev_dbgmsg_print_tree(rvd, 2);
		return (SET_ERROR(ENXIO));
	}

	return (0);
}

static void
spa_ld_select_uberblock_done(spa_t *spa, uberblock_t *ub)
{
	spa->spa_state = POOL_STATE_ACTIVE;
	spa->spa_ubsync = spa->spa_uberblock;
	spa->spa_verify_min_txg = spa->spa_extreme_rewind ?
	    TXG_INITIAL - 1 : spa_last_synced_txg(spa) - TXG_DEFER_SIZE - 1;
	spa->spa_first_txg = spa->spa_last_ubsync_txg ?
	    spa->spa_last_ubsync_txg : spa_last_synced_txg(spa) + 1;
	spa->spa_claim_max_txg = spa->spa_first_txg;
	spa->spa_prev_software_version = ub->ub_software_version;
}

static int
spa_ld_select_uberblock(spa_t *spa, spa_import_type_t type)
{
	vdev_t *rvd = spa->spa_root_vdev;
	nvlist_t *label;
	uberblock_t *ub = &spa->spa_uberblock;
	boolean_t activity_check = B_FALSE;

	/*
	 * If we are opening the checkpointed state of the pool by
	 * rewinding to it, at this point we will have written the
	 * checkpointed uberblock to the vdev labels, so searching
	 * the labels will find the right uberblock.  However, if
	 * we are opening the checkpointed state read-only, we have
	 * not modified the labels. Therefore, we must ignore the
	 * labels and continue using the spa_uberblock that was set
	 * by spa_ld_checkpoint_rewind.
	 *
	 * Note that it would be fine to ignore the labels when
	 * rewinding (opening writeable) as well. However, if we
	 * crash just after writing the labels, we will end up
	 * searching the labels. Doing so in the common case means
	 * that this code path gets exercised normally, rather than
	 * just in the edge case.
	 */
	if (ub->ub_checkpoint_txg != 0 &&
	    spa_importing_readonly_checkpoint(spa)) {
		spa_ld_select_uberblock_done(spa, ub);
		return (0);
	}

	/*
	 * Find the best uberblock.
	 */
	vdev_uberblock_load(rvd, ub, &label);

	/*
	 * If we weren't able to find a single valid uberblock, return failure.
	 */
	if (ub->ub_txg == 0) {
		nvlist_free(label);
		spa_load_failed(spa, "no valid uberblock found");
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, ENXIO));
	}

	if (spa->spa_load_max_txg != UINT64_MAX) {
		(void) spa_import_progress_set_max_txg(spa_guid(spa),
		    (u_longlong_t)spa->spa_load_max_txg);
	}
	spa_load_note(spa, "using uberblock with txg=%llu",
	    (u_longlong_t)ub->ub_txg);
	if (ub->ub_raidz_reflow_info != 0) {
		spa_load_note(spa, "uberblock raidz_reflow_info: "
		    "state=%u offset=%llu",
		    (int)RRSS_GET_STATE(ub),
		    (u_longlong_t)RRSS_GET_OFFSET(ub));
	}


	/*
	 * For pools which have the multihost property on determine if the
	 * pool is truly inactive and can be safely imported.  Prevent
	 * hosts which don't have a hostid set from importing the pool.
	 */
	activity_check = spa_activity_check_required(spa, ub, label,
	    spa->spa_config);
	if (activity_check) {
		if (ub->ub_mmp_magic == MMP_MAGIC && ub->ub_mmp_delay &&
		    spa_get_hostid(spa) == 0) {
			nvlist_free(label);
			fnvlist_add_uint64(spa->spa_load_info,
			    ZPOOL_CONFIG_MMP_STATE, MMP_STATE_NO_HOSTID);
			return (spa_vdev_err(rvd, VDEV_AUX_ACTIVE, EREMOTEIO));
		}

		int error = spa_activity_check(spa, ub, spa->spa_config);
		if (error) {
			nvlist_free(label);
			return (error);
		}

		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_MMP_STATE, MMP_STATE_INACTIVE);
		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_MMP_TXG, ub->ub_txg);
		fnvlist_add_uint16(spa->spa_load_info,
		    ZPOOL_CONFIG_MMP_SEQ,
		    (MMP_SEQ_VALID(ub) ? MMP_SEQ(ub) : 0));
	}

	/*
	 * If the pool has an unsupported version we can't open it.
	 */
	if (!SPA_VERSION_IS_SUPPORTED(ub->ub_version)) {
		nvlist_free(label);
		spa_load_failed(spa, "version %llu is not supported",
		    (u_longlong_t)ub->ub_version);
		return (spa_vdev_err(rvd, VDEV_AUX_VERSION_NEWER, ENOTSUP));
	}

	if (ub->ub_version >= SPA_VERSION_FEATURES) {
		nvlist_t *features;

		/*
		 * If we weren't able to find what's necessary for reading the
		 * MOS in the label, return failure.
		 */
		if (label == NULL) {
			spa_load_failed(spa, "label config unavailable");
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA,
			    ENXIO));
		}

		if (nvlist_lookup_nvlist(label, ZPOOL_CONFIG_FEATURES_FOR_READ,
		    &features) != 0) {
			nvlist_free(label);
			spa_load_failed(spa, "invalid label: '%s' missing",
			    ZPOOL_CONFIG_FEATURES_FOR_READ);
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA,
			    ENXIO));
		}

		/*
		 * Update our in-core representation with the definitive values
		 * from the label.
		 */
		nvlist_free(spa->spa_label_features);
		spa->spa_label_features = fnvlist_dup(features);
	}

	nvlist_free(label);

	/*
	 * Look through entries in the label nvlist's features_for_read. If
	 * there is a feature listed there which we don't understand then we
	 * cannot open a pool.
	 */
	if (ub->ub_version >= SPA_VERSION_FEATURES) {
		nvlist_t *unsup_feat;

		unsup_feat = fnvlist_alloc();

		for (nvpair_t *nvp = nvlist_next_nvpair(spa->spa_label_features,
		    NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(spa->spa_label_features, nvp)) {
			if (!zfeature_is_supported(nvpair_name(nvp))) {
				fnvlist_add_string(unsup_feat,
				    nvpair_name(nvp), "");
			}
		}

		if (!nvlist_empty(unsup_feat)) {
			fnvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_UNSUP_FEAT, unsup_feat);
			nvlist_free(unsup_feat);
			spa_load_failed(spa, "some features are unsupported");
			return (spa_vdev_err(rvd, VDEV_AUX_UNSUP_FEAT,
			    ENOTSUP));
		}

		nvlist_free(unsup_feat);
	}

	if (type != SPA_IMPORT_ASSEMBLE && spa->spa_config_splitting) {
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_try_repair(spa, spa->spa_config);
		spa_config_exit(spa, SCL_ALL, FTAG);
		nvlist_free(spa->spa_config_splitting);
		spa->spa_config_splitting = NULL;
	}

	/*
	 * Initialize internal SPA structures.
	 */
	spa_ld_select_uberblock_done(spa, ub);

	return (0);
}

static int
spa_ld_open_rootbp(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	error = dsl_pool_init(spa, spa->spa_first_txg, &spa->spa_dsl_pool);
	if (error != 0) {
		spa_load_failed(spa, "unable to open rootbp in dsl_pool_init "
		    "[error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}
	spa->spa_meta_objset = spa->spa_dsl_pool->dp_meta_objset;

	return (0);
}

static int
spa_ld_trusted_config(spa_t *spa, spa_import_type_t type,
    boolean_t reloading)
{
	vdev_t *mrvd, *rvd = spa->spa_root_vdev;
	nvlist_t *nv, *mos_config, *policy;
	int error = 0, copy_error;
	uint64_t healthy_tvds, healthy_tvds_mos;
	uint64_t mos_config_txg;

	if (spa_dir_prop(spa, DMU_POOL_CONFIG, &spa->spa_config_object, B_TRUE)
	    != 0)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * If we're assembling a pool from a split, the config provided is
	 * already trusted so there is nothing to do.
	 */
	if (type == SPA_IMPORT_ASSEMBLE)
		return (0);

	healthy_tvds = spa_healthy_core_tvds(spa);

	if (load_nvlist(spa, spa->spa_config_object, &mos_config)
	    != 0) {
		spa_load_failed(spa, "unable to retrieve MOS config");
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * If we are doing an open, pool owner wasn't verified yet, thus do
	 * the verification here.
	 */
	if (spa->spa_load_state == SPA_LOAD_OPEN) {
		error = spa_verify_host(spa, mos_config);
		if (error != 0) {
			nvlist_free(mos_config);
			return (error);
		}
	}

	nv = fnvlist_lookup_nvlist(mos_config, ZPOOL_CONFIG_VDEV_TREE);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * Build a new vdev tree from the trusted config
	 */
	error = spa_config_parse(spa, &mrvd, nv, NULL, 0, VDEV_ALLOC_LOAD);
	if (error != 0) {
		nvlist_free(mos_config);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa_load_failed(spa, "spa_config_parse failed [error=%d]",
		    error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, error));
	}

	/*
	 * Vdev paths in the MOS may be obsolete. If the untrusted config was
	 * obtained by scanning /dev/dsk, then it will have the right vdev
	 * paths. We update the trusted MOS config with this information.
	 * We first try to copy the paths with vdev_copy_path_strict, which
	 * succeeds only when both configs have exactly the same vdev tree.
	 * If that fails, we fall back to a more flexible method that has a
	 * best effort policy.
	 */
	copy_error = vdev_copy_path_strict(rvd, mrvd);
	if (copy_error != 0 || spa_load_print_vdev_tree) {
		spa_load_note(spa, "provided vdev tree:");
		vdev_dbgmsg_print_tree(rvd, 2);
		spa_load_note(spa, "MOS vdev tree:");
		vdev_dbgmsg_print_tree(mrvd, 2);
	}
	if (copy_error != 0) {
		spa_load_note(spa, "vdev_copy_path_strict failed, falling "
		    "back to vdev_copy_path_relaxed");
		vdev_copy_path_relaxed(rvd, mrvd);
	}

	vdev_close(rvd);
	vdev_free(rvd);
	spa->spa_root_vdev = mrvd;
	rvd = mrvd;
	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * If 'zpool import' used a cached config, then the on-disk hostid and
	 * hostname may be different to the cached config in ways that should
	 * prevent import.  Userspace can't discover this without a scan, but
	 * we know, so we add these values to LOAD_INFO so the caller can know
	 * the difference.
	 *
	 * Note that we have to do this before the config is regenerated,
	 * because the new config will have the hostid and hostname for this
	 * host, in readiness for import.
	 */
	if (nvlist_exists(mos_config, ZPOOL_CONFIG_HOSTID))
		fnvlist_add_uint64(spa->spa_load_info, ZPOOL_CONFIG_HOSTID,
		    fnvlist_lookup_uint64(mos_config, ZPOOL_CONFIG_HOSTID));
	if (nvlist_exists(mos_config, ZPOOL_CONFIG_HOSTNAME))
		fnvlist_add_string(spa->spa_load_info, ZPOOL_CONFIG_HOSTNAME,
		    fnvlist_lookup_string(mos_config, ZPOOL_CONFIG_HOSTNAME));

	/*
	 * We will use spa_config if we decide to reload the spa or if spa_load
	 * fails and we rewind. We must thus regenerate the config using the
	 * MOS information with the updated paths. ZPOOL_LOAD_POLICY is used to
	 * pass settings on how to load the pool and is not stored in the MOS.
	 * We copy it over to our new, trusted config.
	 */
	mos_config_txg = fnvlist_lookup_uint64(mos_config,
	    ZPOOL_CONFIG_POOL_TXG);
	nvlist_free(mos_config);
	mos_config = spa_config_generate(spa, NULL, mos_config_txg, B_FALSE);
	if (nvlist_lookup_nvlist(spa->spa_config, ZPOOL_LOAD_POLICY,
	    &policy) == 0)
		fnvlist_add_nvlist(mos_config, ZPOOL_LOAD_POLICY, policy);
	spa_config_set(spa, mos_config);
	spa->spa_config_source = SPA_CONFIG_SRC_MOS;

	/*
	 * Now that we got the config from the MOS, we should be more strict
	 * in checking blkptrs and can make assumptions about the consistency
	 * of the vdev tree. spa_trust_config must be set to true before opening
	 * vdevs in order for them to be writeable.
	 */
	spa->spa_trust_config = B_TRUE;

	/*
	 * Open and validate the new vdev tree
	 */
	error = spa_ld_open_vdevs(spa);
	if (error != 0)
		return (error);

	error = spa_ld_validate_vdevs(spa);
	if (error != 0)
		return (error);

	if (copy_error != 0 || spa_load_print_vdev_tree) {
		spa_load_note(spa, "final vdev tree:");
		vdev_dbgmsg_print_tree(rvd, 2);
	}

	if (spa->spa_load_state != SPA_LOAD_TRYIMPORT &&
	    !spa->spa_extreme_rewind && zfs_max_missing_tvds == 0) {
		/*
		 * Sanity check to make sure that we are indeed loading the
		 * latest uberblock. If we missed SPA_SYNC_MIN_VDEVS tvds
		 * in the config provided and they happened to be the only ones
		 * to have the latest uberblock, we could involuntarily perform
		 * an extreme rewind.
		 */
		healthy_tvds_mos = spa_healthy_core_tvds(spa);
		if (healthy_tvds_mos - healthy_tvds >=
		    SPA_SYNC_MIN_VDEVS) {
			spa_load_note(spa, "config provided misses too many "
			    "top-level vdevs compared to MOS (%lld vs %lld). ",
			    (u_longlong_t)healthy_tvds,
			    (u_longlong_t)healthy_tvds_mos);
			spa_load_note(spa, "vdev tree:");
			vdev_dbgmsg_print_tree(rvd, 2);
			if (reloading) {
				spa_load_failed(spa, "config was already "
				    "provided from MOS. Aborting.");
				return (spa_vdev_err(rvd,
				    VDEV_AUX_CORRUPT_DATA, EIO));
			}
			spa_load_note(spa, "spa must be reloaded using MOS "
			    "config");
			return (SET_ERROR(EAGAIN));
		}
	}

	error = spa_check_for_missing_logs(spa);
	if (error != 0)
		return (spa_vdev_err(rvd, VDEV_AUX_BAD_GUID_SUM, ENXIO));

	if (rvd->vdev_guid_sum != spa->spa_uberblock.ub_guid_sum) {
		spa_load_failed(spa, "uberblock guid sum doesn't match MOS "
		    "guid sum (%llu != %llu)",
		    (u_longlong_t)spa->spa_uberblock.ub_guid_sum,
		    (u_longlong_t)rvd->vdev_guid_sum);
		return (spa_vdev_err(rvd, VDEV_AUX_BAD_GUID_SUM,
		    ENXIO));
	}

	return (0);
}

static int
spa_ld_open_indirect_vdev_metadata(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * Everything that we read before spa_remove_init() must be stored
	 * on concreted vdevs.  Therefore we do this as early as possible.
	 */
	error = spa_remove_init(spa);
	if (error != 0) {
		spa_load_failed(spa, "spa_remove_init failed [error=%d]",
		    error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * Retrieve information needed to condense indirect vdev mappings.
	 */
	error = spa_condense_init(spa);
	if (error != 0) {
		spa_load_failed(spa, "spa_condense_init failed [error=%d]",
		    error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, error));
	}

	return (0);
}

static int
spa_ld_check_features(spa_t *spa, boolean_t *missing_feat_writep)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	if (spa_version(spa) >= SPA_VERSION_FEATURES) {
		boolean_t missing_feat_read = B_FALSE;
		nvlist_t *unsup_feat, *enabled_feat;

		if (spa_dir_prop(spa, DMU_POOL_FEATURES_FOR_READ,
		    &spa->spa_feat_for_read_obj, B_TRUE) != 0) {
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		if (spa_dir_prop(spa, DMU_POOL_FEATURES_FOR_WRITE,
		    &spa->spa_feat_for_write_obj, B_TRUE) != 0) {
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		if (spa_dir_prop(spa, DMU_POOL_FEATURE_DESCRIPTIONS,
		    &spa->spa_feat_desc_obj, B_TRUE) != 0) {
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		enabled_feat = fnvlist_alloc();
		unsup_feat = fnvlist_alloc();

		if (!spa_features_check(spa, B_FALSE,
		    unsup_feat, enabled_feat))
			missing_feat_read = B_TRUE;

		if (spa_writeable(spa) ||
		    spa->spa_load_state == SPA_LOAD_TRYIMPORT) {
			if (!spa_features_check(spa, B_TRUE,
			    unsup_feat, enabled_feat)) {
				*missing_feat_writep = B_TRUE;
			}
		}

		fnvlist_add_nvlist(spa->spa_load_info,
		    ZPOOL_CONFIG_ENABLED_FEAT, enabled_feat);

		if (!nvlist_empty(unsup_feat)) {
			fnvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_UNSUP_FEAT, unsup_feat);
		}

		fnvlist_free(enabled_feat);
		fnvlist_free(unsup_feat);

		if (!missing_feat_read) {
			fnvlist_add_boolean(spa->spa_load_info,
			    ZPOOL_CONFIG_CAN_RDONLY);
		}

		/*
		 * If the state is SPA_LOAD_TRYIMPORT, our objective is
		 * twofold: to determine whether the pool is available for
		 * import in read-write mode and (if it is not) whether the
		 * pool is available for import in read-only mode. If the pool
		 * is available for import in read-write mode, it is displayed
		 * as available in userland; if it is not available for import
		 * in read-only mode, it is displayed as unavailable in
		 * userland. If the pool is available for import in read-only
		 * mode but not read-write mode, it is displayed as unavailable
		 * in userland with a special note that the pool is actually
		 * available for open in read-only mode.
		 *
		 * As a result, if the state is SPA_LOAD_TRYIMPORT and we are
		 * missing a feature for write, we must first determine whether
		 * the pool can be opened read-only before returning to
		 * userland in order to know whether to display the
		 * abovementioned note.
		 */
		if (missing_feat_read || (*missing_feat_writep &&
		    spa_writeable(spa))) {
			spa_load_failed(spa, "pool uses unsupported features");
			return (spa_vdev_err(rvd, VDEV_AUX_UNSUP_FEAT,
			    ENOTSUP));
		}

		/*
		 * Load refcounts for ZFS features from disk into an in-memory
		 * cache during SPA initialization.
		 */
		for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
			uint64_t refcount;

			error = feature_get_refcount_from_disk(spa,
			    &spa_feature_table[i], &refcount);
			if (error == 0) {
				spa->spa_feat_refcount_cache[i] = refcount;
			} else if (error == ENOTSUP) {
				spa->spa_feat_refcount_cache[i] =
				    SPA_FEATURE_DISABLED;
			} else {
				spa_load_failed(spa, "error getting refcount "
				    "for feature %s [error=%d]",
				    spa_feature_table[i].fi_guid, error);
				return (spa_vdev_err(rvd,
				    VDEV_AUX_CORRUPT_DATA, EIO));
			}
		}
	}

	if (spa_feature_is_active(spa, SPA_FEATURE_ENABLED_TXG)) {
		if (spa_dir_prop(spa, DMU_POOL_FEATURE_ENABLED_TXG,
		    &spa->spa_feat_enabled_txg_obj, B_TRUE) != 0)
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * Encryption was added before bookmark_v2, even though bookmark_v2
	 * is now a dependency. If this pool has encryption enabled without
	 * bookmark_v2, trigger an errata message.
	 */
	if (spa_feature_is_enabled(spa, SPA_FEATURE_ENCRYPTION) &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_BOOKMARK_V2)) {
		spa->spa_errata = ZPOOL_ERRATA_ZOL_8308_ENCRYPTION;
	}

	return (0);
}

static int
spa_ld_load_special_directories(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	spa->spa_is_initializing = B_TRUE;
	error = dsl_pool_open(spa->spa_dsl_pool);
	spa->spa_is_initializing = B_FALSE;
	if (error != 0) {
		spa_load_failed(spa, "dsl_pool_open failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	return (0);
}

static int
spa_ld_get_props(spa_t *spa)
{
	int error = 0;
	uint64_t obj;
	vdev_t *rvd = spa->spa_root_vdev;

	/* Grab the checksum salt from the MOS. */
	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CHECKSUM_SALT, 1,
	    sizeof (spa->spa_cksum_salt.zcs_bytes),
	    spa->spa_cksum_salt.zcs_bytes);
	if (error == ENOENT) {
		/* Generate a new salt for subsequent use */
		(void) random_get_pseudo_bytes(spa->spa_cksum_salt.zcs_bytes,
		    sizeof (spa->spa_cksum_salt.zcs_bytes));
	} else if (error != 0) {
		spa_load_failed(spa, "unable to retrieve checksum salt from "
		    "MOS [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	if (spa_dir_prop(spa, DMU_POOL_SYNC_BPOBJ, &obj, B_TRUE) != 0)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	error = bpobj_open(&spa->spa_deferred_bpobj, spa->spa_meta_objset, obj);
	if (error != 0) {
		spa_load_failed(spa, "error opening deferred-frees bpobj "
		    "[error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * Load the bit that tells us to use the new accounting function
	 * (raid-z deflation).  If we have an older pool, this will not
	 * be present.
	 */
	error = spa_dir_prop(spa, DMU_POOL_DEFLATE, &spa->spa_deflate, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	error = spa_dir_prop(spa, DMU_POOL_CREATION_VERSION,
	    &spa->spa_creation_version, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the persistent error log.  If we have an older pool, this will
	 * not be present.
	 */
	error = spa_dir_prop(spa, DMU_POOL_ERRLOG_LAST, &spa->spa_errlog_last,
	    B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	error = spa_dir_prop(spa, DMU_POOL_ERRLOG_SCRUB,
	    &spa->spa_errlog_scrub, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the livelist deletion field. If a livelist is queued for
	 * deletion, indicate that in the spa
	 */
	error = spa_dir_prop(spa, DMU_POOL_DELETED_CLONES,
	    &spa->spa_livelists_to_delete, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the history object.  If we have an older pool, this
	 * will not be present.
	 */
	error = spa_dir_prop(spa, DMU_POOL_HISTORY, &spa->spa_history, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the per-vdev ZAP map. If we have an older pool, this will not
	 * be present; in this case, defer its creation to a later time to
	 * avoid dirtying the MOS this early / out of sync context. See
	 * spa_sync_config_object.
	 */

	/* The sentinel is only available in the MOS config. */
	nvlist_t *mos_config;
	if (load_nvlist(spa, spa->spa_config_object, &mos_config) != 0) {
		spa_load_failed(spa, "unable to retrieve MOS config");
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	error = spa_dir_prop(spa, DMU_POOL_VDEV_ZAP_MAP,
	    &spa->spa_all_vdev_zaps, B_FALSE);

	if (error == ENOENT) {
		VERIFY(!nvlist_exists(mos_config,
		    ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS));
		spa->spa_avz_action = AVZ_ACTION_INITIALIZE;
		ASSERT0(vdev_count_verify_zaps(spa->spa_root_vdev));
	} else if (error != 0) {
		nvlist_free(mos_config);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	} else if (!nvlist_exists(mos_config, ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS)) {
		/*
		 * An older version of ZFS overwrote the sentinel value, so
		 * we have orphaned per-vdev ZAPs in the MOS. Defer their
		 * destruction to later; see spa_sync_config_object.
		 */
		spa->spa_avz_action = AVZ_ACTION_DESTROY;
		/*
		 * We're assuming that no vdevs have had their ZAPs created
		 * before this. Better be sure of it.
		 */
		ASSERT0(vdev_count_verify_zaps(spa->spa_root_vdev));
	}
	nvlist_free(mos_config);

	spa->spa_delegation = zpool_prop_default_numeric(ZPOOL_PROP_DELEGATION);

	error = spa_dir_prop(spa, DMU_POOL_PROPS, &spa->spa_pool_props_object,
	    B_FALSE);
	if (error && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	if (error == 0) {
		uint64_t autoreplace = 0;

		spa_prop_find(spa, ZPOOL_PROP_BOOTFS, &spa->spa_bootfs);
		spa_prop_find(spa, ZPOOL_PROP_AUTOREPLACE, &autoreplace);
		spa_prop_find(spa, ZPOOL_PROP_DELEGATION, &spa->spa_delegation);
		spa_prop_find(spa, ZPOOL_PROP_FAILUREMODE, &spa->spa_failmode);
		spa_prop_find(spa, ZPOOL_PROP_AUTOEXPAND, &spa->spa_autoexpand);
		spa_prop_find(spa, ZPOOL_PROP_MULTIHOST, &spa->spa_multihost);
		spa_prop_find(spa, ZPOOL_PROP_AUTOTRIM, &spa->spa_autotrim);
		spa->spa_autoreplace = (autoreplace != 0);
	}

	/*
	 * If we are importing a pool with missing top-level vdevs,
	 * we enforce that the pool doesn't panic or get suspended on
	 * error since the likelihood of missing data is extremely high.
	 */
	if (spa->spa_missing_tvds > 0 &&
	    spa->spa_failmode != ZIO_FAILURE_MODE_CONTINUE &&
	    spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
		spa_load_note(spa, "forcing failmode to 'continue' "
		    "as some top level vdevs are missing");
		spa->spa_failmode = ZIO_FAILURE_MODE_CONTINUE;
	}

	return (0);
}

static int
spa_ld_open_aux_vdevs(spa_t *spa, spa_import_type_t type)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * If we're assembling the pool from the split-off vdevs of
	 * an existing pool, we don't want to attach the spares & cache
	 * devices.
	 */

	/*
	 * Load any hot spares for this pool.
	 */
	error = spa_dir_prop(spa, DMU_POOL_SPARES, &spa->spa_spares.sav_object,
	    B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	if (error == 0 && type != SPA_IMPORT_ASSEMBLE) {
		ASSERT(spa_version(spa) >= SPA_VERSION_SPARES);
		if (load_nvlist(spa, spa->spa_spares.sav_object,
		    &spa->spa_spares.sav_config) != 0) {
			spa_load_failed(spa, "error loading spares nvlist");
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
	} else if (error == 0) {
		spa->spa_spares.sav_sync = B_TRUE;
	}

	/*
	 * Load any level 2 ARC devices for this pool.
	 */
	error = spa_dir_prop(spa, DMU_POOL_L2CACHE,
	    &spa->spa_l2cache.sav_object, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	if (error == 0 && type != SPA_IMPORT_ASSEMBLE) {
		ASSERT(spa_version(spa) >= SPA_VERSION_L2CACHE);
		if (load_nvlist(spa, spa->spa_l2cache.sav_object,
		    &spa->spa_l2cache.sav_config) != 0) {
			spa_load_failed(spa, "error loading l2cache nvlist");
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
	} else if (error == 0) {
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	return (0);
}

static int
spa_ld_load_vdev_metadata(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * If the 'multihost' property is set, then never allow a pool to
	 * be imported when the system hostid is zero.  The exception to
	 * this rule is zdb which is always allowed to access pools.
	 */
	if (spa_multihost(spa) && spa_get_hostid(spa) == 0 &&
	    (spa->spa_import_flags & ZFS_IMPORT_SKIP_MMP) == 0) {
		fnvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_MMP_STATE, MMP_STATE_NO_HOSTID);
		return (spa_vdev_err(rvd, VDEV_AUX_ACTIVE, EREMOTEIO));
	}

	/*
	 * If the 'autoreplace' property is set, then post a resource notifying
	 * the ZFS DE that it should not issue any faults for unopenable
	 * devices.  We also iterate over the vdevs, and post a sysevent for any
	 * unopenable vdevs so that the normal autoreplace handler can take
	 * over.
	 */
	if (spa->spa_autoreplace && spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
		spa_check_removed(spa->spa_root_vdev);
		/*
		 * For the import case, this is done in spa_import(), because
		 * at this point we're using the spare definitions from
		 * the MOS config, not necessarily from the userland config.
		 */
		if (spa->spa_load_state != SPA_LOAD_IMPORT) {
			spa_aux_check_removed(&spa->spa_spares);
			spa_aux_check_removed(&spa->spa_l2cache);
		}
	}

	/*
	 * Load the vdev metadata such as metaslabs, DTLs, spacemap object, etc.
	 */
	error = vdev_load(rvd);
	if (error != 0) {
		spa_load_failed(spa, "vdev_load failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, error));
	}

	error = spa_ld_log_spacemaps(spa);
	if (error != 0) {
		spa_load_failed(spa, "spa_ld_log_spacemaps failed [error=%d]",
		    error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, error));
	}

	/*
	 * Propagate the leaf DTLs we just loaded all the way up the vdev tree.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_dtl_reassess(rvd, 0, 0, B_FALSE, B_FALSE);
	spa_config_exit(spa, SCL_ALL, FTAG);

	return (0);
}

static int
spa_ld_load_dedup_tables(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	error = ddt_load(spa);
	if (error != 0) {
		spa_load_failed(spa, "ddt_load failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	return (0);
}

static int
spa_ld_load_brt(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	error = brt_load(spa);
	if (error != 0) {
		spa_load_failed(spa, "brt_load failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	return (0);
}

static int
spa_ld_verify_logs(spa_t *spa, spa_import_type_t type, const char **ereport)
{
	vdev_t *rvd = spa->spa_root_vdev;

	if (type != SPA_IMPORT_ASSEMBLE && spa_writeable(spa)) {
		boolean_t missing = spa_check_logs(spa);
		if (missing) {
			if (spa->spa_missing_tvds != 0) {
				spa_load_note(spa, "spa_check_logs failed "
				    "so dropping the logs");
			} else {
				*ereport = FM_EREPORT_ZFS_LOG_REPLAY;
				spa_load_failed(spa, "spa_check_logs failed");
				return (spa_vdev_err(rvd, VDEV_AUX_BAD_LOG,
				    ENXIO));
			}
		}
	}

	return (0);
}

static int
spa_ld_verify_pool_data(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * We've successfully opened the pool, verify that we're ready
	 * to start pushing transactions.
	 */
	if (spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
		error = spa_load_verify(spa);
		if (error != 0) {
			spa_load_failed(spa, "spa_load_verify failed "
			    "[error=%d]", error);
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA,
			    error));
		}
	}

	return (0);
}

static void
spa_ld_claim_log_blocks(spa_t *spa)
{
	dmu_tx_t *tx;
	dsl_pool_t *dp = spa_get_dsl(spa);

	/*
	 * Claim log blocks that haven't been committed yet.
	 * This must all happen in a single txg.
	 * Note: spa_claim_max_txg is updated by spa_claim_notify(),
	 * invoked from zil_claim_log_block()'s i/o done callback.
	 * Price of rollback is that we abandon the log.
	 */
	spa->spa_claiming = B_TRUE;

	tx = dmu_tx_create_assigned(dp, spa_first_txg(spa));
	(void) dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
	    zil_claim, tx, DS_FIND_CHILDREN);
	dmu_tx_commit(tx);

	spa->spa_claiming = B_FALSE;

	spa_set_log_state(spa, SPA_LOG_GOOD);
}

static void
spa_ld_check_for_config_update(spa_t *spa, uint64_t config_cache_txg,
    boolean_t update_config_cache)
{
	vdev_t *rvd = spa->spa_root_vdev;
	int need_update = B_FALSE;

	/*
	 * If the config cache is stale, or we have uninitialized
	 * metaslabs (see spa_vdev_add()), then update the config.
	 *
	 * If this is a verbatim import, trust the current
	 * in-core spa_config and update the disk labels.
	 */
	if (update_config_cache || config_cache_txg != spa->spa_config_txg ||
	    spa->spa_load_state == SPA_LOAD_IMPORT ||
	    spa->spa_load_state == SPA_LOAD_RECOVER ||
	    (spa->spa_import_flags & ZFS_IMPORT_VERBATIM))
		need_update = B_TRUE;

	for (int c = 0; c < rvd->vdev_children; c++)
		if (rvd->vdev_child[c]->vdev_ms_array == 0)
			need_update = B_TRUE;

	/*
	 * Update the config cache asynchronously in case we're the
	 * root pool, in which case the config cache isn't writable yet.
	 */
	if (need_update)
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
}

static void
spa_ld_prepare_for_reload(spa_t *spa)
{
	spa_mode_t mode = spa->spa_mode;
	int async_suspended = spa->spa_async_suspended;

	spa_unload(spa);
	spa_deactivate(spa);
	spa_activate(spa, mode);

	/*
	 * We save the value of spa_async_suspended as it gets reset to 0 by
	 * spa_unload(). We want to restore it back to the original value before
	 * returning as we might be calling spa_async_resume() later.
	 */
	spa->spa_async_suspended = async_suspended;
}

static int
spa_ld_read_checkpoint_txg(spa_t *spa)
{
	uberblock_t checkpoint;
	int error = 0;

	ASSERT0(spa->spa_checkpoint_txg);
	ASSERT(MUTEX_HELD(&spa_namespace_lock) ||
	    spa->spa_load_thread == curthread);

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ZPOOL_CHECKPOINT, sizeof (uint64_t),
	    sizeof (uberblock_t) / sizeof (uint64_t), &checkpoint);

	if (error == ENOENT)
		return (0);

	if (error != 0)
		return (error);

	ASSERT3U(checkpoint.ub_txg, !=, 0);
	ASSERT3U(checkpoint.ub_checkpoint_txg, !=, 0);
	ASSERT3U(checkpoint.ub_timestamp, !=, 0);
	spa->spa_checkpoint_txg = checkpoint.ub_txg;
	spa->spa_checkpoint_info.sci_timestamp = checkpoint.ub_timestamp;

	return (0);
}

static int
spa_ld_mos_init(spa_t *spa, spa_import_type_t type)
{
	int error = 0;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_config_source != SPA_CONFIG_SRC_NONE);

	/*
	 * Never trust the config that is provided unless we are assembling
	 * a pool following a split.
	 * This means don't trust blkptrs and the vdev tree in general. This
	 * also effectively puts the spa in read-only mode since
	 * spa_writeable() checks for spa_trust_config to be true.
	 * We will later load a trusted config from the MOS.
	 */
	if (type != SPA_IMPORT_ASSEMBLE)
		spa->spa_trust_config = B_FALSE;

	/*
	 * Parse the config provided to create a vdev tree.
	 */
	error = spa_ld_parse_config(spa, type);
	if (error != 0)
		return (error);

	spa_import_progress_add(spa);

	/*
	 * Now that we have the vdev tree, try to open each vdev. This involves
	 * opening the underlying physical device, retrieving its geometry and
	 * probing the vdev with a dummy I/O. The state of each vdev will be set
	 * based on the success of those operations. After this we'll be ready
	 * to read from the vdevs.
	 */
	error = spa_ld_open_vdevs(spa);
	if (error != 0)
		return (error);

	/*
	 * Read the label of each vdev and make sure that the GUIDs stored
	 * there match the GUIDs in the config provided.
	 * If we're assembling a new pool that's been split off from an
	 * existing pool, the labels haven't yet been updated so we skip
	 * validation for now.
	 */
	if (type != SPA_IMPORT_ASSEMBLE) {
		error = spa_ld_validate_vdevs(spa);
		if (error != 0)
			return (error);
	}

	/*
	 * Read all vdev labels to find the best uberblock (i.e. latest,
	 * unless spa_load_max_txg is set) and store it in spa_uberblock. We
	 * get the list of features required to read blkptrs in the MOS from
	 * the vdev label with the best uberblock and verify that our version
	 * of zfs supports them all.
	 */
	error = spa_ld_select_uberblock(spa, type);
	if (error != 0)
		return (error);

	/*
	 * Pass that uberblock to the dsl_pool layer which will open the root
	 * blkptr. This blkptr points to the latest version of the MOS and will
	 * allow us to read its contents.
	 */
	error = spa_ld_open_rootbp(spa);
	if (error != 0)
		return (error);

	return (0);
}

static int
spa_ld_checkpoint_rewind(spa_t *spa)
{
	uberblock_t checkpoint;
	int error = 0;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT);

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ZPOOL_CHECKPOINT, sizeof (uint64_t),
	    sizeof (uberblock_t) / sizeof (uint64_t), &checkpoint);

	if (error != 0) {
		spa_load_failed(spa, "unable to retrieve checkpointed "
		    "uberblock from the MOS config [error=%d]", error);

		if (error == ENOENT)
			error = ZFS_ERR_NO_CHECKPOINT;

		return (error);
	}

	ASSERT3U(checkpoint.ub_txg, <, spa->spa_uberblock.ub_txg);
	ASSERT3U(checkpoint.ub_txg, ==, checkpoint.ub_checkpoint_txg);

	/*
	 * We need to update the txg and timestamp of the checkpointed
	 * uberblock to be higher than the latest one. This ensures that
	 * the checkpointed uberblock is selected if we were to close and
	 * reopen the pool right after we've written it in the vdev labels.
	 * (also see block comment in vdev_uberblock_compare)
	 */
	checkpoint.ub_txg = spa->spa_uberblock.ub_txg + 1;
	checkpoint.ub_timestamp = gethrestime_sec();

	/*
	 * Set current uberblock to be the checkpointed uberblock.
	 */
	spa->spa_uberblock = checkpoint;

	/*
	 * If we are doing a normal rewind, then the pool is open for
	 * writing and we sync the "updated" checkpointed uberblock to
	 * disk. Once this is done, we've basically rewound the whole
	 * pool and there is no way back.
	 *
	 * There are cases when we don't want to attempt and sync the
	 * checkpointed uberblock to disk because we are opening a
	 * pool as read-only. Specifically, verifying the checkpointed
	 * state with zdb, and importing the checkpointed state to get
	 * a "preview" of its content.
	 */
	if (spa_writeable(spa)) {
		vdev_t *rvd = spa->spa_root_vdev;

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		vdev_t *svd[SPA_SYNC_MIN_VDEVS] = { NULL };
		int svdcount = 0;
		int children = rvd->vdev_children;
		int c0 = random_in_range(children);

		for (int c = 0; c < children; c++) {
			vdev_t *vd = rvd->vdev_child[(c0 + c) % children];

			/* Stop when revisiting the first vdev */
			if (c > 0 && svd[0] == vd)
				break;

			if (vd->vdev_ms_array == 0 || vd->vdev_islog ||
			    !vdev_is_concrete(vd))
				continue;

			svd[svdcount++] = vd;
			if (svdcount == SPA_SYNC_MIN_VDEVS)
				break;
		}
		error = vdev_config_sync(svd, svdcount, spa->spa_first_txg);
		if (error == 0)
			spa->spa_last_synced_guid = rvd->vdev_guid;
		spa_config_exit(spa, SCL_ALL, FTAG);

		if (error != 0) {
			spa_load_failed(spa, "failed to write checkpointed "
			    "uberblock to the vdev labels [error=%d]", error);
			return (error);
		}
	}

	return (0);
}

static int
spa_ld_mos_with_trusted_config(spa_t *spa, spa_import_type_t type,
    boolean_t *update_config_cache)
{
	int error;

	/*
	 * Parse the config for pool, open and validate vdevs,
	 * select an uberblock, and use that uberblock to open
	 * the MOS.
	 */
	error = spa_ld_mos_init(spa, type);
	if (error != 0)
		return (error);

	/*
	 * Retrieve the trusted config stored in the MOS and use it to create
	 * a new, exact version of the vdev tree, then reopen all vdevs.
	 */
	error = spa_ld_trusted_config(spa, type, B_FALSE);
	if (error == EAGAIN) {
		if (update_config_cache != NULL)
			*update_config_cache = B_TRUE;

		/*
		 * Redo the loading process with the trusted config if it is
		 * too different from the untrusted config.
		 */
		spa_ld_prepare_for_reload(spa);
		spa_load_note(spa, "RELOADING");
		error = spa_ld_mos_init(spa, type);
		if (error != 0)
			return (error);

		error = spa_ld_trusted_config(spa, type, B_TRUE);
		if (error != 0)
			return (error);

	} else if (error != 0) {
		return (error);
	}

	return (0);
}

/*
 * Load an existing storage pool, using the config provided. This config
 * describes which vdevs are part of the pool and is later validated against
 * partial configs present in each vdev's label and an entire copy of the
 * config stored in the MOS.
 */
static int
spa_load_impl(spa_t *spa, spa_import_type_t type, const char **ereport)
{
	int error = 0;
	boolean_t missing_feat_write = B_FALSE;
	boolean_t checkpoint_rewind =
	    (spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT);
	boolean_t update_config_cache = B_FALSE;
	hrtime_t load_start = gethrtime();

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_config_source != SPA_CONFIG_SRC_NONE);

	spa_load_note(spa, "LOADING");

	error = spa_ld_mos_with_trusted_config(spa, type, &update_config_cache);
	if (error != 0)
		return (error);

	/*
	 * If we are rewinding to the checkpoint then we need to repeat
	 * everything we've done so far in this function but this time
	 * selecting the checkpointed uberblock and using that to open
	 * the MOS.
	 */
	if (checkpoint_rewind) {
		/*
		 * If we are rewinding to the checkpoint update config cache
		 * anyway.
		 */
		update_config_cache = B_TRUE;

		/*
		 * Extract the checkpointed uberblock from the current MOS
		 * and use this as the pool's uberblock from now on. If the
		 * pool is imported as writeable we also write the checkpoint
		 * uberblock to the labels, making the rewind permanent.
		 */
		error = spa_ld_checkpoint_rewind(spa);
		if (error != 0)
			return (error);

		/*
		 * Redo the loading process again with the
		 * checkpointed uberblock.
		 */
		spa_ld_prepare_for_reload(spa);
		spa_load_note(spa, "LOADING checkpointed uberblock");
		error = spa_ld_mos_with_trusted_config(spa, type, NULL);
		if (error != 0)
			return (error);
	}

	/*
	 * Drop the namespace lock for the rest of the function.
	 */
	spa->spa_load_thread = curthread;
	mutex_exit(&spa_namespace_lock);

	/*
	 * Retrieve the checkpoint txg if the pool has a checkpoint.
	 */
	spa_import_progress_set_notes(spa, "Loading checkpoint txg");
	error = spa_ld_read_checkpoint_txg(spa);
	if (error != 0)
		goto fail;

	/*
	 * Retrieve the mapping of indirect vdevs. Those vdevs were removed
	 * from the pool and their contents were re-mapped to other vdevs. Note
	 * that everything that we read before this step must have been
	 * rewritten on concrete vdevs after the last device removal was
	 * initiated. Otherwise we could be reading from indirect vdevs before
	 * we have loaded their mappings.
	 */
	spa_import_progress_set_notes(spa, "Loading indirect vdev metadata");
	error = spa_ld_open_indirect_vdev_metadata(spa);
	if (error != 0)
		goto fail;

	/*
	 * Retrieve the full list of active features from the MOS and check if
	 * they are all supported.
	 */
	spa_import_progress_set_notes(spa, "Checking feature flags");
	error = spa_ld_check_features(spa, &missing_feat_write);
	if (error != 0)
		goto fail;

	/*
	 * Load several special directories from the MOS needed by the dsl_pool
	 * layer.
	 */
	spa_import_progress_set_notes(spa, "Loading special MOS directories");
	error = spa_ld_load_special_directories(spa);
	if (error != 0)
		goto fail;

	/*
	 * Retrieve pool properties from the MOS.
	 */
	spa_import_progress_set_notes(spa, "Loading properties");
	error = spa_ld_get_props(spa);
	if (error != 0)
		goto fail;

	/*
	 * Retrieve the list of auxiliary devices - cache devices and spares -
	 * and open them.
	 */
	spa_import_progress_set_notes(spa, "Loading AUX vdevs");
	error = spa_ld_open_aux_vdevs(spa, type);
	if (error != 0)
		goto fail;

	/*
	 * Load the metadata for all vdevs. Also check if unopenable devices
	 * should be autoreplaced.
	 */
	spa_import_progress_set_notes(spa, "Loading vdev metadata");
	error = spa_ld_load_vdev_metadata(spa);
	if (error != 0)
		goto fail;

	spa_import_progress_set_notes(spa, "Loading dedup tables");
	error = spa_ld_load_dedup_tables(spa);
	if (error != 0)
		goto fail;

	spa_import_progress_set_notes(spa, "Loading BRT");
	error = spa_ld_load_brt(spa);
	if (error != 0)
		goto fail;

	/*
	 * Verify the logs now to make sure we don't have any unexpected errors
	 * when we claim log blocks later.
	 */
	spa_import_progress_set_notes(spa, "Verifying Log Devices");
	error = spa_ld_verify_logs(spa, type, ereport);
	if (error != 0)
		goto fail;

	if (missing_feat_write) {
		ASSERT(spa->spa_load_state == SPA_LOAD_TRYIMPORT);

		/*
		 * At this point, we know that we can open the pool in
		 * read-only mode but not read-write mode. We now have enough
		 * information and can return to userland.
		 */
		error = spa_vdev_err(spa->spa_root_vdev, VDEV_AUX_UNSUP_FEAT,
		    ENOTSUP);
		goto fail;
	}

	/*
	 * Traverse the last txgs to make sure the pool was left off in a safe
	 * state. When performing an extreme rewind, we verify the whole pool,
	 * which can take a very long time.
	 */
	spa_import_progress_set_notes(spa, "Verifying pool data");
	error = spa_ld_verify_pool_data(spa);
	if (error != 0)
		goto fail;

	/*
	 * Calculate the deflated space for the pool. This must be done before
	 * we write anything to the pool because we'd need to update the space
	 * accounting using the deflated sizes.
	 */
	spa_import_progress_set_notes(spa, "Calculating deflated space");
	spa_update_dspace(spa);

	/*
	 * We have now retrieved all the information we needed to open the
	 * pool. If we are importing the pool in read-write mode, a few
	 * additional steps must be performed to finish the import.
	 */
	spa_import_progress_set_notes(spa, "Starting import");
	if (spa_writeable(spa) && (spa->spa_load_state == SPA_LOAD_RECOVER ||
	    spa->spa_load_max_txg == UINT64_MAX)) {
		uint64_t config_cache_txg = spa->spa_config_txg;

		ASSERT(spa->spa_load_state != SPA_LOAD_TRYIMPORT);

		/*
		 * Before we do any zio_write's, complete the raidz expansion
		 * scratch space copying, if necessary.
		 */
		if (RRSS_GET_STATE(&spa->spa_uberblock) == RRSS_SCRATCH_VALID)
			vdev_raidz_reflow_copy_scratch(spa);

		/*
		 * In case of a checkpoint rewind, log the original txg
		 * of the checkpointed uberblock.
		 */
		if (checkpoint_rewind) {
			spa_history_log_internal(spa, "checkpoint rewind",
			    NULL, "rewound state to txg=%llu",
			    (u_longlong_t)spa->spa_uberblock.ub_checkpoint_txg);
		}

		spa_import_progress_set_notes(spa, "Claiming ZIL blocks");
		/*
		 * Traverse the ZIL and claim all blocks.
		 */
		spa_ld_claim_log_blocks(spa);

		/*
		 * Kick-off the syncing thread.
		 */
		spa->spa_sync_on = B_TRUE;
		txg_sync_start(spa->spa_dsl_pool);
		mmp_thread_start(spa);

		/*
		 * Wait for all claims to sync.  We sync up to the highest
		 * claimed log block birth time so that claimed log blocks
		 * don't appear to be from the future.  spa_claim_max_txg
		 * will have been set for us by ZIL traversal operations
		 * performed above.
		 */
		spa_import_progress_set_notes(spa, "Syncing ZIL claims");
		txg_wait_synced(spa->spa_dsl_pool, spa->spa_claim_max_txg);

		/*
		 * Check if we need to request an update of the config. On the
		 * next sync, we would update the config stored in vdev labels
		 * and the cachefile (by default /etc/zfs/zpool.cache).
		 */
		spa_import_progress_set_notes(spa, "Updating configs");
		spa_ld_check_for_config_update(spa, config_cache_txg,
		    update_config_cache);

		/*
		 * Check if a rebuild was in progress and if so resume it.
		 * Then check all DTLs to see if anything needs resilvering.
		 * The resilver will be deferred if a rebuild was started.
		 */
		spa_import_progress_set_notes(spa, "Starting resilvers");
		if (vdev_rebuild_active(spa->spa_root_vdev)) {
			vdev_rebuild_restart(spa);
		} else if (!dsl_scan_resilvering(spa->spa_dsl_pool) &&
		    vdev_resilver_needed(spa->spa_root_vdev, NULL, NULL)) {
			spa_async_request(spa, SPA_ASYNC_RESILVER);
		}

		/*
		 * Log the fact that we booted up (so that we can detect if
		 * we rebooted in the middle of an operation).
		 */
		spa_history_log_version(spa, "open", NULL);

		spa_import_progress_set_notes(spa,
		    "Restarting device removals");
		spa_restart_removal(spa);
		spa_spawn_aux_threads(spa);

		/*
		 * Delete any inconsistent datasets.
		 *
		 * Note:
		 * Since we may be issuing deletes for clones here,
		 * we make sure to do so after we've spawned all the
		 * auxiliary threads above (from which the livelist
		 * deletion zthr is part of).
		 */
		spa_import_progress_set_notes(spa,
		    "Cleaning up inconsistent objsets");
		(void) dmu_objset_find(spa_name(spa),
		    dsl_destroy_inconsistent, NULL, DS_FIND_CHILDREN);

		/*
		 * Clean up any stale temporary dataset userrefs.
		 */
		spa_import_progress_set_notes(spa,
		    "Cleaning up temporary userrefs");
		dsl_pool_clean_tmp_userrefs(spa->spa_dsl_pool);

		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		spa_import_progress_set_notes(spa, "Restarting initialize");
		vdev_initialize_restart(spa->spa_root_vdev);
		spa_import_progress_set_notes(spa, "Restarting TRIM");
		vdev_trim_restart(spa->spa_root_vdev);
		vdev_autotrim_restart(spa);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		spa_import_progress_set_notes(spa, "Finished importing");
	}
	zio_handle_import_delay(spa, gethrtime() - load_start);

	spa_import_progress_remove(spa_guid(spa));
	spa_async_request(spa, SPA_ASYNC_L2CACHE_REBUILD);

	spa_load_note(spa, "LOADED");
fail:
	mutex_enter(&spa_namespace_lock);
	spa->spa_load_thread = NULL;
	cv_broadcast(&spa_namespace_cv);

	return (error);

}

static int
spa_load_retry(spa_t *spa, spa_load_state_t state)
{
	spa_mode_t mode = spa->spa_mode;

	spa_unload(spa);
	spa_deactivate(spa);

	spa->spa_load_max_txg = spa->spa_uberblock.ub_txg - 1;

	spa_activate(spa, mode);
	spa_async_suspend(spa);

	spa_load_note(spa, "spa_load_retry: rewind, max txg: %llu",
	    (u_longlong_t)spa->spa_load_max_txg);

	return (spa_load(spa, state, SPA_IMPORT_EXISTING));
}

/*
 * If spa_load() fails this function will try loading prior txg's. If
 * 'state' is SPA_LOAD_RECOVER and one of these loads succeeds the pool
 * will be rewound to that txg. If 'state' is not SPA_LOAD_RECOVER this
 * function will not rewind the pool and will return the same error as
 * spa_load().
 */
static int
spa_load_best(spa_t *spa, spa_load_state_t state, uint64_t max_request,
    int rewind_flags)
{
	nvlist_t *loadinfo = NULL;
	nvlist_t *config = NULL;
	int load_error, rewind_error;
	uint64_t safe_rewind_txg;
	uint64_t min_txg;

	if (spa->spa_load_txg && state == SPA_LOAD_RECOVER) {
		spa->spa_load_max_txg = spa->spa_load_txg;
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	} else {
		spa->spa_load_max_txg = max_request;
		if (max_request != UINT64_MAX)
			spa->spa_extreme_rewind = B_TRUE;
	}

	load_error = rewind_error = spa_load(spa, state, SPA_IMPORT_EXISTING);
	if (load_error == 0)
		return (0);
	if (load_error == ZFS_ERR_NO_CHECKPOINT) {
		/*
		 * When attempting checkpoint-rewind on a pool with no
		 * checkpoint, we should not attempt to load uberblocks
		 * from previous txgs when spa_load fails.
		 */
		ASSERT(spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT);
		spa_import_progress_remove(spa_guid(spa));
		return (load_error);
	}

	if (spa->spa_root_vdev != NULL)
		config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);

	spa->spa_last_ubsync_txg = spa->spa_uberblock.ub_txg;
	spa->spa_last_ubsync_txg_ts = spa->spa_uberblock.ub_timestamp;

	if (rewind_flags & ZPOOL_NEVER_REWIND) {
		nvlist_free(config);
		spa_import_progress_remove(spa_guid(spa));
		return (load_error);
	}

	if (state == SPA_LOAD_RECOVER) {
		/* Price of rolling back is discarding txgs, including log */
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	} else {
		/*
		 * If we aren't rolling back save the load info from our first
		 * import attempt so that we can restore it after attempting
		 * to rewind.
		 */
		loadinfo = spa->spa_load_info;
		spa->spa_load_info = fnvlist_alloc();
	}

	spa->spa_load_max_txg = spa->spa_last_ubsync_txg;
	safe_rewind_txg = spa->spa_last_ubsync_txg - TXG_DEFER_SIZE;
	min_txg = (rewind_flags & ZPOOL_EXTREME_REWIND) ?
	    TXG_INITIAL : safe_rewind_txg;

	/*
	 * Continue as long as we're finding errors, we're still within
	 * the acceptable rewind range, and we're still finding uberblocks
	 */
	while (rewind_error && spa->spa_uberblock.ub_txg >= min_txg &&
	    spa->spa_uberblock.ub_txg <= spa->spa_load_max_txg) {
		if (spa->spa_load_max_txg < safe_rewind_txg)
			spa->spa_extreme_rewind = B_TRUE;
		rewind_error = spa_load_retry(spa, state);
	}

	spa->spa_extreme_rewind = B_FALSE;
	spa->spa_load_max_txg = UINT64_MAX;

	if (config && (rewind_error || state != SPA_LOAD_RECOVER))
		spa_config_set(spa, config);
	else
		nvlist_free(config);

	if (state == SPA_LOAD_RECOVER) {
		ASSERT3P(loadinfo, ==, NULL);
		spa_import_progress_remove(spa_guid(spa));
		return (rewind_error);
	} else {
		/* Store the rewind info as part of the initial load info */
		fnvlist_add_nvlist(loadinfo, ZPOOL_CONFIG_REWIND_INFO,
		    spa->spa_load_info);

		/* Restore the initial load info */
		fnvlist_free(spa->spa_load_info);
		spa->spa_load_info = loadinfo;

		spa_import_progress_remove(spa_guid(spa));
		return (load_error);
	}
}

/*
 * Pool Open/Import
 *
 * The import case is identical to an open except that the configuration is sent
 * down from userland, instead of grabbed from the configuration cache.  For the
 * case of an open, the pool configuration will exist in the
 * POOL_STATE_UNINITIALIZED state.
 *
 * The stats information (gen/count/ustats) is used to gather vdev statistics at
 * the same time open the pool, without having to keep around the spa_t in some
 * ambiguous state.
 */
static int
spa_open_common(const char *pool, spa_t **spapp, const void *tag,
    nvlist_t *nvpolicy, nvlist_t **config)
{
	spa_t *spa;
	spa_load_state_t state = SPA_LOAD_OPEN;
	int error;
	int locked = B_FALSE;
	int firstopen = B_FALSE;

	*spapp = NULL;

	/*
	 * As disgusting as this is, we need to support recursive calls to this
	 * function because dsl_dir_open() is called during spa_load(), and ends
	 * up calling spa_open() again.  The real fix is to figure out how to
	 * avoid dsl_dir_open() calling this in the first place.
	 */
	if (MUTEX_NOT_HELD(&spa_namespace_lock)) {
		mutex_enter(&spa_namespace_lock);
		locked = B_TRUE;
	}

	if ((spa = spa_lookup(pool)) == NULL) {
		if (locked)
			mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ENOENT));
	}

	if (spa->spa_state == POOL_STATE_UNINITIALIZED) {
		zpool_load_policy_t policy;

		firstopen = B_TRUE;

		zpool_get_load_policy(nvpolicy ? nvpolicy : spa->spa_config,
		    &policy);
		if (policy.zlp_rewind & ZPOOL_DO_REWIND)
			state = SPA_LOAD_RECOVER;

		spa_activate(spa, spa_mode_global);

		if (state != SPA_LOAD_RECOVER)
			spa->spa_last_ubsync_txg = spa->spa_load_txg = 0;
		spa->spa_config_source = SPA_CONFIG_SRC_CACHEFILE;

		zfs_dbgmsg("spa_open_common: opening %s", pool);
		error = spa_load_best(spa, state, policy.zlp_txg,
		    policy.zlp_rewind);

		if (error == EBADF) {
			/*
			 * If vdev_validate() returns failure (indicated by
			 * EBADF), it indicates that one of the vdevs indicates
			 * that the pool has been exported or destroyed.  If
			 * this is the case, the config cache is out of sync and
			 * we should remove the pool from the namespace.
			 */
			spa_unload(spa);
			spa_deactivate(spa);
			spa_write_cachefile(spa, B_TRUE, B_TRUE, B_FALSE);
			spa_remove(spa);
			if (locked)
				mutex_exit(&spa_namespace_lock);
			return (SET_ERROR(ENOENT));
		}

		if (error) {
			/*
			 * We can't open the pool, but we still have useful
			 * information: the state of each vdev after the
			 * attempted vdev_open().  Return this to the user.
			 */
			if (config != NULL && spa->spa_config) {
				*config = fnvlist_dup(spa->spa_config);
				fnvlist_add_nvlist(*config,
				    ZPOOL_CONFIG_LOAD_INFO,
				    spa->spa_load_info);
			}
			spa_unload(spa);
			spa_deactivate(spa);
			spa->spa_last_open_failed = error;
			if (locked)
				mutex_exit(&spa_namespace_lock);
			*spapp = NULL;
			return (error);
		}
	}

	spa_open_ref(spa, tag);

	if (config != NULL)
		*config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);

	/*
	 * If we've recovered the pool, pass back any information we
	 * gathered while doing the load.
	 */
	if (state == SPA_LOAD_RECOVER && config != NULL) {
		fnvlist_add_nvlist(*config, ZPOOL_CONFIG_LOAD_INFO,
		    spa->spa_load_info);
	}

	if (locked) {
		spa->spa_last_open_failed = 0;
		spa->spa_last_ubsync_txg = 0;
		spa->spa_load_txg = 0;
		mutex_exit(&spa_namespace_lock);
	}

	if (firstopen)
		zvol_create_minors_recursive(spa_name(spa));

	*spapp = spa;

	return (0);
}

int
spa_open_rewind(const char *name, spa_t **spapp, const void *tag,
    nvlist_t *policy, nvlist_t **config)
{
	return (spa_open_common(name, spapp, tag, policy, config));
}

int
spa_open(const char *name, spa_t **spapp, const void *tag)
{
	return (spa_open_common(name, spapp, tag, NULL, NULL));
}

/*
 * Lookup the given spa_t, incrementing the inject count in the process,
 * preventing it from being exported or destroyed.
 */
spa_t *
spa_inject_addref(char *name)
{
	spa_t *spa;

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(name)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (NULL);
	}
	spa->spa_inject_ref++;
	mutex_exit(&spa_namespace_lock);

	return (spa);
}

void
spa_inject_delref(spa_t *spa)
{
	mutex_enter(&spa_namespace_lock);
	spa->spa_inject_ref--;
	mutex_exit(&spa_namespace_lock);
}

/*
 * Add spares device information to the nvlist.
 */
static void
spa_add_spares(spa_t *spa, nvlist_t *config)
{
	nvlist_t **spares;
	uint_t i, nspares;
	nvlist_t *nvroot;
	uint64_t guid;
	vdev_stat_t *vs;
	uint_t vsc;
	uint64_t pool;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	if (spa->spa_spares.sav_count == 0)
		return;

	nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	VERIFY0(nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares));
	if (nspares != 0) {
		fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    (const nvlist_t * const *)spares, nspares);
		VERIFY0(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares));

		/*
		 * Go through and find any spares which have since been
		 * repurposed as an active spare.  If this is the case, update
		 * their status appropriately.
		 */
		for (i = 0; i < nspares; i++) {
			guid = fnvlist_lookup_uint64(spares[i],
			    ZPOOL_CONFIG_GUID);
			VERIFY0(nvlist_lookup_uint64_array(spares[i],
			    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&vs, &vsc));
			if (spa_spare_exists(guid, &pool, NULL) &&
			    pool != 0ULL) {
				vs->vs_state = VDEV_STATE_CANT_OPEN;
				vs->vs_aux = VDEV_AUX_SPARED;
			} else {
				vs->vs_state =
				    spa->spa_spares.sav_vdevs[i]->vdev_state;
			}
		}
	}
}

/*
 * Add l2cache device information to the nvlist, including vdev stats.
 */
static void
spa_add_l2cache(spa_t *spa, nvlist_t *config)
{
	nvlist_t **l2cache;
	uint_t i, j, nl2cache;
	nvlist_t *nvroot;
	uint64_t guid;
	vdev_t *vd;
	vdev_stat_t *vs;
	uint_t vsc;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	if (spa->spa_l2cache.sav_count == 0)
		return;

	nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	VERIFY0(nvlist_lookup_nvlist_array(spa->spa_l2cache.sav_config,
	    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache));
	if (nl2cache != 0) {
		fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    (const nvlist_t * const *)l2cache, nl2cache);
		VERIFY0(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache));

		/*
		 * Update level 2 cache device stats.
		 */

		for (i = 0; i < nl2cache; i++) {
			guid = fnvlist_lookup_uint64(l2cache[i],
			    ZPOOL_CONFIG_GUID);

			vd = NULL;
			for (j = 0; j < spa->spa_l2cache.sav_count; j++) {
				if (guid ==
				    spa->spa_l2cache.sav_vdevs[j]->vdev_guid) {
					vd = spa->spa_l2cache.sav_vdevs[j];
					break;
				}
			}
			ASSERT(vd != NULL);

			VERIFY0(nvlist_lookup_uint64_array(l2cache[i],
			    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&vs, &vsc));
			vdev_get_stats(vd, vs);
			vdev_config_generate_stats(vd, l2cache[i]);

		}
	}
}

static void
spa_feature_stats_from_disk(spa_t *spa, nvlist_t *features)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	if (spa->spa_feat_for_read_obj != 0) {
		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_feat_for_read_obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			ASSERT(za.za_integer_length == sizeof (uint64_t) &&
			    za.za_num_integers == 1);
			VERIFY0(nvlist_add_uint64(features, za.za_name,
			    za.za_first_integer));
		}
		zap_cursor_fini(&zc);
	}

	if (spa->spa_feat_for_write_obj != 0) {
		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_feat_for_write_obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			ASSERT(za.za_integer_length == sizeof (uint64_t) &&
			    za.za_num_integers == 1);
			VERIFY0(nvlist_add_uint64(features, za.za_name,
			    za.za_first_integer));
		}
		zap_cursor_fini(&zc);
	}
}

static void
spa_feature_stats_from_cache(spa_t *spa, nvlist_t *features)
{
	int i;

	for (i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t feature = spa_feature_table[i];
		uint64_t refcount;

		if (feature_get_refcount(spa, &feature, &refcount) != 0)
			continue;

		VERIFY0(nvlist_add_uint64(features, feature.fi_guid, refcount));
	}
}

/*
 * Store a list of pool features and their reference counts in the
 * config.
 *
 * The first time this is called on a spa, allocate a new nvlist, fetch
 * the pool features and reference counts from disk, then save the list
 * in the spa. In subsequent calls on the same spa use the saved nvlist
 * and refresh its values from the cached reference counts.  This
 * ensures we don't block here on I/O on a suspended pool so 'zpool
 * clear' can resume the pool.
 */
static void
spa_add_feature_stats(spa_t *spa, nvlist_t *config)
{
	nvlist_t *features;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	mutex_enter(&spa->spa_feat_stats_lock);
	features = spa->spa_feat_stats;

	if (features != NULL) {
		spa_feature_stats_from_cache(spa, features);
	} else {
		VERIFY0(nvlist_alloc(&features, NV_UNIQUE_NAME, KM_SLEEP));
		spa->spa_feat_stats = features;
		spa_feature_stats_from_disk(spa, features);
	}

	VERIFY0(nvlist_add_nvlist(config, ZPOOL_CONFIG_FEATURE_STATS,
	    features));

	mutex_exit(&spa->spa_feat_stats_lock);
}

int
spa_get_stats(const char *name, nvlist_t **config,
    char *altroot, size_t buflen)
{
	int error;
	spa_t *spa;

	*config = NULL;
	error = spa_open_common(name, &spa, FTAG, NULL, config);

	if (spa != NULL) {
		/*
		 * This still leaves a window of inconsistency where the spares
		 * or l2cache devices could change and the config would be
		 * self-inconsistent.
		 */
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

		if (*config != NULL) {
			uint64_t loadtimes[2];

			loadtimes[0] = spa->spa_loaded_ts.tv_sec;
			loadtimes[1] = spa->spa_loaded_ts.tv_nsec;
			fnvlist_add_uint64_array(*config,
			    ZPOOL_CONFIG_LOADED_TIME, loadtimes, 2);

			fnvlist_add_uint64(*config,
			    ZPOOL_CONFIG_ERRCOUNT,
			    spa_approx_errlog_size(spa));

			if (spa_suspended(spa)) {
				fnvlist_add_uint64(*config,
				    ZPOOL_CONFIG_SUSPENDED,
				    spa->spa_failmode);
				fnvlist_add_uint64(*config,
				    ZPOOL_CONFIG_SUSPENDED_REASON,
				    spa->spa_suspended);
			}

			spa_add_spares(spa, *config);
			spa_add_l2cache(spa, *config);
			spa_add_feature_stats(spa, *config);
		}
	}

	/*
	 * We want to get the alternate root even for faulted pools, so we cheat
	 * and call spa_lookup() directly.
	 */
	if (altroot) {
		if (spa == NULL) {
			mutex_enter(&spa_namespace_lock);
			spa = spa_lookup(name);
			if (spa)
				spa_altroot(spa, altroot, buflen);
			else
				altroot[0] = '\0';
			spa = NULL;
			mutex_exit(&spa_namespace_lock);
		} else {
			spa_altroot(spa, altroot, buflen);
		}
	}

	if (spa != NULL) {
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		spa_close(spa, FTAG);
	}

	return (error);
}

/*
 * Validate that the auxiliary device array is well formed.  We must have an
 * array of nvlists, each which describes a valid leaf vdev.  If this is an
 * import (mode is VDEV_ALLOC_SPARE), then we allow corrupted spares to be
 * specified, as long as they are well-formed.
 */
static int
spa_validate_aux_devs(spa_t *spa, nvlist_t *nvroot, uint64_t crtxg, int mode,
    spa_aux_vdev_t *sav, const char *config, uint64_t version,
    vdev_labeltype_t label)
{
	nvlist_t **dev;
	uint_t i, ndev;
	vdev_t *vd;
	int error;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/*
	 * It's acceptable to have no devs specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, config, &dev, &ndev) != 0)
		return (0);

	if (ndev == 0)
		return (SET_ERROR(EINVAL));

	/*
	 * Make sure the pool is formatted with a version that supports this
	 * device type.
	 */
	if (spa_version(spa) < version)
		return (SET_ERROR(ENOTSUP));

	/*
	 * Set the pending device list so we correctly handle device in-use
	 * checking.
	 */
	sav->sav_pending = dev;
	sav->sav_npending = ndev;

	for (i = 0; i < ndev; i++) {
		if ((error = spa_config_parse(spa, &vd, dev[i], NULL, 0,
		    mode)) != 0)
			goto out;

		if (!vd->vdev_ops->vdev_op_leaf) {
			vdev_free(vd);
			error = SET_ERROR(EINVAL);
			goto out;
		}

		vd->vdev_top = vd;

		if ((error = vdev_open(vd)) == 0 &&
		    (error = vdev_label_init(vd, crtxg, label)) == 0) {
			fnvlist_add_uint64(dev[i], ZPOOL_CONFIG_GUID,
			    vd->vdev_guid);
		}

		vdev_free(vd);

		if (error &&
		    (mode != VDEV_ALLOC_SPARE && mode != VDEV_ALLOC_L2CACHE))
			goto out;
		else
			error = 0;
	}

out:
	sav->sav_pending = NULL;
	sav->sav_npending = 0;
	return (error);
}

static int
spa_validate_aux(spa_t *spa, nvlist_t *nvroot, uint64_t crtxg, int mode)
{
	int error;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if ((error = spa_validate_aux_devs(spa, nvroot, crtxg, mode,
	    &spa->spa_spares, ZPOOL_CONFIG_SPARES, SPA_VERSION_SPARES,
	    VDEV_LABEL_SPARE)) != 0) {
		return (error);
	}

	return (spa_validate_aux_devs(spa, nvroot, crtxg, mode,
	    &spa->spa_l2cache, ZPOOL_CONFIG_L2CACHE, SPA_VERSION_L2CACHE,
	    VDEV_LABEL_L2CACHE));
}

static void
spa_set_aux_vdevs(spa_aux_vdev_t *sav, nvlist_t **devs, int ndevs,
    const char *config)
{
	int i;

	if (sav->sav_config != NULL) {
		nvlist_t **olddevs;
		uint_t oldndevs;
		nvlist_t **newdevs;

		/*
		 * Generate new dev list by concatenating with the
		 * current dev list.
		 */
		VERIFY0(nvlist_lookup_nvlist_array(sav->sav_config, config,
		    &olddevs, &oldndevs));

		newdevs = kmem_alloc(sizeof (void *) *
		    (ndevs + oldndevs), KM_SLEEP);
		for (i = 0; i < oldndevs; i++)
			newdevs[i] = fnvlist_dup(olddevs[i]);
		for (i = 0; i < ndevs; i++)
			newdevs[i + oldndevs] = fnvlist_dup(devs[i]);

		fnvlist_remove(sav->sav_config, config);

		fnvlist_add_nvlist_array(sav->sav_config, config,
		    (const nvlist_t * const *)newdevs, ndevs + oldndevs);
		for (i = 0; i < oldndevs + ndevs; i++)
			nvlist_free(newdevs[i]);
		kmem_free(newdevs, (oldndevs + ndevs) * sizeof (void *));
	} else {
		/*
		 * Generate a new dev list.
		 */
		sav->sav_config = fnvlist_alloc();
		fnvlist_add_nvlist_array(sav->sav_config, config,
		    (const nvlist_t * const *)devs, ndevs);
	}
}

/*
 * Stop and drop level 2 ARC devices
 */
void
spa_l2cache_drop(spa_t *spa)
{
	vdev_t *vd;
	int i;
	spa_aux_vdev_t *sav = &spa->spa_l2cache;

	for (i = 0; i < sav->sav_count; i++) {
		uint64_t pool;

		vd = sav->sav_vdevs[i];
		ASSERT(vd != NULL);

		if (spa_l2cache_exists(vd->vdev_guid, &pool) &&
		    pool != 0ULL && l2arc_vdev_present(vd))
			l2arc_remove_vdev(vd);
	}
}

/*
 * Verify encryption parameters for spa creation. If we are encrypting, we must
 * have the encryption feature flag enabled.
 */
static int
spa_create_check_encryption_params(dsl_crypto_params_t *dcp,
    boolean_t has_encryption)
{
	if (dcp->cp_crypt != ZIO_CRYPT_OFF &&
	    dcp->cp_crypt != ZIO_CRYPT_INHERIT &&
	    !has_encryption)
		return (SET_ERROR(ENOTSUP));

	return (dmu_objset_create_crypt_check(NULL, dcp, NULL));
}

/*
 * Pool Creation
 */
int
spa_create(const char *pool, nvlist_t *nvroot, nvlist_t *props,
    nvlist_t *zplprops, dsl_crypto_params_t *dcp)
{
	spa_t *spa;
	const char *altroot = NULL;
	vdev_t *rvd;
	dsl_pool_t *dp;
	dmu_tx_t *tx;
	int error = 0;
	uint64_t txg = TXG_INITIAL;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;
	uint64_t version, obj, ndraid = 0;
	boolean_t has_features;
	boolean_t has_encryption;
	boolean_t has_allocclass;
	spa_feature_t feat;
	const char *feat_name;
	const char *poolname;
	nvlist_t *nvl;

	if (props == NULL ||
	    nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_TNAME), &poolname) != 0)
		poolname = (char *)pool;

	/*
	 * If this pool already exists, return failure.
	 */
	mutex_enter(&spa_namespace_lock);
	if (spa_lookup(poolname) != NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EEXIST));
	}

	/*
	 * Allocate a new spa_t structure.
	 */
	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_POOL_NAME, pool);
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);
	spa = spa_add(poolname, nvl, altroot);
	fnvlist_free(nvl);
	spa_activate(spa, spa_mode_global);

	if (props && (error = spa_prop_validate(spa, props))) {
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	/*
	 * Temporary pool names should never be written to disk.
	 */
	if (poolname != pool)
		spa->spa_import_flags |= ZFS_IMPORT_TEMP_NAME;

	has_features = B_FALSE;
	has_encryption = B_FALSE;
	has_allocclass = B_FALSE;
	for (nvpair_t *elem = nvlist_next_nvpair(props, NULL);
	    elem != NULL; elem = nvlist_next_nvpair(props, elem)) {
		if (zpool_prop_feature(nvpair_name(elem))) {
			has_features = B_TRUE;

			feat_name = strchr(nvpair_name(elem), '@') + 1;
			VERIFY0(zfeature_lookup_name(feat_name, &feat));
			if (feat == SPA_FEATURE_ENCRYPTION)
				has_encryption = B_TRUE;
			if (feat == SPA_FEATURE_ALLOCATION_CLASSES)
				has_allocclass = B_TRUE;
		}
	}

	/* verify encryption params, if they were provided */
	if (dcp != NULL) {
		error = spa_create_check_encryption_params(dcp, has_encryption);
		if (error != 0) {
			spa_deactivate(spa);
			spa_remove(spa);
			mutex_exit(&spa_namespace_lock);
			return (error);
		}
	}
	if (!has_allocclass && zfs_special_devs(nvroot, NULL)) {
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (ENOTSUP);
	}

	if (has_features || nvlist_lookup_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_VERSION), &version) != 0) {
		version = SPA_VERSION;
	}
	ASSERT(SPA_VERSION_IS_SUPPORTED(version));

	spa->spa_first_txg = txg;
	spa->spa_uberblock.ub_txg = txg - 1;
	spa->spa_uberblock.ub_version = version;
	spa->spa_ubsync = spa->spa_uberblock;
	spa->spa_load_state = SPA_LOAD_CREATE;
	spa->spa_removing_phys.sr_state = DSS_NONE;
	spa->spa_removing_phys.sr_removing_vdev = -1;
	spa->spa_removing_phys.sr_prev_indirect_vdev = -1;
	spa->spa_indirect_vdevs_loaded = B_TRUE;

	/*
	 * Create "The Godfather" zio to hold all async IOs
	 */
	spa->spa_async_zio_root = kmem_alloc(max_ncpus * sizeof (void *),
	    KM_SLEEP);
	for (int i = 0; i < max_ncpus; i++) {
		spa->spa_async_zio_root[i] = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
		    ZIO_FLAG_GODFATHER);
	}

	/*
	 * Create the root vdev.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	error = spa_config_parse(spa, &rvd, nvroot, NULL, 0, VDEV_ALLOC_ADD);

	ASSERT(error != 0 || rvd != NULL);
	ASSERT(error != 0 || spa->spa_root_vdev == rvd);

	if (error == 0 && !zfs_allocatable_devs(nvroot))
		error = SET_ERROR(EINVAL);

	if (error == 0 &&
	    (error = vdev_create(rvd, txg, B_FALSE)) == 0 &&
	    (error = vdev_draid_spare_create(nvroot, rvd, &ndraid, 0)) == 0 &&
	    (error = spa_validate_aux(spa, nvroot, txg, VDEV_ALLOC_ADD)) == 0) {
		/*
		 * instantiate the metaslab groups (this will dirty the vdevs)
		 * we can no longer error exit past this point
		 */
		for (int c = 0; error == 0 && c < rvd->vdev_children; c++) {
			vdev_t *vd = rvd->vdev_child[c];

			vdev_metaslab_set_size(vd);
			vdev_expand(vd, txg);
		}
	}

	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_unload(spa);
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	/*
	 * Get the list of spares, if specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		spa->spa_spares.sav_config = fnvlist_alloc();
		fnvlist_add_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, (const nvlist_t * const *)spares,
		    nspares);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_spares.sav_sync = B_TRUE;
	}

	/*
	 * Get the list of level 2 cache devices, if specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		VERIFY0(nvlist_alloc(&spa->spa_l2cache.sav_config,
		    NV_UNIQUE_NAME, KM_SLEEP));
		fnvlist_add_nvlist_array(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, (const nvlist_t * const *)l2cache,
		    nl2cache);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	spa->spa_is_initializing = B_TRUE;
	spa->spa_dsl_pool = dp = dsl_pool_create(spa, zplprops, dcp, txg);
	spa->spa_is_initializing = B_FALSE;

	/*
	 * Create DDTs (dedup tables).
	 */
	ddt_create(spa);
	/*
	 * Create BRT table and BRT table object.
	 */
	brt_create(spa);

	spa_update_dspace(spa);

	tx = dmu_tx_create_assigned(dp, txg);

	/*
	 * Create the pool's history object.
	 */
	if (version >= SPA_VERSION_ZPOOL_HISTORY && !spa->spa_history)
		spa_history_create_obj(spa, tx);

	spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_CREATE);
	spa_history_log_version(spa, "create", tx);

	/*
	 * Create the pool config object.
	 */
	spa->spa_config_object = dmu_object_alloc(spa->spa_meta_objset,
	    DMU_OT_PACKED_NVLIST, SPA_CONFIG_BLOCKSIZE,
	    DMU_OT_PACKED_NVLIST_SIZE, sizeof (uint64_t), tx);

	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CONFIG,
	    sizeof (uint64_t), 1, &spa->spa_config_object, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add pool config");
	}

	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CREATION_VERSION,
	    sizeof (uint64_t), 1, &version, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add pool version");
	}

	/* Newly created pools with the right version are always deflated. */
	if (version >= SPA_VERSION_RAIDZ_DEFLATE) {
		spa->spa_deflate = TRUE;
		if (zap_add(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
		    sizeof (uint64_t), 1, &spa->spa_deflate, tx) != 0) {
			cmn_err(CE_PANIC, "failed to add deflate");
		}
	}

	/*
	 * Create the deferred-free bpobj.  Turn off compression
	 * because sync-to-convergence takes longer if the blocksize
	 * keeps changing.
	 */
	obj = bpobj_alloc(spa->spa_meta_objset, 1 << 14, tx);
	dmu_object_set_compress(spa->spa_meta_objset, obj,
	    ZIO_COMPRESS_OFF, tx);
	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SYNC_BPOBJ,
	    sizeof (uint64_t), 1, &obj, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add bpobj");
	}
	VERIFY3U(0, ==, bpobj_open(&spa->spa_deferred_bpobj,
	    spa->spa_meta_objset, obj));

	/*
	 * Generate some random noise for salted checksums to operate on.
	 */
	(void) random_get_pseudo_bytes(spa->spa_cksum_salt.zcs_bytes,
	    sizeof (spa->spa_cksum_salt.zcs_bytes));

	/*
	 * Set pool properties.
	 */
	spa->spa_bootfs = zpool_prop_default_numeric(ZPOOL_PROP_BOOTFS);
	spa->spa_delegation = zpool_prop_default_numeric(ZPOOL_PROP_DELEGATION);
	spa->spa_failmode = zpool_prop_default_numeric(ZPOOL_PROP_FAILUREMODE);
	spa->spa_autoexpand = zpool_prop_default_numeric(ZPOOL_PROP_AUTOEXPAND);
	spa->spa_multihost = zpool_prop_default_numeric(ZPOOL_PROP_MULTIHOST);
	spa->spa_autotrim = zpool_prop_default_numeric(ZPOOL_PROP_AUTOTRIM);

	if (props != NULL) {
		spa_configfile_set(spa, props, B_FALSE);
		spa_sync_props(props, tx);
	}

	for (int i = 0; i < ndraid; i++)
		spa_feature_incr(spa, SPA_FEATURE_DRAID, tx);

	dmu_tx_commit(tx);

	spa->spa_sync_on = B_TRUE;
	txg_sync_start(dp);
	mmp_thread_start(spa);
	txg_wait_synced(dp, txg);

	spa_spawn_aux_threads(spa);

	spa_write_cachefile(spa, B_FALSE, B_TRUE, B_TRUE);

	/*
	 * Don't count references from objsets that are already closed
	 * and are making their way through the eviction process.
	 */
	spa_evicting_os_wait(spa);
	spa->spa_minref = zfs_refcount_count(&spa->spa_refcount);
	spa->spa_load_state = SPA_LOAD_NONE;

	spa_import_os(spa);

	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Import a non-root pool into the system.
 */
int
spa_import(char *pool, nvlist_t *config, nvlist_t *props, uint64_t flags)
{
	spa_t *spa;
	const char *altroot = NULL;
	spa_load_state_t state = SPA_LOAD_IMPORT;
	zpool_load_policy_t policy;
	spa_mode_t mode = spa_mode_global;
	uint64_t readonly = B_FALSE;
	int error;
	nvlist_t *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	/*
	 * If a pool with this name exists, return failure.
	 */
	mutex_enter(&spa_namespace_lock);
	if (spa_lookup(pool) != NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EEXIST));
	}

	/*
	 * Create and initialize the spa structure.
	 */
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);
	(void) nvlist_lookup_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_READONLY), &readonly);
	if (readonly)
		mode = SPA_MODE_READ;
	spa = spa_add(pool, config, altroot);
	spa->spa_import_flags = flags;

	/*
	 * Verbatim import - Take a pool and insert it into the namespace
	 * as if it had been loaded at boot.
	 */
	if (spa->spa_import_flags & ZFS_IMPORT_VERBATIM) {
		if (props != NULL)
			spa_configfile_set(spa, props, B_FALSE);

		spa_write_cachefile(spa, B_FALSE, B_TRUE, B_FALSE);
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_IMPORT);
		zfs_dbgmsg("spa_import: verbatim import of %s", pool);
		mutex_exit(&spa_namespace_lock);
		return (0);
	}

	spa_activate(spa, mode);

	/*
	 * Don't start async tasks until we know everything is healthy.
	 */
	spa_async_suspend(spa);

	zpool_get_load_policy(config, &policy);
	if (policy.zlp_rewind & ZPOOL_DO_REWIND)
		state = SPA_LOAD_RECOVER;

	spa->spa_config_source = SPA_CONFIG_SRC_TRYIMPORT;

	if (state != SPA_LOAD_RECOVER) {
		spa->spa_last_ubsync_txg = spa->spa_load_txg = 0;
		zfs_dbgmsg("spa_import: importing %s", pool);
	} else {
		zfs_dbgmsg("spa_import: importing %s, max_txg=%lld "
		    "(RECOVERY MODE)", pool, (longlong_t)policy.zlp_txg);
	}
	error = spa_load_best(spa, state, policy.zlp_txg, policy.zlp_rewind);

	/*
	 * Propagate anything learned while loading the pool and pass it
	 * back to caller (i.e. rewind info, missing devices, etc).
	 */
	fnvlist_add_nvlist(config, ZPOOL_CONFIG_LOAD_INFO, spa->spa_load_info);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	/*
	 * Toss any existing sparelist, as it doesn't have any validity
	 * anymore, and conflicts with spa_has_spare().
	 */
	if (spa->spa_spares.sav_config) {
		nvlist_free(spa->spa_spares.sav_config);
		spa->spa_spares.sav_config = NULL;
		spa_load_spares(spa);
	}
	if (spa->spa_l2cache.sav_config) {
		nvlist_free(spa->spa_l2cache.sav_config);
		spa->spa_l2cache.sav_config = NULL;
		spa_load_l2cache(spa);
	}

	nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (props != NULL)
		spa_configfile_set(spa, props, B_FALSE);

	if (error != 0 || (props && spa_writeable(spa) &&
	    (error = spa_prop_set(spa, props)))) {
		spa_unload(spa);
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	spa_async_resume(spa);

	/*
	 * Override any spares and level 2 cache devices as specified by
	 * the user, as these may have correct device names/devids, etc.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		if (spa->spa_spares.sav_config)
			fnvlist_remove(spa->spa_spares.sav_config,
			    ZPOOL_CONFIG_SPARES);
		else
			spa->spa_spares.sav_config = fnvlist_alloc();
		fnvlist_add_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, (const nvlist_t * const *)spares,
		    nspares);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_spares.sav_sync = B_TRUE;
	}
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		if (spa->spa_l2cache.sav_config)
			fnvlist_remove(spa->spa_l2cache.sav_config,
			    ZPOOL_CONFIG_L2CACHE);
		else
			spa->spa_l2cache.sav_config = fnvlist_alloc();
		fnvlist_add_nvlist_array(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, (const nvlist_t * const *)l2cache,
		    nl2cache);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	/*
	 * Check for any removed devices.
	 */
	if (spa->spa_autoreplace) {
		spa_aux_check_removed(&spa->spa_spares);
		spa_aux_check_removed(&spa->spa_l2cache);
	}

	if (spa_writeable(spa)) {
		/*
		 * Update the config cache to include the newly-imported pool.
		 */
		spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
	}

	/*
	 * It's possible that the pool was expanded while it was exported.
	 * We kick off an async task to handle this for us.
	 */
	spa_async_request(spa, SPA_ASYNC_AUTOEXPAND);

	spa_history_log_version(spa, "import", NULL);

	spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_IMPORT);

	mutex_exit(&spa_namespace_lock);

	zvol_create_minors_recursive(pool);

	spa_import_os(spa);

	return (0);
}

nvlist_t *
spa_tryimport(nvlist_t *tryconfig)
{
	nvlist_t *config = NULL;
	const char *poolname, *cachefile;
	spa_t *spa;
	uint64_t state;
	int error;
	zpool_load_policy_t policy;

	if (nvlist_lookup_string(tryconfig, ZPOOL_CONFIG_POOL_NAME, &poolname))
		return (NULL);

	if (nvlist_lookup_uint64(tryconfig, ZPOOL_CONFIG_POOL_STATE, &state))
		return (NULL);

	/*
	 * Create and initialize the spa structure.
	 */
	char *name = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) snprintf(name, MAXPATHLEN, "%s-%llx-%s",
	    TRYIMPORT_NAME, (u_longlong_t)curthread, poolname);

	mutex_enter(&spa_namespace_lock);
	spa = spa_add(name, tryconfig, NULL);
	spa_activate(spa, SPA_MODE_READ);
	kmem_free(name, MAXPATHLEN);

	/*
	 * Rewind pool if a max txg was provided.
	 */
	zpool_get_load_policy(spa->spa_config, &policy);
	if (policy.zlp_txg != UINT64_MAX) {
		spa->spa_load_max_txg = policy.zlp_txg;
		spa->spa_extreme_rewind = B_TRUE;
		zfs_dbgmsg("spa_tryimport: importing %s, max_txg=%lld",
		    poolname, (longlong_t)policy.zlp_txg);
	} else {
		zfs_dbgmsg("spa_tryimport: importing %s", poolname);
	}

	if (nvlist_lookup_string(tryconfig, ZPOOL_CONFIG_CACHEFILE, &cachefile)
	    == 0) {
		zfs_dbgmsg("spa_tryimport: using cachefile '%s'", cachefile);
		spa->spa_config_source = SPA_CONFIG_SRC_CACHEFILE;
	} else {
		spa->spa_config_source = SPA_CONFIG_SRC_SCAN;
	}

	/*
	 * spa_import() relies on a pool config fetched by spa_try_import()
	 * for spare/cache devices. Import flags are not passed to
	 * spa_tryimport(), which makes it return early due to a missing log
	 * device and missing retrieving the cache device and spare eventually.
	 * Passing ZFS_IMPORT_MISSING_LOG to spa_tryimport() makes it fetch
	 * the correct configuration regardless of the missing log device.
	 */
	spa->spa_import_flags |= ZFS_IMPORT_MISSING_LOG;

	error = spa_load(spa, SPA_LOAD_TRYIMPORT, SPA_IMPORT_EXISTING);

	/*
	 * If 'tryconfig' was at least parsable, return the current config.
	 */
	if (spa->spa_root_vdev != NULL) {
		config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);
		fnvlist_add_string(config, ZPOOL_CONFIG_POOL_NAME, poolname);
		fnvlist_add_uint64(config, ZPOOL_CONFIG_POOL_STATE, state);
		fnvlist_add_uint64(config, ZPOOL_CONFIG_TIMESTAMP,
		    spa->spa_uberblock.ub_timestamp);
		fnvlist_add_nvlist(config, ZPOOL_CONFIG_LOAD_INFO,
		    spa->spa_load_info);
		fnvlist_add_uint64(config, ZPOOL_CONFIG_ERRATA,
		    spa->spa_errata);

		/*
		 * If the bootfs property exists on this pool then we
		 * copy it out so that external consumers can tell which
		 * pools are bootable.
		 */
		if ((!error || error == EEXIST) && spa->spa_bootfs) {
			char *tmpname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

			/*
			 * We have to play games with the name since the
			 * pool was opened as TRYIMPORT_NAME.
			 */
			if (dsl_dsobj_to_dsname(spa_name(spa),
			    spa->spa_bootfs, tmpname) == 0) {
				char *cp;
				char *dsname;

				dsname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

				cp = strchr(tmpname, '/');
				if (cp == NULL) {
					(void) strlcpy(dsname, tmpname,
					    MAXPATHLEN);
				} else {
					(void) snprintf(dsname, MAXPATHLEN,
					    "%s/%s", poolname, ++cp);
				}
				fnvlist_add_string(config, ZPOOL_CONFIG_BOOTFS,
				    dsname);
				kmem_free(dsname, MAXPATHLEN);
			}
			kmem_free(tmpname, MAXPATHLEN);
		}

		/*
		 * Add the list of hot spares and level 2 cache devices.
		 */
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		spa_add_spares(spa, config);
		spa_add_l2cache(spa, config);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	spa_unload(spa);
	spa_deactivate(spa);
	spa_remove(spa);
	mutex_exit(&spa_namespace_lock);

	return (config);
}

/*
 * Pool export/destroy
 *
 * The act of destroying or exporting a pool is very simple.  We make sure there
 * is no more pending I/O and any references to the pool are gone.  Then, we
 * update the pool state and sync all the labels to disk, removing the
 * configuration from the cache afterwards. If the 'hardforce' flag is set, then
 * we don't sync the labels or remove the configuration cache.
 */
static int
spa_export_common(const char *pool, int new_state, nvlist_t **oldconfig,
    boolean_t force, boolean_t hardforce)
{
	int error;
	spa_t *spa;
	hrtime_t export_start = gethrtime();

	if (oldconfig)
		*oldconfig = NULL;

	if (!(spa_mode_global & SPA_MODE_WRITE))
		return (SET_ERROR(EROFS));

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(pool)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ENOENT));
	}

	if (spa->spa_is_exporting) {
		/* the pool is being exported by another thread */
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ZFS_ERR_EXPORT_IN_PROGRESS));
	}
	spa->spa_is_exporting = B_TRUE;

	/*
	 * Put a hold on the pool, drop the namespace lock, stop async tasks,
	 * reacquire the namespace lock, and see if we can export.
	 */
	spa_open_ref(spa, FTAG);
	mutex_exit(&spa_namespace_lock);
	spa_async_suspend(spa);
	if (spa->spa_zvol_taskq) {
		zvol_remove_minors(spa, spa_name(spa), B_TRUE);
		taskq_wait(spa->spa_zvol_taskq);
	}
	mutex_enter(&spa_namespace_lock);
	spa_close(spa, FTAG);

	if (spa->spa_state == POOL_STATE_UNINITIALIZED)
		goto export_spa;
	/*
	 * The pool will be in core if it's openable, in which case we can
	 * modify its state.  Objsets may be open only because they're dirty,
	 * so we have to force it to sync before checking spa_refcnt.
	 */
	if (spa->spa_sync_on) {
		txg_wait_synced(spa->spa_dsl_pool, 0);
		spa_evicting_os_wait(spa);
	}

	/*
	 * A pool cannot be exported or destroyed if there are active
	 * references.  If we are resetting a pool, allow references by
	 * fault injection handlers.
	 */
	if (!spa_refcount_zero(spa) || (spa->spa_inject_ref != 0)) {
		error = SET_ERROR(EBUSY);
		goto fail;
	}

	if (spa->spa_sync_on) {
		vdev_t *rvd = spa->spa_root_vdev;
		/*
		 * A pool cannot be exported if it has an active shared spare.
		 * This is to prevent other pools stealing the active spare
		 * from an exported pool. At user's own will, such pool can
		 * be forcedly exported.
		 */
		if (!force && new_state == POOL_STATE_EXPORTED &&
		    spa_has_active_shared_spare(spa)) {
			error = SET_ERROR(EXDEV);
			goto fail;
		}

		/*
		 * We're about to export or destroy this pool. Make sure
		 * we stop all initialization and trim activity here before
		 * we set the spa_final_txg. This will ensure that all
		 * dirty data resulting from the initialization is
		 * committed to disk before we unload the pool.
		 */
		vdev_initialize_stop_all(rvd, VDEV_INITIALIZE_ACTIVE);
		vdev_trim_stop_all(rvd, VDEV_TRIM_ACTIVE);
		vdev_autotrim_stop_all(spa);
		vdev_rebuild_stop_all(spa);

		/*
		 * We want this to be reflected on every label,
		 * so mark them all dirty.  spa_unload() will do the
		 * final sync that pushes these changes out.
		 */
		if (new_state != POOL_STATE_UNINITIALIZED && !hardforce) {
			spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
			spa->spa_state = new_state;
			vdev_config_dirty(rvd);
			spa_config_exit(spa, SCL_ALL, FTAG);
		}

		/*
		 * If the log space map feature is enabled and the pool is
		 * getting exported (but not destroyed), we want to spend some
		 * time flushing as many metaslabs as we can in an attempt to
		 * destroy log space maps and save import time. This has to be
		 * done before we set the spa_final_txg, otherwise
		 * spa_sync() -> spa_flush_metaslabs() may dirty the final TXGs.
		 * spa_should_flush_logs_on_unload() should be called after
		 * spa_state has been set to the new_state.
		 */
		if (spa_should_flush_logs_on_unload(spa))
			spa_unload_log_sm_flush_all(spa);

		if (new_state != POOL_STATE_UNINITIALIZED && !hardforce) {
			spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
			spa->spa_final_txg = spa_last_synced_txg(spa) +
			    TXG_DEFER_SIZE + 1;
			spa_config_exit(spa, SCL_ALL, FTAG);
		}
	}

export_spa:
	spa_export_os(spa);

	if (new_state == POOL_STATE_DESTROYED)
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_DESTROY);
	else if (new_state == POOL_STATE_EXPORTED)
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_EXPORT);

	if (spa->spa_state != POOL_STATE_UNINITIALIZED) {
		spa_unload(spa);
		spa_deactivate(spa);
	}

	if (oldconfig && spa->spa_config)
		*oldconfig = fnvlist_dup(spa->spa_config);

	if (new_state != POOL_STATE_UNINITIALIZED) {
		if (!hardforce)
			spa_write_cachefile(spa, B_TRUE, B_TRUE, B_FALSE);
		spa_remove(spa);
	} else {
		/*
		 * If spa_remove() is not called for this spa_t and
		 * there is any possibility that it can be reused,
		 * we make sure to reset the exporting flag.
		 */
		spa->spa_is_exporting = B_FALSE;
	}

	if (new_state == POOL_STATE_EXPORTED)
		zio_handle_export_delay(spa, gethrtime() - export_start);

	mutex_exit(&spa_namespace_lock);
	return (0);

fail:
	spa->spa_is_exporting = B_FALSE;
	spa_async_resume(spa);
	mutex_exit(&spa_namespace_lock);
	return (error);
}

/*
 * Destroy a storage pool.
 */
int
spa_destroy(const char *pool)
{
	return (spa_export_common(pool, POOL_STATE_DESTROYED, NULL,
	    B_FALSE, B_FALSE));
}

/*
 * Export a storage pool.
 */
int
spa_export(const char *pool, nvlist_t **oldconfig, boolean_t force,
    boolean_t hardforce)
{
	return (spa_export_common(pool, POOL_STATE_EXPORTED, oldconfig,
	    force, hardforce));
}

/*
 * Similar to spa_export(), this unloads the spa_t without actually removing it
 * from the namespace in any way.
 */
int
spa_reset(const char *pool)
{
	return (spa_export_common(pool, POOL_STATE_UNINITIALIZED, NULL,
	    B_FALSE, B_FALSE));
}

/*
 * ==========================================================================
 * Device manipulation
 * ==========================================================================
 */

/*
 * This is called as a synctask to increment the draid feature flag
 */
static void
spa_draid_feature_incr(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	int draid = (int)(uintptr_t)arg;

	for (int c = 0; c < draid; c++)
		spa_feature_incr(spa, SPA_FEATURE_DRAID, tx);
}

/*
 * Add a device to a storage pool.
 */
int
spa_vdev_add(spa_t *spa, nvlist_t *nvroot, boolean_t check_ashift)
{
	uint64_t txg, ndraid = 0;
	int error;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd, *tvd;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	if ((error = spa_config_parse(spa, &vd, nvroot, NULL, 0,
	    VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, NULL, txg, error));

	spa->spa_pending_vdev = vd;	/* spa_vdev_exit() will clear this */

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES, &spares,
	    &nspares) != 0)
		nspares = 0;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE, &l2cache,
	    &nl2cache) != 0)
		nl2cache = 0;

	if (vd->vdev_children == 0 && nspares == 0 && nl2cache == 0)
		return (spa_vdev_exit(spa, vd, txg, EINVAL));

	if (vd->vdev_children != 0 &&
	    (error = vdev_create(vd, txg, B_FALSE)) != 0) {
		return (spa_vdev_exit(spa, vd, txg, error));
	}

	/*
	 * The virtual dRAID spares must be added after vdev tree is created
	 * and the vdev guids are generated.  The guid of their associated
	 * dRAID is stored in the config and used when opening the spare.
	 */
	if ((error = vdev_draid_spare_create(nvroot, vd, &ndraid,
	    rvd->vdev_children)) == 0) {
		if (ndraid > 0 && nvlist_lookup_nvlist_array(nvroot,
		    ZPOOL_CONFIG_SPARES, &spares, &nspares) != 0)
			nspares = 0;
	} else {
		return (spa_vdev_exit(spa, vd, txg, error));
	}

	/*
	 * We must validate the spares and l2cache devices after checking the
	 * children.  Otherwise, vdev_inuse() will blindly overwrite the spare.
	 */
	if ((error = spa_validate_aux(spa, nvroot, txg, VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, vd, txg, error));

	/*
	 * If we are in the middle of a device removal, we can only add
	 * devices which match the existing devices in the pool.
	 * If we are in the middle of a removal, or have some indirect
	 * vdevs, we can not add raidz or dRAID top levels.
	 */
	if (spa->spa_vdev_removal != NULL ||
	    spa->spa_removing_phys.sr_prev_indirect_vdev != -1) {
		for (int c = 0; c < vd->vdev_children; c++) {
			tvd = vd->vdev_child[c];
			if (spa->spa_vdev_removal != NULL &&
			    tvd->vdev_ashift != spa->spa_max_ashift) {
				return (spa_vdev_exit(spa, vd, txg, EINVAL));
			}
			/* Fail if top level vdev is raidz or a dRAID */
			if (vdev_get_nparity(tvd) != 0)
				return (spa_vdev_exit(spa, vd, txg, EINVAL));

			/*
			 * Need the top level mirror to be
			 * a mirror of leaf vdevs only
			 */
			if (tvd->vdev_ops == &vdev_mirror_ops) {
				for (uint64_t cid = 0;
				    cid < tvd->vdev_children; cid++) {
					vdev_t *cvd = tvd->vdev_child[cid];
					if (!cvd->vdev_ops->vdev_op_leaf) {
						return (spa_vdev_exit(spa, vd,
						    txg, EINVAL));
					}
				}
			}
		}
	}

	if (check_ashift && spa->spa_max_ashift == spa->spa_min_ashift) {
		for (int c = 0; c < vd->vdev_children; c++) {
			tvd = vd->vdev_child[c];
			if (tvd->vdev_ashift != spa->spa_max_ashift) {
				return (spa_vdev_exit(spa, vd, txg,
				    ZFS_ERR_ASHIFT_MISMATCH));
			}
		}
	}

	for (int c = 0; c < vd->vdev_children; c++) {
		tvd = vd->vdev_child[c];
		vdev_remove_child(vd, tvd);
		tvd->vdev_id = rvd->vdev_children;
		vdev_add_child(rvd, tvd);
		vdev_config_dirty(tvd);
	}

	if (nspares != 0) {
		spa_set_aux_vdevs(&spa->spa_spares, spares, nspares,
		    ZPOOL_CONFIG_SPARES);
		spa_load_spares(spa);
		spa->spa_spares.sav_sync = B_TRUE;
	}

	if (nl2cache != 0) {
		spa_set_aux_vdevs(&spa->spa_l2cache, l2cache, nl2cache,
		    ZPOOL_CONFIG_L2CACHE);
		spa_load_l2cache(spa);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	/*
	 * We can't increment a feature while holding spa_vdev so we
	 * have to do it in a synctask.
	 */
	if (ndraid != 0) {
		dmu_tx_t *tx;

		tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);
		dsl_sync_task_nowait(spa->spa_dsl_pool, spa_draid_feature_incr,
		    (void *)(uintptr_t)ndraid, tx);
		dmu_tx_commit(tx);
	}

	/*
	 * We have to be careful when adding new vdevs to an existing pool.
	 * If other threads start allocating from these vdevs before we
	 * sync the config cache, and we lose power, then upon reboot we may
	 * fail to open the pool because there are DVAs that the config cache
	 * can't translate.  Therefore, we first add the vdevs without
	 * initializing metaslabs; sync the config cache (via spa_vdev_exit());
	 * and then let spa_config_update() initialize the new metaslabs.
	 *
	 * spa_load() checks for added-but-not-initialized vdevs, so that
	 * if we lose power at any point in this sequence, the remaining
	 * steps will be completed the next time we load the pool.
	 */
	(void) spa_vdev_exit(spa, vd, txg, 0);

	mutex_enter(&spa_namespace_lock);
	spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
	spa_event_notify(spa, NULL, NULL, ESC_ZFS_VDEV_ADD);
	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Attach a device to a vdev specified by its guid.  The vdev type can be
 * a mirror, a raidz, or a leaf device that is also a top-level (e.g. a
 * single device). When the vdev is a single device, a mirror vdev will be
 * automatically inserted.
 *
 * If 'replacing' is specified, the new device is intended to replace the
 * existing device; in this case the two devices are made into their own
 * mirror using the 'replacing' vdev, which is functionally identical to
 * the mirror vdev (it actually reuses all the same ops) but has a few
 * extra rules: you can't attach to it after it's been created, and upon
 * completion of resilvering, the first disk (the one being replaced)
 * is automatically detached.
 *
 * If 'rebuild' is specified, then sequential reconstruction (a.ka. rebuild)
 * should be performed instead of traditional healing reconstruction.  From
 * an administrators perspective these are both resilver operations.
 */
int
spa_vdev_attach(spa_t *spa, uint64_t guid, nvlist_t *nvroot, int replacing,
    int rebuild)
{
	uint64_t txg, dtl_max_txg;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *oldvd, *newvd, *newrootvd, *pvd, *tvd;
	vdev_ops_t *pvops;
	char *oldvdpath, *newvdpath;
	int newvd_isspare = B_FALSE;
	int error;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	oldvd = spa_lookup_by_guid(spa, guid, B_FALSE);

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	if (rebuild) {
		if (!spa_feature_is_enabled(spa, SPA_FEATURE_DEVICE_REBUILD))
			return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

		if (dsl_scan_resilvering(spa_get_dsl(spa)) ||
		    dsl_scan_resilver_scheduled(spa_get_dsl(spa))) {
			return (spa_vdev_exit(spa, NULL, txg,
			    ZFS_ERR_RESILVER_IN_PROGRESS));
		}
	} else {
		if (vdev_rebuild_active(rvd))
			return (spa_vdev_exit(spa, NULL, txg,
			    ZFS_ERR_REBUILD_IN_PROGRESS));
	}

	if (spa->spa_vdev_removal != NULL) {
		return (spa_vdev_exit(spa, NULL, txg,
		    ZFS_ERR_DEVRM_IN_PROGRESS));
	}

	if (oldvd == NULL)
		return (spa_vdev_exit(spa, NULL, txg, ENODEV));

	boolean_t raidz = oldvd->vdev_ops == &vdev_raidz_ops;

	if (raidz) {
		if (!spa_feature_is_enabled(spa, SPA_FEATURE_RAIDZ_EXPANSION))
			return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

		/*
		 * Can't expand a raidz while prior expand is in progress.
		 */
		if (spa->spa_raidz_expand != NULL) {
			return (spa_vdev_exit(spa, NULL, txg,
			    ZFS_ERR_RAIDZ_EXPAND_IN_PROGRESS));
		}
	} else if (!oldvd->vdev_ops->vdev_op_leaf) {
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));
	}

	if (raidz)
		pvd = oldvd;
	else
		pvd = oldvd->vdev_parent;

	if (spa_config_parse(spa, &newrootvd, nvroot, NULL, 0,
	    VDEV_ALLOC_ATTACH) != 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	if (newrootvd->vdev_children != 1)
		return (spa_vdev_exit(spa, newrootvd, txg, EINVAL));

	newvd = newrootvd->vdev_child[0];

	if (!newvd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, newrootvd, txg, EINVAL));

	if ((error = vdev_create(newrootvd, txg, replacing)) != 0)
		return (spa_vdev_exit(spa, newrootvd, txg, error));

	/*
	 * log, dedup and special vdevs should not be replaced by spares.
	 */
	if ((oldvd->vdev_top->vdev_alloc_bias != VDEV_BIAS_NONE ||
	    oldvd->vdev_top->vdev_islog) && newvd->vdev_isspare) {
		return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
	}

	/*
	 * A dRAID spare can only replace a child of its parent dRAID vdev.
	 */
	if (newvd->vdev_ops == &vdev_draid_spare_ops &&
	    oldvd->vdev_top != vdev_draid_spare_get_parent(newvd)) {
		return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
	}

	if (rebuild) {
		/*
		 * For rebuilds, the top vdev must support reconstruction
		 * using only space maps.  This means the only allowable
		 * vdevs types are the root vdev, a mirror, or dRAID.
		 */
		tvd = pvd;
		if (pvd->vdev_top != NULL)
			tvd = pvd->vdev_top;

		if (tvd->vdev_ops != &vdev_mirror_ops &&
		    tvd->vdev_ops != &vdev_root_ops &&
		    tvd->vdev_ops != &vdev_draid_ops) {
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		}
	}

	if (!replacing) {
		/*
		 * For attach, the only allowable parent is a mirror or
		 * the root vdev. A raidz vdev can be attached to, but
		 * you cannot attach to a raidz child.
		 */
		if (pvd->vdev_ops != &vdev_mirror_ops &&
		    pvd->vdev_ops != &vdev_root_ops &&
		    !raidz)
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		pvops = &vdev_mirror_ops;
	} else {
		/*
		 * Active hot spares can only be replaced by inactive hot
		 * spares.
		 */
		if (pvd->vdev_ops == &vdev_spare_ops &&
		    oldvd->vdev_isspare &&
		    !spa_has_spare(spa, newvd->vdev_guid))
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		/*
		 * If the source is a hot spare, and the parent isn't already a
		 * spare, then we want to create a new hot spare.  Otherwise, we
		 * want to create a replacing vdev.  The user is not allowed to
		 * attach to a spared vdev child unless the 'isspare' state is
		 * the same (spare replaces spare, non-spare replaces
		 * non-spare).
		 */
		if (pvd->vdev_ops == &vdev_replacing_ops &&
		    spa_version(spa) < SPA_VERSION_MULTI_REPLACE) {
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		} else if (pvd->vdev_ops == &vdev_spare_ops &&
		    newvd->vdev_isspare != oldvd->vdev_isspare) {
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		}

		if (newvd->vdev_isspare)
			pvops = &vdev_spare_ops;
		else
			pvops = &vdev_replacing_ops;
	}

	/*
	 * Make sure the new device is big enough.
	 */
	vdev_t *min_vdev = raidz ? oldvd->vdev_child[0] : oldvd;
	if (newvd->vdev_asize < vdev_get_min_asize(min_vdev))
		return (spa_vdev_exit(spa, newrootvd, txg, EOVERFLOW));

	/*
	 * The new device cannot have a higher alignment requirement
	 * than the top-level vdev.
	 */
	if (newvd->vdev_ashift > oldvd->vdev_top->vdev_ashift)
		return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

	/*
	 * RAIDZ-expansion-specific checks.
	 */
	if (raidz) {
		if (vdev_raidz_attach_check(newvd) != 0)
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		/*
		 * Fail early if a child is not healthy or being replaced
		 */
		for (int i = 0; i < oldvd->vdev_children; i++) {
			if (vdev_is_dead(oldvd->vdev_child[i]) ||
			    !oldvd->vdev_child[i]->vdev_ops->vdev_op_leaf) {
				return (spa_vdev_exit(spa, newrootvd, txg,
				    ENXIO));
			}
			/* Also fail if reserved boot area is in-use */
			if (vdev_check_boot_reserve(spa, oldvd->vdev_child[i])
			    != 0) {
				return (spa_vdev_exit(spa, newrootvd, txg,
				    EADDRINUSE));
			}
		}
	}

	if (raidz) {
		/*
		 * Note: oldvdpath is freed by spa_strfree(),  but
		 * kmem_asprintf() is freed by kmem_strfree(), so we have to
		 * move it to a spa_strdup-ed string.
		 */
		char *tmp = kmem_asprintf("raidz%u-%u",
		    (uint_t)vdev_get_nparity(oldvd), (uint_t)oldvd->vdev_id);
		oldvdpath = spa_strdup(tmp);
		kmem_strfree(tmp);
	} else {
		oldvdpath = spa_strdup(oldvd->vdev_path);
	}
	newvdpath = spa_strdup(newvd->vdev_path);

	/*
	 * If this is an in-place replacement, update oldvd's path and devid
	 * to make it distinguishable from newvd, and unopenable from now on.
	 */
	if (strcmp(oldvdpath, newvdpath) == 0) {
		spa_strfree(oldvd->vdev_path);
		oldvd->vdev_path = kmem_alloc(strlen(newvdpath) + 5,
		    KM_SLEEP);
		(void) sprintf(oldvd->vdev_path, "%s/old",
		    newvdpath);
		if (oldvd->vdev_devid != NULL) {
			spa_strfree(oldvd->vdev_devid);
			oldvd->vdev_devid = NULL;
		}
		spa_strfree(oldvdpath);
		oldvdpath = spa_strdup(oldvd->vdev_path);
	}

	/*
	 * If the parent is not a mirror, or if we're replacing, insert the new
	 * mirror/replacing/spare vdev above oldvd.
	 */
	if (!raidz && pvd->vdev_ops != pvops) {
		pvd = vdev_add_parent(oldvd, pvops);
		ASSERT(pvd->vdev_ops == pvops);
		ASSERT(oldvd->vdev_parent == pvd);
	}

	ASSERT(pvd->vdev_top->vdev_parent == rvd);

	/*
	 * Extract the new device from its root and add it to pvd.
	 */
	vdev_remove_child(newrootvd, newvd);
	newvd->vdev_id = pvd->vdev_children;
	newvd->vdev_crtxg = oldvd->vdev_crtxg;
	vdev_add_child(pvd, newvd);

	/*
	 * Reevaluate the parent vdev state.
	 */
	vdev_propagate_state(pvd);

	tvd = newvd->vdev_top;
	ASSERT(pvd->vdev_top == tvd);
	ASSERT(tvd->vdev_parent == rvd);

	vdev_config_dirty(tvd);

	/*
	 * Set newvd's DTL to [TXG_INITIAL, dtl_max_txg) so that we account
	 * for any dmu_sync-ed blocks.  It will propagate upward when
	 * spa_vdev_exit() calls vdev_dtl_reassess().
	 */
	dtl_max_txg = txg + TXG_CONCURRENT_STATES;

	if (raidz) {
		/*
		 * Wait for the youngest allocations and frees to sync,
		 * and then wait for the deferral of those frees to finish.
		 */
		spa_vdev_config_exit(spa, NULL,
		    txg + TXG_CONCURRENT_STATES + TXG_DEFER_SIZE, 0, FTAG);

		vdev_initialize_stop_all(tvd, VDEV_INITIALIZE_ACTIVE);
		vdev_trim_stop_all(tvd, VDEV_TRIM_ACTIVE);
		vdev_autotrim_stop_wait(tvd);

		dtl_max_txg = spa_vdev_config_enter(spa);

		tvd->vdev_rz_expanding = B_TRUE;

		vdev_dirty_leaves(tvd, VDD_DTL, dtl_max_txg);
		vdev_config_dirty(tvd);

		dmu_tx_t *tx = dmu_tx_create_assigned(spa->spa_dsl_pool,
		    dtl_max_txg);
		dsl_sync_task_nowait(spa->spa_dsl_pool, vdev_raidz_attach_sync,
		    newvd, tx);
		dmu_tx_commit(tx);
	} else {
		vdev_dtl_dirty(newvd, DTL_MISSING, TXG_INITIAL,
		    dtl_max_txg - TXG_INITIAL);

		if (newvd->vdev_isspare) {
			spa_spare_activate(newvd);
			spa_event_notify(spa, newvd, NULL, ESC_ZFS_VDEV_SPARE);
		}

		newvd_isspare = newvd->vdev_isspare;

		/*
		 * Mark newvd's DTL dirty in this txg.
		 */
		vdev_dirty(tvd, VDD_DTL, newvd, txg);

		/*
		 * Schedule the resilver or rebuild to restart in the future.
		 * We do this to ensure that dmu_sync-ed blocks have been
		 * stitched into the respective datasets.
		 */
		if (rebuild) {
			newvd->vdev_rebuild_txg = txg;

			vdev_rebuild(tvd);
		} else {
			newvd->vdev_resilver_txg = txg;

			if (dsl_scan_resilvering(spa_get_dsl(spa)) &&
			    spa_feature_is_enabled(spa,
			    SPA_FEATURE_RESILVER_DEFER)) {
				vdev_defer_resilver(newvd);
			} else {
				dsl_scan_restart_resilver(spa->spa_dsl_pool,
				    dtl_max_txg);
			}
		}
	}

	if (spa->spa_bootfs)
		spa_event_notify(spa, newvd, NULL, ESC_ZFS_BOOTFS_VDEV_ATTACH);

	spa_event_notify(spa, newvd, NULL, ESC_ZFS_VDEV_ATTACH);

	/*
	 * Commit the config
	 */
	(void) spa_vdev_exit(spa, newrootvd, dtl_max_txg, 0);

	spa_history_log_internal(spa, "vdev attach", NULL,
	    "%s vdev=%s %s vdev=%s",
	    replacing && newvd_isspare ? "spare in" :
	    replacing ? "replace" : "attach", newvdpath,
	    replacing ? "for" : "to", oldvdpath);

	spa_strfree(oldvdpath);
	spa_strfree(newvdpath);

	return (0);
}

/*
 * Detach a device from a mirror or replacing vdev.
 *
 * If 'replace_done' is specified, only detach if the parent
 * is a replacing or a spare vdev.
 */
int
spa_vdev_detach(spa_t *spa, uint64_t guid, uint64_t pguid, int replace_done)
{
	uint64_t txg;
	int error;
	vdev_t *rvd __maybe_unused = spa->spa_root_vdev;
	vdev_t *vd, *pvd, *cvd, *tvd;
	boolean_t unspare = B_FALSE;
	uint64_t unspare_guid = 0;
	char *vdpath;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_detach_enter(spa, guid);

	vd = spa_lookup_by_guid(spa, guid, B_FALSE);

	/*
	 * Besides being called directly from the userland through the
	 * ioctl interface, spa_vdev_detach() can be potentially called
	 * at the end of spa_vdev_resilver_done().
	 *
	 * In the regular case, when we have a checkpoint this shouldn't
	 * happen as we never empty the DTLs of a vdev during the scrub
	 * [see comment in dsl_scan_done()]. Thus spa_vdev_resilvering_done()
	 * should never get here when we have a checkpoint.
	 *
	 * That said, even in a case when we checkpoint the pool exactly
	 * as spa_vdev_resilver_done() calls this function everything
	 * should be fine as the resilver will return right away.
	 */
	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	if (vd == NULL)
		return (spa_vdev_exit(spa, NULL, txg, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	pvd = vd->vdev_parent;

	/*
	 * If the parent/child relationship is not as expected, don't do it.
	 * Consider M(A,R(B,C)) -- that is, a mirror of A with a replacing
	 * vdev that's replacing B with C.  The user's intent in replacing
	 * is to go from M(A,B) to M(A,C).  If the user decides to cancel
	 * the replace by detaching C, the expected behavior is to end up
	 * M(A,B).  But suppose that right after deciding to detach C,
	 * the replacement of B completes.  We would have M(A,C), and then
	 * ask to detach C, which would leave us with just A -- not what
	 * the user wanted.  To prevent this, we make sure that the
	 * parent/child relationship hasn't changed -- in this example,
	 * that C's parent is still the replacing vdev R.
	 */
	if (pvd->vdev_guid != pguid && pguid != 0)
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	/*
	 * Only 'replacing' or 'spare' vdevs can be replaced.
	 */
	if (replace_done && pvd->vdev_ops != &vdev_replacing_ops &&
	    pvd->vdev_ops != &vdev_spare_ops)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	ASSERT(pvd->vdev_ops != &vdev_spare_ops ||
	    spa_version(spa) >= SPA_VERSION_SPARES);

	/*
	 * Only mirror, replacing, and spare vdevs support detach.
	 */
	if (pvd->vdev_ops != &vdev_replacing_ops &&
	    pvd->vdev_ops != &vdev_mirror_ops &&
	    pvd->vdev_ops != &vdev_spare_ops)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	/*
	 * If this device has the only valid copy of some data,
	 * we cannot safely detach it.
	 */
	if (vdev_dtl_required(vd))
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	ASSERT(pvd->vdev_children >= 2);

	/*
	 * If we are detaching the second disk from a replacing vdev, then
	 * check to see if we changed the original vdev's path to have "/old"
	 * at the end in spa_vdev_attach().  If so, undo that change now.
	 */
	if (pvd->vdev_ops == &vdev_replacing_ops && vd->vdev_id > 0 &&
	    vd->vdev_path != NULL) {
		size_t len = strlen(vd->vdev_path);

		for (int c = 0; c < pvd->vdev_children; c++) {
			cvd = pvd->vdev_child[c];

			if (cvd == vd || cvd->vdev_path == NULL)
				continue;

			if (strncmp(cvd->vdev_path, vd->vdev_path, len) == 0 &&
			    strcmp(cvd->vdev_path + len, "/old") == 0) {
				spa_strfree(cvd->vdev_path);
				cvd->vdev_path = spa_strdup(vd->vdev_path);
				break;
			}
		}
	}

	/*
	 * If we are detaching the original disk from a normal spare, then it
	 * implies that the spare should become a real disk, and be removed
	 * from the active spare list for the pool.  dRAID spares on the
	 * other hand are coupled to the pool and thus should never be removed
	 * from the spares list.
	 */
	if (pvd->vdev_ops == &vdev_spare_ops && vd->vdev_id == 0) {
		vdev_t *last_cvd = pvd->vdev_child[pvd->vdev_children - 1];

		if (last_cvd->vdev_isspare &&
		    last_cvd->vdev_ops != &vdev_draid_spare_ops) {
			unspare = B_TRUE;
		}
	}

	/*
	 * Erase the disk labels so the disk can be used for other things.
	 * This must be done after all other error cases are handled,
	 * but before we disembowel vd (so we can still do I/O to it).
	 * But if we can't do it, don't treat the error as fatal --
	 * it may be that the unwritability of the disk is the reason
	 * it's being detached!
	 */
	(void) vdev_label_init(vd, 0, VDEV_LABEL_REMOVE);

	/*
	 * Remove vd from its parent and compact the parent's children.
	 */
	vdev_remove_child(pvd, vd);
	vdev_compact_children(pvd);

	/*
	 * Remember one of the remaining children so we can get tvd below.
	 */
	cvd = pvd->vdev_child[pvd->vdev_children - 1];

	/*
	 * If we need to remove the remaining child from the list of hot spares,
	 * do it now, marking the vdev as no longer a spare in the process.
	 * We must do this before vdev_remove_parent(), because that can
	 * change the GUID if it creates a new toplevel GUID.  For a similar
	 * reason, we must remove the spare now, in the same txg as the detach;
	 * otherwise someone could attach a new sibling, change the GUID, and
	 * the subsequent attempt to spa_vdev_remove(unspare_guid) would fail.
	 */
	if (unspare) {
		ASSERT(cvd->vdev_isspare);
		spa_spare_remove(cvd);
		unspare_guid = cvd->vdev_guid;
		(void) spa_vdev_remove(spa, unspare_guid, B_TRUE);
		cvd->vdev_unspare = B_TRUE;
	}

	/*
	 * If the parent mirror/replacing vdev only has one child,
	 * the parent is no longer needed.  Remove it from the tree.
	 */
	if (pvd->vdev_children == 1) {
		if (pvd->vdev_ops == &vdev_spare_ops)
			cvd->vdev_unspare = B_FALSE;
		vdev_remove_parent(cvd);
	}

	/*
	 * We don't set tvd until now because the parent we just removed
	 * may have been the previous top-level vdev.
	 */
	tvd = cvd->vdev_top;
	ASSERT(tvd->vdev_parent == rvd);

	/*
	 * Reevaluate the parent vdev state.
	 */
	vdev_propagate_state(cvd);

	/*
	 * If the 'autoexpand' property is set on the pool then automatically
	 * try to expand the size of the pool. For example if the device we
	 * just detached was smaller than the others, it may be possible to
	 * add metaslabs (i.e. grow the pool). We need to reopen the vdev
	 * first so that we can obtain the updated sizes of the leaf vdevs.
	 */
	if (spa->spa_autoexpand) {
		vdev_reopen(tvd);
		vdev_expand(tvd, txg);
	}

	vdev_config_dirty(tvd);

	/*
	 * Mark vd's DTL as dirty in this txg.  vdev_dtl_sync() will see that
	 * vd->vdev_detached is set and free vd's DTL object in syncing context.
	 * But first make sure we're not on any *other* txg's DTL list, to
	 * prevent vd from being accessed after it's freed.
	 */
	vdpath = spa_strdup(vd->vdev_path ? vd->vdev_path : "none");
	for (int t = 0; t < TXG_SIZE; t++)
		(void) txg_list_remove_this(&tvd->vdev_dtl_list, vd, t);
	vd->vdev_detached = B_TRUE;
	vdev_dirty(tvd, VDD_DTL, vd, txg);

	spa_event_notify(spa, vd, NULL, ESC_ZFS_VDEV_REMOVE);
	spa_notify_waiters(spa);

	/* hang on to the spa before we release the lock */
	spa_open_ref(spa, FTAG);

	error = spa_vdev_exit(spa, vd, txg, 0);

	spa_history_log_internal(spa, "detach", NULL,
	    "vdev=%s", vdpath);
	spa_strfree(vdpath);

	/*
	 * If this was the removal of the original device in a hot spare vdev,
	 * then we want to go through and remove the device from the hot spare
	 * list of every other pool.
	 */
	if (unspare) {
		spa_t *altspa = NULL;

		mutex_enter(&spa_namespace_lock);
		while ((altspa = spa_next(altspa)) != NULL) {
			if (altspa->spa_state != POOL_STATE_ACTIVE ||
			    altspa == spa)
				continue;

			spa_open_ref(altspa, FTAG);
			mutex_exit(&spa_namespace_lock);
			(void) spa_vdev_remove(altspa, unspare_guid, B_TRUE);
			mutex_enter(&spa_namespace_lock);
			spa_close(altspa, FTAG);
		}
		mutex_exit(&spa_namespace_lock);

		/* search the rest of the vdevs for spares to remove */
		spa_vdev_resilver_done(spa);
	}

	/* all done with the spa; OK to release */
	mutex_enter(&spa_namespace_lock);
	spa_close(spa, FTAG);
	mutex_exit(&spa_namespace_lock);

	return (error);
}

static int
spa_vdev_initialize_impl(spa_t *spa, uint64_t guid, uint64_t cmd_type,
    list_t *vd_list)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);

	/* Look up vdev and ensure it's a leaf. */
	vdev_t *vd = spa_lookup_by_guid(spa, guid, B_FALSE);
	if (vd == NULL || vd->vdev_detached) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(ENODEV));
	} else if (!vd->vdev_ops->vdev_op_leaf || !vdev_is_concrete(vd)) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(EINVAL));
	} else if (!vdev_writeable(vd)) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(EROFS));
	}
	mutex_enter(&vd->vdev_initialize_lock);
	spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);

	/*
	 * When we activate an initialize action we check to see
	 * if the vdev_initialize_thread is NULL. We do this instead
	 * of using the vdev_initialize_state since there might be
	 * a previous initialization process which has completed but
	 * the thread is not exited.
	 */
	if (cmd_type == POOL_INITIALIZE_START &&
	    (vd->vdev_initialize_thread != NULL ||
	    vd->vdev_top->vdev_removing || vd->vdev_top->vdev_rz_expanding)) {
		mutex_exit(&vd->vdev_initialize_lock);
		return (SET_ERROR(EBUSY));
	} else if (cmd_type == POOL_INITIALIZE_CANCEL &&
	    (vd->vdev_initialize_state != VDEV_INITIALIZE_ACTIVE &&
	    vd->vdev_initialize_state != VDEV_INITIALIZE_SUSPENDED)) {
		mutex_exit(&vd->vdev_initialize_lock);
		return (SET_ERROR(ESRCH));
	} else if (cmd_type == POOL_INITIALIZE_SUSPEND &&
	    vd->vdev_initialize_state != VDEV_INITIALIZE_ACTIVE) {
		mutex_exit(&vd->vdev_initialize_lock);
		return (SET_ERROR(ESRCH));
	} else if (cmd_type == POOL_INITIALIZE_UNINIT &&
	    vd->vdev_initialize_thread != NULL) {
		mutex_exit(&vd->vdev_initialize_lock);
		return (SET_ERROR(EBUSY));
	}

	switch (cmd_type) {
	case POOL_INITIALIZE_START:
		vdev_initialize(vd);
		break;
	case POOL_INITIALIZE_CANCEL:
		vdev_initialize_stop(vd, VDEV_INITIALIZE_CANCELED, vd_list);
		break;
	case POOL_INITIALIZE_SUSPEND:
		vdev_initialize_stop(vd, VDEV_INITIALIZE_SUSPENDED, vd_list);
		break;
	case POOL_INITIALIZE_UNINIT:
		vdev_uninitialize(vd);
		break;
	default:
		panic("invalid cmd_type %llu", (unsigned long long)cmd_type);
	}
	mutex_exit(&vd->vdev_initialize_lock);

	return (0);
}

int
spa_vdev_initialize(spa_t *spa, nvlist_t *nv, uint64_t cmd_type,
    nvlist_t *vdev_errlist)
{
	int total_errors = 0;
	list_t vd_list;

	list_create(&vd_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_initialize_node));

	/*
	 * We hold the namespace lock through the whole function
	 * to prevent any changes to the pool while we're starting or
	 * stopping initialization. The config and state locks are held so that
	 * we can properly assess the vdev state before we commit to
	 * the initializing operation.
	 */
	mutex_enter(&spa_namespace_lock);

	for (nvpair_t *pair = nvlist_next_nvpair(nv, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(nv, pair)) {
		uint64_t vdev_guid = fnvpair_value_uint64(pair);

		int error = spa_vdev_initialize_impl(spa, vdev_guid, cmd_type,
		    &vd_list);
		if (error != 0) {
			char guid_as_str[MAXNAMELEN];

			(void) snprintf(guid_as_str, sizeof (guid_as_str),
			    "%llu", (unsigned long long)vdev_guid);
			fnvlist_add_int64(vdev_errlist, guid_as_str, error);
			total_errors++;
		}
	}

	/* Wait for all initialize threads to stop. */
	vdev_initialize_stop_wait(spa, &vd_list);

	/* Sync out the initializing state */
	txg_wait_synced(spa->spa_dsl_pool, 0);
	mutex_exit(&spa_namespace_lock);

	list_destroy(&vd_list);

	return (total_errors);
}

static int
spa_vdev_trim_impl(spa_t *spa, uint64_t guid, uint64_t cmd_type,
    uint64_t rate, boolean_t partial, boolean_t secure, list_t *vd_list)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);

	/* Look up vdev and ensure it's a leaf. */
	vdev_t *vd = spa_lookup_by_guid(spa, guid, B_FALSE);
	if (vd == NULL || vd->vdev_detached) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(ENODEV));
	} else if (!vd->vdev_ops->vdev_op_leaf || !vdev_is_concrete(vd)) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(EINVAL));
	} else if (!vdev_writeable(vd)) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(EROFS));
	} else if (!vd->vdev_has_trim) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	} else if (secure && !vd->vdev_has_securetrim) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}
	mutex_enter(&vd->vdev_trim_lock);
	spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);

	/*
	 * When we activate a TRIM action we check to see if the
	 * vdev_trim_thread is NULL. We do this instead of using the
	 * vdev_trim_state since there might be a previous TRIM process
	 * which has completed but the thread is not exited.
	 */
	if (cmd_type == POOL_TRIM_START &&
	    (vd->vdev_trim_thread != NULL || vd->vdev_top->vdev_removing ||
	    vd->vdev_top->vdev_rz_expanding)) {
		mutex_exit(&vd->vdev_trim_lock);
		return (SET_ERROR(EBUSY));
	} else if (cmd_type == POOL_TRIM_CANCEL &&
	    (vd->vdev_trim_state != VDEV_TRIM_ACTIVE &&
	    vd->vdev_trim_state != VDEV_TRIM_SUSPENDED)) {
		mutex_exit(&vd->vdev_trim_lock);
		return (SET_ERROR(ESRCH));
	} else if (cmd_type == POOL_TRIM_SUSPEND &&
	    vd->vdev_trim_state != VDEV_TRIM_ACTIVE) {
		mutex_exit(&vd->vdev_trim_lock);
		return (SET_ERROR(ESRCH));
	}

	switch (cmd_type) {
	case POOL_TRIM_START:
		vdev_trim(vd, rate, partial, secure);
		break;
	case POOL_TRIM_CANCEL:
		vdev_trim_stop(vd, VDEV_TRIM_CANCELED, vd_list);
		break;
	case POOL_TRIM_SUSPEND:
		vdev_trim_stop(vd, VDEV_TRIM_SUSPENDED, vd_list);
		break;
	default:
		panic("invalid cmd_type %llu", (unsigned long long)cmd_type);
	}
	mutex_exit(&vd->vdev_trim_lock);

	return (0);
}

/*
 * Initiates a manual TRIM for the requested vdevs. This kicks off individual
 * TRIM threads for each child vdev.  These threads pass over all of the free
 * space in the vdev's metaslabs and issues TRIM commands for that space.
 */
int
spa_vdev_trim(spa_t *spa, nvlist_t *nv, uint64_t cmd_type, uint64_t rate,
    boolean_t partial, boolean_t secure, nvlist_t *vdev_errlist)
{
	int total_errors = 0;
	list_t vd_list;

	list_create(&vd_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_trim_node));

	/*
	 * We hold the namespace lock through the whole function
	 * to prevent any changes to the pool while we're starting or
	 * stopping TRIM. The config and state locks are held so that
	 * we can properly assess the vdev state before we commit to
	 * the TRIM operation.
	 */
	mutex_enter(&spa_namespace_lock);

	for (nvpair_t *pair = nvlist_next_nvpair(nv, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(nv, pair)) {
		uint64_t vdev_guid = fnvpair_value_uint64(pair);

		int error = spa_vdev_trim_impl(spa, vdev_guid, cmd_type,
		    rate, partial, secure, &vd_list);
		if (error != 0) {
			char guid_as_str[MAXNAMELEN];

			(void) snprintf(guid_as_str, sizeof (guid_as_str),
			    "%llu", (unsigned long long)vdev_guid);
			fnvlist_add_int64(vdev_errlist, guid_as_str, error);
			total_errors++;
		}
	}

	/* Wait for all TRIM threads to stop. */
	vdev_trim_stop_wait(spa, &vd_list);

	/* Sync out the TRIM state */
	txg_wait_synced(spa->spa_dsl_pool, 0);
	mutex_exit(&spa_namespace_lock);

	list_destroy(&vd_list);

	return (total_errors);
}

/*
 * Split a set of devices from their mirrors, and create a new pool from them.
 */
int
spa_vdev_split_mirror(spa_t *spa, const char *newname, nvlist_t *config,
    nvlist_t *props, boolean_t exp)
{
	int error = 0;
	uint64_t txg, *glist;
	spa_t *newspa;
	uint_t c, children, lastlog;
	nvlist_t **child, *nvl, *tmp;
	dmu_tx_t *tx;
	const char *altroot = NULL;
	vdev_t *rvd, **vml = NULL;			/* vdev modify list */
	boolean_t activate_slog;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	/* clear the log and flush everything up to now */
	activate_slog = spa_passivate_log(spa);
	(void) spa_vdev_config_exit(spa, NULL, txg, 0, FTAG);
	error = spa_reset_logs(spa);
	txg = spa_vdev_config_enter(spa);

	if (activate_slog)
		spa_activate_log(spa);

	if (error != 0)
		return (spa_vdev_exit(spa, NULL, txg, error));

	/* check new spa name before going any further */
	if (spa_lookup(newname) != NULL)
		return (spa_vdev_exit(spa, NULL, txg, EEXIST));

	/*
	 * scan through all the children to ensure they're all mirrors
	 */
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvl) != 0 ||
	    nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	/* first, check to ensure we've got the right child count */
	rvd = spa->spa_root_vdev;
	lastlog = 0;
	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];

		/* don't count the holes & logs as children */
		if (vd->vdev_islog || (vd->vdev_ops != &vdev_indirect_ops &&
		    !vdev_is_concrete(vd))) {
			if (lastlog == 0)
				lastlog = c;
			continue;
		}

		lastlog = 0;
	}
	if (children != (lastlog != 0 ? lastlog : rvd->vdev_children))
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	/* next, ensure no spare or cache devices are part of the split */
	if (nvlist_lookup_nvlist(nvl, ZPOOL_CONFIG_SPARES, &tmp) == 0 ||
	    nvlist_lookup_nvlist(nvl, ZPOOL_CONFIG_L2CACHE, &tmp) == 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	vml = kmem_zalloc(children * sizeof (vdev_t *), KM_SLEEP);
	glist = kmem_zalloc(children * sizeof (uint64_t), KM_SLEEP);

	/* then, loop over each vdev and validate it */
	for (c = 0; c < children; c++) {
		uint64_t is_hole = 0;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &is_hole);

		if (is_hole != 0) {
			if (spa->spa_root_vdev->vdev_child[c]->vdev_ishole ||
			    spa->spa_root_vdev->vdev_child[c]->vdev_islog) {
				continue;
			} else {
				error = SET_ERROR(EINVAL);
				break;
			}
		}

		/* deal with indirect vdevs */
		if (spa->spa_root_vdev->vdev_child[c]->vdev_ops ==
		    &vdev_indirect_ops)
			continue;

		/* which disk is going to be split? */
		if (nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_GUID,
		    &glist[c]) != 0) {
			error = SET_ERROR(EINVAL);
			break;
		}

		/* look it up in the spa */
		vml[c] = spa_lookup_by_guid(spa, glist[c], B_FALSE);
		if (vml[c] == NULL) {
			error = SET_ERROR(ENODEV);
			break;
		}

		/* make sure there's nothing stopping the split */
		if (vml[c]->vdev_parent->vdev_ops != &vdev_mirror_ops ||
		    vml[c]->vdev_islog ||
		    !vdev_is_concrete(vml[c]) ||
		    vml[c]->vdev_isspare ||
		    vml[c]->vdev_isl2cache ||
		    !vdev_writeable(vml[c]) ||
		    vml[c]->vdev_children != 0 ||
		    vml[c]->vdev_state != VDEV_STATE_HEALTHY ||
		    c != spa->spa_root_vdev->vdev_child[c]->vdev_id) {
			error = SET_ERROR(EINVAL);
			break;
		}

		if (vdev_dtl_required(vml[c]) ||
		    vdev_resilver_needed(vml[c], NULL, NULL)) {
			error = SET_ERROR(EBUSY);
			break;
		}

		/* we need certain info from the top level */
		fnvlist_add_uint64(child[c], ZPOOL_CONFIG_METASLAB_ARRAY,
		    vml[c]->vdev_top->vdev_ms_array);
		fnvlist_add_uint64(child[c], ZPOOL_CONFIG_METASLAB_SHIFT,
		    vml[c]->vdev_top->vdev_ms_shift);
		fnvlist_add_uint64(child[c], ZPOOL_CONFIG_ASIZE,
		    vml[c]->vdev_top->vdev_asize);
		fnvlist_add_uint64(child[c], ZPOOL_CONFIG_ASHIFT,
		    vml[c]->vdev_top->vdev_ashift);

		/* transfer per-vdev ZAPs */
		ASSERT3U(vml[c]->vdev_leaf_zap, !=, 0);
		VERIFY0(nvlist_add_uint64(child[c],
		    ZPOOL_CONFIG_VDEV_LEAF_ZAP, vml[c]->vdev_leaf_zap));

		ASSERT3U(vml[c]->vdev_top->vdev_top_zap, !=, 0);
		VERIFY0(nvlist_add_uint64(child[c],
		    ZPOOL_CONFIG_VDEV_TOP_ZAP,
		    vml[c]->vdev_parent->vdev_top_zap));
	}

	if (error != 0) {
		kmem_free(vml, children * sizeof (vdev_t *));
		kmem_free(glist, children * sizeof (uint64_t));
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	/* stop writers from using the disks */
	for (c = 0; c < children; c++) {
		if (vml[c] != NULL)
			vml[c]->vdev_offline = B_TRUE;
	}
	vdev_reopen(spa->spa_root_vdev);

	/*
	 * Temporarily record the splitting vdevs in the spa config.  This
	 * will disappear once the config is regenerated.
	 */
	nvl = fnvlist_alloc();
	fnvlist_add_uint64_array(nvl, ZPOOL_CONFIG_SPLIT_LIST, glist, children);
	kmem_free(glist, children * sizeof (uint64_t));

	mutex_enter(&spa->spa_props_lock);
	fnvlist_add_nvlist(spa->spa_config, ZPOOL_CONFIG_SPLIT, nvl);
	mutex_exit(&spa->spa_props_lock);
	spa->spa_config_splitting = nvl;
	vdev_config_dirty(spa->spa_root_vdev);

	/* configure and create the new pool */
	fnvlist_add_string(config, ZPOOL_CONFIG_POOL_NAME, newname);
	fnvlist_add_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    exp ? POOL_STATE_EXPORTED : POOL_STATE_ACTIVE);
	fnvlist_add_uint64(config, ZPOOL_CONFIG_VERSION, spa_version(spa));
	fnvlist_add_uint64(config, ZPOOL_CONFIG_POOL_TXG, spa->spa_config_txg);
	fnvlist_add_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    spa_generate_guid(NULL));
	VERIFY0(nvlist_add_boolean(config, ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS));
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);

	/* add the new pool to the namespace */
	newspa = spa_add(newname, config, altroot);
	newspa->spa_avz_action = AVZ_ACTION_REBUILD;
	newspa->spa_config_txg = spa->spa_config_txg;
	spa_set_log_state(newspa, SPA_LOG_CLEAR);

	/* release the spa config lock, retaining the namespace lock */
	spa_vdev_config_exit(spa, NULL, txg, 0, FTAG);

	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, FTAG, 1);

	spa_activate(newspa, spa_mode_global);
	spa_async_suspend(newspa);

	/*
	 * Temporarily stop the initializing and TRIM activity.  We set the
	 * state to ACTIVE so that we know to resume initializing or TRIM
	 * once the split has completed.
	 */
	list_t vd_initialize_list;
	list_create(&vd_initialize_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_initialize_node));

	list_t vd_trim_list;
	list_create(&vd_trim_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_trim_node));

	for (c = 0; c < children; c++) {
		if (vml[c] != NULL && vml[c]->vdev_ops != &vdev_indirect_ops) {
			mutex_enter(&vml[c]->vdev_initialize_lock);
			vdev_initialize_stop(vml[c],
			    VDEV_INITIALIZE_ACTIVE, &vd_initialize_list);
			mutex_exit(&vml[c]->vdev_initialize_lock);

			mutex_enter(&vml[c]->vdev_trim_lock);
			vdev_trim_stop(vml[c], VDEV_TRIM_ACTIVE, &vd_trim_list);
			mutex_exit(&vml[c]->vdev_trim_lock);
		}
	}

	vdev_initialize_stop_wait(spa, &vd_initialize_list);
	vdev_trim_stop_wait(spa, &vd_trim_list);

	list_destroy(&vd_initialize_list);
	list_destroy(&vd_trim_list);

	newspa->spa_config_source = SPA_CONFIG_SRC_SPLIT;
	newspa->spa_is_splitting = B_TRUE;

	/* create the new pool from the disks of the original pool */
	error = spa_load(newspa, SPA_LOAD_IMPORT, SPA_IMPORT_ASSEMBLE);
	if (error)
		goto out;

	/* if that worked, generate a real config for the new pool */
	if (newspa->spa_root_vdev != NULL) {
		newspa->spa_config_splitting = fnvlist_alloc();
		fnvlist_add_uint64(newspa->spa_config_splitting,
		    ZPOOL_CONFIG_SPLIT_GUID, spa_guid(spa));
		spa_config_set(newspa, spa_config_generate(newspa, NULL, -1ULL,
		    B_TRUE));
	}

	/* set the props */
	if (props != NULL) {
		spa_configfile_set(newspa, props, B_FALSE);
		error = spa_prop_set(newspa, props);
		if (error)
			goto out;
	}

	/* flush everything */
	txg = spa_vdev_config_enter(newspa);
	vdev_config_dirty(newspa->spa_root_vdev);
	(void) spa_vdev_config_exit(newspa, NULL, txg, 0, FTAG);

	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, FTAG, 2);

	spa_async_resume(newspa);

	/* finally, update the original pool's config */
	txg = spa_vdev_config_enter(spa);
	tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error != 0)
		dmu_tx_abort(tx);
	for (c = 0; c < children; c++) {
		if (vml[c] != NULL && vml[c]->vdev_ops != &vdev_indirect_ops) {
			vdev_t *tvd = vml[c]->vdev_top;

			/*
			 * Need to be sure the detachable VDEV is not
			 * on any *other* txg's DTL list to prevent it
			 * from being accessed after it's freed.
			 */
			for (int t = 0; t < TXG_SIZE; t++) {
				(void) txg_list_remove_this(
				    &tvd->vdev_dtl_list, vml[c], t);
			}

			vdev_split(vml[c]);
			if (error == 0)
				spa_history_log_internal(spa, "detach", tx,
				    "vdev=%s", vml[c]->vdev_path);

			vdev_free(vml[c]);
		}
	}
	spa->spa_avz_action = AVZ_ACTION_REBUILD;
	vdev_config_dirty(spa->spa_root_vdev);
	spa->spa_config_splitting = NULL;
	nvlist_free(nvl);
	if (error == 0)
		dmu_tx_commit(tx);
	(void) spa_vdev_exit(spa, NULL, txg, 0);

	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, FTAG, 3);

	/* split is complete; log a history record */
	spa_history_log_internal(newspa, "split", NULL,
	    "from pool %s", spa_name(spa));

	newspa->spa_is_splitting = B_FALSE;
	kmem_free(vml, children * sizeof (vdev_t *));

	/* if we're not going to mount the filesystems in userland, export */
	if (exp)
		error = spa_export_common(newname, POOL_STATE_EXPORTED, NULL,
		    B_FALSE, B_FALSE);

	return (error);

out:
	spa_unload(newspa);
	spa_deactivate(newspa);
	spa_remove(newspa);

	txg = spa_vdev_config_enter(spa);

	/* re-online all offlined disks */
	for (c = 0; c < children; c++) {
		if (vml[c] != NULL)
			vml[c]->vdev_offline = B_FALSE;
	}

	/* restart initializing or trimming disks as necessary */
	spa_async_request(spa, SPA_ASYNC_INITIALIZE_RESTART);
	spa_async_request(spa, SPA_ASYNC_TRIM_RESTART);
	spa_async_request(spa, SPA_ASYNC_AUTOTRIM_RESTART);

	vdev_reopen(spa->spa_root_vdev);

	nvlist_free(spa->spa_config_splitting);
	spa->spa_config_splitting = NULL;
	(void) spa_vdev_exit(spa, NULL, txg, error);

	kmem_free(vml, children * sizeof (vdev_t *));
	return (error);
}

/*
 * Find any device that's done replacing, or a vdev marked 'unspare' that's
 * currently spared, so we can detach it.
 */
static vdev_t *
spa_vdev_resilver_done_hunt(vdev_t *vd)
{
	vdev_t *newvd, *oldvd;

	for (int c = 0; c < vd->vdev_children; c++) {
		oldvd = spa_vdev_resilver_done_hunt(vd->vdev_child[c]);
		if (oldvd != NULL)
			return (oldvd);
	}

	/*
	 * Check for a completed replacement.  We always consider the first
	 * vdev in the list to be the oldest vdev, and the last one to be
	 * the newest (see spa_vdev_attach() for how that works).  In
	 * the case where the newest vdev is faulted, we will not automatically
	 * remove it after a resilver completes.  This is OK as it will require
	 * user intervention to determine which disk the admin wishes to keep.
	 */
	if (vd->vdev_ops == &vdev_replacing_ops) {
		ASSERT(vd->vdev_children > 1);

		newvd = vd->vdev_child[vd->vdev_children - 1];
		oldvd = vd->vdev_child[0];

		if (vdev_dtl_empty(newvd, DTL_MISSING) &&
		    vdev_dtl_empty(newvd, DTL_OUTAGE) &&
		    !vdev_dtl_required(oldvd))
			return (oldvd);
	}

	/*
	 * Check for a completed resilver with the 'unspare' flag set.
	 * Also potentially update faulted state.
	 */
	if (vd->vdev_ops == &vdev_spare_ops) {
		vdev_t *first = vd->vdev_child[0];
		vdev_t *last = vd->vdev_child[vd->vdev_children - 1];

		if (last->vdev_unspare) {
			oldvd = first;
			newvd = last;
		} else if (first->vdev_unspare) {
			oldvd = last;
			newvd = first;
		} else {
			oldvd = NULL;
		}

		if (oldvd != NULL &&
		    vdev_dtl_empty(newvd, DTL_MISSING) &&
		    vdev_dtl_empty(newvd, DTL_OUTAGE) &&
		    !vdev_dtl_required(oldvd))
			return (oldvd);

		vdev_propagate_state(vd);

		/*
		 * If there are more than two spares attached to a disk,
		 * and those spares are not required, then we want to
		 * attempt to free them up now so that they can be used
		 * by other pools.  Once we're back down to a single
		 * disk+spare, we stop removing them.
		 */
		if (vd->vdev_children > 2) {
			newvd = vd->vdev_child[1];

			if (newvd->vdev_isspare && last->vdev_isspare &&
			    vdev_dtl_empty(last, DTL_MISSING) &&
			    vdev_dtl_empty(last, DTL_OUTAGE) &&
			    !vdev_dtl_required(newvd))
				return (newvd);
		}
	}

	return (NULL);
}

static void
spa_vdev_resilver_done(spa_t *spa)
{
	vdev_t *vd, *pvd, *ppvd;
	uint64_t guid, sguid, pguid, ppguid;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	while ((vd = spa_vdev_resilver_done_hunt(spa->spa_root_vdev)) != NULL) {
		pvd = vd->vdev_parent;
		ppvd = pvd->vdev_parent;
		guid = vd->vdev_guid;
		pguid = pvd->vdev_guid;
		ppguid = ppvd->vdev_guid;
		sguid = 0;
		/*
		 * If we have just finished replacing a hot spared device, then
		 * we need to detach the parent's first child (the original hot
		 * spare) as well.
		 */
		if (ppvd->vdev_ops == &vdev_spare_ops && pvd->vdev_id == 0 &&
		    ppvd->vdev_children == 2) {
			ASSERT(pvd->vdev_ops == &vdev_replacing_ops);
			sguid = ppvd->vdev_child[1]->vdev_guid;
		}
		ASSERT(vd->vdev_resilver_txg == 0 || !vdev_dtl_required(vd));

		spa_config_exit(spa, SCL_ALL, FTAG);
		if (spa_vdev_detach(spa, guid, pguid, B_TRUE) != 0)
			return;
		if (sguid && spa_vdev_detach(spa, sguid, ppguid, B_TRUE) != 0)
			return;
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	}

	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * If a detach was not performed above replace waiters will not have
	 * been notified.  In which case we must do so now.
	 */
	spa_notify_waiters(spa);
}

/*
 * Update the stored path or FRU for this vdev.
 */
static int
spa_vdev_set_common(spa_t *spa, uint64_t guid, const char *value,
    boolean_t ispath)
{
	vdev_t *vd;
	boolean_t sync = B_FALSE;

	ASSERT(spa_writeable(spa));

	spa_vdev_state_enter(spa, SCL_ALL);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, ENOENT));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	if (ispath) {
		if (strcmp(value, vd->vdev_path) != 0) {
			spa_strfree(vd->vdev_path);
			vd->vdev_path = spa_strdup(value);
			sync = B_TRUE;
		}
	} else {
		if (vd->vdev_fru == NULL) {
			vd->vdev_fru = spa_strdup(value);
			sync = B_TRUE;
		} else if (strcmp(value, vd->vdev_fru) != 0) {
			spa_strfree(vd->vdev_fru);
			vd->vdev_fru = spa_strdup(value);
			sync = B_TRUE;
		}
	}

	return (spa_vdev_state_exit(spa, sync ? vd : NULL, 0));
}

int
spa_vdev_setpath(spa_t *spa, uint64_t guid, const char *newpath)
{
	return (spa_vdev_set_common(spa, guid, newpath, B_TRUE));
}

int
spa_vdev_setfru(spa_t *spa, uint64_t guid, const char *newfru)
{
	return (spa_vdev_set_common(spa, guid, newfru, B_FALSE));
}

/*
 * ==========================================================================
 * SPA Scanning
 * ==========================================================================
 */
int
spa_scrub_pause_resume(spa_t *spa, pool_scrub_cmd_t cmd)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);

	if (dsl_scan_resilvering(spa->spa_dsl_pool))
		return (SET_ERROR(EBUSY));

	return (dsl_scrub_set_pause_resume(spa->spa_dsl_pool, cmd));
}

int
spa_scan_stop(spa_t *spa)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);
	if (dsl_scan_resilvering(spa->spa_dsl_pool))
		return (SET_ERROR(EBUSY));

	return (dsl_scan_cancel(spa->spa_dsl_pool));
}

int
spa_scan(spa_t *spa, pool_scan_func_t func)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);

	if (func >= POOL_SCAN_FUNCS || func == POOL_SCAN_NONE)
		return (SET_ERROR(ENOTSUP));

	if (func == POOL_SCAN_RESILVER &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_RESILVER_DEFER))
		return (SET_ERROR(ENOTSUP));

	/*
	 * If a resilver was requested, but there is no DTL on a
	 * writeable leaf device, we have nothing to do.
	 */
	if (func == POOL_SCAN_RESILVER &&
	    !vdev_resilver_needed(spa->spa_root_vdev, NULL, NULL)) {
		spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);
		return (0);
	}

	if (func == POOL_SCAN_ERRORSCRUB &&
	    !spa_feature_is_enabled(spa, SPA_FEATURE_HEAD_ERRLOG))
		return (SET_ERROR(ENOTSUP));

	return (dsl_scan(spa->spa_dsl_pool, func));
}

/*
 * ==========================================================================
 * SPA async task processing
 * ==========================================================================
 */

static void
spa_async_remove(spa_t *spa, vdev_t *vd)
{
	if (vd->vdev_remove_wanted) {
		vd->vdev_remove_wanted = B_FALSE;
		vd->vdev_delayed_close = B_FALSE;
		vdev_set_state(vd, B_FALSE, VDEV_STATE_REMOVED, VDEV_AUX_NONE);

		/*
		 * We want to clear the stats, but we don't want to do a full
		 * vdev_clear() as that will cause us to throw away
		 * degraded/faulted state as well as attempt to reopen the
		 * device, all of which is a waste.
		 */
		vd->vdev_stat.vs_read_errors = 0;
		vd->vdev_stat.vs_write_errors = 0;
		vd->vdev_stat.vs_checksum_errors = 0;

		vdev_state_dirty(vd->vdev_top);

		/* Tell userspace that the vdev is gone. */
		zfs_post_remove(spa, vd);
	}

	for (int c = 0; c < vd->vdev_children; c++)
		spa_async_remove(spa, vd->vdev_child[c]);
}

static void
spa_async_probe(spa_t *spa, vdev_t *vd)
{
	if (vd->vdev_probe_wanted) {
		vd->vdev_probe_wanted = B_FALSE;
		vdev_reopen(vd);	/* vdev_open() does the actual probe */
	}

	for (int c = 0; c < vd->vdev_children; c++)
		spa_async_probe(spa, vd->vdev_child[c]);
}

static void
spa_async_autoexpand(spa_t *spa, vdev_t *vd)
{
	if (!spa->spa_autoexpand)
		return;

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		spa_async_autoexpand(spa, cvd);
	}

	if (!vd->vdev_ops->vdev_op_leaf || vd->vdev_physpath == NULL)
		return;

	spa_event_notify(vd->vdev_spa, vd, NULL, ESC_ZFS_VDEV_AUTOEXPAND);
}

static __attribute__((noreturn)) void
spa_async_thread(void *arg)
{
	spa_t *spa = (spa_t *)arg;
	dsl_pool_t *dp = spa->spa_dsl_pool;
	int tasks;

	ASSERT(spa->spa_sync_on);

	mutex_enter(&spa->spa_async_lock);
	tasks = spa->spa_async_tasks;
	spa->spa_async_tasks = 0;
	mutex_exit(&spa->spa_async_lock);

	/*
	 * See if the config needs to be updated.
	 */
	if (tasks & SPA_ASYNC_CONFIG_UPDATE) {
		uint64_t old_space, new_space;

		mutex_enter(&spa_namespace_lock);
		old_space = metaslab_class_get_space(spa_normal_class(spa));
		old_space += metaslab_class_get_space(spa_special_class(spa));
		old_space += metaslab_class_get_space(spa_dedup_class(spa));
		old_space += metaslab_class_get_space(
		    spa_embedded_log_class(spa));

		spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);

		new_space = metaslab_class_get_space(spa_normal_class(spa));
		new_space += metaslab_class_get_space(spa_special_class(spa));
		new_space += metaslab_class_get_space(spa_dedup_class(spa));
		new_space += metaslab_class_get_space(
		    spa_embedded_log_class(spa));
		mutex_exit(&spa_namespace_lock);

		/*
		 * If the pool grew as a result of the config update,
		 * then log an internal history event.
		 */
		if (new_space != old_space) {
			spa_history_log_internal(spa, "vdev online", NULL,
			    "pool '%s' size: %llu(+%llu)",
			    spa_name(spa), (u_longlong_t)new_space,
			    (u_longlong_t)(new_space - old_space));
		}
	}

	/*
	 * See if any devices need to be marked REMOVED.
	 */
	if (tasks & SPA_ASYNC_REMOVE) {
		spa_vdev_state_enter(spa, SCL_NONE);
		spa_async_remove(spa, spa->spa_root_vdev);
		for (int i = 0; i < spa->spa_l2cache.sav_count; i++)
			spa_async_remove(spa, spa->spa_l2cache.sav_vdevs[i]);
		for (int i = 0; i < spa->spa_spares.sav_count; i++)
			spa_async_remove(spa, spa->spa_spares.sav_vdevs[i]);
		(void) spa_vdev_state_exit(spa, NULL, 0);
	}

	if ((tasks & SPA_ASYNC_AUTOEXPAND) && !spa_suspended(spa)) {
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		spa_async_autoexpand(spa, spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	/*
	 * See if any devices need to be probed.
	 */
	if (tasks & SPA_ASYNC_PROBE) {
		spa_vdev_state_enter(spa, SCL_NONE);
		spa_async_probe(spa, spa->spa_root_vdev);
		(void) spa_vdev_state_exit(spa, NULL, 0);
	}

	/*
	 * If any devices are done replacing, detach them.
	 */
	if (tasks & SPA_ASYNC_RESILVER_DONE ||
	    tasks & SPA_ASYNC_REBUILD_DONE ||
	    tasks & SPA_ASYNC_DETACH_SPARE) {
		spa_vdev_resilver_done(spa);
	}

	/*
	 * Kick off a resilver.
	 */
	if (tasks & SPA_ASYNC_RESILVER &&
	    !vdev_rebuild_active(spa->spa_root_vdev) &&
	    (!dsl_scan_resilvering(dp) ||
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_RESILVER_DEFER)))
		dsl_scan_restart_resilver(dp, 0);

	if (tasks & SPA_ASYNC_INITIALIZE_RESTART) {
		mutex_enter(&spa_namespace_lock);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		vdev_initialize_restart(spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		mutex_exit(&spa_namespace_lock);
	}

	if (tasks & SPA_ASYNC_TRIM_RESTART) {
		mutex_enter(&spa_namespace_lock);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		vdev_trim_restart(spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		mutex_exit(&spa_namespace_lock);
	}

	if (tasks & SPA_ASYNC_AUTOTRIM_RESTART) {
		mutex_enter(&spa_namespace_lock);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		vdev_autotrim_restart(spa);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		mutex_exit(&spa_namespace_lock);
	}

	/*
	 * Kick off L2 cache whole device TRIM.
	 */
	if (tasks & SPA_ASYNC_L2CACHE_TRIM) {
		mutex_enter(&spa_namespace_lock);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		vdev_trim_l2arc(spa);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		mutex_exit(&spa_namespace_lock);
	}

	/*
	 * Kick off L2 cache rebuilding.
	 */
	if (tasks & SPA_ASYNC_L2CACHE_REBUILD) {
		mutex_enter(&spa_namespace_lock);
		spa_config_enter(spa, SCL_L2ARC, FTAG, RW_READER);
		l2arc_spa_rebuild_start(spa);
		spa_config_exit(spa, SCL_L2ARC, FTAG);
		mutex_exit(&spa_namespace_lock);
	}

	/*
	 * Let the world know that we're done.
	 */
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_thread = NULL;
	cv_broadcast(&spa->spa_async_cv);
	mutex_exit(&spa->spa_async_lock);
	thread_exit();
}

void
spa_async_suspend(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_suspended++;
	while (spa->spa_async_thread != NULL)
		cv_wait(&spa->spa_async_cv, &spa->spa_async_lock);
	mutex_exit(&spa->spa_async_lock);

	spa_vdev_remove_suspend(spa);

	zthr_t *condense_thread = spa->spa_condense_zthr;
	if (condense_thread != NULL)
		zthr_cancel(condense_thread);

	zthr_t *raidz_expand_thread = spa->spa_raidz_expand_zthr;
	if (raidz_expand_thread != NULL)
		zthr_cancel(raidz_expand_thread);

	zthr_t *discard_thread = spa->spa_checkpoint_discard_zthr;
	if (discard_thread != NULL)
		zthr_cancel(discard_thread);

	zthr_t *ll_delete_thread = spa->spa_livelist_delete_zthr;
	if (ll_delete_thread != NULL)
		zthr_cancel(ll_delete_thread);

	zthr_t *ll_condense_thread = spa->spa_livelist_condense_zthr;
	if (ll_condense_thread != NULL)
		zthr_cancel(ll_condense_thread);
}

void
spa_async_resume(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	ASSERT(spa->spa_async_suspended != 0);
	spa->spa_async_suspended--;
	mutex_exit(&spa->spa_async_lock);
	spa_restart_removal(spa);

	zthr_t *condense_thread = spa->spa_condense_zthr;
	if (condense_thread != NULL)
		zthr_resume(condense_thread);

	zthr_t *raidz_expand_thread = spa->spa_raidz_expand_zthr;
	if (raidz_expand_thread != NULL)
		zthr_resume(raidz_expand_thread);

	zthr_t *discard_thread = spa->spa_checkpoint_discard_zthr;
	if (discard_thread != NULL)
		zthr_resume(discard_thread);

	zthr_t *ll_delete_thread = spa->spa_livelist_delete_zthr;
	if (ll_delete_thread != NULL)
		zthr_resume(ll_delete_thread);

	zthr_t *ll_condense_thread = spa->spa_livelist_condense_zthr;
	if (ll_condense_thread != NULL)
		zthr_resume(ll_condense_thread);
}

static boolean_t
spa_async_tasks_pending(spa_t *spa)
{
	uint_t non_config_tasks;
	uint_t config_task;
	boolean_t config_task_suspended;

	non_config_tasks = spa->spa_async_tasks & ~SPA_ASYNC_CONFIG_UPDATE;
	config_task = spa->spa_async_tasks & SPA_ASYNC_CONFIG_UPDATE;
	if (spa->spa_ccw_fail_time == 0) {
		config_task_suspended = B_FALSE;
	} else {
		config_task_suspended =
		    (gethrtime() - spa->spa_ccw_fail_time) <
		    ((hrtime_t)zfs_ccw_retry_interval * NANOSEC);
	}

	return (non_config_tasks || (config_task && !config_task_suspended));
}

static void
spa_async_dispatch(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	if (spa_async_tasks_pending(spa) &&
	    !spa->spa_async_suspended &&
	    spa->spa_async_thread == NULL)
		spa->spa_async_thread = thread_create(NULL, 0,
		    spa_async_thread, spa, 0, &p0, TS_RUN, maxclsyspri);
	mutex_exit(&spa->spa_async_lock);
}

void
spa_async_request(spa_t *spa, int task)
{
	zfs_dbgmsg("spa=%s async request task=%u", spa->spa_name, task);
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_tasks |= task;
	mutex_exit(&spa->spa_async_lock);
}

int
spa_async_tasks(spa_t *spa)
{
	return (spa->spa_async_tasks);
}

/*
 * ==========================================================================
 * SPA syncing routines
 * ==========================================================================
 */


static int
bpobj_enqueue_cb(void *arg, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	bpobj_t *bpo = arg;
	bpobj_enqueue(bpo, bp, bp_freed, tx);
	return (0);
}

int
bpobj_enqueue_alloc_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	return (bpobj_enqueue_cb(arg, bp, B_FALSE, tx));
}

int
bpobj_enqueue_free_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	return (bpobj_enqueue_cb(arg, bp, B_TRUE, tx));
}

static int
spa_free_sync_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	zio_t *pio = arg;

	zio_nowait(zio_free_sync(pio, pio->io_spa, dmu_tx_get_txg(tx), bp,
	    pio->io_flags));
	return (0);
}

static int
bpobj_spa_free_sync_cb(void *arg, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	ASSERT(!bp_freed);
	return (spa_free_sync_cb(arg, bp, tx));
}

/*
 * Note: this simple function is not inlined to make it easier to dtrace the
 * amount of time spent syncing frees.
 */
static void
spa_sync_frees(spa_t *spa, bplist_t *bpl, dmu_tx_t *tx)
{
	zio_t *zio = zio_root(spa, NULL, NULL, 0);
	bplist_iterate(bpl, spa_free_sync_cb, zio, tx);
	VERIFY(zio_wait(zio) == 0);
}

/*
 * Note: this simple function is not inlined to make it easier to dtrace the
 * amount of time spent syncing deferred frees.
 */
static void
spa_sync_deferred_frees(spa_t *spa, dmu_tx_t *tx)
{
	if (spa_sync_pass(spa) != 1)
		return;

	/*
	 * Note:
	 * If the log space map feature is active, we stop deferring
	 * frees to the next TXG and therefore running this function
	 * would be considered a no-op as spa_deferred_bpobj should
	 * not have any entries.
	 *
	 * That said we run this function anyway (instead of returning
	 * immediately) for the edge-case scenario where we just
	 * activated the log space map feature in this TXG but we have
	 * deferred frees from the previous TXG.
	 */
	zio_t *zio = zio_root(spa, NULL, NULL, 0);
	VERIFY3U(bpobj_iterate(&spa->spa_deferred_bpobj,
	    bpobj_spa_free_sync_cb, zio, tx), ==, 0);
	VERIFY0(zio_wait(zio));
}

static void
spa_sync_nvlist(spa_t *spa, uint64_t obj, nvlist_t *nv, dmu_tx_t *tx)
{
	char *packed = NULL;
	size_t bufsize;
	size_t nvsize = 0;
	dmu_buf_t *db;

	VERIFY(nvlist_size(nv, &nvsize, NV_ENCODE_XDR) == 0);

	/*
	 * Write full (SPA_CONFIG_BLOCKSIZE) blocks of configuration
	 * information.  This avoids the dmu_buf_will_dirty() path and
	 * saves us a pre-read to get data we don't actually care about.
	 */
	bufsize = P2ROUNDUP((uint64_t)nvsize, SPA_CONFIG_BLOCKSIZE);
	packed = vmem_alloc(bufsize, KM_SLEEP);

	VERIFY(nvlist_pack(nv, &packed, &nvsize, NV_ENCODE_XDR,
	    KM_SLEEP) == 0);
	memset(packed + nvsize, 0, bufsize - nvsize);

	dmu_write(spa->spa_meta_objset, obj, 0, bufsize, packed, tx);

	vmem_free(packed, bufsize);

	VERIFY(0 == dmu_bonus_hold(spa->spa_meta_objset, obj, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	*(uint64_t *)db->db_data = nvsize;
	dmu_buf_rele(db, FTAG);
}

static void
spa_sync_aux_dev(spa_t *spa, spa_aux_vdev_t *sav, dmu_tx_t *tx,
    const char *config, const char *entry)
{
	nvlist_t *nvroot;
	nvlist_t **list;
	int i;

	if (!sav->sav_sync)
		return;

	/*
	 * Update the MOS nvlist describing the list of available devices.
	 * spa_validate_aux() will have already made sure this nvlist is
	 * valid and the vdevs are labeled appropriately.
	 */
	if (sav->sav_object == 0) {
		sav->sav_object = dmu_object_alloc(spa->spa_meta_objset,
		    DMU_OT_PACKED_NVLIST, 1 << 14, DMU_OT_PACKED_NVLIST_SIZE,
		    sizeof (uint64_t), tx);
		VERIFY(zap_update(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, entry, sizeof (uint64_t), 1,
		    &sav->sav_object, tx) == 0);
	}

	nvroot = fnvlist_alloc();
	if (sav->sav_count == 0) {
		fnvlist_add_nvlist_array(nvroot, config,
		    (const nvlist_t * const *)NULL, 0);
	} else {
		list = kmem_alloc(sav->sav_count*sizeof (void *), KM_SLEEP);
		for (i = 0; i < sav->sav_count; i++)
			list[i] = vdev_config_generate(spa, sav->sav_vdevs[i],
			    B_FALSE, VDEV_CONFIG_L2CACHE);
		fnvlist_add_nvlist_array(nvroot, config,
		    (const nvlist_t * const *)list, sav->sav_count);
		for (i = 0; i < sav->sav_count; i++)
			nvlist_free(list[i]);
		kmem_free(list, sav->sav_count * sizeof (void *));
	}

	spa_sync_nvlist(spa, sav->sav_object, nvroot, tx);
	nvlist_free(nvroot);

	sav->sav_sync = B_FALSE;
}

/*
 * Rebuild spa's all-vdev ZAP from the vdev ZAPs indicated in each vdev_t.
 * The all-vdev ZAP must be empty.
 */
static void
spa_avz_build(vdev_t *vd, uint64_t avz, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;

	if (vd->vdev_root_zap != 0 &&
	    spa_feature_is_active(spa, SPA_FEATURE_AVZ_V2)) {
		VERIFY0(zap_add_int(spa->spa_meta_objset, avz,
		    vd->vdev_root_zap, tx));
	}
	if (vd->vdev_top_zap != 0) {
		VERIFY0(zap_add_int(spa->spa_meta_objset, avz,
		    vd->vdev_top_zap, tx));
	}
	if (vd->vdev_leaf_zap != 0) {
		VERIFY0(zap_add_int(spa->spa_meta_objset, avz,
		    vd->vdev_leaf_zap, tx));
	}
	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		spa_avz_build(vd->vdev_child[i], avz, tx);
	}
}

static void
spa_sync_config_object(spa_t *spa, dmu_tx_t *tx)
{
	nvlist_t *config;

	/*
	 * If the pool is being imported from a pre-per-vdev-ZAP version of ZFS,
	 * its config may not be dirty but we still need to build per-vdev ZAPs.
	 * Similarly, if the pool is being assembled (e.g. after a split), we
	 * need to rebuild the AVZ although the config may not be dirty.
	 */
	if (list_is_empty(&spa->spa_config_dirty_list) &&
	    spa->spa_avz_action == AVZ_ACTION_NONE)
		return;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	ASSERT(spa->spa_avz_action == AVZ_ACTION_NONE ||
	    spa->spa_avz_action == AVZ_ACTION_INITIALIZE ||
	    spa->spa_all_vdev_zaps != 0);

	if (spa->spa_avz_action == AVZ_ACTION_REBUILD) {
		/* Make and build the new AVZ */
		uint64_t new_avz = zap_create(spa->spa_meta_objset,
		    DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);
		spa_avz_build(spa->spa_root_vdev, new_avz, tx);

		/* Diff old AVZ with new one */
		zap_cursor_t zc;
		zap_attribute_t za;

		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			uint64_t vdzap = za.za_first_integer;
			if (zap_lookup_int(spa->spa_meta_objset, new_avz,
			    vdzap) == ENOENT) {
				/*
				 * ZAP is listed in old AVZ but not in new one;
				 * destroy it
				 */
				VERIFY0(zap_destroy(spa->spa_meta_objset, vdzap,
				    tx));
			}
		}

		zap_cursor_fini(&zc);

		/* Destroy the old AVZ */
		VERIFY0(zap_destroy(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, tx));

		/* Replace the old AVZ in the dir obj with the new one */
		VERIFY0(zap_update(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_VDEV_ZAP_MAP,
		    sizeof (new_avz), 1, &new_avz, tx));

		spa->spa_all_vdev_zaps = new_avz;
	} else if (spa->spa_avz_action == AVZ_ACTION_DESTROY) {
		zap_cursor_t zc;
		zap_attribute_t za;

		/* Walk through the AVZ and destroy all listed ZAPs */
		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			uint64_t zap = za.za_first_integer;
			VERIFY0(zap_destroy(spa->spa_meta_objset, zap, tx));
		}

		zap_cursor_fini(&zc);

		/* Destroy and unlink the AVZ itself */
		VERIFY0(zap_destroy(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, tx));
		VERIFY0(zap_remove(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_VDEV_ZAP_MAP, tx));
		spa->spa_all_vdev_zaps = 0;
	}

	if (spa->spa_all_vdev_zaps == 0) {
		spa->spa_all_vdev_zaps = zap_create_link(spa->spa_meta_objset,
		    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_VDEV_ZAP_MAP, tx);
	}
	spa->spa_avz_action = AVZ_ACTION_NONE;

	/* Create ZAPs for vdevs that don't have them. */
	vdev_construct_zaps(spa->spa_root_vdev, tx);

	config = spa_config_generate(spa, spa->spa_root_vdev,
	    dmu_tx_get_txg(tx), B_FALSE);

	/*
	 * If we're upgrading the spa version then make sure that
	 * the config object gets updated with the correct version.
	 */
	if (spa->spa_ubsync.ub_version < spa->spa_uberblock.ub_version)
		fnvlist_add_uint64(config, ZPOOL_CONFIG_VERSION,
		    spa->spa_uberblock.ub_version);

	spa_config_exit(spa, SCL_STATE, FTAG);

	nvlist_free(spa->spa_config_syncing);
	spa->spa_config_syncing = config;

	spa_sync_nvlist(spa, spa->spa_config_object, config, tx);
}

static void
spa_sync_version(void *arg, dmu_tx_t *tx)
{
	uint64_t *versionp = arg;
	uint64_t version = *versionp;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	/*
	 * Setting the version is special cased when first creating the pool.
	 */
	ASSERT(tx->tx_txg != TXG_INITIAL);

	ASSERT(SPA_VERSION_IS_SUPPORTED(version));
	ASSERT(version >= spa_version(spa));

	spa->spa_uberblock.ub_version = version;
	vdev_config_dirty(spa->spa_root_vdev);
	spa_history_log_internal(spa, "set", tx, "version=%lld",
	    (longlong_t)version);
}

/*
 * Set zpool properties.
 */
static void
spa_sync_props(void *arg, dmu_tx_t *tx)
{
	nvlist_t *nvp = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	objset_t *mos = spa->spa_meta_objset;
	nvpair_t *elem = NULL;

	mutex_enter(&spa->spa_props_lock);

	while ((elem = nvlist_next_nvpair(nvp, elem))) {
		uint64_t intval;
		const char *strval, *fname;
		zpool_prop_t prop;
		const char *propname;
		const char *elemname = nvpair_name(elem);
		zprop_type_t proptype;
		spa_feature_t fid;

		switch (prop = zpool_name_to_prop(elemname)) {
		case ZPOOL_PROP_VERSION:
			intval = fnvpair_value_uint64(elem);
			/*
			 * The version is synced separately before other
			 * properties and should be correct by now.
			 */
			ASSERT3U(spa_version(spa), >=, intval);
			break;

		case ZPOOL_PROP_ALTROOT:
			/*
			 * 'altroot' is a non-persistent property. It should
			 * have been set temporarily at creation or import time.
			 */
			ASSERT(spa->spa_root != NULL);
			break;

		case ZPOOL_PROP_READONLY:
		case ZPOOL_PROP_CACHEFILE:
			/*
			 * 'readonly' and 'cachefile' are also non-persistent
			 * properties.
			 */
			break;
		case ZPOOL_PROP_COMMENT:
			strval = fnvpair_value_string(elem);
			if (spa->spa_comment != NULL)
				spa_strfree(spa->spa_comment);
			spa->spa_comment = spa_strdup(strval);
			/*
			 * We need to dirty the configuration on all the vdevs
			 * so that their labels get updated.  We also need to
			 * update the cache file to keep it in sync with the
			 * MOS version. It's unnecessary to do this for pool
			 * creation since the vdev's configuration has already
			 * been dirtied.
			 */
			if (tx->tx_txg != TXG_INITIAL) {
				vdev_config_dirty(spa->spa_root_vdev);
				spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
			}
			spa_history_log_internal(spa, "set", tx,
			    "%s=%s", elemname, strval);
			break;
		case ZPOOL_PROP_COMPATIBILITY:
			strval = fnvpair_value_string(elem);
			if (spa->spa_compatibility != NULL)
				spa_strfree(spa->spa_compatibility);
			spa->spa_compatibility = spa_strdup(strval);
			/*
			 * Dirty the configuration on vdevs as above.
			 */
			if (tx->tx_txg != TXG_INITIAL) {
				vdev_config_dirty(spa->spa_root_vdev);
				spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
			}

			spa_history_log_internal(spa, "set", tx,
			    "%s=%s", nvpair_name(elem), strval);
			break;

		case ZPOOL_PROP_INVAL:
			if (zpool_prop_feature(elemname)) {
				fname = strchr(elemname, '@') + 1;
				VERIFY0(zfeature_lookup_name(fname, &fid));

				spa_feature_enable(spa, fid, tx);
				spa_history_log_internal(spa, "set", tx,
				    "%s=enabled", elemname);
				break;
			} else if (!zfs_prop_user(elemname)) {
				ASSERT(zpool_prop_feature(elemname));
				break;
			}
			zfs_fallthrough;
		default:
			/*
			 * Set pool property values in the poolprops mos object.
			 */
			if (spa->spa_pool_props_object == 0) {
				spa->spa_pool_props_object =
				    zap_create_link(mos, DMU_OT_POOL_PROPS,
				    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_PROPS,
				    tx);
			}

			/* normalize the property name */
			if (prop == ZPOOL_PROP_INVAL) {
				propname = elemname;
				proptype = PROP_TYPE_STRING;
			} else {
				propname = zpool_prop_to_name(prop);
				proptype = zpool_prop_get_type(prop);
			}

			if (nvpair_type(elem) == DATA_TYPE_STRING) {
				ASSERT(proptype == PROP_TYPE_STRING);
				strval = fnvpair_value_string(elem);
				VERIFY0(zap_update(mos,
				    spa->spa_pool_props_object, propname,
				    1, strlen(strval) + 1, strval, tx));
				spa_history_log_internal(spa, "set", tx,
				    "%s=%s", elemname, strval);
			} else if (nvpair_type(elem) == DATA_TYPE_UINT64) {
				intval = fnvpair_value_uint64(elem);

				if (proptype == PROP_TYPE_INDEX) {
					const char *unused;
					VERIFY0(zpool_prop_index_to_string(
					    prop, intval, &unused));
				}
				VERIFY0(zap_update(mos,
				    spa->spa_pool_props_object, propname,
				    8, 1, &intval, tx));
				spa_history_log_internal(spa, "set", tx,
				    "%s=%lld", elemname,
				    (longlong_t)intval);

				switch (prop) {
				case ZPOOL_PROP_DELEGATION:
					spa->spa_delegation = intval;
					break;
				case ZPOOL_PROP_BOOTFS:
					spa->spa_bootfs = intval;
					break;
				case ZPOOL_PROP_FAILUREMODE:
					spa->spa_failmode = intval;
					break;
				case ZPOOL_PROP_AUTOTRIM:
					spa->spa_autotrim = intval;
					spa_async_request(spa,
					    SPA_ASYNC_AUTOTRIM_RESTART);
					break;
				case ZPOOL_PROP_AUTOEXPAND:
					spa->spa_autoexpand = intval;
					if (tx->tx_txg != TXG_INITIAL)
						spa_async_request(spa,
						    SPA_ASYNC_AUTOEXPAND);
					break;
				case ZPOOL_PROP_MULTIHOST:
					spa->spa_multihost = intval;
					break;
				default:
					break;
				}
			} else {
				ASSERT(0); /* not allowed */
			}
		}

	}

	mutex_exit(&spa->spa_props_lock);
}

/*
 * Perform one-time upgrade on-disk changes.  spa_version() does not
 * reflect the new version this txg, so there must be no changes this
 * txg to anything that the upgrade code depends on after it executes.
 * Therefore this must be called after dsl_pool_sync() does the sync
 * tasks.
 */
static void
spa_sync_upgrades(spa_t *spa, dmu_tx_t *tx)
{
	if (spa_sync_pass(spa) != 1)
		return;

	dsl_pool_t *dp = spa->spa_dsl_pool;
	rrw_enter(&dp->dp_config_rwlock, RW_WRITER, FTAG);

	if (spa->spa_ubsync.ub_version < SPA_VERSION_ORIGIN &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_ORIGIN) {
		dsl_pool_create_origin(dp, tx);

		/* Keeping the origin open increases spa_minref */
		spa->spa_minref += 3;
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_NEXT_CLONES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_NEXT_CLONES) {
		dsl_pool_upgrade_clones(dp, tx);
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_DIR_CLONES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_DIR_CLONES) {
		dsl_pool_upgrade_dir_clones(dp, tx);

		/* Keeping the freedir open increases spa_minref */
		spa->spa_minref += 3;
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_FEATURES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_FEATURES) {
		spa_feature_create_zap_objects(spa, tx);
	}

	/*
	 * LZ4_COMPRESS feature's behaviour was changed to activate_on_enable
	 * when possibility to use lz4 compression for metadata was added
	 * Old pools that have this feature enabled must be upgraded to have
	 * this feature active
	 */
	if (spa->spa_uberblock.ub_version >= SPA_VERSION_FEATURES) {
		boolean_t lz4_en = spa_feature_is_enabled(spa,
		    SPA_FEATURE_LZ4_COMPRESS);
		boolean_t lz4_ac = spa_feature_is_active(spa,
		    SPA_FEATURE_LZ4_COMPRESS);

		if (lz4_en && !lz4_ac)
			spa_feature_incr(spa, SPA_FEATURE_LZ4_COMPRESS, tx);
	}

	/*
	 * If we haven't written the salt, do so now.  Note that the
	 * feature may not be activated yet, but that's fine since
	 * the presence of this ZAP entry is backwards compatible.
	 */
	if (zap_contains(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CHECKSUM_SALT) == ENOENT) {
		VERIFY0(zap_add(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CHECKSUM_SALT, 1,
		    sizeof (spa->spa_cksum_salt.zcs_bytes),
		    spa->spa_cksum_salt.zcs_bytes, tx));
	}

	rrw_exit(&dp->dp_config_rwlock, FTAG);
}

static void
vdev_indirect_state_sync_verify(vdev_t *vd)
{
	vdev_indirect_mapping_t *vim __maybe_unused = vd->vdev_indirect_mapping;
	vdev_indirect_births_t *vib __maybe_unused = vd->vdev_indirect_births;

	if (vd->vdev_ops == &vdev_indirect_ops) {
		ASSERT(vim != NULL);
		ASSERT(vib != NULL);
	}

	uint64_t obsolete_sm_object = 0;
	ASSERT0(vdev_obsolete_sm_object(vd, &obsolete_sm_object));
	if (obsolete_sm_object != 0) {
		ASSERT(vd->vdev_obsolete_sm != NULL);
		ASSERT(vd->vdev_removing ||
		    vd->vdev_ops == &vdev_indirect_ops);
		ASSERT(vdev_indirect_mapping_num_entries(vim) > 0);
		ASSERT(vdev_indirect_mapping_bytes_mapped(vim) > 0);
		ASSERT3U(obsolete_sm_object, ==,
		    space_map_object(vd->vdev_obsolete_sm));
		ASSERT3U(vdev_indirect_mapping_bytes_mapped(vim), >=,
		    space_map_allocated(vd->vdev_obsolete_sm));
	}
	ASSERT(vd->vdev_obsolete_segments != NULL);

	/*
	 * Since frees / remaps to an indirect vdev can only
	 * happen in syncing context, the obsolete segments
	 * tree must be empty when we start syncing.
	 */
	ASSERT0(range_tree_space(vd->vdev_obsolete_segments));
}

/*
 * Set the top-level vdev's max queue depth. Evaluate each top-level's
 * async write queue depth in case it changed. The max queue depth will
 * not change in the middle of syncing out this txg.
 */
static void
spa_sync_adjust_vdev_max_queue_depth(spa_t *spa)
{
	ASSERT(spa_writeable(spa));

	vdev_t *rvd = spa->spa_root_vdev;
	uint32_t max_queue_depth = zfs_vdev_async_write_max_active *
	    zfs_vdev_queue_depth_pct / 100;
	metaslab_class_t *normal = spa_normal_class(spa);
	metaslab_class_t *special = spa_special_class(spa);
	metaslab_class_t *dedup = spa_dedup_class(spa);

	uint64_t slots_per_allocator = 0;
	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		metaslab_group_t *mg = tvd->vdev_mg;
		if (mg == NULL || !metaslab_group_initialized(mg))
			continue;

		metaslab_class_t *mc = mg->mg_class;
		if (mc != normal && mc != special && mc != dedup)
			continue;

		/*
		 * It is safe to do a lock-free check here because only async
		 * allocations look at mg_max_alloc_queue_depth, and async
		 * allocations all happen from spa_sync().
		 */
		for (int i = 0; i < mg->mg_allocators; i++) {
			ASSERT0(zfs_refcount_count(
			    &(mg->mg_allocator[i].mga_alloc_queue_depth)));
		}
		mg->mg_max_alloc_queue_depth = max_queue_depth;

		for (int i = 0; i < mg->mg_allocators; i++) {
			mg->mg_allocator[i].mga_cur_max_alloc_queue_depth =
			    zfs_vdev_def_queue_depth;
		}
		slots_per_allocator += zfs_vdev_def_queue_depth;
	}

	for (int i = 0; i < spa->spa_alloc_count; i++) {
		ASSERT0(zfs_refcount_count(&normal->mc_allocator[i].
		    mca_alloc_slots));
		ASSERT0(zfs_refcount_count(&special->mc_allocator[i].
		    mca_alloc_slots));
		ASSERT0(zfs_refcount_count(&dedup->mc_allocator[i].
		    mca_alloc_slots));
		normal->mc_allocator[i].mca_alloc_max_slots =
		    slots_per_allocator;
		special->mc_allocator[i].mca_alloc_max_slots =
		    slots_per_allocator;
		dedup->mc_allocator[i].mca_alloc_max_slots =
		    slots_per_allocator;
	}
	normal->mc_alloc_throttle_enabled = zio_dva_throttle_enabled;
	special->mc_alloc_throttle_enabled = zio_dva_throttle_enabled;
	dedup->mc_alloc_throttle_enabled = zio_dva_throttle_enabled;
}

static void
spa_sync_condense_indirect(spa_t *spa, dmu_tx_t *tx)
{
	ASSERT(spa_writeable(spa));

	vdev_t *rvd = spa->spa_root_vdev;
	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];
		vdev_indirect_state_sync_verify(vd);

		if (vdev_indirect_should_condense(vd)) {
			spa_condense_indirect_start_sync(vd, tx);
			break;
		}
	}
}

static void
spa_sync_iterate_to_convergence(spa_t *spa, dmu_tx_t *tx)
{
	objset_t *mos = spa->spa_meta_objset;
	dsl_pool_t *dp = spa->spa_dsl_pool;
	uint64_t txg = tx->tx_txg;
	bplist_t *free_bpl = &spa->spa_free_bplist[txg & TXG_MASK];

	do {
		int pass = ++spa->spa_sync_pass;

		spa_sync_config_object(spa, tx);
		spa_sync_aux_dev(spa, &spa->spa_spares, tx,
		    ZPOOL_CONFIG_SPARES, DMU_POOL_SPARES);
		spa_sync_aux_dev(spa, &spa->spa_l2cache, tx,
		    ZPOOL_CONFIG_L2CACHE, DMU_POOL_L2CACHE);
		spa_errlog_sync(spa, txg);
		dsl_pool_sync(dp, txg);

		if (pass < zfs_sync_pass_deferred_free ||
		    spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP)) {
			/*
			 * If the log space map feature is active we don't
			 * care about deferred frees and the deferred bpobj
			 * as the log space map should effectively have the
			 * same results (i.e. appending only to one object).
			 */
			spa_sync_frees(spa, free_bpl, tx);
		} else {
			/*
			 * We can not defer frees in pass 1, because
			 * we sync the deferred frees later in pass 1.
			 */
			ASSERT3U(pass, >, 1);
			bplist_iterate(free_bpl, bpobj_enqueue_alloc_cb,
			    &spa->spa_deferred_bpobj, tx);
		}

		brt_sync(spa, txg);
		ddt_sync(spa, txg);
		dsl_scan_sync(dp, tx);
		dsl_errorscrub_sync(dp, tx);
		svr_sync(spa, tx);
		spa_sync_upgrades(spa, tx);

		spa_flush_metaslabs(spa, tx);

		vdev_t *vd = NULL;
		while ((vd = txg_list_remove(&spa->spa_vdev_txg_list, txg))
		    != NULL)
			vdev_sync(vd, txg);

		if (pass == 1) {
			/*
			 * dsl_pool_sync() -> dp_sync_tasks may have dirtied
			 * the config. If that happens, this txg should not
			 * be a no-op. So we must sync the config to the MOS
			 * before checking for no-op.
			 *
			 * Note that when the config is dirty, it will
			 * be written to the MOS (i.e. the MOS will be
			 * dirtied) every time we call spa_sync_config_object()
			 * in this txg.  Therefore we can't call this after
			 * dsl_pool_sync() every pass, because it would
			 * prevent us from converging, since we'd dirty
			 * the MOS every pass.
			 *
			 * Sync tasks can only be processed in pass 1, so
			 * there's no need to do this in later passes.
			 */
			spa_sync_config_object(spa, tx);
		}

		/*
		 * Note: We need to check if the MOS is dirty because we could
		 * have marked the MOS dirty without updating the uberblock
		 * (e.g. if we have sync tasks but no dirty user data). We need
		 * to check the uberblock's rootbp because it is updated if we
		 * have synced out dirty data (though in this case the MOS will
		 * most likely also be dirty due to second order effects, we
		 * don't want to rely on that here).
		 */
		if (pass == 1 &&
		    BP_GET_LOGICAL_BIRTH(&spa->spa_uberblock.ub_rootbp) < txg &&
		    !dmu_objset_is_dirty(mos, txg)) {
			/*
			 * Nothing changed on the first pass, therefore this
			 * TXG is a no-op. Avoid syncing deferred frees, so
			 * that we can keep this TXG as a no-op.
			 */
			ASSERT(txg_list_empty(&dp->dp_dirty_datasets, txg));
			ASSERT(txg_list_empty(&dp->dp_dirty_dirs, txg));
			ASSERT(txg_list_empty(&dp->dp_sync_tasks, txg));
			ASSERT(txg_list_empty(&dp->dp_early_sync_tasks, txg));
			break;
		}

		spa_sync_deferred_frees(spa, tx);
	} while (dmu_objset_is_dirty(mos, txg));
}

/*
 * Rewrite the vdev configuration (which includes the uberblock) to
 * commit the transaction group.
 *
 * If there are no dirty vdevs, we sync the uberblock to a few random
 * top-level vdevs that are known to be visible in the config cache
 * (see spa_vdev_add() for a complete description). If there *are* dirty
 * vdevs, sync the uberblock to all vdevs.
 */
static void
spa_sync_rewrite_vdev_config(spa_t *spa, dmu_tx_t *tx)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t txg = tx->tx_txg;

	for (;;) {
		int error = 0;

		/*
		 * We hold SCL_STATE to prevent vdev open/close/etc.
		 * while we're attempting to write the vdev labels.
		 */
		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

		if (list_is_empty(&spa->spa_config_dirty_list)) {
			vdev_t *svd[SPA_SYNC_MIN_VDEVS] = { NULL };
			int svdcount = 0;
			int children = rvd->vdev_children;
			int c0 = random_in_range(children);

			for (int c = 0; c < children; c++) {
				vdev_t *vd =
				    rvd->vdev_child[(c0 + c) % children];

				/* Stop when revisiting the first vdev */
				if (c > 0 && svd[0] == vd)
					break;

				if (vd->vdev_ms_array == 0 ||
				    vd->vdev_islog ||
				    !vdev_is_concrete(vd))
					continue;

				svd[svdcount++] = vd;
				if (svdcount == SPA_SYNC_MIN_VDEVS)
					break;
			}
			error = vdev_config_sync(svd, svdcount, txg);
		} else {
			error = vdev_config_sync(rvd->vdev_child,
			    rvd->vdev_children, txg);
		}

		if (error == 0)
			spa->spa_last_synced_guid = rvd->vdev_guid;

		spa_config_exit(spa, SCL_STATE, FTAG);

		if (error == 0)
			break;
		zio_suspend(spa, NULL, ZIO_SUSPEND_IOERR);
		zio_resume_wait(spa);
	}
}

/*
 * Sync the specified transaction group.  New blocks may be dirtied as
 * part of the process, so we iterate until it converges.
 */
void
spa_sync(spa_t *spa, uint64_t txg)
{
	vdev_t *vd = NULL;

	VERIFY(spa_writeable(spa));

	/*
	 * Wait for i/os issued in open context that need to complete
	 * before this txg syncs.
	 */
	(void) zio_wait(spa->spa_txg_zio[txg & TXG_MASK]);
	spa->spa_txg_zio[txg & TXG_MASK] = zio_root(spa, NULL, NULL,
	    ZIO_FLAG_CANFAIL);

	/*
	 * Now that there can be no more cloning in this transaction group,
	 * but we are still before issuing frees, we can process pending BRT
	 * updates.
	 */
	brt_pending_apply(spa, txg);

	/*
	 * Lock out configuration changes.
	 */
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	spa->spa_syncing_txg = txg;
	spa->spa_sync_pass = 0;

	for (int i = 0; i < spa->spa_alloc_count; i++) {
		mutex_enter(&spa->spa_allocs[i].spaa_lock);
		VERIFY0(avl_numnodes(&spa->spa_allocs[i].spaa_tree));
		mutex_exit(&spa->spa_allocs[i].spaa_lock);
	}

	/*
	 * If there are any pending vdev state changes, convert them
	 * into config changes that go out with this transaction group.
	 */
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	while ((vd = list_head(&spa->spa_state_dirty_list)) != NULL) {
		/* Avoid holding the write lock unless actually necessary */
		if (vd->vdev_aux == NULL) {
			vdev_state_clean(vd);
			vdev_config_dirty(vd);
			continue;
		}
		/*
		 * We need the write lock here because, for aux vdevs,
		 * calling vdev_config_dirty() modifies sav_config.
		 * This is ugly and will become unnecessary when we
		 * eliminate the aux vdev wart by integrating all vdevs
		 * into the root vdev tree.
		 */
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_WRITER);
		while ((vd = list_head(&spa->spa_state_dirty_list)) != NULL) {
			vdev_state_clean(vd);
			vdev_config_dirty(vd);
		}
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);
	}
	spa_config_exit(spa, SCL_STATE, FTAG);

	dsl_pool_t *dp = spa->spa_dsl_pool;
	dmu_tx_t *tx = dmu_tx_create_assigned(dp, txg);

	spa->spa_sync_starttime = gethrtime();
	taskq_cancel_id(system_delay_taskq, spa->spa_deadman_tqid);
	spa->spa_deadman_tqid = taskq_dispatch_delay(system_delay_taskq,
	    spa_deadman, spa, TQ_SLEEP, ddi_get_lbolt() +
	    NSEC_TO_TICK(spa->spa_deadman_synctime));

	/*
	 * If we are upgrading to SPA_VERSION_RAIDZ_DEFLATE this txg,
	 * set spa_deflate if we have no raid-z vdevs.
	 */
	if (spa->spa_ubsync.ub_version < SPA_VERSION_RAIDZ_DEFLATE &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_RAIDZ_DEFLATE) {
		vdev_t *rvd = spa->spa_root_vdev;

		int i;
		for (i = 0; i < rvd->vdev_children; i++) {
			vd = rvd->vdev_child[i];
			if (vd->vdev_deflate_ratio != SPA_MINBLOCKSIZE)
				break;
		}
		if (i == rvd->vdev_children) {
			spa->spa_deflate = TRUE;
			VERIFY0(zap_add(spa->spa_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
			    sizeof (uint64_t), 1, &spa->spa_deflate, tx));
		}
	}

	spa_sync_adjust_vdev_max_queue_depth(spa);

	spa_sync_condense_indirect(spa, tx);

	spa_sync_iterate_to_convergence(spa, tx);

#ifdef ZFS_DEBUG
	if (!list_is_empty(&spa->spa_config_dirty_list)) {
	/*
	 * Make sure that the number of ZAPs for all the vdevs matches
	 * the number of ZAPs in the per-vdev ZAP list. This only gets
	 * called if the config is dirty; otherwise there may be
	 * outstanding AVZ operations that weren't completed in
	 * spa_sync_config_object.
	 */
		uint64_t all_vdev_zap_entry_count;
		ASSERT0(zap_count(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, &all_vdev_zap_entry_count));
		ASSERT3U(vdev_count_verify_zaps(spa->spa_root_vdev), ==,
		    all_vdev_zap_entry_count);
	}
#endif

	if (spa->spa_vdev_removal != NULL) {
		ASSERT0(spa->spa_vdev_removal->svr_bytes_done[txg & TXG_MASK]);
	}

	spa_sync_rewrite_vdev_config(spa, tx);
	dmu_tx_commit(tx);

	taskq_cancel_id(system_delay_taskq, spa->spa_deadman_tqid);
	spa->spa_deadman_tqid = 0;

	/*
	 * Clear the dirty config list.
	 */
	while ((vd = list_head(&spa->spa_config_dirty_list)) != NULL)
		vdev_config_clean(vd);

	/*
	 * Now that the new config has synced transactionally,
	 * let it become visible to the config cache.
	 */
	if (spa->spa_config_syncing != NULL) {
		spa_config_set(spa, spa->spa_config_syncing);
		spa->spa_config_txg = txg;
		spa->spa_config_syncing = NULL;
	}

	dsl_pool_sync_done(dp, txg);

	for (int i = 0; i < spa->spa_alloc_count; i++) {
		mutex_enter(&spa->spa_allocs[i].spaa_lock);
		VERIFY0(avl_numnodes(&spa->spa_allocs[i].spaa_tree));
		mutex_exit(&spa->spa_allocs[i].spaa_lock);
	}

	/*
	 * Update usable space statistics.
	 */
	while ((vd = txg_list_remove(&spa->spa_vdev_txg_list, TXG_CLEAN(txg)))
	    != NULL)
		vdev_sync_done(vd, txg);

	metaslab_class_evict_old(spa->spa_normal_class, txg);
	metaslab_class_evict_old(spa->spa_log_class, txg);

	spa_sync_close_syncing_log_sm(spa);

	spa_update_dspace(spa);

	if (spa_get_autotrim(spa) == SPA_AUTOTRIM_ON)
		vdev_autotrim_kick(spa);

	/*
	 * It had better be the case that we didn't dirty anything
	 * since vdev_config_sync().
	 */
	ASSERT(txg_list_empty(&dp->dp_dirty_datasets, txg));
	ASSERT(txg_list_empty(&dp->dp_dirty_dirs, txg));
	ASSERT(txg_list_empty(&spa->spa_vdev_txg_list, txg));

	while (zfs_pause_spa_sync)
		delay(1);

	spa->spa_sync_pass = 0;

	/*
	 * Update the last synced uberblock here. We want to do this at
	 * the end of spa_sync() so that consumers of spa_last_synced_txg()
	 * will be guaranteed that all the processing associated with
	 * that txg has been completed.
	 */
	spa->spa_ubsync = spa->spa_uberblock;
	spa_config_exit(spa, SCL_CONFIG, FTAG);

	spa_handle_ignored_writes(spa);

	/*
	 * If any async tasks have been requested, kick them off.
	 */
	spa_async_dispatch(spa);
}

/*
 * Sync all pools.  We don't want to hold the namespace lock across these
 * operations, so we take a reference on the spa_t and drop the lock during the
 * sync.
 */
void
spa_sync_allpools(void)
{
	spa_t *spa = NULL;
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL) {
		if (spa_state(spa) != POOL_STATE_ACTIVE ||
		    !spa_writeable(spa) || spa_suspended(spa))
			continue;
		spa_open_ref(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		txg_wait_synced(spa_get_dsl(spa), 0);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);
	}
	mutex_exit(&spa_namespace_lock);
}

taskq_t *
spa_sync_tq_create(spa_t *spa, const char *name)
{
	kthread_t **kthreads;

	ASSERT(spa->spa_sync_tq == NULL);
	ASSERT3S(spa->spa_alloc_count, <=, boot_ncpus);

	/*
	 * - do not allow more allocators than cpus.
	 * - there may be more cpus than allocators.
	 * - do not allow more sync taskq threads than allocators or cpus.
	 */
	int nthreads = spa->spa_alloc_count;
	spa->spa_syncthreads = kmem_zalloc(sizeof (spa_syncthread_info_t) *
	    nthreads, KM_SLEEP);

	spa->spa_sync_tq = taskq_create_synced(name, nthreads, minclsyspri,
	    nthreads, INT_MAX, TASKQ_PREPOPULATE, &kthreads);
	VERIFY(spa->spa_sync_tq != NULL);
	VERIFY(kthreads != NULL);

	spa_taskqs_t *tqs =
	    &spa->spa_zio_taskq[ZIO_TYPE_WRITE][ZIO_TASKQ_ISSUE];

	spa_syncthread_info_t *ti = spa->spa_syncthreads;
	for (int i = 0, w = 0; i < nthreads; i++, w++, ti++) {
		ti->sti_thread = kthreads[i];
		if (w == tqs->stqs_count) {
			w = 0;
		}
		ti->sti_wr_iss_tq = tqs->stqs_taskq[w];
	}

	kmem_free(kthreads, sizeof (*kthreads) * nthreads);
	return (spa->spa_sync_tq);
}

void
spa_sync_tq_destroy(spa_t *spa)
{
	ASSERT(spa->spa_sync_tq != NULL);

	taskq_wait(spa->spa_sync_tq);
	taskq_destroy(spa->spa_sync_tq);
	kmem_free(spa->spa_syncthreads,
	    sizeof (spa_syncthread_info_t) * spa->spa_alloc_count);
	spa->spa_sync_tq = NULL;
}

void
spa_select_allocator(zio_t *zio)
{
	zbookmark_phys_t *bm = &zio->io_bookmark;
	spa_t *spa = zio->io_spa;

	ASSERT(zio->io_type == ZIO_TYPE_WRITE);

	/*
	 * A gang block (for example) may have inherited its parent's
	 * allocator, in which case there is nothing further to do here.
	 */
	if (ZIO_HAS_ALLOCATOR(zio))
		return;

	ASSERT(spa != NULL);
	ASSERT(bm != NULL);

	/*
	 * First try to use an allocator assigned to the syncthread, and set
	 * the corresponding write issue taskq for the allocator.
	 * Note, we must have an open pool to do this.
	 */
	if (spa->spa_sync_tq != NULL) {
		spa_syncthread_info_t *ti = spa->spa_syncthreads;
		for (int i = 0; i < spa->spa_alloc_count; i++, ti++) {
			if (ti->sti_thread == curthread) {
				zio->io_allocator = i;
				zio->io_wr_iss_tq = ti->sti_wr_iss_tq;
				return;
			}
		}
	}

	/*
	 * We want to try to use as many allocators as possible to help improve
	 * performance, but we also want logically adjacent IOs to be physically
	 * adjacent to improve sequential read performance. We chunk each object
	 * into 2^20 block regions, and then hash based on the objset, object,
	 * level, and region to accomplish both of these goals.
	 */
	uint64_t hv = cityhash4(bm->zb_objset, bm->zb_object, bm->zb_level,
	    bm->zb_blkid >> 20);

	zio->io_allocator = (uint_t)hv % spa->spa_alloc_count;
	zio->io_wr_iss_tq = NULL;
}

/*
 * ==========================================================================
 * Miscellaneous routines
 * ==========================================================================
 */

/*
 * Remove all pools in the system.
 */
void
spa_evict_all(void)
{
	spa_t *spa;

	/*
	 * Remove all cached state.  All pools should be closed now,
	 * so every spa in the AVL tree should be unreferenced.
	 */
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(NULL)) != NULL) {
		/*
		 * Stop async tasks.  The async thread may need to detach
		 * a device that's been replaced, which requires grabbing
		 * spa_namespace_lock, so we must drop it here.
		 */
		spa_open_ref(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		spa_async_suspend(spa);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);

		if (spa->spa_state != POOL_STATE_UNINITIALIZED) {
			spa_unload(spa);
			spa_deactivate(spa);
		}
		spa_remove(spa);
	}
	mutex_exit(&spa_namespace_lock);
}

vdev_t *
spa_lookup_by_guid(spa_t *spa, uint64_t guid, boolean_t aux)
{
	vdev_t *vd;
	int i;

	if ((vd = vdev_lookup_by_guid(spa->spa_root_vdev, guid)) != NULL)
		return (vd);

	if (aux) {
		for (i = 0; i < spa->spa_l2cache.sav_count; i++) {
			vd = spa->spa_l2cache.sav_vdevs[i];
			if (vd->vdev_guid == guid)
				return (vd);
		}

		for (i = 0; i < spa->spa_spares.sav_count; i++) {
			vd = spa->spa_spares.sav_vdevs[i];
			if (vd->vdev_guid == guid)
				return (vd);
		}
	}

	return (NULL);
}

void
spa_upgrade(spa_t *spa, uint64_t version)
{
	ASSERT(spa_writeable(spa));

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * This should only be called for a non-faulted pool, and since a
	 * future version would result in an unopenable pool, this shouldn't be
	 * possible.
	 */
	ASSERT(SPA_VERSION_IS_SUPPORTED(spa->spa_uberblock.ub_version));
	ASSERT3U(version, >=, spa->spa_uberblock.ub_version);

	spa->spa_uberblock.ub_version = version;
	vdev_config_dirty(spa->spa_root_vdev);

	spa_config_exit(spa, SCL_ALL, FTAG);

	txg_wait_synced(spa_get_dsl(spa), 0);
}

static boolean_t
spa_has_aux_vdev(spa_t *spa, uint64_t guid, spa_aux_vdev_t *sav)
{
	(void) spa;
	int i;
	uint64_t vdev_guid;

	for (i = 0; i < sav->sav_count; i++)
		if (sav->sav_vdevs[i]->vdev_guid == guid)
			return (B_TRUE);

	for (i = 0; i < sav->sav_npending; i++) {
		if (nvlist_lookup_uint64(sav->sav_pending[i], ZPOOL_CONFIG_GUID,
		    &vdev_guid) == 0 && vdev_guid == guid)
			return (B_TRUE);
	}

	return (B_FALSE);
}

boolean_t
spa_has_l2cache(spa_t *spa, uint64_t guid)
{
	return (spa_has_aux_vdev(spa, guid, &spa->spa_l2cache));
}

boolean_t
spa_has_spare(spa_t *spa, uint64_t guid)
{
	return (spa_has_aux_vdev(spa, guid, &spa->spa_spares));
}

/*
 * Check if a pool has an active shared spare device.
 * Note: reference count of an active spare is 2, as a spare and as a replace
 */
static boolean_t
spa_has_active_shared_spare(spa_t *spa)
{
	int i, refcnt;
	uint64_t pool;
	spa_aux_vdev_t *sav = &spa->spa_spares;

	for (i = 0; i < sav->sav_count; i++) {
		if (spa_spare_exists(sav->sav_vdevs[i]->vdev_guid, &pool,
		    &refcnt) && pool != 0ULL && pool == spa_guid(spa) &&
		    refcnt > 2)
			return (B_TRUE);
	}

	return (B_FALSE);
}

uint64_t
spa_total_metaslabs(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	uint64_t m = 0;
	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];
		if (!vdev_is_concrete(vd))
			continue;
		m += vd->vdev_ms_count;
	}
	return (m);
}

/*
 * Notify any waiting threads that some activity has switched from being in-
 * progress to not-in-progress so that the thread can wake up and determine
 * whether it is finished waiting.
 */
void
spa_notify_waiters(spa_t *spa)
{
	/*
	 * Acquiring spa_activities_lock here prevents the cv_broadcast from
	 * happening between the waiting thread's check and cv_wait.
	 */
	mutex_enter(&spa->spa_activities_lock);
	cv_broadcast(&spa->spa_activities_cv);
	mutex_exit(&spa->spa_activities_lock);
}

/*
 * Notify any waiting threads that the pool is exporting, and then block until
 * they are finished using the spa_t.
 */
void
spa_wake_waiters(spa_t *spa)
{
	mutex_enter(&spa->spa_activities_lock);
	spa->spa_waiters_cancel = B_TRUE;
	cv_broadcast(&spa->spa_activities_cv);
	while (spa->spa_waiters != 0)
		cv_wait(&spa->spa_waiters_cv, &spa->spa_activities_lock);
	spa->spa_waiters_cancel = B_FALSE;
	mutex_exit(&spa->spa_activities_lock);
}

/* Whether the vdev or any of its descendants are being initialized/trimmed. */
static boolean_t
spa_vdev_activity_in_progress_impl(vdev_t *vd, zpool_wait_activity_t activity)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(spa_config_held(spa, SCL_CONFIG | SCL_STATE, RW_READER));
	ASSERT(MUTEX_HELD(&spa->spa_activities_lock));
	ASSERT(activity == ZPOOL_WAIT_INITIALIZE ||
	    activity == ZPOOL_WAIT_TRIM);

	kmutex_t *lock = activity == ZPOOL_WAIT_INITIALIZE ?
	    &vd->vdev_initialize_lock : &vd->vdev_trim_lock;

	mutex_exit(&spa->spa_activities_lock);
	mutex_enter(lock);
	mutex_enter(&spa->spa_activities_lock);

	boolean_t in_progress = (activity == ZPOOL_WAIT_INITIALIZE) ?
	    (vd->vdev_initialize_state == VDEV_INITIALIZE_ACTIVE) :
	    (vd->vdev_trim_state == VDEV_TRIM_ACTIVE);
	mutex_exit(lock);

	if (in_progress)
		return (B_TRUE);

	for (int i = 0; i < vd->vdev_children; i++) {
		if (spa_vdev_activity_in_progress_impl(vd->vdev_child[i],
		    activity))
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * If use_guid is true, this checks whether the vdev specified by guid is
 * being initialized/trimmed. Otherwise, it checks whether any vdev in the pool
 * is being initialized/trimmed. The caller must hold the config lock and
 * spa_activities_lock.
 */
static int
spa_vdev_activity_in_progress(spa_t *spa, boolean_t use_guid, uint64_t guid,
    zpool_wait_activity_t activity, boolean_t *in_progress)
{
	mutex_exit(&spa->spa_activities_lock);
	spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);
	mutex_enter(&spa->spa_activities_lock);

	vdev_t *vd;
	if (use_guid) {
		vd = spa_lookup_by_guid(spa, guid, B_FALSE);
		if (vd == NULL || !vd->vdev_ops->vdev_op_leaf) {
			spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
			return (EINVAL);
		}
	} else {
		vd = spa->spa_root_vdev;
	}

	*in_progress = spa_vdev_activity_in_progress_impl(vd, activity);

	spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
	return (0);
}

/*
 * Locking for waiting threads
 * ---------------------------
 *
 * Waiting threads need a way to check whether a given activity is in progress,
 * and then, if it is, wait for it to complete. Each activity will have some
 * in-memory representation of the relevant on-disk state which can be used to
 * determine whether or not the activity is in progress. The in-memory state and
 * the locking used to protect it will be different for each activity, and may
 * not be suitable for use with a cvar (e.g., some state is protected by the
 * config lock). To allow waiting threads to wait without any races, another
 * lock, spa_activities_lock, is used.
 *
 * When the state is checked, both the activity-specific lock (if there is one)
 * and spa_activities_lock are held. In some cases, the activity-specific lock
 * is acquired explicitly (e.g. the config lock). In others, the locking is
 * internal to some check (e.g. bpobj_is_empty). After checking, the waiting
 * thread releases the activity-specific lock and, if the activity is in
 * progress, then cv_waits using spa_activities_lock.
 *
 * The waiting thread is woken when another thread, one completing some
 * activity, updates the state of the activity and then calls
 * spa_notify_waiters, which will cv_broadcast. This 'completing' thread only
 * needs to hold its activity-specific lock when updating the state, and this
 * lock can (but doesn't have to) be dropped before calling spa_notify_waiters.
 *
 * Because spa_notify_waiters acquires spa_activities_lock before broadcasting,
 * and because it is held when the waiting thread checks the state of the
 * activity, it can never be the case that the completing thread both updates
 * the activity state and cv_broadcasts in between the waiting thread's check
 * and cv_wait. Thus, a waiting thread can never miss a wakeup.
 *
 * In order to prevent deadlock, when the waiting thread does its check, in some
 * cases it will temporarily drop spa_activities_lock in order to acquire the
 * activity-specific lock. The order in which spa_activities_lock and the
 * activity specific lock are acquired in the waiting thread is determined by
 * the order in which they are acquired in the completing thread; if the
 * completing thread calls spa_notify_waiters with the activity-specific lock
 * held, then the waiting thread must also acquire the activity-specific lock
 * first.
 */

static int
spa_activity_in_progress(spa_t *spa, zpool_wait_activity_t activity,
    boolean_t use_tag, uint64_t tag, boolean_t *in_progress)
{
	int error = 0;

	ASSERT(MUTEX_HELD(&spa->spa_activities_lock));

	switch (activity) {
	case ZPOOL_WAIT_CKPT_DISCARD:
		*in_progress =
		    (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT) &&
		    zap_contains(spa_meta_objset(spa),
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_ZPOOL_CHECKPOINT) ==
		    ENOENT);
		break;
	case ZPOOL_WAIT_FREE:
		*in_progress = ((spa_version(spa) >= SPA_VERSION_DEADLISTS &&
		    !bpobj_is_empty(&spa->spa_dsl_pool->dp_free_bpobj)) ||
		    spa_feature_is_active(spa, SPA_FEATURE_ASYNC_DESTROY) ||
		    spa_livelist_delete_check(spa));
		break;
	case ZPOOL_WAIT_INITIALIZE:
	case ZPOOL_WAIT_TRIM:
		error = spa_vdev_activity_in_progress(spa, use_tag, tag,
		    activity, in_progress);
		break;
	case ZPOOL_WAIT_REPLACE:
		mutex_exit(&spa->spa_activities_lock);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);
		mutex_enter(&spa->spa_activities_lock);

		*in_progress = vdev_replace_in_progress(spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		break;
	case ZPOOL_WAIT_REMOVE:
		*in_progress = (spa->spa_removing_phys.sr_state ==
		    DSS_SCANNING);
		break;
	case ZPOOL_WAIT_RESILVER:
		*in_progress = vdev_rebuild_active(spa->spa_root_vdev);
		if (*in_progress)
			break;
		zfs_fallthrough;
	case ZPOOL_WAIT_SCRUB:
	{
		boolean_t scanning, paused, is_scrub;
		dsl_scan_t *scn =  spa->spa_dsl_pool->dp_scan;

		is_scrub = (scn->scn_phys.scn_func == POOL_SCAN_SCRUB);
		scanning = (scn->scn_phys.scn_state == DSS_SCANNING);
		paused = dsl_scan_is_paused_scrub(scn);
		*in_progress = (scanning && !paused &&
		    is_scrub == (activity == ZPOOL_WAIT_SCRUB));
		break;
	}
	case ZPOOL_WAIT_RAIDZ_EXPAND:
	{
		vdev_raidz_expand_t *vre = spa->spa_raidz_expand;
		*in_progress = (vre != NULL && vre->vre_state == DSS_SCANNING);
		break;
	}
	default:
		panic("unrecognized value for activity %d", activity);
	}

	return (error);
}

static int
spa_wait_common(const char *pool, zpool_wait_activity_t activity,
    boolean_t use_tag, uint64_t tag, boolean_t *waited)
{
	/*
	 * The tag is used to distinguish between instances of an activity.
	 * 'initialize' and 'trim' are the only activities that we use this for.
	 * The other activities can only have a single instance in progress in a
	 * pool at one time, making the tag unnecessary.
	 *
	 * There can be multiple devices being replaced at once, but since they
	 * all finish once resilvering finishes, we don't bother keeping track
	 * of them individually, we just wait for them all to finish.
	 */
	if (use_tag && activity != ZPOOL_WAIT_INITIALIZE &&
	    activity != ZPOOL_WAIT_TRIM)
		return (EINVAL);

	if (activity < 0 || activity >= ZPOOL_WAIT_NUM_ACTIVITIES)
		return (EINVAL);

	spa_t *spa;
	int error = spa_open(pool, &spa, FTAG);
	if (error != 0)
		return (error);

	/*
	 * Increment the spa's waiter count so that we can call spa_close and
	 * still ensure that the spa_t doesn't get freed before this thread is
	 * finished with it when the pool is exported. We want to call spa_close
	 * before we start waiting because otherwise the additional ref would
	 * prevent the pool from being exported or destroyed throughout the
	 * potentially long wait.
	 */
	mutex_enter(&spa->spa_activities_lock);
	spa->spa_waiters++;
	spa_close(spa, FTAG);

	*waited = B_FALSE;
	for (;;) {
		boolean_t in_progress;
		error = spa_activity_in_progress(spa, activity, use_tag, tag,
		    &in_progress);

		if (error || !in_progress || spa->spa_waiters_cancel)
			break;

		*waited = B_TRUE;

		if (cv_wait_sig(&spa->spa_activities_cv,
		    &spa->spa_activities_lock) == 0) {
			error = EINTR;
			break;
		}
	}

	spa->spa_waiters--;
	cv_signal(&spa->spa_waiters_cv);
	mutex_exit(&spa->spa_activities_lock);

	return (error);
}

/*
 * Wait for a particular instance of the specified activity to complete, where
 * the instance is identified by 'tag'
 */
int
spa_wait_tag(const char *pool, zpool_wait_activity_t activity, uint64_t tag,
    boolean_t *waited)
{
	return (spa_wait_common(pool, activity, B_TRUE, tag, waited));
}

/*
 * Wait for all instances of the specified activity complete
 */
int
spa_wait(const char *pool, zpool_wait_activity_t activity, boolean_t *waited)
{

	return (spa_wait_common(pool, activity, B_FALSE, 0, waited));
}

sysevent_t *
spa_event_create(spa_t *spa, vdev_t *vd, nvlist_t *hist_nvl, const char *name)
{
	sysevent_t *ev = NULL;
#ifdef _KERNEL
	nvlist_t *resource;

	resource = zfs_event_create(spa, vd, FM_SYSEVENT_CLASS, name, hist_nvl);
	if (resource) {
		ev = kmem_alloc(sizeof (sysevent_t), KM_SLEEP);
		ev->resource = resource;
	}
#else
	(void) spa, (void) vd, (void) hist_nvl, (void) name;
#endif
	return (ev);
}

void
spa_event_post(sysevent_t *ev)
{
#ifdef _KERNEL
	if (ev) {
		zfs_zevent_post(ev->resource, NULL, zfs_zevent_post_cb);
		kmem_free(ev, sizeof (*ev));
	}
#else
	(void) ev;
#endif
}

/*
 * Post a zevent corresponding to the given sysevent.   The 'name' must be one
 * of the event definitions in sys/sysevent/eventdefs.h.  The payload will be
 * filled in from the spa and (optionally) the vdev.  This doesn't do anything
 * in the userland libzpool, as we don't want consumers to misinterpret ztest
 * or zdb as real changes.
 */
void
spa_event_notify(spa_t *spa, vdev_t *vd, nvlist_t *hist_nvl, const char *name)
{
	spa_event_post(spa_event_create(spa, vd, hist_nvl, name));
}

/* state manipulation functions */
EXPORT_SYMBOL(spa_open);
EXPORT_SYMBOL(spa_open_rewind);
EXPORT_SYMBOL(spa_get_stats);
EXPORT_SYMBOL(spa_create);
EXPORT_SYMBOL(spa_import);
EXPORT_SYMBOL(spa_tryimport);
EXPORT_SYMBOL(spa_destroy);
EXPORT_SYMBOL(spa_export);
EXPORT_SYMBOL(spa_reset);
EXPORT_SYMBOL(spa_async_request);
EXPORT_SYMBOL(spa_async_suspend);
EXPORT_SYMBOL(spa_async_resume);
EXPORT_SYMBOL(spa_inject_addref);
EXPORT_SYMBOL(spa_inject_delref);
EXPORT_SYMBOL(spa_scan_stat_init);
EXPORT_SYMBOL(spa_scan_get_stats);

/* device manipulation */
EXPORT_SYMBOL(spa_vdev_add);
EXPORT_SYMBOL(spa_vdev_attach);
EXPORT_SYMBOL(spa_vdev_detach);
EXPORT_SYMBOL(spa_vdev_setpath);
EXPORT_SYMBOL(spa_vdev_setfru);
EXPORT_SYMBOL(spa_vdev_split_mirror);

/* spare statech is global across all pools) */
EXPORT_SYMBOL(spa_spare_add);
EXPORT_SYMBOL(spa_spare_remove);
EXPORT_SYMBOL(spa_spare_exists);
EXPORT_SYMBOL(spa_spare_activate);

/* L2ARC statech is global across all pools) */
EXPORT_SYMBOL(spa_l2cache_add);
EXPORT_SYMBOL(spa_l2cache_remove);
EXPORT_SYMBOL(spa_l2cache_exists);
EXPORT_SYMBOL(spa_l2cache_activate);
EXPORT_SYMBOL(spa_l2cache_drop);

/* scanning */
EXPORT_SYMBOL(spa_scan);
EXPORT_SYMBOL(spa_scan_stop);

/* spa syncing */
EXPORT_SYMBOL(spa_sync); /* only for DMU use */
EXPORT_SYMBOL(spa_sync_allpools);

/* properties */
EXPORT_SYMBOL(spa_prop_set);
EXPORT_SYMBOL(spa_prop_get);
EXPORT_SYMBOL(spa_prop_clear_bootfs);

/* asynchronous event notification */
EXPORT_SYMBOL(spa_event_notify);

ZFS_MODULE_PARAM(zfs_metaslab, metaslab_, preload_pct, UINT, ZMOD_RW,
	"Percentage of CPUs to run a metaslab preload taskq");

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_spa, spa_, load_verify_shift, UINT, ZMOD_RW,
	"log2 fraction of arc that can be used by inflight I/Os when "
	"verifying pool during import");
/* END CSTYLED */

ZFS_MODULE_PARAM(zfs_spa, spa_, load_verify_metadata, INT, ZMOD_RW,
	"Set to traverse metadata on pool import");

ZFS_MODULE_PARAM(zfs_spa, spa_, load_verify_data, INT, ZMOD_RW,
	"Set to traverse data on pool import");

ZFS_MODULE_PARAM(zfs_spa, spa_, load_print_vdev_tree, INT, ZMOD_RW,
	"Print vdev tree to zfs_dbgmsg during pool import");

ZFS_MODULE_PARAM(zfs_zio, zio_, taskq_batch_pct, UINT, ZMOD_RD,
	"Percentage of CPUs to run an IO worker thread");

ZFS_MODULE_PARAM(zfs_zio, zio_, taskq_batch_tpq, UINT, ZMOD_RD,
	"Number of threads per IO worker taskqueue");

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs, zfs_, max_missing_tvds, U64, ZMOD_RW,
	"Allow importing pool with up to this number of missing top-level "
	"vdevs (in read-only mode)");
/* END CSTYLED */

ZFS_MODULE_PARAM(zfs_livelist_condense, zfs_livelist_condense_, zthr_pause, INT,
	ZMOD_RW, "Set the livelist condense zthr to pause");

ZFS_MODULE_PARAM(zfs_livelist_condense, zfs_livelist_condense_, sync_pause, INT,
	ZMOD_RW, "Set the livelist condense synctask to pause");

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_livelist_condense, zfs_livelist_condense_, sync_cancel,
	INT, ZMOD_RW,
	"Whether livelist condensing was canceled in the synctask");

ZFS_MODULE_PARAM(zfs_livelist_condense, zfs_livelist_condense_, zthr_cancel,
	INT, ZMOD_RW,
	"Whether livelist condensing was canceled in the zthr function");

ZFS_MODULE_PARAM(zfs_livelist_condense, zfs_livelist_condense_, new_alloc, INT,
	ZMOD_RW,
	"Whether extra ALLOC blkptrs were added to a livelist entry while it "
	"was being condensed");

#ifdef _KERNEL
ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs_zio, zio_, taskq_read,
	spa_taskq_read_param_set, spa_taskq_read_param_get, ZMOD_RD,
	"Configure IO queues for read IO");
ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs_zio, zio_, taskq_write,
	spa_taskq_write_param_set, spa_taskq_write_param_get, ZMOD_RD,
	"Configure IO queues for write IO");
#endif
/* END CSTYLED */

ZFS_MODULE_PARAM(zfs_zio, zio_, taskq_wr_iss_ncpus, UINT, ZMOD_RW,
	"Number of CPUs to run write issue taskqs");
