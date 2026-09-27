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

#ifndef ZFS_CONTEXT_WIN32_OS_H_
#define	ZFS_CONTEXT_WIN32_OS_H_

#include <termios.h>

#define	ZFS_EXPORTS_PATH	"/etc/exports"
#define	MNTTYPE_ZFS_SUBTYPE ('Z'<<24|'F'<<16|'S'<<8)

struct spa_iokit;
typedef struct spa_iokit spa_iokit_t;

typedef off_t loff_t;

struct zfs_handle;

#define	noinline		__attribute__((noinline))

extern void libzfs_macos_wrapfd(int *srcfd, boolean_t send);

#define	FSCTL_ZFS_VOLUME_MOUNTPOINT CTL_CODE(FILE_DEVICE_UNKNOWN, \
    0x8ff, METHOD_BUFFERED, FILE_ANY_ACCESS)
typedef struct
{
    int len;
    WCHAR buffer[1]; // make this dynamic?
} fsctl_zfs_volume_mountpoint_t;

#endif
