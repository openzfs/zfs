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

#ifndef _SPL_TIMER_H
#define	_SPL_TIMER_H

#include <kern/clock.h>

/* Open Solaris lbolt is in hz */
static inline uint64_t
zfs_lbolt(void)
{
	struct timeval tv;
	uint64_t lbolt_hz;
	microuptime(&tv);
	lbolt_hz = ((uint64_t)tv.tv_sec * USEC_PER_SEC + tv.tv_usec) / 10000;
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
