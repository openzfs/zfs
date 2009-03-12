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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/sunldi.h>

/*
 * Virtual device vector for disks.
 */
typedef struct dio_request {
	struct completion	dr_comp;	/* Completion for sync IO */
	spinlock_t		dr_lock;	/* Completion lock */
	zio_t			*dr_zio;	/* Parent ZIO */
	int			dr_ref;		/* Outstanding bio count */
	int			dr_rw;		/* Read/Write */
	int			dr_error;	/* Bio error */
	int			dr_bio_count;	/* Count of bio's */
        struct bio		*dr_bio[0];	/* Attached bio's */
} dio_request_t;

static int
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *ashift)
{
	struct block_device *vd_lh;
	vdev_disk_t *dvd;

	/* Must have a pathname and it must be absolute. */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return EINVAL;
	}

	dvd = kmem_zalloc(sizeof(vdev_disk_t), KM_SLEEP);
	if (dvd == NULL)
		return ENOMEM;

	/* XXX: Since we do not have devid support like Solaris we
	 * currently can't be as clever about opening the right device.
	 * For now we will simply open the device name provided and
	 * fail when it doesn't exist.  If your devices get reordered
	 * your going to be screwed, use udev for now to prevent this.
	 *
	 * XXX: mode here could be the global spa_mode with a little
	 * munging of the flags to make then more agreeable to linux.
	 * However, simply passing a 0 for now gets us W/R behavior.
	 */
	vd_lh = open_bdev_excl(vd->vdev_path, 0, dvd);
	if (IS_ERR(vd_lh)) {
		kmem_free(dvd, sizeof(vdev_disk_t));
		return -PTR_ERR(vd_lh);
	}

	/* XXX: Long term validate stored dvd->vd_devid with a unique
	 * identifier read from the disk, likely EFI support.
	 */

	vd->vdev_tsd = dvd;
	dvd->vd_lh = vd_lh;

	/* Check if this is a whole device.  When vd_lh->bd_contains ==
	 * vd_lh we have a whole device and not simply a partition. */
	vd->vdev_wholedisk = !!(vd_lh->bd_contains == vd_lh);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	vd->vdev_nowritecache = B_FALSE;

	/* Determine the actual size of the device (in bytes)
	 *
	 * XXX: SECTOR_SIZE is defined to 512b which may not be true for
	 * your device, we must use the actual hardware sector size.
	 */
	*psize = get_capacity(vd_lh->bd_disk) * SECTOR_SIZE;

	/* Based on the minimum sector size set the block size */
	*ashift = highbit(MAX(SECTOR_SIZE, SPA_MINBLOCKSIZE)) - 1;

	return 0;
}

static void
vdev_disk_close(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (dvd == NULL)
		return;

	if (dvd->vd_lh != NULL)
		close_bdev_excl(dvd->vd_lh);

	kmem_free(dvd, sizeof(vdev_disk_t));
	vd->vdev_tsd = NULL;
}

#ifdef HAVE_2ARGS_BIO_END_IO_T
static void
vdev_disk_physio_completion(struct bio *bio, int rc)
#else
static int
vdev_disk_physio_completion(struct bio *bio, unsigned int size, int rc)
#endif /* HAVE_2ARGS_BIO_END_IO_T */
{
	dio_request_t *dr = bio->bi_private;
	zio_t *zio;
	int i, error;

	/* Fatal error but print some useful debugging before asserting */
	if (dr == NULL) {
		printk("FATAL: bio->bi_private == NULL\n"
		       "bi_next: %p, bi_flags: %lx, bi_rw: %lu, bi_vcnt: %d\n"
		       "bi_idx: %d, bi->size: %d, bi_end_io: %p, bi_cnt: %d\n",
		       bio->bi_next, bio->bi_flags, bio->bi_rw, bio->bi_vcnt,
		       bio->bi_idx, bio->bi_size, bio->bi_end_io,
		       atomic_read(&bio->bi_cnt));
		SBUG();
	}

	/* Incomplete */
	if (bio->bi_size) {
		rc = 1;
		goto out;
	}

	error = rc;
	if (error == 0 && !test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = EIO;

	spin_lock(&dr->dr_lock);

	dr->dr_ref--;
	if (dr->dr_error == 0)
		dr->dr_error = error;

	/*
	 * All bio's attached to this dio request have completed.  This
	 * means it is safe to access the dio outside the spin lock, we
	 * are assured there will be no racing accesses.
	 */
	if (dr->dr_ref == 0) {
		zio = dr->dr_zio;
		spin_unlock(&dr->dr_lock);

		/* Syncronous dio cleanup handled by waiter */
		if (dr->dr_rw & (1 << BIO_RW_SYNC)) {
			complete(&dr->dr_comp);
		} else {
			for (i = 0; i < dr->dr_bio_count; i++)
				bio_put(dr->dr_bio[i]);

			kmem_free(dr, sizeof(dio_request_t) +
			          sizeof(struct bio *) * dr->dr_bio_count);
		}

		if (zio) {
			zio->io_error = dr->dr_error;
			zio_interrupt(zio);
		}
	} else {
		spin_unlock(&dr->dr_lock);
	}

	rc = 0;
out:
#ifdef HAVE_2ARGS_BIO_END_IO_T
	return;
#else
	return rc;
#endif /* HAVE_2ARGS_BIO_END_IO_T */
}

static struct bio *
bio_map_virt(struct request_queue *q, void *data,
               unsigned int len, gfp_t gfp_mask)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	unsigned int offset, i, data_len = len;
	const int nr_pages = end - start;
	struct page *page;
	struct bio *bio;

	bio = bio_alloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	offset = offset_in_page(kaddr);
	for (i = 0; i < nr_pages; i++) {
		unsigned int bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		VERIFY3P(page = vmalloc_to_page(data), !=, NULL);
		VERIFY3U(bio_add_pc_page(q, bio, page, bytes, offset), ==, bytes);

		data += bytes;
		len -= bytes;
		offset = 0;
		bytes = PAGE_SIZE;
	}

	VERIFY3U(bio->bi_size, ==, data_len);
        return bio;
}

static struct bio *
bio_map(struct request_queue *q, void *data, unsigned int len, gfp_t gfp_mask)
{
	struct bio *bio;

	/* Cleanly map buffer we are passed in to a bio regardless
	 * of if the buffer is a virtual or physical address. */
	if (kmem_virt(data))
		bio = bio_map_virt(q, data, len, gfp_mask);
	else
		bio = bio_map_kern(q, data, len, gfp_mask);

	return bio;
}

static int
__vdev_disk_physio(struct block_device *vd_lh, zio_t *zio, caddr_t kbuf_ptr,
                   size_t kbuf_size, uint64_t kbuf_offset, int flags)
{
	struct request_queue *q = vd_lh->bd_disk->queue;
        dio_request_t *dr;
	caddr_t bio_ptr;
	uint64_t bio_offset;
	int i, j, error = 0, bio_count, bio_size, dio_size;

	ASSERT3S(kbuf_offset % SECTOR_SIZE, ==, 0);
	ASSERT3S(flags &
		 ~((1 << BIO_RW) |
		   (1 << BIO_RW_SYNC) |
		   (1 << BIO_RW_FAILFAST)), ==, 0);

	bio_count = (kbuf_size / (q->max_hw_sectors << 9)) + 1;
	dio_size  = sizeof(dio_request_t) + sizeof(struct bio *) * bio_count;
	dr = kmem_zalloc(dio_size, KM_SLEEP);
	if (dr == NULL)
		return ENOMEM;

	init_completion(&dr->dr_comp);
	spin_lock_init(&dr->dr_lock);
	dr->dr_ref = 0;
	dr->dr_zio = zio;
	dr->dr_rw = READ;
	dr->dr_error = 0;
	dr->dr_bio_count = bio_count;

	if (flags & (1 << BIO_RW))
		dr->dr_rw = (flags & (1 << BIO_RW_SYNC)) ? WRITE_SYNC : WRITE;

	if (flags & (1 << BIO_RW_FAILFAST))
		dr->dr_rw |= 1 << BIO_RW_FAILFAST;

	/*
	 * When the IO size exceeds the maximum bio size for the request
	 * queue we are forced to break the IO in multiple bio's and wait
	 * for them all to complete.  Ideally, all pool users will set
	 * their volume block size to match the maximum request size and
	 * the common case will be one bio per vdev IO request.
	 */
	bio_ptr = kbuf_ptr;
	bio_offset = kbuf_offset;
	for (i = 0; i < dr->dr_bio_count; i++) {
		bio_size = MIN(kbuf_size, q->max_hw_sectors << 9);

		dr->dr_bio[i] = bio_map(q, bio_ptr, bio_size, GFP_NOIO);
		if (IS_ERR(dr->dr_bio[i])) {
			for (j = 0; j < i; j++)
				bio_put(dr->dr_bio[j]);

			error = -PTR_ERR(dr->dr_bio[i]);
			kmem_free(dr, dio_size);
			return error;
		}

		dr->dr_bio[i]->bi_bdev = vd_lh;
		dr->dr_bio[i]->bi_sector = bio_offset >> 9;
		dr->dr_bio[i]->bi_end_io = vdev_disk_physio_completion;
		dr->dr_bio[i]->bi_private = dr;
		dr->dr_ref++;

		bio_ptr    += bio_size;
		bio_offset += bio_size;
		kbuf_size  -= bio_size;
	}

	for (i = 0; i < dr->dr_bio_count; i++)
		submit_bio(dr->dr_rw, dr->dr_bio[i]);

	/*
	 * On syncronous blocking requests we wait for all bio the completion
	 * callbacks to run.  We will be woken when the last callback runs
	 * for this dio.  We are responsible for freeing the dio_request_t as
	 * well as the final reference on all attached bios.
	 */
	if (dr->dr_rw & (1 << BIO_RW_SYNC)) {
		wait_for_completion(&dr->dr_comp);
		ASSERT(dr->dr_ref == 0);
		error = dr->dr_error;

		for (i = 0; i < dr->dr_bio_count; i++)
			bio_put(dr->dr_bio[i]);

		kmem_free(dr, dio_size);
	}

	return error;
}

int
vdev_disk_physio(ldi_handle_t vd_lh, caddr_t kbuf,
		 size_t size, uint64_t offset, int flags)
{
	return __vdev_disk_physio(vd_lh, NULL, kbuf, size, offset, flags);
}

#if 0
/* XXX: Not yet supported */
static void
vdev_disk_ioctl_done(void *zio_arg, int error)
{
	zio_t *zio = zio_arg;

	zio->io_error = error;

	zio_interrupt(zio);
}
#endif

static int
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_disk_t *dvd = vd->vdev_tsd;
	int flags, error;

	if (zio->io_type == ZIO_TYPE_IOCTL) {

		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = ENXIO;
			return ZIO_PIPELINE_CONTINUE;
		}

		switch (zio->io_cmd) {

		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (vd->vdev_nowritecache) {
				zio->io_error = ENOTSUP;
				break;
			}

#if 0
			/* XXX: Not yet supported */
			vdev_disk_t *dvd = vd->vdev_tsd;

			zio->io_dk_callback.dkc_callback = vdev_disk_ioctl_done;
			zio->io_dk_callback.dkc_flag = FLUSH_VOLATILE;
			zio->io_dk_callback.dkc_cookie = zio;

			error = ldi_ioctl(dvd->vd_lh, zio->io_cmd,
			    (uintptr_t)&zio->io_dk_callback,
			    FKIOCTL, kcred, NULL);

			if (error == 0) {
				/*
				 * The ioctl will be done asychronously,
				 * and will call vdev_disk_ioctl_done()
				 * upon completion.
				 */
				return ZIO_PIPELINE_STOP;
			}
#else
			error = ENOTSUP;
#endif

			if (error == ENOTSUP || error == ENOTTY) {
				/*
				 * If we get ENOTSUP or ENOTTY, we know that
				 * no future attempts will ever succeed.
				 * In this case we set a persistent bit so
				 * that we don't bother with the ioctl in the
				 * future.
				 */
				vd->vdev_nowritecache = B_TRUE;
			}
			zio->io_error = error;

			break;

		default:
			zio->io_error = ENOTSUP;
		}

		return ZIO_PIPELINE_CONTINUE;
	}

	/*
	 * B_BUSY	XXX: Not supported
	 * B_NOCACHE	XXX: Not supported
	 */
	flags = ((zio->io_type == ZIO_TYPE_READ) ? READ : WRITE);

	if (zio->io_flags & ZIO_FLAG_IO_RETRY)
		flags |= (1 << BIO_RW_FAILFAST);

	error = __vdev_disk_physio(dvd->vd_lh, zio, zio->io_data,
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
	 * If the device returned EIO, then attempt a DKIOCSTATE ioctl to see if
	 * the device has been removed.  If this is the case, then we trigger an
	 * asynchronous removal of the device. Otherwise, probe the device and
	 * make sure it's still accessible.
	 */
	VERIFY3S(zio->io_error, ==, 0);
#if 0
		vdev_disk_t *dvd = vd->vdev_tsd;
		int state = DKIO_NONE;

		if (ldi_ioctl(dvd->vd_lh, DKIOCSTATE, (intptr_t)&state,
		    FKIOCTL, kcred, NULL) == 0 && state != DKIO_INSERTED) {
			vd->vdev_remove_wanted = B_TRUE;
			spa_async_request(zio->io_spa, SPA_ASYNC_REMOVE);
		}
#endif
}

vdev_ops_t vdev_disk_ops = {
	vdev_disk_open,
	vdev_disk_close,
	vdev_default_asize,
	vdev_disk_io_start,
	vdev_disk_io_done,
	NULL,
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
	struct block_device *vd_lh;
	vdev_label_t *label;
	uint64_t s, size;
	int i;

	/*
	 * Read the device label and build the nvlist.
	 * XXX: Not yet supported
	 */
#if 0
	if (devid != NULL && ddi_devid_str_decode(devid, &tmpdevid,
	    &minor_name) == 0) {
		error = ldi_open_by_devid(tmpdevid, minor_name, spa_mode,
					  kcred, &vd_lh, zfs_li);
		ddi_devid_free(tmpdevid);
		ddi_devid_str_free(minor_name);
	}
#endif

	vd_lh = open_bdev_excl(devpath, MS_RDONLY, NULL);
	if (IS_ERR(vd_lh))
		return -PTR_ERR(vd_lh);

	s = get_capacity(vd_lh->bd_disk) * SECTOR_SIZE;
	if (s == 0) {
		close_bdev_excl(vd_lh);
		return EIO;
	}

	size = P2ALIGN_TYPED(s, sizeof(vdev_label_t), uint64_t);
	label = vmem_alloc(sizeof(vdev_label_t), KM_SLEEP);

	for (i = 0; i < VDEV_LABELS; i++) {
	        uint64_t offset, state, txg = 0;

		/* read vdev label */
		offset = vdev_label_offset(size, i, 0);
		if (vdev_disk_physio(vd_lh, (caddr_t)label,
		    VDEV_SKIP_SIZE + VDEV_BOOT_HEADER_SIZE +
		    VDEV_PHYS_SIZE, offset, READ) != 0)
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
	close_bdev_excl(vd_lh);

	return 0;
}
