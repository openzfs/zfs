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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_ZFS_DEBUG_H
#define	_SYS_ZFS_DEBUG_H

#ifndef TRUE
#define	TRUE 1
#endif

#ifndef FALSE
#define	FALSE 0
#endif

/*
 * ZFS debugging - Always enabled for user space builds.
 */
#if defined(DEBUG) || !defined(_KERNEL)
#define	ZFS_DEBUG
#endif

extern int zfs_flags;
extern int zfs_recover;

#define	ZFS_DEBUG_DPRINTF	0x0001
#define	ZFS_DEBUG_DBUF_VERIFY	0x0002
#define	ZFS_DEBUG_DNODE_VERIFY	0x0004
#define	ZFS_DEBUG_SNAPNAMES	0x0008
#define	ZFS_DEBUG_MODIFY	0x0010

/*
 * Always log zfs debug messages to the spl debug subsystem as SS_USER1.
 * When the SPL is configured with debugging enabled these messages will
 * appear in the internal spl debug log, otherwise they are a no-op.
 */
#if defined(_KERNEL)

#include <spl-debug.h>
#define	dprintf(...)                                                   \
	if (zfs_flags & ZFS_DEBUG_DPRINTF)                             \
		__SDEBUG(NULL, SS_USER1, SD_DPRINTF, __VA_ARGS__)

/*
 * When zfs is running is user space the debugging is always enabled.
 * The messages will be printed using the __dprintf() function and
 * filtered based on the zfs_flags variable.
 */
#else
#define dprintf(...)                                                   \
	if (zfs_flags & ZFS_DEBUG_DPRINTF)                             \
		__dprintf(__FILE__, __func__, __LINE__, __VA_ARGS__)

#endif /* _KERNEL */

void zfs_panic_recover(const char *fmt, ...);
#define	zfs_dbgmsg(...)	dprintf(__VA_ARGS__)
void zfs_dbgmsg_init(void);
void zfs_dbgmsg_fini(void);

#endif	/* _SYS_ZFS_DEBUG_H */
