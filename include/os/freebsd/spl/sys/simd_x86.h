/*
 * Copyright (c) 2020 iXsystems, Inc.
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/pcb.h>
#include <x86/x86_var.h>
#include <x86/specialreg.h>

#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)

#define	kfpu_begin() {					\
	if (__predict_false(!is_fpu_kern_thread(0)))		\
		fpu_kern_enter(curthread, NULL, FPU_KERN_NOCTX);\
}

#define	kfpu_end()	{			\
	if (__predict_false(curpcb->pcb_flags & PCB_FPUNOSAVE))	\
		fpu_kern_leave(curthread, NULL);	\
}

/*
 * Check if OS supports AVX and AVX2 by checking XCR0
 * Only call this function if CPUID indicates that AVX feature is
 * supported by the CPU, otherwise it might be an illegal instruction.
 */
static inline uint64_t
xgetbv(uint32_t index)
{
	uint32_t eax, edx;
	/* xgetbv - instruction byte code */
	__asm__ __volatile__(".byte 0x0f; .byte 0x01; .byte 0xd0"
	    : "=a" (eax), "=d" (edx)
	    : "c" (index));

	return ((((uint64_t)edx)<<32) | (uint64_t)eax);
}


/*
 * Detect register set support
 */
static inline boolean_t
__simd_state_enabled(const uint64_t state)
{
	boolean_t has_osxsave;
	uint64_t xcr0;

	has_osxsave = !!(cpu_feature2 & CPUID2_OSXSAVE);

	if (!has_osxsave)
		return (B_FALSE);

	xcr0 = xgetbv(0);
	return ((xcr0 & state) == state);
}

#define	_XSTATE_SSE_AVX		(0x2 | 0x4)
#define	_XSTATE_AVX512		(0xE0 | _XSTATE_SSE_AVX)

#define	__ymm_enabled() __simd_state_enabled(_XSTATE_SSE_AVX)
#define	__zmm_enabled() __simd_state_enabled(_XSTATE_AVX512)


/*
 * Check if SSE instruction set is available
 */
static inline boolean_t
zfs_sse_available(void)
{
	return (!!(cpu_feature & CPUID_SSE));
}

/*
 * Check if SSE2 instruction set is available
 */
static inline boolean_t
zfs_sse2_available(void)
{
	return (!!(cpu_feature & CPUID_SSE2));
}

/*
 * Check if SSE3 instruction set is available
 */
static inline boolean_t
zfs_sse3_available(void)
{
	return (!!(cpu_feature2 & CPUID2_SSE3));
}

/*
 * Check if SSSE3 instruction set is available
 */
static inline boolean_t
zfs_ssse3_available(void)
{
	return (!!(cpu_feature2 & CPUID2_SSSE3));
}

/*
 * Check if SSE4.1 instruction set is available
 */
static inline boolean_t
zfs_sse4_1_available(void)
{
	return (!!(cpu_feature2 & CPUID2_SSE41));
}

/*
 * Check if SSE4.2 instruction set is available
 */
static inline boolean_t
zfs_sse4_2_available(void)
{
	return (!!(cpu_feature2 & CPUID2_SSE42));
}

/*
 * Check if AVX instruction set is available
 */
static inline boolean_t
zfs_avx_available(void)
{
	boolean_t has_avx;

	has_avx = !!(cpu_feature2 & CPUID2_AVX);

	return (has_avx && __ymm_enabled());
}

/*
 * Check if AVX2 instruction set is available
 */
static inline boolean_t
zfs_avx2_available(void)
{
	boolean_t has_avx2;

	has_avx2 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX2);

	return (has_avx2 && __ymm_enabled());
}

/*
 * AVX-512 family of instruction sets:
 *
 * AVX512F	Foundation
 * AVX512CD	Conflict Detection Instructions
 * AVX512ER	Exponential and Reciprocal Instructions
 * AVX512PF	Prefetch Instructions
 *
 * AVX512BW	Byte and Word Instructions
 * AVX512DQ	Double-word and Quadword Instructions
 * AVX512VL	Vector Length Extensions
 *
 * AVX512IFMA	Integer Fused Multiply Add (Not supported by kernel 4.4)
 * AVX512VBMI	Vector Byte Manipulation Instructions
 */


/* Check if AVX512F instruction set is available */
static inline boolean_t
zfs_avx512f_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512CD instruction set is available */
static inline boolean_t
zfs_avx512cd_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_AVX512CD);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512ER instruction set is available */
static inline boolean_t
zfs_avx512er_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_AVX512CD);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512PF instruction set is available */
static inline boolean_t
zfs_avx512pf_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_AVX512PF);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512BW instruction set is available */
static inline boolean_t
zfs_avx512bw_available(void)
{
	boolean_t has_avx512 = B_FALSE;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512BW);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512DQ instruction set is available */
static inline boolean_t
zfs_avx512dq_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_AVX512DQ);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512VL instruction set is available */
static inline boolean_t
zfs_avx512vl_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_AVX512VL);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512IFMA instruction set is available */
static inline boolean_t
zfs_avx512ifma_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_AVX512IFMA);

	return (has_avx512 && __zmm_enabled());
}

/* Check if AVX512VBMI instruction set is available */
static inline boolean_t
zfs_avx512vbmi_available(void)
{
	boolean_t has_avx512;

	has_avx512 = !!(cpu_stdext_feature & CPUID_STDEXT_AVX512F) &&
	    !!(cpu_stdext_feature & CPUID_STDEXT_BMI1);

	return (has_avx512 && __zmm_enabled());
}
