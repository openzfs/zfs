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

#ifndef _SYS_ZFS_QUOTA_H
#define	_SYS_ZFS_QUOTA_H

#include <sys/dmu.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_vfsops.h>

extern int zfs_space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp, uint64_t *projectp);

extern int zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valuep);
extern int zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep);
extern int zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota);

extern boolean_t zfs_id_overobjquota(zfsvfs_t *zfsvfs, uint64_t usedobj,
    uint64_t id);
extern boolean_t zfs_id_overblockquota(zfsvfs_t *zfsvfs, uint64_t usedobj,
    uint64_t id);
extern boolean_t zfs_id_overquota(zfsvfs_t *zfsvfs, uint64_t usedobj,
    uint64_t id);

#endif
