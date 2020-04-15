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
 * OSX Atomic functions using clang builtins.
 *
 * Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_ATOMIC_H
#define	_SPL_ATOMIC_H
#include <sys/types.h>
#include <string.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Increment target
 */

#define	ATOMIC_INC(name, type)						\
	static inline __attribute__((always_inline))			\
	void atomic_inc_##name(volatile type *target)			\
	{								\
		(void) __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST); \
	}


ATOMIC_INC(8, uint8_t)
ATOMIC_INC(uchar, uchar_t)
ATOMIC_INC(16, uint16_t)
ATOMIC_INC(32, uint32_t)
ATOMIC_INC(64, uint64_t)

/* BEGIN CSTYLED */
ATOMIC_INC(ushort, ushort_t)
ATOMIC_INC(uint, uint_t)
ATOMIC_INC(ulong, ulong_t)
/* END CSTYLED */

#define	ATOMIC_INC_NV(name, type)			    \
	static inline __attribute__((always_inline))	    \
	type atomic_inc_##name##_nv(volatile type * target) \
	{						    \
	return (__atomic_add_fetch(target, 1, 		    \
		__ATOMIC_SEQ_CST));			    \
	    }



ATOMIC_INC_NV(8, uint8_t)
ATOMIC_INC_NV(uchar, uchar_t)
ATOMIC_INC_NV(16, uint16_t)
ATOMIC_INC_NV(32, uint32_t)
ATOMIC_INC_NV(64, uint64_t)

/* BEGIN CSTYLED */
ATOMIC_INC_NV(ushort, ushort_t)
ATOMIC_INC_NV(uint, uint_t)
ATOMIC_INC_NV(ulong, ulong_t)
/* END CSTYLED */

/*
 * Decrement target
 */


#define	ATOMIC_DEC(name, type)						\
	static inline __attribute__((always_inline))			\
	void atomic_dec_##name(volatile type *target)			\
	{								\
		(void) __atomic_sub_fetch(target, 1,			\
		    __ATOMIC_SEQ_CST);					\
	}



ATOMIC_DEC(8, uint8_t)
ATOMIC_DEC(uchar, uchar_t)
ATOMIC_DEC(16, uint16_t)
ATOMIC_DEC(32, uint32_t)
ATOMIC_DEC(64, uint64_t)

/* BEGIN CSTYLED */
ATOMIC_DEC(uint, uint_t)
ATOMIC_DEC(ulong, ulong_t)
ATOMIC_DEC(ushort, ushort_t)
/* END CSTYLED */

#define	ATOMIC_DEC_NV(name, type)					\
	static inline __attribute__((always_inline))			\
	type atomic_dec_##name##_nv(volatile type *target)		\
	{								\
		return (__atomic_sub_fetch(target, 1,			\
			__ATOMIC_SEQ_CST));				\
	}


ATOMIC_DEC_NV(16, uint16_t)
ATOMIC_DEC_NV(32, uint32_t)
ATOMIC_DEC_NV(64, uint64_t)
ATOMIC_DEC_NV(8, uint8_t)
ATOMIC_DEC_NV(uchar, uchar_t)

/* BEGIN CSTYLED */
ATOMIC_DEC_NV(uint, uint_t)
ATOMIC_DEC_NV(ulong, ulong_t)
ATOMIC_DEC_NV(ushort, ushort_t)
/* END CSTYLED */

/*
 * Add delta to target
 */

#define	ATOMIC_ADD(name, type1, type2)					\
	static inline __attribute__((always_inline))			\
	void atomic_add_##name(volatile type1 *target, type2 bits)      \
	{                                                               \
		(void) __atomic_add_fetch((volatile type2 *) target,	\
		    bits, __ATOMIC_SEQ_CST);				\
	}


ATOMIC_ADD(16, uint16_t, int16_t)
ATOMIC_ADD(32, uint32_t, int32_t)
ATOMIC_ADD(64, uint64_t, int64_t)
ATOMIC_ADD(8, uint8_t, int8_t)
ATOMIC_ADD(char, uchar_t, signed char)

/* BEGIN CSTYLED */
ATOMIC_ADD(int, uint_t, int)
ATOMIC_ADD(long, ulong_t, long)
ATOMIC_ADD(short, ushort_t, short)
/* END CSTYLED */

#define	ATOMIC_ADD_NV(name, type1, type2)			\
	static inline __attribute__((always_inline))		\
	type1 atomic_add_##name##_nv(volatile type1 *target,	\
	    type2 x)						\
	{							\
		return ((type1)__atomic_add_fetch(		\
			(volatile type2 *)target, x,		\
			__ATOMIC_SEQ_CST));			\
	}

ATOMIC_ADD_NV(16, uint16_t, int16_t)
ATOMIC_ADD_NV(32, uint32_t, int32_t)
ATOMIC_ADD_NV(64, uint64_t, int64_t)
ATOMIC_ADD_NV(8, uint8_t, int8_t)
ATOMIC_ADD_NV(char, uchar_t, signed char)

/* BEGIN CSTYLED */
ATOMIC_ADD_NV(int, uint_t, int)
ATOMIC_ADD_NV(long, ulong_t, long)
ATOMIC_ADD_NV(short, ushort_t, short)
/* END CSTYLED */

/*
 * Subtract delta to target
 */

#define	ATOMIC_SUB(name, type1, type2)					\
	static inline __attribute__((always_inline))			\
	void atomic_sub_##name(volatile type1 *target, type2 bits)      \
	{								\
		(void) __atomic_sub_fetch((volatile type2 *)target,	\
		    bits, __ATOMIC_SEQ_CST);				\
	}


ATOMIC_SUB(16, uint16_t, int16_t)
ATOMIC_SUB(32, uint32_t, int32_t)
ATOMIC_SUB(64, uint64_t, int64_t)
ATOMIC_SUB(8, uint8_t, int8_t)
ATOMIC_SUB(char, uchar_t, signed char)

/* BEGIN CSTYLED */
ATOMIC_SUB(int, uint_t, int)
ATOMIC_SUB(long, ulong_t, long)
ATOMIC_SUB(short, ushort_t, short)
/* END CSTYLED */

#define	ATOMIC_SUB_NV(name, type1, type2)				\
	static inline __attribute__((always_inline))			\
	type1 atomic_sub_##name##_nv(volatile type1 *target,		\
	    type2 bits)							\
	{								\
		return ((type1) __atomic_sub_fetch(			\
			(volatile type2 *)target,			\
			bits, __ATOMIC_SEQ_CST));			\
	}

ATOMIC_SUB_NV(16, uint16_t, int16_t)
ATOMIC_SUB_NV(32, uint32_t, int32_t)
ATOMIC_SUB_NV(64, uint64_t, int64_t)
ATOMIC_SUB_NV(8, uint8_t, int8_t)
ATOMIC_SUB_NV(char, uchar_t, signed char)

/* BEGIN CSTYLED */
ATOMIC_SUB_NV(int, uint_t, int)
ATOMIC_SUB_NV(long, ulong_t, long)
ATOMIC_SUB_NV(short, ushort_t, short)
/* END CSTYLED */

/*
 * logical OR bits with target
 */

#define	ATOMIC_OR(name, type)						\
	static inline __attribute__((always_inline))			\
	void atomic_or_##name(volatile type *target, type bits)         \
	{								\
		(void) __atomic_or_fetch(target, bits, __ATOMIC_SEQ_CST); \
	}


ATOMIC_OR(16, uint16_t)
ATOMIC_OR(32, uint32_t)
ATOMIC_OR(64, uint64_t)
ATOMIC_OR(8, uint8_t)
ATOMIC_OR(uchar, uchar_t)

/* BEGIN CSTYLED */
ATOMIC_OR(uint, uint_t)
ATOMIC_OR(ulong, ulong_t)
ATOMIC_OR(ushort, ushort_t)
/* END CSTYLED */

#define	ATOMIC_OR_NV(name, type)					\
	static inline __attribute__((always_inline))			\
	type atomic_or_##name##_nv(volatile type *target, type bits)    \
	{								\
		return (__atomic_or_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_OR_NV(16, uint16_t)
ATOMIC_OR_NV(32, uint32_t)
ATOMIC_OR_NV(64, uint64_t)
ATOMIC_OR_NV(8, uint8_t)
ATOMIC_OR_NV(uchar, uchar_t)

/* BEGIN CSTYLED */
ATOMIC_OR_NV(uint, uint_t)
ATOMIC_OR_NV(ulong, ulong_t)
ATOMIC_OR_NV(ushort, ushort_t)
/* END CSTYLED */

/*
 * logical AND bits with target
 */

#define	ATOMIC_AND(name, type)						\
	static inline __attribute__((always_inline))			\
	void atomic_and_##name(volatile type *target, type bits)        \
	{								\
		(void) __atomic_and_fetch(target, bits, __ATOMIC_SEQ_CST); \
	}

ATOMIC_AND(16, uint16_t)
ATOMIC_AND(32, uint32_t)
ATOMIC_AND(64, uint64_t)
ATOMIC_AND(8, uint8_t)
ATOMIC_AND(uchar, uchar_t)

/* BEGIN CSTYLED */
ATOMIC_AND(uint, uint_t)
ATOMIC_AND(ulong, ulong_t)
ATOMIC_AND(ushort, ushort_t)
/* END CSTYLED */

/*
 * Compare And Set
 * if *arg1 == arg2, then set *arg1 = arg3; return old value.
 */

#define	ATOMIC_CAS(name, type)						\
	static inline __attribute__((always_inline))			\
	type atomic_cas_##name(volatile type *target, type exp, type des) \
	{                                                               \
		__atomic_compare_exchange_n(target,			\
		    &exp, des, B_FALSE,					\
		    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);                \
		return (exp);                                           \
	}

ATOMIC_CAS(16, uint16_t)
ATOMIC_CAS(32, uint32_t)
ATOMIC_CAS(64, uint64_t)
ATOMIC_CAS(8, uint8_t)
ATOMIC_CAS(uchar, uchar_t)

/* BEGIN CSTYLED */
ATOMIC_CAS(uint, uint_t)
ATOMIC_CAS(ulong, ulong_t)
ATOMIC_CAS(ushort, ushort_t)
/* END CSTYLED */

#define	ATOMIC_SWAP(name, type)						\
	static inline __attribute__((always_inline))			\
	type atomic_swap_##name(volatile type *target, type bits)       \
	{                                                               \
		return (__atomic_exchange_n(target, bits, __ATOMIC_SEQ_CST)); \
	}

ATOMIC_SWAP(16, uint16_t)
ATOMIC_SWAP(32, uint32_t)
ATOMIC_SWAP(64, uint64_t)
ATOMIC_SWAP(8, uint8_t)
ATOMIC_SWAP(uchar, uchar_t)

/* BEGIN CSTYLED */
ATOMIC_SWAP(uint, uint_t)
ATOMIC_SWAP(ulong, ulong_t)
ATOMIC_SWAP(ushort, ushort_t)
/* END CSTYLED */

extern void *
atomic_cas_ptr(volatile void *target, void *cmp,  void *newval);

static inline uint64_t
atomic_load_64(volatile uint64_t *target)
{
	return (__atomic_load_n(target, __ATOMIC_ACQUIRE));
}

static inline void
atomic_store_64(volatile uint64_t *target, uint64_t bits)
{
	return (__atomic_store_n(target, bits, __ATOMIC_RELEASE));
}

static inline __attribute__((always_inline)) void
membar_producer(void)
{
	/*
	 * On arm, Xcode 14 clang produces "dmb ish" here, which
	 * matches the assembly in xnu's dtrace_membar_prodcer()
	 */
	__c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
}

static inline __attribute__((always_inline)) void
membar_consumer(void)
{
	__c11_atomic_thread_fence(__ATOMIC_ACQ_REL);
}

#ifdef	__cplusplus
}
#endif
#endif  /* _SPL_ATOMIC_H */
