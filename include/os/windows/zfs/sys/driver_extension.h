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

/*
 * Copyright (c) 2024 by Jorgen Lundman <lundman@lundman.net>.
 */

#ifndef SYS_DRIVER_EXTENSION_H
#define	SYS_DRIVER_EXTENSION_H

struct OpenZFS_Driver_Extension_s {
	PDEVICE_OBJECT PhysicalDeviceObject; // AddDevice
	PDEVICE_OBJECT LowerDeviceObject; // Attached
	PDEVICE_OBJECT FunctionalDeviceObject; // OpenZFS_bus
	PDEVICE_OBJECT ioctlDeviceObject;  // /dev/zfs pdo
	PDEVICE_OBJECT fsDiskDeviceObject; // /dev/zfs vdo
	boolean_t Unload_Module;
};

typedef struct OpenZFS_Driver_Extension_s OpenZFS_Driver_Extension;

#define	ZFS_DRIVER_EXTENSION(DO, V) \
    OpenZFS_Driver_Extension *(V) = \
	(OpenZFS_Driver_Extension *) IoGetDriverObjectExtension((DO), (DO));

extern int
zfs_init_driver_extension(PDRIVER_OBJECT);

extern void zfs_unload_stage_1(void);
extern void zfs_unload_stage_2(void);

#endif
