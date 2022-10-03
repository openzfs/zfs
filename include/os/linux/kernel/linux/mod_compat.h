/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

/*
 * Despite constifying struct kernel_param_ops, some older kernels define a
 * `__check_old_set_param()` function in their headers that checks for a
 * non-constified `->set()`. This has long been fixed in Linux mainline, but
 * since we support older kernels, we workaround it by using a preprocessor
 * definition to disable it.
 */
#define	__check_old_set_param(_) (0)

typedef const struct kernel_param zfs_kernel_param_t;

#define	ZMOD_RW 0644
#define	ZMOD_RD 0444

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
 * While we define our own s64/u64 types, there is no reason to reimplement the
 * existing Linux kernel types, so we use the preprocessor to remap our
 * "custom" implementations to the kernel ones. This is done because the CPP
 * does not allow us to write conditional definitions. The fourth definition
 * exists because the CPP will not allow us to replace things like INT with int
 * before string concatenation.
 */

#define	spl_param_set_int param_set_int
#define	spl_param_get_int param_get_int
#define	spl_param_ops_int param_ops_int
#define	spl_param_ops_INT param_ops_int

#define	spl_param_set_long param_set_long
#define	spl_param_get_long param_get_long
#define	spl_param_ops_long param_ops_long
#define	spl_param_ops_LONG param_ops_long

#define	spl_param_set_uint param_set_uint
#define	spl_param_get_uint param_get_uint
#define	spl_param_ops_uint param_ops_uint
#define	spl_param_ops_UINT param_ops_uint

#define	spl_param_set_ulong param_set_ulong
#define	spl_param_get_ulong param_get_ulong
#define	spl_param_ops_ulong param_ops_ulong
#define	spl_param_ops_ULONG param_ops_ulong

#define	spl_param_set_charp param_set_charp
#define	spl_param_get_charp param_get_charp
#define	spl_param_ops_charp param_ops_charp
#define	spl_param_ops_STRING param_ops_charp

int spl_param_set_s64(const char *val, zfs_kernel_param_t *kp);
extern int spl_param_get_s64(char *buffer, zfs_kernel_param_t *kp);
extern const struct kernel_param_ops spl_param_ops_s64;
#define	spl_param_ops_S64 spl_param_ops_s64

extern int spl_param_set_u64(const char *val, zfs_kernel_param_t *kp);
extern int spl_param_get_u64(char *buffer, zfs_kernel_param_t *kp);
extern const struct kernel_param_ops spl_param_ops_u64;
#define	spl_param_ops_U64 spl_param_ops_u64

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
