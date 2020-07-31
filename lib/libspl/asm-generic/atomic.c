/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2009 by Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <atomic.h>
#include <assert.h>
#include <pthread.h>

/*
 * All operations are implemented by serializing them through a global
 * pthread mutex.  This provides a correct generic implementation.
 * However all supported architectures are encouraged to provide a
 * native implementation is assembly for performance reasons.
 */
pthread_mutex_t atomic_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * These are the void returning variants
 */
/* BEGIN CSTYLED */
#define	ATOMIC_INC(name, type) \
	void atomic_inc_##name(volatile type *target)			\
	{								\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		(*target)++;						\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
	}

ATOMIC_INC(8, uint8_t)
ATOMIC_INC(uchar, uchar_t)
ATOMIC_INC(16, uint16_t)
ATOMIC_INC(ushort, ushort_t)
ATOMIC_INC(32, uint32_t)
ATOMIC_INC(uint, uint_t)
ATOMIC_INC(ulong, ulong_t)
ATOMIC_INC(64, uint64_t)


#define	ATOMIC_DEC(name, type) \
	void atomic_dec_##name(volatile type *target)			\
	{								\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		(*target)--;						\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
	}

ATOMIC_DEC(8, uint8_t)
ATOMIC_DEC(uchar, uchar_t)
ATOMIC_DEC(16, uint16_t)
ATOMIC_DEC(ushort, ushort_t)
ATOMIC_DEC(32, uint32_t)
ATOMIC_DEC(uint, uint_t)
ATOMIC_DEC(ulong, ulong_t)
ATOMIC_DEC(64, uint64_t)


#define	ATOMIC_ADD(name, type1, type2) \
	void atomic_add_##name(volatile type1 *target, type2 bits)	\
	{								\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		*target += bits;					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
	}

ATOMIC_ADD(8, uint8_t, int8_t)
ATOMIC_ADD(char, uchar_t, signed char)
ATOMIC_ADD(16, uint16_t, int16_t)
ATOMIC_ADD(short, ushort_t, short)
ATOMIC_ADD(32, uint32_t, int32_t)
ATOMIC_ADD(int, uint_t, int)
ATOMIC_ADD(long, ulong_t, long)
ATOMIC_ADD(64, uint64_t, int64_t)

void
atomic_add_ptr(volatile void *target, ssize_t bits)
{
	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	*(caddr_t *)target += bits;
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);
}


#define	ATOMIC_SUB(name, type1, type2) \
	void atomic_sub_##name(volatile type1 *target, type2 bits)	\
	{								\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		*target -= bits;					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
	}

ATOMIC_SUB(8, uint8_t, int8_t)
ATOMIC_SUB(char, uchar_t, signed char)
ATOMIC_SUB(16, uint16_t, int16_t)
ATOMIC_SUB(short, ushort_t, short)
ATOMIC_SUB(32, uint32_t, int32_t)
ATOMIC_SUB(int, uint_t, int)
ATOMIC_SUB(long, ulong_t, long)
ATOMIC_SUB(64, uint64_t, int64_t)

void
atomic_sub_ptr(volatile void *target, ssize_t bits)
{
	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	*(caddr_t *)target -= bits;
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);
}


#define	ATOMIC_OR(name, type) \
	void atomic_or_##name(volatile type *target, type bits)		\
	{								\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		*target |= bits;					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
	}

ATOMIC_OR(8, uint8_t)
ATOMIC_OR(uchar, uchar_t)
ATOMIC_OR(16, uint16_t)
ATOMIC_OR(ushort, ushort_t)
ATOMIC_OR(32, uint32_t)
ATOMIC_OR(uint, uint_t)
ATOMIC_OR(ulong, ulong_t)
ATOMIC_OR(64, uint64_t)


#define	ATOMIC_AND(name, type) \
	void atomic_and_##name(volatile type *target, type bits)	\
	{								\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		*target &= bits;					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
	}

ATOMIC_AND(8, uint8_t)
ATOMIC_AND(uchar, uchar_t)
ATOMIC_AND(16, uint16_t)
ATOMIC_AND(ushort, ushort_t)
ATOMIC_AND(32, uint32_t)
ATOMIC_AND(uint, uint_t)
ATOMIC_AND(ulong, ulong_t)
ATOMIC_AND(64, uint64_t)


/*
 * New value returning variants
 */

#define	ATOMIC_INC_NV(name, type) \
	type atomic_inc_##name##_nv(volatile type *target)		\
	{								\
		type rc;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		rc = (++(*target));					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (rc);						\
	}

ATOMIC_INC_NV(8, uint8_t)
ATOMIC_INC_NV(uchar, uchar_t)
ATOMIC_INC_NV(16, uint16_t)
ATOMIC_INC_NV(ushort, ushort_t)
ATOMIC_INC_NV(32, uint32_t)
ATOMIC_INC_NV(uint, uint_t)
ATOMIC_INC_NV(ulong, ulong_t)
ATOMIC_INC_NV(64, uint64_t)


#define	ATOMIC_DEC_NV(name, type) \
	type atomic_dec_##name##_nv(volatile type *target)		\
	{								\
		type rc;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		rc = (--(*target));					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (rc);						\
	}

ATOMIC_DEC_NV(8, uint8_t)
ATOMIC_DEC_NV(uchar, uchar_t)
ATOMIC_DEC_NV(16, uint16_t)
ATOMIC_DEC_NV(ushort, ushort_t)
ATOMIC_DEC_NV(32, uint32_t)
ATOMIC_DEC_NV(uint, uint_t)
ATOMIC_DEC_NV(ulong, ulong_t)
ATOMIC_DEC_NV(64, uint64_t)


#define	ATOMIC_ADD_NV(name, type1, type2) \
	type1 atomic_add_##name##_nv(volatile type1 *target, type2 bits)\
	{								\
		type1 rc;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		rc = (*target += bits);					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (rc);						\
	}

ATOMIC_ADD_NV(8, uint8_t, int8_t)
ATOMIC_ADD_NV(char, uchar_t, signed char)
ATOMIC_ADD_NV(16, uint16_t, int16_t)
ATOMIC_ADD_NV(short, ushort_t, short)
ATOMIC_ADD_NV(32, uint32_t, int32_t)
ATOMIC_ADD_NV(int, uint_t, int)
ATOMIC_ADD_NV(long, ulong_t, long)
ATOMIC_ADD_NV(64, uint64_t, int64_t)

void *
atomic_add_ptr_nv(volatile void *target, ssize_t bits)
{
	void *ptr;

	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	ptr = (*(caddr_t *)target += bits);
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);

	return (ptr);
}


#define	ATOMIC_SUB_NV(name, type1, type2) \
	type1 atomic_sub_##name##_nv(volatile type1 *target, type2 bits)\
	{								\
		type1 rc;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		rc = (*target -= bits);					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (rc);						\
	}

ATOMIC_SUB_NV(8, uint8_t, int8_t)
ATOMIC_SUB_NV(char, uchar_t, signed char)
ATOMIC_SUB_NV(16, uint16_t, int16_t)
ATOMIC_SUB_NV(short, ushort_t, short)
ATOMIC_SUB_NV(32, uint32_t, int32_t)
ATOMIC_SUB_NV(int, uint_t, int)
ATOMIC_SUB_NV(long, ulong_t, long)
ATOMIC_SUB_NV(64, uint64_t, int64_t)

void *
atomic_sub_ptr_nv(volatile void *target, ssize_t bits)
{
	void *ptr;

	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	ptr = (*(caddr_t *)target -= bits);
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);

	return (ptr);
}


#define	ATOMIC_OR_NV(name, type) \
	type atomic_or_##name##_nv(volatile type *target, type bits)	\
	{								\
		type rc;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		rc = (*target |= bits);					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (rc);						\
	}

ATOMIC_OR_NV(8, uint8_t)
ATOMIC_OR_NV(uchar, uchar_t)
ATOMIC_OR_NV(16, uint16_t)
ATOMIC_OR_NV(ushort, ushort_t)
ATOMIC_OR_NV(32, uint32_t)
ATOMIC_OR_NV(uint, uint_t)
ATOMIC_OR_NV(ulong, ulong_t)
ATOMIC_OR_NV(64, uint64_t)


#define	ATOMIC_AND_NV(name, type) \
	type atomic_and_##name##_nv(volatile type *target, type bits)	\
	{								\
		type rc;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		rc = (*target &= bits);					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (rc);						\
	}

ATOMIC_AND_NV(8, uint8_t)
ATOMIC_AND_NV(uchar, uchar_t)
ATOMIC_AND_NV(16, uint16_t)
ATOMIC_AND_NV(ushort, ushort_t)
ATOMIC_AND_NV(32, uint32_t)
ATOMIC_AND_NV(uint, uint_t)
ATOMIC_AND_NV(ulong, ulong_t)
ATOMIC_AND_NV(64, uint64_t)


/*
 *  If *arg1 == arg2, set *arg1 = arg3; return old value
 */

#define	ATOMIC_CAS(name, type) \
	type atomic_cas_##name(volatile type *target, type arg1, type arg2) \
	{								\
		type old;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		old = *target;						\
		if (old == arg1)					\
			*target = arg2;					\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (old);						\
	}

ATOMIC_CAS(8, uint8_t)
ATOMIC_CAS(uchar, uchar_t)
ATOMIC_CAS(16, uint16_t)
ATOMIC_CAS(ushort, ushort_t)
ATOMIC_CAS(32, uint32_t)
ATOMIC_CAS(uint, uint_t)
ATOMIC_CAS(ulong, ulong_t)
ATOMIC_CAS(64, uint64_t)

void *
atomic_cas_ptr(volatile void *target, void *arg1, void *arg2)
{
	void *old;

	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	old = *(void **)target;
	if (old == arg1)
		*(void **)target = arg2;
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);

	return (old);
}


/*
 * Swap target and return old value
 */

#define	ATOMIC_SWAP(name, type) \
	type atomic_swap_##name(volatile type *target, type bits)	\
	{								\
		type old;						\
		VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);	\
		old = *target;						\
		*target = bits;						\
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);	\
		return (old);						\
	}

ATOMIC_SWAP(8, uint8_t)
ATOMIC_SWAP(uchar, uchar_t)
ATOMIC_SWAP(16, uint16_t)
ATOMIC_SWAP(ushort, ushort_t)
ATOMIC_SWAP(32, uint32_t)
ATOMIC_SWAP(uint, uint_t)
ATOMIC_SWAP(ulong, ulong_t)
ATOMIC_SWAP(64, uint64_t)
/* END CSTYLED */

void *
atomic_swap_ptr(volatile void *target, void *bits)
{
	void *old;

	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	old = *(void **)target;
	*(void **)target = bits;
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);

	return (old);
}


int
atomic_set_long_excl(volatile ulong_t *target, uint_t value)
{
	ulong_t bit;

	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	bit = (1UL << value);
	if ((*target & bit) != 0) {
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);
		return (-1);
	}
	*target |= bit;
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);

	return (0);
}

int
atomic_clear_long_excl(volatile ulong_t *target, uint_t value)
{
	ulong_t bit;

	VERIFY3S(pthread_mutex_lock(&atomic_lock), ==, 0);
	bit = (1UL << value);
	if ((*target & bit) == 0) {
		VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);
		return (-1);
	}
	*target &= ~bit;
	VERIFY3S(pthread_mutex_unlock(&atomic_lock), ==, 0);

	return (0);
}

void
membar_enter(void)
{
	/* XXX - Implement me */
}

void
membar_exit(void)
{
	/* XXX - Implement me */
}

void
membar_producer(void)
{
	/* XXX - Implement me */
}

void
membar_consumer(void)
{
	/* XXX - Implement me */
}
