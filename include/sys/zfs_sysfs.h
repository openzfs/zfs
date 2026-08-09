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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef	_SYS_ZFS_SYSFS_H
#define	_SYS_ZFS_SYSFS_H extern __attribute__((visibility("default")))

struct zfs_mod_supported_features;
struct zfs_mod_supported_features *zfs_mod_list_supported(const char *scope);
void zfs_mod_list_supported_free(struct zfs_mod_supported_features *);

#ifdef _KERNEL

void zfs_sysfs_init(void);
void zfs_sysfs_fini(void);

#else

#define	zfs_sysfs_init()
#define	zfs_sysfs_fini()

_SYS_ZFS_SYSFS_H boolean_t zfs_mod_supported(const char *, const char *,
    const struct zfs_mod_supported_features *);
#endif

#define	ZFS_SYSFS_POOL_PROPERTIES	"properties.pool"
#define	ZFS_SYSFS_VDEV_PROPERTIES	"properties.vdev"
#define	ZFS_SYSFS_DATASET_PROPERTIES	"properties.dataset"
#define	ZFS_SYSFS_KERNEL_FEATURES	"features.kernel"
#define	ZFS_SYSFS_POOL_FEATURES		"features.pool"

#define	ZFS_SYSFS_DIR			"/sys/module/zfs"
/* Alternate location when ZFS is built as part of the kernel (rare) */
#define	ZFS_SYSFS_ALT_DIR		"/sys/fs/zfs"

#endif	/* _SYS_ZFS_SYSFS_H */
