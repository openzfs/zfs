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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_TYPES_H
#define	_LIBSPL_SYS_TYPES_H

#if defined(HAVE_MAKEDEV_IN_SYSMACROS)
#include <sys/sysmacros.h>
#elif defined(HAVE_MAKEDEV_IN_MKDEV)
#include <sys/mkdev.h>
#endif

#include <sys/isa_defs.h>
#include <sys/feature_tests.h>
#if defined(__NetBSD__)
/*
 * Native NetBSD is `typedef int64_t longlong_t` / `uint64_t
 * u_longlong_t` (long / unsigned long on LP64).  OpenZFS common code
 * treats them as long long.  Same rename-then-typedef as the kernel
 * SPL boolean_t / longlong_t hijack.
 */
#define	longlong_t	__netbsd_longlong_t
#define	u_longlong_t	__netbsd_u_longlong_t
#endif
#include_next <sys/types.h>
#if defined(__NetBSD__)
#undef	longlong_t
#undef	u_longlong_t
#endif
#include <sys/types32.h>
#include <stdarg.h>
#include <sys/stdtypes.h>

#ifndef HAVE_INTTYPES
#include <inttypes.h>
#endif /* HAVE_INTTYPES */

typedef uint_t		zoneid_t;
typedef int		projid_t;

#include <sys/param.h> /* for NBBY */

#endif
