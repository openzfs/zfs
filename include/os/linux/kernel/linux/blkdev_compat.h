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
#include <linux/backing-dev.h>
#include <linux/hdreg.h>
#include <linux/major.h>
#include <linux/msdos_fs.h>	/* for SECTOR_* */

#ifndef HAVE_BLK_QUEUE_FLAG_SET
static inline void
blk_queue_flag_set(unsigned int flag, struct request_queue *q)
{
	queue_flag_set(flag, q);
}
#endif

#ifndef HAVE_BLK_QUEUE_FLAG_CLEAR
static inline void
blk_queue_flag_clear(unsigned int flag, struct request_queue *q)
{
	queue_flag_clear(flag, q);
}
#endif

/*
 * 4.7 API,
 * The blk_queue_write_cache() interface has replaced blk_queue_flush()
 * interface.  However, the new interface is GPL-only thus we implement
 * our own trivial wrapper when the GPL-only version is detected.
 *
 * 2.6.36 - 4.6 API,
 * The blk_queue_flush() interface has replaced blk_queue_ordered()
 * interface.  However, while the old interface was available to all the
 * new one is GPL-only.   Thus if the GPL-only version is detected we
 * implement our own trivial helper.
 */
static inline void
blk_queue_set_write_cache(struct request_queue *q, bool wc, bool fua)
{
#if defined(HAVE_BLK_QUEUE_WRITE_CACHE_GPL_ONLY)
	if (wc)
		blk_queue_flag_set(QUEUE_FLAG_WC, q);
	else
		blk_queue_flag_clear(QUEUE_FLAG_WC, q);
	if (fua)
		blk_queue_flag_set(QUEUE_FLAG_FUA, q);
	else
		blk_queue_flag_clear(QUEUE_FLAG_FUA, q);
#elif defined(HAVE_BLK_QUEUE_WRITE_CACHE)
	blk_queue_write_cache(q, wc, fua);
#elif defined(HAVE_BLK_QUEUE_FLUSH_GPL_ONLY)
	if (wc)
		q->flush_flags |= REQ_FLUSH;
	if (fua)
		q->flush_flags |= REQ_FUA;
#elif defined(HAVE_BLK_QUEUE_FLUSH)
	blk_queue_flush(q, (wc ? REQ_FLUSH : 0) | (fua ? REQ_FUA : 0));
#else
#error "Unsupported kernel"
#endif
}

static inline void
blk_queue_set_read_ahead(struct request_queue *q, unsigned long ra_pages)
{
#if !defined(HAVE_BLK_QUEUE_UPDATE_READAHEAD) && \
	!defined(HAVE_DISK_UPDATE_READAHEAD)
#ifdef HAVE_BLK_QUEUE_BDI_DYNAMIC
	q->backing_dev_info->ra_pages = ra_pages;
#else
	q->backing_dev_info.ra_pages = ra_pages;
#endif
#endif
}

#ifdef HAVE_BIO_BVEC_ITER
#define	BIO_BI_SECTOR(bio)	(bio)->bi_iter.bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_iter.bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_iter.bi_idx
#define	BIO_BI_SKIP(bio)	(bio)->bi_iter.bi_bvec_done
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bv), (b), (i))
typedef struct bvec_iter bvec_iterator_t;
#else
#define	BIO_BI_SECTOR(bio)	(bio)->bi_sector
#define	BIO_BI_SIZE(bio)	(bio)->bi_size
#define	BIO_BI_IDX(bio)		(bio)->bi_idx
#define	BIO_BI_SKIP(bio)	(0)
#define	bio_for_each_segment4(bv, bvp, b, i)	\
	bio_for_each_segment((bvp), (b), (i))
typedef int bvec_iterator_t;
#endif

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

	*flags |= REQ_FAILFAST_MASK;
}

/*
 * Maximum disk label length, it may be undefined for some kernels.
 */
#if !defined(DISK_NAME_LEN)
#define	DISK_NAME_LEN	32
#endif /* DISK_NAME_LEN */

#ifdef HAVE_BIO_BI_STATUS
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
	case BLK_STS_NEXUS:
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
		return (BLK_STS_NEXUS);
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
#endif /* HAVE_BIO_BI_STATUS */

/*
 * 4.3 API change
 * The bio_endio() prototype changed slightly.  These are helper
 * macro's to ensure the prototype and invocation are handled.
 */
#ifdef HAVE_1ARG_BIO_END_IO_T
#ifdef HAVE_BIO_BI_STATUS
#define	BIO_END_IO_ERROR(bio)		bi_status_to_errno(bio->bi_status)
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x)
#define	BIO_END_IO(bio, error)		bio_set_bi_status(bio, error)
static inline void
bio_set_bi_status(struct bio *bio, int error)
{
	ASSERT3S(error, <=, 0);
	bio->bi_status = errno_to_bi_status(-error);
	bio_endio(bio);
}
#else
#define	BIO_END_IO_ERROR(bio)		(-(bio->bi_error))
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x)
#define	BIO_END_IO(bio, error)		bio_set_bi_error(bio, error)
static inline void
bio_set_bi_error(struct bio *bio, int error)
{
	ASSERT3S(error, <=, 0);
	bio->bi_error = error;
	bio_endio(bio);
}
#endif /* HAVE_BIO_BI_STATUS */

#else
#define	BIO_END_IO_PROTO(fn, x, z)	static void fn(struct bio *x, int z)
#define	BIO_END_IO(bio, error)		bio_endio(bio, error);
#endif /* HAVE_1ARG_BIO_END_IO_T */

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
 * 4.4.0-6.21 API change for Ubuntu
 * lookup_bdev() gained a second argument, FMODE_*, to check inode permissions.
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
#elif defined(HAVE_MODE_LOOKUP_BDEV)
	struct block_device *bdev = lookup_bdev(path, FMODE_READ);
	if (IS_ERR(bdev))
		return (PTR_ERR(bdev));

	*dev = bdev->bd_dev;
	bdput(bdev);

	return (0);
#else
#error "Unsupported kernel"
#endif
}

/*
 * Kernels without bio_set_op_attrs use bi_rw for the bio flags.
 */
#if !defined(HAVE_BIO_SET_OP_ATTRS)
static inline void
bio_set_op_attrs(struct bio *bio, unsigned rw, unsigned flags)
{
	bio->bi_rw |= rw | flags;
}
#endif

/*
 * bio_set_flush - Set the appropriate flags in a bio to guarantee
 * data are on non-volatile media on completion.
 *
 * 2.6.37 - 4.8 API,
 *   Introduce WRITE_FLUSH, WRITE_FUA, and WRITE_FLUSH_FUA flags as a
 *   replacement for WRITE_BARRIER to allow expressing richer semantics
 *   to the block layer.  It's up to the block layer to implement the
 *   semantics correctly. Use the WRITE_FLUSH_FUA flag combination.
 *
 * 4.8 - 4.9 API,
 *   REQ_FLUSH was renamed to REQ_PREFLUSH.  For consistency with previous
 *   OpenZFS releases, prefer the WRITE_FLUSH_FUA flag set if it's available.
 *
 * 4.10 API,
 *   The read/write flags and their modifiers, including WRITE_FLUSH,
 *   WRITE_FUA and WRITE_FLUSH_FUA were removed from fs.h in
 *   torvalds/linux@70fd7614 and replaced by direct flag modification
 *   of the REQ_ flags in bio->bi_opf.  Use REQ_PREFLUSH.
 */
static inline void
bio_set_flush(struct bio *bio)
{
#if defined(HAVE_REQ_PREFLUSH)	/* >= 4.10 */
	bio_set_op_attrs(bio, 0, REQ_PREFLUSH);
#elif defined(WRITE_FLUSH_FUA)	/* >= 2.6.37 and <= 4.9 */
	bio_set_op_attrs(bio, 0, WRITE_FLUSH_FUA);
#else
#error	"Allowing the build will cause bio_set_flush requests to be ignored."
#endif
}

/*
 * 4.8 API,
 *   REQ_OP_FLUSH
 *
 * 4.8-rc0 - 4.8-rc1,
 *   REQ_PREFLUSH
 *
 * 2.6.36 - 4.7 API,
 *   REQ_FLUSH
 *
 * in all cases but may have a performance impact for some kernels.  It
 * has the advantage of minimizing kernel specific changes in the zvol code.
 *
 */
static inline boolean_t
bio_is_flush(struct bio *bio)
{
#if defined(HAVE_REQ_OP_FLUSH) && defined(HAVE_BIO_BI_OPF)
	return ((bio_op(bio) == REQ_OP_FLUSH) || (bio->bi_opf & REQ_PREFLUSH));
#elif defined(HAVE_REQ_PREFLUSH) && defined(HAVE_BIO_BI_OPF)
	return (bio->bi_opf & REQ_PREFLUSH);
#elif defined(HAVE_REQ_PREFLUSH) && !defined(HAVE_BIO_BI_OPF)
	return (bio->bi_rw & REQ_PREFLUSH);
#elif defined(HAVE_REQ_FLUSH)
	return (bio->bi_rw & REQ_FLUSH);
#else
#error	"Unsupported kernel"
#endif
}

/*
 * 4.8 API,
 *   REQ_FUA flag moved to bio->bi_opf
 *
 * 2.6.x - 4.7 API,
 *   REQ_FUA
 */
static inline boolean_t
bio_is_fua(struct bio *bio)
{
#if defined(HAVE_BIO_BI_OPF)
	return (bio->bi_opf & REQ_FUA);
#elif defined(REQ_FUA)
	return (bio->bi_rw & REQ_FUA);
#else
#error	"Allowing the build will cause fua requests to be ignored."
#endif
}

/*
 * 4.8 API,
 *   REQ_OP_DISCARD
 *
 * 2.6.36 - 4.7 API,
 *   REQ_DISCARD
 *
 * In all cases the normal I/O path is used for discards.  The only
 * difference is how the kernel tags individual I/Os as discards.
 */
static inline boolean_t
bio_is_discard(struct bio *bio)
{
#if defined(HAVE_REQ_OP_DISCARD)
	return (bio_op(bio) == REQ_OP_DISCARD);
#elif defined(HAVE_REQ_DISCARD)
	return (bio->bi_rw & REQ_DISCARD);
#else
#error "Unsupported kernel"
#endif
}

/*
 * 4.8 API,
 *   REQ_OP_SECURE_ERASE
 *
 * 2.6.36 - 4.7 API,
 *   REQ_SECURE
 */
static inline boolean_t
bio_is_secure_erase(struct bio *bio)
{
#if defined(HAVE_REQ_OP_SECURE_ERASE)
	return (bio_op(bio) == REQ_OP_SECURE_ERASE);
#elif defined(REQ_SECURE)
	return (bio->bi_rw & REQ_SECURE);
#else
	return (0);
#endif
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
	return (!!bdev_max_discard_sectors(bdev));
#elif defined(HAVE_BLK_QUEUE_DISCARD)
	return (!!blk_queue_discard(bdev_get_queue(bdev)));
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
 *
 * 2.6.36 - 4.7 API,
 *   blk_queue_secdiscard()
 */
static inline boolean_t
bdev_secure_discard_supported(struct block_device *bdev)
{
#if defined(HAVE_BDEV_MAX_SECURE_ERASE_SECTORS)
	return (!!bdev_max_secure_erase_sectors(bdev));
#elif defined(HAVE_BLK_QUEUE_SECURE_ERASE)
	return (!!blk_queue_secure_erase(bdev_get_queue(bdev)));
#elif defined(HAVE_BLK_QUEUE_SECDISCARD)
	return (!!blk_queue_secdiscard(bdev_get_queue(bdev)));
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
#if defined(HAVE_BDEV_IO_ACCT)
	return (bdev_start_io_acct(bio->bi_bdev, bio_sectors(bio),
	    bio_op(bio), jiffies));
#elif defined(HAVE_DISK_IO_ACCT)
	return (disk_start_io_acct(disk, bio_sectors(bio), bio_op(bio)));
#elif defined(HAVE_BIO_IO_ACCT)
	return (bio_start_io_acct(bio));
#elif defined(HAVE_GENERIC_IO_ACCT_3ARG)
	unsigned long start_time = jiffies;
	generic_start_io_acct(rw, bio_sectors(bio), &disk->part0);
	return (start_time);
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
#if defined(HAVE_BDEV_IO_ACCT)
	bdev_end_io_acct(bio->bi_bdev, bio_op(bio), start_time);
#elif defined(HAVE_DISK_IO_ACCT)
	disk_end_io_acct(disk, bio_op(bio), start_time);
#elif defined(HAVE_BIO_IO_ACCT)
	bio_end_io_acct(bio, start_time);
#elif defined(HAVE_GENERIC_IO_ACCT_3ARG)
	generic_end_io_acct(rw, &disk->part0, start_time);
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

#endif /* _ZFS_BLKDEV_H */
