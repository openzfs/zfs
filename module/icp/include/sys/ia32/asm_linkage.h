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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _IA32_SYS_ASM_LINKAGE_H
#define	_IA32_SYS_ASM_LINKAGE_H

#include <sys/stack.h>
#include <sys/trap.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM	/* The remainder of this file is only for assembly files */

/*
 * make annoying differences in assembler syntax go away
 */

/*
 * D16 and A16 are used to insert instructions prefixes; the
 * macros help the assembler code be slightly more portable.
 */
#if !defined(__GNUC_AS__)
/*
 * /usr/ccs/bin/as prefixes are parsed as separate instructions
 */
#define	D16	data16;
#define	A16	addr16;

/*
 * (There are some weird constructs in constant expressions)
 */
#define	_CONST(const)		[const]
#define	_BITNOT(const)		-1!_CONST(const)
#define	_MUL(a, b)		_CONST(a \* b)

#else
/*
 * Why not use the 'data16' and 'addr16' prefixes .. well, the
 * assembler doesn't quite believe in real mode, and thus argues with
 * us about what we're trying to do.
 */
#define	D16	.byte	0x66;
#define	A16	.byte	0x67;

#define	_CONST(const)		(const)
#define	_BITNOT(const)		~_CONST(const)
#define	_MUL(a, b)		_CONST(a * b)

#endif

/*
 * C pointers are different sizes between i386 and amd64.
 * These constants can be used to compute offsets into pointer arrays.
 */
#if defined(__amd64)
#define	CLONGSHIFT	3
#define	CLONGSIZE	8
#define	CLONGMASK	7
#elif defined(__i386)
#define	CLONGSHIFT	2
#define	CLONGSIZE	4
#define	CLONGMASK	3
#endif

/*
 * Since we know we're either ILP32 or LP64 ..
 */
#define	CPTRSHIFT	CLONGSHIFT
#define	CPTRSIZE	CLONGSIZE
#define	CPTRMASK	CLONGMASK

#if CPTRSIZE != (1 << CPTRSHIFT) || CLONGSIZE != (1 << CLONGSHIFT)
#error	"inconsistent shift constants"
#endif

#if CPTRMASK != (CPTRSIZE - 1) || CLONGMASK != (CLONGSIZE - 1)
#error	"inconsistent mask constants"
#endif

#define	ASM_ENTRY_ALIGN	16

/*
 * SSE register alignment and save areas
 */

#define	XMM_SIZE	16
#define	XMM_ALIGN	16

/*
 * ENTRY provides the standard procedure entry code and an easy way to
 * insert the calls to mcount for profiling. ENTRY_NP is identical, but
 * never calls mcount.
 */
#define	ENTRY(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:	MCOUNT(x)

#define	ENTRY_NP(x) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, @function; \
x:

/*
 * ENTRY2 is identical to ENTRY but provides two labels for the entry point.
 */
#define	ENTRY2(x, y) \
	.text;	\
	.align	ASM_ENTRY_ALIGN; \
	.globl	x, y; \
	.type	x, @function; \
	.type	y, @function; \
x:; \
y:	MCOUNT(x)

#define	ENTRY_NP2(x, y) \
	.text; \
	.align	ASM_ENTRY_ALIGN; \
	.globl	x, y; \
	.type	x, @function; \
	.type	y, @function; \
x:; \
y:


/*
 * SET_SIZE trails a function and set the size for the ELF symbol table.
 */
#define	SET_SIZE(x) \
	.size	x, [.-x]

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _IA32_SYS_ASM_LINKAGE_H */
