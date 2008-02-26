#ifndef _SYS_LINUX_CONDVAR_H
#define _SYS_LINUX_CONDVAR_H

#ifdef  __cplusplus
extern "C" {
#endif

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
} kcondvar_t;

typedef enum { CV_DEFAULT=0, CV_DRIVER } kcv_type_t;

static __inline__ void
cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	BUG_ON(cvp == NULL);
	BUG_ON(type != CV_DEFAULT);
	BUG_ON(arg != NULL);

	cvp->cv_magic = CV_MAGIC;
	init_waitqueue_head(&cvp->cv_event);
	atomic_set(&cvp->cv_waiters, 0);
	cvp->cv_mutex = NULL;
	cvp->cv_name = NULL;

	if (name) {
	        cvp->cv_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
	        if (cvp->cv_name)
		        strcpy(cvp->cv_name, name);
	}
}

static __inline__ void
cv_destroy(kcondvar_t *cvp)
{
	BUG_ON(cvp == NULL);
	BUG_ON(cvp->cv_magic != CV_MAGIC);
	BUG_ON(atomic_read(&cvp->cv_waiters) != 0);
	BUG_ON(waitqueue_active(&cvp->cv_event));

	if (cvp->cv_name)
		kfree(cvp->cv_name);

	memset(cvp, CV_POISON, sizeof(*cvp));
}

static __inline__ void
cv_wait(kcondvar_t *cvp, kmutex_t *mtx)
{
	DEFINE_WAIT(wait);
	int flag = 1;

	BUG_ON(cvp == NULL || mtx == NULL);
	BUG_ON(cvp->cv_magic != CV_MAGIC);
	BUG_ON(!mutex_owned(mtx));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mtx;

	/* Ensure the same mutex is used by all callers */
	BUG_ON(cvp->cv_mutex != mtx);

	for (;;) {
		prepare_to_wait_exclusive(&cvp->cv_event, &wait,
					  TASK_INTERRUPTIBLE);
		/* Must occur after we are added to the list but only once */
		if (flag) {
			atomic_inc(&cvp->cv_waiters);
			flag = 0;
		}

		/* XXX - The correct thing to do here may be to wake up and 
		 * force the caller to handle the signal.  Spurious wakeups 
		 * should already be safely handled by the caller. */
		if (signal_pending(current))
			flush_signals(current);

		/* Mutex should be dropped after prepare_to_wait() this
		 * ensures we're linked in to the waiters list and avoids the
		 * race where 'cvp->cv_waiters > 0' but the list is empty. */
		mutex_exit(mtx);
		schedule();
		mutex_enter(mtx);

		/* XXX - The correct thing to do here may be to wake up and 
		 * force the caller to handle the signal.  Spurious wakeups 
		 * should already be safely handled by the caller. */
		if (signal_pending(current))
			continue;

		break;
	}

	atomic_dec(&cvp->cv_waiters);
	finish_wait(&cvp->cv_event, &wait);
}

/* 'expire_time' argument is an absolute wall clock time in jiffies.
 * Return value is time left (expire_time - now) or -1 if timeout occurred.
 */
static __inline__ clock_t
cv_timedwait(kcondvar_t *cvp, kmutex_t *mtx, clock_t expire_time)
{
	DEFINE_WAIT(wait);
	clock_t time_left;
	int flag = 1;

	BUG_ON(cvp == NULL || mtx == NULL);
	BUG_ON(cvp->cv_magic != CV_MAGIC);
	BUG_ON(!mutex_owned(mtx));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mtx;

	/* XXX - Does not handle jiffie wrap properly */
	time_left = expire_time - jiffies;
	if (time_left <= 0)
		return -1;

	/* Ensure the same mutex is used by all callers */
	BUG_ON(cvp->cv_mutex != mtx);

	for (;;) {
		prepare_to_wait_exclusive(&cvp->cv_event, &wait,
					  TASK_INTERRUPTIBLE);
		if (flag) {
			atomic_inc(&cvp->cv_waiters);
			flag = 0;
		}

		/* XXX - The correct thing to do here may be to wake up and 
		 * force the caller to handle the signal.  Spurious wakeups 
		 * should already be safely handled by the caller. */
		if (signal_pending(current))
			flush_signals(current);

		/* Mutex should be dropped after prepare_to_wait() this
		 * ensures we're linked in to the waiters list and avoids the
		 * race where 'cvp->cv_waiters > 0' but the list is empty. */
		mutex_exit(mtx);
		time_left = schedule_timeout(time_left);
		mutex_enter(mtx);

		/* XXX - The correct thing to do here may be to wake up and 
		 * force the caller to handle the signal.  Spurious wakeups 
		 * should already be safely handled by the caller. */
		if (signal_pending(current)) {
			if (time_left > 0)
				continue;

			flush_signals(current);
		}

		break;
	}

	atomic_dec(&cvp->cv_waiters);
	finish_wait(&cvp->cv_event, &wait);

	return (time_left > 0 ? time_left : -1);
}

static __inline__ void
cv_signal(kcondvar_t *cvp)
{
	BUG_ON(cvp == NULL);
	BUG_ON(cvp->cv_magic != CV_MAGIC);

	/* All waiters are added with WQ_FLAG_EXCLUSIVE so only one
	 * waiter will be set runable with each call to wake_up().
	 * Additionally wake_up() holds a spin_lock assoicated with
	 * the wait queue to ensure we don't race waking up processes. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up(&cvp->cv_event);
}

static __inline__ void
cv_broadcast(kcondvar_t *cvp)
{
	BUG_ON(cvp == NULL);
	BUG_ON(cvp->cv_magic != CV_MAGIC);

	/* Wake_up_all() will wake up all waiters even those which
	 * have the WQ_FLAG_EXCLUSIVE flag set. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up_all(&cvp->cv_event);
}
#endif /* _SYS_LINUX_CONDVAR_H */
