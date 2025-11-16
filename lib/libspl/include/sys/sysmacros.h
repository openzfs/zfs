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
 */

#ifndef _LIBSPL_SYS_SYSMACROS_H
#define	_LIBSPL_SYS_SYSMACROS_H

#include <stdint.h>

#ifdef __linux__
/*
 * On Linux, we need the system-provided sysmacros.h to get the makedev(),
 * major() and minor() definitions for makedevice() below. FreeBSD does not
 * have this header, so include_next won't find it and will abort. So, we
 * protect it with a platform check.
 */
#include_next <sys/sysmacros.h>
#endif

/* common macros */
#ifndef MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)	((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define	ABS(a)		((a) < 0 ? -(a) : (a))
#endif
#ifndef ARRAY_SIZE
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))
#endif
#ifndef	DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif

#define	makedevice(maj, min)	makedev(maj, min)
#define	_sysconf(a)		sysconf(a)

/*
 * Compatibility macros/typedefs needed for Solaris -> Linux port
 */
// Deprecated. Use P2ALIGN_TYPED instead.
// #define	P2ALIGN(x, align)	((x) & -(align))
#define	P2CROSS(x, y, align)	(((x) ^ (y)) > (align) - 1)
#define	P2ROUNDUP(x, align)	((((x) - 1) | ((align) - 1)) + 1)
#define	P2BOUNDARY(off, len, align) \
				(((off) ^ ((off) + (len) - 1)) > (align) - 1)
#define	P2PHASE(x, align)	((x) & ((align) - 1))
#define	P2NPHASE(x, align)	(-(x) & ((align) - 1))
#define	P2NPHASE_TYPED(x, align, type) \
				(-(type)(x) & ((type)(align) - 1))
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 *      P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 *      P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define	P2ALIGN_TYPED(x, align, type)		\
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)		\
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)		\
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type)		\
	((((type)(x) - 1) | ((type)(align) - 1)) + 1)
#define	P2END_TYPED(x, align, type)		\
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)	\
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)	\
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type)		\
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

#define	max_ncpus	64
#define	boot_ncpus	(sysconf(_SC_NPROCESSORS_ONLN))

/*
 * Process priorities as defined by setpriority(2) and getpriority(2).
 */
#define	minclsyspri	19
#define	defclsyspri	0
/* Write issue taskq priority. */
#define	wtqclsyspri	-19
#define	maxclsyspri	-20

#define	CPU_SEQID	((uintptr_t)pthread_self() & (max_ncpus - 1))
#define	CPU_SEQID_UNSTABLE	CPU_SEQID

extern int lowbit64(uint64_t i);
extern int highbit64(uint64_t i);

#endif /* _SYS_SYSMACROS_H */
