
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

#ifndef _AARCH64_SYS_ASM_LINKAGE_H
#define	_AARCH64_SYS_ASM_LINKAGE_H

/* You can set to nothing on Unix platforms */
#undef ASMABI
#define	ASMABI	__attribute__((sysv_abi))

#define	SECTION_TEXT .text
#define	SECTION_STATIC .const
#define	SECTION_STATIC1(...) .const
// #define	SECTION_STATIC1(...) .rodata##__VA_ARGS__

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
x:

#define	ENTRY_ALIGN(x, a) \
	.text %% \
	.balign	a %% \
	.globl _##x %% \
_##x: %% \
x:

#define	FUNCTION(x) \
x:

#define	SET_SIZE(x)

#define	SET_OBJ(x)


#endif
