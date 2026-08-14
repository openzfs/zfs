// SPDX-License-Identifier: BSD-2-Clause
/*
 * 64-bit atomics for OpenZFS ILP32 targets that lack
 * __HAVE_ATOMIC64_OPS (powerpc32, mips o32, sparc v8, riscv32,
 * m68k, hppa, sh3, vax).  i386 and ARMv6+ EABI already export
 * kernel atomic_*_64.  64-bit load/store on every ILP32 kernel
 * go through atomic_cas_64 / atomic_swap_64 (see sys/atomic.h)
 * so gcc never emits __atomic_load_8 / __atomic_store_8.
 *
 * Emulation is not a mutex.  Interrupts go off first (splhigh),
 * then a raw __cpu_simple_lock.  An interrupt handler that
 * touches a 64-bit atomic cannot run on this CPU, so it cannot
 * deadlock on the same lock.  Other CPUs spin.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/intr.h>
#include <sys/cpu.h>
#include <machine/lock.h>

void	spl_atomic64_init(void);
void	spl_atomic64_fini(void);

#ifdef __HAVE_ATOMIC64_OPS

void
spl_atomic64_init(void)
{
}

void
spl_atomic64_fini(void)
{
}

#else /* !__HAVE_ATOMIC64_OPS */

static __cpu_simple_lock_t atomic64_slock;

static inline int
spl_atomic64_enter(void)
{
	int s;

	s = splhigh();
	__cpu_simple_lock(&atomic64_slock);
	return (s);
}

static inline void
spl_atomic64_exit(int s)
{
	__cpu_simple_unlock(&atomic64_slock);
	splx(s);
}

void
spl_atomic64_init(void)
{
	__cpu_simple_lock_init(&atomic64_slock);
}

void
spl_atomic64_fini(void)
{
}

void
atomic_add_64(volatile uint64_t *target, int64_t delta)
{
	int s = spl_atomic64_enter();

	*target += (uint64_t)delta;
	spl_atomic64_exit(s);
}

void
atomic_dec_64(volatile uint64_t *target)
{
	int s = spl_atomic64_enter();

	*target -= 1;
	spl_atomic64_exit(s);
}

void
atomic_inc_64(volatile uint64_t *target)
{
	int s = spl_atomic64_enter();

	*target += 1;
	spl_atomic64_exit(s);
}

uint64_t
atomic_inc_64_nv(volatile uint64_t *target)
{
	uint64_t newval;
	int s = spl_atomic64_enter();

	newval = ++(*target);
	spl_atomic64_exit(s);
	return (newval);
}

uint64_t
atomic_dec_64_nv(volatile uint64_t *target)
{
	uint64_t newval;
	int s = spl_atomic64_enter();

	newval = --(*target);
	spl_atomic64_exit(s);
	return (newval);
}

void
atomic_or_64(volatile uint64_t *target, uint64_t bits)
{
	int s = spl_atomic64_enter();

	*target |= bits;
	spl_atomic64_exit(s);
}

uint64_t
atomic_or_64_nv(volatile uint64_t *target, uint64_t bits)
{
	uint64_t newval;
	int s = spl_atomic64_enter();

	newval = (*target |= bits);
	spl_atomic64_exit(s);
	return (newval);
}

void
atomic_and_64(volatile uint64_t *target, uint64_t bits)
{
	int s = spl_atomic64_enter();

	*target &= bits;
	spl_atomic64_exit(s);
}

uint64_t
atomic_and_64_nv(volatile uint64_t *target, uint64_t bits)
{
	uint64_t newval;
	int s = spl_atomic64_enter();

	newval = (*target &= bits);
	spl_atomic64_exit(s);
	return (newval);
}

uint64_t
atomic_swap_64(volatile uint64_t *a, uint64_t value)
{
	uint64_t ret;
	int s = spl_atomic64_enter();

	ret = *a;
	*a = value;
	spl_atomic64_exit(s);
	return (ret);
}

uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
	uint64_t newval;
	int s = spl_atomic64_enter();

	newval = (*target += (uint64_t)delta);
	spl_atomic64_exit(s);
	return (newval);
}

uint64_t
atomic_cas_64(volatile uint64_t *target, uint64_t cmp, uint64_t newval)
{
	uint64_t oldval;
	int s = spl_atomic64_enter();

	oldval = *target;
	if (oldval == cmp)
		*target = newval;
	spl_atomic64_exit(s);
	return (oldval);
}

#ifndef atomic_load_64
uint64_t
atomic_load_64(volatile uint64_t *target)
{
	return (atomic_cas_64(target, 0, 0));
}

void
atomic_store_64(volatile uint64_t *target, uint64_t value)
{
	(void) atomic_swap_64(target, value);
}
#endif /* atomic_load_64 */
#endif /* !__HAVE_ATOMIC64_OPS */
