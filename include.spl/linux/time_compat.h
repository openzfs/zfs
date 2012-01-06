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
\*****************************************************************************/

#ifndef _SPL_TIME_COMPAT_H
#define _SPL_TIME_COMPAT_H

#include <linux/time.h>

/* timespec_sub() API changes
 * 2.6.18  - 2.6.x: Inline function provided by linux/time.h
 */
#ifndef HAVE_TIMESPEC_SUB
static inline struct timespec
timespec_sub(struct timespec lhs, struct timespec rhs)
{
        struct timespec ts_delta;
        set_normalized_timespec(&ts_delta, lhs.tv_sec - rhs.tv_sec,
                                lhs.tv_nsec - rhs.tv_nsec);
        return ts_delta;
}
#endif /* HAVE_TIMESPEC_SUB */

#endif /* _SPL_TIME_COMPAT_H */

