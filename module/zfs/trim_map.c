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
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/trim_map.h>

typedef struct trim_map {
	list_t		tm_head;		/* List of segments sorted by txg. */
	avl_tree_t	tm_queued_frees;	/* AVL tree of segments waiting for TRIM. */
	avl_tree_t	tm_inflight_frees;	/* AVL tree of in-flight TRIMs. */
	avl_tree_t	tm_inflight_writes;	/* AVL tree of in-flight writes. */
	list_t		tm_pending_writes;	/* Writes blocked on in-flight frees. */
	kmutex_t	tm_lock;
} trim_map_t;

typedef struct trim_seg {
	avl_node_t	ts_node;	/* AVL node. */
	list_node_t	ts_next;	/* List element. */
	uint64_t	ts_start;	/* Starting offset of this segment. */
	uint64_t	ts_end;		/* Ending offset (non-inclusive). */
	uint64_t	ts_txg;		/* Segment creation txg. */
} trim_seg_t;

int trim_txg_limit = 64;

static void trim_map_vdev_commit_done(spa_t *spa, vdev_t *vd);

static int
trim_map_seg_compare(const void *x1, const void *x2)
{
	const trim_seg_t *s1 = x1;
	const trim_seg_t *s2 = x2;

	if (s1->ts_start < s2->ts_start) {
		if (s1->ts_end > s2->ts_start)
			return (0);
		return (-1);
	}
	if (s1->ts_start > s2->ts_start) {
		if (s1->ts_start < s2->ts_end)
			return (0);
		return (1);
	}
	return (0);
}

static int
trim_map_zio_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_offset < z2->io_offset) {
		if (z1->io_offset + z1->io_size > z2->io_offset)
			return (0);
		return (-1);
	}
	if (z1->io_offset > z2->io_offset) {
		if (z1->io_offset < z2->io_offset + z2->io_size)
			return (0);
		return (1);
	}
	return (0);
}

void
trim_map_create(vdev_t *vd)
{
	trim_map_t *tm;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (zfs_notrim)
		return;

	tm = kmem_zalloc(sizeof (*tm), KM_SLEEP);
	mutex_init(&tm->tm_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&tm->tm_head, sizeof (trim_seg_t),
	    offsetof(trim_seg_t, ts_next));
	list_create(&tm->tm_pending_writes, sizeof (zio_t),
	    offsetof(zio_t, io_trim_link));
	avl_create(&tm->tm_queued_frees, trim_map_seg_compare,
	    sizeof (trim_seg_t), offsetof(trim_seg_t, ts_node));
	avl_create(&tm->tm_inflight_frees, trim_map_seg_compare,
	    sizeof (trim_seg_t), offsetof(trim_seg_t, ts_node));
	avl_create(&tm->tm_inflight_writes, trim_map_zio_compare,
	    sizeof (zio_t), offsetof(zio_t, io_trim_node));
	vd->vdev_trimmap = tm;
}

void
trim_map_destroy(vdev_t *vd)
{
	trim_map_t *tm;
	trim_seg_t *ts;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (zfs_notrim)
		return;

	tm = vd->vdev_trimmap;
	if (tm == NULL)
		return;

	/*
	 * We may have been called before trim_map_vdev_commit_done()
	 * had a chance to run, so do it now to prune the remaining
	 * inflight frees.
	 */
	trim_map_vdev_commit_done(vd->vdev_spa, vd);

	mutex_enter(&tm->tm_lock);
	while ((ts = list_head(&tm->tm_head)) != NULL) {
		avl_remove(&tm->tm_queued_frees, ts);
		list_remove(&tm->tm_head, ts);
		kmem_free(ts, sizeof (*ts));
	}
	mutex_exit(&tm->tm_lock);

	avl_destroy(&tm->tm_queued_frees);
	avl_destroy(&tm->tm_inflight_frees);
	avl_destroy(&tm->tm_inflight_writes);
	list_destroy(&tm->tm_pending_writes);
	list_destroy(&tm->tm_head);
	mutex_destroy(&tm->tm_lock);
	kmem_free(tm, sizeof (*tm));
	vd->vdev_trimmap = NULL;
}

static void
trim_map_segment_add(trim_map_t *tm, uint64_t start, uint64_t end, uint64_t txg)
{
	avl_index_t where;
	trim_seg_t tsearch, *ts_before, *ts_after, *ts;
	boolean_t merge_before, merge_after;

	ASSERT(MUTEX_HELD(&tm->tm_lock));
	VERIFY(start < end);

	tsearch.ts_start = start;
	tsearch.ts_end = end;

	ts = avl_find(&tm->tm_queued_frees, &tsearch, &where);
	if (ts != NULL) {
		if (start < ts->ts_start)
			trim_map_segment_add(tm, start, ts->ts_start, txg);
		if (end > ts->ts_end)
			trim_map_segment_add(tm, ts->ts_end, end, txg);
		return;
	}

	ts_before = avl_nearest(&tm->tm_queued_frees, where, AVL_BEFORE);
	ts_after = avl_nearest(&tm->tm_queued_frees, where, AVL_AFTER);

	merge_before = (ts_before != NULL && ts_before->ts_end == start &&
	    ts_before->ts_txg == txg);
	merge_after = (ts_after != NULL && ts_after->ts_start == end &&
	    ts_after->ts_txg == txg);

	if (merge_before && merge_after) {
		avl_remove(&tm->tm_queued_frees, ts_before);
		list_remove(&tm->tm_head, ts_before);
		ts_after->ts_start = ts_before->ts_start;
		kmem_free(ts_before, sizeof (*ts_before));
	} else if (merge_before) {
		ts_before->ts_end = end;
	} else if (merge_after) {
		ts_after->ts_start = start;
	} else {
		ts = kmem_alloc(sizeof (*ts), KM_SLEEP);
		ts->ts_start = start;
		ts->ts_end = end;
		ts->ts_txg = txg;
		avl_insert(&tm->tm_queued_frees, ts, where);
		list_insert_tail(&tm->tm_head, ts);
	}
}

static void
trim_map_segment_remove(trim_map_t *tm, trim_seg_t *ts, uint64_t start,
    uint64_t end)
{
	trim_seg_t *nts;
	boolean_t left_over, right_over;

	ASSERT(MUTEX_HELD(&tm->tm_lock));

	left_over = (ts->ts_start < start);
	right_over = (ts->ts_end > end);

	if (left_over && right_over) {
		nts = kmem_alloc(sizeof (*nts), KM_SLEEP);
		nts->ts_start = end;
		nts->ts_end = ts->ts_end;
		nts->ts_txg = ts->ts_txg;
		ts->ts_end = start;
		avl_insert_here(&tm->tm_queued_frees, nts, ts, AVL_AFTER);
		list_insert_after(&tm->tm_head, ts, nts);
	} else if (left_over) {
		ts->ts_end = start;
	} else if (right_over) {
		ts->ts_start = end;
	} else {
		avl_remove(&tm->tm_queued_frees, ts);
		list_remove(&tm->tm_head, ts);
		kmem_free(ts, sizeof (*ts));
	}
}

static void
trim_map_free_locked(trim_map_t *tm, uint64_t start, uint64_t end, uint64_t txg)
{
	zio_t *zs;
	/*
	 * Declaring a zio_t variable on the stack makes the frame size too
	 * large for the taste of GCC. We work around the issue by allocating
	 * just the fields we actually need.
	 */
	char zsearch_buffer[offsetof(zio_t, io_offset) + sizeof(zs->io_offset)];
	zio_t *zsearch = (zio_t *)zsearch_buffer;

	ASSERT(MUTEX_HELD(&tm->tm_lock));

	zsearch->io_offset = start;
	zsearch->io_size = end - start;

	zs = avl_find(&tm->tm_inflight_writes, zsearch, NULL);
	if (zs == NULL) {
		trim_map_segment_add(tm, start, end, txg);
		return;
	}
	if (start < zs->io_offset)
		trim_map_free_locked(tm, start, zs->io_offset, txg);
	if (zs->io_offset + zs->io_size < end)
		trim_map_free_locked(tm, zs->io_offset + zs->io_size, end, txg);
}

void
trim_map_free(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	trim_map_t *tm = vd->vdev_trimmap;

	if (zfs_notrim || vd->vdev_notrim || tm == NULL)
		return;

	mutex_enter(&tm->tm_lock);
	trim_map_free_locked(tm, zio->io_offset, zio->io_offset + zio->io_size,
	    vd->vdev_spa->spa_syncing_txg);
	mutex_exit(&tm->tm_lock);
}

boolean_t
trim_map_write_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	trim_map_t *tm = vd->vdev_trimmap;
	trim_seg_t tsearch, *ts;
	uint64_t start, end;

	if (zfs_notrim || vd->vdev_notrim || tm == NULL)
		return (B_TRUE);

	start = zio->io_offset;
	end = start + zio->io_size;
	tsearch.ts_start = start;
	tsearch.ts_end = end;

	mutex_enter(&tm->tm_lock);

	/*
	 * Checking for colliding in-flight frees.
	 */
	ts = avl_find(&tm->tm_inflight_frees, &tsearch, NULL);
	if (ts != NULL) {
		list_insert_tail(&tm->tm_pending_writes, zio);
		mutex_exit(&tm->tm_lock);
		return (B_FALSE);
	}

	ts = avl_find(&tm->tm_queued_frees, &tsearch, NULL);
	if (ts != NULL) {
		/*
		 * Loop until all overlapping segments are removed.
		 */
		do {
			trim_map_segment_remove(tm, ts, start, end);
			ts = avl_find(&tm->tm_queued_frees, &tsearch, NULL);
		} while (ts != NULL);
	}
	avl_add(&tm->tm_inflight_writes, zio);

	mutex_exit(&tm->tm_lock);

	return (B_TRUE);
}

void
trim_map_write_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	trim_map_t *tm = vd->vdev_trimmap;

	/*
	 * Don't check for vdev_notrim, since the write could have
	 * started before vdev_notrim was set.
	 */
	if (zfs_notrim || tm == NULL)
		return;

	mutex_enter(&tm->tm_lock);
	/*
	 * Don't fail if the write isn't in the tree, since the write
	 * could have started after vdev_notrim was set.
	 */
	if (zio->io_trim_node.avl_child[0] ||
	    zio->io_trim_node.avl_child[1] ||
	    AVL_XPARENT(&zio->io_trim_node) ||
	    tm->tm_inflight_writes.avl_root == &zio->io_trim_node)
		avl_remove(&tm->tm_inflight_writes, zio);
	mutex_exit(&tm->tm_lock);
}

/*
 * Return the oldest segment (the one with the lowest txg) or false if
 * the list is empty or the first element's txg is greater than txg given
 * as function argument.
 */
static trim_seg_t *
trim_map_first(trim_map_t *tm, uint64_t txg)
{
	trim_seg_t *ts;

	ASSERT(MUTEX_HELD(&tm->tm_lock));

	ts = list_head(&tm->tm_head);
	if (ts != NULL && ts->ts_txg <= txg)
		return (ts);
	return (NULL);
}

static void
trim_map_vdev_commit(spa_t *spa, zio_t *zio, vdev_t *vd)
{
	trim_map_t *tm = vd->vdev_trimmap;
	trim_seg_t *ts;
	uint64_t txglimit;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (tm == NULL)
		return;

	txglimit = MIN(spa->spa_syncing_txg, spa_freeze_txg(spa)) -
	    trim_txg_limit;

	mutex_enter(&tm->tm_lock);
	/*
	 * Loop until we send all frees up to the txglimit.
	 */
	while ((ts = trim_map_first(tm, txglimit)) != NULL) {
		list_remove(&tm->tm_head, ts);
		avl_remove(&tm->tm_queued_frees, ts);
		avl_add(&tm->tm_inflight_frees, ts);
		zio_trim(zio, vd, ts->ts_start, ts->ts_end - ts->ts_start);
	}
	mutex_exit(&tm->tm_lock);
}

static void
trim_map_vdev_commit_done(spa_t *spa, vdev_t *vd)
{
	trim_map_t *tm = vd->vdev_trimmap;
	trim_seg_t *ts;
	list_t pending_writes;
	zio_t *zio;
	void *cookie;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if (tm == NULL)
		return;

	mutex_enter(&tm->tm_lock);
	if (!avl_is_empty(&tm->tm_inflight_frees)) {
		cookie = NULL;
		while ((ts = avl_destroy_nodes(&tm->tm_inflight_frees,
		    &cookie)) != NULL) {
			kmem_free(ts, sizeof (*ts));
		}
	}
	list_create(&pending_writes, sizeof (zio_t), offsetof(zio_t,
	    io_trim_link));
	list_move_tail(&pending_writes, &tm->tm_pending_writes);
	mutex_exit(&tm->tm_lock);

	while ((zio = list_remove_head(&pending_writes)) != NULL) {
		zio_vdev_io_reissue(zio);
		zio_execute(zio);
	}
	list_destroy(&pending_writes);
}

static void
trim_map_commit(spa_t *spa, zio_t *zio, vdev_t *vd)
{
	int c;

	if (vd == NULL || spa->spa_syncing_txg <= trim_txg_limit)
		return;

	if (vd->vdev_ops->vdev_op_leaf) {
		trim_map_vdev_commit(spa, zio, vd);
	} else {
		for (c = 0; c < vd->vdev_children; c++)
			trim_map_commit(spa, zio, vd->vdev_child[c]);
	}
}

static void
trim_map_commit_done(spa_t *spa, vdev_t *vd)
{
	int c;

	if (vd == NULL)
		return;

	if (vd->vdev_ops->vdev_op_leaf) {
		trim_map_vdev_commit_done(spa, vd);
	} else {
		for (c = 0; c < vd->vdev_children; c++)
			trim_map_commit_done(spa, vd->vdev_child[c]);
	}
}

static void
trim_thread(void *arg)
{
	spa_t *spa = arg;
	zio_t *zio;

	for (;;) {
		mutex_enter(&spa->spa_trim_lock);
		if (spa->spa_trim_thread == NULL) {
			spa->spa_trim_thread = curthread;
			cv_signal(&spa->spa_trim_cv);
			mutex_exit(&spa->spa_trim_lock);
			thread_exit();
		}
		cv_wait(&spa->spa_trim_cv, &spa->spa_trim_lock);
		mutex_exit(&spa->spa_trim_lock);

		zio = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL);

		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
		trim_map_commit(spa, zio, spa->spa_root_vdev);
		(void) zio_wait(zio);
		trim_map_commit_done(spa, spa->spa_root_vdev);
		spa_config_exit(spa, SCL_STATE, FTAG);
	}
}

void
trim_thread_create(spa_t *spa)
{

	if (zfs_notrim)
		return;

	mutex_init(&spa->spa_trim_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&spa->spa_trim_cv, NULL, CV_DEFAULT, NULL);
	mutex_enter(&spa->spa_trim_lock);
	spa->spa_trim_thread = thread_create(NULL, 0, trim_thread, spa, 0, &p0,
	    TS_RUN, minclsyspri);
	mutex_exit(&spa->spa_trim_lock);
}

void
trim_thread_destroy(spa_t *spa)
{

	if (zfs_notrim)
		return;
	if (spa->spa_trim_thread == NULL)
		return;

	mutex_enter(&spa->spa_trim_lock);
	/* Setting spa_trim_thread to NULL tells the thread to stop. */
	spa->spa_trim_thread = NULL;
	cv_signal(&spa->spa_trim_cv);
	/* The thread will set it back to != NULL on exit. */
	while (spa->spa_trim_thread == NULL)
		cv_wait(&spa->spa_trim_cv, &spa->spa_trim_lock);
	spa->spa_trim_thread = NULL;
	mutex_exit(&spa->spa_trim_lock);

	cv_destroy(&spa->spa_trim_cv);
	mutex_destroy(&spa->spa_trim_lock);
}

void
trim_thread_wakeup(spa_t *spa)
{

	if (zfs_notrim)
		return;
	if (spa->spa_trim_thread == NULL)
		return;

	mutex_enter(&spa->spa_trim_lock);
	cv_signal(&spa->spa_trim_cv);
	mutex_exit(&spa->spa_trim_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_notrim, int, 0444);
MODULE_PARM_DESC(zfs_notrim, "Disable TRIM.");

module_param(trim_txg_limit, int, 0644);
MODULE_PARM_DESC(trim_txg_limit, "Delay TRIMs by that many TXGs.");
#endif

