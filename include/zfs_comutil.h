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
