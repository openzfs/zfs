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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 * Following the guide at http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
 * and implementing the second-to-last suggestion, albeit in kernel mode,
 * and replacing CriticalSection with Atomics. At some point, we should
 * perhaps look at the final "SignalObjectAndWait" solution, presumably
 * by using the Wait argument to Mutex, and call WaitForObject.
 */

#include <sys/atomic.h>
#include <sys/condvar.h>
#include <spl-debug.h>
#include <sys/callb.h>

#ifdef SPL_DEBUG_MUTEX
void spl_wdlist_settime(void *mpleak, uint64_t value);
#endif

#define	CONDVAR_INIT 0x12345678

void
spl_cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	(void) cvp;	(void) name; (void) type; (void) arg;

	KeInitializeEvent(&cvp->cv_kevent[CV_SIGNAL], SynchronizationEvent,
	    FALSE);
	KeInitializeEvent(&cvp->cv_kevent[CV_BROADCAST], NotificationEvent,
	    FALSE);

	cvp->cv_waiters_count = 0;
	cvp->cv_initialised = CONDVAR_INIT;
}

void
spl_cv_destroy(kcondvar_t *cvp)
{
	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);
	// We have probably already signalled the waiters, but we need to
	// kick around long enough for them to wake.
	while (cvp->cv_waiters_count > 0)
		cv_broadcast(cvp);
	ASSERT0(cvp->cv_waiters_count);
	cvp->cv_initialised = 0;
}

void
spl_cv_signal(kcondvar_t *cvp)
{
	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	uint32_t have_waiters = cvp->cv_waiters_count > 0;

	if (have_waiters)
		KeSetEvent(&cvp->cv_kevent[CV_SIGNAL], 0, FALSE);
}

// WakeConditionVariable or WakeAllConditionVariable function.

void
spl_cv_broadcast(kcondvar_t *cvp)
{
	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	int have_waiters = cvp->cv_waiters_count > 0;

	if (have_waiters)
		KeSetEvent(&cvp->cv_kevent[CV_BROADCAST], 0, FALSE);
}

/*
 * Block on the indicated condition variable and
 * release the associated mutex while blocked.
 */
int
spl_cv_wait(kcondvar_t *cvp, kmutex_t *mp, int flags, const char *msg)
{
	int result;
	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	if (msg != NULL && msg[0] == '&')
		++msg;  /* skip over '&' prefixes */
#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	atomic_inc_32(&cvp->cv_waiters_count);
	mutex_exit(mp);

	void *locks[CV_MAX_EVENTS] =
		{ &cvp->cv_kevent[CV_SIGNAL], &cvp->cv_kevent[CV_BROADCAST] };

	result = KeWaitForMultipleObjects(2, locks, WaitAny, Executive,
	    KernelMode, FALSE, NULL, NULL);

	// If last listener, clear BROADCAST event. (Even if it was SIGNAL
	// overclearing will not hurt?)
	mutex_enter(mp);

	if (cvp->cv_waiters_count == 1)
		KeClearEvent(&cvp->cv_kevent[CV_BROADCAST]);

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
	int result;
	clock_t timenow;
	LARGE_INTEGER timeout;
	(void) cvp;	(void) flags;

	if (cvp->cv_initialised != CONDVAR_INIT)
		panic("%s: not cv_initialised", __func__);

	if (msg != NULL && msg[0] == '&')
		++msg;  /* skip over '&' prefixes */

	timenow = zfs_lbolt();

	// Check for events already in the past
	if (tim < timenow)
		tim = timenow;

	/*
	 * Pointer to a time-out value that specifies the absolute or
	 * relative time, in 100-nanosecond units, at which the wait is to
	 * be completed.  A positive value specifies an absolute time,
	 * relative to January 1, 1601. A negative value specifies an
	 * interval relative to the current time.
	 */
	timeout.QuadPart = -100000 * MAX(1, (tim - timenow) / hz);

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	atomic_inc_32(&cvp->cv_waiters_count);
	mutex_exit(mp);

	void *locks[CV_MAX_EVENTS] =
		{ &cvp->cv_kevent[CV_SIGNAL], &cvp->cv_kevent[CV_BROADCAST] };

	result = KeWaitForMultipleObjects(2, locks, WaitAny, Executive,
	    KernelMode, FALSE, &timeout, NULL);

	int last_waiter =
	    result == STATUS_WAIT_0 + CV_BROADCAST &&
	    cvp->cv_waiters_count == 1;

	if (last_waiter)
		KeClearEvent(&cvp->cv_kevent[CV_BROADCAST]);

	atomic_dec_32(&cvp->cv_waiters_count);

	mutex_enter(mp);

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
		// 'tim' here is absolute UNIX time (from gethrtime()) so
		// convert it to absolute Windows time
		hrtime_t now = gethrtime();

		tim -= now; // Remove the ticks, what remains is "sleep" amount.
	}
	timeout.QuadPart = -tim / 100;

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	atomic_inc_32(&cvp->cv_waiters_count);
	mutex_exit(mp);

	void *locks[CV_MAX_EVENTS] =
	    { &cvp->cv_kevent[CV_SIGNAL], &cvp->cv_kevent[CV_BROADCAST] };

	result = KeWaitForMultipleObjects(2, locks, WaitAny, Executive,
	    KernelMode, FALSE, &timeout, NULL);

	int last_waiter =
	    result == STATUS_WAIT_0 + CV_BROADCAST &&
	    cvp->cv_waiters_count == 1;

	if (last_waiter)
		KeClearEvent(&cvp->cv_kevent[CV_BROADCAST]);

	atomic_dec_32(&cvp->cv_waiters_count);

	mutex_enter(mp);

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
