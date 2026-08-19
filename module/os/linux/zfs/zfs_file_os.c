// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
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
zfs_file_open(const char *path, int flags, int mode, cred_t *cr,
    zfs_file_t **fpp)
{
	struct file *filp;
	int saved_umask;

	if (!(flags & O_CREAT) && (flags & O_WRONLY))
		flags |= O_EXCL;

	if (flags & O_CREAT)
		saved_umask = xchg(&current->fs->umask, 0);

	const cred_t *oldcr = override_creds(cr);
	filp = filp_open(path, flags, mode);
	revert_creds(oldcr);

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

	rc = kernel_write(fp, buf, count, &off);
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
    uint8_t ashift, ssize_t *resid)
{
	(void) ashift;
	ssize_t rc;

	rc  = kernel_write(fp, buf, count, &off);
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

	rc = kernel_read(fp, buf, count, &off);
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

	rc = kernel_read(fp, buf, count, &off);
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

	if (*offp < 0)
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

	rc = vfs_getattr(&filp->f_path, &stat, STATX_BASIC_STATS,
	    AT_STATX_SYNC_AS_STAT);
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

	if (flags & O_DSYNC)
		datasync = 1;

	error = -vfs_fsync(filp, datasync);

	return (error);
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
	/*
	 * When supported by the underlying file system preferentially
	 * use the fallocate() callback to preallocate the space.
	 */
	int error = EOPNOTSUPP;
	if (fp->f_op->fallocate)
		error = -fp->f_op->fallocate(fp,
		    FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, len);

	if (error)
		return (SET_ERROR(error));

	return (0);
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
