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
 * Copyright (c) 2022, SmartX Inc. All rights reserved.
 */

#ifndef	_LIBUZFS_H
#define	_LIBUZFS_H

#include <libnvpair.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct libuzfs_zpool_handle libuzfs_zpool_handle_t;
typedef struct libuzfs_dataset_handle libuzfs_dataset_handle_t;

extern void libuzfs_init(void);
extern void libuzfs_fini(void);
extern void libuzfs_set_zpool_cache_path(const char *zpool_cache);

extern int libuzfs_zpool_create(const char *zpool, const char *path,
    nvlist_t *props, nvlist_t *fsprops);

extern int libuzfs_zpool_destroy(const char *zpool);
extern libuzfs_zpool_handle_t *libuzfs_zpool_open(const char *zpool);
extern void libuzfs_zpool_close(libuzfs_zpool_handle_t *zhp);

extern void libuzfs_zpool_prop_set(libuzfs_zpool_handle_t *zhp,
    zpool_prop_t prop, uint64_t value);

extern int libuzfs_zpool_prop_get(libuzfs_zpool_handle_t *zhp,
    zpool_prop_t prop, uint64_t *value);

extern int libuzfs_dataset_create(const char *dsname);
extern void libuzfs_dataset_destroy(const char *dsname);
extern libuzfs_dataset_handle_t *libuzfs_dataset_open(const char *dsname);
extern void libuzfs_dataset_close(libuzfs_dataset_handle_t *dhp);

extern int libuzfs_object_stat(libuzfs_dataset_handle_t *dhp, uint64_t obj,
    dmu_object_info_t *doi);

extern int libuzfs_object_create(libuzfs_dataset_handle_t *dhp, uint64_t *obj);
extern int libuzfs_object_delete(libuzfs_dataset_handle_t *dhp, uint64_t obj);
extern int libuzfs_object_claim(libuzfs_dataset_handle_t *dhp, uint64_t obj);
extern int libuzfs_object_list(libuzfs_dataset_handle_t *dhp);

extern int libuzfs_object_read(libuzfs_dataset_handle_t *dhp, uint64_t obj,
    uint64_t offset, uint64_t size, char *buf);

extern int libuzfs_object_write(libuzfs_dataset_handle_t *dhp, uint64_t obj,
    uint64_t offset, uint64_t size, const char *buf);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBUZFS_H */
