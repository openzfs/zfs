/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ASM_LINKAGE_H
#define	_SYS_ASM_LINKAGE_H



#include <sys/stack.h>
#include <sys/trap.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM	/* The remainder of this file is only for assembly files */

/*
 * C pointers are different sizes between V8 and V9.
 * These constants can be used to compute offsets into pointer arrays.
 */
#ifdef __sparcv9
#define	CPTRSHIFT	3
#define	CLONGSHIFT	3
#else
#define	CPTRSHIFT	2
#define	CLONGSHIFT	2
#endif
#define	CPTRSIZE	(1<<CPTRSHIFT)
#define	CLONGSIZE	(1<<CLONGSHIFT)
#define	CPTRMASK	(CPTRSIZE - 1)
#define	CLONGMASK	(CLONGSIZE - 1)

/*
 * Symbolic section definitions.
 */
#define	RODATA	".rodata"

/*
 * profiling causes defintions of the MCOUNT and RTMCOUNT
 * particular to the type
 */
#ifdef GPROF

#define	MCOUNT_SIZE	(4*4)	/* 4 instructions */
#define	MCOUNT(x) \
	save	%sp, -SA(MINFRAME), %sp; \
	call	_mcount; \
	nop; \
	restore;

#endif /* GPROF */

#ifdef PROF

#if defined(__sparcv9)

#define	MCOUNT_SIZE	(9*4)	/* 9 instructions */
#define	MCOUNT(x) \
	save	%sp, -SA(MINFRAME), %sp; \
/* CSTYLED */ \
	sethi	%hh(.L_/**/x/**/1), %o0; \
/* CSTYLED */ \
	sethi	%lm(.L_/**/x/**/1), %o1; \
/* CSTYLED */ \
	or	%o0, %hm(.L_/**/x/**/1), %o0; \
/* CSTYLED */ \
	or	%o1, %lo(.L_/**/x/**/1), %o1; \
	sllx	%o0, 32, %o0; \
	call	_mcount; \
	or	%o0, %o1, %o0; \
	restore; \
/* CSTYLED */ \
	.common .L_/**/x/**/1, 8, 8

#else	/* __sparcv9 */

#define	MCOUNT_SIZE	(5*4)	/* 5 instructions */
#define	MCOUNT(x) \
	save	%sp, -SA(MINFRAME), %sp; \
/* CSTYLED */ \
	sethi	%hi(.L_/**/x/**/1), %o0; \
	call	_mcount; \
/* CSTYLED */ \
	or	%o0, %lo(.L_/**/x/**/1), %o0; \
	restore; \
/* CSTYLED */ \
	.common .L_/**/x/**/1, 4, 4

#endif	/* __sparcv9 */

#endif /* PROF */

/*
 * if we are not profiling, MCOUNT should be defined to nothing
 */
#if !defined(PROF) && !defined(GPROF)
#define	MCOUNT_SIZE	0	/* no instructions inserted */
#define	MCOUNT(x)
#endif /* !defined(PROF) && !defined(GPROF) */

#define	RTMCOUNT(x)	MCOUNT(x)

/*
 * Macro to define weak symbol aliases. These are similar to the ANSI-C
 *	#pragma weak name = _name
 * except a compiler can determine type. The assembler must be told. Hence,
 * the second parameter must be the type of the symbol (i.e.: function,...)
 */
#define	ANSI_PRAGMA_WEAK(sym, stype)	\
	.weak	sym; \
	.type sym, #stype; \
/* CSTYLED */ \
sym	= _/**/sym

/*
 * Like ANSI_PRAGMA_WEAK(), but for unrelated names, as in:
 *	#pragma weak sym1 = sym2
 */
#define	ANSI_PRAGMA_WEAK2(sym1, sym2, stype)	\
	.weak	sym1; \
	.type sym1, #stype; \
sym1	= sym2

/*
 * ENTRY provides the standard procedure entry code and an easy way to
 * insert the calls to mcount for profiling. ENTRY_NP is identical, but
 * never calls mcount.
 */
#define	ENTRY(x) \
	.section	".text"; \
	.align	4; \
	.global	x; \
	.type	x, #function; \
x:	MCOUNT(x)

#define	ENTRY_SIZE	MCOUNT_SIZE

#define	ENTRY_NP(x) \
	.section	".text"; \
	.align	4; \
	.global	x; \
	.type	x, #function; \
x:

#define	RTENTRY(x) \
	.section	".text"; \
	.align	4; \
	.global	x; \
	.type	x, #function; \
x:	RTMCOUNT(x)

/*
 * ENTRY2 is identical to ENTRY but provides two labels for the entry point.
 */
#define	ENTRY2(x, y) \
	.section	".text"; \
	.align	4; \
	.global	x, y; \
	.type	x, #function; \
	.type	y, #function; \
/* CSTYLED */ \
x:	; \
y:	MCOUNT(x)

#define	ENTRY_NP2(x, y) \
	.section	".text"; \
	.align	4; \
	.global	x, y; \
	.type	x, #function; \
	.type	y, #function; \
/* CSTYLED */ \
x:	; \
y:


/*
 * ALTENTRY provides for additional entry points.
 */
#define	ALTENTRY(x) \
	.global x; \
	.type	x, #function; \
x:

/*
 * DGDEF and DGDEF2 provide global data declarations.
 *
 * DGDEF provides a word aligned word of storage.
 *
 * DGDEF2 allocates "sz" bytes of storage with **NO** alignment.  This
 * implies this macro is best used for byte arrays.
 *
 * DGDEF3 allocates "sz" bytes of storage with "algn" alignment.
 */
#define	DGDEF2(name, sz) \
	.section	".data"; \
	.global name; \
	.type	name, #object; \
	.size	name, sz; \
name:

#define	DGDEF3(name, sz, algn) \
	.section	".data"; \
	.align	algn; \
	.global name; \
	.type	name, #object; \
	.size	name, sz; \
name:

#define	DGDEF(name)	DGDEF3(name, 4, 4)

/*
 * SET_SIZE trails a function and set the size for the ELF symbol table.
 */
#define	SET_SIZE(x) \
	.size	x, (.-x)

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ASM_LINKAGE_H */
