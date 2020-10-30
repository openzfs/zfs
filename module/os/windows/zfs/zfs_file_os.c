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
#include <sys/spa.h>
#include <sys/zfs_ioctl.h>
#include <fcntl.h>

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
	vnode_t *vp;
	wchar_t buf[PATH_MAX];
	UNICODE_STRING uniName;
	OBJECT_ATTRIBUTES objAttr;
	HANDLE   handle;
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK    ioStatusBlock;
	DWORD desiredAccess = 0;
	DWORD dwCreationDisposition;

	mbstowcs(buf, path, sizeof (buf));
	if (flags == O_RDONLY) {
		desiredAccess = GENERIC_READ;
		dwCreationDisposition = FILE_OPEN_IF;
	}
	if (flags & O_WRONLY) {
		desiredAccess = GENERIC_WRITE;
		dwCreationDisposition = FILE_OVERWRITE_IF;
	}
	if (flags & O_RDWR) {
		desiredAccess = GENERIC_READ | GENERIC_WRITE;
		dwCreationDisposition = FILE_OVERWRITE_IF;
	}
	if (flags & O_TRUNC)
		dwCreationDisposition = FILE_SUPERSEDE;

	RtlInitUnicodeString(&uniName, buf);
	InitializeObjectAttributes(&objAttr, &uniName,
			OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			NULL, NULL);

	if(KeGetCurrentIrql() != PASSIVE_LEVEL)
		return -1;

	ntstatus = ZwCreateFile(&handle,
			desiredAccess,
			&objAttr, &ioStatusBlock, NULL,
			FILE_ATTRIBUTE_NORMAL,
			0,
			dwCreationDisposition,
			FILE_SYNCHRONOUS_IO_NONALERT,
			NULL, 0);

	if (ntstatus != STATUS_SUCCESS) {
		return -1;
	}

	// Since we will use DeviceObject and FileObject to do ioctl and IO
	// we grab them now and lock them in place.
	// Convert HANDLE to FileObject
	PFILE_OBJECT        FileObject;
	PDEVICE_OBJECT      DeviceObject;
	NTSTATUS status;

	// This adds a reference to FileObject
	status = ObReferenceObjectByHandle(
		handle,
		0,
		*IoFileObjectType,
		KernelMode,
		&FileObject,
		NULL
	);
	if (status != STATUS_SUCCESS) {
		ZwClose(handle);
                return (EIO);
	}

	// Convert FileObject to DeviceObject
	DeviceObject = IoGetRelatedDeviceObject(FileObject);

	// Grab a reference to DeviceObject
	ObReferenceObject(DeviceObject);

	zfs_file_t *fp;
	fp = (zfs_file_t *)kmem_zalloc(sizeof (zfs_file_t), KM_SLEEP);
	fp->f_vnode = NULL;
	fp->f_handle = handle;
	fp->f_fileobject = FileObject;
	fp->f_deviceobject = DeviceObject;

	*fpp = fp;

	return 0;
}

void
zfs_file_close(zfs_file_t *fp)
{
	if (fp->f_fileobject != NULL)
		ObDereferenceObject(fp->f_fileobject);
	if (fp->f_deviceobject != NULL)
		ObDereferenceObject(fp->f_deviceobject);

	ZwClose(fp->f_handle);

	kmem_free(fp, sizeof (zfs_file_t));
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
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK ioStatusBlock;

	ntstatus = ZwWriteFile(fp->f_handle, NULL, NULL, NULL,
			&ioStatusBlock, buf, count, NULL, NULL);

	if (resid)
		*resid = 0;

	if (STATUS_SUCCESS != ntstatus)
		return (EIO);
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
int zfs_file_read(zfs_file_t *fp, /* const */void *buf, size_t count, ssize_t *resid)
{
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK ioStatusBlock;
	ntstatus = ZwReadFile(fp->f_handle, NULL, NULL, NULL, &ioStatusBlock, buf, count, NULL, NULL);
	if (STATUS_SUCCESS != ntstatus)
		return (EIO);
	if (resid)
		*resid = 0;
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
zfs_file_pwrite(zfs_file_t* fp, const void* buf, size_t count, loff_t off,
	ssize_t* resid)
{
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK ioStatusBlock;
	LARGE_INTEGER offset = { 0 };
	offset.QuadPart = off;
	ntstatus = ZwReadFile(fp, NULL, NULL, NULL, &ioStatusBlock, buf, count, &offset, NULL);
	// reset fp to its original position
	if (STATUS_SUCCESS != ntstatus)
		return (EIO);
	if (resid)
		*resid = 0;
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
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK ioStatusBlock;
	LARGE_INTEGER offset = { 0 };
	offset.QuadPart = off;
	ntstatus = ZwReadFile(fp, NULL, NULL, NULL, &ioStatusBlock, buf, count, &offset, NULL);
	if (STATUS_SUCCESS != ntstatus)
		return (EIO);
	if (resid)
		*resid = 0;
	return (0);
}

/*
 * Sync file to disk
 *
 * hFile - handle to file
 *
 * Returns 0 on success or error code of underlying sync call on failure.
 */
int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	if (KeGetCurrentIrql() != PASSIVE_LEVEL)
		return -1;
	IO_STATUS_BLOCK	ioStatusBlock;
	NTSTATUS ntStatus;
	ntStatus = ZwFlushBuffersFile(
		fp->f_handle,
		&ioStatusBlock
	);
	if (ntStatus != STATUS_SUCCESS) {
		return -1;
	}
	return 0;
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
	int error;
	struct flock flck;

	bzero(&flck, sizeof(flck));
	flck.l_type = F_FREESP;
	flck.l_start = offset;
	flck.l_len = len;
	flck.l_whence = 0;

	error = VOP_SPACE(fp->f_handle, F_FREESP, &flck,
	    0, 0, kcred, NULL);

	return (error);
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
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
	FILE_STANDARD_INFORMATION fileInfo = { 0 };
	IO_STATUS_BLOCK ioStatusBlock;
	NTSTATUS ntStatus;
	ntStatus = ZwQueryInformationFile(
		fp->f_handle,
		&ioStatusBlock,
		&fileInfo,
		sizeof(fileInfo),
		FileNameInformation
	);
	if (ntStatus != STATUS_SUCCESS) {
		return -1;
	}
	zfattr->zfa_size = fileInfo.EndOfFile.QuadPart;
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
	return (fp->f_offset);
}

/*
 * Request file pointer private data
 *
 * fp - pointer to file
 *
 * Returns pointer to file private data.
 */
extern kmutex_t zfsdev_state_lock;
dev_t zfsdev_get_dev(void);

void *
zfs_file_private(zfs_file_t *fp)
{
	dev_t dev;
	void *zs;

	dev = zfsdev_get_dev();
	dprintf("%s: fetching dev x%x\n", __func__, dev);
	if (dev == 0)
		return (NULL);

	mutex_enter(&zfsdev_state_lock);
	zs = zfsdev_get_state(minor(dev), ZST_ALL);
	mutex_exit(&zfsdev_state_lock);
	dprintf("%s: searching minor %d %p\n", __func__, minor(dev), zs);

	return (zs);
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
zfs_file_unlink(const char* path)
{
	return (EOPNOTSUPP);
}

/*
 * Get reference to file pointer
 *
 * fd - input file descriptor
 * fpp - pointer to file pointer
 *
 * Returns 0 on success EBADF on failure.
 */
int
zfs_file_get(int fd, zfs_file_t **fpp)
{
	*fpp = getf(fd);
	if (*fpp == NULL)
		return (EBADF);
	return (0);
}

/*
 * Drop reference to file pointer
 *
 * fd - input file descriptor
 */
void
zfs_file_put(int fd)
{
	releasef(fd);
}
