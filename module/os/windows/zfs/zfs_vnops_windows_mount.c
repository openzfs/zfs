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
#define	INITGUID
#include <Ntifs.h>
#include <intsafe.h>
#include <ntddvol.h>
#include <ntdddisk.h>
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
#include <sys/uuid.h>

#include <sys/types.h>
#include <sys/zfs_mount.h>

#include <sys/zfs_windows.h>
#include <sys/driver_extension.h>
#include <sys/zfs_vss.h>

#undef _NTDDK_

#include <wdmsec.h>
#pragma comment(lib, "wdmsec.lib")

DEFINE_GUID(GUID_IO_VOLUME_DISMOUNT, 0xd16a55e8, 0x1059, 0x11d2,
    0x8f, 0x61, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e);

extern int getzfsvfs(const char *dsname, zfsvfs_t **zfvp);

uint64_t zfs_disable_removablemedia = 1;
ZFS_MODULE_RAW(zfs, disable_removablemedia, zfs_disable_removablemedia,
    U64, ZMOD_RW, 0, "Disable Removable Media");

extern kmem_cache_t *znode_cache;

int zfs_windows_unmount_impl(const char *datasetname);

/*
 * Give permissions to the volume, in particular
 * DELETE so regular users can delete items on the volume,
 * or regular users will get ACCESS_DENIED when trying to delete.
 */
static const UNICODE_STRING sddlVolume = RTL_CONSTANT_STRING(
    L"D:P"		// DACL, protected
    L"(A;;FA;;;SY)"	// System = Full
    L"(A;;FA;;;BA)"	// Admins = Full
    L"(A;;GA;;;AU)"	// Authenticated Users = GenericAll
);

/*
 * Jump through the hoops needed to make a mount happen.
 *
 * Create a new Volume name
 * Register a new unknown device
 * Assign volume name
 * Register device as disk
 * fill in disk information
 * broadcast information
 *
 * Important details when watching `mountvol` adding E:\ntfs
 * to volume;
 * Paths should start with "\??\", as in "\??\Volume{xxxxx}"
 * If it ends with trailing backslash, remove it for MountMgr, but
 * it is needed for ReparsePoint.
 * When announcing to MountMgr, IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED
 * path is changed to
 * SourceVolumeName: "\??\E:\ntfs\" then final backslash removed (len-=2)
 * TargetVolumeName: "\??\Volume{6585de16-a73e-451e-8962-443130fde716}"
 *
 */

/*
 * check if valid mountpoint, like \DosDevices\X:
 */
BOOLEAN
MOUNTMGR_IS_DRIVE_LETTER_A(char *mountpoint)
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
BOOLEAN
MOUNTMGR_IS_VOLUME_NAME_A(char *mountpoint)
{
	UNICODE_STRING wc_mpt;
	wchar_t buf[PATH_MAX];
	mbstowcs(buf, mountpoint, sizeof (buf));
	RtlInitUnicodeString(&wc_mpt, buf);
	return (MOUNTMGR_IS_VOLUME_NAME(&wc_mpt));
}

NTSTATUS
SendIoctlToMountManager(__in ULONG IoControlCode, __in PVOID InputBuffer,
    __in ULONG Length, __out PVOID OutputBuffer,
    __in ULONG OutputLength)
{
	NTSTATUS status;
	PIRP irp;
	KEVENT driverEvent;
	IO_STATUS_BLOCK iosb;

	UNICODE_STRING	mountmgrname;
	RtlInitUnicodeString(&mountmgrname, MOUNTMGR_DEVICE_NAME);
	PFILE_OBJECT mountmgr_fileobject = NULL;
	PDEVICE_OBJECT mountmgr_deviceobject = NULL;

	(void) IoGetDeviceObjectPointer(&mountmgrname, FILE_READ_ATTRIBUTES,
	    &mountmgr_fileobject, &mountmgr_deviceobject);

	if (mountmgr_deviceobject == NULL) {
		dprintf("mountmgr_get_mountpoint: failed\n");
		return (STATUS_INVALID_PARAMETER);
	}

	KeInitializeEvent(&driverEvent, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(IoControlCode,
	    mountmgr_deviceobject, InputBuffer, Length,
	    OutputBuffer, OutputLength, FALSE, &driverEvent, &iosb);

	if (irp == NULL) {
		dprintf("  IoBuildDeviceIoControlRequest failed\n");
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	// Deadlock possible here.
	status = IoCallDriver(mountmgr_deviceobject, irp);

	if (status == STATUS_PENDING)
		KeWaitForSingleObject(&driverEvent, Executive, KernelMode,
		    FALSE, NULL);

	status = iosb.Status;

	if (NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) {
		dprintf("  IoCallDriver success\n");
	} else {
		dprintf("  IoCallDriver failed: 0x%lx\n", status);
	}

	if (mountmgr_fileobject)
		ObDereferenceObject(mountmgr_fileobject);

	return (status);
}

/*
 * Returns the last mountpoint for the device (devpath) (unfiltered)
 * This is either \DosDevices\X: or \??\Volume{abc} in most cases
 * If only_driveletter or only_volume_name is set TRUE,
 * every mountpoint will be checked with MOUNTMGR_IS_DRIVE_LETTER or
 * MOUNTMGR_IS_VOLUME_NAME and discarded if not valid
 * only_driveletter and only_volume_name are mutual exclusive
 */
NTSTATUS
mountmgr_get_mountpoint(PUNICODE_STRING devpath, PUNICODE_STRING savename,
    BOOLEAN only_driveletter, BOOLEAN only_volume_name)
{
	MOUNTMGR_MOUNT_POINT point = { 0 };
	MOUNTMGR_MOUNT_POINTS points;
	PMOUNTMGR_MOUNT_POINTS ppoints = NULL;
	int len;
	NTSTATUS Status;

	if (only_driveletter && only_volume_name)
		return (STATUS_INVALID_PARAMETER);

	ppoints = &points;
	Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_POINTS,
	    &point, sizeof (MOUNTMGR_MOUNT_POINT), ppoints,
	    sizeof (MOUNTMGR_MOUNT_POINTS));

	if (Status == STATUS_BUFFER_OVERFLOW) {
		len = points.Size;
		ppoints = kmem_alloc(len, KM_SLEEP);
		Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_POINTS,
		    &point, sizeof (MOUNTMGR_MOUNT_POINT),
		    ppoints, len);

	}
	dprintf("IOCTL_MOUNTMGR_QUERY_POINTS return %lx - looking for '%wZ'\n",
	    Status, devpath);
	if (Status == STATUS_SUCCESS) {
		for (int Index = 0;
		    Index < ppoints->NumberOfMountPoints;
		    Index++) {
			PMOUNTMGR_MOUNT_POINT ipoint =
			    ppoints->MountPoints + Index;
			PWCHAR DeviceName =
			    (PWCHAR)((PUCHAR)ppoints +
			    ipoint->DeviceNameOffset);
			PWCHAR SymbolicLinkName =
			    (PWCHAR)((PUCHAR)ppoints +
			    ipoint->SymbolicLinkNameOffset);

			// Why is this hackery needed, we should be able
			// to lookup the drive letter from volume name
			dprintf("   point %d: '%.*S' '%.*S'\n", Index,
			    (int)(ipoint->DeviceNameLength / sizeof (WCHAR)),
			    DeviceName,
			    (int)(ipoint->SymbolicLinkNameLength /
			    sizeof (WCHAR)),
			    SymbolicLinkName);
			if (wcsncmp(DeviceName, devpath->Buffer,
			    ipoint->DeviceNameLength / sizeof (WCHAR)) == 0) {

				RtlUnicodeStringCbCopyStringN(savename,
				    SymbolicLinkName,
				    ipoint->SymbolicLinkNameLength);
				// Might as well null terminate.
				savename->Buffer[
				    ipoint->SymbolicLinkNameLength /
				    sizeof (WCHAR)] = 0;

				if (only_driveletter &&
				    !MOUNTMGR_IS_DRIVE_LETTER(savename))
					savename->Length = 0;
				else if (only_volume_name &&
				    !MOUNTMGR_IS_VOLUME_NAME(savename))
					savename->Length = 0;

				if (MOUNTMGR_IS_DRIVE_LETTER(savename) ||
				    MOUNTMGR_IS_VOLUME_NAME(savename))
					break;
			}
		}
	}

	if (ppoints != NULL) kmem_free(ppoints, len);

	return (STATUS_SUCCESS);
}

#define	MOUNTMGR_IS_DOSDEVICES(s, l) ( \
	(l) >= 26 && \
	(s)[0] == '\\' && \
	(s)[1] == 'D' && \
	(s)[2] == 'o' && \
	(s)[3] == 's' && \
	(s)[4] == 'D' && \
	(s)[5] == 'e' && \
	(s)[6] == 'v' && \
	(s)[7] == 'i' && \
	(s)[8] == 'c' && \
	(s)[9] == 'e' && \
	(s)[10] == 's' && \
	(s)[11] == '\\' && \
	(s)[12] >= 'A' && \
	(s)[12] <= 'Z' && \
	(s)[13] == ':')

// Merge mountmgr_get_mountpoint into mountmgr_get_mountpoint2
NTSTATUS
mountmgr_get_mountpoint2(PUNICODE_STRING devpath,
    PUNICODE_STRING symbolicname,
    PUNICODE_STRING mountpoint,
    BOOLEAN stop_when_found)
{
	MOUNTMGR_MOUNT_POINT point = { 0 };
	MOUNTMGR_MOUNT_POINTS points;
	PMOUNTMGR_MOUNT_POINTS ppoints = NULL;
	int len;
	NTSTATUS Status;
	int found = 0;

	ppoints = &points;
	Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_POINTS,
	    &point, sizeof (MOUNTMGR_MOUNT_POINT), ppoints,
	    sizeof (MOUNTMGR_MOUNT_POINTS));

	if (Status == STATUS_BUFFER_OVERFLOW) {
		len = points.Size;
		ppoints = kmem_alloc(len, KM_SLEEP);
		Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_POINTS,
		    &point, sizeof (MOUNTMGR_MOUNT_POINT),
		    ppoints, len);

	}
	dprintf("IOCTL_MOUNTMGR_QUERY_POINTS return %lx - looking for '%wZ'\n",
	    Status, devpath);
	if (Status == STATUS_SUCCESS) {
		for (int Index = 0;
		    Index < ppoints->NumberOfMountPoints;
		    Index++) {
			PMOUNTMGR_MOUNT_POINT ipoint =
			    ppoints->MountPoints + Index;
			PWCHAR DeviceName =
			    (PWCHAR)((PUCHAR)ppoints +
			    ipoint->DeviceNameOffset);
			PWCHAR SymbolicLinkName =
			    (PWCHAR)((PUCHAR)ppoints +
			    ipoint->SymbolicLinkNameOffset);
			// UniqueId is a binary blob.

			// Why is this hackery needed, we should be able
			// to lookup the drive letter from volume name
			dprintf("   point %d: '%.*S' '%.*S'\n", Index,
			    (int)(ipoint->DeviceNameLength / sizeof (WCHAR)),
			    DeviceName,
			    (int)(ipoint->SymbolicLinkNameLength /
			    sizeof (WCHAR)),
			    SymbolicLinkName);

			if (wcsncmp(DeviceName, devpath->Buffer,
			    ipoint->DeviceNameLength / sizeof (WCHAR)) == 0) {

				if (mountpoint != NULL &&
				    MOUNTMGR_IS_DOSDEVICES(SymbolicLinkName,
				    ipoint->SymbolicLinkNameLength)) {
					// Mountpoint
					RtlUnicodeStringCbCopyStringN(
					    mountpoint,
					    SymbolicLinkName,
					    ipoint->SymbolicLinkNameLength);
					// Might as well null terminate.
					mountpoint->Buffer[
					    ipoint->SymbolicLinkNameLength /
					    sizeof (WCHAR)] = 0;
					found++;
				} else if (symbolicname != NULL) {
					// SymbolicLinkName
					RtlUnicodeStringCbCopyStringN(
					    symbolicname,
					    SymbolicLinkName,
					    ipoint->SymbolicLinkNameLength);
					// Might as well null terminate.
					symbolicname->Buffer[
					    ipoint->SymbolicLinkNameLength /
					    sizeof (WCHAR)] = 0;
					found++;
				}

				if (stop_when_found && found == 2)
					break;
			} // DeviceName match
		} // for
	}

	if (ppoints != NULL) kmem_free(ppoints, len);

	return (STATUS_SUCCESS);
}

/*
 * Returns the last valid mountpoint of the device according
 * to MOUNTMGR_IS_DRIVE_LETTER()
 */
NTSTATUS
mountmgr_get_drive_letter(PUNICODE_STRING devpath, PUNICODE_STRING savename)
{
	return (mountmgr_get_mountpoint(devpath, savename,
	    TRUE, FALSE));
}

/*
 * Returns the last valid mountpoint of the device according
 * to MOUNTMGR_IS_VOLUME_NAME()
 */
NTSTATUS
mountmgr_get_volume_name_mountpoint(PUNICODE_STRING devpath,
    PUNICODE_STRING savename)
{
	return (mountmgr_get_mountpoint(devpath, savename,
	    FALSE, TRUE));
}


// Copilot guesses this should be in "\Device\..." syntax
NTSTATUS
SendVolumeRemovalNotification(PUNICODE_STRING DeviceName)
{
	NTSTATUS status;
	PMOUNTMGR_TARGET_NAME targetName;
	ULONG length;

	dprintf("=> SendVolumeRemovalNotification: '%wZ'\n", DeviceName);

	length = sizeof (MOUNTMGR_TARGET_NAME) + DeviceName->Length - 1;
	targetName = ExAllocatePoolWithTag(PagedPool, length, 'ZFSV');

	if (targetName == NULL) {
		dprintf("  can't allocate MOUNTMGR_TARGET_NAME\n");
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	RtlZeroMemory(targetName, length);

	targetName->DeviceNameLength = DeviceName->Length;
	RtlCopyMemory(targetName->DeviceName, DeviceName->Buffer,
	    DeviceName->Length);

#ifndef IOCTL_MOUNTMGR_VOLUME_REMOVAL_NOTIFICATION
#define	IOCTL_MOUNTMGR_VOLUME_REMOVAL_NOTIFICATION  \
	CTL_CODE(MOUNTMGRCONTROLTYPE, 22, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

	status = SendIoctlToMountManager(
	    IOCTL_MOUNTMGR_VOLUME_REMOVAL_NOTIFICATION,
	    targetName, length, NULL, 0);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success\n");
	} else {
		dprintf("  IoCallDriver failed: 0x%lx\n", status);
	}

	ExFreePool(targetName);

	dprintf("<= SendVolumeRemovalNotification\n");

	return (status);
}

//
// Microsoft documentation example shows:
// SymbolicLinkName: "\DosDevices\..."
// DeviceName: "\Device\..."
//
NTSTATUS
SendVolumeCreatePoint(__in PUNICODE_STRING DeviceName,
    __in PUNICODE_STRING MountPoint)
{
	NTSTATUS status;
	PMOUNTMGR_CREATE_POINT_INPUT point;
	ULONG length;

	dprintf("=> SendVolumeCreatePoint\n");

	length = sizeof (MOUNTMGR_CREATE_POINT_INPUT) + MountPoint->Length +
	    DeviceName->Length;
	point = ExAllocatePoolWithTag(PagedPool, length, 'ZFSV');

	if (point == NULL) {
		dprintf("  can't allocate MOUNTMGR_CREATE_POINT_INPUT\n");
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	RtlZeroMemory(point, length);

	dprintf("  DeviceName: %wZ\n", DeviceName);
	point->DeviceNameOffset = sizeof (MOUNTMGR_CREATE_POINT_INPUT);
	point->DeviceNameLength = DeviceName->Length;
	RtlCopyMemory((PCHAR)point + point->DeviceNameOffset,
	    DeviceName->Buffer,
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
		dprintf("  IoCallDriver failed: 0x%lx\n", status);
	}

	ExFreePool(point);

	dprintf("<= SendVolumeCreatePoint\n");

	return (status);
}

/*
 * It is so easy to deadlock MountMgr, it is surprising it has
 * not been fixed. This appears to be what others do,
 * disable automount, tell mountmgr about new mount, then
 * restore automount, if it was on.
 *
 * TargetName: "\Device\..."
 *
 */
static uint64_t mountmgr_automount_count = 0ULL;
static MOUNTMGR_QUERY_AUTO_MOUNT QueryAutoMount;
static ULONG QueryAutoMountSize = sizeof (MOUNTMGR_QUERY_AUTO_MOUNT);

NTSTATUS
NotifyVolumeArrival(PUNICODE_STRING unicodeSourceVolumeName)
{
	NTSTATUS status;
	MOUNTMGR_SET_AUTO_MOUNT SetAutoMount;

	// Check if automount is on
	// Disable AutoMount
	if (atomic_add_64_nv(&mountmgr_automount_count, 1) == 1) {

		SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT,
		    NULL, 0, &QueryAutoMount, QueryAutoMountSize);

		dprintf("%s: AutoMount is %s - disabling\n", __func__,
		    QueryAutoMount.CurrentState == 0 ?
		    "Disabled" : "Enabled");

		SetAutoMount.NewState = 0; // Disabled
		SendIoctlToMountManager(IOCTL_MOUNTMGR_SET_AUTO_MOUNT,
		    &SetAutoMount, sizeof (SetAutoMount), NULL, 0);

	}


	// Now tell MountMgr about the new mount

	PMOUNTMGR_TARGET_NAME target;
	ULONG size;
	dprintf("=> NotifyVolumeArrival\n");

	size = sizeof (MOUNTMGR_TARGET_NAME) +
	    unicodeSourceVolumeName->Length; // one wchar in struct

	target = ExAllocatePoolWithTag(PagedPool, size, 'ZFSV');
	if (target == NULL) {
		dprintf("  can't allocate\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	RtlZeroMemory(target, size);

	target->DeviceNameLength = unicodeSourceVolumeName->Length;
	RtlCopyMemory(target->DeviceName, unicodeSourceVolumeName->Buffer,
	    unicodeSourceVolumeName->Length);

	status = SendIoctlToMountManager(
	    IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION,
	    target, size, NULL, 0);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success\n");
	} else {
		dprintf("  IoCallDriver failed: 0x%lx\n", status);
	}

	ExFreePool(target);

out:
	// Restore AutoMount
	if (atomic_sub_64_nv(&mountmgr_automount_count, 1) == 0) {
		dprintf("Restoring automount\n");
		SetAutoMount.NewState = QueryAutoMount.CurrentState;
		SendIoctlToMountManager(IOCTL_MOUNTMGR_SET_AUTO_MOUNT,
		    &SetAutoMount, sizeof (SetAutoMount), NULL, 0);
	}

	dprintf("<= NotifyVolumeArrival\n");
	return (status);
}

/*
 * IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED can
 * not be called while MountMgr holds RemoteDatabaseSemaphore
 * ie, in IRP_MN_MOUNT_VOLUME context.
 *
 * SourceVolumeName: "\DosDevices\D:\..."
 * TargetVolumeName: "\??\Volume{xxxxxx-xxxx-xxxx-xxxx-xxxxxxx}"
 *
 */
NTSTATUS
NotifyMountMgr(
    PUNICODE_STRING unicodeSourceVolumeName,
    PUNICODE_STRING unicodeTargetVolumeName,
    boolean_t IsPointCreated)
{
	// unicodeSourceVolumeName "\??\E:\ntfs" - not this one
	// unicodeSourceVolumeName "\DosDevices\E:\ntfs" - this one
	// unicodeTargetVolumeName "\??\Volume{xxxxxx-xxxx-xxxx-xxxx-xxxxxxx}"
	NTSTATUS status;
	PMOUNTMGR_VOLUME_MOUNT_POINT input;
	ULONG inputSize;

	dprintf("=> NotifyMountMgr: \n");

	inputSize = sizeof (MOUNTMGR_VOLUME_MOUNT_POINT) +
	    unicodeSourceVolumeName->Length +
	    unicodeTargetVolumeName->Length;

	input = ExAllocatePoolWithTag(PagedPool, inputSize, 'ZFSV');

	if (input == NULL) {
		dprintf("  can't allocate MOUNTMGR_VOLUME_MOUNT_POINT\n");
		return (STATUS_INSUFFICIENT_RESOURCES);
	}
	RtlZeroMemory(input, inputSize);

	input->SourceVolumeNameOffset = sizeof (MOUNTMGR_VOLUME_MOUNT_POINT);
	input->SourceVolumeNameLength = unicodeSourceVolumeName->Length;
	input->TargetVolumeNameOffset = input->SourceVolumeNameOffset +
	    input->SourceVolumeNameLength;
	input->TargetVolumeNameLength = unicodeTargetVolumeName->Length;

	RtlCopyMemory((PCHAR) input + input->SourceVolumeNameOffset,
	    unicodeSourceVolumeName->Buffer,
	    input->SourceVolumeNameLength);

	RtlCopyMemory((PCHAR) input + input->TargetVolumeNameOffset,
	    unicodeTargetVolumeName->Buffer,
	    input->TargetVolumeNameLength);

	// ((PWSTR) ((PCHAR) input + input->TargetVolumeNameOffset))[1] = '?';

	dprintf("  SourceVolumeName: %wZ\n", unicodeSourceVolumeName);
	dprintf("  TargetVolumeName: %wZ\n", unicodeTargetVolumeName);

	status = SendIoctlToMountManager(
	    IsPointCreated ? IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED :
	    IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_DELETED,
	    input, inputSize, NULL, 0);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success\n");
	} else {
		dprintf("  IoCallDriver failed: 0x%lx\n", status);
	}

	ExFreePool(input);

	dprintf("<= NotifyMountMgr\n");

	return (status);
}

//
// SymbolicLinkName": "\DosDevices\X:"
//
NTSTATUS
SendVolumeDeletePoints(
    __in PUNICODE_STRING MountPoint,
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
		return (STATUS_SUCCESS);
	}

	length = sizeof (MOUNTMGR_MOUNT_POINT) + MountPoint->Length;
	if (DeviceName != NULL) {
		length += DeviceName->Length;
	}
	point = kmem_alloc(length, KM_SLEEP);

	if (point == NULL) {
		dprintf("  can't allocate MOUNTMGR_CREATE_POINT_INPUT\n");
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	olength = sizeof (MOUNTMGR_MOUNT_POINTS) + 1024; // length
	deletedPoints = kmem_alloc(olength, KM_SLEEP);
	if (deletedPoints == NULL) {
		dprintf("  can't allocate PMOUNTMGR_MOUNT_POINTS\n");
		kmem_free(point, length);
		return (STATUS_INSUFFICIENT_RESOURCES);
	}

	RtlZeroMemory(point, length); // kmem_zalloc
	RtlZeroMemory(deletedPoints, olength);

	dprintf("  MountPoint: %wZ\n", MountPoint);
	point->SymbolicLinkNameOffset = sizeof (MOUNTMGR_MOUNT_POINT);
	point->SymbolicLinkNameLength = MountPoint->Length;
	RtlCopyMemory((PCHAR)point + point->SymbolicLinkNameOffset,
	    MountPoint->Buffer, MountPoint->Length);
	if (DeviceName != NULL) {
		dprintf("  DeviceName: %wZ\n", DeviceName);
		point->DeviceNameOffset =
		    point->SymbolicLinkNameOffset +
		    point->SymbolicLinkNameLength;
		point->DeviceNameLength = DeviceName->Length;
		RtlCopyMemory((PCHAR)point + point->DeviceNameOffset,
		    DeviceName->Buffer, DeviceName->Length);
	}

	// Only symbolic link can be deleted with IOCTL_MOUNTMGR_DELETE_POINTS.
	// If any other entry is specified, the mount manager will ignore
	// subsequent IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION for the
	// same volume ID.
	status = SendIoctlToMountManager(IOCTL_MOUNTMGR_DELETE_POINTS,
	    point, length, deletedPoints, olength);

	if (NT_SUCCESS(status)) {
		dprintf("  IoCallDriver success, %ld mount points deleted.\n",
		    deletedPoints->NumberOfMountPoints);
	} else {
		dprintf("  IoCallDriver failed: 0x%lx\n", status);
	}

	kmem_free(point, length);
	kmem_free(deletedPoints, olength);

	dprintf("<= SendVolumeDeletePoints\n");

	return (status);
}

void
zfs_release_mount(mount_t *zmo)
{
	dprintf("Releasing mount %p\n", zmo);
	if (zmo->ascii_name) {
		kmem_strfree(zmo->ascii_name);
		zmo->ascii_name = NULL;
	}
	FreeUnicodeString(&zmo->name);
	FreeUnicodeString(&zmo->arc_name);
	FreeUnicodeString(&zmo->symlink_name);
	FreeUnicodeString(&zmo->device_name);
	FreeUnicodeString(&zmo->fs_name);
	FreeUnicodeString(&zmo->uuid);
	FreeUnicodeString(&zmo->mountpoint);
	FreeUnicodeString(&zmo->dosdevices_mountpoint);
	FreeUnicodeString(&zmo->deviceInterfaceName);
	FreeUnicodeString(&zmo->fsInterfaceName);
	FreeUnicodeString(&zmo->volumeInterfaceName);
	FreeUnicodeString(&zmo->MountMgr_name);
	FreeUnicodeString(&zmo->MountMgr_mountpoint);
	vfs_set_mountedon(zmo, NULL);
	if (zmo->vpb) {
		zmo->vpb->DeviceObject = NULL;
		zmo->vpb->RealDevice = NULL;
		zmo->vpb->Flags = 0;
	}
}

void
InitVpb(__in PVPB Vpb, __in PDEVICE_OBJECT VolumeDevice, mount_t *zmo)
{
	if (Vpb != NULL) {
		Vpb->DeviceObject = VolumeDevice;
#if 0
		Vpb->VolumeLabelLength =
		    MIN(sizeof (VOLUME_LABEL) - sizeof (WCHAR),
		    sizeof (Vpb->VolumeLabel));
		RtlCopyMemory(Vpb->VolumeLabel, VOLUME_LABEL,
		    Vpb->VolumeLabelLength);
#else
		Vpb->VolumeLabelLength =
		    MIN(zmo->name.Length,
		    sizeof (Vpb->VolumeLabel));
		RtlCopyMemory(Vpb->VolumeLabel, zmo->name.Buffer,
		    Vpb->VolumeLabelLength);
#endif
		Vpb->SerialNumber = 0x19831116;
		Vpb->Flags |= VPB_MOUNTED;
	}
}

typedef struct _REPARSE_DELETE_HDR {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
} REPARSE_DELETE_HDR;

//
// PrintName:
// SubstituteName:
//
NTSTATUS
CreateReparsePoint(POBJECT_ATTRIBUTES poa, PCUNICODE_STRING SubstituteName,
    PCUNICODE_STRING PrintName)
{
	HANDLE hFile;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	FILE_DISPOSITION_INFORMATION fdi = { .DeleteFile = TRUE };
	REPARSE_DELETE_HDR hdr = { IO_REPARSE_TAG_MOUNT_POINT, 0, 0 };

	dprintf("%s: \n", __func__);
	xprintf("%s: \n", __func__);

	poa->Attributes |= OBJ_DONT_REPARSE;

	status = ZwCreateFile(&hFile,
	    DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
	    poa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	    FILE_OPEN,
	    FILE_DIRECTORY_FILE |
	    FILE_OPEN_REPARSE_POINT |
	    FILE_OPEN_FOR_BACKUP_INTENT |
	    FILE_SYNCHRONOUS_IO_NONALERT,
	    NULL, 0);

	poa->Attributes &= ~OBJ_DONT_REPARSE;

	if (!NT_SUCCESS(status)) {
		dprintf("pre-rmdir failed 0x%lx - means it wasn't there\n",
		    status);
	} else {
		status = ZwFsControlFile(hFile, NULL, NULL, NULL, &iosb,
		    FSCTL_DELETE_REPARSE_POINT,
		    &hdr, sizeof (hdr), NULL, 0);
		dprintf("Removing REPARSE_POINT status 0x%lx (failure is OK)\n",
		    status);

		status = ZwSetInformationFile(hFile, &iosb, &fdi, sizeof (fdi),
		    FileDispositionInformation);
		dprintf("Setting delete-file status 0x%lx (failure not ok)\n",
		    status);

		ZwClose(hFile);
	}

	// Now create the reparsepoint to mount on

	status = ZwCreateFile(&hFile, FILE_ALL_ACCESS, poa, &iosb, 0, 0, 0,
	    FILE_CREATE, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
	    0, 0);
	dprintf("ZwCreateFile 0x%lx\n", status);

	if (status == STATUS_OBJECT_NAME_COLLISION) {
		/*
		 * The directory already exists — typically a ctldir virtual
		 * node (e.g. .zfs/snapshot/<name>) that can't be deleted and
		 * recreated.  Open it in-place and set the reparse point on
		 * the existing node so MountManager learns the mount path.
		 */
		status = ZwCreateFile(&hFile, FILE_ALL_ACCESS, poa, &iosb,
		    NULL, 0,
		    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		    FILE_OPEN,
		    FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
		    FILE_SYNCHRONOUS_IO_NONALERT,
		    NULL, 0);
		dprintf("ZwCreateFile fallback open 0x%lx\n", status);
	}

	if (!NT_SUCCESS(status))
		return (status);

	// SubstituteName must be first, offset = 0
	// Both names must be NULL terminated
	// length must be offset/length fields + buffer + 2 null chars
	USHORT cb = 2 * sizeof (WCHAR) +
	    FIELD_OFFSET(REPARSE_DATA_BUFFER,
	    MountPointReparseBuffer.PathBuffer) +
	    SubstituteName->Length + PrintName->Length;
	PREPARSE_DATA_BUFFER prdb =
	    (PREPARSE_DATA_BUFFER) ExAllocatePoolWithTag(PagedPool, cb, 'ZFSM');
	RtlZeroMemory(prdb, cb);
	prdb->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	prdb->ReparseDataLength = cb - REPARSE_DATA_BUFFER_HEADER_SIZE;
	prdb->MountPointReparseBuffer.SubstituteNameLength =
	    SubstituteName->Length;
	prdb->MountPointReparseBuffer.PrintNameLength = PrintName->Length;
	prdb->MountPointReparseBuffer.PrintNameOffset =
	    SubstituteName->Length + sizeof (WCHAR);
	memcpy(prdb->MountPointReparseBuffer.PathBuffer,
	    SubstituteName->Buffer, SubstituteName->Length);
	memcpy(RtlOffsetToPointer(prdb->MountPointReparseBuffer.PathBuffer,
	    SubstituteName->Length + sizeof (WCHAR)),
	    PrintName->Buffer, PrintName->Length);

	status = ZwFsControlFile(hFile, 0, 0, 0, &iosb,
	    FSCTL_SET_REPARSE_POINT, prdb, cb, 0, 0);
	dprintf("%s: ControlFile %ld / 0x%lx\n", __func__, status, status);

	if (!NT_SUCCESS(status)) {
		ZwSetInformationFile(hFile, &iosb, &fdi,
		    sizeof (fdi), FileDispositionInformation);
	}
	ZwClose(hFile);
	xprintf("-%s: \n", __func__);
	return (status);
}

void
zfs_release_userland(mount_t *dcb)
{
	DECLARE_UNICODE_STRING_SIZE(eventName,
	    PATH_MAX);
	HANDLE hEvent = NULL;
	OBJECT_ATTRIBUTES objAttributes;

	RtlUnicodeStringPrintf(&eventName,
	    L"\\BaseNamedObjects\\MountComplete_{%wZ}",
	    &dcb->name);

	InitializeObjectAttributes(&objAttributes,
	    &eventName, OBJ_KERNEL_HANDLE, NULL, NULL);

	ZwOpenEvent(&hEvent, EVENT_ALL_ACCESS, &objAttributes);
	xprintf("Open event %wZ said %p\n", &eventName, hEvent);

	if (hEvent) {
		xprintf("Sending event to userland\n");
		dprintf("Sending event to userland\n");
		ZwSetEvent(hEvent, NULL);
		ZwClose(hEvent);
	}
}

/*
 * IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED can
 * not be called while MountMgr holds RemoteDatabaseSemaphore
 * ie, in IRP_MN_MOUNT_VOLUME context. Inside NotifyMountMgr()
 */
static void
NotifyMountMgr_impl(void *arg)
{
	mount_t *vcb = (mount_t *)arg;
	mount_t *dcb = vcb->parent_device;
	NTSTATUS status;
	OBJECT_ATTRIBUTES poa;
	// 36(uuid) + 6 (punct) + 6 (Volume)
	DECLARE_UNICODE_STRING_SIZE(volStr,
	    ZFS_MAX_DATASET_NAME_LEN);
	/* Volume junctions always use an empty PrintName. */
	DECLARE_UNICODE_STRING_SIZE(emptyStr, 1);
	emptyStr.Length = 0;
	xprintf("%s: start\n", __func__);

	/* Reparse points need trailing backslash on the substitute name. */
	RtlUnicodeStringPrintf(&volStr,
	    L"%wZ\\",
	    &dcb->MountMgr_name);

	dprintf("Creating reparse mountpoint on '%wZ' for "
	    "volume '%wZ'\n",
	    &dcb->mountpoint, &volStr);

	InitializeObjectAttributes(&poa,
	    &dcb->mountpoint, OBJ_KERNEL_HANDLE, NULL, NULL);

	/*
	 * Temporarily remove from mount list so the reparse-point
	 * delete/create path does not hit the has_mountpoint() guard.
	 */
	boolean_t is = vfs_mount_member(dcb);
	if (is)
		vfs_mount_remove(dcb);
	status = CreateReparsePoint(&poa, &volStr,
	    &emptyStr);
	if (is)
		vfs_mount_add(dcb);

	if (!NT_SUCCESS(status))
		dprintf("CreateReparsePoint failed %lx\n", status);

	// We send "\DosDevices\E:\reparsepoint"
	// and MountMgr symlink "\??\Volume{xxxxxx-xxxx-xxxx-xxxx-xxxxxxx}\"

	status = NotifyMountMgr(&dcb->dosdevices_mountpoint,
	    &dcb->MountMgr_name, B_TRUE);
	if (!NT_SUCCESS(status))
		dprintf("NotifyMountMgr failed %lx\n", status);

	if (dcb->MountMgr_mountpoint.Length > 1) {
		SendVolumeDeletePoints(&dcb->MountMgr_mountpoint,
		    &dcb->device_name);
		FreeUnicodeString(&dcb->MountMgr_mountpoint);
	}

	status = NotifyVolumeArrival(&dcb->MountMgr_name);
	if (!NT_SUCCESS(status))
		dprintf("NotifyVolumeArrival failed %lx\n", status);

	if (vcb->root_file)
		status = FsRtlNotifyVolumeEvent(vcb->root_file,
		    FSRTL_VOLUME_MOUNT);

	zfs_release_userland(dcb);

	xprintf("%s: stop\n", __func__);
}

NTSTATUS
DeleteReparsePoint(POBJECT_ATTRIBUTES poa)
{
	HANDLE hFile;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	REPARSE_DATA_BUFFER ReparseData;

	dprintf("%s: \n", __func__);

	status = ZwCreateFile(&hFile, FILE_ALL_ACCESS, poa, &iosb, 0, 0, 0,
	    FILE_OPEN_IF, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
	    FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT,
	    0, 0);
	if (0 > status)
		return (status);
	dprintf("%s: create ok\n", __func__);

	memset(&ReparseData, 0, REPARSE_DATA_BUFFER_HEADER_SIZE);
	ReparseData.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;

	status = ZwFsControlFile(hFile, 0, 0, 0, &iosb,
	    FSCTL_DELETE_REPARSE_POINT, &ReparseData,
	    REPARSE_DATA_BUFFER_HEADER_SIZE, NULL, 0);

	ZwClose(hFile);
	return (status);
}

/*
 * go through all mointpoints (IOCTL_MOUNTMGR_QUERY_POINTS)
 * and check if our driveletter is in the list
 * return 1 if yes, otherwise 0
 */
NTSTATUS
mountmgr_is_driveletter_assigned(wchar_t driveletter, BOOLEAN *ret)
{
	MOUNTMGR_MOUNT_POINT point = { 0 };
	MOUNTMGR_MOUNT_POINTS points;
	PMOUNTMGR_MOUNT_POINTS ppoints = NULL;
	int len;
	*ret = 0;
	NTSTATUS Status;

	ppoints = &points;
	Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_POINTS, &point,
	    sizeof (MOUNTMGR_MOUNT_POINT), ppoints,
	    sizeof (MOUNTMGR_MOUNT_POINTS));

	if (Status == STATUS_BUFFER_OVERFLOW) {
		len = points.Size;
		ppoints = kmem_alloc(len, KM_SLEEP);
		Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_QUERY_POINTS,
		    &point, sizeof (MOUNTMGR_MOUNT_POINT), ppoints,
		    len);
	}
	dprintf("IOCTL_MOUNTMGR_QUERY_POINTS return %lx - "
	    "looking for driveletter '%c'\n",
	    Status, driveletter);
	if (Status == STATUS_SUCCESS) {
		char mpt_name[PATH_MAX] = { 0 };
		for (int Index = 0;
		    Index < ppoints->NumberOfMountPoints;
		    Index++) {
			PMOUNTMGR_MOUNT_POINT ipoint =
			    ppoints->MountPoints + Index;
			PWCHAR DeviceName =
			    (PWCHAR)((PUCHAR)ppoints +
			    ipoint->DeviceNameOffset);
			PWCHAR SymbolicLinkName =
			    (PWCHAR)((PUCHAR)ppoints +
			    ipoint->SymbolicLinkNameOffset);

			dprintf("   point %d: '%.*S' '%.*S'\n", Index,
			    (int)(ipoint->DeviceNameLength / sizeof (WCHAR)),
			    DeviceName,
			    (int)(ipoint->SymbolicLinkNameLength /
			    sizeof (WCHAR)),
			    SymbolicLinkName);

			ULONG length = 0;
			RtlUnicodeToUTF8N(mpt_name, MAXPATHLEN - 1, &length,
			    SymbolicLinkName,
			    ipoint->SymbolicLinkNameLength);
			mpt_name[length] = 0;
			char c_driveletter;
			wctomb(&c_driveletter, driveletter);
			if (MOUNTMGR_IS_DRIVE_LETTER_A(mpt_name) &&
			    mpt_name[12] == c_driveletter) {
				*ret = 1;
				if (ppoints != NULL) kmem_free(ppoints, len);
				return (STATUS_SUCCESS);
			}
		}
	}

	if (ppoints != NULL) kmem_free(ppoints, len);
	return (Status);
}

void
generateGUID(char *pguid)
{
	char *uuid_format = "xxxxxxxx-xxxx-4xxx-Nxxx-xxxxxxxxxxxx";
	char *szHex = "0123456789ABCDEF-";
	int len = strlen(uuid_format);

	for (int i = 0; i < len + 1; i++) {
		int r = rand() % 16;
		char c = ' ';

		switch (uuid_format[i])	{
		case 'x': { c = szHex[r]; }
			break;
		case 'N': { c = szHex[(r & 0x03) | 0x08]; }
			break;
		case '-': { c = '-'; }
			break;
		case '4': { c = '4'; }
			break;
		}

		pguid[i] = (i < len) ? c : 0x00;
	}
}

void
generateVolumeNameMountpoint(wchar_t *vol_mpt)
{
	char GUID[50];
	wchar_t wc_guid[50];
	generateGUID((char *)&GUID);
	mbstowcs((wchar_t *)&wc_guid, GUID, 50);
	_snwprintf(vol_mpt, 50, L"\\??\\Volume{%s}\\", wc_guid);
}

/*
 * assign driveletter with IOCTL_MOUNTMGR_CREATE_POINT
 */
NTSTATUS
mountmgr_assign_driveletter(PUNICODE_STRING device_name,
    wchar_t driveletter)
{
	DECLARE_UNICODE_STRING_SIZE(mpt, 16);
	RtlUnicodeStringPrintf(&mpt, L"\\DosDevices\\%c:",
	    toupper(driveletter));
	return (SendVolumeCreatePoint(device_name, &mpt));
}

/*
 * assign next free driveletter (D..Z) if mountmgr is offended
 * and refuses to do it
 */
NTSTATUS
SetNextDriveletterManually(PUNICODE_STRING device_name)
{
	NTSTATUS status;
	for (wchar_t c = 'D'; c <= 'Z'; c++) {
		BOOLEAN ret;
		status = mountmgr_is_driveletter_assigned(c, &ret);
		if (status == STATUS_SUCCESS && ret == 0) {
			status = mountmgr_assign_driveletter(device_name, c);

			if (status == STATUS_SUCCESS) {
				// prove it
				status =
				    mountmgr_is_driveletter_assigned(c, &ret);
				if (status == STATUS_SUCCESS) {
					if (ret == 1)
						return (STATUS_SUCCESS);
					else
						return
						    (STATUS_VOLUME_DISMOUNTED);
				} else {
					return (status);
				}
			}
		}
	}
	return (status);
}

//
// SymbolicLinkName": "\DosDevices\X:"
//
int
zfs_remove_driveletter(mount_t *zmo)
{
	NTSTATUS Status;

	dprintf("%s: removing driveletter for '%wZ'\n", __func__, &zmo->name);

	MOUNTMGR_MOUNT_POINT *mmp = NULL;
	ULONG mmpsize;
	MOUNTMGR_MOUNT_POINTS mmps1, *mmps2 = NULL;

	mmpsize = sizeof (MOUNTMGR_MOUNT_POINT) + zmo->device_name.Length;

	mmp = kmem_zalloc(mmpsize, KM_SLEEP);

	mmp->DeviceNameOffset = sizeof (MOUNTMGR_MOUNT_POINT);
	mmp->DeviceNameLength = zmo->device_name.Length;
	RtlCopyMemory(&mmp[1], zmo->device_name.Buffer,
	    zmo->device_name.Length);

	Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_DELETE_POINTS,
	    mmp, mmpsize, &mmps1, sizeof (MOUNTMGR_MOUNT_POINTS));

	if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) {
		goto out;
	}

	if (Status != STATUS_BUFFER_OVERFLOW || mmps1.Size == 0) {
		Status = STATUS_NOT_FOUND;
		goto out;
	}

	mmps2 = kmem_zalloc(mmps1.Size, KM_SLEEP);

	Status = SendIoctlToMountManager(IOCTL_MOUNTMGR_DELETE_POINTS,
	    mmp, mmpsize, mmps2, mmps1.Size);

out:
	dprintf("%s: removing driveletter returns 0x%lx\n", __func__, Status);

	if (mmps2)
		kmem_free(mmps2, mmps1.Size);
	if (mmp)
		kmem_free(mmp, mmpsize);
	return (Status);
}

int
zfs_windows_mount_impl(const char *name, char *value, size_t valuelen,
    uint64_t mountflags)
{
	dprintf("%s: '%s' '%s'\n", __func__, name, value);
	NTSTATUS status;
	uuid_t uuid;
	char uuid_a[UUID_PRINTABLE_STRING_LENGTH];
	// PDEVICE_OBJECT pdo = NULL;
	PDEVICE_OBJECT diskDeviceObject = NULL;
	// PDEVICE_OBJECT fsDeviceObject = NULL;
	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	/*
	 * We expect mountpath (zv_value) to be already sanitised, ie, Windows
	 * translated paths. So it should be on this style:
	 * "\\??\\c:"  mount as drive letter C:
	 * "\\??\\?:"  mount as first available drive letter
	 * "\\??\\c:\\BOOM"  mount as drive letter C:\BOOM
	 */
	int mplen = strlen(value);
	if ((mplen < 6) ||
	    strncmp("\\??\\", value, 4)) {
		dprintf("%s: mountpoint '%s' does not start with \\??\\x:",
		    __func__, value);
		return (EINVAL);
	}

	zfs_vfs_uuid_gen(name, uuid);
	zfs_vfs_uuid_unparse(uuid, uuid_a);

	char buf[PATH_MAX];
	// snprintf(buf, sizeof (buf), "\\Device\\ZFS{%s}", uuid_a);
	// L"\\Device\\Volume"
	// WCHAR diskDeviceNameBuf[MAXIMUM_FILENAME_LENGTH];
	// WCHAR fsDeviceNameBuf[MAXIMUM_FILENAME_LENGTH]; // L"\\Device\\ZFS"
	// L"\\DosDevices\\Global\\Volume"
	// WCHAR symbolicLinkNameBuf[MAXIMUM_FILENAME_LENGTH];
	UNICODE_STRING diskDeviceName;
	UNICODE_STRING fsDeviceName;
	UNICODE_STRING symbolicLinkTarget;
	UNICODE_STRING arcLinkTarget;

	ANSI_STRING pants;
	ULONG deviceCharacteristics;
	deviceCharacteristics = 0; // FILE_DEVICE_IS_MOUNTED;
	/* Allow $recycle.bin - don't set removable. */
	// if (!zfs_disable_removablemedia)
	//	deviceCharacteristics |= FILE_REMOVABLE_MEDIA;

	// snprintf(buf, sizeof (buf), "\\Device\\Volume{%s}", uuid_a);


	// Big retry loop, due to flakey mountmgr
	int retry = 0;
	mount_t *zmo_dcb = NULL;

	do {
		xprintf("Mount attempt %d\n", retry);
		dprintf("Mount attempt %d\n", retry);

		snprintf(buf, sizeof (buf), "\\Device\\zfs-%s", uuid_a);

		pants.Buffer = buf;
		pants.Length = strlen(buf);
		pants.MaximumLength = PATH_MAX;
		status = RtlAnsiStringToUnicodeString(&diskDeviceName, &pants,
		    TRUE);
		dprintf("%s: new devstring '%wZ'\n", __func__, &diskDeviceName);

		// Autogen gives a name like \Device\00000a9
		// We can use FILE_DEVICE_DISK and get popups like
		// "Decide what to do with removable device E:"
		// or FILE_DEVICE_VIRTUAL_DISK to skip popup.
		status = IoCreateDeviceSecure(WIN_DriverObject,
		    sizeof (mount_t),
		    &diskDeviceName,
		    FILE_DEVICE_DISK,
		    deviceCharacteristics | FILE_DEVICE_SECURE_OPEN,
		    FALSE,
		    &sddlVolume,
		    NULL,
		    &diskDeviceObject);

		if (status != STATUS_SUCCESS) {
			dprintf("IoCreateDeviceSecure returned %08lx\n",
			    status);
			if (status == STATUS_OBJECT_NAME_COLLISION) {
				/*
				 * Device already exists:
				 * snapshot already mounted.
				 */
				dprintf("%s: '%s' already mounted\n",
				    __func__, name);
				snprintf(value, valuelen,
				    "\\DosDevices\\Global\\Volume{%s}", uuid_a);
				return (0);
			}
			return (status);
		}

		zmo_dcb = diskDeviceObject->DeviceExtension;

		zmo_dcb->type = MOUNT_TYPE_DCB;
		zmo_dcb->size = sizeof (mount_t);

		// Wait until AddDevice is called.
		KeInitializeEvent((PRKEVENT)&zmo_dcb->volume_adddevice_event,
		    SynchronizationEvent, FALSE);

		// Get ready to wait for the volume mounted notification
		KeInitializeEvent((PRKEVENT)&zmo_dcb->volume_mounted_event,
		    SynchronizationEvent, FALSE);

		zfs_vfs_uuid_gen(name, zmo_dcb->rawuuid);

		vfs_setfsprivate(zmo_dcb, NULL);
		dprintf("%s: created dcb at %p asked for size %llu\n",
		    __func__, zmo_dcb, sizeof (mount_t));
		AsciiStringToUnicodeString(uuid_a, &zmo_dcb->uuid);
		// Should we keep the name with slashes like "BOOM/lower" or
		// just "lower". Turns out the name in Explorer only
		// works for 4 chars or lower. Why?
		AsciiStringToUnicodeStringNP(name, &zmo_dcb->name);
		RtlDuplicateUnicodeString(0, &diskDeviceName,
		    &zmo_dcb->device_name);
		zmo_dcb->ascii_name = kmem_strdup(name);

		// strlcpy(zc->zc_value, buf, sizeof (zc->zc_value));
		zmo_dcb->FunctionalDeviceObject = diskDeviceObject;

		dprintf("New device %p has extension %p\n",
		    diskDeviceObject, zmo_dcb);

		/*
		 * We do not attach the diskDeviceObject here, or all the
		 * mounts will be under one PDO (bus) - this confuses Windows
		 * which sends all lookups to the last mount. Instead, we keep
		 * them separate so they become their own PDOs. They are
		 * returned in BusRelations as expected.
		 */
		// zmo_dcb->AttachedDevice = IoAttachDeviceToDeviceStack(
		//    diskDeviceObject, DriverExtension->PhysicalDeviceObject);

		snprintf(buf, sizeof (buf), "\\DosDevices\\Global\\Volume{%s}",
		    uuid_a);
		pants.Buffer = buf;
		pants.Length = strlen(buf);
		pants.MaximumLength = PATH_MAX;
		status = RtlAnsiStringToUnicodeString(&symbolicLinkTarget,
		    &pants, TRUE);
		dprintf("%s: new symlink '%wZ'\n", __func__,
		    &symbolicLinkTarget);
		AsciiStringToUnicodeString(buf, &zmo_dcb->symlink_name);

		snprintf(buf, sizeof (buf), "\\ArcName\\OpenZFS(%s)", uuid_a);
		pants.Buffer = buf;
		pants.Length = strlen(buf);
		pants.MaximumLength = PATH_MAX;
		status = RtlAnsiStringToUnicodeString(&arcLinkTarget, &pants,
		    TRUE);
		dprintf("%s: new symlink '%wZ'\n", __func__, &arcLinkTarget);
		AsciiStringToUnicodeString(buf, &zmo_dcb->arc_name);

		snprintf(buf, sizeof (buf), "\\Device\\ZFS{%s}", uuid_a);
		pants.Buffer = buf;
		pants.Length = strlen(buf);
		pants.MaximumLength = PATH_MAX;
		status = RtlAnsiStringToUnicodeString(&fsDeviceName, &pants,
		    TRUE);
		dprintf("%s: new fsname '%wZ'\n", __func__, &fsDeviceName);
		AsciiStringToUnicodeString(buf, &zmo_dcb->fs_name);

		diskDeviceObject->Flags |= DO_DIRECT_IO;
		diskDeviceObject->Flags |= DO_BUS_ENUMERATED_DEVICE;
		// diskDeviceObject->Flags |= DO_POWER_PAGABLE;
		diskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

		status = IoCreateSymbolicLink(&zmo_dcb->symlink_name,
		    &zmo_dcb->device_name);
		dprintf("%s: IoCreateSymbolicLink %lu\n", __func__,
		    status);
		status = IoCreateSymbolicLink(&zmo_dcb->arc_name,
		    &zmo_dcb->device_name);
		dprintf("%s: IoCreateSymbolicLink %lu\n", __func__,
		    status);

		// zmo->VolumeDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

		if (mountflags & MNT_RDONLY)
			vfs_setrdonly(zmo_dcb);
	// mount was here
		// Check if we are to mount with driveletter, or path
		// We already check that path is "\\??\\" above, and
		// at least 6 chars. Seventh char can be zero, or "/"
		// then zero, for drive only mount.
		if ((value[6] == 0) ||
		    ((value[6] == '/') &&
		    (value[7] == 0))) {
			zmo_dcb->justDriveLetter = B_TRUE;
		} else {
			zmo_dcb->justDriveLetter = B_FALSE;
		}

		// mountpoint is "\??\D:\path"
		AsciiStringToUnicodeString(value, &zmo_dcb->mountpoint);

		// dosdevices_mountpoint is "\DosDevices\D:\path"
		snprintf(buf, sizeof (buf), "\\DosDevices\\%s",
		    &value[4]);
		AsciiStringToUnicodeString(buf,
		    &zmo_dcb->dosdevices_mountpoint);

		dprintf("%s: driveletter %d '%wZ'\n",
		    __func__, zmo_dcb->justDriveLetter, &zmo_dcb->mountpoint);

		// Mark devices as initialized
		// diskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
		// ObReferenceObject(diskDeviceObject);

		/*
		 * IoVerifyVolume() kicks off quite a large amount of work
		 * which can grow the stack very large. Usually it goes
		 * something like:
		 * IoVerifyVolume()
		 * NtIoCallDriver()
		 * zfs_vnop_mount()
		 * CreateReparsePoint()
		 * NtIoCallDriver()
		 * zfs_vnop_lookup()
		 * zfs_mkdir()
		 * dnode_allocate()
		 * dbuf_dirty()
		 * so any cal to kmem_alloc() which might trigger a
		 * magazine alloc would tip us over.
		 */

		// Add to list for BusRelations
		dprintf("%s: Adding zmo %p with FDO %p on list.\n",
		    __func__, zmo_dcb, zmo_dcb->FunctionalDeviceObject);

		// Add to the list of mounts, for BusRelations query
		vfs_mount_add(zmo_dcb);

		// Set the mountpoint, might be "/" for driveletters or
		// a subdirectory for lower mounts.
		char *rootpath = &value[6]; // Above checks for \\??\\x:
		while (rootpath[0] == '\\' &&
		    rootpath[1] == '\\')
			rootpath++;
		vfs_set_mountedon(zmo_dcb, rootpath);

		// Ping the bus, Windows will now query BusRelations to
		// get list of mounts.
		IoInvalidateDeviceRelations(
		    DriverExtension->PhysicalDeviceObject, BusRelations);

		// Free diskDeviceName
		FreeUnicodeString(&diskDeviceName);

		/*
		 * Here the rest of the mount will happen async, but
		 * if we return now, userland will carry on and mount the
		 * next thing, which might live in this mount, so we
		 * wait for the event to signal completion, before returning.
		 * Unix call mount() will only return once it is fully mounted.
		 */
		dprintf("Waiting on AddDevice\n");
		LARGE_INTEGER timeout;
		timeout.QuadPart = -10000000 * 4; // 10s
		status = KeWaitForSingleObject(&zmo_dcb->volume_adddevice_event,
		    Executive, KernelMode, TRUE, &timeout);

		dprintf("AddDevice wait said %ld.\n", status);

		// If mount went well, we can exit loop.
		if (status != STATUS_TIMEOUT)
			break;

		// This mount failed, delete everything, try again.
		dprintf("Timeout waiting for AddDevice\n");
		// DbgBreakPoint();
		vfs_set_mountedon(zmo_dcb, NULL);
		vfs_mount_remove(zmo_dcb);
		IoDeleteSymbolicLink(&zmo_dcb->symlink_name);
		IoDeleteSymbolicLink(&zmo_dcb->arc_name);
		zfs_release_mount(zmo_dcb);
		if (zmo_dcb->AttachedDevice)
			IoDetachDevice(zmo_dcb->AttachedDevice);
		IoDeleteDevice(diskDeviceObject);

		retry++;
		if (retry >= 4) {
			dprintf("Too many retries, giving up\n");
			return (STATUS_MOUNT_POINT_NOT_RESOLVED);
		}

	} while (1);

	/* Return volume name to caller */
	snprintf(value, valuelen,
	    "\\DosDevices\\Global\\Volume{%s}", uuid_a);

	dprintf("Finished AddDevice retry %d\n", retry);
	xprintf("Finished AddDevice retry %d\n", retry);

	dprintf("Waiting mount to finish\n");

	status = STATUS_SUCCESS;
	return (status);
}

int
zfs_windows_mount(zfs_cmd_t *zc)
{
	return (zfs_windows_mount_impl(zc->zc_name, zc->zc_value,
	    sizeof (zc->zc_value), zc->zc_cleanup_fd));
}

static NTSTATUS
ResolveSymlinkTarget(PCUNICODE_STRING Link, PUNICODE_STRING TargetBufOut)
{
	NTSTATUS st;
	HANDLE h = NULL;
	OBJECT_ATTRIBUTES oa;

	InitializeObjectAttributes(&oa, (PUNICODE_STRING)Link,
	    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	st = ZwOpenSymbolicLinkObject(&h, SYMBOLIC_LINK_QUERY, &oa);
	if (!NT_SUCCESS(st))
		return (st);

	ULONG dummy = 0;
	st = ZwQuerySymbolicLinkObject(h, TargetBufOut, &dummy);
	ZwClose(h);
	return (st);
}

static BOOLEAN
DosDevicesToGlobal(const UNICODE_STRING *dos,
    UNICODE_STRING *globalOut, WCHAR *scratch, USHORT scratchChars)
{
	static const WCHAR prefix[] = L"\\GLOBAL??\\";
	const WCHAR *s = dos->Buffer;
	USHORT n = dos->Length / sizeof (WCHAR);

	if (n < 12 || _wcsnicmp(s, L"\\DosDevices\\", 12) != 0)
		return (FALSE);

	USHORT need = ARRAYSIZE(prefix) - 1 + (n - 12);
	if (need + 1 > scratchChars)
		return (FALSE);

	USHORT i = 0, j = 0;
	while (prefix[i]) scratch[j++] = prefix[i++];
	for (USHORT k = 12; k < n; ++k) scratch[j++] = s[k];
	scratch[j] = L'\0';

	globalOut->Buffer = scratch;
	globalOut->Length = j * sizeof (WCHAR);
	globalOut->MaximumLength = scratchChars * sizeof (WCHAR);
	return (TRUE);
}

static NTSTATUS
WaitParentLetterReadyFromDcb(mount_t *dcb)
{
	if (!dcb || !dcb->justDriveLetter)
		return (STATUS_SUCCESS);

	// Build \\GLOBAL??\\X:
	WCHAR glBuf[64];
	UNICODE_STRING globalLetter = { 0 };
	if (!DosDevicesToGlobal(&dcb->mountpoint, &globalLetter, glBuf,
	    ARRAYSIZE(glBuf)))
		return (STATUS_INVALID_PARAMETER);

	// Expected device target
	const UNICODE_STRING *expectedDev = &dcb->device_name;

	// Also check that the Volume{GUID} \\?? link (MountMgr_name)
	// points to us
	const UNICODE_STRING *volumeGuidLink = &dcb->MountMgr_name;

	LARGE_INTEGER t50ms;
	t50ms.QuadPart = -50 * 10 * 1000; // 50 ms
	for (int attempt = 0; attempt < 20; ++attempt) {
		// 1) \\GLOBAL??\\X: -> \Device\zfs-...
		WCHAR tgtBuf1[256]; UNICODE_STRING tgt1 = {
		    .Buffer = tgtBuf1,
		    .MaximumLength = sizeof (tgtBuf1)
		};
		NTSTATUS st1 = ResolveSymlinkTarget(&globalLetter, &tgt1);

		// 2) \\??\\Volume{GUID} -> \Device\zfs-...
		WCHAR tgtBuf2[256];
		UNICODE_STRING tgt2 = {
		    .Buffer = tgtBuf2,
		    .MaximumLength = sizeof (tgtBuf2)
		};
		NTSTATUS st2 = ResolveSymlinkTarget(volumeGuidLink, &tgt2);

		BOOLEAN good1 = NT_SUCCESS(st1) &&
		    RtlEqualUnicodeString(&tgt1, expectedDev, TRUE);
		BOOLEAN good2 = NT_SUCCESS(st2) &&
		    RtlEqualUnicodeString(&tgt2, expectedDev, TRUE);

		if (good1 && good2) {
			// 3) Try opening the root of X:
			WCHAR rootW[8];
			// Build "\\??\\X:\\"
			// We know dosdevices_mountpoint ends with "X:",
			// so compose "\\??\\X:\\"
			rootW[0] = L'\\';
			rootW[1] = L'?';
			rootW[2] = L'?';
			rootW[3] = L'\\';
			rootW[4] = dcb->mountpoint.Buffer[
			    dcb->mountpoint.Length / 2 - 2]; // 'X'
			rootW[5] = L':';
			rootW[6] = L'\\';
			rootW[7] = L'\0';
			UNICODE_STRING root = {
			    .Buffer = rootW,
			    .Length = 6 * sizeof (WCHAR),
			    .MaximumLength = 8 * sizeof (WCHAR)
			};

			OBJECT_ATTRIBUTES oa;
			IO_STATUS_BLOCK iosb = { 0 };
			InitializeObjectAttributes(&oa, &root,
			    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
			    NULL, NULL);

			HANDLE h = NULL;
			NTSTATUS st = ZwCreateFile(&h,
			    FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, NULL,
			    FILE_ATTRIBUTE_DIRECTORY,
			    FILE_SHARE_READ | FILE_SHARE_WRITE |
			    FILE_SHARE_DELETE,
			    FILE_OPEN, FILE_DIRECTORY_FILE |
			    FILE_SYNCHRONOUS_IO_NONALERT,
			    NULL, 0);

			if (NT_SUCCESS(st)) {
				ZwClose(h);
				return (STATUS_SUCCESS);
			}

			// Only spin on "not ready yet"
			if (st != STATUS_OBJECT_PATH_NOT_FOUND &&
			    st != STATUS_OBJECT_NAME_NOT_FOUND &&
			    st != STATUS_REPARSE)
				return (st);
		}

		// Spin only on the "not ready yet" cases;
		// otherwise bubble the error.
		if ((!NT_SUCCESS(st1) &&
		    st1 != STATUS_OBJECT_PATH_NOT_FOUND &&
		    st1 != STATUS_OBJECT_NAME_NOT_FOUND &&
		    st1 != STATUS_OBJECT_TYPE_MISMATCH) ||
		    (!NT_SUCCESS(st2) &&
		    st2 != STATUS_OBJECT_PATH_NOT_FOUND &&
		    st2 != STATUS_OBJECT_NAME_NOT_FOUND &&
		    st2 != STATUS_OBJECT_TYPE_MISMATCH)) {
			return (NT_SUCCESS(st1) ? st2 : st1);
		}

		KeDelayExecutionThread(KernelMode, FALSE, &t50ms);
	}

	return (STATUS_OBJECT_PATH_NOT_FOUND); // timed out
}

/*
 * Which IRP_MN_MOUNT_VOLUME is called, volmgr is holding its lock
 * to call us, so we can talk to MountMgr without deadlock. If we
 * do it outside this context we risk lock inversion. But, sending
 * CREATE_POINT needs to be done outside, or it can not do the
 * RemoveDatabase() work we want. So figure out all the names needed,
 * then taskq off the final work.
 * This function assumes MountMgr was opened successfully.
 */

static void
mount_volume_impl(void *arg1)
{
	NTSTATUS status;
	mount_t *vcb = arg1;
	mount_t *dcb = vcb->parent_device;

	dprintf("MOUNT starts here\n");
	xprintf("MOUNT starts here: %wZ\n",
	    &dcb->mountpoint);

	DECLARE_UNICODE_STRING_SIZE(symbolicname, PATH_MAX);
	DECLARE_UNICODE_STRING_SIZE(mountpath, PATH_MAX);

	/*
	 * So MountMgr assigns a SymbolicLinkName for our device
	 * which we need to use when talking to MountMgr. We also
	 * want to know if it was already assigned a driverletter
	 */
	mountmgr_get_mountpoint2(&dcb->device_name,
	    &symbolicname, &mountpath,
	    TRUE);

	RtlDuplicateUnicodeString(0, &symbolicname, &dcb->MountMgr_name);
	RtlDuplicateUnicodeString(0, &mountpath, &dcb->MountMgr_mountpoint);

	// Check if we are to mount as path or just drive letter
	if (dcb->justDriveLetter) {

		// If SendVolumeArrival was executed successfully we should
		// have two mountpoints
		// point 1: \Device\Volumes{abc}	\DosDevices\X:
		// point 2: \Device\Volumes{abc}	\??\Volume{xyz}
		// but if we are in remount and removed the mountpoints
		// for this volume manually before
		// they won't get assigned by mountmgr automatically anymore.
		// So at least check if we got them and if not, try to create.

		if (!MOUNTMGR_IS_DRIVE_LETTER(&mountpath)) {
			DECLARE_UNICODE_STRING_SIZE(mountpoint, PATH_MAX);

			status = mountmgr_get_volume_name_mountpoint(
			    &dcb->device_name,
			    &mountpoint);

			if (!MOUNTMGR_IS_VOLUME_NAME(&mountpoint)) {
				// We have no volume name mountpoint for our
				// device, so generate a valid GUID and mount
				// the device
				UNICODE_STRING vol_mpt;
				wchar_t buf[50];
				generateVolumeNameMountpoint((wchar_t *)&buf);
				RtlInitUnicodeString(&vol_mpt, buf);
				status = SendVolumeCreatePoint(
				    &dcb->device_name, &vol_mpt);
			}

			// If driveletter was provided, try to add it
			// as mountpoint
			if (dcb && dcb->mountpoint.Length > 0 &&
			    dcb->mountpoint.Buffer[4] != '?') {
				// check if driveletter is unassigned
				BOOLEAN ret;

				status = mountmgr_is_driveletter_assigned(
				    dcb->mountpoint.Buffer[4], &ret);

				if (status == STATUS_SUCCESS && ret == 0) {
					// driveletter is unassigned, try to
					// add mountpoint
					status =
					    mountmgr_assign_driveletter(
					    &dcb->device_name,
					    dcb->mountpoint.Buffer[4]);

				} else {
					// driveletter already assigned,
					// find another one
					SetNextDriveletterManually(
					    &dcb->device_name);
				}
			} else {
				// user provided no driveletter,
				// find one on our own
				SetNextDriveletterManually(
				    &dcb->device_name);
			}
		} else { // !MOUNTMGR_IS_DRIVE_LETTER(&actualDriveletter)

			// mountpath is "/DosDevices/E:" but we need to
			// save that in dcb, for future query.
			// We want driveletter, and we have a driveletter
			// but is it the one we want?
			// "\??\y:" vs "\DosDevices\y:"
			if (dcb->mountpoint.Length >= 4 &&
			    mountpath.Length >= 12 &&
			    dcb->mountpoint.Buffer[4] != L'?' &&
			    dcb->mountpoint.Buffer[4] !=
			    mountpath.Buffer[12]) {

				dprintf("Wrong driveletter, removing %lc\n",
				    mountpath.Buffer[12]);
				status = SendVolumeDeletePoints(
				    &mountpath,
				    &dcb->device_name);
				if (!NT_SUCCESS(status))
					dprintf("Failed to remove %lc\n",
					    mountpath.Buffer[12]);

				dprintf("Adding %lc\n",
				    dcb->mountpoint.Buffer[4]);
				status = mountmgr_assign_driveletter(
				    &dcb->MountMgr_name,
				    dcb->mountpoint.Buffer[4]);
				if (!NT_SUCCESS(status))
					dprintf("Failed to add %lc\n",
					    dcb->mountpoint.Buffer[4]);
			}

			FreeUnicodeString(&dcb->mountpoint);
			RtlDuplicateUnicodeString(0, &mountpath,
			    &dcb->mountpoint);

		}
	} else {

		taskq_dispatch(system_taskq, NotifyMountMgr_impl, vcb,
		    TQ_SLEEP);
		return;

	}

	RtlDuplicateUnicodeString(0, &dcb->mountpoint, &vcb->mountpoint);

	if (vcb->root_file)
		status = FsRtlNotifyVolumeEvent(vcb->root_file,
		    FSRTL_VOLUME_MOUNT);

	dprintf("Printing final result\n");
	mountmgr_get_mountpoint2(&dcb->device_name,
	    NULL, NULL, TRUE);

	// This might need fixing

	status = WaitParentLetterReadyFromDcb(dcb);
	if (!NT_SUCCESS(status)) {
		dprintf("Parent DOS letter not ready (st=%08x) for %wZ\n",
		    status, &dcb->mountpoint);
	}

	dprintf("Releasing mount wait\n");
	zfs_release_userland(dcb);
}

NTSTATUS
matched_mount(PIRP Irp, PDEVICE_OBJECT DeviceToMount,
    mount_t *dcb, PIO_STACK_LOCATION IrpSp)
{
	zfsvfs_t *xzfsvfs = NULL;
	NTSTATUS status;
	PDEVICE_OBJECT volDeviceObject;
	PDEVICE_OBJECT DeviceObject =
	    IrpSp->Parameters.MountVolume.DeviceObject;
	PVPB vpb = IrpSp->Parameters.MountVolume.Vpb;

	if (xzfsvfs && xzfsvfs->z_unmounted) {
		dprintf("%s: Is a ZFS dataset -- unmounted. dcb %p ignoring: "
		    "type 0x%x != 0x%x, size %lu != %llu\n",
		    __func__, dcb,
		    dcb->type, MOUNT_TYPE_DCB, dcb->size, sizeof (mount_t));
		return (STATUS_UNRECOGNIZED_VOLUME);
	}

	// We created a DISK before, now we create a VOLUME
	ULONG deviceCharacteristics;
	deviceCharacteristics = 0; // FILE_DEVICE_IS_MOUNTED;

	/* Allow $recycle.bin - don't set removable. */
	if (!zfs_disable_removablemedia)
		deviceCharacteristics |= FILE_REMOVABLE_MEDIA;

	if (dcb->mountflags & MNT_RDONLY)
		deviceCharacteristics |= FILE_READ_ONLY_DEVICE;

	/* This creates the VDO - VolumeDeviceObject */
	status = IoCreateDeviceSecure(WIN_DriverObject,
	    sizeof (mount_t),
	    &dcb->fs_name,
	    FILE_DEVICE_DISK_FILE_SYSTEM,
	    deviceCharacteristics,
	    FALSE,
	    &sddlVolume,
	    NULL,
	    &volDeviceObject);

	if (!NT_SUCCESS(status)) {
		dprintf("%s: IoCreateDevice failed: 0x%lx\n", __func__, status);
		KeSetEvent((PRKEVENT)&dcb->volume_mounted_event,
		    SEMAPHORE_INCREMENT, FALSE);
		return (STATUS_UNRECOGNIZED_VOLUME);
	}

	mount_t *vcb = volDeviceObject->DeviceExtension;
	vcb->type = MOUNT_TYPE_VCB;
	vcb->size = sizeof (mount_t);

	// volDeviceObject->Flags |= DO_BUS_ENUMERATED_DEVICE;
	volDeviceObject->SectorSize = 512;

	zfsvfs_t *zfsvfs = NULL;

	// Call ZFS to mount now, if it fail, undo mount.
	struct zfs_mount_args mnt_args;
	mnt_args.struct_size = sizeof (struct zfs_mount_args);
	mnt_args.optlen = 0;
	mnt_args.mflag = vfs_isrdonly(dcb) ? MNT_RDONLY : 0;
	mnt_args.fspec = dcb->ascii_name;

	dprintf("Calling ZFS mount\n");
	status = zfs_vfs_mount(vcb, NULL, (user_addr_t)&mnt_args, NULL);
	dprintf("%s: zfs_vfs_mount() returns %ld\n", __func__, status);

	if (status) {
		zfs_release_mount(vcb);
		IoDeleteDevice(volDeviceObject);

		status = IoSetDeviceInterfaceState(
		    &dcb->fsInterfaceName, FALSE);
		status = IoSetDeviceInterfaceState(
		    &dcb->deviceInterfaceName, FALSE);
		IoDeleteSymbolicLink(&dcb->symlink_name);
		IoDeleteSymbolicLink(&dcb->arc_name);
		if (dcb->AttachedDevice)
			IoDetachDevice(dcb->AttachedDevice);
		vfs_mount_remove(dcb);
		zfs_release_mount(dcb);
		IoDeleteDevice(dcb->FunctionalDeviceObject);
		return (STATUS_UNRECOGNIZED_VOLUME);
	}

	zfsvfs = vfs_fsprivate(vcb);
	zfsvfs->z_vfs = vcb;

	vfs_setfsprivate(dcb, zfsvfs);
	xzfsvfs = vfs_fsprivate(dcb);
	xzfsvfs->z_vfs = vcb;

	// Remember the parent device, so during unmount we can free both.
	vcb->parent_device = dcb;

	// vcb is the ptr used in unmount, so set both devices here.
	// vcb->diskDeviceObject = dcb->deviceObject;
	vcb->VolumeDeviceObject = volDeviceObject;

	RtlDuplicateUnicodeString(0, &dcb->fs_name, &vcb->fs_name);
	RtlDuplicateUnicodeString(0, &dcb->name, &vcb->name);
	// RtlDuplicateUnicodeString(0, &dcb->device_name, &vcb->device_name);
	RtlDuplicateUnicodeString(0, &dcb->fs_name, &vcb->device_name);
	RtlDuplicateUnicodeString(0, &dcb->symlink_name, &vcb->symlink_name);
	RtlDuplicateUnicodeString(0, &dcb->arc_name, &vcb->arc_name);
	RtlDuplicateUnicodeString(0, &dcb->uuid, &vcb->uuid);
	memcpy(vcb->rawuuid, dcb->rawuuid, sizeof (vcb->rawuuid));
	vfs_set_mountedon(vcb, vfs_mountedon(dcb));
	if (dcb->ascii_name)
		vcb->ascii_name = kmem_strdup(dcb->ascii_name);

	vcb->mountflags = dcb->mountflags;
	if (vfs_isrdonly(dcb))
		vfs_setrdonly(vcb);

	// Directory notification
	InitializeListHead(&vcb->DirNotifyList);
	FsRtlNotifyInitializeSync(&vcb->NotifySync);

	KIRQL OldIrql;

	IoAcquireVpbSpinLock(&OldIrql);
	InitVpb(vpb, volDeviceObject, dcb);
	/*
	 * The IO Manager pre-fills Vpb->RealDevice with the top-of-stack
	 * FDO, but MountMgr's \DosDevices\E: symlink targets our named DCB
	 * (\Device\zfs-{uuid}).  IoQueryFileDosDeviceName() resolves the
	 * drive letter via Vpb->RealDevice, so point it at the DCB so that
	 * tools like FileSpy (and kernel callers of IoQueryFileDosDeviceName)
	 * can resolve file paths to their drive letter instead of showing the
	 * raw \Device\ZFS{uuid}\... NT path.
	 */
	vpb->RealDevice = dcb->FunctionalDeviceObject;
	volDeviceObject->Vpb = vpb;
	vcb->vpb = vpb;
	vpb->ReferenceCount += 1;
	// dcb->vpb = vpb;
	IoReleaseVpbSpinLock(OldIrql);

	vcb->root_file = IoCreateStreamFileObject(NULL, DeviceToMount);
	if (vcb->root_file != NULL) {
		dprintf("root_file is %p\n", vcb->root_file);

		// Attach vp/zp to it. We call volume_close()
		// when unmounting. This holds root busy/mounted.
		status = volume_create(dcb->PhysicalDeviceObject,
		    vcb->root_file,
		    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		    0ULL,
		    FILE_READ_ATTRIBUTES);
		if (NT_SUCCESS(status)) {
			znode_t *zp;
			zfs_ccb_t *zccb = NULL;
			// vp = vcb->root_file->FsContext;
			/* This open needs to point to the real root zp */
			if (zfs_zget(zfsvfs, zfsvfs->z_root, &zp) == 0) {
				zfs_couplefileobject(ZTOV(zp), NULL,
				    vcb->root_file, 0ULL, &zccb, 0ULL, 0, NULL,
				    Irp);
				zrele(zp);
			}
			vcb->root_file->Vpb = vpb;
		} else {
			ObDereferenceObject(vcb->root_file);
			vcb->root_file = NULL;
		}
	}

	DeviceToMount->Flags |= DO_DIRECT_IO;
	// volDeviceObject->Flags |= DO_DIRECT_IO;
	volDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	// SetLongFlag(vcb->Flags, VCB_MOUNTED);


	// OK apparently volmgr forms the relationship between
	// my FILE_DEVICE_DISK_FILE_SYSTEM and the FILE_DEVICE_DISK
	// we are mounting, through the Vpb->DeviceObject returned.
	// Ntfs does not even call IoAttachDeviceToDeviceStack().
	// If we attach here, mountmgr occasionally deadlocks,
	// and always deadlocks of Avast is installed.
#if 0
	PDEVICE_OBJECT attachedDevice;
	attachedDevice = IoAttachDeviceToDeviceStack(volDeviceObject,
	    DeviceToMount); // DeviceToMount); // definitely deadlocks
	if (attachedDevice == NULL) {
		IoDeleteDevice(volDeviceObject);
		status = STATUS_UNSUCCESSFUL;
		goto out;
	}
	vcb->AttachedDevice = attachedDevice;
#endif
	volDeviceObject->StackSize = DeviceObject->StackSize + 1;

	status = STATUS_SUCCESS;


	/*
	 * We can get some deep stacks here, so it might be
	 * best to push the rest off on a fresh stack. However,
	 * we can not leave this function too early due to deadlock.
	 * Here we are called by FLTMGR (which holds a lock) and can
	 * call MountMgr, but if we call MountMgr without the FLTMGR
	 * we can easily deadlock.
	 * We will setup detection for re-entry, in case it is needed,
	 * the ZFS part of the mount can be done as a separate thread
	 * which we can wait on. All MountMgr has to be done here,
	 * Calls like FSCTL_SET_REPARSE_POINT can call into ZFS again,
	 * in which case we have to return STATUS_PENDING, and handle
	 * in a WorkItem thread.
	 */

	tsd_set(zfs_mount_reentry_tsd, (void *) 1);
	atomic_inc_64(&zfs_mount_reentry);

	dprintf("Waiting for ZFS mount to come back\n");
	mount_volume_impl(vcb);
	dprintf("done\n");

	atomic_dec_64(&zfs_mount_reentry);
	tsd_set(zfs_mount_reentry_tsd, NULL);

	dprintf("%s completed.\n", __func__);
out:
	return (status);
}

int
zfs_vnop_mount(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT DeviceToMount, currentDevice, nextDevice;

	if (IrpSp->Parameters.MountVolume.DeviceObject == NULL) {
		dprintf("%s: MountVolume is NULL\n", __func__);
		return (STATUS_UNRECOGNIZED_VOLUME);
	}

	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	// The "/OpenZFS" FILE_DEVICE_DISK_FILE_SYSTEM "masterobj"
	if (DiskDevice != DriverExtension->fsDiskDeviceObject) {
		dprintf("No fsDiskDeviceObject object, unloading?\n");
		return (STATUS_UNRECOGNIZED_VOLUME);
	}

	DeviceToMount = IrpSp->Parameters.MountVolume.DeviceObject;
	if (DeviceToMount->DeviceType != FILE_DEVICE_DISK) {
		dprintf("Not FILE_DEVICE_DISK object -- ignoring\n");
		// Not a disk device, pass it down
		return (STATUS_UNRECOGNIZED_VOLUME);
	}

	mount_t *dcb = NULL;

	// DeviceToMount must be released from here down
	currentDevice = IoGetAttachedDeviceReference(DeviceToMount);
	if (!currentDevice) {
		currentDevice = DeviceToMount;
		ObReferenceObject(currentDevice);
	}

	// Check the whole stack for our device
	for (;
	    currentDevice;
	    currentDevice = nextDevice) {

		dprintf("%s: Checking DeviceObject %p\n",
		    __func__,
		    currentDevice);

		dcb = currentDevice->DeviceExtension;

		if (dcb != NULL && dcb->type == MOUNT_TYPE_DCB &&
		    dcb->ascii_name != NULL &&
		    !vfs_isunmount(dcb)) {

			status = matched_mount(Irp,
			    DeviceToMount,
			    dcb, IrpSp);

			ObDereferenceObject(currentDevice);
			dprintf("%s: exit: 0x%lx\n", __func__, status);
			return (status);
		}

		nextDevice = IoGetLowerDeviceObject(currentDevice);
		ObDereferenceObject(currentDevice);

		if (currentDevice == nextDevice)
			break;
	}

	dprintf("Not our object or unmounting -- ignoring\n");
	status = STATUS_UNRECOGNIZED_VOLUME;

out:
	dprintf("%s: exit: 0x%lx\n", __func__, status);
	return (status);
}

NTSTATUS IoSetDeviceInterfacePropertyData(
	PUNICODE_STRING SymbolicLinkName,
	const DEVPROPKEY *PropertyKey,
	LCID Lcid,
	ULONG Flags,
	DEVPROPTYPE Type,
	ULONG Size,
	PVOID Data
);

void
mount_add_device(PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject)
{
	NTSTATUS status;
	mount_t *zmo = (mount_t *)PhysicalDeviceObject->DeviceExtension;

	if (zmo->PhysicalDeviceObject == PhysicalDeviceObject) {
		// xprintf("Already registered\n");
		return;
	}

	zmo->PhysicalDeviceObject = PhysicalDeviceObject;
#if 1
	status = IoRegisterDeviceInterface(PhysicalDeviceObject,
	    &GUID_DEVINTERFACE_VOLUME, NULL,
	    &zmo->deviceInterfaceName);
	dprintf("register GUID_DEVINTERFACE_VOLUME: 0x%lx\n", status);

	// We can attach to DriverExtension->PhysicalDeviceObject here,
	// but then most IRP go to busDispatcher() first, then we pass
	// down to diskDispatcher().

	status = IoSetDeviceInterfaceState(&zmo->deviceInterfaceName, TRUE);
	dprintf("Enable GUID_DEVINTERFACE_VOLUME: 0x%lx\n", status);

	/*
	 * Set DEVPKEY_Storage_SystemCritical and DEVPKEY_Storage_Portable
	 * = FALSE on our GUID_DEVINTERFACE_VOLUME entry.  twext.dll
	 * (Previous Versions) queries both; if either is absent it skips
	 * the SMB loopback path and FSCTL_SRV_ENUMERATE_SNAPSHOTS is never
	 * sent.  Both must exist (FALSE) for Previous Versions to work.
	 */
	{
		/* {4d1ebee8-0803-4774-9842-b77db50265e9}, 4 */
		static const DEVPROPKEY kStorageSysCritical = {
		    { 0x4d1ebee8, 0x0803, 0x4774,
		    { 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x02, 0x65, 0xe9 } },
		    4
		};
		/* {4d1ebee8-0803-4774-9842-b77db50265e9}, 2 */
		static const DEVPROPKEY kStoragePortable = {
		    { 0x4d1ebee8, 0x0803, 0x4774,
		    { 0x98, 0x42, 0xb7, 0x7d, 0xb5, 0x02, 0x65, 0xe9 } },
		    2
		};
		DEVPROP_BOOLEAN val = DEVPROP_FALSE;
		NTSTATUS sprop_status = IoSetDeviceInterfacePropertyData(
		    &zmo->deviceInterfaceName,
		    &kStorageSysCritical, 0, 0,
		    DEVPROP_TYPE_BOOLEAN, sizeof (DEVPROP_BOOLEAN), &val);
		dprintf("Set DEVPKEY_Storage_SystemCritical: 0x%lx\n",
		    sprop_status);
		sprop_status = IoSetDeviceInterfacePropertyData(
		    &zmo->deviceInterfaceName,
		    &kStoragePortable, 0, 0,
		    DEVPROP_TYPE_BOOLEAN, sizeof (DEVPROP_BOOLEAN), &val);
		dprintf("Set DEVPKEY_Storage_Portable: 0x%lx\n",
		    sprop_status);
	}

	status = IoRegisterDeviceInterface(PhysicalDeviceObject,
	    &MOUNTDEV_MOUNTED_DEVICE_GUID, NULL,
	    &zmo->fsInterfaceName);
	dprintf("register MOUNTDEV_MOUNTED_DEVICE_GUID: 0x%lx\n", status);

	status = IoSetDeviceInterfaceState(&zmo->fsInterfaceName, TRUE);
	dprintf("Enable MOUNTDEV_MOUNTED_DEVICE_GUID: 0x%lx\n", status);
#endif
	dprintf("Releasing mount wait\n");
	KeSetEvent((PRKEVENT)&zmo->volume_adddevice_event,
	    SEMAPHORE_INCREMENT, FALSE);
}


static int
unmount_find_volume(void *arg, void *priv)
{
	mount_t *zmo_dcb = (mount_t *)arg;
	UNICODE_STRING *symlink_name = (UNICODE_STRING *)priv;

	if ((zmo_dcb->deviceInterfaceName.Length == symlink_name->Length) &&
	    RtlCompareMemory(zmo_dcb->deviceInterfaceName.Buffer,
	    symlink_name->Buffer, symlink_name->Length) ==
	    symlink_name->Length) {

		// Let unmount continue below...
		dprintf("%s: found, releasing unmount\n", __func__);
		KeSetEvent((PRKEVENT)&zmo_dcb->volume_removed_event,
		    SEMAPHORE_INCREMENT, FALSE);
		return (1);
	}
	return (0);
}

void
zfs_windows_unmount_free(PUNICODE_STRING symlink_name)
{
	dprintf("%s: looking for %wZ\n", __func__, symlink_name);

	vfs_mount_iterate(unmount_find_volume, symlink_name);
}

/*
 * Force-unmount any VCBs whose ascii_name matches "<parent_name>@*".
 * Called before unmounting the parent dataset so that PnP does not
 * surprise-remove snapshot device objects during pool export.
 *
 * Must be called WITHOUT holding vfs_main_lock (i.e., before getzfsvfs).
 */
static void
zfs_unmount_snap_children(const char *parent_name)
{
	int i, count, nsnaps = 0;
	void **array;
	char **snap_names;
	size_t plen = strlen(parent_name);

	dprintf("%s: %s\n", __func__, parent_name);

	/*
	 * Remove VSS snapshot devices for "<parent_name>@*" before touching
	 * the VCB mounts.  VSS devices are not in the global mount list
	 * until lazily mounted, so we iterate the AVL tree via
	 * zfs_vss_remove_by_prefix.  This must happen before the VCB
	 * snapshot unmounts and before the parent VCB tears down, otherwise
	 * IRP_MN_SURPRISE_REMOVAL races with the live VSS device object.
	 */
	zfs_vss_remove_by_prefix(parent_name);

	count = vfs_mount_count();
	if (count == 0)
		return;

	array = kmem_zalloc(count * sizeof (void *), KM_SLEEP);
	snap_names = kmem_zalloc(count * sizeof (char *), KM_SLEEP);

	vfs_mount_setarray(array, count);

	for (i = 0; i < count; i++) {
		mount_t *m = array[i];
		const unsigned char *aname;
		zfsvfs_t *z;

		if (m == NULL)
			break;
		if (m->type != MOUNT_TYPE_VCB)
			continue;
		z = vfs_fsprivate(m);
		if (z == NULL || !z->z_issnap)
			continue;
		aname = m->ascii_name;
		if (aname == NULL)
			continue;
		if (strlen((const char *)aname) <= plen ||
		    strncmp((const char *)aname, parent_name, plen) != 0 ||
		    aname[plen] != '@')
			continue;
		snap_names[nsnaps++] = kmem_strdup((const char *)aname);
	}

	kmem_free(array, count * sizeof (void *));

	for (i = 0; i < nsnaps; i++) {
		dprintf("%s: preflight-unmounting snapshot '%s'\n",
		    __func__, snap_names[i]);
		(void) zfs_windows_unmount_impl(snap_names[i]);
		kmem_strfree(snap_names[i]);
	}

	kmem_free(snap_names, count * sizeof (char *));
}

int
zfs_windows_unmount(zfs_cmd_t *zc)
{
	/*
	 * Preflight: if this is a non-snapshot dataset, force-unmount any
	 * snapshot children first.  PnP will crash (surprise-remove) if a
	 * snapshot device object disappears while its parent is being torn
	 * down.  Do this BEFORE getzfsvfs so we hold no vfs_main_lock.
	 */
	zfs_unmount_snap_children(zc->zc_name);
	return (zfs_windows_unmount_impl(zc->zc_name));
}

int
zfs_windows_unmount_impl(const char *datasetname)
{
	mount_t *zmo;
	mount_t *zmo_dcb = NULL;
	zfsvfs_t *zfsvfs;
	boolean_t wasmounted = B_FALSE;
	int error = EBUSY;
	ZFS_DRIVER_EXTENSION(WIN_DriverObject, DriverExtension);

	if (getzfsvfs(datasetname, &zfsvfs) == 0) {

		zmo = zfsvfs->z_vfs;
		NTSTATUS ntstatus;
		ASSERT(zmo->type == MOUNT_TYPE_VCB);

		// As part of unmount-preflight, we call vflush()
		// as it will indicate if we should return EBUSY.
		if (vnode_umount_preflight(zmo, NULL,
		    SKIPROOT|SKIPSYSTEM|SKIPSWAP)) {
			vfs_unbusy(zmo);
			return (SET_ERROR(EBUSY));
		}

		// Has to be called before upgrading vfs lock
		CcWaitForCurrentLazyWriterActivity();

		// getzfsvfs() grabs a READER lock,
		// convert it to WRITER, and wait for it.
		vfs_busy(zmo, LK_UPGRADE);

		UNICODE_STRING	name;
		NTSTATUS status;
		// Save the parent device
		zmo_dcb = zmo->parent_device;

		// Query MntMgr for points, just informative
		DECLARE_UNICODE_STRING_SIZE(mountpath, PATH_MAX);

		status = mountmgr_get_mountpoint2(
		    &zmo_dcb->device_name, NULL, &mountpath, TRUE);

		// Get ready to wait for the volume removed notification
		KeInitializeEvent((PRKEVENT)&zmo_dcb->volume_removed_event,
		    SynchronizationEvent, FALSE);

		if (zmo->root_file)
			status = FsRtlNotifyVolumeEvent(zmo->root_file,
			    FSRTL_VOLUME_DISMOUNT);

		dprintf("Sending volume dismount PnP notification\n");
		status = IoReportTargetDeviceChangeAsynchronous(
		    zmo_dcb->PhysicalDeviceObject,
		    &GUID_IO_VOLUME_DISMOUNT,
		    NULL,
		    NULL);

		// Flush volume
		// rdonly = !spa_writeable(dmu_objset_spa(zfsvfs->z_os));

		// Delete mountpoints for our volume manually
		// Query the mountmgr for mountpoints and delete them
		// until no mountpoint is left Because we are not satisfied
		// with mountmgrs work, it gets offended and doesn't
		// automatically create mointpoints for our volume after we
		// deleted them manually But as long as we recheck that in
		// mount and create points manually (if necessary),
		// that should be ok hopefully
		if (MOUNTMGR_IS_DRIVE_LETTER(&mountpath)) {

			zfs_remove_driveletter(zmo_dcb);

		} else {
			/*
			 * If mount uses reparsepoint (not driveletter):
			 * clear mounted_on on both VCB and DCB so that
			 * vfs_has_mount() returns NULL and
			 * delete_reparse_point() does not deny the FSCTL.
			 */
			vfs_set_mountedon(zmo, NULL);
			vfs_set_mountedon(zmo_dcb, NULL);

			OBJECT_ATTRIBUTES poa;

			InitializeObjectAttributes(&poa,
			    &zmo_dcb->mountpoint, OBJ_KERNEL_HANDLE,
			    NULL, NULL);
			dprintf("Deleting reparse mountpoint '%wZ'\n",
			    &zmo_dcb->mountpoint);
			DeleteReparsePoint(&poa);

			/* Remove directory left by the reparse point. */
			{
				HANDLE hdir;
				IO_STATUS_BLOCK rmiosb;
				NTSTATUS rmst;

				rmst = ZwCreateFile(&hdir,
				    DELETE | SYNCHRONIZE,
				    &poa, &rmiosb, NULL, 0,
				    FILE_SHARE_DELETE,
				    FILE_OPEN,
				    FILE_DIRECTORY_FILE |
				    FILE_SYNCHRONOUS_IO_NONALERT |
				    FILE_OPEN_FOR_BACKUP_INTENT,
				    NULL, 0);
				if (NT_SUCCESS(rmst)) {
					FILE_DISPOSITION_INFORMATION fdi;
					fdi.DeleteFile = TRUE;
					rmst = ZwSetInformationFile(hdir,
					    &rmiosb, &fdi, sizeof (fdi),
					    FileDispositionInformation);
					dprintf("%s: rmdir '%wZ' status 0x%x\n",
					    __func__, &zmo_dcb->mountpoint,
					    rmst);
					ZwClose(hdir);
				} else {
					dprintf("%s: open for rmdir '%wZ' "
					    "failed 0x%x\n", __func__,
					    &zmo_dcb->mountpoint, rmst);
				}
			}

			status = NotifyMountMgr(
			    &zmo_dcb->mountpoint,
			    &zmo_dcb->MountMgr_name, B_FALSE);

		}

		KIRQL irql;
		IoAcquireVpbSpinLock(&irql);
		wasmounted = zmo->vpb->Flags & VPB_MOUNTED;
		zmo->vpb->Flags &= ~VPB_MOUNTED;
		// zmo_dcb->vpb->Flags &= ~VPB_MOUNTED;
		zmo->vpb->Flags |= VPB_DIRECT_WRITES_ALLOWED;
		zmo->vpb->Flags |= VPB_REMOVE_PENDING;
		IoReleaseVpbSpinLock(irql);

		// Release any notifications
		FsRtlNotifyCleanupAll(zmo->NotifySync, &zmo->DirNotifyList);

		// This will make it try to mount again, so make sure we dont

		status = SendVolumeRemovalNotification(
		    &zmo_dcb->MountMgr_name);

		dprintf("Closing MountMgr\n");

		struct vnode *vp = NULL;
		if (zmo->root_file) {
			vp = zmo->root_file->FsContext;
			// this calls volume_close, but stop any new volume_open
			status = volume_close(zmo_dcb->PhysicalDeviceObject,
			    zmo->root_file);
			if (vp && VN_HOLD(vp) == 0) {
				dprintf("vp %p\n", vp);
				zfs_decouplefileobject(vp, zmo->root_file);
				vnode_rele(vp);
				VN_RELE(vp);
			}
			ObDereferenceObject(zmo->root_file);
			zmo->root_file = NULL;
		}

		dprintf("Set UNMOUNTING\n");
		vfs_setflags(zmo, MNT_UNMOUNTING);
		vfs_setflags(zmo_dcb, MNT_UNMOUNTING);

		dnlc_purge_vfsp(zmo, 0);

		// Release devices
		status = IoDeleteSymbolicLink(&zmo->arc_name);
		status = IoDeleteSymbolicLink(&zmo->symlink_name);

		// zmo has Volume, and Attached
		status = IoSetDeviceInterfaceState(
		    &zmo_dcb->fsInterfaceName, FALSE);
		status = IoSetDeviceInterfaceState(
		    &zmo_dcb->deviceInterfaceName, FALSE);

		if (zmo->AttachedDevice)
			IoDetachDevice(zmo->AttachedDevice);
		zmo->AttachedDevice = NULL;

		// IoInvalidateDeviceState(zmo_dcb->FunctionalDeviceObject);

		/*
		 * We call mount on DCB, but shouldn't it be VCB? We
		 * match unmount on DCB here so vflush can compare.
		 * DCB and VCB do have almost the same information, but
		 * it is probably more correct to change mount to use VCB.
		 */

		// wait for volume removal notification
		LARGE_INTEGER timeout;
		timeout.QuadPart = -10000000 * 10;
		status = KeWaitForSingleObject(&zmo_dcb->volume_removed_event,
		    Executive, KernelMode, TRUE, &timeout);
		// If we timeout, lets just continue and hope for the best?

		// We must wait for root usecount == 0
		dprintf("Released. Waiting for volume_opens to be released\n");

		// Make sure all volume_open() has called volume_close()
		// This could be improved, condvar etc. But it triggers
		// rarely at the moment.
		int retry = 0;

		while (zmo_dcb->volume_opens > 0) {
			delay(hz >> 2); // Fixme
			if (retry++ > 10) break;
		}

		dprintf("Done. Waiting for mount root to be released\n");

		// Then make sure we aren't still in the middle of
		// a vmode_create() by grabbing zfs_enter() lock.
		ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, FTAG);
		if (vp != NULL) {
			retry = 0;
			while (vp->v_iocount > 0 || vp->v_usecount > 0) {
				delay(hz >> 2); // Fixme, condvar
				if (retry++ > 10) break;
			}
		}

		// Unix would handle this at unmount, effectively dealing
		// with the markroot vnode. So we remove the ROOT part so
		// it can be recycled. vflush() should handle it.
		if (vp)
			vp->v_flags &= ~VNODE_MARKROOT; // Hack
		ZFS_TEARDOWN_EXIT(zfsvfs, FTAG);

		dprintf("%s: calling zfs_vfs_unmount\n", __func__);
		error = zfs_vfs_unmount(zmo, 0, NULL);
		dprintf("%s: zfs_vfs_unmount %d\n", __func__, error);

		IoAcquireVpbSpinLock(&irql);
		if (wasmounted)
			zmo->vpb->ReferenceCount -= 1;
		zmo->vpb->DeviceObject = NULL;
		IoReleaseVpbSpinLock(irql);

		// dcb has physical and functional
		// There should also be a diskDevice above us to release.
		if (zmo_dcb != NULL) {

			// Vpb should be set to NULL before Detach, or
			// PnpFindMountableDevice() might return it as
			// mounted (and BSOD in PnpLockMountableDevice())
			zmo_dcb->FunctionalDeviceObject->Vpb = NULL;
			vfs_mount_remove(zmo_dcb);

			if (zmo_dcb->AttachedDevice) {
				IoDetachDevice(zmo_dcb->AttachedDevice);
				zmo_dcb->AttachedDevice = NULL;
			}

			// Release strings in zmo, then zmo w/ IoDeleteDevice()
			zfs_release_mount(zmo_dcb);

			// Physical and Functional are same for DCB
			if (zmo_dcb->PhysicalDeviceObject !=
			    zmo_dcb->FunctionalDeviceObject) {
				IoDeleteDevice(zmo_dcb->FunctionalDeviceObject);
			}
#if 1
			if (zmo_dcb->PhysicalDeviceObject) {
				zmo_dcb->PhysicalDeviceObject->Vpb = NULL;
				IoDeleteDevice(zmo_dcb->PhysicalDeviceObject);
			}
#endif
			zmo_dcb = NULL;
		}

		// Release strings in zmo, then zmo by calling IoDeleteDevice()
		if (zmo->VolumeDeviceObject) {
			zmo->VolumeDeviceObject->Vpb = NULL;
			zfs_release_mount(zmo);
			IoDetachDevice(zmo->VolumeDeviceObject);
			IoDeleteDevice(zmo->VolumeDeviceObject);
			zmo = NULL;
		}

		// Make Windows recheck what child devices are left.
		IoInvalidateDeviceRelations(
		    DriverExtension->PhysicalDeviceObject, BusRelations);

		error = 0;

out_unlock:
		// counter to getzfvfs
		zfsvfs->z_vfs = NULL;
		vfs_unbusy(NULL);

	}
	return (error);
}
