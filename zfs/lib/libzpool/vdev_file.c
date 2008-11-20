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

#pragma ident	"@(#)vdev_file.c	1.7	07/11/27 SMI"

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_file.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>

/*
 * Virtual device vector for files.
 */

static int
vdev_file_open_common(vdev_t *vd)
{
	vdev_file_t *vf;
	vnode_t *vp;
	int error;

	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (EINVAL);
	}

	vf = vd->vdev_tsd = kmem_zalloc(sizeof (vdev_file_t), KM_SLEEP);

	/*
	 * We always open the files from the root of the global zone, even if
	 * we're in a local zone.  If the user has gotten to this point, the
	 * administrator has already decided that the pool should be available
	 * to local zone users, so the underlying devices should be as well.
	 */
	ASSERT(vd->vdev_path != NULL && vd->vdev_path[0] == '/');
	error = vn_openat(vd->vdev_path + 1, UIO_SYSSPACE,
	    spa_mode | FOFFMAX, 0, &vp, 0, 0, rootdir, -1);

	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}

	vf->vf_vnode = vp;

#ifdef _KERNEL
	/*
	 * Make sure it's a regular file.
	 */
	if (vp->v_type != VREG) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (ENODEV);
	}
#endif

	return (0);
}

static int
vdev_file_open(vdev_t *vd, uint64_t *psize, uint64_t *ashift)
{
	vdev_file_t *vf;
	vattr_t vattr;
	int error;

	if ((error = vdev_file_open_common(vd)) != 0)
		return (error);

	vf = vd->vdev_tsd;

	/*
	 * Determine the physical size of the file.
	 */
	vattr.va_mask = AT_SIZE;
	error = VOP_GETATTR(vf->vf_vnode, &vattr, 0, kcred, NULL);
	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}

	*psize = vattr.va_size;
	*ashift = SPA_MINBLOCKSHIFT;

	return (0);
}

static void
vdev_file_close(vdev_t *vd)
{
	vdev_file_t *vf = vd->vdev_tsd;

	if (vf == NULL)
		return;

	if (vf->vf_vnode != NULL) {
		(void) VOP_PUTPAGE(vf->vf_vnode, 0, 0, B_INVAL, kcred, NULL);
		(void) VOP_CLOSE(vf->vf_vnode, spa_mode, 1, 0, kcred, NULL);
		VN_RELE(vf->vf_vnode);
	}

	kmem_free(vf, sizeof (vdev_file_t));
	vd->vdev_tsd = NULL;
}

static int
vdev_file_probe_io(vdev_t *vd, caddr_t data, size_t size, uint64_t offset,
    enum uio_rw rw)
{
	vdev_file_t *vf = vd->vdev_tsd;
	ssize_t resid;
	int error = 0;

	if (vd == NULL || vf == NULL || vf->vf_vnode == NULL)
		return (EINVAL);

	ASSERT(rw == UIO_READ || rw ==  UIO_WRITE);

	error = vn_rdwr(rw, vf->vf_vnode, data, size, offset, UIO_SYSSPACE,
	    0, RLIM64_INFINITY, kcred, &resid);
	if (error || resid != 0)
		return (EIO);
	return (0);
}

/*
 * Determine if the underlying device is accessible by reading and writing
 * to a known location. We must be able to do this during syncing context
 * and thus we cannot set the vdev state directly.
 */
static int
vdev_file_probe(vdev_t *vd)
{
	vdev_t *nvd;
	char *vl_boot;
	uint64_t offset;
	int l, error = 0, retries = 0;

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
	    offsetof(vdev_label_t, vl_boot_header));

	vl_boot = kmem_alloc(VDEV_BOOT_HEADER_SIZE, KM_SLEEP);

	while ((error = vdev_file_probe_io(nvd, vl_boot, VDEV_BOOT_HEADER_SIZE,
	    offset, UIO_READ)) != 0 && retries == 0) {

		/*
		 * If we failed with the vdev that was passed in then
		 * try allocating a new one and try again.
		 */
		nvd = kmem_zalloc(sizeof (vdev_t), KM_SLEEP);
		if (vd->vdev_path)
			nvd->vdev_path = spa_strdup(vd->vdev_path);
		retries++;

		error = vdev_file_open_common(nvd);
		if (error)
			break;
	}

	if ((spa_mode & FWRITE) && !error) {
		error = vdev_file_probe_io(nvd, vl_boot, VDEV_BOOT_HEADER_SIZE,
		    offset, UIO_WRITE);
	}

	if (retries) {
		vdev_file_close(nvd);
		if (nvd->vdev_path)
			spa_strfree(nvd->vdev_path);
		kmem_free(nvd, sizeof (vdev_t));
	}
	kmem_free(vl_boot, VDEV_BOOT_HEADER_SIZE);

	if (!error)
		vd->vdev_is_failing = B_FALSE;

	return (error);
}

static int
vdev_file_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf = vd->vdev_tsd;
	ssize_t resid;
	int error;

	if (zio->io_type == ZIO_TYPE_IOCTL) {
		zio_vdev_io_bypass(zio);

		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = ENXIO;
			return (ZIO_PIPELINE_CONTINUE);
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:
			zio->io_error = VOP_FSYNC(vf->vf_vnode, FSYNC | FDSYNC,
			    kcred, NULL);
			dprintf("fsync(%s) = %d\n", vdev_description(vd),
			    zio->io_error);
			break;
		default:
			zio->io_error = ENOTSUP;
		}

		return (ZIO_PIPELINE_CONTINUE);
	}

	/*
	 * In the kernel, don't bother double-caching, but in userland,
	 * we want to test the vdev_cache code.
	 */
#ifndef _KERNEL
	if (zio->io_type == ZIO_TYPE_READ && vdev_cache_read(zio) == 0)
		return (ZIO_PIPELINE_STOP);
#endif

	if ((zio = vdev_queue_io(zio)) == NULL)
		return (ZIO_PIPELINE_STOP);

	/* XXPOLICY */
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

	zio->io_error = vn_rdwr(zio->io_type == ZIO_TYPE_READ ?
	    UIO_READ : UIO_WRITE, vf->vf_vnode, zio->io_data,
	    zio->io_size, zio->io_offset, UIO_SYSSPACE,
	    0, RLIM64_INFINITY, kcred, &resid);

	if (resid != 0 && zio->io_error == 0)
		zio->io_error = ENOSPC;

	zio_interrupt(zio);

	return (ZIO_PIPELINE_STOP);
}

static int
vdev_file_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;

	if (zio_injection_enabled && zio->io_error == 0)
		zio->io_error = zio_handle_device_injection(vd, EIO);

	/*
	 * If an error has been encountered then attempt to probe the device
	 * to determine if it's still accessible.
	 */
	if (zio->io_error == EIO && vdev_probe(vd) != 0)
		vd->vdev_is_failing = B_TRUE;

	vdev_queue_io_done(zio);

#ifndef _KERNEL
	if (zio->io_type == ZIO_TYPE_WRITE)
		vdev_cache_write(zio);
#endif

	return (ZIO_PIPELINE_CONTINUE);
}

vdev_ops_t vdev_file_ops = {
	vdev_file_open,
	vdev_file_close,
	vdev_file_probe,
	vdev_default_asize,
	vdev_file_io_start,
	vdev_file_io_done,
	NULL,
	VDEV_TYPE_FILE,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

/*
 * From userland we access disks just like files.
 */
#ifndef _KERNEL

vdev_ops_t vdev_disk_ops = {
	vdev_file_open,
	vdev_file_close,
	vdev_file_probe,
	vdev_default_asize,
	vdev_file_io_start,
	vdev_file_io_done,
	NULL,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

#endif
