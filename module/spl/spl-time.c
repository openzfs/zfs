/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Time Implementation.
\*****************************************************************************/

#include <sys/sysmacros.h>
#include <sys/time.h>

#ifdef HAVE_MONOTONIC_CLOCK
extern unsigned long long monotonic_clock(void);
#endif

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_TIME

void
__gethrestime(timestruc_t *ts)
{
	struct timespec tspec;

	getnstimeofday(&tspec);

	ts->tv_sec = tspec.tv_sec;
	ts->tv_nsec = tspec.tv_nsec;
}
EXPORT_SYMBOL(__gethrestime);

/* Use monotonic_clock() by default. It's faster and is available on older
 * kernels, but few architectures have them, so we must fallback to
 * do_posix_clock_monotonic_gettime().
 */
hrtime_t
__gethrtime(void) {
#ifdef HAVE_MONOTONIC_CLOCK
        unsigned long long res = monotonic_clock();

        /* Deal with signed/unsigned mismatch */
        return (hrtime_t)(res & ~(1ULL << 63));
#else
        struct timespec ts;

        do_posix_clock_monotonic_gettime(&ts);
        return (((hrtime_t)ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec);
#endif
}
EXPORT_SYMBOL(__gethrtime);

/* set_normalized_timespec() API changes
 * 2.6.0  - 2.6.15: Inline function provided by linux/time.h
 * 2.6.16 - 2.6.25: Function prototype defined but not exported
 * 2.6.26 - 2.6.x:  Function defined and exported
 */
#if !defined(HAVE_SET_NORMALIZED_TIMESPEC_INLINE) && \
    !defined(HAVE_SET_NORMALIZED_TIMESPEC_EXPORT)
void
set_normalized_timespec(struct timespec *ts, time_t sec, long nsec)
{
	while (nsec >= NSEC_PER_SEC) {
	        nsec -= NSEC_PER_SEC;
	        ++sec;
	}
	while (nsec < 0) {
	        nsec += NSEC_PER_SEC;
	        --sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}
EXPORT_SYMBOL(set_normalized_timespec);
#endif
