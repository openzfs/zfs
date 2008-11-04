/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/rwlock.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_RWLOCK

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
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
static void
__up_read_locked(struct rw_semaphore *sem)
{
	if (--sem->activity == 0 && !list_empty(&sem->wait_list))
		(void)__rwsem_wake_one_writer_locked(sem);
}

/* trylock for writing -- returns 1 if successful, 0 if contention */
static int
__down_write_trylock_locked(struct rw_semaphore *sem)
{
	int ret = 0;

	if (sem->activity == 0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		ret = 1;
	}

	return ret;
}
#endif

void
__rw_init(krwlock_t *rwlp, char *name, krw_type_t type, void *arg)
{
	int flags = KM_SLEEP;

        ASSERT(rwlp);
        ASSERT(name);
	ASSERT(type == RW_DEFAULT);	/* XXX no irq handler use */
	ASSERT(arg == NULL);		/* XXX no irq handler use */

	rwlp->rw_magic = RW_MAGIC;
	rwlp->rw_owner = NULL;
	rwlp->rw_name = NULL;
        rwlp->rw_name_size = strlen(name) + 1;

        /* We may be called when there is a non-zero preempt_count or
         * interrupts are disabled is which case we must not sleep.
         */
        if (current_thread_info()->preempt_count || irqs_disabled())
                flags = KM_NOSLEEP;

        rwlp->rw_name = kmem_alloc(rwlp->rw_name_size, flags);
        if (rwlp->rw_name == NULL)
                return;

	init_rwsem(&rwlp->rw_sem);
        strcpy(rwlp->rw_name, name);
}
EXPORT_SYMBOL(__rw_init);

void
__rw_destroy(krwlock_t *rwlp)
{
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	ASSERT(rwlp->rw_owner == NULL);
	spin_lock(&rwlp->rw_sem.wait_lock);
	ASSERT(list_empty(&rwlp->rw_sem.wait_list));
	spin_unlock(&rwlp->rw_sem.wait_lock);

        kmem_free(rwlp->rw_name, rwlp->rw_name_size);

	memset(rwlp, RW_POISON, sizeof(krwlock_t));
}
EXPORT_SYMBOL(__rw_destroy);

/* Return 0 if the lock could not be obtained without blocking. */
int
__rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int rc = 0;
	ENTRY;

	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	switch (rw) {
		/* these functions return 1 if success, 0 if contention */
		case RW_READER:
			/* Here the Solaris code would return 0
			 * if there were any write waiters.  Specifically
			 * thinking about the case where readers may have
			 * the lock and we would also allow this thread
			 * to grab the read lock with a writer waiting in the
			 * queue. This doesn't seem like a correctness
			 * issue, so just call down_read_trylock()
			 * for the test.  We may have to revisit this if
			 * it becomes an issue */
			rc = down_read_trylock(&rwlp->rw_sem);
			break;
		case RW_WRITER:
			rc = down_write_trylock(&rwlp->rw_sem);
			if (rc) {
				/* there better not be anyone else
				 * holding the write lock here */
				ASSERT(rwlp->rw_owner == NULL);
				rwlp->rw_owner = current;
			}
			break;
		default:
			SBUG();
	}

	RETURN(rc);
}
EXPORT_SYMBOL(__rw_tryenter);

void
__rw_enter(krwlock_t *rwlp, krw_t rw)
{
	ENTRY;
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	switch (rw) {
		case RW_READER:
			/* Here the Solaris code would block
			 * if there were any write waiters.  Specifically
			 * thinking about the case where readers may have
			 * the lock and we would also allow this thread
			 * to grab the read lock with a writer waiting in the
			 * queue. This doesn't seem like a correctness
			 * issue, so just call down_read()
			 * for the test.  We may have to revisit this if
			 * it becomes an issue */
			down_read(&rwlp->rw_sem);
			break;
		case RW_WRITER:
			down_write(&rwlp->rw_sem);

			/* there better not be anyone else
			 * holding the write lock here */
			ASSERT(rwlp->rw_owner == NULL);
			rwlp->rw_owner = current;
			break;
		default:
			SBUG();
	}
	EXIT;
}
EXPORT_SYMBOL(__rw_enter);

void
__rw_exit(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	/* rw_owner is held by current
	 * thread iff it is a writer */
	if (rwlp->rw_owner == current) {
		rwlp->rw_owner = NULL;
		up_write(&rwlp->rw_sem);
	} else {
		up_read(&rwlp->rw_sem);
	}
	EXIT;
}
EXPORT_SYMBOL(__rw_exit);

void
__rw_downgrade(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	ASSERT(rwlp->rw_owner == current);

	rwlp->rw_owner = NULL;
	downgrade_write(&rwlp->rw_sem);
	EXIT;
}
EXPORT_SYMBOL(__rw_downgrade);

/* Return 0 if unable to perform the upgrade.
 * Might be wise to fix the caller
 * to acquire the write lock first?
 */
int
__rw_tryupgrade(krwlock_t *rwlp)
{
	int rc = 0;
	ENTRY;

	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	spin_lock(&rwlp->rw_sem.wait_lock);

	/* Check if there is anyone waiting for the
	 * lock.  If there is, then we know we should
	 * not try to upgrade the lock */
	if (!list_empty(&rwlp->rw_sem.wait_list)) {
		spin_unlock(&rwlp->rw_sem.wait_lock);
		RETURN(0);
	}
#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	/* Note that activity is protected by
	 * the wait_lock.  Don't try to upgrade
	 * if there are multiple readers currently
	 * holding the lock */
	if (rwlp->rw_sem.activity > 1) {
#else
	/* Don't try to upgrade
	 * if there are multiple readers currently
	 * holding the lock */
	if ((rwlp->rw_sem.count & RWSEM_ACTIVE_MASK) > 1) {
#endif
		spin_unlock(&rwlp->rw_sem.wait_lock);
		RETURN(0);
	}

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	/* Here it should be safe to drop the
	 * read lock and reacquire it for writing since
	 * we know there are no waiters */
	__up_read_locked(&rwlp->rw_sem);

	/* returns 1 if success, 0 if contention */
	rc = __down_write_trylock_locked(&rwlp->rw_sem);
#else
	/* Here it should be safe to drop the
	 * read lock and reacquire it for writing since
	 * we know there are no waiters */
	up_read(&rwlp->rw_sem);

	/* returns 1 if success, 0 if contention */
	rc = down_write_trylock(&rwlp->rw_sem);
#endif

	/* Check if upgrade failed.  Should not ever happen
	 * if we got to this point */
	ASSERT(rc);
	ASSERT(rwlp->rw_owner == NULL);
	rwlp->rw_owner = current;
	spin_unlock(&rwlp->rw_sem.wait_lock);

	RETURN(1);
}
EXPORT_SYMBOL(__rw_tryupgrade);

kthread_t *
__rw_owner(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	RETURN(rwlp->rw_owner);
}
EXPORT_SYMBOL(__rw_owner);

int
__rw_read_held(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	RETURN(__rw_lock_held(rwlp) && rwlp->rw_owner == NULL);
}
EXPORT_SYMBOL(__rw_read_held);

int
__rw_write_held(krwlock_t *rwlp)
{
	ENTRY;
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	RETURN(rwlp->rw_owner == current);
}
EXPORT_SYMBOL(__rw_write_held);

int
__rw_lock_held(krwlock_t *rwlp)
{
	int rc = 0;
	ENTRY;

	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	spin_lock_irq(&(rwlp->rw_sem.wait_lock));
#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	if (rwlp->rw_sem.activity != 0) {
#else
	if (rwlp->rw_sem.count != 0) {
#endif
		rc = 1;
	}

	spin_unlock_irq(&(rwlp->rw_sem.wait_lock));

	RETURN(rc);
}
EXPORT_SYMBOL(__rw_lock_held);
