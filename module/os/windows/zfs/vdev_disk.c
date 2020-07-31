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
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_disk_os.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/abd.h>
#include <sys/abd_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>

#include <ntdddisk.h>
#include <Ntddstor.h>


/*
 * Virtual device vector for disks.
 */


wchar_t zfs_vdev_protection_filter[ZFS_MODULE_STRMAX] = { L"\0" };

static void vdev_disk_close(vdev_t *);

extern void UnlockAndFreeMdl(PMDL);

_Atomic uint64_t spl_lowest_vdev_disk_stack_remaining = 0;

static void
vdev_disk_alloc(vdev_t *vd)
{
	vdev_disk_t *dvd;

	dvd = vd->vdev_tsd = kmem_zalloc(sizeof (vdev_disk_t), KM_SLEEP);

}

static void
vdev_disk_free(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (dvd == NULL)
		return;

	kmem_free(dvd, sizeof (vdev_disk_t));
	vd->vdev_tsd = NULL;
}

static void disk_exclusive(DEVICE_OBJECT *device, boolean_t excl)
{
	SET_DISK_ATTRIBUTES diskAttrs = { 0 };
	DWORD requiredSize;
	DWORD returnedSize;

	// Set disk attributes.
	diskAttrs.Version = sizeof (diskAttrs);
	diskAttrs.AttributesMask =
	    DISK_ATTRIBUTE_OFFLINE | DISK_ATTRIBUTE_READ_ONLY;
	diskAttrs.Attributes =
	    excl ? DISK_ATTRIBUTE_OFFLINE | DISK_ATTRIBUTE_READ_ONLY : 0;
	diskAttrs.Persist = FALSE;

	if (kernel_ioctl(device, NULL, IOCTL_DISK_SET_DISK_ATTRIBUTES,
	    &diskAttrs, sizeof (diskAttrs), NULL, 0) != 0) {
		dprintf("IOCTL_DISK_SET_DISK_ATTRIBUTES");
		return;
	}

	// Tell the system that the disk was changed.
	if (kernel_ioctl(device, NULL, IOCTL_DISK_UPDATE_PROPERTIES, NULL,
	    0, NULL, 0) != 0)
		dprintf("IOCTL_DISK_UPDATE_PROPERTIES");

}


/*
 * We want to be loud in DEBUG kernels when DKIOCGMEDIAINFOEXT fails, or when
 * even a fallback to DKIOCGMEDIAINFO fails.
 */
#ifdef DEBUG
#define	VDEV_DEBUG(...) cmn_err(CE_NOTE, __VA_ARGS__)
#else
#define	VDEV_DEBUG(...) /* Nothing... */
#endif

static int
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *ashift, uint64_t *physical_ashif)
{
	spa_t *spa = vd->vdev_spa;
	vdev_disk_t *dvd = vd->vdev_tsd;
	int error = EINVAL;
	uint64_t capacity = 0, blksz = 0, pbsize = 0;
	int isssd;
	char *vdev_path = NULL;

	PAGED_CODE();

	dprintf("%s: open of '%s' (physpath '%s')\n", __func__,
	    vd->vdev_path, vd->vdev_physpath ? vd->vdev_physpath : "");
	/*
	 * We must have a pathname, and it must be absolute.
	 * It can also start with # for partition encoded paths
	 */
	if (vd->vdev_path == NULL ||
	    (vd->vdev_path[0] != '/' && vd->vdev_path[0] != '#' &&
	    vd->vdev_path[0] != '\\')) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Reopen the device if it's not currently open. Otherwise,
	 * just update the physical size of the device.
	 */
	if (dvd != NULL) {
		if (dvd->vd_ldi_offline && dvd->vd_lh == NULL) {
			/*
			 * If we are opening a device in its offline notify
			 * context, the LDI handle was just closed. Clean
			 * up the LDI event callbacks and free vd->vdev_tsd.
			 */
			vdev_disk_free(vd);
		} else {
			ASSERT(vd->vdev_reopening);
			goto skip_open;
		}
	}

	/*
	 * Create vd->vdev_tsd.
	 */
	vdev_disk_alloc(vd);
	dvd = vd->vdev_tsd;

	/*
	 * If we have not yet opened the device, try to open it by the
	 * specified path.
	 */
	NTSTATUS ntstatus;
	uint8_t *FileName = NULL;
	uint32_t FileLength;

	// Use vd->vdev_physpath first, if set, otherwise
	// usual vd->vdev_path
	if (vd->vdev_physpath)
		vdev_path = spa_strdup(vd->vdev_physpath);
	else
		vdev_path = spa_strdup(vd->vdev_path);

	/* Check for partition encoded paths */
	if (vdev_path[0] == '#') {
		uint8_t *end;
		end = &vdev_path[0];
		while (end && end[0] == '#') end++;
		ddi_strtoull(end, &end, 10, &dvd->vdev_win_offset);
		while (end && end[0] == '#') end++;
		ddi_strtoull(end, &end, 10, &dvd->vdev_win_length);
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

	// Apparently in Userland it is "\\?\" but in
	// kernel has to be "\??\" - is there not a name that works in both?
	if (strncmp("\\\\?\\", FileName, 4) == 0) {
		FileName[1] = '?';
	}

	dprintf("%s: opening '%s'\n", __func__, FileName);

	ANSI_STRING AnsiFilespec;
	UNICODE_STRING UnicodeFilespec;
	OBJECT_ATTRIBUTES ObjectAttributes;

	SHORT UnicodeName[PATH_MAX];
	CHAR AnsiName[PATH_MAX];
	USHORT NameLength = 0;

	memset(UnicodeName, 0, sizeof (SHORT) * PATH_MAX);
	memset(AnsiName, 0, sizeof (UCHAR) * PATH_MAX);

	NameLength = strlen(FileName);
	ASSERT(NameLength < PATH_MAX);

	memmove(AnsiName, FileName, NameLength);

	AnsiFilespec.MaximumLength = AnsiFilespec.Length = NameLength;
	AnsiFilespec.Buffer = AnsiName;

	UnicodeFilespec.MaximumLength = PATH_MAX * 2;
	UnicodeFilespec.Length = 0;
	UnicodeFilespec.Buffer = (PWSTR)UnicodeName;

	RtlAnsiStringToUnicodeString(&UnicodeFilespec, &AnsiFilespec, FALSE);

	ObjectAttributes.Length = sizeof (OBJECT_ATTRIBUTES);
	ObjectAttributes.RootDirectory = NULL;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE;
	ObjectAttributes.ObjectName = &UnicodeFilespec;
	ObjectAttributes.SecurityDescriptor = NULL;
	ObjectAttributes.SecurityQualityOfService = NULL;
	IO_STATUS_BLOCK iostatus;

	ntstatus = ZwCreateFile(&dvd->vd_lh,
	    spa_mode(spa) == SPA_MODE_READ ? GENERIC_READ | SYNCHRONIZE :
	    GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
	    &ObjectAttributes,
	    &iostatus,
	    0,
	    FILE_ATTRIBUTE_NORMAL,
	    /* FILE_SHARE_WRITE | */ FILE_SHARE_READ,
	    FILE_OPEN,
	    FILE_SYNCHRONOUS_IO_NONALERT |
	    (spa_mode(spa) == SPA_MODE_READ ? 0 :
	    FILE_NO_INTERMEDIATE_BUFFERING),
	    NULL,
	    0);

	if (ntstatus == STATUS_SUCCESS) {
		error = 0;
	} else {
		error = EINVAL; // GetLastError();
		dvd->vd_lh = NULL;
	}

	/*
	 * If we succeeded in opening the device, but 'vdev_wholedisk'
	 * is not yet set, then this must be a slice.
	 */
	if (error == 0 && vd->vdev_wholedisk == -1ULL)
		vd->vdev_wholedisk = 0;

	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		spa_strfree(vdev_path);
		return (error);
	}

	// Since we will use DeviceObject and FileObject to do ioctl and IO
	// we grab them now and lock them in place.
	// Convert HANDLE to FileObject
	PFILE_OBJECT FileObject;
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status;

	// This adds a reference to FileObject
	status = ObReferenceObjectByHandle(
	    dvd->vd_lh,  // fixme, keep this in dvd
	    0,
	    *IoFileObjectType,
	    KernelMode,
	    &FileObject,
	    NULL);
	if (status != STATUS_SUCCESS) {
		ZwClose(dvd->vd_lh);
		dvd->vd_lh = NULL;
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		spa_strfree(vdev_path);
		return (EIO);
	}


	// Convert FileObject to DeviceObject
	PDEVICE_OBJECT pTopDevice = IoGetRelatedDeviceObject(FileObject);
	PDEVICE_OBJECT pSendToDevice = pTopDevice; // default

/*
 * Move to the top of the device stack or until we find the protection
 * filter driver. We need to stay under that driver so we can still
 * access the disk after protecting it.
 * The custom protection filter is optional: if none set we stay under
 * the default "partmgr" driver; otherwise we will stay under the first
 * one found. By default the disk gets minimal protection being set offline
 * and read only through "partmgr". A custom filter driver can provide
 * enhanced protection for the vdev disk.
 */
	UNICODE_STRING customFilterName;
	UNICODE_STRING defaultFilterName;
	RtlInitUnicodeString(&customFilterName, zfs_vdev_protection_filter);
	RtlInitUnicodeString(&defaultFilterName, L"\\Driver\\partmgr");

	DeviceObject = FileObject->DeviceObject; // bottom of stack

	/*
	 * Determine the actual size of the device.
	 */

	// If we are given the size in "#offset#size#path"
	capacity = dvd->vdev_win_length;

	// STORAGE_DEVICE_NUMBER sdn = { 0 };
	// kernel_ioctl(DeviceObject, FileObject,
	// IOCTL_STORAGE_GET_DEVICE_NUMBER,
	//	NULL, 0, &sdn, sizeof(sdn));

	if (capacity == 0ULL) {
		PARTITION_INFORMATION_EX pix = {0};
		if (kernel_ioctl(DeviceObject, FileObject,
		    IOCTL_DISK_GET_PARTITION_INFO_EX,
		    NULL, 0, &pix, sizeof (pix)) == 0)
			capacity = pix.PartitionLength.QuadPart;
	}

	if (capacity == 0ULL) {
		PARTITION_INFORMATION pi = {0};
		if (kernel_ioctl(DeviceObject, FileObject,
		    IOCTL_DISK_GET_PARTITION_INFO,
		    NULL, 0, &pi, sizeof (pi)) == 0)
			capacity = pi.PartitionLength.QuadPart;
	}

	if (capacity == 0ULL) {
		GET_LENGTH_INFORMATION len;

		if (kernel_ioctl(DeviceObject, FileObject,
		    IOCTL_DISK_GET_LENGTH_INFO,
		    NULL, 0, &len, sizeof (len)) == 0)
			capacity = len.Length.QuadPart;
	}

	if (capacity == 0ULL && vd->vdev_wholedisk) {
		DISK_GEOMETRY_EX geometry_ex = {0};
		DWORD len;

		if (kernel_ioctl(DeviceObject, FileObject,
		    IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
		    NULL, 0, &geometry_ex, sizeof (geometry_ex)) == 0)
			capacity = geometry_ex.DiskSize.QuadPart;
	}

	if (capacity == 0ULL && vd->vdev_wholedisk) {
		STORAGE_READ_CAPACITY cap = {0};
		cap.Version = sizeof (STORAGE_READ_CAPACITY);
		if (kernel_ioctl(DeviceObject, FileObject,
		    IOCTL_STORAGE_READ_CAPACITY,
		    NULL, 0, &cap, sizeof (cap)) == 0)
			capacity = cap.DiskLength.QuadPart;
	}

	// Remember the size for when we skip_open
	dvd->vdev_win_length = capacity;

	// Now walk up the stack locating the device to lock
	while (DeviceObject) {
		if ((zfs_vdev_protection_filter[0] != L'\0' ?
		    !RtlCompareUnicodeString(
		    &DeviceObject->DriverObject->DriverName,
		    &customFilterName, TRUE) : FALSE) ||
		    !RtlCompareUnicodeString(
		    &DeviceObject->DriverObject->DriverName,
		    &defaultFilterName, TRUE)) {
			dprintf("%s: disk %s : vdev protection "
			    "filter set to %S\n",
			    __func__, FileName,
			    DeviceObject->DriverObject->DriverName.Buffer);
			break;
		}
		pSendToDevice = DeviceObject;
		DeviceObject = DeviceObject->AttachedDevice;
	}
	DeviceObject = pSendToDevice;

	// Grab a reference to DeviceObject
	ObReferenceObject(DeviceObject);

	dvd->vd_FileObject = FileObject;
	dvd->vd_ExclusiveObject = DeviceObject;

	dvd->vd_DeviceObject = DeviceObject;
	ObReferenceObject(dvd->vd_DeviceObject);

	// Make disk readonly and offline, so that users can't
	// partition/format it.
	if (vd->vdev_wholedisk)
		disk_exclusive(dvd->vd_ExclusiveObject, TRUE);
	spa_strfree(vdev_path);

skip_open:

	capacity = dvd->vdev_win_length;

	/*
	 * Determine the device's minimum transfer size.
	 * If the ioctl isn't supported, assume DEV_BSIZE.
	 */
	// fill in capacity, blksz, pbsize
	STORAGE_PROPERTY_QUERY storageQuery;
	memset(&storageQuery, 0, sizeof (STORAGE_PROPERTY_QUERY));
	storageQuery.PropertyId = StorageAccessAlignmentProperty;
	storageQuery.QueryType = PropertyStandardQuery;

	STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR diskAlignment = { 0 };
	memset(&diskAlignment, 0, sizeof (STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));
	DWORD outsize;

	error = kernel_ioctl(dvd->vd_DeviceObject, dvd->vd_FileObject,
	    IOCTL_STORAGE_QUERY_PROPERTY,
	    &storageQuery, sizeof (STORAGE_PROPERTY_QUERY),
	    &diskAlignment, sizeof (STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));

	if (error == 0) {
		blksz = diskAlignment.BytesPerLogicalSector;
		pbsize = diskAlignment.BytesPerPhysicalSector;
	} else {
		blksz = pbsize = DEV_BSIZE;
	}

	// Set psize to the size of the partition. For now, assume virtual
	// since ioctls do not seem to work.
	*psize = capacity;

	// Set max_psize to the biggest it can be, expanding..
	*max_psize = *psize;

	if (!blksz) blksz = DEV_BSIZE;
	if (!pbsize) pbsize = DEV_BSIZE;

	*ashift = highbit64(MAX(pbsize, SPA_MINBLOCKSIZE)) - 1;
	dprintf("%s: picked ashift %llu for device\n", __func__, *ashift);

	/*
	 * Clear the nowritecache bit, so that on a vdev_reopen() we will
	 * try again.
	 */
	vd->vdev_nowritecache = B_FALSE;

	/* Set when device reports it supports TRIM. */
	vd->vdev_has_trim = !!blk_queue_discard(dvd->vd_DeviceObject);

	/* Set when device reports it supports secure TRIM. */
	vd->vdev_has_securetrim =
	    !!blk_queue_discard_secure(dvd->vd_DeviceObject);

	/* Inform the ZIO pipeline that we are non-rotational */
	/* Best choice seems to be either TRIM, or SeekPenalty */
	vd->vdev_nonrot =
	    vd->vdev_has_trim || blk_queue_nonrot(dvd->vd_DeviceObject);

	dprintf("%s: nonrot %d, trim %d, securetrim %d\n", __func__,
	    vd->vdev_nonrot, vd->vdev_has_trim, vd->vdev_has_securetrim);

	return (0);
}


static void
vdev_disk_close(vdev_t *vd)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	if (vd->vdev_reopening || dvd == NULL)
		return;

	vd->vdev_delayed_close = B_FALSE;
	/*
	 * If we closed the LDI handle due to an offline notify from LDI,
	 * don't free vd->vdev_tsd or unregister the callbacks here;
	 * the offline finalize callback or a reopen will take care of it.
	 */
	if (dvd->vd_ldi_offline)
		return;

	if (dvd->vd_lh != NULL) {
		dprintf("%s: \n", __func__);

		// Undo disk readonly and offline.
		if (vd->vdev_wholedisk)
			disk_exclusive(dvd->vd_ExclusiveObject, FALSE);

		// Release our holds
		ObDereferenceObject(dvd->vd_FileObject);
		ObDereferenceObject(dvd->vd_DeviceObject);
		ObDereferenceObject(dvd->vd_ExclusiveObject);
		// Close file
		ZwClose(dvd->vd_lh);
	}

	dvd->vd_lh = NULL;
	dvd->vd_FileObject = NULL;
	dvd->vd_DeviceObject = NULL;
	dvd->vd_ExclusiveObject = NULL;

	vdev_disk_free(vd);
}

int
vdev_disk_physio(vdev_t *vd, caddr_t data,
    size_t size, uint64_t offset, int flags, boolean_t isdump)
{
	vdev_disk_t *dvd = vd->vdev_tsd;

	// dprintf("%s: \n", __func__);

	/*
	 * If the vdev is closed, it's likely in the REMOVED or FAULTED state.
	 * Nothing to be done here but return failure.
	 */
	if (dvd == NULL || (dvd->vd_ldi_offline))
		return (EIO);

	ASSERT(vd->vdev_ops == &vdev_disk_ops);

	return (EIO);
}

static void
vdev_disk_ioctl_free(zio_t *zio)
{
	kmem_free(zio->io_vsd, sizeof (struct dk_callback));
}

static const zio_vsd_ops_t vdev_disk_vsd_ops = {
	vdev_disk_ioctl_free,
	zio_vsd_default_cksum_report
};

static void
vdev_disk_ioctl_done(void *zio_arg, int error)
{
	zio_t *zio = zio_arg;

	zio->io_error = error;

	zio_interrupt(zio);
}

static void
vdev_disk_io_start_done(__in PVOID pDummy, __in PVOID pWkParms)
{
	zio_t *zio = (zio_t *)pWkParms;
	UNREFERENCED_PARAMETER(pDummy);

	ASSERT(zio != NULL);

	IoFreeWorkItem(zio->windows.work_item);
	zio->windows.work_item = NULL;

	NTSTATUS status = zio->windows.irp->IoStatus.Status;
	zio->io_error = (!NT_SUCCESS(status) ? EIO : 0);

	UnlockAndFreeMdl(zio->windows.irp->MdlAddress);
	IoFreeIrp(zio->windows.irp);

	// Return abd buf
	if (zio->io_type == ZIO_TYPE_READ) {
		VERIFY3S(zio->io_abd->abd_size, >=, zio->io_size);
		abd_return_buf_copy(zio->io_abd, zio->windows.b_addr,
		    zio->io_size);
	} else {
		VERIFY3S(zio->io_abd->abd_size, >=, zio->io_size);
		abd_return_buf(zio->io_abd, zio->windows.b_addr,
		    zio->io_size);
	}

	zio_delay_interrupt(zio);
}

/*
 * IO has finished callback, in Windows this is called as a different
 * IRQ level, so we can practically do nothing here. (Can't call mutex
 * locking, like from kmem_free())
 */
IO_COMPLETION_ROUTINE vdev_disk_io_intr;

static NTSTATUS
vdev_disk_io_intr(PDEVICE_OBJECT DeviceObject, PIRP irp, PVOID Context)
{
	zio_t *zio = (zio_t *)Context;

	ASSERT(zio != NULL);

/*
 * Unfortunately:
 * Whatever thread happened to be running is "borrowed" to handle
 * the completion.
 * As about DPCs - in Windows, they are not bound to a thread,
 * KeGetCurrentThread is just nonsense in them.
 *
 * So, the whole Windows kernel framework just does not support the notion of
 * "current thread" in DPCs and thus completion routines.
 *
 * This means our call to "mutex_enter()" will "panic: lock against myself"
 * if that thread it "borrowed" is the actual owner thread.
 *
 * So schedule IoQueueWorkItem() to call the done function in a real context.
 */

	VERIFY3P(zio->windows.work_item, !=, NULL);
	IoQueueWorkItem(zio->windows.work_item, vdev_disk_io_start_done,
	    DelayedWorkQueue, zio);
	return (STATUS_MORE_PROCESSING_REQUIRED);
}

static void
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_disk_t *dvd = vd->vdev_tsd;
	struct dk_callback *dkc;
	buf_t *bp;
	unsigned long trim_flags = 0;
	int flags, error = 0;

	// dprintf("%s: type 0x%x offset 0x%llx len 0x%llx \n",
	// __func__, zio->io_type, zio->io_offset, zio->io_size);

	/*
	 * If the vdev is closed, it's likely in the REMOVED or FAULTED state.
	 * Nothing to be done here but return failure.
	 */
	if (dvd == NULL || (dvd->vd_ldi_offline)) {
		zio->io_error = ENXIO;
		zio_interrupt(zio);
		return;
	}

	switch (zio->io_type) {
	case ZIO_TYPE_IOCTL:

		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			if (vd->vdev_nowritecache) {
				zio->io_error = SET_ERROR(ENOTSUP);
				break;
			}

			zio->io_vsd = dkc = kmem_alloc(sizeof (*dkc), KM_SLEEP);
			zio->io_vsd_ops = &vdev_disk_vsd_ops;

			dkc->dkc_callback = vdev_disk_ioctl_done;
//			dkc->dkc_flag = FLUSH_VOLATILE;
			dkc->dkc_cookie = zio;

			// Windows: find me
//			error = ldi_ioctl(dvd->vd_lh, zio->io_cmd,
//			    (uintptr_t)dkc, FKIOCTL, kcred, NULL);

			if (error == 0) {
				/*
				 * The ioctl will be done asychronously,
				 * and will call vdev_disk_ioctl_done()
				 * upon completion.
				 */
				zio_execute(zio);  // until we have ioctl
				return;
			}

			zio->io_error = error;

			break;

		default:
			zio->io_error = SET_ERROR(ENOTSUP);
		} /* io_cmd */

		zio_execute(zio);
		return;

	case ZIO_TYPE_WRITE:
		// if (zio->io_priority == ZIO_PRIORITY_SYNC_WRITE)
		// Windows: do we need to pass this down IRP somehow?
		break;

	case ZIO_TYPE_READ:
		// if (zio->io_priority == ZIO_PRIORITY_SYNC_READ)
		// Windows: do we need to pass this down IRP somehow?
		break;

	case ZIO_TYPE_TRIM:
#if defined(BLKDEV_DISCARD_SECURE)
		if (zio->io_trim_flags & ZIO_TRIM_SECURE)
			trim_flags |= BLKDEV_DISCARD_SECURE;
#endif
		zio->io_error = -blkdev_issue_discard_bytes(
		    dvd->vd_DeviceObject,
		    zio->io_offset, zio->io_size, trim_flags);
		zio_interrupt(zio);
		return;

	default:
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_execute(zio);
		return;
	} /* io_type */

	ASSERT(zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE);

	zio->io_target_timestamp = zio_handle_io_delay(zio);

	const ULONG_PTR r = IoGetRemainingStackSize();

	if (spl_lowest_vdev_disk_stack_remaining == 0) {
		spl_lowest_vdev_disk_stack_remaining = r;
	} else if (spl_lowest_vdev_disk_stack_remaining > r) {
		spl_lowest_vdev_disk_stack_remaining = r;
	}

	ASSERT(zio->io_size != 0);

	PIRP irp = NULL;
	PIO_STACK_LOCATION irpStack = NULL;
	LARGE_INTEGER offset;

	offset.QuadPart = zio->io_offset + dvd->vdev_win_offset;

	/*
	 * Start IO -> IOCompletion callback 'vdev_disk_io_intr()'
	 *  -> IoQueueWorkItem(DelayedWorkQueue) -> callback vdev_disk_io_done()
	 */

	zio->windows.work_item = IoAllocateWorkItem(dvd->vd_DeviceObject);
	if (zio->windows.work_item == NULL) {
		zio->io_error = EIO;
		zio_interrupt(zio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_READ) {

		zio->windows.b_addr =
		    abd_borrow_buf(zio->io_abd, zio->io_size);

		irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ,
		    dvd->vd_DeviceObject,
		    zio->windows.b_addr,
		    (ULONG)zio->io_size,
		    &offset,
		    &zio->windows.IoStatus);

	} else {
		zio->windows.b_addr =
		    abd_borrow_buf_copy(zio->io_abd, zio->io_size);

		irp = IoBuildAsynchronousFsdRequest(IRP_MJ_WRITE,
		    dvd->vd_DeviceObject,
		    zio->windows.b_addr,
		    (ULONG)zio->io_size,
		    &offset,
		    &zio->windows.IoStatus);
	}

	if (!irp) {

		if (zio->io_type == ZIO_TYPE_READ) {
			abd_return_buf_copy(zio->io_abd, zio->windows.b_addr,
			    zio->io_size);
		} else {
			abd_return_buf(zio->io_abd, zio->windows.b_addr,
			    zio->io_size);
		}

		IoFreeWorkItem(zio->windows.work_item);
		zio->io_error = EIO;
		zio_interrupt(zio);
		return;
	}

	zio->windows.irp = irp;

	irpStack = IoGetNextIrpStackLocation(irp);

	irpStack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
	irpStack->FileObject = dvd->vd_FileObject;

	IoSetCompletionRoutine(irp,
	    vdev_disk_io_intr,
	    zio,   // "Context" in vdev_disk_io_intr()
	    TRUE,  // On Success
	    TRUE,  // On Error
	    TRUE); // On Cancel

	IoCallDriver(dvd->vd_DeviceObject, irp);

}

static void
vdev_disk_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;

	/*
	 * If the device returned EIO, then attempt a DKIOCSTATE ioctl to see if
	 * the device has been removed.  If this is the case, then we trigger an
	 * asynchronous removal of the device. Otherwise, probe the device and
	 * make sure it's still accessible.
	 */
	if (zio->io_error == EIO && !vd->vdev_remove_wanted) {
		vdev_disk_t *dvd = vd->vdev_tsd;
//		int state = DKIO_NONE;
		} else if (!vd->vdev_delayed_close) {
			vd->vdev_delayed_close = B_TRUE;
		}
}

static void
vdev_disk_hold(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* We must have a pathname, and it must be absolute. */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/')
		return;

	/*
	 * Only prefetch path and devid info if the device has
	 * never been opened.
	 */
	if (vd->vdev_tsd != NULL)
		return;

}

static void
vdev_disk_rele(vdev_t *vd)
{
	ASSERT(spa_config_held(vd->vdev_spa, SCL_STATE, RW_WRITER));

	/* XXX: Implement me as a vnode rele for the device */
}

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_disk_open,
	.vdev_op_close = vdev_disk_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_disk_io_start,
	.vdev_op_io_done = vdev_disk_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_disk_hold,
	.vdev_op_rele = vdev_disk_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_DISK,	/* name of this vdev type */
	.vdev_op_leaf = B_TRUE		/* leaf vdev */
};

/*
 * Given the root disk device devid or pathname, read the label from
 * the device, and construct a configuration nvlist.
 */
int
vdev_disk_read_rootlabel(char *devpath, char *devid, nvlist_t **config)
{
	return (-1);
}
