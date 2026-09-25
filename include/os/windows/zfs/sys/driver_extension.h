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
