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
 * Copyright (c) 2016, 2024 by Delphix. All rights reserved.
 * Copyright (c) 2019 by Lawrence Livermore National Security, LLC.
 * Copyright (c) 2021 Hewlett Packard Enterprise Development LP
 * Copyright 2023 RackTop Systems, Inc.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/txg.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_synctask.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/arc_impl.h>

/*
 * TRIM is a feature which is used to notify a SSD that some previously
 * written space is no longer allocated by the pool.  This is useful because
 * writes to a SSD must be performed to blocks which have first been erased.
 * Ensuring the SSD always has a supply of erased blocks for new writes
 * helps prevent the performance from deteriorating.
 *
 * There are two supported TRIM methods; manual and automatic.
 *
 * Manual TRIM:
 *
 * A manual TRIM is initiated by running the 'zpool trim' command.  A single
 * 'vdev_trim' thread is created for each leaf vdev, and it is responsible for
 * managing that vdev TRIM process.  This involves iterating over all the
 * metaslabs, calculating the unallocated space ranges, and then issuing the
 * required TRIM I/Os.
 *
 * While a metaslab is being actively trimmed it is not eligible to perform
 * new allocations.  After traversing all of the metaslabs the thread is
 * terminated.  Finally, both the requested options and current progress of
 * the TRIM are regularly written to the pool.  This allows the TRIM to be
 * suspended and resumed as needed.
 *
 * Automatic TRIM:
 *
 * An automatic TRIM is enabled by setting the 'autotrim' pool property
 * to 'on'.  When enabled, a `vdev_autotrim' thread is created for each
 * top-level (not leaf) vdev in the pool.  These threads perform the same
 * core TRIM process as a manual TRIM, but with a few key differences.
 *
 * 1) Automatic TRIM happens continuously in the background and operates
 *    solely on recently freed blocks (ms_trim not ms_allocatable).
 *
 * 2) Each thread is associated with a top-level (not leaf) vdev.  This has
 *    the benefit of simplifying the threading model, it makes it easier
 *    to coordinate administrative commands, and it ensures only a single
 *    metaslab is disabled at a time.  Unlike manual TRIM, this means each
 *    'vdev_autotrim' thread is responsible for issuing TRIM I/Os for its
 *    children.
 *
 * 3) There is no automatic TRIM progress information stored on disk, nor
 *    is it reported by 'zpool status'.
 *
 * While the automatic TRIM process is highly effective it is more likely
 * than a manual TRIM to encounter tiny ranges.  Ranges less than or equal to
 * 'zfs_trim_extent_bytes_min' (32k) are considered too small to efficiently
 * TRIM and are skipped.  This means small amounts of freed space may not
 * be automatically trimmed.
 *
 * Furthermore, devices with attached hot spares and devices being actively
 * replaced are skipped.  This is done to avoid adding additional stress to
 * a potentially unhealthy device and to minimize the required rebuild time.
 *
 * For this reason it may be beneficial to occasionally manually TRIM a pool
 * even when automatic TRIM is enabled.
 */

/*
 * Maximum size of TRIM I/O, ranges will be chunked in to 128MiB lengths.
 */
static unsigned int zfs_trim_extent_bytes_max = 128 * 1024 * 1024;

/*
 * Minimum size of TRIM I/O, extents smaller than 32Kib will be skipped.
 */
static unsigned int zfs_trim_extent_bytes_min = 32 * 1024;

/*
 * Skip uninitialized metaslabs during the TRIM process.  This option is
 * useful for pools constructed from large thinly-provisioned devices where
 * TRIM operations are slow.  As a pool ages an increasing fraction of
 * the pools metaslabs will be initialized progressively degrading the
 * usefulness of this option.  This setting is stored when starting a
 * manual TRIM and will persist for the duration of the requested TRIM.
 */
unsigned int zfs_trim_metaslab_skip = 0;

/*
 * Maximum number of queued TRIM I/Os per leaf vdev.  The number of
 * concurrent TRIM I/Os issued to the device is controlled by the
 * zfs_vdev_trim_min_active and zfs_vdev_trim_max_active module options.
 */
static unsigned int zfs_trim_queue_limit = 10;

/*
 * The minimum number of transaction groups between automatic trims of a
 * metaslab.  This setting represents a trade-off between issuing more
 * efficient TRIM operations, by allowing them to be aggregated longer,
 * and issuing them promptly so the trimmed space is available.  Note
 * that this value is a minimum; metaslabs can be trimmed less frequently
 * when there are a large number of ranges which need to be trimmed.
 *
 * Increasing this value will allow frees to be aggregated for a longer
 * time.  This can result is larger TRIM operations, and increased memory
 * usage in order to track the ranges to be trimmed.  Decreasing this value
 * has the opposite effect.  The default value of 32 was determined though
 * testing to be a reasonable compromise.
 */
static unsigned int zfs_trim_txg_batch = 32;

/*
 * The trim_args are a control structure which describe how a leaf vdev
 * should be trimmed.  The core elements are the vdev, the metaslab being
 * trimmed and a range tree containing the extents to TRIM.  All provided
 * ranges must be within the metaslab.
 */
typedef struct trim_args {
	/*
	 * These fields are set by the caller of vdev_trim_ranges().
	 */
	vdev_t		*trim_vdev;		/* Leaf vdev to TRIM */
	metaslab_t	*trim_msp;		/* Disabled metaslab */
	zfs_range_tree_t	*trim_tree;	/* TRIM ranges (in metaslab) */
	trim_type_t	trim_type;		/* Manual or auto TRIM */
	uint64_t	trim_extent_bytes_max;	/* Maximum TRIM I/O size */
	uint64_t	trim_extent_bytes_min;	/* Minimum TRIM I/O size */
	enum trim_flag	trim_flags;		/* TRIM flags (secure) */

	/*
	 * These fields are updated by vdev_trim_ranges().
	 */
	hrtime_t	trim_start_time;	/* Start time */
	uint64_t	trim_bytes_done;	/* Bytes trimmed */
} trim_args_t;

/*
 * Determines whether a vdev_trim_thread() should be stopped.
 */
static boolean_t
vdev_trim_should_stop(vdev_t *vd)
{
	return (vd->vdev_trim_exit_wanted || !vdev_writeable(vd) ||
	    vd->vdev_detached || vd->vdev_top->vdev_removing ||
	    vd->vdev_top->vdev_rz_expanding);
}

/*
 * Determines whether a vdev_autotrim_thread() should be stopped.
 */
static boolean_t
vdev_autotrim_should_stop(vdev_t *tvd)
{
	return (tvd->vdev_autotrim_exit_wanted ||
	    !vdev_writeable(tvd) || tvd->vdev_removing ||
	    tvd->vdev_rz_expanding ||
	    spa_get_autotrim(tvd->vdev_spa) == SPA_AUTOTRIM_OFF);
}

/*
 * Wait for given number of kicks, return true if the wait is aborted due to
 * vdev_autotrim_exit_wanted.
 */
static boolean_t
vdev_autotrim_wait_kick(vdev_t *vd, int num_of_kick)
{
	mutex_enter(&vd->vdev_autotrim_lock);
	for (int i = 0; i < num_of_kick; i++) {
		if (vd->vdev_autotrim_exit_wanted)
			break;
		cv_wait_idle(&vd->vdev_autotrim_kick_cv,
		    &vd->vdev_autotrim_lock);
	}
	boolean_t exit_wanted = vd->vdev_autotrim_exit_wanted;
	mutex_exit(&vd->vdev_autotrim_lock);

	return (exit_wanted);
}

/*
 * The sync task for updating the on-disk state of a manual TRIM.  This
 * is scheduled by vdev_trim_change_state().
 */
static void
vdev_trim_zap_update_sync(void *arg, dmu_tx_t *tx)
{
	/*
	 * We pass in the guid instead of the vdev_t since the vdev may
	 * have been freed prior to the sync task being processed.  This
	 * happens when a vdev is detached as we call spa_config_vdev_exit(),
	 * stop the trimming thread, schedule the sync task, and free
	 * the vdev. Later when the scheduled sync task is invoked, it would
	 * find that the vdev has been freed.
	 */
	uint64_t guid = *(uint64_t *)arg;
	uint64_t txg = dmu_tx_get_txg(tx);
	kmem_free(arg, sizeof (uint64_t));

	vdev_t *vd = spa_lookup_by_guid(tx->tx_pool->dp_spa, guid, B_FALSE);
	if (vd == NULL || vd->vdev_top->vdev_removing ||
	    !vdev_is_concrete(vd) || vd->vdev_top->vdev_rz_expanding)
		return;

	uint64_t last_offset = vd->vdev_trim_offset[txg & TXG_MASK];
	vd->vdev_trim_offset[txg & TXG_MASK] = 0;

	VERIFY3U(vd->vdev_leaf_zap, !=, 0);

	objset_t *mos = vd->vdev_spa->spa_meta_objset;

	if (last_offset > 0 || vd->vdev_trim_last_offset == UINT64_MAX) {

		if (vd->vdev_trim_last_offset == UINT64_MAX)
			last_offset = 0;

		vd->vdev_trim_last_offset = last_offset;
		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_TRIM_LAST_OFFSET,
		    sizeof (last_offset), 1, &last_offset, tx));
	}

	if (vd->vdev_trim_action_time > 0) {
		uint64_t val = (uint64_t)vd->vdev_trim_action_time;
		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_TRIM_ACTION_TIME, sizeof (val),
		    1, &val, tx));
	}

	if (vd->vdev_trim_rate > 0) {
		uint64_t rate = (uint64_t)vd->vdev_trim_rate;

		if (rate == UINT64_MAX)
			rate = 0;

		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_TRIM_RATE, sizeof (rate), 1, &rate, tx));
	}

	uint64_t partial = vd->vdev_trim_partial;
	if (partial == UINT64_MAX)
		partial = 0;

	VERIFY0(zap_update(mos, vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_PARTIAL,
	    sizeof (partial), 1, &partial, tx));

	uint64_t secure = vd->vdev_trim_secure;
	if (secure == UINT64_MAX)
		secure = 0;

	VERIFY0(zap_update(mos, vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_SECURE,
	    sizeof (secure), 1, &secure, tx));


	uint64_t trim_state = vd->vdev_trim_state;
	VERIFY0(zap_update(mos, vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_STATE,
	    sizeof (trim_state), 1, &trim_state, tx));
}

/*
 * Update the on-disk state of a manual TRIM.  This is called to request
 * that a TRIM be started/suspended/canceled, or to change one of the
 * TRIM options (partial, secure, rate).
 */
static void
vdev_trim_change_state(vdev_t *vd, vdev_trim_state_t new_state,
    uint64_t rate, boolean_t partial, boolean_t secure)
{
	ASSERT(MUTEX_HELD(&vd->vdev_trim_lock));
	spa_t *spa = vd->vdev_spa;

	if (new_state == vd->vdev_trim_state)
		return;

	/*
	 * Copy the vd's guid, this will be freed by the sync task.
	 */
	uint64_t *guid = kmem_zalloc(sizeof (uint64_t), KM_SLEEP);
	*guid = vd->vdev_guid;

	/*
	 * If we're suspending, then preserve the original start time.
	 */
	if (vd->vdev_trim_state != VDEV_TRIM_SUSPENDED) {
		vd->vdev_trim_action_time = gethrestime_sec();
	}

	/*
	 * If we're activating, then preserve the requested rate and trim
	 * method.  Setting the last offset and rate to UINT64_MAX is used
	 * as a sentinel to indicate they should be reset to default values.
	 */
	if (new_state == VDEV_TRIM_ACTIVE) {
		if (vd->vdev_trim_state == VDEV_TRIM_COMPLETE ||
		    vd->vdev_trim_state == VDEV_TRIM_CANCELED) {
			vd->vdev_trim_last_offset = UINT64_MAX;
			vd->vdev_trim_rate = UINT64_MAX;
			vd->vdev_trim_partial = UINT64_MAX;
			vd->vdev_trim_secure = UINT64_MAX;
		}

		if (rate != 0)
			vd->vdev_trim_rate = rate;

		if (partial != 0)
			vd->vdev_trim_partial = partial;

		if (secure != 0)
			vd->vdev_trim_secure = secure;
	}

	vdev_trim_state_t old_state = vd->vdev_trim_state;
	boolean_t resumed = (old_state == VDEV_TRIM_SUSPENDED);
	vd->vdev_trim_state = new_state;

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	dsl_sync_task_nowait(spa_get_dsl(spa), vdev_trim_zap_update_sync,
	    guid, tx);

	switch (new_state) {
	case VDEV_TRIM_ACTIVE:
		spa_event_notify(spa, vd, NULL,
		    resumed ? ESC_ZFS_TRIM_RESUME : ESC_ZFS_TRIM_START);
		spa_history_log_internal(spa, "trim", tx,
		    "vdev=%s activated", vd->vdev_path);
		break;
	case VDEV_TRIM_SUSPENDED:
		spa_event_notify(spa, vd, NULL, ESC_ZFS_TRIM_SUSPEND);
		spa_history_log_internal(spa, "trim", tx,
		    "vdev=%s suspended", vd->vdev_path);
		break;
	case VDEV_TRIM_CANCELED:
		if (old_state == VDEV_TRIM_ACTIVE ||
		    old_state == VDEV_TRIM_SUSPENDED) {
			spa_event_notify(spa, vd, NULL, ESC_ZFS_TRIM_CANCEL);
			spa_history_log_internal(spa, "trim", tx,
			    "vdev=%s canceled", vd->vdev_path);
		}
		break;
	case VDEV_TRIM_COMPLETE:
		spa_event_notify(spa, vd, NULL, ESC_ZFS_TRIM_FINISH);
		spa_history_log_internal(spa, "trim", tx,
		    "vdev=%s complete", vd->vdev_path);
		break;
	default:
		panic("invalid state %llu", (unsigned long long)new_state);
	}

	dmu_tx_commit(tx);

	if (new_state != VDEV_TRIM_ACTIVE)
		spa_notify_waiters(spa);
}

/*
 * The zio_done_func_t done callback for each manual TRIM issued.  It is
 * responsible for updating the TRIM stats, reissuing failed TRIM I/Os,
 * and limiting the number of in flight TRIM I/Os.
 */
static void
vdev_trim_cb(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;

	mutex_enter(&vd->vdev_trim_io_lock);
	if (zio->io_error == ENXIO && !vdev_writeable(vd)) {
		/*
		 * The I/O failed because the vdev was unavailable; roll the
		 * last offset back. (This works because spa_sync waits on
		 * spa_txg_zio before it runs sync tasks.)
		 */
		uint64_t *offset =
		    &vd->vdev_trim_offset[zio->io_txg & TXG_MASK];
		*offset = MIN(*offset, zio->io_offset);
	} else {
		if (zio->io_error != 0) {
			vd->vdev_stat.vs_trim_errors++;
			spa_iostats_trim_add(vd->vdev_spa, TRIM_TYPE_MANUAL,
			    0, 0, 0, 0, 1, zio->io_orig_size);
		} else {
			spa_iostats_trim_add(vd->vdev_spa, TRIM_TYPE_MANUAL,
			    1, zio->io_orig_size, 0, 0, 0, 0);
		}

		vd->vdev_trim_bytes_done += zio->io_orig_size;
	}

	ASSERT3U(vd->vdev_trim_inflight[TRIM_TYPE_MANUAL], >, 0);
	vd->vdev_trim_inflight[TRIM_TYPE_MANUAL]--;
	cv_broadcast(&vd->vdev_trim_io_cv);
	mutex_exit(&vd->vdev_trim_io_lock);

	spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
}

/*
 * The zio_done_func_t done callback for each automatic TRIM issued.  It
 * is responsible for updating the TRIM stats and limiting the number of
 * in flight TRIM I/Os.  Automatic TRIM I/Os are best effort and are
 * never reissued on failure.
 */
static void
vdev_autotrim_cb(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;

	mutex_enter(&vd->vdev_trim_io_lock);

	if (zio->io_error != 0) {
		vd->vdev_stat.vs_trim_errors++;
		spa_iostats_trim_add(vd->vdev_spa, TRIM_TYPE_AUTO,
		    0, 0, 0, 0, 1, zio->io_orig_size);
	} else {
		spa_iostats_trim_add(vd->vdev_spa, TRIM_TYPE_AUTO,
		    1, zio->io_orig_size, 0, 0, 0, 0);
	}

	ASSERT3U(vd->vdev_trim_inflight[TRIM_TYPE_AUTO], >, 0);
	vd->vdev_trim_inflight[TRIM_TYPE_AUTO]--;
	cv_broadcast(&vd->vdev_trim_io_cv);
	mutex_exit(&vd->vdev_trim_io_lock);

	spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
}

/*
 * The zio_done_func_t done callback for each TRIM issued via
 * vdev_trim_simple(). It is responsible for updating the TRIM stats and
 * limiting the number of in flight TRIM I/Os.  Simple TRIM I/Os are best
 * effort and are never reissued on failure.
 */
static void
vdev_trim_simple_cb(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;

	mutex_enter(&vd->vdev_trim_io_lock);

	if (zio->io_error != 0) {
		vd->vdev_stat.vs_trim_errors++;
		spa_iostats_trim_add(vd->vdev_spa, TRIM_TYPE_SIMPLE,
		    0, 0, 0, 0, 1, zio->io_orig_size);
	} else {
		spa_iostats_trim_add(vd->vdev_spa, TRIM_TYPE_SIMPLE,
		    1, zio->io_orig_size, 0, 0, 0, 0);
	}

	ASSERT3U(vd->vdev_trim_inflight[TRIM_TYPE_SIMPLE], >, 0);
	vd->vdev_trim_inflight[TRIM_TYPE_SIMPLE]--;
	cv_broadcast(&vd->vdev_trim_io_cv);
	mutex_exit(&vd->vdev_trim_io_lock);

	spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
}
/*
 * Returns the average trim rate in bytes/sec for the ta->trim_vdev.
 */
static uint64_t
vdev_trim_calculate_rate(trim_args_t *ta)
{
	return (ta->trim_bytes_done * 1000 /
	    (NSEC2MSEC(gethrtime() - ta->trim_start_time) + 1));
}

/*
 * Issues a physical TRIM and takes care of rate limiting (bytes/sec)
 * and number of concurrent TRIM I/Os.
 */
static int
vdev_trim_range(trim_args_t *ta, uint64_t start, uint64_t size)
{
	vdev_t *vd = ta->trim_vdev;
	spa_t *spa = vd->vdev_spa;
	void *cb;

	mutex_enter(&vd->vdev_trim_io_lock);

	/*
	 * Limit manual TRIM I/Os to the requested rate.  This does not
	 * apply to automatic TRIM since no per vdev rate can be specified.
	 */
	if (ta->trim_type == TRIM_TYPE_MANUAL) {
		while (vd->vdev_trim_rate != 0 && !vdev_trim_should_stop(vd) &&
		    vdev_trim_calculate_rate(ta) > vd->vdev_trim_rate) {
			cv_timedwait_idle(&vd->vdev_trim_io_cv,
			    &vd->vdev_trim_io_lock, ddi_get_lbolt() +
			    MSEC_TO_TICK(10));
		}
	}
	ta->trim_bytes_done += size;

	/* Limit in flight trimming I/Os */
	while (vd->vdev_trim_inflight[0] + vd->vdev_trim_inflight[1] +
	    vd->vdev_trim_inflight[2] >= zfs_trim_queue_limit) {
		cv_wait(&vd->vdev_trim_io_cv, &vd->vdev_trim_io_lock);
	}
	vd->vdev_trim_inflight[ta->trim_type]++;
	mutex_exit(&vd->vdev_trim_io_lock);

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	uint64_t txg = dmu_tx_get_txg(tx);

	spa_config_enter(spa, SCL_STATE_ALL, vd, RW_READER);
	mutex_enter(&vd->vdev_trim_lock);

	if (ta->trim_type == TRIM_TYPE_MANUAL &&
	    vd->vdev_trim_offset[txg & TXG_MASK] == 0) {
		uint64_t *guid = kmem_zalloc(sizeof (uint64_t), KM_SLEEP);
		*guid = vd->vdev_guid;

		/* This is the first write of this txg. */
		dsl_sync_task_nowait(spa_get_dsl(spa),
		    vdev_trim_zap_update_sync, guid, tx);
	}

	/*
	 * We know the vdev_t will still be around since all consumers of
	 * vdev_free must stop the trimming first.
	 */
	if ((ta->trim_type == TRIM_TYPE_MANUAL &&
	    vdev_trim_should_stop(vd)) ||
	    (ta->trim_type == TRIM_TYPE_AUTO &&
	    vdev_autotrim_should_stop(vd->vdev_top))) {
		mutex_enter(&vd->vdev_trim_io_lock);
		vd->vdev_trim_inflight[ta->trim_type]--;
		mutex_exit(&vd->vdev_trim_io_lock);
		spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
		mutex_exit(&vd->vdev_trim_lock);
		dmu_tx_commit(tx);
		return (SET_ERROR(EINTR));
	}
	mutex_exit(&vd->vdev_trim_lock);

	if (ta->trim_type == TRIM_TYPE_MANUAL)
		vd->vdev_trim_offset[txg & TXG_MASK] = start + size;

	if (ta->trim_type == TRIM_TYPE_MANUAL) {
		cb = vdev_trim_cb;
	} else if (ta->trim_type == TRIM_TYPE_AUTO) {
		cb = vdev_autotrim_cb;
	} else {
		cb = vdev_trim_simple_cb;
	}

	zio_nowait(zio_trim(spa->spa_txg_zio[txg & TXG_MASK], vd,
	    start, size, cb, NULL, ZIO_PRIORITY_TRIM, ZIO_FLAG_CANFAIL,
	    ta->trim_flags));
	/* vdev_trim_cb and vdev_autotrim_cb release SCL_STATE_ALL */

	dmu_tx_commit(tx);

	return (0);
}

/*
 * Issues TRIM I/Os for all ranges in the provided ta->trim_tree range tree.
 * Additional parameters describing how the TRIM should be performed must
 * be set in the trim_args structure.  See the trim_args definition for
 * additional information.
 */
static int
vdev_trim_ranges(trim_args_t *ta)
{
	vdev_t *vd = ta->trim_vdev;
	zfs_btree_t *t = &ta->trim_tree->rt_root;
	zfs_btree_index_t idx;
	uint64_t extent_bytes_max = ta->trim_extent_bytes_max;
	uint64_t extent_bytes_min = ta->trim_extent_bytes_min;
	spa_t *spa = vd->vdev_spa;
	int error = 0;

	ta->trim_start_time = gethrtime();
	ta->trim_bytes_done = 0;

	for (zfs_range_seg_t *rs = zfs_btree_first(t, &idx); rs != NULL;
	    rs = zfs_btree_next(t, &idx, &idx)) {
		uint64_t size = zfs_rs_get_end(rs, ta->trim_tree) -
		    zfs_rs_get_start(rs, ta->trim_tree);

		if (extent_bytes_min && size < extent_bytes_min) {
			spa_iostats_trim_add(spa, ta->trim_type,
			    0, 0, 1, size, 0, 0);
			continue;
		}

		/* Split range into legally-sized physical chunks */
		uint64_t writes_required = ((size - 1) / extent_bytes_max) + 1;

		for (uint64_t w = 0; w < writes_required; w++) {
			error = vdev_trim_range(ta, VDEV_LABEL_START_SIZE +
			    zfs_rs_get_start(rs, ta->trim_tree) +
			    (w *extent_bytes_max), MIN(size -
			    (w * extent_bytes_max), extent_bytes_max));
			if (error != 0) {
				goto done;
			}
		}
	}

done:
	/*
	 * Make sure all TRIMs for this metaslab have completed before
	 * returning. TRIM zios have lower priority over regular or syncing
	 * zios, so all TRIM zios for this metaslab must complete before the
	 * metaslab is re-enabled. Otherwise it's possible write zios to
	 * this metaslab could cut ahead of still queued TRIM zios for this
	 * metaslab causing corruption if the ranges overlap.
	 */
	mutex_enter(&vd->vdev_trim_io_lock);
	while (vd->vdev_trim_inflight[0] > 0) {
		cv_wait(&vd->vdev_trim_io_cv, &vd->vdev_trim_io_lock);
	}
	mutex_exit(&vd->vdev_trim_io_lock);

	return (error);
}

static void
vdev_trim_xlate_last_rs_end(void *arg, range_seg64_t *physical_rs)
{
	uint64_t *last_rs_end = (uint64_t *)arg;

	if (physical_rs->rs_end > *last_rs_end)
		*last_rs_end = physical_rs->rs_end;
}

static void
vdev_trim_xlate_progress(void *arg, range_seg64_t *physical_rs)
{
	vdev_t *vd = (vdev_t *)arg;

	uint64_t size = physical_rs->rs_end - physical_rs->rs_start;
	vd->vdev_trim_bytes_est += size;

	if (vd->vdev_trim_last_offset >= physical_rs->rs_end) {
		vd->vdev_trim_bytes_done += size;
	} else if (vd->vdev_trim_last_offset > physical_rs->rs_start &&
	    vd->vdev_trim_last_offset <= physical_rs->rs_end) {
		vd->vdev_trim_bytes_done +=
		    vd->vdev_trim_last_offset - physical_rs->rs_start;
	}
}

/*
 * Calculates the completion percentage of a manual TRIM.
 */
static void
vdev_trim_calculate_progress(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_READER) ||
	    spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_WRITER));
	ASSERT(vd->vdev_leaf_zap != 0);

	vd->vdev_trim_bytes_est = 0;
	vd->vdev_trim_bytes_done = 0;

	for (uint64_t i = 0; i < vd->vdev_top->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_top->vdev_ms[i];
		mutex_enter(&msp->ms_lock);

		uint64_t ms_free = (msp->ms_size -
		    metaslab_allocated_space(msp)) /
		    vdev_get_ndisks(vd->vdev_top);

		/*
		 * Convert the metaslab range to a physical range
		 * on our vdev. We use this to determine if we are
		 * in the middle of this metaslab range.
		 */
		range_seg64_t logical_rs, physical_rs, remain_rs;
		logical_rs.rs_start = msp->ms_start;
		logical_rs.rs_end = msp->ms_start + msp->ms_size;

		/* Metaslab space after this offset has not been trimmed. */
		vdev_xlate(vd, &logical_rs, &physical_rs, &remain_rs);
		if (vd->vdev_trim_last_offset <= physical_rs.rs_start) {
			vd->vdev_trim_bytes_est += ms_free;
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/* Metaslab space before this offset has been trimmed */
		uint64_t last_rs_end = physical_rs.rs_end;
		if (!vdev_xlate_is_empty(&remain_rs)) {
			vdev_xlate_walk(vd, &remain_rs,
			    vdev_trim_xlate_last_rs_end, &last_rs_end);
		}

		if (vd->vdev_trim_last_offset > last_rs_end) {
			vd->vdev_trim_bytes_done += ms_free;
			vd->vdev_trim_bytes_est += ms_free;
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/*
		 * If we get here, we're in the middle of trimming this
		 * metaslab.  Load it and walk the free tree for more
		 * accurate progress estimation.
		 */
		VERIFY0(metaslab_load(msp));

		zfs_range_tree_t *rt = msp->ms_allocatable;
		zfs_btree_t *bt = &rt->rt_root;
		zfs_btree_index_t idx;
		for (zfs_range_seg_t *rs = zfs_btree_first(bt, &idx);
		    rs != NULL; rs = zfs_btree_next(bt, &idx, &idx)) {
			logical_rs.rs_start = zfs_rs_get_start(rs, rt);
			logical_rs.rs_end = zfs_rs_get_end(rs, rt);

			vdev_xlate_walk(vd, &logical_rs,
			    vdev_trim_xlate_progress, vd);
		}
		mutex_exit(&msp->ms_lock);
	}
}

/*
 * Load from disk the vdev's manual TRIM information.  This includes the
 * state, progress, and options provided when initiating the manual TRIM.
 */
static int
vdev_trim_load(vdev_t *vd)
{
	int err = 0;
	ASSERT(spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_READER) ||
	    spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_WRITER));
	ASSERT(vd->vdev_leaf_zap != 0);

	if (vd->vdev_trim_state == VDEV_TRIM_ACTIVE ||
	    vd->vdev_trim_state == VDEV_TRIM_SUSPENDED) {
		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_LAST_OFFSET,
		    sizeof (vd->vdev_trim_last_offset), 1,
		    &vd->vdev_trim_last_offset);
		if (err == ENOENT) {
			vd->vdev_trim_last_offset = 0;
			err = 0;
		}

		if (err == 0) {
			err = zap_lookup(vd->vdev_spa->spa_meta_objset,
			    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_RATE,
			    sizeof (vd->vdev_trim_rate), 1,
			    &vd->vdev_trim_rate);
			if (err == ENOENT) {
				vd->vdev_trim_rate = 0;
				err = 0;
			}
		}

		if (err == 0) {
			err = zap_lookup(vd->vdev_spa->spa_meta_objset,
			    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_PARTIAL,
			    sizeof (vd->vdev_trim_partial), 1,
			    &vd->vdev_trim_partial);
			if (err == ENOENT) {
				vd->vdev_trim_partial = 0;
				err = 0;
			}
		}

		if (err == 0) {
			err = zap_lookup(vd->vdev_spa->spa_meta_objset,
			    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_SECURE,
			    sizeof (vd->vdev_trim_secure), 1,
			    &vd->vdev_trim_secure);
			if (err == ENOENT) {
				vd->vdev_trim_secure = 0;
				err = 0;
			}
		}
	}

	vdev_trim_calculate_progress(vd);

	return (err);
}

static void
vdev_trim_xlate_range_add(void *arg, range_seg64_t *physical_rs)
{
	trim_args_t *ta = arg;
	vdev_t *vd = ta->trim_vdev;

	/*
	 * Only a manual trim will be traversing the vdev sequentially.
	 * For an auto trim all valid ranges should be added.
	 */
	if (ta->trim_type == TRIM_TYPE_MANUAL) {

		/* Only add segments that we have not visited yet */
		if (physical_rs->rs_end <= vd->vdev_trim_last_offset)
			return;

		/* Pick up where we left off mid-range. */
		if (vd->vdev_trim_last_offset > physical_rs->rs_start) {
			ASSERT3U(physical_rs->rs_end, >,
			    vd->vdev_trim_last_offset);
			physical_rs->rs_start = vd->vdev_trim_last_offset;
		}
	}

	ASSERT3U(physical_rs->rs_end, >, physical_rs->rs_start);

	zfs_range_tree_add(ta->trim_tree, physical_rs->rs_start,
	    physical_rs->rs_end - physical_rs->rs_start);
}

/*
 * Convert the logical range into physical ranges and add them to the
 * range tree passed in the trim_args_t.
 */
static void
vdev_trim_range_add(void *arg, uint64_t start, uint64_t size)
{
	trim_args_t *ta = arg;
	vdev_t *vd = ta->trim_vdev;
	range_seg64_t logical_rs;
	logical_rs.rs_start = start;
	logical_rs.rs_end = start + size;

	/*
	 * Every range to be trimmed must be part of ms_allocatable.
	 * When ZFS_DEBUG_TRIM is set load the metaslab to verify this
	 * is always the case.
	 */
	if (zfs_flags & ZFS_DEBUG_TRIM) {
		metaslab_t *msp = ta->trim_msp;
		VERIFY0(metaslab_load(msp));
		VERIFY3B(msp->ms_loaded, ==, B_TRUE);
		VERIFY(zfs_range_tree_contains(msp->ms_allocatable, start,
		    size));
	}

	ASSERT(vd->vdev_ops->vdev_op_leaf);
	vdev_xlate_walk(vd, &logical_rs, vdev_trim_xlate_range_add, arg);
}

/*
 * Each manual TRIM thread is responsible for trimming the unallocated
 * space for each leaf vdev.  This is accomplished by sequentially iterating
 * over its top-level metaslabs and issuing TRIM I/O for the space described
 * by its ms_allocatable.  While a metaslab is undergoing trimming it is
 * not eligible for new allocations.
 */
static __attribute__((noreturn)) void
vdev_trim_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	trim_args_t ta;
	int error = 0;

	/*
	 * The VDEV_LEAF_ZAP_TRIM_* entries may have been updated by
	 * vdev_trim().  Wait for the updated values to be reflected
	 * in the zap in order to start with the requested settings.
	 */
	txg_wait_synced(spa_get_dsl(vd->vdev_spa), 0);

	ASSERT(vdev_is_concrete(vd));
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	vd->vdev_trim_last_offset = 0;
	vd->vdev_trim_rate = 0;
	vd->vdev_trim_partial = 0;
	vd->vdev_trim_secure = 0;

	VERIFY0(vdev_trim_load(vd));

	ta.trim_vdev = vd;
	ta.trim_extent_bytes_max = zfs_trim_extent_bytes_max;
	ta.trim_extent_bytes_min = zfs_trim_extent_bytes_min;
	ta.trim_tree = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0);
	ta.trim_type = TRIM_TYPE_MANUAL;
	ta.trim_flags = 0;

	/*
	 * When a secure TRIM has been requested infer that the intent
	 * is that everything must be trimmed.  Override the default
	 * minimum TRIM size to prevent ranges from being skipped.
	 */
	if (vd->vdev_trim_secure) {
		ta.trim_flags |= ZIO_TRIM_SECURE;
		ta.trim_extent_bytes_min = SPA_MINBLOCKSIZE;
	}

	uint64_t ms_count = 0;
	for (uint64_t i = 0; !vd->vdev_detached &&
	    i < vd->vdev_top->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_top->vdev_ms[i];

		/*
		 * If we've expanded the top-level vdev or it's our
		 * first pass, calculate our progress.
		 */
		if (vd->vdev_top->vdev_ms_count != ms_count) {
			vdev_trim_calculate_progress(vd);
			ms_count = vd->vdev_top->vdev_ms_count;
		}

		spa_config_exit(spa, SCL_CONFIG, FTAG);
		metaslab_disable(msp);
		mutex_enter(&msp->ms_lock);
		VERIFY0(metaslab_load(msp));

		/*
		 * If a partial TRIM was requested skip metaslabs which have
		 * never been initialized and thus have never been written.
		 */
		if (msp->ms_sm == NULL && vd->vdev_trim_partial) {
			mutex_exit(&msp->ms_lock);
			metaslab_enable(msp, B_FALSE, B_FALSE);
			spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
			vdev_trim_calculate_progress(vd);
			continue;
		}

		ta.trim_msp = msp;
		zfs_range_tree_walk(msp->ms_allocatable, vdev_trim_range_add,
		    &ta);
		zfs_range_tree_vacate(msp->ms_trim, NULL, NULL);
		mutex_exit(&msp->ms_lock);

		error = vdev_trim_ranges(&ta);
		metaslab_enable(msp, B_TRUE, B_FALSE);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

		zfs_range_tree_vacate(ta.trim_tree, NULL, NULL);
		if (error != 0)
			break;
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	zfs_range_tree_destroy(ta.trim_tree);

	mutex_enter(&vd->vdev_trim_lock);
	if (!vd->vdev_trim_exit_wanted) {
		if (vdev_writeable(vd)) {
			vdev_trim_change_state(vd, VDEV_TRIM_COMPLETE,
			    vd->vdev_trim_rate, vd->vdev_trim_partial,
			    vd->vdev_trim_secure);
		} else if (vd->vdev_faulted) {
			vdev_trim_change_state(vd, VDEV_TRIM_CANCELED,
			    vd->vdev_trim_rate, vd->vdev_trim_partial,
			    vd->vdev_trim_secure);
		}
	}
	ASSERT(vd->vdev_trim_thread != NULL || vd->vdev_trim_inflight[0] == 0);

	/*
	 * Drop the vdev_trim_lock while we sync out the txg since it's
	 * possible that a device might be trying to come online and must
	 * check to see if it needs to restart a trim. That thread will be
	 * holding the spa_config_lock which would prevent the txg_wait_synced
	 * from completing.
	 */
	mutex_exit(&vd->vdev_trim_lock);
	txg_wait_synced(spa_get_dsl(spa), 0);
	mutex_enter(&vd->vdev_trim_lock);

	vd->vdev_trim_thread = NULL;
	cv_broadcast(&vd->vdev_trim_cv);
	mutex_exit(&vd->vdev_trim_lock);

	thread_exit();
}

/*
 * Initiates a manual TRIM for the vdev_t.  Callers must hold vdev_trim_lock,
 * the vdev_t must be a leaf and cannot already be manually trimming.
 */
void
vdev_trim(vdev_t *vd, uint64_t rate, boolean_t partial, boolean_t secure)
{
	ASSERT(MUTEX_HELD(&vd->vdev_trim_lock));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(vdev_is_concrete(vd));
	ASSERT3P(vd->vdev_trim_thread, ==, NULL);
	ASSERT(!vd->vdev_detached);
	ASSERT(!vd->vdev_trim_exit_wanted);
	ASSERT(!vd->vdev_top->vdev_removing);
	ASSERT(!vd->vdev_rz_expanding);

	vdev_trim_change_state(vd, VDEV_TRIM_ACTIVE, rate, partial, secure);
	vd->vdev_trim_thread = thread_create(NULL, 0,
	    vdev_trim_thread, vd, 0, &p0, TS_RUN, maxclsyspri);
}

/*
 * Wait for the trimming thread to be terminated (canceled or stopped).
 */
static void
vdev_trim_stop_wait_impl(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&vd->vdev_trim_lock));

	while (vd->vdev_trim_thread != NULL)
		cv_wait(&vd->vdev_trim_cv, &vd->vdev_trim_lock);

	ASSERT3P(vd->vdev_trim_thread, ==, NULL);
	vd->vdev_trim_exit_wanted = B_FALSE;
}

/*
 * Wait for vdev trim threads which were listed to cleanly exit.
 */
void
vdev_trim_stop_wait(spa_t *spa, list_t *vd_list)
{
	(void) spa;
	vdev_t *vd;

	ASSERT(MUTEX_HELD(&spa_namespace_lock) ||
	    spa->spa_export_thread == curthread);

	while ((vd = list_remove_head(vd_list)) != NULL) {
		mutex_enter(&vd->vdev_trim_lock);
		vdev_trim_stop_wait_impl(vd);
		mutex_exit(&vd->vdev_trim_lock);
	}
}

/*
 * Stop trimming a device, with the resultant trimming state being tgt_state.
 * For blocking behavior pass NULL for vd_list.  Otherwise, when a list_t is
 * provided the stopping vdev is inserted in to the list.  Callers are then
 * required to call vdev_trim_stop_wait() to block for all the trim threads
 * to exit.  The caller must hold vdev_trim_lock and must not be writing to
 * the spa config, as the trimming thread may try to enter the config as a
 * reader before exiting.
 */
void
vdev_trim_stop(vdev_t *vd, vdev_trim_state_t tgt_state, list_t *vd_list)
{
	ASSERT(!spa_config_held(vd->vdev_spa, SCL_CONFIG|SCL_STATE, RW_WRITER));
	ASSERT(MUTEX_HELD(&vd->vdev_trim_lock));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(vdev_is_concrete(vd));

	/*
	 * Allow cancel requests to proceed even if the trim thread has
	 * stopped.
	 */
	if (vd->vdev_trim_thread == NULL && tgt_state != VDEV_TRIM_CANCELED)
		return;

	vdev_trim_change_state(vd, tgt_state, 0, 0, 0);
	vd->vdev_trim_exit_wanted = B_TRUE;

	if (vd_list == NULL) {
		vdev_trim_stop_wait_impl(vd);
	} else {
		ASSERT(MUTEX_HELD(&spa_namespace_lock) ||
		    vd->vdev_spa->spa_export_thread == curthread);
		list_insert_tail(vd_list, vd);
	}
}

/*
 * Requests that all listed vdevs stop trimming.
 */
static void
vdev_trim_stop_all_impl(vdev_t *vd, vdev_trim_state_t tgt_state,
    list_t *vd_list)
{
	if (vd->vdev_ops->vdev_op_leaf && vdev_is_concrete(vd)) {
		mutex_enter(&vd->vdev_trim_lock);
		vdev_trim_stop(vd, tgt_state, vd_list);
		mutex_exit(&vd->vdev_trim_lock);
		return;
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_trim_stop_all_impl(vd->vdev_child[i], tgt_state,
		    vd_list);
	}
}

/*
 * Convenience function to stop trimming of a vdev tree and set all trim
 * thread pointers to NULL.
 */
void
vdev_trim_stop_all(vdev_t *vd, vdev_trim_state_t tgt_state)
{
	spa_t *spa = vd->vdev_spa;
	list_t vd_list;
	vdev_t *vd_l2cache;

	ASSERT(MUTEX_HELD(&spa_namespace_lock) ||
	    spa->spa_export_thread == curthread);

	list_create(&vd_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_trim_node));

	vdev_trim_stop_all_impl(vd, tgt_state, &vd_list);

	/*
	 * Iterate over cache devices and request stop trimming the
	 * whole device in case we export the pool or remove the cache
	 * device prematurely.
	 */
	for (int i = 0; i < spa->spa_l2cache.sav_count; i++) {
		vd_l2cache = spa->spa_l2cache.sav_vdevs[i];
		vdev_trim_stop_all_impl(vd_l2cache, tgt_state, &vd_list);
	}

	vdev_trim_stop_wait(spa, &vd_list);

	if (vd->vdev_spa->spa_sync_on) {
		/* Make sure that our state has been synced to disk */
		txg_wait_synced(spa_get_dsl(vd->vdev_spa), 0);
	}

	list_destroy(&vd_list);
}

/*
 * Conditionally restarts a manual TRIM given its on-disk state.
 */
void
vdev_trim_restart(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock) ||
	    vd->vdev_spa->spa_load_thread == curthread);
	ASSERT(!spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER));

	if (vd->vdev_leaf_zap != 0) {
		mutex_enter(&vd->vdev_trim_lock);
		uint64_t trim_state = VDEV_TRIM_NONE;
		int err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_STATE,
		    sizeof (trim_state), 1, &trim_state);
		ASSERT(err == 0 || err == ENOENT);
		vd->vdev_trim_state = trim_state;

		uint64_t timestamp = 0;
		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_TRIM_ACTION_TIME,
		    sizeof (timestamp), 1, &timestamp);
		ASSERT(err == 0 || err == ENOENT);
		vd->vdev_trim_action_time = timestamp;

		if ((vd->vdev_trim_state == VDEV_TRIM_SUSPENDED ||
		    vd->vdev_offline) && !vd->vdev_top->vdev_rz_expanding) {
			/* load progress for reporting, but don't resume */
			VERIFY0(vdev_trim_load(vd));
		} else if (vd->vdev_trim_state == VDEV_TRIM_ACTIVE &&
		    vdev_writeable(vd) && !vd->vdev_top->vdev_removing &&
		    !vd->vdev_top->vdev_rz_expanding &&
		    vd->vdev_trim_thread == NULL) {
			VERIFY0(vdev_trim_load(vd));
			vdev_trim(vd, vd->vdev_trim_rate,
			    vd->vdev_trim_partial, vd->vdev_trim_secure);
		}

		mutex_exit(&vd->vdev_trim_lock);
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_trim_restart(vd->vdev_child[i]);
	}
}

/*
 * Used by the automatic TRIM when ZFS_DEBUG_TRIM is set to verify that
 * every TRIM range is contained within ms_allocatable.
 */
static void
vdev_trim_range_verify(void *arg, uint64_t start, uint64_t size)
{
	trim_args_t *ta = arg;
	metaslab_t *msp = ta->trim_msp;

	VERIFY3B(msp->ms_loaded, ==, B_TRUE);
	VERIFY3U(msp->ms_disabled, >, 0);
	VERIFY(zfs_range_tree_contains(msp->ms_allocatable, start, size));
}

/*
 * Each automatic TRIM thread is responsible for managing the trimming of a
 * top-level vdev in the pool.  No automatic TRIM state is maintained on-disk.
 *
 * N.B. This behavior is different from a manual TRIM where a thread
 * is created for each leaf vdev, instead of each top-level vdev.
 */
static __attribute__((noreturn)) void
vdev_autotrim_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	int shift = 0;

	mutex_enter(&vd->vdev_autotrim_lock);
	ASSERT3P(vd->vdev_top, ==, vd);
	ASSERT3P(vd->vdev_autotrim_thread, !=, NULL);
	mutex_exit(&vd->vdev_autotrim_lock);
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	while (!vdev_autotrim_should_stop(vd)) {
		int txgs_per_trim = MAX(zfs_trim_txg_batch, 1);
		uint64_t extent_bytes_max = zfs_trim_extent_bytes_max;
		uint64_t extent_bytes_min = zfs_trim_extent_bytes_min;

		/*
		 * All of the metaslabs are divided in to groups of size
		 * num_metaslabs / zfs_trim_txg_batch.  Each of these groups
		 * is composed of metaslabs which are spread evenly over the
		 * device.
		 *
		 * For example, when zfs_trim_txg_batch = 32 (default) then
		 * group 0 will contain metaslabs 0, 32, 64, ...;
		 * group 1 will contain metaslabs 1, 33, 65, ...;
		 * group 2 will contain metaslabs 2, 34, 66, ...; and so on.
		 *
		 * On each pass through the while() loop one of these groups
		 * is selected.  This is accomplished by using a shift value
		 * to select the starting metaslab, then striding over the
		 * metaslabs using the zfs_trim_txg_batch size.  This is
		 * done to accomplish two things.
		 *
		 * 1) By dividing the metaslabs in to groups, and making sure
		 *    that each group takes a minimum of one txg to process.
		 *    Then zfs_trim_txg_batch controls the minimum number of
		 *    txgs which must occur before a metaslab is revisited.
		 *
		 * 2) Selecting non-consecutive metaslabs distributes the
		 *    TRIM commands for a group evenly over the entire device.
		 *    This can be advantageous for certain types of devices.
		 */
		for (uint64_t i = shift % txgs_per_trim; i < vd->vdev_ms_count;
		    i += txgs_per_trim) {
			metaslab_t *msp = vd->vdev_ms[i];
			zfs_range_tree_t *trim_tree;
			boolean_t issued_trim = B_FALSE;
			boolean_t wait_aborted = B_FALSE;

			spa_config_exit(spa, SCL_CONFIG, FTAG);
			metaslab_disable(msp);
			spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

			mutex_enter(&msp->ms_lock);

			/*
			 * Skip the metaslab when it has never been allocated
			 * or when there are no recent frees to trim.
			 */
			if (msp->ms_sm == NULL ||
			    zfs_range_tree_is_empty(msp->ms_trim)) {
				mutex_exit(&msp->ms_lock);
				metaslab_enable(msp, B_FALSE, B_FALSE);
				continue;
			}

			/*
			 * Skip the metaslab when it has already been disabled.
			 * This may happen when a manual TRIM or initialize
			 * operation is running concurrently.  In the case
			 * of a manual TRIM, the ms_trim tree will have been
			 * vacated.  Only ranges added after the manual TRIM
			 * disabled the metaslab will be included in the tree.
			 * These will be processed when the automatic TRIM
			 * next revisits this metaslab.
			 */
			if (msp->ms_disabled > 1) {
				mutex_exit(&msp->ms_lock);
				metaslab_enable(msp, B_FALSE, B_FALSE);
				continue;
			}

			/*
			 * Allocate an empty range tree which is swapped in
			 * for the existing ms_trim tree while it is processed.
			 */
			trim_tree = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64,
			    NULL, 0, 0);
			zfs_range_tree_swap(&msp->ms_trim, &trim_tree);
			ASSERT(zfs_range_tree_is_empty(msp->ms_trim));

			/*
			 * There are two cases when constructing the per-vdev
			 * trim trees for a metaslab.  If the top-level vdev
			 * has no children then it is also a leaf and should
			 * be trimmed.  Otherwise our children are the leaves
			 * and a trim tree should be constructed for each.
			 */
			trim_args_t *tap;
			uint64_t children = vd->vdev_children;
			if (children == 0) {
				children = 1;
				tap = kmem_zalloc(sizeof (trim_args_t) *
				    children, KM_SLEEP);
				tap[0].trim_vdev = vd;
			} else {
				tap = kmem_zalloc(sizeof (trim_args_t) *
				    children, KM_SLEEP);

				for (uint64_t c = 0; c < children; c++) {
					tap[c].trim_vdev = vd->vdev_child[c];
				}
			}

			for (uint64_t c = 0; c < children; c++) {
				trim_args_t *ta = &tap[c];
				vdev_t *cvd = ta->trim_vdev;

				ta->trim_msp = msp;
				ta->trim_extent_bytes_max = extent_bytes_max;
				ta->trim_extent_bytes_min = extent_bytes_min;
				ta->trim_type = TRIM_TYPE_AUTO;
				ta->trim_flags = 0;

				if (cvd->vdev_detached ||
				    !vdev_writeable(cvd) ||
				    !cvd->vdev_has_trim ||
				    cvd->vdev_trim_thread != NULL) {
					continue;
				}

				/*
				 * When a device has an attached hot spare, or
				 * is being replaced it will not be trimmed.
				 * This is done to avoid adding additional
				 * stress to a potentially unhealthy device,
				 * and to minimize the required rebuild time.
				 */
				if (!cvd->vdev_ops->vdev_op_leaf)
					continue;

				ta->trim_tree = zfs_range_tree_create(NULL,
				    ZFS_RANGE_SEG64, NULL, 0, 0);
				zfs_range_tree_walk(trim_tree,
				    vdev_trim_range_add, ta);
			}

			mutex_exit(&msp->ms_lock);
			spa_config_exit(spa, SCL_CONFIG, FTAG);

			/*
			 * Issue the TRIM I/Os for all ranges covered by the
			 * TRIM trees.  These ranges are safe to TRIM because
			 * no new allocations will be performed until the call
			 * to metaslab_enabled() below.
			 */
			for (uint64_t c = 0; c < children; c++) {
				trim_args_t *ta = &tap[c];

				/*
				 * Always yield to a manual TRIM if one has
				 * been started for the child vdev.
				 */
				if (ta->trim_tree == NULL ||
				    ta->trim_vdev->vdev_trim_thread != NULL) {
					continue;
				}

				/*
				 * After this point metaslab_enable() must be
				 * called with the sync flag set.  This is done
				 * here because vdev_trim_ranges() is allowed
				 * to be interrupted (EINTR) before issuing all
				 * of the required TRIM I/Os.
				 */
				issued_trim = B_TRUE;

				int error = vdev_trim_ranges(ta);
				if (error)
					break;
			}

			/*
			 * Verify every range which was trimmed is still
			 * contained within the ms_allocatable tree.
			 */
			if (zfs_flags & ZFS_DEBUG_TRIM) {
				mutex_enter(&msp->ms_lock);
				VERIFY0(metaslab_load(msp));
				VERIFY3P(tap[0].trim_msp, ==, msp);
				zfs_range_tree_walk(trim_tree,
				    vdev_trim_range_verify, &tap[0]);
				mutex_exit(&msp->ms_lock);
			}

			zfs_range_tree_vacate(trim_tree, NULL, NULL);
			zfs_range_tree_destroy(trim_tree);

			/*
			 * Wait for couples of kicks, to ensure the trim io is
			 * synced. If the wait is aborted due to
			 * vdev_autotrim_exit_wanted, we need to signal
			 * metaslab_enable() to wait for sync.
			 */
			if (issued_trim) {
				wait_aborted = vdev_autotrim_wait_kick(vd,
				    TXG_CONCURRENT_STATES + TXG_DEFER_SIZE);
			}

			metaslab_enable(msp, wait_aborted, B_FALSE);
			spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

			for (uint64_t c = 0; c < children; c++) {
				trim_args_t *ta = &tap[c];

				if (ta->trim_tree == NULL)
					continue;

				zfs_range_tree_vacate(ta->trim_tree, NULL,
				    NULL);
				zfs_range_tree_destroy(ta->trim_tree);
			}

			kmem_free(tap, sizeof (trim_args_t) * children);

			if (vdev_autotrim_should_stop(vd))
				break;
		}

		spa_config_exit(spa, SCL_CONFIG, FTAG);

		vdev_autotrim_wait_kick(vd, 1);

		shift++;
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	}

	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		mutex_enter(&cvd->vdev_trim_io_lock);

		while (cvd->vdev_trim_inflight[1] > 0) {
			cv_wait(&cvd->vdev_trim_io_cv,
			    &cvd->vdev_trim_io_lock);
		}
		mutex_exit(&cvd->vdev_trim_io_lock);
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	/*
	 * When exiting because the autotrim property was set to off, then
	 * abandon any unprocessed ms_trim ranges to reclaim the memory.
	 */
	if (spa_get_autotrim(spa) == SPA_AUTOTRIM_OFF) {
		for (uint64_t i = 0; i < vd->vdev_ms_count; i++) {
			metaslab_t *msp = vd->vdev_ms[i];

			mutex_enter(&msp->ms_lock);
			zfs_range_tree_vacate(msp->ms_trim, NULL, NULL);
			mutex_exit(&msp->ms_lock);
		}
	}

	mutex_enter(&vd->vdev_autotrim_lock);
	ASSERT(vd->vdev_autotrim_thread != NULL);
	vd->vdev_autotrim_thread = NULL;
	cv_broadcast(&vd->vdev_autotrim_cv);
	mutex_exit(&vd->vdev_autotrim_lock);

	thread_exit();
}

/*
 * Starts an autotrim thread, if needed, for each top-level vdev which can be
 * trimmed.  A top-level vdev which has been evacuated will never be trimmed.
 */
void
vdev_autotrim(spa_t *spa)
{
	vdev_t *root_vd = spa->spa_root_vdev;

	for (uint64_t i = 0; i < root_vd->vdev_children; i++) {
		vdev_t *tvd = root_vd->vdev_child[i];

		mutex_enter(&tvd->vdev_autotrim_lock);
		if (vdev_writeable(tvd) && !tvd->vdev_removing &&
		    tvd->vdev_autotrim_thread == NULL &&
		    !tvd->vdev_rz_expanding) {
			ASSERT3P(tvd->vdev_top, ==, tvd);

			tvd->vdev_autotrim_thread = thread_create(NULL, 0,
			    vdev_autotrim_thread, tvd, 0, &p0, TS_RUN,
			    maxclsyspri);
			ASSERT(tvd->vdev_autotrim_thread != NULL);
		}
		mutex_exit(&tvd->vdev_autotrim_lock);
	}
}

/*
 * Wait for the vdev_autotrim_thread associated with the passed top-level
 * vdev to be terminated (canceled or stopped).
 */
void
vdev_autotrim_stop_wait(vdev_t *tvd)
{
	mutex_enter(&tvd->vdev_autotrim_lock);
	if (tvd->vdev_autotrim_thread != NULL) {
		tvd->vdev_autotrim_exit_wanted = B_TRUE;
		cv_broadcast(&tvd->vdev_autotrim_kick_cv);
		cv_wait(&tvd->vdev_autotrim_cv,
		    &tvd->vdev_autotrim_lock);

		ASSERT3P(tvd->vdev_autotrim_thread, ==, NULL);
		tvd->vdev_autotrim_exit_wanted = B_FALSE;
	}
	mutex_exit(&tvd->vdev_autotrim_lock);
}

void
vdev_autotrim_kick(spa_t *spa)
{
	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	vdev_t *root_vd = spa->spa_root_vdev;
	vdev_t *tvd;

	for (uint64_t i = 0; i < root_vd->vdev_children; i++) {
		tvd = root_vd->vdev_child[i];

		mutex_enter(&tvd->vdev_autotrim_lock);
		if (tvd->vdev_autotrim_thread != NULL)
			cv_broadcast(&tvd->vdev_autotrim_kick_cv);
		mutex_exit(&tvd->vdev_autotrim_lock);
	}
}

/*
 * Wait for all of the vdev_autotrim_thread associated with the pool to
 * be terminated (canceled or stopped).
 */
void
vdev_autotrim_stop_all(spa_t *spa)
{
	vdev_t *root_vd = spa->spa_root_vdev;

	for (uint64_t i = 0; i < root_vd->vdev_children; i++)
		vdev_autotrim_stop_wait(root_vd->vdev_child[i]);
}

/*
 * Conditionally restart all of the vdev_autotrim_thread's for the pool.
 */
void
vdev_autotrim_restart(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock) ||
	    spa->spa_load_thread == curthread);
	if (spa->spa_autotrim)
		vdev_autotrim(spa);
}

static __attribute__((noreturn)) void
vdev_trim_l2arc_thread(void *arg)
{
	vdev_t		*vd = arg;
	spa_t		*spa = vd->vdev_spa;
	l2arc_dev_t	*dev = l2arc_vdev_get(vd);
	trim_args_t	ta = {0};
	range_seg64_t 	physical_rs;

	ASSERT(vdev_is_concrete(vd));
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	vd->vdev_trim_last_offset = 0;
	vd->vdev_trim_rate = 0;
	vd->vdev_trim_partial = 0;
	vd->vdev_trim_secure = 0;

	ta.trim_vdev = vd;
	ta.trim_tree = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0);
	ta.trim_type = TRIM_TYPE_MANUAL;
	ta.trim_extent_bytes_max = zfs_trim_extent_bytes_max;
	ta.trim_extent_bytes_min = SPA_MINBLOCKSIZE;
	ta.trim_flags = 0;

	physical_rs.rs_start = vd->vdev_trim_bytes_done = 0;
	physical_rs.rs_end = vd->vdev_trim_bytes_est =
	    vdev_get_min_asize(vd);

	zfs_range_tree_add(ta.trim_tree, physical_rs.rs_start,
	    physical_rs.rs_end - physical_rs.rs_start);

	mutex_enter(&vd->vdev_trim_lock);
	vdev_trim_change_state(vd, VDEV_TRIM_ACTIVE, 0, 0, 0);
	mutex_exit(&vd->vdev_trim_lock);

	(void) vdev_trim_ranges(&ta);

	spa_config_exit(spa, SCL_CONFIG, FTAG);
	mutex_enter(&vd->vdev_trim_io_lock);
	while (vd->vdev_trim_inflight[TRIM_TYPE_MANUAL] > 0) {
		cv_wait(&vd->vdev_trim_io_cv, &vd->vdev_trim_io_lock);
	}
	mutex_exit(&vd->vdev_trim_io_lock);

	zfs_range_tree_vacate(ta.trim_tree, NULL, NULL);
	zfs_range_tree_destroy(ta.trim_tree);

	mutex_enter(&vd->vdev_trim_lock);
	if (!vd->vdev_trim_exit_wanted && vdev_writeable(vd)) {
		vdev_trim_change_state(vd, VDEV_TRIM_COMPLETE,
		    vd->vdev_trim_rate, vd->vdev_trim_partial,
		    vd->vdev_trim_secure);
	}
	ASSERT(vd->vdev_trim_thread != NULL ||
	    vd->vdev_trim_inflight[TRIM_TYPE_MANUAL] == 0);

	/*
	 * Drop the vdev_trim_lock while we sync out the txg since it's
	 * possible that a device might be trying to come online and
	 * must check to see if it needs to restart a trim. That thread
	 * will be holding the spa_config_lock which would prevent the
	 * txg_wait_synced from completing. Same strategy as in
	 * vdev_trim_thread().
	 */
	mutex_exit(&vd->vdev_trim_lock);
	txg_wait_synced(spa_get_dsl(vd->vdev_spa), 0);
	mutex_enter(&vd->vdev_trim_lock);

	/*
	 * Update the header of the cache device here, before
	 * broadcasting vdev_trim_cv which may lead to the removal
	 * of the device. The same applies for setting l2ad_trim_all to
	 * false.
	 */
	spa_config_enter(vd->vdev_spa, SCL_L2ARC, vd,
	    RW_READER);
	memset(dev->l2ad_dev_hdr, 0, dev->l2ad_dev_hdr_asize);
	l2arc_dev_hdr_update(dev);
	spa_config_exit(vd->vdev_spa, SCL_L2ARC, vd);

	vd->vdev_trim_thread = NULL;
	if (vd->vdev_trim_state == VDEV_TRIM_COMPLETE)
		dev->l2ad_trim_all = B_FALSE;

	cv_broadcast(&vd->vdev_trim_cv);
	mutex_exit(&vd->vdev_trim_lock);

	thread_exit();
}

/*
 * Punches out TRIM threads for the L2ARC devices in a spa and assigns them
 * to vd->vdev_trim_thread variable. This facilitates the management of
 * trimming the whole cache device using TRIM_TYPE_MANUAL upon addition
 * to a pool or pool creation or when the header of the device is invalid.
 */
void
vdev_trim_l2arc(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	/*
	 * Locate the spa's l2arc devices and kick off TRIM threads.
	 */
	for (int i = 0; i < spa->spa_l2cache.sav_count; i++) {
		vdev_t *vd = spa->spa_l2cache.sav_vdevs[i];
		l2arc_dev_t *dev = l2arc_vdev_get(vd);

		if (dev == NULL || !dev->l2ad_trim_all) {
			/*
			 * Don't attempt TRIM if the vdev is UNAVAIL or if the
			 * cache device was not marked for whole device TRIM
			 * (ie l2arc_trim_ahead = 0, or the L2ARC device header
			 * is valid with trim_state = VDEV_TRIM_COMPLETE and
			 * l2ad_log_entries > 0).
			 */
			continue;
		}

		mutex_enter(&vd->vdev_trim_lock);
		ASSERT(vd->vdev_ops->vdev_op_leaf);
		ASSERT(vdev_is_concrete(vd));
		ASSERT3P(vd->vdev_trim_thread, ==, NULL);
		ASSERT(!vd->vdev_detached);
		ASSERT(!vd->vdev_trim_exit_wanted);
		ASSERT(!vd->vdev_top->vdev_removing);
		vdev_trim_change_state(vd, VDEV_TRIM_ACTIVE, 0, 0, 0);
		vd->vdev_trim_thread = thread_create(NULL, 0,
		    vdev_trim_l2arc_thread, vd, 0, &p0, TS_RUN, maxclsyspri);
		mutex_exit(&vd->vdev_trim_lock);
	}
}

/*
 * A wrapper which calls vdev_trim_ranges(). It is intended to be called
 * on leaf vdevs.
 */
int
vdev_trim_simple(vdev_t *vd, uint64_t start, uint64_t size)
{
	trim_args_t ta = {0};
	range_seg64_t physical_rs;
	int error;
	physical_rs.rs_start = start;
	physical_rs.rs_end = start + size;

	ASSERT(vdev_is_concrete(vd));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(!vd->vdev_detached);
	ASSERT(!vd->vdev_top->vdev_removing);
	ASSERT(!vd->vdev_top->vdev_rz_expanding);

	ta.trim_vdev = vd;
	ta.trim_tree = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0);
	ta.trim_type = TRIM_TYPE_SIMPLE;
	ta.trim_extent_bytes_max = zfs_trim_extent_bytes_max;
	ta.trim_extent_bytes_min = SPA_MINBLOCKSIZE;
	ta.trim_flags = 0;

	ASSERT3U(physical_rs.rs_end, >=, physical_rs.rs_start);

	if (physical_rs.rs_end > physical_rs.rs_start) {
		zfs_range_tree_add(ta.trim_tree, physical_rs.rs_start,
		    physical_rs.rs_end - physical_rs.rs_start);
	} else {
		ASSERT3U(physical_rs.rs_end, ==, physical_rs.rs_start);
	}

	error = vdev_trim_ranges(&ta);

	mutex_enter(&vd->vdev_trim_io_lock);
	while (vd->vdev_trim_inflight[TRIM_TYPE_SIMPLE] > 0) {
		cv_wait(&vd->vdev_trim_io_cv, &vd->vdev_trim_io_lock);
	}
	mutex_exit(&vd->vdev_trim_io_lock);

	zfs_range_tree_vacate(ta.trim_tree, NULL, NULL);
	zfs_range_tree_destroy(ta.trim_tree);

	return (error);
}

EXPORT_SYMBOL(vdev_trim);
EXPORT_SYMBOL(vdev_trim_stop);
EXPORT_SYMBOL(vdev_trim_stop_all);
EXPORT_SYMBOL(vdev_trim_stop_wait);
EXPORT_SYMBOL(vdev_trim_restart);
EXPORT_SYMBOL(vdev_autotrim);
EXPORT_SYMBOL(vdev_autotrim_stop_all);
EXPORT_SYMBOL(vdev_autotrim_stop_wait);
EXPORT_SYMBOL(vdev_autotrim_restart);
EXPORT_SYMBOL(vdev_trim_l2arc);
EXPORT_SYMBOL(vdev_trim_simple);

ZFS_MODULE_PARAM(zfs_trim, zfs_trim_, extent_bytes_max, UINT, ZMOD_RW,
	"Max size of TRIM commands, larger will be split");

ZFS_MODULE_PARAM(zfs_trim, zfs_trim_, extent_bytes_min, UINT, ZMOD_RW,
	"Min size of TRIM commands, smaller will be skipped");

ZFS_MODULE_PARAM(zfs_trim, zfs_trim_, metaslab_skip, UINT, ZMOD_RW,
	"Skip metaslabs which have never been initialized");

ZFS_MODULE_PARAM(zfs_trim, zfs_trim_, txg_batch, UINT, ZMOD_RW,
	"Min number of txgs to aggregate frees before issuing TRIM");

ZFS_MODULE_PARAM(zfs_trim, zfs_trim_, queue_limit, UINT, ZMOD_RW,
	"Max queued TRIMs outstanding per leaf vdev");
