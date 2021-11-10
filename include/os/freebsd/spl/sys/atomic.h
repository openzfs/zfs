/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_ATOMIC_H_
#define	_OPENSOLARIS_SYS_ATOMIC_H_

#ifndef _STANDALONE

#include <sys/types.h>
#include <machine/atomic.h>

#define	atomic_sub_64	atomic_subtract_64

#if defined(__i386__) && (defined(_KERNEL) || defined(KLD_MODULE))
#define	I386_HAVE_ATOMIC64
#endif

#if defined(__i386__) || defined(__amd64__) || defined(__arm__)
/* No spurious failures from fcmpset. */
#define	STRONG_FCMPSET
#endif

#if !defined(__LP64__) && !defined(__mips_n32) && \
	!defined(ARM_HAVE_ATOMIC64) && !defined(I386_HAVE_ATOMIC64) && \
	!defined(HAS_EMULATED_ATOMIC64)
extern void atomic_add_64(volatile uint64_t *target, int64_t delta);
extern void atomic_dec_64(volatile uint64_t *target);
extern uint64_t atomic_swap_64(volatile uint64_t *a, uint64_t value);
extern uint64_t atomic_load_64(volatile uint64_t *a);
extern uint64_t atomic_add_64_nv(volatile uint64_t *target, int64_t delta);
extern uint64_t atomic_cas_64(volatile uint64_t *target, uint64_t cmp,
    uint64_t newval);
#endif

#define	membar_producer	atomic_thread_fence_rel

static __inline uint32_t
atomic_add_32_nv(volatile uint32_t *target, int32_t delta)
{
	return (atomic_fetchadd_32(target, delta) + delta);
}

static __inline uint_t
atomic_add_int_nv(volatile uint_t *target, int delta)
{
	return (atomic_add_32_nv(target, delta));
}

static __inline void
atomic_inc_32(volatile uint32_t *target)
{
	atomic_add_32(target, 1);
}

static __inline uint32_t
atomic_inc_32_nv(volatile uint32_t *target)
{
	return (atomic_add_32_nv(target, 1));
}

static __inline void
atomic_dec_32(volatile uint32_t *target)
{
	atomic_subtract_32(target, 1);
}

static __inline uint32_t
atomic_dec_32_nv(volatile uint32_t *target)
{
	return (atomic_add_32_nv(target, -1));
}

#ifndef __sparc64__
static inline uint32_t
atomic_cas_32(volatile uint32_t *target, uint32_t cmp, uint32_t newval)
{
#ifdef STRONG_FCMPSET
	(void) atomic_fcmpset_32(target, &cmp, newval);
#else
	uint32_t expected = cmp;

	do {
		if (atomic_fcmpset_32(target, &cmp, newval))
			break;
	} while (cmp == expected);
#endif
	return (cmp);
}
#endif

#if defined(__LP64__) || defined(__mips_n32) || \
	defined(ARM_HAVE_ATOMIC64) || defined(I386_HAVE_ATOMIC64) || \
	defined(HAS_EMULATED_ATOMIC64)
static __inline void
atomic_dec_64(volatile uint64_t *target)
{
	atomic_subtract_64(target, 1);
}

static inline uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
	return (atomic_fetchadd_64(target, delta) + delta);
}

#ifndef __sparc64__
static inline uint64_t
atomic_cas_64(volatile uint64_t *target, uint64_t cmp, uint64_t newval)
{
#ifdef STRONG_FCMPSET
	(void) atomic_fcmpset_64(target, &cmp, newval);
#else
	uint64_t expected = cmp;

	do {
		if (atomic_fcmpset_64(target, &cmp, newval))
			break;
	} while (cmp == expected);
#endif
	return (cmp);
}
#endif
#endif

static __inline void
atomic_inc_64(volatile uint64_t *target)
{
	atomic_add_64(target, 1);
}

static __inline uint64_t
atomic_inc_64_nv(volatile uint64_t *target)
{
	return (atomic_add_64_nv(target, 1));
}

static __inline uint64_t
atomic_dec_64_nv(volatile uint64_t *target)
{
	return (atomic_add_64_nv(target, -1));
}

#if !defined(COMPAT_32BIT) && defined(__LP64__)
static __inline void *
atomic_cas_ptr(volatile void *target, void *cmp,  void *newval)
{
	return ((void *)atomic_cas_64((volatile uint64_t *)target,
	    (uint64_t)cmp, (uint64_t)newval));
}
#else
static __inline void *
atomic_cas_ptr(volatile void *target, void *cmp,  void *newval)
{
	return ((void *)atomic_cas_32((volatile uint32_t *)target,
	    (uint32_t)cmp, (uint32_t)newval));
}
#endif	/* !defined(COMPAT_32BIT) && defined(__LP64__) */

#else /* _STANDALONE */
/*
 * sometimes atomic_add_64 is defined, sometimes not, but the
 * following is always right for the boot loader.
 */
#undef atomic_add_64
#define	atomic_add_64(ptr, val) *(ptr) += val
#endif /* !_STANDALONE */

#endif	/* !_OPENSOLARIS_SYS_ATOMIC_H_ */
