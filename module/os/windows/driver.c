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
#include <storport.h>

#include <sys/taskq.h>
#include <sys/wzvol.h>

#include "Trace.h"

DRIVER_INITIALIZE DriverEntry;

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
extern void sysctl_os_fini(void);


#ifdef __clang__
#error "This file should be compiled with MSVC not Clang"
#endif

PDRIVER_OBJECT WIN_DriverObject = NULL;
PDRIVER_UNLOAD STOR_DriverUnload = NULL;
PDRIVER_DISPATCH STOR_MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];

wzvolDriverInfo STOR_wzvolDriverInfo;

DRIVER_UNLOAD OpenZFS_Fini;

extern _Function_class_(WORKER_THREAD_ROUTINE)
void __stdcall
sysctl_os_registry_change(PVOID Parameter);


void
OpenZFS_Fini(PDRIVER_OBJECT DriverObject)
{
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "OpenZFS_Fini\n"));

	zfs_vfsops_fini();

	if (STOR_DriverUnload != NULL) {
		STOR_DriverUnload(DriverObject);
		STOR_DriverUnload = NULL;
	}

	zfs_kmod_fini();

	system_taskq_fini();

	sysctl_os_fini();

	spl_stop();
	finiDbgCircularBuffer();

	if (STOR_wzvolDriverInfo.zvContextArray) {
		ExFreePoolWithTag(STOR_wzvolDriverInfo.zvContextArray,
		    MP_TAG_GENERAL);
		STOR_wzvolDriverInfo.zvContextArray = NULL;
	}
	ZFSWppCleanup(DriverObject);
}

/*
 * Setup a Storage Miniport Driver, used only by ZVOL to create virtual disks.
 */
NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING pRegistryPath)
{
	NTSTATUS status;
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
	    "OpenZFS: DriverEntry\n"));

	ZFSWppInit(DriverObject, pRegistryPath);

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
	    "OpenZFS: DriverEntry\n"));

	// Setup global so zfs_ioctl.c can setup devnode
	WIN_DriverObject = DriverObject;

	/* Setup print buffer, since we print from SPL */
	initDbgCircularBuffer();

#ifdef DBG
	// DEBUG build, let's be noisy by default.
	extern int zfs_flags;
	zfs_flags |= 1;
#endif

	spl_start(pRegistryPath);

	sysctl_os_init(pRegistryPath);

	system_taskq_init();

	/*
	 * Initialise storport for the ZVOL virtual disks. This also
	 * sets the Driver Callbacks, so we make a copy of them, so
	 * that Dispatcher can use them.
	 */
	status = zvol_start(DriverObject, pRegistryPath);

	if (STATUS_SUCCESS != status) {
		/* If we failed, we carryon without ZVOL support. */
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "OpenZFS: StorPortInitialize() failed, no ZVOL. %d/0x%x\n",
		    status, status));
		memset(STOR_MajorFunction, 0, sizeof (STOR_MajorFunction));
		STOR_DriverUnload = NULL;
	} else {
		/* Make a copy of the Driver Callbacks for miniport */
		memcpy(STOR_MajorFunction, WIN_DriverObject->MajorFunction,
		    sizeof (STOR_MajorFunction));
		STOR_DriverUnload = WIN_DriverObject->DriverUnload;
	}

	/* Now set the Driver Callbacks to dispatcher and start ZFS */
	WIN_DriverObject->DriverUnload = OpenZFS_Fini;

	/* Start ZFS itself */
	zfs_kmod_init();

	/* Register fs with Win */
	zfs_vfsops_init();

	/* Start monitoring Registry for changes */
	sysctl_os_registry_change(pRegistryPath);

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZFS: Started\n"));
	return (STATUS_SUCCESS);
}
