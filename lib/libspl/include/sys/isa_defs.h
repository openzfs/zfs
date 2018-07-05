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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ISA_DEFS_H
#define	_SYS_ISA_DEFS_H

#ifdef  __cplusplus
extern "C" {
#endif

/* x86_64 arch specific defines */
#if defined(__x86_64) || defined(__x86_64__)

#if !defined(__x86_64)
#define	__x86_64
#endif

#if !defined(__amd64)
#define	__amd64
#endif

#if !defined(__x86)
#define	__x86
#endif

#if !defined(_LP64)
#define	_LP64
#endif

#if !defined(_LITTLE_ENDIAN)
#define	_LITTLE_ENDIAN
#endif

#define	_SUNOS_VTOC_16
#define	HAVE_EFFICIENT_UNALIGNED_ACCESS

/* i386 arch specific defines */
#elif defined(__i386) || defined(__i386__)

#if !defined(__i386)
#define	__i386
#endif

#if !defined(__x86)
#define	__x86
#endif

#if !defined(_ILP32)
#define	_ILP32
#endif

#if !defined(_LITTLE_ENDIAN)
#define	_LITTLE_ENDIAN
#endif

#define	_SUNOS_VTOC_16
#define	HAVE_EFFICIENT_UNALIGNED_ACCESS

/* powerpc arch specific defines */
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__)

#if !defined(__powerpc)
#define	__powerpc
#endif

#if !defined(__powerpc__)
#define	__powerpc__
#endif

#if defined(__powerpc64__)
#if !defined(_LP64)
#define	_LP64
#endif
#else
#if !defined(_ILP32)
#define	_ILP32
#endif
#endif

#define	_SUNOS_VTOC_16
#define	HAVE_EFFICIENT_UNALIGNED_ACCESS

/* arm arch specific defines */
#elif defined(__arm) || defined(__arm__) || defined(__aarch64__)

#if !defined(__arm)
#define	__arm
#endif

#if !defined(__arm__)
#define	__arm__
#endif

#if defined(__aarch64__)
#if !defined(_LP64)
#define	_LP64
#endif
#else
#if !defined(_ILP32)
#define	_ILP32
#endif
#endif

#if defined(__ARMEL__) || defined(__AARCH64EL__)
#define	_LITTLE_ENDIAN
#else
#define	_BIG_ENDIAN
#endif

#define	_SUNOS_VTOC_16

#if defined(__ARM_FEATURE_UNALIGNED)
#define	HAVE_EFFICIENT_UNALIGNED_ACCESS
#endif

/* sparc arch specific defines */
#elif defined(__sparc) || defined(__sparc__)

#if !defined(__sparc)
#define	__sparc
#endif

#if !defined(__sparc__)
#define	__sparc__
#endif

#define	_BIG_ENDIAN
#define	_SUNOS_VTOC_16

#if defined(__arch64__)
#if !defined(_LP64)
#define	_LP64
#endif
#else
#if !defined(_ILP32)
#define	_ILP32
#endif
#endif

/* s390 arch specific defines */
#elif defined(__s390__)
#if defined(__s390x__)
#if !defined(_LP64)
#define	_LP64
#endif
#else
#if !defined(_ILP32)
#define	_ILP32
#endif
#endif

#define	_BIG_ENDIAN
#define	_SUNOS_VTOC_16

/* MIPS arch specific defines */
#elif defined(__mips__)

#if defined(__MIPSEB__)
#define	_BIG_ENDIAN
#elif defined(__MIPSEL__)
#define	_LITTLE_ENDIAN
#else
#error MIPS no endian specified
#endif

#ifndef _LP64
#define	_ILP32
#endif

#define	_SUNOS_VTOC_16

#else
/*
 * Currently supported:
 * x86_64, i386, arm, powerpc, s390, sparc, and mips
 */
#error "Unsupported ISA type"
#endif

#if defined(_ILP32) && defined(_LP64)
#error "Both _ILP32 and _LP64 are defined"
#endif

#if !defined(_ILP32) && !defined(_LP64)
#error "Neither _ILP32 or _LP64 are defined"
#endif

#if defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN)
#error "Both _LITTLE_ENDIAN and _BIG_ENDIAN are defined"
#endif

#if !defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)
#error "Neither _LITTLE_ENDIAN nor _BIG_ENDIAN are defined"
#endif

#ifdef  __cplusplus
}
#endif

#endif	/* _SYS_ISA_DEFS_H */
