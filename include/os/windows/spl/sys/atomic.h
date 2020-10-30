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
 * OSX Atomic functions using GCC builtins.
 *
 * Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_ATOMIC_H
#define _SPL_ATOMIC_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 *
 * GCC atomic versions. These are preferrable once we sort out compatibility
 * issues with GCC versions?
 */

/* The _nv variants return the NewValue */

/*
 * Increment target
 */
static inline void atomic_inc_32(volatile uint32_t *target)
{
	InterlockedIncrement((volatile LONG *)target);
}
static inline void atomic_inc_64(volatile uint64_t *target)
{
	InterlockedIncrement64((volatile LONG64 *)target);
}
static inline int32_t atomic_inc_32_nv(volatile uint32_t *target)
{
    return InterlockedIncrement((volatile LONG *)target);
}
static inline int64_t atomic_inc_64_nv(volatile uint64_t *target)
{
	return InterlockedIncrement64((volatile LONG64 *)target);
}



/*
 * Decrement target
 */
static inline void atomic_dec_32(volatile uint32_t *target)
{
	InterlockedDecrement((volatile LONG *)target);
}
static inline void atomic_dec_64(volatile uint64_t *target)
{
	InterlockedDecrement64((volatile LONG64 *)target);
}
static inline int32_t atomic_dec_32_nv(volatile uint32_t *target)
{
	return InterlockedDecrement((volatile LONG *)target);
}
static inline int64_t atomic_dec_64_nv(volatile uint64_t *target)
{
	return InterlockedDecrement64((volatile LONG64 *)target);
}




/*
 * Add delta to target
 */
static inline void
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	InterlockedExchangeAdd((volatile LONG *)target, delta);
}
static inline uint32_t
atomic_add_32_nv(volatile uint32_t *target, int32_t delta)
{
    return InterlockedExchangeAdd((volatile LONG *)target, delta) + delta;
}
static inline void
atomic_add_64(volatile uint64_t *target, int64_t delta)
{
	InterlockedExchangeAdd64((volatile LONG64 *)target, delta);
}
static inline uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
	return InterlockedExchangeAdd64((volatile LONG64 *)target, delta) + delta;
}


/*
 * Subtract delta to target
 */
static inline void
atomic_sub_32(volatile uint32_t *target, int32_t delta)
{
	InterlockedExchangeAdd((volatile LONG *)target, -delta);
}
static inline void
atomic_sub_64(volatile uint64_t *target, int64_t delta)
{
	InterlockedExchangeAdd64((volatile LONG64 *)target, -delta);
}
static inline uint64_t
atomic_sub_64_nv(volatile uint64_t *target, int64_t delta)
{
	return InterlockedExchangeAdd64((volatile LONG64 *)target, -delta) - delta;
}


/*
 * logical OR bits with target
 */

/*
 * logical AND bits with target
 */


/*
 * Compare And Set
 * if *arg1 == arg2, then set *arg1 = arg3; return old value.
 */

static inline uint32_t
atomic_cas_32(volatile uint32_t *_target, uint32_t _cmp, uint32_t _new)
{
	return InterlockedCompareExchange((volatile LONG *)_target, _new, _cmp);
}
static inline uint64_t
atomic_cas_64(volatile uint64_t *_target, uint64_t _cmp, uint64_t _new)
{
	return InterlockedCompareExchange64((volatile LONG64 *)_target, _new, _cmp);
}

static inline uint32_t
atomic_swap_32(volatile uint32_t *_target, uint32_t _new)
{
    return InterlockedExchange((volatile LONG *)_target, _new);
}

static inline uint64_t
atomic_swap_64(volatile uint64_t *_target, uint64_t _new)
{
    return InterlockedExchange64((volatile LONG64 *)_target, _new);
}

extern void *atomic_cas_ptr(volatile void *_target, void *_cmp, void *_new);

static inline void membar_producer(void) { _mm_mfence(); }

#ifdef	__cplusplus
}
#endif

#endif  /* _SPL_ATOMIC_H */
