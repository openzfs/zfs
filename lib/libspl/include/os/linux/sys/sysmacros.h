/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_SYSMACROS_H
#define	_LIBSPL_SYS_SYSMACROS_H

#include_next <sys/sysmacros.h>

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
#define	P2ALIGN(x, align)	((x) & -(align))
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


/* avoid any possibility of clashing with <stddef.h> version */
#if defined(_KERNEL) && !defined(_KMEMUSER) && !defined(offsetof)
#define	offsetof(s, m)	((size_t)(&(((s *)0)->m)))
#endif

#endif /* _LIBSPL_SYS_SYSMACROS_H */
