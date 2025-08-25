// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef _SYS_TUNABLES_H
#define	_SYS_TUNABLES_H

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

int zfs_tunable_set(const zfs_tunable_t *tunable, const char *val);
int zfs_tunable_get(const zfs_tunable_t *tunable, char *val, size_t valsz);

const zfs_tunable_t *zfs_tunable_lookup(const char *name);

typedef int (*zfs_tunable_iter_t)(const zfs_tunable_t *tunable, void *arg);
void zfs_tunable_iter(zfs_tunable_iter_t cb, void *arg);

#endif
