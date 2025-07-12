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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef _SYS_MOD_H
#define	_SYS_MOD_H

#include <sys/tunables.h>

#define	ZFS_MODULE_PARAM(scope, prefix, name, type, perm, desc)		\
	static const zfs_tunable_t _zfs_tunable_##prefix##name = {	\
		.zt_name = #prefix#name,				\
		.zt_varp = &prefix##name,				\
		.zt_varsz = sizeof (prefix##name),			\
		.zt_type = ZFS_TUNABLE_TYPE_##type,			\
		.zt_perm = ZFS_TUNABLE_PERM_##perm,			\
		.zt_desc = desc						\
	};								\
	static const zfs_tunable_t *					\
	__zfs_tunable_##prefix##name					\
	__attribute__((__section__("zfs_tunables")))			\
	__attribute__((__used__))					\
	= &_zfs_tunable_##prefix##name;

#define	ZFS_MODULE_PARAM_ARGS void
#define	ZFS_MODULE_PARAM_CALL(scope_prefix, name_prefix, name, setfunc, \
	getfunc, perm, desc)

#define	EXPORT_SYMBOL(x)

#endif
