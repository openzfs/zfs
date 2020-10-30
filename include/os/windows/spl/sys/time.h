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
#define _SPL_TIME_H

typedef long long	hrtime_t;

#include <sys/types.h>
//#include_next <sys/time.h>
#include <sys/timer.h>
//#include <mach/mach_time.h>
#include <crt/time.h>
struct timespec;

#if defined(CONFIG_64BIT)
#define TIME_MAX			INT64_MAX
#define TIME_MIN			INT64_MIN
#else
#define TIME_MAX			INT32_MAX
#define TIME_MIN			INT32_MIN
#endif

#define SEC				1
#define MILLISEC			1000
#define MICROSEC			1000000
#define NANOSEC				1000000000

#define        NSEC2SEC(n)     ((n) / (NANOSEC / SEC))
#define        SEC2NSEC(m)     ((hrtime_t)(m) * (NANOSEC / SEC))

/* Already defined in include/linux/time.h */
#undef CLOCK_THREAD_CPUTIME_ID
#undef CLOCK_REALTIME
#undef CLOCK_MONOTONIC
#undef CLOCK_PROCESS_CPUTIME_ID

typedef enum clock_type {
	__CLOCK_REALTIME0 =		0,	/* obsolete; same as CLOCK_REALTIME */
	CLOCK_VIRTUAL =			1,	/* thread's user-level CPU clock */
	CLOCK_THREAD_CPUTIME_ID	=	2,	/* thread's user+system CPU clock */
	CLOCK_REALTIME =		3,	/* wall clock */
	CLOCK_MONOTONIC =		4,	/* high resolution monotonic clock */
	CLOCK_PROCESS_CPUTIME_ID =	5,	/* process's user+system CPU clock */
	CLOCK_HIGHRES =			CLOCK_MONOTONIC,	/* alternate name */
	CLOCK_PROF =			CLOCK_THREAD_CPUTIME_ID,/* alternate name */
} clock_type_t;

#if 0
#define hz					\
({						\
        ASSERT(HZ >= 100 && HZ <= MICROSEC);	\
        HZ;					\
})
#endif

#define TIMESPEC_OVERFLOW(ts)		\
	((ts)->tv_sec < TIME_MIN || (ts)->tv_sec > TIME_MAX)


extern hrtime_t gethrtime(void);
extern void gethrestime(struct timespec *);
extern time_t gethrestime_sec(void);
extern void hrt2ts(hrtime_t hrt, struct timespec *tsp);

#define	MSEC2NSEC(m)    ((hrtime_t)(m) * (NANOSEC / MILLISEC))
#define	USEC2NSEC(u)    ((hrtime_t)(u) * (NANOSEC / MICROSEC))
#define	NSEC2MSEC(n)    ((n) / (NANOSEC / MILLISEC))

// Windows 100NS 
#define	SEC2NSEC100(n) ((n) * 10000000ULL)
#define	NSEC2NSEC100(n) ((n) / 100ULL)

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	NSEC_TO_TICK(nsec)	((nsec) / (NANOSEC / hz))

#define	NSEC2USEC(n)    ((n) / (NANOSEC / MICROSEC))



// ZFS time is 2* 64bit values, which are seconds, and nanoseconds since 1970
// Windows time is 1 64bit value; representing the number of 100-nanosecond intervals since January 1, 1601 (UTC).
// There's 116444736000000000 100ns between 1601 and 1970

// I think these functions handle sec correctly, but nsec should be */100 
#define TIME_WINDOWS_TO_UNIX(WT, UT) do { \
	uint64_t unixepoch = (WT) - 116444736000000000ULL; \
	(UT)[0] = /* seconds */ unixepoch / 10000000ULL; \
	(UT)[1] = /* remainding nsec */ unixepoch - ((UT)[0] * 10000000ULL); \
	} while(0)

#define TIME_UNIX_TO_WINDOWS(UT, WT) do { \
	(WT) = ((UT)[1]) + ((UT)[0] * 10000000ULL) + 116444736000000000ULL; \
	} while(0)

#define TIME_UNIX_TO_WINDOWS_EX(SEC, USEC, WT) do { \
	(WT) = (USEC) + ((SEC) * 10000000ULL) + 116444736000000000ULL; \
	} while(0)

#endif  /* _SPL_TIME_H */
