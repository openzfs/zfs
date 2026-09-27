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
#ifndef _ZFS_VDEV_DISK_OS_H
#define	_ZFS_VDEV_DISK_OS_H

typedef struct vdev_disk {
	ddi_devid_t	vd_devid;
	char		*vd_minor;
	list_t		vd_ldi_cbs;
	boolean_t	vd_ldi_offline;
	HANDLE		vd_lh;
	PFILE_OBJECT	vd_FileObject;
	PDEVICE_OBJECT	vd_DeviceObject;
	PDEVICE_OBJECT	vd_ExclusiveObject;
	uint64_t	vdev_win_offset; /* soft partition start */
	uint64_t	vdev_win_length; /* soft partition length */
} vdev_disk_t;

#endif
