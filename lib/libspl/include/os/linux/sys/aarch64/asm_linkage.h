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

#ifndef _AARCH64_SYS_ASM_LINKAGE_H
#define	_AARCH64_SYS_ASM_LINKAGE_H

#if defined(_KERNEL) && defined(__linux__)
#include <linux/linkage.h>
#endif

#define	SECTION_TEXT .text
#define	SECTION_STATIC .section .rodata

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM	/* The remainder of this file is only for assembly files */

#define	ASM_ENTRY_ALIGN	16

/*
 * ENTRY provides the standard procedure entry code.  Note that it emits
 * no instructions of its own: AArch64 callees differ in their prologue
 * (BTI landing pads vs. pointer authentication), so each function states
 * its own.
 */
#undef ENTRY
#define	ENTRY(x) \
	.text; \
	.balign	ASM_ENTRY_ALIGN; \
	.globl	x; \
	.type	x, %function; \
x:

#define	ENTRY_ALIGN(x, a) \
	.text; \
	.balign	a; \
	.globl	x; \
	.type	x, %function; \
x:

#define	FUNCTION(x) \
	.type	x, %function; \
x:

/*
 * SET_SIZE trails a function and set the size for the ELF symbol table.
 */
#define	SET_SIZE(x) \
	.size	x, [.-x]

#define	SET_OBJ(x) .type	x, %object

/*
 * LOCAL_LABEL defines a label which should not appear in the symbol table.
 */
#define	LOCAL_LABEL(x) .L##x

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _AARCH64_SYS_ASM_LINKAGE_H */
