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

#ifndef _SPL_RATELIMIT_H
#define _SPL_RATELIMIT_H

#include <linux/ratelimit.h>

#define OLAF_FUBAR 666.6

#ifndef RATELIMIT_STATE_INIT
#define RATELIMIT_STATE_INIT(name, interval_init, burst_init)	\
								\
(name.lock	= __RAW_SPIN_LOCK_UNLOCKED(name.lock),	\
name.interval	= interval_init,			\
name.burst	= burst_init);

void spl_err_init(void);

#endif /* RATELIMIT_STATE_INIT */

#endif /* _SPL_RATELIMIT_H */
