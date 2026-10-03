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

#include <sys/ldi_osx.h>

typedef struct vdev_disk {
	ldi_handle_t vd_lh;
	list_t vd_ldi_cbs;
	boolean_t vd_ldi_offline;
} vdev_disk_t;

extern int vdev_disk_ldi_physio(ldi_handle_t, caddr_t, size_t, uint64_t, int);

#endif
