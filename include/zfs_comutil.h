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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 */

#ifndef	_ZFS_COMUTIL_H
#define	_ZFS_COMUTIL_H extern __attribute__((visibility("default")))

#include <sys/fs/zfs.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

_ZFS_COMUTIL_H boolean_t zfs_allocatable_devs(nvlist_t *);
_ZFS_COMUTIL_H boolean_t zfs_special_devs(nvlist_t *, const char *);
_ZFS_COMUTIL_H void zpool_get_load_policy(nvlist_t *, zpool_load_policy_t *);

_ZFS_COMUTIL_H int zfs_zpl_version_map(int spa_version);
_ZFS_COMUTIL_H int zfs_spa_version_map(int zpl_version);

_ZFS_COMUTIL_H boolean_t zfs_dataset_name_hidden(const char *);

#define	ZFS_NUM_LEGACY_HISTORY_EVENTS 41
_ZFS_COMUTIL_H const char *const
    zfs_history_event_names[ZFS_NUM_LEGACY_HISTORY_EVENTS];

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_COMUTIL_H */
