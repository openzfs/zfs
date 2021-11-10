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
 * Copyright (C) 2018 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_COMPILER_COMPAT_H
#define	_ZFS_COMPILER_COMPAT_H

#include <linux/compiler.h>

#if !defined(fallthrough)
#if defined(HAVE_IMPLICIT_FALLTHROUGH)
#define	fallthrough		__attribute__((__fallthrough__))
#else
#define	fallthrough		((void)0)
#endif
#endif

#if !defined(READ_ONCE)
#define	READ_ONCE(x)		ACCESS_ONCE(x)
#endif

#endif	/* _ZFS_COMPILER_COMPAT_H */
