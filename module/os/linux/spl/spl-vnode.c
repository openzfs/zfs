/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Vnode Implementation.
 */

#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/kmem_cache.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#ifdef HAVE_FDTABLE_HEADER
#include <linux/fdtable.h>
#endif

vnode_t *rootdir = (vnode_t *)0xabcd1234;
EXPORT_SYMBOL(rootdir);

static spl_kmem_cache_t *vn_cache;
static spl_kmem_cache_t *vn_file_cache;

static spinlock_t vn_file_lock;
static LIST_HEAD(vn_file_list);

static int
spl_filp_fallocate(struct file *fp, int mode, loff_t offset, loff_t len)
{
	int error = -EOPNOTSUPP;

#ifdef HAVE_FILE_FALLOCATE
	if (fp->f_op->fallocate)
		error = fp->f_op->fallocate(fp, mode, offset, len);
#else
#ifdef HAVE_INODE_FALLOCATE
	if (fp->f_dentry && fp->f_dentry->d_inode &&
	    fp->f_dentry->d_inode->i_op->fallocate)
		error = fp->f_dentry->d_inode->i_op->fallocate(
		    fp->f_dentry->d_inode, mode, offset, len);
#endif /* HAVE_INODE_FALLOCATE */
#endif /* HAVE_FILE_FALLOCATE */

	return (error);
}

static int
spl_filp_fsync(struct file *fp, int sync)
{
#ifdef HAVE_2ARGS_VFS_FSYNC
	return (vfs_fsync(fp, sync));
#else
	return (vfs_fsync(fp, (fp)->f_dentry, sync));
#endif /* HAVE_2ARGS_VFS_FSYNC */
}

static ssize_t
spl_kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos)
{
#if defined(HAVE_KERNEL_WRITE_PPOS)
	return (kernel_write(file, buf, count, pos));
#else
	mm_segment_t saved_fs;
	ssize_t ret;

	saved_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_write(file, (__force const char __user *)buf, count, pos);

	set_fs(saved_fs);

	return (ret);
#endif
}

static ssize_t
spl_kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
#if defined(HAVE_KERNEL_READ_PPOS)
	return (kernel_read(file, buf, count, pos));
#else
	mm_segment_t saved_fs;
	ssize_t ret;

	saved_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_read(file, (void __user *)buf, count, pos);

	set_fs(saved_fs);

	return (ret);
#endif
}

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

	return (VNON);
} /* vn_mode_to_vtype() */
EXPORT_SYMBOL(vn_mode_to_vtype);

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
} /* vn_vtype_to_mode() */
EXPORT_SYMBOL(vn_vtype_to_mode);

vnode_t *
vn_alloc(int flag)
{
	vnode_t *vp;

	vp = kmem_cache_alloc(vn_cache, flag);
	if (vp != NULL) {
		vp->v_file = NULL;
		vp->v_type = 0;
	}

	return (vp);
} /* vn_alloc() */
EXPORT_SYMBOL(vn_alloc);

void
vn_free(vnode_t *vp)
{
	kmem_cache_free(vn_cache, vp);
} /* vn_free() */
EXPORT_SYMBOL(vn_free);

int
vn_open(const char *path, uio_seg_t seg, int flags, int mode, vnode_t **vpp,
    int x1, void *x2)
{
	struct file *fp;
	struct kstat stat;
	int rc, saved_umask = 0;
	gfp_t saved_gfp;
	vnode_t *vp;

	ASSERT(flags & (FWRITE | FREAD));
	ASSERT(seg == UIO_SYSSPACE);
	ASSERT(vpp);
	*vpp = NULL;

	if (!(flags & FCREAT) && (flags & FWRITE))
		flags |= FEXCL;

	/*
	 * Note for filp_open() the two low bits must be remapped to mean:
	 * 01 - read-only  -> 00 read-only
	 * 10 - write-only -> 01 write-only
	 * 11 - read-write -> 10 read-write
	 */
	flags--;

	if (flags & FCREAT)
		saved_umask = xchg(&current->fs->umask, 0);

	fp = filp_open(path, flags, mode);

	if (flags & FCREAT)
		(void) xchg(&current->fs->umask, saved_umask);

	if (IS_ERR(fp))
		return (-PTR_ERR(fp));

#if defined(HAVE_4ARGS_VFS_GETATTR)
	rc = vfs_getattr(&fp->f_path, &stat, STATX_TYPE, AT_STATX_SYNC_AS_STAT);
#elif defined(HAVE_2ARGS_VFS_GETATTR)
	rc = vfs_getattr(&fp->f_path, &stat);
#else
	rc = vfs_getattr(fp->f_path.mnt, fp->f_dentry, &stat);
#endif
	if (rc) {
		filp_close(fp, 0);
		return (-rc);
	}

	vp = vn_alloc(KM_SLEEP);
	if (!vp) {
		filp_close(fp, 0);
		return (ENOMEM);
	}

	saved_gfp = mapping_gfp_mask(fp->f_mapping);
	mapping_set_gfp_mask(fp->f_mapping, saved_gfp & ~(__GFP_IO|__GFP_FS));

	mutex_enter(&vp->v_lock);
	vp->v_type = vn_mode_to_vtype(stat.mode);
	vp->v_file = fp;
	vp->v_gfp_mask = saved_gfp;
	*vpp = vp;
	mutex_exit(&vp->v_lock);

	return (0);
} /* vn_open() */
EXPORT_SYMBOL(vn_open);

int
vn_openat(const char *path, uio_seg_t seg, int flags, int mode,
    vnode_t **vpp, int x1, void *x2, vnode_t *vp, int fd)
{
	char *realpath;
	int len, rc;

	ASSERT(vp == rootdir);

	len = strlen(path) + 2;
	realpath = kmalloc(len, kmem_flags_convert(KM_SLEEP));
	if (!realpath)
		return (ENOMEM);

	(void) snprintf(realpath, len, "/%s", path);
	rc = vn_open(realpath, seg, flags, mode, vpp, x1, x2);
	kfree(realpath);

	return (rc);
} /* vn_openat() */
EXPORT_SYMBOL(vn_openat);

int
vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len, offset_t off,
    uio_seg_t seg, int ioflag, rlim64_t x2, void *x3, ssize_t *residp)
{
	struct file *fp = vp->v_file;
	loff_t offset = off;
	int rc;

	ASSERT(uio == UIO_WRITE || uio == UIO_READ);
	ASSERT(seg == UIO_SYSSPACE);
	ASSERT((ioflag & ~FAPPEND) == 0);

	if (ioflag & FAPPEND)
		offset = fp->f_pos;

	if (uio & UIO_WRITE)
		rc = spl_kernel_write(fp, addr, len, &offset);
	else
		rc = spl_kernel_read(fp, addr, len, &offset);

	fp->f_pos = offset;

	if (rc < 0)
		return (-rc);

	if (residp) {
		*residp = len - rc;
	} else {
		if (rc != len)
			return (EIO);
	}

	return (0);
} /* vn_rdwr() */
EXPORT_SYMBOL(vn_rdwr);

int
vn_close(vnode_t *vp, int flags, int x1, int x2, void *x3, void *x4)
{
	int rc;

	ASSERT(vp);
	ASSERT(vp->v_file);

	mapping_set_gfp_mask(vp->v_file->f_mapping, vp->v_gfp_mask);
	rc = filp_close(vp->v_file, 0);
	vn_free(vp);

	return (-rc);
} /* vn_close() */
EXPORT_SYMBOL(vn_close);

/*
 * vn_seek() does not actually seek it only performs bounds checking on the
 * proposed seek.  We perform minimal checking and allow vn_rdwr() to catch
 * anything more serious.
 */
int
vn_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, void *ct)
{
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}
EXPORT_SYMBOL(vn_seek);

int
vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *x3, void *x4)
{
	struct file *fp;
	struct kstat stat;
	int rc;

	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(vap);

	fp = vp->v_file;

#if defined(HAVE_4ARGS_VFS_GETATTR)
	rc = vfs_getattr(&fp->f_path, &stat, STATX_BASIC_STATS,
	    AT_STATX_SYNC_AS_STAT);
#elif defined(HAVE_2ARGS_VFS_GETATTR)
	rc = vfs_getattr(&fp->f_path, &stat);
#else
	rc = vfs_getattr(fp->f_path.mnt, fp->f_dentry, &stat);
#endif
	if (rc)
		return (-rc);

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
EXPORT_SYMBOL(vn_getattr);

int
vn_fsync(vnode_t *vp, int flags, void *x3, void *x4)
{
	int datasync = 0;
	int error;
	int fstrans;

	ASSERT(vp);
	ASSERT(vp->v_file);

	if (flags & FDSYNC)
		datasync = 1;

	/*
	 * May enter XFS which generates a warning when PF_FSTRANS is set.
	 * To avoid this the flag is cleared over vfs_sync() and then reset.
	 */
	fstrans = __spl_pf_fstrans_check();
	if (fstrans)
		current->flags &= ~(__SPL_PF_FSTRANS);

	error = -spl_filp_fsync(vp->v_file, datasync);
	if (fstrans)
		current->flags |= __SPL_PF_FSTRANS;

	return (error);
} /* vn_fsync() */
EXPORT_SYMBOL(vn_fsync);

int vn_space(vnode_t *vp, int cmd, struct flock *bfp, int flag,
    offset_t offset, void *x6, void *x7)
{
	int error = EOPNOTSUPP;
#ifdef FALLOC_FL_PUNCH_HOLE
	int fstrans;
#endif

	if (cmd != F_FREESP || bfp->l_whence != SEEK_SET)
		return (EOPNOTSUPP);

	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(bfp->l_start >= 0 && bfp->l_len > 0);

#ifdef FALLOC_FL_PUNCH_HOLE
	/*
	 * May enter XFS which generates a warning when PF_FSTRANS is set.
	 * To avoid this the flag is cleared over vfs_sync() and then reset.
	 */
	fstrans = __spl_pf_fstrans_check();
	if (fstrans)
		current->flags &= ~(__SPL_PF_FSTRANS);

	/*
	 * When supported by the underlying file system preferentially
	 * use the fallocate() callback to preallocate the space.
	 */
	error = -spl_filp_fallocate(vp->v_file,
	    FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
	    bfp->l_start, bfp->l_len);

	if (fstrans)
		current->flags |= __SPL_PF_FSTRANS;

	if (error == 0)
		return (0);
#endif

#ifdef HAVE_INODE_TRUNCATE_RANGE
	if (vp->v_file->f_dentry && vp->v_file->f_dentry->d_inode &&
	    vp->v_file->f_dentry->d_inode->i_op &&
	    vp->v_file->f_dentry->d_inode->i_op->truncate_range) {
		off_t end = bfp->l_start + bfp->l_len;
		/*
		 * Judging from the code in shmem_truncate_range(),
		 * it seems the kernel expects the end offset to be
		 * inclusive and aligned to the end of a page.
		 */
		if (end % PAGE_SIZE != 0) {
			end &= ~(off_t)(PAGE_SIZE - 1);
			if (end <= bfp->l_start)
				return (0);
		}
		--end;

		vp->v_file->f_dentry->d_inode->i_op->truncate_range(
		    vp->v_file->f_dentry->d_inode, bfp->l_start, end);

		return (0);
	}
#endif

	return (error);
}
EXPORT_SYMBOL(vn_space);

/* Function must be called while holding the vn_file_lock */
static file_t *
file_find(int fd, struct task_struct *task)
{
	file_t *fp;

	list_for_each_entry(fp, &vn_file_list,  f_list) {
		if (fd == fp->f_fd && fp->f_task == task) {
			ASSERT(atomic_read(&fp->f_ref) != 0);
			return (fp);
		}
	}

	return (NULL);
} /* file_find() */

file_t *
vn_getf(int fd)
{
	struct kstat stat;
	struct file *lfp;
	file_t *fp;
	vnode_t *vp;
	int rc = 0;

	if (fd < 0)
		return (NULL);

	/* Already open just take an extra reference */
	spin_lock(&vn_file_lock);

	fp = file_find(fd, current);
	if (fp) {
		lfp = fget(fd);
		fput(fp->f_file);
		/*
		 * areleasef() can cause us to see a stale reference when
		 * userspace has reused a file descriptor before areleasef()
		 * has run. fput() the stale reference and replace it. We
		 * retain the original reference count such that the concurrent
		 * areleasef() will decrement its reference and terminate.
		 */
		if (lfp != fp->f_file) {
			fp->f_file = lfp;
			fp->f_vnode->v_file = lfp;
		}
		atomic_inc(&fp->f_ref);
		spin_unlock(&vn_file_lock);
		return (fp);
	}

	spin_unlock(&vn_file_lock);

	/* File was not yet opened create the object and setup */
	fp = kmem_cache_alloc(vn_file_cache, KM_SLEEP);
	if (fp == NULL)
		goto out;

	mutex_enter(&fp->f_lock);

	fp->f_fd = fd;
	fp->f_task = current;
	fp->f_offset = 0;
	atomic_inc(&fp->f_ref);

	lfp = fget(fd);
	if (lfp == NULL)
		goto out_mutex;

	vp = vn_alloc(KM_SLEEP);
	if (vp == NULL)
		goto out_fget;

#if defined(HAVE_4ARGS_VFS_GETATTR)
	rc = vfs_getattr(&lfp->f_path, &stat, STATX_TYPE,
	    AT_STATX_SYNC_AS_STAT);
#elif defined(HAVE_2ARGS_VFS_GETATTR)
	rc = vfs_getattr(&lfp->f_path, &stat);
#else
	rc = vfs_getattr(lfp->f_path.mnt, lfp->f_dentry, &stat);
#endif
	if (rc)
		goto out_vnode;

	mutex_enter(&vp->v_lock);
	vp->v_type = vn_mode_to_vtype(stat.mode);
	vp->v_file = lfp;
	mutex_exit(&vp->v_lock);

	fp->f_vnode = vp;
	fp->f_file = lfp;

	/* Put it on the tracking list */
	spin_lock(&vn_file_lock);
	list_add(&fp->f_list, &vn_file_list);
	spin_unlock(&vn_file_lock);

	mutex_exit(&fp->f_lock);
	return (fp);

out_vnode:
	vn_free(vp);
out_fget:
	fput(lfp);
out_mutex:
	mutex_exit(&fp->f_lock);
	kmem_cache_free(vn_file_cache, fp);
out:
	return (NULL);
} /* getf() */
EXPORT_SYMBOL(getf);

static void releasef_locked(file_t *fp)
{
	ASSERT(fp->f_file);
	ASSERT(fp->f_vnode);

	/* Unlinked from list, no refs, safe to free outside mutex */
	fput(fp->f_file);
	vn_free(fp->f_vnode);

	kmem_cache_free(vn_file_cache, fp);
}

void
vn_releasef(int fd)
{
	areleasef(fd, P_FINFO(current));
}
EXPORT_SYMBOL(releasef);

void
vn_areleasef(int fd, uf_info_t *fip)
{
	file_t *fp;
	struct task_struct *task = (struct task_struct *)fip;

	if (fd < 0)
		return;

	spin_lock(&vn_file_lock);
	fp = file_find(fd, task);
	if (fp) {
		atomic_dec(&fp->f_ref);
		if (atomic_read(&fp->f_ref) > 0) {
			spin_unlock(&vn_file_lock);
			return;
		}

		list_del(&fp->f_list);
		releasef_locked(fp);
	}
	spin_unlock(&vn_file_lock);
} /* releasef() */
EXPORT_SYMBOL(areleasef);

static int
vn_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	struct vnode *vp = buf;

	mutex_init(&vp->v_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
} /* vn_cache_constructor() */

static void
vn_cache_destructor(void *buf, void *cdrarg)
{
	struct vnode *vp = buf;

	mutex_destroy(&vp->v_lock);
} /* vn_cache_destructor() */

static int
vn_file_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	file_t *fp = buf;

	atomic_set(&fp->f_ref, 0);
	mutex_init(&fp->f_lock, NULL, MUTEX_DEFAULT, NULL);
	INIT_LIST_HEAD(&fp->f_list);

	return (0);
} /* vn_file_cache_constructor() */

static void
vn_file_cache_destructor(void *buf, void *cdrarg)
{
	file_t *fp = buf;

	mutex_destroy(&fp->f_lock);
} /* vn_file_cache_destructor() */

int
spl_vn_init(void)
{
	spin_lock_init(&vn_file_lock);

	vn_cache = kmem_cache_create("spl_vn_cache",
	    sizeof (struct vnode), 64, vn_cache_constructor,
	    vn_cache_destructor, NULL, NULL, NULL, 0);

	vn_file_cache = kmem_cache_create("spl_vn_file_cache",
	    sizeof (file_t), 64, vn_file_cache_constructor,
	    vn_file_cache_destructor, NULL, NULL, NULL, 0);

	return (0);
} /* spl_vn_init() */

void
spl_vn_fini(void)
{
	file_t *fp, *next_fp;
	int leaked = 0;

	spin_lock(&vn_file_lock);

	list_for_each_entry_safe(fp, next_fp, &vn_file_list,  f_list) {
		list_del(&fp->f_list);
		releasef_locked(fp);
		leaked++;
	}

	spin_unlock(&vn_file_lock);

	if (leaked > 0)
		printk(KERN_WARNING "WARNING: %d vnode files leaked\n", leaked);

	kmem_cache_destroy(vn_file_cache);
	kmem_cache_destroy(vn_cache);
} /* spl_vn_fini() */
