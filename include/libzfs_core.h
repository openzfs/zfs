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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

#ifndef	_LIBZFS_CORE_H
#define	_LIBZFS_CORE_H

#include <libnvpair.h>
#include <sys/param.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

enum dmu_objset_type;

typedef int (*lzc_iter_f)(nvlist_t *, void *);

int libzfs_core_init(void);
void libzfs_core_fini(void);

int lzc_pool_configs(nvlist_t *, nvlist_t **);
int lzc_pool_getprops(const char *, nvlist_t *, nvlist_t **);
int lzc_pool_export(const char *, nvlist_t *);
int lzc_pool_import(const char *, nvlist_t *, nvlist_t *,
    nvlist_t **);
int lzc_pool_tryimport(nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_pool_stats(const char *, nvlist_t *, nvlist_t **);

int lzc_list(const char *, nvlist_t *);
int lzc_list_iter(const char *, nvlist_t *, lzc_iter_f, void *);
int lzc_snapshot(nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_snapshot_ext(nvlist_t *, nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_create(const char *, enum dmu_objset_type, nvlist_t *);
int lzc_create_ext(const char *, const char *, nvlist_t *, nvlist_t *,
    nvlist_t **);
int lzc_clone(const char *, const char *, nvlist_t *);
int lzc_clone_ext(const char *, const char *, nvlist_t *, nvlist_t *,
    nvlist_t **);
int lzc_promote(const char *, nvlist_t *, nvlist_t **);
int lzc_set_props(const char *, nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_destroy_one(const char *, nvlist_t *);
int lzc_destroy_snaps(nvlist_t *, boolean_t, nvlist_t **);
int lzc_destroy_snaps_ext(const char *, nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_bookmark(nvlist_t *, nvlist_t **);
int lzc_bookmark_ext(nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_get_bookmarks(const char *, nvlist_t *, nvlist_t **);
int lzc_destroy_bookmarks(nvlist_t *, nvlist_t **);
int lzc_destroy_bookmarks_ext(nvlist_t *, nvlist_t *opts, nvlist_t **);

int lzc_snaprange_space(const char *, const char *, uint64_t *);

int lzc_hold(nvlist_t *, int, nvlist_t **);
int lzc_hold_ext(nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_release(nvlist_t *, nvlist_t **);
int lzc_release_ext(nvlist_t *, nvlist_t *opts, nvlist_t **);
int lzc_get_holds(const char *, nvlist_t **);

int lzc_inherit(const char *fsname, const char *propname, nvlist_t *opts);
int lzc_rename(const char *oldname, const char *newname, nvlist_t *opts,
    char **errname);

enum lzc_send_flags {
	LZC_SEND_FLAG_EMBED_DATA = 1 << 0,
	LZC_SEND_FLAG_LARGE_BLOCK = 1 << 1
};

int lzc_send(const char *, const char *, int, enum lzc_send_flags);
int lzc_send_resume(const char *, const char *, int,
    enum lzc_send_flags, uint64_t, uint64_t);
int lzc_send_space(const char *, const char *, uint64_t *);
int lzc_send_progress(const char *, int, uint64_t *);

struct dmu_replay_record;

int lzc_receive(const char *, nvlist_t *, const char *, boolean_t, int);
int lzc_receive_with_header(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, int, const struct dmu_replay_record *);

struct dmu_replay_record;

int lzc_receive(const char *, nvlist_t *, const char *, boolean_t, int);
int lzc_receive_resumable(const char *, nvlist_t *, const char *,
    boolean_t, int);
int lzc_receive_with_header(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, int, const struct dmu_replay_record *);
int lzc_receive_one(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, int, const struct dmu_replay_record *, int, uint64_t *,
    uint64_t *, uint64_t *, nvlist_t **);

boolean_t lzc_exists(const char *);

int lzc_rollback(const char *, char *, int);
int lzc_rollback_ext(const char *, char *, int, nvlist_t *opts);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_CORE_H */
