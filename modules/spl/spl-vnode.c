/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/sysmacros.h>
#include <sys/vnode.h>


#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_VNODE

void *rootdir = NULL;
EXPORT_SYMBOL(rootdir);

static spl_kmem_cache_t *vn_cache;
static spl_kmem_cache_t *vn_file_cache;

static spinlock_t vn_file_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(vn_file_list);

static vtype_t
vn_get_sol_type(umode_t mode)
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
} /* vn_get_sol_type() */

vnode_t *
vn_alloc(int flag)
{
	vnode_t *vp;
	ENTRY;

	vp = kmem_cache_alloc(vn_cache, flag);
	if (vp != NULL) {
		vp->v_file = NULL;
		vp->v_type = 0;
	}

	RETURN(vp);
} /* vn_alloc() */
EXPORT_SYMBOL(vn_alloc);

void
vn_free(vnode_t *vp)
{
	ENTRY;
	kmem_cache_free(vn_cache, vp);
	EXIT;
} /* vn_free() */
EXPORT_SYMBOL(vn_free);

int
vn_open(const char *path, uio_seg_t seg, int flags, int mode,
	vnode_t **vpp, int x1, void *x2)
{
        struct file *fp;
        struct kstat stat;
        int rc, saved_umask = 0;
	vnode_t *vp;
	ENTRY;

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
		RETURN(-PTR_ERR(fp));

        rc = vfs_getattr(fp->f_vfsmnt, fp->f_dentry, &stat);
	if (rc) {
		filp_close(fp, 0);
		RETURN(-rc);
	}

	vp = vn_alloc(KM_SLEEP);
	if (!vp) {
		filp_close(fp, 0);
		RETURN(ENOMEM);
	}

	mutex_enter(&vp->v_lock);
	vp->v_type = vn_get_sol_type(stat.mode);
	vp->v_file = fp;
	*vpp = vp;
	mutex_exit(&vp->v_lock);

	RETURN(0);
} /* vn_open() */
EXPORT_SYMBOL(vn_open);

int
vn_openat(const char *path, uio_seg_t seg, int flags, int mode,
	  vnode_t **vpp, int x1, void *x2, vnode_t *vp, int fd)
{
	char *realpath;
	int len, rc;
	ENTRY;

	ASSERT(vp == rootdir);

	len = strlen(path) + 2;
	realpath = kmalloc(len, GFP_KERNEL);
	if (!realpath)
		RETURN(ENOMEM);

	(void)snprintf(realpath, len, "/%s", path);
	rc = vn_open(realpath, seg, flags, mode, vpp, x1, x2);
	kfree(realpath);

	RETURN(rc);
} /* vn_openat() */
EXPORT_SYMBOL(vn_openat);

int
vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len, offset_t off,
	uio_seg_t seg, int x1, rlim64_t x2, void *x3, ssize_t *residp)
{
	loff_t offset;
	mm_segment_t saved_fs;
	struct file *fp;
	int rc;
	ENTRY;

	ASSERT(uio == UIO_WRITE || uio == UIO_READ);
	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(seg == UIO_SYSSPACE);
	ASSERT(x1 == 0);
	ASSERT(x2 == RLIM64_INFINITY);

	offset = off;
	fp = vp->v_file;

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

	if (rc < 0)
		RETURN(-rc);

	if (residp) {
		*residp = len - rc;
	} else {
		if (rc != len)
			RETURN(EIO);
	}

	RETURN(0);
} /* vn_rdwr() */
EXPORT_SYMBOL(vn_rdwr);

int
vn_close(vnode_t *vp, int flags, int x1, int x2, void *x3, void *x4)
{
	int rc;
	ENTRY;

	ASSERT(vp);
	ASSERT(vp->v_file);

	rc = filp_close(vp->v_file, 0);
	vn_free(vp);

	RETURN(-rc);
} /* vn_close() */
EXPORT_SYMBOL(vn_close);

/* vn_seek() does not actually seek it only performs bounds checking on the
 * proposed seek.  We perform minimal checking and allow vn_rdwr() to catch
 * anything more serious. */
int
vn_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, caller_context_t *ct)
{
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}
EXPORT_SYMBOL(vn_seek);

static struct dentry *
vn_lookup_hash(struct nameidata *nd)
{
	return lookup_one_len(nd->last.name, nd->nd_dentry, nd->last.len);
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
        ENTRY;

        ASSERT(seg == UIO_SYSSPACE);
        ASSERT(flags == RMFILE);

        rc = path_lookup(path, LOOKUP_PARENT, &nd);
        if (rc)
                GOTO(exit, rc);

        rc = -EISDIR;
        if (nd.last_type != LAST_NORM)
                GOTO(exit1, rc);

#ifdef HAVE_INODE_I_MUTEX
        mutex_lock_nested(&nd.nd_dentry->d_inode->i_mutex, I_MUTEX_PARENT);
#else
        down(&nd.nd_dentry->d_inode->i_sem);
#endif
        dentry = vn_lookup_hash(&nd);
        rc = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                /* Why not before? Because we want correct rc value */
                if (nd.last.name[nd.last.len])
                        GOTO(slashes, rc);

                inode = dentry->d_inode;
                if (inode)
                        atomic_inc(&inode->i_count);
                rc = vfs_unlink(nd.nd_dentry->d_inode, dentry);
exit2:
                dput(dentry);
        }
#ifdef HAVE_INODE_I_MUTEX
        mutex_unlock(&nd.nd_dentry->d_inode->i_mutex);
#else
        up(&nd.nd_dentry->d_inode->i_sem);
#endif
        if (inode)
                iput(inode);    /* truncate the inode here */
exit1:
        vn_path_release(&nd);
exit:
        RETURN(-rc);

slashes:
        rc = !dentry->d_inode ? -ENOENT :
                S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
        GOTO(exit2, rc);
} /* vn_remove() */
EXPORT_SYMBOL(vn_remove);

/* Modified do_rename() from linux/fs/namei.c, only uses exported symbols */
int
vn_rename(const char *oldname, const char *newname, int x1)
{
        struct dentry * old_dir, * new_dir;
        struct dentry * old_dentry, *new_dentry;
        struct dentry * trap;
        struct nameidata oldnd, newnd;
        int rc = 0;
	ENTRY;

        rc = path_lookup(oldname, LOOKUP_PARENT, &oldnd);
        if (rc)
                GOTO(exit, rc);

        rc = path_lookup(newname, LOOKUP_PARENT, &newnd);
        if (rc)
                GOTO(exit1, rc);

        rc = -EXDEV;
        if (oldnd.nd_mnt != newnd.nd_mnt)
                GOTO(exit2, rc);

        old_dir = oldnd.nd_dentry;
        rc = -EBUSY;
        if (oldnd.last_type != LAST_NORM)
                GOTO(exit2, rc);

        new_dir = newnd.nd_dentry;
        if (newnd.last_type != LAST_NORM)
                GOTO(exit2, rc);

        trap = lock_rename(new_dir, old_dir);

        old_dentry = vn_lookup_hash(&oldnd);

        rc = PTR_ERR(old_dentry);
        if (IS_ERR(old_dentry))
                GOTO(exit3, rc);

        /* source must exist */
        rc = -ENOENT;
        if (!old_dentry->d_inode)
                GOTO(exit4, rc);

        /* unless the source is a directory trailing slashes give -ENOTDIR */
        if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
                rc = -ENOTDIR;
                if (oldnd.last.name[oldnd.last.len])
                        GOTO(exit4, rc);
                if (newnd.last.name[newnd.last.len])
                        GOTO(exit4, rc);
        }

        /* source should not be ancestor of target */
        rc = -EINVAL;
        if (old_dentry == trap)
                GOTO(exit4, rc);

        new_dentry = vn_lookup_hash(&newnd);
        rc = PTR_ERR(new_dentry);
        if (IS_ERR(new_dentry))
                GOTO(exit4, rc);

        /* target should not be an ancestor of source */
        rc = -ENOTEMPTY;
        if (new_dentry == trap)
                GOTO(exit5, rc);

        rc = vfs_rename(old_dir->d_inode, old_dentry,
                        new_dir->d_inode, new_dentry);
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
        RETURN(-rc);
}
EXPORT_SYMBOL(vn_rename);

int
vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *x3, void *x4)
{
	struct file *fp;
        struct kstat stat;
	int rc;
	ENTRY;

	ASSERT(vp);
	ASSERT(vp->v_file);
	ASSERT(vap);

	fp = vp->v_file;

        rc = vfs_getattr(fp->f_vfsmnt, fp->f_dentry, &stat);
	if (rc)
		RETURN(-rc);

	vap->va_type          = vn_get_sol_type(stat.mode);
	vap->va_mode          = stat.mode;
	vap->va_uid           = stat.uid;
	vap->va_gid           = stat.gid;
	vap->va_fsid          = 0;
	vap->va_nodeid        = stat.ino;
	vap->va_nlink         = stat.nlink;
        vap->va_size          = stat.size;
	vap->va_blocksize     = stat.blksize;
	vap->va_atime.tv_sec  = stat.atime.tv_sec;
	vap->va_atime.tv_usec = stat.atime.tv_nsec / NSEC_PER_USEC;
	vap->va_mtime.tv_sec  = stat.mtime.tv_sec;
	vap->va_mtime.tv_usec = stat.mtime.tv_nsec / NSEC_PER_USEC;
	vap->va_ctime.tv_sec  = stat.ctime.tv_sec;
	vap->va_ctime.tv_usec = stat.ctime.tv_nsec / NSEC_PER_USEC;
	vap->va_rdev          = stat.rdev;
	vap->va_blocks        = stat.blocks;

        RETURN(0);
}
EXPORT_SYMBOL(vn_getattr);

int vn_fsync(vnode_t *vp, int flags, void *x3, void *x4)
{
	int datasync = 0;
	ENTRY;

	ASSERT(vp);
	ASSERT(vp->v_file);

	if (flags & FDSYNC)
		datasync = 1;

	RETURN(-file_fsync(vp->v_file, vp->v_file->f_dentry, datasync));
} /* vn_fsync() */
EXPORT_SYMBOL(vn_fsync);

/* Function must be called while holding the vn_file_lock */
static file_t *
file_find(int fd)
{
        file_t *fp;

	ASSERT(spin_is_locked(&vn_file_lock));

        list_for_each_entry(fp, &vn_file_list,  f_list) {
		if (fd == fp->f_fd) {
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
	ENTRY;

	/* Already open just take an extra reference */
	spin_lock(&vn_file_lock);

	fp = file_find(fd);
	if (fp) {
		atomic_inc(&fp->f_ref);
		spin_unlock(&vn_file_lock);
		RETURN(fp);
	}

	spin_unlock(&vn_file_lock);

	/* File was not yet opened create the object and setup */
	fp = kmem_cache_alloc(vn_file_cache, KM_SLEEP);
	if (fp == NULL)
		GOTO(out, rc);

	mutex_enter(&fp->f_lock);

	fp->f_fd = fd;
	fp->f_offset = 0;
	atomic_inc(&fp->f_ref);

	lfp = fget(fd);
	if (lfp == NULL)
		GOTO(out_mutex, rc);

	vp = vn_alloc(KM_SLEEP);
	if (vp == NULL)
		GOTO(out_fget, rc);

        if (vfs_getattr(lfp->f_vfsmnt, lfp->f_dentry, &stat))
		GOTO(out_vnode, rc);

	mutex_enter(&vp->v_lock);
	vp->v_type = vn_get_sol_type(stat.mode);
	vp->v_file = lfp;
	mutex_exit(&vp->v_lock);

	fp->f_vnode = vp;
	fp->f_file = lfp;

	/* Put it on the tracking list */
	spin_lock(&vn_file_lock);
	list_add(&fp->f_list, &vn_file_list);
	spin_unlock(&vn_file_lock);

	mutex_exit(&fp->f_lock);
	RETURN(fp);

out_vnode:
	vn_free(vp);
out_fget:
	fput(lfp);
out_mutex:
	mutex_exit(&fp->f_lock);
	kmem_cache_free(vn_file_cache, fp);
out:
        RETURN(NULL);
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
	ENTRY;

	spin_lock(&vn_file_lock);
	fp = file_find(fd);
	if (fp) {
		atomic_dec(&fp->f_ref);
		if (atomic_read(&fp->f_ref) > 0) {
			spin_unlock(&vn_file_lock);
			EXIT;
			return;
		}

	        list_del(&fp->f_list);
		releasef_locked(fp);
	}
	spin_unlock(&vn_file_lock);

	EXIT;
	return;
} /* releasef() */
EXPORT_SYMBOL(releasef);

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

int
vn_init(void)
{
	ENTRY;
	vn_cache = kmem_cache_create("spl_vn_cache",
				     sizeof(struct vnode), 64,
	                             vn_cache_constructor,
				     vn_cache_destructor,
				     NULL, NULL, NULL, 0);

	vn_file_cache = kmem_cache_create("spl_vn_file_cache",
					  sizeof(file_t), 64,
				          vn_file_cache_constructor,
				          vn_file_cache_destructor,
				          NULL, NULL, NULL, 0);
	RETURN(0);
} /* vn_init() */

void
vn_fini(void)
{
        file_t *fp, *next_fp;
	int leaked = 0;
	ENTRY;

	spin_lock(&vn_file_lock);

        list_for_each_entry_safe(fp, next_fp, &vn_file_list,  f_list) {
	        list_del(&fp->f_list);
		releasef_locked(fp);
		leaked++;
	}

	kmem_cache_destroy(vn_file_cache);
	vn_file_cache = NULL;
	spin_unlock(&vn_file_lock);

	if (leaked > 0)
		CWARN("Warning %d files leaked\n", leaked);

	kmem_cache_destroy(vn_cache);

	EXIT;
	return;
} /* vn_fini() */
