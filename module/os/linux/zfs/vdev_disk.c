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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Rewritten for Linux by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 * Copyright (c) 2012, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2023, 2024, Klara Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <linux/blkpg.h>
#include <linux/msdos_fs.h>
#include <linux/vfs_compat.h>
#ifdef HAVE_LINUX_BLK_CGROUP_HEADER
#include <linux/blk-cgroup.h>
#endif

/*
 * Linux 6.8.x uses a bdev_handle as an instance/refcount for an underlying
 * block_device. Since it carries the block_device inside, its convenient to
 * just use the handle as a proxy.
 *
 * Linux 6.9.x uses a file for the same purpose.
 *
 * For pre-6.8, we just emulate this with a cast, since we don't need any of
 * the other fields inside the handle.
 */
#if defined(HAVE_BDEV_OPEN_BY_PATH)
typedef struct bdev_handle zfs_bdev_handle_t;
#define	BDH_BDEV(bdh)		((bdh)->bdev)
#define	BDH_IS_ERR(bdh)		(IS_ERR(bdh))
#define	BDH_PTR_ERR(bdh)	(PTR_ERR(bdh))
#define	BDH_ERR_PTR(err)	(ERR_PTR(err))
#elif defined(HAVE_BDEV_FILE_OPEN_BY_PATH)
typedef struct file zfs_bdev_handle_t;
#define	BDH_BDEV(bdh)		(file_bdev(bdh))
#define	BDH_IS_ERR(bdh)		(IS_ERR(bdh))
#define	BDH_PTR_ERR(bdh)	(PTR_ERR(bdh))
#define	BDH_ERR_PTR(err)	(ERR_PTR(err))
#else
typedef void zfs_bdev_handle_t;
#define	BDH_BDEV(bdh)		((struct block_device *)bdh)
#define	BDH_IS_ERR(bdh)		(IS_ERR(BDH_BDEV(bdh)))
#define	BDH_PTR_ERR(bdh)	(PTR_ERR(BDH_BDEV(bdh)))
#define	BDH_ERR_PTR(err)	(ERR_PTR(err))
#endif

typedef struct vdev_disk {
	zfs_bdev_handle_t		*vd_bdh;
	krwlock_t			vd_lock;
} vdev_disk_t;

/*
 * Maximum number of segments to add to a bio (min 4). If this is higher than
 * the maximum allowed by the device queue or the kernel itself, it will be
 * clamped. Setting it to zero will cause the kernel's ideal size to be used.
 */
uint_t zfs_vdev_disk_max_segs = 0;

/*
 * Unique identifier for the exclusive vdev holder.
 */
static void *zfs_vdev_holder = VDEV_HOLDER;

/*
 * Wait up to zfs_vdev_open_timeout_ms milliseconds before determining the
 * device is missing. The missing path may be transient since the links
 * can be briefly removed and recreated in response to udev events.
 */
static uint_t zfs_vdev_open_timeout_ms = 1000;

/*
 * Size of the "reserved" partition, in blocks.
 */
#define	EFI_MIN_RESV_SIZE	(16 * 1024)

/*
 * BIO request failfast mask.
 */

static unsigned int zfs_vdev_failfast_mask = 1;

/*
 * Convert SPA mode flags into bdev open mode flags.
 */
#ifdef HAVE_BLK_MODE_T
typedef blk_mode_t vdev_bdev_mode_t;
#define	VDEV_BDEV_MODE_READ	BLK_OPEN_READ
#define	VDEV_BDEV_MODE_WRITE	BLK_OPEN_WRITE
#define	VDEV_BDEV_MODE_EXCL	BLK_OPEN_EXCL
#define	VDEV_BDEV_MODE_MASK	(BLK_OPEN_READ|BLK_OPEN_WRITE|BLK_OPEN_EXCL)
#else
typedef fmode_t vdev_bdev_mode_t;
#define	VDEV_BDEV_MODE_READ	FMODE_READ
#define	VDEV_BDEV_MODE_WRITE	FMODE_WRITE
#define	VDEV_BDEV_MODE_EXCL	FMODE_EXCL
#define	VDEV_BDEV_MODE_MASK	(FMODE_READ|FMODE_WRITE|FMODE_EXCL)
#endif

static vdev_bdev_mode_t
vdev_bdev_mode(spa_mode_t smode)
{
	ASSERT3U(smode, !=, SPA_MODE_UNINIT);
	ASSERT0(smode & ~(SPA_MODE_READ|SPA_MODE_WRITE));

	vdev_bdev_mode_t bmode = VDEV_BDEV_MODE_EXCL;

	if (smode & SPA_MODE_READ)
		bmode |= VDEV_BDEV_MODE_READ;

	if (smode & SPA_MODE_WRITE)
		bmode |= VDEV_BDEV_MODE_WRITE;

	ASSERT(bmode & VDEV_BDEV_MODE_MASK);
	ASSERT0(bmode & ~VDEV_BDEV_MODE_MASK);

	return (bmode);
}

/*
 * Returns the usable capacity (in bytes) for the partition or disk.
 */
static uint64_t
bdev_capacity(struct block_device *bdev)
{
#ifdef HAVE_BDEV_NR_BYTES
	return (bdev_nr_bytes(bdev));
#else
	return (i_size_read(bdev->bd_inode));
#endif
}

#if !defined(HAVE_BDEV_WHOLE)
static inline struct block_device *
bdev_whole(struct block_device *bdev)
{
	return (bdev->bd_contains);
}
#endif

#if defined(HAVE_BDEVNAME)
#define	vdev_bdevname(bdev, name)	bdevname(bdev, name)
#else
static inline void
vdev_bdevname(struct block_device *bdev, char *name)
{
	snprintf(name, BDEVNAME_SIZE, "%pg", bdev);
}
#endif

/*
 * Returns the maximum expansion capacity of the block device (in bytes).
 *
 * It is possible to expand a vdev when it has been created as a wholedisk
 * and the containing block device has increased in capacity.  Or when the
 * partition containing the pool has been manually increased in size.
 *
 * This function is only responsible for calculating the potential expansion
 * size so it can be reported by 'zpool list'.  The efi_use_whole_disk() is
 * responsible for verifying the expected partition layout in the wholedisk
 * case, and updating the partition table if appropriate.  Once the partition
 * size has been increased the additional capacity will be visible using
 * bdev_capacity().
 *
 * The returned maximum expansion capacity is always expected to be larger, or
 * at the very least equal, to its usable capacity to prevent overestimating
 * the pool expandsize.
 */
static uint64_t
bdev_max_capacity(struct block_device *bdev, uint64_t wholedisk)
{
	uint64_t psize;
	int64_t available;

	if (wholedisk && bdev != bdev_whole(bdev)) {
		/*
		 * When reporting maximum expansion capacity for a wholedisk
		 * deduct any capacity which is expected to be lost due to
		 * alignment restrictions.  Over reporting this value isn't
		 * harmful and would only result in slightly less capacity
		 * than expected post expansion.
		 * The estimated available space may be slightly smaller than
		 * bdev_capacity() for devices where the number of sectors is
		 * not a multiple of the alignment size and the partition layout
		 * is keeping less than PARTITION_END_ALIGNMENT bytes after the
		 * "reserved" EFI partition: in such cases return the device
		 * usable capacity.
		 */
		available = bdev_capacity(bdev_whole(bdev)) -
		    ((EFI_MIN_RESV_SIZE + NEW_START_BLOCK +
		    PARTITION_END_ALIGNMENT) << SECTOR_BITS);
		psize = MAX(available, bdev_capacity(bdev));
	} else {
		psize = bdev_capacity(bdev);
	}

	return (psize);
}

static void
vdev_disk_error(zio_t *zio)
{
	/*
	 * This function can be called in interrupt context, for instance while
	 * handling IRQs coming from a misbehaving disk device; use printk()
	 * which is safe from any context.
	 */
	printk(KERN_WARNING "zio pool=%s vdev=%s error=%d type=%d "
	    "offset=%llu size=%llu flags=%llu\n", spa_name(zio->io_spa),
	    zio->io_vd->vdev_path, zio->io_error, zio->io_type,
	    (u_longlong_t)zio->io_offset, (u_longlong_t)zio->io_size,
	    zio->io_flags);
}

static void
vdev_disk_kobj_evt_post(vdev_t *v)
{
	vdev_disk_t *vd = v->vdev_tsd;
	if (vd && vd->vd_bdh) {
		spl_signal_kobj_evt(BDH_BDEV(vd->vd_bdh));
	} else {
		vdev_dbgmsg(v, "vdev_disk_t is NULL for VDEV:%s\n",
		    v->vdev_path);
	}
}

static zfs_bdev_handle_t *
vdev_blkdev_get_by_path(const char *path, spa_mode_t smode, void *holder)
{
	vdev_bdev_mode_t bmode = vdev_bdev_mode(smode);

#if defined(HAVE_BDEV_FILE_OPEN_BY_PATH)
	return (bdev_file_open_by_path(path, bmode, holder, NULL));
#elif defined(HAVE_BDEV_OPEN_BY_PATH)
	return (bdev_open_by_path(path, bmode, holder, NULL));
#elif defined(HAVE_BLKDEV_GET_BY_PATH_4ARG)
	return (blkdev_get_by_path(path, bmode, holder, NULL));
#else
	return (blkdev_get_by_path(path, bmode, holder));
#endif
}

static void
vdev_blkdev_put(zfs_bdev_handle_t *bdh, spa_mode_t smode, void *holder)
{
#if defined(HAVE_BDEV_RELEASE)
	return (bdev_release(bdh));
#elif defined(HAVE_BLKDEV_PUT_HOLDER)
	return (blkdev_put(BDH_BDEV(bdh), holder));
#elif defined(HAVE_BLKDEV_PUT)
	return (blkdev_put(BDH_BDEV(bdh), vdev_bdev_mode(smode)));
#else
	fput(bdh);
#endif
}

static int
vdev_disk_open(vdev_t *v, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	zfs_bdev_handle_t *bdh;
	spa_mode_t smode = spa_mode(v->vdev_spa);
	hrtime_t timeout = MSEC2NSEC(zfs_vdev_open_timeout_ms);
	vdev_disk_t *vd;

	/* Must have a pathname and it must be absolute. */
	if (v->vdev_path == NULL || v->vdev_path[0] != '/') {
		v->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		vdev_dbgmsg(v, "invalid vdev_path");
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Reopen the device if it is currently open.  When expanding a
	 * partition force re-scanning the partition table if userland
	 * did not take care of this already. We need to do this while closed
	 * in order to get an accurate updated block device size.  Then
	 * since udev may need to recreate the device links increase the
	 * open retry timeout before reporting the device as unavailable.
	 */
	vd = v->vdev_tsd;
	if (vd) {
		char disk_name[BDEVNAME_SIZE + 6] = "/dev/";
		boolean_t reread_part = B_FALSE;

		rw_enter(&vd->vd_lock, RW_WRITER);
		bdh = vd->vd_bdh;
		vd->vd_bdh = NULL;

		if (bdh) {
			struct block_device *bdev = BDH_BDEV(bdh);
			if (v->vdev_expanding && bdev != bdev_whole(bdev)) {
				vdev_bdevname(bdev_whole(bdev), disk_name + 5);
				/*
				 * If userland has BLKPG_RESIZE_PARTITION,
				 * then it should have updated the partition
				 * table already. We can detect this by
				 * comparing our current physical size
				 * with that of the device. If they are
				 * the same, then we must not have
				 * BLKPG_RESIZE_PARTITION or it failed to
				 * update the partition table online. We
				 * fallback to rescanning the partition
				 * table from the kernel below. However,
				 * if the capacity already reflects the
				 * updated partition, then we skip
				 * rescanning the partition table here.
				 */
				if (v->vdev_psize == bdev_capacity(bdev))
					reread_part = B_TRUE;
			}

			vdev_blkdev_put(bdh, smode, zfs_vdev_holder);
		}

		if (reread_part) {
			bdh = vdev_blkdev_get_by_path(disk_name, smode,
			    zfs_vdev_holder);
			if (!BDH_IS_ERR(bdh)) {
				int error =
				    vdev_bdev_reread_part(BDH_BDEV(bdh));
				vdev_blkdev_put(bdh, smode, zfs_vdev_holder);
				if (error == 0) {
					timeout = MSEC2NSEC(
					    zfs_vdev_open_timeout_ms * 2);
				}
			}
		}
	} else {
		vd = kmem_zalloc(sizeof (vdev_disk_t), KM_SLEEP);

		rw_init(&vd->vd_lock, NULL, RW_DEFAULT, NULL);
		rw_enter(&vd->vd_lock, RW_WRITER);
	}

	/*
	 * Devices are always opened by the path provided at configuration
	 * time.  This means that if the provided path is a udev by-id path
	 * then drives may be re-cabled without an issue.  If the provided
	 * path is a udev by-path path, then the physical location information
	 * will be preserved.  This can be critical for more complicated
	 * configurations where drives are located in specific physical
	 * locations to maximize the systems tolerance to component failure.
	 *
	 * Alternatively, you can provide your own udev rule to flexibly map
	 * the drives as you see fit.  It is not advised that you use the
	 * /dev/[hd]d devices which may be reordered due to probing order.
	 * Devices in the wrong locations will be detected by the higher
	 * level vdev validation.
	 *
	 * The specified paths may be briefly removed and recreated in
	 * response to udev events.  This should be exceptionally unlikely
	 * because the zpool command makes every effort to verify these paths
	 * have already settled prior to reaching this point.  Therefore,
	 * a ENOENT failure at this point is highly likely to be transient
	 * and it is reasonable to sleep and retry before giving up.  In
	 * practice delays have been observed to be on the order of 100ms.
	 *
	 * When ERESTARTSYS is returned it indicates the block device is
	 * a zvol which could not be opened due to the deadlock detection
	 * logic in zvol_open().  Extend the timeout and retry the open
	 * subsequent attempts are expected to eventually succeed.
	 */
	hrtime_t start = gethrtime();
	bdh = BDH_ERR_PTR(-ENXIO);
	while (BDH_IS_ERR(bdh) && ((gethrtime() - start) < timeout)) {
		bdh = vdev_blkdev_get_by_path(v->vdev_path, smode,
		    zfs_vdev_holder);
		if (unlikely(BDH_PTR_ERR(bdh) == -ENOENT)) {
			/*
			 * There is no point of waiting since device is removed
			 * explicitly
			 */
			if (v->vdev_removed)
				break;

			schedule_timeout(MSEC_TO_TICK(10));
		} else if (unlikely(BDH_PTR_ERR(bdh) == -ERESTARTSYS)) {
			timeout = MSEC2NSEC(zfs_vdev_open_timeout_ms * 10);
			continue;
		} else if (BDH_IS_ERR(bdh)) {
			break;
		}
	}

	if (BDH_IS_ERR(bdh)) {
		int error = -BDH_PTR_ERR(bdh);
		vdev_dbgmsg(v, "open error=%d timeout=%llu/%llu", error,
		    (u_longlong_t)(gethrtime() - start),
		    (u_longlong_t)timeout);
		vd->vd_bdh = NULL;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
		return (SET_ERROR(error));
	} else {
		vd->vd_bdh = bdh;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
	}

	struct block_device *bdev = BDH_BDEV(vd->vd_bdh);

	/*  Determine the physical block size */
	int physical_block_size = bdev_physical_block_size(bdev);

	/*  Determine the logical block size */
	int logical_block_size = bdev_logical_block_size(bdev);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	v->vdev_nowritecache = B_FALSE;

	/* Set when device reports it supports TRIM. */
	v->vdev_has_trim = bdev_discard_supported(bdev);

	/* Set when device reports it supports secure TRIM. */
	v->vdev_has_securetrim = bdev_secure_discard_supported(bdev);

	/* Inform the ZIO pipeline that we are non-rotational */
	v->vdev_nonrot = blk_queue_nonrot(bdev_get_queue(bdev));

	/* Physical volume size in bytes for the partition */
	*psize = bdev_capacity(bdev);

	/* Physical volume size in bytes including possible expansion space */
	*max_psize = bdev_max_capacity(bdev, v->vdev_wholedisk);

	/* Based on the minimum sector size set the block size */
	*physical_ashift = highbit64(MAX(physical_block_size,
	    SPA_MINBLOCKSIZE)) - 1;

	*logical_ashift = highbit64(MAX(logical_block_size,
	    SPA_MINBLOCKSIZE)) - 1;

	return (0);
}

static void
vdev_disk_close(vdev_t *v)
{
	vdev_disk_t *vd = v->vdev_tsd;

	if (v->vdev_reopening || vd == NULL)
		return;

	if (vd->vd_bdh != NULL)
		vdev_blkdev_put(vd->vd_bdh, spa_mode(v->vdev_spa),
		    zfs_vdev_holder);

	rw_destroy(&vd->vd_lock);
	kmem_free(vd, sizeof (vdev_disk_t));
	v->vdev_tsd = NULL;
}

static inline void
vdev_submit_bio_impl(struct bio *bio)
{
#ifdef HAVE_1ARG_SUBMIT_BIO
	(void) submit_bio(bio);
#else
	(void) submit_bio(bio_data_dir(bio), bio);
#endif
}

/*
 * preempt_schedule_notrace is GPL-only which breaks the ZFS build, so
 * replace it with preempt_schedule under the following condition:
 */
#if defined(CONFIG_ARM64) && \
    defined(CONFIG_PREEMPTION) && \
    defined(CONFIG_BLK_CGROUP)
#define	preempt_schedule_notrace(x) preempt_schedule(x)
#endif

/*
 * As for the Linux 5.18 kernel bio_alloc() expects a block_device struct
 * as an argument removing the need to set it with bio_set_dev().  This
 * removes the need for all of the following compatibility code.
 */
#if !defined(HAVE_BIO_ALLOC_4ARG)

#ifdef HAVE_BIO_SET_DEV
#if defined(CONFIG_BLK_CGROUP) && defined(HAVE_BIO_SET_DEV_GPL_ONLY)
/*
 * The Linux 5.5 kernel updated percpu_ref_tryget() which is inlined by
 * blkg_tryget() to use rcu_read_lock() instead of rcu_read_lock_sched().
 * As a side effect the function was converted to GPL-only.  Define our
 * own version when needed which uses rcu_read_lock_sched().
 *
 * The Linux 5.17 kernel split linux/blk-cgroup.h into a private and a public
 * part, moving blkg_tryget into the private one. Define our own version.
 */
#if defined(HAVE_BLKG_TRYGET_GPL_ONLY) || !defined(HAVE_BLKG_TRYGET)
static inline bool
vdev_blkg_tryget(struct blkcg_gq *blkg)
{
	struct percpu_ref *ref = &blkg->refcnt;
	unsigned long __percpu *count;
	bool rc;

	rcu_read_lock_sched();

	if (__ref_is_percpu(ref, &count)) {
		this_cpu_inc(*count);
		rc = true;
	} else {
#ifdef ZFS_PERCPU_REF_COUNT_IN_DATA
		rc = atomic_long_inc_not_zero(&ref->data->count);
#else
		rc = atomic_long_inc_not_zero(&ref->count);
#endif
	}

	rcu_read_unlock_sched();

	return (rc);
}
#else
#define	vdev_blkg_tryget(bg)	blkg_tryget(bg)
#endif
#ifdef HAVE_BIO_SET_DEV_MACRO
/*
 * The Linux 5.0 kernel updated the bio_set_dev() macro so it calls the
 * GPL-only bio_associate_blkg() symbol thus inadvertently converting
 * the entire macro.  Provide a minimal version which always assigns the
 * request queue's root_blkg to the bio.
 */
static inline void
vdev_bio_associate_blkg(struct bio *bio)
{
#if defined(HAVE_BIO_BDEV_DISK)
	struct request_queue *q = bio->bi_bdev->bd_disk->queue;
#else
	struct request_queue *q = bio->bi_disk->queue;
#endif

	ASSERT3P(q, !=, NULL);
	ASSERT3P(bio->bi_blkg, ==, NULL);

	if (q->root_blkg && vdev_blkg_tryget(q->root_blkg))
		bio->bi_blkg = q->root_blkg;
}

#define	bio_associate_blkg vdev_bio_associate_blkg
#else
static inline void
vdev_bio_set_dev(struct bio *bio, struct block_device *bdev)
{
#if defined(HAVE_BIO_BDEV_DISK)
	struct request_queue *q = bdev->bd_disk->queue;
#else
	struct request_queue *q = bio->bi_disk->queue;
#endif
	bio_clear_flag(bio, BIO_REMAPPED);
	if (bio->bi_bdev != bdev)
		bio_clear_flag(bio, BIO_THROTTLED);
	bio->bi_bdev = bdev;

	ASSERT3P(q, !=, NULL);
	ASSERT3P(bio->bi_blkg, ==, NULL);

	if (q->root_blkg && vdev_blkg_tryget(q->root_blkg))
		bio->bi_blkg = q->root_blkg;
}
#define	bio_set_dev		vdev_bio_set_dev
#endif
#endif
#else
/*
 * Provide a bio_set_dev() helper macro for pre-Linux 4.14 kernels.
 */
static inline void
bio_set_dev(struct bio *bio, struct block_device *bdev)
{
	bio->bi_bdev = bdev;
}
#endif /* HAVE_BIO_SET_DEV */
#endif /* !HAVE_BIO_ALLOC_4ARG */

static inline void
vdev_submit_bio(struct bio *bio)
{
	struct bio_list *bio_list = current->bio_list;
	current->bio_list = NULL;
	vdev_submit_bio_impl(bio);
	current->bio_list = bio_list;
}

static inline struct bio *
vdev_bio_alloc(struct block_device *bdev, gfp_t gfp_mask,
    unsigned short nr_vecs)
{
	struct bio *bio;

#ifdef HAVE_BIO_ALLOC_4ARG
	bio = bio_alloc(bdev, nr_vecs, 0, gfp_mask);
#else
	bio = bio_alloc(gfp_mask, nr_vecs);
	if (likely(bio != NULL))
		bio_set_dev(bio, bdev);
#endif

	return (bio);
}

static inline uint_t
vdev_bio_max_segs(struct block_device *bdev)
{
	/*
	 * Smallest of the device max segs and the tuneable max segs. Minimum
	 * 4, so there's room to finish split pages if they come up.
	 */
	const uint_t dev_max_segs = queue_max_segments(bdev_get_queue(bdev));
	const uint_t tune_max_segs = (zfs_vdev_disk_max_segs > 0) ?
	    MAX(4, zfs_vdev_disk_max_segs) : dev_max_segs;
	const uint_t max_segs = MIN(tune_max_segs, dev_max_segs);

#ifdef HAVE_BIO_MAX_SEGS
	return (bio_max_segs(max_segs));
#else
	return (MIN(max_segs, BIO_MAX_PAGES));
#endif
}

static inline uint_t
vdev_bio_max_bytes(struct block_device *bdev)
{
	return (queue_max_sectors(bdev_get_queue(bdev)) << 9);
}


/*
 * Virtual block IO object (VBIO)
 *
 * Linux block IO (BIO) objects have a limit on how many data segments (pages)
 * they can hold. Depending on how they're allocated and structured, a large
 * ZIO can require more than one BIO to be submitted to the kernel, which then
 * all have to complete before we can return the completed ZIO back to ZFS.
 *
 * A VBIO is a wrapper around multiple BIOs, carrying everything needed to
 * translate a ZIO down into the kernel block layer and back again.
 *
 * Note that these are only used for data ZIOs (read/write). Meta-operations
 * (flush/trim) don't need multiple BIOs and so can just make the call
 * directly.
 */
typedef struct {
	zio_t		*vbio_zio;	/* parent zio */

	struct block_device *vbio_bdev;	/* blockdev to submit bios to */

	abd_t		*vbio_abd;	/* abd carrying borrowed linear buf */

	uint_t		vbio_max_segs;	/* max segs per bio */

	uint_t		vbio_max_bytes;	/* max bytes per bio */
	uint_t		vbio_lbs_mask;	/* logical block size mask */

	uint64_t	vbio_offset;	/* start offset of next bio */

	struct bio	*vbio_bio;	/* pointer to the current bio */
	int		vbio_flags;	/* bio flags */
} vbio_t;

static vbio_t *
vbio_alloc(zio_t *zio, struct block_device *bdev, int flags)
{
	vbio_t *vbio = kmem_zalloc(sizeof (vbio_t), KM_SLEEP);

	vbio->vbio_zio = zio;
	vbio->vbio_bdev = bdev;
	vbio->vbio_abd = NULL;
	vbio->vbio_max_segs = vdev_bio_max_segs(bdev);
	vbio->vbio_max_bytes = vdev_bio_max_bytes(bdev);
	vbio->vbio_lbs_mask = ~(bdev_logical_block_size(bdev)-1);
	vbio->vbio_offset = zio->io_offset;
	vbio->vbio_bio = NULL;
	vbio->vbio_flags = flags;

	return (vbio);
}

BIO_END_IO_PROTO(vbio_completion, bio, error);

static int
vbio_add_page(vbio_t *vbio, struct page *page, uint_t size, uint_t offset)
{
	struct bio *bio = vbio->vbio_bio;
	uint_t ssize;

	while (size > 0) {
		if (bio == NULL) {
			/* New BIO, allocate and set up */
			bio = vdev_bio_alloc(vbio->vbio_bdev, GFP_NOIO,
			    vbio->vbio_max_segs);
			VERIFY(bio);

			BIO_BI_SECTOR(bio) = vbio->vbio_offset >> 9;
			bio_set_op_attrs(bio,
			    vbio->vbio_zio->io_type == ZIO_TYPE_WRITE ?
			    WRITE : READ, vbio->vbio_flags);

			if (vbio->vbio_bio) {
				bio_chain(vbio->vbio_bio, bio);
				vdev_submit_bio(vbio->vbio_bio);
			}
			vbio->vbio_bio = bio;
		}

		/*
		 * Only load as much of the current page data as will fit in
		 * the space left in the BIO, respecting lbs alignment. Older
		 * kernels will error if we try to overfill the BIO, while
		 * newer ones will accept it and split the BIO. This ensures
		 * everything works on older kernels, and avoids an additional
		 * overhead on the new.
		 */
		ssize = MIN(size, (vbio->vbio_max_bytes - BIO_BI_SIZE(bio)) &
		    vbio->vbio_lbs_mask);
		if (ssize > 0 &&
		    bio_add_page(bio, page, ssize, offset) == ssize) {
			/* Accepted, adjust and load any remaining. */
			size -= ssize;
			offset += ssize;
			continue;
		}

		/* No room, set up for a new BIO and loop */
		vbio->vbio_offset += BIO_BI_SIZE(bio);

		/* Signal new BIO allocation wanted */
		bio = NULL;
	}

	return (0);
}

/* Iterator callback to submit ABD pages to the vbio. */
static int
vbio_fill_cb(struct page *page, size_t off, size_t len, void *priv)
{
	vbio_t *vbio = priv;
	return (vbio_add_page(vbio, page, len, off));
}

/* Create some BIOs, fill them with data and submit them */
static void
vbio_submit(vbio_t *vbio, abd_t *abd, uint64_t size)
{
	/*
	 * We plug so we can submit the BIOs as we go and only unplug them when
	 * they are fully created and submitted. This is important; if we don't
	 * plug, then the kernel may start executing earlier BIOs while we're
	 * still creating and executing later ones, and if the device goes
	 * away while that's happening, older kernels can get confused and
	 * trample memory.
	 */
	struct blk_plug plug;
	blk_start_plug(&plug);

	(void) abd_iterate_page_func(abd, 0, size, vbio_fill_cb, vbio);
	ASSERT(vbio->vbio_bio);

	vbio->vbio_bio->bi_end_io = vbio_completion;
	vbio->vbio_bio->bi_private = vbio;

	/*
	 * Once submitted, vbio_bio now owns vbio (through bi_private) and we
	 * can't touch it again. The bio may complete and vbio_completion() be
	 * called and free the vbio before this task is run again, so we must
	 * consider it invalid from this point.
	 */
	vdev_submit_bio(vbio->vbio_bio);

	blk_finish_plug(&plug);
}

/* IO completion callback */
BIO_END_IO_PROTO(vbio_completion, bio, error)
{
	vbio_t *vbio = bio->bi_private;
	zio_t *zio = vbio->vbio_zio;

	ASSERT(zio);

	/* Capture and log any errors */
#ifdef HAVE_1ARG_BIO_END_IO_T
	zio->io_error = BIO_END_IO_ERROR(bio);
#else
	zio->io_error = 0;
	if (error)
		zio->io_error = -(error);
	else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		zio->io_error = EIO;
#endif
	ASSERT3U(zio->io_error, >=, 0);

	if (zio->io_error)
		vdev_disk_error(zio);

	/* Return the BIO to the kernel */
	bio_put(bio);

	/*
	 * If we copied the ABD before issuing it, clean up and return the copy
	 * to the ADB, with changes if appropriate.
	 */
	if (vbio->vbio_abd != NULL) {
		if (zio->io_type == ZIO_TYPE_READ)
			abd_copy(zio->io_abd, vbio->vbio_abd, zio->io_size);

		abd_free(vbio->vbio_abd);
		vbio->vbio_abd = NULL;
	}

	/* Final cleanup */
	kmem_free(vbio, sizeof (vbio_t));

	/* All done, submit for processing */
	zio_delay_interrupt(zio);
}

/*
 * Iterator callback to count ABD pages and check their size & alignment.
 *
 * On Linux, each BIO segment can take a page pointer, and an offset+length of
 * the data within that page. A page can be arbitrarily large ("compound"
 * pages) but we still have to ensure the data portion is correctly sized and
 * aligned to the logical block size, to ensure that if the kernel wants to
 * split the BIO, the two halves will still be properly aligned.
 */
typedef struct {
	size_t	blocksize;
	int	seen_first;
	int	seen_last;
} vdev_disk_check_alignment_t;

static int
vdev_disk_check_alignment_cb(struct page *page, size_t off, size_t len,
    void *priv)
{
	(void) page;
	vdev_disk_check_alignment_t *s = priv;

	/*
	 * The cardinal rule: a single on-disk block must never cross an
	 * physical (order-0) page boundary, as the kernel expects to be able
	 * to split at both LBS and page boundaries.
	 *
	 * This implies various alignment rules for the blocks in this
	 * (possibly compound) page, which we can check for.
	 */

	/*
	 * If the previous page did not end on a page boundary, then we
	 * can't proceed without creating a hole.
	 */
	if (s->seen_last)
		return (1);

	/* This page must contain only whole LBS-sized blocks. */
	if (!IS_P2ALIGNED(len, s->blocksize))
		return (1);

	/*
	 * If this is not the first page in the ABD, then the data must start
	 * on a page-aligned boundary (so the kernel can split on page
	 * boundaries without having to deal with a hole). If it is, then
	 * it can start on LBS-alignment.
	 */
	if (s->seen_first) {
		if (!IS_P2ALIGNED(off, PAGESIZE))
			return (1);
	} else {
		if (!IS_P2ALIGNED(off, s->blocksize))
			return (1);
		s->seen_first = 1;
	}

	/*
	 * If this data does not end on a page-aligned boundary, then this
	 * must be the last page in the ABD, for the same reason.
	 */
	s->seen_last = !IS_P2ALIGNED(off+len, PAGESIZE);

	return (0);
}

/*
 * Check if we can submit the pages in this ABD to the kernel as-is. Returns
 * the number of pages, or 0 if it can't be submitted like this.
 */
static boolean_t
vdev_disk_check_alignment(abd_t *abd, uint64_t size, struct block_device *bdev)
{
	vdev_disk_check_alignment_t s = {
	    .blocksize = bdev_logical_block_size(bdev),
	};

	if (abd_iterate_page_func(abd, 0, size,
	    vdev_disk_check_alignment_cb, &s))
		return (B_FALSE);

	return (B_TRUE);
}

static int
vdev_disk_io_rw(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	struct block_device *bdev = BDH_BDEV(vd->vd_bdh);
	int flags = 0;

	/*
	 * Accessing outside the block device is never allowed.
	 */
	if (zio->io_offset + zio->io_size > bdev_capacity(bdev)) {
		vdev_dbgmsg(zio->io_vd,
		    "Illegal access %llu size %llu, device size %llu",
		    (u_longlong_t)zio->io_offset,
		    (u_longlong_t)zio->io_size,
		    (u_longlong_t)bdev_capacity(bdev));
		return (SET_ERROR(EIO));
	}

	if (!(zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)) &&
	    v->vdev_failfast == B_TRUE) {
		bio_set_flags_failfast(bdev, &flags, zfs_vdev_failfast_mask & 1,
		    zfs_vdev_failfast_mask & 2, zfs_vdev_failfast_mask & 4);
	}

	/*
	 * Check alignment of the incoming ABD. If any part of it would require
	 * submitting a page that is not aligned to both the logical block size
	 * and the page size, then we take a copy into a new memory region with
	 * correct alignment.  This should be impossible on a 512b LBS. On
	 * larger blocks, this can happen at least when a small number of
	 * blocks (usually 1) are allocated from a shared slab, or when
	 * abnormally-small data regions (eg gang headers) are mixed into the
	 * same ABD as larger allocations (eg aggregations).
	 */
	abd_t *abd = zio->io_abd;
	if (!vdev_disk_check_alignment(abd, zio->io_size, bdev)) {
		/* Allocate a new memory region with guaranteed alignment */
		abd = abd_alloc_for_io(zio->io_size,
		    zio->io_abd->abd_flags & ABD_FLAG_META);

		/* If we're writing copy our data into it */
		if (zio->io_type == ZIO_TYPE_WRITE)
			abd_copy(abd, zio->io_abd, zio->io_size);

		/*
		 * False here would mean the new allocation has an invalid
		 * alignment too, which would mean that abd_alloc() is not
		 * guaranteeing this, or our logic in
		 * vdev_disk_check_alignment() is wrong. In either case,
		 * something in seriously wrong and its not safe to continue.
		 */
		VERIFY(vdev_disk_check_alignment(abd, zio->io_size, bdev));
	}

	/* Allocate vbio, with a pointer to the borrowed ABD if necessary */
	vbio_t *vbio = vbio_alloc(zio, bdev, flags);
	if (abd != zio->io_abd)
		vbio->vbio_abd = abd;

	/* Fill it with data pages and submit it to the kernel */
	vbio_submit(vbio, abd, zio->io_size);
	return (0);
}

/* ========== */

/*
 * This is the classic, battle-tested BIO submission code. Until we're totally
 * sure that the new code is safe and correct in all cases, this will remain
 * available.
 *
 * It is enabled by setting zfs_vdev_disk_classic=1 at module load time. It is
 * enabled (=1) by default since 2.2.4, and disabled by default (=0) on master.
 *
 * These functions have been renamed to vdev_classic_* to make it clear what
 * they belong to, but their implementations are unchanged.
 */

/*
 * Virtual device vector for disks.
 */
typedef struct dio_request {
	zio_t			*dr_zio;	/* Parent ZIO */
	atomic_t		dr_ref;		/* References */
	int			dr_error;	/* Bio error */
	int			dr_bio_count;	/* Count of bio's */
	struct bio		*dr_bio[];	/* Attached bio's */
} dio_request_t;

static dio_request_t *
vdev_classic_dio_alloc(int bio_count)
{
	dio_request_t *dr = kmem_zalloc(sizeof (dio_request_t) +
	    sizeof (struct bio *) * bio_count, KM_SLEEP);
	atomic_set(&dr->dr_ref, 0);
	dr->dr_bio_count = bio_count;
	dr->dr_error = 0;

	for (int i = 0; i < dr->dr_bio_count; i++)
		dr->dr_bio[i] = NULL;

	return (dr);
}

static void
vdev_classic_dio_free(dio_request_t *dr)
{
	int i;

	for (i = 0; i < dr->dr_bio_count; i++)
		if (dr->dr_bio[i])
			bio_put(dr->dr_bio[i]);

	kmem_free(dr, sizeof (dio_request_t) +
	    sizeof (struct bio *) * dr->dr_bio_count);
}

static void
vdev_classic_dio_get(dio_request_t *dr)
{
	atomic_inc(&dr->dr_ref);
}

static void
vdev_classic_dio_put(dio_request_t *dr)
{
	int rc = atomic_dec_return(&dr->dr_ref);

	/*
	 * Free the dio_request when the last reference is dropped and
	 * ensure zio_interpret is called only once with the correct zio
	 */
	if (rc == 0) {
		zio_t *zio = dr->dr_zio;
		int error = dr->dr_error;

		vdev_classic_dio_free(dr);

		if (zio) {
			zio->io_error = error;
			ASSERT3S(zio->io_error, >=, 0);
			if (zio->io_error)
				vdev_disk_error(zio);

			zio_delay_interrupt(zio);
		}
	}
}

BIO_END_IO_PROTO(vdev_classic_physio_completion, bio, error)
{
	dio_request_t *dr = bio->bi_private;

	if (dr->dr_error == 0) {
#ifdef HAVE_1ARG_BIO_END_IO_T
		dr->dr_error = BIO_END_IO_ERROR(bio);
#else
		if (error)
			dr->dr_error = -(error);
		else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
			dr->dr_error = EIO;
#endif
	}

	/* Drop reference acquired by vdev_classic_physio */
	vdev_classic_dio_put(dr);
}

static inline unsigned int
vdev_classic_bio_max_segs(zio_t *zio, int bio_size, uint64_t abd_offset)
{
	unsigned long nr_segs = abd_nr_pages_off(zio->io_abd,
	    bio_size, abd_offset);

#ifdef HAVE_BIO_MAX_SEGS
	return (bio_max_segs(nr_segs));
#else
	return (MIN(nr_segs, BIO_MAX_PAGES));
#endif
}

static int
vdev_classic_physio(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	struct block_device *bdev = BDH_BDEV(vd->vd_bdh);
	size_t io_size = zio->io_size;
	uint64_t io_offset = zio->io_offset;
	int rw = zio->io_type == ZIO_TYPE_READ ? READ : WRITE;
	int flags = 0;

	dio_request_t *dr;
	uint64_t abd_offset;
	uint64_t bio_offset;
	int bio_size;
	int bio_count = 16;
	int error = 0;
	struct blk_plug plug;
	unsigned short nr_vecs;

	/*
	 * Accessing outside the block device is never allowed.
	 */
	if (io_offset + io_size > bdev_capacity(bdev)) {
		vdev_dbgmsg(zio->io_vd,
		    "Illegal access %llu size %llu, device size %llu",
		    (u_longlong_t)io_offset,
		    (u_longlong_t)io_size,
		    (u_longlong_t)bdev_capacity(bdev));
		return (SET_ERROR(EIO));
	}

retry:
	dr = vdev_classic_dio_alloc(bio_count);

	if (!(zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)) &&
	    zio->io_vd->vdev_failfast == B_TRUE) {
		bio_set_flags_failfast(bdev, &flags, zfs_vdev_failfast_mask & 1,
		    zfs_vdev_failfast_mask & 2, zfs_vdev_failfast_mask & 4);
	}

	dr->dr_zio = zio;

	/*
	 * Since bio's can have up to BIO_MAX_PAGES=256 iovec's, each of which
	 * is at least 512 bytes and at most PAGESIZE (typically 4K), one bio
	 * can cover at least 128KB and at most 1MB.  When the required number
	 * of iovec's exceeds this, we are forced to break the IO in multiple
	 * bio's and wait for them all to complete.  This is likely if the
	 * recordsize property is increased beyond 1MB.  The default
	 * bio_count=16 should typically accommodate the maximum-size zio of
	 * 16MB.
	 */

	abd_offset = 0;
	bio_offset = io_offset;
	bio_size = io_size;
	for (int i = 0; i <= dr->dr_bio_count; i++) {

		/* Finished constructing bio's for given buffer */
		if (bio_size <= 0)
			break;

		/*
		 * If additional bio's are required, we have to retry, but
		 * this should be rare - see the comment above.
		 */
		if (dr->dr_bio_count == i) {
			vdev_classic_dio_free(dr);
			bio_count *= 2;
			goto retry;
		}

		nr_vecs = vdev_classic_bio_max_segs(zio, bio_size, abd_offset);
		dr->dr_bio[i] = vdev_bio_alloc(bdev, GFP_NOIO, nr_vecs);
		if (unlikely(dr->dr_bio[i] == NULL)) {
			vdev_classic_dio_free(dr);
			return (SET_ERROR(ENOMEM));
		}

		/* Matching put called by vdev_classic_physio_completion */
		vdev_classic_dio_get(dr);

		BIO_BI_SECTOR(dr->dr_bio[i]) = bio_offset >> 9;
		dr->dr_bio[i]->bi_end_io = vdev_classic_physio_completion;
		dr->dr_bio[i]->bi_private = dr;
		bio_set_op_attrs(dr->dr_bio[i], rw, flags);

		/* Remaining size is returned to become the new size */
		bio_size = abd_bio_map_off(dr->dr_bio[i], zio->io_abd,
		    bio_size, abd_offset);

		/* Advance in buffer and construct another bio if needed */
		abd_offset += BIO_BI_SIZE(dr->dr_bio[i]);
		bio_offset += BIO_BI_SIZE(dr->dr_bio[i]);
	}

	/* Extra reference to protect dio_request during vdev_submit_bio */
	vdev_classic_dio_get(dr);

	if (dr->dr_bio_count > 1)
		blk_start_plug(&plug);

	/* Submit all bio's associated with this dio */
	for (int i = 0; i < dr->dr_bio_count; i++) {
		if (dr->dr_bio[i])
			vdev_submit_bio(dr->dr_bio[i]);
	}

	if (dr->dr_bio_count > 1)
		blk_finish_plug(&plug);

	vdev_classic_dio_put(dr);

	return (error);
}

/* ========== */

BIO_END_IO_PROTO(vdev_disk_io_flush_completion, bio, error)
{
	zio_t *zio = bio->bi_private;
#ifdef HAVE_1ARG_BIO_END_IO_T
	zio->io_error = BIO_END_IO_ERROR(bio);
#else
	zio->io_error = -error;
#endif

	if (zio->io_error && (zio->io_error == EOPNOTSUPP))
		zio->io_vd->vdev_nowritecache = B_TRUE;

	bio_put(bio);
	ASSERT3S(zio->io_error, >=, 0);
	if (zio->io_error)
		vdev_disk_error(zio);
	zio_interrupt(zio);
}

static int
vdev_disk_io_flush(struct block_device *bdev, zio_t *zio)
{
	struct request_queue *q;
	struct bio *bio;

	q = bdev_get_queue(bdev);
	if (!q)
		return (SET_ERROR(ENXIO));

	bio = vdev_bio_alloc(bdev, GFP_NOIO, 0);
	if (unlikely(bio == NULL))
		return (SET_ERROR(ENOMEM));

	bio->bi_end_io = vdev_disk_io_flush_completion;
	bio->bi_private = zio;
	bio_set_flush(bio);
	vdev_submit_bio(bio);
	invalidate_bdev(bdev);

	return (0);
}

BIO_END_IO_PROTO(vdev_disk_discard_end_io, bio, error)
{
	zio_t *zio = bio->bi_private;
#ifdef HAVE_1ARG_BIO_END_IO_T
	zio->io_error = BIO_END_IO_ERROR(bio);
#else
	zio->io_error = -error;
#endif
	bio_put(bio);
	if (zio->io_error)
		vdev_disk_error(zio);
	zio_interrupt(zio);
}

/*
 * Wrappers for the different secure erase and discard APIs. We use async
 * when available; in this case, *biop is set to the last bio in the chain.
 */
static int
vdev_bdev_issue_secure_erase(zfs_bdev_handle_t *bdh, sector_t sector,
    sector_t nsect, struct bio **biop)
{
	*biop = NULL;
	int error;

#if defined(HAVE_BLKDEV_ISSUE_SECURE_ERASE)
	error = blkdev_issue_secure_erase(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS);
#elif defined(HAVE_BLKDEV_ISSUE_DISCARD_ASYNC_FLAGS)
	error = __blkdev_issue_discard(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS, BLKDEV_DISCARD_SECURE, biop);
#elif defined(HAVE_BLKDEV_ISSUE_DISCARD_FLAGS)
	error = blkdev_issue_discard(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS, BLKDEV_DISCARD_SECURE);
#else
#error "unsupported kernel"
#endif

	return (error);
}

static int
vdev_bdev_issue_discard(zfs_bdev_handle_t *bdh, sector_t sector,
    sector_t nsect, struct bio **biop)
{
	*biop = NULL;
	int error;

#if defined(HAVE_BLKDEV_ISSUE_DISCARD_ASYNC_FLAGS)
	error = __blkdev_issue_discard(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS, 0, biop);
#elif defined(HAVE_BLKDEV_ISSUE_DISCARD_ASYNC_NOFLAGS)
	error = __blkdev_issue_discard(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS, biop);
#elif defined(HAVE_BLKDEV_ISSUE_DISCARD_FLAGS)
	error = blkdev_issue_discard(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS, 0);
#elif defined(HAVE_BLKDEV_ISSUE_DISCARD_NOFLAGS)
	error = blkdev_issue_discard(BDH_BDEV(bdh),
	    sector, nsect, GFP_NOFS);
#else
#error "unsupported kernel"
#endif

	return (error);
}

/*
 * Entry point for TRIM ops. This calls the right wrapper for secure erase or
 * discard, and then does the appropriate finishing work for error vs success
 * and async vs sync.
 */
static int
vdev_disk_io_trim(zio_t *zio)
{
	int error;
	struct bio *bio;

	zfs_bdev_handle_t *bdh = ((vdev_disk_t *)zio->io_vd->vdev_tsd)->vd_bdh;
	sector_t sector = zio->io_offset >> 9;
	sector_t nsects = zio->io_size >> 9;

	if (zio->io_trim_flags & ZIO_TRIM_SECURE)
		error = vdev_bdev_issue_secure_erase(bdh, sector, nsects, &bio);
	else
		error = vdev_bdev_issue_discard(bdh, sector, nsects, &bio);

	if (error != 0)
		return (SET_ERROR(-error));

	if (bio == NULL) {
		/*
		 * This was a synchronous op that completed successfully, so
		 * return it to ZFS immediately.
		 */
		zio_interrupt(zio);
	} else {
		/*
		 * This was an asynchronous op; set up completion callback and
		 * issue it.
		 */
		bio->bi_private = zio;
		bio->bi_end_io = vdev_disk_discard_end_io;
		vdev_submit_bio(bio);
	}

	return (0);
}

int (*vdev_disk_io_rw_fn)(zio_t *zio) = NULL;

static void
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	int error;

	/*
	 * If the vdev is closed, it's likely in the REMOVED or FAULTED state.
	 * Nothing to be done here but return failure.
	 */
	if (vd == NULL) {
		zio->io_error = ENXIO;
		zio_interrupt(zio);
		return;
	}

	rw_enter(&vd->vd_lock, RW_READER);

	/*
	 * If the vdev is closed, it's likely due to a failed reopen and is
	 * in the UNAVAIL state.  Nothing to be done here but return failure.
	 */
	if (vd->vd_bdh == NULL) {
		rw_exit(&vd->vd_lock);
		zio->io_error = ENXIO;
		zio_interrupt(zio);
		return;
	}

	switch (zio->io_type) {
	case ZIO_TYPE_IOCTL:

		if (!vdev_readable(v)) {
			rw_exit(&vd->vd_lock);
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (v->vdev_nowritecache) {
				zio->io_error = SET_ERROR(ENOTSUP);
				break;
			}

			error = vdev_disk_io_flush(BDH_BDEV(vd->vd_bdh), zio);
			if (error == 0) {
				rw_exit(&vd->vd_lock);
				return;
			}

			zio->io_error = error;

			break;

		default:
			zio->io_error = SET_ERROR(ENOTSUP);
		}

		rw_exit(&vd->vd_lock);
		zio_execute(zio);
		return;

	case ZIO_TYPE_TRIM:
		error = vdev_disk_io_trim(zio);
		rw_exit(&vd->vd_lock);
		if (error) {
			zio->io_error = error;
			zio_execute(zio);
		}
		return;

	case ZIO_TYPE_READ:
	case ZIO_TYPE_WRITE:
		zio->io_target_timestamp = zio_handle_io_delay(zio);
		error = vdev_disk_io_rw_fn(zio);
		rw_exit(&vd->vd_lock);
		if (error) {
			zio->io_error = error;
			zio_interrupt(zio);
		}
		return;

	default:
		/*
		 * Getting here means our parent vdev has made a very strange
		 * request of us, and shouldn't happen. Assert here to force a
		 * crash in dev builds, but in production return the IO
		 * unhandled. The pool will likely suspend anyway but that's
		 * nicer than crashing the kernel.
		 */
		ASSERT3S(zio->io_type, ==, -1);

		rw_exit(&vd->vd_lock);
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_interrupt(zio);
		return;
	}

	__builtin_unreachable();
}

static void
vdev_disk_io_done(zio_t *zio)
{
	/*
	 * If the device returned EIO, we revalidate the media.  If it is
	 * determined the media has changed this triggers the asynchronous
	 * removal of the device from the configuration.
	 */
	if (zio->io_error == EIO) {
		vdev_t *v = zio->io_vd;
		vdev_disk_t *vd = v->vdev_tsd;

		if (!zfs_check_disk_status(BDH_BDEV(vd->vd_bdh))) {
			invalidate_bdev(BDH_BDEV(vd->vd_bdh));
			v->vdev_remove_wanted = B_TRUE;
			spa_async_request(zio->io_spa, SPA_ASYNC_REMOVE);
		}
	}
}

static void
vdev_disk_hold(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* We must have a pathname, and it must be absolute. */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/')
		return;

	/*
	 * Only prefetch path and devid info if the device has
	 * never been opened.
	 */
	if (vd->vdev_tsd != NULL)
		return;

}

static void
vdev_disk_rele(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* XXX: Implement me as a vnode rele for the device */
}

/*
 * BIO submission method. See comment above about vdev_classic.
 * Set zfs_vdev_disk_classic=0 for new, =1 for classic
 */
static uint_t zfs_vdev_disk_classic = 1;	/* default classic */

/* Set submission function from module parameter */
static int
vdev_disk_param_set_classic(const char *buf, zfs_kernel_param_t *kp)
{
	int err = param_set_uint(buf, kp);
	if (err < 0)
		return (SET_ERROR(err));

	vdev_disk_io_rw_fn =
	    zfs_vdev_disk_classic ? vdev_classic_physio : vdev_disk_io_rw;

	printk(KERN_INFO "ZFS: forcing %s BIO submission\n",
	    zfs_vdev_disk_classic ? "classic" : "new");

	return (0);
}

/*
 * At first use vdev use, set the submission function from the default value if
 * it hasn't been set already.
 */
static int
vdev_disk_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	(void) spa;
	(void) nv;
	(void) tsd;

	if (vdev_disk_io_rw_fn == NULL)
		vdev_disk_io_rw_fn = zfs_vdev_disk_classic ?
		    vdev_classic_physio : vdev_disk_io_rw;

	return (0);
}

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = vdev_disk_init,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_disk_open,
	.vdev_op_close = vdev_disk_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_disk_io_start,
	.vdev_op_io_done = vdev_disk_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_disk_hold,
	.vdev_op_rele = vdev_disk_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_DISK,		/* name of this vdev type */
	.vdev_op_leaf = B_TRUE,			/* leaf vdev */
	.vdev_op_kobj_evt_post = vdev_disk_kobj_evt_post
};

/*
 * The zfs_vdev_scheduler module option has been deprecated. Setting this
 * value no longer has any effect.  It has not yet been entirely removed
 * to allow the module to be loaded if this option is specified in the
 * /etc/modprobe.d/zfs.conf file.  The following warning will be logged.
 */
static int
param_set_vdev_scheduler(const char *val, zfs_kernel_param_t *kp)
{
	int error = param_set_charp(val, kp);
	if (error == 0) {
		printk(KERN_INFO "The 'zfs_vdev_scheduler' module option "
		    "is not supported.\n");
	}

	return (error);
}

static const char *zfs_vdev_scheduler = "unused";
module_param_call(zfs_vdev_scheduler, param_set_vdev_scheduler,
    param_get_charp, &zfs_vdev_scheduler, 0644);
MODULE_PARM_DESC(zfs_vdev_scheduler, "I/O scheduler");

int
param_set_min_auto_ashift(const char *buf, zfs_kernel_param_t *kp)
{
	uint_t val;
	int error;

	error = kstrtouint(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val < ASHIFT_MIN || val > zfs_vdev_max_auto_ashift)
		return (SET_ERROR(-EINVAL));

	error = param_set_uint(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

int
param_set_max_auto_ashift(const char *buf, zfs_kernel_param_t *kp)
{
	uint_t val;
	int error;

	error = kstrtouint(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val > ASHIFT_MAX || val < zfs_vdev_min_auto_ashift)
		return (SET_ERROR(-EINVAL));

	error = param_set_uint(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, open_timeout_ms, UINT, ZMOD_RW,
	"Timeout before determining that a device is missing");

ZFS_MODULE_PARAM(zfs_vdev, zfs_vdev_, failfast_mask, UINT, ZMOD_RW,
	"Defines failfast mask: 1 - device, 2 - transport, 4 - driver");

ZFS_MODULE_PARAM(zfs_vdev_disk, zfs_vdev_disk_, max_segs, UINT, ZMOD_RW,
	"Maximum number of data segments to add to an IO request (min 4)");

ZFS_MODULE_PARAM_CALL(zfs_vdev_disk, zfs_vdev_disk_, classic,
    vdev_disk_param_set_classic, param_get_uint, ZMOD_RD,
	"Use classic BIO submission method");
