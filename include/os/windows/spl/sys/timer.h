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
#define _SPL_TIMER_H

#include <sys/sched.h>


// Typical timespec is smaller, but we need to retain the precision
// to copy time between Unix and Windows.
struct timespec {
	uint64_t tv_sec;
	uint64_t tv_nsec;
};

//#define USEC_PER_SEC    1000000         /* microseconds per second */



#define lbolt zfs_lbolt()
#define lbolt64 zfs_lbolt()

#define        ddi_get_lbolt()         (zfs_lbolt())
#define        ddi_get_lbolt64()       (zfs_lbolt())

#define typecheck(type,x) \
	({      type __dummy;		  \
		typeof(x) __dummy2;					 \
		(void)(&__dummy == &__dummy2);		 \
        1;									 \
	})


#pragma error( disable: 4296 )
#define ddi_time_before(a, b)           ((a) - (b) < 0)
#define ddi_time_after(a, b)            ddi_time_before(b, a)

#define ddi_time_before64(a, b)         ((a) - (b) < 0)
#define ddi_time_after64(a, b)          ddi_time_before64(b, a)
#pragma error( default: 4296 )

extern uint64_t zfs_lbolt(void);

#define	usleep_range(wakeup, whocares)                  \
	do {                                            \
	hrtime_t delta = wakeup - gethrtime();  \
	if (delta > 0) {                        \
	    LARGE_INTEGER ft; \
	ft.QuadPart = -((__int64)NSEC2NSEC100(delta)); \
	(void) KeWaitForMultipleObjects(0, NULL, WaitAny, Executive, KernelMode, FALSE, &ft, NULL);\
	}                                       \
	} while (0)


#endif  /* _SPL_TIMER_H */
