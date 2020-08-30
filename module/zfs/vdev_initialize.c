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
 * Copyright (c) 2016, 2019 by Delphix. All rights reserved.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/txg.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_synctask.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/vdev_initialize.h>

/*
 * Value that is written to disk during initialization.
 */
#ifdef _ILP32
unsigned long zfs_initialize_value = 0xdeadbeefUL;
#else
unsigned long zfs_initialize_value = 0xdeadbeefdeadbeeeULL;
#endif

/* maximum number of I/Os outstanding per leaf vdev */
int zfs_initialize_limit = 1;

/* size of initializing writes; default 1MiB, see zfs_remove_max_segment */
unsigned long zfs_initialize_chunk_size = 1024 * 1024;

static boolean_t
vdev_initialize_should_stop(vdev_t *vd)
{
	return (vd->vdev_initialize_exit_wanted || !vdev_writeable(vd) ||
	    vd->vdev_detached || vd->vdev_top->vdev_removing);
}

static void
vdev_initialize_zap_update_sync(void *arg, dmu_tx_t *tx)
{
	/*
	 * We pass in the guid instead of the vdev_t since the vdev may
	 * have been freed prior to the sync task being processed. This
	 * happens when a vdev is detached as we call spa_config_vdev_exit(),
	 * stop the initializing thread, schedule the sync task, and free
	 * the vdev. Later when the scheduled sync task is invoked, it would
	 * find that the vdev has been freed.
	 */
	uint64_t guid = *(uint64_t *)arg;
	uint64_t txg = dmu_tx_get_txg(tx);
	kmem_free(arg, sizeof (uint64_t));

	vdev_t *vd = spa_lookup_by_guid(tx->tx_pool->dp_spa, guid, B_FALSE);
	if (vd == NULL || vd->vdev_top->vdev_removing || !vdev_is_concrete(vd))
		return;

	uint64_t last_offset = vd->vdev_initialize_offset[txg & TXG_MASK];
	vd->vdev_initialize_offset[txg & TXG_MASK] = 0;

	VERIFY(vd->vdev_leaf_zap != 0);

	objset_t *mos = vd->vdev_spa->spa_meta_objset;

	if (last_offset > 0) {
		vd->vdev_initialize_last_offset = last_offset;
		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_INITIALIZE_LAST_OFFSET,
		    sizeof (last_offset), 1, &last_offset, tx));
	}
	if (vd->vdev_initialize_action_time > 0) {
		uint64_t val = (uint64_t)vd->vdev_initialize_action_time;
		VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
		    VDEV_LEAF_ZAP_INITIALIZE_ACTION_TIME, sizeof (val),
		    1, &val, tx));
	}

	uint64_t initialize_state = vd->vdev_initialize_state;
	VERIFY0(zap_update(mos, vd->vdev_leaf_zap,
	    VDEV_LEAF_ZAP_INITIALIZE_STATE, sizeof (initialize_state), 1,
	    &initialize_state, tx));
}

static void
vdev_initialize_change_state(vdev_t *vd, vdev_initializing_state_t new_state)
{
	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));
	spa_t *spa = vd->vdev_spa;

	if (new_state == vd->vdev_initialize_state)
		return;

	/*
	 * Copy the vd's guid, this will be freed by the sync task.
	 */
	uint64_t *guid = kmem_zalloc(sizeof (uint64_t), KM_SLEEP);
	*guid = vd->vdev_guid;

	/*
	 * If we're suspending, then preserving the original start time.
	 */
	if (vd->vdev_initialize_state != VDEV_INITIALIZE_SUSPENDED) {
		vd->vdev_initialize_action_time = gethrestime_sec();
	}
	vd->vdev_initialize_state = new_state;

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	dsl_sync_task_nowait(spa_get_dsl(spa), vdev_initialize_zap_update_sync,
	    guid, tx);

	switch (new_state) {
	case VDEV_INITIALIZE_ACTIVE:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s activated", vd->vdev_path);
		break;
	case VDEV_INITIALIZE_SUSPENDED:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s suspended", vd->vdev_path);
		break;
	case VDEV_INITIALIZE_CANCELED:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s canceled", vd->vdev_path);
		break;
	case VDEV_INITIALIZE_COMPLETE:
		spa_history_log_internal(spa, "initialize", tx,
		    "vdev=%s complete", vd->vdev_path);
		break;
	default:
		panic("invalid state %llu", (unsigned long long)new_state);
	}

	dmu_tx_commit(tx);

	if (new_state != VDEV_INITIALIZE_ACTIVE)
		spa_notify_waiters(spa);
}

static void
vdev_initialize_cb(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	mutex_enter(&vd->vdev_initialize_io_lock);
	if (zio->io_error == ENXIO && !vdev_writeable(vd)) {
		/*
		 * The I/O failed because the vdev was unavailable; roll the
		 * last offset back. (This works because spa_sync waits on
		 * spa_txg_zio before it runs sync tasks.)
		 */
		uint64_t *off =
		    &vd->vdev_initialize_offset[zio->io_txg & TXG_MASK];
		*off = MIN(*off, zio->io_offset);
	} else {
		/*
		 * Since initializing is best-effort, we ignore I/O errors and
		 * rely on vdev_probe to determine if the errors are more
		 * critical.
		 */
		if (zio->io_error != 0)
			vd->vdev_stat.vs_initialize_errors++;

		vd->vdev_initialize_bytes_done += zio->io_orig_size;
	}
	ASSERT3U(vd->vdev_initialize_inflight, >, 0);
	vd->vdev_initialize_inflight--;
	cv_broadcast(&vd->vdev_initialize_io_cv);
	mutex_exit(&vd->vdev_initialize_io_lock);

	spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
}

/* Takes care of physical writing and limiting # of concurrent ZIOs. */
static int
vdev_initialize_write(vdev_t *vd, uint64_t start, uint64_t size, abd_t *data)
{
	spa_t *spa = vd->vdev_spa;

	/* Limit inflight initializing I/Os */
	mutex_enter(&vd->vdev_initialize_io_lock);
	while (vd->vdev_initialize_inflight >= zfs_initialize_limit) {
		cv_wait(&vd->vdev_initialize_io_cv,
		    &vd->vdev_initialize_io_lock);
	}
	vd->vdev_initialize_inflight++;
	mutex_exit(&vd->vdev_initialize_io_lock);

	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	uint64_t txg = dmu_tx_get_txg(tx);

	spa_config_enter(spa, SCL_STATE_ALL, vd, RW_READER);
	mutex_enter(&vd->vdev_initialize_lock);

	if (vd->vdev_initialize_offset[txg & TXG_MASK] == 0) {
		uint64_t *guid = kmem_zalloc(sizeof (uint64_t), KM_SLEEP);
		*guid = vd->vdev_guid;

		/* This is the first write of this txg. */
		dsl_sync_task_nowait(spa_get_dsl(spa),
		    vdev_initialize_zap_update_sync, guid, tx);
	}

	/*
	 * We know the vdev struct will still be around since all
	 * consumers of vdev_free must stop the initialization first.
	 */
	if (vdev_initialize_should_stop(vd)) {
		mutex_enter(&vd->vdev_initialize_io_lock);
		ASSERT3U(vd->vdev_initialize_inflight, >, 0);
		vd->vdev_initialize_inflight--;
		mutex_exit(&vd->vdev_initialize_io_lock);
		spa_config_exit(vd->vdev_spa, SCL_STATE_ALL, vd);
		mutex_exit(&vd->vdev_initialize_lock);
		dmu_tx_commit(tx);
		return (SET_ERROR(EINTR));
	}
	mutex_exit(&vd->vdev_initialize_lock);

	vd->vdev_initialize_offset[txg & TXG_MASK] = start + size;
	zio_nowait(zio_write_phys(spa->spa_txg_zio[txg & TXG_MASK], vd, start,
	    size, data, ZIO_CHECKSUM_OFF, vdev_initialize_cb, NULL,
	    ZIO_PRIORITY_INITIALIZING, ZIO_FLAG_CANFAIL, B_FALSE));
	/* vdev_initialize_cb releases SCL_STATE_ALL */

	dmu_tx_commit(tx);

	return (0);
}

/*
 * Callback to fill each ABD chunk with zfs_initialize_value. len must be
 * divisible by sizeof (uint64_t), and buf must be 8-byte aligned. The ABD
 * allocation will guarantee these for us.
 */
/* ARGSUSED */
static int
vdev_initialize_block_fill(void *buf, size_t len, void *unused)
{
	ASSERT0(len % sizeof (uint64_t));
#ifdef _ILP32
	for (uint64_t i = 0; i < len; i += sizeof (uint32_t)) {
		*(uint32_t *)((char *)(buf) + i) = zfs_initialize_value;
	}
#else
	for (uint64_t i = 0; i < len; i += sizeof (uint64_t)) {
		*(uint64_t *)((char *)(buf) + i) = zfs_initialize_value;
	}
#endif
	return (0);
}

static abd_t *
vdev_initialize_block_alloc(void)
{
	/* Allocate ABD for filler data */
	abd_t *data = abd_alloc_for_io(zfs_initialize_chunk_size, B_FALSE);

	ASSERT0(zfs_initialize_chunk_size % sizeof (uint64_t));
	(void) abd_iterate_func(data, 0, zfs_initialize_chunk_size,
	    vdev_initialize_block_fill, NULL);

	return (data);
}

static void
vdev_initialize_block_free(abd_t *data)
{
	abd_free(data);
}

static int
vdev_initialize_ranges(vdev_t *vd, abd_t *data)
{
	range_tree_t *rt = vd->vdev_initialize_tree;
	zfs_btree_t *bt = &rt->rt_root;
	zfs_btree_index_t where;

	for (range_seg_t *rs = zfs_btree_first(bt, &where); rs != NULL;
	    rs = zfs_btree_next(bt, &where, &where)) {
		uint64_t size = rs_get_end(rs, rt) - rs_get_start(rs, rt);

		/* Split range into legally-sized physical chunks */
		uint64_t writes_required =
		    ((size - 1) / zfs_initialize_chunk_size) + 1;

		for (uint64_t w = 0; w < writes_required; w++) {
			int error;

			error = vdev_initialize_write(vd,
			    VDEV_LABEL_START_SIZE + rs_get_start(rs, rt) +
			    (w * zfs_initialize_chunk_size),
			    MIN(size - (w * zfs_initialize_chunk_size),
			    zfs_initialize_chunk_size), data);
			if (error != 0)
				return (error);
		}
	}
	return (0);
}

static void
vdev_initialize_calculate_progress(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_READER) ||
	    spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_WRITER));
	ASSERT(vd->vdev_leaf_zap != 0);

	vd->vdev_initialize_bytes_est = 0;
	vd->vdev_initialize_bytes_done = 0;

	for (uint64_t i = 0; i < vd->vdev_top->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_top->vdev_ms[i];
		mutex_enter(&msp->ms_lock);

		uint64_t ms_free = msp->ms_size -
		    metaslab_allocated_space(msp);

		if (vd->vdev_top->vdev_ops == &vdev_raidz_ops)
			ms_free /= vd->vdev_top->vdev_children;

		/*
		 * Convert the metaslab range to a physical range
		 * on our vdev. We use this to determine if we are
		 * in the middle of this metaslab range.
		 */
		range_seg64_t logical_rs, physical_rs;
		logical_rs.rs_start = msp->ms_start;
		logical_rs.rs_end = msp->ms_start + msp->ms_size;
		vdev_xlate(vd, &logical_rs, &physical_rs);

		if (vd->vdev_initialize_last_offset <= physical_rs.rs_start) {
			vd->vdev_initialize_bytes_est += ms_free;
			mutex_exit(&msp->ms_lock);
			continue;
		} else if (vd->vdev_initialize_last_offset >
		    physical_rs.rs_end) {
			vd->vdev_initialize_bytes_done += ms_free;
			vd->vdev_initialize_bytes_est += ms_free;
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/*
		 * If we get here, we're in the middle of initializing this
		 * metaslab. Load it and walk the free tree for more accurate
		 * progress estimation.
		 */
		VERIFY0(metaslab_load(msp));

		zfs_btree_index_t where;
		range_tree_t *rt = msp->ms_allocatable;
		for (range_seg_t *rs =
		    zfs_btree_first(&rt->rt_root, &where); rs;
		    rs = zfs_btree_next(&rt->rt_root, &where,
		    &where)) {
			logical_rs.rs_start = rs_get_start(rs, rt);
			logical_rs.rs_end = rs_get_end(rs, rt);
			vdev_xlate(vd, &logical_rs, &physical_rs);

			uint64_t size = physical_rs.rs_end -
			    physical_rs.rs_start;
			vd->vdev_initialize_bytes_est += size;
			if (vd->vdev_initialize_last_offset >
			    physical_rs.rs_end) {
				vd->vdev_initialize_bytes_done += size;
			} else if (vd->vdev_initialize_last_offset >
			    physical_rs.rs_start &&
			    vd->vdev_initialize_last_offset <
			    physical_rs.rs_end) {
				vd->vdev_initialize_bytes_done +=
				    vd->vdev_initialize_last_offset -
				    physical_rs.rs_start;
			}
		}
		mutex_exit(&msp->ms_lock);
	}
}

static int
vdev_initialize_load(vdev_t *vd)
{
	int err = 0;
	ASSERT(spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_READER) ||
	    spa_config_held(vd->vdev_spa, SCL_CONFIG, RW_WRITER));
	ASSERT(vd->vdev_leaf_zap != 0);

	if (vd->vdev_initialize_state == VDEV_INITIALIZE_ACTIVE ||
	    vd->vdev_initialize_state == VDEV_INITIALIZE_SUSPENDED) {
		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_INITIALIZE_LAST_OFFSET,
		    sizeof (vd->vdev_initialize_last_offset), 1,
		    &vd->vdev_initialize_last_offset);
		if (err == ENOENT) {
			vd->vdev_initialize_last_offset = 0;
			err = 0;
		}
	}

	vdev_initialize_calculate_progress(vd);
	return (err);
}

/*
 * Convert the logical range into a physical range and add it to our
 * avl tree.
 */
static void
vdev_initialize_range_add(void *arg, uint64_t start, uint64_t size)
{
	vdev_t *vd = arg;
	range_seg64_t logical_rs, physical_rs;
	logical_rs.rs_start = start;
	logical_rs.rs_end = start + size;

	ASSERT(vd->vdev_ops->vdev_op_leaf);
	vdev_xlate(vd, &logical_rs, &physical_rs);

	IMPLY(vd->vdev_top == vd,
	    logical_rs.rs_start == physical_rs.rs_start);
	IMPLY(vd->vdev_top == vd,
	    logical_rs.rs_end == physical_rs.rs_end);

	/* Only add segments that we have not visited yet */
	if (physical_rs.rs_end <= vd->vdev_initialize_last_offset)
		return;

	/* Pick up where we left off mid-range. */
	if (vd->vdev_initialize_last_offset > physical_rs.rs_start) {
		zfs_dbgmsg("range write: vd %s changed (%llu, %llu) to "
		    "(%llu, %llu)", vd->vdev_path,
		    (u_longlong_t)physical_rs.rs_start,
		    (u_longlong_t)physical_rs.rs_end,
		    (u_longlong_t)vd->vdev_initialize_last_offset,
		    (u_longlong_t)physical_rs.rs_end);
		ASSERT3U(physical_rs.rs_end, >,
		    vd->vdev_initialize_last_offset);
		physical_rs.rs_start = vd->vdev_initialize_last_offset;
	}
	ASSERT3U(physical_rs.rs_end, >=, physical_rs.rs_start);

	/*
	 * With raidz, it's possible that the logical range does not live on
	 * this leaf vdev. We only add the physical range to this vdev's if it
	 * has a length greater than 0.
	 */
	if (physical_rs.rs_end > physical_rs.rs_start) {
		range_tree_add(vd->vdev_initialize_tree, physical_rs.rs_start,
		    physical_rs.rs_end - physical_rs.rs_start);
	} else {
		ASSERT3U(physical_rs.rs_end, ==, physical_rs.rs_start);
	}
}

static void
vdev_initialize_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	int error = 0;
	uint64_t ms_count = 0;

	ASSERT(vdev_is_concrete(vd));
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	vd->vdev_initialize_last_offset = 0;
	VERIFY0(vdev_initialize_load(vd));

	abd_t *deadbeef = vdev_initialize_block_alloc();

	vd->vdev_initialize_tree = range_tree_create(NULL, RANGE_SEG64, NULL,
	    0, 0);

	for (uint64_t i = 0; !vd->vdev_detached &&
	    i < vd->vdev_top->vdev_ms_count; i++) {
		metaslab_t *msp = vd->vdev_top->vdev_ms[i];
		boolean_t unload_when_done = B_FALSE;

		/*
		 * If we've expanded the top-level vdev or it's our
		 * first pass, calculate our progress.
		 */
		if (vd->vdev_top->vdev_ms_count != ms_count) {
			vdev_initialize_calculate_progress(vd);
			ms_count = vd->vdev_top->vdev_ms_count;
		}

		spa_config_exit(spa, SCL_CONFIG, FTAG);
		metaslab_disable(msp);
		mutex_enter(&msp->ms_lock);
		if (!msp->ms_loaded && !msp->ms_loading)
			unload_when_done = B_TRUE;
		VERIFY0(metaslab_load(msp));

		range_tree_walk(msp->ms_allocatable, vdev_initialize_range_add,
		    vd);
		mutex_exit(&msp->ms_lock);

		error = vdev_initialize_ranges(vd, deadbeef);
		metaslab_enable(msp, B_TRUE, unload_when_done);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

		range_tree_vacate(vd->vdev_initialize_tree, NULL, NULL);
		if (error != 0)
			break;
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);
	mutex_enter(&vd->vdev_initialize_io_lock);
	while (vd->vdev_initialize_inflight > 0) {
		cv_wait(&vd->vdev_initialize_io_cv,
		    &vd->vdev_initialize_io_lock);
	}
	mutex_exit(&vd->vdev_initialize_io_lock);

	range_tree_destroy(vd->vdev_initialize_tree);
	vdev_initialize_block_free(deadbeef);
	vd->vdev_initialize_tree = NULL;

	mutex_enter(&vd->vdev_initialize_lock);
	if (!vd->vdev_initialize_exit_wanted && vdev_writeable(vd)) {
		vdev_initialize_change_state(vd, VDEV_INITIALIZE_COMPLETE);
	}
	ASSERT(vd->vdev_initialize_thread != NULL ||
	    vd->vdev_initialize_inflight == 0);

	/*
	 * Drop the vdev_initialize_lock while we sync out the
	 * txg since it's possible that a device might be trying to
	 * come online and must check to see if it needs to restart an
	 * initialization. That thread will be holding the spa_config_lock
	 * which would prevent the txg_wait_synced from completing.
	 */
	mutex_exit(&vd->vdev_initialize_lock);
	txg_wait_synced(spa_get_dsl(spa), 0);
	mutex_enter(&vd->vdev_initialize_lock);

	vd->vdev_initialize_thread = NULL;
	cv_broadcast(&vd->vdev_initialize_cv);
	mutex_exit(&vd->vdev_initialize_lock);

	thread_exit();
}

/*
 * Initiates a device. Caller must hold vdev_initialize_lock.
 * Device must be a leaf and not already be initializing.
 */
void
vdev_initialize(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(vdev_is_concrete(vd));
	ASSERT3P(vd->vdev_initialize_thread, ==, NULL);
	ASSERT(!vd->vdev_detached);
	ASSERT(!vd->vdev_initialize_exit_wanted);
	ASSERT(!vd->vdev_top->vdev_removing);

	vdev_initialize_change_state(vd, VDEV_INITIALIZE_ACTIVE);
	vd->vdev_initialize_thread = thread_create(NULL, 0,
	    vdev_initialize_thread, vd, 0, &p0, TS_RUN, maxclsyspri);
}

/*
 * Wait for the initialize thread to be terminated (cancelled or stopped).
 */
static void
vdev_initialize_stop_wait_impl(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));

	while (vd->vdev_initialize_thread != NULL)
		cv_wait(&vd->vdev_initialize_cv, &vd->vdev_initialize_lock);

	ASSERT3P(vd->vdev_initialize_thread, ==, NULL);
	vd->vdev_initialize_exit_wanted = B_FALSE;
}

/*
 * Wait for vdev initialize threads which were either to cleanly exit.
 */
void
vdev_initialize_stop_wait(spa_t *spa, list_t *vd_list)
{
	vdev_t *vd;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	while ((vd = list_remove_head(vd_list)) != NULL) {
		mutex_enter(&vd->vdev_initialize_lock);
		vdev_initialize_stop_wait_impl(vd);
		mutex_exit(&vd->vdev_initialize_lock);
	}
}

/*
 * Stop initializing a device, with the resultant initializing state being
 * tgt_state.  For blocking behavior pass NULL for vd_list.  Otherwise, when
 * a list_t is provided the stopping vdev is inserted in to the list.  Callers
 * are then required to call vdev_initialize_stop_wait() to block for all the
 * initialization threads to exit.  The caller must hold vdev_initialize_lock
 * and must not be writing to the spa config, as the initializing thread may
 * try to enter the config as a reader before exiting.
 */
void
vdev_initialize_stop(vdev_t *vd, vdev_initializing_state_t tgt_state,
    list_t *vd_list)
{
	ASSERT(!spa_config_held(vd->vdev_spa, SCL_CONFIG|SCL_STATE, RW_WRITER));
	ASSERT(MUTEX_HELD(&vd->vdev_initialize_lock));
	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(vdev_is_concrete(vd));

	/*
	 * Allow cancel requests to proceed even if the initialize thread
	 * has stopped.
	 */
	if (vd->vdev_initialize_thread == NULL &&
	    tgt_state != VDEV_INITIALIZE_CANCELED) {
		return;
	}

	vdev_initialize_change_state(vd, tgt_state);
	vd->vdev_initialize_exit_wanted = B_TRUE;

	if (vd_list == NULL) {
		vdev_initialize_stop_wait_impl(vd);
	} else {
		ASSERT(MUTEX_HELD(&spa_namespace_lock));
		list_insert_tail(vd_list, vd);
	}
}

static void
vdev_initialize_stop_all_impl(vdev_t *vd, vdev_initializing_state_t tgt_state,
    list_t *vd_list)
{
	if (vd->vdev_ops->vdev_op_leaf && vdev_is_concrete(vd)) {
		mutex_enter(&vd->vdev_initialize_lock);
		vdev_initialize_stop(vd, tgt_state, vd_list);
		mutex_exit(&vd->vdev_initialize_lock);
		return;
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_initialize_stop_all_impl(vd->vdev_child[i], tgt_state,
		    vd_list);
	}
}

/*
 * Convenience function to stop initializing of a vdev tree and set all
 * initialize thread pointers to NULL.
 */
void
vdev_initialize_stop_all(vdev_t *vd, vdev_initializing_state_t tgt_state)
{
	spa_t *spa = vd->vdev_spa;
	list_t vd_list;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	list_create(&vd_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_initialize_node));

	vdev_initialize_stop_all_impl(vd, tgt_state, &vd_list);
	vdev_initialize_stop_wait(spa, &vd_list);

	if (vd->vdev_spa->spa_sync_on) {
		/* Make sure that our state has been synced to disk */
		txg_wait_synced(spa_get_dsl(vd->vdev_spa), 0);
	}

	list_destroy(&vd_list);
}

void
vdev_initialize_restart(vdev_t *vd)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(!spa_config_held(vd->vdev_spa, SCL_ALL, RW_WRITER));

	if (vd->vdev_leaf_zap != 0) {
		mutex_enter(&vd->vdev_initialize_lock);
		uint64_t initialize_state = VDEV_INITIALIZE_NONE;
		int err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_INITIALIZE_STATE,
		    sizeof (initialize_state), 1, &initialize_state);
		ASSERT(err == 0 || err == ENOENT);
		vd->vdev_initialize_state = initialize_state;

		uint64_t timestamp = 0;
		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_leaf_zap, VDEV_LEAF_ZAP_INITIALIZE_ACTION_TIME,
		    sizeof (timestamp), 1, &timestamp);
		ASSERT(err == 0 || err == ENOENT);
		vd->vdev_initialize_action_time = timestamp;

		if (vd->vdev_initialize_state == VDEV_INITIALIZE_SUSPENDED ||
		    vd->vdev_offline) {
			/* load progress for reporting, but don't resume */
			VERIFY0(vdev_initialize_load(vd));
		} else if (vd->vdev_initialize_state ==
		    VDEV_INITIALIZE_ACTIVE && vdev_writeable(vd) &&
		    !vd->vdev_top->vdev_removing &&
		    vd->vdev_initialize_thread == NULL) {
			vdev_initialize(vd);
		}

		mutex_exit(&vd->vdev_initialize_lock);
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		vdev_initialize_restart(vd->vdev_child[i]);
	}
}

EXPORT_SYMBOL(vdev_initialize);
EXPORT_SYMBOL(vdev_initialize_stop);
EXPORT_SYMBOL(vdev_initialize_stop_all);
EXPORT_SYMBOL(vdev_initialize_stop_wait);
EXPORT_SYMBOL(vdev_initialize_restart);

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs, zfs_, initialize_value, ULONG, ZMOD_RW,
	"Value written during zpool initialize");

ZFS_MODULE_PARAM(zfs, zfs_, initialize_chunk_size, ULONG, ZMOD_RW,
	"Size in bytes of writes by zpool initialize");
/* END CSTYLED */
