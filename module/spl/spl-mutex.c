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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Mutex Implementation.
\*****************************************************************************/

#include <sys/mutex.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_MUTEX

/*
 * While a standard mutex implementation has been available in the kernel
 * for quite some time.  It was not until 2.6.29 and latter kernels that
 * adaptive mutexs were embraced and integrated with the scheduler.  This
 * brought a significant performance improvement, but just as importantly
 * it added a lock owner to the generic mutex outside CONFIG_DEBUG_MUTEXES
 * builds.  This is critical for correctly supporting the mutex_owner()
 * Solaris primitive.  When the owner is available we use a pure Linux
 * mutex implementation.  When the owner is not available we still use
 * Linux mutexs as a base but also reserve space for an owner field right
 * after the mutex structure.
 *
 * In the case when HAVE_MUTEX_OWNER is not defined your code may
 * still me able to leverage adaptive mutexs.  As long as the task_curr()
 * symbol is exported this code will provide a poor mans adaptive mutex
 * implementation.  However, this is not required and if the symbol is
 * unavailable we provide a standard mutex.
 */

#if !defined(HAVE_MUTEX_OWNER) || !defined(CONFIG_SMP) || defined(CONFIG_DEBUG_MUTEXES)
#ifdef HAVE_TASK_CURR
/*
 * mutex_spin_max = { 0, -1, 1-MAX_INT }
 *  0:         Never spin when trying to acquire lock
 * -1:         Spin until acquired or holder yields without dropping lock
 *  1-MAX_INT: Spin for N attempts before sleeping for lock
 */
int mutex_spin_max = 0;
module_param(mutex_spin_max, int, 0644);
MODULE_PARM_DESC(mutex_spin_max, "Spin a maximum of N times to acquire lock");

int
spl_mutex_spin_max(void)
{
        return mutex_spin_max;
}
EXPORT_SYMBOL(spl_mutex_spin_max);

#endif /* HAVE_TASK_CURR */
#endif /* !HAVE_MUTEX_OWNER */

int spl_mutex_init(void) { return 0; }
void spl_mutex_fini(void) { }
