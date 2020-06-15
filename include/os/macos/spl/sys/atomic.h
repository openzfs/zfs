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
#include <libkern/OSAtomic.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Increment target
 */
static inline void
atomic_inc_8(volatile uint8_t *target)
{
	__sync_fetch_and_add(target, 1);
}

static inline void
atomic_inc_16(volatile uint16_t *target)
{
	__sync_fetch_and_add(target, 1);
}

static inline void
atomic_inc_32(volatile uint32_t *target)
{
	__sync_fetch_and_add(target, 1);
}

static inline void
atomic_inc_64(volatile uint64_t *target)
{
	__sync_fetch_and_add(target, 1);
}

static inline int32_t
atomic_inc_32_nv(volatile uint32_t *target)
{
	return (__sync_add_and_fetch(target, 1));
}

static inline int64_t
atomic_inc_64_nv(volatile uint64_t *target)
{
	return (__sync_add_and_fetch(target, 1));
}



/*
 * Decrement target
 */
static inline void
atomic_dec_8(volatile uint8_t *target)
{
	__sync_fetch_and_sub(target, 1);
}

static inline void
atomic_dec_16(volatile uint16_t *target)
{
	__sync_fetch_and_sub(target, 1);
}

static inline void
atomic_dec_32(volatile uint32_t *target)
{
	__sync_fetch_and_sub(target, 1);
}

static inline void
atomic_dec_64(volatile uint64_t *target)
{
	__sync_fetch_and_sub(target, 1);
}

static inline int32_t
atomic_dec_32_nv(volatile uint32_t *target)
{
	return (__sync_sub_and_fetch(target, 1));
}

static inline int64_t
atomic_dec_64_nv(volatile uint64_t *target)
{
	return (__sync_sub_and_fetch(target, 1));
}

/*
 * Add delta to target
 */
static inline void
atomic_add_8(volatile uint8_t *target, int8_t delta)
{
	__sync_add_and_fetch(target, delta);
}

static inline void
atomic_add_16(volatile uint16_t *target, int16_t delta)
{
	__sync_add_and_fetch(target, delta);
}

static inline void
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	__sync_add_and_fetch(target, delta);
}

static inline uint32_t
atomic_add_32_nv(volatile uint32_t *target, int32_t delta)
{
	return (__sync_add_and_fetch(target, delta));
}

static inline void
atomic_add_64(volatile uint64_t *target, int64_t delta)
{
	__sync_add_and_fetch(target, delta);
}

static inline uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
	return (__sync_add_and_fetch(target, delta));
}


/*
 * Subtract delta to target
 */
static inline void
atomic_sub_8(volatile uint8_t *target, int8_t delta)
{
	__sync_sub_and_fetch(target, delta);
}

static inline void
atomic_sub_16(volatile uint16_t *target, int16_t delta)
{
	__sync_sub_and_fetch(target, delta);
}

static inline void
atomic_sub_32(volatile uint32_t *target, int32_t delta)
{
	__sync_sub_and_fetch(target, delta);
}

static inline void
atomic_sub_64(volatile uint64_t *target, int64_t delta)
{
	__sync_sub_and_fetch(target, delta);
}

static inline uint64_t
atomic_sub_64_nv(volatile uint64_t *target, int64_t delta)
{
	return (__sync_sub_and_fetch(target, delta));
}

/*
 * logical OR bits with target
 */
static inline void
atomic_or_8(volatile uint8_t *target, uint8_t mask)
{
	__sync_or_and_fetch(target, mask);
}

static inline void
atomic_or_16(volatile uint16_t *target, uint16_t mask)
{
	__sync_or_and_fetch(target, mask);
}

static inline void
atomic_or_32(volatile uint32_t *target, uint32_t mask)
{
	__sync_or_and_fetch(target, mask);
}

/*
 * logical AND bits with target
 */
static inline void
atomic_and_8(volatile uint8_t *target, uint8_t mask)
{
	__sync_and_and_fetch(target, mask);
}

static inline void
atomic_and_16(volatile uint16_t *target, uint16_t mask)
{
	__sync_and_and_fetch(target, mask);
}

static inline void
atomic_and_32(volatile uint32_t *target, uint32_t mask)
{
	__sync_and_and_fetch(target, mask);
}

/*
 * Compare And Set
 * if *arg1 == arg2, then set *arg1 = arg3; return old value.
 */
static inline uint8_t
atomic_cas_8(volatile uint8_t *_target, uint8_t _cmp, uint8_t _new)
{
	return (__sync_val_compare_and_swap(_target, _cmp, _new));
}

static inline uint16_t
atomic_cas_16(volatile uint16_t *_target, uint16_t _cmp, uint16_t _new)
{
	return (__sync_val_compare_and_swap(_target, _cmp, _new));
}

static inline uint32_t
atomic_cas_32(volatile uint32_t *_target, uint32_t _cmp, uint32_t _new)
{
	return (__sync_val_compare_and_swap(_target, _cmp, _new));
}

static inline uint64_t
atomic_cas_64(volatile uint64_t *_target, uint64_t _cmp, uint64_t _new)
{
	return (__sync_val_compare_and_swap(_target, _cmp, _new));
}

static inline uint32_t
atomic_swap_32(volatile uint32_t *_target, uint32_t _new)
{
	return (__sync_lock_test_and_set(_target, _new));
}

static inline uint64_t
atomic_swap_64(volatile uint64_t *_target, uint64_t _new)
{
	return (__sync_lock_test_and_set(_target, _new));
}

extern void *atomic_cas_ptr(volatile void *_target, void *_cmp, void *_new);

static inline void
membar_producer(void)
{
	__c11_atomic_thread_fence(__ATOMIC_SEQ_CST);
}

#ifdef	__cplusplus
}
#endif

#endif  /* _SPL_ATOMIC_H */
