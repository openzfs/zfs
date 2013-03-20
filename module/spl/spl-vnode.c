/*****************************************************************************\
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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Vnode Implementation.
\*****************************************************************************/

#include <sys/vnode.h>
#include <linux/falloc.h>
#include <spl-debug.h>

#ifdef SS_DEBUG_SUBSYS
#undef SS_DEBUG_SUBSYS
#endif

#define SS_DEBUG_SUBSYS SS_VNODE

vnode_t *rootdir = (vnode_t *)0xabcd1234;
EXPORT_SYMBOL(rootdir);

static spl_kmem_cache_t *vn_cache;
static spl_kmem_cache_t *vn_file_cache;

static DEFINE_SPINLOCK(vn_file_lock);
static LIST_HEAD(vn_file_list);

#ifdef HAVE_KERN_PATH_PARENT_HEADER
#ifndef HAVE_KERN_PATH_PARENT_SYMBOL
kern_path_parent_t kern_path_parent_fn = SYMBOL_POISON;
EXPORT_SYMBOL(kern_path_parent_fn);
#endif /* HAVE_KERN_PATH_PARENT_SYMBOL */
#endif /* HAVE_KERN_PATH_PARENT_HEADER */

#ifdef HAVE_KERN_PATH_LOCKED
kern_path_locked_t kern_path_locked_fn = SYMBOL_POISON;
#endif /* HAVE_KERN_PATH_LOCKED */

vtype_t
vn_mode_to_vtype(mode_t mode)
{
	if (S_ISREG(mode))
		return VREG;

	if (S_ISDIR(mode))
		return VDIR;

	if (S_ISCHR(mode))
		return VCHR;

	if (S_ISBLK(mode))
		return VBLK;

	if (S_ISFIFO(mode))
		return VFIFO;

	if (S_ISLNK(mode))
		return VLNK;

	if (S_ISSOCK(mode))
		return VSOCK;

	if (S_ISCHR(mode))
		return VCHR;

	return VNON;
} /* vn_mode_to_vtype() */
EXPORT_SYMBOL(vn_mode_to_vtype);

mode_t
vn_vtype_to_mode(vtype_t vtype)
{
	if (vtype == VREG)
		return S_IFREG;

	if (vtype == VDIR)
		return S_IFDIR;

	if (vtype == VCHR)
		return S_IFCHR;

	if (vtype == VBLK)
		return S_IFBLK;

	if (vtype == VFIFO)
		return S_IFIFO;

	if (vtype == VLNK)
		return S_IFLNK;

	if (vtype == VSOCK)
		return S_IFSOCK;

	return VNON;
} /* vn_vtype_to_mode() */
EXPORT_SYMBOL(vn_vtype_to_mode);

vnode_t *
vn_alloc(int flag)
{
	vnode_t *vp;
	SENTRY;

	vp = kmem_cache_alloc(vn_cache, flag);
	if (vp != NULL) {
		vp->v_file = NULL;
		vp->v_type = 0;
	}

	SRETURN(vp);
} /* vn_alloc() */
EXPORT_SYMBOL(vn_alloc);

void
vn_free(vnode_t *vp)
{
	SENTRY;
	kmem_cache_free(vn_cache, vp);
	SEXIT;
} /* vn_free() */
EXPORT_SYMBOL(vn_free);

int
vn_open(const char *path, uio_seg_t seg, int flags, int mode,
	vnode_t **vpp, int x1, void *x2)
{
	struct file *fp;
	struct kstat stat;
	int rc, saved_umask = 0;
	gfp_t saved_gfp;
	vnode_t *vp;
	SENTRY;

	ASSERT(flags & (FWRITE | FREAD));
	ASSERT(seg == UIO_SYSSPACE);
	ASSERT(vpp);
	*vpp = NULL;

	if (!(flags & FCREAT) && (flags & FWRITE))
		flags |= FEXCL;

	/* Note for filp_open() the two low bits must be remapped to mean:
	 * 01 - read-only  -> 00 read-only
	 * 10 - write-only -> 01 write-only
	 * 11 - read-write -> 10 read-write
	 */
	flags--;

	if (flags & FCREAT)
		saved_umask = xchg(&current->fs->umask, 0);

	fp = filp_open(path, flags, mode);

	if (flags & FCREAT)
		(void)xchg(&current->fs->umask, saved_umask);

	if (IS_ERR(fp))
		SRETURN(-PTR_ERR(fp));

#ifdef HAVE_2ARGS_VFS_GETATTR
	rc = vfs_getattr(&fp->f_path, &stat);
#else
	rc = vfs_getattr(fp->f_path.mnt, fp->f_dentry, &stat);
#endif
	if (rc) {
		filp_close(fp, 0);
		SRETURN(-rc);
	}

	vp = vn_alloc(KM_SLEEP);
	if (!vp) {
		filp_close(fp, 0);
		SRETURN(ENOMEM);
	}

	saved_gfp = mapping_gfp_mask(fp->f_mapping);
	mapping_set_gfp_mask(fp->f_mapping, saved_gfp & ~(__GFP_IO|__GFP_FS));

	mutex_enter(&vp->v_lock);
	vp->v_type = vn_mode_to_vtype(stat.mode);
	vp->v_file = fp;
	vp->v_gfp_mask = saved_gfp;
	*vpp = vp;
	mutex_exit(&vp->v_lock);

	SRETURN(0);
} /* vn_open() */
EXPORT_SYMBOL(vn_open);

int
vn_openat(const char *path, uio_seg_t seg, int flags, int mode,
	  vnode_t **vpp, int x1, void *x2, vnode_t *vp, int fd)
{
	char *realpath;
	int len, rc;
	SENTRY;

	ASSERT(vp == rootdir);

	len = strlen(path) + 2;
	realpath = kmalloc(len, GFP_KERNEL);
	if (!realpath)
		SRETURN(ENOMEM);

	(void)snprintf(realpath, len, "/%s", path);
	rc = vn_open(realpath, seg, flags, mode, vpp, x1, x2);
	kfree(realpath);

	SRETURN(rc);
} /* vn_openat() */
EXPORT_SYMBOL(vn_openat);

int
vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len, offset_t off,
	uio_seg_t seg, int ioflag, rlim64_t x2, void *x3, ssize_t *residp)
{
	loff_t offset;
	mm_segment_t saved_fs;
	struct file *fp;
	int rc;
	SENTRY;

	ASSERT(uio == UIO_WRITE || uio == UIO_READ);
	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(seg == UIO_SYSSPACE);
	ASSERT((ioflag & ~FAPPEND) == 0);
	ASSERT(x2 == RLIM64_INFINITY);

	fp = vp->v_file;

	offset = off;
	if (ioflag & FAPPEND)
		offset = fp->f_pos;

	/* Writable user data segment must be briefly increased for this
	 * process so we can use the user space read call paths to write
	 * in to memory allocated by the kernel. */
	saved_fs = get_fs();
        set_fs(get_ds());

	if (uio & UIO_WRITE)
		rc = vfs_write(fp, addr, len, &offset);
	else
		rc = vfs_read(fp, addr, len, &offset);

	set_fs(saved_fs);
	fp->f_pos = offset;

	if (rc < 0)
		SRETURN(-rc);

	if (residp) {
		*residp = len - rc;
	} else {
		if (rc != len)
			SRETURN(EIO);
	}

	SRETURN(0);
} /* vn_rdwr() */
EXPORT_SYMBOL(vn_rdwr);

int
vn_close(vnode_t *vp, int flags, int x1, int x2, void *x3, void *x4)
{
	int rc;
	SENTRY;

	ASSERT(vp);
	ASSERT(vp->v_file);

	mapping_set_gfp_mask(vp->v_file->f_mapping, vp->v_gfp_mask);
	rc = filp_close(vp->v_file, 0);
	vn_free(vp);

	SRETURN(-rc);
} /* vn_close() */
EXPORT_SYMBOL(vn_close);

/* vn_seek() does not actually seek it only performs bounds checking on the
 * proposed seek.  We perform minimal checking and allow vn_rdwr() to catch
 * anything more serious. */
int
vn_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, void *ct)
{
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}
EXPORT_SYMBOL(vn_seek);

#ifdef HAVE_KERN_PATH_LOCKED
/* Based on do_unlinkat() from linux/fs/namei.c */
int
vn_remove(const char *path, uio_seg_t seg, int flags)
{
	struct dentry *dentry;
	struct path parent;
	struct inode *inode = NULL;
	int rc = 0;
	SENTRY;

	ASSERT(seg == UIO_SYSSPACE);
	ASSERT(flags == RMFILE);

	dentry = spl_kern_path_locked(path, &parent);
	rc = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		if (parent.dentry->d_name.name[parent.dentry->d_name.len])
			SGOTO(slashes, rc = 0);

		inode = dentry->d_inode;
		if (!inode)
			SGOTO(slashes, rc = 0);

		if (inode)
			ihold(inode);

		rc = vfs_unlink(parent.dentry->d_inode, dentry);
exit1:
		dput(dentry);
	} else {
		return (-rc);
	}

	spl_inode_unlock(parent.dentry->d_inode);
	if (inode)
		iput(inode);    /* truncate the inode here */

	path_put(&parent);
	SRETURN(-rc);

slashes:
	rc = !dentry->d_inode ? -ENOENT :
	    S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
	SGOTO(exit1, rc);
} /* vn_remove() */
EXPORT_SYMBOL(vn_remove);

/* Based on do_rename() from linux/fs/namei.c */
int
vn_rename(const char *oldname, const char *newname, int x1)
{
	struct dentry *old_dir, *new_dir;
	struct dentry *old_dentry, *new_dentry;
	struct dentry *trap;
	struct path old_parent, new_parent;
	int rc = 0;
	SENTRY;

	old_dentry = spl_kern_path_locked(oldname, &old_parent);
	if (IS_ERR(old_dentry))
		SGOTO(exit, rc = PTR_ERR(old_dentry));

	spl_inode_unlock(old_parent.dentry->d_inode);

	new_dentry = spl_kern_path_locked(newname, &new_parent);
	if (IS_ERR(new_dentry))
		SGOTO(exit2, rc = PTR_ERR(new_dentry));

	spl_inode_unlock(new_parent.dentry->d_inode);

	rc = -EXDEV;
	if (old_parent.mnt != new_parent.mnt)
		SGOTO(exit3, rc);

	old_dir = old_parent.dentry;
	new_dir = new_parent.dentry;
	trap = lock_rename(new_dir, old_dir);

	/* source should not be ancestor of target */
	rc = -EINVAL;
	if (old_dentry == trap)
		SGOTO(exit4, rc);

	/* target should not be an ancestor of source */
	rc = -ENOTEMPTY;
	if (new_dentry == trap)
		SGOTO(exit4, rc);

	/* source must exist */
	rc = -ENOENT;
	if (!old_dentry->d_inode)
		SGOTO(exit4, rc);

	/* unless the source is a directory trailing slashes give -ENOTDIR */
	if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
		rc = -ENOTDIR;
		if (old_dentry->d_name.name[old_dentry->d_name.len])
			SGOTO(exit4, rc);
		if (new_dentry->d_name.name[new_dentry->d_name.len])
			SGOTO(exit4, rc);
	}

#ifdef HAVE_4ARGS_VFS_RENAME
	rc = vfs_rename(old_dir->d_inode, old_dentry,
			new_dir->d_inode, new_dentry);
#else
	rc = vfs_rename(old_dir->d_inode, old_dentry, oldnd.nd_mnt,
			new_dir->d_inode, new_dentry, newnd.nd_mnt);
#endif /* HAVE_4ARGS_VFS_RENAME */
exit4:
	unlock_rename(new_dir, old_dir);
exit3:
	dput(new_dentry);
	path_put(&new_parent);
exit2:
	dput(old_dentry);
	path_put(&old_parent);
exit:
	SRETURN(-rc);
}
EXPORT_SYMBOL(vn_rename);

#else
static struct dentry *
vn_lookup_hash(struct nameidata *nd)
{
	return lookup_one_len((const char *)nd->last.name,
			      nd->nd_dentry, nd->last.len);
} /* lookup_hash() */

static void
vn_path_release(struct nameidata *nd)
{
	dput(nd->nd_dentry);
	mntput(nd->nd_mnt);
}

/* Modified do_unlinkat() from linux/fs/namei.c, only uses exported symbols */
int
vn_remove(const char *path, uio_seg_t seg, int flags)
{
        struct dentry *dentry;
        struct nameidata nd;
        struct inode *inode = NULL;
        int rc = 0;
        SENTRY;

        ASSERT(seg == UIO_SYSSPACE);
        ASSERT(flags == RMFILE);

	rc = spl_kern_path_parent(path, &nd);
        if (rc)
                SGOTO(exit, rc);

        rc = -EISDIR;
        if (nd.last_type != LAST_NORM)
                SGOTO(exit1, rc);

        spl_inode_lock_nested(nd.nd_dentry->d_inode, I_MUTEX_PARENT);
        dentry = vn_lookup_hash(&nd);
        rc = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                /* Why not before? Because we want correct rc value */
                if (nd.last.name[nd.last.len])
                        SGOTO(slashes, rc);

                inode = dentry->d_inode;
                if (inode)
                        atomic_inc(&inode->i_count);
#ifdef HAVE_2ARGS_VFS_UNLINK
                rc = vfs_unlink(nd.nd_dentry->d_inode, dentry);
#else
                rc = vfs_unlink(nd.nd_dentry->d_inode, dentry, nd.nd_mnt);
#endif /* HAVE_2ARGS_VFS_UNLINK */
exit2:
                dput(dentry);
        }

        spl_inode_unlock(nd.nd_dentry->d_inode);
        if (inode)
                iput(inode);    /* truncate the inode here */
exit1:
        vn_path_release(&nd);
exit:
        SRETURN(-rc);

slashes:
        rc = !dentry->d_inode ? -ENOENT :
                S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
        SGOTO(exit2, rc);
} /* vn_remove() */
EXPORT_SYMBOL(vn_remove);

/* Modified do_rename() from linux/fs/namei.c, only uses exported symbols */
int
vn_rename(const char *oldname, const char *newname, int x1)
{
        struct dentry *old_dir, *new_dir;
        struct dentry *old_dentry, *new_dentry;
        struct dentry *trap;
        struct nameidata oldnd, newnd;
        int rc = 0;
	SENTRY;

        rc = spl_kern_path_parent(oldname, &oldnd);
        if (rc)
                SGOTO(exit, rc);

        rc = spl_kern_path_parent(newname, &newnd);
        if (rc)
                SGOTO(exit1, rc);

        rc = -EXDEV;
        if (oldnd.nd_mnt != newnd.nd_mnt)
                SGOTO(exit2, rc);

        old_dir = oldnd.nd_dentry;
        rc = -EBUSY;
        if (oldnd.last_type != LAST_NORM)
                SGOTO(exit2, rc);

        new_dir = newnd.nd_dentry;
        if (newnd.last_type != LAST_NORM)
                SGOTO(exit2, rc);

        trap = lock_rename(new_dir, old_dir);

        old_dentry = vn_lookup_hash(&oldnd);

        rc = PTR_ERR(old_dentry);
        if (IS_ERR(old_dentry))
                SGOTO(exit3, rc);

        /* source must exist */
        rc = -ENOENT;
        if (!old_dentry->d_inode)
                SGOTO(exit4, rc);

        /* unless the source is a directory trailing slashes give -ENOTDIR */
        if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
                rc = -ENOTDIR;
                if (oldnd.last.name[oldnd.last.len])
                        SGOTO(exit4, rc);
                if (newnd.last.name[newnd.last.len])
                        SGOTO(exit4, rc);
        }

        /* source should not be ancestor of target */
        rc = -EINVAL;
        if (old_dentry == trap)
                SGOTO(exit4, rc);

        new_dentry = vn_lookup_hash(&newnd);
        rc = PTR_ERR(new_dentry);
        if (IS_ERR(new_dentry))
                SGOTO(exit4, rc);

        /* target should not be an ancestor of source */
        rc = -ENOTEMPTY;
        if (new_dentry == trap)
                SGOTO(exit5, rc);

#ifdef HAVE_4ARGS_VFS_RENAME
        rc = vfs_rename(old_dir->d_inode, old_dentry,
                        new_dir->d_inode, new_dentry);
#else
        rc = vfs_rename(old_dir->d_inode, old_dentry, oldnd.nd_mnt,
                        new_dir->d_inode, new_dentry, newnd.nd_mnt);
#endif /* HAVE_4ARGS_VFS_RENAME */
exit5:
        dput(new_dentry);
exit4:
        dput(old_dentry);
exit3:
        unlock_rename(new_dir, old_dir);
exit2:
        vn_path_release(&newnd);
exit1:
        vn_path_release(&oldnd);
exit:
        SRETURN(-rc);
}
EXPORT_SYMBOL(vn_rename);
#endif /* HAVE_KERN_PATH_LOCKED */

int
vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *x3, void *x4)
{
	struct file *fp;
	struct kstat stat;
	int rc;
	SENTRY;

	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(vap);

	fp = vp->v_file;

#ifdef HAVE_2ARGS_VFS_GETATTR
	rc = vfs_getattr(&fp->f_path, &stat);
#else
	rc = vfs_getattr(fp->f_path.mnt, fp->f_dentry, &stat);
#endif
	if (rc)
		SRETURN(-rc);

	vap->va_type          = vn_mode_to_vtype(stat.mode);
	vap->va_mode          = stat.mode;
	vap->va_uid           = stat.uid;
	vap->va_gid           = stat.gid;
	vap->va_fsid          = 0;
	vap->va_nodeid        = stat.ino;
	vap->va_nlink         = stat.nlink;
        vap->va_size          = stat.size;
	vap->va_blksize       = stat.blksize;
	vap->va_atime         = stat.atime;
	vap->va_mtime         = stat.mtime;
	vap->va_ctime         = stat.ctime;
	vap->va_rdev          = stat.rdev;
	vap->va_nblocks       = stat.blocks;

	SRETURN(0);
}
EXPORT_SYMBOL(vn_getattr);

int vn_fsync(vnode_t *vp, int flags, void *x3, void *x4)
{
	int datasync = 0;
	SENTRY;

	ASSERT(vp);
	ASSERT(vp->v_file);

	if (flags & FDSYNC)
		datasync = 1;

	SRETURN(-spl_filp_fsync(vp->v_file, datasync));
} /* vn_fsync() */
EXPORT_SYMBOL(vn_fsync);

int vn_space(vnode_t *vp, int cmd, struct flock *bfp, int flag,
    offset_t offset, void *x6, void *x7)
{
	int error = EOPNOTSUPP;
	SENTRY;

	if (cmd != F_FREESP || bfp->l_whence != 0)
		SRETURN(EOPNOTSUPP);

	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(bfp->l_start >= 0 && bfp->l_len > 0);

#ifdef FALLOC_FL_PUNCH_HOLE
	/*
	 * When supported by the underlying file system preferentially
	 * use the fallocate() callback to preallocate the space.
	 */
	error = -spl_filp_fallocate(vp->v_file,
	    FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
	    bfp->l_start, bfp->l_len);
	if (error == 0)
		SRETURN(0);
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
				SRETURN(0);
		}
		--end;

		vp->v_file->f_dentry->d_inode->i_op->truncate_range(
			vp->v_file->f_dentry->d_inode,
			bfp->l_start, end
		);
		SRETURN(0);
	}
#endif

	SRETURN(error);
}
EXPORT_SYMBOL(vn_space);

/* Function must be called while holding the vn_file_lock */
static file_t *
file_find(int fd)
{
        file_t *fp;

	ASSERT(spin_is_locked(&vn_file_lock));

        list_for_each_entry(fp, &vn_file_list,  f_list) {
		if (fd == fp->f_fd && fp->f_task == current) {
			ASSERT(atomic_read(&fp->f_ref) != 0);
                        return fp;
		}
	}

        return NULL;
} /* file_find() */

file_t *
vn_getf(int fd)
{
        struct kstat stat;
	struct file *lfp;
	file_t *fp;
	vnode_t *vp;
	int rc = 0;
	SENTRY;

	/* Already open just take an extra reference */
	spin_lock(&vn_file_lock);

	fp = file_find(fd);
	if (fp) {
		atomic_inc(&fp->f_ref);
		spin_unlock(&vn_file_lock);
		SRETURN(fp);
	}

	spin_unlock(&vn_file_lock);

	/* File was not yet opened create the object and setup */
	fp = kmem_cache_alloc(vn_file_cache, KM_SLEEP);
	if (fp == NULL)
		SGOTO(out, rc);

	mutex_enter(&fp->f_lock);

	fp->f_fd = fd;
	fp->f_task = current;
	fp->f_offset = 0;
	atomic_inc(&fp->f_ref);

	lfp = fget(fd);
	if (lfp == NULL)
		SGOTO(out_mutex, rc);

	vp = vn_alloc(KM_SLEEP);
	if (vp == NULL)
		SGOTO(out_fget, rc);

#ifdef HAVE_2ARGS_VFS_GETATTR
	rc = vfs_getattr(&lfp->f_path, &stat);
#else
	rc = vfs_getattr(lfp->f_path.mnt, lfp->f_dentry, &stat);
#endif
        if (rc)
		SGOTO(out_vnode, rc);

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
	SRETURN(fp);

out_vnode:
	vn_free(vp);
out_fget:
	fput(lfp);
out_mutex:
	mutex_exit(&fp->f_lock);
	kmem_cache_free(vn_file_cache, fp);
out:
        SRETURN(NULL);
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
	file_t *fp;
	SENTRY;

	spin_lock(&vn_file_lock);
	fp = file_find(fd);
	if (fp) {
		atomic_dec(&fp->f_ref);
		if (atomic_read(&fp->f_ref) > 0) {
			spin_unlock(&vn_file_lock);
			SEXIT;
			return;
		}

	        list_del(&fp->f_list);
		releasef_locked(fp);
	}
	spin_unlock(&vn_file_lock);

	SEXIT;
	return;
} /* releasef() */
EXPORT_SYMBOL(releasef);

#ifndef HAVE_SET_FS_PWD
void
#  ifdef HAVE_SET_FS_PWD_WITH_CONST
set_fs_pwd(struct fs_struct *fs, const struct path *path)
#  else
set_fs_pwd(struct fs_struct *fs, struct path *path)
#  endif
{
	struct path old_pwd;

#  ifdef HAVE_FS_STRUCT_SPINLOCK
	spin_lock(&fs->lock);
	old_pwd = fs->pwd;
	fs->pwd = *path;
	path_get(path);
	spin_unlock(&fs->lock);
#  else
	write_lock(&fs->lock);
	old_pwd = fs->pwd;
	fs->pwd = *path;
	path_get(path);
	write_unlock(&fs->lock);
#  endif /* HAVE_FS_STRUCT_SPINLOCK */

	if (old_pwd.dentry)
		path_put(&old_pwd);
}
#endif /* HAVE_SET_FS_PWD */

int
vn_set_pwd(const char *filename)
{
#ifdef HAVE_USER_PATH_DIR
        struct path path;
#else
        struct nameidata nd;
#endif /* HAVE_USER_PATH_DIR */
        mm_segment_t saved_fs;
        int rc;
        SENTRY;

        /*
         * user_path_dir() and __user_walk() both expect 'filename' to be
         * a user space address so we must briefly increase the data segment
         * size to ensure strncpy_from_user() does not fail with -EFAULT.
         */
        saved_fs = get_fs();
        set_fs(get_ds());

# ifdef HAVE_USER_PATH_DIR
        rc = user_path_dir(filename, &path);
        if (rc)
                SGOTO(out, rc);

        rc = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_ACCESS);
        if (rc)
                SGOTO(dput_and_out, rc);

        set_fs_pwd(current->fs, &path);

dput_and_out:
        path_put(&path);
# else
        rc = __user_walk(filename,
                         LOOKUP_FOLLOW|LOOKUP_DIRECTORY|LOOKUP_CHDIR, &nd);
        if (rc)
                SGOTO(out, rc);

        rc = vfs_permission(&nd, MAY_EXEC);
        if (rc)
                SGOTO(dput_and_out, rc);

        set_fs_pwd(current->fs, &nd.path);

dput_and_out:
        path_put(&nd.path);
# endif /* HAVE_USER_PATH_DIR */
out:
	set_fs(saved_fs);

        SRETURN(-rc);
} /* vn_set_pwd() */
EXPORT_SYMBOL(vn_set_pwd);

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
} /* file_cache_constructor() */

static void
vn_file_cache_destructor(void *buf, void *cdrarg)
{
	file_t *fp = buf;

	mutex_destroy(&fp->f_lock);
} /* vn_file_cache_destructor() */

int spl_vn_init_kallsyms_lookup(void)
{
#ifdef HAVE_KERN_PATH_PARENT_HEADER
#ifndef HAVE_KERN_PATH_PARENT_SYMBOL
	kern_path_parent_fn = (kern_path_parent_t)
		spl_kallsyms_lookup_name("kern_path_parent");
	if (!kern_path_parent_fn) {
		printk(KERN_ERR "Error: Unknown symbol kern_path_parent\n");
		return -EFAULT;
	}
#endif /* HAVE_KERN_PATH_PARENT_SYMBOL */
#endif /* HAVE_KERN_PATH_PARENT_HEADER */

#ifdef HAVE_KERN_PATH_LOCKED
        kern_path_locked_fn = (kern_path_locked_t)
                spl_kallsyms_lookup_name("kern_path_locked");
        if (!kern_path_locked_fn) {
                printk(KERN_ERR "Error: Unknown symbol kern_path_locked\n");
                return -EFAULT;
        }
#endif

	return (0);
}

int
spl_vn_init(void)
{
	SENTRY;
	vn_cache = kmem_cache_create("spl_vn_cache",
				     sizeof(struct vnode), 64,
	                             vn_cache_constructor,
				     vn_cache_destructor,
				     NULL, NULL, NULL, KMC_KMEM);

	vn_file_cache = kmem_cache_create("spl_vn_file_cache",
					  sizeof(file_t), 64,
				          vn_file_cache_constructor,
				          vn_file_cache_destructor,
				          NULL, NULL, NULL, KMC_KMEM);
	SRETURN(0);
} /* vn_init() */

void
spl_vn_fini(void)
{
        file_t *fp, *next_fp;
	int leaked = 0;
	SENTRY;

	spin_lock(&vn_file_lock);

        list_for_each_entry_safe(fp, next_fp, &vn_file_list,  f_list) {
	        list_del(&fp->f_list);
		releasef_locked(fp);
		leaked++;
	}

	spin_unlock(&vn_file_lock);

	if (leaked > 0)
		SWARN("Warning %d files leaked\n", leaked);

	kmem_cache_destroy(vn_file_cache);
	kmem_cache_destroy(vn_cache);

	SEXIT;
	return;
} /* vn_fini() */
