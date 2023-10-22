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
	(MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12)

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
