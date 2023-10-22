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
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_TIME_H
#define	_SPL_TIME_H

#include <sys/types.h>
#include_next <sys/time.h>
#include <sys/timer.h>
#include <mach/mach_time.h>

#if defined(CONFIG_64BIT)
#define	TIME_MAX			INT64_MAX
#define	TIME_MIN			INT64_MIN
#else
#define	TIME_MAX			INT32_MAX
#define	TIME_MIN			INT32_MIN
#endif

#define	SEC				1
#define	MILLISEC			1000
#define	MICROSEC			1000000
#define	NANOSEC				1000000000

/* Already defined in include/linux/time.h */
#undef CLOCK_THREAD_CPUTIME_ID
#undef CLOCK_REALTIME
#undef CLOCK_MONOTONIC
#undef CLOCK_PROCESS_CPUTIME_ID

typedef enum clock_type {
	__CLOCK_REALTIME0 =	0,	/* obsolete; same as CLOCK_REALTIME */
	CLOCK_VIRTUAL =		1,	/* thread's user-level CPU clock */
	CLOCK_THREAD_CPUTIME_ID	= 2,	/* thread's user+system CPU clock */
	CLOCK_REALTIME =	3,	/* wall clock */
	CLOCK_MONOTONIC =	4,	/* high resolution monotonic clock */
	CLOCK_PROCESS_CPUTIME_ID = 5,	/* process's user+system CPU clock */
	CLOCK_HIGHRES =		CLOCK_MONOTONIC,	 /* alternate name */
	CLOCK_PROF =		CLOCK_THREAD_CPUTIME_ID, /* alternate name */
} clock_type_t;

#define	TIMESPEC_OVERFLOW(ts)		\
	((ts)->tv_sec < TIME_MIN || (ts)->tv_sec > TIME_MAX)

typedef long long	hrtime_t;

extern hrtime_t gethrtime(void);
extern void gethrestime(struct timespec *);
extern time_t gethrestime_sec(void);
extern void hrt2ts(hrtime_t hrt, struct timespec *tsp);

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	NSEC_TO_TICK(nsec)	((nsec) / (NANOSEC / hz))

#define	MSEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MILLISEC))
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))

#define	USEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MICROSEC))
#define	NSEC2USEC(n)	((n) / (NANOSEC / MICROSEC))

#define	NSEC2SEC(n)	((n) / (NANOSEC / SEC))
#define	SEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / SEC))


#endif  /* _SPL_TIME_H */
