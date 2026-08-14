// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS condvar wrappers for NetBSD kcondvar_t.
 *
 * OpenZFS cv_timedwait() takes an absolute lbolt deadline and returns
 * 1 on wakeup, -1 on timeout.  NetBSD cv_timedwait() takes relative
 * ticks and returns 0 on wakeup / EWOULDBLOCK on timeout.
 * Signal waits: EINTR/ERESTART → 0 (interrupted), else as above.
 */

#ifndef _OPENSOLARIS_SYS_CONDVAR_H_
#define	_OPENSOLARIS_SYS_CONDVAR_H_

#include <sys/param.h>
#include <sys/systm.h>
#include_next <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <limits.h>

#ifdef _KERNEL

typedef enum {
	CV_DEFAULT,
	CV_DRIVER
} kcv_type_t;

#define	CALLOUT_FLAG_ABSOLUTE	1

/*
 * Prefer the caller-supplied wait name (OpenZFS often passes a
 * short static string).  Fall back to the first lowercase run of
 * the stringified cv expression.  NetBSD stores the pointer; do
 * not copy into a stack buffer.  ps(1) truncates wmesg to 8
 * characters on display -- that is not a kernel assert.
 *
 * The cv_init wrapper calling cv_init is safe: after
 * `#define cv_init zfs_cv_init`, the token cv_init inside
 * zfs_cv_init is blue-painted and is not expanded again
 * (gcc -E).  The body therefore calls NetBSD's cv_init(2).
 */
#define	zfs_cv_init(cv, name, type, arg)	do {			\
	const char *_name = (name);					\
	const char *_expr;						\
	if (_name == NULL || _name[0] == '\0') {			\
		for (_expr = #cv; *_expr != '\0'; _expr++) {		\
			if (*_expr >= 'a' && *_expr <= 'z')		\
				break;					\
		}							\
		_name = (*_expr != '\0') ? _expr : #cv;			\
	}								\
	cv_init((cv), _name);						\
} while (0)
#define	cv_init(cv, name, type, arg)	zfs_cv_init(cv, name, type, arg)

static inline int
zfs_cv_wait_sig(kcondvar_t *cvp, kmutex_t *mp)
{
	int rc = cv_wait_sig(cvp, mp);

	return (rc == 0);
}

static inline int
zfs_cv_relticks(clock_t deadline)
{
	int64_t left;

	left = zfs_lbolt_left(deadline);
	if (left <= 0)
		return (0);
	if (left > (int64_t)INT_MAX)
		return (INT_MAX);
	return ((int)left);
}

static inline int
zfs_cv_from_native(int rc)
{
	if (rc == EWOULDBLOCK)
		return (-1);
	return (1);
}

static inline int
zfs_cv_from_native_sig(int rc)
{
	if (rc == EWOULDBLOCK)
		return (-1);
	if (rc == EINTR || rc == ERESTART)
		return (0);
	return (1);
}

static inline int
zfs_cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, clock_t timo)
{
	int left = zfs_cv_relticks(timo);

	if (left <= 0)
		return (-1);
	return (zfs_cv_from_native(cv_timedwait(cvp, mp, left)));
}

static inline int
zfs_cv_timedwait_sig(kcondvar_t *cvp, kmutex_t *mp, clock_t timo)
{
	int left = zfs_cv_relticks(timo);

	if (left <= 0)
		return (-1);
	return (zfs_cv_from_native_sig(cv_timedwait_sig(cvp, mp, left)));
}

static inline void
zfs_hrtime_to_bintime(hrtime_t tim, struct bintime *bt)
{
	struct timespec ts;

	if (tim < 0)
		tim = 0;
	ts.tv_sec = (time_t)(tim / NANOSEC);
	ts.tv_nsec = (long)(tim % NANOSEC);
	timespec2bintime(&ts, bt);
}

static inline int
zfs_cv_hires_duration(hrtime_t tim, int flag, hrtime_t *durp)
{
	hrtime_t now;

	/*
	 * cv_timedwaitbt() takes a relative timeout and decrements it
	 * in place.  flag == 0 is already a duration.  An absolute
	 * hrtime deadline has to be converted to the time remaining.
	 * Passing the deadline itself waits about as long as the
	 * machine has been up.
	 */
	if (flag == 0) {
		if (tim <= 0)
			return (-1);
		*durp = tim;
		return (0);
	}
	now = gethrtime();
	if (tim <= now)
		return (-1);
	*durp = tim - now;
	return (0);
}

static inline int
zfs_cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim,
    hrtime_t res, int flag)
{
	struct bintime bt, eps;
	hrtime_t dur;

	if (zfs_cv_hires_duration(tim, flag, &dur) != 0)
		return (-1);
	zfs_hrtime_to_bintime(dur, &bt);
	if (res > 0)
		zfs_hrtime_to_bintime(res, &eps);
	else
		memset(&eps, 0, sizeof (eps));
	return (zfs_cv_from_native(cv_timedwaitbt(cvp, mp, &bt, &eps)));
}

static inline int
zfs_cv_timedwait_sig_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim,
    hrtime_t res, int flag)
{
	struct bintime bt, eps;
	hrtime_t dur;

	if (zfs_cv_hires_duration(tim, flag, &dur) != 0)
		return (-1);
	zfs_hrtime_to_bintime(dur, &bt);
	if (res > 0)
		zfs_hrtime_to_bintime(res, &eps);
	else
		memset(&eps, 0, sizeof (eps));
	return (zfs_cv_from_native_sig(cv_timedwaitbt_sig(cvp, mp, &bt,
	    &eps)));
}

#define	cv_wait_sig			zfs_cv_wait_sig
#define	cv_timedwait			zfs_cv_timedwait
#define	cv_timedwait_sig		zfs_cv_timedwait_sig
#define	cv_timedwait_hires		zfs_cv_timedwait_hires
#define	cv_timedwait_sig_hires		zfs_cv_timedwait_sig_hires
#define	cv_timedwait_io			cv_timedwait
#define	cv_timedwait_idle		cv_timedwait
#define	cv_timedwait_sig_io		cv_timedwait_sig
#define	cv_wait_io			cv_wait
#define	cv_wait_io_sig			cv_wait_sig
#define	cv_wait_idle			cv_wait
#define	cv_timedwait_io_hires		cv_timedwait_hires
#define	cv_timedwait_idle_hires		cv_timedwait_hires

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_CONDVAR_H_ */
