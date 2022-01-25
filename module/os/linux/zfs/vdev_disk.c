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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Rewritten for Linux by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 * Copyright (c) 2012, 2019 by Delphix. All rights reserved.
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

typedef struct vdev_disk {
	struct block_device		*vd_bdev;
	krwlock_t			vd_lock;
} vdev_disk_t;

/*
 * Unique identifier for the exclusive vdev holder.
 */
static void *zfs_vdev_holder = VDEV_HOLDER;

/*
 * Wait up to zfs_vdev_open_timeout_ms milliseconds before determining the
 * device is missing. The missing path may be transient since the links
 * can be briefly removed and recreated in response to udev events.
 */
static unsigned zfs_vdev_open_timeout_ms = 1000;

/*
 * Size of the "reserved" partition, in blocks.
 */
#define	EFI_MIN_RESV_SIZE	(16 * 1024)

/*
 * Virtual device vector for disks.
 */
typedef struct dio_request {
	zio_t			*dr_zio;	/* Parent ZIO */
	atomic_t		dr_ref;		/* References */
	int			dr_error;	/* Bio error */
	int			dr_bio_count;	/* Count of bio's */
	struct bio		*dr_bio[0];	/* Attached bio's */
} dio_request_t;

static fmode_t
vdev_bdev_mode(spa_mode_t spa_mode)
{
	fmode_t mode = 0;

	if (spa_mode & SPA_MODE_READ)
		mode |= FMODE_READ;

	if (spa_mode & SPA_MODE_WRITE)
		mode |= FMODE_WRITE;

	return (mode);
}

/*
 * Returns the usable capacity (in bytes) for the partition or disk.
 */
static uint64_t
bdev_capacity(struct block_device *bdev)
{
	return (i_size_read(bdev->bd_inode));
}

#if !defined(HAVE_BDEV_WHOLE)
static inline struct block_device *
bdev_whole(struct block_device *bdev)
{
	return (bdev->bd_contains);
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
		available = i_size_read(bdev_whole(bdev)->bd_inode) -
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
	    "offset=%llu size=%llu flags=%x\n", spa_name(zio->io_spa),
	    zio->io_vd->vdev_path, zio->io_error, zio->io_type,
	    (u_longlong_t)zio->io_offset, (u_longlong_t)zio->io_size,
	    zio->io_flags);
}

static int
vdev_disk_open(vdev_t *v, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	struct block_device *bdev;
	fmode_t mode = vdev_bdev_mode(spa_mode(v->vdev_spa));
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
		bdev = vd->vd_bdev;
		vd->vd_bdev = NULL;

		if (bdev) {
			if (v->vdev_expanding && bdev != bdev_whole(bdev)) {
				bdevname(bdev_whole(bdev), disk_name + 5);
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

			blkdev_put(bdev, mode | FMODE_EXCL);
		}

		if (reread_part) {
			bdev = blkdev_get_by_path(disk_name, mode | FMODE_EXCL,
			    zfs_vdev_holder);
			if (!IS_ERR(bdev)) {
				int error = vdev_bdev_reread_part(bdev);
				blkdev_put(bdev, mode | FMODE_EXCL);
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
	bdev = ERR_PTR(-ENXIO);
	while (IS_ERR(bdev) && ((gethrtime() - start) < timeout)) {
		bdev = blkdev_get_by_path(v->vdev_path, mode | FMODE_EXCL,
		    zfs_vdev_holder);
		if (unlikely(PTR_ERR(bdev) == -ENOENT)) {
			schedule_timeout(MSEC_TO_TICK(10));
		} else if (unlikely(PTR_ERR(bdev) == -ERESTARTSYS)) {
			timeout = MSEC2NSEC(zfs_vdev_open_timeout_ms * 10);
			continue;
		} else if (IS_ERR(bdev)) {
			break;
		}
	}

	if (IS_ERR(bdev)) {
		int error = -PTR_ERR(bdev);
		vdev_dbgmsg(v, "open error=%d timeout=%llu/%llu", error,
		    (u_longlong_t)(gethrtime() - start),
		    (u_longlong_t)timeout);
		vd->vd_bdev = NULL;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
		return (SET_ERROR(error));
	} else {
		vd->vd_bdev = bdev;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
	}

	struct request_queue *q = bdev_get_queue(vd->vd_bdev);

	/*  Determine the physical block size */
	int physical_block_size = bdev_physical_block_size(vd->vd_bdev);

	/*  Determine the logical block size */
	int logical_block_size = bdev_logical_block_size(vd->vd_bdev);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	v->vdev_nowritecache = B_FALSE;

	/* Set when device reports it supports TRIM. */
	v->vdev_has_trim = !!blk_queue_discard(q);

	/* Set when device reports it supports secure TRIM. */
	v->vdev_has_securetrim = !!blk_queue_discard_secure(q);

	/* Inform the ZIO pipeline that we are non-rotational */
	v->vdev_nonrot = blk_queue_nonrot(q);

	/* Physical volume size in bytes for the partition */
	*psize = bdev_capacity(vd->vd_bdev);

	/* Physical volume size in bytes including possible expansion space */
	*max_psize = bdev_max_capacity(vd->vd_bdev, v->vdev_wholedisk);

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

	if (vd->vd_bdev != NULL) {
		blkdev_put(vd->vd_bdev,
		    vdev_bdev_mode(spa_mode(v->vdev_spa)) | FMODE_EXCL);
	}

	rw_destroy(&vd->vd_lock);
	kmem_free(vd, sizeof (vdev_disk_t));
	v->vdev_tsd = NULL;
}

static dio_request_t *
vdev_disk_dio_alloc(int bio_count)
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
vdev_disk_dio_free(dio_request_t *dr)
{
	int i;

	for (i = 0; i < dr->dr_bio_count; i++)
		if (dr->dr_bio[i])
			bio_put(dr->dr_bio[i]);

	kmem_free(dr, sizeof (dio_request_t) +
	    sizeof (struct bio *) * dr->dr_bio_count);
}

static void
vdev_disk_dio_get(dio_request_t *dr)
{
	atomic_inc(&dr->dr_ref);
}

static int
vdev_disk_dio_put(dio_request_t *dr)
{
	int rc = atomic_dec_return(&dr->dr_ref);

	/*
	 * Free the dio_request when the last reference is dropped and
	 * ensure zio_interpret is called only once with the correct zio
	 */
	if (rc == 0) {
		zio_t *zio = dr->dr_zio;
		int error = dr->dr_error;

		vdev_disk_dio_free(dr);

		if (zio) {
			zio->io_error = error;
			ASSERT3S(zio->io_error, >=, 0);
			if (zio->io_error)
				vdev_disk_error(zio);

			zio_delay_interrupt(zio);
		}
	}

	return (rc);
}

BIO_END_IO_PROTO(vdev_disk_physio_completion, bio, error)
{
	dio_request_t *dr = bio->bi_private;
	int rc;

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

	/* Drop reference acquired by __vdev_disk_physio */
	rc = vdev_disk_dio_put(dr);
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

#ifdef HAVE_BIO_SET_DEV
#if defined(CONFIG_BLK_CGROUP) && defined(HAVE_BIO_SET_DEV_GPL_ONLY)
/*
 * The Linux 5.5 kernel updated percpu_ref_tryget() which is inlined by
 * blkg_tryget() to use rcu_read_lock() instead of rcu_read_lock_sched().
 * As a side effect the function was converted to GPL-only.  Define our
 * own version when needed which uses rcu_read_lock_sched().
 */
#if defined(HAVE_BLKG_TRYGET_GPL_ONLY)
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
#elif defined(HAVE_BLKG_TRYGET)
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

static inline void
vdev_submit_bio(struct bio *bio)
{
	struct bio_list *bio_list = current->bio_list;
	current->bio_list = NULL;
	vdev_submit_bio_impl(bio);
	current->bio_list = bio_list;
}

static int
__vdev_disk_physio(struct block_device *bdev, zio_t *zio,
    size_t io_size, uint64_t io_offset, int rw, int flags)
{
	dio_request_t *dr;
	uint64_t abd_offset;
	uint64_t bio_offset;
	int bio_size;
	int bio_count = 16;
	int error = 0;
	struct blk_plug plug;

	/*
	 * Accessing outside the block device is never allowed.
	 */
	if (io_offset + io_size > bdev->bd_inode->i_size) {
		vdev_dbgmsg(zio->io_vd,
		    "Illegal access %llu size %llu, device size %llu",
		    (u_longlong_t)io_offset,
		    (u_longlong_t)io_size,
		    (u_longlong_t)i_size_read(bdev->bd_inode));
		return (SET_ERROR(EIO));
	}

retry:
	dr = vdev_disk_dio_alloc(bio_count);

	if (zio && !(zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)))
		bio_set_flags_failfast(bdev, &flags);

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
			vdev_disk_dio_free(dr);
			bio_count *= 2;
			goto retry;
		}

		/* bio_alloc() with __GFP_WAIT never returns NULL */
#ifdef HAVE_BIO_MAX_SEGS
		dr->dr_bio[i] = bio_alloc(GFP_NOIO, bio_max_segs(
		    abd_nr_pages_off(zio->io_abd, bio_size, abd_offset)));
#else
		dr->dr_bio[i] = bio_alloc(GFP_NOIO,
		    MIN(abd_nr_pages_off(zio->io_abd, bio_size, abd_offset),
		    BIO_MAX_PAGES));
#endif
		if (unlikely(dr->dr_bio[i] == NULL)) {
			vdev_disk_dio_free(dr);
			return (SET_ERROR(ENOMEM));
		}

		/* Matching put called by vdev_disk_physio_completion */
		vdev_disk_dio_get(dr);

		bio_set_dev(dr->dr_bio[i], bdev);
		BIO_BI_SECTOR(dr->dr_bio[i]) = bio_offset >> 9;
		dr->dr_bio[i]->bi_end_io = vdev_disk_physio_completion;
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
	vdev_disk_dio_get(dr);

	if (dr->dr_bio_count > 1)
		blk_start_plug(&plug);

	/* Submit all bio's associated with this dio */
	for (int i = 0; i < dr->dr_bio_count; i++) {
		if (dr->dr_bio[i])
			vdev_submit_bio(dr->dr_bio[i]);
	}

	if (dr->dr_bio_count > 1)
		blk_finish_plug(&plug);

	(void) vdev_disk_dio_put(dr);

	return (error);
}

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

	bio = bio_alloc(GFP_NOIO, 0);
	/* bio_alloc() with __GFP_WAIT never returns NULL */
	if (unlikely(bio == NULL))
		return (SET_ERROR(ENOMEM));

	bio->bi_end_io = vdev_disk_io_flush_completion;
	bio->bi_private = zio;
	bio_set_dev(bio, bdev);
	bio_set_flush(bio);
	vdev_submit_bio(bio);
	invalidate_bdev(bdev);

	return (0);
}

static void
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	unsigned long trim_flags = 0;
	int rw, error;

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
	if (vd->vd_bdev == NULL) {
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

			error = vdev_disk_io_flush(vd->vd_bdev, zio);
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
	case ZIO_TYPE_WRITE:
		rw = WRITE;
		break;

	case ZIO_TYPE_READ:
		rw = READ;
		break;

	case ZIO_TYPE_TRIM:
#if defined(BLKDEV_DISCARD_SECURE)
		if (zio->io_trim_flags & ZIO_TRIM_SECURE)
			trim_flags |= BLKDEV_DISCARD_SECURE;
#endif
		zio->io_error = -blkdev_issue_discard(vd->vd_bdev,
		    zio->io_offset >> 9, zio->io_size >> 9, GFP_NOFS,
		    trim_flags);

		rw_exit(&vd->vd_lock);
		zio_interrupt(zio);
		return;

	default:
		rw_exit(&vd->vd_lock);
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_interrupt(zio);
		return;
	}

	zio->io_target_timestamp = zio_handle_io_delay(zio);
	error = __vdev_disk_physio(vd->vd_bdev, zio,
	    zio->io_size, zio->io_offset, rw, 0);
	rw_exit(&vd->vd_lock);

	if (error) {
		zio->io_error = error;
		zio_interrupt(zio);
		return;
	}
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

		if (zfs_check_media_change(vd->vd_bdev)) {
			invalidate_bdev(vd->vd_bdev);
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

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
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
	.vdev_op_leaf = B_TRUE			/* leaf vdev */
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
	uint64_t val;
	int error;

	error = kstrtoull(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val < ASHIFT_MIN || val > zfs_vdev_max_auto_ashift)
		return (SET_ERROR(-EINVAL));

	error = param_set_ulong(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

int
param_set_max_auto_ashift(const char *buf, zfs_kernel_param_t *kp)
{
	uint64_t val;
	int error;

	error = kstrtoull(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val > ASHIFT_MAX || val < zfs_vdev_min_auto_ashift)
		return (SET_ERROR(-EINVAL));

	error = param_set_ulong(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}
