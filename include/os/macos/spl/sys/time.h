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

static __inline hrtime_t
getlrtime(void)
{
	struct timespec ts;
	hrtime_t nsec;

	getnanouptime(&ts);
	nsec = ((hrtime_t)ts.tv_sec * NANOSEC) + ts.tv_nsec;
	return (nsec);
}


#endif  /* _SPL_TIME_H */
