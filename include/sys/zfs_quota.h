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

struct zfsvfs;
struct zfs_file_info_t;

extern int zpl_get_file_info(dmu_object_type_t,
    const void *, struct zfs_file_info *);

extern int zfs_userspace_one(struct zfsvfs *, zfs_userquota_prop_t,
    const char *, uint64_t, uint64_t *);
extern int zfs_userspace_many(struct zfsvfs *, zfs_userquota_prop_t,
    uint64_t *, void *, uint64_t *);
extern int zfs_set_userquota(struct zfsvfs *, zfs_userquota_prop_t,
    const char *, uint64_t, uint64_t);

extern boolean_t zfs_id_overobjquota(struct zfsvfs *, uint64_t, uint64_t);
extern boolean_t zfs_id_overblockquota(struct zfsvfs *, uint64_t, uint64_t);
extern boolean_t zfs_id_overquota(struct zfsvfs *, uint64_t, uint64_t);

#endif
