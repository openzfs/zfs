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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef	_LIBZFS_CORE_H
#define	_LIBZFS_CORE_H

#include <libnvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

int libzfs_core_init(void);
void libzfs_core_fini(void);

int lzc_snapshot(nvlist_t *snaps, nvlist_t *props, nvlist_t **errlist);
int lzc_create(const char *fsname, dmu_objset_type_t type, nvlist_t *props);
int lzc_clone(const char *fsname, const char *origin, nvlist_t *props);
int lzc_destroy_snaps(nvlist_t *snaps, boolean_t defer, nvlist_t **errlist);

int lzc_snaprange_space(const char *firstsnap, const char *lastsnap,
    uint64_t *usedp);

int lzc_hold(nvlist_t *holds, int cleanup_fd, nvlist_t **errlist);
int lzc_release(nvlist_t *holds, nvlist_t **errlist);
int lzc_get_holds(const char *snapname, nvlist_t **holdsp);

int lzc_send(const char *snapname, const char *fromsnap, int fd);
int lzc_receive(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, int fd);
int lzc_send_space(const char *snapname, const char *fromsnap,
    uint64_t *result);

boolean_t lzc_exists(const char *dataset);

int lzc_rollback(const char *fsname, char *snapnamebuf, int snapnamelen);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_CORE_H */
