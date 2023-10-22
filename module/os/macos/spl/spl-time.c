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

#include <sys/sysmacros.h>
#include <sys/time.h>
#include <kern/clock.h>

/*
 * gethrtime() provides high-resolution timestamps with
 * machine-dependent origin Hence its primary use is to specify
 * intervals.
 */

static hrtime_t
zfs_abs_to_nano(uint64_t elapsed)
{
	static mach_timebase_info_data_t sTimebaseInfo = { 0, 0 };

	/*
	 * If this is the first time we've run, get the timebase.
	 * We can use denom == 0 to indicate that sTimebaseInfo is
	 * uninitialised because it makes no sense to have a zero
	 * denominator in a fraction.
	 */

	if (sTimebaseInfo.denom == 0) {
		(void) clock_timebase_info(&sTimebaseInfo);
	}

	/*
	 * Convert to nanoseconds.
	 * return (elapsed * (uint64_t)sTimebaseInfo.numer) /
	 * (uint64_t)sTimebaseInfo.denom;
	 *
	 * Provided the final result is representable in 64 bits the
	 * following maneuver will deliver that result without intermediate
	 * overflow.
	 */
	if (sTimebaseInfo.denom == sTimebaseInfo.numer)
		return (elapsed);
	else if (sTimebaseInfo.denom == 1)
		return (elapsed * (uint64_t)sTimebaseInfo.numer);
	else {
		/* Decompose elapsed = eta32 * 2^32 + eps32: */
		uint64_t eta32 = elapsed >> 32;
		uint64_t eps32 = elapsed & 0x00000000ffffffffLL;

		uint32_t numer = sTimebaseInfo.numer;
		uint32_t denom = sTimebaseInfo.denom;

		/* Form product of elapsed64 (decomposed) and numer: */
		uint64_t mu64 = numer * eta32;
		uint64_t lambda64 = numer * eps32;

		/* Divide the constituents by denom: */
		uint64_t q32 = mu64/denom;
		uint64_t r32 = mu64 - (q32 * denom); /* mu64 % denom */

		return ((q32 << 32) + ((r32 << 32) + lambda64) / denom);
	}
}


hrtime_t
gethrtime(void)
{
	static uint64_t start = 0;
	if (start == 0)
		start = mach_absolute_time();
	return (zfs_abs_to_nano(mach_absolute_time() - start));
}


void
gethrestime(struct timespec *ts)
{
	nanotime(ts);
}

time_t
gethrestime_sec(void)
{
	struct timeval tv;

	microtime(&tv);
	return (tv.tv_sec);
}

void
hrt2ts(hrtime_t hrt, struct timespec *tsp)
{
	uint32_t sec, nsec, tmp;

	tmp = (uint32_t)(hrt >> 30);
	sec = tmp - (tmp >> 2);
	sec = tmp - (sec >> 5);
	sec = tmp + (sec >> 1);
	sec = tmp - (sec >> 6) + 7;
	sec = tmp - (sec >> 3);
	sec = tmp + (sec >> 1);
	sec = tmp + (sec >> 3);
	sec = tmp + (sec >> 4);
	tmp = (sec << 7) - sec - sec - sec;
	tmp = (tmp << 7) - tmp - tmp - tmp;
	tmp = (tmp << 7) - tmp - tmp - tmp;
	nsec = (uint32_t)hrt - (tmp << 9);
	while (nsec >= NANOSEC) {
		nsec -= NANOSEC;
		sec++;
	}
	tsp->tv_sec = (time_t)sec;
	tsp->tv_nsec = nsec;
}
