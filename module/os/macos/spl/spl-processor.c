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

#ifdef __x86_64__

#include <i386/cpuid.h>

#define	_spl_cpuid(func, a, b, c, d)			\
	__asm__ __volatile__( \
	"        pushq %%rbx        \n" \
	"        xorq %%rcx,%%rcx   \n" \
	"        cpuid              \n" \
	"        movq %%rbx, %%rsi  \n" \
	"        popq %%rbx         \n" : \
	"=a" (a), "=S" (b), "=c" (c), "=d" (d) : "a" (func))

#else /* Add ARM */

#define	_spl_cpuid(func, a, b, c, d)				\
	a = b = c = d = 0

#endif

static uint64_t _spl_cpuid_features = 0ULL;
static uint64_t _spl_cpuid_features_leaf7 = 0ULL;
static boolean_t _spl_cpuid_has_xgetbv = B_FALSE;

uint32_t
getcpuid()
{
#if defined(__aarch64__)
	// Find arm64 solution.
	return (0);
#else
	return ((uint32_t)cpu_number());
#endif
}

uint64_t
spl_cpuid_features(void)
{

#if defined(__aarch64__)

	// Find arm64 solution.
	_spl_cpuid_has_xgetbv = B_FALSE; /* Silence unused */

#else /* X64 */

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
#endif

	return (_spl_cpuid_features);
}

uint64_t
spl_cpuid_leaf7_features(void)
{
	return (_spl_cpuid_features_leaf7);
}
