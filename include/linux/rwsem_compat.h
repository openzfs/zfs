/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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

#ifdef RWSEM_IS_LOCKED_TAKES_WAIT_LOCK
/*
 * A race condition in rwsem_is_locked() was fixed in Linux 2.6.33 and the fix
 * was backported to RHEL5 as of kernel 2.6.18-190.el5.  Details can be found
 * here:
 *
 * https://bugzilla.redhat.com/show_bug.cgi?id=526092

 * The race condition was fixed in the kernel by acquiring the semaphore's
 * wait_lock inside rwsem_is_locked().  The SPL worked around the race
 * condition by acquiring the wait_lock before calling that function, but
 * with the fix in place we must not do that.
 */

#define spl_rwsem_is_locked(rwsem)					\
({									\
	rwsem_is_locked(rwsem);						\
})

#else

#define spl_rwsem_is_locked(rwsem)					\
({									\
	unsigned long _flags_;						\
	int _rc_;							\
	spin_lock_irqsave(&rwsem->wait_lock, _flags_);			\
	_rc_ = rwsem_is_locked(rwsem);					\
	spin_unlock_irqrestore(&rwsem->wait_lock, _flags_);		\
	_rc_;								\
})

#endif /* RWSEM_IS_LOCKED_TAKES_WAIT_LOCK */

#endif /* _SPL_RWSEM_COMPAT_H */
