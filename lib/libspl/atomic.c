// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2009 by Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <atomic.h>

/*
 * These are the void returning variants
 */
#define	ATOMIC_INC(name, type) \
	void atomic_inc_##name(volatile type *target)			\
	{								\
		(void) __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);	\
	}

ATOMIC_INC(8, uint8_t)
ATOMIC_INC(16, uint16_t)
ATOMIC_INC(32, uint32_t)
ATOMIC_INC(64, uint64_t)
ATOMIC_INC(uchar, uchar_t)
ATOMIC_INC(ushort, ushort_t)
ATOMIC_INC(uint, uint_t)
ATOMIC_INC(ulong, ulong_t)


#define	ATOMIC_DEC(name, type) \
	void atomic_dec_##name(volatile type *target)			\
	{								\
		(void) __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);	\
	}

ATOMIC_DEC(8, uint8_t)
ATOMIC_DEC(16, uint16_t)
ATOMIC_DEC(32, uint32_t)
ATOMIC_DEC(64, uint64_t)
ATOMIC_DEC(uchar, uchar_t)
ATOMIC_DEC(ushort, ushort_t)
ATOMIC_DEC(uint, uint_t)
ATOMIC_DEC(ulong, ulong_t)


#define	ATOMIC_ADD(name, type1, type2) \
	void atomic_add_##name(volatile type1 *target, type2 bits)	\
	{								\
		(void) __atomic_add_fetch(target, bits, __ATOMIC_SEQ_CST); \
	}

void
atomic_add_ptr(volatile void *target, ssize_t bits)
{
	(void) __atomic_add_fetch((void **)target, bits, __ATOMIC_SEQ_CST);
}

ATOMIC_ADD(8, uint8_t, int8_t)
ATOMIC_ADD(16, uint16_t, int16_t)
ATOMIC_ADD(32, uint32_t, int32_t)
ATOMIC_ADD(64, uint64_t, int64_t)
ATOMIC_ADD(char, uchar_t, signed char)
ATOMIC_ADD(short, ushort_t, short)
ATOMIC_ADD(int, uint_t, int)
ATOMIC_ADD(long, ulong_t, long)


#define	ATOMIC_SUB(name, type1, type2) \
	void atomic_sub_##name(volatile type1 *target, type2 bits)	\
	{								\
		(void) __atomic_sub_fetch(target, bits, __ATOMIC_SEQ_CST); \
	}

void
atomic_sub_ptr(volatile void *target, ssize_t bits)
{
	(void) __atomic_sub_fetch((void **)target, bits, __ATOMIC_SEQ_CST);
}

ATOMIC_SUB(8, uint8_t, int8_t)
ATOMIC_SUB(16, uint16_t, int16_t)
ATOMIC_SUB(32, uint32_t, int32_t)
ATOMIC_SUB(64, uint64_t, int64_t)
ATOMIC_SUB(char, uchar_t, signed char)
ATOMIC_SUB(short, ushort_t, short)
ATOMIC_SUB(int, uint_t, int)
ATOMIC_SUB(long, ulong_t, long)


#define	ATOMIC_OR(name, type) \
	void atomic_or_##name(volatile type *target, type bits)		\
	{								\
		(void) __atomic_or_fetch(target, bits, __ATOMIC_SEQ_CST); \
	}

ATOMIC_OR(8, uint8_t)
ATOMIC_OR(16, uint16_t)
ATOMIC_OR(32, uint32_t)
ATOMIC_OR(64, uint64_t)
ATOMIC_OR(uchar, uchar_t)
ATOMIC_OR(ushort, ushort_t)
ATOMIC_OR(uint, uint_t)
ATOMIC_OR(ulong, ulong_t)


#define	ATOMIC_AND(name, type) \
	void atomic_and_##name(volatile type *target, type bits)	\
	{								\
		(void) __atomic_and_fetch(target, bits, __ATOMIC_SEQ_CST); \
	}

ATOMIC_AND(8, uint8_t)
ATOMIC_AND(16, uint16_t)
ATOMIC_AND(32, uint32_t)
ATOMIC_AND(64, uint64_t)
ATOMIC_AND(uchar, uchar_t)
ATOMIC_AND(ushort, ushort_t)
ATOMIC_AND(uint, uint_t)
ATOMIC_AND(ulong, ulong_t)


/*
 * New value returning variants
 */

#define	ATOMIC_INC_NV(name, type) \
	type atomic_inc_##name##_nv(volatile type *target)		\
	{								\
		return (__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_INC_NV(8, uint8_t)
ATOMIC_INC_NV(16, uint16_t)
ATOMIC_INC_NV(32, uint32_t)
ATOMIC_INC_NV(64, uint64_t)
ATOMIC_INC_NV(uchar, uchar_t)
ATOMIC_INC_NV(ushort, ushort_t)
ATOMIC_INC_NV(uint, uint_t)
ATOMIC_INC_NV(ulong, ulong_t)


#define	ATOMIC_DEC_NV(name, type) \
	type atomic_dec_##name##_nv(volatile type *target)		\
	{								\
		return (__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_DEC_NV(8, uint8_t)
ATOMIC_DEC_NV(16, uint16_t)
ATOMIC_DEC_NV(32, uint32_t)
ATOMIC_DEC_NV(64, uint64_t)
ATOMIC_DEC_NV(uchar, uchar_t)
ATOMIC_DEC_NV(ushort, ushort_t)
ATOMIC_DEC_NV(uint, uint_t)
ATOMIC_DEC_NV(ulong, ulong_t)


#define	ATOMIC_ADD_NV(name, type1, type2) \
	type1 atomic_add_##name##_nv(volatile type1 *target, type2 bits) \
	{								\
		return (__atomic_add_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

void *
atomic_add_ptr_nv(volatile void *target, ssize_t bits)
{
	return (__atomic_add_fetch((void **)target, bits, __ATOMIC_SEQ_CST));
}

ATOMIC_ADD_NV(8, uint8_t, int8_t)
ATOMIC_ADD_NV(16, uint16_t, int16_t)
ATOMIC_ADD_NV(32, uint32_t, int32_t)
ATOMIC_ADD_NV(64, uint64_t, int64_t)
ATOMIC_ADD_NV(char, uchar_t, signed char)
ATOMIC_ADD_NV(short, ushort_t, short)
ATOMIC_ADD_NV(int, uint_t, int)
ATOMIC_ADD_NV(long, ulong_t, long)


#define	ATOMIC_SUB_NV(name, type1, type2) \
	type1 atomic_sub_##name##_nv(volatile type1 *target, type2 bits) \
	{								\
		return (__atomic_sub_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

void *
atomic_sub_ptr_nv(volatile void *target, ssize_t bits)
{
	return (__atomic_sub_fetch((void **)target, bits, __ATOMIC_SEQ_CST));
}

ATOMIC_SUB_NV(8, uint8_t, int8_t)
ATOMIC_SUB_NV(char, uchar_t, signed char)
ATOMIC_SUB_NV(16, uint16_t, int16_t)
ATOMIC_SUB_NV(short, ushort_t, short)
ATOMIC_SUB_NV(32, uint32_t, int32_t)
ATOMIC_SUB_NV(int, uint_t, int)
ATOMIC_SUB_NV(long, ulong_t, long)
ATOMIC_SUB_NV(64, uint64_t, int64_t)


#define	ATOMIC_OR_NV(name, type) \
	type atomic_or_##name##_nv(volatile type *target, type bits)	\
	{								\
		return (__atomic_or_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_OR_NV(8, uint8_t)
ATOMIC_OR_NV(16, uint16_t)
ATOMIC_OR_NV(32, uint32_t)
ATOMIC_OR_NV(64, uint64_t)
ATOMIC_OR_NV(uchar, uchar_t)
ATOMIC_OR_NV(ushort, ushort_t)
ATOMIC_OR_NV(uint, uint_t)
ATOMIC_OR_NV(ulong, ulong_t)


#define	ATOMIC_AND_NV(name, type) \
	type atomic_and_##name##_nv(volatile type *target, type bits)	\
	{								\
		return (__atomic_and_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_AND_NV(8, uint8_t)
ATOMIC_AND_NV(16, uint16_t)
ATOMIC_AND_NV(32, uint32_t)
ATOMIC_AND_NV(64, uint64_t)
ATOMIC_AND_NV(uchar, uchar_t)
ATOMIC_AND_NV(ushort, ushort_t)
ATOMIC_AND_NV(uint, uint_t)
ATOMIC_AND_NV(ulong, ulong_t)


/*
 * If *tgt == exp, set *tgt = des; return old value
 *
 * This may not look right on the first pass (or the sixteenth), but,
 * from https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html:
 * > If they are not equal, the operation is a read
 * > and the current contents of *ptr are written into *expected.
 * And, in the converse case, exp is already *target by definition.
 */

#define	ATOMIC_CAS(name, type) \
	type atomic_cas_##name(volatile type *target, type exp, type des) \
	{								\
		__atomic_compare_exchange_n(target, &exp, des, B_FALSE,	\
		    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);		\
		return (exp);						\
	}

void *
atomic_cas_ptr(volatile void *target, void *exp, void *des)
{

	__atomic_compare_exchange_n((void **)target, &exp, des, B_FALSE,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return (exp);
}

ATOMIC_CAS(8, uint8_t)
ATOMIC_CAS(16, uint16_t)
ATOMIC_CAS(32, uint32_t)
ATOMIC_CAS(64, uint64_t)
ATOMIC_CAS(uchar, uchar_t)
ATOMIC_CAS(ushort, ushort_t)
ATOMIC_CAS(uint, uint_t)
ATOMIC_CAS(ulong, ulong_t)


/*
 * Swap target and return old value
 */

#define	ATOMIC_SWAP(name, type) \
	type atomic_swap_##name(volatile type *target, type bits)	\
	{								\
		return (__atomic_exchange_n(target, bits, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_SWAP(8, uint8_t)
ATOMIC_SWAP(16, uint16_t)
ATOMIC_SWAP(32, uint32_t)
ATOMIC_SWAP(64, uint64_t)
ATOMIC_SWAP(uchar, uchar_t)
ATOMIC_SWAP(ushort, ushort_t)
ATOMIC_SWAP(uint, uint_t)
ATOMIC_SWAP(ulong, ulong_t)

void *
atomic_swap_ptr(volatile void *target, void *bits)
{
	return (__atomic_exchange_n((void **)target, bits, __ATOMIC_SEQ_CST));
}

#ifndef _LP64
uint64_t
atomic_load_64(volatile uint64_t *target)
{
	return (__atomic_load_n(target, __ATOMIC_RELAXED));
}

void
atomic_store_64(volatile uint64_t *target, uint64_t bits)
{
	return (__atomic_store_n(target, bits, __ATOMIC_RELAXED));
}
#endif

int
atomic_set_long_excl(volatile ulong_t *target, uint_t value)
{
	ulong_t bit = 1UL << value;
	ulong_t old = __atomic_fetch_or(target, bit, __ATOMIC_SEQ_CST);
	return ((old & bit) ? -1 : 0);
}

int
atomic_clear_long_excl(volatile ulong_t *target, uint_t value)
{
	ulong_t bit = 1UL << value;
	ulong_t old = __atomic_fetch_and(target, ~bit, __ATOMIC_SEQ_CST);
	return ((old & bit) ? 0 : -1);
}

void
membar_enter(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void
membar_exit(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void
membar_sync(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void
membar_producer(void)
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

void
membar_consumer(void)
{
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
}
