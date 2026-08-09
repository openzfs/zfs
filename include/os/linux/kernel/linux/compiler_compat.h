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
 * Copyright (C) 2018 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_COMPILER_COMPAT_H
#define	_ZFS_COMPILER_COMPAT_H

#include <linux/compiler.h>

#if !defined(zfs_fallthrough)
#if defined(HAVE_IMPLICIT_FALLTHROUGH)
#define	zfs_fallthrough		__attribute__((__fallthrough__))
#else
#define	zfs_fallthrough		((void)0)
#endif
#endif

#if !defined(READ_ONCE)
#define	READ_ONCE(x)		ACCESS_ONCE(x)
#endif

#endif	/* _ZFS_COMPILER_COMPAT_H */
