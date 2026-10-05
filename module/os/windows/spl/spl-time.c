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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */
#define	_KERNEL_MODE
#include <ntifs.h>
#include <wdm.h>

#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/timer.h>

/*
 * gethrtime() provides high-resolution timestamps with machine-dependent
 * origin. Hence its primary use is to specify intervals.
 */

/* Open Solaris lbolt is in hz */
uint64_t
zfs_lbolt(void)
{
	uint64_t lbolt_hz;
	lbolt_hz = gethrtime() / 100;
	lbolt_hz /= (10000000 / 119); // Solaris hz ?
	return (lbolt_hz);
}

hrtime_t
gethrtime(void)
{
	static LARGE_INTEGER start = { 0 };
	static LARGE_INTEGER freq = { 0 };
	LARGE_INTEGER now;
	if (start.QuadPart == 0) {
		start = KeQueryPerformanceCounter(&freq);
		ASSERT(freq.QuadPart < NANOSEC);
		ASSERT(freq.QuadPart > 0);
		freq.QuadPart = NANOSEC / freq.QuadPart;
	}
	now = KeQueryPerformanceCounter(NULL);
	return ((now.QuadPart - start.QuadPart) * freq.QuadPart);
}

/*
 * Get bytes from the /dev/random generator. Returns 0
 * on success. Returns EAGAIN if there is insufficient entropy.
 */
int
random_get_bytes(uint8_t *ptr, uint32_t len)
{
	LARGE_INTEGER PerfCounter;
	ULONG r;
	PULONG b;
	int i;

	PerfCounter = KeQueryPerformanceCounter(NULL);

	b = (PULONG) ptr;

	for (i = 0; i < len / sizeof (ULONG); i++)
		b[i] = RtlRandomEx(&PerfCounter.LowPart);

	len &= (sizeof (ULONG) - 1);
	if (len > 0) {
		r = RtlRandomEx(&PerfCounter.LowPart);
		RtlCopyMemory(&b[i], &r, len);
	}
	return (0);
}


void
gethrestime(struct timespec *ts)
{
	LARGE_INTEGER now;
	uint64_t tv[2];
#if _WIN32_WINNT >= 0x0602
	KeQuerySystemTimePrecise(&now);
#else
	KeQuerySystemTime(&now);
#endif
	TIME_WINDOWS_TO_UNIX(now.QuadPart, tv);
	// change macro to take 2 dst args, "sec and nsec" to avoid this step?
	ts->tv_sec = tv[0];
	ts->tv_nsec = tv[1];
}

time_t
gethrestime_sec(void)
{
	struct timespec tv;
	gethrestime(&tv);
	return (tv.tv_sec);
}

#if 0
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
#endif
