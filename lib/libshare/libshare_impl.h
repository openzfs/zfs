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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Gunnar Beutner
 */

struct sa_handle_impl;

typedef struct sa_share_fsinfo {
	boolean_t active;
	char *resource;
	char *shareopts;
} sa_share_fsinfo_t;

typedef struct sa_share_impl {
	struct sa_share_impl *next;

	struct sa_handle_impl *handle;

	char *sharepath;
	char *dataset;

	sa_share_fsinfo_t *fsinfo; /* per-fstype information */
} *sa_share_impl_t;

#define	FSINFO(impl_share, fstype) (&(impl_share->fsinfo[fstype->fsinfo_index]))

typedef struct sa_share_ops {
	int (*enable_share)(sa_share_impl_t share);
	int (*disable_share)(sa_share_impl_t share);
	int (*validate_shareopts)(const char *shareopts);
	int (*update_shareopts)(sa_share_impl_t impl_share,
	    const char *resource, const char *shareopts);
	void (*clear_shareopts)(sa_share_impl_t impl_share);
} sa_share_ops_t;

typedef struct sa_fstype {
	struct sa_fstype *next;

	const char *name;
	const sa_share_ops_t *ops;
	int fsinfo_index;
} sa_fstype_t;

typedef struct sa_handle_impl {
	libzfs_handle_t *zfs_libhandle;
	sa_share_impl_t shares;
} *sa_handle_impl_t;

sa_fstype_t *register_fstype(const char *name, const sa_share_ops_t *ops);
