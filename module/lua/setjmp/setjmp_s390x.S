// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2005-2014 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

	.global setjmp
	.type   setjmp,@function
setjmp:
	stmg %r6, %r15, 0(%r2)

	std  %f8,  10*8(%r2)
	std  %f9,  11*8(%r2)
	std  %f10, 12*8(%r2)
	std  %f11, 13*8(%r2)
	std  %f12, 14*8(%r2)
	std  %f13, 15*8(%r2)
	std  %f14, 16*8(%r2)
	std  %f15, 17*8(%r2)

	lghi %r2, 0
	br   %r14

	.global longjmp
	.type   longjmp,@function
longjmp:

1:
	lmg %r6, %r15, 0(%r2)

	ld  %f8, 10*8(%r2)
	ld  %f9, 11*8(%r2)
	ld %f10, 12*8(%r2)
	ld %f11, 13*8(%r2)
	ld %f12, 14*8(%r2)
	ld %f13, 15*8(%r2)
	ld %f14, 16*8(%r2)
	ld %f15, 17*8(%r2)

	ltgr %r2, %r3
	bnzr %r14
	lhi  %r2, 1
	br   %r14

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
