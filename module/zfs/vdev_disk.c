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

#if defined(_KERNEL)

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

/* FIXME: A slab entry for these would probably be good */
typedef struct dio_request {
	struct completion dr_comp;
	atomic_t dr_ref;
	vdev_t *dr_vd;
	zio_t *dr_zio;
	int dr_rc;
} dio_request_t;

static int
vdev_disk_open_common(vdev_t *vd)
{
	vdev_disk_t *dvd;
	struct block_device *bdev;
	int mode = 0;

	// dprintf("vd=%p\n", vd);

	/* Must have a pathname and it must be absolute. */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return EINVAL;
	}

	dvd = kmem_zalloc(sizeof(vdev_disk_t), KM_SLEEP);
	if (dvd == NULL)
		return ENOMEM;

	/* FIXME: Since we do not have devid support like Solaris we
	 * currently can't be as clever about opening the right device.
	 * For now we will simple open the device name provided and
	 * fail when it doesn't exist.  If your devices get reordered
	 * your going to be screwed, use udev for now to prevent this.
	 *
	 * FIXME: mode here could be the global spa_mode with a little
	 * munging of the flags to make then more agreeable to linux.
	 * However, simply passing a 0 for now gets us W/R behavior.
	 */
	bdev = open_bdev_excl(vd->vdev_path, mode, dvd);
	if (IS_ERR(bdev)) {
		kmem_free(dvd, sizeof(vdev_disk_t));
		return -PTR_ERR(bdev);
	}

	/* FIXME: Long term validate stored dvd->vd_devid with
	 * a unique identifier read from the disk.
	 */

	dvd->vd_lh = bdev;
	vd->vdev_tsd = dvd;

	return 0;
}

static int
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *ashift)
{
	vdev_disk_t *dvd;
	struct block_device *bdev;
	int rc;

	// dprintf("vd=%p, psize=%p, ashift=%p\n", vd, psize, ashift);
	dprintf("adding disk %s\n",
	        vd->vdev_path ? vd->vdev_path : "<none>");

	rc = vdev_disk_open_common(vd);
	if (rc)
		return rc;

	dvd = vd->vdev_tsd;
	bdev = dvd->vd_lh;

	/* Determine the actual size of the device (in bytes) */
	*psize = get_capacity(bdev->bd_disk) * SECTOR_SIZE;

	/* Check if this is a whole device and if it is try and
	 * enable the write cache, it is OK if this fails.
	 *
	 * FIXME: This behavior should probably be configurable.
	 */
	if (bdev->bd_contains == bdev) {
		int wce = 1;

		vd->vdev_wholedisk = 1ULL;

		/* Different methods are needed for an IDE vs SCSI disk.
		 * Since we're not sure what type of disk this is try IDE,
		 * if that fails try SCSI. */
		rc = ioctl_by_bdev(bdev, HDIO_SET_WCACHE, (unsigned long)&wce);
		if (rc)
			dprintf("Unable to enable IDE WCE and SCSI WCE "
				"not yet supported: %d\n", rc);

		/* FIXME: To implement the scsi WCE enable we are going to need
		 * to use the SG_IO ioctl.  But that means fully forming the
		 * SCSI command as the ioctl arg.  To get this right I need
		 * to look at the sdparm source which does this.
		 */
		rc = 0;
	} else {
		/* Must be a partition, that's fine. */
		vd->vdev_wholedisk = 0;
	}

	/* Based on the minimum sector size set the block size */
	*ashift = highbit(MAX(SECTOR_SIZE, SPA_MINBLOCKSIZE)) - 1;

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	vd->vdev_nowritecache = B_FALSE;

	return rc;
}

static void
vdev_disk_close(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	// dprintf("vd=%p\n", vd);
	dprintf("removing disk %s\n",
	        vd->vdev_path ? vd->vdev_path : "<none>");

	if (dvd == NULL)
		return;

	close_bdev_excl(dvd->vd_lh);

	kmem_free(dvd, sizeof(vdev_disk_t));
	vd->vdev_tsd = NULL;
}

#ifdef HAVE_2ARGS_BIO_END_IO_T
static void
vdev_disk_probe_io_completion(struct bio *bio, int rc)
#else
static int
vdev_disk_probe_io_completion(struct bio *bio, unsigned int size, int rc)
#endif /* HAVE_2ARGS_BIO_END_IO_T */
{
        dio_request_t *dr = bio->bi_private;
	zio_t *zio;
	int error;


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

	zio = dr->dr_zio;
	if (zio) {
		zio->io_error = error;
		zio_interrupt(zio);
	}

	dr->dr_rc = error;
        atomic_dec(&dr->dr_ref);

        if (bio_sync(bio)) {
		complete(&dr->dr_comp);
	} else {
		kmem_free(dr, sizeof(dio_request_t));
	        bio_put(bio);
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
__bio_map_vmem(struct request_queue *q, void *data,
               unsigned int len, gfp_t gfp_mask)
{
        unsigned long kaddr = (unsigned long)data;
        unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
        unsigned long start = kaddr >> PAGE_SHIFT;
        const int nr_pages = end - start;
        int offset, i;
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

		page = vmalloc_to_page(data);
		ASSERT(page); /* Expecting virtual linear address */

                if (bio_add_pc_page(q, bio, page, bytes, offset) < bytes)
                        break;

                data += bytes;
                len -= bytes;
                offset = 0;
		bytes = PAGE_SIZE;
        }

        return bio;
}

static struct bio *
bio_map_vmem(struct request_queue *q, void *data,
             unsigned int len, gfp_t gfp_mask)
{
	struct bio *bio;

	bio = __bio_map_vmem(q, data, len, gfp_mask);
	if (IS_ERR(bio))
		return bio;

	if (bio->bi_size != len) {
		bio_put(bio);
		return ERR_PTR(-EINVAL);
	}

	return bio;
}

static struct bio *
bio_map(struct request_queue *q, void *data, unsigned int len, gfp_t gfp_mask)
{
	struct bio *bio;

	/* Cleanly map buffer we are passed in to a bio regardless
	 * of if the buffer is a virtual or physical address. */
	if (kmem_virt(data))
		bio = bio_map_vmem(q, data, len, gfp_mask);
	else
		bio = bio_map_kern(q, data, len, gfp_mask);

	return bio;
}

static int
vdev_disk_io(vdev_t *vd, zio_t *zio, caddr_t kbuf, size_t size,
             uint64_t offset, int flags)
{
	struct bio *bio;
        dio_request_t *dr;
	int rw, rc = 0;
	struct block_device *bdev;
	struct request_queue *q;

	// dprintf("vd=%p, zio=%p, kbuf=%p, size=%ld, offset=%lu, flag=%lx\n",
	//        vd, zio, kbuf, size, offset, flags);

	ASSERT((offset % SECTOR_SIZE) == 0); /* Sector aligned */

	if (vd == NULL || vd->vdev_tsd == NULL)
		return EINVAL;

	dr = kmem_alloc(sizeof(dio_request_t), KM_SLEEP);
	if (dr == NULL)
		return ENOMEM;

	atomic_set(&dr->dr_ref, 0);
	dr->dr_vd = vd;
	dr->dr_zio = zio;
	dr->dr_rc = 0;

	bdev = ((vdev_disk_t *)(vd->vdev_tsd))->vd_lh;
	q = bdev->bd_disk->queue;

	bio = bio_map(q, kbuf, size, GFP_NOIO);
	if (IS_ERR(bio)) {
		kmem_free(dr, sizeof(dio_request_t));
		return -PTR_ERR(bio);
	}

	bio->bi_bdev = bdev;
	bio->bi_sector = offset / SECTOR_SIZE;
	bio->bi_end_io = vdev_disk_probe_io_completion;
	bio->bi_private = dr;

	init_completion(&dr->dr_comp);
	atomic_inc(&dr->dr_ref);

	if (flags & (1 << BIO_RW))
		rw = (flags & (1 << BIO_RW_SYNC)) ? WRITE_SYNC : WRITE;
	else
		rw = READ;

	if (flags & (1 << BIO_RW_FAILFAST))
		rw |= 1 << BIO_RW_FAILFAST;

	ASSERT3S(flags & ~((1 << BIO_RW) | (1 << BIO_RW_SYNC) |
	    (1 << BIO_RW_FAILFAST)), ==, 0);

	submit_bio(rw, bio);

	/*
	 * On syncronous blocking requests we wait for the completion
	 * callback to wake us.  Then we are responsible for freeing
	 * the dio_request_t as well as dropping the final bio reference.
	 */
	if (bio_sync(bio)) {
		wait_for_completion(&dr->dr_comp);
		ASSERT(atomic_read(&dr->dr_ref) == 0);
		rc = dr->dr_rc;
		kmem_free(dr, sizeof(dio_request_t));
	        bio_put(bio);
	}

	if (zio_injection_enabled && rc == 0)
		rc = zio_handle_device_injection(vd, EIO);

	return rc;
}

static int
vdev_disk_probe_io(vdev_t *vd, caddr_t kbuf, size_t size,
		   uint64_t offset, int flags)
{
	int rc;

	// dprintf("vd=%p, kbuf=%p, size=%ld, offset=%lu, flag=%d\n",
	//        vd, kbuf, size, offset, flags);

	flags |= (1 << BIO_RW_SYNC);
	flags |= (1 << BIO_RW_FAILFAST);

	/* FIXME: offset must be block aligned or we need to take
	 * care of it */

	rc = vdev_disk_io(vd, NULL, kbuf, size, offset, flags);

	return rc;
}

/*
 * Determine if the underlying device is accessible by reading and writing
 * to a known location. We must be able to do this during syncing context
 * and thus we cannot set the vdev state directly.
 */
static int
vdev_disk_probe(vdev_t *vd)
{
	vdev_t *nvd;
	int label_idx, rc = 0, retries = 0;
	uint64_t offset;
        char *vl_pad;

	// dprintf("vd=%p\n", vd);

	if (vd == NULL)
		return EINVAL;

	/* Hijack the current vdev */
	nvd = vd;

	/* Pick a random label to rewrite */
	label_idx = spa_get_random(VDEV_LABELS);
	ASSERT(label_idx < VDEV_LABELS);

	offset = vdev_label_offset(vd->vdev_psize, label_idx,
	                           offsetof(vdev_label_t, vl_pad));

        vl_pad = vmem_alloc(VDEV_SKIP_SIZE, KM_SLEEP);
	if (vl_pad == NULL)
		return ENOMEM;

	/*
	 * Try to read and write to a special location on the
	 * label. We use the existing vdev initially and only
	 * try to create and reopen it if we encounter a failure.
	 */
	while ((rc = vdev_disk_probe_io(nvd, vl_pad,
	        VDEV_SKIP_SIZE, offset, READ)) != 0 && retries == 0) {

		nvd = kmem_zalloc(sizeof(vdev_t), KM_SLEEP);

		if (vd->vdev_path)
			nvd->vdev_path = spa_strdup(vd->vdev_path);
		if (vd->vdev_physpath)
			nvd->vdev_physpath = spa_strdup(vd->vdev_physpath);
		if (vd->vdev_devid)
			nvd->vdev_devid = spa_strdup(vd->vdev_devid);

		nvd->vdev_wholedisk = vd->vdev_wholedisk;
		nvd->vdev_guid = vd->vdev_guid;
		retries++;

		rc = vdev_disk_open_common(nvd);
		if (rc)
			break;
	}

	if (!rc)
		rc = vdev_disk_probe_io(nvd, vl_pad, VDEV_SKIP_SIZE,
		                        offset, WRITE);

	/* Clean up if we allocated a new vdev */
	if (retries) {
		vdev_disk_close(nvd);
		if (nvd->vdev_path)
			spa_strfree(nvd->vdev_path);
		if (nvd->vdev_physpath)
			spa_strfree(nvd->vdev_physpath);
		if (nvd->vdev_devid)
			spa_strfree(nvd->vdev_devid);
		kmem_free(nvd, sizeof(vdev_t));
	}

	vmem_free(vl_pad, VDEV_SKIP_SIZE);

	/* Reset the failing flag */
	if (!rc)
		vd->vdev_is_failing = B_FALSE;

	return rc;
}

#if 0
static void
vdev_disk_ioctl_done(void *zio_arg, int rc)
{
	zio_t *zio = zio_arg;

	zio->io_error = rc;

	zio_interrupt(zio);
}
#endif

static int
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
//	vdev_disk_t *dvd = vd->vdev_tsd;
	int flags, rc;

	// dprintf("zio=%p\n", zio);

	if (zio->io_type == ZIO_TYPE_IOCTL) {
		zio_vdev_io_bypass(zio);

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
			zio->io_dk_callback.dkc_callback = vdev_disk_ioctl_done;
			zio->io_dk_callback.dkc_flag = FLUSH_VOLATILE;
			zio->io_dk_callback.dkc_cookie = zio;

			rc = ldi_ioctl(dvd->vd_lh, zio->io_cmd,
			    (uintptr_t)&zio->io_dk_callback,
			    FKIOCTL, kcred, NULL);

			if (rc == 0) {
				/*
				 * The ioctl will be done asychronously,
				 * and will call vdev_disk_ioctl_done()
				 * upon completion.
				 */
				return ZIO_PIPELINE_STOP;
			}
#else
			rc = ENOTSUP;
#endif

			if (rc == ENOTSUP || rc == ENOTTY) {
				/*
				 * If we get ENOTSUP or ENOTTY, we know that
				 * no future attempts will ever succeed.
				 * In this case we set a persistent bit so
				 * that we don't bother with the ioctl in the
				 * future.
				 */
				vd->vdev_nowritecache = B_TRUE;
			}
			zio->io_error = rc;

			break;

		default:
			zio->io_error = ENOTSUP;
		}

		return ZIO_PIPELINE_CONTINUE;
	}

	if (zio->io_type == ZIO_TYPE_READ && vdev_cache_read(zio) == 0)
		return ZIO_PIPELINE_STOP;

	if ((zio = vdev_queue_io(zio)) == NULL)
		return ZIO_PIPELINE_STOP;

	if (zio->io_type == ZIO_TYPE_WRITE)
		rc = vdev_writeable(vd) ? vdev_error_inject(vd, zio) : ENXIO;
	else
		rc = vdev_readable(vd) ? vdev_error_inject(vd, zio) : ENXIO;

	rc = (vd->vdev_remove_wanted || vd->vdev_is_failing) ? ENXIO : rc;

	if (rc) {
		zio->io_error = rc;
		zio_interrupt(zio);
		return ZIO_PIPELINE_STOP;
	}

	flags = ((zio->io_type == ZIO_TYPE_READ) ? READ : WRITE);
	/* flags |= B_BUSY | B_NOCACHE; FIXME : Not supported */

	if (zio->io_flags & ZIO_FLAG_FAILFAST)
		flags |= (1 << BIO_RW_FAILFAST);


	vdev_disk_io(vd, zio, zio->io_data, zio->io_size,
	             zio->io_offset, flags);

	return ZIO_PIPELINE_STOP;
}

static int
vdev_disk_io_done(zio_t *zio)
{
	// dprintf("zio=%p\n", zio);

	vdev_queue_io_done(zio);

	if (zio->io_type == ZIO_TYPE_WRITE)
		vdev_cache_write(zio);

	if (zio_injection_enabled && zio->io_error == 0)
		zio->io_error = zio_handle_device_injection(zio->io_vd, EIO);

	/*
	 * If the device returned EIO, then attempt a DKIOCSTATE ioctl to see if
	 * the device has been removed.  If this is the case, then we trigger an
	 * asynchronous removal of the device. Otherwise, probe the device and
	 * make sure it's still accessible.
	 */
	if (zio->io_error == EIO) {
		ASSERT(0); /* FIXME: Not yet supported */
#if 0
		vdev_t *vd = zio->io_vd;
		vdev_disk_t *dvd = vd->vdev_tsd;
		int state;

		state = DKIO_NONE;
		if (dvd && ldi_ioctl(dvd->vd_lh, DKIOCSTATE, (intptr_t)&state,
		    FKIOCTL, kcred, NULL) == 0 &&
		    state != DKIO_INSERTED) {
			vd->vdev_remove_wanted = B_TRUE;
			spa_async_request(zio->io_spa, SPA_ASYNC_REMOVE);
		} else if (vdev_probe(vd) != 0) {
			ASSERT(vd->vdev_ops->vdev_op_leaf);
			vd->vdev_is_failing = B_TRUE;
		}
#endif
	}

	return ZIO_PIPELINE_CONTINUE;
}

nvlist_t *
vdev_disk_read_rootlabel(char *devpath, char *devid, nvlist_t **config)
{
	return NULL;
}

vdev_ops_t vdev_disk_ops = {
	vdev_disk_open,
	vdev_disk_close,
	vdev_disk_probe,
	vdev_default_asize,
	vdev_disk_io_start,
	vdev_disk_io_done,
	NULL,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

#endif  /* _KERNEL */
