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
 * Copyright 2014 Xin Li <delphij@FreeBSD.org>.  All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>.  All rights reserved.
 * Use is subject to license terms.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_SYS_ZFS_IOCTL_COMPAT_H
#define	_SYS_ZFS_IOCTL_COMPAT_H

#include <sys/zfs_ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * FreeBSD used to have its own ZFS port descended from the illumos code,
 * before switching to OpenZFS in 13. The original port had its own versioned
 * ioctl structure, zfs_iocparm_t, which for OpenZFS wraps zfs_cmd_t and uses a
 * special version number ZFS_IOCVER_OZFS. The compatibility layer is now gone
 * but there's no particular reason to switch to using zfs_cmd_t directly for
 * its own sake, so the version number and wrapping struct are retained.
 */

#define	ZFS_IOCVER_OZFS		15

typedef struct zfs_iocparm {
	uint32_t	zfs_ioctl_version;
	uint64_t	zfs_cmd;
	uint64_t	zfs_cmd_size;
} zfs_iocparm_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOCTL_COMPAT_H */
