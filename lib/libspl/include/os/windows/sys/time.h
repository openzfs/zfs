/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_WINDOWS_SYS_TIME_H
#define	_LIBSPL_WINDOWS_SYS_TIME_H

// #include_next <sys/time.h>
#include <sys/types.h>
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

typedef struct timespec	inode_timespec_t;

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

#ifndef USEC2NSEC
#define	USEC2NSEC(u)	((hrtime_t)(u) * (NANOSEC / MICROSEC))
#endif


#ifndef NSEC2MSEC
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))
#endif

#define	NSEC2SEC(n)	((n) / (NANOSEC / SEC))
#define	SEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / SEC))
#define	NSEC2USEC(n)	((n) / (NANOSEC / MICROSEC))

/* Technically timeval lives in winsock2.h */
#if 0
#ifndef _WINSOCK2API_
struct timeval {
	long	tv_sec;         /* seconds */
	long	tv_usec;        /* and microseconds */
};
#endif
#endif

typedef enum clock_type {
	__CLOCK_REALTIME0 =     0,      /* obsolete; same as CLOCK_REALTIME */
	CLOCK_VIRTUAL =         1,      /* thread's user-level CPU clock */
	CLOCK_THREAD_CPUTIME_ID = 2,    /* thread's user+system CPU clock */
	CLOCK_REALTIME =        3,      /* wall clock */
	CLOCK_MONOTONIC =       4,      /* high resolution monotonic clock */
	CLOCK_PROCESS_CPUTIME_ID = 5,   /* process's user+system CPU clock */
	CLOCK_HIGHRES =         CLOCK_MONOTONIC,         /* alternate name */
	CLOCK_PROF =            CLOCK_THREAD_CPUTIME_ID, /* alternate name */
} clock_type_t;

extern void gethrestime(timestruc_t *);

const char *win_ctime_r(char *buffer, size_t bufsize, time_t cur_time);
#define ctime_r(CLOCK, STR) win_ctime_r((STR), sizeof((STR)), (CLOCK))

struct tm *localtime_r(const time_t *clock, struct tm *result);

#endif /* _LIBSPL_SYS_TIME_H */
