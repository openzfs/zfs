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

#define	SECTION_TEXT .text
#define	SECTION_STATIC .section .rodata

#ifdef _ASM	/* The remainder of this file is only for assembly files */

#define	ASM_ENTRY_ALIGN	2

/*
 * ENTRY/ENTRY_ALIGN/SET_SIZE/LOCAL_LABEL for GNU as on ELF aarch64
 * targets. Symbol names need no leading underscore here, unlike the
 * macOS/Mach-O variant of this header.
 */
#define	ENTRY(x) \
	.text; \
	.globl	x; \
	.balign	ASM_ENTRY_ALIGN; \
	.type	x, #function; \
x:

#define	ENTRY_ALIGN(x, a) \
	.text; \
	.globl	x; \
	.balign	a; \
	.type	x, #function; \
x:

#define	FUNCTION(x) \
	.type	x, #function; \
x:

#define	SET_SIZE(x) \
	.size	x, . - x

#define	SET_OBJ(x) .type	x, #object

/*
 * LOCAL_LABEL defines a label which should not appear in the symbol table.
 */
#define	LOCAL_LABEL(x) .L##x

#endif /* _ASM */

#endif	/* _AARCH64_SYS_ASM_LINKAGE_H */
