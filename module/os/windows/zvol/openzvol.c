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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>
 */

/*
 * It is undesirable for /FileSystem/OpenZFS and the StorPort miniport for ZVOL
 * to be in the same driver. Not only as they both need to set DriverObject
 * MajorFunction table, AddDevice and DriverUnload function pointers, but they
 * need to attach at different places in the driver stack, as well as, to be
 * installed as different device types from the .INF file.
 *
 * So OpenZFS.sys driver, and the StorPort miniport driver for ZVOLs
 * (OpenZVOL.sys) are now separate, with two .INF files, OpenZFS.inf and
 * OpenZVOL.inf.
 *
 * We consider OpenZFS.sys to be the main driver, and can function without
 * OpenZVOL.sys, although without ZVOL device support. Datasets can still be
 * used and mounted, just not ZVOL Volumes.
 *
 * OpenZVOL.sys's DriverLoad() calls StorPortInit() to get ready, and uses
 * IoCreateDriver() to have a second set of MajorFunctions for the creation of
 * \Device\OpenZVOL device node. This is used for IRP communication with
 * OpenZVOL.sys driver.
 *
 * When OpenZFS.sys is loaded, it will start a taskq to poll for OpenZVOL.sys
 * to load (including if it is already loaded) by polling for the existence of
 * \Device\OpenZVOL. Once the OpenZVOL.sys driver has loaded and is detected,
 * OpenZFS.sys will register itself. OpenZFS.sys will keep the FileObject to
 * OpenZVOL.sys held, until deregister time. Both for performance, and to
 * ensure the driver will not unload.
 *
 * zvol_os_register_module(zfs_api_t *): Issue IRP [OPENZVOL_REGISTER] with
 *         function pointers for IO; namely zvol_os_read_zv(),
 *         zvol_os_write_zv() and zvol_os_unmap().
 *         OpenZVOL when receiving IRP will save the function pointers, and
 *         keep a reference on DriverObject.
 *
 * zvol_os_deregister_module(void): Issue IRP [OPENZVOL_DEREGISTER].
 * Closes FileObject
 *         to \Device\OpenZVOL.
 *         OpenZVOL will clear the function pointers, and release DriverObject.
 *
 * zvol_os_assign_targetid(zvol_state_t *): Issue IRP [OPENZVOL_ASSIGN_TARGETID]
 *         Passing over a pointer to "zv" zvol_state_t.
 *         OpenZVOL looks up available (lun, tag) and assigns to "zv".
 *         After completion "zv" is not used by OpenZVOL, just the address to
 *         map (lun, id) and "zv".
 *
 * zvol_os_clear_targetid(zvol_state_t *): Issue IRP [OPENZVOL_CLEAR_TARGETID]
 *         Passing pointer to "zv" zvol_state_t.
 *         OpenZVOL will release (lun, id) mapping to "zv" and not refer
 *         to it again.
 *
 * zvol_os_announce_buschange(void): Issue IRP [OPENZVOL_ANNOUNCE_BUSCHANGE]
 *         OpenZVOL will issue buschanged notification, for drive added/removed.
 *
 * It could be considered hacky to pass the IO functions (zvol_os_read_zv(),
 * zvol_os_write_zv() and zvol_os_unmap()) to OpenZVOL.sys, and call them
 * directly. It hopefully means there is no performance penalty to separate
 * the two drivers, but it could be explored to instead issue IRPs for IO
 * as well. Comments welcome.
 * Certainly direct calling functions would not work in Unix, so it is
 * surprising that it does in Windows.
 *
 *
 */


// Get "_daylight: has bad storage class" in time.h
#define	_INC_TIME

#include <sys/types.h>
#include <sys/mod.h>
#include <ntddk.h>

// #include <ntddstor.h>
#include <storport.h>
#include <fltKernel.h>

#include <sys/zfs_context.h>
#include <sys/wzvol.h>

#include <sys/spa.h>
#include <sys/zfs_rlock.h>
#include <sys/dataset_kstats.h>
#include <sys/zil.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>
#include <sys/zvol_os.h>

#include <sys/openzvol.h>

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD OpenZFS_Fini;
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, OpenZFS_Fini)

int zfs_flags = 0;

#ifdef __clang__
// #error "This file should be compiled with MSVC not Clang"
#endif

PDRIVER_OBJECT Storport_DriverObject = NULL;
static PDRIVER_OBJECT OpenZVOL_DriverObject = NULL;
static PDRIVER_OBJECT StopUnload_DriverObject = NULL;
static HANDLE g_PinHandle = NULL;
extern pHW_HBA_EXT STOR_HBAExt;

// Only if we call DEREGISTER should we delete the
// devices. If PnpMgr is doing it, we should not.
static uint64_t Deregister_Unload = 0ULL;

boolean_t Storport_Unloaded = FALSE;

extern int zvol_start(PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING pRegistryPath);
extern int initDbgCircularBuffer(void);
extern int finiDbgCircularBuffer(void);

int (*pzvol_os_read_zv)(zvol_state_t *zv, zfs_uio_t *uio,
    int flags) = NULL;
int (*pzvol_os_write_zv)(zvol_state_t *zv, zfs_uio_t *uio,
    int flags) = NULL;
int (*pzvol_os_unmap)(zvol_state_t *zv, uint64_t off,
    uint64_t bytes) = NULL;

NTSTATUS NTAPI IoCreateDriver(
    _In_opt_ PUNICODE_STRING 	DriverName,
    _In_ PDRIVER_INITIALIZE 	InitializationFunction
);

/*
 * These things are here cos we have cheated with the
 * SPL / ZFS barrier. SPL should never refer to ZFS
 * but here we are. Eventually we will clean up this
 * lazy code
 */

wchar_t zfs_vdev_protection_filter[ZFS_MODULE_STRMAX] = { L"\0" };
_Atomic uint64_t spl_lowest_vdev_disk_stack_remaining = 0;
_Atomic uint64_t spl_lowest_zvol_stack_remaining = 0;

void
spl_set_arc_no_grow(int i)
{
}

void
zfs_inactive(void *vp, void *cr, void *ct)
{
}

int
zfs_vnop_reclaim(void *vp)
{
	return (0);
}

/* end of shoddy */

// file-scope

// Call at PASSIVE_LEVEL after your adapter is up (e.g. after HwInitialize)
NTSTATUS
PinMyAdapterWithPdoName(_In_ PVOID HwDeviceExtension)
{
	PDEVICE_OBJECT Fdo = NULL, Pdo = NULL, Ldo = NULL;

	// NOTE: 1st = FDO, 2nd = PDO, 3rd = LDO
	ULONG got = StorPortGetDeviceObjects(HwDeviceExtension, (PVOID)&Fdo,
	    (PVOID)&Pdo, (PVOID)&Ldo);
	if (got != 0 || Pdo == NULL)
		return (STATUS_UNSUCCESSFUL);

	// Query PDO name: "\Device\00xxxx"
	ULONG need = 0;
	NTSTATUS st = IoGetDeviceProperty(Pdo,
	    DevicePropertyPhysicalDeviceObjectName,
	    0, NULL, &need);

	if (st != STATUS_BUFFER_TOO_SMALL || need < sizeof (WCHAR))
		return (NT_ERROR(st) ? st : STATUS_UNSUCCESSFUL);

	WCHAR *buf = ExAllocatePoolWithTag(PagedPool, need, 'lvOP');
	if (!buf)
		return (STATUS_INSUFFICIENT_RESOURCES);

	st = IoGetDeviceProperty(Pdo,
	    DevicePropertyPhysicalDeviceObjectName,
	    need, buf, &need);

	if (!NT_SUCCESS(st)) {
		ExFreePoolWithTag(buf, 'lvOP');
		return (st);
	}

	UNICODE_STRING pdoName;
	RtlInitUnicodeString(&pdoName, buf);

	OBJECT_ATTRIBUTES oa;
	InitializeObjectAttributes(&oa, &pdoName,
	    OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
	    NULL, NULL);

	IO_STATUS_BLOCK iosb;
	HANDLE h = NULL;
	st = ZwCreateFile(&h,
	    FILE_READ_ATTRIBUTES | SYNCHRONIZE,
	    &oa, &iosb, NULL,
	    FILE_ATTRIBUTE_NORMAL,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	    FILE_OPEN,
	    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
	    NULL, 0);

	ExFreePoolWithTag(buf, 'lvOP');

	if (NT_SUCCESS(st)) {
		// hold this to cause PNP_VetoOutstandingOpen
		// which stops module unloading.
		g_PinHandle = h;
	}

	return (st);
}

void
UnpinMyAdapter(void)
{
	if (g_PinHandle) {
		HANDLE h = g_PinHandle;
		g_PinHandle = NULL;
		ZwClose(h);
	}
}

NTSTATUS
ControlDeviceIoctlHandler(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	zvol_state_t *zv = NULL;
	void *addr;

	switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
	case OPENZVOL_REGISTER:
		dprintf("OPENZVOL_REGISTER\n");

		zvol_api_t *api = (zvol_api_t *)Irp->AssociatedIrp.SystemBuffer;

		if (irpSp->Parameters.DeviceIoControl.InputBufferLength !=
		    sizeof (zvol_api_t) || api == NULL) {
			dprintf("Received IOCTL with invalid size or buffer\n");
			status = STATUS_INVALID_PARAMETER;
			goto out;
		}

		// All functions must be set
		if (api->zvol_os_read_zv != NULL &&
		    api->zvol_os_write_zv != NULL &&
		    api->zvol_os_unmap != NULL) {
			pzvol_os_read_zv = api->zvol_os_read_zv;
			pzvol_os_write_zv = api->zvol_os_write_zv;
			pzvol_os_unmap = api->zvol_os_unmap;

			// Lock this driver until deregistered
			ObReferenceObject(DeviceObject);
			status = PinMyAdapterWithPdoName(STOR_HBAExt);
			if (status)
				dprintf("PinMyAdapterWithPdoName failed: %x\n",
				    status);

		}

		status = STATUS_SUCCESS;
		break;

	case OPENZVOL_DEREGISTER:
		dprintf("OPENZVOL_DEREGISTER\n");
		if (pzvol_os_read_zv != NULL) {
			pzvol_os_read_zv = NULL;
			pzvol_os_write_zv = NULL;
			pzvol_os_unmap = NULL;

			// Unlock this driver
			ObDereferenceObject(DeviceObject);
			UnpinMyAdapter();

			atomic_inc_64(&Deregister_Unload);

			OpenZVOLUnloadRoutine(OpenZVOL_DriverObject);
		}
		status = STATUS_SUCCESS;
		break;

	case OPENZVOL_ASSIGN_TARGETID:
		dprintf("OPENZVOL_ASSIGN_TARGETID\n");
		addr = Irp->AssociatedIrp.SystemBuffer;
		if (irpSp->Parameters.DeviceIoControl.InputBufferLength !=
		    sizeof (zvol_state_t *) || addr == NULL) {
			dprintf("Received IOCTL with invalid size or buffer\n");
			status = STATUS_INVALID_PARAMETER;
			goto out;
		}
		// zv is a pointer to a pointer, so we need to dereference it
		zv = *((zvol_state_t **)addr);
		wzvol_assign_targetid(zv);
		status = STATUS_SUCCESS;
		break;
	case OPENZVOL_CLEAR_TARGETID:
		dprintf("OPENZVOL_CLEAR_TARGETID\n");
		addr = Irp->AssociatedIrp.SystemBuffer;
		if (irpSp->Parameters.DeviceIoControl.InputBufferLength !=
		    sizeof (zvol_state_t *) || addr == NULL) {
			dprintf("Received IOCTL with invalid size or buffer\n");
			status = STATUS_INVALID_PARAMETER;
			goto out;
		}
		// zv is a pointer to a pointer, so we need to dereference it
		zv = *((zvol_state_t **)addr);
		wzvol_clear_targetid(zv->zv_zso->zso_target_id,
		    zv->zv_zso->zso_lun_id, zv);
		status = STATUS_SUCCESS;
		break;
	case OPENZVOL_ANNOUNCE_BUSCHANGE:
		dprintf("OPENZVOL_ANNOUNCE_BUSCHANGE\n");
		wzvol_announce_buschange();
		status = STATUS_SUCCESS;
		break;

	default:
		dprintf("Received unknown IOCTL 0x%08x\n",
		    irpSp->Parameters.DeviceIoControl.IoControlCode);
	}
out:
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return (status);
}

NTSTATUS
OpenZVOLCreateCloseCleanUp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

	// Hold driver so we don't Unload before IRP_MJ_CLOSE
	// has called us.
	switch (irpSp->MajorFunction) {
	case IRP_MJ_CREATE:
		// Not needed actually.
		ObReferenceObject(DeviceObject);
		break;
	case IRP_MJ_CLOSE:
		ObDereferenceObject(DeviceObject);
		break;
	default:
		break;
	}

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return (STATUS_SUCCESS);
}

NTSTATUS
OpenZVOLNoCall(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return (STATUS_SUCCESS);
}

void
OpenZVOLUnloadRoutine(IN PDRIVER_OBJECT DriverObject)
{
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "Deleting /Device/OpenZVOL\n"));

	if (DriverObject == NULL) {
		Storport_Unloaded = TRUE;
		DriverObject = OpenZVOL_DriverObject;
	}

	// Only proceed once Storport has unloaded and no IO is in progress.
	if (!Storport_Unloaded || pzvol_os_read_zv != NULL)
		return;

	Storport_Unloaded = FALSE;

	UNICODE_STRING symbolicLinkName;
	RtlInitUnicodeString(&symbolicLinkName, SYMBOLIC_LINK_NAME);

	UNICODE_STRING symbolicLinkName2;
	RtlInitUnicodeString(&symbolicLinkName2, STOP_UNLOAD_LINK_NAME);

	// Delete the symbolic link
	IoDeleteSymbolicLink(&symbolicLinkName);
	IoDeleteSymbolicLink(&symbolicLinkName2);

	if (StopUnload_DriverObject != NULL) {
		IoDeleteDevice(StopUnload_DriverObject->DeviceObject);
		StopUnload_DriverObject = NULL;
	}
	if (OpenZVOL_DriverObject != NULL) {
		IoDeleteDevice(OpenZVOL_DriverObject->DeviceObject);
		OpenZVOL_DriverObject = NULL;
	}

	finiDbgCircularBuffer();
}

NTSTATUS
OpenZVOLDriverInitialize(IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status;
	UNICODE_STRING deviceName;
	UNICODE_STRING symbolicLinkName;

	OpenZVOL_DriverObject = DriverObject;

	RtlInitUnicodeString(&deviceName, DEVICE_NAME);
	RtlInitUnicodeString(&symbolicLinkName, SYMBOLIC_LINK_NAME);

	PDEVICE_OBJECT deviceObject;
	status = IoCreateDevice(DriverObject, 0, &deviceName,
	    FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);

	if (!NT_SUCCESS(status))
		return (status);

	status = IoCreateSymbolicLink(&symbolicLinkName, &deviceName);

	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(deviceObject);
		return (status);
	}

	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
		DriverObject->MajorFunction[i] = OpenZVOLNoCall;

	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
	    ControlDeviceIoctlHandler;
	DriverObject->MajorFunction[IRP_MJ_CREATE] =
	    OpenZVOLCreateCloseCleanUp;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] =
	    OpenZVOLCreateCloseCleanUp;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] =
	    OpenZVOLCreateCloseCleanUp;
	DriverObject->DriverUnload = OpenZVOLUnloadRoutine;
	deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "Created /Device/OpenZVOL\n"));

	return (STATUS_SUCCESS);
}

NTSTATUS
StopUnload_Relay(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);

	// dprintf("%s: Major %d Minor %d\n",
	//    __func__, irpStack->MajorFunction, irpStack->MinorFunction);

	// The device is not attached, complete the IRP and return an error
	Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
	Irp->IoStatus.Information = 0;  // No data has been processed
	IoCompleteRequest(Irp, IO_NO_INCREMENT);  // Complete the IRP
	return (STATUS_INVALID_DEVICE_REQUEST);
}

NTSTATUS
StopUnloadDriver(IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status;
	UNICODE_STRING deviceName;
	UNICODE_STRING symbolicLinkName;

	StopUnload_DriverObject = DriverObject;

	RtlInitUnicodeString(&deviceName, STOP_UNLOAD_NAME);
	RtlInitUnicodeString(&symbolicLinkName, STOP_UNLOAD_LINK_NAME);

	PDEVICE_OBJECT deviceObject;
	status = IoCreateDevice(DriverObject, 0, &deviceName,
	    FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);

	if (!NT_SUCCESS(status))
		return (status);

	status = IoCreateSymbolicLink(&symbolicLinkName, &deviceName);

	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(deviceObject);
		return (status);
	}

	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = NULL;
		if (Storport_DriverObject->MajorFunction[i] != NULL)
			DriverObject->MajorFunction[i] = StopUnload_Relay;
	}

	deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "Created /Device/StopUnload\n"));

	return (STATUS_SUCCESS);
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING pRegistryPath)
{
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZVOL: DriverEntry\n"));

	Storport_DriverObject = DriverObject;

	initDbgCircularBuffer();

	dprintf("OpenZVOL: DriverEntry\n");

	// Init storport
	zvol_start(DriverObject, pRegistryPath);

	/*
	 * So I have had a difficult time trying to stop Storport from
	 * Unloading so eagerly. Even when disks are in use and mounted,
	 * it just unloads if asked. So we create this fake Device here
	 * which will open the PDO "\Devices\000091" (or similar name)
	 * of storport created device.
	 * This will consider the device busy, and will be vetod for
	 * unload, before even asking storport to unload. We close the
	 * open descriptor once OpenZFS has deregistered, and it can
	 * unload.
	 */
	NTSTATUS status = IoCreateDriver(NULL,
	    &StopUnloadDriver);
	if (!NT_SUCCESS(status)) {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "Failed to create StopUnloadDriver: %08X\n",
		    status));
	}

	// Init minimal device object for communication last,
	// so REGISTER comes last.
	status = IoCreateDriver(NULL,
	    &OpenZVOLDriverInitialize);
	if (!NT_SUCCESS(status)) {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "Failed to create OpenZVOL helper driver: %08X\n",
		    status));
	}

	dprintf("OpenZVOL: DriverEntry completed.\n");
	return (STATUS_SUCCESS);
}

/* storport has its own Unload, so we can't */
void
OpenZVOL_Fini(PDRIVER_OBJECT DriverObject)
{
	(void) DriverObject;
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "OpenZVOL_Fini\n"));

	/*
	 * Storport is unloading.  Tear down the \Device\OpenZVOL communication
	 * device before our code pages are freed.  Without this, the
	 * OpenZFS.sys polling thread (zvol_os_wait_openzvol) can call
	 * IoGetDeviceObjectPointer, find the device still in the namespace,
	 * and have the kernel dispatch an IRP into our freed code — which
	 * produces a 0xCE bugcheck.
	 */
	OpenZVOLUnloadRoutine(NULL);

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZVOL: Goodbye.\n"));
}
