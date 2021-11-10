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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_ATOMIC_H
#define	_SYS_ATOMIC_H

#include <sys/types.h>
#include <sys/inttypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__STDC__)
/*
 * Increment target.
 */
extern void atomic_inc_8(volatile uint8_t *);
extern void atomic_inc_uchar(volatile uchar_t *);
extern void atomic_inc_16(volatile uint16_t *);
extern void atomic_inc_ushort(volatile ushort_t *);
extern void atomic_inc_32(volatile uint32_t *);
extern void atomic_inc_uint(volatile uint_t *);
extern void atomic_inc_ulong(volatile ulong_t *);
#if defined(_INT64_TYPE)
extern void atomic_inc_64(volatile uint64_t *);
#endif

/*
 * Decrement target
 */
extern void atomic_dec_8(volatile uint8_t *);
extern void atomic_dec_uchar(volatile uchar_t *);
extern void atomic_dec_16(volatile uint16_t *);
extern void atomic_dec_ushort(volatile ushort_t *);
extern void atomic_dec_32(volatile uint32_t *);
extern void atomic_dec_uint(volatile uint_t *);
extern void atomic_dec_ulong(volatile ulong_t *);
#if defined(_INT64_TYPE)
extern void atomic_dec_64(volatile uint64_t *);
#endif

/*
 * Add delta to target
 */
extern void atomic_add_8(volatile uint8_t *, int8_t);
extern void atomic_add_char(volatile uchar_t *, signed char);
extern void atomic_add_16(volatile uint16_t *, int16_t);
extern void atomic_add_short(volatile ushort_t *, short);
extern void atomic_add_32(volatile uint32_t *, int32_t);
extern void atomic_add_int(volatile uint_t *, int);
extern void atomic_add_ptr(volatile void *, ssize_t);
extern void atomic_add_long(volatile ulong_t *, long);
#if defined(_INT64_TYPE)
extern void atomic_add_64(volatile uint64_t *, int64_t);
#endif

/*
 * Subtract delta from target
 */
extern void atomic_sub_8(volatile uint8_t *, int8_t);
extern void atomic_sub_char(volatile uchar_t *, signed char);
extern void atomic_sub_16(volatile uint16_t *, int16_t);
extern void atomic_sub_short(volatile ushort_t *, short);
extern void atomic_sub_32(volatile uint32_t *, int32_t);
extern void atomic_sub_int(volatile uint_t *, int);
extern void atomic_sub_ptr(volatile void *, ssize_t);
extern void atomic_sub_long(volatile ulong_t *, long);
#if defined(_INT64_TYPE)
extern void atomic_sub_64(volatile uint64_t *, int64_t);
#endif

/*
 * logical OR bits with target
 */
extern void atomic_or_8(volatile uint8_t *, uint8_t);
extern void atomic_or_uchar(volatile uchar_t *, uchar_t);
extern void atomic_or_16(volatile uint16_t *, uint16_t);
extern void atomic_or_ushort(volatile ushort_t *, ushort_t);
extern void atomic_or_32(volatile uint32_t *, uint32_t);
extern void atomic_or_uint(volatile uint_t *, uint_t);
extern void atomic_or_ulong(volatile ulong_t *, ulong_t);
#if defined(_INT64_TYPE)
extern void atomic_or_64(volatile uint64_t *, uint64_t);
#endif

/*
 * logical AND bits with target
 */
extern void atomic_and_8(volatile uint8_t *, uint8_t);
extern void atomic_and_uchar(volatile uchar_t *, uchar_t);
extern void atomic_and_16(volatile uint16_t *, uint16_t);
extern void atomic_and_ushort(volatile ushort_t *, ushort_t);
extern void atomic_and_32(volatile uint32_t *, uint32_t);
extern void atomic_and_uint(volatile uint_t *, uint_t);
extern void atomic_and_ulong(volatile ulong_t *, ulong_t);
#if defined(_INT64_TYPE)
extern void atomic_and_64(volatile uint64_t *, uint64_t);
#endif

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
extern uint8_t atomic_inc_8_nv(volatile uint8_t *);
extern uchar_t atomic_inc_uchar_nv(volatile uchar_t *);
extern uint16_t atomic_inc_16_nv(volatile uint16_t *);
extern ushort_t atomic_inc_ushort_nv(volatile ushort_t *);
extern uint32_t atomic_inc_32_nv(volatile uint32_t *);
extern uint_t atomic_inc_uint_nv(volatile uint_t *);
extern ulong_t atomic_inc_ulong_nv(volatile ulong_t *);
#if defined(_INT64_TYPE)
extern uint64_t atomic_inc_64_nv(volatile uint64_t *);
#endif

/*
 * Decrement target and return new value.
 */
extern uint8_t atomic_dec_8_nv(volatile uint8_t *);
extern uchar_t atomic_dec_uchar_nv(volatile uchar_t *);
extern uint16_t atomic_dec_16_nv(volatile uint16_t *);
extern ushort_t atomic_dec_ushort_nv(volatile ushort_t *);
extern uint32_t atomic_dec_32_nv(volatile uint32_t *);
extern uint_t atomic_dec_uint_nv(volatile uint_t *);
extern ulong_t atomic_dec_ulong_nv(volatile ulong_t *);
#if defined(_INT64_TYPE)
extern uint64_t atomic_dec_64_nv(volatile uint64_t *);
#endif

/*
 * Add delta to target
 */
extern uint8_t atomic_add_8_nv(volatile uint8_t *, int8_t);
extern uchar_t atomic_add_char_nv(volatile uchar_t *, signed char);
extern uint16_t atomic_add_16_nv(volatile uint16_t *, int16_t);
extern ushort_t atomic_add_short_nv(volatile ushort_t *, short);
extern uint32_t atomic_add_32_nv(volatile uint32_t *, int32_t);
extern uint_t atomic_add_int_nv(volatile uint_t *, int);
extern void *atomic_add_ptr_nv(volatile void *, ssize_t);
extern ulong_t atomic_add_long_nv(volatile ulong_t *, long);
#if defined(_INT64_TYPE)
extern uint64_t atomic_add_64_nv(volatile uint64_t *, int64_t);
#endif

/*
 * Subtract delta from target
 */
extern uint8_t atomic_sub_8_nv(volatile uint8_t *, int8_t);
extern uchar_t atomic_sub_char_nv(volatile uchar_t *, signed char);
extern uint16_t atomic_sub_16_nv(volatile uint16_t *, int16_t);
extern ushort_t atomic_sub_short_nv(volatile ushort_t *, short);
extern uint32_t atomic_sub_32_nv(volatile uint32_t *, int32_t);
extern uint_t atomic_sub_int_nv(volatile uint_t *, int);
extern void *atomic_sub_ptr_nv(volatile void *, ssize_t);
extern ulong_t atomic_sub_long_nv(volatile ulong_t *, long);
#if defined(_INT64_TYPE)
extern uint64_t atomic_sub_64_nv(volatile uint64_t *, int64_t);
#endif

/*
 * logical OR bits with target and return new value.
 */
extern uint8_t atomic_or_8_nv(volatile uint8_t *, uint8_t);
extern uchar_t atomic_or_uchar_nv(volatile uchar_t *, uchar_t);
extern uint16_t atomic_or_16_nv(volatile uint16_t *, uint16_t);
extern ushort_t atomic_or_ushort_nv(volatile ushort_t *, ushort_t);
extern uint32_t atomic_or_32_nv(volatile uint32_t *, uint32_t);
extern uint_t atomic_or_uint_nv(volatile uint_t *, uint_t);
extern ulong_t atomic_or_ulong_nv(volatile ulong_t *, ulong_t);
#if defined(_INT64_TYPE)
extern uint64_t atomic_or_64_nv(volatile uint64_t *, uint64_t);
#endif

/*
 * logical AND bits with target and return new value.
 */
extern uint8_t atomic_and_8_nv(volatile uint8_t *, uint8_t);
extern uchar_t atomic_and_uchar_nv(volatile uchar_t *, uchar_t);
extern uint16_t atomic_and_16_nv(volatile uint16_t *, uint16_t);
extern ushort_t atomic_and_ushort_nv(volatile ushort_t *, ushort_t);
extern uint32_t atomic_and_32_nv(volatile uint32_t *, uint32_t);
extern uint_t atomic_and_uint_nv(volatile uint_t *, uint_t);
extern ulong_t atomic_and_ulong_nv(volatile ulong_t *, ulong_t);
#if defined(_INT64_TYPE)
extern uint64_t atomic_and_64_nv(volatile uint64_t *, uint64_t);
#endif

/*
 * If *arg1 == arg2, set *arg1 = arg3; return old value
 */
extern uint8_t atomic_cas_8(volatile uint8_t *, uint8_t, uint8_t);
extern uchar_t atomic_cas_uchar(volatile uchar_t *, uchar_t, uchar_t);
extern uint16_t atomic_cas_16(volatile uint16_t *, uint16_t, uint16_t);
extern ushort_t atomic_cas_ushort(volatile ushort_t *, ushort_t, ushort_t);
extern uint32_t atomic_cas_32(volatile uint32_t *, uint32_t, uint32_t);
extern uint_t atomic_cas_uint(volatile uint_t *, uint_t, uint_t);
extern void *atomic_cas_ptr(volatile void *, void *, void *);
extern ulong_t atomic_cas_ulong(volatile ulong_t *, ulong_t, ulong_t);
#if defined(_INT64_TYPE)
extern uint64_t atomic_cas_64(volatile uint64_t *, uint64_t, uint64_t);
#endif

/*
 * Swap target and return old value
 */
extern uint8_t atomic_swap_8(volatile uint8_t *, uint8_t);
extern uchar_t atomic_swap_uchar(volatile uchar_t *, uchar_t);
extern uint16_t atomic_swap_16(volatile uint16_t *, uint16_t);
extern ushort_t atomic_swap_ushort(volatile ushort_t *, ushort_t);
extern uint32_t atomic_swap_32(volatile uint32_t *, uint32_t);
extern uint_t atomic_swap_uint(volatile uint_t *, uint_t);
extern void *atomic_swap_ptr(volatile void *, void *);
extern ulong_t atomic_swap_ulong(volatile ulong_t *, ulong_t);
#if defined(_INT64_TYPE)
extern uint64_t atomic_swap_64(volatile uint64_t *, uint64_t);
#endif

/*
 * Atomically read variable.
 */
#define	atomic_load_char(p)	(*(volatile uchar_t *)(p))
#define	atomic_load_short(p)	(*(volatile ushort_t *)(p))
#define	atomic_load_int(p)	(*(volatile uint_t *)(p))
#define	atomic_load_long(p)	(*(volatile ulong_t *)(p))
#define	atomic_load_ptr(p)	(*(volatile __typeof(*p) *)(p))
#define	atomic_load_8(p)	(*(volatile uint8_t *)(p))
#define	atomic_load_16(p)	(*(volatile uint16_t *)(p))
#define	atomic_load_32(p)	(*(volatile uint32_t *)(p))
#ifdef _LP64
#define	atomic_load_64(p)	(*(volatile uint64_t *)(p))
#elif defined(_INT64_TYPE)
extern uint64_t atomic_load_64(volatile uint64_t *);
#endif

/*
 * Atomically write variable.
 */
#define	atomic_store_char(p, v)		\
	(*(volatile uchar_t *)(p) = (uchar_t)(v))
#define	atomic_store_short(p, v)	\
	(*(volatile ushort_t *)(p) = (ushort_t)(v))
#define	atomic_store_int(p, v)		\
	(*(volatile uint_t *)(p) = (uint_t)(v))
#define	atomic_store_long(p, v)		\
	(*(volatile ulong_t *)(p) = (ulong_t)(v))
#define	atomic_store_ptr(p, v)		\
	(*(volatile __typeof(*p) *)(p) = (v))
#define	atomic_store_8(p, v)		\
	(*(volatile uint8_t *)(p) = (uint8_t)(v))
#define	atomic_store_16(p, v)		\
	(*(volatile uint16_t *)(p) = (uint16_t)(v))
#define	atomic_store_32(p, v)		\
	(*(volatile uint32_t *)(p) = (uint32_t)(v))
#ifdef _LP64
#define	atomic_store_64(p, v)		\
	(*(volatile uint64_t *)(p) = (uint64_t)(v))
#elif defined(_INT64_TYPE)
extern void atomic_store_64(volatile uint64_t *, uint64_t);
#endif

/*
 * Perform an exclusive atomic bit set/clear on a target.
 * Returns 0 if bit was successfully set/cleared, or -1
 * if the bit was already set/cleared.
 */
extern int atomic_set_long_excl(volatile ulong_t *, uint_t);
extern int atomic_clear_long_excl(volatile ulong_t *, uint_t);

/*
 * Generic memory barrier used during lock entry, placed after the
 * memory operation that acquires the lock to guarantee that the lock
 * protects its data.  No stores from after the memory barrier will
 * reach visibility, and no loads from after the barrier will be
 * resolved, before the lock acquisition reaches global visibility.
 */
extern void membar_enter(void);

/*
 * Generic memory barrier used during lock exit, placed before the
 * memory operation that releases the lock to guarantee that the lock
 * protects its data.  All loads and stores issued before the barrier
 * will be resolved before the subsequent lock update reaches visibility.
 */
extern void membar_exit(void);

/*
 * Arrange that all stores issued before this point in the code reach
 * global visibility before any stores that follow; useful in producer
 * modules that update a data item, then set a flag that it is available.
 * The memory barrier guarantees that the available flag is not visible
 * earlier than the updated data, i.e. it imposes store ordering.
 */
extern void membar_producer(void);

/*
 * Arrange that all loads issued before this point in the code are
 * completed before any subsequent loads; useful in consumer modules
 * that check to see if data is available and read the data.
 * The memory barrier guarantees that the data is not sampled until
 * after the available flag has been seen, i.e. it imposes load ordering.
 */
extern void membar_consumer(void);
#endif  /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ATOMIC_H */
