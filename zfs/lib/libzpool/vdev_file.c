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

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_file.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>

/*
 * Virtual device vector for files.
 */

static int
vdev_file_open(vdev_t *vd, uint64_t *psize, uint64_t *ashift)
{
	vdev_file_t *vf;
	vnode_t *vp;
	vattr_t vattr;
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
vdev_file_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf = vd->vdev_tsd;
	ssize_t resid;

	if (zio->io_type == ZIO_TYPE_IOCTL) {
		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = ENXIO;
			return (ZIO_PIPELINE_CONTINUE);
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:
			zio->io_error = VOP_FSYNC(vf->vf_vnode, FSYNC | FDSYNC,
			    kcred, NULL);
			break;
		default:
			zio->io_error = ENOTSUP;
		}

		return (ZIO_PIPELINE_CONTINUE);
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

/* ARGSUSED */
static void
vdev_file_io_done(zio_t *zio)
{
}

vdev_ops_t vdev_file_ops = {
	vdev_file_open,
	vdev_file_close,
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
	vdev_default_asize,
	vdev_file_io_start,
	vdev_file_io_done,
	NULL,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

#endif
