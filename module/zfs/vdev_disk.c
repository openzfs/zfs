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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <linux/mod_compat.h>
#include <linux/msdos_fs.h>
#include <linux/vfs_compat.h>

char *zfs_vdev_scheduler = VDEV_SCHEDULER;
static void *zfs_vdev_holder = VDEV_HOLDER;

/* size of the "reserved" partition, in blocks */
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


#ifdef HAVE_OPEN_BDEV_EXCLUSIVE
static fmode_t
vdev_bdev_mode(int smode)
{
	fmode_t mode = 0;

	ASSERT3S(smode & (FREAD | FWRITE), !=, 0);

	if (smode & FREAD)
		mode |= FMODE_READ;

	if (smode & FWRITE)
		mode |= FMODE_WRITE;

	return (mode);
}
#else
static int
vdev_bdev_mode(int smode)
{
	int mode = 0;

	ASSERT3S(smode & (FREAD | FWRITE), !=, 0);

	if ((smode & FREAD) && !(smode & FWRITE))
		mode = SB_RDONLY;

	return (mode);
}
#endif /* HAVE_OPEN_BDEV_EXCLUSIVE */

/*
 * Returns the usable capacity (in bytes) for the partition or disk.
 */
static uint64_t
bdev_capacity(struct block_device *bdev)
{
	return (i_size_read(bdev->bd_inode));
}

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
 */
static uint64_t
bdev_max_capacity(struct block_device *bdev, uint64_t wholedisk)
{
	uint64_t psize;
	int64_t available;

	if (wholedisk && bdev->bd_part != NULL && bdev != bdev->bd_contains) {
		/*
		 * When reporting maximum expansion capacity for a wholedisk
		 * deduct any capacity which is expected to be lost due to
		 * alignment restrictions.  Over reporting this value isn't
		 * harmful and would only result in slightly less capacity
		 * than expected post expansion.
		 */
		available = i_size_read(bdev->bd_contains->bd_inode) -
		    ((EFI_MIN_RESV_SIZE + NEW_START_BLOCK +
		    PARTITION_END_ALIGNMENT) << SECTOR_BITS);
		if (available > 0)
			psize = available;
		else
			psize = bdev_capacity(bdev);
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

/*
 * Use the Linux 'noop' elevator for zfs managed block devices.  This
 * strikes the ideal balance by allowing the zfs elevator to do all
 * request ordering and prioritization.  While allowing the Linux
 * elevator to do the maximum front/back merging allowed by the
 * physical device.  This yields the largest possible requests for
 * the device with the lowest total overhead.
 */
static void
vdev_elevator_switch(vdev_t *v, char *elevator)
{
	vdev_disk_t *vd = v->vdev_tsd;
	struct request_queue *q;
	char *device;
	int error;

	for (int c = 0; c < v->vdev_children; c++)
		vdev_elevator_switch(v->vdev_child[c], elevator);

	if (!v->vdev_ops->vdev_op_leaf || vd->vd_bdev == NULL)
		return;

	q = bdev_get_queue(vd->vd_bdev);
	device = vd->vd_bdev->bd_disk->disk_name;

	/*
	 * Skip devices which are not whole disks (partitions).
	 * Device-mapper devices are excepted since they may be whole
	 * disks despite the vdev_wholedisk flag, in which case we can
	 * and should switch the elevator. If the device-mapper device
	 * does not have an elevator (i.e. dm-raid, dm-crypt, etc.) the
	 * "Skip devices without schedulers" check below will fail.
	 */
	if (!v->vdev_wholedisk && strncmp(device, "dm-", 3) != 0)
		return;

	/* Leave existing scheduler when set to "none" */
	if ((strncmp(elevator, "none", 4) == 0) && (strlen(elevator) == 4))
		return;

	/*
	 * The elevator_change() function was available in kernels from
	 * 2.6.36 to 4.11.  When not available fall back to using the user
	 * mode helper functionality to set the elevator via sysfs.  This
	 * requires /bin/echo and sysfs to be mounted which may not be true
	 * early in the boot process.
	 */
#ifdef HAVE_ELEVATOR_CHANGE
	error = elevator_change(q, elevator);
#else
#define	SET_SCHEDULER_CMD \
	"exec 0</dev/null " \
	"     1>/sys/block/%s/queue/scheduler " \
	"     2>/dev/null; " \
	"echo %s"

	char *argv[] = { "/bin/sh", "-c", NULL, NULL };
	char *envp[] = { NULL };

	argv[2] = kmem_asprintf(SET_SCHEDULER_CMD, device, elevator);
	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	strfree(argv[2]);
#endif /* HAVE_ELEVATOR_CHANGE */
	if (error) {
		zfs_dbgmsg("Unable to set \"%s\" scheduler for %s (%s): %d\n",
		    elevator, v->vdev_path, device, error);
	}
}

static int
vdev_disk_open(vdev_t *v, uint64_t *psize, uint64_t *max_psize,
    uint64_t *ashift)
{
	struct block_device *bdev;
	fmode_t mode = vdev_bdev_mode(spa_mode(v->vdev_spa));
	int count = 0, block_size;
	int bdev_retry_count = 50;
	vdev_disk_t *vd;

	/* Must have a pathname and it must be absolute. */
	if (v->vdev_path == NULL || v->vdev_path[0] != '/') {
		v->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		vdev_dbgmsg(v, "invalid vdev_path");
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Reopen the device if it is currently open.  When expanding a
	 * partition force re-scanning the partition table while closed
	 * in order to get an accurate updated block device size.  Then
	 * since udev may need to recreate the device links increase the
	 * open retry count before reporting the device as unavailable.
	 */
	vd = v->vdev_tsd;
	if (vd) {
		char disk_name[BDEVNAME_SIZE + 6] = "/dev/";
		boolean_t reread_part = B_FALSE;

		rw_enter(&vd->vd_lock, RW_WRITER);
		bdev = vd->vd_bdev;
		vd->vd_bdev = NULL;

		if (bdev) {
			if (v->vdev_expanding && bdev != bdev->bd_contains) {
				bdevname(bdev->bd_contains, disk_name + 5);
				reread_part = B_TRUE;
			}

			vdev_bdev_close(bdev, mode);
		}

		if (reread_part) {
			bdev = vdev_bdev_open(disk_name, mode, zfs_vdev_holder);
			if (!IS_ERR(bdev)) {
				int error = vdev_bdev_reread_part(bdev);
				vdev_bdev_close(bdev, mode);
				if (error == 0)
					bdev_retry_count = 100;
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
	 */
	bdev = ERR_PTR(-ENXIO);
	while (IS_ERR(bdev) && count < bdev_retry_count) {
		bdev = vdev_bdev_open(v->vdev_path, mode, zfs_vdev_holder);
		if (unlikely(PTR_ERR(bdev) == -ENOENT)) {
			schedule_timeout(MSEC_TO_TICK(10));
			count++;
		} else if (IS_ERR(bdev)) {
			break;
		}
	}

	if (IS_ERR(bdev)) {
		int error = -PTR_ERR(bdev);
		vdev_dbgmsg(v, "open error=%d count=%d\n", error, count);
		vd->vd_bdev = NULL;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
		return (SET_ERROR(error));
	} else {
		vd->vd_bdev = bdev;
		v->vdev_tsd = vd;
		rw_exit(&vd->vd_lock);
	}

	/*  Determine the physical block size */
	block_size = vdev_bdev_block_size(vd->vd_bdev);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	v->vdev_nowritecache = B_FALSE;

	/* Inform the ZIO pipeline that we are non-rotational */
	v->vdev_nonrot = blk_queue_nonrot(bdev_get_queue(vd->vd_bdev));

	/* Physical volume size in bytes for the partition */
	*psize = bdev_capacity(vd->vd_bdev);

	/* Physical volume size in bytes including possible expansion space */
	*max_psize = bdev_max_capacity(vd->vd_bdev, v->vdev_wholedisk);

	/* Based on the minimum sector size set the block size */
	*ashift = highbit64(MAX(block_size, SPA_MINBLOCKSIZE)) - 1;

	/* Try to set the io scheduler elevator algorithm */
	(void) vdev_elevator_switch(v, zfs_vdev_scheduler);

	return (0);
}

static void
vdev_disk_close(vdev_t *v)
{
	vdev_disk_t *vd = v->vdev_tsd;

	if (v->vdev_reopening || vd == NULL)
		return;

	if (vd->vd_bdev != NULL) {
		vdev_bdev_close(vd->vd_bdev,
		    vdev_bdev_mode(spa_mode(v->vdev_spa)));
	}

	rw_destroy(&vd->vd_lock);
	kmem_free(vd, sizeof (vdev_disk_t));
	v->vdev_tsd = NULL;
}

static dio_request_t *
vdev_disk_dio_alloc(int bio_count)
{
	dio_request_t *dr;
	int i;

	dr = kmem_zalloc(sizeof (dio_request_t) +
	    sizeof (struct bio *) * bio_count, KM_SLEEP);
	if (dr) {
		atomic_set(&dr->dr_ref, 0);
		dr->dr_bio_count = bio_count;
		dr->dr_error = 0;

		for (i = 0; i < dr->dr_bio_count; i++)
			dr->dr_bio[i] = NULL;
	}

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

static unsigned int
bio_map(struct bio *bio, void *bio_ptr, unsigned int bio_size)
{
	unsigned int offset, size, i;
	struct page *page;

	offset = offset_in_page(bio_ptr);
	for (i = 0; i < bio->bi_max_vecs; i++) {
		size = PAGE_SIZE - offset;

		if (bio_size <= 0)
			break;

		if (size > bio_size)
			size = bio_size;

		if (is_vmalloc_addr(bio_ptr))
			page = vmalloc_to_page(bio_ptr);
		else
			page = virt_to_page(bio_ptr);

		/*
		 * Some network related block device uses tcp_sendpage, which
		 * doesn't behave well when using 0-count page, this is a
		 * safety net to catch them.
		 */
		ASSERT3S(page_count(page), >, 0);

		if (bio_add_page(bio, page, size, offset) != size)
			break;

		bio_ptr  += size;
		bio_size -= size;
		offset = 0;
	}

	return (bio_size);
}

static unsigned int
bio_map_abd_off(struct bio *bio, abd_t *abd, unsigned int size, size_t off)
{
	if (abd_is_linear(abd))
		return (bio_map(bio, ((char *)abd_to_buf(abd)) + off, size));

	return (abd_scatter_bio_map_off(bio, abd, size, off));
}

static inline void
vdev_submit_bio_impl(struct bio *bio)
{
#ifdef HAVE_1ARG_SUBMIT_BIO
	submit_bio(bio);
#else
	submit_bio(0, bio);
#endif
}

#ifdef HAVE_BIO_SET_DEV
#if defined(CONFIG_BLK_CGROUP) && defined(HAVE_BIO_SET_DEV_GPL_ONLY)
/*
 * The Linux 5.0 kernel updated the bio_set_dev() macro so it calls the
 * GPL-only bio_associate_blkg() symbol thus inadvertently converting
 * the entire macro.  Provide a minimal version which always assigns the
 * request queue's root_blkg to the bio.
 */
static inline void
vdev_bio_associate_blkg(struct bio *bio)
{
	struct request_queue *q = bio->bi_disk->queue;

	ASSERT3P(q, !=, NULL);
	ASSERT3P(q->root_blkg, !=, NULL);
	ASSERT3P(bio->bi_blkg, ==, NULL);

	if (blkg_tryget(q->root_blkg))
		bio->bi_blkg = q->root_blkg;
}
#define	bio_associate_blkg vdev_bio_associate_blkg
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
#ifdef HAVE_CURRENT_BIO_TAIL
	struct bio **bio_tail = current->bio_tail;
	current->bio_tail = NULL;
	vdev_submit_bio_impl(bio);
	current->bio_tail = bio_tail;
#else
	struct bio_list *bio_list = current->bio_list;
	current->bio_list = NULL;
	vdev_submit_bio_impl(bio);
	current->bio_list = bio_list;
#endif
}

static int
__vdev_disk_physio(struct block_device *bdev, zio_t *zio,
    size_t io_size, uint64_t io_offset, int rw, int flags)
{
	dio_request_t *dr;
	uint64_t abd_offset;
	uint64_t bio_offset;
	int bio_size, bio_count = 16;
	int i = 0, error = 0;
#if defined(HAVE_BLK_QUEUE_HAVE_BLK_PLUG)
	struct blk_plug plug;
#endif
	/*
	 * Accessing outside the block device is never allowed.
	 */
	if (io_offset + io_size > bdev->bd_inode->i_size) {
		vdev_dbgmsg(zio->io_vd,
		    "Illegal access %llu size %llu, device size %llu",
		    io_offset, io_size, i_size_read(bdev->bd_inode));
		return (SET_ERROR(EIO));
	}

retry:
	dr = vdev_disk_dio_alloc(bio_count);
	if (dr == NULL)
		return (SET_ERROR(ENOMEM));

	if (zio && !(zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)))
		bio_set_flags_failfast(bdev, &flags);

	dr->dr_zio = zio;

	/*
	 * When the IO size exceeds the maximum bio size for the request
	 * queue we are forced to break the IO in multiple bio's and wait
	 * for them all to complete.  Ideally, all pool users will set
	 * their volume block size to match the maximum request size and
	 * the common case will be one bio per vdev IO request.
	 */

	abd_offset = 0;
	bio_offset = io_offset;
	bio_size   = io_size;
	for (i = 0; i <= dr->dr_bio_count; i++) {

		/* Finished constructing bio's for given buffer */
		if (bio_size <= 0)
			break;

		/*
		 * By default only 'bio_count' bio's per dio are allowed.
		 * However, if we find ourselves in a situation where more
		 * are needed we allocate a larger dio and warn the user.
		 */
		if (dr->dr_bio_count == i) {
			vdev_disk_dio_free(dr);
			bio_count *= 2;
			goto retry;
		}

		/* bio_alloc() with __GFP_WAIT never returns NULL */
		dr->dr_bio[i] = bio_alloc(GFP_NOIO,
		    MIN(abd_nr_pages_off(zio->io_abd, bio_size, abd_offset),
		    BIO_MAX_PAGES));
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
		bio_size = bio_map_abd_off(dr->dr_bio[i], zio->io_abd,
		    bio_size, abd_offset);

		/* Advance in buffer and construct another bio if needed */
		abd_offset += BIO_BI_SIZE(dr->dr_bio[i]);
		bio_offset += BIO_BI_SIZE(dr->dr_bio[i]);
	}

	/* Extra reference to protect dio_request during vdev_submit_bio */
	vdev_disk_dio_get(dr);

#if defined(HAVE_BLK_QUEUE_HAVE_BLK_PLUG)
	if (dr->dr_bio_count > 1)
		blk_start_plug(&plug);
#endif

	/* Submit all bio's associated with this dio */
	for (i = 0; i < dr->dr_bio_count; i++)
		if (dr->dr_bio[i])
			vdev_submit_bio(dr->dr_bio[i]);

#if defined(HAVE_BLK_QUEUE_HAVE_BLK_PLUG)
	if (dr->dr_bio_count > 1)
		blk_finish_plug(&plug);
#endif

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
	int rw, flags, error;

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
#if defined(HAVE_BLK_QUEUE_HAVE_BIO_RW_UNPLUG)
		flags = (1 << BIO_RW_UNPLUG);
#elif defined(REQ_UNPLUG)
		flags = REQ_UNPLUG;
#else
		flags = 0;
#endif
		break;

	case ZIO_TYPE_READ:
		rw = READ;
#if defined(HAVE_BLK_QUEUE_HAVE_BIO_RW_UNPLUG)
		flags = (1 << BIO_RW_UNPLUG);
#elif defined(REQ_UNPLUG)
		flags = REQ_UNPLUG;
#else
		flags = 0;
#endif
		break;

	default:
		rw_exit(&vd->vd_lock);
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_interrupt(zio);
		return;
	}

	zio->io_target_timestamp = zio_handle_io_delay(zio);
	error = __vdev_disk_physio(vd->vd_bdev, zio,
	    zio->io_size, zio->io_offset, rw, flags);
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

		if (check_disk_change(vd->vd_bdev)) {
			vdev_bdev_invalidate(vd->vd_bdev);
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

	/* XXX: Implement me as a vnode lookup for the device */
	vd->vdev_name_vp = NULL;
	vd->vdev_devid_vp = NULL;
}

static void
vdev_disk_rele(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* XXX: Implement me as a vnode rele for the device */
}

static int
param_set_vdev_scheduler(const char *val, zfs_kernel_param_t *kp)
{
	spa_t *spa = NULL;
	char *p;

	if (val == NULL)
		return (SET_ERROR(-EINVAL));

	if ((p = strchr(val, '\n')) != NULL)
		*p = '\0';

	if (spa_mode_global != 0) {
		mutex_enter(&spa_namespace_lock);
		while ((spa = spa_next(spa)) != NULL) {
			if (spa_state(spa) != POOL_STATE_ACTIVE ||
			    !spa_writeable(spa) || spa_suspended(spa))
				continue;

			spa_open_ref(spa, FTAG);
			mutex_exit(&spa_namespace_lock);
			vdev_elevator_switch(spa->spa_root_vdev, (char *)val);
			mutex_enter(&spa_namespace_lock);
			spa_close(spa, FTAG);
		}
		mutex_exit(&spa_namespace_lock);
	}

	return (param_set_charp(val, kp));
}

vdev_ops_t vdev_disk_ops = {
	vdev_disk_open,
	vdev_disk_close,
	vdev_default_asize,
	vdev_disk_io_start,
	vdev_disk_io_done,
	NULL,
	NULL,
	vdev_disk_hold,
	vdev_disk_rele,
	NULL,
	vdev_default_xlate,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

module_param_call(zfs_vdev_scheduler, param_set_vdev_scheduler,
    param_get_charp, &zfs_vdev_scheduler, 0644);
MODULE_PARM_DESC(zfs_vdev_scheduler, "I/O scheduler");
