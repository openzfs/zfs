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

#if !defined(MAC_OS_X_VERSION_10_12) || \
	(MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12)
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
