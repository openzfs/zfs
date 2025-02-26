// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2024, Klara, Inc.
 */

#ifndef _ZFEATURE_COMMON_H
#define	_ZFEATURE_COMMON_H extern __attribute__((visibility("default")))

#include <sys/fs/zfs.h>
#include <sys/inttypes.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zfeature_info;

typedef enum spa_feature {
	SPA_FEATURE_NONE = -1,
	SPA_FEATURE_ASYNC_DESTROY,
	SPA_FEATURE_EMPTY_BPOBJ,
	SPA_FEATURE_LZ4_COMPRESS,
	SPA_FEATURE_MULTI_VDEV_CRASH_DUMP,
	SPA_FEATURE_SPACEMAP_HISTOGRAM,
	SPA_FEATURE_ENABLED_TXG,
	SPA_FEATURE_HOLE_BIRTH,
	SPA_FEATURE_EXTENSIBLE_DATASET,
	SPA_FEATURE_EMBEDDED_DATA,
	SPA_FEATURE_BOOKMARKS,
	SPA_FEATURE_FS_SS_LIMIT,
	SPA_FEATURE_LARGE_BLOCKS,
	SPA_FEATURE_LARGE_DNODE,
	SPA_FEATURE_SHA512,
	SPA_FEATURE_SKEIN,
	SPA_FEATURE_EDONR,
	SPA_FEATURE_USEROBJ_ACCOUNTING,
	SPA_FEATURE_ENCRYPTION,
	SPA_FEATURE_PROJECT_QUOTA,
	SPA_FEATURE_DEVICE_REMOVAL,
	SPA_FEATURE_OBSOLETE_COUNTS,
	SPA_FEATURE_POOL_CHECKPOINT,
	SPA_FEATURE_SPACEMAP_V2,
	SPA_FEATURE_ALLOCATION_CLASSES,
	SPA_FEATURE_RESILVER_DEFER,
	SPA_FEATURE_BOOKMARK_V2,
	SPA_FEATURE_REDACTION_BOOKMARKS,
	SPA_FEATURE_REDACTED_DATASETS,
	SPA_FEATURE_BOOKMARK_WRITTEN,
	SPA_FEATURE_LOG_SPACEMAP,
	SPA_FEATURE_LIVELIST,
	SPA_FEATURE_DEVICE_REBUILD,
	SPA_FEATURE_ZSTD_COMPRESS,
	SPA_FEATURE_DRAID,
	SPA_FEATURE_ZILSAXATTR,
	SPA_FEATURE_HEAD_ERRLOG,
	SPA_FEATURE_BLAKE3,
	SPA_FEATURE_BLOCK_CLONING,
	SPA_FEATURE_AVZ_V2,
	SPA_FEATURE_REDACTION_LIST_SPILL,
	SPA_FEATURE_RAIDZ_EXPANSION,
	SPA_FEATURE_FAST_DEDUP,
	SPA_FEATURE_LONGNAME,
	SPA_FEATURE_LARGE_MICROZAP,
	SPA_FEATURES
} spa_feature_t;

#define	SPA_FEATURE_DISABLED	(-1ULL)

typedef enum zfeature_flags {
	/* Can open pool readonly even if this feature is not supported. */
	ZFEATURE_FLAG_READONLY_COMPAT =		(1 << 0),
	/*
	 * Is this feature necessary to load the pool? i.e. do we need this
	 * feature to read the full feature list out of the MOS?
	 */
	ZFEATURE_FLAG_MOS =			(1 << 1),
	/* Activate this feature at the same time it is enabled. */
	ZFEATURE_FLAG_ACTIVATE_ON_ENABLE =	(1 << 2),
	/* Each dataset has a field set if it has ever used this feature. */
	ZFEATURE_FLAG_PER_DATASET =		(1 << 3)
} zfeature_flags_t;

typedef enum zfeature_type {
	ZFEATURE_TYPE_BOOLEAN,
	ZFEATURE_TYPE_UINT64_ARRAY,
	ZFEATURE_NUM_TYPES
} zfeature_type_t;

typedef struct zfeature_info {
	spa_feature_t fi_feature;
	const char *fi_uname;	/* User-facing feature name */
	const char *fi_guid;	/* On-disk feature identifier */
	const char *fi_desc;	/* Feature description */
	zfeature_flags_t fi_flags;
	boolean_t fi_zfs_mod_supported;	/* supported by running zfs module */
	zfeature_type_t fi_type; /* Only relevant for PER_DATASET features */
	/* array of dependencies, terminated by SPA_FEATURE_NONE */
	const spa_feature_t *fi_depends;
} zfeature_info_t;

typedef int (zfeature_func_t)(zfeature_info_t *, void *);

#define	ZFS_FEATURE_DEBUG

_ZFEATURE_COMMON_H zfeature_info_t spa_feature_table[SPA_FEATURES];
_ZFEATURE_COMMON_H boolean_t zfeature_checks_disable;

_ZFEATURE_COMMON_H boolean_t zfeature_is_valid_guid(const char *);

_ZFEATURE_COMMON_H boolean_t zfeature_is_supported(const char *);
_ZFEATURE_COMMON_H int zfeature_lookup_guid(const char *, spa_feature_t *);
_ZFEATURE_COMMON_H int zfeature_lookup_name(const char *, spa_feature_t *);
_ZFEATURE_COMMON_H boolean_t zfeature_depends_on(spa_feature_t, spa_feature_t);

_ZFEATURE_COMMON_H void zpool_feature_init(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFEATURE_COMMON_H */
