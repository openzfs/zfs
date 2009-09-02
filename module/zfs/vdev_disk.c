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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
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

static int
vdev_disk_open(vdev_t *v, uint64_t *psize, uint64_t *ashift)
{
	struct block_device *bdev;
	vdev_disk_t *vd;
	int mode;

	/* Must have a pathname and it must be absolute. */
	if (v->vdev_path == NULL || v->vdev_path[0] != '/') {
		v->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return EINVAL;
	}

	vd = kmem_zalloc(sizeof(vdev_disk_t), KM_SLEEP);
	if (vd == NULL)
		return ENOMEM;

	/*
	 * XXX: Since we do not have devid support like Solaris we
	 * currently can't be as clever about opening the right device.
	 * For now we will simply open the device name provided and
	 * fail when it doesn't exist.  If your devices get reordered
	 * your going to be screwed, use udev for now to prevent this.
	 */
	mode = spa_mode(v->vdev_spa);
	bdev = vdev_bdev_open(v->vdev_path, vdev_bdev_mode(mode), vd);
	if (IS_ERR(bdev)) {
		kmem_free(vd, sizeof(vdev_disk_t));
		return -PTR_ERR(bdev);
	}

	/*
	 * XXX: Long term validate stored vd->vd_devid with a unique
	 * identifier read from the disk, likely EFI support.
	 */

	v->vdev_tsd = vd;
	vd->vd_bdev = bdev;

	/* Check if this is a whole device.  When bdev->bd_contains ==
	 * bdev we have a whole device and not simply a partition. */
	v->vdev_wholedisk = !!(bdev->bd_contains == bdev);

	/* Clear the nowritecache bit, causes vdev_reopen() to try again. */
	v->vdev_nowritecache = B_FALSE;

	/* Determine the actual size of the device (in bytes)
	 *
	 * XXX: SECTOR_SIZE is defined to 512b which may not be true for
	 * your device, we must use the actual hardware sector size.
	 */
	*psize = get_capacity(bdev->bd_disk) * SECTOR_SIZE;

	/* Based on the minimum sector size set the block size */
	*ashift = highbit(MAX(SECTOR_SIZE, SPA_MINBLOCKSIZE)) - 1;

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
	                 sizeof(struct bio *) * bio_count, KM_SLEEP);
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

static void
vdev_disk_dio_get(dio_request_t *dr)
{
	atomic_inc(&dr->dr_ref);
}

static int
vdev_disk_dio_put(dio_request_t *dr)
{
	int rc = atomic_dec_return(&dr->dr_ref);

	/* Free the dio_request when the last reference is dropped and
	 * ensure zio_interpret is called only once with the correct zio */
	if (rc == 0) {
		zio_t *zio = dr->dr_zio;
		int error = dr->dr_error;

		vdev_disk_dio_free(dr);

		if (zio) {
			zio->io_error = error;
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
	if (dr == NULL) {
		printk("FATAL: bio->bi_private == NULL\n"
		       "bi_next: %p, bi_flags: %lx, bi_rw: %lu, bi_vcnt: %d\n"
		       "bi_idx: %d, bi_size: %d, bi_end_io: %p, bi_cnt: %d\n",
		       bio->bi_next, bio->bi_flags, bio->bi_rw, bio->bi_vcnt,
		       bio->bi_idx, bio->bi_size, bio->bi_end_io,
		       atomic_read(&bio->bi_cnt));
		SBUG();
	}

#ifndef HAVE_2ARGS_BIO_END_IO_T
	if (bio->bi_size)
		return 1;
#endif /* HAVE_2ARGS_BIO_END_IO_T */

	if (error == 0 && !test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = EIO;

	if (dr->dr_error == 0)
		dr->dr_error = error;

	/* Drop reference aquired by __vdev_disk_physio */
	rc = vdev_disk_dio_put(dr);

	/* Wake up synchronous waiter this is the last outstanding bio */
	if ((rc == 1) && (dr->dr_rw & (1 << DIO_RW_SYNCIO)))
		complete(&dr->dr_comp);

	BIO_END_IO_RETURN(0);
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
		VERIFY3U(bio_add_pc_page(q, bio, page, bytes, offset),==,bytes);

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
__vdev_disk_physio(struct block_device *bdev, zio_t *zio, caddr_t kbuf_ptr,
                   size_t kbuf_size, uint64_t kbuf_offset, int flags)
{
	struct request_queue *q;
        dio_request_t *dr;
	caddr_t bio_ptr;
	uint64_t bio_offset;
	int i, error = 0, bio_count, bio_size;

	ASSERT3S(kbuf_offset % SECTOR_SIZE, ==, 0);
	q = bdev_get_queue(bdev);
	if (!q)
		return ENXIO;

	bio_count = (kbuf_size / (q->max_hw_sectors << 9)) + 1;
	dr = vdev_disk_dio_alloc(bio_count);
	if (dr == NULL)
		return ENOMEM;

	dr->dr_zio = zio;
	dr->dr_rw = flags;

#ifdef BIO_RW_FAILFAST
	if (flags & (1 << BIO_RW_FAILFAST))
		dr->dr_rw |= 1 << BIO_RW_FAILFAST;
#endif /* BIO_RW_FAILFAST */

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
			error = -PTR_ERR(dr->dr_bio[i]);
			vdev_disk_dio_free(dr);
			return error;
		}

		/* Matching put called by vdev_disk_physio_completion */
		vdev_disk_dio_get(dr);

		dr->dr_bio[i]->bi_bdev = bdev;
		dr->dr_bio[i]->bi_sector = bio_offset >> 9;
		dr->dr_bio[i]->bi_end_io = vdev_disk_physio_completion;
		dr->dr_bio[i]->bi_private = dr;

		bio_ptr    += bio_size;
		bio_offset += bio_size;
		kbuf_size  -= bio_size;
	}

	/* Extra reference to protect dio_request during submit_bio */
	vdev_disk_dio_get(dr);

	for (i = 0; i < dr->dr_bio_count; i++)
		submit_bio(dr->dr_rw, dr->dr_bio[i]);

	/*
	 * On synchronous blocking requests we wait for all bio the completion
	 * callbacks to run.  We will be woken when the last callback runs
	 * for this dio.  We are responsible for putting the last dio_request
	 * reference will in turn put back the last bio references.  The
	 * only synchronous consumer is vdev_disk_read_rootlabel() all other
	 * IO originating from vdev_disk_io_start() is asynchronous.
	 */
	if (dr->dr_rw & (1 << DIO_RW_SYNCIO)) {
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
	return __vdev_disk_physio(bdev, NULL, kbuf, size, offset, flags);
}

/* 2.6.24 API change */
#ifdef HAVE_BIO_EMPTY_BARRIER
BIO_END_IO_PROTO(vdev_disk_io_flush_completion, bio, size, rc)
{
	zio_t *zio = bio->bi_private;

	zio->io_error = -rc;
	if (rc && (rc == -EOPNOTSUPP))
		zio->io_vd->vdev_nowritecache = B_TRUE;

	bio_put(bio);
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
	submit_bio(WRITE_BARRIER, bio);

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

#ifdef BIO_RW_FAILFAST
	if (zio->io_flags & (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD))
		flags |= (1 << BIO_RW_FAILFAST);
#endif /* BIO_RW_FAILFAST */

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
	struct block_device *bdev;
	vdev_label_t *label;
	uint64_t s, size;
	int i;

	bdev = vdev_bdev_open(devpath, vdev_bdev_mode(FREAD), NULL);
	if (IS_ERR(bdev))
		return -PTR_ERR(bdev);

	s = get_capacity(bdev->bd_disk) * SECTOR_SIZE;
	if (s == 0) {
		vdev_bdev_close(bdev, vdev_bdev_mode(FREAD));
		return EIO;
	}

	size = P2ALIGN_TYPED(s, sizeof(vdev_label_t), uint64_t);
	label = vmem_alloc(sizeof(vdev_label_t), KM_SLEEP);

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
