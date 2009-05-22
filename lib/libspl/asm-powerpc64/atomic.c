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
 * Copyright (c) 2006 by Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <atomic.h>

/*
 * Theses are the void returning variants
 */

#define ATOMIC_INC(name, type) \
	void atomic_inc_##name(volatile type *target)	\
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	addi	%[lock],%[lock],1\n"		\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target)				\
		: "cc");					\
	}

ATOMIC_INC(long, unsigned long)
ATOMIC_INC(8, uint8_t)
ATOMIC_INC(uchar, uchar_t)
ATOMIC_INC(16, uint16_t)
ATOMIC_INC(ushort, ushort_t)
ATOMIC_INC(32, uint32_t)
ATOMIC_INC(uint, uint_t)
ATOMIC_INC(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
void atomic_inc_64(volatile uint64_t *target)
{
	/* XXX - Implement me */
	(*target)++;
}
#endif

#define ATOMIC_DEC(name, type) \
	void atomic_dec_##name(volatile type *target)	\
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	addi	%[lock],%[lock],-1\n"		\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target)				\
		: "cc");					\
	}

ATOMIC_DEC(long, unsigned long)
ATOMIC_DEC(8, uint8_t)
ATOMIC_DEC(uchar, uchar_t)
ATOMIC_DEC(16, uint16_t)
ATOMIC_DEC(ushort, ushort_t)
ATOMIC_DEC(32, uint32_t)
ATOMIC_DEC(uint, uint_t)
ATOMIC_DEC(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
void atomic_dec_64(volatile uint64_t *target)
{
	/* XXX - Implement me */
	(*target)--;
}
#endif

#define ATOMIC_ADD(name, type1, type2) \
	void atomic_add_##name(volatile type1 *target, type2 bits) \
	{							\
		type1 lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	add	%[lock],%[bits],%[lock]\n"	\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
	}

ATOMIC_ADD(8, uint8_t, int8_t)
ATOMIC_ADD(char, uchar_t, signed char)
ATOMIC_ADD(16, uint16_t, int16_t)
ATOMIC_ADD(short, ushort_t, short)
ATOMIC_ADD(32, uint32_t, int32_t)
ATOMIC_ADD(int, uint_t, int)
ATOMIC_ADD(long, ulong_t, long)

void atomic_add_ptr(volatile void *target, ssize_t bits)
{
	/* XXX - Implement me */
	*(caddr_t *)target += bits;
}

#if defined(_KERNEL) || defined(_INT64_TYPE)
void atomic_add_64(volatile uint64_t *target, int64_t bits)
{
	/* XXX - Implement me */
	*target += bits;
}
#endif

#define ATOMIC_OR(name, type) \
	void atomic_or_##name(volatile type *target, type bits) \
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	or	%[lock],%[bits],%[lock]\n"	\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
	}

//ATOMIC_OR(long, ulong_t)
ATOMIC_OR(8, uint8_t)
ATOMIC_OR(uchar, uchar_t)
ATOMIC_OR(16, uint16_t)
ATOMIC_OR(ushort, ushort_t)
ATOMIC_OR(32, uint32_t)
ATOMIC_OR(uint, uint_t)
ATOMIC_OR(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
void atomic_or_64(volatile uint64_t *target, uint64_t bits)
{
	/* XXX - Implement me */
	*target |= bits;
}
#endif

#define ATOMIC_AND(name, type) \
	void atomic_and_##name(volatile type *target, type bits) \
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	and	%[lock],%[bits],%[lock]\n"	\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
	}

//ATOMIC_AND(long, ulong_t)
ATOMIC_AND(8, uint8_t)
ATOMIC_AND(uchar, uchar_t)
ATOMIC_AND(16, uint16_t)
ATOMIC_AND(ushort, ushort_t)
ATOMIC_AND(32, uint32_t)
ATOMIC_AND(uint, uint_t)
ATOMIC_AND(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
void atomic_and_64(volatile uint64_t *target, uint64_t bits)
{
	/* XXX - Implement me */
	*target &= bits;
}
#endif

/*
 * New value returning variants
 */ 

#define ATOMIC_INC_NV(name, type) \
	type atomic_inc_##name##_nv(volatile type *target) \
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	addi	%[lock],%[lock],1\n"		\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target)				\
		: "cc");					\
								\
		return lock;					\
	}

ATOMIC_INC_NV(long, unsigned long)
ATOMIC_INC_NV(8, uint8_t)
ATOMIC_INC_NV(uchar, uchar_t)
ATOMIC_INC_NV(16, uint16_t)
ATOMIC_INC_NV(ushort, ushort_t)
ATOMIC_INC_NV(32, uint32_t)
ATOMIC_INC_NV(uint, uint_t)
ATOMIC_INC_NV(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_inc_64_nv(volatile uint64_t *target)
{
	/* XXX - Implement me */
	return (++(*target));
}
#endif

#define ATOMIC_DEC_NV(name, type) \
	type atomic_dec_##name##_nv(volatile type *target) \
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	addi	%[lock],%[lock],-1\n"		\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target)				\
		: "cc");					\
								\
		return lock;					\
	}

ATOMIC_DEC_NV(long, unsigned long)
ATOMIC_DEC_NV(8, uint8_t)
ATOMIC_DEC_NV(uchar, uchar_t)
ATOMIC_DEC_NV(16, uint16_t)
ATOMIC_DEC_NV(ushort, ushort_t)
ATOMIC_DEC_NV(32, uint32_t)
ATOMIC_DEC_NV(uint, uint_t)
ATOMIC_DEC_NV(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_dec_64_nv(volatile uint64_t *target)
{
	/* XXX - Implement me */
	return (--(*target));
}
#endif

#define ATOMIC_ADD_NV(name, type1, type2) \
	type1 atomic_add_##name##_nv(volatile type1 *target, type2 bits) \
	{							\
		type1 lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	add	%[lock],%[bits],%[lock]\n"	\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
								\
		return lock;					\
	}

ATOMIC_ADD_NV(8, uint8_t, int8_t)
ATOMIC_ADD_NV(char, uchar_t, signed char)
ATOMIC_ADD_NV(16, uint16_t, int16_t)
ATOMIC_ADD_NV(short, ushort_t, short)
ATOMIC_ADD_NV(32, uint32_t, int32_t)
ATOMIC_ADD_NV(int, uint_t, int)
ATOMIC_ADD_NV(long, ulong_t, long)

void *atomic_add_ptr_nv(volatile void *target, ssize_t bits)
{
	/* XXX - Implement me */
	return (*(caddr_t *)target += bits);
}

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_add_64_nv(volatile uint64_t *target, int64_t bits)
{
	/* XXX - Implement me */
	return (*target += bits);
}
#endif

#define ATOMIC_OR_NV(name, type) \
	type atomic_or_##name##_nv(volatile type *target, type bits) \
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	or	%[lock],%[bits],%[lock]\n"	\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
								\
		return lock;					\
	}

ATOMIC_OR_NV(long, unsigned long)
ATOMIC_OR_NV(8, uint8_t)
ATOMIC_OR_NV(uchar, uchar_t)
ATOMIC_OR_NV(16, uint16_t)
ATOMIC_OR_NV(ushort, ushort_t)
ATOMIC_OR_NV(32, uint32_t)
ATOMIC_OR_NV(uint, uint_t)
ATOMIC_OR_NV(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_or_64_nv(volatile uint64_t *target, uint64_t bits)
{
	/* XXX - Implement me */
	return (*target |= bits);
}
#endif

#define ATOMIC_AND_NV(name, type) \
	type atomic_and_##name##_nv(volatile type *target, type bits) \
	{							\
		type lock;					\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	and	%[lock],%[bits],%[lock]\n"	\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-    1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
								\
		return lock;					\
	}

ATOMIC_AND_NV(long, unsigned long)
ATOMIC_AND_NV(8, uint8_t)
ATOMIC_AND_NV(uchar, uchar_t)
ATOMIC_AND_NV(16, uint16_t)
ATOMIC_AND_NV(ushort, ushort_t)
ATOMIC_AND_NV(32, uint32_t)
ATOMIC_AND_NV(uint, uint_t)
ATOMIC_AND_NV(ulong, ulong_t)

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_and_64_nv(volatile uint64_t *target, uint64_t bits)
{
	/* XXX - Implement me */
	return (*target &= bits);
}
#endif


/*
 *  If *arg1 == arg2, set *arg1 = arg3; return old value
 */

#define ATOMIC_CAS(name, type) \
	type atomic_cas_##name(volatile type *target, type arg1, type arg2) \
	{							\
		type lock, old = *target;			\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	cmpw	%[lock],%[arg1]\n"		\
		"	bne-	2f\n"				\
		"	mr	%[lock],%[arg2]\n"		\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-	1b\n"				\
		"2:"						\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [arg1] "r" (arg1), [arg2] "r" (arg2)	\
		: "cc");					\
								\
		return old;					\
	}

ATOMIC_CAS(8, uint8_t)
ATOMIC_CAS(uchar, uchar_t)
ATOMIC_CAS(16, uint16_t)
ATOMIC_CAS(ushort, ushort_t)
ATOMIC_CAS(32, uint32_t)
ATOMIC_CAS(uint, uint_t)
ATOMIC_CAS(ulong, ulong_t)

void *atomic_cas_ptr(volatile void *target, void *arg1, void *arg2)
{
	/* XXX - Implement me */
	void *old = *(void **)target;
        if (old == arg1)
                *(void **)target = arg2;
        return (old);
}

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_cas_64(volatile uint64_t *target, uint64_t arg1, uint64_t arg2)
{
	/* XXX - Implement me */
	uint64_t old = *target;
        if (old == arg1)
                *target = arg2;
        return (old);
}
#endif

/*
 * Swap target and return old value
 */

#define ATOMIC_SWAP(name, type) \
	type atomic_swap_##name(volatile type *target, type bits) \
	{							\
		type lock, old = *target;			\
								\
		__asm__ __volatile__(				\
		"1:	lwarx	%[lock],0,%[target]\n"		\
		"	mr	%[lock],%[bits]\n"		\
		"	stwcx.	%[lock],0,%[target]\n"		\
		"	bne-	1b"				\
		: [lock] "=&r" (lock)				\
		: [target] "r" (target), [bits] "r" (bits)	\
		: "cc");					\
								\
		return old;					\
	}

ATOMIC_SWAP(8, uint8_t)
ATOMIC_SWAP(uchar, uchar_t)
ATOMIC_SWAP(16, uint16_t)
ATOMIC_SWAP(ushort, ushort_t)
ATOMIC_SWAP(32, uint32_t)
ATOMIC_SWAP(uint, uint_t)
ATOMIC_SWAP(ulong, ulong_t)

void *atomic_swap_ptr(volatile void *target, void *bits)
{
	/* XXX - Implement me */
	void *old = *(void **)target;
	*(void **)target = bits;
	return (old);
}

#if defined(_KERNEL) || defined(_INT64_TYPE)
uint64_t atomic_swap_64(volatile uint64_t *target, uint64_t bits)
{
	/* XXX - Implement me */
	uint64_t old = *target;
	*target = bits;
	return (old);
}
#endif

int atomic_set_long_excl(volatile ulong_t *target, uint_t value)
{
	/* XXX - Implement me */
	ulong_t bit = (1UL << value);
	if ((*target & bit) != 0)
		return (-1);
	*target |= bit;
	return (0);
}

int atomic_clear_long_excl(volatile ulong_t *target, uint_t value)
{
	/* XXX - Implement me */
	ulong_t bit = (1UL << value);
	if ((*target & bit) != 0)
		return (-1);
	*target &= ~bit;
	return (0);
}

void membar_enter(void)
{
	/* XXX - Implement me */
}

void membar_exit(void)
{
	/* XXX - Implement me */
}

void membar_producer(void)
{
	/* XXX - Implement me */
}

void membar_consumer(void)
{
	/* XXX - Implement me */
}

/* Legacy kernel interfaces; they will go away (eventually). */

uint8_t cas8(uint8_t *target, uint8_t arg1, uint8_t arg2)
{
	return atomic_cas_8(target, arg1, arg2);
}

uint32_t cas32(uint32_t *target, uint32_t arg1, uint32_t arg2)
{
	return atomic_cas_32(target, arg1, arg2);
}

uint64_t cas64(uint64_t *target, uint64_t arg1, uint64_t arg2)
{
	return atomic_cas_64(target, arg1, arg2);
}

ulong_t caslong(ulong_t *target, ulong_t arg1, ulong_t arg2)
{
	return atomic_cas_ulong(target, arg1, arg2);
}

void *casptr(void *target, void *arg1, void *arg2)
{
	return atomic_cas_ptr(target, arg1, arg2);
}

void atomic_and_long(ulong_t *target, ulong_t bits)
{
	return atomic_and_ulong(target, bits);
}

void atomic_or_long(ulong_t *target, ulong_t bits)
{
	return atomic_or_ulong(target, bits);
}
