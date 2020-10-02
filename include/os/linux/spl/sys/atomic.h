/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_ATOMIC_H
#define	_SPL_ATOMIC_H

#include <linux/module.h>
#include <linux/spinlock.h>
#include <sys/types.h>

/*
 * Map the atomic_* functions to the Linux counterparts.  This relies on the
 * fact that the atomic types are internally really a uint32 or uint64.  If
 * this were to change an alternate approach would be needed.
 *
 * N.B. Due to the limitations of the original API atomicity is not strictly
 * preserved when using the 64-bit functions on a 32-bit system.  In order
 * to support this all consumers would need to be updated to use the Linux
 * provided atomic_t and atomic64_t types.
 */
#define	atomic_inc_32(v)	atomic_inc((atomic_t *)(v))
#define	atomic_dec_32(v)	atomic_dec((atomic_t *)(v))
#define	atomic_add_32(v, i)	atomic_add((i), (atomic_t *)(v))
#define	atomic_sub_32(v, i)	atomic_sub((i), (atomic_t *)(v))
#define	atomic_inc_32_nv(v)	atomic_inc_return((atomic_t *)(v))
#define	atomic_dec_32_nv(v)	atomic_dec_return((atomic_t *)(v))
#define	atomic_add_32_nv(v, i)	atomic_add_return((i), (atomic_t *)(v))
#define	atomic_sub_32_nv(v, i)	atomic_sub_return((i), (atomic_t *)(v))
#define	atomic_cas_32(v, x, y)	atomic_cmpxchg((atomic_t *)(v), x, y)
#define	atomic_swap_32(v, x)	atomic_xchg((atomic_t *)(v), x)
#define	atomic_inc_64(v)	atomic64_inc((atomic64_t *)(v))
#define	atomic_dec_64(v)	atomic64_dec((atomic64_t *)(v))
#define	atomic_add_64(v, i)	atomic64_add((i), (atomic64_t *)(v))
#define	atomic_sub_64(v, i)	atomic64_sub((i), (atomic64_t *)(v))
#define	atomic_inc_64_nv(v)	atomic64_inc_return((atomic64_t *)(v))
#define	atomic_dec_64_nv(v)	atomic64_dec_return((atomic64_t *)(v))
#define	atomic_add_64_nv(v, i)	atomic64_add_return((i), (atomic64_t *)(v))
#define	atomic_sub_64_nv(v, i)	atomic64_sub_return((i), (atomic64_t *)(v))
#define	atomic_cas_64(v, x, y)	atomic64_cmpxchg((atomic64_t *)(v), x, y)
#define	atomic_swap_64(v, x)	atomic64_xchg((atomic64_t *)(v), x)

#ifdef _LP64
static __inline__ void *
atomic_cas_ptr(volatile void *target,  void *cmp, void *newval)
{
	return ((void *)atomic_cas_64((volatile uint64_t *)target,
	    (uint64_t)cmp, (uint64_t)newval));
}
#else /* _LP64 */
static __inline__ void *
atomic_cas_ptr(volatile void *target,  void *cmp, void *newval)
{
	return ((void *)atomic_cas_32((volatile uint32_t *)target,
	    (uint32_t)cmp, (uint32_t)newval));
}
#endif /* _LP64 */

#endif  /* _SPL_ATOMIC_H */
