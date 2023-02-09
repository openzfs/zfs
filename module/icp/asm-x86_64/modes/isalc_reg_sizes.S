////////////////////////////////////////////////////////////////////////////////
//  Copyright(c) 2011-2019 Intel Corporation All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions
//  are met:
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in
//      the documentation and/or other materials provided with the
//      distribution.
//    * Neither the name of Intel Corporation nor the names of its
//      contributors may be used to endorse or promote products derived
//      from this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES// LOSS OF USE,
//  DATA, OR PROFITS// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////

// Port to GNU as, translation to GNU as att-syntax and adoptions for the ICP
// Copyright(c) 2023 Attila Fülöp <attila@fueloep.org>

#ifndef _REG_SIZES_ASM_
#define _REG_SIZES_ASM_


// define d, w  and b variants for registers

.macro dwordreg reg
	.if \reg == %r8 || \reg == %r9 || \reg == %r10 || \reg == %r11 || \reg == %r12 || \reg == %r13 || \reg == %r14 || \reg == %r15
		.set dreg, \reg\()d
	.elseif \reg == %rax
		.set dreg, %eax
	.elseif \reg == %rcx
		.set dreg, %ecx
	.elseif \reg == %rdx
		.set dreg, %edx
	.elseif \reg == %rbx
		.set dreg, %ebx
	.elseif \reg == %rsp
		.set dreg, %esp
	.elseif \reg == %rbp
		.set dreg, %ebp
	.elseif \reg == %rsi
		.set dreg, %esi
	.elseif \reg == %rdi
		.set dreg, %edi
	.else
		.error "Invalid register '\reg\()' while expanding macro 'dwordreg\()'"
	.endif
.endm

.macro wordreg reg
	.if \reg == %r8 || \reg == %r9 || \reg == %r10 || \reg == %r11 || \reg == %r12 || \reg == %r13 || \reg == %r14 || \reg == %r15
		.set wreg, \reg\()w
	.elseif \reg == %rax
		.set wreg, %ax
	.elseif \reg == %rcx
		.set wreg, %cx
	.elseif \reg == %rdx
		.set wreg, %dx
	.elseif \reg == %rbx
		.set wreg, %bx
	.elseif \reg == %rsp
		.set wreg, %sp
	.elseif \reg == %rbp
		.set wreg, %bp
	.elseif \reg == %rsi
		.set wreg, %si
	.elseif \reg == %rdi
		.set wreg, %di
	.else
		.error "Invalid register '\reg\()' while expanding macro 'wordreg\()'"
	.endif
.endm


.macro bytereg reg
	.if \reg == %r8 || \reg == %r9 || \reg == %r10 || \reg == %r11 || \reg == %r12 || \reg == %r13 || \reg == %r14 || \reg == %r15
		.set breg, \reg\()b
	.elseif \reg == %rax
		.set breg, %al
	.elseif \reg == %rcx
		.set breg, %cl
	.elseif \reg == %rdx
		.set breg, %dl
	.elseif \reg == %rbx
		.set breg, %bl
	.elseif \reg == rsp
		.set breg, %spl
	.elseif \reg == %rbp
		.set breg, %bpl
	.elseif \reg == rsi
		.set breg, %sil
	.elseif \reg == rdi
		.set breg, %dil
	.else
		.error "Invalid register '\reg\()' while expanding macro 'bytereg\()'"
	.endif
.endm

// clang compat: Below won't owrk with clang; do it a bit different
// #define ZERO_TO_THIRTYONE \
// 	0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16, \
// 	17,18,19,20,21,22,23,24,25,26,27,28,29,30,31

// .macro xword reg
// 	.irep i, ZERO_TO_THIRTYONE
// 		.if \reg == %xmm\i || \reg == %ymm\i || \reg == %zmm\i
// 			.set xmmreg, %xmm\i
// 		.endif
// 	.endr
// .endm

// .macro yword reg
// 	.irep i, ZERO_TO_THIRTYONE
// 		.if \reg == %xmm\i || \reg == %ymm\i || \reg == %zmm\i
// 			.set ymmreg, %ymm\i
// 		.endif
// 	.endr
// .endm

// .macro zword reg
// 	.irep i, ZERO_TO_THIRTYONE
// 		.if \reg == %xmm\i || \reg == %ymm\i || \reg == %zmm\i
// 			.set zmmreg, %zmm\i
// 		.endif
// 	.endr
// .endm

// Example usage:
// xword %zmm12
// pxor xmmreg, xmmreg // =>  pxor %xmm12, %xmm12
.macro xword reg
	.set i, 0
	.rep 32
		.altmacro
		do_xyzword <\reg>, xmm, %i
		.noaltmacro
	.set i, (i+1)
	.endr
.endm

.macro yword reg
	.set i, 0
	.rep 32
		.altmacro
		do_xyzword <\reg>, ymm, %i
		.noaltmacro
	.set i, (i+1)
	.endr
.endm

.macro zword reg
	.set i, 0
	.rep 32
		.altmacro
		do_xyzword <\reg>, zmm, %i
		.noaltmacro
	.set i, (i+1)
	.endr
.endm

.macro  do_xyzword creg, prfx, idx
	.if \creg == %xmm\idx || \creg == %ymm\idx || \creg == %zmm\idx
		.set \prfx\()reg, %\prfx\idx
	.endif
.endm


// FIXME: handle later
#define	elf32		1
#define	elf64		2
#define	win64		3
#define	machos64	4

#ifndef __OUTPUT_FORMAT__
#define __OUTPUT_FORMAT__ elf64
#endif

#if __OUTPUT_FORMAT__ == elf32
.section .note.GNU-stack,"",%progbits
.section .text
#endif
#if __OUTPUT_FORMAT__ == elf64
#ifndef __x86_64__
#define __x86_64__
#endif
.section .note.GNU-stack,"",%progbits
.section .text
#endif
#if __OUTPUT_FORMAT__ == win64
#define __x86_64__
#endif
#if __OUTPUT_FORMAT__ == macho64
#define __x86_64__
#endif


#ifdef REL_TEXT
#define WRT_OPT
#elif __OUTPUT_FORMAT__ == elf64
#define WRT_OPT        wrt ..plt
#else
#define WRT_OPT
#endif

#endif // ifndef _REG_SIZES_ASM_
