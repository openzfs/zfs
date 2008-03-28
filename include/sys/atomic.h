#ifndef _SPL_ATOMIC_H
#define _SPL_ATOMIC_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/spinlock.h>

/* XXX: Serialize everything through global locks.  This is
 * going to be bad for performance, but for now it's the easiest
 * way to ensure correct behavior.  I don't like it at all.
 * It would be nicer to make these function to the atomic linux
 * functions, but the normal uint64_t type complicates this.
 */
extern spinlock_t atomic64_lock;
extern spinlock_t atomic32_lock;
extern spinlock_t atomic_lock;

static __inline__ uint32_t
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	uint32_t rc;

	spin_lock(&atomic32_lock);
	rc = *target;
	*target += delta;
	spin_unlock(&atomic32_lock);

	return rc;
}

static __inline__ void
atomic_inc_64(volatile uint64_t *target)
{
	spin_lock(&atomic64_lock);
	(*target)++;
	spin_unlock(&atomic64_lock);
}

static __inline__ void
atomic_dec_64(volatile uint64_t *target)
{
	spin_lock(&atomic64_lock);
	(*target)--;
	spin_unlock(&atomic64_lock);
}

static __inline__ uint64_t
atomic_add_64(volatile uint64_t *target, uint64_t delta)
{
	uint64_t rc;

	spin_lock(&atomic64_lock);
	rc = *target;
	*target += delta;
	spin_unlock(&atomic64_lock);

	return rc;
}

static __inline__ uint64_t
atomic_sub_64(volatile uint64_t *target, uint64_t delta)
{
	uint64_t rc;

	spin_lock(&atomic64_lock);
	rc = *target;
	*target -= delta;
	spin_unlock(&atomic64_lock);

	return rc;
}

static __inline__ uint64_t
atomic_add_64_nv(volatile uint64_t *target, uint64_t delta)
{
	spin_lock(&atomic64_lock);
	*target += delta;
	spin_unlock(&atomic64_lock);

	return *target;
}

static __inline__ uint64_t
atomic_sub_64_nv(volatile uint64_t *target, uint64_t delta)
{
	spin_lock(&atomic64_lock);
	*target -= delta;
	spin_unlock(&atomic64_lock);

	return *target;
}

static __inline__ uint64_t
atomic_cas_64(volatile uint64_t  *target,  uint64_t cmp,
               uint64_t newval)
{
	uint64_t rc;

	spin_lock(&atomic64_lock);
	rc = *target;
	if (*target == cmp)
		*target = newval;
	spin_unlock(&atomic64_lock);

	return rc;
}

static __inline__ void *
atomic_cas_ptr(volatile void  *target,  void *cmp, void *newval)
{
	void *rc;

	spin_lock(&atomic_lock);
	rc = (void *)target;
	if (target == cmp)
		target = newval;
	spin_unlock(&atomic_lock);

	return rc;
}

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_ATOMIC_H */

