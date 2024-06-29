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

#ifndef _SYS_VFS_RATELIMIT_H
#define	_SYS_VFS_RATELIMIT_H

#include <sys/dmu_objset.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct vfs_ratelimit;

#define	ZFS_RATELIMIT_BW_READ	0
#define	ZFS_RATELIMIT_BW_WRITE	1
#define	ZFS_RATELIMIT_BW_TOTAL	2
#define	ZFS_RATELIMIT_OP_READ	3
#define	ZFS_RATELIMIT_OP_WRITE	4
#define	ZFS_RATELIMIT_OP_TOTAL	5
#define	ZFS_RATELIMIT_FIRST	ZFS_RATELIMIT_BW_READ
#define	ZFS_RATELIMIT_LAST	ZFS_RATELIMIT_OP_TOTAL
#define	ZFS_RATELIMIT_NTYPES	(ZFS_RATELIMIT_LAST + 1)

int vfs_ratelimit_prop_to_type(zfs_prop_t prop);
zfs_prop_t vfs_ratelimit_type_to_prop(int type);

struct vfs_ratelimit *vfs_ratelimit_alloc(const uint64_t *limits);
void vfs_ratelimit_free(struct vfs_ratelimit *rl);
struct vfs_ratelimit *vfs_ratelimit_set(struct vfs_ratelimit *rl,
    zfs_prop_t prop, uint64_t limit);

void vfs_ratelimit_data_read(objset_t *os, size_t blocksize, size_t bytes);
void vfs_ratelimit_data_write(objset_t *os, size_t blocksize, size_t bytes);
void vfs_ratelimit_metadata_read(objset_t *os);
void vfs_ratelimit_metadata_write(objset_t *os);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VFS_RATELIMIT_H */
