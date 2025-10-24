// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed by Pawel Dawidek <pawel@dawidek.net>
 * under sponsorship from the FreeBSD Foundation.
 */

#ifndef _SYS_ZFS_IOLIMIT_H
#define	_SYS_ZFS_IOLIMIT_H

#include <sys/dmu_objset.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zfs_iolimit;

#define	ZFS_IOLIMIT_BW_READ	0
#define	ZFS_IOLIMIT_BW_WRITE	1
#define	ZFS_IOLIMIT_BW_TOTAL	2
#define	ZFS_IOLIMIT_OP_READ	3
#define	ZFS_IOLIMIT_OP_WRITE	4
#define	ZFS_IOLIMIT_OP_TOTAL	5
#define	ZFS_IOLIMIT_FIRST	ZFS_IOLIMIT_BW_READ
#define	ZFS_IOLIMIT_LAST	ZFS_IOLIMIT_OP_TOTAL
#define	ZFS_IOLIMIT_NTYPES	(ZFS_IOLIMIT_LAST + 1)

int zfs_iolimit_prop_to_type(zfs_prop_t prop);
zfs_prop_t zfs_iolimit_type_to_prop(int type);

struct zfs_iolimit *zfs_iolimit_alloc(const uint64_t *limits);
void zfs_iolimit_free(struct zfs_iolimit *iol);
struct zfs_iolimit *zfs_iolimit_set(struct zfs_iolimit *iol, zfs_prop_t prop,
    uint64_t limit);

int zfs_iolimit_data_read(objset_t *os, size_t blocksize, size_t bytes);
int zfs_iolimit_data_write(objset_t *os, size_t blocksize, size_t bytes);
int zfs_iolimit_data_copy(objset_t *srcos, objset_t *dstos, size_t blocksize,
    size_t bytes);
int zfs_iolimit_metadata_read(objset_t *os);
int zfs_iolimit_metadata_write(objset_t *os);

void zfs_iolimit_data_read_spin(objset_t *os, size_t blocksize, size_t bytes);
void zfs_iolimit_data_write_spin(objset_t *os, size_t blocksize,
    size_t bytes);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_IOLIMIT_H */
