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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@gmail.com>.
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#ifndef _MOD_COMPAT_H
#define	_MOD_COMPAT_H

#include <linux/module.h>
#include <linux/moduleparam.h>

/* Grsecurity kernel API change */
#ifdef MODULE_PARAM_CALL_CONST
typedef const struct kernel_param zfs_kernel_param_t;
#else
typedef struct kernel_param zfs_kernel_param_t;
#endif

#define	ZMOD_RW 0644
#define	ZMOD_RD 0444

/* BEGIN CSTYLED */
#define	INT int
#define	UINT uint
#define	ULONG ulong
#define	LONG long
#define	STRING charp
/* END CSTYLED */

enum scope_prefix_types {
	zfs,
	zfs_arc,
	zfs_condense,
	zfs_dbuf,
	zfs_dbuf_cache,
	zfs_deadman,
	zfs_dedup,
	zfs_l2arc,
	zfs_livelist,
	zfs_livelist_condense,
	zfs_lua,
	zfs_metaslab,
	zfs_mg,
	zfs_multihost,
	zfs_prefetch,
	zfs_reconstruct,
	zfs_recv,
	zfs_send,
	zfs_spa,
	zfs_trim,
	zfs_txg,
	zfs_vdev,
	zfs_vdev_cache,
	zfs_vdev_file,
	zfs_vdev_mirror,
	zfs_vnops,
	zfs_zevent,
	zfs_zio,
	zfs_zil
};

/*
 * Declare a module parameter / sysctl node
 *
 * "scope_prefix" the part of the sysctl / sysfs tree the node resides under
 *   (currently a no-op on Linux)
 * "name_prefix" the part of the variable name that will be excluded from the
 *   exported names on platforms with a hierarchical namespace
 * "name" the part of the variable that will be exposed on platforms with a
 *    hierarchical namespace, or as name_prefix ## name on Linux
 * "type" the variable type
 * "perm" the permissions (read/write or read only)
 * "desc" a brief description of the option
 *
 * Examples:
 * ZFS_MODULE_PARAM(zfs_vdev_mirror, zfs_vdev_mirror_, rotating_inc, UINT,
 * 	ZMOD_RW, "Rotating media load increment for non-seeking I/O's");
 * on FreeBSD:
 *   vfs.zfs.vdev.mirror.rotating_inc
 * on Linux:
 *   zfs_vdev_mirror_rotating_inc
 *
 * ZFS_MODULE_PARAM(zfs, , dmu_prefetch_max, UINT, ZMOD_RW,
 * 	"Limit one prefetch call to this size");
 * on FreeBSD:
 *   vfs.zfs.dmu_prefetch_max
 * on Linux:
 *   dmu_prefetch_max
 */
/* BEGIN CSTYLED */
#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc) \
	CTASSERT_GLOBAL((sizeof (scope_prefix) == sizeof (enum scope_prefix_types))); \
	module_param(name_prefix ## name, type, perm); \
	MODULE_PARM_DESC(name_prefix ## name, desc)
/* END CSTYLED */

/*
 * Declare a module parameter / sysctl node
 *
 * "scope_prefix" the part of the the sysctl / sysfs tree the node resides under
 *   (currently a no-op on Linux)
 * "name_prefix" the part of the variable name that will be excluded from the
 *   exported names on platforms with a hierarchical namespace
 * "name" the part of the variable that will be exposed on platforms with a
 *    hierarchical namespace, or as name_prefix ## name on Linux
 * "setfunc" setter function
 * "getfunc" getter function
 * "perm" the permissions (read/write or read only)
 * "desc" a brief description of the option
 *
 * Examples:
 * ZFS_MODULE_PARAM_CALL(zfs_spa, spa_, slop_shift, param_set_slop_shift,
 * 	param_get_int, ZMOD_RW, "Reserved free space in pool");
 * on FreeBSD:
 *   vfs.zfs.spa_slop_shift
 * on Linux:
 *   spa_slop_shift
 */
/* BEGIN CSTYLED */
#define	ZFS_MODULE_PARAM_CALL(scope_prefix, name_prefix, name, setfunc, getfunc, perm, desc) \
	CTASSERT_GLOBAL((sizeof (scope_prefix) == sizeof (enum scope_prefix_types))); \
	module_param_call(name_prefix ## name, setfunc, getfunc, &name_prefix ## name, perm); \
	MODULE_PARM_DESC(name_prefix ## name, desc)
/* END CSTYLED */

/*
 * As above, but there is no variable with the name name_prefix ## name,
 * so NULL is passed to module_param_call instead.
 */
/* BEGIN CSTYLED */
#define	ZFS_MODULE_VIRTUAL_PARAM_CALL(scope_prefix, name_prefix, name, setfunc, getfunc, perm, desc) \
	CTASSERT_GLOBAL((sizeof (scope_prefix) == sizeof (enum scope_prefix_types))); \
	module_param_call(name_prefix ## name, setfunc, getfunc, NULL, perm); \
	MODULE_PARM_DESC(name_prefix ## name, desc)
/* END CSTYLED */

#define	ZFS_MODULE_PARAM_ARGS	const char *buf, zfs_kernel_param_t *kp

#define	ZFS_MODULE_DESCRIPTION(s) MODULE_DESCRIPTION(s)
#define	ZFS_MODULE_AUTHOR(s) MODULE_AUTHOR(s)
#define	ZFS_MODULE_LICENSE(s) MODULE_LICENSE(s)
#define	ZFS_MODULE_VERSION(s) MODULE_VERSION(s)

#define	module_init_early(fn) module_init(fn)

#endif	/* _MOD_COMPAT_H */
