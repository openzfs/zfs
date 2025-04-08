// SPDX-License-Identifier: BSD-2-Clause
/*-
 * Copyright 2022 Han Gao <gaohan@uniontech.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if __loongarch_lp64

#define ENTRY(symbol) \
  .text; \
  .globl symbol; \
  .align 3; \
  .type symbol, @function; \
  symbol:

#define END(function) \
  .size function, .- function;

ENTRY(setjmp)
	st.d	$ra, $a0, 0*8
	st.d	$sp, $a0, 1*8
	st.d	$r21, $a0, 2*8
	st.d	$fp, $a0, 3*8
	st.d	$s0, $a0, 4*8
	st.d	$s1, $a0, 5*8
	st.d	$s2, $a0, 6*8
	st.d	$s3, $a0, 7*8
	st.d	$s4, $a0, 8*8
	st.d	$s5, $a0, 9*8
	st.d	$s6, $a0, 10*8
	st.d	$s7, $a0, 11*8
	st.d	$s8, $a0, 12*8

	li.w	$a0, 0
	jr	$ra
END(setjmp)

ENTRY(longjmp)
	ld.d	$ra, $a0, 0*8
	ld.d	$sp, $a0, 1*8
	ld.d	$r21, $a0, 2*8
	ld.d	$fp, $a0, 3*8
	ld.d	$s0, $a0, 4*8
	ld.d	$s1, $a0, 5*8
	ld.d	$s2, $a0, 6*8
	ld.d	$s3, $a0, 7*8
	ld.d	$s4, $a0, 8*8
	ld.d	$s5, $a0, 9*8
	ld.d	$s6, $a0, 10*8
	ld.d	$s7, $a0, 11*8
	ld.d	$s8, $a0, 12*8

	sltui	$a0, $a1, 1
	add.d	$a0, $a0, $a1	// a0 = (a1 == 0) ? 1 : a1
	jr	$ra
END(longjmp)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif

#endif
