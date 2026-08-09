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
