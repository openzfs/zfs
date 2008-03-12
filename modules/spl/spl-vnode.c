#include <sys/sysmacros.h>
#include <sys/vnode.h>
#include "config.h"

void *rootdir = NULL;
EXPORT_SYMBOL(rootdir);

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

int
vn_open(const char *path, int seg, int flags, int mode,
	vnode_t **vpp, int x1, void *x2)
{
        struct file *fp;
        struct kstat stat;
        int rc, saved_umask, flags_rw;
	vnode_t *vp;

	BUG_ON(seg != UIO_SYSSPACE);
	BUG_ON(!vpp);
	*vpp = NULL;

	if (!(flags & FCREAT) && (flags & FWRITE))
		flags |= FEXCL;

	flags_rw = flags & (FWRITE | FREAD);
	flags &= ~(FWRITE | FREAD);
	switch (flags_rw) {
		case FWRITE:		flags |= O_WRONLY;
		case FREAD:		flags |= O_RDONLY;
		case (FWRITE | FREAD):	flags |= O_RDWR;
	}

	if (flags & FCREAT)
		saved_umask = xchg(&current->fs->umask, 0);

        fp = filp_open(path, flags, mode);

	if (flags & FCREAT)
		(void)xchg(&current->fs->umask, saved_umask);

        if (IS_ERR(fp))
		return PTR_ERR(fp);

        rc = vfs_getattr(fp->f_vfsmnt, fp->f_dentry, &stat);
	if (rc) {
		filp_close(fp, 0);
		return rc;
	}

	vp = kmalloc(sizeof(vnode_t), GFP_ATOMIC);
	if (!vp) {
		filp_close(fp, 0);
		return -ENOMEM;
	}

	vp->v_type = vn_get_sol_type(stat.mode);
	vp->v_fp = fp;
	*vpp = vp;

	return 0;
} /* vn_open() */
EXPORT_SYMBOL(vn_open);

int
vn_openat(const char *path, int seg, int flags, int mode,
	  vnode_t **vpp, int x1, void *x2, vnode_t *vp, int fd)
{
	char *realpath;
	int rc;

	BUG_ON(vp != rootdir);

	realpath = kmalloc(strlen(path) + 2, GFP_KERNEL);
	if (!realpath)
		return -ENOMEM;

	sprintf(realpath, "/%s", path);
	rc = vn_open(realpath, seg, flags, mode, vpp, x1, x2);

	kfree(realpath);

	return rc;
} /* vn_openat() */
EXPORT_SYMBOL(vn_openat);

int
vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len, offset_t off,
	int seg, int x1, rlim64_t x2, void *x3, ssize_t *residp)
{
	loff_t offset;
	mm_segment_t saved_fs;
	struct file *fp;
	int rc;

	BUG_ON(!(uio == UIO_WRITE || uio == UIO_READ));
	BUG_ON(!vp);
	BUG_ON(!vp->v_fp);
	BUG_ON(seg != UIO_SYSSPACE);
	BUG_ON(x1 != 0);
	BUG_ON(x2 != RLIM64_INFINITY);

	offset = off;
	fp = vp->v_fp;

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
		return rc;

	if (residp) {
		*residp = len - rc;
	} else {
		if (rc != len)
			return -EIO;
	}

	return 0;
} /* vn_rdwr() */
EXPORT_SYMBOL(vn_rdwr);

int
vn_close(vnode_t *vp, int flags, int x1, int x2, int x3, int x4)
{
	int rc;

	BUG_ON(!vp);
	BUG_ON(!vp->v_fp);

        rc = filp_close(vp->v_fp, 0);
        kfree(vp);

	return rc;
} /* vn_close() */
EXPORT_SYMBOL(vn_close);

static struct dentry *lookup_hash(struct nameidata *nd)
{
	return __lookup_hash(&nd->last, nd->dentry, nd);
} /* lookup_hash() */

/* Modified do_unlinkat() from linux/fs/namei.c, only uses exported symbols */
int
vn_remove(const char *path, int x1, int x2)
{
        struct dentry *dentry;
        struct nameidata nd;
        struct inode *inode = NULL;
        int rc = 0;

        rc = path_lookup(path, LOOKUP_PARENT, &nd);
        if (rc)
                goto exit;

        rc = -EISDIR;
        if (nd.last_type != LAST_NORM)
                goto exit1;

        mutex_lock_nested(&nd.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
        dentry = lookup_hash(&nd);
        rc = PTR_ERR(dentry);
        if (!IS_ERR(dentry)) {
                /* Why not before? Because we want correct rc value */
                if (nd.last.name[nd.last.len])
                        goto slashes;
                inode = dentry->d_inode;
                if (inode)
                        atomic_inc(&inode->i_count);
                rc = vfs_unlink(nd.dentry->d_inode, dentry);
exit2:
                dput(dentry);
        }
        mutex_unlock(&nd.dentry->d_inode->i_mutex);
        if (inode)
                iput(inode);    /* truncate the inode here */
exit1:
        path_release(&nd);
exit:
        return rc;

slashes:
        rc = !dentry->d_inode ? -ENOENT :
                S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
        goto exit2;
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

        rc = path_lookup(oldname, LOOKUP_PARENT, &oldnd);
        if (rc)
                goto exit;

        rc = path_lookup(newname, LOOKUP_PARENT, &newnd);
        if (rc)
                goto exit1;

        rc = -EXDEV;
        if (oldnd.mnt != newnd.mnt)
                goto exit2;

        old_dir = oldnd.dentry;
        rc = -EBUSY;
        if (oldnd.last_type != LAST_NORM)
                goto exit2;

        new_dir = newnd.dentry;
        if (newnd.last_type != LAST_NORM)
                goto exit2;

        trap = lock_rename(new_dir, old_dir);

        old_dentry = lookup_hash(&oldnd);

        rc = PTR_ERR(old_dentry);
        if (IS_ERR(old_dentry))
                goto exit3;

        /* source must exist */
        rc = -ENOENT;
        if (!old_dentry->d_inode)
                goto exit4;

        /* unless the source is a directory trailing slashes give -ENOTDIR */
        if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
                rc = -ENOTDIR;
                if (oldnd.last.name[oldnd.last.len])
                        goto exit4;
                if (newnd.last.name[newnd.last.len])
                        goto exit4;
        }

        /* source should not be ancestor of target */
        rc = -EINVAL;
        if (old_dentry == trap)
                goto exit4;

        new_dentry = lookup_hash(&newnd);
        rc = PTR_ERR(new_dentry);
        if (IS_ERR(new_dentry))
                goto exit4;

        /* target should not be an ancestor of source */
        rc = -ENOTEMPTY;
        if (new_dentry == trap)
                goto exit5;

        rc = vfs_rename(old_dir->d_inode, old_dentry,
                        new_dir->d_inode, new_dentry);
exit5:
        dput(new_dentry);
exit4:
        dput(old_dentry);
exit3:
        unlock_rename(new_dir, old_dir);
exit2:
        path_release(&newnd);
exit1:
        path_release(&oldnd);
exit:
        return rc;
}
EXPORT_SYMBOL(vn_rename);

int
vn_getattr(vnode_t *vp, vattr_t *vap, int flags, int x3, void *x4)
{
	struct file *fp;
        struct kstat stat;
	int rc;

	BUG_ON(!vp);
	BUG_ON(!vp->v_fp);
	BUG_ON(!vap);

	fp = vp->v_fp;

        rc = vfs_getattr(fp->f_vfsmnt, fp->f_dentry, &stat);
	if (rc)
		return rc;

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

        return rc;
}
EXPORT_SYMBOL(vn_getattr);

int vn_fsync(vnode_t *vp, int flags, int x3, int x4)
{
	BUG_ON(!vp);
	BUG_ON(!vp->v_fp);

	return file_fsync(vp->v_fp, vp->v_fp->f_dentry, 0);
} /* vn_fsync() */
EXPORT_SYMBOL(vn_fsync);
