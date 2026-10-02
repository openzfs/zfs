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

/* You can set to nothing on Unix platforms */
#undef ASMABI
#define	ASMABI	__attribute__((sysv_abi))

#define	SECTION_TEXT .text
#define	SECTION_STATIC .const
#define	SECTION_STATIC1(...) .const
// #define	SECTION_STATIC1(x) .rodata##x

#define	ASM_ENTRY_ALIGN	16

#define	PAGE @PAGE
#define	PAGEOFF @PAGEOFF
// Linux has them empty
// #define	PAGE
// #define	PAGEOFF

/*
 * semi-colon is comment, so use secret %%
 * M1 is 64 bit only
 * and needs "_" prepended, but we add one without, in case
 * the assembler function needs to call itself
 */
#define	ENTRY(x) \
    .text %% \
    .balign ASM_ENTRY_ALIGN %% \
    .globl _##x %% \
_##x: %% \
x: \
	bti c // hint	#34

#define	ENTRY_ALIGN(x, a) \
	.text %% \
	.balign	a %% \
	.globl _##x %% \
_##x: %% \
x: \
	bti c // hint	#34

#define	FUNCTION(x) \
x:

#define	SET_SIZE(x)

#define	SET_OBJ(x)


#endif
