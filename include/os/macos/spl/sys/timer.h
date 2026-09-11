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

#ifndef _SPL_TIMER_H
#define	_SPL_TIMER_H

#include <kern/clock.h>

/* Open Solaris lbolt is in hz */
static inline int64_t
zfs_lbolt(void)
{
	struct timeval tv;
	int64_t lbolt_hz;
	microuptime(&tv);
	lbolt_hz = ((int64_t)tv.tv_sec * USEC_PER_SEC + tv.tv_usec) / 10000;
	return (lbolt_hz);
}


#define	lbolt zfs_lbolt()
#define	lbolt64 zfs_lbolt()

#define	ddi_get_lbolt()		(zfs_lbolt())
#define	ddi_get_lbolt64()	(zfs_lbolt())

#define	typecheck(type, x)			\
	( 					\
	{ type __dummy;				\
		typeof(x) __dummy2;		\
		(void) (&__dummy == &__dummy2);	\
		1;				\
	})



#define	ddi_time_before(a, b) (typecheck(clock_t, a) && \
		typecheck(clock_t, b) && \
		((a) - (b) < 0))
#define	ddi_time_after(a, b) ddi_time_before(b, a)

#define	ddi_time_before64(a, b) (typecheck(int64_t, a) && \
		typecheck(int64_t, b) &&  \
		((a) - (b) < 0))
#define	ddi_time_after64(a, b) ddi_time_before64(b, a)



extern void delay(clock_t ticks);

#define	usleep_range(wakeup, whocares)			\
	do {						\
		hrtime_t delta = wakeup - gethrtime();	\
		if (delta > 0) {			\
			struct timespec ts;		\
			ts.tv_sec = delta / NANOSEC;	\
			ts.tv_nsec = delta % NANOSEC;	\
			(void) msleep(NULL, NULL, PWAIT, "usleep_range", &ts); \
		}					\
	} while (0)


#endif  /* _SPL_TIMER_H */
