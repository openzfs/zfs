// SPDX-License-Identifier: BSD-2-Clause
#ifndef _OPENSOLARIS_SYS_ATOMIC_H_
#define	_OPENSOLARIS_SYS_ATOMIC_H_

#ifndef _STANDALONE

#include_next <sys/atomic.h>

#ifndef atomic_sub_64
/*
 * Two's-complement subtract via add.  `-(int64_t)v` is UB on
 * INT64_MIN and implementation-defined for v > INT64_MAX.
 */
#define	atomic_sub_64(p, v) \
	atomic_add_64((p), (int64_t)(~(uint64_t)(v) + 1ULL))
#endif

#ifndef atomic_load_64
#if defined(_KERNEL) && !defined(_LP64)
/*
 * gcc turns 64-bit __atomic_load_n / __atomic_store_n into
 * __atomic_load_8 / __atomic_store_8.  The NetBSD kernel does not
 * provide those helpers on any ILP32 port (i386, arm, powerpc,
 * mips, sparc, riscv32, ...).  Use the native 64-bit CAS/SWAP,
 * which i386 and ARMv6+ export and which spl_atomic.c emulates
 * everywhere else.
 */
#define	atomic_load_64(p) \
	atomic_cas_64((volatile uint64_t *)(p), 0, 0)
#define	atomic_store_64(p, v) \
	((void)atomic_swap_64((volatile uint64_t *)(p), (uint64_t)(v)))
#else
#define	atomic_load_64(p) \
	__atomic_load_n((const volatile uint64_t *)(p), __ATOMIC_SEQ_CST)
#define	atomic_store_64(p, v) \
	__atomic_store_n((volatile uint64_t *)(p), (uint64_t)(v), \
	    __ATOMIC_SEQ_CST)
#endif
#endif
/*
 * NetBSD 11 declares the 64-bit ops unconditionally, but
 * __HAVE_ATOMIC64_OPS is what provides the bodies.  Repeat the
 * native signatures so a header that omits them still compiles.
 * add delta is int64_t (same as NetBSD).
 *
 * ILP32 without the hardware ops (powerpc32, mips o32, sparc v8,
 * riscv32): bodies are in spl_atomic.c -- splhigh then a
 * __cpu_simple_lock so an interrupt handler cannot deadlock on
 * the same lock.  i386 and ARMv6+ already export kernel
 * atomic_*_64; do not replace those.
 */
#if defined(_KERNEL) && !defined(__HAVE_ATOMIC64_OPS)
void	atomic_add_64(volatile uint64_t *, int64_t);
uint64_t atomic_add_64_nv(volatile uint64_t *, int64_t);
void	atomic_inc_64(volatile uint64_t *);
uint64_t atomic_inc_64_nv(volatile uint64_t *);
void	atomic_dec_64(volatile uint64_t *);
uint64_t atomic_dec_64_nv(volatile uint64_t *);
void	atomic_or_64(volatile uint64_t *, uint64_t);
uint64_t atomic_or_64_nv(volatile uint64_t *, uint64_t);
void	atomic_and_64(volatile uint64_t *, uint64_t);
uint64_t atomic_and_64_nv(volatile uint64_t *, uint64_t);
uint64_t atomic_swap_64(volatile uint64_t *, uint64_t);
uint64_t atomic_cas_64(volatile uint64_t *, uint64_t, uint64_t);
#endif

#else /* _STANDALONE */
#undef atomic_add_64
#define	atomic_add_64(ptr, val)	*(ptr) += val
#undef atomic_sub_64
#define	atomic_sub_64(ptr, val)	*(ptr) -= val
#endif

#endif /* _OPENSOLARIS_SYS_ATOMIC_H_ */
