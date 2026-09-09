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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@gmail.com>.
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#ifndef _MOD_COMPAT_H
#define	_MOD_COMPAT_H

#include <linux/module.h>
#include <linux/moduleparam.h>

typedef const struct kernel_param zfs_kernel_param_t;

#define	ZMOD_RW 0644
#define	ZMOD_RD 0444

enum scope_prefix_types {
	zfs,
	zfs_arc,
	zfs_brt,
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
	zfs_vdev_disk,
	zfs_vdev_file,
	zfs_vdev_mirror,
	zfs_vol,
	zfs_vnops,
	zfs_zevent,
	zfs_zio,
	zfs_zil
};

/*
 * Our uint64 params are called U64 in part because we had them before Linux
 * provided ULLONG param ops. Now it does, and we use them, but we retain the
 * U64 name to keep many existing tunables working without issue.
 */
#define	spl_param_set_u64	param_set_ullong
#define	spl_param_get_u64	param_get_ullong
#define	spl_param_ops_U64	param_ops_ullong

/*
 * We keep our own names for param ops to make expanding them in
 * ZFS_MODULE_PARAM easy.
 */
#define	spl_param_ops_INT	param_ops_int
#define	spl_param_ops_LONG	param_ops_long
#define	spl_param_ops_UINT	param_ops_uint
#define	spl_param_ops_ULONG	param_ops_ulong
#define	spl_param_ops_STRING	param_ops_charp

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
#define	ZFS_MODULE_PARAM(scope_prefix, name_prefix, name, type, perm, desc) \
	_Static_assert( \
	    sizeof (scope_prefix) == sizeof (enum scope_prefix_types), \
	    "" #scope_prefix " size mismatch with enum scope_prefix_types"); \
	module_param_cb(name_prefix ## name, &spl_param_ops_ ## type, \
	    &name_prefix ## name, perm); \
	MODULE_PARM_DESC(name_prefix ## name, desc)

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
#define	ZFS_MODULE_PARAM_CALL( \
    scope_prefix, name_prefix, name, setfunc, getfunc, perm, desc) \
	_Static_assert( \
	    sizeof (scope_prefix) == sizeof (enum scope_prefix_types), \
	    "" #scope_prefix " size mismatch with enum scope_prefix_types"); \
	module_param_call(name_prefix ## name, setfunc, getfunc, \
	    &name_prefix ## name, perm); \
	MODULE_PARM_DESC(name_prefix ## name, desc)

/*
 * As above, but there is no variable with the name name_prefix ## name,
 * so NULL is passed to module_param_call instead.
 */
#define	ZFS_MODULE_VIRTUAL_PARAM_CALL( \
    scope_prefix, name_prefix, name, setfunc, getfunc, perm, desc) \
	_Static_assert( \
	    sizeof (scope_prefix) == sizeof (enum scope_prefix_types), \
	    "" #scope_prefix " size mismatch with enum scope_prefix_types"); \
	module_param_call(name_prefix ## name, setfunc, getfunc, NULL, perm); \
	MODULE_PARM_DESC(name_prefix ## name, desc)

#define	ZFS_MODULE_PARAM_ARGS	const char *buf, zfs_kernel_param_t *kp

#endif	/* _MOD_COMPAT_H */
