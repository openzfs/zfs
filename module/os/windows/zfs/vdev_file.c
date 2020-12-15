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
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/types.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/vnode.h>
#include <fcntl.h>

/*
 * Virtual device vector for files.
 */

static taskq_t *vdev_file_taskq;

#ifdef _KERNEL
extern void UnlockAndFreeMdl(PMDL);
#endif

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

#ifdef _KERNEL
extern int VOP_GETATTR(struct vnode *vp, vattr_t *vap, int flags, void *x3, void *x4);
#endif

static mode_t vdev_file_open_mode(spa_mode_t spa_mode)
{
	mode_t mode = 0;
	// TODO :- Add flags
	if ((spa_mode & SPA_MODE_READ) && (spa_mode & SPA_MODE_WRITE)) {
		mode = O_RDWR;
	}
	else if (spa_mode & SPA_MODE_READ) {
		mode = O_RDONLY;
	}
	else if (spa_mode & SPA_MODE_WRITE) {
		mode = O_WRONLY;
	}
	return mode;
}

static int
vdev_file_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *ashift, uint64_t *physical_ashift)
{
	vdev_file_t *vf;
	zfs_file_t *fp;
	zfs_file_attr_t zfa;
	int error = 0;

	dprintf("vdev_file_open %p\n", vd->vdev_tsd);
	/* Rotational optimizations only make sense on block devices */
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
	if (vd->vdev_path == NULL || (vd->vdev_path[0] != '/' &&
		vd->vdev_path[0] != '\\')) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Reopen the device if it's not currently open.  Otherwise,
	 * just update the physical size of the device.
	 */
#ifdef _KERNEL
	if (vd->vdev_tsd != NULL) {
		ASSERT(vd->vdev_reopening);
		vf = vd->vdev_tsd;
		goto skip_open;
	}
#endif

	vf = vd->vdev_tsd = kmem_zalloc(sizeof(vdev_file_t), KM_SLEEP);


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

#ifdef _KERNEL
	// Change it to SPARSE, so TRIM might work
	error = ZwFsControlFile(
		fp->f_handle,
		NULL,
		NULL,
		NULL,
		NULL,
		FSCTL_SET_SPARSE,
		NULL,
		0,
		NULL,
		0
	);
	dprintf("%s: set Sparse 0x%x.\n", __func__, error);
#else
	// Userland?
#endif

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
	*ashift = SPA_MINBLOCKSHIFT;
	*physical_ashift = SPA_MINBLOCKSHIFT;

	return (0);
}

static void
vdev_file_close(vdev_t *vd)
{
	vdev_file_t *vf = vd->vdev_tsd;

	if (vd->vdev_reopening || vf == NULL)
		return;


	// Release our holds
	if (vf->vf_file != NULL) {
		zfs_file_close(vf->vf_file);
	}

	vd->vdev_delayed_close = B_FALSE;
	kmem_free(vf, sizeof (vdev_file_t));
	vd->vdev_tsd = NULL;
}

#ifdef _KERNEL
struct vdev_file_callback_struct {
	zio_t *zio;
	PIRP irp;
	void *b_data;
	char work_item[0];
};
typedef struct vdev_file_callback_struct vf_callback_t;

static void
vdev_file_io_start_done(void *param)
{
	vf_callback_t *vb = (vf_callback_t *)param;

	ASSERT(vb != NULL);

	NTSTATUS status = vb->irp->IoStatus.Status;
	zio_t *zio = vb->zio;
	zio->io_error = (!NT_SUCCESS(status) ? EIO : 0);

	// Return abd buf
	if (zio->io_type == ZIO_TYPE_READ) {
		abd_return_buf_copy(zio->io_abd, vb->b_data,
			zio->io_size);
	} else {
		abd_return_buf(zio->io_abd, vb->b_data,
			zio->io_size);
	}

	UnlockAndFreeMdl(vb->irp->MdlAddress);
	IoFreeIrp(vb->irp);
	kmem_free(vb, sizeof(vf_callback_t) + IoSizeofWorkItem());
	vb = NULL;
	zio_delay_interrupt(zio);
}

static VOID
FileIoWkRtn(
	__in PVOID           pDummy,           // Not used.
	__in PVOID           pWkParms          // Parm list pointer.
)
{
	vf_callback_t *vb = (vf_callback_t *)pWkParms;

	UNREFERENCED_PARAMETER(pDummy);
	IoUninitializeWorkItem((PIO_WORKITEM)vb->work_item);
	vdev_file_io_start_done(vb);
}

static NTSTATUS
vdev_file_io_intrxxx(PDEVICE_OBJECT DeviceObject, PIRP irp, PVOID Context)
{
	vf_callback_t *vb = (vf_callback_t *)Context;

	ASSERT(vb != NULL);

	/* If IRQL is below DIPATCH_LEVEL then there is no issue in calling
	 * vdev_file_io_start_done() directly; otherwise queue a new Work Item
	*/
	if (KeGetCurrentIrql() < DISPATCH_LEVEL)
		vdev_file_io_start_done(vb);
	else {
		vdev_file_t *vf = vb->zio->io_vd->vdev_tsd;
		zfs_file_t *fp = vf->vf_file;
		IoInitializeWorkItem(fp->f_deviceobject, (PIO_WORKITEM)vb->work_item);
		IoQueueWorkItem((PIO_WORKITEM)vb->work_item, FileIoWkRtn, DelayedWorkQueue, vb);
	}

	return STATUS_MORE_PROCESSING_REQUIRED;
}
#endif

static void
vdev_file_io_strategy(void *arg)
{
	zio_t *zio = (zio_t *)arg;
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf = vd->vdev_tsd;
	zfs_file_t *fp = vf->vf_file;
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
	ssize_t resid = 0;
	vdev_file_t* vf = vd->vdev_tsd;
	zfs_file_t *fp = vf->vf_file;

	if (zio->io_type == ZIO_TYPE_IOCTL) {

		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
	        }

	        switch (zio->io_cmd) {
	        case DKIOCFLUSHWRITECACHE:
			zio->io_error = zfs_file_fsync(fp, 0); 
			break;
	        default:
			zio->io_error = SET_ERROR(ENOTSUP);
		}

		zio_execute(zio);
	        return;

	} else if (zio->io_type == ZIO_TYPE_TRIM) {
		int mode = 0;
		zio->io_error = zfs_file_fallocate(vf->vf_file,
		    mode, zio->io_offset, zio->io_size);
		zio_execute(zio);
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE);
	zio->io_target_timestamp = zio_handle_io_delay(zio);


	ASSERT(zio->io_size != 0);
	LARGE_INTEGER offset;
	offset.QuadPart = zio->io_offset /*+ vd->vdev_win_offset */;

	VERIFY3U(taskq_dispatch(system_taskq, vdev_file_io_strategy, zio,
	    TQ_SLEEP), !=, 0);
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
	NULL,
	vdev_file_hold,
	vdev_file_rele,
	NULL,
	vdev_default_xlate,
	VDEV_TYPE_FILE,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

void
vdev_file_init(void)
{
	vdev_file_taskq = taskq_create("vdev_file_taskq", 100, minclsyspri,
	    max_ncpus, INT_MAX, TASKQ_PREPOPULATE | TASKQ_THREADS_CPU_PCT);

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
	vdev_file_open,
	vdev_file_close,
	vdev_default_asize,
	vdev_file_io_start,
	vdev_file_io_done,
	NULL,
	NULL,
	vdev_file_hold,
	vdev_file_rele,
	NULL,
	vdev_default_xlate,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};

#endif
