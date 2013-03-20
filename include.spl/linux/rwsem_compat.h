/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
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
\*****************************************************************************/

#ifndef _SPL_RWSEM_COMPAT_H
#define _SPL_RWSEM_COMPAT_H

#include <linux/rwsem.h>

#if defined(RWSEM_SPINLOCK_IS_RAW)
#define spl_rwsem_lock_irqsave(lk, fl)       raw_spin_lock_irqsave(lk, fl)
#define spl_rwsem_unlock_irqrestore(lk, fl)  raw_spin_unlock_irqrestore(lk, fl)
#define spl_rwsem_trylock_irqsave(lk, fl)    raw_spin_trylock_irqsave(lk, fl)
#else
#define spl_rwsem_lock_irqsave(lk, fl)       spin_lock_irqsave(lk, fl)
#define spl_rwsem_unlock_irqrestore(lk, fl)  spin_unlock_irqrestore(lk, fl)
#define spl_rwsem_trylock_irqsave(lk, fl)    spin_trylock_irqsave(lk, fl)
#endif /* RWSEM_SPINLOCK_IS_RAW */

/*
 * Prior to Linux 2.6.33 there existed a race condition in rwsem_is_locked().
 * The semaphore's activity was checked outside of the wait_lock which
 * could result in some readers getting the incorrect activity value.
 *
 * When a kernel without this fix is detected the SPL takes responsibility
 * for acquiring the wait_lock to avoid this race.
 */
#if defined(RWSEM_IS_LOCKED_TAKES_WAIT_LOCK)
#define spl_rwsem_is_locked(rwsem)           rwsem_is_locked(rwsem)
#else
static inline int
spl_rwsem_is_locked(struct rw_semaphore *rwsem)
{
	unsigned long flags;
	int rc = 1;

	if (spl_rwsem_trylock_irqsave(&rwsem->wait_lock, flags)) {
		rc = rwsem_is_locked(rwsem);
		spl_rwsem_unlock_irqrestore(&rwsem->wait_lock, flags);
	}

	return (rc);
}
#endif /* RWSEM_IS_LOCKED_TAKES_WAIT_LOCK */

#endif /* _SPL_RWSEM_COMPAT_H */
