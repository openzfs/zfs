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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/abd.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/zfs_file.h>
#ifdef _KERNEL
#include <linux/falloc.h>
#endif
/*
 * Virtual device vector for files.
 */

static taskq_t *vdev_file_taskq;

/*
 * By default, the logical/physical ashift for file vdevs is set to
 * SPA_MINBLOCKSHIFT (9). This allows all file vdevs to use 512B (1 << 9)
 * blocksizes. Users may opt to change one or both of these for testing
 * or performance reasons. Care should be taken as these values will
 * impact the vdev_ashift setting which can only be set at vdev creation
 * time.
 */
static uint_t vdev_file_logical_ashift = SPA_MINBLOCKSHIFT;
static uint_t vdev_file_physical_ashift = SPA_MINBLOCKSHIFT;

static void
vdev_file_hold(vdev_t *vd)
{
	ASSERT(vd->vdev_path != NULL);
}

static void
vdev_file_rele(vdev_t *vd)
{
	ASSERT(vd->vdev_path != NULL);
}

static mode_t
vdev_file_open_mode(spa_mode_t spa_mode)
{
	mode_t mode = 0;

	if ((spa_mode & SPA_MODE_READ) && (spa_mode & SPA_MODE_WRITE)) {
		mode = O_RDWR;
	} else if (spa_mode & SPA_MODE_READ) {
		mode = O_RDONLY;
	} else if (spa_mode & SPA_MODE_WRITE) {
		mode = O_WRONLY;
	}

	return (mode | O_LARGEFILE);
}

static int
vdev_file_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_file_t *vf;
	zfs_file_t *fp;
	zfs_file_attr_t zfa;
	int error;

	/*
	 * Rotational optimizations only make sense on block devices.
	 */
	vd->vdev_nonrot = B_TRUE;

	/*
	 * Allow TRIM on file based vdevs.  This may not always be supported,
	 * since it depends on your kernel version and underlying filesystem
	 * type but it is always safe to attempt.
	 */
	vd->vdev_has_trim = B_TRUE;

	/*
	 * Disable secure TRIM on file based vdevs.  There is no way to
	 * request this behavior from the underlying filesystem.
	 */
	vd->vdev_has_securetrim = B_FALSE;

	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Reopen the device if it's not currently open.  Otherwise,
	 * just update the physical size of the device.
	 */
	if (vd->vdev_tsd != NULL) {
		ASSERT(vd->vdev_reopening);
		vf = vd->vdev_tsd;
		goto skip_open;
	}

	vf = vd->vdev_tsd = kmem_zalloc(sizeof (vdev_file_t), KM_SLEEP);

	/*
	 * We always open the files from the root of the global zone, even if
	 * we're in a local zone.  If the user has gotten to this point, the
	 * administrator has already decided that the pool should be available
	 * to local zone users, so the underlying devices should be as well.
	 */
	ASSERT(vd->vdev_path != NULL && vd->vdev_path[0] == '/');

	error = zfs_file_open(vd->vdev_path,
	    vdev_file_open_mode(spa_mode(vd->vdev_spa)), 0, &fp);
	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}

	vf->vf_file = fp;

#ifdef _KERNEL
	/*
	 * Make sure it's a regular file.
	 */
	if (zfs_file_getattr(fp, &zfa)) {
		return (SET_ERROR(ENODEV));
	}
	if (!S_ISREG(zfa.zfa_mode)) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (SET_ERROR(ENODEV));
	}
#endif

skip_open:

	error =  zfs_file_getattr(vf->vf_file, &zfa);
	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}

	*max_psize = *psize = zfa.zfa_size;
	*logical_ashift = vdev_file_logical_ashift;
	*physical_ashift = vdev_file_physical_ashift;

	return (0);
}

static void
vdev_file_close(vdev_t *vd)
{
	vdev_file_t *vf = vd->vdev_tsd;

	if (vd->vdev_reopening || vf == NULL)
		return;

	if (vf->vf_file != NULL) {
		(void) zfs_file_close(vf->vf_file);
	}

	vd->vdev_delayed_close = B_FALSE;
	kmem_free(vf, sizeof (vdev_file_t));
	vd->vdev_tsd = NULL;
}

static void
vdev_file_io_strategy(void *arg)
{
	zio_t *zio = (zio_t *)arg;
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf = vd->vdev_tsd;
	ssize_t resid;
	void *buf;
	loff_t off;
	ssize_t size;
	int err;

	off = zio->io_offset;
	size = zio->io_size;
	resid = 0;

	if (zio->io_type == ZIO_TYPE_READ) {
		buf = abd_borrow_buf(zio->io_abd, zio->io_size);
		err = zfs_file_pread(vf->vf_file, buf, size, off, &resid);
		abd_return_buf_copy(zio->io_abd, buf, size);
	} else {
		buf = abd_borrow_buf_copy(zio->io_abd, zio->io_size);
		err = zfs_file_pwrite(vf->vf_file, buf, size, off, &resid);
		abd_return_buf(zio->io_abd, buf, size);
	}
	zio->io_error = err;
	if (resid != 0 && zio->io_error == 0)
		zio->io_error = SET_ERROR(ENOSPC);

	zio_delay_interrupt(zio);
}

static void
vdev_file_io_fsync(void *arg)
{
	zio_t *zio = (zio_t *)arg;
	vdev_file_t *vf = zio->io_vd->vdev_tsd;

	zio->io_error = zfs_file_fsync(vf->vf_file, O_SYNC | O_DSYNC);

	zio_interrupt(zio);
}

static void
vdev_file_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf = vd->vdev_tsd;

	if (zio->io_type == ZIO_TYPE_IOCTL) {
		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			/*
			 * We cannot safely call vfs_fsync() when PF_FSTRANS
			 * is set in the current context.  Filesystems like
			 * XFS include sanity checks to verify it is not
			 * already set, see xfs_vm_writepage().  Therefore
			 * the sync must be dispatched to a different context.
			 */
			if (__spl_pf_fstrans_check()) {
				VERIFY3U(taskq_dispatch(vdev_file_taskq,
				    vdev_file_io_fsync, zio, TQ_SLEEP), !=,
				    TASKQID_INVALID);
				return;
			}

			zio->io_error = zfs_file_fsync(vf->vf_file,
			    O_SYNC | O_DSYNC);
			break;
		default:
			zio->io_error = SET_ERROR(ENOTSUP);
		}

		zio_execute(zio);
		return;
	} else if (zio->io_type == ZIO_TYPE_TRIM) {
		int mode = 0;

		ASSERT3U(zio->io_size, !=, 0);
#ifdef __linux__
		mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
#endif
		zio->io_error = zfs_file_fallocate(vf->vf_file,
		    mode, zio->io_offset, zio->io_size);
		zio_execute(zio);
		return;
	}

	zio->io_target_timestamp = zio_handle_io_delay(zio);

	VERIFY3U(taskq_dispatch(vdev_file_taskq, vdev_file_io_strategy, zio,
	    TQ_SLEEP), !=, TASKQID_INVALID);
}

static void
vdev_file_io_done(zio_t *zio)
{
	(void) zio;
}

vdev_ops_t vdev_file_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_file_open,
	.vdev_op_close = vdev_file_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_file_io_start,
	.vdev_op_io_done = vdev_file_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_file_hold,
	.vdev_op_rele = vdev_file_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_FILE,		/* name of this vdev type */
	.vdev_op_leaf = B_TRUE			/* leaf vdev */
};

void
vdev_file_init(void)
{
	vdev_file_taskq = taskq_create("z_vdev_file", MAX(boot_ncpus, 16),
	    minclsyspri, boot_ncpus, INT_MAX, TASKQ_DYNAMIC);

	VERIFY(vdev_file_taskq);
}

void
vdev_file_fini(void)
{
	taskq_destroy(vdev_file_taskq);
}

/*
 * From userland we access disks just like files.
 */
#ifndef _KERNEL

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_file_open,
	.vdev_op_close = vdev_file_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_file_io_start,
	.vdev_op_io_done = vdev_file_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_file_hold,
	.vdev_op_rele = vdev_file_rele,
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

#endif

ZFS_MODULE_PARAM(zfs_vdev_file, vdev_file_, logical_ashift, UINT, ZMOD_RW,
	"Logical ashift for file-based devices");
ZFS_MODULE_PARAM(zfs_vdev_file, vdev_file_, physical_ashift, UINT, ZMOD_RW,
	"Physical ashift for file-based devices");
