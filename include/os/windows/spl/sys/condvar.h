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
 */

#ifndef SPL_CONDVAR_H
#define SPL_CONDVAR_H

#include <sys/time.h>

struct kmutex;

#define	hz   119  /* frequency when using gethrtime() >> 23 for lbolt */

typedef enum {
        CV_DEFAULT,
        CV_DRIVER
} kcv_type_t;

enum {
	CV_SIGNAL = 0,
	CV_BROADCAST = 1,
	CV_MAX_EVENTS = 2
};

struct cv {
	KEVENT kevent[CV_MAX_EVENTS]; // signal event, broadcast event
	KSPIN_LOCK waiters_count_lock;
	uint32_t waiters_count;
	uint32_t initialised; // Just used as sanity
};

typedef struct cv  kcondvar_t;

#define PRIBIO 1
#define PCATCH 2


void spl_cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg);
void spl_cv_destroy(kcondvar_t *cvp);
void spl_cv_signal(kcondvar_t *cvp);
void spl_cv_broadcast(kcondvar_t *cvp);
int spl_cv_wait(kcondvar_t *cvp, struct kmutex *mp, int flags, const char *msg);
int  spl_cv_timedwait(kcondvar_t *cvp, struct kmutex *mp, clock_t tim, int flags,
					  const char *msg);
int cv_timedwait_hires(kcondvar_t *cvp, struct kmutex *mp,
                           hrtime_t tim, hrtime_t res, int flag);


/*
 * Use these wrapper macros to obtain the CV variable
 * name to make ZFS more gdb debugging friendly!
 * This name shows up as a thread's wait_event string.
 */
#define cv_wait(cvp, mp)        \
	(void) spl_cv_wait((cvp), (mp), PRIBIO, #cvp)

/* Linux provides a cv_wait_io so the schedular will know why we block.
 * find OSX equivalent?
 */
#define cv_wait_io(cvp, mp)                     \
    (void) spl_cv_wait((cvp), (mp), PRIBIO, #cvp)

#define cv_wait_idle(cvp, mp)   \
        (void) spl_cv_wait((cvp), (mp), PRIBIO, #cvp)

#define cv_timedwait(cvp, mp, tim)      \
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO, #cvp)

#define cv_timedwait_io(cvp, mp, tim)   \
        spl_cv_timedwait((cvp), (mp), (tim), PRIBIO, #cvp)

#define cv_timedwait_idle(cvp, mp, tim) \
        spl_cv_timedwait((cvp), (mp), (tim), PRIBIO, #cvp)

#define cv_wait_interruptible(cvp, mp)        \
	(void) spl_cv_wait((cvp), (mp), PRIBIO|PCATCH, #cvp)

#define cv_timedwait_interruptible(cvp, mp, tim)  \
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO|PCATCH, #cvp)

/* cv_wait_sig is the correct name for cv_wait_interruptible */
#define cv_wait_sig(cvp, mp)        \
	spl_cv_wait((cvp), (mp), PRIBIO|PCATCH, #cvp)

#define	cv_wait_io_sig(cvp, mp) \
	spl_cv_wait((cvp), (mp), PRIBIO|PCATCH, #cvp)

#define cv_timedwait_sig(cvp, mp, tim)  \
	spl_cv_timedwait((cvp), (mp), (tim), PRIBIO|PCATCH, #cvp)


#define TICK_TO_NSEC(tick)              ((hrtime_t)(tick) * 1000000000 / hz)
#define cv_reltimedwait(cvp, mp, tim, type) \
	cv_timedwait_hires((cvp), (mp), TICK_TO_NSEC((tim)), 0, 0)

#define cv_timedwait_idle_hires(cvp, mp, tim, res, flag)        \
        cv_timedwait_hires(cvp, mp, tim, res, (flag)|PCATCH)

#define cv_init spl_cv_init
#define cv_destroy spl_cv_destroy
#define cv_broadcast spl_cv_broadcast
#define cv_signal spl_cv_signal


#endif
