/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017, 2018 by Delphix. All rights reserved.
 */

/*
 * ZTHR Infrastructure
 * ===================
 *
 * ZTHR threads are used for isolated operations that span multiple txgs
 * within a SPA. They generally exist from SPA creation/loading and until
 * the SPA is exported/destroyed. The ideal requirements for an operation
 * to be modeled with a zthr are the following:
 *
 * 1] The operation needs to run over multiple txgs.
 * 2] There is be a single point of reference in memory or on disk that
 *    indicates whether the operation should run/is running or is
 *    stopped.
 *
 * If the operation satisfies the above then the following rules guarantee
 * a certain level of correctness:
 *
 * 1] Any thread EXCEPT the zthr changes the work indicator from stopped
 *    to running but not the opposite.
 * 2] Only the zthr can change the work indicator from running to stopped
 *    (e.g. when it is done) but not the opposite.
 *
 * This way a normal zthr cycle should go like this:
 *
 * 1] An external thread changes the work indicator from stopped to
 *    running and wakes up the zthr.
 * 2] The zthr wakes up, checks the indicator and starts working.
 * 3] When the zthr is done, it changes the indicator to stopped, allowing
 *    a new cycle to start.
 *
 * == ZTHR creation
 *
 * Every zthr needs three inputs to start running:
 *
 * 1] A user-defined checker function (checkfunc) that decides whether
 *    the zthr should start working or go to sleep. The function should
 *    return TRUE when the zthr needs to work or FALSE to let it sleep,
 *    and should adhere to the following signature:
 *    boolean_t checkfunc_name(void *args, zthr_t *t);
 *
 * 2] A user-defined ZTHR function (func) which the zthr executes when
 *    it is not sleeping. The function should adhere to the following
 *    signature type:
 *    void func_name(void *args, zthr_t *t);
 *
 * 3] A void args pointer that will be passed to checkfunc and func
 *    implicitly by the infrastructure.
 *
 * The reason why the above API needs two different functions,
 * instead of one that both checks and does the work, has to do with
 * the zthr's internal lock (zthr_lock) and the allowed cancellation
 * windows. We want to hold the zthr_lock while running checkfunc
 * but not while running func. This way the zthr can be cancelled
 * while doing work and not while checking for work.
 *
 * To start a zthr:
 *     zthr_t *zthr_pointer = zthr_create(checkfunc, func, args);
 *
 * After that you should be able to wakeup, cancel, and resume the
 * zthr from another thread using zthr_pointer.
 *
 * NOTE: ZTHR threads could potentially wake up spuriously and the
 * user should take this into account when writing a checkfunc.
 * [see ZTHR state transitions]
 *
 * == ZTHR cancellation
 *
 * ZTHR threads must be cancelled when their SPA is being exported
 * or when they need to be paused so they don't interfere with other
 * operations.
 *
 * To cancel a zthr:
 *     zthr_cancel(zthr_pointer);
 *
 * To resume it:
 *     zthr_resume(zthr_pointer);
 *
 * A zthr will implicitly check if it has received a cancellation
 * signal every time func returns and everytime it wakes up [see ZTHR
 * state transitions below].
 *
 * At times, waiting for the zthr's func to finish its job may take
 * time. This may be very time-consuming for some operations that
 * need to cancel the SPA's zthrs (e.g spa_export). For this scenario
 * the user can explicitly make their ZTHR function aware of incoming
 * cancellation signals using zthr_iscancelled(). A common pattern for
 * that looks like this:
 *
 * int
 * func_name(void *args, zthr_t *t)
 * {
 *     ... <unpack args> ...
 *     while (!work_done && !zthr_iscancelled(t)) {
 *         ... <do more work> ...
 *     }
 * }
 *
 * == ZTHR cleanup
 *
 * Cancelling a zthr doesn't clean up its metadata (internal locks,
 * function pointers to func and checkfunc, etc..). This is because
 * we want to keep them around in case we want to resume the execution
 * of the zthr later. Similarly for zthrs that exit themselves.
 *
 * To completely cleanup a zthr, cancel it first to ensure that it
 * is not running and then use zthr_destroy().
 *
 * == ZTHR state transitions
 *
 *    zthr creation
 *      +
 *      |
 *      |      woke up
 *      |   +--------------+ sleep
 *      |   |                  ^
 *      |   |                  |
 *      |   |                  | FALSE
 *      |   |                  |
 *      v   v     FALSE        +
 *   cancelled? +---------> checkfunc?
 *      +   ^                  +
 *      |   |                  |
 *      |   |                  | TRUE
 *      |   |                  |
 *      |   |  func returned   v
 *      |   +---------------+ func
 *      |
 *      | TRUE
 *      |
 *      v
 *   zthr stopped running
 *
 */

#include <sys/zfs_context.h>
#include <sys/zthr.h>

struct zthr {
	kthread_t	*zthr_thread;
	kmutex_t	zthr_lock;
	kcondvar_t	zthr_cv;
	boolean_t	zthr_cancel;
	uint64_t	zthr_cancellation_stamp;

	kcondvar_t	zthr_initializing_cv;
	boolean_t	zthr_initializing;

	/* consumer-provided data */
	zthr_checkfunc_t	*zthr_checkfunc;
	zthr_func_t	*zthr_func;
	void		*zthr_arg;
};

static void
zthr_procedure(void *arg)
{
	zthr_t *t = arg;

	mutex_enter(&t->zthr_lock);
	ASSERT3P(t->zthr_thread, ==, curthread);

	/*
	 * Mark the end of initialization and notify waiters
	 * (e.g. callers of zthr_resume()) that we are done.
	 */
	t->zthr_initializing = B_FALSE;
	cv_broadcast(&t->zthr_initializing_cv);

	while (!t->zthr_cancel) {
		if (t->zthr_checkfunc(t->zthr_arg, t)) {
			mutex_exit(&t->zthr_lock);
			t->zthr_func(t->zthr_arg, t);
			mutex_enter(&t->zthr_lock);
		} else {
			/* go to sleep */
			cv_wait_sig(&t->zthr_cv, &t->zthr_lock);
		}
	}

	/*
	 * Clear out any metadata that keep track of the running thread
	 * and notify the respective waiters (e.g. callers of zthr_cancel()).
	 *
	 * We also increment the cancellation stamp so any threads that
	 * called zthr_cancel() on this cycle won't hang forever [see
	 * comment in zthr_cancel()].
	 */
	t->zthr_thread = NULL;
	t->zthr_cancel = B_FALSE;
	t->zthr_cancellation_stamp++;
	cv_broadcast(&t->zthr_cv);

	mutex_exit(&t->zthr_lock);
	thread_exit();
}

zthr_t *
zthr_create(zthr_checkfunc_t *checkfunc, zthr_func_t *func, void *arg)
{
	zthr_t *t = kmem_zalloc(sizeof (*t), KM_SLEEP);
	mutex_init(&t->zthr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&t->zthr_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&t->zthr_initializing_cv, NULL, CV_DEFAULT, NULL);

	mutex_enter(&t->zthr_lock);
	t->zthr_checkfunc = checkfunc;
	t->zthr_func = func;
	t->zthr_arg = arg;

	t->zthr_thread = thread_create(NULL, 0, zthr_procedure, t,
	    0, &p0, TS_RUN, minclsyspri);

	/*
	 * Wait until the zthr has finished initializing. This ensures
	 * that the only time another thread can obtain the lock is when
	 * the zthr func is running or the zthr is asleep.
	 */
	t->zthr_initializing = B_TRUE;
	while (!t->zthr_initializing) {
		cv_wait(&t->zthr_initializing_cv, &t->zthr_lock);
	}
	mutex_exit(&t->zthr_lock);

	return (t);
}

void
zthr_destroy(zthr_t *t)
{
	ASSERT(!MUTEX_HELD(&t->zthr_lock));
	VERIFY3P(t->zthr_thread, ==, NULL);
	mutex_destroy(&t->zthr_lock);
	cv_destroy(&t->zthr_cv);
	cv_destroy(&t->zthr_initializing_cv);
	kmem_free(t, sizeof (*t));
}

/*
 * Note: If the zthr is not sleeping and misses the wakeup
 * (e.g it is running its ZTHR function), it will check if
 * there is work to do before going to sleep using its checker
 * function [see ZTHR state transition in ZTHR block comment].
 * Thus, missing the wakeup still yields the expected behavior.
 */
void
zthr_wakeup(zthr_t *t)
{
	mutex_enter(&t->zthr_lock);
	cv_broadcast(&t->zthr_cv);
	mutex_exit(&t->zthr_lock);
}

/*
 * Sends a cancel request to the zthr and blocks until the zthr is
 * blocked. If the zthr is not running (e.g. has been cancelled
 * already), this is a no-op.
 */
void
zthr_cancel(zthr_t *t)
{
	mutex_enter(&t->zthr_lock);

	if (t->zthr_thread != NULL) {
		/* broadcast in case the zthr is sleeping */
		cv_broadcast(&t->zthr_cv);

		/*
		 * We raise the cancellation flag so the zthr gets notified.
		 *
		 * We also save the current cancellation stamp and compare it
		 * to the value that it has next time we wake up. The point
		 * of this is to ensure that we (e.g. this thread) don't hang
		 * when the zthr gets cancelled from us, but then immediately
		 * resumed from somewhere else (e.g. zthr_thread gets
		 * repopulated) before we get to wake up and exit this function.
		 */
		uint64_t stamp = t->zthr_cancellation_stamp;
		t->zthr_cancel = B_TRUE;
		while (t->zthr_thread != NULL &&
		    stamp == t->zthr_cancellation_stamp) {
			cv_wait(&t->zthr_cv, &t->zthr_lock);
		}
	}

	mutex_exit(&t->zthr_lock);
}

/*
 * Sends a resume request to the zthr and blocks until the zthr is
 * running again. If the zthr is already running, this is a no-op.
 */
void
zthr_resume(zthr_t *t)
{
	mutex_enter(&t->zthr_lock);

	if (t->zthr_thread == NULL) {
		ASSERT3P(&t->zthr_checkfunc, !=, NULL);
		ASSERT3P(&t->zthr_func, !=, NULL);
		ASSERT(!t->zthr_cancel);

		t->zthr_thread = thread_create(NULL, 0, zthr_procedure, t,
		    0, &p0, TS_RUN, minclsyspri);

		/*
		* Wait until the zthr has finished initializing.
		* This ensures that the only time another thread
		* can obtain the lock is when the zthr func is
		* running or the zthr is asleep.
		*/
		t->zthr_initializing = B_TRUE;
		while (t->zthr_initializing) {
			cv_wait(&t->zthr_initializing_cv, &t->zthr_lock);
		}
	}

	mutex_exit(&t->zthr_lock);
}

/*
 * This function is intended to be used by the zthr itself
 * to check if another thread has signal it to stop running.
 *
 * returns TRUE if we are in the middle of trying to cancel
 *     this thread.
 *
 * returns FALSE otherwise.
 */
boolean_t
zthr_iscancelled(zthr_t *t)
{
	ASSERT3P(t->zthr_thread, ==, curthread);

	mutex_enter(&t->zthr_lock);
	boolean_t cancelled = t->zthr_cancel;
	mutex_exit(&t->zthr_lock);

	return (cancelled);
}

boolean_t
zthr_isrunning(zthr_t *t)
{
	mutex_enter(&t->zthr_lock);
	boolean_t running = (t->zthr_thread != NULL);
	mutex_exit(&t->zthr_lock);

	return (running);
}
