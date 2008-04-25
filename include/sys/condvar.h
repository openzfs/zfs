#ifndef _SPL_CONDVAR_H
#define _SPL_CONDVAR_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/wait.h>

/* The kcondvar_t struct is protected by mutex taken externally before
 * calling any of the wait/signal funs, and passed into the wait funs.
 */
#define CV_MAGIC			0x346545f4
#define CV_POISON			0x95

typedef struct {
	int cv_magic;
	char *cv_name;
	wait_queue_head_t cv_event;
	atomic_t cv_waiters;
	kmutex_t *cv_mutex; /* only for verification purposes */
	spinlock_t cv_lock;
} kcondvar_t;

typedef enum { CV_DEFAULT=0, CV_DRIVER } kcv_type_t;

static __inline__ void
cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	ENTRY;
	ASSERT(cvp);
	ASSERT(type == CV_DEFAULT);
	ASSERT(arg == NULL);

	cvp->cv_magic = CV_MAGIC;
	init_waitqueue_head(&cvp->cv_event);
	spin_lock_init(&cvp->cv_lock);
	atomic_set(&cvp->cv_waiters, 0);
	cvp->cv_mutex = NULL;
	cvp->cv_name = NULL;

	if (name) {
		cvp->cv_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
		if (cvp->cv_name)
		        strcpy(cvp->cv_name, name);
	}

	EXIT;
}

static __inline__ void
cv_destroy(kcondvar_t *cvp)
{
	ENTRY;
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	spin_lock(&cvp->cv_lock);
	ASSERT(atomic_read(&cvp->cv_waiters) == 0);
	ASSERT(!waitqueue_active(&cvp->cv_event));

	if (cvp->cv_name)
		kfree(cvp->cv_name);

	memset(cvp, CV_POISON, sizeof(*cvp));
	spin_unlock(&cvp->cv_lock);
	EXIT;
}

static __inline__ void
cv_wait(kcondvar_t *cvp, kmutex_t *mtx)
{
	DEFINE_WAIT(wait);
	ENTRY;

	ASSERT(cvp);
        ASSERT(mtx);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	spin_lock(&cvp->cv_lock);
	ASSERT(mutex_owned(mtx));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mtx;

	/* Ensure the same mutex is used by all callers */
	ASSERT(cvp->cv_mutex == mtx);
	spin_unlock(&cvp->cv_lock);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait,
				  TASK_UNINTERRUPTIBLE);
	atomic_inc(&cvp->cv_waiters);

	/* Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty. */
	mutex_exit(mtx);
	schedule();
	mutex_enter(mtx);

	atomic_dec(&cvp->cv_waiters);
	finish_wait(&cvp->cv_event, &wait);
	EXIT;
}

/* 'expire_time' argument is an absolute wall clock time in jiffies.
 * Return value is time left (expire_time - now) or -1 if timeout occurred.
 */
static __inline__ clock_t
cv_timedwait(kcondvar_t *cvp, kmutex_t *mtx, clock_t expire_time)
{
	DEFINE_WAIT(wait);
	clock_t time_left;
	ENTRY;

	ASSERT(cvp);
        ASSERT(mtx);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	spin_lock(&cvp->cv_lock);
	ASSERT(mutex_owned(mtx));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mtx;

	/* Ensure the same mutex is used by all callers */
	ASSERT(cvp->cv_mutex == mtx);
	spin_unlock(&cvp->cv_lock);

	/* XXX - Does not handle jiffie wrap properly */
	time_left = expire_time - jiffies;
	if (time_left <= 0)
		RETURN(-1);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait,
				  TASK_UNINTERRUPTIBLE);
	atomic_inc(&cvp->cv_waiters);

	/* Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty. */
	mutex_exit(mtx);
	time_left = schedule_timeout(time_left);
	mutex_enter(mtx);

	atomic_dec(&cvp->cv_waiters);
	finish_wait(&cvp->cv_event, &wait);

	RETURN(time_left > 0 ? time_left : -1);
}

static __inline__ void
cv_signal(kcondvar_t *cvp)
{
	ENTRY;
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);

	/* All waiters are added with WQ_FLAG_EXCLUSIVE so only one
	 * waiter will be set runable with each call to wake_up().
	 * Additionally wake_up() holds a spin_lock assoicated with
	 * the wait queue to ensure we don't race waking up processes. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up(&cvp->cv_event);

	EXIT;
}

static __inline__ void
cv_broadcast(kcondvar_t *cvp)
{
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ENTRY;

	/* Wake_up_all() will wake up all waiters even those which
	 * have the WQ_FLAG_EXCLUSIVE flag set. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up_all(&cvp->cv_event);

	EXIT;
}
#endif /* _SPL_CONDVAR_H */
