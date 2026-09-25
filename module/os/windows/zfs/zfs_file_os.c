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

/*
 * Copyright(c) 2021 Jorgen Lundman <lundman@lundman.net>
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
zfs_file_open(const char *path, int flags, int mode, cred_t *cr,
    zfs_file_t **fpp)
{
	(void) cr;
	vnode_t *vp;
	wchar_t buf[PATH_MAX];
	UNICODE_STRING uniName;
	OBJECT_ATTRIBUTES objAttr;
	HANDLE handle;
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK    ioStatusBlock;
	DWORD desiredAccess = 0;
	DWORD dwCreationDisposition;

	desiredAccess = GENERIC_READ;
	if (flags&O_WRONLY)
		desiredAccess = GENERIC_WRITE;
	if (flags&O_RDWR)
		desiredAccess = GENERIC_READ | GENERIC_WRITE;

	switch (flags&(O_CREAT | O_TRUNC | O_EXCL)) {
	case O_CREAT:
		dwCreationDisposition = FILE_OPEN_IF;
		break;
	case O_TRUNC:
		dwCreationDisposition = FILE_SUPERSEDE;
		break;
	case (O_CREAT | O_EXCL):
		// Only creating new implies starting from 0
	case (O_CREAT | O_EXCL | O_TRUNC):
		dwCreationDisposition = FILE_CREATE;
		break;
	case (O_CREAT | O_TRUNC):
		dwCreationDisposition = FILE_OVERWRITE_IF;
		break;
	default:
	case O_EXCL: // Invalid, ignore bit - treat as normal open
		dwCreationDisposition = FILE_OPEN;
		break;
	}

	if (flags&O_APPEND) mode |= FILE_APPEND_DATA;

#ifdef O_EXLOCK
	// if (flags&O_EXLOCK) share &= ~FILE_SHARE_WRITE;
#endif
	uint64_t vdev_win_offset = 0ULL; /* soft partition start */
	uint64_t vdev_win_length = 0ULL; /* soft partition length */
	char *vdev_path = NULL, *FileName = NULL;
	vdev_path = spa_strdup(path);

	if (vdev_path[0] == '#') {
		uint8_t *end;
		end = &vdev_path[0];
		while (end && end[0] == '#') end++;
		ddi_strtoull(end, (char **)&end, 10, &vdev_win_offset);
		while (end && end[0] == '#') end++;
		ddi_strtoull(end, (char **)&end, 10, &vdev_win_length);
		while (end && end[0] == '#') end++;

		FileName = end;
	} else {
		FileName = vdev_path;

		// Sometimes only vdev_path is set, with "/dev/physicaldrive"
		// make it be " \??\physicaldrive" space skipped over.
		if (strncmp("/dev/", FileName, 5) == 0) {
			FileName[0] = ' ';
			FileName[1] = '\\';
			FileName[2] = '?';
			FileName[3] = '?';
			FileName[4] = '\\';
			FileName++;
		}
	}

#ifdef _KERNEL
	if (strncmp("\\\\?\\", FileName, 4) == 0) {
		FileName[1] = '?';
	}
	if (strncmp("//./", FileName, 4) == 0) {
		FileName[1] = '?';
		FileName[2] = '?';
		for (int i = 0; FileName[i] != 0; i++)
			if (FileName[i] == '/')
				FileName[i] = '\\';
	}
#endif

	mbstowcs(buf, FileName, sizeof (buf));

	RtlInitUnicodeString(&uniName, buf);
	InitializeObjectAttributes(&objAttr, &uniName,
	    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
	    NULL, NULL);

	if (KeGetCurrentIrql() != PASSIVE_LEVEL)
		return (-1);

	ntstatus = ZwCreateFile(&handle,
	    desiredAccess,
	    &objAttr, &ioStatusBlock, NULL,
	    FILE_ATTRIBUTE_NORMAL,
	    0,
	    dwCreationDisposition,
	    FILE_SYNCHRONOUS_IO_NONALERT,
	    NULL, 0);

	spa_strfree(vdev_path);

	if (ntstatus != STATUS_SUCCESS)
		return (-1);

	// Since we will use DeviceObject and FileObject to do ioctl and IO
	// we grab them now and lock them in place.
	// Convert HANDLE to FileObject
	PFILE_OBJECT FileObject;
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status;

	// This adds a reference to FileObject
	status = ObReferenceObjectByHandle(
	    handle,
	    0,
	    *IoFileObjectType,
	    KernelMode,
	    (void *)&FileObject,
	    NULL);
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
	fp->f_win_offset = vdev_win_offset;
	fp->f_win_length = vdev_win_length;

	*fpp = fp;

#ifdef _WIN32
	int error;
	// Change it to SPARSE, so TRIM might work
	error = ZwFsControlFile(
	    fp->f_handle,
	    NULL,
	    NULL,
	    NULL,
	    NULL,
	    FSCTL_SET_SPARSE,
	    NULL,
	    0,
	    NULL,
	    0);
	dprintf("%s: set Sparse 0x%x.\n", __func__, error);
#endif

	return (0);
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
int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK ioStatusBlock;
	size_t bytesRead = 0;

	while (bytesRead < count) {
		ULONG remainingLength = count - bytesRead;

		/* So we can get short-reads from pipes under MSYS2 */
		ntstatus = ZwReadFile(fp->f_handle, NULL, NULL, NULL,
		    &ioStatusBlock, (PUCHAR)buf + bytesRead, remainingLength,
		    NULL, NULL);
		if (STATUS_SUCCESS != ntstatus)
			return (EIO);

		// No more data to read, break the loop
		if (ioStatusBlock.Information == 0)
			break;

		bytesRead += (ULONG)ioStatusBlock.Information;
	}

	/*
	 * A short read (pipe writer closed early, truncated stream) is a
	 * normal occurrence for callers like dmu_recv, which read the resid
	 * to detect it -- panicking here (as VERIFY3U(count, ==, bytesRead)
	 * previously did) turned an expected short read into a bugcheck.
	 * Match module/os/linux/zfs/zfs_file_os.c: report the shortfall via
	 * *resid when the caller asked for it, otherwise treat it as EIO.
	 */
	if (resid) {
		*resid = count - bytesRead;
	} else if (bytesRead != count) {
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
	NTSTATUS ntstatus;
	IO_STATUS_BLOCK ioStatusBlock;
	LARGE_INTEGER offset = { 0 };
	offset.QuadPart = off + fp->f_win_offset;
	ntstatus = ZwWriteFile(fp->f_handle, NULL, NULL, NULL,
	    &ioStatusBlock, buf, count, &offset, NULL);
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
	offset.QuadPart = off + fp->f_win_offset;
	ntstatus = ZwReadFile(fp->f_handle, NULL, NULL, NULL,
	    &ioStatusBlock, buf, count, &offset, NULL);
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
		return (-1);
	IO_STATUS_BLOCK	ioStatusBlock;
	NTSTATUS ntStatus;
	ntStatus = ZwFlushBuffersFile(
	    fp->f_handle,
	    &ioStatusBlock);
	if (ntStatus != STATUS_SUCCESS) {
		return (-1);
	}
	return (0);
}
/*
 * deallocate - allocate or free space on disk
 *
 * fp - file pointer
 * offset - offset to start allocating or freeing from
 * len - length to free / allocate
 *
 * OPTIONAL
 */
int
zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len)
{
	int error;
	struct flock flck;

	memset(&flck, 0, sizeof (flck));
	flck.l_type = F_FREESP;
	flck.l_start = offset + fp->f_win_offset;
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

	// Linux code checks for S_ISREG in vdev_file.c
	zfattr->zfa_mode = S_IFREG;

	if (fp->f_win_length != 0ULL) {
		// Soft partition, return its length
		zfattr->zfa_size = fp->f_win_length;
		return (0);
	}

	ntStatus = ZwQueryInformationFile(
	    fp->f_handle,
	    &ioStatusBlock,
	    &fileInfo,
	    sizeof (fileInfo),
	    FileStandardInformation);
	if (ntStatus != STATUS_SUCCESS) {
		return (-1);
	}
	zfattr->zfa_size = fileInfo.EndOfFile.QuadPart;
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

	/*
	 * dev_t for a zfsdev handle is its FILE_OBJECT pointer (see
	 * zfsdev_open(), which uses (dev_t)IrpSp->FileObject both as the
	 * dev_t passed to zfsdev_state_init() and, via minor(), as
	 * zs_minor).  fp->f_fp is that same FILE_OBJECT*, obtained by
	 * getf() via ObReferenceObjectByHandle() on the fd this zfs_file_t
	 * wraps -- so it recovers the exact handle fp refers to.
	 *
	 * This used to call zfsdev_get_dev(), which reads a per-OS-thread
	 * TSD slot set once in zfsdev_open() to "whichever dev_t this
	 * thread most recently opened" -- ignoring fp entirely.  That is
	 * only correct if a thread never touches more than one zfsdev
	 * handle in its lifetime.  zed opens more than one /dev/zfs handle
	 * (general control plus a dedicated event stream); if their opens
	 * or ioctls ever land on the same or a reused kernel thread, the
	 * TSD slot reflects the wrong handle and zc_cleanup_fd-based
	 * lookups (e.g. ZFS_IOC_EVENTS_NEXT) can silently resolve to a
	 * different, possibly already-closed-and-freed zfsdev_state_t /
	 * zfs_zevent_t -- confirmed as the cause of a UAF crash in
	 * zfs_zevent_next() (nvlist_common) via zc_cleanup_fd.
	 */
	dev = (dev_t)fp->f_fp;
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
zfs_file_unlink(const char *path)
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
zfs_file_t *
zfs_file_get(int fd)
{
	return (getf(fd));
}

/*
 * Drop reference to file pointer
 *
 * fd - input file descriptor
 */
void
zfs_file_put(zfs_file_t *fp)
{
	releasefp(fp);
}
