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
 * Copyright (c) 2017, Intel Corporation. All rights reserved.
 */

#ifndef	_ZFS_PROJECTUTIL_H
#define	_ZFS_PROJECTUTIL_H

typedef enum {
	ZFS_PROJECT_OP_DEFAULT	= 0,
	ZFS_PROJECT_OP_LIST	= 1,
	ZFS_PROJECT_OP_CHECK	= 2,
	ZFS_PROJECT_OP_CLEAR	= 3,
	ZFS_PROJECT_OP_SET	= 4,
} zfs_project_ops_t;

typedef struct zfs_project_control {
	uint64_t		zpc_expected_projid;
	zfs_project_ops_t	zpc_op;
	boolean_t		zpc_dironly;
	boolean_t		zpc_ignore_noent;
	boolean_t		zpc_keep_projid;
	boolean_t		zpc_newline;
	boolean_t		zpc_recursive;
	boolean_t		zpc_set_flag;
} zfs_project_control_t;

int zfs_project_handle(const char *name, zfs_project_control_t *zpc);

#endif	/* _ZFS_PROJECTUTIL_H */
