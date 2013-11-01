/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_TIME_H
#define _SPL_TIME_H

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */
#include <linux/module.h>
#include <linux/time.h>
#include <sys/types.h>
#include <sys/timer.h>

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

#define	MSEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MILLISEC))
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))

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

#define hz					\
({						\
        ASSERT(HZ >= 100 && HZ <= MICROSEC);	\
        HZ;					\
})

extern void __gethrestime(timestruc_t *);
extern int __clock_gettime(clock_type_t, timespec_t *);
extern hrtime_t __gethrtime(void);

#define gethrestime(ts)			__gethrestime(ts)
#define clock_gettime(fl, tp)		__clock_gettime(fl, tp)
#define gethrtime()			__gethrtime()

static __inline__ time_t
gethrestime_sec(void)
{
        timestruc_t now;

        __gethrestime(&now);
        return now.tv_sec;
}

#define TIMESPEC_OVERFLOW(ts)		\
	((ts)->tv_sec < TIME_MIN || (ts)->tv_sec > TIME_MAX)

#endif  /* _SPL_TIME_H */
