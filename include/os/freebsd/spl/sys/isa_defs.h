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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ISA_DEFS_H
#define	_SYS_ISA_DEFS_H
#include <sys/endian.h>

/*
 * This header file serves to group a set of well known defines and to
 * set these for each instruction set architecture.  These defines may
 * be divided into two groups;  characteristics of the processor and
 * implementation choices for Solaris on a processor.
 *
 * Processor Characteristics:
 *
 * _LITTLE_ENDIAN / _BIG_ENDIAN:
 *	The natural byte order of the processor.  A pointer to an int points
 *	to the least/most significant byte of that int.
 *
 *
 * Implementation Choices:
 *
 * _ILP32 / _LP64:
 *	This specifies the compiler data type implementation as specified in
 *	the relevant ABI.  The choice between these is strongly influenced
 *	by the underlying hardware, but is not absolutely tied to it.
 *	Currently only two data type models are supported:
 *
 *	_ILP32:
 *		Int/Long/Pointer are 32 bits.  This is the historical UNIX
 *		and Solaris implementation.  Due to its historical standing,
 *		this is the default case.
 *
 *	_LP64:
 *		Long/Pointer are 64 bits, Int is 32 bits.  This is the chosen
 *		implementation for 64-bit ABIs such as SPARC V9.
 *
 *	In all cases, Char is 8 bits and Short is 16 bits.
 *
 * _SUNOS_VTOC_8 / _SUNOS_VTOC_16 / _SVR4_VTOC_16:
 *	This specifies the form of the disk VTOC (or label):
 *
 *	_SUNOS_VTOC_8:
 *		This is a VTOC form which is upwardly compatible with the
 *		SunOS 4.x disk label and allows 8 partitions per disk.
 *
 *	_SUNOS_VTOC_16:
 *		In this format the incore vtoc image matches the ondisk
 *		version.  It allows 16 slices per disk, and is not
 *		compatible with the SunOS 4.x disk label.
 *
 *	Note that these are not the only two VTOC forms possible and
 *	additional forms may be added.  One possible form would be the
 *	SVr4 VTOC form.  The symbol for that is reserved now, although
 *	it is not implemented.
 *
 *	_SVR4_VTOC_16:
 *		This VTOC form is compatible with the System V Release 4
 *		VTOC (as implemented on the SVr4 Intel and 3b ports) with
 *		16 partitions per disk.
 *
 *
 * __x86
 *	This is ONLY a synonym for defined(__i386) || defined(__amd64)
 *	which is useful only insofar as these two architectures share
 *	common attributes.  Analogous to __sparc.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following set of definitions characterize Solaris on AMD's
 * 64-bit systems.
 */
#if defined(__x86_64) || defined(__amd64)

#if !defined(__amd64)
#define	__amd64		/* preferred guard */
#endif

#if !defined(__x86)
#define	__x86
#endif

/*
 * Define the appropriate "implementation choices".
 */
#if !defined(_LP64)
#error "_LP64 not defined"
#endif
#define	_SUNOS_VTOC_16

/*
 * The feature test macro __i386 is generic for all processors implementing
 * the Intel 386 instruction set or a superset of it.  Specifically, this
 * includes all members of the 386, 486, and Pentium family of processors.
 */
#elif defined(__i386) || defined(__i386__)

#if !defined(__i386)
#define	__i386
#endif

#if !defined(__x86)
#define	__x86
#endif

/*
 * Define the appropriate "implementation choices".
 */
#if !defined(_ILP32)
#define	_ILP32
#endif
#define	_SUNOS_VTOC_16

#elif defined(__aarch64__)

/*
 * Define the appropriate "implementation choices"
 */
#if !defined(_LP64)
#error "_LP64 not defined"
#endif
#define	_SUNOS_VTOC_16

#elif defined(__riscv)

/*
 * Define the appropriate "implementation choices"
 */
#if !defined(_LP64)
#define	_LP64
#endif
#define	_SUNOS_VTOC_16

#elif defined(__arm__)

/*
 * Define the appropriate "implementation choices".
 */
#if !defined(_ILP32)
#define	_ILP32
#endif
#define	_SUNOS_VTOC_16

#elif defined(__mips__)

#if defined(__mips_n64)
/*
 * Define the appropriate "implementation choices".
 */
#if !defined(_LP64)
#error "_LP64 not defined"
#endif
#else
/*
 * Define the appropriate "implementation choices".
 */
#if !defined(_ILP32)
#define	_ILP32
#endif
#endif
#define	_SUNOS_VTOC_16

#elif defined(__powerpc__)

#if !defined(__powerpc)
#define	__powerpc
#endif

#define	_SUNOS_VTOC_16	1

/*
 * The following set of definitions characterize the Solaris on SPARC systems.
 *
 * The symbol __sparc indicates any of the SPARC family of processor
 * architectures.  This includes SPARC V7, SPARC V8 and SPARC V9.
 *
 * The symbol __sparcv8 indicates the 32-bit SPARC V8 architecture as defined
 * by Version 8 of the SPARC Architecture Manual.  (SPARC V7 is close enough
 * to SPARC V8 for the former to be subsumed into the latter definition.)
 *
 * The symbol __sparcv9 indicates the 64-bit SPARC V9 architecture as defined
 * by Version 9 of the SPARC Architecture Manual.
 *
 * The symbols __sparcv8 and __sparcv9 are mutually exclusive, and are only
 * relevant when the symbol __sparc is defined.
 */
/*
 * XXX Due to the existence of 5110166, "defined(__sparcv9)" needs to be added
 * to support backwards builds.  This workaround should be removed in s10_71.
 */
#elif defined(__sparc) || defined(__sparcv9) || defined(__sparc__)
#if !defined(__sparc)
#define	__sparc
#endif

/*
 * You can be 32-bit or 64-bit, but not both at the same time.
 */
#if defined(__sparcv8) && defined(__sparcv9)
#error	"SPARC Versions 8 and 9 are mutually exclusive choices"
#endif

/*
 * Existing compilers do not set __sparcv8.  Years will transpire before
 * the compilers can be depended on to set the feature test macro. In
 * the interim, we'll set it here on the basis of historical behaviour;
 * if you haven't asked for SPARC V9, then you must've meant SPARC V8.
 */
#if !defined(__sparcv9) && !defined(__sparcv8)
#define	__sparcv8
#endif

/*
 * Define the appropriate "implementation choices" shared between versions.
 */
#define	_SUNOS_VTOC_8

/*
 * The following set of definitions characterize the implementation of
 * 32-bit Solaris on SPARC V8 systems.
 */
#if defined(__sparcv8)

/*
 * Define the appropriate "implementation choices"
 */
#define	_ILP32

/*
 * The following set of definitions characterize the implementation of
 * 64-bit Solaris on SPARC V9 systems.
 */
#elif defined(__sparcv9)

/*
 * Define the appropriate "implementation choices"
 */
#if !defined(_LP64)
#error "_LP64 not defined"
#endif

#else
#error	"unknown SPARC version"
#endif

/*
 * #error is strictly ansi-C, but works as well as anything for K&R systems.
 */
#else
#error "ISA not supported"
#endif

#if defined(_ILP32) && defined(_LP64)
#error "Both _ILP32 and _LP64 are defined"
#endif

#if BYTE_ORDER == _BIG_ENDIAN
#define	_ZFS_BIG_ENDIAN
#elif BYTE_ORDER == _LITTLE_ENDIAN
#define	_ZFS_LITTLE_ENDIAN
#else
#error "unknown byte order"
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ISA_DEFS_H */
