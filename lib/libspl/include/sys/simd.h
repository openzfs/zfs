/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_SIMD_H
#define	_LIBSPL_SYS_SIMD_H

#include <sys/isa_defs.h>
#include <sys/types.h>

#if defined(__x86)
#include <cpuid.h>

#define	kfpu_allowed()		1
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

/*
 * CPUID feature tests for user-space.
 *
 * x86 registers used implicitly by CPUID
 */
typedef enum cpuid_regs {
	EAX = 0,
	EBX,
	ECX,
	EDX,
	CPUID_REG_CNT = 4
} cpuid_regs_t;

/*
 * List of instruction sets identified by CPUID
 */
typedef enum cpuid_inst_sets {
	SSE = 0,
	SSE2,
	SSE3,
	SSSE3,
	SSE4_1,
	SSE4_2,
	OSXSAVE,
	AVX,
	AVX2,
	BMI1,
	BMI2,
	AVX512F,
	AVX512CD,
	AVX512DQ,
	AVX512BW,
	AVX512IFMA,
	AVX512VBMI,
	AVX512PF,
	AVX512ER,
	AVX512VL,
	AES,
	PCLMULQDQ,
	MOVBE
} cpuid_inst_sets_t;

/*
 * Instruction set descriptor.
 */
typedef struct cpuid_feature_desc {
	uint32_t leaf;		/* CPUID leaf */
	uint32_t subleaf;	/* CPUID sub-leaf */
	uint32_t flag;		/* bit mask of the feature */
	cpuid_regs_t reg;	/* which CPUID return register to test */
} cpuid_feature_desc_t;

#define	_AVX512F_BIT		(1U << 16)
#define	_AVX512CD_BIT		(_AVX512F_BIT | (1U << 28))
#define	_AVX512DQ_BIT		(_AVX512F_BIT | (1U << 17))
#define	_AVX512BW_BIT		(_AVX512F_BIT | (1U << 30))
#define	_AVX512IFMA_BIT		(_AVX512F_BIT | (1U << 21))
#define	_AVX512VBMI_BIT		(1U << 1) /* AVX512F_BIT is on another leaf  */
#define	_AVX512PF_BIT		(_AVX512F_BIT | (1U << 26))
#define	_AVX512ER_BIT		(_AVX512F_BIT | (1U << 27))
#define	_AVX512VL_BIT		(1U << 31) /* if used also check other levels */
#define	_AES_BIT		(1U << 25)
#define	_PCLMULQDQ_BIT		(1U << 1)
#define	_MOVBE_BIT		(1U << 22)

/*
 * Descriptions of supported instruction sets
 */
static const cpuid_feature_desc_t cpuid_features[] = {
	[SSE]		= {1U, 0U,	1U << 25,	EDX	},
	[SSE2]		= {1U, 0U,	1U << 26,	EDX	},
	[SSE3]		= {1U, 0U,	1U << 0,	ECX	},
	[SSSE3]		= {1U, 0U,	1U << 9,	ECX	},
	[SSE4_1]	= {1U, 0U,	1U << 19,	ECX	},
	[SSE4_2]	= {1U, 0U,	1U << 20,	ECX	},
	[OSXSAVE]	= {1U, 0U,	1U << 27,	ECX	},
	[AVX]		= {1U, 0U,	1U << 28,	ECX	},
	[AVX2]		= {7U, 0U,	1U << 5,	EBX	},
	[BMI1]		= {7U, 0U,	1U << 3,	EBX	},
	[BMI2]		= {7U, 0U,	1U << 8,	EBX	},
	[AVX512F]	= {7U, 0U, _AVX512F_BIT,	EBX	},
	[AVX512CD]	= {7U, 0U, _AVX512CD_BIT,	EBX	},
	[AVX512DQ]	= {7U, 0U, _AVX512DQ_BIT,	EBX	},
	[AVX512BW]	= {7U, 0U, _AVX512BW_BIT,	EBX	},
	[AVX512IFMA]	= {7U, 0U, _AVX512IFMA_BIT,	EBX	},
	[AVX512VBMI]	= {7U, 0U, _AVX512VBMI_BIT,	ECX	},
	[AVX512PF]	= {7U, 0U, _AVX512PF_BIT,	EBX	},
	[AVX512ER]	= {7U, 0U, _AVX512ER_BIT,	EBX	},
	[AVX512VL]	= {7U, 0U, _AVX512ER_BIT,	EBX	},
	[AES]		= {1U, 0U, _AES_BIT,		ECX	},
	[PCLMULQDQ]	= {1U, 0U, _PCLMULQDQ_BIT,	ECX	},
	[MOVBE]		= {1U, 0U, _MOVBE_BIT,		ECX	},
};

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
 * Check if CPU supports a feature
 */
static inline boolean_t
__cpuid_check_feature(const cpuid_feature_desc_t *desc)
{
	uint32_t r[CPUID_REG_CNT];

	if (__get_cpuid_max(0, NULL) >= desc->leaf) {
		/*
		 * __cpuid_count is needed to properly check
		 * for AVX2. It is a macro, so return parameters
		 * are passed by value.
		 */
		__cpuid_count(desc->leaf, desc->subleaf,
		    r[EAX], r[EBX], r[ECX], r[EDX]);
		return ((r[desc->reg] & desc->flag) == desc->flag);
	}
	return (B_FALSE);
}

#define	CPUID_FEATURE_CHECK(name, id)				\
static inline boolean_t						\
__cpuid_has_ ## name(void)					\
{								\
	return (__cpuid_check_feature(&cpuid_features[id]));	\
}

/*
 * Define functions for user-space CPUID features testing
 */
CPUID_FEATURE_CHECK(sse, SSE);
CPUID_FEATURE_CHECK(sse2, SSE2);
CPUID_FEATURE_CHECK(sse3, SSE3);
CPUID_FEATURE_CHECK(ssse3, SSSE3);
CPUID_FEATURE_CHECK(sse4_1, SSE4_1);
CPUID_FEATURE_CHECK(sse4_2, SSE4_2);
CPUID_FEATURE_CHECK(avx, AVX);
CPUID_FEATURE_CHECK(avx2, AVX2);
CPUID_FEATURE_CHECK(osxsave, OSXSAVE);
CPUID_FEATURE_CHECK(bmi1, BMI1);
CPUID_FEATURE_CHECK(bmi2, BMI2);
CPUID_FEATURE_CHECK(avx512f, AVX512F);
CPUID_FEATURE_CHECK(avx512cd, AVX512CD);
CPUID_FEATURE_CHECK(avx512dq, AVX512DQ);
CPUID_FEATURE_CHECK(avx512bw, AVX512BW);
CPUID_FEATURE_CHECK(avx512ifma, AVX512IFMA);
CPUID_FEATURE_CHECK(avx512vbmi, AVX512VBMI);
CPUID_FEATURE_CHECK(avx512pf, AVX512PF);
CPUID_FEATURE_CHECK(avx512er, AVX512ER);
CPUID_FEATURE_CHECK(avx512vl, AVX512VL);
CPUID_FEATURE_CHECK(aes, AES);
CPUID_FEATURE_CHECK(pclmulqdq, PCLMULQDQ);
CPUID_FEATURE_CHECK(movbe, MOVBE);

/*
 * Detect register set support
 */
static inline boolean_t
__simd_state_enabled(const uint64_t state)
{
	boolean_t has_osxsave;
	uint64_t xcr0;

	has_osxsave = __cpuid_has_osxsave();
	if (!has_osxsave)
		return (B_FALSE);

	xcr0 = xgetbv(0);
	return ((xcr0 & state) == state);
}

#define	_XSTATE_SSE_AVX		(0x2 | 0x4)
#define	_XSTATE_AVX512		(0xE0 | _XSTATE_SSE_AVX)

#define	__ymm_enabled()		__simd_state_enabled(_XSTATE_SSE_AVX)
#define	__zmm_enabled()		__simd_state_enabled(_XSTATE_AVX512)

/*
 * Check if SSE instruction set is available
 */
static inline boolean_t
zfs_sse_available(void)
{
	return (__cpuid_has_sse());
}

/*
 * Check if SSE2 instruction set is available
 */
static inline boolean_t
zfs_sse2_available(void)
{
	return (__cpuid_has_sse2());
}

/*
 * Check if SSE3 instruction set is available
 */
static inline boolean_t
zfs_sse3_available(void)
{
	return (__cpuid_has_sse3());
}

/*
 * Check if SSSE3 instruction set is available
 */
static inline boolean_t
zfs_ssse3_available(void)
{
	return (__cpuid_has_ssse3());
}

/*
 * Check if SSE4.1 instruction set is available
 */
static inline boolean_t
zfs_sse4_1_available(void)
{
	return (__cpuid_has_sse4_1());
}

/*
 * Check if SSE4.2 instruction set is available
 */
static inline boolean_t
zfs_sse4_2_available(void)
{
	return (__cpuid_has_sse4_2());
}

/*
 * Check if AVX instruction set is available
 */
static inline boolean_t
zfs_avx_available(void)
{
	return (__cpuid_has_avx() && __ymm_enabled());
}

/*
 * Check if AVX2 instruction set is available
 */
static inline boolean_t
zfs_avx2_available(void)
{
	return (__cpuid_has_avx2() && __ymm_enabled());
}

/*
 * Check if BMI1 instruction set is available
 */
static inline boolean_t
zfs_bmi1_available(void)
{
	return (__cpuid_has_bmi1());
}

/*
 * Check if BMI2 instruction set is available
 */
static inline boolean_t
zfs_bmi2_available(void)
{
	return (__cpuid_has_bmi2());
}

/*
 * Check if AES instruction set is available
 */
static inline boolean_t
zfs_aes_available(void)
{
	return (__cpuid_has_aes());
}

/*
 * Check if PCLMULQDQ instruction set is available
 */
static inline boolean_t
zfs_pclmulqdq_available(void)
{
	return (__cpuid_has_pclmulqdq());
}

/*
 * Check if MOVBE instruction is available
 */
static inline boolean_t
zfs_movbe_available(void)
{
	return (__cpuid_has_movbe());
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
	return (__cpuid_has_avx512f() && __zmm_enabled());
}

/*
 * Check if AVX512CD instruction set is available
 */
static inline boolean_t
zfs_avx512cd_available(void)
{
	return (__cpuid_has_avx512cd() && __zmm_enabled());
}

/*
 * Check if AVX512ER instruction set is available
 */
static inline boolean_t
zfs_avx512er_available(void)
{
	return (__cpuid_has_avx512er() && __zmm_enabled());
}

/*
 * Check if AVX512PF instruction set is available
 */
static inline boolean_t
zfs_avx512pf_available(void)
{
	return (__cpuid_has_avx512pf() && __zmm_enabled());
}

/*
 * Check if AVX512BW instruction set is available
 */
static inline boolean_t
zfs_avx512bw_available(void)
{
	return (__cpuid_has_avx512bw() && __zmm_enabled());
}

/*
 * Check if AVX512DQ instruction set is available
 */
static inline boolean_t
zfs_avx512dq_available(void)
{
	return (__cpuid_has_avx512dq() && __zmm_enabled());
}

/*
 * Check if AVX512VL instruction set is available
 */
static inline boolean_t
zfs_avx512vl_available(void)
{
	return (__cpuid_has_avx512vl() && __zmm_enabled());
}

/*
 * Check if AVX512IFMA instruction set is available
 */
static inline boolean_t
zfs_avx512ifma_available(void)
{
	return (__cpuid_has_avx512ifma() && __zmm_enabled());
}

/*
 * Check if AVX512VBMI instruction set is available
 */
static inline boolean_t
zfs_avx512vbmi_available(void)
{
	return (__cpuid_has_avx512f() && __cpuid_has_avx512vbmi() &&
	    __zmm_enabled());
}

#elif defined(__aarch64__)

#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)

#elif defined(__powerpc__)

#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)

/*
 * Check if AltiVec instruction set is available
 * No easy way beyond 'altivec works' :-(
 */
#include <signal.h>
#include <setjmp.h>

#if defined(__ALTIVEC__) && !defined(__FreeBSD__)
static jmp_buf env;
static void sigillhandler(int x)
{
	(void) x;
	longjmp(env, 1);
}
#endif

static inline boolean_t
zfs_altivec_available(void)
{
	boolean_t has_altivec = B_FALSE;
#if defined(__ALTIVEC__) && !defined(__FreeBSD__)
	sighandler_t savesig;
	savesig = signal(SIGILL, sigillhandler);
	if (setjmp(env)) {
		signal(SIGILL, savesig);
		has_altivec = B_FALSE;
	} else {
		__asm__ __volatile__("vor 0,0,0\n" : : : "v0");
		signal(SIGILL, savesig);
		has_altivec = B_TRUE;
	}
#endif
	return (has_altivec);
}
static inline boolean_t
zfs_vsx_available(void)
{
	boolean_t has_vsx = B_FALSE;
#if defined(__ALTIVEC__) && !defined(__FreeBSD__)
	sighandler_t savesig;
	savesig = signal(SIGILL, sigillhandler);
	if (setjmp(env)) {
		signal(SIGILL, savesig);
		has_vsx = B_FALSE;
	} else {
		__asm__ __volatile__("xssubsp 0,0,0\n");
		signal(SIGILL, savesig);
		has_vsx = B_TRUE;
	}
#endif
	return (has_vsx);
}
#else

#define	kfpu_allowed()		0
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)

#endif

#endif /* _LIBSPL_SYS_SIMD_H */
