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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/avl.h>
#include <sys/dsl_pool.h>
#include <sys/metaslab_impl.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/kstat.h>
#include <sys/abd.h>

/*
 * ZFS I/O Scheduler
 * ---------------
 *
 * ZFS issues I/O operations to leaf vdevs to satisfy and complete zios.  The
 * I/O scheduler determines when and in what order those operations are
 * issued.  The I/O scheduler divides operations into five I/O classes
 * prioritized in the following order: sync read, sync write, async read,
 * async write, and scrub/resilver.  Each queue defines the minimum and
 * maximum number of concurrent operations that may be issued to the device.
 * In addition, the device has an aggregate maximum. Note that the sum of the
 * per-queue minimums must not exceed the aggregate maximum. If the
 * sum of the per-queue maximums exceeds the aggregate maximum, then the
 * number of active i/os may reach zfs_vdev_max_active, in which case no
 * further i/os will be issued regardless of whether all per-queue
 * minimums have been met.
 *
 * For many physical devices, throughput increases with the number of
 * concurrent operations, but latency typically suffers. Further, physical
 * devices typically have a limit at which more concurrent operations have no
 * effect on throughput or can actually cause it to decrease.
 *
 * The scheduler selects the next operation to issue by first looking for an
 * I/O class whose minimum has not been satisfied. Once all are satisfied and
 * the aggregate maximum has not been hit, the scheduler looks for classes
 * whose maximum has not been satisfied. Iteration through the I/O classes is
 * done in the order specified above. No further operations are issued if the
 * aggregate maximum number of concurrent operations has been hit or if there
 * are no operations queued for an I/O class that has not hit its maximum.
 * Every time an i/o is queued or an operation completes, the I/O scheduler
 * looks for new operations to issue.
 *
 * All I/O classes have a fixed maximum number of outstanding operations
 * except for the async write class. Asynchronous writes represent the data
 * that is committed to stable storage during the syncing stage for
 * transaction groups (see txg.c). Transaction groups enter the syncing state
 * periodically so the number of queued async writes will quickly burst up and
 * then bleed down to zero. Rather than servicing them as quickly as possible,
 * the I/O scheduler changes the maximum number of active async write i/os
 * according to the amount of dirty data in the pool (see dsl_pool.c). Since
 * both throughput and latency typically increase with the number of
 * concurrent operations issued to physical devices, reducing the burstiness
 * in the number of concurrent operations also stabilizes the response time of
 * operations from other -- and in particular synchronous -- queues. In broad
 * strokes, the I/O scheduler will issue more concurrent operations from the
 * async write queue as there's more dirty data in the pool.
 *
 * Async Writes
 *
 * The number of concurrent operations issued for the async write I/O class
 * follows a piece-wise linear function defined by a few adjustable points.
 *
 *        |                   o---------| <-- zfs_vdev_async_write_max_active
 *   ^    |                  /^         |
 *   |    |                 / |         |
 * active |                /  |         |
 *  I/O   |               /   |         |
 * count  |              /    |         |
 *        |             /     |         |
 *        |------------o      |         | <-- zfs_vdev_async_write_min_active
 *       0|____________^______|_________|
 *        0%           |      |       100% of zfs_dirty_data_max
 *                     |      |
 *                     |      `-- zfs_vdev_async_write_active_max_dirty_percent
 *                     `--------- zfs_vdev_async_write_active_min_dirty_percent
 *
 * Until the amount of dirty data exceeds a minimum percentage of the dirty
 * data allowed in the pool, the I/O scheduler will limit the number of
 * concurrent operations to the minimum. As that threshold is crossed, the
 * number of concurrent operations issued increases linearly to the maximum at
 * the specified maximum percentage of the dirty data allowed in the pool.
 *
 * Ideally, the amount of dirty data on a busy pool will stay in the sloped
 * part of the function between zfs_vdev_async_write_active_min_dirty_percent
 * and zfs_vdev_async_write_active_max_dirty_percent. If it exceeds the
 * maximum percentage, this indicates that the rate of incoming data is
 * greater than the rate that the backend storage can handle. In this case, we
 * must further throttle incoming writes (see dmu_tx_delay() for details).
 */

/*
 * The maximum number of i/os active to each device.  Ideally, this will be >=
 * the sum of each queue's max_active.
 */
uint32_t zfs_vdev_max_active = 1000;

/*
 * Per-queue limits on the number of i/os active to each device.  If the
 * number of active i/os is < zfs_vdev_max_active, then the min_active comes
 * into play.  We will send min_active from each queue round-robin, and then
 * send from queues in the order defined by zio_priority_t up to max_active.
 * Some queues have additional mechanisms to limit number of active I/Os in
 * addition to min_active and max_active, see below.
 *
 * In general, smaller max_active's will lead to lower latency of synchronous
 * operations.  Larger max_active's may lead to higher overall throughput,
 * depending on underlying storage.
 *
 * The ratio of the queues' max_actives determines the balance of performance
 * between reads, writes, and scrubs.  E.g., increasing
 * zfs_vdev_scrub_max_active will cause the scrub or resilver to complete
 * more quickly, but reads and writes to have higher latency and lower
 * throughput.
 */
uint32_t zfs_vdev_sync_read_min_active = 10;
uint32_t zfs_vdev_sync_read_max_active = 10;
uint32_t zfs_vdev_sync_write_min_active = 10;
uint32_t zfs_vdev_sync_write_max_active = 10;
uint32_t zfs_vdev_async_read_min_active = 1;
uint32_t zfs_vdev_async_read_max_active = 3;
uint32_t zfs_vdev_async_write_min_active = 2;
uint32_t zfs_vdev_async_write_max_active = 10;
uint32_t zfs_vdev_scrub_min_active = 1;
uint32_t zfs_vdev_scrub_max_active = 3;
uint32_t zfs_vdev_removal_min_active = 1;
uint32_t zfs_vdev_removal_max_active = 2;
uint32_t zfs_vdev_initializing_min_active = 1;
uint32_t zfs_vdev_initializing_max_active = 1;
uint32_t zfs_vdev_trim_min_active = 1;
uint32_t zfs_vdev_trim_max_active = 2;
uint32_t zfs_vdev_rebuild_min_active = 1;
uint32_t zfs_vdev_rebuild_max_active = 3;

/*
 * When the pool has less than zfs_vdev_async_write_active_min_dirty_percent
 * dirty data, use zfs_vdev_async_write_min_active.  When it has more than
 * zfs_vdev_async_write_active_max_dirty_percent, use
 * zfs_vdev_async_write_max_active. The value is linearly interpolated
 * between min and max.
 */
int zfs_vdev_async_write_active_min_dirty_percent = 30;
int zfs_vdev_async_write_active_max_dirty_percent = 60;

/*
 * For non-interactive I/O (scrub, resilver, removal, initialize and rebuild),
 * the number of concurrently-active I/O's is limited to *_min_active, unless
 * the vdev is "idle".  When there are no interactive I/Os active (sync or
 * async), and zfs_vdev_nia_delay I/Os have completed since the last
 * interactive I/O, then the vdev is considered to be "idle", and the number
 * of concurrently-active non-interactive I/O's is increased to *_max_active.
 */
uint_t zfs_vdev_nia_delay = 5;

/*
 * Some HDDs tend to prioritize sequential I/O so high that concurrent
 * random I/O latency reaches several seconds.  On some HDDs it happens
 * even if sequential I/Os are submitted one at a time, and so setting
 * *_max_active to 1 does not help.  To prevent non-interactive I/Os, like
 * scrub, from monopolizing the device no more than zfs_vdev_nia_credit
 * I/Os can be sent while there are outstanding incomplete interactive
 * I/Os.  This enforced wait ensures the HDD services the interactive I/O
 * within a reasonable amount of time.
 */
uint_t zfs_vdev_nia_credit = 5;

/*
 * To reduce IOPs, we aggregate small adjacent I/Os into one large I/O.
 * For read I/Os, we also aggregate across small adjacency gaps; for writes
 * we include spans of optional I/Os to aid aggregation at the disk even when
 * they aren't able to help us aggregate at this level.
 */
int zfs_vdev_aggregation_limit = 1 << 20;
int zfs_vdev_aggregation_limit_non_rotating = SPA_OLD_MAXBLOCKSIZE;
int zfs_vdev_read_gap_limit = 32 << 10;
int zfs_vdev_write_gap_limit = 4 << 10;

/*
 * Define the queue depth percentage for each top-level. This percentage is
 * used in conjunction with zfs_vdev_async_max_active to determine how many
 * allocations a specific top-level vdev should handle. Once the queue depth
 * reaches zfs_vdev_queue_depth_pct * zfs_vdev_async_write_max_active / 100
 * then allocator will stop allocating blocks on that top-level device.
 * The default kernel setting is 1000% which will yield 100 allocations per
 * device. For userland testing, the default setting is 300% which equates
 * to 30 allocations per device.
 */
#ifdef _KERNEL
int zfs_vdev_queue_depth_pct = 1000;
#else
int zfs_vdev_queue_depth_pct = 300;
#endif

/*
 * When performing allocations for a given metaslab, we want to make sure that
 * there are enough IOs to aggregate together to improve throughput. We want to
 * ensure that there are at least 128k worth of IOs that can be aggregated, and
 * we assume that the average allocation size is 4k, so we need the queue depth
 * to be 32 per allocator to get good aggregation of sequential writes.
 */
int zfs_vdev_def_queue_depth = 32;

/*
 * Allow TRIM I/Os to be aggregated.  This should normally not be needed since
 * TRIM I/O for extents up to zfs_trim_extent_bytes_max (128M) can be submitted
 * by the TRIM code in zfs_trim.c.
 */
int zfs_vdev_aggregate_trim = 0;

static int
vdev_queue_offset_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = (const zio_t *)x1;
	const zio_t *z2 = (const zio_t *)x2;

	int cmp = TREE_CMP(z1->io_offset, z2->io_offset);

	if (likely(cmp))
		return (cmp);

	return (TREE_PCMP(z1, z2));
}

static inline avl_tree_t *
vdev_queue_class_tree(vdev_queue_t *vq, zio_priority_t p)
{
	return (&vq->vq_class[p].vqc_queued_tree);
}

static inline avl_tree_t *
vdev_queue_type_tree(vdev_queue_t *vq, zio_type_t t)
{
	ASSERT(t == ZIO_TYPE_READ || t == ZIO_TYPE_WRITE || t == ZIO_TYPE_TRIM);
	if (t == ZIO_TYPE_READ)
		return (&vq->vq_read_offset_tree);
	else if (t == ZIO_TYPE_WRITE)
		return (&vq->vq_write_offset_tree);
	else
		return (&vq->vq_trim_offset_tree);
}

static int
vdev_queue_timestamp_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = (const zio_t *)x1;
	const zio_t *z2 = (const zio_t *)x2;

	int cmp = TREE_CMP(z1->io_timestamp, z2->io_timestamp);

	if (likely(cmp))
		return (cmp);

	return (TREE_PCMP(z1, z2));
}

static int
vdev_queue_class_min_active(vdev_queue_t *vq, zio_priority_t p)
{
	switch (p) {
	case ZIO_PRIORITY_SYNC_READ:
		return (zfs_vdev_sync_read_min_active);
	case ZIO_PRIORITY_SYNC_WRITE:
		return (zfs_vdev_sync_write_min_active);
	case ZIO_PRIORITY_ASYNC_READ:
		return (zfs_vdev_async_read_min_active);
	case ZIO_PRIORITY_ASYNC_WRITE:
		return (zfs_vdev_async_write_min_active);
	case ZIO_PRIORITY_SCRUB:
		return (vq->vq_ia_active == 0 ? zfs_vdev_scrub_min_active :
		    MIN(vq->vq_nia_credit, zfs_vdev_scrub_min_active));
	case ZIO_PRIORITY_REMOVAL:
		return (vq->vq_ia_active == 0 ? zfs_vdev_removal_min_active :
		    MIN(vq->vq_nia_credit, zfs_vdev_removal_min_active));
	case ZIO_PRIORITY_INITIALIZING:
		return (vq->vq_ia_active == 0 ?zfs_vdev_initializing_min_active:
		    MIN(vq->vq_nia_credit, zfs_vdev_initializing_min_active));
	case ZIO_PRIORITY_TRIM:
		return (zfs_vdev_trim_min_active);
	case ZIO_PRIORITY_REBUILD:
		return (vq->vq_ia_active == 0 ? zfs_vdev_rebuild_min_active :
		    MIN(vq->vq_nia_credit, zfs_vdev_rebuild_min_active));
	default:
		panic("invalid priority %u", p);
		return (0);
	}
}

static int
vdev_queue_max_async_writes(spa_t *spa)
{
	int writes;
	uint64_t dirty = 0;
	dsl_pool_t *dp = spa_get_dsl(spa);
	uint64_t min_bytes = zfs_dirty_data_max *
	    zfs_vdev_async_write_active_min_dirty_percent / 100;
	uint64_t max_bytes = zfs_dirty_data_max *
	    zfs_vdev_async_write_active_max_dirty_percent / 100;

	/*
	 * Async writes may occur before the assignment of the spa's
	 * dsl_pool_t if a self-healing zio is issued prior to the
	 * completion of dmu_objset_open_impl().
	 */
	if (dp == NULL)
		return (zfs_vdev_async_write_max_active);

	/*
	 * Sync tasks correspond to interactive user actions. To reduce the
	 * execution time of those actions we push data out as fast as possible.
	 */
	if (spa_has_pending_synctask(spa))
		return (zfs_vdev_async_write_max_active);

	dirty = dp->dp_dirty_total;
	if (dirty < min_bytes)
		return (zfs_vdev_async_write_min_active);
	if (dirty > max_bytes)
		return (zfs_vdev_async_write_max_active);

	/*
	 * linear interpolation:
	 * slope = (max_writes - min_writes) / (max_bytes - min_bytes)
	 * move right by min_bytes
	 * move up by min_writes
	 */
	writes = (dirty - min_bytes) *
	    (zfs_vdev_async_write_max_active -
	    zfs_vdev_async_write_min_active) /
	    (max_bytes - min_bytes) +
	    zfs_vdev_async_write_min_active;
	ASSERT3U(writes, >=, zfs_vdev_async_write_min_active);
	ASSERT3U(writes, <=, zfs_vdev_async_write_max_active);
	return (writes);
}

static int
vdev_queue_class_max_active(spa_t *spa, vdev_queue_t *vq, zio_priority_t p)
{
	switch (p) {
	case ZIO_PRIORITY_SYNC_READ:
		return (zfs_vdev_sync_read_max_active);
	case ZIO_PRIORITY_SYNC_WRITE:
		return (zfs_vdev_sync_write_max_active);
	case ZIO_PRIORITY_ASYNC_READ:
		return (zfs_vdev_async_read_max_active);
	case ZIO_PRIORITY_ASYNC_WRITE:
		return (vdev_queue_max_async_writes(spa));
	case ZIO_PRIORITY_SCRUB:
		if (vq->vq_ia_active > 0) {
			return (MIN(vq->vq_nia_credit,
			    zfs_vdev_scrub_min_active));
		} else if (vq->vq_nia_credit < zfs_vdev_nia_delay)
			return (MAX(1, zfs_vdev_scrub_min_active));
		return (zfs_vdev_scrub_max_active);
	case ZIO_PRIORITY_REMOVAL:
		if (vq->vq_ia_active > 0) {
			return (MIN(vq->vq_nia_credit,
			    zfs_vdev_removal_min_active));
		} else if (vq->vq_nia_credit < zfs_vdev_nia_delay)
			return (MAX(1, zfs_vdev_removal_min_active));
		return (zfs_vdev_removal_max_active);
	case ZIO_PRIORITY_INITIALIZING:
		if (vq->vq_ia_active > 0) {
			return (MIN(vq->vq_nia_credit,
			    zfs_vdev_initializing_min_active));
		} else if (vq->vq_nia_credit < zfs_vdev_nia_delay)
			return (MAX(1, zfs_vdev_initializing_min_active));
		return (zfs_vdev_initializing_max_active);
	case ZIO_PRIORITY_TRIM:
		return (zfs_vdev_trim_max_active);
	case ZIO_PRIORITY_REBUILD:
		if (vq->vq_ia_active > 0) {
			return (MIN(vq->vq_nia_credit,
			    zfs_vdev_rebuild_min_active));
		} else if (vq->vq_nia_credit < zfs_vdev_nia_delay)
			return (MAX(1, zfs_vdev_rebuild_min_active));
		return (zfs_vdev_rebuild_max_active);
	default:
		panic("invalid priority %u", p);
		return (0);
	}
}

/*
 * Return the i/o class to issue from, or ZIO_PRIORITY_MAX_QUEUEABLE if
 * there is no eligible class.
 */
static zio_priority_t
vdev_queue_class_to_issue(vdev_queue_t *vq)
{
	spa_t *spa = vq->vq_vdev->vdev_spa;
	zio_priority_t p, n;

	if (avl_numnodes(&vq->vq_active_tree) >= zfs_vdev_max_active)
		return (ZIO_PRIORITY_NUM_QUEUEABLE);

	/*
	 * Find a queue that has not reached its minimum # outstanding i/os.
	 * Do round-robin to reduce starvation due to zfs_vdev_max_active
	 * and vq_nia_credit limits.
	 */
	for (n = 0; n < ZIO_PRIORITY_NUM_QUEUEABLE; n++) {
		p = (vq->vq_last_prio + n + 1) % ZIO_PRIORITY_NUM_QUEUEABLE;
		if (avl_numnodes(vdev_queue_class_tree(vq, p)) > 0 &&
		    vq->vq_class[p].vqc_active <
		    vdev_queue_class_min_active(vq, p)) {
			vq->vq_last_prio = p;
			return (p);
		}
	}

	/*
	 * If we haven't found a queue, look for one that hasn't reached its
	 * maximum # outstanding i/os.
	 */
	for (p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++) {
		if (avl_numnodes(vdev_queue_class_tree(vq, p)) > 0 &&
		    vq->vq_class[p].vqc_active <
		    vdev_queue_class_max_active(spa, vq, p)) {
			vq->vq_last_prio = p;
			return (p);
		}
	}

	/* No eligible queued i/os */
	return (ZIO_PRIORITY_NUM_QUEUEABLE);
}

void
vdev_queue_init(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;
	zio_priority_t p;

	mutex_init(&vq->vq_lock, NULL, MUTEX_DEFAULT, NULL);
	vq->vq_vdev = vd;
	taskq_init_ent(&vd->vdev_queue.vq_io_search.io_tqent);

	avl_create(&vq->vq_active_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_queue_node));
	avl_create(vdev_queue_type_tree(vq, ZIO_TYPE_READ),
	    vdev_queue_offset_compare, sizeof (zio_t),
	    offsetof(struct zio, io_offset_node));
	avl_create(vdev_queue_type_tree(vq, ZIO_TYPE_WRITE),
	    vdev_queue_offset_compare, sizeof (zio_t),
	    offsetof(struct zio, io_offset_node));
	avl_create(vdev_queue_type_tree(vq, ZIO_TYPE_TRIM),
	    vdev_queue_offset_compare, sizeof (zio_t),
	    offsetof(struct zio, io_offset_node));

	for (p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++) {
		int (*compfn) (const void *, const void *);

		/*
		 * The synchronous/trim i/o queues are dispatched in FIFO rather
		 * than LBA order. This provides more consistent latency for
		 * these i/os.
		 */
		if (p == ZIO_PRIORITY_SYNC_READ ||
		    p == ZIO_PRIORITY_SYNC_WRITE ||
		    p == ZIO_PRIORITY_TRIM) {
			compfn = vdev_queue_timestamp_compare;
		} else {
			compfn = vdev_queue_offset_compare;
		}
		avl_create(vdev_queue_class_tree(vq, p), compfn,
		    sizeof (zio_t), offsetof(struct zio, io_queue_node));
	}

	vq->vq_last_offset = 0;
}

void
vdev_queue_fini(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;

	for (zio_priority_t p = 0; p < ZIO_PRIORITY_NUM_QUEUEABLE; p++)
		avl_destroy(vdev_queue_class_tree(vq, p));
	avl_destroy(&vq->vq_active_tree);
	avl_destroy(vdev_queue_type_tree(vq, ZIO_TYPE_READ));
	avl_destroy(vdev_queue_type_tree(vq, ZIO_TYPE_WRITE));
	avl_destroy(vdev_queue_type_tree(vq, ZIO_TYPE_TRIM));

	mutex_destroy(&vq->vq_lock);
}

static void
vdev_queue_io_add(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	spa_history_kstat_t *shk = &spa->spa_stats.io_history;

	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	avl_add(vdev_queue_class_tree(vq, zio->io_priority), zio);
	avl_add(vdev_queue_type_tree(vq, zio->io_type), zio);

	if (shk->kstat != NULL) {
		mutex_enter(&shk->lock);
		kstat_waitq_enter(shk->kstat->ks_data);
		mutex_exit(&shk->lock);
	}
}

static void
vdev_queue_io_remove(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	spa_history_kstat_t *shk = &spa->spa_stats.io_history;

	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	avl_remove(vdev_queue_class_tree(vq, zio->io_priority), zio);
	avl_remove(vdev_queue_type_tree(vq, zio->io_type), zio);

	if (shk->kstat != NULL) {
		mutex_enter(&shk->lock);
		kstat_waitq_exit(shk->kstat->ks_data);
		mutex_exit(&shk->lock);
	}
}

static boolean_t
vdev_queue_is_interactive(zio_priority_t p)
{
	switch (p) {
	case ZIO_PRIORITY_SCRUB:
	case ZIO_PRIORITY_REMOVAL:
	case ZIO_PRIORITY_INITIALIZING:
	case ZIO_PRIORITY_REBUILD:
		return (B_FALSE);
	default:
		return (B_TRUE);
	}
}

static void
vdev_queue_pending_add(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	spa_history_kstat_t *shk = &spa->spa_stats.io_history;

	ASSERT(MUTEX_HELD(&vq->vq_lock));
	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	vq->vq_class[zio->io_priority].vqc_active++;
	if (vdev_queue_is_interactive(zio->io_priority)) {
		if (++vq->vq_ia_active == 1)
			vq->vq_nia_credit = 1;
	} else if (vq->vq_ia_active > 0) {
		vq->vq_nia_credit--;
	}
	avl_add(&vq->vq_active_tree, zio);

	if (shk->kstat != NULL) {
		mutex_enter(&shk->lock);
		kstat_runq_enter(shk->kstat->ks_data);
		mutex_exit(&shk->lock);
	}
}

static void
vdev_queue_pending_remove(vdev_queue_t *vq, zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	spa_history_kstat_t *shk = &spa->spa_stats.io_history;

	ASSERT(MUTEX_HELD(&vq->vq_lock));
	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	vq->vq_class[zio->io_priority].vqc_active--;
	if (vdev_queue_is_interactive(zio->io_priority)) {
		if (--vq->vq_ia_active == 0)
			vq->vq_nia_credit = 0;
		else
			vq->vq_nia_credit = zfs_vdev_nia_credit;
	} else if (vq->vq_ia_active == 0)
		vq->vq_nia_credit++;
	avl_remove(&vq->vq_active_tree, zio);

	if (shk->kstat != NULL) {
		kstat_io_t *ksio = shk->kstat->ks_data;

		mutex_enter(&shk->lock);
		kstat_runq_exit(ksio);
		if (zio->io_type == ZIO_TYPE_READ) {
			ksio->reads++;
			ksio->nread += zio->io_size;
		} else if (zio->io_type == ZIO_TYPE_WRITE) {
			ksio->writes++;
			ksio->nwritten += zio->io_size;
		}
		mutex_exit(&shk->lock);
	}
}

static void
vdev_queue_agg_io_done(zio_t *aio)
{
	abd_free(aio->io_abd);
}

/*
 * Compute the range spanned by two i/os, which is the endpoint of the last
 * (lio->io_offset + lio->io_size) minus start of the first (fio->io_offset).
 * Conveniently, the gap between fio and lio is given by -IO_SPAN(lio, fio);
 * thus fio and lio are adjacent if and only if IO_SPAN(lio, fio) == 0.
 */
#define	IO_SPAN(fio, lio) ((lio)->io_offset + (lio)->io_size - (fio)->io_offset)
#define	IO_GAP(fio, lio) (-IO_SPAN(lio, fio))

/*
 * Sufficiently adjacent io_offset's in ZIOs will be aggregated. We do this
 * by creating a gang ABD from the adjacent ZIOs io_abd's. By using
 * a gang ABD we avoid doing memory copies to and from the parent,
 * child ZIOs. The gang ABD also accounts for gaps between adjacent
 * io_offsets by simply getting the zero ABD for writes or allocating
 * a new ABD for reads and placing them in the gang ABD as well.
 */
static zio_t *
vdev_queue_aggregate(vdev_queue_t *vq, zio_t *zio)
{
	zio_t *first, *last, *aio, *dio, *mandatory, *nio;
	zio_link_t *zl = NULL;
	uint64_t maxgap = 0;
	uint64_t size;
	uint64_t limit;
	int maxblocksize;
	boolean_t stretch = B_FALSE;
	avl_tree_t *t = vdev_queue_type_tree(vq, zio->io_type);
	enum zio_flag flags = zio->io_flags & ZIO_FLAG_AGG_INHERIT;
	uint64_t next_offset;
	abd_t *abd;

	maxblocksize = spa_maxblocksize(vq->vq_vdev->vdev_spa);
	if (vq->vq_vdev->vdev_nonrot)
		limit = zfs_vdev_aggregation_limit_non_rotating;
	else
		limit = zfs_vdev_aggregation_limit;
	limit = MAX(MIN(limit, maxblocksize), 0);

	if (zio->io_flags & ZIO_FLAG_DONT_AGGREGATE || limit == 0)
		return (NULL);

	/*
	 * While TRIM commands could be aggregated based on offset this
	 * behavior is disabled until it's determined to be beneficial.
	 */
	if (zio->io_type == ZIO_TYPE_TRIM && !zfs_vdev_aggregate_trim)
		return (NULL);

	first = last = zio;

	if (zio->io_type == ZIO_TYPE_READ)
		maxgap = zfs_vdev_read_gap_limit;

	/*
	 * We can aggregate I/Os that are sufficiently adjacent and of
	 * the same flavor, as expressed by the AGG_INHERIT flags.
	 * The latter requirement is necessary so that certain
	 * attributes of the I/O, such as whether it's a normal I/O
	 * or a scrub/resilver, can be preserved in the aggregate.
	 * We can include optional I/Os, but don't allow them
	 * to begin a range as they add no benefit in that situation.
	 */

	/*
	 * We keep track of the last non-optional I/O.
	 */
	mandatory = (first->io_flags & ZIO_FLAG_OPTIONAL) ? NULL : first;

	/*
	 * Walk backwards through sufficiently contiguous I/Os
	 * recording the last non-optional I/O.
	 */
	while ((dio = AVL_PREV(t, first)) != NULL &&
	    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
	    IO_SPAN(dio, last) <= limit &&
	    IO_GAP(dio, first) <= maxgap &&
	    dio->io_type == zio->io_type) {
		first = dio;
		if (mandatory == NULL && !(first->io_flags & ZIO_FLAG_OPTIONAL))
			mandatory = first;
	}

	/*
	 * Skip any initial optional I/Os.
	 */
	while ((first->io_flags & ZIO_FLAG_OPTIONAL) && first != last) {
		first = AVL_NEXT(t, first);
		ASSERT(first != NULL);
	}


	/*
	 * Walk forward through sufficiently contiguous I/Os.
	 * The aggregation limit does not apply to optional i/os, so that
	 * we can issue contiguous writes even if they are larger than the
	 * aggregation limit.
	 */
	while ((dio = AVL_NEXT(t, last)) != NULL &&
	    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
	    (IO_SPAN(first, dio) <= limit ||
	    (dio->io_flags & ZIO_FLAG_OPTIONAL)) &&
	    IO_SPAN(first, dio) <= maxblocksize &&
	    IO_GAP(last, dio) <= maxgap &&
	    dio->io_type == zio->io_type) {
		last = dio;
		if (!(last->io_flags & ZIO_FLAG_OPTIONAL))
			mandatory = last;
	}

	/*
	 * Now that we've established the range of the I/O aggregation
	 * we must decide what to do with trailing optional I/Os.
	 * For reads, there's nothing to do. While we are unable to
	 * aggregate further, it's possible that a trailing optional
	 * I/O would allow the underlying device to aggregate with
	 * subsequent I/Os. We must therefore determine if the next
	 * non-optional I/O is close enough to make aggregation
	 * worthwhile.
	 */
	if (zio->io_type == ZIO_TYPE_WRITE && mandatory != NULL) {
		zio_t *nio = last;
		while ((dio = AVL_NEXT(t, nio)) != NULL &&
		    IO_GAP(nio, dio) == 0 &&
		    IO_GAP(mandatory, dio) <= zfs_vdev_write_gap_limit) {
			nio = dio;
			if (!(nio->io_flags & ZIO_FLAG_OPTIONAL)) {
				stretch = B_TRUE;
				break;
			}
		}
	}

	if (stretch) {
		/*
		 * We are going to include an optional io in our aggregated
		 * span, thus closing the write gap.  Only mandatory i/os can
		 * start aggregated spans, so make sure that the next i/o
		 * after our span is mandatory.
		 */
		dio = AVL_NEXT(t, last);
		dio->io_flags &= ~ZIO_FLAG_OPTIONAL;
	} else {
		/* do not include the optional i/o */
		while (last != mandatory && last != first) {
			ASSERT(last->io_flags & ZIO_FLAG_OPTIONAL);
			last = AVL_PREV(t, last);
			ASSERT(last != NULL);
		}
	}

	if (first == last)
		return (NULL);

	size = IO_SPAN(first, last);
	ASSERT3U(size, <=, maxblocksize);

	abd = abd_alloc_gang_abd();
	if (abd == NULL)
		return (NULL);

	aio = zio_vdev_delegated_io(first->io_vd, first->io_offset,
	    abd, size, first->io_type, zio->io_priority,
	    flags | ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE,
	    vdev_queue_agg_io_done, NULL);
	aio->io_timestamp = first->io_timestamp;

	nio = first;
	next_offset = first->io_offset;
	do {
		dio = nio;
		nio = AVL_NEXT(t, dio);
		zio_add_child(dio, aio);
		vdev_queue_io_remove(vq, dio);

		if (dio->io_offset != next_offset) {
			/* allocate a buffer for a read gap */
			ASSERT3U(dio->io_type, ==, ZIO_TYPE_READ);
			ASSERT3U(dio->io_offset, >, next_offset);
			abd = abd_alloc_for_io(
			    dio->io_offset - next_offset, B_TRUE);
			abd_gang_add(aio->io_abd, abd, B_TRUE);
		}
		if (dio->io_abd &&
		    (dio->io_size != abd_get_size(dio->io_abd))) {
			/* abd size not the same as IO size */
			ASSERT3U(abd_get_size(dio->io_abd), >, dio->io_size);
			abd = abd_get_offset_size(dio->io_abd, 0, dio->io_size);
			abd_gang_add(aio->io_abd, abd, B_TRUE);
		} else {
			if (dio->io_flags & ZIO_FLAG_NODATA) {
				/* allocate a buffer for a write gap */
				ASSERT3U(dio->io_type, ==, ZIO_TYPE_WRITE);
				ASSERT3P(dio->io_abd, ==, NULL);
				abd_gang_add(aio->io_abd,
				    abd_get_zeros(dio->io_size), B_TRUE);
			} else {
				/*
				 * We pass B_FALSE to abd_gang_add()
				 * because we did not allocate a new
				 * ABD, so it is assumed the caller
				 * will free this ABD.
				 */
				abd_gang_add(aio->io_abd, dio->io_abd,
				    B_FALSE);
			}
		}
		next_offset = dio->io_offset + dio->io_size;
	} while (dio != last);
	ASSERT3U(abd_get_size(aio->io_abd), ==, aio->io_size);

	/*
	 * We need to drop the vdev queue's lock during zio_execute() to
	 * avoid a deadlock that we could encounter due to lock order
	 * reversal between vq_lock and io_lock in zio_change_priority().
	 */
	mutex_exit(&vq->vq_lock);
	while ((dio = zio_walk_parents(aio, &zl)) != NULL) {
		ASSERT3U(dio->io_type, ==, aio->io_type);

		zio_vdev_io_bypass(dio);
		zio_execute(dio);
	}
	mutex_enter(&vq->vq_lock);

	return (aio);
}

static zio_t *
vdev_queue_io_to_issue(vdev_queue_t *vq)
{
	zio_t *zio, *aio;
	zio_priority_t p;
	avl_index_t idx;
	avl_tree_t *tree;

again:
	ASSERT(MUTEX_HELD(&vq->vq_lock));

	p = vdev_queue_class_to_issue(vq);

	if (p == ZIO_PRIORITY_NUM_QUEUEABLE) {
		/* No eligible queued i/os */
		return (NULL);
	}

	/*
	 * For LBA-ordered queues (async / scrub / initializing), issue the
	 * i/o which follows the most recently issued i/o in LBA (offset) order.
	 *
	 * For FIFO queues (sync/trim), issue the i/o with the lowest timestamp.
	 */
	tree = vdev_queue_class_tree(vq, p);
	vq->vq_io_search.io_timestamp = 0;
	vq->vq_io_search.io_offset = vq->vq_last_offset - 1;
	VERIFY3P(avl_find(tree, &vq->vq_io_search, &idx), ==, NULL);
	zio = avl_nearest(tree, idx, AVL_AFTER);
	if (zio == NULL)
		zio = avl_first(tree);
	ASSERT3U(zio->io_priority, ==, p);

	aio = vdev_queue_aggregate(vq, zio);
	if (aio != NULL)
		zio = aio;
	else
		vdev_queue_io_remove(vq, zio);

	/*
	 * If the I/O is or was optional and therefore has no data, we need to
	 * simply discard it. We need to drop the vdev queue's lock to avoid a
	 * deadlock that we could encounter since this I/O will complete
	 * immediately.
	 */
	if (zio->io_flags & ZIO_FLAG_NODATA) {
		mutex_exit(&vq->vq_lock);
		zio_vdev_io_bypass(zio);
		zio_execute(zio);
		mutex_enter(&vq->vq_lock);
		goto again;
	}

	vdev_queue_pending_add(vq, zio);
	vq->vq_last_offset = zio->io_offset + zio->io_size;

	return (zio);
}

zio_t *
vdev_queue_io(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	zio_t *nio;

	if (zio->io_flags & ZIO_FLAG_DONT_QUEUE)
		return (zio);

	/*
	 * Children i/os inherent their parent's priority, which might
	 * not match the child's i/o type.  Fix it up here.
	 */
	if (zio->io_type == ZIO_TYPE_READ) {
		ASSERT(zio->io_priority != ZIO_PRIORITY_TRIM);

		if (zio->io_priority != ZIO_PRIORITY_SYNC_READ &&
		    zio->io_priority != ZIO_PRIORITY_ASYNC_READ &&
		    zio->io_priority != ZIO_PRIORITY_SCRUB &&
		    zio->io_priority != ZIO_PRIORITY_REMOVAL &&
		    zio->io_priority != ZIO_PRIORITY_INITIALIZING &&
		    zio->io_priority != ZIO_PRIORITY_REBUILD) {
			zio->io_priority = ZIO_PRIORITY_ASYNC_READ;
		}
	} else if (zio->io_type == ZIO_TYPE_WRITE) {
		ASSERT(zio->io_priority != ZIO_PRIORITY_TRIM);

		if (zio->io_priority != ZIO_PRIORITY_SYNC_WRITE &&
		    zio->io_priority != ZIO_PRIORITY_ASYNC_WRITE &&
		    zio->io_priority != ZIO_PRIORITY_REMOVAL &&
		    zio->io_priority != ZIO_PRIORITY_INITIALIZING &&
		    zio->io_priority != ZIO_PRIORITY_REBUILD) {
			zio->io_priority = ZIO_PRIORITY_ASYNC_WRITE;
		}
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_TRIM);
		ASSERT(zio->io_priority == ZIO_PRIORITY_TRIM);
	}

	zio->io_flags |= ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE;

	mutex_enter(&vq->vq_lock);
	zio->io_timestamp = gethrtime();
	vdev_queue_io_add(vq, zio);
	nio = vdev_queue_io_to_issue(vq);
	mutex_exit(&vq->vq_lock);

	if (nio == NULL)
		return (NULL);

	if (nio->io_done == vdev_queue_agg_io_done) {
		zio_nowait(nio);
		return (NULL);
	}

	return (nio);
}

void
vdev_queue_io_done(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	zio_t *nio;

	mutex_enter(&vq->vq_lock);

	vdev_queue_pending_remove(vq, zio);

	zio->io_delta = gethrtime() - zio->io_timestamp;
	vq->vq_io_complete_ts = gethrtime();
	vq->vq_io_delta_ts = vq->vq_io_complete_ts - zio->io_timestamp;

	while ((nio = vdev_queue_io_to_issue(vq)) != NULL) {
		mutex_exit(&vq->vq_lock);
		if (nio->io_done == vdev_queue_agg_io_done) {
			zio_nowait(nio);
		} else {
			zio_vdev_io_reissue(nio);
			zio_execute(nio);
		}
		mutex_enter(&vq->vq_lock);
	}

	mutex_exit(&vq->vq_lock);
}

void
vdev_queue_change_io_priority(zio_t *zio, zio_priority_t priority)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	avl_tree_t *tree;

	/*
	 * ZIO_PRIORITY_NOW is used by the vdev cache code and the aggregate zio
	 * code to issue IOs without adding them to the vdev queue. In this
	 * case, the zio is already going to be issued as quickly as possible
	 * and so it doesn't need any reprioritization to help.
	 */
	if (zio->io_priority == ZIO_PRIORITY_NOW)
		return;

	ASSERT3U(zio->io_priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);
	ASSERT3U(priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);

	if (zio->io_type == ZIO_TYPE_READ) {
		if (priority != ZIO_PRIORITY_SYNC_READ &&
		    priority != ZIO_PRIORITY_ASYNC_READ &&
		    priority != ZIO_PRIORITY_SCRUB)
			priority = ZIO_PRIORITY_ASYNC_READ;
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);
		if (priority != ZIO_PRIORITY_SYNC_WRITE &&
		    priority != ZIO_PRIORITY_ASYNC_WRITE)
			priority = ZIO_PRIORITY_ASYNC_WRITE;
	}

	mutex_enter(&vq->vq_lock);

	/*
	 * If the zio is in none of the queues we can simply change
	 * the priority. If the zio is waiting to be submitted we must
	 * remove it from the queue and re-insert it with the new priority.
	 * Otherwise, the zio is currently active and we cannot change its
	 * priority.
	 */
	tree = vdev_queue_class_tree(vq, zio->io_priority);
	if (avl_find(tree, zio, NULL) == zio) {
		avl_remove(vdev_queue_class_tree(vq, zio->io_priority), zio);
		zio->io_priority = priority;
		avl_add(vdev_queue_class_tree(vq, zio->io_priority), zio);
	} else if (avl_find(&vq->vq_active_tree, zio, NULL) != zio) {
		zio->io_priority = priority;
	}

	mutex_exit(&vq->vq_lock);
}

/*
 * As these two methods are only used for load calculations we're not
 * concerned if we get an incorrect value on 32bit platforms due to lack of
 * vq_lock mutex use here, instead we prefer to keep it lock free for
 * performance.
 */
int
vdev_queue_length(vdev_t *vd)
{
	return (avl_numnodes(&vd->vdev_queue.vq_active_tree));
}

uint64_t
vdev_queue_last_offset(vdev_t *vd)
{
	return (vd->vdev_queue.vq_last_offset);
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, aggregation_limit, INT, ZMOD_RW,
	"Max vdev I/O aggregation size");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, aggregation_limit_non_rotating, INT, ZMOD_RW,
	"Max vdev I/O aggregation size for non-rotating media");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, aggregate_trim, INT, ZMOD_RW,
	"Allow TRIM I/O to be aggregated");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, read_gap_limit, INT, ZMOD_RW,
	"Aggregate read I/O over gap");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, write_gap_limit, INT, ZMOD_RW,
	"Aggregate write I/O over gap");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, max_active, INT, ZMOD_RW,
	"Maximum number of active I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, async_write_active_max_dirty_percent, INT, ZMOD_RW,
	"Async write concurrency max threshold");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, async_write_active_min_dirty_percent, INT, ZMOD_RW,
	"Async write concurrency min threshold");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, async_read_max_active, INT, ZMOD_RW,
	"Max active async read I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, async_read_min_active, INT, ZMOD_RW,
	"Min active async read I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, async_write_max_active, INT, ZMOD_RW,
	"Max active async write I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, async_write_min_active, INT, ZMOD_RW,
	"Min active async write I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, initializing_max_active, INT, ZMOD_RW,
	"Max active initializing I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, initializing_min_active, INT, ZMOD_RW,
	"Min active initializing I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, removal_max_active, INT, ZMOD_RW,
	"Max active removal I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, removal_min_active, INT, ZMOD_RW,
	"Min active removal I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, scrub_max_active, INT, ZMOD_RW,
	"Max active scrub I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, scrub_min_active, INT, ZMOD_RW,
	"Min active scrub I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, sync_read_max_active, INT, ZMOD_RW,
	"Max active sync read I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, sync_read_min_active, INT, ZMOD_RW,
	"Min active sync read I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, sync_write_max_active, INT, ZMOD_RW,
	"Max active sync write I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, sync_write_min_active, INT, ZMOD_RW,
	"Min active sync write I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, trim_max_active, INT, ZMOD_RW,
	"Max active trim/discard I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, trim_min_active, INT, ZMOD_RW,
	"Min active trim/discard I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, rebuild_max_active, INT, ZMOD_RW,
	"Max active rebuild I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, rebuild_min_active, INT, ZMOD_RW,
	"Min active rebuild I/Os per vdev");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, nia_credit, INT, ZMOD_RW,
	"Number of non-interactive I/Os to allow in sequence");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, nia_delay, INT, ZMOD_RW,
	"Number of non-interactive I/Os before _max_active");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, queue_depth_pct, INT, ZMOD_RW,
	"Queue depth percentage for each top-level vdev");
/* END CSTYLED */
