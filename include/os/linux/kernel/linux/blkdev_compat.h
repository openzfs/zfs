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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 */

#ifndef _ZFS_BLKDEV_H
#define	_ZFS_BLKDEV_H

#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/hdreg.h>
#include <linux/major.h>
#include <linux/msdos_fs.h>	/* for SECTOR_* */
#include <linux/bio.h>
#include <linux/blk-mq.h>

/*
 * 6.11 API
 * Setting the flush flags directly is no longer possible; flush flags are set
 * on the queue_limits structure and passed to blk_disk_alloc(). In this case
 * we remove this function entirely.
 */
#if !defined(HAVE_BLK_ALLOC_DISK_2ARG) || \
	!defined(HAVE_BLKDEV_QUEUE_LIMITS_FEATURES)
static inline void
blk_queue_set_write_cache(struct request_queue *q, bool on)
{
	if (on) {
		blk_queue_flag_set(QUEUE_FLAG_WC, q);
		blk_queue_flag_set(QUEUE_FLAG_FUA, q);
	} else {
		blk_queue_flag_clear(QUEUE_FLAG_WC, q);
		blk_queue_flag_clear(QUEUE_FLAG_FUA, q);
	}
}
#endif /* !HAVE_BLK_ALLOC_DISK_2ARG || !HAVE_BLKDEV_QUEUE_LIMITS_FEATURES */

/*
 * Detect if a device has a write cache. Used to set the intial value for the
 * vdev nowritecache flag.
 *
 * 4.10: QUEUE_FLAG_WC added. Initialised by the driver, but can be changed
 *       later by the operator. If not set, kernel will return flush requests
 *       immediately without doing anything.
 * 6.6: QUEUE_FLAG_HW_WC added. Initialised by the driver, can't be changed.
 *      Only controls if the operator is allowed to change _WC. Initial version
 *      buggy; aliased to QUEUE_FLAG_FUA, so unuseable.
 * 6.6.10, 6.7: QUEUE_FLAG_HW_WC fixed.
 *
 * Older than 4.10 we just assume write cache, and let the normal flush fail
 * detection apply.
 */
static inline boolean_t
zfs_bdev_has_write_cache(struct block_device *bdev)
{
#if defined(QUEUE_FLAG_HW_WC) && QUEUE_FLAG_HW_WC != QUEUE_FLAG_FUA
	return (test_bit(QUEUE_FLAG_HW_WC, &bdev_get_queue(bdev)->queue_flags));
#elif defined(QUEUE_FLAG_WC)
	return (test_bit(QUEUE_FLAG_WC, &bdev_get_queue(bdev)->queue_flags));
#else
	return (B_TRUE);
#endif
}

static inline void
blk_queue_set_read_ahead(struct request_queue *q, unsigned long ra_pages)
{
#if !defined(HAVE_BLK_QUEUE_UPDATE_READAHEAD) && \
	!defined(HAVE_DISK_UPDATE_READAHEAD)
#if defined(HAVE_BLK_QUEUE_BDI_DYNAMIC)
	q->backing_dev_info->ra_pages = ra_pages;
#elif defined(HAVE_BLK_QUEUE_DISK_BDI)
	q->disk->bdi->ra_pages = ra_pages;
#else
	q->backing_dev_info.ra_pages = ra_pages;
#endif
#endif
}

#define	BIO_BI_SECTOR(bio)	(bio)->bi_iter.bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_iter.bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_iter.bi_idx
#define	BIO_BI_SKIP(bio)	(bio)->bi_iter.bi_bvec_done
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bv), (b), (i))
typedef struct bvec_iter bvec_iterator_t;

static inline void
bio_set_flags_failfast(struct block_device *bdev, int *flags, bool dev,
    bool transport, bool driver)
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

	if (dev)
		*flags |= REQ_FAILFAST_DEV;
	if (transport)
		*flags |= REQ_FAILFAST_TRANSPORT;
	if (driver)
		*flags |= REQ_FAILFAST_DRIVER;
}

/*
 * Maximum disk label length, it may be undefined for some kernels.
 */
#if !defined(DISK_NAME_LEN)
#define	DISK_NAME_LEN	32
#endif /* DISK_NAME_LEN */

static inline int
bi_status_to_errno(blk_status_t status)
{
	switch (status)	{
	case BLK_STS_OK:
		return (0);
	case BLK_STS_NOTSUPP:
		return (EOPNOTSUPP);
	case BLK_STS_TIMEOUT:
		return (ETIMEDOUT);
	case BLK_STS_NOSPC:
		return (ENOSPC);
	case BLK_STS_TRANSPORT:
		return (ENOLINK);
	case BLK_STS_TARGET:
		return (EREMOTEIO);
#ifdef HAVE_BLK_STS_RESV_CONFLICT
	case BLK_STS_RESV_CONFLICT:
#else
	case BLK_STS_NEXUS:
#endif
		return (EBADE);
	case BLK_STS_MEDIUM:
		return (ENODATA);
	case BLK_STS_PROTECTION:
		return (EILSEQ);
	case BLK_STS_RESOURCE:
		return (ENOMEM);
	case BLK_STS_AGAIN:
		return (EAGAIN);
	case BLK_STS_IOERR:
		return (EIO);
	default:
		return (EIO);
	}
}

static inline blk_status_t
errno_to_bi_status(int error)
{
	switch (error) {
	case 0:
		return (BLK_STS_OK);
	case EOPNOTSUPP:
		return (BLK_STS_NOTSUPP);
	case ETIMEDOUT:
		return (BLK_STS_TIMEOUT);
	case ENOSPC:
		return (BLK_STS_NOSPC);
	case ENOLINK:
		return (BLK_STS_TRANSPORT);
	case EREMOTEIO:
		return (BLK_STS_TARGET);
	case EBADE:
#ifdef HAVE_BLK_STS_RESV_CONFLICT
		return (BLK_STS_RESV_CONFLICT);
#else
		return (BLK_STS_NEXUS);
#endif
	case ENODATA:
		return (BLK_STS_MEDIUM);
	case EILSEQ:
		return (BLK_STS_PROTECTION);
	case ENOMEM:
		return (BLK_STS_RESOURCE);
	case EAGAIN:
		return (BLK_STS_AGAIN);
	case EIO:
		return (BLK_STS_IOERR);
	default:
		return (BLK_STS_IOERR);
	}
}

/*
 * 5.15 MACRO,
 *   GD_DEAD
 *
 * 2.6.36 - 5.14 MACRO,
 *   GENHD_FL_UP
 *
 * Check the disk status and return B_TRUE if alive
 * otherwise B_FALSE
 */
static inline boolean_t
zfs_check_disk_status(struct block_device *bdev)
{
#if defined(GENHD_FL_UP)
	return (!!(bdev->bd_disk->flags & GENHD_FL_UP));
#elif defined(GD_DEAD)
	return (!test_bit(GD_DEAD, &bdev->bd_disk->state));
#else
/*
 * This is encountered if neither GENHD_FL_UP nor GD_DEAD is available in
 * the kernel - likely due to an MACRO change that needs to be chased down.
 */
#error "Unsupported kernel: no usable disk status check"
#endif
}

/*
 * 5.17 API change
 *
 * GENHD_FL_EXT_DEVT flag removed
 * GENHD_FL_NO_PART_SCAN renamed GENHD_FL_NO_PART
 */
#ifndef HAVE_GENHD_FL_EXT_DEVT
#define	GENHD_FL_EXT_DEVT	(0)
#endif
#ifndef HAVE_GENHD_FL_NO_PART
#define	GENHD_FL_NO_PART	(GENHD_FL_NO_PART_SCAN)
#endif

/*
 * 4.1 API,
 * 3.10.0 CentOS 7.x API,
 *   blkdev_reread_part()
 *
 * For older kernels trigger a re-reading of the partition table by calling
 * check_disk_change() which calls flush_disk() to invalidate the device.
 *
 * For newer kernels (as of 5.10), bdev_check_media_change is used, in favor of
 * check_disk_change(), with the modification that invalidation is no longer
 * forced.
 */
#ifdef HAVE_CHECK_DISK_CHANGE
#define	zfs_check_media_change(bdev)	check_disk_change(bdev)
#ifdef HAVE_BLKDEV_REREAD_PART
#define	vdev_bdev_reread_part(bdev)	blkdev_reread_part(bdev)
#else
#define	vdev_bdev_reread_part(bdev)	check_disk_change(bdev)
#endif /* HAVE_BLKDEV_REREAD_PART */
#else
#ifdef HAVE_BDEV_CHECK_MEDIA_CHANGE
static inline int
zfs_check_media_change(struct block_device *bdev)
{
#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
	struct gendisk *gd = bdev->bd_disk;
	const struct block_device_operations *bdo = gd->fops;
#endif

	if (!bdev_check_media_change(bdev))
		return (0);

#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
	/*
	 * Force revalidation, to mimic the old behavior of
	 * check_disk_change()
	 */
	if (bdo->revalidate_disk)
		bdo->revalidate_disk(gd);
#endif

	return (0);
}
#define	vdev_bdev_reread_part(bdev)	zfs_check_media_change(bdev)
#elif defined(HAVE_DISK_CHECK_MEDIA_CHANGE)
#define	vdev_bdev_reread_part(bdev)	disk_check_media_change(bdev->bd_disk)
#define	zfs_check_media_change(bdev)	disk_check_media_change(bdev->bd_disk)
#else
/*
 * This is encountered if check_disk_change() and bdev_check_media_change()
 * are not available in the kernel - likely due to an API change that needs
 * to be chased down.
 */
#error "Unsupported kernel: no usable disk change check"
#endif /* HAVE_BDEV_CHECK_MEDIA_CHANGE */
#endif /* HAVE_CHECK_DISK_CHANGE */

/*
 * 2.6.27 API change
 * The function was exported for use, prior to this it existed but the
 * symbol was not exported.
 *
 * 5.11 API change
 * Changed to take a dev_t argument which is set on success and return a
 * non-zero error code on failure.
 */
static inline int
vdev_lookup_bdev(const char *path, dev_t *dev)
{
#if defined(HAVE_DEVT_LOOKUP_BDEV)
	return (lookup_bdev(path, dev));
#elif defined(HAVE_1ARG_LOOKUP_BDEV)
	struct block_device *bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return (PTR_ERR(bdev));

	*dev = bdev->bd_dev;
	bdput(bdev);

	return (0);
#else
#error "Unsupported kernel"
#endif
}

#if defined(HAVE_BLK_MODE_T)
#define	blk_mode_is_open_write(flag)	((flag) & BLK_OPEN_WRITE)
#else
#define	blk_mode_is_open_write(flag)	((flag) & FMODE_WRITE)
#endif

/*
 * Kernels without bio_set_op_attrs use bi_rw for the bio flags.
 */
#if !defined(HAVE_BIO_SET_OP_ATTRS)
static inline void
bio_set_op_attrs(struct bio *bio, unsigned rw, unsigned flags)
{
	bio->bi_opf = rw | flags;
}
#endif

/*
 * bio_set_flush - Set the appropriate flags in a bio to guarantee
 * data are on non-volatile media on completion.
 */
static inline void
bio_set_flush(struct bio *bio)
{
	bio_set_op_attrs(bio, 0, REQ_PREFLUSH | REQ_OP_WRITE);
}

/*
 * 4.8 API,
 *   REQ_OP_FLUSH
 *
 * in all cases but may have a performance impact for some kernels.  It
 * has the advantage of minimizing kernel specific changes in the zvol code.
 *
 */
static inline boolean_t
bio_is_flush(struct bio *bio)
{
	return (bio_op(bio) == REQ_OP_FLUSH);
}

/*
 * 4.8 API,
 *   REQ_FUA flag moved to bio->bi_opf
 */
static inline boolean_t
bio_is_fua(struct bio *bio)
{
	return (bio->bi_opf & REQ_FUA);
}

/*
 * 4.8 API,
 *   REQ_OP_DISCARD
 *
 * In all cases the normal I/O path is used for discards.  The only
 * difference is how the kernel tags individual I/Os as discards.
 */
static inline boolean_t
bio_is_discard(struct bio *bio)
{
	return (bio_op(bio) == REQ_OP_DISCARD);
}

/*
 * 4.8 API,
 *   REQ_OP_SECURE_ERASE
 */
static inline boolean_t
bio_is_secure_erase(struct bio *bio)
{
	return (bio_op(bio) == REQ_OP_SECURE_ERASE);
}

/*
 * 2.6.33 API change
 * Discard granularity and alignment restrictions may now be set.  For
 * older kernels which do not support this it is safe to skip it.
 */
static inline void
blk_queue_discard_granularity(struct request_queue *q, unsigned int dg)
{
	q->limits.discard_granularity = dg;
}

/*
 * 5.19 API,
 *   bdev_max_discard_sectors()
 *
 * 2.6.32 API,
 *   blk_queue_discard()
 */
static inline boolean_t
bdev_discard_supported(struct block_device *bdev)
{
#if defined(HAVE_BDEV_MAX_DISCARD_SECTORS)
	return (bdev_max_discard_sectors(bdev) > 0 &&
	    bdev_discard_granularity(bdev) > 0);
#elif defined(HAVE_BLK_QUEUE_DISCARD)
	return (blk_queue_discard(bdev_get_queue(bdev)) > 0 &&
	    bdev_get_queue(bdev)->limits.discard_granularity > 0);
#else
#error "Unsupported kernel"
#endif
}

/*
 * 5.19 API,
 *   bdev_max_secure_erase_sectors()
 *
 * 4.8 API,
 *   blk_queue_secure_erase()
 */
static inline boolean_t
bdev_secure_discard_supported(struct block_device *bdev)
{
#if defined(HAVE_BDEV_MAX_SECURE_ERASE_SECTORS)
	return (!!bdev_max_secure_erase_sectors(bdev));
#elif defined(HAVE_BLK_QUEUE_SECURE_ERASE)
	return (!!blk_queue_secure_erase(bdev_get_queue(bdev)));
#else
#error "Unsupported kernel"
#endif
}

/*
 * A common holder for vdev_bdev_open() is used to relax the exclusive open
 * semantics slightly.  Internal vdev disk callers may pass VDEV_HOLDER to
 * allow them to open the device multiple times.  Other kernel callers and
 * user space processes which don't pass this value will get EBUSY.  This is
 * currently required for the correct operation of hot spares.
 */
#define	VDEV_HOLDER			((void *)0x2401de7)

static inline unsigned long
blk_generic_start_io_acct(struct request_queue *q __attribute__((unused)),
    struct gendisk *disk __attribute__((unused)),
    int rw __attribute__((unused)), struct bio *bio)
{
#if defined(HAVE_BDEV_IO_ACCT_63)
	return (bdev_start_io_acct(bio->bi_bdev, bio_op(bio),
	    jiffies));
#elif defined(HAVE_BDEV_IO_ACCT_OLD)
	return (bdev_start_io_acct(bio->bi_bdev, bio_sectors(bio),
	    bio_op(bio), jiffies));
#elif defined(HAVE_DISK_IO_ACCT)
	return (disk_start_io_acct(disk, bio_sectors(bio), bio_op(bio)));
#elif defined(HAVE_BIO_IO_ACCT)
	return (bio_start_io_acct(bio));
#elif defined(HAVE_GENERIC_IO_ACCT_4ARG)
	unsigned long start_time = jiffies;
	generic_start_io_acct(q, rw, bio_sectors(bio), &disk->part0);
	return (start_time);
#else
	/* Unsupported */
	return (0);
#endif
}

static inline void
blk_generic_end_io_acct(struct request_queue *q __attribute__((unused)),
    struct gendisk *disk __attribute__((unused)),
    int rw __attribute__((unused)), struct bio *bio, unsigned long start_time)
{
#if defined(HAVE_BDEV_IO_ACCT_63)
	bdev_end_io_acct(bio->bi_bdev, bio_op(bio), bio_sectors(bio),
	    start_time);
#elif defined(HAVE_BDEV_IO_ACCT_OLD)
	bdev_end_io_acct(bio->bi_bdev, bio_op(bio), start_time);
#elif defined(HAVE_DISK_IO_ACCT)
	disk_end_io_acct(disk, bio_op(bio), start_time);
#elif defined(HAVE_BIO_IO_ACCT)
	bio_end_io_acct(bio, start_time);
#elif defined(HAVE_GENERIC_IO_ACCT_4ARG)
	generic_end_io_acct(q, rw, &disk->part0, start_time);
#endif
}

#ifndef HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS
static inline struct request_queue *
blk_generic_alloc_queue(make_request_fn make_request, int node_id)
{
#if defined(HAVE_BLK_ALLOC_QUEUE_REQUEST_FN)
	return (blk_alloc_queue(make_request, node_id));
#elif defined(HAVE_BLK_ALLOC_QUEUE_REQUEST_FN_RH)
	return (blk_alloc_queue_rh(make_request, node_id));
#else
	struct request_queue *q = blk_alloc_queue(GFP_KERNEL);
	if (q != NULL)
		blk_queue_make_request(q, make_request);

	return (q);
#endif
}
#endif /* !HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS */

/*
 * All the io_*() helper functions below can operate on a bio, or a rq, but
 * not both.  The older submit_bio() codepath will pass a bio, and the
 * newer blk-mq codepath will pass a rq.
 */
static inline int
io_data_dir(struct bio *bio, struct request *rq)
{
	if (rq != NULL) {
		if (op_is_write(req_op(rq))) {
			return (WRITE);
		} else {
			return (READ);
		}
	}
	return (bio_data_dir(bio));
}

static inline int
io_is_flush(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (req_op(rq) == REQ_OP_FLUSH);
	return (bio_is_flush(bio));
}

static inline int
io_is_discard(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (req_op(rq) == REQ_OP_DISCARD);
	return (bio_is_discard(bio));
}

static inline int
io_is_secure_erase(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (req_op(rq) == REQ_OP_SECURE_ERASE);
	return (bio_is_secure_erase(bio));
}

static inline int
io_is_fua(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (rq->cmd_flags & REQ_FUA);
	return (bio_is_fua(bio));
}


static inline uint64_t
io_offset(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (blk_rq_pos(rq) << 9);
	return (BIO_BI_SECTOR(bio) << 9);
}

static inline uint64_t
io_size(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (blk_rq_bytes(rq));
	return (BIO_BI_SIZE(bio));
}

static inline int
io_has_data(struct bio *bio, struct request *rq)
{
	if (rq != NULL)
		return (bio_has_data(rq->bio));
	return (bio_has_data(bio));
}
#endif /* _ZFS_BLKDEV_H */
