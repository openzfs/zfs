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
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/sunldi.h>

char *zfs_vdev_scheduler = VDEV_SCHEDULER;

/*
 * Virtual device vector for disks.
 */
typedef struct dio_request {
	struct completion	dr_comp;	/* Completion for sync IO */
	atomic_t		dr_ref;		/* References */
	zio_t			*dr_zio;	/* Parent ZIO */
	int			dr_rw;		/* Read/Write */
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

	return mode;
}
#else
static int
vdev_bdev_mode(int smode)
{
	int mode = 0;

	ASSERT3S(smode & (FREAD | FWRITE), !=, 0);

	if ((smode & FREAD) && !(smode & FWRITE))
		mode = MS_RDONLY;

	return mode;
}
#endif /* HAVE_OPEN_BDEV_EXCLUSIVE */

static uint64_t
bdev_capacity(struct block_device *bdev)
{
	struct hd_struct *part = bdev->bd_part;

	/* The partition capacity referenced by the block device */
	if (part)
		return (part->nr_sects << 9);

	/* Otherwise assume the full device capacity */
	return (get_capacity(bdev->bd_disk) << 9);
}

static void
vdev_disk_error(zio_t *zio)
{
#ifdef ZFS_DEBUG
	printk("ZFS: zio error=%d type=%d offset=%llu size=%llu "
	    "flags=%x delay=%llu\n", zio->io_error, zio->io_type,
	    (u_longlong_t)zio->io_offset, (u_longlong_t)zio->io_size,
	    zio->io_flags, (u_longlong_t)zio->io_delay);
#endif
}

/*
 * Use the Linux 'noop' elevator for zfs managed block devices.  This
 * strikes the ideal balance by allowing the zfs elevator to do all
 * request ordering and prioritization.  While allowing the Linux
 * elevator to do the maximum front/back merging allowed by the
 * physical device.  This yields the largest possible requests for
 * the device with the lowest total overhead.
 *
 * Unfortunately we cannot directly call the elevator_switch() function
 * because it is not exported from the block layer.  This means we have
 * to use the sysfs interface and a user space upcall.  Pools will be
 * automatically imported on module load so we must do this at device
 * open time from the kernel.
 */
#define SET_SCHEDULER_CMD \
	"exec 0</dev/null " \
	"     1>/sys/block/%s/queue/scheduler " \
	"     2>/dev/null; " \
	"echo %s"

static int
vdev_elevator_switch(vdev_t *v, char *elevator)
{
	vdev_disk_t *vd = v->vdev_tsd;
	struct block_device *bdev = vd->vd_bdev;
	struct request_queue *q = bdev_get_queue(bdev);
	char *device = bdev->bd_disk->disk_name;
	char *argv[] = { "/bin/sh", "-c", NULL, NULL };
	char *envp[] = { NULL };
	int error;

	/* Skip devices which are not whole disks (partitions) */
	if (!v->vdev_wholedisk)
		return (0);

	/* Skip devices without schedulers (loop, ram, dm, etc) */
	if (!q->elevator || !blk_queue_stackable(q))
		return (0);

	/* Leave existing scheduler when set to "none" */
	if (!strncmp(elevator, "none", 4) && (strlen(elevator) == 4))
		return (0);

	argv[2] = kmem_asprintf(SET_SCHEDULER_CMD, device, elevator);
	error = call_usermodehelper(argv[0], argv, envp, 1);
	if (error)
		printk("ZFS: Unable to set \"%s\" scheduler for %s (%s): %d\n",
		       elevator, v->vdev_path, device, error);

	strfree(argv[2]);

	return (error);
}

/*
 * Expanding a whole disk vdev involves invoking BLKRRPART on the
 * whole disk device. This poses a problem, because BLKRRPART will
 * return EBUSY if one of the disk's partitions is open. That's why
 * we have to do it here, just before opening the data partition.
 * Unfortunately, BLKRRPART works by dropping all partitions and
 * recreating them, which means that for a short time window, all
 * /dev/sdxN device files disappear (until udev recreates them).
 * This means two things:
 *  - When we open the data partition just after a BLKRRPART, we
 *    can't do it using the normal device file path because of the
 *    obvious race condition with udev. Instead, we use reliable
 *    kernel APIs to get a handle to the new partition device from
 *    the whole disk device.
 *  - Because vdev_disk_open() initially needs to find the device
 *    using its path, multiple vdev_disk_open() invocations in
 *    short succession on the same disk with BLKRRPARTs in the
 *    middle have a high probability of failure (because of the
 *    race condition with udev). A typical situation where this
 *    might happen is when the zpool userspace tool does a
 *    TRYIMPORT immediately followed by an IMPORT. For this
 *    reason, we only invoke BLKRRPART in the module when strictly
 *    necessary (zpool online -e case), and rely on userspace to
 *    do it when possible.
 */
static struct block_device *
vdev_disk_rrpart(const char *path, int mode, vdev_disk_t *vd)
{
#if defined(HAVE_3ARG_BLKDEV_GET) && defined(HAVE_GET_GENDISK)
	struct block_device *bdev, *result = ERR_PTR(-ENXIO);
	struct gendisk *disk;
	int error, partno;

	bdev = vdev_bdev_open(path, vdev_bdev_mode(mode), vd);
	if (IS_ERR(bdev))
		return bdev;

	disk = get_gendisk(bdev->bd_dev, &partno);
	vdev_bdev_close(bdev, vdev_bdev_mode(mode));

	if (disk) {
		bdev = bdget(disk_devt(disk));
		if (bdev) {
			error = blkdev_get(bdev, vdev_bdev_mode(mode), vd);
			if (error == 0)
				error = ioctl_by_bdev(bdev, BLKRRPART, 0);
			vdev_bdev_close(bdev, vdev_bdev_mode(mode));
		}

		bdev = bdget_disk(disk, partno);
		if (bdev) {
			error = blkdev_get(bdev,
			    vdev_bdev_mode(mode) | FMODE_EXCL, vd);
			if (error == 0)
				result = bdev;
		}
		put_disk(disk);
	}

	return result;
#else
	return ERR_PTR(-EOPNOTSUPP);
#endif /* defined(HAVE_3ARG_BLKDEV_GET) && defined(HAVE_GET_GENDISK) */
}

static int
vdev_disk_open(vdev_t *v, uint64_t *psize, uint64_t *ashift)
{
	struct block_device *bdev = ERR_PTR(-ENXIO);
	vdev_disk_t *vd;
	int mode, block_size;

	/* Must have a pathname and it must be absolute. */
	if (v->vdev_path == NULL || v->vdev_path[0] != '/') {
		v->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return EINVAL;
	}

	vd = kmem_zalloc(sizeof(vdev_disk_t), KM_PUSHPAGE);
	if (vd == NULL)
		return ENOMEM;

	/*
	 * Devices are always opened by the path provided at configuration
	 * time.  This means that if the provided path is a udev by-id path
	 * then drives may be recabled without an issue.  If the provided
	 * path is a udev by-path path then the physical location information
	 * will be preserved.  This can be critical for more complicated
	 * configurations where drives are located in specific physical
	 * locations to maximize the systems tolerence to component failure.
	 * Alternately you can provide your own udev rule to flexibly map
	 * the drives as you see fit.  It is not advised that you use the
	 * /dev/[hd]d devices which may be reorder due to probing order.
	 * Devices in the wrong locations will be detected by the higher
	 * level vdev validation.
	 */
	mode = spa_mode(v->vdev_spa);
	if (v->vdev_wholedisk && v->vdev_expanding)
		bdev = vdev_disk_rrpart(v->vdev_path, mode, vd);
	if (IS_ERR(bdev))
		bdev = vdev_bdev_open(v->vdev_path, vdev_bdev_mode(mode), vd);
	if (IS_ERR(bdev)) {
		kmem_free(vd, sizeof(vdev_disk_t));
		return -PTR_ERR(bdev);
	}

	v->vdev_tsd = vd;
	vd->vd_bdev = bdev;
	block_size =  vdev_bdev_block_size(bdev);

	/* We think the wholedisk property should always be set when this
	 * function is called.  ASSERT here so if any legitimate cases exist
	 * where it's not set, we'll find them during debugging.  If we never
	 * hit the ASSERT, this and the following conditional statement can be
	 * removed. */
	ASSERT3S(v->vdev_wholedisk, !=, -1ULL);

	/* The wholedisk property was initialized to -1 in vdev_alloc() if it
	 * was unspecified.  In that case, check if this is a whole device.
	 * When bdev->bd_contains == bdev we have a whole device and not simply
	 * a partition. */
	if (v->vdev_wholedisk == -1ULL)
		v->vdev_wholedisk = (bdev->bd_contains == bdev);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	v->vdev_nowritecache = B_FALSE;

	/* Physical volume size in bytes */
	*psize = bdev_capacity(bdev);

	/* Based on the minimum sector size set the block size */
	*ashift = highbit(MAX(block_size, SPA_MINBLOCKSIZE)) - 1;

	/* Try to set the io scheduler elevator algorithm */
	(void) vdev_elevator_switch(v, zfs_vdev_scheduler);

	return 0;
}

static void
vdev_disk_close(vdev_t *v)
{
	vdev_disk_t *vd = v->vdev_tsd;

	if (vd == NULL)
		return;

	if (vd->vd_bdev != NULL)
		vdev_bdev_close(vd->vd_bdev,
		                vdev_bdev_mode(spa_mode(v->vdev_spa)));

	kmem_free(vd, sizeof(vdev_disk_t));
	v->vdev_tsd = NULL;
}

static dio_request_t *
vdev_disk_dio_alloc(int bio_count)
{
	dio_request_t *dr;
	int i;

	dr = kmem_zalloc(sizeof(dio_request_t) +
	                 sizeof(struct bio *) * bio_count, KM_PUSHPAGE);
	if (dr) {
		init_completion(&dr->dr_comp);
		atomic_set(&dr->dr_ref, 0);
		dr->dr_bio_count = bio_count;
		dr->dr_error = 0;

		for (i = 0; i < dr->dr_bio_count; i++)
			dr->dr_bio[i] = NULL;
	}

	return dr;
}

static void
vdev_disk_dio_free(dio_request_t *dr)
{
	int i;

	for (i = 0; i < dr->dr_bio_count; i++)
		if (dr->dr_bio[i])
			bio_put(dr->dr_bio[i]);

	kmem_free(dr, sizeof(dio_request_t) +
	          sizeof(struct bio *) * dr->dr_bio_count);
}

static int
vdev_disk_dio_is_sync(dio_request_t *dr)
{
#ifdef HAVE_BIO_RW_SYNC
	/* BIO_RW_SYNC preferred interface from 2.6.12-2.6.29 */
        return (dr->dr_rw & (1 << BIO_RW_SYNC));
#else
# ifdef HAVE_BIO_RW_SYNCIO
	/* BIO_RW_SYNCIO preferred interface from 2.6.30-2.6.35 */
        return (dr->dr_rw & (1 << BIO_RW_SYNCIO));
# else
#  ifdef HAVE_REQ_SYNC
	/* REQ_SYNC preferred interface from 2.6.36-2.6.xx */
        return (dr->dr_rw & REQ_SYNC);
#  else
#   error "Unable to determine bio sync flag"
#  endif /* HAVE_REQ_SYNC */
# endif /* HAVE_BIO_RW_SYNC */
#endif /* HAVE_BIO_RW_SYNCIO */
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
			zio->io_delay = jiffies_to_msecs(
			    jiffies_64 - zio->io_delay);
			zio->io_error = error;
			ASSERT3S(zio->io_error, >=, 0);
			if (zio->io_error)
				vdev_disk_error(zio);
			zio_interrupt(zio);
		}
	}

	return rc;
}

BIO_END_IO_PROTO(vdev_disk_physio_completion, bio, size, error)
{
	dio_request_t *dr = bio->bi_private;
	int rc;

	/* Fatal error but print some useful debugging before asserting */
	if (dr == NULL)
		PANIC("dr == NULL, bio->bi_private == NULL\n"
		    "bi_next: %p, bi_flags: %lx, bi_rw: %lu, bi_vcnt: %d\n"
		    "bi_idx: %d, bi_size: %d, bi_end_io: %p, bi_cnt: %d\n",
		    bio->bi_next, bio->bi_flags, bio->bi_rw, bio->bi_vcnt,
		    bio->bi_idx, bio->bi_size, bio->bi_end_io,
		    atomic_read(&bio->bi_cnt));

#ifndef HAVE_2ARGS_BIO_END_IO_T
	if (bio->bi_size)
		return 1;
#endif /* HAVE_2ARGS_BIO_END_IO_T */

	if (error == 0 && !test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = -EIO;

	if (dr->dr_error == 0)
		dr->dr_error = -error;

	/* Drop reference aquired by __vdev_disk_physio */
	rc = vdev_disk_dio_put(dr);

	/* Wake up synchronous waiter this is the last outstanding bio */
	if ((rc == 1) && vdev_disk_dio_is_sync(dr))
		complete(&dr->dr_comp);

	BIO_END_IO_RETURN(0);
}

static inline unsigned long
bio_nr_pages(void *bio_ptr, unsigned int bio_size)
{
	return ((((unsigned long)bio_ptr + bio_size + PAGE_SIZE - 1) >>
	        PAGE_SHIFT) - ((unsigned long)bio_ptr >> PAGE_SHIFT));
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

		if (kmem_virt(bio_ptr))
			page = vmalloc_to_page(bio_ptr);
		else
			page = virt_to_page(bio_ptr);

		if (bio_add_page(bio, page, size, offset) != size)
			break;

		bio_ptr  += size;
		bio_size -= size;
		offset = 0;
	}

        return bio_size;
}

static int
__vdev_disk_physio(struct block_device *bdev, zio_t *zio, caddr_t kbuf_ptr,
                   size_t kbuf_size, uint64_t kbuf_offset, int flags)
{
        dio_request_t *dr;
	caddr_t bio_ptr;
	uint64_t bio_offset;
	int bio_size, bio_count = 16;
	int i = 0, error = 0;

	ASSERT3U(kbuf_offset + kbuf_size, <=, bdev->bd_inode->i_size);

retry:
	dr = vdev_disk_dio_alloc(bio_count);
	if (dr == NULL)
		return ENOMEM;

	if (zio && !(zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)))
			bio_set_flags_failfast(bdev, &flags);

	dr->dr_zio = zio;
	dr->dr_rw = flags;

	/*
	 * When the IO size exceeds the maximum bio size for the request
	 * queue we are forced to break the IO in multiple bio's and wait
	 * for them all to complete.  Ideally, all pool users will set
	 * their volume block size to match the maximum request size and
	 * the common case will be one bio per vdev IO request.
	 */
	bio_ptr    = kbuf_ptr;
	bio_offset = kbuf_offset;
	bio_size   = kbuf_size;
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
			printk("WARNING: Resized bio's/dio to %d\n",bio_count);
			goto retry;
		}

		dr->dr_bio[i] = bio_alloc(GFP_NOIO,
		                          bio_nr_pages(bio_ptr, bio_size));
		if (dr->dr_bio[i] == NULL) {
			vdev_disk_dio_free(dr);
			return ENOMEM;
		}

		/* Matching put called by vdev_disk_physio_completion */
		vdev_disk_dio_get(dr);

		dr->dr_bio[i]->bi_bdev = bdev;
		dr->dr_bio[i]->bi_sector = bio_offset >> 9;
		dr->dr_bio[i]->bi_rw = dr->dr_rw;
		dr->dr_bio[i]->bi_end_io = vdev_disk_physio_completion;
		dr->dr_bio[i]->bi_private = dr;

		/* Remaining size is returned to become the new size */
		bio_size = bio_map(dr->dr_bio[i], bio_ptr, bio_size);

		/* Advance in buffer and construct another bio if needed */
		bio_ptr    += dr->dr_bio[i]->bi_size;
		bio_offset += dr->dr_bio[i]->bi_size;
	}

	/* Extra reference to protect dio_request during submit_bio */
	vdev_disk_dio_get(dr);
	if (zio)
		zio->io_delay = jiffies_64;

	/* Submit all bio's associated with this dio */
	for (i = 0; i < dr->dr_bio_count; i++)
		if (dr->dr_bio[i])
			submit_bio(dr->dr_rw, dr->dr_bio[i]);

	/*
	 * On synchronous blocking requests we wait for all bio the completion
	 * callbacks to run.  We will be woken when the last callback runs
	 * for this dio.  We are responsible for putting the last dio_request
	 * reference will in turn put back the last bio references.  The
	 * only synchronous consumer is vdev_disk_read_rootlabel() all other
	 * IO originating from vdev_disk_io_start() is asynchronous.
	 */
	if (vdev_disk_dio_is_sync(dr)) {
		wait_for_completion(&dr->dr_comp);
		error = dr->dr_error;
		ASSERT3S(atomic_read(&dr->dr_ref), ==, 1);
	}

	(void)vdev_disk_dio_put(dr);

	return error;
}

int
vdev_disk_physio(struct block_device *bdev, caddr_t kbuf,
		 size_t size, uint64_t offset, int flags)
{
	bio_set_flags_failfast(bdev, &flags);
	return __vdev_disk_physio(bdev, NULL, kbuf, size, offset, flags);
}

/* 2.6.24 API change */
#ifdef HAVE_BIO_EMPTY_BARRIER
BIO_END_IO_PROTO(vdev_disk_io_flush_completion, bio, size, rc)
{
	zio_t *zio = bio->bi_private;

	zio->io_delay = jiffies_to_msecs(jiffies_64 - zio->io_delay);
	zio->io_error = -rc;
	if (rc && (rc == -EOPNOTSUPP))
		zio->io_vd->vdev_nowritecache = B_TRUE;

	bio_put(bio);
	ASSERT3S(zio->io_error, >=, 0);
	if (zio->io_error)
		vdev_disk_error(zio);
	zio_interrupt(zio);

	BIO_END_IO_RETURN(0);
}

static int
vdev_disk_io_flush(struct block_device *bdev, zio_t *zio)
{
	struct request_queue *q;
	struct bio *bio;

	q = bdev_get_queue(bdev);
	if (!q)
		return ENXIO;

	bio = bio_alloc(GFP_KERNEL, 0);
	if (!bio)
		return ENOMEM;

	bio->bi_end_io = vdev_disk_io_flush_completion;
	bio->bi_private = zio;
	bio->bi_bdev = bdev;
	zio->io_delay = jiffies_64;
	submit_bio(VDEV_WRITE_FLUSH_FUA, bio);

	return 0;
}
#else
static int
vdev_disk_io_flush(struct block_device *bdev, zio_t *zio)
{
	return ENOTSUP;
}
#endif /* HAVE_BIO_EMPTY_BARRIER */

static int
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *v = zio->io_vd;
	vdev_disk_t *vd = v->vdev_tsd;
	int flags, error;

	switch (zio->io_type) {
	case ZIO_TYPE_IOCTL:

		if (!vdev_readable(v)) {
			zio->io_error = ENXIO;
			return ZIO_PIPELINE_CONTINUE;
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (v->vdev_nowritecache) {
				zio->io_error = ENOTSUP;
				break;
			}

			error = vdev_disk_io_flush(vd->vd_bdev, zio);
			if (error == 0)
				return ZIO_PIPELINE_STOP;

			zio->io_error = error;
			if (error == ENOTSUP)
				v->vdev_nowritecache = B_TRUE;

			break;

		default:
			zio->io_error = ENOTSUP;
		}

		return ZIO_PIPELINE_CONTINUE;

	case ZIO_TYPE_WRITE:
		flags = WRITE;
		break;

	case ZIO_TYPE_READ:
		flags = READ;
		break;

	default:
		zio->io_error = ENOTSUP;
		return ZIO_PIPELINE_CONTINUE;
	}

	error = __vdev_disk_physio(vd->vd_bdev, zio, zio->io_data,
		                   zio->io_size, zio->io_offset, flags);
	if (error) {
		zio->io_error = error;
		return ZIO_PIPELINE_CONTINUE;
	}

	return ZIO_PIPELINE_STOP;
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

vdev_ops_t vdev_disk_ops = {
	vdev_disk_open,
	vdev_disk_close,
	vdev_default_asize,
	vdev_disk_io_start,
	vdev_disk_io_done,
	NULL,
	vdev_disk_hold,
	vdev_disk_rele,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

/*
 * Given the root disk device devid or pathname, read the label from
 * the device, and construct a configuration nvlist.
 */
int
vdev_disk_read_rootlabel(char *devpath, char *devid, nvlist_t **config)
{
	struct block_device *bdev;
	vdev_label_t *label;
	uint64_t s, size;
	int i;

	bdev = vdev_bdev_open(devpath, vdev_bdev_mode(FREAD), NULL);
	if (IS_ERR(bdev))
		return -PTR_ERR(bdev);

	s = bdev_capacity(bdev);
	if (s == 0) {
		vdev_bdev_close(bdev, vdev_bdev_mode(FREAD));
		return EIO;
	}

	size = P2ALIGN_TYPED(s, sizeof(vdev_label_t), uint64_t);
	label = vmem_alloc(sizeof(vdev_label_t), KM_PUSHPAGE);

	for (i = 0; i < VDEV_LABELS; i++) {
	        uint64_t offset, state, txg = 0;

		/* read vdev label */
		offset = vdev_label_offset(size, i, 0);
		if (vdev_disk_physio(bdev, (caddr_t)label,
		    VDEV_SKIP_SIZE + VDEV_PHYS_SIZE, offset, READ_SYNC) != 0)
			continue;

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), config, 0) != 0) {
			*config = NULL;
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state >= POOL_STATE_DESTROYED) {
			nvlist_free(*config);
			*config = NULL;
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0) {
			nvlist_free(*config);
			*config = NULL;
			continue;
		}

		break;
	}

	vmem_free(label, sizeof(vdev_label_t));
	vdev_bdev_close(bdev, vdev_bdev_mode(FREAD));

	return 0;
}

module_param(zfs_vdev_scheduler, charp, 0644);
MODULE_PARM_DESC(zfs_vdev_scheduler, "I/O scheduler");
