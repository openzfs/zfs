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
 * Copyright (c) 2009 by Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_SYS_ATOMIC_H
#define	_SYS_ATOMIC_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__STDC__)

/*
 * "token override" machinery. _SELECT_OVERRIDE expands the first token if it
 * is defined with _OVERRIDE(), otherwise the second.
 */
/* CSTYLED */
#define	_SPL_ATOMIC_OVERRIDE(v)		, v
#define	_SPL_ATOMIC_SELECT_3(a, b, c, ...)	c
#define	_SPL_ATOMIC_SELECT_OVERRIDE(...) \
	_SPL_ATOMIC_SELECT_3(, __VA_ARGS__)

/*
 * Emit type for param or return in signature. If type is void, use void*,
 * otherwise use as-is.
 */
#define	_SPL_ATOMIC_PARAM_void		_SPL_ATOMIC_OVERRIDE(void *)
#define	_SPL_ATOMIC_PARAM(type)	\
	_SPL_ATOMIC_SELECT_OVERRIDE(_SPL_ATOMIC_PARAM_##type, type)

/*
 * Emit target token, with cast if necessary. If type is void, cast to void**,
 * otherwise use as-is.
 */
#define	_SPL_ATOMIC_CAST_void(target)	_SPL_ATOMIC_OVERRIDE((void **)target)
#define	_SPL_ATOMIC_CAST(type, target)	\
	_SPL_ATOMIC_SELECT_OVERRIDE(_SPL_ATOMIC_CAST_##type(target), target)

/*
 * Increment target.
 */
#define	SPL_ATOMIC_INC(name, type) \
	static inline void						\
	atomic_inc_##name(volatile type *target) {			\
		__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);	\
	}

SPL_ATOMIC_INC(8, uint8_t)
SPL_ATOMIC_INC(16, uint16_t)
SPL_ATOMIC_INC(32, uint32_t)
SPL_ATOMIC_INC(64, uint64_t)
SPL_ATOMIC_INC(uchar, uchar_t)
SPL_ATOMIC_INC(ushort, ushort_t)
SPL_ATOMIC_INC(uint, uint_t)
SPL_ATOMIC_INC(ulong, ulong_t)
#undef SPL_ATOMIC_INC

/*
 * Decrement target
 */
#define	SPL_ATOMIC_DEC(name, type) \
	static inline void						\
	atomic_dec_##name(volatile type *target) {			\
		__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);	\
	}

SPL_ATOMIC_DEC(8, uint8_t)
SPL_ATOMIC_DEC(16, uint16_t)
SPL_ATOMIC_DEC(32, uint32_t)
SPL_ATOMIC_DEC(64, uint64_t)
SPL_ATOMIC_DEC(uchar, uchar_t)
SPL_ATOMIC_DEC(ushort, ushort_t)
SPL_ATOMIC_DEC(uint, uint_t)
SPL_ATOMIC_DEC(ulong, ulong_t)
#undef SPL_ATOMIC_DEC

/*
 * Add delta to target
 */
#define	SPL_ATOMIC_ADD(name, type1, type2) \
	static inline void						\
	atomic_add_##name(volatile type1 *target, type2 bits) {		\
		__atomic_add_fetch(_SPL_ATOMIC_CAST(type1, target),	\
		    bits, __ATOMIC_SEQ_CST);				\
	}

SPL_ATOMIC_ADD(8, uint8_t, int8_t)
SPL_ATOMIC_ADD(16, uint16_t, int16_t)
SPL_ATOMIC_ADD(32, uint32_t, int32_t)
SPL_ATOMIC_ADD(64, uint64_t, int64_t)
SPL_ATOMIC_ADD(char, uchar_t, signed char)
SPL_ATOMIC_ADD(short, ushort_t, short)
SPL_ATOMIC_ADD(int, uint_t, int)
SPL_ATOMIC_ADD(long, ulong_t, long)
SPL_ATOMIC_ADD(ptr, void, ssize_t)
#undef SPL_ATOMIC_ADD

/*
 * Subtract delta from target
 */
#define	SPL_ATOMIC_SUB(name, type1, type2) \
	static inline void						\
	atomic_sub_##name(volatile type1 *target, type2 bits) {		\
		__atomic_sub_fetch(_SPL_ATOMIC_CAST(type1, target),	\
		    bits, __ATOMIC_SEQ_CST);				\
	}

SPL_ATOMIC_SUB(8, uint8_t, int8_t)
SPL_ATOMIC_SUB(16, uint16_t, int16_t)
SPL_ATOMIC_SUB(32, uint32_t, int32_t)
SPL_ATOMIC_SUB(64, uint64_t, int64_t)
SPL_ATOMIC_SUB(char, uchar_t, signed char)
SPL_ATOMIC_SUB(short, ushort_t, short)
SPL_ATOMIC_SUB(int, uint_t, int)
SPL_ATOMIC_SUB(long, ulong_t, long)
SPL_ATOMIC_SUB(ptr, void, ssize_t)
#undef SPL_ATOMIC_SUB

/*
 * logical OR bits with target
 */
#define	SPL_ATOMIC_OR(name, type) \
	static inline void						\
	atomic_or_##name(volatile type *target, type bits) {		\
		__atomic_or_fetch(target, bits, __ATOMIC_SEQ_CST);	\
	}

SPL_ATOMIC_OR(8, uint8_t)
SPL_ATOMIC_OR(16, uint16_t)
SPL_ATOMIC_OR(32, uint32_t)
SPL_ATOMIC_OR(64, uint64_t)
SPL_ATOMIC_OR(uchar, uchar_t)
SPL_ATOMIC_OR(ushort, ushort_t)
SPL_ATOMIC_OR(uint, uint_t)
SPL_ATOMIC_OR(ulong, ulong_t)
#undef SPL_ATOMIC_OR

/*
 * logical AND bits with target
 */
#define	SPL_ATOMIC_AND(name, type) \
	static inline void						\
	atomic_and_##name(volatile type *target, type bits) {		\
		__atomic_and_fetch(target, bits, __ATOMIC_SEQ_CST);	\
	}

SPL_ATOMIC_AND(8, uint8_t)
SPL_ATOMIC_AND(16, uint16_t)
SPL_ATOMIC_AND(32, uint32_t)
SPL_ATOMIC_AND(64, uint64_t)
SPL_ATOMIC_AND(uchar, uchar_t)
SPL_ATOMIC_AND(ushort, ushort_t)
SPL_ATOMIC_AND(uint, uint_t)
SPL_ATOMIC_AND(ulong, ulong_t)
#undef SPL_ATOMIC_AND

/*
 * As above, but return the new value.  Note that these _nv() variants are
 * substantially more expensive on some platforms than the no-return-value
 * versions above, so don't use them unless you really need to know the
 * new value *atomically* (e.g. when decrementing a reference count and
 * checking whether it went to zero).
 */

/*
 * Increment target and return new value.
 */
#define	SPL_ATOMIC_INC_NV(name, type) \
	static inline type						\
	atomic_inc_##name##_nv(volatile type *target) {			\
		return (__atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST)); \
	}

SPL_ATOMIC_INC_NV(8, uint8_t)
SPL_ATOMIC_INC_NV(16, uint16_t)
SPL_ATOMIC_INC_NV(32, uint32_t)
SPL_ATOMIC_INC_NV(64, uint64_t)
SPL_ATOMIC_INC_NV(uchar, uchar_t)
SPL_ATOMIC_INC_NV(ushort, ushort_t)
SPL_ATOMIC_INC_NV(uint, uint_t)
SPL_ATOMIC_INC_NV(ulong, ulong_t)
#undef SPL_ATOMIC_INC_NV

/*
 * Decrement target and return new value.
 */
#define	SPL_ATOMIC_DEC_NV(name, type) \
	static inline type						\
	atomic_dec_##name##_nv(volatile type *target) {			\
		return (__atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST)); \
	}

SPL_ATOMIC_DEC_NV(8, uint8_t)
SPL_ATOMIC_DEC_NV(16, uint16_t)
SPL_ATOMIC_DEC_NV(32, uint32_t)
SPL_ATOMIC_DEC_NV(64, uint64_t)
SPL_ATOMIC_DEC_NV(uchar, uchar_t)
SPL_ATOMIC_DEC_NV(ushort, ushort_t)
SPL_ATOMIC_DEC_NV(uint, uint_t)
SPL_ATOMIC_DEC_NV(ulong, ulong_t)
#undef SPL_ATOMIC_DEC_NV

/*
 * Add delta to target
 */
#define	SPL_ATOMIC_ADD_NV(name, type1, type2) \
	static inline _SPL_ATOMIC_PARAM(type1)				\
	atomic_add_##name##_nv(volatile type1 *target, type2 bits) {	\
		return (__atomic_add_fetch(_SPL_ATOMIC_CAST(type1, target), \
		    bits, __ATOMIC_SEQ_CST)); \
	}

SPL_ATOMIC_ADD_NV(8, uint8_t, int8_t)
SPL_ATOMIC_ADD_NV(16, uint16_t, int16_t)
SPL_ATOMIC_ADD_NV(32, uint32_t, int32_t)
SPL_ATOMIC_ADD_NV(64, uint64_t, int64_t)
SPL_ATOMIC_ADD_NV(char, uchar_t, signed char)
SPL_ATOMIC_ADD_NV(short, ushort_t, short)
SPL_ATOMIC_ADD_NV(int, uint_t, int)
SPL_ATOMIC_ADD_NV(long, ulong_t, long)
SPL_ATOMIC_ADD_NV(ptr, void, ssize_t)
#undef SPL_ATOMIC_ADD_NV

/*
 * Subtract delta from target
 */
#define	SPL_ATOMIC_SUB_NV(name, type1, type2) \
	static inline _SPL_ATOMIC_PARAM(type1)				\
	atomic_sub_##name##_nv(volatile type1 *target, type2 bits) {	\
		return (__atomic_sub_fetch(_SPL_ATOMIC_CAST(type1, target), \
		    bits, __ATOMIC_SEQ_CST)); \
	}

SPL_ATOMIC_SUB_NV(8, uint8_t, int8_t)
SPL_ATOMIC_SUB_NV(16, uint16_t, int16_t)
SPL_ATOMIC_SUB_NV(32, uint32_t, int32_t)
SPL_ATOMIC_SUB_NV(64, uint64_t, int64_t)
SPL_ATOMIC_SUB_NV(char, uchar_t, signed char)
SPL_ATOMIC_SUB_NV(short, ushort_t, short)
SPL_ATOMIC_SUB_NV(int, uint_t, int)
SPL_ATOMIC_SUB_NV(long, ulong_t, long)
SPL_ATOMIC_SUB_NV(ptr, void, ssize_t)
#undef SPL_ATOMIC_SUB_NV

/*
 * logical OR bits with target and return new value.
 */
#define	SPL_ATOMIC_OR_NV(name, type) \
	static inline type						\
	atomic_or_##name##_nv(volatile type *target, type bits)	{	\
		return (__atomic_or_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

SPL_ATOMIC_OR_NV(8, uint8_t)
SPL_ATOMIC_OR_NV(16, uint16_t)
SPL_ATOMIC_OR_NV(32, uint32_t)
SPL_ATOMIC_OR_NV(64, uint64_t)
SPL_ATOMIC_OR_NV(uchar, uchar_t)
SPL_ATOMIC_OR_NV(ushort, ushort_t)
SPL_ATOMIC_OR_NV(uint, uint_t)
SPL_ATOMIC_OR_NV(ulong, ulong_t)
#undef SPL_ATOMIC_OR_NV

/*
 * logical AND bits with target and return new value.
 */

#define	SPL_ATOMIC_AND_NV(name, type) \
	static inline type						\
	atomic_and_##name##_nv(volatile type *target, type bits) {	\
		return (__atomic_and_fetch(target, bits, __ATOMIC_SEQ_CST)); \
	}

SPL_ATOMIC_AND_NV(8, uint8_t)
SPL_ATOMIC_AND_NV(16, uint16_t)
SPL_ATOMIC_AND_NV(32, uint32_t)
SPL_ATOMIC_AND_NV(64, uint64_t)
SPL_ATOMIC_AND_NV(uchar, uchar_t)
SPL_ATOMIC_AND_NV(ushort, ushort_t)
SPL_ATOMIC_AND_NV(uint, uint_t)
SPL_ATOMIC_AND_NV(ulong, ulong_t)
#undef SPL_ATOMIC_AND_NV

/*
 * If *tgt == exp, set *tgt = des; return old value
 *
 * This may not look right on the first pass (or the sixteenth), but,
 * from https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html:
 * > If they are not equal, the operation is a read
 * > and the current contents of *ptr are written into *expected.
 * And, in the converse case, exp is already *target by definition.
 */
#define	SPL_ATOMIC_CAS(name, type) \
	static inline _SPL_ATOMIC_PARAM(type)				\
	atomic_cas_##name(volatile type *target,			\
	    _SPL_ATOMIC_PARAM(type) exp, _SPL_ATOMIC_PARAM(type) des) { \
		__atomic_compare_exchange_n(_SPL_ATOMIC_CAST(type, target), \
		    &exp, des, B_FALSE,	__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
		return (exp);						\
	}

SPL_ATOMIC_CAS(8, uint8_t)
SPL_ATOMIC_CAS(16, uint16_t)
SPL_ATOMIC_CAS(32, uint32_t)
SPL_ATOMIC_CAS(64, uint64_t)
SPL_ATOMIC_CAS(uchar, uchar_t)
SPL_ATOMIC_CAS(ushort, ushort_t)
SPL_ATOMIC_CAS(uint, uint_t)
SPL_ATOMIC_CAS(ulong, ulong_t)
SPL_ATOMIC_CAS(ptr, void)
#undef SPL_ATOMIC_CAS

/*
 * Swap target and return old value
 */
#define	SPL_ATOMIC_SWAP(name, type) \
	static inline _SPL_ATOMIC_PARAM(type)				\
	atomic_swap_##name(volatile type *target,			\
	    _SPL_ATOMIC_PARAM(type) bits) {				\
		return (__atomic_exchange_n(_SPL_ATOMIC_CAST(type, target), \
		    bits, __ATOMIC_SEQ_CST));				\
	}

SPL_ATOMIC_SWAP(8, uint8_t)
SPL_ATOMIC_SWAP(16, uint16_t)
SPL_ATOMIC_SWAP(32, uint32_t)
SPL_ATOMIC_SWAP(64, uint64_t)
SPL_ATOMIC_SWAP(uchar, uchar_t)
SPL_ATOMIC_SWAP(ushort, ushort_t)
SPL_ATOMIC_SWAP(uint, uint_t)
SPL_ATOMIC_SWAP(ulong, ulong_t)
SPL_ATOMIC_SWAP(ptr, void)
#undef SPL_ATOMIC_SWAP

/*
 * Atomically read variable.
 */
#define	SPL_ATOMIC_LOAD(name, type) \
	static inline _SPL_ATOMIC_PARAM(type)				\
	atomic_load_##name(volatile type *target) {			\
		return (__atomic_load_n(_SPL_ATOMIC_CAST(type, target), \
		    __ATOMIC_RELAXED));					\
	}

SPL_ATOMIC_LOAD(8, uint8_t)
SPL_ATOMIC_LOAD(16, uint16_t)
SPL_ATOMIC_LOAD(32, uint32_t)
SPL_ATOMIC_LOAD(64, uint64_t)
SPL_ATOMIC_LOAD(char, uchar_t)
SPL_ATOMIC_LOAD(short, ushort_t)
SPL_ATOMIC_LOAD(int, uint_t)
SPL_ATOMIC_LOAD(long, ulong_t)
SPL_ATOMIC_LOAD(ptr, void)
#undef SPL_ATOMIC_LOAD

/*
 * Atomically write variable.
 */
#define	SPL_ATOMIC_STORE(name, type) \
	static inline void						\
	atomic_store_##name(volatile type *target,			\
	    _SPL_ATOMIC_PARAM(type) bits) {				\
		__atomic_store_n(_SPL_ATOMIC_CAST(type, target), bits,	\
		    __ATOMIC_RELAXED);					\
	}

SPL_ATOMIC_STORE(8, uint8_t)
SPL_ATOMIC_STORE(16, uint16_t)
SPL_ATOMIC_STORE(32, uint32_t)
SPL_ATOMIC_STORE(64, uint64_t)
SPL_ATOMIC_STORE(char, uchar_t)
SPL_ATOMIC_STORE(short, ushort_t)
SPL_ATOMIC_STORE(int, uint_t)
SPL_ATOMIC_STORE(long, ulong_t)
SPL_ATOMIC_STORE(ptr, void)
#undef SPL_ATOMIC_STORE

/*
 * Perform an exclusive atomic bit set/clear on a target.
 * Returns 0 if bit was successfully set/cleared, or -1
 * if the bit was already set/cleared.
 */
static inline int
atomic_set_long_excl(volatile ulong_t *target, uint_t value)
{
	ulong_t bit = 1UL << value;
	ulong_t old = __atomic_fetch_or(target, bit, __ATOMIC_SEQ_CST);
	return ((old & bit) ? -1 : 0);
}

static inline int
atomic_clear_long_excl(volatile ulong_t *target, uint_t value)
{
	ulong_t bit = 1UL << value;
	ulong_t old = __atomic_fetch_and(target, ~bit, __ATOMIC_SEQ_CST);
	return ((old & bit) ? 0 : -1);
}

/*
 * Make all stores and loads emitted prior to the the barrier complete before
 * crossing it, while also making sure stores and loads emitted after the
 * barrier only start being executed after crossing it.
 */
static inline void
membar_sync(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/*
 * Arrange that all stores issued before this point in the code reach
 * global visibility before any stores that follow; useful in producer
 * modules that update a data item, then set a flag that it is available.
 * The memory barrier guarantees that the available flag is not visible
 * earlier than the updated data, i.e. it imposes store ordering.
 */
static inline void
membar_producer(void)
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

/*
 * Arrange that all loads issued before this point in the code are
 * completed before any subsequent loads; useful in consumer modules
 * that check to see if data is available and read the data.
 * The memory barrier guarantees that the data is not sampled until
 * after the available flag has been seen, i.e. it imposes load ordering.
 */
static inline void
membar_consumer(void)
{
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
}

#undef	_SPL_ATOMIC_CAST
#undef	_SPL_ATOMIC_CAST_void
#undef	_SPL_ATOMIC_PARAM
#undef	_SPL_ATOMIC_PARAM_void
#undef	_SPL_ATOMIC_SELECT_OVERRIDE
#undef	_SPL_ATOMIC_SELECT_3
#undef	_SPL_ATOMIC_OVERRIDE

#endif  /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ATOMIC_H */
