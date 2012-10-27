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

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/avl.h>

/*
 * These tunables are for performance analysis.
 */
/*
 * zfs_vdev_max_pending is the maximum number of i/os concurrently
 * pending to each device.  zfs_vdev_min_pending is the initial number
 * of i/os pending to each device (before it starts ramping up to
 * max_pending).
 */
int zfs_vdev_max_pending = 10;
int zfs_vdev_min_pending = 4;

/* deadline = pri + ddi_get_lbolt64() >> time_shift) */
int zfs_vdev_time_shift = 6;

/* exponential I/O issue ramp-up rate */
int zfs_vdev_ramp_rate = 2;

/*
 * To reduce IOPs, we aggregate small adjacent I/Os into one large I/O.
 * For read I/Os, we also aggregate across small adjacency gaps; for writes
 * we include spans of optional I/Os to aid aggregation at the disk even when
 * they aren't able to help us aggregate at this level.
 */
int zfs_vdev_aggregation_limit = SPA_MAXBLOCKSIZE;
int zfs_vdev_read_gap_limit = 32 << 10;
int zfs_vdev_write_gap_limit = 4 << 10;

/*
 * Virtual device vector for disk I/O scheduling.
 */
int
vdev_queue_deadline_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_deadline < z2->io_deadline)
		return (-1);
	if (z1->io_deadline > z2->io_deadline)
		return (1);

	if (z1->io_offset < z2->io_offset)
		return (-1);
	if (z1->io_offset > z2->io_offset)
		return (1);

	if (z1 < z2)
		return (-1);
	if (z1 > z2)
		return (1);

	return (0);
}

int
vdev_queue_offset_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_offset < z2->io_offset)
		return (-1);
	if (z1->io_offset > z2->io_offset)
		return (1);

	if (z1 < z2)
		return (-1);
	if (z1 > z2)
		return (1);

	return (0);
}

void
vdev_queue_init(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;
	int i;

	mutex_init(&vq->vq_lock, NULL, MUTEX_DEFAULT, NULL);

	avl_create(&vq->vq_deadline_tree, vdev_queue_deadline_compare,
	    sizeof (zio_t), offsetof(struct zio, io_deadline_node));

	avl_create(&vq->vq_read_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	avl_create(&vq->vq_write_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	avl_create(&vq->vq_pending_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	/*
	 * A list of buffers which can be used for aggregate I/O, this
	 * avoids the need to allocate them on demand when memory is low.
	 */
	list_create(&vq->vq_io_list, sizeof (vdev_io_t),
	    offsetof(vdev_io_t, vi_node));

	for (i = 0; i < zfs_vdev_max_pending; i++)
		list_insert_tail(&vq->vq_io_list, zio_vdev_alloc());
}

void
vdev_queue_fini(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;
	vdev_io_t *vi;

	avl_destroy(&vq->vq_deadline_tree);
	avl_destroy(&vq->vq_read_tree);
	avl_destroy(&vq->vq_write_tree);
	avl_destroy(&vq->vq_pending_tree);

	while ((vi = list_head(&vq->vq_io_list)) != NULL) {
		list_remove(&vq->vq_io_list, vi);
		zio_vdev_free(vi);
	}

	list_destroy(&vq->vq_io_list);

	mutex_destroy(&vq->vq_lock);
}

static void
vdev_queue_io_add(vdev_queue_t *vq, zio_t *zio)
{
	avl_add(&vq->vq_deadline_tree, zio);
	avl_add(zio->io_vdev_tree, zio);
}

static void
vdev_queue_io_remove(vdev_queue_t *vq, zio_t *zio)
{
	avl_remove(&vq->vq_deadline_tree, zio);
	avl_remove(zio->io_vdev_tree, zio);
}

static void
vdev_queue_agg_io_done(zio_t *aio)
{
	vdev_queue_t *vq = &aio->io_vd->vdev_queue;
	vdev_io_t *vi = aio->io_data;
	zio_t *pio;

	while ((pio = zio_walk_parents(aio)) != NULL)
		if (aio->io_type == ZIO_TYPE_READ)
			bcopy((char *)aio->io_data + (pio->io_offset -
			    aio->io_offset), pio->io_data, pio->io_size);

	mutex_enter(&vq->vq_lock);
	list_insert_tail(&vq->vq_io_list, vi);
	mutex_exit(&vq->vq_lock);
}

/*
 * Compute the range spanned by two i/os, which is the endpoint of the last
 * (lio->io_offset + lio->io_size) minus start of the first (fio->io_offset).
 * Conveniently, the gap between fio and lio is given by -IO_SPAN(lio, fio);
 * thus fio and lio are adjacent if and only if IO_SPAN(lio, fio) == 0.
 */
#define	IO_SPAN(fio, lio) ((lio)->io_offset + (lio)->io_size - (fio)->io_offset)
#define	IO_GAP(fio, lio) (-IO_SPAN(lio, fio))

static zio_t *
vdev_queue_io_to_issue(vdev_queue_t *vq, uint64_t pending_limit)
{
	zio_t *fio, *lio, *aio, *dio, *nio, *mio;
	avl_tree_t *t;
	vdev_io_t *vi;
	int flags;
	uint64_t maxspan = MIN(zfs_vdev_aggregation_limit, SPA_MAXBLOCKSIZE);
	uint64_t maxgap;
	int stretch;

again:
	ASSERT(MUTEX_HELD(&vq->vq_lock));

	if (avl_numnodes(&vq->vq_pending_tree) >= pending_limit ||
	    avl_numnodes(&vq->vq_deadline_tree) == 0)
		return (NULL);

	fio = lio = avl_first(&vq->vq_deadline_tree);

	t = fio->io_vdev_tree;
	flags = fio->io_flags & ZIO_FLAG_AGG_INHERIT;
	maxgap = (t == &vq->vq_read_tree) ? zfs_vdev_read_gap_limit : 0;

	vi = list_head(&vq->vq_io_list);
	if (vi == NULL) {
		vi = zio_vdev_alloc();
		list_insert_head(&vq->vq_io_list, vi);
	}

	if (!(flags & ZIO_FLAG_DONT_AGGREGATE)) {
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
		mio = (fio->io_flags & ZIO_FLAG_OPTIONAL) ? NULL : fio;

		/*
		 * Walk backwards through sufficiently contiguous I/Os
		 * recording the last non-option I/O.
		 */
		while ((dio = AVL_PREV(t, fio)) != NULL &&
		    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
		    IO_SPAN(dio, lio) <= maxspan &&
		    IO_GAP(dio, fio) <= maxgap) {
			fio = dio;
			if (mio == NULL && !(fio->io_flags & ZIO_FLAG_OPTIONAL))
				mio = fio;
		}

		/*
		 * Skip any initial optional I/Os.
		 */
		while ((fio->io_flags & ZIO_FLAG_OPTIONAL) && fio != lio) {
			fio = AVL_NEXT(t, fio);
			ASSERT(fio != NULL);
		}

		/*
		 * Walk forward through sufficiently contiguous I/Os.
		 */
		while ((dio = AVL_NEXT(t, lio)) != NULL &&
		    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
		    IO_SPAN(fio, dio) <= maxspan &&
		    IO_GAP(lio, dio) <= maxgap) {
			lio = dio;
			if (!(lio->io_flags & ZIO_FLAG_OPTIONAL))
				mio = lio;
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
		stretch = B_FALSE;
		if (t != &vq->vq_read_tree && mio != NULL) {
			nio = lio;
			while ((dio = AVL_NEXT(t, nio)) != NULL &&
			    IO_GAP(nio, dio) == 0 &&
			    IO_GAP(mio, dio) <= zfs_vdev_write_gap_limit) {
				nio = dio;
				if (!(nio->io_flags & ZIO_FLAG_OPTIONAL)) {
					stretch = B_TRUE;
					break;
				}
			}
		}

		if (stretch) {
			/* This may be a no-op. */
			VERIFY((dio = AVL_NEXT(t, lio)) != NULL);
			dio->io_flags &= ~ZIO_FLAG_OPTIONAL;
		} else {
			while (lio != mio && lio != fio) {
				ASSERT(lio->io_flags & ZIO_FLAG_OPTIONAL);
				lio = AVL_PREV(t, lio);
				ASSERT(lio != NULL);
			}
		}
	}

	if (fio != lio) {
		uint64_t size = IO_SPAN(fio, lio);
		ASSERT(size <= maxspan);
		ASSERT(vi != NULL);

		aio = zio_vdev_delegated_io(fio->io_vd, fio->io_offset,
		    vi, size, fio->io_type, ZIO_PRIORITY_AGG,
		    flags | ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE,
		    vdev_queue_agg_io_done, NULL);

		nio = fio;
		do {
			dio = nio;
			nio = AVL_NEXT(t, dio);
			ASSERT(dio->io_type == aio->io_type);
			ASSERT(dio->io_vdev_tree == t);

			if (dio->io_flags & ZIO_FLAG_NODATA) {
				ASSERT(dio->io_type == ZIO_TYPE_WRITE);
				bzero((char *)aio->io_data + (dio->io_offset -
				    aio->io_offset), dio->io_size);
			} else if (dio->io_type == ZIO_TYPE_WRITE) {
				bcopy(dio->io_data, (char *)aio->io_data +
				    (dio->io_offset - aio->io_offset),
				    dio->io_size);
			}

			zio_add_child(dio, aio);
			vdev_queue_io_remove(vq, dio);
			zio_vdev_io_bypass(dio);
			zio_execute(dio);
		} while (dio != lio);

		avl_add(&vq->vq_pending_tree, aio);
		list_remove(&vq->vq_io_list, vi);

		return (aio);
	}

	ASSERT(fio->io_vdev_tree == t);
	vdev_queue_io_remove(vq, fio);

	/*
	 * If the I/O is or was optional and therefore has no data, we need to
	 * simply discard it. We need to drop the vdev queue's lock to avoid a
	 * deadlock that we could encounter since this I/O will complete
	 * immediately.
	 */
	if (fio->io_flags & ZIO_FLAG_NODATA) {
		mutex_exit(&vq->vq_lock);
		zio_vdev_io_bypass(fio);
		zio_execute(fio);
		mutex_enter(&vq->vq_lock);
		goto again;
	}

	avl_add(&vq->vq_pending_tree, fio);

	return (fio);
}

zio_t *
vdev_queue_io(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	zio_t *nio;

	ASSERT(zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE);

	if (zio->io_flags & ZIO_FLAG_DONT_QUEUE)
		return (zio);

	zio->io_flags |= ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE;

	if (zio->io_type == ZIO_TYPE_READ)
		zio->io_vdev_tree = &vq->vq_read_tree;
	else
		zio->io_vdev_tree = &vq->vq_write_tree;

	mutex_enter(&vq->vq_lock);

	zio->io_deadline = (ddi_get_lbolt64() >> zfs_vdev_time_shift) +
	    zio->io_priority;

	vdev_queue_io_add(vq, zio);

	nio = vdev_queue_io_to_issue(vq, zfs_vdev_min_pending);

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
	int i;

	mutex_enter(&vq->vq_lock);

	avl_remove(&vq->vq_pending_tree, zio);

	for (i = 0; i < zfs_vdev_ramp_rate; i++) {
		zio_t *nio = vdev_queue_io_to_issue(vq, zfs_vdev_max_pending);
		if (nio == NULL)
			break;
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

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_vdev_max_pending, int, 0644);
MODULE_PARM_DESC(zfs_vdev_max_pending, "Max pending per-vdev I/Os");

module_param(zfs_vdev_min_pending, int, 0644);
MODULE_PARM_DESC(zfs_vdev_min_pending, "Min pending per-vdev I/Os");

module_param(zfs_vdev_aggregation_limit, int, 0644);
MODULE_PARM_DESC(zfs_vdev_aggregation_limit, "Max vdev I/O aggregation size");

module_param(zfs_vdev_time_shift, int, 0644);
MODULE_PARM_DESC(zfs_vdev_time_shift, "Deadline time shift for vdev I/O");

module_param(zfs_vdev_ramp_rate, int, 0644);
MODULE_PARM_DESC(zfs_vdev_ramp_rate, "Exponential I/O issue ramp-up rate");

module_param(zfs_vdev_read_gap_limit, int, 0644);
MODULE_PARM_DESC(zfs_vdev_read_gap_limit, "Aggregate read I/O over gap");

module_param(zfs_vdev_write_gap_limit, int, 0644);
MODULE_PARM_DESC(zfs_vdev_write_gap_limit, "Aggregate write I/O over gap");
#endif
