/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/sysmacros.h>
#include <sys/time.h>
#include "config.h"

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_TIME

void
__gethrestime(timestruc_t *ts)
{
	getnstimeofday((struct timespec *)ts);
}
EXPORT_SYMBOL(__gethrestime);

int
__clock_gettime(clock_type_t type, timespec_t *tp)
{
	/* Only support CLOCK_REALTIME+__CLOCK_REALTIME0 for now */
        ASSERT((type == CLOCK_REALTIME) || (type == __CLOCK_REALTIME0));

        getnstimeofday(tp);
        return 0;
}
EXPORT_SYMBOL(__clock_gettime);

/* This function may not be as fast as using monotonic_clock() but it
 * should be much more portable, if performance becomes as issue we can
 * look at using monotonic_clock() for x86_64 and x86 arches.
 */
hrtime_t
__gethrtime(void) {
        timespec_t tv;
        hrtime_t rc;

        do_posix_clock_monotonic_gettime(&tv);
        rc = (NSEC_PER_SEC * (hrtime_t)tv.tv_sec) + (hrtime_t)tv.tv_nsec;

        return rc;
}
EXPORT_SYMBOL(__gethrtime);
