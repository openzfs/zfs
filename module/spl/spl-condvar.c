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
 *  Solaris Porting Layer (SPL) Credential Implementation.
\*****************************************************************************/

#include <sys/condvar.h>
#include <spl-debug.h>

#ifdef SS_DEBUG_SUBSYS
#undef SS_DEBUG_SUBSYS
#endif

#define SS_DEBUG_SUBSYS SS_CONDVAR

void
__cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	int flags = KM_SLEEP;

	SENTRY;
	ASSERT(cvp);
	ASSERT(name == NULL);
	ASSERT(type == CV_DEFAULT);
	ASSERT(arg == NULL);

	cvp->cv_magic = CV_MAGIC;
	init_waitqueue_head(&cvp->cv_event);
	init_waitqueue_head(&cvp->cv_destroy);
	atomic_set(&cvp->cv_waiters, 0);
	cvp->cv_mutex = NULL;

        /* We may be called when there is a non-zero preempt_count or
	 * interrupts are disabled is which case we must not sleep.
	 */
        if (current_thread_info()->preempt_count || irqs_disabled())
		flags = KM_NOSLEEP;

	SEXIT;
}
EXPORT_SYMBOL(__cv_init);

static int
cv_destroy_wakeup(kcondvar_t *cvp)
{
	if ((waitqueue_active(&cvp->cv_event)) ||
	    (atomic_read(&cvp->cv_waiters) > 0))
		return 0;

	return 1;
}

void
__cv_destroy(kcondvar_t *cvp)
{
	SENTRY;
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);

	/* Block until all waiters have woken */
	while (cv_destroy_wakeup(cvp) == 0)
		wait_event_timeout(cvp->cv_destroy, cv_destroy_wakeup(cvp), 1);

	ASSERT(cvp->cv_mutex == NULL);
	ASSERT(atomic_read(&cvp->cv_waiters) == 0);
	ASSERT(!waitqueue_active(&cvp->cv_event));

	SEXIT;
}
EXPORT_SYMBOL(__cv_destroy);

static void
cv_wait_common(kcondvar_t *cvp, kmutex_t *mp, int state)
{
	DEFINE_WAIT(wait);
	SENTRY;

	ASSERT(cvp);
        ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ASSERT(mutex_owned(mp));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mp;

	/* Ensure the same mutex is used by all callers */
	ASSERT(cvp->cv_mutex == mp);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait, state);
	atomic_inc(&cvp->cv_waiters);

	/* Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty. */
	mutex_exit(mp);
	schedule();
	mutex_enter(mp);

	/* No more waiters a different mutex could be used */
	if (atomic_dec_and_test(&cvp->cv_waiters)) {
		cvp->cv_mutex = NULL;
		wake_up(&cvp->cv_destroy);
	}

	finish_wait(&cvp->cv_event, &wait);

	SEXIT;
}

void
__cv_wait(kcondvar_t *cvp, kmutex_t *mp)
{
	cv_wait_common(cvp, mp, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(__cv_wait);

void
__cv_wait_interruptible(kcondvar_t *cvp, kmutex_t *mp)
{
	cv_wait_common(cvp, mp, TASK_INTERRUPTIBLE);
}
EXPORT_SYMBOL(__cv_wait_interruptible);

/* 'expire_time' argument is an absolute wall clock time in jiffies.
 * Return value is time left (expire_time - now) or -1 if timeout occurred.
 */
static clock_t
__cv_timedwait_common(kcondvar_t *cvp, kmutex_t *mp,
		      clock_t expire_time, int state)
{
	DEFINE_WAIT(wait);
	clock_t time_left;
	SENTRY;

	ASSERT(cvp);
        ASSERT(mp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	ASSERT(mutex_owned(mp));

	if (cvp->cv_mutex == NULL)
		cvp->cv_mutex = mp;

	/* Ensure the same mutex is used by all callers */
	ASSERT(cvp->cv_mutex == mp);

	/* XXX - Does not handle jiffie wrap properly */
	time_left = expire_time - jiffies;
	if (time_left <= 0)
		SRETURN(-1);

	prepare_to_wait_exclusive(&cvp->cv_event, &wait, state);
	atomic_inc(&cvp->cv_waiters);

	/* Mutex should be dropped after prepare_to_wait() this
	 * ensures we're linked in to the waiters list and avoids the
	 * race where 'cvp->cv_waiters > 0' but the list is empty. */
	mutex_exit(mp);
	time_left = schedule_timeout(time_left);
	mutex_enter(mp);

	/* No more waiters a different mutex could be used */
	if (atomic_dec_and_test(&cvp->cv_waiters)) {
		cvp->cv_mutex = NULL;
		wake_up(&cvp->cv_destroy);
	}

	finish_wait(&cvp->cv_event, &wait);

	SRETURN(time_left > 0 ? time_left : -1);
}

clock_t
__cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, clock_t exp_time)
{
	return __cv_timedwait_common(cvp, mp, exp_time, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(__cv_timedwait);

clock_t
__cv_timedwait_interruptible(kcondvar_t *cvp, kmutex_t *mp, clock_t exp_time)
{
	return __cv_timedwait_common(cvp, mp, exp_time, TASK_INTERRUPTIBLE);
}
EXPORT_SYMBOL(__cv_timedwait_interruptible);

void
__cv_signal(kcondvar_t *cvp)
{
	SENTRY;
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);

	/* All waiters are added with WQ_FLAG_EXCLUSIVE so only one
	 * waiter will be set runable with each call to wake_up().
	 * Additionally wake_up() holds a spin_lock assoicated with
	 * the wait queue to ensure we don't race waking up processes. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up(&cvp->cv_event);

	SEXIT;
}
EXPORT_SYMBOL(__cv_signal);

void
__cv_broadcast(kcondvar_t *cvp)
{
	ASSERT(cvp);
	ASSERT(cvp->cv_magic == CV_MAGIC);
	SENTRY;

	/* Wake_up_all() will wake up all waiters even those which
	 * have the WQ_FLAG_EXCLUSIVE flag set. */
	if (atomic_read(&cvp->cv_waiters) > 0)
		wake_up_all(&cvp->cv_event);

	SEXIT;
}
EXPORT_SYMBOL(__cv_broadcast);
