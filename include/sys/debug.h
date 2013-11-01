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

/*
 * Available Solaris debug functions.  All of the ASSERT() macros will be
 * compiled out when NDEBUG is defined, this is the default behavior for
 * the SPL.  To enable assertions use the --enable-debug with configure.
 * The VERIFY() functions are never compiled out and cannot be disabled.
 *
 * PANIC()	- Panic the node and print message.
 * ASSERT()	- Assert X is true, if not panic.
 * ASSERTF()	- Assert X is true, if not panic and print message.
 * ASSERTV()	- Wraps a variable declaration which is only used by ASSERT().
 * ASSERT3S()	- Assert signed X OP Y is true, if not panic.
 * ASSERT3U()	- Assert unsigned X OP Y is true, if not panic.
 * ASSERT3P()	- Assert pointer X OP Y is true, if not panic.
 * ASSERT0()	- Assert value is zero, if not panic.
 * VERIFY()	- Verify X is true, if not panic.
 * VERIFY3S()	- Verify signed X OP Y is true, if not panic.
 * VERIFY3U()	- Verify unsigned X OP Y is true, if not panic.
 * VERIFY3P()	- Verify pointer X OP Y is true, if not panic.
 * VERIFY0()	- Verify value is zero, if not panic.
 */

#ifndef _SPL_DEBUG_H
#define _SPL_DEBUG_H

#include <spl-debug.h>

#ifdef NDEBUG /* Debugging Disabled */

/* Define SPL_DEBUG_STR to make clear which ASSERT definitions are used */
#define SPL_DEBUG_STR	""

#define PANIC(fmt, a...)						\
do {									\
	printk(KERN_EMERG fmt, ## a);					\
	spl_debug_bug(__FILE__, __FUNCTION__, __LINE__, 0);		\
} while (0)

#define __ASSERT(x)			((void)0)
#define ASSERT(x)			((void)0)
#define ASSERTF(x, y, z...)		((void)0)
#define ASSERTV(x)
#define VERIFY(cond)							\
do {									\
	if (unlikely(!(cond)))						\
		PANIC("VERIFY(" #cond ") failed\n");			\
} while (0)

#define VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE, FMT, CAST)			\
do {									\
	if (!((TYPE)(LEFT) OP (TYPE)(RIGHT)))				\
		PANIC("VERIFY3(" #LEFT " " #OP " " #RIGHT ") "		\
		    "failed (" FMT " " #OP " " FMT ")\n",		\
		    CAST (LEFT), CAST (RIGHT));				\
} while (0)

#define VERIFY3S(x,y,z)	VERIFY3_IMPL(x, y, z, int64_t, "%lld", (long long))
#define VERIFY3U(x,y,z)	VERIFY3_IMPL(x, y, z, uint64_t, "%llu",		\
				    (unsigned long long))
#define VERIFY3P(x,y,z)	VERIFY3_IMPL(x, y, z, uintptr_t, "%p", (void *))
#define VERIFY0(x)	VERIFY3_IMPL(0, ==, x, int64_t, "%lld",	(long long))

#define ASSERT3S(x,y,z)	((void)0)
#define ASSERT3U(x,y,z)	((void)0)
#define ASSERT3P(x,y,z)	((void)0)
#define ASSERT0(x)	((void)0)

#else /* Debugging Enabled */

/* Define SPL_DEBUG_STR to make clear which ASSERT definitions are used */
#define SPL_DEBUG_STR	" (DEBUG mode)"

#define PANIC(fmt, a...)						\
do {									\
	spl_debug_msg(NULL, 0, 0,					\
	     __FILE__, __FUNCTION__, __LINE__,	fmt, ## a);		\
	spl_debug_bug(__FILE__, __FUNCTION__, __LINE__, 0);		\
} while (0)

/* ASSERTION that is safe to use within the debug system */
#define __ASSERT(cond)							\
do {									\
	if (unlikely(!(cond))) {					\
	    printk(KERN_EMERG "ASSERTION(" #cond ") failed\n");		\
	    BUG();							\
	}								\
} while (0)

/* ASSERTION that will debug log used outside the debug sysytem */
#define ASSERT(cond)							\
do {									\
	if (unlikely(!(cond)))						\
		PANIC("ASSERTION(" #cond ") failed\n");			\
} while (0)

#define ASSERTF(cond, fmt, a...)					\
do {									\
	if (unlikely(!(cond)))						\
		PANIC("ASSERTION(" #cond ") failed: " fmt, ## a);	\
} while (0)

#define VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE, FMT, CAST)			\
do {									\
	if (!((TYPE)(LEFT) OP (TYPE)(RIGHT)))				\
		PANIC("VERIFY3(" #LEFT " " #OP " " #RIGHT ") "		\
		    "failed (" FMT " " #OP " " FMT ")\n",		\
		    CAST (LEFT), CAST (RIGHT));				\
} while (0)

#define VERIFY3S(x,y,z)	VERIFY3_IMPL(x, y, z, int64_t, "%lld", (long long))
#define VERIFY3U(x,y,z)	VERIFY3_IMPL(x, y, z, uint64_t, "%llu",		\
				    (unsigned long long))
#define VERIFY3P(x,y,z)	VERIFY3_IMPL(x, y, z, uintptr_t, "%p", (void *))
#define VERIFY0(x)	VERIFY3_IMPL(0, ==, x, int64_t, "%lld", (long long))

#define ASSERT3S(x,y,z)	VERIFY3S(x, y, z)
#define ASSERT3U(x,y,z)	VERIFY3U(x, y, z)
#define ASSERT3P(x,y,z)	VERIFY3P(x, y, z)
#define ASSERT0(x)	VERIFY0(x)

#define ASSERTV(x)	x
#define VERIFY(x)	ASSERT(x)

#endif /* NDEBUG */

/*
 * Compile-time assertion. The condition 'x' must be constant.
 */
#define	CTASSERT_GLOBAL(x)		_CTASSERT(x, __LINE__)
#define	CTASSERT(x)			{ _CTASSERT(x, __LINE__); }
#define	_CTASSERT(x, y)			__CTASSERT(x, y)
#define	__CTASSERT(x, y)			\
	typedef char __attribute__ ((unused))	\
	__compile_time_assertion__ ## y[(x) ? 1 : -1]

#endif /* SPL_DEBUG_H */
