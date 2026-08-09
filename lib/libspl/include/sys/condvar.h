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
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_CONDVAR_H
#define	_SYS_CONDVAR_H

#include <pthread.h>
#include <sys/time.h>
#include <sys/mutex.h>

/*
 * Condition variables
 */
typedef pthread_cond_t		kcondvar_t;

#define	CV_DEFAULT		0
#define	CALLOUT_FLAG_ABSOLUTE	0x2

extern void cv_init(kcondvar_t *cv, char *name, int type, void *arg);
extern void cv_destroy(kcondvar_t *cv);
extern void cv_wait(kcondvar_t *cv, kmutex_t *mp);
extern int cv_wait_sig(kcondvar_t *cv, kmutex_t *mp);
extern int cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime);
extern int cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim,
    hrtime_t res, int flag);
extern void cv_signal(kcondvar_t *cv);
extern void cv_broadcast(kcondvar_t *cv);

#define	cv_timedwait_io(cv, mp, at)		cv_timedwait(cv, mp, at)
#define	cv_timedwait_idle(cv, mp, at)		cv_timedwait(cv, mp, at)
#define	cv_timedwait_sig(cv, mp, at)		cv_timedwait(cv, mp, at)
#define	cv_wait_io(cv, mp)			cv_wait(cv, mp)
#define	cv_wait_idle(cv, mp)			cv_wait(cv, mp)
#define	cv_wait_io_sig(cv, mp)			cv_wait_sig(cv, mp)
#define	cv_timedwait_sig_hires(cv, mp, t, r, f) \
	cv_timedwait_hires(cv, mp, t, r, f)
#define	cv_timedwait_idle_hires(cv, mp, t, r, f) \
	cv_timedwait_hires(cv, mp, t, r, f)

#endif /* _SYS_CONDVAR_H */
