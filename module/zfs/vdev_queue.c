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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
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
int zfs_vdev_max_pending = 35;
int zfs_vdev_min_pending = 4;

/* deadline = pri + (lbolt >> time_shift) */
int zfs_vdev_time_shift = 6;

/* exponential I/O issue ramp-up rate */
int zfs_vdev_ramp_rate = 2;

/*
 * i/os will be aggregated into a single large i/o up to
 * zfs_vdev_aggregation_limit bytes long.
 */
int zfs_vdev_aggregation_limit = SPA_MAXBLOCKSIZE;

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

	mutex_init(&vq->vq_lock, NULL, MUTEX_DEFAULT, NULL);

	avl_create(&vq->vq_deadline_tree, vdev_queue_deadline_compare,
	    sizeof (zio_t), offsetof(struct zio, io_deadline_node));

	avl_create(&vq->vq_read_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	avl_create(&vq->vq_write_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	avl_create(&vq->vq_pending_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));
}

void
vdev_queue_fini(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;

	avl_destroy(&vq->vq_deadline_tree);
	avl_destroy(&vq->vq_read_tree);
	avl_destroy(&vq->vq_write_tree);
	avl_destroy(&vq->vq_pending_tree);

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
	zio_t *dio;
	uint64_t offset = 0;

	while ((dio = aio->io_delegate_list) != NULL) {
		if (aio->io_type == ZIO_TYPE_READ)
			bcopy((char *)aio->io_data + offset, dio->io_data,
			    dio->io_size);
		offset += dio->io_size;
		aio->io_delegate_list = dio->io_delegate_next;
		dio->io_delegate_next = NULL;
		dio->io_error = aio->io_error;
		zio_execute(dio);
	}
	ASSERT3U(offset, ==, aio->io_size);

	zio_buf_free(aio->io_data, aio->io_size);
}

#define	IS_ADJACENT(io, nio) \
	((io)->io_offset + (io)->io_size == (nio)->io_offset)

static zio_t *
vdev_queue_io_to_issue(vdev_queue_t *vq, uint64_t pending_limit)
{
	zio_t *fio, *lio, *aio, *dio;
	avl_tree_t *tree;
	uint64_t size;
	int flags;

	ASSERT(MUTEX_HELD(&vq->vq_lock));

	if (avl_numnodes(&vq->vq_pending_tree) >= pending_limit ||
	    avl_numnodes(&vq->vq_deadline_tree) == 0)
		return (NULL);

	fio = lio = avl_first(&vq->vq_deadline_tree);

	tree = fio->io_vdev_tree;
	size = fio->io_size;
	flags = fio->io_flags & ZIO_FLAG_AGG_INHERIT;

	if (!(flags & ZIO_FLAG_DONT_AGGREGATE)) {
		/*
		 * We can aggregate I/Os that are adjacent and of the
		 * same flavor, as expressed by the AGG_INHERIT flags.
		 * The latter is necessary so that certain attributes
		 * of the I/O, such as whether it's a normal I/O or a
		 * scrub/resilver, can be preserved in the aggregate.
		 */
		while ((dio = AVL_PREV(tree, fio)) != NULL &&
		    IS_ADJACENT(dio, fio) &&
		    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
		    size + dio->io_size <= zfs_vdev_aggregation_limit) {
			dio->io_delegate_next = fio;
			fio = dio;
			size += dio->io_size;
		}
		while ((dio = AVL_NEXT(tree, lio)) != NULL &&
		    IS_ADJACENT(lio, dio) &&
		    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
		    size + dio->io_size <= zfs_vdev_aggregation_limit) {
			lio->io_delegate_next = dio;
			lio = dio;
			size += dio->io_size;
		}
	}

	if (fio != lio) {
		char *buf = zio_buf_alloc(size);
		uint64_t offset = 0;

		ASSERT(size <= zfs_vdev_aggregation_limit);

		aio = zio_vdev_delegated_io(fio->io_vd, fio->io_offset,
		    buf, size, fio->io_type, ZIO_PRIORITY_NOW,
		    flags | ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE,
		    vdev_queue_agg_io_done, NULL);

		aio->io_delegate_list = fio;

		for (dio = fio; dio != NULL; dio = dio->io_delegate_next) {
			ASSERT(dio->io_type == aio->io_type);
			ASSERT(dio->io_vdev_tree == tree);
			if (dio->io_type == ZIO_TYPE_WRITE)
				bcopy(dio->io_data, buf + offset, dio->io_size);
			offset += dio->io_size;
			vdev_queue_io_remove(vq, dio);
			zio_vdev_io_bypass(dio);
		}

		ASSERT(offset == size);

		avl_add(&vq->vq_pending_tree, aio);

		return (aio);
	}

	ASSERT(fio->io_vdev_tree == tree);
	vdev_queue_io_remove(vq, fio);

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

	zio->io_deadline = (lbolt64 >> zfs_vdev_time_shift) + zio->io_priority;

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

	mutex_enter(&vq->vq_lock);

	avl_remove(&vq->vq_pending_tree, zio);

	for (int i = 0; i < zfs_vdev_ramp_rate; i++) {
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
