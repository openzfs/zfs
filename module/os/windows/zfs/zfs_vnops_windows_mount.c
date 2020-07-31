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
 * Copyright (c) 2019 Jorgen Lundman <lundman@lundman.net>
 */
#define INITGUID
//#include <ntdef.h>
//#include <wdm.h>
#include <Ntifs.h>
#include <intsafe.h>
#include <ntddvol.h>
//#include <ntddstor.h>
#include <ntdddisk.h>
//#include <wdmguid.h>
#include <mountmgr.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>

#include <sys/unistd.h>
//#include <sys/xattr.h>
#include <sys/uuid.h>
//#include <sys/utfconv.h>

#include <sys/types.h>
#include <sys/w32_types.h>
#include <sys/zfs_mount.h>

#include <sys/zfs_windows.h>

#undef _NTDDK_

#include <wdmsec.h>
#pragma comment(lib, "wdmsec.lib")


extern int getzfsvfs(const char *dsname, zfsvfs_t **zfvp);

uint64_t zfs_disable_removablemedia = 0;


/*
* Jump through the hoops needed to make a mount happen.
*
* Create a new Volume name
* Register a new unknown device
* Assign volume name
* Register device as disk
* fill in disk information
* broadcast information
*/

NTSTATUS mountmgr_add_drive_letter(PDEVICE_OBJECT mountmgr, PUNICODE_STRING devpath) 
{
	NTSTATUS Status;
	ULONG mmdltsize;
	MOUNTMGR_DRIVE_LETTER_TARGET* mmdlt;
	MOUNTMGR_DRIVE_LETTER_INFORMATION mmdli;

	mmdltsize = offsetof(MOUNTMGR_DRIVE_LETTER_TARGET, DeviceName[0]) + devpath->Length;

	mmdlt = kmem_alloc(mmdltsize, KM_SLEEP);

	mmdlt->DeviceNameLength = devpath->Length;
	RtlCopyMemory(&mmdlt->DeviceName, devpath->Buffer, devpath->Length);
	dprintf("mmdlt = %.*S\n", mmdlt->DeviceNameLength / sizeof(WCHAR), mmdlt->DeviceName);

	Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER, mmdlt, mmdltsize, &mmdli, sizeof(MOUNTMGR_DRIVE_LETTER_INFORMATION), FALSE, NULL);

	if (!NT_SUCCESS(Status))
		dprintf("IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER returned %08x\n", Status);
	else
		dprintf("DriveLetterWasAssigned = %u, CurrentDriveLetter = %c\n", mmdli.DriveLetterWasAssigned, mmdli.CurrentDriveLetter);

	kmem_free(mmdlt, mmdltsize);

	return Status;
}

/*
 * check if valid mountpoint, like \DosDevices\X:
 */
BOOLEAN MOUNTMGR_IS_DRIVE_LETTER_A(char *mountpoint)
{
	UNICODE_STRING wc_mpt;
	wchar_t buf[PATH_MAX];
	mbstowcs(buf, mountpoint, sizeof (buf));
	RtlInitUnicodeString(&wc_mpt, buf);
	return (MOUNTMGR_IS_DRIVE_LETTER(&wc_mpt));
}

/*
 * check if valid mountpoint, like \??\Volume{abc}
 */
BOOLEAN MOUNTMGR_IS_VOLUME_NAME_A(char *mountpoint)
{
	UNICODE_STRING wc_mpt;
	wchar_t buf[PATH_MAX];
	mbstowcs(buf, mountpoint, sizeof (buf));
	RtlInitUnicodeString(&wc_mpt, buf);
	return (MOUNTMGR_IS_VOLUME_NAME(&wc_mpt));
}

/*
 * Returns the last mountpoint for the device (devpath) (unfiltered)
 * This is either \DosDevices\X: or \??\Volume{abc} in most cases
 * If only_driveletter or only_volume_name is set TRUE,
 * every mountpoint will be checked with MOUNTMGR_IS_DRIVE_LETTER or
 * MOUNTMGR_IS_VOLUME_NAME and discarded if not valid
 * only_driveletter and only_volume_name are mutual exclusive
 */
NTSTATUS mountmgr_get_mountpoint(PDEVICE_OBJECT mountmgr,
	PUNICODE_STRING devpath, char *savename, BOOLEAN only_driveletter,
	BOOLEAN only_volume_name)
{
	MOUNTMGR_MOUNT_POINT point = { 0 };
	MOUNTMGR_MOUNT_POINTS points;
	PMOUNTMGR_MOUNT_POINTS ppoints = NULL;
	int len;
	NTSTATUS Status;

	if (only_driveletter && only_volume_name)
		return STATUS_INVALID_PARAMETER;

	ppoints = &points;
	Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_QUERY_POINTS, &point, sizeof(MOUNTMGR_MOUNT_POINT), ppoints, sizeof(MOUNTMGR_MOUNT_POINTS), FALSE, NULL);

	if (Status == STATUS_BUFFER_OVERFLOW) {
		len = points.Size;
		ppoints = kmem_alloc(len, KM_SLEEP);
		Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_QUERY_POINTS, &point, sizeof(MOUNTMGR_MOUNT_POINT), ppoints, len, FALSE, NULL);

	}
	dprintf("IOCTL_MOUNTMGR_QUERY_POINTS return %x - looking for '%wZ'\n", Status,
		devpath);
	if (Status == STATUS_SUCCESS) {
		for (int Index = 0;
			Index < ppoints->NumberOfMountPoints;
			Index++) {
			PMOUNTMGR_MOUNT_POINT ipoint = ppoints->MountPoints + Index;
			PWCHAR DeviceName = (PWCHAR)((PUCHAR)ppoints + ipoint->DeviceNameOffset);
			PWCHAR SymbolicLinkName = (PWCHAR)((PUCHAR)ppoints + ipoint->SymbolicLinkNameOffset);

			// Why is this hackery needed, we should be able to lookup the drive letter from volume name
			dprintf("   point %d: '%.*S' '%.*S'\n", Index,
				ipoint->DeviceNameLength / sizeof(WCHAR), DeviceName,
				ipoint->SymbolicLinkNameLength / sizeof(WCHAR), SymbolicLinkName);
			if (wcsncmp(DeviceName, devpath->Buffer, ipoint->DeviceNameLength / sizeof(WCHAR)) == 0) {
				ULONG length = 0;
				RtlUnicodeToUTF8N(savename, MAXPATHLEN, &length, SymbolicLinkName, ipoint->SymbolicLinkNameLength);
				savename[length] = 0;
				if (only_driveletter && !MOUNTMGR_IS_DRIVE_LETTER_A(savename))
					savename[0] = 0;
				else if (only_volume_name && !MOUNTMGR_IS_VOLUME_NAME_A(savename))
					savename[0] = 0;
			}
		}
	}

	if (ppoints != NULL) kmem_free(ppoints, len);
	return STATUS_SUCCESS;
}

/*
* Returns the last valid mountpoint of the device according to MOUNTMGR_IS_DRIVE_LETTER()
*/
NTSTATUS mountmgr_get_drive_letter(	PDEVICE_OBJECT mountmgr,
	PUNICODE_STRING devpath, char *savename)
{
	return mountmgr_get_mountpoint(mountmgr, devpath, savename, TRUE, FALSE);
}

/*
* Returns the last valid mountpoint of the device according to MOUNTMGR_IS_VOLUME_NAME()
*/
NTSTATUS mountmgr_get_volume_name_mountpoint(PDEVICE_OBJECT mountmgr,
	PUNICODE_STRING devpath, char *savename)
{
	return mountmgr_get_mountpoint(mountmgr, devpath, savename, FALSE, TRUE);
}

NTSTATUS
	SendIoctlToMountManager(__in ULONG IoControlCode, __in PVOID InputBuffer,
	__in ULONG Length, __out PVOID OutputBuffer,
	__in ULONG OutputLength) 
{
	NTSTATUS status;
	UNICODE_STRING mountManagerName;
	PFILE_OBJECT mountFileObject;
	PDEVICE_OBJECT mountDeviceObject;
	PIRP irp;
	KEVENT driverEvent;
	IO_STATUS_BLOCK iosb;

	RtlInitUnicodeString(&mountManagerName, MOUNTMGR_DEVICE_NAME);

	status = IoGetDeviceObjectPointer(&mountManagerName, FILE_READ_ATTRIBUTES,
		&mountFileObject, &mountDeviceObject);

	if (!NT_SUCCESS(status)) {
		dprintf("  IoGetDeviceObjectPointer failed: 0x%x\n", status);
		return status;
	}

	KeInitializeEvent(&driverEvent, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(IoControlCode, mountDeviceObject,
		InputBuffer, Length, OutputBuffer,
		OutputLength, FALSE, &driverEvent, &iosb);

	if (irp == NULL) {
		dprintf("  IoBuildDeviceIoControlRequest failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = IoCallDriver(mountDeviceObject, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&driverEvent, Executive, KernelMode, FALSE, NULL);
	}
	status = iosb.Status;

	ObDereferenceObject(mountFileObject);
	// Don't dereference mountDeviceObject, mountFileObject is enough

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success\n");
	}
	else {
		dprintf("  IoCallDriver failed: 0x%x\n", status);
	}

	return status;
}

NTSTATUS MountMgrChangeNotify(void)
{
	NTSTATUS					status;
	ULONG						length;
	MOUNTMGR_CHANGE_NOTIFY_INFO chinfo_in;
	MOUNTMGR_CHANGE_NOTIFY_INFO chinfo_out;


	dprintf("=> MountMgrChangeNotify\n");

	length = sizeof(MOUNTMGR_CHANGE_NOTIFY_INFO);

	status = SendIoctlToMountManager(
		IOCTL_MOUNTMGR_CHANGE_NOTIFY, &chinfo_in, length, &chinfo_out, length);

	if (NT_SUCCESS(status))
		dprintf("  IoCallDriver success\n");
	else
		dprintf("  IoCallDriver failed: 0x%x\n", status);

	dprintf("<= MountMgrChangeNotify\n");

	return (status);
}

NTSTATUS
SendVolumeArrivalNotification(PUNICODE_STRING DeviceName)
{
	NTSTATUS		status;
	PMOUNTMGR_TARGET_NAME targetName;
	ULONG			length;

	dprintf("=> SendVolumeArrivalNotification: '%wZ'\n", DeviceName);

	length = sizeof(MOUNTMGR_TARGET_NAME) + DeviceName->Length - 1;
	targetName = ExAllocatePool(PagedPool, length);

	if (targetName == NULL) {
		dprintf("  can't allocate MOUNTMGR_TARGET_NAME\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(targetName, length);

	targetName->DeviceNameLength = DeviceName->Length;
	RtlCopyMemory(targetName->DeviceName, DeviceName->Buffer, DeviceName->Length);

	status = SendIoctlToMountManager(
		IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION, targetName, length, NULL, 0);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success\n");
	} else {
		dprintf("  IoCallDriver failed: 0x%x\n", status);
	}

	ExFreePool(targetName);

	dprintf("<= SendVolumeArrivalNotification\n");

	return status;
}


NTSTATUS
RegisterDeviceInterface(__in PDRIVER_OBJECT DriverObject,
	__in PDEVICE_OBJECT DeviceObject, __in mount_t *Dcb)
{
	PDEVICE_OBJECT	pnpDeviceObject = NULL;
	NTSTATUS		status;

	status = IoReportDetectedDevice(
		DriverObject,
		InterfaceTypeUndefined,
		0,
		0,
		NULL,
		NULL,
		FALSE,
		&pnpDeviceObject);

	if (NT_SUCCESS(status)) {
		dprintf("  IoReportDetectedDevice success\n");
	} else {
		dprintf("  IoReportDetectedDevice failed: 0x%x\n", status);
		return status;
	}

	if (IoAttachDeviceToDeviceStack(pnpDeviceObject, DeviceObject) != NULL) {
		dprintf("  IoAttachDeviceToDeviceStack success\n");
	} else {
		dprintf("  IoAttachDeviceToDeviceStack failed\n");
	}

	status = IoRegisterDeviceInterface(
		pnpDeviceObject,
		&GUID_DEVINTERFACE_DISK,
		NULL,
		&Dcb->device_name);

	if (NT_SUCCESS(status)) {
		dprintf("  IoRegisterDeviceInterface success: %wZ\n", &Dcb->device_name);
	} else {
		dprintf("  IoRegisterDeviceInterface failed: 0x%x\n", status);
		return status;
	}

	status = IoSetDeviceInterfaceState(&Dcb->device_name, TRUE);

	if (NT_SUCCESS(status)) {
		dprintf("  IoSetDeviceInterfaceState success\n");
	} else {
		dprintf("  IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	status = IoRegisterDeviceInterface(
		pnpDeviceObject,
		&MOUNTDEV_MOUNTED_DEVICE_GUID,
		NULL,
		&Dcb->fs_name);

	if (NT_SUCCESS(status)) {
		dprintf("  IoRegisterDeviceInterface success: %wZ\n", &Dcb->fs_name);
	} else {
		dprintf("  IoRegisterDeviceInterface failed: 0x%x\n", status);
		return status;
	}

	status = IoSetDeviceInterfaceState(&Dcb->fs_name, TRUE);

	if (NT_SUCCESS(status)) {
		dprintf("  IoSetDeviceInterfaceState success\n");
	} else {
		dprintf("  IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	return status;
}

NTSTATUS
SendVolumeCreatePoint(__in PUNICODE_STRING DeviceName,
	__in PUNICODE_STRING MountPoint) 
{
	NTSTATUS status;
	PMOUNTMGR_CREATE_POINT_INPUT point;
	ULONG length;

	dprintf("=> SendVolumeCreatePoint\n");

	length = sizeof(MOUNTMGR_CREATE_POINT_INPUT) + MountPoint->Length +
		DeviceName->Length;
	point = ExAllocatePool(PagedPool, length);

	if (point == NULL) {
		dprintf("  can't allocate MOUNTMGR_CREATE_POINT_INPUT\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(point, length);

	dprintf("  DeviceName: %wZ\n", DeviceName);
	point->DeviceNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT);
	point->DeviceNameLength = DeviceName->Length;
	RtlCopyMemory((PCHAR)point + point->DeviceNameOffset, DeviceName->Buffer,
		DeviceName->Length);

	dprintf("  MountPoint: %wZ\n", MountPoint);
	point->SymbolicLinkNameOffset =
		point->DeviceNameOffset + point->DeviceNameLength;
	point->SymbolicLinkNameLength = MountPoint->Length;
	RtlCopyMemory((PCHAR)point + point->SymbolicLinkNameOffset,
		MountPoint->Buffer, MountPoint->Length);
	
	status = SendIoctlToMountManager(IOCTL_MOUNTMGR_CREATE_POINT, point,
		length, NULL, 0);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success\n");
	} else {
		dprintf("  IoCallDriver failed: 0x%x\n", status);
	}

	ExFreePool(point);

	dprintf("<= SendVolumeCreatePoint\n");

	return status;
}

NTSTATUS
SendVolumeDeletePoints(__in PUNICODE_STRING MountPoint,
	__in PUNICODE_STRING DeviceName)
{
	NTSTATUS status;
	PMOUNTMGR_MOUNT_POINT point;
	PMOUNTMGR_MOUNT_POINTS deletedPoints;
	ULONG length;
	ULONG olength;

	dprintf("=> SendVolumeDeletePoints: '%wZ'\n", DeviceName);

	if (_wcsnicmp(L"\\DosDevices\\", MountPoint->Buffer, 12)) {
		dprintf("Not a drive letter, skipping\n");
		return STATUS_SUCCESS;
	}

	length = sizeof(MOUNTMGR_MOUNT_POINT) + MountPoint->Length;
	if (DeviceName != NULL) {
		length += DeviceName->Length;
	}
	point = kmem_alloc(length, KM_SLEEP);

	if (point == NULL) {
		dprintf("  can't allocate MOUNTMGR_CREATE_POINT_INPUT\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	olength = sizeof(MOUNTMGR_MOUNT_POINTS) + 1024;
	deletedPoints = kmem_alloc(olength, KM_SLEEP);
	if (deletedPoints == NULL) {
		dprintf("  can't allocate PMOUNTMGR_MOUNT_POINTS\n");
		kmem_free(point, length);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(point, length); //kmem_zalloc
	RtlZeroMemory(deletedPoints, olength);

	dprintf("  MountPoint: %wZ\n", MountPoint);
	point->SymbolicLinkNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
	point->SymbolicLinkNameLength = MountPoint->Length;
	RtlCopyMemory((PCHAR)point + point->SymbolicLinkNameOffset,
		MountPoint->Buffer, MountPoint->Length);
	if (DeviceName != NULL) {
		dprintf("  DeviceName: %wZ\n", DeviceName);
		point->DeviceNameOffset =
			point->SymbolicLinkNameOffset + point->SymbolicLinkNameLength;
		point->DeviceNameLength = DeviceName->Length;
		RtlCopyMemory((PCHAR)point + point->DeviceNameOffset, DeviceName->Buffer,
			DeviceName->Length);
	}

	// Only symbolic link can be deleted with IOCTL_MOUNTMGR_DELETE_POINTS. 
	// If any other entry is specified, the mount manager will ignore subsequent
	// IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION for the same volume ID.
	status = SendIoctlToMountManager(IOCTL_MOUNTMGR_DELETE_POINTS, point,
		length, deletedPoints, olength);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success, %d mount points deleted.\n",
			deletedPoints->NumberOfMountPoints);
	} else {
		dprintf("  IoCallDriver failed: 0x%x\n", status);
	}

	kmem_free(point, length);
	kmem_free(deletedPoints, olength);

	dprintf("<= SendVolumeDeletePoints\n");

	return status;
}

void zfs_release_mount(mount_t *zmo)
{
	FreeUnicodeString(&zmo->symlink_name);
	FreeUnicodeString(&zmo->device_name);
	FreeUnicodeString(&zmo->fs_name);
	FreeUnicodeString(&zmo->uuid);
	FreeUnicodeString(&zmo->mountpoint);

	if (zmo->vpb) {
		zmo->vpb->DeviceObject = NULL;
		zmo->vpb->RealDevice = NULL;
		zmo->vpb->Flags = 0;
	}
}

int zfs_windows_mount(zfs_cmd_t *zc)
{
	dprintf("%s: '%s' '%s'\n", __func__, zc->zc_name, zc->zc_value);
	NTSTATUS status;
	uuid_t uuid;
	char uuid_a[UUID_PRINTABLE_STRING_LENGTH];
	PDEVICE_OBJECT pdo = NULL;
	PDEVICE_OBJECT diskDeviceObject = NULL;
	PDEVICE_OBJECT fsDeviceObject = NULL;

	/*
	 * We expect mountpath (zv_value) to be already sanitised, ie, Windows
	 * translated paths. So it should be on this style:
	 * "\\??\\c:"  mount as drive letter C:
	 * "\\??\\?:"  mount as first available drive letter
	 * "\\??\\c:\\BOOM"  mount as drive letter C:\BOOM
	 */
	int mplen = strlen(zc->zc_value);
	if ((mplen < 6) ||
		strncmp("\\??\\", zc->zc_value, 4)) {
		dprintf("%s: mountpoint '%s' does not start with \\??\\x:", __func__, zc->zc_value);
		return EINVAL;
	}

	zfs_vfs_uuid_gen(zc->zc_name, uuid);
	zfs_vfs_uuid_unparse(uuid, uuid_a);

	char buf[PATH_MAX];
	//snprintf(buf, sizeof(buf), "\\Device\\ZFS{%s}", uuid_a);
	WCHAR				diskDeviceNameBuf[MAXIMUM_FILENAME_LENGTH];    // L"\\Device\\Volume"
	WCHAR				fsDeviceNameBuf[MAXIMUM_FILENAME_LENGTH];      // L"\\Device\\ZFS"
	WCHAR				symbolicLinkNameBuf[MAXIMUM_FILENAME_LENGTH];  // L"\\DosDevices\\Global\\Volume"
	UNICODE_STRING		diskDeviceName;
	UNICODE_STRING		fsDeviceName;
	UNICODE_STRING		symbolicLinkTarget;

	ANSI_STRING pants;
	ULONG				deviceCharacteristics;
	deviceCharacteristics = FILE_DEVICE_IS_MOUNTED;
	/* Allow $recycle.bin - don't set removable. */
	if (!zfs_disable_removablemedia)
		deviceCharacteristics |= FILE_REMOVABLE_MEDIA;

	snprintf(buf, sizeof(buf), "\\Device\\Volume{%s}", uuid_a);
	//	snprintf(buf, sizeof(buf), "\\Device\\ZFS_%s", zc->zc_name);
	pants.Buffer = buf;
	pants.Length = strlen(buf);
	pants.MaximumLength = PATH_MAX;
	status = RtlAnsiStringToUnicodeString(&diskDeviceName, &pants, TRUE);
	dprintf("%s: new devstring '%wZ'\n", __func__, &diskDeviceName);

	status = IoCreateDeviceSecure(WIN_DriverObject,			// DriverObject
		sizeof(mount_t),			// DeviceExtensionSize
		&diskDeviceName,
		FILE_DEVICE_DISK,// DeviceType
		deviceCharacteristics,							// DeviceCharacteristics
		FALSE,						// Not Exclusive
		&SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R, // Default SDDL String
		NULL, // Device Class GUID
		&diskDeviceObject);				// DeviceObject

	if (status != STATUS_SUCCESS) {
		dprintf("IoCreateDeviceSecure returned %08x\n", status);
		return status;
	}
	mount_t *zmo_dcb = diskDeviceObject->DeviceExtension;
	zmo_dcb->type = MOUNT_TYPE_DCB;
	zmo_dcb->size = sizeof(mount_t);
	vfs_setfsprivate(zmo_dcb, NULL);
	dprintf("%s: created dcb at %p asked for size %d\n", __func__, zmo_dcb, sizeof(mount_t));
	AsciiStringToUnicodeString(uuid_a, &zmo_dcb->uuid);
	// Should we keep the name with slashes like "BOOM/lower" or just "lower".
	// Turns out the name in Explorer only works for 4 chars or lower. Why?
#if 0
	char *r;
	if ((r = strrchr(zc->zc_name, '/')) != NULL)
		r = &r[1];
	else
		r = zc->zc_name;
	AsciiStringToUnicodeString(r, &zmo_dcb->name);
#else
	AsciiStringToUnicodeString(zc->zc_name, &zmo_dcb->name);
#endif
	AsciiStringToUnicodeString(buf, &zmo_dcb->device_name);
	//strlcpy(zc->zc_value, buf, sizeof(zc->zc_value)); // Copy to userland
	zmo_dcb->deviceObject = diskDeviceObject;
	dprintf("New device %p has extension %p\n", diskDeviceObject, zmo_dcb);

	snprintf(buf, sizeof(buf), "\\DosDevices\\Global\\Volume{%s}", uuid_a);
	pants.Buffer = buf;
	pants.Length = strlen(buf);
	pants.MaximumLength = PATH_MAX;
	status = RtlAnsiStringToUnicodeString(&symbolicLinkTarget, &pants, TRUE);
	dprintf("%s: new symlink '%wZ'\n", __func__, &symbolicLinkTarget);
	AsciiStringToUnicodeString(buf, &zmo_dcb->symlink_name);

	snprintf(buf, sizeof(buf), "\\Device\\ZFS{%s}", uuid_a);
	pants.Buffer = buf;
	pants.Length = strlen(buf);
	pants.MaximumLength = PATH_MAX;
	status = RtlAnsiStringToUnicodeString(&fsDeviceName, &pants, TRUE);
	dprintf("%s: new fsname '%wZ'\n", __func__, &fsDeviceName);
	AsciiStringToUnicodeString(buf, &zmo_dcb->fs_name);

	diskDeviceObject->Flags |= DO_DIRECT_IO;


	status = IoCreateSymbolicLink(&symbolicLinkTarget, &diskDeviceName);

	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(diskDeviceObject);
		dprintf("  IoCreateSymbolicLink returned 0x%x\n", status);
		return status;
	}

	//InsertMountEntry(WIN_DriverObject, NULL, FALSE);


	// Call ZFS and have it setup a mount "zfsvfs"
	// we don7t have the vcb yet, but we want to find out mount
	// problems early.
	struct zfs_mount_args mnt_args;
	mnt_args.struct_size = sizeof(struct zfs_mount_args);
	mnt_args.optlen = 0;
	mnt_args.mflag = 0; // Set flags
	mnt_args.fspec = zc->zc_name;

	// Mount will temporarily be pointing to "dcb" until the 
	// zfs_vnop_mount() below corrects it to "vcb".
	status = zfs_vfs_mount(zmo_dcb, NULL, (user_addr_t)&mnt_args, NULL);
	dprintf("%s: zfs_vfs_mount() returns %d\n", __func__, status);

	if (status) {
		zfs_release_mount(zmo_dcb);
		IoDeleteDevice(diskDeviceObject);
		return status;
	}

	// Check if we are to mount with driveletter, or path
	// We already check that path is "\\??\\" above, and 
	// at least 6 chars. Seventh char can be zero, or "/"
	// then zero, for drive only mount.
	if ((zc->zc_value[6] == 0) ||
		((zc->zc_value[6] == '/') &&
		(zc->zc_value[7] == 0))) {
		zmo_dcb->justDriveLetter = B_TRUE;
	} else {
		zmo_dcb->justDriveLetter = B_FALSE;
	}

	// Remember mountpoint path
	AsciiStringToUnicodeString(zc->zc_value, &zmo_dcb->mountpoint);

	dprintf("%s: driveletter %d '%wZ'\n", __func__, zmo_dcb->justDriveLetter, &zmo_dcb->mountpoint);

	// Return volume name to userland
	snprintf(zc->zc_value, sizeof(zc->zc_value), "\\DosDevices\\Global\\Volume{%s}", uuid_a);

	// Mark devices as initialized
	diskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	ObReferenceObject(diskDeviceObject);

	dprintf("Verify Volume\n");
	IoVerifyVolume(diskDeviceObject, FALSE);

	status = STATUS_SUCCESS;
	return status;
}

VOID InitVpb(__in PVPB Vpb, __in PDEVICE_OBJECT VolumeDevice) 
{
	if (Vpb != NULL) {
		Vpb->DeviceObject = VolumeDevice;
		Vpb->VolumeLabelLength = (USHORT)wcslen(VOLUME_LABEL) * sizeof(WCHAR);
		RtlStringCchCopyW(Vpb->VolumeLabel,
			sizeof(Vpb->VolumeLabel) / sizeof(WCHAR), VOLUME_LABEL);
		Vpb->SerialNumber = 0x19831116;
		Vpb->Flags |= VPB_MOUNTED;
	}
}


/*
							[ffff9284a0e84080 zpool.exe]
1de4.000b48  ffff9284a56db080 fffd57fc Blocked    nt!KiSwapContext+0x76
										nt!KiSwapThread+0x3f2
										nt!KiCommitThreadWait+0x144
										nt!KeWaitForSingleObject+0x255
										nt!IopWaitForLockAlertable+0x48
										nt!IopMountVolume+0x106
										nt!IopCheckVpbMounted+0x1b3
										nt!IopParseDevice+0x31f
										nt!ObpLookupObjectName+0x78f
										nt!ObOpenObjectByNameEx+0x201
										nt!NtDeleteFile+0x114
										nt!KiSystemServiceCopyEnd+0x25
										nt!KiServiceLinkage
										ZFSin!CreateReparsePoint+0x4c
										ZFSin!zfs_vnop_mount_cont+0x3f8
										ZFSin!zfs_vnop_mount+0x424
										ZFSin!fsDispatcher+0xa15
										ZFSin!dispatcher+0x178
										nt!IofCallDriver+0x59
										+0xfffff804357c076e
										+0xffff9284a03e9010
										+0xffff928400000000

*  
*   Justification: The LPCWSTR could be non-NULL terminated when passed and causes the buffer to be overran.
*/
NTSTATUS CreateReparsePoint(POBJECT_ATTRIBUTES poa, PCUNICODE_STRING SubstituteName,
	PCUNICODE_STRING PrintName)
{
	HANDLE hFile;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;

	dprintf("%s: \n", __func__);

	status = ZwDeleteFile(poa); // this is stalled forever waiting for event of deletion - possibly ZFS doesnt send event?
	if (status != STATUS_SUCCESS) dprintf("pre-rmdir failed 0x%x\n", status);
	status = ZwCreateFile(&hFile, FILE_ALL_ACCESS, poa, &iosb, 0, 0, 0,
		FILE_CREATE, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, 0, 0);
	if (0 > status)
		return status;
	dprintf("%s: create ok\n", __func__);
	USHORT cb = 2 * sizeof(WCHAR) + FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) + SubstituteName->Length + PrintName->Length;
	PREPARSE_DATA_BUFFER prdb = (PREPARSE_DATA_BUFFER)alloca(cb);
	RtlZeroMemory(prdb, cb);
	prdb->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	prdb->ReparseDataLength = cb - REPARSE_DATA_BUFFER_HEADER_SIZE;
	prdb->MountPointReparseBuffer.SubstituteNameLength = SubstituteName->Length;
	prdb->MountPointReparseBuffer.PrintNameLength = PrintName->Length;
	prdb->MountPointReparseBuffer.PrintNameOffset = SubstituteName->Length + sizeof(WCHAR);
	memcpy(prdb->MountPointReparseBuffer.PathBuffer, SubstituteName->Buffer, SubstituteName->Length);
	memcpy(RtlOffsetToPointer(prdb->MountPointReparseBuffer.PathBuffer, SubstituteName->Length + sizeof(WCHAR)), PrintName->Buffer, PrintName->Length);
	status = ZwFsControlFile(hFile, 0, 0, 0, &iosb, FSCTL_SET_REPARSE_POINT, prdb, cb, 0, 0);
	dprintf("%s: ControlFile %d / 0x%x\n", __func__, status, status);

	if (0 > status) {
		static FILE_DISPOSITION_INFORMATION fdi = { TRUE };
		ZwSetInformationFile(hFile, &iosb, &fdi, sizeof fdi, FileDispositionInformation);
	}
	ZwClose(hFile);
	return status;
}


/*
 * go through all mointpoints (IOCTL_MOUNTMGR_QUERY_POINTS)
 * and check if our driveletter is in the list
 * return 1 if yes, otherwise 0
 */
NTSTATUS mountmgr_is_driveletter_assigned(PDEVICE_OBJECT mountmgr,
	wchar_t driveletter, BOOLEAN *ret)
{
	MOUNTMGR_MOUNT_POINT point = { 0 };
	MOUNTMGR_MOUNT_POINTS points;
	PMOUNTMGR_MOUNT_POINTS ppoints = NULL;
	int len;
	*ret = 0;
	NTSTATUS Status;

	ppoints = &points;
	Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_QUERY_POINTS, &point,
		sizeof(MOUNTMGR_MOUNT_POINT), ppoints,
		sizeof(MOUNTMGR_MOUNT_POINTS), FALSE, NULL);

	if (Status == STATUS_BUFFER_OVERFLOW) {
		len = points.Size;
		ppoints = kmem_alloc(len, KM_SLEEP);
		Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_QUERY_POINTS,
			&point, sizeof(MOUNTMGR_MOUNT_POINT), ppoints,
			len, FALSE, NULL);
	}
	dprintf("IOCTL_MOUNTMGR_QUERY_POINTS return %x - looking for driveletter '%c'\n",
		Status, driveletter);
	if (Status == STATUS_SUCCESS) {
		char mpt_name[PATH_MAX] = { 0 };
		for (int Index = 0;
			Index < ppoints->NumberOfMountPoints;
			Index++) {
			PMOUNTMGR_MOUNT_POINT ipoint = ppoints->MountPoints + Index;
			PWCHAR DeviceName = (PWCHAR)((PUCHAR)ppoints + ipoint->DeviceNameOffset);
			PWCHAR SymbolicLinkName = (PWCHAR)((PUCHAR)ppoints + ipoint->SymbolicLinkNameOffset);

			dprintf("   point %d: '%.*S' '%.*S'\n", Index,
				ipoint->DeviceNameLength / sizeof(WCHAR), DeviceName,
				ipoint->SymbolicLinkNameLength / sizeof(WCHAR), SymbolicLinkName);

			ULONG length = 0;
			RtlUnicodeToUTF8N(mpt_name, MAXPATHLEN, &length, SymbolicLinkName,
				ipoint->SymbolicLinkNameLength);
			mpt_name[length] = 0;
			char c_driveletter;
			wctomb(&c_driveletter, driveletter);
			if (MOUNTMGR_IS_DRIVE_LETTER_A(mpt_name) && mpt_name[12] == c_driveletter) {
				*ret = 1;
				if (ppoints != NULL) kmem_free(ppoints, len);
				return STATUS_SUCCESS;
			}
		}
	}

	if (ppoints != NULL) kmem_free(ppoints, len);
	return (Status);
}

/*
 * assign driveletter with IOCTL_MOUNTMGR_CREATE_POINT
 */
NTSTATUS mountmgr_assign_driveletter(PUNICODE_STRING device_name,
	wchar_t driveletter)
{
	DECLARE_UNICODE_STRING_SIZE(mpt, 16);
	RtlUnicodeStringPrintf(&mpt, L"\\DosDevices\\%c:", driveletter);
	return (SendVolumeCreatePoint(device_name, &mpt));
}


/*
 * assign next free driveletter (D..Z) if mountmgr is offended and refuses to do it
 */
NTSTATUS SetNextDriveletterManually(PDEVICE_OBJECT mountmgr,
	PUNICODE_STRING device_name)
{
	NTSTATUS status;
	for (wchar_t c = 'D'; c <= 'Z'; c++) {
		BOOLEAN ret;
		status = mountmgr_is_driveletter_assigned(mountmgr, c, &ret);
		if (status == STATUS_SUCCESS && ret == 0) {
			status = mountmgr_assign_driveletter(device_name, c);

			if (status == STATUS_SUCCESS) {
				// prove it 
				status = mountmgr_is_driveletter_assigned(mountmgr, c, &ret);
				if (status == STATUS_SUCCESS) {
					if (ret == 1)
						return STATUS_SUCCESS;
					else
						return STATUS_VOLUME_DISMOUNTED;
				} else {
					return status;
				}
			}
		}
	}
	return status;
}



void generateGUID(char* pguid)
{
	char *uuid_format = "xxxxxxxx-xxxx-4xxx-Nxxx-xxxxxxxxxxxx";
	char *szHex = "0123456789ABCDEF-";
	int len = strlen(uuid_format);

	for (int i = 0; i < len + 1; i++)
	{
		int r = rand() % 16;
		char c = ' ';

		switch (uuid_format[i])
		{
		case 'x': { c = szHex[r]; } break;
		case 'N': { c = szHex[r & 0x03 | 0x08]; } break;
		case '-': { c = '-'; } break;
		case '4': { c = '4'; } break;
		}

		pguid[i] = (i < len) ? c : 0x00;
	}
}


void generateVolumeNameMountpoint(wchar_t *vol_mpt)
{
	char GUID[50];
	wchar_t wc_guid[50];
	generateGUID(&GUID);
	mbstowcs(&wc_guid, GUID, 50);
	int len = _snwprintf(vol_mpt, 50, L"\\??\\Volume{%s}", wc_guid);
}

int zfs_vnop_mount(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	PDRIVER_OBJECT DriverObject = DiskDevice->DriverObject;
	PDEVICE_OBJECT volDeviceObject;
	NTSTATUS status;
	PDEVICE_OBJECT DeviceToMount;

	dprintf("%s\n", __func__);

#if 0
	DeviceToMount = IrpSp->Parameters.MountVolume.DeviceObject;

	dprintf("*** mount request for %p : minor\n", DeviceToMount);
	delay(hz << 1);
	PDEVICE_OBJECT pdo = DeviceToMount;

	MOUNTDEV_NAME *mdn2;
	mdn2 = kmem_alloc(256, KM_SLEEP);
	status = dev_ioctl(pdo, IOCTL_MOUNTDEV_QUERY_DEVICE_NAME, NULL, 0, mdn2, 256, TRUE, NULL);
	if (NT_SUCCESS(status)) {
		dprintf("%s: given deviceName '%.*S'\n", __func__,
			mdn2->NameLength / sizeof(WCHAR), mdn2->Name);
		delay(hz << 1);
	}

	while (IoGetLowerDeviceObject(pdo)) {
		pdo = IoGetLowerDeviceObject(pdo);
		dprintf(".. going deeper %p\n", pdo);
		status = dev_ioctl(pdo, IOCTL_MOUNTDEV_QUERY_DEVICE_NAME, NULL, 0, mdn2, 256, TRUE, NULL);
		if (NT_SUCCESS(status)) {
			dprintf("%s: given deviceName '%.*S'\n", __func__,
				mdn2->NameLength / sizeof(WCHAR), mdn2->Name);
			delay(hz << 1);
		}
	}
	kmem_free(mdn2, 256);
	dprintf("done dumping device names\n");
#else
	if (IrpSp->Parameters.MountVolume.DeviceObject == NULL) {
		dprintf("%s: MountVolume is NULL\n", __func__);
		return STATUS_UNRECOGNIZED_VOLUME;
	}

	DeviceToMount = IoGetDeviceAttachmentBaseRef(IrpSp->Parameters.MountVolume.DeviceObject);
	dprintf("*** mount request for %p : minor\n", DeviceToMount);

	if (DeviceToMount == NULL) {
		dprintf("%s: DeviceToMount is NULL\n", __func__);
		return STATUS_UNRECOGNIZED_VOLUME;
	}

	// DeviceToMount must be released from here down

	if (DeviceToMount->DriverObject == WIN_DriverObject) {
		dprintf("*** The device belong to us\n");
	} else {
		dprintf("*** The device does NOT belong to us\n");
		status = STATUS_UNRECOGNIZED_VOLUME;
		goto out;
	}
#endif
	mount_t *dcb = DeviceToMount->DeviceExtension;
	if (dcb == NULL) {
		dprintf("%s: Not a ZFS dataset -- ignoring\n", __func__);
		status = STATUS_UNRECOGNIZED_VOLUME;
		goto out;
	}
		
	if ((dcb->type != MOUNT_TYPE_DCB) ||
		(dcb->size != sizeof(mount_t))) {
		dprintf("%s: Not a ZFS dataset -- dcb %p ignoring: type 0x%x != 0x%x, size %d != %d\n", 
			__func__, dcb,
			dcb->type, MOUNT_TYPE_DCB, dcb->size, sizeof(mount_t));
		status = STATUS_UNRECOGNIZED_VOLUME;
		goto out;
	}

	// ZFS Dataset being mounted:
	//dprintf("%s: mounting '%wZ'\n", __func__, dcb->name);

	// We created a DISK before, now we create a VOLUME
	ULONG				deviceCharacteristics;
	deviceCharacteristics = FILE_DEVICE_IS_MOUNTED;
	/* Allow $recycle.bin - don't set removable. */
	if (!zfs_disable_removablemedia)
		deviceCharacteristics |= FILE_REMOVABLE_MEDIA;

	status = IoCreateDevice(DriverObject,               // DriverObject
		sizeof(mount_t),           // DeviceExtensionSize
		NULL,                       // DeviceName
		FILE_DEVICE_DISK,      // DeviceType  FILE_DEVICE_DISK_FILE_SYSTEM
		deviceCharacteristics, // DeviceCharacteristics
		FALSE,                      // Not Exclusive
		&volDeviceObject);          // DeviceObject

	if (!NT_SUCCESS(status)) {
		dprintf("%s: IoCreateDevice failed: 0x%x\n", __func__, status);
		goto out;
	}

	mount_t *vcb = volDeviceObject->DeviceExtension;
	vcb->type = MOUNT_TYPE_VCB;
	vcb->size = sizeof(mount_t);

	// FIXME for proper sync
	if (vfs_fsprivate(dcb) == NULL) delay(hz);

	// Move the fsprivate ptr from dcb to vcb
	vfs_setfsprivate(vcb, vfs_fsprivate(dcb)); // HACK
	vfs_setfsprivate(dcb, NULL);
	zfsvfs_t *zfsvfs = vfs_fsprivate(vcb);
	if (zfsvfs == NULL) {
		dprintf("zfsvfs not resolved yet\n");
		status = STATUS_MOUNT_POINT_NOT_RESOLVED;
		goto out;
	}
	zfsvfs->z_vfs = vcb;

	// Remember the parent device, so during unmount we can free both.
	vcb->parent_device = dcb;

	// vcb is the ptr used in unmount, so set both devices here.
	//vcb->diskDeviceObject = dcb->deviceObject;
	vcb->deviceObject = volDeviceObject;

	RtlDuplicateUnicodeString(0, &dcb->fs_name, &vcb->fs_name);
	RtlDuplicateUnicodeString(0, &dcb->name, &vcb->name);
	RtlDuplicateUnicodeString(0, &dcb->device_name, &vcb->device_name);
	RtlDuplicateUnicodeString(0, &dcb->symlink_name, &vcb->symlink_name);
	RtlDuplicateUnicodeString(0, &dcb->uuid, &vcb->uuid);

	vcb->mountflags = dcb->mountflags;

	// Directory notification
	InitializeListHead(&vcb->DirNotifyList);
	FsRtlNotifyInitializeSync(&vcb->NotifySync);
	//   FsRtlNotifyCleanup(vcb->NotifySync, &vcb->DirNotifyList, ccb);
	// VOID FsRtlNotifyCleanupAll(
	//_In_ PNOTIFY_SYNC NotifySync,
	//	_In_ PLIST_ENTRY  NotifyList
	//	);

	PVPB vpb = NULL;
	vpb = IrpSp->Parameters.MountVolume.Vpb;
	InitVpb(vpb, volDeviceObject);
	vcb->vpb = vpb;
	dcb->vpb = vpb;

	volDeviceObject->Flags |= DO_DIRECT_IO;
	volDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	//SetLongFlag(vcb->Flags, VCB_MOUNTED);

	ObReferenceObject(volDeviceObject);


	status = SendVolumeArrivalNotification(&dcb->device_name);
	if (!NT_SUCCESS(status)) {
		dprintf("  SendVolumeArrivalNotification failed: 0x%x\n", status);
	}
#if 0
	UNICODE_STRING  mountp;
	UNICODE_STRING  devv;
	//RtlInitUnicodeString(&mountp, L"\\DosDevices\\F:");
	RtlInitUnicodeString(&mountp, L"\\DosDevices\\Global\\C:\\BOOM\\");
	dprintf("Trying to connect %wZ with %wZ\n", &mountp, &dcb->device_name);
	status = IoCreateSymbolicLink(&mountp, &dcb->device_name);
	dprintf("Create symlink said %d / 0x%x\n", status, status);
	RtlInitUnicodeString(&mountp, L"\\DosDevices\\Global\\C:\\BOOM");
	dprintf("Trying to connect %wZ with %wZ\n", &mountp, &dcb->symlink_name);
	status = IoCreateSymbolicLink(&mountp, &dcb->symlink_name);
	dprintf("Create symlink said %d / 0x%x\n", status, status);

	RtlInitUnicodeString(&mountp, L"\\DosDevices\\Global\\C:\\BOOM");
	RtlInitUnicodeString(&devv, L"\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}");
	dprintf("Trying to connect %wZ with %wZ\n", &mountp, &devv);
	status = IoCreateSymbolicLink(&mountp, &devv);
	dprintf("Create symlink said %d / 0x%x\n", status, status);

	RtlInitUnicodeString(&mountp, L"\\DosDevices\\Global\\C:\\BOOM");
	RtlInitUnicodeString(&devv, L"\\Devices\\ZFS_BOOM\\");
	dprintf("Trying to connect %wZ with %wZ\n", &mountp, &devv);
	status = IoCreateSymbolicLink(&mountp, &devv);
	dprintf("Create symlink said %d / 0x%x\n", status, status);

	SendVolumeCreatePoint(&dcb->symlink_name, &mountp);
	//gui	0x560000
	// IOCTL_DISK_GET_PARTITION_INFO_EX	0x70048
#endif


	// Set the mountpoint if necessary
#if 0
	OBJECT_ATTRIBUTES poa;
	UNICODE_STRING usStr;
	RtlInitUnicodeString(&usStr, L"\\??\\c:\\BOOM");
	InitializeObjectAttributes(&poa, &usStr, OBJ_KERNEL_HANDLE, NULL, NULL);
	//CreateReparsePoint(&poa, L"\\??\\Volume{7cc383a0-beac-11e7-b56d-02150b22a130}", L"AnyBOOM");
	CreateReparsePoint(&poa, L"\\??\\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}", L"AnyBOOM");
#endif
	UNICODE_STRING	name;
	PFILE_OBJECT	fileObject;
	PDEVICE_OBJECT	mountmgr;

	// Query MntMgr for points, just informative
	RtlInitUnicodeString(&name, MOUNTMGR_DEVICE_NAME);
	status = IoGetDeviceObjectPointer(&name, FILE_READ_ATTRIBUTES, &fileObject,
		&mountmgr);
	char namex[PATH_MAX] = "";
	status = mountmgr_get_drive_letter(mountmgr, &dcb->device_name, namex);

	// Check if we are to mount as path or just drive letter
	if (dcb->justDriveLetter) {

		// If SendVolumeArrival was executed successfully we should have two mountpoints
		// point 1: \Device\Volumes{abc}	\DosDevices\X:
		// point 2: \Device\Volumes{abc}	\??\Volume{xyz}
		// but if we are in remount and removed the mountpoints for this volume manually before
		// they won't get assigned by mountmgr automatically anymore.
		// So at least check if we got them and if not, try to create.

		if (!MOUNTMGR_IS_DRIVE_LETTER_A(namex)) {

			namex[0] = 0;
			status = mountmgr_get_volume_name_mountpoint(mountmgr, &dcb->device_name, &namex);
			if (!MOUNTMGR_IS_VOLUME_NAME_A(namex)) {
				// We have no volume name mountpoint for our device,
				// so generate a valid GUID and mount the device
				UNICODE_STRING vol_mpt;
				wchar_t buf[50];
				generateVolumeNameMountpoint(&buf);
				RtlInitUnicodeString(&vol_mpt, buf);
				status = SendVolumeCreatePoint(&dcb->device_name, &vol_mpt);
			}

			// If driveletter was provided, try to add it as mountpoint
			if (dcb && dcb->mountpoint.Length > 0 && dcb->mountpoint.Buffer[4] != '?') {
				// check if driveletter is unassigned
				BOOLEAN ret;
				status = mountmgr_is_driveletter_assigned(mountmgr, dcb->mountpoint.Buffer[4], &ret);

				if (status == STATUS_SUCCESS && ret == 0) {
					// driveletter is unassigned, try to add mountpoint
					status = mountmgr_assign_driveletter(&dcb->device_name, dcb->mountpoint.Buffer[4]);
				} else {
					// driveletter already assigned, find another one
					SetNextDriveletterManually(mountmgr, &dcb->device_name);
				}
			} else {
				// user provided no driveletter, find one on our own
				SetNextDriveletterManually(mountmgr, &dcb->device_name);
			}
		} // !MOUNTMGR_IS_DRIVE_LETTER(&actualDriveletter)
		namex[0] = 0;
		status = mountmgr_get_drive_letter(mountmgr, &dcb->device_name, namex);
	} else {

		OBJECT_ATTRIBUTES poa;
		DECLARE_UNICODE_STRING_SIZE(volStr, ZFS_MAX_DATASET_NAME_LEN); // 36(uuid) + 6 (punct) + 6 (Volume)
		RtlUnicodeStringPrintf(&volStr, L"\\??\\Volume{%wZ}", vcb->uuid); // "\??\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}"
		InitializeObjectAttributes(&poa, &dcb->mountpoint, OBJ_KERNEL_HANDLE, NULL, NULL);
		dprintf("Creating reparse mountpoint on '%wZ' for volume '%wZ'\n", &dcb->mountpoint, &volStr);
		CreateReparsePoint(&poa, &volStr, &vcb->name); // 3rd arg is visible in DOS box

		// Remove drive letter?
		// RtlUnicodeStringPrintf(&volStr, L"\\DosDevices\\E:");  // FIXME
		// RtlUnicodeStringPrintf(&volStr, L"%s", namex); // "\??\Volume{0b1bb601-af0b-32e8-a1d2-54c167af6277}"
		ULONG lenx;
		RtlUTF8ToUnicodeN(volStr.Buffer, ZFS_MAX_DATASET_NAME_LEN, &lenx, namex, strlen(namex));
		volStr.Length = lenx;

		status = SendVolumeDeletePoints(&volStr, &dcb->device_name);

	}

	// match IoGetDeviceAttachmentBaseRef()
	ObDereferenceObject(fileObject);

out:
	ObDereferenceObject(DeviceToMount);
	dprintf("%s: exit: 0x%x\n", __func__, status);
	return status;
}

int zfs_remove_driveletter(mount_t *zmo)
{
	UNICODE_STRING name;
	PFILE_OBJECT                        fileObject;
	PDEVICE_OBJECT                      mountmgr;
	NTSTATUS Status;

	dprintf("%s: removing driveletter for '%wZ'\n", __func__, &zmo->name);

	// Query MntMgr for points, just informative
	RtlInitUnicodeString(&name, MOUNTMGR_DEVICE_NAME);
	Status = IoGetDeviceObjectPointer(&name, FILE_READ_ATTRIBUTES, &fileObject,
		&mountmgr);
	ObDereferenceObject(fileObject);

	MOUNTMGR_MOUNT_POINT* mmp = NULL;
	ULONG mmpsize;
	MOUNTMGR_MOUNT_POINTS mmps1, *mmps2;

	mmpsize = sizeof(MOUNTMGR_MOUNT_POINT) + zmo->device_name.Length;

	mmp = kmem_zalloc(mmpsize, KM_SLEEP);
	
	mmp->DeviceNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
	mmp->DeviceNameLength = zmo->device_name.Length;
	RtlCopyMemory(&mmp[1], zmo->device_name.Buffer, zmo->device_name.Length);

	Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_DELETE_POINTS, mmp, mmpsize, &mmps1, sizeof(MOUNTMGR_MOUNT_POINTS), FALSE, NULL);

	if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) {
		goto out;
	}

	if (Status != STATUS_BUFFER_OVERFLOW || mmps1.Size == 0) {
		Status = STATUS_NOT_FOUND;
		goto out;
	}

	mmps2 = kmem_zalloc(mmps1.Size, KM_SLEEP);

	Status = dev_ioctl(mountmgr, IOCTL_MOUNTMGR_DELETE_POINTS, mmp, mmpsize, mmps2, mmps1.Size, FALSE, NULL);

	//if (!NT_SUCCESS(Status))
	//	ERR("IOCTL_MOUNTMGR_DELETE_POINTS 2 returned %08x\n", Status);

out:
	dprintf("%s: removing driveletter returns 0x%x\n", __func__, Status);

	if (mmps2)
		kmem_free(mmps2, mmps1.Size);
	if (mmp)
		kmem_free(mmp, mmpsize);

	ObDereferenceObject(mountmgr);
	return Status;
}

int zfs_windows_unmount(zfs_cmd_t *zc)
{
	// IRP_MN_QUERY_REMOVE_DEVICE
	// IRP_MN_REMOVE_DEVICE
	// FsRtlNotifyVolumeEvent(, FSRTL_VOLUME_DISMOUNT);

	// Use name, lookup zfsvfs
	// use zfsvfs to get mount_t
	// mount_t has deviceObject, names etc.
	mount_t *zmo;
	mount_t *zmo_dcb = NULL;
	zfsvfs_t *zfsvfs;
	int error = EBUSY;
	znode_t *zp;
	//int rdonly;

	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {

		zmo = zfsvfs->z_vfs;
		ASSERT(zmo->type == MOUNT_TYPE_VCB);

		// Flush volume
		// rdonly = !spa_writeable(dmu_objset_spa(zfsvfs->z_os));
		error = zfs_vfs_unmount(zmo, 0, NULL);
		dprintf("%s: zfs_vfs_unmount %d\n", __func__, error);
		if (error) goto out_unlock;

		// Delete mountpoints for our volume manually
		// Query the mountmgr for mountpoints and delete them until no mountpoint is left
		// Because we are not satisfied with mountmgrs work, it gets offended and
		// doesn't automatically create mointpoints for our volume after we deleted them manually
		// But as long as we recheck that in mount and create points manually (if necessary),
		// that should be ok hopefully

		UNICODE_STRING	name;
		PFILE_OBJECT	fileObject;
		PDEVICE_OBJECT	mountmgr;

		// Query MntMgr for points, just informative
		RtlInitUnicodeString(&name, MOUNTMGR_DEVICE_NAME);
		NTSTATUS status = IoGetDeviceObjectPointer(&name, FILE_READ_ATTRIBUTES,
			&fileObject, &mountmgr);
		char namex[PATH_MAX] = "";
		status = mountmgr_get_drive_letter(mountmgr, &zmo->device_name, namex);

		// We used to loop here and keep deleting anything we find, but we
		// are only allowed to remove symlinks, anything else and MountMgr
		// ignores the device.
#if 0
		while (strlen(namex) > 0) {
			UNICODE_STRING unicode_mpt;
			wchar_t wbuf[PATH_MAX];
			mbstowcs(wbuf, namex, sizeof(namex));
			RtlInitUnicodeString(&unicode_mpt, wbuf);
			status = SendVolumeDeletePoints(&unicode_mpt, &zmo->device_name);
			namex[0] = 0;
			status = mountmgr_get_mountpoint(mountmgr, &zmo->device_name, namex, FALSE, FALSE);
		}
#endif
		ObDereferenceObject(fileObject);


		// Save the parent device
		zmo_dcb = zmo->parent_device;

		// Release any notifications
#if (NTDDI_VERSION >= NTDDI_VISTA)
		FsRtlNotifyCleanupAll(zmo->NotifySync, &zmo->DirNotifyList);
#endif

		// Release devices
		IoDeleteSymbolicLink(&zmo->symlink_name);

		// fsDeviceObject
		if (zmo->deviceObject)
			IoDeleteDevice(zmo->deviceObject);
		// diskDeviceObject
		if (zmo->diskDeviceObject)
			IoDeleteDevice(zmo->diskDeviceObject);

		zfs_release_mount(zmo);

		// There should also be a diskDevice above us to release.
		if (zmo_dcb != NULL) {
			if (zmo_dcb->deviceObject)
				IoDeleteDevice(zmo_dcb->deviceObject);
			if (zmo_dcb->diskDeviceObject)
				IoDeleteDevice(zmo_dcb->diskDeviceObject);
			zfs_release_mount(zmo_dcb);
		}


		error = 0;

out_unlock:
		// counter to getzfvfs
		vfs_unbusy(zfsvfs->z_vfs);
	}
	return error;
}
