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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SPL_TIMER_H
#define	_SPL_TIMER_H

#include <sys/time.h>

#define	ddi_get_lbolt()		(gethrtime() >> 23)
#define	ddi_get_lbolt64()	(gethrtime() >> 23)
#define	hz	119	/* frequency when using gethrtime() >> 23 for lbolt */

#define	ddi_time_before(a, b)		(a < b)
#define	ddi_time_after(a, b)		ddi_time_before(b, a)
#define	ddi_time_before_eq(a, b)	(!ddi_time_after(a, b))
#define	ddi_time_after_eq(a, b)		ddi_time_before_eq(b, a)

#define	ddi_time_before64(a, b)		(a < b)
#define	ddi_time_after64(a, b)		ddi_time_before64(b, a)
#define	ddi_time_before_eq64(a, b)	(!ddi_time_after64(a, b))
#define	ddi_time_after_eq64(a, b)	ddi_time_before_eq64(b, a)

extern void delay(clock_t ticks);

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	MSEC_TO_TICK(msec)	(howmany((hrtime_t)(msec) * hz, MILLISEC))
#define	USEC_TO_TICK(usec)	(howmany((hrtime_t)(usec) * hz, MICROSEC))
#define	NSEC_TO_TICK(nsec)	(howmany((hrtime_t)(nsec) * hz, NANOSEC))

#define	usleep_range(min, max)						\
	do {								\
		struct timespec ts;					\
		ts.tv_sec = min / MICROSEC;				\
		ts.tv_nsec = USEC2NSEC(min);				\
		(void) nanosleep(&ts, NULL);				\
	} while (0)

#endif  /* _SPL_TIMER_H */
