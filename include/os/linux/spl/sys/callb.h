/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
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
 */

#ifndef _SPL_CALLB_H
#define	_SPL_CALLB_H

#include <linux/module.h>
#include <sys/mutex.h>

#define	CALLB_CPR_ASSERT(cp)		ASSERT(MUTEX_HELD((cp)->cc_lockp));

typedef struct callb_cpr {
	kmutex_t	*cc_lockp;
} callb_cpr_t;

#define	CALLB_CPR_INIT(cp, lockp, func, name)   {               \
	(cp)->cc_lockp = lockp;                                 \
}

#define	CALLB_CPR_SAFE_BEGIN(cp) {                              \
	CALLB_CPR_ASSERT(cp);					\
}

#define	CALLB_CPR_SAFE_END(cp, lockp) {                         \
	CALLB_CPR_ASSERT(cp);					\
}

#define	CALLB_CPR_EXIT(cp) {                                    \
	ASSERT(MUTEX_HELD((cp)->cc_lockp));                     \
	mutex_exit((cp)->cc_lockp);                             \
}

#endif  /* _SPL_CALLB_H */
