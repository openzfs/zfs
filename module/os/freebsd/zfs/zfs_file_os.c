// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_recv.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_os.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_file.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/file.h>
#include <geom/geom.h>
#include <sys/stat.h>

SYSCTL_DECL(_vfs_zfs_vdev);
/* Don't send BIO_FLUSH. */
static int zfs_file_bio_flush_disable;
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, file_bio_flush_disable, CTLFLAG_RWTUN,
	&zfs_file_bio_flush_disable, 0, "Disable vdev_file BIO_FLUSH");
/* Don't send BIO_DELETE. */
static int zfs_file_bio_delete_disable;
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, file_bio_delete_disable, CTLFLAG_RWTUN,
	&zfs_file_bio_delete_disable, 0, "Disable vdev_file BIO_DELETE");

int
zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fpp)
{
	struct thread *td;
	struct vnode *vp;
	struct file *fp;
	struct nameidata nd;
	int error;

	td = curthread;
	pwd_ensure_dirs();

	KASSERT((flags & (O_EXEC | O_PATH)) == 0,
	    ("invalid flags: 0x%x", flags));
	KASSERT((flags & O_ACCMODE) != O_ACCMODE,
	    ("invalid flags: 0x%x", flags));
	flags = FFLAGS(flags);

	error = falloc_noinstall(td, &fp);
	if (error != 0) {
		return (error);
	}
	fp->f_flag = flags & FMASK;

#if __FreeBSD_version >= 1400043
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path);
#else
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path, td);
#endif
	error = vn_open(&nd, &flags, mode, fp);
	if (error != 0) {
		falloc_abort(td, fp);
		return (SET_ERROR(error));
	}
	NDFREE_PNBUF(&nd);
	vp = nd.ni_vp;
	fp->f_vnode = vp;
	if (fp->f_ops == &badfileops) {
		finit_vnode(fp, flags, NULL, &vnops);
	}
	VOP_UNLOCK(vp);
	if (vp->v_type != VREG && !IS_DEVVP(vp)) {
		zfs_file_close(fp);
		return (SET_ERROR(EACCES));
	}

	if (!IS_DEVVP(vp) && flags & O_TRUNC) {
		error = fo_truncate(fp, 0, td->td_ucred, td);
		if (error != 0) {
			zfs_file_close(fp);
			return (SET_ERROR(error));
		}
	}

	*fpp = fp;

	return (0);
}

void
zfs_file_close(zfs_file_t *fp)
{
	fdrop(fp, curthread);
}

static int
zfs_file_write_impl(zfs_file_t *fp, const void *buf, size_t count, loff_t *offp,
    ssize_t *resid)
{
	ssize_t rc;
	struct uio auio;
	struct thread *td;
	struct iovec aiov;

	td = curthread;
	aiov.iov_base = (void *)(uintptr_t)buf;
	aiov.iov_len = count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_resid = count;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;
	auio.uio_offset = *offp;

	if ((fp->f_flag & FWRITE) == 0)
		return (SET_ERROR(EBADF));

	if (fp->f_type == DTYPE_VNODE)
		bwillwrite();

	rc = fo_write(fp, &auio, td->td_ucred, FOF_OFFSET, td);
	if (rc)
		return (SET_ERROR(rc));
	if (resid)
		*resid = auio.uio_resid;
	else if (auio.uio_resid)
		return (SET_ERROR(EIO));
	*offp += count - auio.uio_resid;
	return (rc);
}

int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
	loff_t off = fp->f_offset;
	ssize_t rc;

	rc = zfs_file_write_impl(fp, buf, count, &off, resid);
	if (rc == 0)
		fp->f_offset = off;

	return (SET_ERROR(rc));
}

int
zfs_file_pwrite(zfs_file_t *fp, const void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	return (zfs_file_write_impl(fp, buf, count, &off, resid));
}

static int
zfs_file_read_impl(zfs_file_t *fp, void *buf, size_t count, loff_t *offp,
    ssize_t *resid)
{
	ssize_t rc;
	struct uio auio;
	struct thread *td;
	struct iovec aiov;

	td = curthread;
	aiov.iov_base = (void *)(uintptr_t)buf;
	aiov.iov_len = count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_resid = count;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;
	auio.uio_offset = *offp;

	if ((fp->f_flag & FREAD) == 0)
		return (SET_ERROR(EBADF));

	rc = fo_read(fp, &auio, td->td_ucred, FOF_OFFSET, td);
	if (rc)
		return (SET_ERROR(rc));
	if (resid)
		*resid = auio.uio_resid;
	*offp += count - auio.uio_resid;
	return (SET_ERROR(0));
}

int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	loff_t off = fp->f_offset;
	ssize_t rc;

	rc = zfs_file_read_impl(fp, buf, count, &off, resid);
	if (rc == 0)
		fp->f_offset = off;
	return (rc);
}

int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	return (zfs_file_read_impl(fp, buf, count, &off, resid));
}

int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
	int rc;
	struct thread *td;

	td = curthread;
	if ((fp->f_ops->fo_flags & DFLAG_SEEKABLE) == 0)
		return (SET_ERROR(ESPIPE));
	rc = fo_seek(fp, *offp, whence, td);
	if (rc == 0)
		*offp = td->td_uretoff.tdu_off;
	return (SET_ERROR(rc));
}

/*
 * Callback to translate the ABD segment into array of physical pages.
 */
static int
zfs_file_fill_unmap_cb(void *buf, size_t len, void *priv)
{
	struct bio *bp = priv;
	vm_offset_t addr = (vm_offset_t)buf;
	vm_offset_t end = addr + len;

	if (bp->bio_ma_n == 0) {
		bp->bio_ma_offset = addr & PAGE_MASK;
		addr &= ~PAGE_MASK;
	} else {
		ASSERT0(P2PHASE(addr, PAGE_SIZE));
	}
	do {
		bp->bio_ma[bp->bio_ma_n++] =
		    PHYS_TO_VM_PAGE(pmap_kextract(addr));
		addr += PAGE_SIZE;
	} while (addr < end);
	return (0);
}

static void
zfs_file_io_intr(struct bio *bp)
{
	vdev_t *vd;
	zio_t *zio;

	zio = bp->bio_caller1;
	vd = zio->io_vd;
	zio->io_error = bp->bio_error;
	if (zio->io_error == 0 && bp->bio_resid != 0)
		zio->io_error = SET_ERROR(EIO);

	switch (zio->io_error) {
	case ENXIO:
		if (!vd->vdev_remove_wanted) {
			if (bp->bio_to->error != 0) {
				vd->vdev_remove_wanted = B_TRUE;
				spa_async_request(zio->io_spa,
				    SPA_ASYNC_REMOVE);
			} else if (!vd->vdev_delayed_close) {
				vd->vdev_delayed_close = B_TRUE;
			}
		}
		break;
	}

	/*
	 * We have to split bio freeing into two parts, because the ABD code
	 * cannot be called in this context and vdev_op_io_done is not called
	 * for ZIO_TYPE_FLUSH zio-s.
	 */
	if (zio->io_type != ZIO_TYPE_READ && zio->io_type != ZIO_TYPE_WRITE) {
		g_destroy_bio(bp);
		zio->io_bio = NULL;
	}
	zio_delay_interrupt(zio);
}

struct zfs_file_check_unmapped_cb_state {
	int	pages;
	uint_t	end;
};

/*
 * Callback to check the ABD segment size/alignment and count the pages.
 */
static int
zfs_file_check_unmapped_cb(void *buf, size_t len, void *priv)
{
	struct zfs_file_check_unmapped_cb_state *s = priv;
	vm_offset_t off = (vm_offset_t)buf & PAGE_MASK;

	if (s->pages != 0 && off != 0)
		return (1);
	if (s->end != 0)
		return (1);
	s->end = (off + len) & PAGE_MASK;
	s->pages += (off + len + PAGE_MASK) >> PAGE_SHIFT;
	return (0);
}

/*
 * Check whether we can use unmapped I/O for this ZIO on this device to
 * avoid data copying between scattered and/or gang ABD buffer and linear.
 */
static int
zfs_file_check_unmapped(zio_t *zio)
{
	struct zfs_file_check_unmapped_cb_state s;

	/* If unmapped I/O is administratively disabled, respect that. */
	if (!unmapped_buf_allowed)
		return (0);

	/* If the buffer is already linear, then nothing to do here. */
	if (abd_is_linear(zio->io_abd))
		return (0);

	/* Check the buffer chunks sizes/alignments and count pages. */
	s.pages = s.end = 0;
	if (abd_iterate_func(zio->io_abd, 0, zio->io_size,
	    zfs_file_check_unmapped_cb, &s))
		return (0);
	return (s.pages);
}

void
zfs_file_io_strategy(zfs_file_t *fp, void *arg)
{
	vdev_t *vd;
	zio_t *zio = arg;
	struct vdev_data *vdp;
	struct bio *bp;
	struct cdevsw *csw;
	struct cdev *dev;
	int ref;

	vd = zio->io_vd;

	if (zio->io_type == ZIO_TYPE_FLUSH) {
		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		if (zfs_nocacheflush || zfs_file_bio_flush_disable) {
			zio_execute(zio);
			return;
		}

		if (vd->vdev_nowritecache) {
			zio->io_error = SET_ERROR(ENOTSUP);
			zio_execute(zio);
			return;
		}
	} else if (zio->io_type == ZIO_TYPE_TRIM) {
		if (zfs_file_bio_delete_disable) {
			zio_execute(zio);
			return;
		}
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ ||
	    zio->io_type == ZIO_TYPE_WRITE ||
	    zio->io_type == ZIO_TYPE_TRIM ||
	    zio->io_type == ZIO_TYPE_FLUSH);

	vdp = vd->vdev_tsd;
	if (vdp == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}

	bp = g_alloc_bio();
	bp->bio_caller1 = zio;
	switch (zio->io_type) {
	case ZIO_TYPE_READ:
	case ZIO_TYPE_WRITE:
		zio->io_target_timestamp = zio_handle_io_delay(zio);
		bp->bio_offset = zio->io_offset;
		bp->bio_bcount = bp->bio_length = zio->io_size;
		if (zio->io_type == ZIO_TYPE_READ)
			bp->bio_cmd = BIO_READ;
		else
			bp->bio_cmd = BIO_WRITE;

		/*
		 * If possible, represent scattered and/or gang ABD buffer
		 * as an array of physical pages.  It allows to satisfy
		 * requirement of virtually contiguous buffer without copying.
		 */
		int pgs = zfs_file_check_unmapped(zio);
		if (pgs > 0) {
			bp->bio_ma = malloc(sizeof (struct vm_page *) * pgs,
			    M_DEVBUF, M_WAITOK);
			bp->bio_ma_n = 0;
			bp->bio_ma_offset = 0;
			abd_iterate_func(zio->io_abd, 0, zio->io_size,
			    zfs_file_fill_unmap_cb, bp);
			bp->bio_data = unmapped_buf;
			bp->bio_flags |= BIO_UNMAPPED;
		} else {
			if (zio->io_type == ZIO_TYPE_READ) {
				bp->bio_data = abd_borrow_buf(zio->io_abd,
				    zio->io_size);
			} else {
				bp->bio_data = abd_borrow_buf_copy(zio->io_abd,
				    zio->io_size);
			}
		}
		break;
	case ZIO_TYPE_TRIM:
		bp->bio_cmd = BIO_DELETE;
		bp->bio_data = NULL;
		bp->bio_offset = zio->io_offset;
		bp->bio_length = zio->io_size;
		break;
	case ZIO_TYPE_FLUSH:
		bp->bio_cmd = BIO_FLUSH;
		bp->bio_data = NULL;
		bp->bio_offset = vd->vdev_asize;
		bp->bio_length = 0;
		break;
	default:
		panic("invalid zio->io_type: %d\n", zio->io_type);
	}
	bp->bio_done = zfs_file_io_intr;
	zio->io_bio = bp;
	csw = devvn_refthread(fp->f_vnode, &dev, &ref);
	if (csw == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}
	bp->bio_dev = dev;
	csw->d_strategy(bp);
	dev_relthread(dev, ref);
}

void
zfs_file_io_strategy_done(zfs_file_t *fp, void *arg)
{
	zio_t *zio = arg;
	struct bio *bp = zio->io_bio;

	if (zio->io_type != ZIO_TYPE_READ && zio->io_type != ZIO_TYPE_WRITE) {
		ASSERT3P(bp, ==, NULL);
		return;
	}

	if (bp == NULL) {
		ASSERT3S(zio->io_error, ==, ENXIO);
		return;
	}

	if (bp->bio_ma != NULL) {
		free(bp->bio_ma, M_DEVBUF);
	} else {
		if (zio->io_type == ZIO_TYPE_READ) {
			abd_return_buf_copy(zio->io_abd, bp->bio_data,
			    zio->io_size);
		} else {
			abd_return_buf(zio->io_abd, bp->bio_data,
			    zio->io_size);
		}
	}

	g_destroy_bio(bp);
	zio->io_bio = NULL;
}

int
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
	struct thread *td;
	struct stat sb;
	off_t stripesize = 0;
	off_t stripeoffset = 0;
	off_t mediasize;
	uint_t sectorsize;
	int rc;

	td = curthread;

#if __FreeBSD_version < 1400037
	rc = fo_stat(fp, &sb, td->td_ucred, td);
#else
	rc = fo_stat(fp, &sb, td->td_ucred);
#endif
	if (rc)
		return (SET_ERROR(rc));

	zfattr->zfa_size = sb.st_size;
	zfattr->zfa_mode = sb.st_mode;
	zfattr->zfa_logical_block_size = 0;
	zfattr->zfa_physical_block_size = 0;

	if (fp->f_vnode->v_type == VREG)
		return (0);

	rc = fo_ioctl(fp, DIOCGMEDIASIZE, (caddr_t)&mediasize,
	    td->td_ucred, td);
	if (rc) {
		zfs_dbgmsg("zfs file open: cannot get media size");
		vrele(fp->f_vnode);
		fdrop(fp, curthread);
		return (SET_ERROR(EINVAL));
	}

	zfattr->zfa_size = mediasize;

	rc = fo_ioctl(fp, DIOCGSECTORSIZE, (caddr_t)&sectorsize,
	    td->td_ucred, td);
	if (rc) {
		zfs_dbgmsg("zfs file open: cannot get sector size");
		vrele(fp->f_vnode);
		fdrop(fp, curthread);
		return (SET_ERROR(EINVAL));
	}

	rc = fo_ioctl(fp, DIOCGSTRIPESIZE, (caddr_t)&stripesize,
	    td->td_ucred, td);
	if (rc)
		zfs_dbgmsg("zfs file open: cannot get stripe size");

	rc = fo_ioctl(fp, DIOCGSTRIPEOFFSET, (caddr_t)&stripeoffset,
	    td->td_ucred, td);
	if (rc)
		zfs_dbgmsg("zfs file open: cannot get stripe offset");

	zfattr->zfa_logical_block_size = MAX(sectorsize, SPA_MINBLOCKSIZE);
	zfattr->zfa_physical_block_size = 0;
	if (stripesize && stripesize > zfattr->zfa_logical_block_size &&
	    ISP2(stripesize) && stripeoffset == 0)
		zfattr->zfa_physical_block_size = stripesize;

	return (0);
}

static __inline int
zfs_vop_fsync(vnode_t *vp)
{
	struct mount *mp;
	int error;

#if __FreeBSD_version < 1400068
	if ((error = vn_start_write(vp, &mp, V_WAIT | PCATCH)) != 0)
#else
	if ((error = vn_start_write(vp, &mp, V_WAIT | V_PCATCH)) != 0)
#endif
		goto drop;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(vp, MNT_WAIT, curthread);
	VOP_UNLOCK(vp);
	vn_finished_write(mp);
drop:
	return (SET_ERROR(error));
}

int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	if (fp->f_type != DTYPE_VNODE)
		return (EINVAL);

	return (zfs_vop_fsync(fp->f_vnode));
}

/*
 * deallocate - zero and/or deallocate file storage
 *
 * fp - file pointer
 * offset - offset to start zeroing or deallocating
 * len - length to zero or deallocate
 */
int
zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len)
{
	int rc;
#if __FreeBSD_version >= 1400029
	struct thread *td;

	td = curthread;
	rc = fo_fspacectl(fp, SPACECTL_DEALLOC, &offset, &len, 0,
	    td->td_ucred, td);
#else
	(void) fp, (void) offset, (void) len;
	rc = EOPNOTSUPP;
#endif
	if (rc)
		return (SET_ERROR(rc));
	return (0);
}

zfs_file_t *
zfs_file_get(int fd)
{
	struct file *fp;

	if (fget(curthread, fd, &cap_no_rights, &fp))
		return (NULL);

	return (fp);
}

void
zfs_file_put(zfs_file_t *fp)
{
	zfs_file_close(fp);
}

loff_t
zfs_file_off(zfs_file_t *fp)
{
	return (fp->f_offset);
}

void *
zfs_file_private(zfs_file_t *fp)
{
	file_t *tmpfp;
	void *data;
	int error;

	tmpfp = curthread->td_fpop;
	curthread->td_fpop = fp;
	error = devfs_get_cdevpriv(&data);
	curthread->td_fpop = tmpfp;
	if (error != 0)
		return (NULL);
	return (data);
}

int
zfs_file_unlink(const char *fnamep)
{
	zfs_uio_seg_t seg = UIO_SYSSPACE;
	int rc;

	rc = kern_funlinkat(curthread, AT_FDCWD, fnamep, FD_NONE, seg, 0, 0);
	return (SET_ERROR(rc));
}
