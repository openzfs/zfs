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
#ifndef _LIBSPL_SYS_OSX_TIME_H
#define	_LIBSPL_SYS_OSX_TIME_H

#include_next <sys/time.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

/*
 * clock_gettime() is defined from 10.12 (High Sierra) onwards.
 * For older platforms, we define in here.
 */

#if !defined(MAC_OS_X_VERSION_10_12) || \
	(MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_12)

#include <time.h>
#include <sys/types.h>
#include <sys/_types/_timespec.h>
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>


#define	CLOCK_REALTIME 0
#define	CLOCK_MONOTONIC_RAW 4
#define	CLOCK_MONOTONIC 6

static inline int
clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	int retval = 0;
	struct timeval now;
	clock_serv_t cclock;
	mach_timespec_t mts;

	switch (clk_id) {
		case CLOCK_MONOTONIC_RAW:
		case CLOCK_MONOTONIC:

			host_get_clock_service(mach_host_self(), CALENDAR_CLOCK,
			    &cclock);
			retval = clock_get_time(cclock, &mts);
			mach_port_deallocate(mach_task_self(), cclock);

			tp->tv_sec = mts.tv_sec;
			tp->tv_nsec = mts.tv_nsec;
			break;
		case CLOCK_REALTIME:
			gettimeofday(&now, NULL);
			tp->tv_sec  = now.tv_sec;
			tp->tv_nsec = now.tv_usec * 1000;
			break;
	}
	return (retval);
}
#endif

#endif
