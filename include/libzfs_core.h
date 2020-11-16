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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
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

/*
 * NB: this type should be kept binary compatible with dmu_objset_type_t.
 */
enum lzc_dataset_type {
	LZC_DATSET_TYPE_ZFS = 2,
	LZC_DATSET_TYPE_ZVOL
};

int lzc_snapshot(nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_create(const char *, enum lzc_dataset_type, nvlist_t *, uint8_t *,
    uint_t);
int lzc_clone(const char *, const char *, nvlist_t *);
int lzc_promote(const char *, char *, int);
int lzc_destroy_snaps(nvlist_t *, boolean_t, nvlist_t **);
int lzc_bookmark(nvlist_t *, nvlist_t **);
int lzc_get_bookmarks(const char *, nvlist_t *, nvlist_t **);
int lzc_get_bookmark_props(const char *, nvlist_t **);
int lzc_destroy_bookmarks(nvlist_t *, nvlist_t **);
int lzc_load_key(const char *, boolean_t, uint8_t *, uint_t);
int lzc_unload_key(const char *);
int lzc_change_key(const char *, uint64_t, nvlist_t *, uint8_t *, uint_t);
int lzc_initialize(const char *, pool_initialize_func_t, nvlist_t *,
    nvlist_t **);
int lzc_trim(const char *, pool_trim_func_t, uint64_t, boolean_t,
    nvlist_t *, nvlist_t **);
int lzc_redact(const char *, const char *, nvlist_t *);

int lzc_snaprange_space(const char *, const char *, uint64_t *);

int lzc_hold(nvlist_t *, int, nvlist_t **);
int lzc_release(nvlist_t *, nvlist_t **);
int lzc_get_holds(const char *, nvlist_t **);

enum lzc_send_flags {
	LZC_SEND_FLAG_EMBED_DATA = 1 << 0,
	LZC_SEND_FLAG_LARGE_BLOCK = 1 << 1,
	LZC_SEND_FLAG_COMPRESS = 1 << 2,
	LZC_SEND_FLAG_RAW = 1 << 3,
	LZC_SEND_FLAG_SAVED = 1 << 4,
};

int lzc_send(const char *, const char *, int, enum lzc_send_flags);
int lzc_send_resume(const char *, const char *, int,
    enum lzc_send_flags, uint64_t, uint64_t);
int lzc_send_space(const char *, const char *, enum lzc_send_flags, uint64_t *);

struct dmu_replay_record;

int lzc_send_redacted(const char *, const char *, int, enum lzc_send_flags,
    const char *);
int lzc_send_resume_redacted(const char *, const char *, int,
    enum lzc_send_flags, uint64_t, uint64_t, const char *);
int lzc_receive(const char *, nvlist_t *, const char *, boolean_t, boolean_t,
    int);
int lzc_receive_resumable(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, int);
int lzc_receive_with_header(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, boolean_t, int, const struct dmu_replay_record *);
int lzc_receive_one(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, boolean_t, int, const struct dmu_replay_record *, int,
    uint64_t *, uint64_t *, uint64_t *, nvlist_t **);
int lzc_receive_with_cmdprops(const char *, nvlist_t *, nvlist_t *,
    uint8_t *, uint_t, const char *, boolean_t, boolean_t, boolean_t, int,
    const struct dmu_replay_record *, int, uint64_t *, uint64_t *,
    uint64_t *, nvlist_t **);
int lzc_send_space(const char *, const char *, enum lzc_send_flags, uint64_t *);
int lzc_send_space_resume_redacted(const char *, const char *,
    enum lzc_send_flags, uint64_t, uint64_t, uint64_t, const char *,
    int, uint64_t *);
uint64_t lzc_send_progress(int);

boolean_t lzc_exists(const char *);

int lzc_rollback(const char *, char *, int);
int lzc_rollback_to(const char *, const char *);

int lzc_rename(const char *, const char *);
int lzc_destroy(const char *);

int lzc_channel_program(const char *, const char *, uint64_t,
    uint64_t, nvlist_t *, nvlist_t **);
int lzc_channel_program_nosync(const char *, const char *, uint64_t,
    uint64_t, nvlist_t *, nvlist_t **);

int lzc_sync(const char *, nvlist_t *, nvlist_t **);
int lzc_reopen(const char *, boolean_t);

int lzc_pool_checkpoint(const char *);
int lzc_pool_checkpoint_discard(const char *);

int lzc_wait(const char *, zpool_wait_activity_t, boolean_t *);
int lzc_wait_tag(const char *, zpool_wait_activity_t, uint64_t, boolean_t *);
int lzc_wait_fs(const char *, zfs_wait_activity_t, boolean_t *);

int lzc_set_bootenv(const char *, const nvlist_t *);
int lzc_get_bootenv(const char *, nvlist_t **);
#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_CORE_H */
