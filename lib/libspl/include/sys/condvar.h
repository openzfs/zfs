// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
