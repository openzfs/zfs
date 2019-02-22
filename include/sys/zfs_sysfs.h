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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef	_SYS_ZFS_SYSFS_H
#define	_SYS_ZFS_SYSFS_H

#ifdef _KERNEL

void zfs_sysfs_init(void);
void zfs_sysfs_fini(void);

#else

#define	zfs_sysfs_init()
#define	zfs_sysfs_fini()

boolean_t zfs_mod_supported(const char *, const char *);
#endif

#define	ZFS_SYSFS_POOL_PROPERTIES	"properties.pool"
#define	ZFS_SYSFS_DATASET_PROPERTIES	"properties.dataset"
#define	ZFS_SYSFS_KERNEL_FEATURES	"features.kernel"
#define	ZFS_SYSFS_POOL_FEATURES		"features.pool"

#define	ZFS_SYSFS_DIR			"/sys/module/zfs"
/* Alternate location when ZFS is built as part of the kernel (rare) */
#define	ZFS_SYSFS_ALT_DIR		"/sys/fs/zfs"

#endif	/* _SYS_ZFS_SYSFS_H */
