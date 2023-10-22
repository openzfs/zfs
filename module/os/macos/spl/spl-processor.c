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
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/processor.h>

extern int cpu_number(void);

#if defined(__x86_64__)

#include <i386/cpuid.h>

#define	_spl_cpuid(func, a, b, c, d)			\
	__asm__ __volatile__( \
	"        pushq %%rbx        \n" \
	"        xorq %%rcx,%%rcx   \n" \
	"        cpuid              \n" \
	"        movq %%rbx, %%rsi  \n" \
	"        popq %%rbx         \n" : \
	"=a" (a), "=S" (b), "=c" (c), "=d" (d) : "a" (func))

static uint64_t _spl_cpuid_features = 0ULL;
static uint64_t _spl_cpuid_features_leaf7 = 0ULL;
static boolean_t _spl_cpuid_has_xgetbv = B_FALSE;

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


#if defined(__x86_64__)
uint64_t
spl_cpuid_features(void)
{
	static int first_time = 1;
	uint64_t a, b, c, d;

	if (first_time == 1) {
		first_time = 0;
		// Wikipedia: stored in EAX, EBX, EDX, ECX (in that order).
		_spl_cpuid(0, a, b, d, c);
		if (a >= 1) {
			_spl_cpuid(1, a, b, d, c);
			_spl_cpuid_features = d | (c << 32);

			// GETBV is bit 26 in ECX. Apple defines it as:
			// CPUID_FEATURE_XSAVE _HBit(26)
			// ( ECX & (1 << 26)
			// or, (feature & 400000000000000)
			_spl_cpuid_has_xgetbv =
			    _spl_cpuid_features & CPUID_FEATURE_XSAVE;
		}
		if (a >= 7) {
			c = 0;
			_spl_cpuid(7, a, b, d, c);
			_spl_cpuid_features_leaf7 = b | (c << 32);
		}


		printf("SPL: CPUID 0x%08llx and leaf7 0x%08llx\n",
		    _spl_cpuid_features, _spl_cpuid_features_leaf7);

	}

	return (_spl_cpuid_features);
}

uint64_t
spl_cpuid_leaf7_features(void)
{
	return (_spl_cpuid_features_leaf7);
}

#endif /* x86_64 */


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

uint64_t
spl_cpuid_features(void)
{
	spl_cpuid_id_aa64isar0_el1();
	spl_cpuid_id_aa64isar1_el1();
	return (0ULL);
}

#endif /* aarch64 */
