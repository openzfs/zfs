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
#define	_ZFS_BLKDEV_H

#include <linux/blkdev.h>
#include <linux/elevator.h>

#ifndef HAVE_FMODE_T
typedef unsigned __bitwise__ fmode_t;
#endif /* HAVE_FMODE_T */

/*
 * 2.6.36 API change,
 * The blk_queue_flush() interface has replaced blk_queue_ordered()
 * interface.  However, while the old interface was available to all the
 * new one is GPL-only.   Thus if the GPL-only version is detected we
 * implement our own trivial helper compatibility funcion.   The hope is
 * that long term this function will be opened up.
 */
#if defined(HAVE_BLK_QUEUE_FLUSH) && defined(HAVE_BLK_QUEUE_FLUSH_GPL_ONLY)
#define	blk_queue_flush __blk_queue_flush
static inline void
__blk_queue_flush(struct request_queue *q, unsigned int flags)
{
	q->flush_flags = flags & (REQ_FLUSH | REQ_FUA);
}
#endif /* HAVE_BLK_QUEUE_FLUSH && HAVE_BLK_QUEUE_FLUSH_GPL_ONLY */
/*
 * Most of the blk_* macros were removed in 2.6.36.  Ostensibly this was
 * done to improve readability and allow easier grepping.  However, from
 * a portability stand point the macros are helpful.  Therefore the needed
 * macros are redefined here if they are missing from the kernel.
 */
#ifndef blk_fs_request
#define	blk_fs_request(rq)	((rq)->cmd_type == REQ_TYPE_FS)
#endif

/*
 * 2.6.27 API change,
 * The blk_queue_stackable() queue flag was added in 2.6.27 to handle dm
 * stacking drivers.  Prior to this request stacking drivers were detected
 * by checking (q->request_fn == NULL), for earlier kernels we revert to
 * this legacy behavior.
 */
#ifndef blk_queue_stackable
#define	blk_queue_stackable(q)	((q)->request_fn == NULL)
#endif

/*
 * 2.6.34 API change,
 * The blk_queue_max_hw_sectors() function replaces blk_queue_max_sectors().
 */
#ifndef HAVE_BLK_QUEUE_MAX_HW_SECTORS
#define	blk_queue_max_hw_sectors __blk_queue_max_hw_sectors
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
#define	blk_queue_max_segments __blk_queue_max_segments
static inline void
__blk_queue_max_segments(struct request_queue *q, unsigned short max_segments)
{
	blk_queue_max_phys_segments(q, max_segments);
	blk_queue_max_hw_segments(q, max_segments);
}
#endif

#ifndef HAVE_GET_DISK_RO
static inline int
get_disk_ro(struct gendisk *disk)
{
	int policy = 0;

	if (disk->part[0])
		policy = disk->part[0]->policy;

	return (policy);
}
#endif /* HAVE_GET_DISK_RO */

#ifdef HAVE_BIO_BVEC_ITER
#define	BIO_BI_SECTOR(bio)	(bio)->bi_iter.bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_iter.bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_iter.bi_idx
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bv), (b), (i))
typedef struct bvec_iter bvec_iterator_t;
#else
#define	BIO_BI_SECTOR(bio)	(bio)->bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_idx
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bvp), (b), (i))
typedef int bvec_iterator_t;
#endif

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

#if defined(HAVE_BIO_RW_FAILFAST_DTD)
	/* BIO_RW_FAILFAST_* preferred interface from 2.6.28 - 2.6.35 */
	*flags |= (
	    (1 << BIO_RW_FAILFAST_DEV) |
	    (1 << BIO_RW_FAILFAST_TRANSPORT) |
	    (1 << BIO_RW_FAILFAST_DRIVER));
#elif defined(HAVE_REQ_FAILFAST_MASK)
	/*
	 * REQ_FAILFAST_* preferred interface from 2.6.36 - 2.6.xx,
	 * the BIO_* and REQ_* flags were unified under REQ_* flags.
	 */
	*flags |= REQ_FAILFAST_MASK;
#else
#error "Undefined block IO FAILFAST interface."
#endif
}

/*
 * Maximum disk label length, it may be undefined for some kernels.
 */
#ifndef DISK_NAME_LEN
#define	DISK_NAME_LEN	32
#endif /* DISK_NAME_LEN */

/*
 * 4.3 API change
 * The bio_endio() prototype changed slightly.  These are helper
 * macro's to ensure the prototype and invocation are handled.
 */
#ifdef HAVE_1ARG_BIO_END_IO_T
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x)
#define	BIO_END_IO(bio, error)		bio->bi_error = error; bio_endio(bio);
#else
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x, int z)
#define	BIO_END_IO(bio, error)		bio_endio(bio, error);
#endif /* HAVE_1ARG_BIO_END_IO_T */

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
#define	vdev_bdev_open(path, md, hld)	blkdev_get_by_path(path, \
					    (md) | FMODE_EXCL, hld)
#define	vdev_bdev_close(bdev, md)	blkdev_put(bdev, (md) | FMODE_EXCL)
#elif defined(HAVE_OPEN_BDEV_EXCLUSIVE)
#define	vdev_bdev_open(path, md, hld)	open_bdev_exclusive(path, md, hld)
#define	vdev_bdev_close(bdev, md)	close_bdev_exclusive(bdev, md)
#else
#define	vdev_bdev_open(path, md, hld)	open_bdev_excl(path, md, hld)
#define	vdev_bdev_close(bdev, md)	close_bdev_excl(bdev)
#endif /* HAVE_BLKDEV_GET_BY_PATH | HAVE_OPEN_BDEV_EXCLUSIVE */

/*
 * 2.6.22 API change
 * The function invalidate_bdev() lost it's second argument because
 * it was unused.
 */
#ifdef HAVE_1ARG_INVALIDATE_BDEV
#define	vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev)
#else
#define	vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev, 1)
#endif /* HAVE_1ARG_INVALIDATE_BDEV */

/*
 * 2.6.27 API change
 * The function was exported for use, prior to this it existed by the
 * symbol was not exported.
 */
#ifndef HAVE_LOOKUP_BDEV
#define	lookup_bdev(path)		ERR_PTR(-ENOTSUP)
#endif

/*
 * 2.6.30 API change
 * To ensure good performance preferentially use the physical block size
 * for proper alignment.  The physical size is supposed to be the internal
 * sector size used by the device.  This is often 4096 byte for AF devices,
 * while a smaller 512 byte logical size is supported for compatibility.
 *
 * Unfortunately, many drives still misreport their physical sector size.
 * For devices which are known to lie you may need to manually set this
 * at pool creation time with 'zpool create -o ashift=12 ...'.
 *
 * When the physical block size interface isn't available, we fall back to
 * the logical block size interface and then the older hard sector size.
 */
#ifdef HAVE_BDEV_PHYSICAL_BLOCK_SIZE
#define	vdev_bdev_block_size(bdev)	bdev_physical_block_size(bdev)
#else
#ifdef HAVE_BDEV_LOGICAL_BLOCK_SIZE
#define	vdev_bdev_block_size(bdev)	bdev_logical_block_size(bdev)
#else
#define	vdev_bdev_block_size(bdev)	bdev_hardsect_size(bdev)
#endif /* HAVE_BDEV_LOGICAL_BLOCK_SIZE */
#endif /* HAVE_BDEV_PHYSICAL_BLOCK_SIZE */

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
#define	VDEV_WRITE_FLUSH_FUA		WRITE_FLUSH_FUA
#define	VDEV_REQ_FLUSH			REQ_FLUSH
#define	VDEV_REQ_FUA			REQ_FUA
#else
#define	VDEV_WRITE_FLUSH_FUA		WRITE_BARRIER
#ifdef HAVE_BIO_RW_BARRIER
#define	VDEV_REQ_FLUSH			(1 << BIO_RW_BARRIER)
#define	VDEV_REQ_FUA			(1 << BIO_RW_BARRIER)
#else
#define	VDEV_REQ_FLUSH			REQ_HARDBARRIER
#define	VDEV_REQ_FUA			REQ_FUA
#endif
#endif

/*
 * 2.6.32 API change
 * Use the normal I/O patch for discards.
 */
#ifdef QUEUE_FLAG_DISCARD
#ifdef HAVE_BIO_RW_DISCARD
#define	VDEV_REQ_DISCARD		(1 << BIO_RW_DISCARD)
#else
#define	VDEV_REQ_DISCARD		REQ_DISCARD
#endif
#else
#error	"Allowing the build will cause discard requests to become writes "
	"potentially triggering the DMU_MAX_ACCESS assertion. Please file a "
	"an issue report at: https://github.com/zfsonlinux/zfs/issues/new"
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
#define	blk_queue_discard_granularity(x, dg)	((void)0)
#endif /* HAVE_DISCARD_GRANULARITY */

/*
 * Default Linux IO Scheduler,
 * Setting the scheduler to noop will allow the Linux IO scheduler to
 * still perform front and back merging, while leaving the request
 * ordering and prioritization to the ZFS IO scheduler.
 */
#define	VDEV_SCHEDULER			"noop"

/*
 * A common holder for vdev_bdev_open() is used to relax the exclusive open
 * semantics slightly.  Internal vdev disk callers may pass VDEV_HOLDER to
 * allow them to open the device multiple times.  Other kernel callers and
 * user space processes which don't pass this value will get EBUSY.  This is
 * currently required for the correct operation of hot spares.
 */
#define	VDEV_HOLDER			((void *)0x2401de7)

#ifndef HAVE_GENERIC_IO_ACCT
#define	generic_start_io_acct(rw, slen, part)		((void)0)
#define	generic_end_io_acct(rw, part, start_jiffies)	((void)0)
#endif

#endif /* _ZFS_BLKDEV_H */
