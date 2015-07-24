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

#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <linux/file_compat.h>
#include <linux/dcache_compat.h>

vnode_t *rootdir = (vnode_t *)0xabcd1234;

vtype_t
vn_mode_to_vtype(mode_t mode)
{
	if (S_ISREG(mode))
		return (VREG);

	if (S_ISDIR(mode))
		return (VDIR);

	if (S_ISCHR(mode))
		return (VCHR);

	if (S_ISBLK(mode))
		return (VBLK);

	if (S_ISFIFO(mode))
		return (VFIFO);

	if (S_ISLNK(mode))
		return (VLNK);

	if (S_ISSOCK(mode))
		return (VSOCK);

	if (S_ISCHR(mode))
		return (VCHR);

	return (VNON);
}

mode_t
vn_vtype_to_mode(vtype_t vtype)
{
	if (vtype == VREG)
		return (S_IFREG);

	if (vtype == VDIR)
		return (S_IFDIR);

	if (vtype == VCHR)
		return (S_IFCHR);

	if (vtype == VBLK)
		return (S_IFBLK);

	if (vtype == VFIFO)
		return (S_IFIFO);

	if (vtype == VLNK)
		return (S_IFLNK);

	if (vtype == VSOCK)
		return (S_IFSOCK);

	return (VNON);
}

int
vn_open(const char *path, uio_seg_t seg, int flags, int mode,
    vnode_t **vpp, int unused1, void *unused2)
{
	int file_flags = (flags & ~O_ACCMODE);
	struct file *fp;

	if ((flags & FREAD) && !(flags & FWRITE))
		file_flags |= O_RDONLY;

	if (!(flags & FREAD) && (flags & FWRITE))
		file_flags |= O_WRONLY;

	if ((flags & FREAD) && (flags & FWRITE))
		file_flags |= O_RDWR;

	if (!(flags & FCREAT) && (flags & FWRITE))
		file_flags |= O_EXCL;

	fp = spl_file_open(path, file_flags, mode);
	if (IS_ERR(fp))
		return (-PTR_ERR(fp));

	*vpp = (vnode_t *)fp;

	return (0);
}

int
vn_openat(const char *path, uio_seg_t seg, int flags, int mode, vnode_t **vpp,
    int unused1, void *unused2, vnode_t *startvp, int unused4)
{
	char *realpath;
	int error;

	ASSERT(startvp == rootdir);
	realpath = kmem_alloc(strlen(path) + 2, KM_SLEEP);
	(void) sprintf(realpath, "/%s", path);

	error = vn_open(realpath, seg, flags, mode, vpp, unused1, unused2);

	kmem_free(realpath, strlen(path) + 2);

	return (error);
}

int
vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len,
    offset_t off, uio_seg_t seg, int flags, rlim64_t unused1,
    void *unused2, ssize_t *residp)
{
	struct file *fp = (struct file *)vp;
	loff_t offset = off;
	ssize_t size;

	if (flags & FAPPEND)
		offset = spl_file_pos(fp);

	if (uio & UIO_WRITE)
		size = spl_file_write(fp, addr, len, &offset);
	else
		size = spl_file_read(fp, addr, len, &offset);


	spl_file_pos(fp) = offset;

	if (size < 0)
		return (-size);

	if (residp)
		*residp = (len - size);
	else if (size != len)
		return (EIO);

	return (0);
}

int
vn_close(vnode_t *vp, int unused1, int unused2, int unused3,
    void *unused4, void *unused5)
{
	return (-spl_file_close((struct file *)vp));
}

int
vn_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, void *ct)
{
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}

int
vn_remove(const char *path, uio_seg_t seg, int flags)
{
	struct file *fp;
	struct dentry *file_dentry;
	struct dentry *dir_dentry;
	int error;

	fp = spl_file_open(path, O_RDWR, 0644);
	if (IS_ERR(fp))
		return (-PTR_ERR(fp));

	file_dentry = spl_file_dentry(fp);
	dir_dentry = dget_parent(file_dentry);
	spl_inode_lock(dir_dentry->d_inode);

	error = spl_file_unlink(dir_dentry->d_inode, file_dentry);

	spl_inode_unlock(dir_dentry->d_inode);
	dput(dir_dentry);

	spl_file_close(fp);

	return (error);
}

int
vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *unused1, void *unused2)
{
	struct kstat stat;
	int error;

	error = -spl_file_stat((struct file *)vp, &stat);
	if (error)
		return (error);

	vap->va_type	= vn_mode_to_vtype(stat.mode);
	vap->va_mode	= stat.mode;
	vap->va_uid	= KUID_TO_SUID(stat.uid);
	vap->va_gid	= KGID_TO_SGID(stat.gid);
	vap->va_fsid	= 0;
	vap->va_nodeid	= stat.ino;
	vap->va_nlink	= stat.nlink;
	vap->va_size	= stat.size;
	vap->va_blksize	= stat.blksize;
	vap->va_atime	= stat.atime;
	vap->va_mtime	= stat.mtime;
	vap->va_ctime	= stat.ctime;
	vap->va_rdev	= stat.rdev;
	vap->va_nblocks	= stat.blocks;

	return (0);
}

/*
 * Note: PF_FSTRANS must not be set when entering XFS or a warning will
 * be generated.  Clear the flag over vfs_sync() and restore if needed.
 */
int
vn_fsync(vnode_t *vp, int flags, void *unused1, void *unused2)
{
	int cookie;
	int error;

	cookie = spl_fstrans_check();
	if (cookie)
		current->flags &= ~(PF_FSTRANS);

	error = -spl_file_fsync((struct file *)vp, !!(flags & FDSYNC));

	if (cookie)
		current->flags |= (PF_FSTRANS);

	return (error);
}

/*
 * For the kernel wrappers vnode's and file's are one and the same as a
 * simplification.  However, this isn't true for the user wrappers so an
 * interface to perform this conversion is provided.
 */
vnode_t *
vn_from_file(struct file *fp)
{
	return ((vnode_t *) fp);
}

EXPORT_SYMBOL(vn_mode_to_vtype);
EXPORT_SYMBOL(vn_vtype_to_mode);
EXPORT_SYMBOL(vn_open);
EXPORT_SYMBOL(vn_openat);
EXPORT_SYMBOL(vn_rdwr);
EXPORT_SYMBOL(vn_close);
EXPORT_SYMBOL(vn_seek);
EXPORT_SYMBOL(vn_remove);
EXPORT_SYMBOL(vn_getattr);
EXPORT_SYMBOL(vn_fsync);
