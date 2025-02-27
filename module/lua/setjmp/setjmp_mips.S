// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 * Copyright (c) 2017 MIPS Technologies, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <asm/asm.h>
#include <asm/regdef.h>

/*
 * setjmp and longjmp for MIPS.
 */

   .text
   .set noreorder

   /*
    * int setjmp(jmp_buf jb);
    *
    * Save the current state so we can return again from the call later
    * if/when longjmp is called. (If the function that called setjmp
    * returns before longjmp is called, the results are undefined. We
    * only need to save registers, not the whole contents of the stack.)
    */
LEAF(setjmp)
   /*
    * jmp_buf is in a0. We need to save s0-s8, sp, gp, and ra in it.
    * Don't store more registers without adjusting machine/setjmp.h.
    */

   REG_S sp, 0(a0)		/* save registers */
   REG_S ra, 1*SZREG(a0)
   REG_S gp, 2*SZREG(a0)
   REG_S s0, 3*SZREG(a0)
   REG_S s1, 4*SZREG(a0)
   REG_S s2, 5*SZREG(a0)
   REG_S s3, 6*SZREG(a0)
   REG_S s4, 7*SZREG(a0)
   REG_S s5, 8*SZREG(a0)
   REG_S s6, 9*SZREG(a0)
   REG_S s7, 10*SZREG(a0)
   REG_S s8, 11*SZREG(a0)

   jr ra		/* done */
    move v0, zero	/* return 0 (in delay slot) */
END(setjmp)


   /*
    * void longjmp(jmp_buf jb, int code);
    */
LEAF(longjmp)
   /*
    * jmp_buf is in a0. Return code is in a1.
    * We need to restore s0-s8, sp, gp, and ra from the jmp_buf.
    * The return code is forced to 1 if 0 is passed in.
    */

   sltiu t0, a1, 1	/* set t0 to 1 if return code is 0... otherwise 0 */
   addu a1, a1, t0	/* update the return code */

   REG_L sp, 0(a0)		/* restore registers */
   REG_L ra, 1*SZREG(a0)
   REG_L gp, 2*SZREG(a0)
   REG_L s0, 3*SZREG(a0)
   REG_L s1, 4*SZREG(a0)
   REG_L s2, 5*SZREG(a0)
   REG_L s3, 6*SZREG(a0)
   REG_L s4, 7*SZREG(a0)
   REG_L s5, 8*SZREG(a0)
   REG_L s6, 9*SZREG(a0)
   REG_L s7, 10*SZREG(a0)
   REG_L s8, 11*SZREG(a0)

   jr ra		/* return, to where setjmp was called from */
    move v0, a1		/* set return value */
END(longjmp)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
