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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
*/
#define	_KERNEL_MODE
#include <ntifs.h>
#include <wdm.h>

#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/timer.h>


//#include <kern/clock.h>


/*
 * gethrtime() provides high-resolution timestamps with machine-dependent origin
.
 * Hence its primary use is to specify intervals.
 */

static hrtime_t
zfs_abs_to_nano(uint64_t elapsed)
{
	return elapsed * KeQueryTimeIncrement() * 100;
}

/* Open Solaris lbolt is in hz */
uint64_t
zfs_lbolt(void)
{
	uint64_t lbolt_hz;
	LARGE_INTEGER ticks;
	KeQueryTickCount(&ticks);
	lbolt_hz = ticks.QuadPart * KeQueryTimeIncrement();
	lbolt_hz /= (10000000 / 119); // Solaris hz ?
	return (lbolt_hz);
}

hrtime_t gethrtime(void)
{
    //static uint64_t start = 0;
	static LARGE_INTEGER start = { 0 };
	LARGE_INTEGER now;
	if (start.QuadPart == 0) {
		KeQueryTickCount(&start);
		start.QuadPart--;
	}
	KeQueryTickCount(&now);
	ASSERT((now.QuadPart != start.QuadPart));
    return zfs_abs_to_nano(now.QuadPart - start.QuadPart);
}

/*
 * Get bytes from the /dev/random generator. Returns 0
 * on success. Returns EAGAIN if there is insufficient entropy.
 */
int
random_get_bytes(uint8_t *ptr, uint32_t len)
{
	//read_random(ptr, len);
	LARGE_INTEGER TickCount;
	ULONG r;
	PULONG b;
	int i;

	KeQueryTickCount(&TickCount);

	b = (PULONG) ptr;

	for (i = 0; i < len / sizeof(ULONG); i++)
        b[i] = RtlRandomEx(&TickCount.LowPart);

	len &= (sizeof(ULONG) - 1);
    if (len > 0) {
        r = RtlRandomEx(&TickCount.LowPart);
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
