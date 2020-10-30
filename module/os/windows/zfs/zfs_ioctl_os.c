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
 * Copyright (c) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/fm/util.h>
#include <sys/dsl_crypt.h>
#include <sys/zfs_windows.h>

#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_ioctl_compat.h>
#include <sys/zvol_os.h>
#include <sys/kstat_windows.h>

#include <zfs_gitrev.h>

#include <Wdmsec.h>

// extern void zfs_windows_vnops_callback(PDEVICE_OBJECT deviceObject);

int zfs_major			= 0;
int zfs_bmajor			= 0;
static void *zfs_devnode 	= NULL;
#define	ZFS_MAJOR		-24

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_vfs != NULL);
}

int
zfs_vfs_ref(zfsvfs_t **zfvp)
{
	int error = 0;

	if (*zfvp == NULL || (*zfvp)->z_vfs == NULL)
		return (SET_ERROR(ESRCH));

	error = vfs_busy((*zfvp)->z_vfs, LK_NOWAIT);
	if (error != 0) {
		*zfvp = NULL;
		error = SET_ERROR(ESRCH);
	}
	return (error);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	vfs_unbusy(zfsvfs->z_vfs);
}

static uint_t zfsdev_private_tsd;

static int
zfsdev_state_init(dev_t dev)
{
	zfsdev_state_t *zs, *zsprev = NULL;
	minor_t minor;
	boolean_t newzs = B_FALSE;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	minor = minor(dev);
	if (minor == 0)
		return (SET_ERROR(ENXIO));

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {
		if (zs->zs_minor == -1)
			break;
		zsprev = zs;
	}

	if (!zs) {
		zs = kmem_zalloc(sizeof (zfsdev_state_t), KM_SLEEP);
		newzs = B_TRUE;
	}

	/* Store this dev_t in tsd, so zfs_get_private() can retrieve it */
	tsd_set(zfsdev_private_tsd, (void *)(uintptr_t)dev);

	zfs_onexit_init((zfs_onexit_t **)&zs->zs_onexit);
	zfs_zevent_init((zfs_zevent_t **)&zs->zs_zevent);

	/*
	 * In order to provide for lock-free concurrent read access
	 * to the minor list in zfsdev_get_state_impl(), new entries
	 * must be completely written before linking them into the
	 * list whereas existing entries are already linked; the last
	 * operation must be updating zs_minor (from -1 to the new
	 * value).
	 */
	if (newzs) {
		zs->zs_minor = minor;
		zsprev->zs_next = zs;
	} else {
		zs->zs_minor = minor;
	}

	return (0);
}

dev_t
zfsdev_get_dev(void)
{
	return ((dev_t)tsd_get(zfsdev_private_tsd));
}

static int
zfsdev_state_destroy(dev_t dev)
{
	zfsdev_state_t *zs;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	tsd_set(zfsdev_private_tsd, NULL);

	zs = zfsdev_get_state(minor(dev), ZST_ALL);

	if (!zs) {
		dprintf("%s: no cleanup for minor x%x\n", __func__,
		    minor(dev));
		return (0);
	}

	ASSERT(zs != NULL);
	if (zs->zs_minor != -1) {
		zs->zs_minor = -1;
		zfs_onexit_destroy(zs->zs_onexit);
		zfs_zevent_destroy(zs->zs_zevent);
		zs->zs_onexit = NULL;
		zs->zs_zevent = NULL;
	}
	return (0);
}

static NTSTATUS
zfsdev_open(dev_t dev, PIRP Irp)
{
	int error;
	int flags = 0;
	int devtype = 0;
	struct proc *p = current_proc();
	PAGED_CODE();

	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_get_state(minor(dev), ZST_ALL)) {
		mutex_exit(&zfsdev_state_lock);
		return (0);
	}
	error = zfsdev_state_init(dev);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static NTSTATUS
zfsdev_release(dev_t dev, PIRP Irp)
{
	int error;
	int flags = 0;
	int devtype = 0;
	struct proc *p = current_proc();
	PAGED_CODE();

	mutex_enter(&zfsdev_state_lock);
	error = zfsdev_state_destroy(dev);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static NTSTATUS
zfsdev_ioctl(PDEVICE_OBJECT DeviceObject, PIRP Irp, int flag)
{
	uint_t len, vecnum;
	zfs_iocparm_t zit;
	zfs_cmd_t *zc;
	int error, rc;
	user_addr_t uaddr;
	u_long cmd = 0;
	caddr_t arg = NULL;

	PIO_STACK_LOCATION  irpSp;
	irpSp = IoGetCurrentIrpStackLocation(Irp);

	len = irpSp->Parameters.DeviceIoControl.InputBufferLength;
	cmd = irpSp->Parameters.DeviceIoControl.IoControlCode;
	arg = irpSp->Parameters.DeviceIoControl.Type3InputBuffer;

	vecnum = cmd - CTL_CODE(ZFSIOCTL_TYPE, ZFSIOCTL_BASE, METHOD_NEITHER, FILE_ANY_ACCESS);

	if (len != sizeof (zfs_iocparm_t)) {
		/*
		 * printf("len %d vecnum: %d sizeof (zfs_cmd_t) %lu\n",
		 *  len, vecnum, sizeof (zfs_cmd_t));
		 */
		return (EINVAL);
	}

	// Copy in the wrapper, which contains real zfs_cmd_t addr, len, and compat version
	error = ddi_copyin((void *)arg, &zit, len, 0);
	if (error != 0) {
		return (EINVAL);
	}

	uaddr = (user_addr_t)zit.zfs_cmd;

	// get ready for zfs_cmd_t
	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	if (copyin(uaddr, zc, sizeof (zfs_cmd_t))) {
		error = SET_ERROR(EFAULT);
		goto out;
	}

	error = zfsdev_ioctl_common(vecnum, zc, 0);

	rc = copyout(zc, uaddr, sizeof (zfs_cmd_t));

	if (error == 0 && rc != 0)
		error = -SET_ERROR(EFAULT);

	// Set the real return code in struct.
	// XNU only calls copyout if error=0, but
	// presumably we can skip that in Windows and just return? 
	zit.zfs_ioc_error = error;
	error = ddi_copyout(&zit, (void *)arg, len, 0);
	error = 0;

out:
	kmem_free(zc, sizeof (zfs_cmd_t));
	return (error);

}

/*
* inputs:
* zc_name		dataset name to mount
* zc_value	path location to mount
*
* outputs:
* return code
*/
int zfs_windows_mount(zfs_cmd_t *zc);  // move me to headers

static int
zfs_ioc_mount(zfs_cmd_t *zc)
{
	return zfs_windows_mount(zc);
}

/*
* inputs:
* zc_name		dataset name to unmount
* zc_value	path location to unmount
*
* outputs:
* return code
*/
int zfs_windows_unmount(zfs_cmd_t *zc); // move me to headers

static int
zfs_ioc_unmount(zfs_cmd_t *zc)
{
	dprintf("%s: enter\n", __func__);
	return zfs_windows_unmount(zc);
}

void
zfs_ioctl_init_os(void)
{
	/*
	* Windows functions
	*/
	zfs_ioctl_register_legacy(ZFS_IOC_MOUNT, zfs_ioc_mount,
		zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_legacy(ZFS_IOC_UNMOUNT, zfs_ioc_unmount,
		zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);

}


/* ioctl handler for block device. Relay to zvol */
static int
zfsdev_bioctl(dev_t dev, ulong_t cmd, caddr_t data,
    __unused int flag, struct proc *p)
{
	return (zvol_os_ioctl(dev, cmd, data, 1, NULL, NULL));
}

// Callback to print registered filesystems. Not needed
VOID  DriverNotificationRoutine(_In_ struct _DEVICE_OBJECT *DeviceObject,
    _In_ BOOLEAN               FsActive)
{
	CHAR nibuf[512];        // buffer that receives name information and name
	POBJECT_NAME_INFORMATION name_info = (POBJECT_NAME_INFORMATION)nibuf;
	ULONG ret_len;
	NTSTATUS status;

	status = ObQueryNameString(DeviceObject, name_info, sizeof(nibuf), &ret_len);
	if (NT_SUCCESS(status)) {
		dprintf("Filesystem %p: '%wZ'\n", DeviceObject, name_info);
	} else {
		dprintf("Filesystem %p: '%wZ'\n", DeviceObject, DeviceObject->DriverObject->DriverName);
	}
}

// extern PDRIVER_UNLOAD STOR_DriverUnload;
uint64_t
zfs_ioc_unregister_fs(void) 
{
	dprintf("%s\n", __func__);
	if (zfs_module_busy != 0) {
		dprintf("%s: datasets still busy: %llu pool(s)\n", __func__, zfs_module_busy);
		return zfs_module_busy;
	}
	if (fsDiskDeviceObject != NULL) {
		IoUnregisterFsRegistrationChange(WIN_DriverObject, DriverNotificationRoutine);
		IoUnregisterFileSystem(fsDiskDeviceObject);
		ObDereferenceObject(fsDiskDeviceObject);
		IoDeleteDevice(fsDiskDeviceObject);
		fsDiskDeviceObject = NULL;
		ObDereferenceObject(ioctlDeviceObject);
		IoDeleteDevice(ioctlDeviceObject);
		ioctlDeviceObject = NULL;
	}
#if 0
	// Do not unload these, so that the zfsinstaller uninstall can
	// find the devnode to trigger uninstall.
	if (STOR_DriverUnload != NULL) {
		STOR_DriverUnload(WIN_DriverObject);
		STOR_DriverUnload = NULL;
	}
#endif
	return 0;
}


int
zfsdev_attach(void)
{
	NTSTATUS ntStatus;
	UNICODE_STRING  ntUnicodeString;    // NT Device Name
	UNICODE_STRING ntWin32NameString; // Win32 Name 

	mutex_init(&zfsdev_state_lock, NULL, MUTEX_DEFAULT, NULL);
	zfsdev_state_list = kmem_zalloc(sizeof(zfsdev_state_t), KM_SLEEP);
	zfsdev_state_list->zs_minor = -1;

	static UNICODE_STRING sddl = RTL_CONSTANT_STRING(
		L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGWGX;;;WD)(A;;GRGX;;;RC)");
	// Or use &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R

	RtlInitUnicodeString(&ntUnicodeString, ZFS_DEV_KERNEL);
	ntStatus = IoCreateDeviceSecure(
		WIN_DriverObject,                   // Our Driver Object
		sizeof(mount_t),
		&ntUnicodeString,               // Device name "\Device\SIOCTL"
		FILE_DEVICE_UNKNOWN,  // Device type
		/*FILE_DEVICE_SECURE_OPEN*/ 0,     // Device characteristics
		FALSE,                          // Not an exclusive device
		&sddl,
		NULL,
		&ioctlDeviceObject);                // Returned ptr to Device Object

	if (!NT_SUCCESS(ntStatus)) {
		dprintf("ZFS: Couldn't create the device object /dev/zfs (%wZ)\n", ZFS_DEV_KERNEL);
		return ntStatus;
	}
	dprintf("ZFS: created kernel device node: %p: name %wZ\n", ioctlDeviceObject, ZFS_DEV_KERNEL);


	UNICODE_STRING fsDiskDeviceName;
	RtlInitUnicodeString(&fsDiskDeviceName, ZFS_GLOBAL_FS_DISK_DEVICE_NAME);

	ntStatus = IoCreateDeviceSecure(WIN_DriverObject,      // DriverObject
		sizeof(mount_t),      // DeviceExtensionSize
		&fsDiskDeviceName, // DeviceName
		FILE_DEVICE_DISK_FILE_SYSTEM, // DeviceType
		0,                    // DeviceCharacteristics
		FALSE,                // Not Exclusive
		&sddl,                // Default SDDL String
		NULL,                 // Device Class GUID
		&fsDiskDeviceObject); // DeviceObject


	ObReferenceObject(ioctlDeviceObject);

	mount_t *dgl;
	dgl = ioctlDeviceObject->DeviceExtension;
	dgl->type = MOUNT_TYPE_DGL;
	dgl->size = sizeof(mount_t);

	mount_t *vcb;
	vcb = fsDiskDeviceObject->DeviceExtension;
	vcb->type = MOUNT_TYPE_VCB;
	vcb->size = sizeof(mount_t);

	if (ntStatus == STATUS_SUCCESS) {
		dprintf("DiskFileSystemDevice: 0x%0x  %wZ created\n", ntStatus, &fsDiskDeviceName);
	}

	// Initialize a Unicode String containing the Win32 name
	// for our device.
	RtlInitUnicodeString(&ntWin32NameString, ZFS_DEV_DOS);

	// Create a symbolic link between our device name  and the Win32 name
	ntStatus = IoCreateSymbolicLink(
		&ntWin32NameString, &ntUnicodeString);

	if (!NT_SUCCESS(ntStatus)) {
		dprintf("ZFS: Couldn't create userland symbolic link to /dev/zfs (%wZ)\n", ZFS_DEV);
		ObDereferenceObject(ioctlDeviceObject);
		IoDeleteDevice(ioctlDeviceObject);
		return -1;
	}
	dprintf("ZFS: created userland device symlink\n");

	fsDiskDeviceObject->Flags |= DO_DIRECT_IO;
	fsDiskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	IoRegisterFileSystem(fsDiskDeviceObject);
	ObReferenceObject(fsDiskDeviceObject);

	// Set all the callbacks to "dispatch()"
	WIN_DriverObject->MajorFunction[IRP_MJ_CREATE] = (PDRIVER_DISPATCH)dispatcher;   // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_CLOSE] = (PDRIVER_DISPATCH)dispatcher;     // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_READ] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_WRITE] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_EA] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_EA] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = (PDRIVER_DISPATCH)dispatcher; // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = (PDRIVER_DISPATCH)dispatcher; // zfs_ioctl.c
	WIN_DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_CLEANUP] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_DEVICE_CHANGE] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_PNP] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_QUERY_SECURITY] = (PDRIVER_DISPATCH)dispatcher;
	WIN_DriverObject->MajorFunction[IRP_MJ_SET_SECURITY] = (PDRIVER_DISPATCH)dispatcher;

	// Dump all registered filesystems
	ntStatus = IoRegisterFsRegistrationChange(WIN_DriverObject, DriverNotificationRoutine);

	wrap_avl_init();
	wrap_unicode_init();
	wrap_nvpair_init();
	wrap_zcommon_init();
	wrap_icp_init();
	wrap_lua_init();

	tsd_create(&zfsdev_private_tsd, NULL);

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "ZFS: Loaded module %s, "
            "ZFS pool version %s, ZFS filesystem version %s\n",
            ZFS_META_GITREV,
            SPA_VERSION_STRING, ZPL_VERSION_STRING);

	return (0);
}

void
zfsdev_detach(void)
{
	zfsdev_state_t *zs, *zsprev = NULL;

	PDEVICE_OBJECT deviceObject = WIN_DriverObject->DeviceObject;
	UNICODE_STRING uniWin32NameString;

	RtlInitUnicodeString(&uniWin32NameString, ZFS_DEV_DOS);
	IoDeleteSymbolicLink(&uniWin32NameString);
	if (deviceObject != NULL) {
		ObDereferenceObject(deviceObject);
		IoDeleteDevice(deviceObject);
	}

	mutex_destroy(&zfsdev_state_lock);

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {
		if (zsprev) {
			if (zsprev->zs_minor != -1) {
				zfs_onexit_destroy(zsprev->zs_onexit);
				zfs_zevent_destroy(zsprev->zs_zevent);
			}
			kmem_free(zsprev, sizeof (zfsdev_state_t));
		}
		zsprev = zs;
	}
	if (zsprev)
		kmem_free(zsprev, sizeof (zfsdev_state_t));

	tsd_destroy(&zfsdev_private_tsd);

	wrap_lua_fini();
	wrap_icp_fini();
	wrap_zcommon_fini();
	wrap_nvpair_fini();
	wrap_unicode_fini();
	wrap_avl_fini();
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);

	return (KMALLOC_MAX_SIZE);
}
