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
 * Copyright (c) 2017, Intel Corporation. All rights reserved.
 */

#ifndef	_SYS_ZFS_PROJECT_H
#define	_SYS_ZFS_PROJECT_H

#ifndef _KERNEL
#ifndef _SYS_MOUNT_H
/* XXX: some hack to avoid include sys/mount.h */
#define	_SYS_MOUNT_H
#endif
#endif

#include <sys/vfs.h>

#ifdef FS_PROJINHERIT_FL
#define	ZFS_PROJINHERIT_FL	FS_PROJINHERIT_FL
#else
#define	ZFS_PROJINHERIT_FL	0x20000000
#endif

#ifdef FS_IOC_FSGETXATTR
typedef struct fsxattr zfsxattr_t;

#define	ZFS_IOC_FSGETXATTR	FS_IOC_FSGETXATTR
#define	ZFS_IOC_FSSETXATTR	FS_IOC_FSSETXATTR
#else
struct zfsxattr {
	uint32_t	fsx_xflags;	/* xflags field value (get/set) */
	uint32_t	fsx_extsize;	/* extsize field value (get/set) */
	uint32_t	fsx_nextents;	/* nextents field value (get)   */
	uint32_t	fsx_projid;	/* project identifier (get/set) */
	uint32_t	fsx_cowextsize;
	unsigned char	fsx_pad[8];
};
typedef struct zfsxattr zfsxattr_t;

#define	ZFS_IOC_FSGETXATTR	_IOR('X', 31, zfsxattr_t)
#define	ZFS_IOC_FSSETXATTR	_IOW('X', 32, zfsxattr_t)
#endif

#define	ZFS_DEFAULT_PROJID	(0ULL)
/*
 * It is NOT ondisk project ID value. Just means either the object has
 * no project ID or the operation does not touch project ID attribute.
 */
#define	ZFS_INVALID_PROJID	(-1ULL)

static inline boolean_t
zpl_is_valid_projid(uint32_t projid)
{
	/*
	 * zfsxattr::fsx_projid is 32-bits, when convert to uint64_t,
	 * the higher 32-bits will be set as zero, so cannot directly
	 * compare with ZFS_INVALID_PROJID (-1ULL)
	 */
	if ((uint32_t)ZFS_INVALID_PROJID == projid)
		return (B_FALSE);
	return (B_TRUE);
}

#endif	/* _SYS_ZFS_PROJECT_H */
