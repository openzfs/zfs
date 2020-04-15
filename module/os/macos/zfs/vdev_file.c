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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2016 by Delphix. All rights reserved.
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
static unsigned long vdev_file_logical_ashift = SPA_MINBLOCKSHIFT;
static unsigned long vdev_file_physical_ashift = SPA_MINBLOCKSHIFT;

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
	int error = 0;

	dprintf("vdev_file_open %p\n", vd->vdev_tsd);

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
#ifdef _KERNEL
	if (vd->vdev_tsd != NULL) {
		vf = vd->vdev_tsd;
		if (vf->vf_file != NULL) {
			ASSERT(vd->vdev_reopening);
			vf = vd->vdev_tsd;
			goto skip_open;
		}
	}
#endif

	if (vd->vdev_tsd == NULL)
		vf = vd->vdev_tsd = kmem_zalloc(sizeof (vdev_file_t), KM_SLEEP);

	ASSERT(vd->vdev_path != NULL && vd->vdev_path[0] == '/');

	error = zfs_file_open(vd->vdev_path,
	    vdev_file_open_mode(spa_mode(vd->vdev_spa)), 0, &fp);

	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}

	vf->vf_file = fp;

	/*
	 * Make sure it's a regular file.
	 */
	if (zfs_file_getattr(fp, &zfa)) {
		return (SET_ERROR(ENODEV));
	}

skip_open:
	/*
	 * Determine the physical size of the file.
	 */
	error = zfs_file_getattr(vf->vf_file, &zfa);

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
		zfs_file_close(vf->vf_file);
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
	loff_t off;
	void *data;
	ssize_t size;
	int err;

	off = zio->io_offset;
	size = zio->io_size;
	resid = 0;

	if (zio->io_type == ZIO_TYPE_READ) {
		data =
		    abd_borrow_buf(zio->io_abd, size);
		err = zfs_file_pread(vf->vf_file, data, size, off, &resid);
		abd_return_buf_copy(zio->io_abd, data, size);
	} else {
		data =
		    abd_borrow_buf_copy(zio->io_abd, size);
		err = zfs_file_pwrite(vf->vf_file, data, size, off, &resid);
		abd_return_buf(zio->io_abd, data, size);
	}

	zio->io_error = (err != 0 ? EIO : 0);

	if (zio->io_error == 0 && resid != 0)
		zio->io_error = SET_ERROR(ENOSPC);

	zio_delay_interrupt(zio);
}

static void
vdev_file_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf = vd->vdev_tsd;

#ifdef CLOSE_ON_UNMOUNT
	if (vf->vf_file == NULL) {
		zfs_file_t *fp = NULL;
		int error;

		error = zfs_file_open(vd->vdev_path,
		    vdev_file_open_mode(spa_mode(vd->vdev_spa)), 0, &fp);

		if (error == 0) {
			atomic_cas_ptr(&vf->vf_file, NULL, fp);
			if (vf->vf_file != fp)
				zfs_file_close(fp); /* We lost */
		}

		if (vf->vf_file == NULL) {
			zio->io_error = SET_ERROR(EIO);
			zio_delay_interrupt(zio);
			return;
		}
	}
#endif

	if (zio->io_type == ZIO_TYPE_IOCTL) {

		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		switch (zio->io_cmd) {
			case DKIOCFLUSHWRITECACHE:
				zio->io_error = zfs_file_fsync(vf->vf_file,
				    O_SYNC|O_DSYNC);
				break;
			default:
				zio->io_error = SET_ERROR(ENOTSUP);
		}

		zio_execute(zio);
		return;
	} else if (zio->io_type == ZIO_TYPE_TRIM) {
		int mode = 0;

		ASSERT3U(zio->io_size, !=, 0);

		/* XXX FreeBSD has no fallocate routine in file ops */
		zio->io_error = zfs_file_fallocate(vf->vf_file,
		    mode, zio->io_offset, zio->io_size);
		zio_execute(zio);
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE);
	zio->io_target_timestamp = zio_handle_io_delay(zio);

	VERIFY3U(taskq_dispatch(vdev_file_taskq, vdev_file_io_strategy, zio,
	    TQ_SLEEP), !=, 0);
}


static void
vdev_file_io_done(zio_t *zio)
{
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
	.vdev_op_type = VDEV_TYPE_FILE,	/* name of this vdev type */
	.vdev_op_leaf = B_TRUE		/* leaf vdev */
};

extern void vdev_disk_init(void);
extern void vdev_disk_fini(void);

void
vdev_file_init(void)
{
	vdev_file_taskq = taskq_create("vdev_file_taskq", 100, minclsyspri,
	    max_ncpus, INT_MAX, TASKQ_PREPOPULATE | TASKQ_THREADS_CPU_PCT);

	VERIFY(vdev_file_taskq);

	vdev_disk_init();
}

void
vdev_file_fini(void)
{
	vdev_disk_fini();

	taskq_destroy(vdev_file_taskq);
}

#ifdef CLOSE_ON_UNMOUNT
/*
 * This sucks!
 *
 * So vdev_file opens the file on an underlying file-system, which
 * means calling vnode_open("diskimage"), this will then hold a
 * v_usecount during the time that the pool is open (imported).
 * When it comes time to "reboot" the system, it only issues
 * unmount to all mounted file-systems. This is not an export.
 * The pool still has v_usecount on "diskimage", so vflush() of
 * that file-system can not complete, and we hang waiting forever
 * for the usecount to go down (.. and all file-systems to be unmounted)
 *
 * Sadly Apple has left us no way to know when the system is in
 * reboot/shutdown.
 *
 * So now, any unmount request for a dataset, it will run through
 * all the vdevs (in that pool), and if the vdev is one of "vdev_file",
 * we will close the underlying opened file. We do this "dirty".
 *
 * The next call to vdev_file_io_start() will notice the file is
 * not open, and reopen it.
 *
 * In theory, vdev_file pools are not common, and unmounting is not
 * common, so the penalty might not be too bad. Compared to reboots
 * that hang.
 *
 * vdev_disk gets away with it, as all open disks are in /dev/ mount,
 * which is not unmounted at reboot. (virtual filesystem).
 *
 * Linux and FreeBSD appear to get away with it by using
 * file-descriptor equivalent opens, which is flushed at the start of
 * reboots. Apple will not let us open files this way from within the
 * kernel.
 *
 */
static uint32_t vdev_file_close_on_unmount = 1;
ZFS_MODULE_PARAM(zfs_vdev_file, vdev_file_, close_on_unmount, UINT, ZMOD_RW,
	"close vdevs on unmount to avoid reboot hang");

static void
vdev_file_close_all_impl(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf) {
		if (vd->vdev_ops == &vdev_file_ops) {
			vdev_file_t *vf;
			vf = vd->vdev_tsd;
			zfs_file_t *fp = vf->vf_file;
			if (fp != NULL) {
				atomic_cas_ptr(&vf->vf_file, fp, NULL);
				if (vf->vf_file == NULL) {
					zfs_file_close(fp);
					printf("closed '%s' "
					    "(close_on_unmount)\n",
					    vd->vdev_path);
				}
			}
		}
		return;
	}

	for (int c = 0; c < vd->vdev_children; c++)
		vdev_file_close_all_impl(vd->vdev_child[c]);
}

void
vdev_file_close_all(objset_t *os)
{
	spa_t *spa = dmu_objset_spa(os);
	if (!vdev_file_close_on_unmount)
		return;
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	vdev_file_close_all_impl(spa->spa_root_vdev);
	spa_config_exit(spa, SCL_VDEV, FTAG);
}
#endif

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
	.vdev_op_type = VDEV_TYPE_DISK,	/* name of this vdev type */
	.vdev_op_leaf = B_TRUE		/* leaf vdev */
};

#endif

ZFS_MODULE_PARAM(zfs_vdev_file, vdev_file_, logical_ashift, ULONG, ZMOD_RW,
	"Logical ashift for file-based devices");
ZFS_MODULE_PARAM(zfs_vdev_file, vdev_file_, physical_ashift, ULONG, ZMOD_RW,
	"Physical ashift for file-based devices");
