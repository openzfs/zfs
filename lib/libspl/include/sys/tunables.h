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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef _SYS_TUNABLES_H
#define	_SYS_TUNABLES_H extern __attribute__((visibility("hidden")))

typedef enum {
	ZFS_TUNABLE_TYPE_INT,
	ZFS_TUNABLE_TYPE_UINT,
	ZFS_TUNABLE_TYPE_ULONG,
	ZFS_TUNABLE_TYPE_U64,
	ZFS_TUNABLE_TYPE_STRING,
} zfs_tunable_type_t;

typedef enum {
	ZFS_TUNABLE_PERM_ZMOD_RW,
	ZFS_TUNABLE_PERM_ZMOD_RD,
} zfs_tunable_perm_t;

typedef struct zfs_tunable {
	const char		*zt_name;
	void			*zt_varp;
	size_t			zt_varsz;
	zfs_tunable_type_t	zt_type;
	zfs_tunable_perm_t	zt_perm;
	const char		*zt_desc;
} zfs_tunable_t;

_SYS_TUNABLES_H int zfs_tunable_set(const zfs_tunable_t *tunable,
    const char *val);
_SYS_TUNABLES_H int zfs_tunable_get(const zfs_tunable_t *tunable, char *val,
    size_t valsz);

_SYS_TUNABLES_H const zfs_tunable_t *zfs_tunable_lookup(const char *name);

typedef int (*zfs_tunable_iter_t)(const zfs_tunable_t *tunable, void *arg);
_SYS_TUNABLES_H void zfs_tunable_iter(zfs_tunable_iter_t cb, void *arg);

#endif
