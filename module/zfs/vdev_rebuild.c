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
 *
 * Copyright (c) 2018, Intel Corporation.
 * Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
 * Copyright (c) 2022 Hewlett Packard Enterprise Development LP.
 */

#include <sys/vdev_impl.h>
#include <sys/vdev_draid.h>
#include <sys/dsl_scan.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/vdev_rebuild.h>
#include <sys/zio.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/arc_impl.h>
#include <sys/zap.h>

/*
 * This file contains the sequential reconstruction implementation for
 * resilvering.  This form of resilvering is internally referred to as device
 * rebuild to avoid conflating it with the traditional healing reconstruction
 * performed by the dsl scan code.
 *
 * When replacing a device, or scrubbing the pool, ZFS has historically used
 * a process called resilvering which is a form of healing reconstruction.
 * This approach has the advantage that as blocks are read from disk their
 * checksums can be immediately verified and the data repaired.  Unfortunately,
 * it also results in a random IO pattern to the disk even when extra care
 * is taken to sequentialize the IO as much as possible.  This substantially
 * increases the time required to resilver the pool and restore redundancy.
 *
 * For mirrored devices it's possible to implement an alternate sequential
 * reconstruction strategy when resilvering.  Sequential reconstruction
 * behaves like a traditional RAID rebuild and reconstructs a device in LBA
 * order without verifying the checksum.  After this phase completes a second
 * scrub phase is started to verify all of the checksums.  This two phase
 * process will take longer than the healing reconstruction described above.
 * However, it has that advantage that after the reconstruction first phase
 * completes redundancy has been restored.  At this point the pool can incur
 * another device failure without risking data loss.
 *
 * There are a few noteworthy limitations and other advantages of resilvering
 * using sequential reconstruction vs healing reconstruction.
 *
 * Limitations:
 *
 *   - Sequential reconstruction is not possible on RAIDZ due to its
 *     variable stripe width.  Note dRAID uses a fixed stripe width which
 *     avoids this issue, but comes at the expense of some usable capacity.
 *
 *   - Block checksums are not verified during sequential reconstruction.
 *     Similar to traditional RAID the parity/mirror data is reconstructed
 *     but cannot be immediately double checked.  For this reason when the
 *     last active resilver completes the pool is automatically scrubbed
 *     by default.
 *
 *   - Deferred resilvers using sequential reconstruction are not currently
 *     supported.  When adding another vdev to an active top-level resilver
 *     it must be restarted.
 *
 * Advantages:
 *
 *   - Sequential reconstruction is performed in LBA order which may be faster
 *     than healing reconstruction particularly when using HDDs (or
 *     especially with SMR devices).  Only allocated capacity is resilvered.
 *
 *   - Sequential reconstruction is not constrained by ZFS block boundaries.
 *     This allows it to issue larger IOs to disk which span multiple blocks
 *     allowing all of these logical blocks to be repaired with a single IO.
 *
 *   - Unlike a healing resilver or scrub which are pool wide operations,
 *     sequential reconstruction is handled by the top-level vdevs.  This
 *     allows for it to be started or canceled on a top-level vdev without
 *     impacting any other top-level vdevs in the pool.
 *
 *   - Data only referenced by a pool checkpoint will be repaired because
 *     that space is reflected in the space maps.  This differs for a
 *     healing resilver or scrub which will not repair that data.
 */


/*
 * Size of rebuild reads; defaults to 1MiB per data disk and is capped at
 * SPA_MAXBLOCKSIZE.
 */
static uint64_t zfs_rebuild_max_segment = 1024 * 1024;

/*
 * Maximum number of parallelly executed bytes per leaf vdev caused by a
 * sequential resilver.  We attempt to strike a balance here between keeping
 * the vdev queues full of I/Os at all times and not overflowing the queues
 * to cause long latency, which would cause long txg sync times.
 *
 * A large default value can be safely used here because the default target
 * segment size is also large (zfs_rebuild_max_segment=1M).  This helps keep
 * the queue depth short.
 *
 * 64MB was observed to deliver the best performance and set as the default.
 * Testing was performed with a 106-drive dRAID HDD pool (draid2:11d:106c)
 * and a rebuild rate of 1.2GB/s was measured to the distribute spare.
 * Smaller values were unable to fully saturate the available pool I/O.
 */
static uint64_t zfs_rebuild_vdev_limit = 64 << 20;

/*
 * Automatically start a pool scrub when the last active sequential resilver
 * completes in order to verify the checksums of all blocks which have been
 * resilvered. This option is enabled by default and is strongly recommended.
 */
static int zfs_rebuild_scrub_enabled = 1;

/*
 * For vdev_rebuild_initiate_sync() and vdev_rebuild_reset_sync().
 */
static __attribute__((noreturn)) void vdev_rebuild_thread(void *arg);
static void vdev_rebuild_reset_sync(void *arg, dmu_tx_t *tx);

/*
 * Clear the per-vdev rebuild bytes value for a vdev tree.
 */
static void
clear_rebuild_bytes(vdev_t *vd)
{
	vdev_stat_t *vs = &vd->vdev_stat;

	for (uint64_t i = 0; i < vd->vdev_children; i++)
		clear_rebuild_bytes(vd->vdev_child[i]);

	mutex_enter(&vd->vdev_stat_lock);
	vs->vs_rebuild_processed = 0;
	mutex_exit(&vd->vdev_stat_lock);
}

/*
 * Determines whether a vdev_rebuild_thread() should be stopped.
 */
static boolean_t
vdev_rebuild_should_stop(vdev_t *vd)
{
	return (!vdev_writeable(vd) || vd->vdev_removing ||
	    vd->vdev_rebuild_exit_wanted ||
	    vd->vdev_rebuild_cancel_wanted ||
	    vd->vdev_rebuild_reset_wanted);
}

/*
 * Determine if the rebuild should be canceled.  This may happen when all
 * vdevs with MISSING DTLs are detached.
 */
static boolean_t
vdev_rebuild_should_cancel(vdev_t *vd)
{
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

	if (!vdev_resilver_needed(vd, &vrp->vrp_min_txg, &vrp->vrp_max_txg))
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * The sync task for updating the on-disk state of a rebuild.  This is
 * scheduled by vdev_rebuild_range().
 */
static void
vdev_rebuild_update_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
	uint64_t txg = dmu_tx_get_txg(tx);

	mutex_enter(&vd->vdev_rebuild_lock);

	if (vr->vr_scan_offset[txg & TXG_MASK] > 0) {
		vrp->vrp_last_offset = vr->vr_scan_offset[txg & TXG_MASK];
		vr->vr_scan_offset[txg & TXG_MASK] = 0;
	}

	vrp->vrp_scan_time_ms = vr->vr_prev_scan_time_ms +
	    NSEC2MSEC(gethrtime() - vr->vr_pass_start_time);

	VERIFY0(zap_update(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp, tx));

	mutex_exit(&vd->vdev_rebuild_lock);
}

/*
 * Initialize the on-disk state for a new rebuild, start the rebuild thread.
 */
static void
vdev_rebuild_initiate_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

	ASSERT(vd->vdev_rebuilding);

	spa_feature_incr(vd->vdev_spa, SPA_FEATURE_DEVICE_REBUILD, tx);

	mutex_enter(&vd->vdev_rebuild_lock);
	memset(vrp, 0, sizeof (uint64_t) * REBUILD_PHYS_ENTRIES);
	vrp->vrp_rebuild_state = VDEV_REBUILD_ACTIVE;
	vrp->vrp_min_txg = 0;
	vrp->vrp_max_txg = dmu_tx_get_txg(tx);
	vrp->vrp_start_time = gethrestime_sec();
	vrp->vrp_scan_time_ms = 0;
	vr->vr_prev_scan_time_ms = 0;

	/*
	 * Rebuilds are currently only used when replacing a device, in which
	 * case there must be DTL_MISSING entries.  In the future, we could
	 * allow rebuilds to be used in a way similar to a scrub.  This would
	 * be useful because it would allow us to rebuild the space used by
	 * pool checkpoints.
	 */
	VERIFY(vdev_resilver_needed(vd, &vrp->vrp_min_txg, &vrp->vrp_max_txg));

	VERIFY0(zap_update(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp, tx));

	spa_history_log_internal(spa, "rebuild", tx,
	    "vdev_id=%llu vdev_guid=%llu started",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)vd->vdev_guid);

	ASSERT3P(vd->vdev_rebuild_thread, ==, NULL);
	vd->vdev_rebuild_thread = thread_create(NULL, 0,
	    vdev_rebuild_thread, vd, 0, &p0, TS_RUN, maxclsyspri);

	mutex_exit(&vd->vdev_rebuild_lock);
}

static void
vdev_rebuild_log_notify(spa_t *spa, vdev_t *vd, const char *name)
{
	nvlist_t *aux = fnvlist_alloc();

	fnvlist_add_string(aux, ZFS_EV_RESILVER_TYPE, "sequential");
	spa_event_notify(spa, vd, aux, name);
	nvlist_free(aux);
}

/*
 * Called to request that a new rebuild be started.  The feature will remain
 * active for the duration of the rebuild, then revert to the enabled state.
 */
static void
vdev_rebuild_initiate(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(vd->vdev_top == vd);
	ASSERT(MUTEX_HELD(&vd->vdev_rebuild_lock));
	ASSERT(!vd->vdev_rebuilding);

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

	vd->vdev_rebuilding = B_TRUE;

	dsl_sync_task_nowait(spa_get_dsl(spa), vdev_rebuild_initiate_sync,
	    (void *)(uintptr_t)vd->vdev_id, tx);
	dmu_tx_commit(tx);

	vdev_rebuild_log_notify(spa, vd, ESC_ZFS_RESILVER_START);
}

/*
 * Update the on-disk state to completed when a rebuild finishes.
 */
static void
vdev_rebuild_complete_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

	mutex_enter(&vd->vdev_rebuild_lock);

	/*
	 * Handle a second device failure if it occurs after all rebuild I/O
	 * has completed but before this sync task has been executed.
	 */
	if (vd->vdev_rebuild_reset_wanted) {
		mutex_exit(&vd->vdev_rebuild_lock);
		vdev_rebuild_reset_sync(arg, tx);
		return;
	}

	vrp->vrp_rebuild_state = VDEV_REBUILD_COMPLETE;
	vrp->vrp_end_time = gethrestime_sec();

	VERIFY0(zap_update(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp, tx));

	vdev_dtl_reassess(vd, tx->tx_txg, vrp->vrp_max_txg, B_TRUE, B_TRUE);
	spa_feature_decr(vd->vdev_spa, SPA_FEATURE_DEVICE_REBUILD, tx);

	spa_history_log_internal(spa, "rebuild",  tx,
	    "vdev_id=%llu vdev_guid=%llu complete",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)vd->vdev_guid);
	vdev_rebuild_log_notify(spa, vd, ESC_ZFS_RESILVER_FINISH);

	/* Handles detaching of spares */
	spa_async_request(spa, SPA_ASYNC_REBUILD_DONE);
	vd->vdev_rebuilding = B_FALSE;
	mutex_exit(&vd->vdev_rebuild_lock);

	/*
	 * While we're in syncing context take the opportunity to
	 * setup the scrub when there are no more active rebuilds.
	 */
	pool_scan_func_t func = POOL_SCAN_SCRUB;
	if (dsl_scan_setup_check(&func, tx) == 0 &&
	    zfs_rebuild_scrub_enabled) {
		dsl_scan_setup_sync(&func, tx);
	}

	cv_broadcast(&vd->vdev_rebuild_cv);

	/* Clear recent error events (i.e. duplicate events tracking) */
	zfs_ereport_clear(spa, NULL);
}

/*
 * Update the on-disk state to canceled when a rebuild finishes.
 */
static void
vdev_rebuild_cancel_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

	mutex_enter(&vd->vdev_rebuild_lock);
	vrp->vrp_rebuild_state = VDEV_REBUILD_CANCELED;
	vrp->vrp_end_time = gethrestime_sec();

	VERIFY0(zap_update(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp, tx));

	spa_feature_decr(vd->vdev_spa, SPA_FEATURE_DEVICE_REBUILD, tx);

	spa_history_log_internal(spa, "rebuild",  tx,
	    "vdev_id=%llu vdev_guid=%llu canceled",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)vd->vdev_guid);
	vdev_rebuild_log_notify(spa, vd, ESC_ZFS_RESILVER_FINISH);

	vd->vdev_rebuild_cancel_wanted = B_FALSE;
	vd->vdev_rebuilding = B_FALSE;
	mutex_exit(&vd->vdev_rebuild_lock);

	spa_notify_waiters(spa);
	cv_broadcast(&vd->vdev_rebuild_cv);
}

/*
 * Resets the progress of a running rebuild.  This will occur when a new
 * vdev is added to rebuild.
 */
static void
vdev_rebuild_reset_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

	mutex_enter(&vd->vdev_rebuild_lock);

	ASSERT(vrp->vrp_rebuild_state == VDEV_REBUILD_ACTIVE);
	ASSERT3P(vd->vdev_rebuild_thread, ==, NULL);

	vrp->vrp_last_offset = 0;
	vrp->vrp_min_txg = 0;
	vrp->vrp_max_txg = dmu_tx_get_txg(tx);
	vrp->vrp_bytes_scanned = 0;
	vrp->vrp_bytes_issued = 0;
	vrp->vrp_bytes_rebuilt = 0;
	vrp->vrp_bytes_est = 0;
	vrp->vrp_scan_time_ms = 0;
	vr->vr_prev_scan_time_ms = 0;

	/* See vdev_rebuild_initiate_sync comment */
	VERIFY(vdev_resilver_needed(vd, &vrp->vrp_min_txg, &vrp->vrp_max_txg));

	VERIFY0(zap_update(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp, tx));

	spa_history_log_internal(spa, "rebuild",  tx,
	    "vdev_id=%llu vdev_guid=%llu reset",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)vd->vdev_guid);

	vd->vdev_rebuild_reset_wanted = B_FALSE;
	ASSERT(vd->vdev_rebuilding);

	vd->vdev_rebuild_thread = thread_create(NULL, 0,
	    vdev_rebuild_thread, vd, 0, &p0, TS_RUN, maxclsyspri);

	mutex_exit(&vd->vdev_rebuild_lock);
}

/*
 * Clear the last rebuild status.
 */
void
vdev_rebuild_clear_sync(void *arg, dmu_tx_t *tx)
{
	int vdev_id = (uintptr_t)arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *vd = vdev_lookup_top(spa, vdev_id);
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
	objset_t *mos = spa_meta_objset(spa);

	mutex_enter(&vd->vdev_rebuild_lock);

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_DEVICE_REBUILD) ||
	    vrp->vrp_rebuild_state == VDEV_REBUILD_ACTIVE) {
		mutex_exit(&vd->vdev_rebuild_lock);
		return;
	}

	clear_rebuild_bytes(vd);
	memset(vrp, 0, sizeof (uint64_t) * REBUILD_PHYS_ENTRIES);

	if (vd->vdev_top_zap != 0 && zap_contains(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS) == 0) {
		VERIFY0(zap_update(mos, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
		    REBUILD_PHYS_ENTRIES, vrp, tx));
	}

	mutex_exit(&vd->vdev_rebuild_lock);
}

/*
 * The zio_done_func_t callback for each rebuild I/O issued.  It's responsible
 * for updating the rebuild stats and limiting the number of in flight I/Os.
 */
static void
vdev_rebuild_cb(zio_t *zio)
{
	vdev_rebuild_t *vr = zio->io_private;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
	vdev_t *vd = vr->vr_top_vdev;

	mutex_enter(&vr->vr_io_lock);
	if (zio->io_error == ENXIO && !vdev_writeable(vd)) {
		/*
		 * The I/O failed because the top-level vdev was unavailable.
		 * Attempt to roll back to the last completed offset, in order
		 * resume from the correct location if the pool is resumed.
		 * (This works because spa_sync waits on spa_txg_zio before
		 * it runs sync tasks.)
		 */
		uint64_t *off = &vr->vr_scan_offset[zio->io_txg & TXG_MASK];
		*off = MIN(*off, zio->io_offset);
	} else if (zio->io_error) {
		vrp->vrp_errors++;
	}

	abd_free(zio->io_abd);

	ASSERT3U(vr->vr_bytes_inflight, >, 0);
	vr->vr_bytes_inflight -= zio->io_size;
	cv_broadcast(&vr->vr_io_cv);
	mutex_exit(&vr->vr_io_lock);

	spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
}

/*
 * Initialize a block pointer that can be used to read the given segment
 * for sequential rebuild.
 */
static void
vdev_rebuild_blkptr_init(blkptr_t *bp, vdev_t *vd, uint64_t start,
    uint64_t asize)
{
	ASSERT(vd->vdev_ops == &vdev_draid_ops ||
	    vd->vdev_ops == &vdev_mirror_ops ||
	    vd->vdev_ops == &vdev_replacing_ops ||
	    vd->vdev_ops == &vdev_spare_ops);

	uint64_t psize = vd->vdev_ops == &vdev_draid_ops ?
	    vdev_draid_asize_to_psize(vd, asize) : asize;

	BP_ZERO(bp);

	DVA_SET_VDEV(&bp->blk_dva[0], vd->vdev_id);
	DVA_SET_OFFSET(&bp->blk_dva[0], start);
	DVA_SET_GANG(&bp->blk_dva[0], 0);
	DVA_SET_ASIZE(&bp->blk_dva[0], asize);

	BP_SET_BIRTH(bp, TXG_INITIAL, TXG_INITIAL);
	BP_SET_LSIZE(bp, psize);
	BP_SET_PSIZE(bp, psize);
	BP_SET_COMPRESS(bp, ZIO_COMPRESS_OFF);
	BP_SET_CHECKSUM(bp, ZIO_CHECKSUM_OFF);
	BP_SET_TYPE(bp, DMU_OT_NONE);
	BP_SET_LEVEL(bp, 0);
	BP_SET_DEDUP(bp, 0);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);
}

/*
 * Issues a rebuild I/O and takes care of rate limiting the number of queued
 * rebuild I/Os.  The provided start and size must be properly aligned for the
 * top-level vdev type being rebuilt.
 */
static int
vdev_rebuild_range(vdev_rebuild_t *vr, uint64_t start, uint64_t size)
{
	uint64_t ms_id __maybe_unused = vr->vr_scan_msp->ms_id;
	vdev_t *vd = vr->vr_top_vdev;
	spa_t *spa = vd->vdev_spa;
	blkptr_t blk;

	ASSERT3U(ms_id, ==, start >> vd->vdev_ms_shift);
	ASSERT3U(ms_id, ==, (start + size - 1) >> vd->vdev_ms_shift);

	vr->vr_pass_bytes_scanned += size;
	vr->vr_rebuild_phys.vrp_bytes_scanned += size;

	/*
	 * Rebuild the data in this range by constructing a special block
	 * pointer.  It has no relation to any existing blocks in the pool.
	 * However, by disabling checksum verification and issuing a scrub IO
	 * we can reconstruct and repair any children with missing data.
	 */
	vdev_rebuild_blkptr_init(&blk, vd, start, size);
	uint64_t psize = BP_GET_PSIZE(&blk);

	if (!vdev_dtl_need_resilver(vd, &blk.blk_dva[0], psize, TXG_UNKNOWN)) {
		vr->vr_pass_bytes_skipped += size;
		return (0);
	}

	mutex_enter(&vr->vr_io_lock);

	/* Limit in flight rebuild I/Os */
	while (vr->vr_bytes_inflight >= vr->vr_bytes_inflight_max)
		cv_wait(&vr->vr_io_cv, &vr->vr_io_lock);

	vr->vr_bytes_inflight += psize;
	mutex_exit(&vr->vr_io_lock);

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	uint64_t txg = dmu_tx_get_txg(tx);

	spa_config_enter(spa, SCL_STATE_ALL, vd, RW_READER);
	mutex_enter(&vd->vdev_rebuild_lock);

	/* This is the first I/O for this txg. */
	if (vr->vr_scan_offset[txg & TXG_MASK] == 0) {
		vr->vr_scan_offset[txg & TXG_MASK] = start;
		dsl_sync_task_nowait(spa_get_dsl(spa),
		    vdev_rebuild_update_sync,
		    (void *)(uintptr_t)vd->vdev_id, tx);
	}

	/* When exiting write out our progress. */
	if (vdev_rebuild_should_stop(vd)) {
		mutex_enter(&vr->vr_io_lock);
		vr->vr_bytes_inflight -= psize;
		mutex_exit(&vr->vr_io_lock);
		spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
		mutex_exit(&vd->vdev_rebuild_lock);
		dmu_tx_commit(tx);
		return (SET_ERROR(EINTR));
	}
	mutex_exit(&vd->vdev_rebuild_lock);
	dmu_tx_commit(tx);

	vr->vr_scan_offset[txg & TXG_MASK] = start + size;
	vr->vr_pass_bytes_issued += size;
	vr->vr_rebuild_phys.vrp_bytes_issued += size;

	zio_nowait(zio_read(spa->spa_txg_zio[txg & TXG_MASK], spa, &blk,
	    abd_alloc(psize, B_FALSE), psize, vdev_rebuild_cb, vr,
	    ZIO_PRIORITY_REBUILD, ZIO_FLAG_RAW | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_RESILVER, NULL));

	return (0);
}

/*
 * Issues rebuild I/Os for all ranges in the provided vr->vr_tree range tree.
 */
static int
vdev_rebuild_ranges(vdev_rebuild_t *vr)
{
	vdev_t *vd = vr->vr_top_vdev;
	zfs_btree_t *t = &vr->vr_scan_tree->rt_root;
	zfs_btree_index_t idx;
	int error;

	for (range_seg_t *rs = zfs_btree_first(t, &idx); rs != NULL;
	    rs = zfs_btree_next(t, &idx, &idx)) {
		uint64_t start = rs_get_start(rs, vr->vr_scan_tree);
		uint64_t size = rs_get_end(rs, vr->vr_scan_tree) - start;

		/*
		 * zfs_scan_suspend_progress can be set to disable rebuild
		 * progress for testing.  See comment in dsl_scan_sync().
		 */
		while (zfs_scan_suspend_progress &&
		    !vdev_rebuild_should_stop(vd)) {
			delay(hz);
		}

		while (size > 0) {
			uint64_t chunk_size;

			/*
			 * Split range into legally-sized logical chunks
			 * given the constraints of the top-level vdev
			 * being rebuilt (dRAID or mirror).
			 */
			ASSERT3P(vd->vdev_ops, !=, NULL);
			chunk_size = vd->vdev_ops->vdev_op_rebuild_asize(vd,
			    start, size, zfs_rebuild_max_segment);

			error = vdev_rebuild_range(vr, start, chunk_size);
			if (error != 0)
				return (error);

			size -= chunk_size;
			start += chunk_size;
		}
	}

	return (0);
}

/*
 * Calculates the estimated capacity which remains to be scanned.  Since
 * we traverse the pool in metaslab order only allocated capacity beyond
 * the vrp_last_offset need be considered.  All lower offsets must have
 * already been rebuilt and are thus already included in vrp_bytes_scanned.
 */
static void
vdev_rebuild_update_bytes_est(vdev_t *vd, uint64_t ms_id)
{
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
	uint64_t bytes_est = vrp->vrp_bytes_scanned;

	if (vrp->vrp_last_offset < vd->vdev_ms[ms_id]->ms_start)
		return;

	for (uint64_t i = ms_id; i < vd->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_ms[i];

		mutex_enter(&msp->ms_lock);
		bytes_est += metaslab_allocated_space(msp);
		mutex_exit(&msp->ms_lock);
	}

	vrp->vrp_bytes_est = bytes_est;
}

/*
 * Load from disk the top-level vdev's rebuild information.
 */
int
vdev_rebuild_load(vdev_t *vd)
{
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
	spa_t *spa = vd->vdev_spa;
	int err = 0;

	mutex_enter(&vd->vdev_rebuild_lock);
	vd->vdev_rebuilding = B_FALSE;

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_DEVICE_REBUILD)) {
		memset(vrp, 0, sizeof (uint64_t) * REBUILD_PHYS_ENTRIES);
		mutex_exit(&vd->vdev_rebuild_lock);
		return (SET_ERROR(ENOTSUP));
	}

	ASSERT(vd->vdev_top == vd);

	err = zap_lookup(spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp);

	/*
	 * A missing or damaged VDEV_TOP_ZAP_VDEV_REBUILD_PHYS should
	 * not prevent a pool from being imported.  Clear the rebuild
	 * status allowing a new resilver/rebuild to be started.
	 */
	if (err == ENOENT || err == EOVERFLOW || err == ECKSUM) {
		memset(vrp, 0, sizeof (uint64_t) * REBUILD_PHYS_ENTRIES);
	} else if (err) {
		mutex_exit(&vd->vdev_rebuild_lock);
		return (err);
	}

	vr->vr_prev_scan_time_ms = vrp->vrp_scan_time_ms;
	vr->vr_top_vdev = vd;

	mutex_exit(&vd->vdev_rebuild_lock);

	return (0);
}

/*
 * Each scan thread is responsible for rebuilding a top-level vdev.  The
 * rebuild progress in tracked on-disk in VDEV_TOP_ZAP_VDEV_REBUILD_PHYS.
 */
static __attribute__((noreturn)) void
vdev_rebuild_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	int error = 0;

	/*
	 * If there's a scrub in process request that it be stopped.  This
	 * is not required for a correct rebuild, but we do want rebuilds to
	 * emulate the resilver behavior as much as possible.
	 */
	dsl_pool_t *dsl = spa_get_dsl(spa);
	if (dsl_scan_scrubbing(dsl))
		dsl_scan_cancel(dsl);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	mutex_enter(&vd->vdev_rebuild_lock);

	ASSERT3P(vd->vdev_top, ==, vd);
	ASSERT3P(vd->vdev_rebuild_thread, !=, NULL);
	ASSERT(vd->vdev_rebuilding);
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_DEVICE_REBUILD));
	ASSERT3B(vd->vdev_rebuild_cancel_wanted, ==, B_FALSE);

	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;
	vr->vr_top_vdev = vd;
	vr->vr_scan_msp = NULL;
	vr->vr_scan_tree = range_tree_create(NULL, RANGE_SEG64, NULL, 0, 0);
	mutex_init(&vr->vr_io_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vr->vr_io_cv, NULL, CV_DEFAULT, NULL);

	vr->vr_pass_start_time = gethrtime();
	vr->vr_pass_bytes_scanned = 0;
	vr->vr_pass_bytes_issued = 0;
	vr->vr_pass_bytes_skipped = 0;

	uint64_t update_est_time = gethrtime();
	vdev_rebuild_update_bytes_est(vd, 0);

	clear_rebuild_bytes(vr->vr_top_vdev);

	mutex_exit(&vd->vdev_rebuild_lock);

	/*
	 * Systematically walk the metaslabs and issue rebuild I/Os for
	 * all ranges in the allocated space map.
	 */
	for (uint64_t i = 0; i < vd->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_ms[i];
		vr->vr_scan_msp = msp;

		/*
		 * Calculate the max number of in-flight bytes for top-level
		 * vdev scanning operations (minimum 1MB, maximum 1/2 of
		 * arc_c_max shared by all top-level vdevs).  Limits for the
		 * issuing phase are done per top-level vdev and are handled
		 * separately.
		 */
		uint64_t limit = (arc_c_max / 2) / MAX(rvd->vdev_children, 1);
		vr->vr_bytes_inflight_max = MIN(limit, MAX(1ULL << 20,
		    zfs_rebuild_vdev_limit * vd->vdev_children));

		/*
		 * Removal of vdevs from the vdev tree may eliminate the need
		 * for the rebuild, in which case it should be canceled.  The
		 * vdev_rebuild_cancel_wanted flag is set until the sync task
		 * completes.  This may be after the rebuild thread exits.
		 */
		if (vdev_rebuild_should_cancel(vd)) {
			vd->vdev_rebuild_cancel_wanted = B_TRUE;
			error = EINTR;
			break;
		}

		ASSERT0(range_tree_space(vr->vr_scan_tree));

		/* Disable any new allocations to this metaslab */
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		metaslab_disable(msp);

		mutex_enter(&msp->ms_sync_lock);
		mutex_enter(&msp->ms_lock);

		/*
		 * If there are outstanding allocations wait for them to be
		 * synced.  This is needed to ensure all allocated ranges are
		 * on disk and therefore will be rebuilt.
		 */
		for (int j = 0; j < TXG_SIZE; j++) {
			if (range_tree_space(msp->ms_allocating[j])) {
				mutex_exit(&msp->ms_lock);
				mutex_exit(&msp->ms_sync_lock);
				txg_wait_synced(dsl, 0);
				mutex_enter(&msp->ms_sync_lock);
				mutex_enter(&msp->ms_lock);
				break;
			}
		}

		/*
		 * When a metaslab has been allocated from read its allocated
		 * ranges from the space map object into the vr_scan_tree.
		 * Then add inflight / unflushed ranges and remove inflight /
		 * unflushed frees.  This is the minimum range to be rebuilt.
		 */
		if (msp->ms_sm != NULL) {
			VERIFY0(space_map_load(msp->ms_sm,
			    vr->vr_scan_tree, SM_ALLOC));

			for (int i = 0; i < TXG_SIZE; i++) {
				ASSERT0(range_tree_space(
				    msp->ms_allocating[i]));
			}

			range_tree_walk(msp->ms_unflushed_allocs,
			    range_tree_add, vr->vr_scan_tree);
			range_tree_walk(msp->ms_unflushed_frees,
			    range_tree_remove, vr->vr_scan_tree);

			/*
			 * Remove ranges which have already been rebuilt based
			 * on the last offset.  This can happen when restarting
			 * a scan after exporting and re-importing the pool.
			 */
			range_tree_clear(vr->vr_scan_tree, 0,
			    vrp->vrp_last_offset);
		}

		mutex_exit(&msp->ms_lock);
		mutex_exit(&msp->ms_sync_lock);

		/*
		 * To provide an accurate estimate re-calculate the estimated
		 * size every 5 minutes to account for recent allocations and
		 * frees made to space maps which have not yet been rebuilt.
		 */
		if (gethrtime() > update_est_time + SEC2NSEC(300)) {
			update_est_time = gethrtime();
			vdev_rebuild_update_bytes_est(vd, i);
		}

		/*
		 * Walk the allocated space map and issue the rebuild I/O.
		 */
		error = vdev_rebuild_ranges(vr);
		range_tree_vacate(vr->vr_scan_tree, NULL, NULL);

		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		metaslab_enable(msp, B_FALSE, B_FALSE);

		if (error != 0)
			break;
	}

	range_tree_destroy(vr->vr_scan_tree);
	spa_config_exit(spa, SCL_CONFIG, FTAG);

	/* Wait for any remaining rebuild I/O to complete */
	mutex_enter(&vr->vr_io_lock);
	while (vr->vr_bytes_inflight > 0)
		cv_wait(&vr->vr_io_cv, &vr->vr_io_lock);

	mutex_exit(&vr->vr_io_lock);

	mutex_destroy(&vr->vr_io_lock);
	cv_destroy(&vr->vr_io_cv);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	dsl_pool_t *dp = spa_get_dsl(spa);
	dmu_tx_t *tx = dmu_tx_create_dd(dp->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

	mutex_enter(&vd->vdev_rebuild_lock);
	if (error == 0) {
		/*
		 * After a successful rebuild clear the DTLs of all ranges
		 * which were missing when the rebuild was started.  These
		 * ranges must have been rebuilt as a consequence of rebuilding
		 * all allocated space.  Note that unlike a scrub or resilver
		 * the rebuild operation will reconstruct data only referenced
		 * by a pool checkpoint.  See the dsl_scan_done() comments.
		 */
		dsl_sync_task_nowait(dp, vdev_rebuild_complete_sync,
		    (void *)(uintptr_t)vd->vdev_id, tx);
	} else if (vd->vdev_rebuild_cancel_wanted) {
		/*
		 * The rebuild operation was canceled.  This will occur when
		 * a device participating in the rebuild is detached.
		 */
		dsl_sync_task_nowait(dp, vdev_rebuild_cancel_sync,
		    (void *)(uintptr_t)vd->vdev_id, tx);
	} else if (vd->vdev_rebuild_reset_wanted) {
		/*
		 * Reset the running rebuild without canceling and restarting
		 * it.  This will occur when a new device is attached and must
		 * participate in the rebuild.
		 */
		dsl_sync_task_nowait(dp, vdev_rebuild_reset_sync,
		    (void *)(uintptr_t)vd->vdev_id, tx);
	} else {
		/*
		 * The rebuild operation should be suspended.  This may occur
		 * when detaching a child vdev or when exporting the pool.  The
		 * rebuild is left in the active state so it will be resumed.
		 */
		ASSERT(vrp->vrp_rebuild_state == VDEV_REBUILD_ACTIVE);
		vd->vdev_rebuilding = B_FALSE;
	}

	dmu_tx_commit(tx);

	vd->vdev_rebuild_thread = NULL;
	mutex_exit(&vd->vdev_rebuild_lock);
	spa_config_exit(spa, SCL_CONFIG, FTAG);

	cv_broadcast(&vd->vdev_rebuild_cv);

	thread_exit();
}

/*
 * Returns B_TRUE if any top-level vdev are rebuilding.
 */
boolean_t
vdev_rebuild_active(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	boolean_t ret = B_FALSE;

	if (vd == spa->spa_root_vdev) {
		for (uint64_t i = 0; i < vd->vdev_children; i++) {
			ret = vdev_rebuild_active(vd->vdev_child[i]);
			if (ret)
				return (ret);
		}
	} else if (vd->vdev_top_zap != 0) {
		vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
		vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

		mutex_enter(&vd->vdev_rebuild_lock);
		ret = (vrp->vrp_rebuild_state == VDEV_REBUILD_ACTIVE);
		mutex_exit(&vd->vdev_rebuild_lock);
	}

	return (ret);
}

/*
 * Start a rebuild operation.  The rebuild may be restarted when the
 * top-level vdev is currently actively rebuilding.
 */
void
vdev_rebuild(vdev_t *vd)
{
	vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
	vdev_rebuild_phys_t *vrp __maybe_unused = &vr->vr_rebuild_phys;

	ASSERT(vd->vdev_top == vd);
	ASSERT(vdev_is_concrete(vd));
	ASSERT(!vd->vdev_removing);
	ASSERT(spa_feature_is_enabled(vd->vdev_spa,
	    SPA_FEATURE_DEVICE_REBUILD));

	mutex_enter(&vd->vdev_rebuild_lock);
	if (vd->vdev_rebuilding) {
		ASSERT3U(vrp->vrp_rebuild_state, ==, VDEV_REBUILD_ACTIVE);

		/*
		 * Signal a running rebuild operation that it should restart
		 * from the beginning because a new device was attached.  The
		 * vdev_rebuild_reset_wanted flag is set until the sync task
		 * completes.  This may be after the rebuild thread exits.
		 */
		if (!vd->vdev_rebuild_reset_wanted)
			vd->vdev_rebuild_reset_wanted = B_TRUE;
	} else {
		vdev_rebuild_initiate(vd);
	}
	mutex_exit(&vd->vdev_rebuild_lock);
}

static void
vdev_rebuild_restart_impl(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	if (vd == spa->spa_root_vdev) {
		for (uint64_t i = 0; i < vd->vdev_children; i++)
			vdev_rebuild_restart_impl(vd->vdev_child[i]);

	} else if (vd->vdev_top_zap != 0) {
		vdev_rebuild_t *vr = &vd->vdev_rebuild_config;
		vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

		mutex_enter(&vd->vdev_rebuild_lock);
		if (vrp->vrp_rebuild_state == VDEV_REBUILD_ACTIVE &&
		    vdev_writeable(vd) && !vd->vdev_rebuilding) {
			ASSERT(spa_feature_is_active(spa,
			    SPA_FEATURE_DEVICE_REBUILD));
			vd->vdev_rebuilding = B_TRUE;
			vd->vdev_rebuild_thread = thread_create(NULL, 0,
			    vdev_rebuild_thread, vd, 0, &p0, TS_RUN,
			    maxclsyspri);
		}
		mutex_exit(&vd->vdev_rebuild_lock);
	}
}

/*
 * Conditionally restart all of the vdev_rebuild_thread's for a pool.  The
 * feature flag must be active and the rebuild in the active state.   This
 * cannot be used to start a new rebuild.
 */
void
vdev_rebuild_restart(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	vdev_rebuild_restart_impl(spa->spa_root_vdev);
}

/*
 * Stop and wait for all of the vdev_rebuild_thread's associated with the
 * vdev tree provide to be terminated (canceled or stopped).
 */
void
vdev_rebuild_stop_wait(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	if (vd == spa->spa_root_vdev) {
		for (uint64_t i = 0; i < vd->vdev_children; i++)
			vdev_rebuild_stop_wait(vd->vdev_child[i]);

	} else if (vd->vdev_top_zap != 0) {
		ASSERT(vd == vd->vdev_top);

		mutex_enter(&vd->vdev_rebuild_lock);
		if (vd->vdev_rebuild_thread != NULL) {
			vd->vdev_rebuild_exit_wanted = B_TRUE;
			while (vd->vdev_rebuilding) {
				cv_wait(&vd->vdev_rebuild_cv,
				    &vd->vdev_rebuild_lock);
			}
			vd->vdev_rebuild_exit_wanted = B_FALSE;
		}
		mutex_exit(&vd->vdev_rebuild_lock);
	}
}

/*
 * Stop all rebuild operations but leave them in the active state so they
 * will be resumed when importing the pool.
 */
void
vdev_rebuild_stop_all(spa_t *spa)
{
	vdev_rebuild_stop_wait(spa->spa_root_vdev);
}

/*
 * Rebuild statistics reported per top-level vdev.
 */
int
vdev_rebuild_get_stats(vdev_t *tvd, vdev_rebuild_stat_t *vrs)
{
	spa_t *spa = tvd->vdev_spa;

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_DEVICE_REBUILD))
		return (SET_ERROR(ENOTSUP));

	if (tvd != tvd->vdev_top || tvd->vdev_top_zap == 0)
		return (SET_ERROR(EINVAL));

	int error = zap_contains(spa_meta_objset(spa),
	    tvd->vdev_top_zap, VDEV_TOP_ZAP_VDEV_REBUILD_PHYS);

	if (error == ENOENT) {
		memset(vrs, 0, sizeof (vdev_rebuild_stat_t));
		vrs->vrs_state = VDEV_REBUILD_NONE;
		error = 0;
	} else if (error == 0) {
		vdev_rebuild_t *vr = &tvd->vdev_rebuild_config;
		vdev_rebuild_phys_t *vrp = &vr->vr_rebuild_phys;

		mutex_enter(&tvd->vdev_rebuild_lock);
		vrs->vrs_state = vrp->vrp_rebuild_state;
		vrs->vrs_start_time = vrp->vrp_start_time;
		vrs->vrs_end_time = vrp->vrp_end_time;
		vrs->vrs_scan_time_ms = vrp->vrp_scan_time_ms;
		vrs->vrs_bytes_scanned = vrp->vrp_bytes_scanned;
		vrs->vrs_bytes_issued = vrp->vrp_bytes_issued;
		vrs->vrs_bytes_rebuilt = vrp->vrp_bytes_rebuilt;
		vrs->vrs_bytes_est = vrp->vrp_bytes_est;
		vrs->vrs_errors = vrp->vrp_errors;
		vrs->vrs_pass_time_ms = NSEC2MSEC(gethrtime() -
		    vr->vr_pass_start_time);
		vrs->vrs_pass_bytes_scanned = vr->vr_pass_bytes_scanned;
		vrs->vrs_pass_bytes_issued = vr->vr_pass_bytes_issued;
		vrs->vrs_pass_bytes_skipped = vr->vr_pass_bytes_skipped;
		mutex_exit(&tvd->vdev_rebuild_lock);
	}

	return (error);
}

ZFS_MODULE_PARAM(zfs, zfs_, rebuild_max_segment, U64, ZMOD_RW,
	"Max segment size in bytes of rebuild reads");

ZFS_MODULE_PARAM(zfs, zfs_, rebuild_vdev_limit, U64, ZMOD_RW,
	"Max bytes in flight per leaf vdev for sequential resilvers");

ZFS_MODULE_PARAM(zfs, zfs_, rebuild_scrub_enabled, INT, ZMOD_RW,
	"Automatically scrub after sequential resilver completes");
