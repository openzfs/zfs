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
#ifndef _LIBSPL_TIME_H
#define	_LIBSPL_TIME_H

#include_next <time.h>
#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

/* Linux also has a timer_create() API we need to emulate. */

/*
 * OsX version can probably be implemented by using:
 * dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
 * dispatch_source_set_event_handler(timer1, ^{vector1(timer1);});
 * dispatch_source_set_cancel_handler(timer1
 * dispatch_time_t start = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC);
 * dispatch_source_set_timer(timer1, start, NSEC_PER_SEC / 5, 0);
 */

typedef void *timer_t;

/*
 * clockid_t was added to the macOS SDK in 10.12. Guard on CLOCK_REALTIME
 * rather than availability macros: newer SDKs (macOS 26+) no longer define
 * MAC_OS_X_VERSION_10_12, which would make the old !defined() check fire
 * incorrectly and conflict with the SDK's own enum clockid_t.
 */
#ifndef CLOCK_REALTIME
typedef int clockid_t;
#endif

struct itimerspec {
	struct timespec it_interval;	/* timer period */
	struct timespec it_value;		/* timer expiration */
};

struct sigevent;

static inline int
timer_create(clockid_t clockid,
    struct sigevent *sevp,
    timer_t *timerid)
{
	(void) clockid;
	(void) sevp;
	(void) timerid;
	return (0);
}

static inline int
timer_settime(timer_t id, int flags,
    const struct itimerspec *its, struct itimerspec *remainvalue)
{
	(void) id;
	(void) flags;
	(void) its;
	(void) remainvalue;
	return (0);
}

static inline int
timer_delete(timer_t id)
{
	(void) id;
	return (0);
}

#endif
