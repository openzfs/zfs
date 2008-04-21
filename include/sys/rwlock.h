#ifndef _SPL_RWLOCK_H
#define	_SPL_RWLOCK_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <asm/current.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	RW_DRIVER  = 2,		/* driver (DDI) rwlock */
	RW_DEFAULT = 4		/* kernel default rwlock */
} krw_type_t;

typedef enum {
	RW_WRITER,
	RW_READER
} krw_t;

#define RW_READ_HELD(x)         (__rw_read_held((x)))
#define RW_WRITE_HELD(x)        (__rw_write_held((x)))
#define RW_LOCK_HELD(x)         (__rw_lock_held((x)))
#define RW_ISWRITER(x)          (__rw_iswriter(x))

#define RW_MAGIC  0x3423645a
#define RW_POISON 0xa6

typedef struct {
	int rw_magic;
	char *rw_name;
	struct rw_semaphore rw_sem;
	struct task_struct *rw_owner;	/* holder of the write lock */
} krwlock_t;

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	unsigned int flags;
#define RWSEM_WAITING_FOR_READ	0x00000001
#define RWSEM_WAITING_FOR_WRITE	0x00000002
};

/*
 * wake a single writer
 */
static inline struct rw_semaphore *
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

/*
 * release a read lock on the semaphore
 */
static void fastcall
__up_read_locked(struct rw_semaphore *sem)
{
	if (--sem->activity == 0 && !list_empty(&sem->wait_list))
		sem = __rwsem_wake_one_writer_locked(sem);
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static int fastcall
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

extern int __rw_read_held(krwlock_t *rwlp);
extern int __rw_write_held(krwlock_t *rwlp);
extern int __rw_lock_held(krwlock_t *rwlp);

static __inline__ void
rw_init(krwlock_t *rwlp, char *name, krw_type_t type, void *arg)
{
	ASSERT(type == RW_DEFAULT);	/* XXX no irq handler use */
	ASSERT(arg == NULL);		/* XXX no irq handler use */

	rwlp->rw_magic = RW_MAGIC;
	rwlp->rw_owner = NULL;          /* no one holds the write lock yet */
	init_rwsem(&rwlp->rw_sem);
	rwlp->rw_name = NULL;

        if (name) {
                rwlp->rw_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
                if (rwlp->rw_name)
                        strcpy(rwlp->rw_name, name);
        }
}

static __inline__ void
rw_destroy(krwlock_t *rwlp)
{
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	ASSERT(rwlp->rw_owner == NULL);
	spin_lock(&rwlp->rw_sem.wait_lock);
	ASSERT(list_empty(&rwlp->rw_sem.wait_list));
	spin_unlock(&rwlp->rw_sem.wait_lock);

	if (rwlp->rw_name)
                kfree(rwlp->rw_name);

	memset(rwlp, RW_POISON, sizeof(krwlock_t));
}

/* Return 0 if the lock could not be obtained without blocking.
 */
static __inline__ int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int result;

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
			result = down_read_trylock(&rwlp->rw_sem);
			break;
		case RW_WRITER:
			result = down_write_trylock(&rwlp->rw_sem);
			if (result) {
				/* there better not be anyone else
				 * holding the write lock here */
				ASSERT(rwlp->rw_owner == NULL);
				rwlp->rw_owner = current;
			}
			break;
		default:
			SBUG();
	}

	return result;
}

static __inline__ void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
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
}

static __inline__ void
rw_exit(krwlock_t *rwlp)
{
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
}

static __inline__ void
rw_downgrade(krwlock_t *rwlp)
{
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);
	ASSERT(rwlp->rw_owner == current);

	rwlp->rw_owner = NULL;
	downgrade_write(&rwlp->rw_sem);
}

/* Return 0 if unable to perform the upgrade.
 * Might be wise to fix the caller
 * to acquire the write lock first?
 */
static __inline__ int
rw_tryupgrade(krwlock_t *rwlp)
{
	int result = 0;

	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	spin_lock(&rwlp->rw_sem.wait_lock);

	/* Check if there is anyone waiting for the
	 * lock.  If there is, then we know we should
	 * not try to upgrade the lock */
	if (!list_empty(&rwlp->rw_sem.wait_list)) {
		printk("spl: Warning There are threads waiting\n");
		spin_unlock(&rwlp->rw_sem.wait_lock);
		return 0;
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
		return 0;
	}

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	/* Here it should be safe to drop the
	 * read lock and reacquire it for writing since
	 * we know there are no waiters */
	__up_read_locked(&rwlp->rw_sem);

	/* returns 1 if success, 0 if contention */
	result = __down_write_trylock_locked(&rwlp->rw_sem);
#else
	/* Here it should be safe to drop the
	 * read lock and reacquire it for writing since
	 * we know there are no waiters */
	up_read(&rwlp->rw_sem);

	/* returns 1 if success, 0 if contention */
	result = down_write_trylock(&rwlp->rw_sem);
#endif

	/* Check if upgrade failed.  Should not ever happen
	 * if we got to this point */
	ASSERT(result);
	ASSERT(rwlp->rw_owner == NULL);
	rwlp->rw_owner = current;
	spin_unlock(&rwlp->rw_sem.wait_lock);
	return 1;
}

static __inline__ kthread_t *
rw_owner(krwlock_t *rwlp)
{
	ASSERT(rwlp);
	ASSERT(rwlp->rw_magic == RW_MAGIC);

	return rwlp->rw_owner;
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_RWLOCK_H */
