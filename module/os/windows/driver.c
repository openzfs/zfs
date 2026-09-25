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

// Get "_daylight: has bad storage class" in time.h
#define	_INC_TIME


#include <sys/kstat.h>

#include <ntddk.h>
// #include <ntddstor.h>
#include <storport.h>

#include <sys/taskq.h>
#include <sys/wzvol.h>
#include <sys/mount.h>
#include <sys/random.h>
#include <sys/driver_extension.h>

#include "Trace.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD OpenZFS_Fini;
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, OpenZFS_Fini)

extern int initDbgCircularBuffer(void);
extern int finiDbgCircularBuffer(void);
extern int spl_start(PUNICODE_STRING RegistryPath);
extern int spl_stop(void);
extern int zfs_start(void);
extern void zfs_stop(void);
extern void windows_delay(int ticks);
extern int zfs_vfsops_init(void);
extern int zfs_vfsops_fini(void);
extern int zfs_kmod_init(void);
extern void zfs_kmod_fini(void);

extern void sysctl_os_init(PUNICODE_STRING RegistryPath);
extern void sysctl_os_all(PUNICODE_STRING RegistryPath);
extern void sysctl_os_fini(void);

extern int  zvol_os_register_module(void);
extern void zvol_os_deregister_module(void);
extern void saveBuffer(void);

#ifdef __clang__
#error "This file should be compiled with MSVC not Clang"
#endif

PDRIVER_OBJECT WIN_DriverObject = NULL;

extern _Function_class_(IO_WORKITEM_ROUTINE)
void __stdcall
sysctl_os_registry_change(DEVICE_OBJECT *DeviceObject,
    PVOID Parameter);

void
OpenZFS_Fini(PDRIVER_OBJECT DriverObject)
{
	ZFS_DRIVER_EXTENSION(DriverObject, DriverExtension);

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "OpenZFS_Fini\n"));

	sysctl_os_fini();

	zfs_unload_stage_2();

	// Called from zfs_ioc_unregister_fs
	// zvol_os_deregister_module();

	zfs_vfsops_fini();

	zfs_kmod_fini();

	system_taskq_fini();

	spl_stop();

	saveBuffer();

#ifdef DBG
	if (KD_DEBUGGER_ENABLED && !KD_DEBUGGER_NOT_PRESENT) {
		xprintf("Breaking into debugger after unload\n");
		DbgBreakPoint();
	}
#endif
	finiDbgCircularBuffer();

	ZFSWppCleanup(DriverObject);

	// IoFreeDriverObjectExtension(DriverObject);

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZFS: Goodbye.\n"));
}

/*
 * Setup a Storage Miniport Driver, used only by ZVOL to create virtual disks.
 */
NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING pRegistryPath)
{
	NTSTATUS status = -1;

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
	    "OpenZFS: DriverEntry\n"));

	if (zfs_init_driver_extension(DriverObject))
		return (STATUS_INSUFFICIENT_RESOURCES);

	ZFS_DRIVER_EXTENSION(DriverObject, DriverExtension);

	ZFSWppInit(DriverObject, pRegistryPath);

	// Setup global so zfs_ioctl.c can setup devnode
	WIN_DriverObject = DriverObject;

	/* Setup print buffer, since we print from SPL */
	initDbgCircularBuffer();

#ifdef DBG
	// DEBUG build - dprintf controlled via zfs_flags / ZFS_DEBUG_DPRINTF.
	// Default is off; set zfs_flags |= 1 at runtime to enable.
#endif

	sysctl_os_init(pRegistryPath);
	// Process Registry once before init.
	sysctl_os_all(pRegistryPath);

	spl_start(pRegistryPath);

	system_taskq_init();

	/*
	 * Initialise storport for the ZVOL virtual disks. This also
	 * sets the Driver Callbacks, so we make a copy of them, so
	 * that Dispatcher can use them.
	 */

	/* Now set the Driver Callbacks to dispatcher and start ZFS */
	WIN_DriverObject->DriverUnload = OpenZFS_Fini;

	/* Start ZFS itself */
	zfs_kmod_init();

	/* Register fs with Win */
	zfs_vfsops_init();

	/* Start monitoring Registry for changes */
	if (DriverExtension->fsDiskDeviceObject)
		sysctl_os_registry_change(DriverExtension->fsDiskDeviceObject,
		    pRegistryPath);

	zvol_os_register_module();

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZFS: Started\n"));

	return (STATUS_SUCCESS);
}
