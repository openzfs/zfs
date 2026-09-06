// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/processor.h>
#include <sys/sysmacros.h>

extern int cpu_number(void);

#if defined(__x86_64__)

#include <i386/cpuid.h>
#include <sys/simd_x86.h>

#elif defined(__aarch64__)

#include <sys/simd_aarch64.h>

static uint64_t _spl_cpuid_id_aa64isar0_el1 = 0ULL;
static uint64_t _spl_cpuid_id_aa64isar1_el1 = 0ULL;

#else

#define	_spl_cpuid(func, a, b, c, d)				\
	a = b = c = d = 0

#endif

uint32_t
getcpuid(void)
{
#if defined(__aarch64__)
	uint64_t mpidr_el1;

	asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr_el1));
	/*
	 * To save us looking up number of eCores and pCores, we
	 * just wrap eCores backwards from max_ncpu.
	 * 0: [P0 P1 P2 ... Px Ex .. E2 E1 E0] : max_ncpu
	 *
	 * XNU: Aff2: "1" - PCORE, "0" - ECORE
	 */
#define	PCORE_BIT (1ULL << 16)
	if (mpidr_el1 & PCORE_BIT)
		return ((uint32_t)mpidr_el1 & 0xff);
	else
		return ((max_ncpus -1) - (uint32_t)(mpidr_el1 & 0xff));

#else
	return ((uint32_t)cpu_number());
#endif
}

#if defined(__aarch64__)

/* 4,5,6,7 -> GET_BITS(4, 8) */
#define	GET_BITS(S, F, T) \
		((S) >> (F)) & ((1 << ((T) - (F))) - 1)

uint64_t
spl_cpuid_id_aa64isar0_el1(void)
{
	static int first_time = 1;

	if (first_time == 1) {
		first_time = 0;
		uint64_t value;
		uint32_t aes, sha1, sha2, sha3;

		asm volatile("mrs %0, ID_AA64ISAR0_EL1" :
		    "=r"(value) ::);

		_spl_cpuid_id_aa64isar0_el1 = value;

		printf("cpu_features0: 0x%016llx \n",
		    _spl_cpuid_id_aa64isar0_el1);

		aes = GET_BITS(_spl_cpuid_id_aa64isar0_el1, 4, 8);
		sha1 = GET_BITS(_spl_cpuid_id_aa64isar0_el1, 8, 12);
		sha2 = GET_BITS(_spl_cpuid_id_aa64isar0_el1, 12, 16);
		sha3 = GET_BITS(_spl_cpuid_id_aa64isar0_el1, 32, 36);

		printf("cpu_features0: %s%s%s%s%s%s\n",
		    aes & 3 ? "AES " : "",
		    aes & 2 ? "PMULL " : "",
		    sha1    ? "SHA1 " : "",
		    sha2    ? "SHA256 " : "",
		    sha2 & 2 ? "SHA512 " : "",
		    sha3    ? "SHA3 " : "");
	}

	return (_spl_cpuid_id_aa64isar0_el1);
}

uint64_t
spl_cpuid_id_aa64isar1_el1(void)
{
	static int first_time = 1;

	if (first_time == 1) {
		first_time = 0;
		uint64_t value;
		uint32_t bf16, i8mm;

		asm volatile("mrs %0, ID_AA64ISAR1_EL1" :
		    "=r"(value) ::);

		_spl_cpuid_id_aa64isar1_el1 = value;

		printf("cpu_features: 0x%016llx \n",
		    _spl_cpuid_id_aa64isar1_el1);

		bf16 = GET_BITS(_spl_cpuid_id_aa64isar1_el1, 44, 48);
		i8mm = GET_BITS(_spl_cpuid_id_aa64isar1_el1, 52, 56);

		printf("cpu_features1: %s%s\n",
		    bf16 ? "BF16 " : "",
		    i8mm ? "I8MM " : "");
	}

	return (_spl_cpuid_id_aa64isar1_el1);
}

#endif /* aarch64 */

int
spl_processor_init(void)
{

#if defined(__aarch64__)
	spl_cpuid_id_aa64isar0_el1();
	spl_cpuid_id_aa64isar1_el1();
#endif

#if defined(__x86)
	printf("CPUID: %s%s%s%s%s%s%s\n",
	    zfs_osxsave_available() ? "osxsave " : "",
	    zfs_sse_available() ? "sse " : "",
	    zfs_sse2_available() ? "sse2 " : "",
	    zfs_sse3_available() ? "sse3 " : "",
	    zfs_ssse3_available() ? "ssse3 " : "",
	    zfs_sse4_1_available() ? "sse4.1 " : "",
	    zfs_sse4_2_available() ? "sse4.2 " : "");
	printf("CPUID: %s%s%s%s%s%s%s\n",
	    zfs_avx_available() ? "avx " : "",
	    zfs_avx2_available() ? "avx2 " : "",
	    zfs_aes_available() ? "aes " : "",
	    zfs_pclmulqdq_available() ? "pclmulqdq " : "",
	    zfs_avx512f_available() ? "avx512f " : "",
	    zfs_movbe_available() ? "movbe " : "",
	    zfs_shani_available() ? "sha-ni " : "");
#endif

	return (0);
}
