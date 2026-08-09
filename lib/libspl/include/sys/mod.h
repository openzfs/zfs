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
