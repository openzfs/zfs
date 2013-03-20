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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Reader/Writer Lock Implementation.
\*****************************************************************************/

#include <sys/rwlock.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_RWLOCK

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK

/*
 * From lib/rwsem-spinlock.c but modified such that the caller is
 * responsible for acquiring and dropping the sem->wait_lock.
 */
struct rwsem_waiter {
        struct list_head list;
        struct task_struct *task;
        unsigned int flags;
#define RWSEM_WAITING_FOR_READ  0x00000001
#define RWSEM_WAITING_FOR_WRITE 0x00000002
};

/* wake a single writer */
static struct rw_semaphore *
__rwsem_wake_one_writer_locked(struct rw_semaphore *sem)
{
        struct rwsem_waiter *waiter;
        struct task_struct *tsk;

        sem->activity = -1;

        waiter = list_entry(sem->wait_list.next, struct rwsem_waiter, list);
        list_del(&waiter->list);

        tsk = waiter->task;
        smp_mb();
        waiter->task = NULL;
        wake_up_process(tsk);
        put_task_struct(tsk);
        return sem;
}

/* release a read lock on the semaphore */
void
__up_read_locked(struct rw_semaphore *sem)
{
        if (--sem->activity == 0 && !list_empty(&sem->wait_list))
                (void)__rwsem_wake_one_writer_locked(sem);
}
EXPORT_SYMBOL(__up_read_locked);

/* trylock for writing -- returns 1 if successful, 0 if contention */
int
__down_write_trylock_locked(struct rw_semaphore *sem)
{
        int ret = 0;

        if (sem->activity == 0 && list_empty(&sem->wait_list)) {
                sem->activity = -1;
                ret = 1;
        }

        return ret;
}
EXPORT_SYMBOL(__down_write_trylock_locked);

#endif

int spl_rw_init(void) { return 0; }
void spl_rw_fini(void) { }
