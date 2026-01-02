// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Generic Implementation.
 */

#include <sys/isa_defs.h>
#include <sys/sysmacros.h>

/*
 * 64-bit math support for 32-bit platforms. Compilers will generatee
 * references to the functions here if required.
 */

#if BITS_PER_LONG == 32

/*
 * Support 64/64 => 64 division on a 32-bit platform.  While the kernel
 * provides a div64_u64() function for this we do not use it because the
 * implementation is flawed.  There are cases which return incorrect
 * results as late as linux-2.6.35.  Until this is fixed upstream the
 * spl must provide its own implementation.
 *
 * This implementation is a slightly modified version of the algorithm
 * proposed by the book 'Hacker's Delight'.  The original source can be
 * found here and is available for use without restriction.
 *
 * http://www.hackersdelight.org/HDcode/newCode/divDouble.c
 */

/*
 * Calculate number of leading of zeros for a 64-bit value.
 */
static int
nlz64(uint64_t x)
{
	register int n = 0;

	if (x == 0)
		return (64);

	if (x <= 0x00000000FFFFFFFFULL) { n = n + 32; x = x << 32; }
	if (x <= 0x0000FFFFFFFFFFFFULL) { n = n + 16; x = x << 16; }
	if (x <= 0x00FFFFFFFFFFFFFFULL) { n = n +  8; x = x <<  8; }
	if (x <= 0x0FFFFFFFFFFFFFFFULL) { n = n +  4; x = x <<  4; }
	if (x <= 0x3FFFFFFFFFFFFFFFULL) { n = n +  2; x = x <<  2; }
	if (x <= 0x7FFFFFFFFFFFFFFFULL) { n = n +  1; }

	return (n);
}

/*
 * Newer kernels have a div_u64() function but we define our own
 * to simplify portability between kernel versions.
 */
static inline uint64_t
__div_u64(uint64_t u, uint32_t v)
{
	(void) do_div(u, v);
	return (u);
}

/*
 * Implementation of 64-bit unsigned division for 32-bit machines.
 *
 * First the procedure takes care of the case in which the divisor is a
 * 32-bit quantity. There are two subcases: (1) If the left half of the
 * dividend is less than the divisor, one execution of do_div() is all that
 * is required (overflow is not possible). (2) Otherwise it does two
 * divisions, using the grade school method.
 */
uint64_t
__udivdi3(uint64_t u, uint64_t v)
{
	uint64_t u0, u1, v1, q0, q1, k;
	int n;

	if (v >> 32 == 0) {			// If v < 2**32:
		if (u >> 32 < v) {		// If u/v cannot overflow,
			return (__div_u64(u, v)); // just do one division.
		} else {			// If u/v would overflow:
			u1 = u >> 32;		// Break u into two halves.
			u0 = u & 0xFFFFFFFF;
			q1 = __div_u64(u1, v);	// First quotient digit.
			k  = u1 - q1 * v;	// First remainder, < v.
			u0 += (k << 32);
			q0 = __div_u64(u0, v);	// Seconds quotient digit.
			return ((q1 << 32) + q0);
		}
	} else {				// If v >= 2**32:
		n = nlz64(v);			// 0 <= n <= 31.
		v1 = (v << n) >> 32;		// Normalize divisor, MSB is 1.
		u1 = u >> 1;			// To ensure no overflow.
		q1 = __div_u64(u1, v1);		// Get quotient from
		q0 = (q1 << n) >> 31;		// Undo normalization and
						// division of u by 2.
		if (q0 != 0)			// Make q0 correct or
			q0 = q0 - 1;		// too small by 1.
		if ((u - q0 * v) >= v)
			q0 = q0 + 1;		// Now q0 is correct.

		return (q0);
	}
}
EXPORT_SYMBOL(__udivdi3);

#ifndef abs64
/* CSTYLED */
#define	abs64(x)	({ uint64_t t = (x) >> 63; ((x) ^ t) - t; })
#endif

/*
 * Implementation of 64-bit signed division for 32-bit machines.
 */
int64_t
__divdi3(int64_t u, int64_t v)
{
	int64_t q, t;
	q = __udivdi3(abs64(u), abs64(v));
	t = (u ^ v) >> 63;	// If u, v have different
	return ((q ^ t) - t);	// signs, negate q.
}
EXPORT_SYMBOL(__divdi3);

/*
 * Implementation of 64-bit unsigned modulo for 32-bit machines.
 */
uint64_t
__umoddi3(uint64_t dividend, uint64_t divisor)
{
	return (dividend - (divisor * __udivdi3(dividend, divisor)));
}
EXPORT_SYMBOL(__umoddi3);

/* 64-bit signed modulo for 32-bit machines. */
int64_t
__moddi3(int64_t n, int64_t d)
{
	int64_t q;
	boolean_t nn = B_FALSE;

	if (n < 0) {
		nn = B_TRUE;
		n = -n;
	}
	if (d < 0)
		d = -d;

	q = __umoddi3(n, d);

	return (nn ? -q : q);
}
EXPORT_SYMBOL(__moddi3);

/*
 * Implementation of 64-bit unsigned division/modulo for 32-bit machines.
 */
uint64_t
__udivmoddi4(uint64_t n, uint64_t d, uint64_t *r)
{
	uint64_t q = __udivdi3(n, d);
	if (r)
		*r = n - d * q;
	return (q);
}
EXPORT_SYMBOL(__udivmoddi4);

/*
 * Implementation of 64-bit signed division/modulo for 32-bit machines.
 */
int64_t
__divmoddi4(int64_t n, int64_t d, int64_t *r)
{
	int64_t q, rr;
	boolean_t nn = B_FALSE;
	boolean_t nd = B_FALSE;
	if (n < 0) {
		nn = B_TRUE;
		n = -n;
	}
	if (d < 0) {
		nd = B_TRUE;
		d = -d;
	}

	q = __udivmoddi4(n, d, (uint64_t *)&rr);

	if (nn != nd)
		q = -q;
	if (nn)
		rr = -rr;
	if (r)
		*r = rr;
	return (q);
}
EXPORT_SYMBOL(__divmoddi4);

#if defined(__arm) || defined(__arm__)
/*
 * Implementation of 64-bit (un)signed division for 32-bit arm machines.
 *
 * Run-time ABI for the ARM Architecture (page 20).  A pair of (unsigned)
 * long longs is returned in {{r0, r1}, {r2,r3}}, the quotient in {r0, r1},
 * and the remainder in {r2, r3}.  The return type is specifically left
 * set to 'void' to ensure the compiler does not overwrite these registers
 * during the return.  All results are in registers as per ABI
 */
void
__aeabi_uldivmod(uint64_t u, uint64_t v)
{
	uint64_t res;
	uint64_t mod;

	res = __udivdi3(u, v);
	mod = __umoddi3(u, v);
	{
		register uint32_t r0 asm("r0") = (res & 0xFFFFFFFF);
		register uint32_t r1 asm("r1") = (res >> 32);
		register uint32_t r2 asm("r2") = (mod & 0xFFFFFFFF);
		register uint32_t r3 asm("r3") = (mod >> 32);

		asm volatile(""
		    : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)  /* output */
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3));    /* input */

		return; /* r0; */
	}
}
EXPORT_SYMBOL(__aeabi_uldivmod);

void
__aeabi_ldivmod(int64_t u, int64_t v)
{
	int64_t res;
	uint64_t mod;

	res =  __divdi3(u, v);
	mod = __umoddi3(u, v);
	{
		register uint32_t r0 asm("r0") = (res & 0xFFFFFFFF);
		register uint32_t r1 asm("r1") = (res >> 32);
		register uint32_t r2 asm("r2") = (mod & 0xFFFFFFFF);
		register uint32_t r3 asm("r3") = (mod >> 32);

		asm volatile(""
		    : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)  /* output */
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3));    /* input */

		return; /* r0; */
	}
}
EXPORT_SYMBOL(__aeabi_ldivmod);
#endif /* __arm || __arm__ */

#endif /* BITS_PER_LONG */
