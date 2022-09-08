/*
 * Copyright (c) 2022 iXsystems, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef KERNEL_EXPORTS_X86_FPU

#include <sys/simd.h>

#if defined(HAVE_KERNEL_FPU)

void
kfpu_begin(void)
{
	kernel_fpu_begin();
}

void
kfpu_end(void)
{
	kernel_fpu_end();
}

#elif defined(HAVE_UNDERSCORE_KERNEL_FPU)

void
kfpu_begin(void)
{
	preempt_disable();
	__kernel_fpu_begin();
}

void
kfpu_end(void)
{
	__kernel_fpu_end();
	preempt_enable();
}

#else
/*
 * This case should be unreachable.  When KERNEL_EXPORTS_X86_FPU is defined
 * then either HAVE_UNDERSCORE_KERNEL_FPU or HAVE_KERNEL_FPU must be defined.
 */
#error "Unreachable kernel configuration"
#endif

EXPORT_SYMBOL(kfpu_begin);
EXPORT_SYMBOL(kfpu_end);

#endif /* KERNEL_EXPORTS_X86_FPU */
