/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 * Per-waiter list condvar implementation.
 *
 * Each cv_wait() call registers a stack-allocated cv_waiter_t on the
 * condvar's internal list (protected by a KSPIN_LOCK) before releasing
 * the caller's mutex.  cv_signal() dequeues the head entry and sets its
 * event; cv_broadcast() dequeues and sets all entries.
 *
 * Because the list is protected independently of the caller's mutex,
 * cv_signal/cv_broadcast are safe to call with or without holding that
 * mutex (POSIX pthread_cond_broadcast semantics).  The caller's mutex
 * still serializes the condition check and the list registration, so
 * there is no missed-wakeup window:
 *
 *   - If the broadcaster changes the condition and fires BEFORE the waiter
 *     acquires the caller's mutex: waiter sees the updated condition and
 *     does not sleep.
 *   - If the waiter acquires the mutex first, finds the condition unsatisfied,
 *     and registers on the list BEFORE the broadcaster fires: broadcaster
 *     finds the entry on the list and sets its event.
 *
 * The two cases are mutually exclusive because both sides hold the same
 * caller's mutex when touching the condition.
 */

#include <sys/atomic.h>
#include <sys/condvar.h>
#include <sys/thread.h>
#include <spl-debug.h>
#include <sys/callb.h>

#ifdef SPL_DEBUG_MUTEX
void spl_wdlist_settime(void *mpleak, uint64_t value);
#endif

#define	CONDVAR_INIT 0x12345678

/*
 * One entry per in-flight cv_wait call, allocated on the sleeping thread's
 * stack.  Registered on kcondvar_t.cv_waiters while the thread sleeps.
 */
typedef struct cv_waiter {
	KEVENT		event;	/* personal auto-reset event */
	LIST_ENTRY	link;	/* chained into kcondvar_t.cv_waiters */
	boolean_t	taken;	/* TRUE once signal/broadcast dequeues this */
} cv_waiter_t;

void
spl_cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	(void) name; (void) type; (void) arg;

	KeInitializeSpinLock(&cvp->cv_lock);
	InitializeListHead(&cvp->cv_waiters);
	cvp->cv_waiters_count = 0;
	cvp->cv_initialised = CONDVAR_INIT;
}

void
spl_cv_destroy(kcondvar_t *cvp)
{
	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	/*
	 * Spin until every thread that entered cv_wait has fully exited
	 * (decremented cv_waiters_count).  Callers must broadcast before
	 * cv_destroy so all waiters have been dequeued and their events set;
	 * by the time we reach here the list is empty.  Calling
	 * spl_cv_broadcast() in this loop would acquire the spinlock at
	 * DISPATCH_LEVEL on every iteration — expensive and unnecessary
	 * since there is nothing left to dequeue.  A plain spin with a CPU
	 * yield hint is enough; the waiter merely has to return from
	 * KeWaitForSingleObject and re-acquire its mutex before decrementing.
	 */
	while (cvp->cv_waiters_count > 0)
		kpreempt(KPREEMPT_SYNC);
	ASSERT0(cvp->cv_waiters_count);

	cvp->cv_initialised = 0;
}

void
spl_cv_signal(kcondvar_t *cvp)
{
	KIRQL irql;

	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	if (!IsListEmpty(&cvp->cv_waiters)) {
		cv_waiter_t *w = CONTAINING_RECORD(
		    RemoveHeadList(&cvp->cv_waiters), cv_waiter_t, link);
		w->taken = TRUE;
		KeSetEvent(&w->event, 0, FALSE);
	}
	KeReleaseSpinLock(&cvp->cv_lock, irql);
}

void
spl_cv_broadcast(kcondvar_t *cvp)
{
	KIRQL irql;

	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	while (!IsListEmpty(&cvp->cv_waiters)) {
		cv_waiter_t *w = CONTAINING_RECORD(
		    RemoveHeadList(&cvp->cv_waiters), cv_waiter_t, link);
		w->taken = TRUE;
		KeSetEvent(&w->event, 0, FALSE);
	}
	KeReleaseSpinLock(&cvp->cv_lock, irql);
}

/*
 * Block on the indicated condition variable and
 * release the associated mutex while blocked.
 */
int
spl_cv_wait(kcondvar_t *cvp, kmutex_t *mp, int flags, const char *msg)
{
	KIRQL irql;
	int result;

	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	if (msg != NULL && msg[0] == '&')
		++msg;  /* skip over '&' prefixes */
#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	cv_waiter_t w;
	KeInitializeEvent(&w.event, SynchronizationEvent, FALSE);
	w.taken = FALSE;

	/*
	 * Count this thread as in-flight before registering on the list.
	 * cv_destroy spins on this count to ensure no thread is still
	 * accessing cvp fields when the struct is freed.
	 */
	atomic_inc_32(&cvp->cv_waiters_count);

	/* Register before releasing the mutex so no broadcast can be missed. */
	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	InsertTailList(&cvp->cv_waiters, &w.link);
	KeReleaseSpinLock(&cvp->cv_lock, irql);

	mutex_exit(mp);

	LARGE_INTEGER timeout;
	timeout.QuadPart = -10000000LL;  /* 1 second */

	/*
	 * Loop on 1-second timeouts when PCATCH is set so that a pending
	 * thread-termination signal can be observed.  Without PCATCH we pass
	 * NULL and block indefinitely.
	 */
	do {
		result = KeWaitForSingleObject(&w.event, Executive, KernelMode,
		    FALSE, (flags & PCATCH) ? &timeout : NULL);
		if (PsIsThreadTerminating(PsGetCurrentThread()))
			result = STATUS_ALERTED;
	} while (result == STATUS_TIMEOUT);

	mutex_enter(mp);

	/*
	 * Remove ourselves from the list if signal/broadcast hasn't already
	 * done so (e.g. STATUS_ALERTED exit path).
	 */
	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	if (!w.taken)
		RemoveEntryList(&w.link);
	KeReleaseSpinLock(&cvp->cv_lock, irql);

	/* All condvar accesses complete; signal cv_destroy that we are done. */
	atomic_dec_32(&cvp->cv_waiters_count);

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, gethrestime_sec());
#endif
	/*
	 * 1 - condvar got cv_signal()/cv_broadcast()
	 * 0 - received signal (kill -signal)
	 */
	return (result == STATUS_ALERTED ? 0 : 1);
}

/*
 * Same as cv_wait except the thread will unblock at 'tim'
 * (an absolute time) if it hasn't already unblocked.
 *
 * Returns the amount of time left from the original 'tim' value
 * when it was unblocked.
 */
int
spl_cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, clock_t tim, int flags,
    const char *msg)
{
	KIRQL irql;
	int result;
	clock_t timenow;
	LARGE_INTEGER timeout;
	(void) flags;

	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	if (msg != NULL && msg[0] == '&')
		++msg;  /* skip over '&' prefixes */

	timenow = zfs_lbolt();

	if (tim < timenow)
		tim = timenow;

	timeout.QuadPart = -100000 * MAX(1, (tim - timenow) / hz);

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	atomic_inc_32(&cvp->cv_waiters_count);

	cv_waiter_t w;
	KeInitializeEvent(&w.event, SynchronizationEvent, FALSE);
	w.taken = FALSE;

	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	InsertTailList(&cvp->cv_waiters, &w.link);
	KeReleaseSpinLock(&cvp->cv_lock, irql);

	mutex_exit(mp);

	result = KeWaitForSingleObject(&w.event, Executive, KernelMode,
	    FALSE, &timeout);

	mutex_enter(mp);

	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	if (!w.taken)
		RemoveEntryList(&w.link);
	KeReleaseSpinLock(&cvp->cv_lock, irql);

	atomic_dec_32(&cvp->cv_waiters_count);

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, gethrestime_sec());
#endif

	switch (result) {

		case STATUS_ALERTED: /* Signal */
		case ERESTART:
			return (0);

		case STATUS_TIMEOUT: /* Timeout */
			return (-1);
	}

	return (1);
}


/*
 * Compatibility wrapper for the cv_timedwait_hires() Illumos interface.
 */
int
cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim,
    hrtime_t res, int flag)
{
	KIRQL irql;
	int result;
	LARGE_INTEGER timeout;

	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);
	ASSERT(cvp->cv_initialised == CONDVAR_INIT);

	if (res > 1) {
		/*
		 * Align expiration to the specified resolution.
		 */
		if (flag & CALLOUT_FLAG_ROUNDUP)
			tim += res - 1;
		tim = (tim / res) * res;
	}

	if (flag & CALLOUT_FLAG_ABSOLUTE) {
		/* 'tim' is absolute UNIX time; convert to relative sleep */
		hrtime_t now = gethrtime();
		tim -= now;
	}
	timeout.QuadPart = -tim / 100;

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	atomic_inc_32(&cvp->cv_waiters_count);

	cv_waiter_t w;
	KeInitializeEvent(&w.event, SynchronizationEvent, FALSE);
	w.taken = FALSE;

	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	InsertTailList(&cvp->cv_waiters, &w.link);
	KeReleaseSpinLock(&cvp->cv_lock, irql);

	mutex_exit(mp);

	result = KeWaitForSingleObject(&w.event, Executive, KernelMode,
	    FALSE, &timeout);

	mutex_enter(mp);

	KeAcquireSpinLock(&cvp->cv_lock, &irql);
	if (!w.taken)
		RemoveEntryList(&w.link);
	KeReleaseSpinLock(&cvp->cv_lock, irql);

	atomic_dec_32(&cvp->cv_waiters_count);

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, gethrestime_sec());
#endif

	switch (result) {

		case STATUS_ALERTED: /* Signal */
		case ERESTART:
			return (0);

		case STATUS_TIMEOUT: /* Timeout */
			return (-1);
	}

	return (1);
}
