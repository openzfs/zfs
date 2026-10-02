// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#ifndef OSX_CONDVAR_H
#define	OSX_CONDVAR_H

#include <sys/time.h>
#include <sys/mutex.h>

#define	hz	100  /* sysctl kern.clockrate */

typedef enum {
	CV_DEFAULT,
	CV_DRIVER
} kcv_type_t;


struct cv {
	uint64_t   pad;
};

typedef struct cv  kcondvar_t;

void spl_cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg);
void spl_cv_destroy(kcondvar_t *cvp);
void spl_cv_signal(kcondvar_t *cvp);
void spl_cv_broadcast(kcondvar_t *cvp);
int	 spl_cv_wait(kcondvar_t *cvp, kmutex_t *mp, int flags, const char *msg);
int  spl_cv_timedwait(kcondvar_t *, kmutex_t *, clock_t, int, const char *msg);
int  cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp,
    hrtime_t tim, hrtime_t res, int flag);

/*
 * Use these wrapper macros to obtain the CV variable
 * name to make ZFS more gdb debugging friendly!
 * This name shows up as a thread's wait_event string.
 */
#define	cv_wait(cvp, mp)	\
	(void) spl_cv_wait((cvp), (mp), PRIBIO, #cvp)

#define	cv_wait_io(cvp, mp)	\
	(void) spl_cv_wait((cvp), (mp), PRIBIO, #cvp)

#define	cv_wait_idle(cvp, mp)	\
	(void) spl_cv_wait((cvp), (mp), PRIBIO, #cvp)

#define	cv_timedwait(cvp, mp, tim)	\
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO, #cvp)

#define	cv_timedwait_io(cvp, mp, tim)	\
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO, #cvp)

#define	cv_timedwait_idle(cvp, mp, tim)	\
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO, #cvp)

#define	cv_wait_interruptible(cvp, mp)	\
	(void) spl_cv_wait((cvp), (mp), PRIBIO|PCATCH, #cvp)

#define	cv_timedwait_interruptible(cvp, mp, tim)	\
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO|PCATCH, #cvp)

/* cv_wait_sig is the correct name for cv_wait_interruptible */
#define	cv_wait_sig(cvp, mp)	\
	spl_cv_wait((cvp), (mp), PRIBIO|PCATCH, #cvp)

#define	cv_wait_io_sig(cvp, mp)	\
	spl_cv_wait((cvp), (mp), PRIBIO|PCATCH, #cvp)

#define	cv_timedwait_sig(cvp, mp, tim)	\
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO|PCATCH, #cvp)

#define	TICK_TO_NSEC(tick)	((hrtime_t)(tick) * 1000000000 / hz)
#define	cv_reltimedwait(cvp, mp, tim, type)	\
	cv_timedwait_hires((cvp), (mp), TICK_TO_NSEC((tim)), 0, 0)

#define	cv_timedwait_sig_hires(cvp, mp, tim, res, flag)	\
	cv_timedwait_hires(cvp, mp, tim, res, (flag)|PCATCH)

#define	cv_timedwait_idle_hires(cvp, mp, tim, res, flag)	\
	cv_timedwait_hires(cvp, mp, tim, res, (flag)|PCATCH)

#define	cv_init	spl_cv_init
#define	cv_destroy	spl_cv_destroy
#define	cv_broadcast	spl_cv_broadcast
#define	cv_signal	spl_cv_signal

#endif
