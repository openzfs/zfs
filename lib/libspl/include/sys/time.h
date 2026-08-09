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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_TIME_H
#define	_LIBSPL_SYS_TIME_H

#include <time.h>
#include <sys/types.h>
#include_next <sys/time.h>

#ifndef SEC
#define	SEC		1
#endif

#ifndef MILLISEC
#define	MILLISEC	1000
#endif

#ifndef MICROSEC
#define	MICROSEC	1000000
#endif

#ifndef NANOSEC
#define	NANOSEC		1000000000
#endif

#ifndef NSEC_PER_USEC
#define	NSEC_PER_USEC	1000L
#endif

#ifndef MSEC2NSEC
#define	MSEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MILLISEC))
#endif

#ifndef NSEC2MSEC
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))
#endif

#ifndef USEC2NSEC
#define	USEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MICROSEC))
#endif

#ifndef NSEC2USEC
#define	NSEC2USEC(n)	((n) / (NANOSEC / MICROSEC))
#endif

#ifndef NSEC2SEC
#define	NSEC2SEC(n)	((n) / (NANOSEC / SEC))
#endif

#ifndef SEC2NSEC
#define	SEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / SEC))
#endif

typedef	long long		hrtime_t;
typedef	struct timespec		timespec_t;
typedef struct timespec		inode_timespec_t;

static inline void
gethrestime(inode_timespec_t *ts)
{
	struct timeval tv;
	(void) gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * NSEC_PER_USEC;
}

static inline uint64_t
gethrestime_sec(void)
{
	struct timeval tv;
	(void) gettimeofday(&tv, NULL);
	return (tv.tv_sec);
}

static inline hrtime_t
getlrtime(void)
{
	struct timeval tv;
	(void) gettimeofday(&tv, NULL);
	return ((((uint64_t)tv.tv_sec) * NANOSEC) +
	    ((uint64_t)tv.tv_usec * NSEC_PER_USEC));
}

static inline hrtime_t
gethrtime(void)
{
	struct timespec ts;
	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((((uint64_t)ts.tv_sec) * NANOSEC) + ts.tv_nsec);
}

#endif /* _LIBSPL_SYS_TIME_H */
