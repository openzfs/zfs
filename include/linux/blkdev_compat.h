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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 */

#ifndef _ZFS_BLKDEV_H
#define _ZFS_BLKDEV_H

#include <linux/blkdev.h>
#include <linux/elevator.h>

#ifndef HAVE_FMODE_T
typedef unsigned __bitwise__ fmode_t;
#endif /* HAVE_FMODE_T */

#ifndef HAVE_BLK_FETCH_REQUEST
static inline struct request *
blk_fetch_request(struct request_queue *q)
{
	struct request *req;

	req = elv_next_request(q);
	if (req)
		blkdev_dequeue_request(req);

	return req;
}
#endif /* HAVE_BLK_FETCH_REQUEST */

#ifndef HAVE_BLK_REQUEUE_REQUEST
static inline void
blk_requeue_request(request_queue_t *q, struct request *req)
{
	elv_requeue_request(q, req);
}
#endif /* HAVE_BLK_REQUEUE_REQUEST */

#ifndef HAVE_BLK_END_REQUEST
static inline bool
__blk_end_request(struct request *req, int error, unsigned int nr_bytes)
{
	LIST_HEAD(list);

	/*
	 * Request has already been dequeued but 2.6.18 version of
	 * end_request() unconditionally dequeues the request so we
	 * add it to a local list to prevent hitting the BUG_ON.
	 */
	list_add(&req->queuelist, &list);

	/*
	 * The old API required the driver to end each segment and not
	 * the entire request.  In our case we always need to end the
	 * entire request partial requests are not supported.
	 */
	req->hard_cur_sectors = nr_bytes >> 9;
	end_request(req, ((error == 0) ? 1 : error));

	return 0;
}

static inline bool
blk_end_request(struct request *req, int error, unsigned int nr_bytes)
{
	struct request_queue *q = req->q;
	bool rc;

	spin_lock_irq(q->queue_lock);
	rc = __blk_end_request(req, error, nr_bytes);
	spin_unlock_irq(q->queue_lock);

	return rc;
}
#else
# ifdef HAVE_BLK_END_REQUEST_GPL_ONLY
/*
 * Define required to avoid conflicting 2.6.29 non-static prototype for a
 * GPL-only version of the helper.  As of 2.6.31 the helper is available
 * to non-GPL modules and is not explicitly exported GPL-only.
 */
# define __blk_end_request __blk_end_request_x
# define blk_end_request blk_end_request_x

static inline bool
__blk_end_request_x(struct request *req, int error, unsigned int nr_bytes)
{
	/*
	 * The old API required the driver to end each segment and not
	 * the entire request.  In our case we always need to end the
	 * entire request partial requests are not supported.
	 */
	req->hard_cur_sectors = nr_bytes >> 9;
	end_request(req, ((error == 0) ? 1 : error));

	return 0;
}
static inline bool
blk_end_request_x(struct request *req, int error, unsigned int nr_bytes)
{
	struct request_queue *q = req->q;
	bool rc;

	spin_lock_irq(q->queue_lock);
	rc = __blk_end_request_x(req, error, nr_bytes);
	spin_unlock_irq(q->queue_lock);

	return rc;
}
# endif /* HAVE_BLK_END_REQUEST_GPL_ONLY */
#endif /* HAVE_BLK_END_REQUEST */

/*
 * 2.6.36 API change,
 * The blk_queue_flush() interface has replaced blk_queue_ordered()
 * interface.  However, while the old interface was available to all the
 * new one is GPL-only.   Thus if the GPL-only version is detected we
 * implement our own trivial helper compatibility funcion.   The hope is
 * that long term this function will be opened up.
 */
#if defined(HAVE_BLK_QUEUE_FLUSH) && defined(HAVE_BLK_QUEUE_FLUSH_GPL_ONLY)
#define blk_queue_flush __blk_queue_flush
static inline void
__blk_queue_flush(struct request_queue *q, unsigned int flags)
{
	q->flush_flags = flags & (REQ_FLUSH | REQ_FUA);
}
#endif /* HAVE_BLK_QUEUE_FLUSH && HAVE_BLK_QUEUE_FLUSH_GPL_ONLY */

#ifndef HAVE_BLK_RQ_POS
static inline sector_t
blk_rq_pos(struct request *req)
{
	return req->sector;
}
#endif /* HAVE_BLK_RQ_POS */

#ifndef HAVE_BLK_RQ_SECTORS
static inline unsigned int
blk_rq_sectors(struct request *req)
{
	return req->nr_sectors;
}
#endif /* HAVE_BLK_RQ_SECTORS */

#if !defined(HAVE_BLK_RQ_BYTES) || defined(HAVE_BLK_RQ_BYTES_GPL_ONLY)
/*
 * Define required to avoid conflicting 2.6.29 non-static prototype for a
 * GPL-only version of the helper.  As of 2.6.31 the helper is available
 * to non-GPL modules in the form of a static inline in the header.
 */
#define blk_rq_bytes __blk_rq_bytes
static inline unsigned int
__blk_rq_bytes(struct request *req)
{
	return blk_rq_sectors(req) << 9;
}
#endif /* !HAVE_BLK_RQ_BYTES || HAVE_BLK_RQ_BYTES_GPL_ONLY */

/*
 * Most of the blk_* macros were removed in 2.6.36.  Ostensibly this was
 * done to improve readability and allow easier grepping.  However, from
 * a portability stand point the macros are helpful.  Therefore the needed
 * macros are redefined here if they are missing from the kernel.
 */
#ifndef blk_fs_request
#define blk_fs_request(rq)	((rq)->cmd_type == REQ_TYPE_FS)
#endif

/*
 * 2.6.27 API change,
 * The blk_queue_stackable() queue flag was added in 2.6.27 to handle dm
 * stacking drivers.  Prior to this request stacking drivers were detected
 * by checking (q->request_fn == NULL), for earlier kernels we revert to
 * this legacy behavior.
 */
#ifndef blk_queue_stackable
#define blk_queue_stackable(q)	((q)->request_fn == NULL)
#endif

/*
 * 2.6.34 API change,
 * The blk_queue_max_hw_sectors() function replaces blk_queue_max_sectors().
 */
#ifndef HAVE_BLK_QUEUE_MAX_HW_SECTORS
#define blk_queue_max_hw_sectors __blk_queue_max_hw_sectors
static inline void
__blk_queue_max_hw_sectors(struct request_queue *q, unsigned int max_hw_sectors)
{
	blk_queue_max_sectors(q, max_hw_sectors);
}
#endif

/*
 * 2.6.34 API change,
 * The blk_queue_max_segments() function consolidates
 * blk_queue_max_hw_segments() and blk_queue_max_phys_segments().
 */
#ifndef HAVE_BLK_QUEUE_MAX_SEGMENTS
#define blk_queue_max_segments __blk_queue_max_segments
static inline void
__blk_queue_max_segments(struct request_queue *q, unsigned short max_segments)
{
	blk_queue_max_phys_segments(q, max_segments);
	blk_queue_max_hw_segments(q, max_segments);
}
#endif

/*
 * 2.6.30 API change,
 * The blk_queue_physical_block_size() function was introduced to
 * indicate the smallest I/O the device can write without incurring
 * a read-modify-write penalty.  For older kernels this is a no-op.
 */
#ifndef HAVE_BLK_QUEUE_PHYSICAL_BLOCK_SIZE
#define blk_queue_physical_block_size(q, x)	((void)(0))
#endif

/*
 * 2.6.30 API change,
 * The blk_queue_io_opt() function was added to indicate the optimal
 * I/O size for the device.  For older kernels this is a no-op.
 */
#ifndef HAVE_BLK_QUEUE_IO_OPT
#define blk_queue_io_opt(q, x)			((void)(0))
#endif

#ifndef HAVE_GET_DISK_RO
static inline int
get_disk_ro(struct gendisk *disk)
{
	int policy = 0;

	if (disk->part[0])
		policy = disk->part[0]->policy;

	return policy;
}
#endif /* HAVE_GET_DISK_RO */

#ifndef HAVE_RQ_IS_SYNC
static inline bool
rq_is_sync(struct request *req)
{
	return (req->flags & REQ_RW_SYNC);
}
#endif /* HAVE_RQ_IS_SYNC */

#ifndef HAVE_RQ_FOR_EACH_SEGMENT
struct req_iterator {
	int i;
	struct bio *bio;
};

# define for_each_bio(_bio)              \
	for (; _bio; _bio = _bio->bi_next)

# define __rq_for_each_bio(_bio, rq)    \
	if ((rq->bio))                  \
		for (_bio = (rq)->bio; _bio; _bio = _bio->bi_next)

# define rq_for_each_segment(bvl, _rq, _iter)                   \
	__rq_for_each_bio(_iter.bio, _rq)                       \
		bio_for_each_segment(bvl, _iter.bio, _iter.i)
#endif /* HAVE_RQ_FOR_EACH_SEGMENT */

/*
 * Portable helper for correctly setting the FAILFAST flags.  The
 * correct usage has changed 3 times from 2.6.12 to 2.6.38.
 */
static inline void
bio_set_flags_failfast(struct block_device *bdev, int *flags)
{
#ifdef CONFIG_BUG
	/*
	 * Disable FAILFAST for loopback devices because of the
	 * following incorrect BUG_ON() in loop_make_request().
	 * This support is also disabled for md devices because the
	 * test suite layers md devices on top of loopback devices.
	 * This may be removed when the loopback driver is fixed.
	 *
	 *   BUG_ON(!lo || (rw != READ && rw != WRITE));
	 */
	if ((MAJOR(bdev->bd_dev) == LOOP_MAJOR) ||
	    (MAJOR(bdev->bd_dev) == MD_MAJOR))
		return;

#ifdef BLOCK_EXT_MAJOR
	if (MAJOR(bdev->bd_dev) == BLOCK_EXT_MAJOR)
		return;
#endif /* BLOCK_EXT_MAJOR */
#endif /* CONFIG_BUG */

#ifdef HAVE_BIO_RW_FAILFAST_DTD
	/* BIO_RW_FAILFAST_* preferred interface from 2.6.28 - 2.6.35 */
	*flags |=
	    ((1 << BIO_RW_FAILFAST_DEV) |
	     (1 << BIO_RW_FAILFAST_TRANSPORT) |
	     (1 << BIO_RW_FAILFAST_DRIVER));
#else
# ifdef HAVE_BIO_RW_FAILFAST
	/* BIO_RW_FAILFAST preferred interface from 2.6.12 - 2.6.27 */
	*flags |= (1 << BIO_RW_FAILFAST);
# else
#  ifdef HAVE_REQ_FAILFAST_MASK
	/* REQ_FAILFAST_* preferred interface from 2.6.36 - 2.6.xx,
	 * the BIO_* and REQ_* flags were unified under REQ_* flags. */
	*flags |= REQ_FAILFAST_MASK;
#  endif /* HAVE_REQ_FAILFAST_MASK */
# endif /* HAVE_BIO_RW_FAILFAST */
#endif /* HAVE_BIO_RW_FAILFAST_DTD */
}

/*
 * Maximum disk label length, it may be undefined for some kernels.
 */
#ifndef DISK_NAME_LEN
#define DISK_NAME_LEN	32
#endif /* DISK_NAME_LEN */

/*
 * 2.6.24 API change,
 * The bio_end_io() prototype changed slightly.  These are helper
 * macro's to ensure the prototype and return value are handled.
 */
#ifdef HAVE_2ARGS_BIO_END_IO_T
# define BIO_END_IO_PROTO(fn, x, y, z)	static void fn(struct bio *x, int z)
# define BIO_END_IO_RETURN(rc)		return
#else
# define BIO_END_IO_PROTO(fn, x, y, z)	static int fn(struct bio *x, \
					              unsigned int y, int z)
# define BIO_END_IO_RETURN(rc)		return rc
#endif /* HAVE_2ARGS_BIO_END_IO_T */

/*
 * 2.6.38 - 2.6.x API,
 *   blkdev_get_by_path()
 *   blkdev_put()
 *
 * 2.6.28 - 2.6.37 API,
 *   open_bdev_exclusive()
 *   close_bdev_exclusive()
 *
 * 2.6.12 - 2.6.27 API,
 *   open_bdev_excl()
 *   close_bdev_excl()
 *
 * Used to exclusively open a block device from within the kernel.
 */
#if defined(HAVE_BLKDEV_GET_BY_PATH)
# define vdev_bdev_open(path, md, hld)	blkdev_get_by_path(path, \
					    (md) | FMODE_EXCL, hld)
# define vdev_bdev_close(bdev, md)	blkdev_put(bdev, (md) | FMODE_EXCL)
#elif defined(HAVE_OPEN_BDEV_EXCLUSIVE)
# define vdev_bdev_open(path, md, hld)	open_bdev_exclusive(path, md, hld)
# define vdev_bdev_close(bdev, md)	close_bdev_exclusive(bdev, md)
#else
# define vdev_bdev_open(path, md, hld)	open_bdev_excl(path, md, hld)
# define vdev_bdev_close(bdev, md)	close_bdev_excl(bdev)
#endif /* HAVE_BLKDEV_GET_BY_PATH | HAVE_OPEN_BDEV_EXCLUSIVE */

/*
 * 2.6.22 API change
 * The function invalidate_bdev() lost it's second argument because
 * it was unused.
 */
#ifdef HAVE_1ARG_INVALIDATE_BDEV
# define vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev)
#else
# define vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev, 1)
#endif /* HAVE_1ARG_INVALIDATE_BDEV */

/*
 * 2.6.30 API change
 * Change to make it explicit there this is the logical block size.
 */
#ifdef HAVE_BDEV_LOGICAL_BLOCK_SIZE
# define vdev_bdev_block_size(bdev)	bdev_logical_block_size(bdev)
#else
# define vdev_bdev_block_size(bdev)	bdev_hardsect_size(bdev)
#endif

/*
 * 2.6.37 API change
 * The WRITE_FLUSH, WRITE_FUA, and WRITE_FLUSH_FUA flags have been
 * introduced as a replacement for WRITE_BARRIER.  This was done to
 * allow richer semantics to be expressed to the block layer.  It is
 * the block layers responsibility to choose the correct way to
 * implement these semantics.
 *
 * The existence of these flags implies that REQ_FLUSH an REQ_FUA are
 * defined.  Thus we can safely define VDEV_REQ_FLUSH and VDEV_REQ_FUA
 * compatibility macros.
 */
#ifdef WRITE_FLUSH_FUA
# define VDEV_WRITE_FLUSH_FUA		WRITE_FLUSH_FUA
# define VDEV_REQ_FLUSH			REQ_FLUSH
# define VDEV_REQ_FUA			REQ_FUA
#else
# define VDEV_WRITE_FLUSH_FUA		WRITE_BARRIER
# define VDEV_REQ_FLUSH			REQ_HARDBARRIER
# define VDEV_REQ_FUA			REQ_HARDBARRIER
#endif

/*
 * 2.6.32 API change
 * Use the normal I/O patch for discards.
 */
#ifdef REQ_DISCARD
# define VDEV_REQ_DISCARD		REQ_DISCARD
#endif

/*
 * 2.6.33 API change
 * Discard granularity and alignment restrictions may now be set.  For
 * older kernels which do not support this it is safe to skip it.
 */
#ifdef HAVE_DISCARD_GRANULARITY
static inline void
blk_queue_discard_granularity(struct request_queue *q, unsigned int dg)
{
	q->limits.discard_granularity = dg;
}
#else
#define blk_queue_discard_granularity(x, dg)	((void)0)
#endif /* HAVE_DISCARD_GRANULARITY */

/*
 * Default Linux IO Scheduler,
 * Setting the scheduler to noop will allow the Linux IO scheduler to
 * still perform front and back merging, while leaving the request
 * ordering and prioritization to the ZFS IO scheduler.
 */
#define	VDEV_SCHEDULER			"noop"

#endif /* _ZFS_BLKDEV_H */
