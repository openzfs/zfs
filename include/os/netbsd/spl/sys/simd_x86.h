// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_NETBSD_SIMD_X86_H
#define	_NETBSD_SIMD_X86_H

/*
 * NetBSD x86 kfpu + CPUID feature probes.
 *
 * Kernel: fpu_kern_enter/leave (sys/arch/x86/x86/fpu.c) save the
 * current LWP's FPU, install a safe state, and raise to splvm.
 * Works from syscall context and from kthreads.  Non-recursive.
 *
 * Features come from cpu_feature[] (boot CPU, same bits as
 * ci_feat_val): [0]=CPUID.1:%edx, [1]=CPUID.1:%ecx,
 * [5]=CPUID.7:%ebx, [6]=CPUID.7:%ecx.
 */

#include <sys/types.h>

#ifdef _KERNEL

#include <machine/specialreg.h>
#include <x86/cpuvar.h>
#include <x86/fpu.h>

#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		fpu_kern_enter()
#define	kfpu_end()		fpu_kern_leave()

#define	_ZFS_CPU_FEAT0		(cpu_feature[0])
#define	_ZFS_CPU_FEAT1		(cpu_feature[1])
#define	_ZFS_CPU_SEF_EBX	(cpu_feature[5])
#define	_ZFS_CPU_SEF_ECX	(cpu_feature[6])

#else /* !_KERNEL */

#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)

/* Userland: compiler builtins, no kernel CPUID tables. */
#define	_ZFS_USERLAND_SIMD	1

#endif /* _KERNEL */

static inline uint64_t
zfs_xgetbv(uint32_t index)
{
	uint32_t eax, edx;

	__asm__ __volatile__(".byte 0x0f, 0x01, 0xd0"
	    : "=a" (eax), "=d" (edx)
	    : "c" (index));
	return (((uint64_t)edx << 32) | eax);
}

static inline boolean_t
__simd_state_enabled(const uint64_t state)
{
#ifdef _KERNEL
	if ((_ZFS_CPU_FEAT1 & CPUID2_OSXSAVE) == 0)
		return (B_FALSE);
#else
	if (!__builtin_cpu_supports("osxsave"))
		return (B_FALSE);
#endif
	return ((zfs_xgetbv(0) & state) == state);
}

#define	_XSTATE_SSE_AVX		(0x2 | 0x4)
#define	_XSTATE_AVX512		(0xE0 | _XSTATE_SSE_AVX)
#define	__ymm_enabled()		__simd_state_enabled(_XSTATE_SSE_AVX)
#define	__zmm_enabled()		__simd_state_enabled(_XSTATE_AVX512)

#ifdef _KERNEL

static inline boolean_t
zfs_sse_available(void)
{
	return ((_ZFS_CPU_FEAT0 & CPUID_SSE) != 0);
}

static inline boolean_t
zfs_sse2_available(void)
{
	return ((_ZFS_CPU_FEAT0 & CPUID_SSE2) != 0);
}

static inline boolean_t
zfs_sse3_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_SSE3) != 0);
}

static inline boolean_t
zfs_ssse3_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_SSSE3) != 0);
}

static inline boolean_t
zfs_sse4_1_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_SSE41) != 0);
}

static inline boolean_t
zfs_sse4_2_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_SSE42) != 0);
}

static inline boolean_t
zfs_avx_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_AVX) != 0 && __ymm_enabled());
}

static inline boolean_t
zfs_osxsave_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_OSXSAVE) != 0);
}

static inline boolean_t
zfs_aes_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_AESNI) != 0);
}

static inline boolean_t
zfs_pclmulqdq_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_PCLMULQDQ) != 0);
}

static inline boolean_t
zfs_movbe_available(void)
{
	return ((_ZFS_CPU_FEAT1 & CPUID2_MOVBE) != 0);
}

static inline boolean_t
zfs_avx2_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX2) != 0 && __ymm_enabled());
}

static inline boolean_t
zfs_bmi1_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_BMI1) != 0);
}

static inline boolean_t
zfs_bmi2_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_BMI2) != 0);
}

static inline boolean_t
zfs_shani_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_SHA) != 0);
}

static inline boolean_t
zfs_sha512ext_available(void)
{
	uint32_t eax, ebx, ecx, edx;

	/*
	 * SHA-512 NI is CPUID.(EAX=7,ECX=1):EAX bit 0.  NetBSD's
	 * cpu_feature[] only caches leaf 1 and leaf 7 subleaf 0, so
	 * this is the equivalent of FreeBSD cpu_stdext_feature4 &
	 * CPUID_STDEXT4_SHA512.  Require OSXSAVE+YMM like FreeBSD.
	 */
	__asm__ __volatile__("cpuid"
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
	    : "a" (0));
	if (eax < 7)
		return (B_FALSE);
	__asm__ __volatile__("cpuid"
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
	    : "a" (7), "c" (0));
	if (eax < 1)
		return (B_FALSE);
	__asm__ __volatile__("cpuid"
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
	    : "a" (7), "c" (1));
	return ((eax & 0x1U) != 0 && __ymm_enabled());
}

static inline boolean_t
zfs_avx512f_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512F) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512cd_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512F) != 0 &&
	    (_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512CD) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512er_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512ER) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512pf_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512PF) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512bw_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512BW) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512dq_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512DQ) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512vl_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512VL) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512ifma_available(void)
{
	return ((_ZFS_CPU_SEF_EBX & CPUID_SEF_AVX512_IFMA) != 0 &&
	    __zmm_enabled());
}

static inline boolean_t
zfs_avx512vbmi_available(void)
{
	return ((_ZFS_CPU_SEF_ECX & CPUID_SEF_AVX512_VBMI) != 0 &&
	    __zmm_enabled());
}

#else /* !_KERNEL */

static inline boolean_t
zfs_sse_available(void) { return (__builtin_cpu_supports("sse")); }
static inline boolean_t
zfs_sse2_available(void) { return (__builtin_cpu_supports("sse2")); }
static inline boolean_t
zfs_sse3_available(void) { return (__builtin_cpu_supports("sse3")); }
static inline boolean_t
zfs_ssse3_available(void) { return (__builtin_cpu_supports("ssse3")); }
static inline boolean_t
zfs_sse4_1_available(void) { return (__builtin_cpu_supports("sse4.1")); }
static inline boolean_t
zfs_sse4_2_available(void) { return (__builtin_cpu_supports("sse4.2")); }
static inline boolean_t
zfs_avx_available(void)
{
	return (__builtin_cpu_supports("avx") && __ymm_enabled());
}
static inline boolean_t
zfs_avx2_available(void)
{
	return (__builtin_cpu_supports("avx2") && __ymm_enabled());
}
static inline boolean_t
zfs_osxsave_available(void) { return (__builtin_cpu_supports("osxsave")); }
static inline boolean_t
zfs_aes_available(void) { return (__builtin_cpu_supports("aes")); }
static inline boolean_t
zfs_pclmulqdq_available(void) { return (__builtin_cpu_supports("pclmul")); }
static inline boolean_t
zfs_movbe_available(void) { return (__builtin_cpu_supports("movbe")); }
static inline boolean_t
zfs_bmi1_available(void) { return (__builtin_cpu_supports("bmi")); }
static inline boolean_t
zfs_bmi2_available(void) { return (__builtin_cpu_supports("bmi2")); }
static inline boolean_t
zfs_shani_available(void)
{
	return (__builtin_cpu_supports("sha") && __ymm_enabled());
}
static inline boolean_t
zfs_sha512ext_available(void)
{
	uint32_t eax, ebx, ecx, edx;

	__asm__ __volatile__("cpuid"
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
	    : "a" (0));
	if (eax < 7)
		return (B_FALSE);
	__asm__ __volatile__("cpuid"
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
	    : "a" (7), "c" (0));
	if (eax < 1)
		return (B_FALSE);
	__asm__ __volatile__("cpuid"
	    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
	    : "a" (7), "c" (1));
	return ((eax & 0x1U) != 0 && __ymm_enabled());
}
static inline boolean_t
zfs_avx512f_available(void)
{
	return (__builtin_cpu_supports("avx512f") && __zmm_enabled());
}
static inline boolean_t
zfs_avx512cd_available(void)
{
	return (__builtin_cpu_supports("avx512cd") && __zmm_enabled());
}
static inline boolean_t
zfs_avx512er_available(void) { return (B_FALSE); }
static inline boolean_t
zfs_avx512pf_available(void) { return (B_FALSE); }
static inline boolean_t
zfs_avx512bw_available(void)
{
	return (__builtin_cpu_supports("avx512bw") && __zmm_enabled());
}
static inline boolean_t
zfs_avx512dq_available(void)
{
	return (__builtin_cpu_supports("avx512dq") && __zmm_enabled());
}
static inline boolean_t
zfs_avx512vl_available(void)
{
	return (__builtin_cpu_supports("avx512vl") && __zmm_enabled());
}
static inline boolean_t
zfs_avx512ifma_available(void)
{
	return (__builtin_cpu_supports("avx512ifma") && __zmm_enabled());
}
static inline boolean_t
zfs_avx512vbmi_available(void)
{
	return (__builtin_cpu_supports("avx512vbmi") && __zmm_enabled());
}

#endif /* _KERNEL */

#endif /* _NETBSD_SIMD_X86_H */
