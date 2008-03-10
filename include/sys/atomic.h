#ifndef _SPL_ATOMIC_H
#define _SPL_ATOMIC_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
/* FIXME - NONE OF THIS IS ATOMIC, IT SHOULD BE.  I think we can
 * get by for now since I'm only working on real 64bit systems but
 * this will need to be addressed properly.
 */

static __inline__ void
atomic_inc_64(volatile uint64_t *target)
{
	(*target)++;
}

static __inline__ void
atomic_dec_64(volatile uint64_t *target)
{
	(*target)--;
}

static __inline__ uint32_t
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	uint32_t rc = *target;
	*target += delta;
	return rc;
}

static __inline__ uint64_t
atomic_add_64(volatile uint64_t *target, uint64_t delta)
{
	uint64_t rc = *target;
	*target += delta;
	return rc;
}

static __inline__ uint64_t
atomic_add_64_nv(volatile uint64_t *target, uint64_t delta)
{
	*target += delta;
	return *target;
}

static __inline__ uint64_t
atomic_cas_64(volatile uint64_t  *target,  uint64_t cmp,
               uint64_t newval)
{
	uint64_t rc = *target;

	if (*target == cmp)
		*target = newval;

	return rc;
}

static __inline__ void *
atomic_cas_ptr(volatile void  *target,  void *cmp, void *newval)
{
	void *rc = (void *)target;

	if (target == cmp)
		target = newval;

	return rc;
}

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_ATOMIC_H */

