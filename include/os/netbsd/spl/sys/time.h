// SPDX-License-Identifier: BSD-2-Clause
#ifndef _OPENSOLARIS_SYS_TIME_H_
#define	_OPENSOLARIS_SYS_TIME_H_

#pragma once
#include_next <sys/time.h>
#include <sys/debug.h>
#ifndef _SYS_KERNEL_H_
extern int hz;
int getticks(void);
#endif

#define	SEC		1
/* Signed, matching illumos.  UL would make NSEC2MSEC unsigned division. */
#define	MILLISEC	1000
#define	MICROSEC	1000000
#define	NANOSEC		1000000000LL
#define	TIME_MAX	LLONG_MAX

#define	MSEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MILLISEC))
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))
#define	USEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MICROSEC))
#define	NSEC2USEC(n)	((n) / (NANOSEC / MICROSEC))
#define	NSEC2SEC(n)	((n) / (NANOSEC / SEC))
#define	SEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / SEC))

typedef longlong_t	hrtime_t;

/*
 * NetBSD time_t is 64-bit on every OpenZFS ISA, including i386.
 * Comparing tv_sec (int64_t) to INT64_MIN/MAX is a tautology and
 * trips -Wtype-limits under -Werror.
 */
#define	TIMESPEC_OVERFLOW(ts)	(0)

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	NSEC_TO_TICK(nsec)	((nsec) / (NANOSEC / hz))

#ifdef _KERNEL
#include <sys/systm.h>

static __inline hrtime_t
getlrtime(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (((hrtime_t)ts.tv_sec * NANOSEC) + ts.tv_nsec);
}

static __inline hrtime_t
gethrtime(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (((hrtime_t)ts.tv_sec * NANOSEC) + ts.tv_nsec);
}

#define	gethrestime_sec()	(time_second)
#define	gethrestime(ts)		getnanotime(ts)
/*
 * gethrtime() is precise (nanouptime).  gethrtime_waitfree() is
 * the illumos wait-free variant: never blocks on the timecounter,
 * may be a tick stale.  getnanouptime() is that cached snapshot.
 */
static __inline hrtime_t
gethrtime_waitfree(void)
{
	struct timespec ts;

	getnanouptime(&ts);
	return (((hrtime_t)ts.tv_sec * NANOSEC) + ts.tv_nsec);
}

#define	ddi_get_lbolt64()	zfs_ddi_get_lbolt64()
#define	ddi_get_lbolt()		((clock_t)zfs_ddi_get_lbolt64())

/*
 * getticks() is a 32-bit hardclock counter; (int64_t)getticks()
 * sign-extends and goes negative after ~248 days at 100 Hz.
 * Callers in dsl_scan / txg / vdev_queue need a true monotonic
 * 64-bit lbolt.  Same idea as FreeBSD's sbinuptime conversion.
 */
static inline int64_t
zfs_ddi_get_lbolt64(void)
{
	struct timespec ts;

	getnanouptime(&ts);
	return ((int64_t)ts.tv_sec * (int64_t)hz +
	    (int64_t)ts.tv_nsec / (int64_t)(NANOSEC / hz));
}

/*
 * Remaining ticks until an absolute lbolt deadline.
 * clock_t is unsigned (unsigned int on current NetBSD, all
 * OpenZFS ISAs).  Subtract as clock_t so wrap is modular,
 * then reinterpret as the same-width signed type before
 * widening.  Deadlines must be less than half the counter
 * range ahead (~248 days at 100 Hz for 32-bit clock_t).
 */
static inline int64_t
zfs_lbolt_delta(clock_t a, clock_t b)
{
	clock_t delta = a - b;

	if (sizeof (clock_t) == sizeof (uint32_t))
		return ((int32_t)(uint32_t)delta);
	if (sizeof (clock_t) == sizeof (uint64_t))
		return ((int64_t)(uint64_t)delta);
	panic("unsupported clock_t size %zu", sizeof (clock_t));
}

static inline int64_t
zfs_lbolt_left(clock_t deadline)
{
	return (zfs_lbolt_delta(deadline, (clock_t)zfs_ddi_get_lbolt64()));
}

#else /* !_KERNEL */

static __inline hrtime_t
gethrtime(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (((uint64_t)ts.tv_sec) * NANOSEC + ts.tv_nsec);
}

#endif /* _KERNEL */

#endif /* !_OPENSOLARIS_SYS_TIME_H_ */
