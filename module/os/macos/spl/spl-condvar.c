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
 * Copyright (C) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/condvar.h>
#include <sys/errno.h>
#include <sys/callb.h>

extern wait_result_t thread_block(thread_continue_t continuation);

/*
 * cv_timedwait() is similar to cv_wait() except that it additionally expects
 * a timeout value specified in ticks.  When woken by cv_signal() or
 * cv_broadcast() it returns 1, otherwise when the timeout is reached -1 is
 * returned.
 *
 * cv_timedwait_sig() behaves the same as cv_timedwait() but blocks
 * interruptibly and can be woken by a signal (EINTR, ERESTART).  When
 * this occurs 0 is returned.
 *
 * cv_timedwait_io() and cv_timedwait_sig_io() are variants of cv_timedwait()
 * and cv_timedwait_sig() which should be used when waiting for outstanding
 * IO to complete.  They are responsible for updating the iowait accounting
 * when this is supported by the platform.
 *
 * cv_timedwait_hires() and cv_timedwait_sig_hires() are high resolution
 * versions of cv_timedwait() and cv_timedwait_sig().  They expect the timeout
 * to be specified as a hrtime_t allowing for timeouts of less than a tick.
 *
 * N.B. The return values differ slightly from the illumos implementation
 * which returns the time remaining, instead of 1, when woken.  They both
 * return -1 on timeout. Consumers which need to know the time remaining
 * are responsible for tracking it themselves.
 */

#ifdef SPL_DEBUG_MUTEX
void spl_wdlist_settime(void *mpleak, uint64_t value);
#endif

void
spl_cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
}

void
spl_cv_destroy(kcondvar_t *cvp)
{
}

void
spl_cv_signal(kcondvar_t *cvp)
{
	wakeup_one((caddr_t)cvp);
}

void
spl_cv_broadcast(kcondvar_t *cvp)
{
	wakeup((caddr_t)cvp);
}


/*
 * Block on the indicated condition variable and
 * release the associated mutex while blocked.
 */
int
spl_cv_wait(kcondvar_t *cvp, kmutex_t *mp, int flags, const char *msg)
{
	int result;

	if (msg != NULL && msg[0] == '&')
		++msg;  /* skip over '&' prefixes */

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif
	mp->m_owner = NULL;
	atomic_inc_64(&mp->m_sleepers);
	result = msleep(cvp, (lck_mtx_t *)&mp->m_lock, flags, msg, 0);
	atomic_dec_64(&mp->m_sleepers);
	mp->m_owner = current_thread();
#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, gethrestime_sec());
#endif

	/*
	 * If already signalled, XNU never releases mutex, so
	 * do so manually if we know there are threads waiting.
	 * Avoids a starvation in bqueue_dequeue().
	 * Does timedwait() versions need the same?
	 */
	if (result == EINTR &&
	    (mp->m_waiters > 0 || mp->m_sleepers > 0)) {
		mutex_exit(mp);
		(void) thread_block(THREAD_CONTINUE_NULL);
		mutex_enter(mp);
	}

	/*
	 * 1 - condvar got cv_signal()/cv_broadcast()
	 * 0 - received signal (kill -signal)
	 */
	return (result == EINTR ? 0 : 1);
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
	struct timespec ts;
	int result;

	if (msg != NULL && msg[0] == '&')
		++msg;  /* skip over '&' prefixes */

	clock_t timenow = zfs_lbolt();

	/* Already expired? */
	if (timenow >= tim)
		return (-1);

	tim -= timenow;

	ts.tv_sec = (tim / hz);
	ts.tv_nsec = (tim % hz) * NSEC_PER_SEC / hz;

	/* Both sec and nsec zero is a blocking call in XNU. (Not poll) */
	if (ts.tv_sec == 0 && ts.tv_nsec == 0)
		ts.tv_nsec = 1000;

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	mp->m_owner = NULL;
	atomic_inc_64(&mp->m_sleepers);
	result = msleep(cvp, (lck_mtx_t *)&mp->m_lock, flags, msg, &ts);
	atomic_dec_64(&mp->m_sleepers);

	mp->m_owner = current_thread();

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, gethrestime_sec());
#endif

	switch (result) {

		case EINTR:			/* Signal */
		case ERESTART:
			return (0);

		case EWOULDBLOCK:	/* Timeout: EAGAIN */
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
	struct timespec ts;
	int result;

	if (res > 1) {
		/*
		 * Align expiration to the specified resolution.
		 */
		if (flag & CALLOUT_FLAG_ROUNDUP)
			tim += res - 1;
		tim = (tim / res) * res;
	}

	if ((flag & CALLOUT_FLAG_ABSOLUTE)) {
		hrtime_t timenow = gethrtime();

		/* Already expired? */
		if (timenow >= tim)
			return (-1);

		tim -= timenow;
	}

	ts.tv_sec = NSEC2SEC(tim);
	ts.tv_nsec = tim - SEC2NSEC(ts.tv_sec);

	/* Both sec and nsec set to zero is a blocking call in XNU. */
	if (ts.tv_sec == 0 && ts.tv_nsec == 0)
		ts.tv_nsec = 1000;

#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, 0);
#endif

	mp->m_owner = NULL;
	atomic_inc_64(&mp->m_sleepers);
	result = msleep(cvp, (lck_mtx_t *)&mp->m_lock,
	    flag, "cv_timedwait_hires", &ts);
	atomic_dec_64(&mp->m_sleepers);
	mp->m_owner = current_thread();
#ifdef SPL_DEBUG_MUTEX
	spl_wdlist_settime(mp->leak, gethrestime_sec());
#endif

	switch (result) {

		case EINTR:			/* Signal */
		case ERESTART:
			return (0);

		case EWOULDBLOCK:	/* Timeout */
			return (-1);
	}

	return (1);
}
