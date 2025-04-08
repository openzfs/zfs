// SPDX-License-Identifier: BSD-2-Clause
/*-
 * Copyright 2004-2014 Olivier Houchard <cognet@FreeBSD.org>
 * Copyright 2012-2014 Ian Lepore <ian@FreeBSD.org>
 * Copyright 2013-2014 Andrew Turner <andrew@FreeBSD.org>
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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


#if defined(__arm__) && !defined(__aarch64__)

#if defined(__thumb2__)
#define	_FUNC_MODE	.code 16; .thumb_func
#else
#define	_FUNC_MODE	.code 32
#endif

#define	ENTRY(x) \
	.text; \
	.syntax unified; \
	.balign 2; \
	.global x; \
	.type x,#function; \
	_FUNC_MODE; \
x:

#define	END(x) \
	.size x, . - x;

#define	RET	bx	lr


/*
 * setjump + longjmp
 */
ENTRY(setjmp)
#if defined(__thumb2__)
	mov	ip, sp
	stmia	r0, {r4-r12,r14}
#else
	stmia	r0, {r4-r14}
#endif
	mov	r0, #0x00000000
	RET
END(setjmp)

ENTRY(longjmp)
#if defined(__thumb2__)
	ldmia	r0, {r4-r12,r14}
	mov	sp, ip
#else
	ldmia	r0, {r4-r14}
#endif
	mov	r0, #0x00000001
	RET
END(longjmp)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif

#endif
