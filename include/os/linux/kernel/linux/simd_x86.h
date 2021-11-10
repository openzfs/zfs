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
 *	kfpu_begin()
 *	kfpu_end()
 *	kfpu_init()
 *	kfpu_fini()
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
#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

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
 * versions which allow the FPU to be safely used.
 */
#if defined(HAVE_KERNEL_FPU_INTERNAL)

#include <linux/mm.h>

extern union fpregs_state **zfs_kfpu_fpregs;

/*
 * Initialize per-cpu variables to store FPU state.
 */
static inline void
kfpu_fini(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (zfs_kfpu_fpregs[cpu] != NULL) {
			free_pages((unsigned long)zfs_kfpu_fpregs[cpu],
			    get_order(sizeof (union fpregs_state)));
		}
	}

	kfree(zfs_kfpu_fpregs);
}

static inline int
kfpu_init(void)
{
	zfs_kfpu_fpregs = kzalloc(num_possible_cpus() *
	    sizeof (union fpregs_state *), GFP_KERNEL);
	if (zfs_kfpu_fpregs == NULL)
		return (-ENOMEM);

	/*
	 * The fxsave and xsave operations require 16-/64-byte alignment of
	 * the target memory. Since kmalloc() provides no alignment
	 * guarantee instead use alloc_pages_node().
	 */
	unsigned int order = get_order(sizeof (union fpregs_state));
	int cpu;

	for_each_possible_cpu(cpu) {
		struct page *page = alloc_pages_node(cpu_to_node(cpu),
		    GFP_KERNEL | __GFP_ZERO, order);
		if (page == NULL) {
			kfpu_fini();
			return (-ENOMEM);
		}

		zfs_kfpu_fpregs[cpu] = page_address(page);
	}

	return (0);
}

#define	kfpu_allowed()		1
#define	ex_handler_fprestore	ex_handler_default

/*
 * FPU save and restore instructions.
 */
#define	__asm			__asm__ __volatile__
#define	kfpu_fxsave(addr)	__asm("fxsave %0" : "=m" (*(addr)))
#define	kfpu_fxsaveq(addr)	__asm("fxsaveq %0" : "=m" (*(addr)))
#define	kfpu_fnsave(addr)	__asm("fnsave %0; fwait" : "=m" (*(addr)))
#define	kfpu_fxrstor(addr)	__asm("fxrstor %0" : : "m" (*(addr)))
#define	kfpu_fxrstorq(addr)	__asm("fxrstorq %0" : : "m" (*(addr)))
#define	kfpu_frstor(addr)	__asm("frstor %0" : : "m" (*(addr)))
#define	kfpu_fxsr_clean(rval)	__asm("fnclex; emms; fildl %P[addr]" \
				    : : [addr] "m" (rval));

static inline void
kfpu_save_xsave(struct xregs_state *addr, uint64_t mask)
{
	uint32_t low, hi;
	int err;

	low = mask;
	hi = mask >> 32;
	XSTATE_XSAVE(addr, low, hi, err);
	WARN_ON_ONCE(err);
}

static inline void
kfpu_save_fxsr(struct fxregs_state *addr)
{
	if (IS_ENABLED(CONFIG_X86_32))
		kfpu_fxsave(addr);
	else
		kfpu_fxsaveq(addr);
}

static inline void
kfpu_save_fsave(struct fregs_state *addr)
{
	kfpu_fnsave(addr);
}

static inline void
kfpu_begin(void)
{
	/*
	 * Preemption and interrupts must be disabled for the critical
	 * region where the FPU state is being modified.
	 */
	preempt_disable();
	local_irq_disable();

	/*
	 * The current FPU registers need to be preserved by kfpu_begin()
	 * and restored by kfpu_end().  They are stored in a dedicated
	 * per-cpu variable, not in the task struct, this allows any user
	 * FPU state to be correctly preserved and restored.
	 */
	union fpregs_state *state = zfs_kfpu_fpregs[smp_processor_id()];

	if (static_cpu_has(X86_FEATURE_XSAVE)) {
		kfpu_save_xsave(&state->xsave, ~0);
	} else if (static_cpu_has(X86_FEATURE_FXSR)) {
		kfpu_save_fxsr(&state->fxsave);
	} else {
		kfpu_save_fsave(&state->fsave);
	}
}

static inline void
kfpu_restore_xsave(struct xregs_state *addr, uint64_t mask)
{
	uint32_t low, hi;

	low = mask;
	hi = mask >> 32;
	XSTATE_XRESTORE(addr, low, hi);
}

static inline void
kfpu_restore_fxsr(struct fxregs_state *addr)
{
	/*
	 * On AuthenticAMD K7 and K8 processors the fxrstor instruction only
	 * restores the _x87 FOP, FIP, and FDP registers when an exception
	 * is pending.  Clean the _x87 state to force the restore.
	 */
	if (unlikely(static_cpu_has_bug(X86_BUG_FXSAVE_LEAK)))
		kfpu_fxsr_clean(addr);

	if (IS_ENABLED(CONFIG_X86_32)) {
		kfpu_fxrstor(addr);
	} else {
		kfpu_fxrstorq(addr);
	}
}

static inline void
kfpu_restore_fsave(struct fregs_state *addr)
{
	kfpu_frstor(addr);
}

static inline void
kfpu_end(void)
{
	union fpregs_state *state = zfs_kfpu_fpregs[smp_processor_id()];

	if (static_cpu_has(X86_FEATURE_XSAVE)) {
		kfpu_restore_xsave(&state->xsave, ~0);
	} else if (static_cpu_has(X86_FEATURE_FXSR)) {
		kfpu_restore_fxsr(&state->fxsave);
	} else {
		kfpu_restore_fsave(&state->fsave);
	}

	local_irq_enable();
	preempt_enable();
}

#else

/*
 * FPU support is unavailable.
 */
#define	kfpu_allowed()		0
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

#endif /* defined(HAVE_KERNEL_FPU_INTERNAL) */
#endif /* defined(KERNEL_EXPORTS_X86_FPU) */

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
 * Check if MOVBE instruction is available
 */
static inline boolean_t
zfs_movbe_available(void)
{
#if defined(X86_FEATURE_MOVBE)
	return (!!boot_cpu_has(X86_FEATURE_MOVBE));
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
