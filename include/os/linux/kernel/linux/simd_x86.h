/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
/*
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@compeng.uni-frankfurt.de>.
 */

/*
 * USER API:
 *
 * Kernel fpu methods:
 *	kfpu_allowed()
 *	kfpu_initialize()
 *	kfpu_begin()
 *	kfpu_end()
 *
 * SIMD support:
 *
 * Following functions should be called to determine whether CPU feature
 * is supported. All functions are usable in kernel and user space.
 * If a SIMD algorithm is using more than one instruction set
 * all relevant feature test functions should be called.
 *
 * Supported features:
 *	zfs_sse_available()
 *	zfs_sse2_available()
 *	zfs_sse3_available()
 *	zfs_ssse3_available()
 *	zfs_sse4_1_available()
 *	zfs_sse4_2_available()
 *
 *	zfs_avx_available()
 *	zfs_avx2_available()
 *
 *	zfs_bmi1_available()
 *	zfs_bmi2_available()
 *
 *	zfs_avx512f_available()
 *	zfs_avx512cd_available()
 *	zfs_avx512er_available()
 *	zfs_avx512pf_available()
 *	zfs_avx512bw_available()
 *	zfs_avx512dq_available()
 *	zfs_avx512vl_available()
 *	zfs_avx512ifma_available()
 *	zfs_avx512vbmi_available()
 *
 * NOTE(AVX-512VL):	If using AVX-512 instructions with 128Bit registers
 *			also add zfs_avx512vl_available() to feature check.
 */

#ifndef _LINUX_SIMD_X86_H
#define	_LINUX_SIMD_X86_H

/* only for __x86 */
#if defined(__x86)

#include <sys/types.h>
#include <asm/cpufeature.h>

/*
 * Disable the WARN_ON_FPU() macro to prevent additional dependencies
 * when providing the kfpu_* functions.  Relevant warnings are included
 * as appropriate and are unconditionally enabled.
 */
#if defined(CONFIG_X86_DEBUG_FPU) && !defined(KERNEL_EXPORTS_X86_FPU)
#undef CONFIG_X86_DEBUG_FPU
#endif

#if defined(HAVE_KERNEL_FPU_API_HEADER)
#include <asm/fpu/api.h>
#include <asm/fpu/internal.h>
#else
#include <asm/i387.h>
#include <asm/xcr.h>
#endif

/*
 * The following cases are for kernels which export either the
 * kernel_fpu_* or __kernel_fpu_* functions.
 */
#if defined(KERNEL_EXPORTS_X86_FPU)

#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)

#if defined(HAVE_UNDERSCORE_KERNEL_FPU)
#define	kfpu_begin()		\
{				\
	preempt_disable();	\
	__kernel_fpu_begin();	\
}
#define	kfpu_end()		\
{				\
	__kernel_fpu_end();	\
	preempt_enable();	\
}

#elif defined(HAVE_KERNEL_FPU)
#define	kfpu_begin()		kernel_fpu_begin()
#define	kfpu_end()		kernel_fpu_end()

#else
/*
 * This case is unreachable.  When KERNEL_EXPORTS_X86_FPU is defined then
 * either HAVE_UNDERSCORE_KERNEL_FPU or HAVE_KERNEL_FPU must be defined.
 */
#error "Unreachable kernel configuration"
#endif

#else /* defined(KERNEL_EXPORTS_X86_FPU) */
/*
 * When the kernel_fpu_* symbols are unavailable then provide our own
 * versions which allow the FPU to be safely used in kernel threads.
 * In practice, this is not a significant restriction for ZFS since the
 * vast majority of SIMD operations are performed by the IO pipeline.
 */

/*
 * Returns non-zero if FPU operations are allowed in the current context.
 */
#if defined(HAVE_KERNEL_TIF_NEED_FPU_LOAD)
#define	kfpu_allowed()		((current->flags & PF_KTHREAD) && \
				test_thread_flag(TIF_NEED_FPU_LOAD))
#elif defined(HAVE_KERNEL_FPU_INITIALIZED)
#define	kfpu_allowed()		((current->flags & PF_KTHREAD) && \
				current->thread.fpu.initialized)
#else
#define	kfpu_allowed()		0
#endif

static inline void
kfpu_initialize(void)
{
	WARN_ON_ONCE(!(current->flags & PF_KTHREAD));

#if defined(HAVE_KERNEL_TIF_NEED_FPU_LOAD)
	__fpu_invalidate_fpregs_state(&current->thread.fpu);
	set_thread_flag(TIF_NEED_FPU_LOAD);
#elif defined(HAVE_KERNEL_FPU_INITIALIZED)
	__fpu_invalidate_fpregs_state(&current->thread.fpu);
	current->thread.fpu.initialized = 1;
#endif
}

static inline void
kfpu_begin(void)
{
	WARN_ON_ONCE(!kfpu_allowed());

	/*
	 * Preemption and interrupts must be disabled for the critical
	 * region where the FPU state is being modified.
	 */
	preempt_disable();
	local_irq_disable();

#if defined(HAVE_KERNEL_TIF_NEED_FPU_LOAD)
	/*
	 * The current FPU registers need to be preserved by kfpu_begin()
	 * and restored by kfpu_end().  This is required because we can
	 * not call __cpu_invalidate_fpregs_state() to invalidate the
	 * per-cpu FPU state and force them to be restored during a
	 * context switch.
	 */
	copy_fpregs_to_fpstate(&current->thread.fpu);
#elif defined(HAVE_KERNEL_FPU_INITIALIZED)
	/*
	 * There is no need to preserve and restore the FPU registers.
	 * They will always be restored from the task's stored FPU state
	 * when switching contexts.
	 */
	WARN_ON_ONCE(current->thread.fpu.initialized == 0);
#endif
}

static inline void
kfpu_end(void)
{
#if defined(HAVE_KERNEL_TIF_NEED_FPU_LOAD)
	union fpregs_state *state = &current->thread.fpu.state;
	int error;

	if (use_xsave()) {
		error = copy_kernel_to_xregs_err(&state->xsave, -1);
	} else if (use_fxsr()) {
		error = copy_kernel_to_fxregs_err(&state->fxsave);
	} else {
		error = copy_kernel_to_fregs_err(&state->fsave);
	}
	WARN_ON_ONCE(error);
#endif

	local_irq_enable();
	preempt_enable();
}
#endif /* defined(HAVE_KERNEL_FPU) */

/*
 * Linux kernel provides an interface for CPU feature testing.
 */
/*
 * Detect register set support
 */
static inline boolean_t
__simd_state_enabled(const uint64_t state)
{
	boolean_t has_osxsave;
	uint64_t xcr0;

#if defined(X86_FEATURE_OSXSAVE)
	has_osxsave = !!boot_cpu_has(X86_FEATURE_OSXSAVE);
#else
	has_osxsave = B_FALSE;
#endif
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
	return (!!boot_cpu_has(X86_FEATURE_XMM));
}

/*
 * Check if SSE2 instruction set is available
 */
static inline boolean_t
zfs_sse2_available(void)
{
	return (!!boot_cpu_has(X86_FEATURE_XMM2));
}

/*
 * Check if SSE3 instruction set is available
 */
static inline boolean_t
zfs_sse3_available(void)
{
	return (!!boot_cpu_has(X86_FEATURE_XMM3));
}

/*
 * Check if SSSE3 instruction set is available
 */
static inline boolean_t
zfs_ssse3_available(void)
{
	return (!!boot_cpu_has(X86_FEATURE_SSSE3));
}

/*
 * Check if SSE4.1 instruction set is available
 */
static inline boolean_t
zfs_sse4_1_available(void)
{
	return (!!boot_cpu_has(X86_FEATURE_XMM4_1));
}

/*
 * Check if SSE4.2 instruction set is available
 */
static inline boolean_t
zfs_sse4_2_available(void)
{
	return (!!boot_cpu_has(X86_FEATURE_XMM4_2));
}

/*
 * Check if AVX instruction set is available
 */
static inline boolean_t
zfs_avx_available(void)
{
	return (boot_cpu_has(X86_FEATURE_AVX) && __ymm_enabled());
}

/*
 * Check if AVX2 instruction set is available
 */
static inline boolean_t
zfs_avx2_available(void)
{
	return (boot_cpu_has(X86_FEATURE_AVX2) && __ymm_enabled());
}

/*
 * Check if BMI1 instruction set is available
 */
static inline boolean_t
zfs_bmi1_available(void)
{
#if defined(X86_FEATURE_BMI1)
	return (!!boot_cpu_has(X86_FEATURE_BMI1));
#else
	return (B_FALSE);
#endif
}

/*
 * Check if BMI2 instruction set is available
 */
static inline boolean_t
zfs_bmi2_available(void)
{
#if defined(X86_FEATURE_BMI2)
	return (!!boot_cpu_has(X86_FEATURE_BMI2));
#else
	return (B_FALSE);
#endif
}

/*
 * Check if AES instruction set is available
 */
static inline boolean_t
zfs_aes_available(void)
{
#if defined(X86_FEATURE_AES)
	return (!!boot_cpu_has(X86_FEATURE_AES));
#else
	return (B_FALSE);
#endif
}

/*
 * Check if PCLMULQDQ instruction set is available
 */
static inline boolean_t
zfs_pclmulqdq_available(void)
{
#if defined(X86_FEATURE_PCLMULQDQ)
	return (!!boot_cpu_has(X86_FEATURE_PCLMULQDQ));
#else
	return (B_FALSE);
#endif
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

/*
 * Check if AVX512F instruction set is available
 */
static inline boolean_t
zfs_avx512f_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512F)
	has_avx512 = !!boot_cpu_has(X86_FEATURE_AVX512F);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512CD instruction set is available
 */
static inline boolean_t
zfs_avx512cd_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512CD)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512CD);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512ER instruction set is available
 */
static inline boolean_t
zfs_avx512er_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512ER)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512ER);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512PF instruction set is available
 */
static inline boolean_t
zfs_avx512pf_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512PF)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512PF);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512BW instruction set is available
 */
static inline boolean_t
zfs_avx512bw_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512BW)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512BW);
#endif

	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512DQ instruction set is available
 */
static inline boolean_t
zfs_avx512dq_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512DQ)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512DQ);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512VL instruction set is available
 */
static inline boolean_t
zfs_avx512vl_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512VL)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512VL);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512IFMA instruction set is available
 */
static inline boolean_t
zfs_avx512ifma_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512IFMA)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512IFMA);
#endif
	return (has_avx512 && __zmm_enabled());
}

/*
 * Check if AVX512VBMI instruction set is available
 */
static inline boolean_t
zfs_avx512vbmi_available(void)
{
	boolean_t has_avx512 = B_FALSE;

#if defined(X86_FEATURE_AVX512VBMI)
	has_avx512 = boot_cpu_has(X86_FEATURE_AVX512F) &&
	    boot_cpu_has(X86_FEATURE_AVX512VBMI);
#endif
	return (has_avx512 && __zmm_enabled());
}

#endif /* defined(__x86) */

#endif /* _LINUX_SIMD_X86_H */
