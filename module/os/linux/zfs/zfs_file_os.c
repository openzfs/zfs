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

#include <sys/zfs_context.h>
#include <sys/zfs_file.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#ifdef HAVE_FDTABLE_HEADER
#include <linux/fdtable.h>
#endif

/*
 * Open file
 *
 * path - fully qualified path to file
 * flags - file attributes O_READ / O_WRITE / O_EXCL
 * fpp - pointer to return file pointer
 *
 * Returns 0 on success underlying error on failure.
 */
int
zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fpp)
{
	struct file *filp;
	int saved_umask;

	if (!(flags & O_CREAT) && (flags & O_WRONLY))
		flags |= O_EXCL;

	if (flags & O_CREAT)
		saved_umask = xchg(&current->fs->umask, 0);

	filp = filp_open(path, flags, mode);

	if (flags & O_CREAT)
		(void) xchg(&current->fs->umask, saved_umask);

	if (IS_ERR(filp))
		return (-PTR_ERR(filp));

	*fpp = filp;
	return (0);
}

void
zfs_file_close(zfs_file_t *fp)
{
	filp_close(fp, 0);
}

static ssize_t
zfs_file_write_impl(zfs_file_t *fp, const void *buf, size_t count, loff_t *off)
{
#if defined(HAVE_KERNEL_WRITE_PPOS)
	return (kernel_write(fp, buf, count, off));
#else
	mm_segment_t saved_fs;
	ssize_t rc;

	saved_fs = get_fs();
	set_fs(KERNEL_DS);

	rc = vfs_write(fp, (__force const char __user __user *)buf, count, off);

	set_fs(saved_fs);

	return (rc);
#endif
}

/*
 * Stateful write - use os internal file pointer to determine where to
 * write and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * resid -  pointer to count of unwritten bytes  (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
	loff_t off = fp->f_pos;
	ssize_t rc;

	rc = zfs_file_write_impl(fp, buf, count, &off);
	if (rc < 0)
		return (-rc);

	fp->f_pos = off;

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless write - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to write to (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pwrite(zfs_file_t *fp, const void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	ssize_t rc;

	rc  = zfs_file_write_impl(fp, buf, count, &off);
	if (rc < 0)
		return (-rc);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

static ssize_t
zfs_file_read_impl(zfs_file_t *fp, void *buf, size_t count, loff_t *off)
{
#if defined(HAVE_KERNEL_READ_PPOS)
	return (kernel_read(fp, buf, count, off));
#else
	mm_segment_t saved_fs;
	ssize_t rc;

	saved_fs = get_fs();
	set_fs(KERNEL_DS);

	rc = vfs_read(fp, (void __user *)buf, count, off);
	set_fs(saved_fs);

	return (rc);
#endif
}

/*
 * Stateful read - use os internal file pointer to determine where to
 * read and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to read
 * resid -  pointer to count of unread bytes (if short read)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	loff_t off = fp->f_pos;
	ssize_t rc;

	rc = zfs_file_read_impl(fp, buf, count, &off);
	if (rc < 0)
		return (-rc);

	fp->f_pos = off;

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless read - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to read from (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	ssize_t rc;

	rc = zfs_file_read_impl(fp, buf, count, &off);
	if (rc < 0)
		return (-rc);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * lseek - set / get file pointer
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * offp - value to seek to, returns current value plus passed offset
 * whence - see man pages for standard lseek whence values
 *
 * Returns 0 on success errno on failure (ESPIPE for non seekable types)
 */
int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
	loff_t rc;

	if (*offp < 0 || *offp > MAXOFFSET_T)
		return (EINVAL);

	rc = vfs_llseek(fp, *offp, whence);
	if (rc < 0)
		return (-rc);

	*offp = rc;

	return (0);
}

/*
 * Get file attributes
 *
 * filp - file pointer
 * zfattr - pointer to file attr structure
 *
 * Currently only used for fetching size and file mode.
 *
 * Returns 0 on success or error code of underlying getattr call on failure.
 */
int
zfs_file_getattr(zfs_file_t *filp, zfs_file_attr_t *zfattr)
{
	struct kstat stat;
	int rc;

#if defined(HAVE_4ARGS_VFS_GETATTR)
	rc = vfs_getattr(&filp->f_path, &stat, STATX_BASIC_STATS,
	    AT_STATX_SYNC_AS_STAT);
#elif defined(HAVE_2ARGS_VFS_GETATTR)
	rc = vfs_getattr(&filp->f_path, &stat);
#elif defined(HAVE_3ARGS_VFS_GETATTR)
	rc = vfs_getattr(filp->f_path.mnt, filp->f_dentry, &stat);
#else
#error "No available vfs_getattr()"
#endif
	if (rc)
		return (-rc);

	zfattr->zfa_size = stat.size;
	zfattr->zfa_mode = stat.mode;

	return (0);
}

/*
 * Sync file to disk
 *
 * filp - file pointer
 * flags - O_SYNC and or O_DSYNC
 *
 * Returns 0 on success or error code of underlying sync call on failure.
 */
int
zfs_file_fsync(zfs_file_t *filp, int flags)
{
	int datasync = 0;
	int error;
	int fstrans;

	if (flags & O_DSYNC)
		datasync = 1;

	/*
	 * May enter XFS which generates a warning when PF_FSTRANS is set.
	 * To avoid this the flag is cleared over vfs_sync() and then reset.
	 */
	fstrans = __spl_pf_fstrans_check();
	if (fstrans)
		current->flags &= ~(__SPL_PF_FSTRANS);

	error = -vfs_fsync(filp, datasync);

	if (fstrans)
		current->flags |= __SPL_PF_FSTRANS;

	return (error);
}

/*
 * fallocate - allocate or free space on disk
 *
 * fp - file pointer
 * mode (non-standard options for hole punching etc)
 * offset - offset to start allocating or freeing from
 * len - length to free / allocate
 *
 * OPTIONAL
 */
int
zfs_file_fallocate(zfs_file_t *fp, int mode, loff_t offset, loff_t len)
{
	/*
	 * May enter XFS which generates a warning when PF_FSTRANS is set.
	 * To avoid this the flag is cleared over vfs_sync() and then reset.
	 */
	int fstrans = __spl_pf_fstrans_check();
	if (fstrans)
		current->flags &= ~(__SPL_PF_FSTRANS);

	/*
	 * When supported by the underlying file system preferentially
	 * use the fallocate() callback to preallocate the space.
	 */
	int error = EOPNOTSUPP;
	if (fp->f_op->fallocate)
		error = fp->f_op->fallocate(fp, mode, offset, len);

	if (fstrans)
		current->flags |= __SPL_PF_FSTRANS;

	return (error);
}

/*
 * Request current file pointer offset
 *
 * fp - pointer to file
 *
 * Returns current file offset.
 */
loff_t
zfs_file_off(zfs_file_t *fp)
{
	return (fp->f_pos);
}

/*
 * Request file pointer private data
 *
 * fp - pointer to file
 *
 * Returns pointer to file private data.
 */
void *
zfs_file_private(zfs_file_t *fp)
{
	return (fp->private_data);
}

/*
 * unlink file
 *
 * path - fully qualified file path
 *
 * Returns 0 on success.
 *
 * OPTIONAL
 */
int
zfs_file_unlink(const char *path)
{
	return (EOPNOTSUPP);
}

/*
 * Get reference to file pointer
 *
 * fd - input file descriptor
 *
 * Returns pointer to file struct or NULL
 */
zfs_file_t *
zfs_file_get(int fd)
{
	return (fget(fd));
}

/*
 * Drop reference to file pointer
 *
 * fp - input file struct pointer
 */
void
zfs_file_put(zfs_file_t *fp)
{
	fput(fp);
}
