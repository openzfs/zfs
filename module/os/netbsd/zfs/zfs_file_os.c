// SPDX-License-Identifier: BSD-2-Clause
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/fcntl.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/lwp.h>
#include <sys/cred.h>
#include <sys/zfs_file.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/vfs_syscalls.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/disk.h>
#include <sys/disklabel.h>

extern void *zfsdev_bind_file(void *);

struct zfs_file {
	struct vnode	*f_vp;
	struct file	*f_fp;
	int		f_flags;
	int		f_fd;
	loff_t		f_off;
	void		*f_private;	/* zfsdev_state_t for /dev/zfs */
};

#undef vn_open
#undef vn_rdwr
#undef vn_lock
#undef VOP_GETATTR
#undef VOP_FSYNC

/*
 * Match Linux kernel_write(current) / FreeBSD fo_write(curthread):
 * VFS runs on the issuer (ioctl LWP, txg_sync, zio).  Kernel
 * uio sets uio_vmspace to vmspace_kernel(); the lwp argument
 * to vn_rdwr is unused.
 */
static kauth_cred_t
zfs_file_cred(void)
{
	kauth_cred_t cred;

	cred = kauth_cred_get();
	if (cred == NULL || cred == NOCRED || cred == FSCRED)
		return ((kauth_cred_t)kcred);
	return (cred);
}

static int
zfs_file_is_zfsreg(struct vnode *vp)
{

	if (vp == NULL || vp->v_type != VREG)
		return (0);
	if (vp->v_tag == VT_ZFS)
		return (1);
	if (vp->v_mount != NULL && vp->v_mount->mnt_op != NULL &&
	    vp->v_mount->mnt_op->vfs_name != NULL &&
	    strcmp(vp->v_mount->mnt_op->vfs_name, MOUNT_ZFS) == 0)
		return (1);
	return (0);
}

/*
 * Nested file-vdev I/O: a child pool whose vdev is a regular file
 * on a parent ZFS dataset.
 *
 * Do not VOP_READ/WRITE: vn_lock across dmu_tx_assign deadlocks
 * when the child spa_sync is waiting on z_vdev_file.
 *
 * Do not zfs_read/zfs_write: those use uio, the ZIL, and
 * update_pages.  A short success (resid != 0, err == 0) becomes
 * ENOSPC in vdev_file_io_strategy and the child pool suspends
 * (failmode=wait).
 *
 * Do not hold z_rangelock across dmu_tx_assign.  That lock is the
 * ZPL file lock.  Process-context VOP_PUTPAGES takes it, then the
 * parent txg cannot finish while this z_vdev_file worker waits for
 * that txg.  The vdev file is opened exclusively by the kernel;
 * UVM pages are dropped in zfs_file_open.
 *
 * dmu_read/dmu_write + z_teardown only.  Never return a short
 * success: zero-fill a read past z_size, and only report a write
 * complete when the full count is in.  Nested I/O must not sit on
 * the parent dirty throttle (DMU_TX_NOTHROTTLE): the child
 * spa_sync is waiting on this worker.
 */
static int
zfs_file_zfsreg_rw(struct vnode *vp, enum uio_rw rw, void *buf,
    size_t count, off_t off, ssize_t *resid)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	int error;
	size_t done = 0;

	if (zp == NULL || zp->z_zfsvfs == NULL)
		return (SET_ERROR(EIO));
	if (off < 0)
		return (SET_ERROR(EINVAL));
	if (count == 0) {
		if (resid != NULL)
			*resid = 0;
		return (0);
	}
	zfsvfs = zp->z_zfsvfs;
	error = zfs_enter_verify_zp(zfsvfs, zp, FTAG);
	if (error != 0)
		return (SET_ERROR(error));

	if (rw == UIO_READ) {
		uint64_t n = count;
		uint64_t zsz = zp->z_size;

		if ((uint64_t)off >= zsz) {
			memset(buf, 0, count);
			done = count;
			error = 0;
		} else {
			if ((uint64_t)off + n > zsz)
				n = zsz - (uint64_t)off;
			error = dmu_read(zfsvfs->z_os, zp->z_id,
			    (uint64_t)off, n, buf, DMU_READ_NO_PREFETCH);
			if (error == 0) {
				if (n < count)
					memset((char *)buf + n, 0,
					    count - n);
				done = count;
			}
		}
	} else {
		dmu_tx_t *tx;
		int tries = 0;
		uint64_t end = (uint64_t)off + count;
		uint64_t newblksz = 0;

		/*
		 * Single-block files have dn_datablkshift == 0.
		 * dmu_write panics if offset+len exceeds dn_datablksz
		 * ("accessing past end of object").  Grow like zfs_write
		 * before the write.  dump_bytes appends a send stream
		 * this way; pre-sized vdev files already have size > blksz.
		 */
		if (end > zp->z_blksz && zp->z_size <= zp->z_blksz) {
			if (zp->z_blksz > zfsvfs->z_max_blksz &&
			    !ISP2(zp->z_blksz))
				newblksz = 1 << highbit64(zp->z_blksz);
			else
				newblksz = zfsvfs->z_max_blksz;
			newblksz = MIN(newblksz,
			    P2ROUNDUP(end, SPA_MINBLOCKSIZE));
			newblksz = MAX(newblksz, zp->z_blksz);
		}

retry_tx:
		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_write(tx, zp->z_id, (uint64_t)off, count);
		if (newblksz != 0)
			dmu_tx_hold_write(tx, zp->z_id, 0, newblksz);
		if (zp->z_sa_hdl != NULL)
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		error = dmu_tx_assign(tx,
		    DMU_TX_WAIT | DMU_TX_NOTHROTTLE);
		if (error != 0) {
			dmu_tx_abort(tx);
			if ((error == ERESTART || error == EINTR) &&
			    tries++ < 8)
				goto retry_tx;
		} else {
			uint64_t newsz;

			if (newblksz != 0)
				zfs_grow_blocksize(zp, newblksz, tx);
			dmu_write(zfsvfs->z_os, zp->z_id, (uint64_t)off,
			    count, buf, tx, 0);
			newsz = MAX(zp->z_size, end);
			if (newsz != zp->z_size && zp->z_sa_hdl != NULL) {
				zp->z_size = newsz;
				(void) sa_update(zp->z_sa_hdl,
				    SA_ZPL_SIZE(zfsvfs), &zp->z_size,
				    sizeof (zp->z_size), tx);
				vnode_pager_setsize(vp, zp->z_size);
			}
			dmu_tx_commit(tx);
			done = count;
			error = 0;
		}
	}
	zfs_exit(zfsvfs, FTAG);
	if (resid != NULL)
		*resid = (ssize_t)(count - done);
	return (error);
}

/*
 * mkfile / VOP_WRITE leaves dirty UVM pages.  Nested vdev I/O does
 * not update_pages, so those pages must not stay dirty: a later
 * VOP_PUTPAGES takes z_rangelock (the ZPL file lock) in process
 * context.  Flush them into the DMU, then drop the cache.
 */
static void
zfs_file_zfsreg_uncache(struct vnode *vp)
{

	if (!zfs_file_is_zfsreg(vp))
		return;
	vn_flush_cached_data(vp, B_TRUE);
	rw_enter(vp->v_uobj.vmobjlock, RW_WRITER);
	(void) VOP_PUTPAGES(vp, 0, 0, PGO_FREE | PGO_ALLPAGES);
}

static int
zfs_file_vn_rdwr(struct vnode *vp, enum uio_rw rw, void *buf, size_t count,
    off_t off, ssize_t *resid)
{
	size_t done = 0;
	int error = 0;
	int intrs = 0;
	kauth_cred_t cr = (kauth_cred_t)kcred;

	/*
	 * Do not call vn_rdwr(): NetBSD applies curlwp RLIMIT_FSIZE
	 * and SIGXFSZ (vfs_vnops.c), and on enforce_rlimit_fsize
	 * failure leaves *aresid unset.  File-vdev I/O from an ioctl
	 * LWP must not inherit the caller's ulimit -f.
	 */
	if (zfs_file_is_zfsreg(vp)) {
		error = zfs_file_zfsreg_rw(vp, rw, buf, count, off, resid);
		return (error);
	}

	while (done < count) {
		struct uio auio;
		struct iovec iov;
		size_t chunk = count - done;
		int lkflag;

		if (chunk > (size_t)(1 << 20))
			chunk = (size_t)(1 << 20);
		iov.iov_base = (char *)buf + done;
		iov.iov_len = chunk;
		memset(&auio, 0, sizeof (auio));
		auio.uio_iov = &iov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = off + done;
		auio.uio_resid = chunk;
		auio.uio_rw = rw;
		auio.uio_vmspace = vmspace_kernel();
		lkflag = (rw == UIO_READ) ? LK_SHARED : LK_EXCLUSIVE;
		error = vn_lock(vp, lkflag | LK_RETRY);
		if (error != 0)
			break;
		if (rw == UIO_READ)
			error = VOP_READ(vp, &auio, IO_UNIT, cr);
		else
			error = VOP_WRITE(vp, &auio, IO_UNIT, cr);
		VOP_UNLOCK(vp);
		if (error == ERESTART || error == EINTR) {
			/*
			 * zio retries a failed I/O.  Do not spin here
			 * until the signal goes away.
			 */
			if (++intrs >= 3)
				break;
			error = 0;
			continue;
		}
		intrs = 0;
		{
			size_t progressed = chunk - auio.uio_resid;

			done += progressed;
			if (error != 0)
				break;
			if (progressed == 0)
				break;
		}
	}
	/*
	 * A short read is a short read.  Zero-filling it hides a
	 * truncated file vdev: vdev_file turns a non-zero resid into
	 * ENOSPC, same as the other platforms.
	 */
	if (resid)
		*resid = (ssize_t)(count - done);
	return (error);
}

static int
zfs_file_rw(zfs_file_t *fp, enum uio_rw rw, void *buf, size_t count,
    off_t off, ssize_t *resid)
{
	struct uio auio;
	struct iovec iov;
	int error;
	off_t foff;
	int fof;

	if (fp == NULL)
		return (SET_ERROR(EINVAL));

	/*
	 * Kernel vdev files only have f_vp. Prefer that path.
	 */
	if (fp->f_vp != NULL) {
		error = zfs_file_vn_rdwr(fp->f_vp, rw, buf, count, off,
		    resid);
		return (error ? SET_ERROR(error) : 0);
	}

	if (fp->f_fp != NULL && fp->f_fp->f_ops != NULL) {
		memset(&auio, 0, sizeof (auio));
		iov.iov_base = buf;
		iov.iov_len = count;
		auio.uio_iov = &iov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = off;
		auio.uio_resid = count;
		auio.uio_rw = rw;
		auio.uio_vmspace = vmspace_kernel();
		foff = off;
		fof = FOF_UPDATE_OFFSET | FOF_IOV_SYSSPACE;
		if (rw == UIO_READ)
			error = (*fp->f_fp->f_ops->fo_read)(fp->f_fp, &foff,
			    &auio, zfs_file_cred(), fof);
		else
			error = (*fp->f_fp->f_ops->fo_write)(fp->f_fp, &foff,
			    &auio, zfs_file_cred(), fof);
		if (resid)
			*resid = auio.uio_resid;
		return (error ? SET_ERROR(error) : 0);
	}

	printf("ZFS: zfs_file_rw bad fp=%p vp=%p file=%p\n",
	    fp, fp->f_vp, fp->f_fp);
	return (SET_ERROR(EINVAL));
}

int
#ifdef ZFS_FILE_OPEN_HAS_CRED
zfs_file_open(const char *path, int flags, int mode, cred_t *cr,
    zfs_file_t **fpp)
#else
zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fpp)
#endif
{
	zfs_file_t *fp;
	struct pathbuf *pb;
	struct vnode *vp;
	kauth_cred_t old;
	cred_t *use;
	int fmode, error;

	fmode = FREAD;
	if (flags & O_RDWR)
		fmode = FREAD | FWRITE;
	else if (flags & O_WRONLY)
		fmode = FWRITE;
	if (flags & O_CREAT)
		fmode |= O_CREAT;
	if (flags & O_TRUNC)
		fmode |= O_TRUNC;

	pb = pathbuf_create(path);
	if (pb == NULL)
		return (SET_ERROR(ENOMEM));
#ifdef ZFS_FILE_OPEN_HAS_CRED
	use = (cr != NULL) ? cr : CRED();
#else
	use = CRED();
#endif
	old = zfs_lwp_cred_swap(use);
	error = vn_open(NULL, pb, 0, fmode, mode, &vp, NULL, NULL);
	zfs_lwp_cred_restore(old, use);
	pathbuf_destroy(pb);
	if (error != 0)
		return (SET_ERROR(error));
	VOP_UNLOCK(vp);
	zfs_file_zfsreg_uncache(vp);

	fp = kmem_zalloc(sizeof (*fp), KM_SLEEP);
	fp->f_vp = vp;
	fp->f_fp = NULL;
	fp->f_fd = -1;
	fp->f_flags = fmode;
	fp->f_off = 0;
	*fpp = fp;
	return (0);
}

void
zfs_file_close(zfs_file_t *fp)
{

	if (fp == NULL)
		return;
	if (fp->f_fd >= 0 && fp->f_fp != NULL) {
		(void) closef(fp->f_fp);
		fp->f_fp = NULL;
		fp->f_fd = -1;
	} else if (fp->f_vp != NULL) {
		/*
		 * NetBSD vn_close() takes LK_EXCLUSIVE itself
		 * (then vput).  Locking first is the FreeBSD
		 * contract and panics: "locking against myself"
		 * on spa_export → vdev_file_close.
		 */
		(void) vn_close(fp->f_vp, fp->f_flags, FSCRED);
		fp->f_vp = NULL;
	}
	kmem_free(fp, sizeof (*fp));
}

int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
	int error;

	error = zfs_file_rw(fp, UIO_WRITE, __UNCONST(buf), count,
	    fp->f_off, resid);
	if (error == 0)
		fp->f_off += count - (resid ? *resid : 0);
	return (error);
}

int
zfs_file_pwrite(zfs_file_t *fp, const void *buf, size_t count, loff_t off,
    uint8_t ashift, ssize_t *resid)
{
	(void) ashift;
	return (zfs_file_rw(fp, UIO_WRITE, __UNCONST(buf), count, off, resid));
}

int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	int error;

	error = zfs_file_rw(fp, UIO_READ, buf, count, fp->f_off, resid);
	if (error == 0)
		fp->f_off += count - (resid ? *resid : 0);
	return (error);
}

int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	return (zfs_file_rw(fp, UIO_READ, buf, count, off, resid));
}

int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
	zfs_file_attr_t za;
	int error;

	switch (whence) {
	case SEEK_SET:
		fp->f_off = *offp;
		break;
	case SEEK_CUR:
		fp->f_off += *offp;
		break;
	case SEEK_END:
		error = zfs_file_getattr(fp, &za);
		if (error)
			return (error);
		fp->f_off = za.zfa_size + *offp;
		break;
	default:
		return (SET_ERROR(EINVAL));
	}
	*offp = fp->f_off;
	return (0);
}

struct zfs_file_attrreq {
	struct vnode	*vp;
	zfs_file_attr_t	*zfattr;
	int		error;
};

static void
zfs_file_getattr_fn(void *arg)
{
	struct zfs_file_attrreq *a = arg;
	struct vattr va;
	int error;

	error = vn_lock(a->vp, LK_SHARED | LK_RETRY);
	if (error != 0) {
		a->error = error;
		return;
	}
	error = VOP_GETATTR(a->vp, &va, (kauth_cred_t)kcred);
	VOP_UNLOCK(a->vp);
	if (error) {
		a->error = error;
		return;
	}
	a->zfattr->zfa_size = va.va_size;
	a->zfattr->zfa_mode = va.va_mode & ALLPERMS;
	switch (va.va_type) {
	case VREG:
		a->zfattr->zfa_mode |= S_IFREG;
		break;
	case VBLK:
		a->zfattr->zfa_mode |= S_IFBLK;
		break;
	case VCHR:
		a->zfattr->zfa_mode |= S_IFCHR;
		break;
	case VDIR:
		a->zfattr->zfa_mode |= S_IFDIR;
		break;
	case VLNK:
		a->zfattr->zfa_mode |= S_IFLNK;
		break;
	case VFIFO:
		a->zfattr->zfa_mode |= S_IFIFO;
		break;
	case VSOCK:
		a->zfattr->zfa_mode |= S_IFSOCK;
		break;
	default:
		break;
	}
	/*
	 * Disk vnodes often report va_size 0.  DIOCGMEDIASIZE is
	 * unlocked (vnode_if ioctl is = = =).
	 */
	if (a->zfattr->zfa_size == 0 &&
	    (va.va_type == VCHR || va.va_type == VBLK)) {
		off_t ms = 0;

		if (VOP_IOCTL(a->vp, DIOCGMEDIASIZE, &ms, FREAD, FSCRED) == 0 &&
		    ms > 0)
			a->zfattr->zfa_size = (uint64_t)ms;
	}
	a->error = 0;
}

int
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
	struct stat st;
	int error;
	struct zfs_file_attrreq a;

	if (fp->f_fp != NULL && fp->f_fp->f_ops != NULL &&
	    fp->f_fp->f_ops->fo_stat != NULL) {
		error = (*fp->f_fp->f_ops->fo_stat)(fp->f_fp, &st);
		if (error)
			return (SET_ERROR(error));
		zfattr->zfa_size = st.st_size;
		zfattr->zfa_mode = st.st_mode;
		return (0);
	}

	if (fp->f_vp == NULL)
		return (SET_ERROR(EINVAL));

	if (zfs_file_is_zfsreg(fp->f_vp)) {
		vattr_t va;

		memset(&va, 0, sizeof (va));
		error = zfs_getattr(fp->f_vp, &va, 0, kcred);
		if (error != 0)
			return (SET_ERROR(error));
		zfattr->zfa_size = va.va_size;
		zfattr->zfa_mode = S_IFREG | (va.va_mode & ALLPERMS);
		return (0);
	}

	memset(&a, 0, sizeof (a));
	a.vp = fp->f_vp;
	a.zfattr = zfattr;
	zfs_file_getattr_fn(&a);
	return (a.error ? SET_ERROR(a.error) : 0);
}

/*
 * disk_find by a few name candidates.  Do not strip a leading 'r'
 * first -- /dev/raid0a would become aid0.  Try as-is, then drop a
 * trailing partition letter, then the raw 'r' prefix.
 *
 * DIOCGDISKINFO is prop_dictionary_copyout_ioctl (user plistref).
 * Unusable from the kmod.  The vnode is open, so the disk will not
 * detach under disk_find.
 */
static struct disk *
zfs_nb_disk_lookup(const char *path)
{
	const char *base;
	char n[32], t[32];
	struct disk *dk;
	size_t len;

	if (path == NULL)
		return (NULL);
	base = strrchr(path, '/');
	base = (base != NULL) ? base + 1 : path;
	strlcpy(n, base, sizeof (n));
	if ((dk = disk_find(n)) != NULL)
		return (dk);
	len = strlen(n);
	if (len > 1 && n[len - 1] >= 'a' && n[len - 1] <= 'p' &&
	    n[len - 2] >= '0' && n[len - 2] <= '9') {
		strlcpy(t, n, sizeof (t));
		t[len - 1] = '\0';
		if ((dk = disk_find(t)) != NULL)
			return (dk);
	}
	if (n[0] == 'r' && n[1] != '\0') {
		if ((dk = disk_find(n + 1)) != NULL)
			return (dk);
		len = strlen(n + 1);
		if (len > 1 && n[1 + len - 1] >= 'a' && n[1 + len - 1] <= 'p' &&
		    n[1 + len - 2] >= '0' && n[1 + len - 2] <= '9') {
			strlcpy(t, n + 1, sizeof (t));
			t[len - 1] = '\0';
			if ((dk = disk_find(t)) != NULL)
				return (dk);
		}
	}
	return (NULL);
}

int
zfs_file_sectorsize(zfs_file_t *fp, const char *path,
    uint32_t *logical, uint32_t *physical)
{
	uint_t sec = 0;
	uint32_t phys = 0;
	int error;
	struct dkwedge_info dkw;
	struct disk *dk = NULL;

	if (fp == NULL || fp->f_vp == NULL || logical == NULL)
		return (SET_ERROR(EINVAL));
	/* vnode_if: ioctl vp = = = -- unlocked. */
	error = VOP_IOCTL(fp->f_vp, DIOCGSECTORSIZE, &sec, FREAD, FSCRED);
	memset(&dkw, 0, sizeof (dkw));
	if (VOP_IOCTL(fp->f_vp, DIOCGWEDGEINFO, &dkw, FREAD, FSCRED) == 0 &&
	    dkw.dkw_parent[0] != '\0')
		dk = disk_find(dkw.dkw_parent);
	if (error != 0 || sec == 0)
		return (SET_ERROR(error != 0 ? error : ENOTTY));
	*logical = (uint32_t)sec;
	if (dk == NULL)
		dk = zfs_nb_disk_lookup(path);
	if (dk != NULL && dk->dk_geom.dg_physsecsize != 0)
		phys = dk->dk_geom.dg_physsecsize;
	if (physical != NULL)
		*physical = phys != 0 ? phys : *logical;
	return (0);
}

int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	int error;
	int force = 1;
	enum vtype vt;

	(void) flags;
	if (fp->f_vp == NULL)
		return (0);
	vt = fp->f_vp->v_type;
	/*
	 * VBLK spec_write is buffered (getblk/bwrite).  spec_fsync
	 * is vflushbuf -- do that first, then DIOCCACHESYNC.
	 * VCHR is physio; fsync is a no-op; cache-sync alone.
	 * ioctl is unlocked.
	 */
	if (vt == VBLK) {
		error = vn_lock(fp->f_vp, LK_EXCLUSIVE | LK_RETRY);
		if (error != 0)
			return (SET_ERROR(error));
		error = VOP_FSYNC(fp->f_vp, (kauth_cred_t)kcred,
		    FSYNC_WAIT | FSYNC_DATAONLY, 0, 0);
		VOP_UNLOCK(fp->f_vp);
		if (error != 0)
			return (SET_ERROR(error));
		error = VOP_IOCTL(fp->f_vp, DIOCCACHESYNC, &force, FWRITE,
		    FSCRED);
		if (error == ENOTTY || error == ENODEV || error == ENOTSUP ||
		    error == EOPNOTSUPP)
			error = 0;
		return (error ? SET_ERROR(error) : 0);
	}
	if (vt == VCHR) {
		error = VOP_IOCTL(fp->f_vp, DIOCCACHESYNC, &force, FWRITE,
		    FSCRED);
		if (error == ENOTTY || error == ENODEV || error == ENOTSUP ||
		    error == EOPNOTSUPP)
			error = 0;
		return (error ? SET_ERROR(error) : 0);
	}
	if (zfs_file_is_zfsreg(fp->f_vp))
		return (zfs_fsync(VTOZ(fp->f_vp), flags, kcred));
	error = vn_lock(fp->f_vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0)
		return (SET_ERROR(error));
	error = VOP_FSYNC(fp->f_vp, (kauth_cred_t)kcred,
	    FSYNC_WAIT | FSYNC_DATAONLY, 0, 0);
	VOP_UNLOCK(fp->f_vp);
	return (error ? SET_ERROR(error) : 0);
}

/*
 * Punch a hole in a ZFS file used as a nested vdev.
 *
 * Do not VOP_FDISCARD: vnode_if holds LK_EXCLUSIVE across
 * dmu_tx_assign.  Do not zfs_freesp: zfs_free_range takes
 * z_rangelock and dmu_free_long_range waits for a txg, including
 * txg_wait_open once the per-txg free threshold is hit.  Process
 * context VOP_PUTPAGES needs that range lock to finish the parent
 * txg, while the child spa_sync is waiting on this worker.
 *
 * The vdev file is opened exclusively and its UVM cache is dropped
 * in zfs_file_open.  Free with DMU_TX_NOTHROTTLE so this worker is
 * not stuck behind the parent dirty throttle.  Chunk the range so
 * one transaction is not an entire vdev.
 */
static int
zfs_file_zfsreg_deallocate(struct vnode *vp, uint64_t off, uint64_t len)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	int error;

	if (zp == NULL || zp->z_zfsvfs == NULL)
		return (SET_ERROR(EIO));
	zfsvfs = zp->z_zfsvfs;
	error = zfs_enter_verify_zp(zfsvfs, zp, FTAG);
	if (error != 0)
		return (SET_ERROR(error));
	if (zfs_is_readonly(zfsvfs)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EROFS));
	}
	if (off >= zp->z_size || len == 0) {
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}
	if (len > zp->z_size - off)
		len = zp->z_size - off;

	while (len != 0 && error == 0) {
		dmu_tx_t *tx;
		uint64_t chunk = len;
		int tries = 0;

		/* 32MB: one tx, not a whole file vdev. */
		if (chunk > (32ULL << 20))
			chunk = 32ULL << 20;
retry_free:
		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_free(tx, zp->z_id, off, chunk);
		dmu_tx_mark_netfree(tx);
		error = dmu_tx_assign(tx, DMU_TX_WAIT | DMU_TX_NOTHROTTLE);
		if (error != 0) {
			dmu_tx_abort(tx);
			if ((error == ERESTART || error == EINTR) &&
			    tries++ < 8)
				goto retry_free;
			break;
		}
		error = dmu_free_range(zfsvfs->z_os, zp->z_id, off, chunk, tx);
		dmu_tx_commit(tx);
		if (error != 0)
			break;
		off += chunk;
		len -= chunk;
	}
	zfs_exit(zfsvfs, FTAG);
	return (error ? SET_ERROR(error) : 0);
}

int
zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len)
{
	struct vnode *vp;
	int error;

	/*
	 * vdev TRIM.  NetBSD 11 has no fo_fspacectl; the native
	 * interface is VOP_FDISCARD (vnode_if: vp L L L) which
	 * spec_fdiscard turns into cdev_discard / bdev_discard
	 * (wd_trim, dkdiscard, lddiscard, zvol_cdev_discard).
	 *
	 * FFS files do not implement it (genfs_eopnotsupp).  Do not
	 * treat a missing VOP as success; that would mark TRIM complete
	 * when nothing was punched.
	 */
	if (fp == NULL || fp->f_vp == NULL)
		return (SET_ERROR(EOPNOTSUPP));
	if (offset < 0 || len <= 0)
		return (SET_ERROR(EINVAL));
	vp = fp->f_vp;
	/*
	 * Nested ZFS file-vdev TRIM.  See zfs_file_zfsreg_deallocate.
	 * vdev_file advertises TRIM as "always safe to attempt".
	 * FFS still has no VOP_FDISCARD; a file there returns
	 * EOPNOTSUPP so the trim is not reported as done.
	 */
	if (zfs_file_is_zfsreg(vp))
		return (zfs_file_zfsreg_deallocate(vp, (uint64_t)offset,
		    (uint64_t)len));
	if (vp->v_type != VCHR && vp->v_type != VBLK)
		return (SET_ERROR(EOPNOTSUPP));

	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0)
		return (SET_ERROR(error));
	error = VOP_FDISCARD(vp, offset, len);
	VOP_UNLOCK(vp);
	return (error ? SET_ERROR(error) : 0);
}

loff_t
zfs_file_off(zfs_file_t *fp)
{
	return (fp->f_off);
}

int
zfs_file_unlink(const char *path)
{
	int error;

	if (path == NULL)
		return (SET_ERROR(EINVAL));
	error = do_sys_unlink(path, UIO_SYSSPACE);
	/* Cache file may not exist yet (first spa_config_update). */
	if (error == ENOENT)
		return (0);
	return (error ? SET_ERROR(error) : 0);
}

zfs_file_t *
zfs_file_get(int fd)
{
	struct file *ff;
	zfs_file_t *fp;

	if (fd < 0)
		return (NULL);
	/* Ioctls run on the calling LWP -- same as Linux/FreeBSD. */
	ff = fd_getfile((unsigned)fd);
	if (ff == NULL)
		return (NULL);

	fp = kmem_zalloc(sizeof (*fp), KM_SLEEP);
	fp->f_fp = ff;
	fp->f_fd = fd;
	fp->f_flags = ff->f_flag;
	fp->f_off = ff->f_offset;
	if (ff->f_type == DTYPE_VNODE)
		fp->f_vp = ff->f_vnode;
	fp->f_private = zfsdev_bind_file(ff);
	return (fp);
}

void
zfs_file_put(zfs_file_t *fp)
{
	if (fp == NULL)
		return;
	/*
	 * VOP_WRITE at an explicit offset does not advance
	 * struct file::f_offset.  zfs send -R writes a compound
	 * header from userland, the kernel writes each substream,
	 * then userland writes the trailer.  Leaving f_offset at
	 * the header makes that trailer overwrite the first
	 * substream (rsend_001_pos, cmp_ds_subs).
	 */
	if (fp->f_fd >= 0 && fp->f_fp != NULL)
		fp->f_fp->f_offset = fp->f_off;
	/* fd_getfile() borrows a reference; fd_putfile() releases it. */
	if (fp->f_fd >= 0)
		fd_putfile((unsigned)fp->f_fd);
	kmem_free(fp, sizeof (*fp));
}

void *
zfs_file_private(zfs_file_t *fp)
{
	if (fp == NULL)
		return (NULL);
	return (fp->f_private);
}
