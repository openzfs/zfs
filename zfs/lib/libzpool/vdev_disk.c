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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)vdev_disk.c	1.15	08/04/09 SMI"

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/refcount.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/sunldi.h>

/*
 * Virtual device vector for disks.
 */

extern ldi_ident_t zfs_li;

typedef struct vdev_disk_buf {
	buf_t	vdb_buf;
	zio_t	*vdb_io;
} vdev_disk_buf_t;

static int
vdev_disk_open_common(vdev_t *vd)
{
	vdev_disk_t *dvd;
	dev_t dev;
	int error;

	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (EINVAL);
	}

	dvd = vd->vdev_tsd = kmem_zalloc(sizeof (vdev_disk_t), KM_SLEEP);

	/*
	 * When opening a disk device, we want to preserve the user's original
	 * intent.  We always want to open the device by the path the user gave
	 * us, even if it is one of multiple paths to the save device.  But we
	 * also want to be able to survive disks being removed/recabled.
	 * Therefore the sequence of opening devices is:
	 *
	 * 1. Try opening the device by path.  For legacy pools without the
	 *    'whole_disk' property, attempt to fix the path by appending 's0'.
	 *
	 * 2. If the devid of the device matches the stored value, return
	 *    success.
	 *
	 * 3. Otherwise, the device may have moved.  Try opening the device
	 *    by the devid instead.
	 *
	 */
	if (vd->vdev_devid != NULL) {
		if (ddi_devid_str_decode(vd->vdev_devid, &dvd->vd_devid,
		    &dvd->vd_minor) != 0) {
			vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
			return (EINVAL);
		}
	}

	error = EINVAL;		/* presume failure */

	if (vd->vdev_path != NULL) {
		ddi_devid_t devid;

		if (vd->vdev_wholedisk == -1ULL) {
			size_t len = strlen(vd->vdev_path) + 3;
			char *buf = kmem_alloc(len, KM_SLEEP);
			ldi_handle_t lh;

			(void) snprintf(buf, len, "%ss0", vd->vdev_path);

			if (ldi_open_by_name(buf, spa_mode, kcred,
			    &lh, zfs_li) == 0) {
				spa_strfree(vd->vdev_path);
				vd->vdev_path = buf;
				vd->vdev_wholedisk = 1ULL;
				(void) ldi_close(lh, spa_mode, kcred);
			} else {
				kmem_free(buf, len);
			}
		}

		error = ldi_open_by_name(vd->vdev_path, spa_mode, kcred,
		    &dvd->vd_lh, zfs_li);

		/*
		 * Compare the devid to the stored value.
		 */
		if (error == 0 && vd->vdev_devid != NULL &&
		    ldi_get_devid(dvd->vd_lh, &devid) == 0) {
			if (ddi_devid_compare(devid, dvd->vd_devid) != 0) {
				error = EINVAL;
				(void) ldi_close(dvd->vd_lh, spa_mode, kcred);
				dvd->vd_lh = NULL;
			}
			ddi_devid_free(devid);
		}

		/*
		 * If we succeeded in opening the device, but 'vdev_wholedisk'
		 * is not yet set, then this must be a slice.
		 */
		if (error == 0 && vd->vdev_wholedisk == -1ULL)
			vd->vdev_wholedisk = 0;
	}

	/*
	 * If we were unable to open by path, or the devid check fails, open by
	 * devid instead.
	 */
	if (error != 0 && vd->vdev_devid != NULL)
		error = ldi_open_by_devid(dvd->vd_devid, dvd->vd_minor,
		    spa_mode, kcred, &dvd->vd_lh, zfs_li);

	/*
	 * If all else fails, then try opening by physical path (if available)
	 * or the logical path (if we failed due to the devid check).  While not
	 * as reliable as the devid, this will give us something, and the higher
	 * level vdev validation will prevent us from opening the wrong device.
	 */
	if (error) {
		if (vd->vdev_physpath != NULL &&
		    (dev = ddi_pathname_to_dev_t(vd->vdev_physpath)) != ENODEV)
			error = ldi_open_by_dev(&dev, OTYP_BLK, spa_mode,
			    kcred, &dvd->vd_lh, zfs_li);

		/*
		 * Note that we don't support the legacy auto-wholedisk support
		 * as above.  This hasn't been used in a very long time and we
		 * don't need to propagate its oddities to this edge condition.
		 */
		if (error && vd->vdev_path != NULL)
			error = ldi_open_by_name(vd->vdev_path, spa_mode, kcred,
			    &dvd->vd_lh, zfs_li);
	}

	if (error)
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;

	return (error);
}

static int
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *ashift)
{
	vdev_disk_t *dvd;
	struct dk_minfo dkm;
	int error;
	dev_t dev;
	int otyp;

	error = vdev_disk_open_common(vd);
	if (error)
		return (error);

	dvd = vd->vdev_tsd;
	/*
	 * Once a device is opened, verify that the physical device path (if
	 * available) is up to date.
	 */
	if (ldi_get_dev(dvd->vd_lh, &dev) == 0 &&
	    ldi_get_otyp(dvd->vd_lh, &otyp) == 0) {
		char *physpath, *minorname;

		physpath = kmem_alloc(MAXPATHLEN, KM_SLEEP);
		minorname = NULL;
		if (ddi_dev_pathname(dev, otyp, physpath) == 0 &&
		    ldi_get_minor_name(dvd->vd_lh, &minorname) == 0 &&
		    (vd->vdev_physpath == NULL ||
		    strcmp(vd->vdev_physpath, physpath) != 0)) {
			if (vd->vdev_physpath)
				spa_strfree(vd->vdev_physpath);
			(void) strlcat(physpath, ":", MAXPATHLEN);
			(void) strlcat(physpath, minorname, MAXPATHLEN);
			vd->vdev_physpath = spa_strdup(physpath);
		}
		if (minorname)
			kmem_free(minorname, strlen(minorname) + 1);
		kmem_free(physpath, MAXPATHLEN);
	}

	/*
	 * Determine the actual size of the device.
	 */
	if (ldi_get_size(dvd->vd_lh, psize) != 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (EINVAL);
	}

	/*
	 * If we own the whole disk, try to enable disk write caching.
	 * We ignore errors because it's OK if we can't do it.
	 */
	if (vd->vdev_wholedisk == 1) {
		int wce = 1;
		(void) ldi_ioctl(dvd->vd_lh, DKIOCSETWCE, (intptr_t)&wce,
		    FKIOCTL, kcred, NULL);
	}

	/*
	 * Determine the device's minimum transfer size.
	 * If the ioctl isn't supported, assume DEV_BSIZE.
	 */
	if (ldi_ioctl(dvd->vd_lh, DKIOCGMEDIAINFO, (intptr_t)&dkm,
	    FKIOCTL, kcred, NULL) != 0)
		dkm.dki_lbsize = DEV_BSIZE;

	*ashift = highbit(MAX(dkm.dki_lbsize, SPA_MINBLOCKSIZE)) - 1;

	/*
	 * Clear the nowritecache bit, so that on a vdev_reopen() we will
	 * try again.
	 */
	vd->vdev_nowritecache = B_FALSE;

	return (0);
}

static void
vdev_disk_close(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (dvd == NULL)
		return;

	if (dvd->vd_minor != NULL)
		ddi_devid_str_free(dvd->vd_minor);

	if (dvd->vd_devid != NULL)
		ddi_devid_free(dvd->vd_devid);

	if (dvd->vd_lh != NULL)
		(void) ldi_close(dvd->vd_lh, spa_mode, kcred);

	kmem_free(dvd, sizeof (vdev_disk_t));
	vd->vdev_tsd = NULL;
}

int
vdev_disk_physio(ldi_handle_t vd_lh, caddr_t data, size_t size,
    uint64_t offset, int flags)
{
	buf_t *bp;
	int error = 0;

	if (vd_lh == NULL)
		return (EINVAL);

	ASSERT(flags & B_READ || flags & B_WRITE);

	bp = getrbuf(KM_SLEEP);
	bp->b_flags = flags | B_BUSY | B_NOCACHE | B_FAILFAST;
	bp->b_bcount = size;
	bp->b_un.b_addr = (void *)data;
	bp->b_lblkno = lbtodb(offset);
	bp->b_bufsize = size;

	error = ldi_strategy(vd_lh, bp);
	ASSERT(error == 0);
	if ((error = biowait(bp)) == 0 && bp->b_resid != 0)
		error = EIO;
	freerbuf(bp);

	return (error);
}

static int
vdev_disk_probe_io(vdev_t *vd, caddr_t data, size_t size, uint64_t offset,
    int flags)
{
	int error = 0;
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (vd == NULL || dvd == NULL || dvd->vd_lh == NULL)
		return (EINVAL);

	error = vdev_disk_physio(dvd->vd_lh, data, size, offset, flags);

	if (zio_injection_enabled && error == 0)
		error = zio_handle_device_injection(vd, EIO);

	return (error);
}

/*
 * Determine if the underlying device is accessible by reading and writing
 * to a known location. We must be able to do this during syncing context
 * and thus we cannot set the vdev state directly.
 */
static int
vdev_disk_probe(vdev_t *vd)
{
	uint64_t offset;
	vdev_t *nvd;
	int l, error = 0, retries = 0;
	char *vl_pad;

	if (vd == NULL)
		return (EINVAL);

	/* Hijack the current vdev */
	nvd = vd;

	/*
	 * Pick a random label to rewrite.
	 */
	l = spa_get_random(VDEV_LABELS);
	ASSERT(l < VDEV_LABELS);

	offset = vdev_label_offset(vd->vdev_psize, l,
	    offsetof(vdev_label_t, vl_pad));

	vl_pad = kmem_alloc(VDEV_SKIP_SIZE, KM_SLEEP);

	/*
	 * Try to read and write to a special location on the
	 * label. We use the existing vdev initially and only
	 * try to create and reopen it if we encounter a failure.
	 */
	while ((error = vdev_disk_probe_io(nvd, vl_pad, VDEV_SKIP_SIZE,
	    offset, B_READ)) != 0 && retries == 0) {

		nvd = kmem_zalloc(sizeof (vdev_t), KM_SLEEP);
		if (vd->vdev_path)
			nvd->vdev_path = spa_strdup(vd->vdev_path);
		if (vd->vdev_physpath)
			nvd->vdev_physpath = spa_strdup(vd->vdev_physpath);
		if (vd->vdev_devid)
			nvd->vdev_devid = spa_strdup(vd->vdev_devid);
		nvd->vdev_wholedisk = vd->vdev_wholedisk;
		nvd->vdev_guid = vd->vdev_guid;
		retries++;

		error = vdev_disk_open_common(nvd);
		if (error)
			break;
	}

	if (!error) {
		error = vdev_disk_probe_io(nvd, vl_pad, VDEV_SKIP_SIZE,
		    offset, B_WRITE);
	}

	/* Clean up if we allocated a new vdev */
	if (retries) {
		vdev_disk_close(nvd);
		if (nvd->vdev_path)
			spa_strfree(nvd->vdev_path);
		if (nvd->vdev_physpath)
			spa_strfree(nvd->vdev_physpath);
		if (nvd->vdev_devid)
			spa_strfree(nvd->vdev_devid);
		kmem_free(nvd, sizeof (vdev_t));
	}
	kmem_free(vl_pad, VDEV_SKIP_SIZE);

	/* Reset the failing flag */
	if (!error)
		vd->vdev_is_failing = B_FALSE;

	return (error);
}

static void
vdev_disk_io_intr(buf_t *bp)
{
	vdev_disk_buf_t *vdb = (vdev_disk_buf_t *)bp;
	zio_t *zio = vdb->vdb_io;

	if ((zio->io_error = geterror(bp)) == 0 && bp->b_resid != 0)
		zio->io_error = EIO;

	kmem_free(vdb, sizeof (vdev_disk_buf_t));

	zio_interrupt(zio);
}

static void
vdev_disk_ioctl_done(void *zio_arg, int error)
{
	zio_t *zio = zio_arg;

	zio->io_error = error;

	zio_interrupt(zio);
}

static int
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_disk_t *dvd = vd->vdev_tsd;
	vdev_disk_buf_t *vdb;
	buf_t *bp;
	int flags, error;

	if (zio->io_type == ZIO_TYPE_IOCTL) {
		zio_vdev_io_bypass(zio);

		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = ENXIO;
			return (ZIO_PIPELINE_CONTINUE);
		}

		switch (zio->io_cmd) {

		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (vd->vdev_nowritecache) {
				zio->io_error = ENOTSUP;
				break;
			}

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
				return (ZIO_PIPELINE_STOP);
			}

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

		return (ZIO_PIPELINE_CONTINUE);
	}

	if (zio->io_type == ZIO_TYPE_READ && vdev_cache_read(zio) == 0)
		return (ZIO_PIPELINE_STOP);

	if ((zio = vdev_queue_io(zio)) == NULL)
		return (ZIO_PIPELINE_STOP);

	if (zio->io_type == ZIO_TYPE_WRITE)
		error = vdev_writeable(vd) ? vdev_error_inject(vd, zio) : ENXIO;
	else
		error = vdev_readable(vd) ? vdev_error_inject(vd, zio) : ENXIO;
	error = (vd->vdev_remove_wanted || vd->vdev_is_failing) ? ENXIO : error;

	if (error) {
		zio->io_error = error;
		zio_interrupt(zio);
		return (ZIO_PIPELINE_STOP);
	}

	flags = (zio->io_type == ZIO_TYPE_READ ? B_READ : B_WRITE);
	flags |= B_BUSY | B_NOCACHE;
	if (zio->io_flags & ZIO_FLAG_FAILFAST)
		flags |= B_FAILFAST;

	vdb = kmem_alloc(sizeof (vdev_disk_buf_t), KM_SLEEP);

	vdb->vdb_io = zio;
	bp = &vdb->vdb_buf;

	bioinit(bp);
	bp->b_flags = flags;
	bp->b_bcount = zio->io_size;
	bp->b_un.b_addr = zio->io_data;
	bp->b_lblkno = lbtodb(zio->io_offset);
	bp->b_bufsize = zio->io_size;
	bp->b_iodone = (int (*)())vdev_disk_io_intr;

	error = ldi_strategy(dvd->vd_lh, bp);
	/* ldi_strategy() will return non-zero only on programming errors */
	ASSERT(error == 0);

	return (ZIO_PIPELINE_STOP);
}

static int
vdev_disk_io_done(zio_t *zio)
{
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
	}

	return (ZIO_PIPELINE_CONTINUE);
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

/*
 * Given the root disk device pathname, read the label from the device,
 * and construct a configuration nvlist.
 */
nvlist_t *
vdev_disk_read_rootlabel(char *devpath)
{
	nvlist_t *config = NULL;
	ldi_handle_t vd_lh;
	vdev_label_t *label;
	uint64_t s, size;
	int l;

	/*
	 * Read the device label and build the nvlist.
	 */
	if (ldi_open_by_name(devpath, FREAD, kcred, &vd_lh, zfs_li))
		return (NULL);

	if (ldi_get_size(vd_lh, &s))
		return (NULL);

	size = P2ALIGN_TYPED(s, sizeof (vdev_label_t), uint64_t);
	label = kmem_alloc(sizeof (vdev_label_t), KM_SLEEP);

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t offset, state, txg = 0;

		/* read vdev label */
		offset = vdev_label_offset(size, l, 0);
		if (vdev_disk_physio(vd_lh, (caddr_t)label,
		    VDEV_SKIP_SIZE + VDEV_BOOT_HEADER_SIZE +
		    VDEV_PHYS_SIZE, offset, B_READ) != 0)
			continue;

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), &config, 0) != 0) {
			config = NULL;
			continue;
		}

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state >= POOL_STATE_DESTROYED) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0) {
			nvlist_free(config);
			config = NULL;
			continue;
		}

		break;
	}

	kmem_free(label, sizeof (vdev_label_t));
	return (config);
}
