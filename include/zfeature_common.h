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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 */

#ifndef _ZFEATURE_COMMON_H
#define	_ZFEATURE_COMMON_H

#include <sys/fs/zfs.h>
#include <sys/inttypes.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zfeature_info;

typedef struct zfeature_info {
	const char *fi_uname;	/* User-facing feature name */
	const char *fi_guid;	/* On-disk feature identifier */
	const char *fi_desc;	/* Feature description */
	boolean_t fi_can_readonly; /* Can open pool readonly w/o support? */
	boolean_t fi_mos;	/* Is the feature necessary to read the MOS? */
	struct zfeature_info **fi_depends; /* array; null terminated */
} zfeature_info_t;

typedef int (zfeature_func_t)(zfeature_info_t *fi, void *arg);

#define	ZFS_FEATURE_DEBUG

typedef enum spa_feature {
	SPA_FEATURE_ASYNC_DESTROY,
	SPA_FEATURE_EMPTY_BPOBJ,
	SPA_FEATURE_LZ4_COMPRESS,
	SPA_FEATURES
} spa_feature_t;

extern zfeature_info_t spa_feature_table[SPA_FEATURES];

extern boolean_t zfeature_is_valid_guid(const char *);

extern boolean_t zfeature_is_supported(const char *);
extern int zfeature_lookup_guid(const char *, zfeature_info_t **res);
extern int zfeature_lookup_name(const char *, zfeature_info_t **res);

extern void zpool_feature_init(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFEATURE_COMMON_H */
