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

/*
 * Available Solaris debug functions.  All of the ASSERT() macros will be
 * compiled out when NDEBUG is defined, this is the default behavior for
 * the SPL.  To enable assertions use the --enable-debug with configure.
 * The VERIFY() functions are never compiled out and cannot be disabled.
 *
 * PANIC()	- Panic the node and print message.
 * ASSERT()	- Assert X is true, if not panic.
 * ASSERT3B()	- Assert boolean X OP Y is true, if not panic.
 * ASSERT3S()	- Assert signed X OP Y is true, if not panic.
 * ASSERT3U()	- Assert unsigned X OP Y is true, if not panic.
 * ASSERT3P()	- Assert pointer X OP Y is true, if not panic.
 * ASSERT0()	- Assert value is zero, if not panic.
 * VERIFY()	- Verify X is true, if not panic.
 * VERIFY3B()	- Verify boolean X OP Y is true, if not panic.
 * VERIFY3S()	- Verify signed X OP Y is true, if not panic.
 * VERIFY3U()	- Verify unsigned X OP Y is true, if not panic.
 * VERIFY3P()	- Verify pointer X OP Y is true, if not panic.
 * VERIFY0()	- Verify value is zero, if not panic.
 */

#ifndef _SPL_DEBUG_H
#define	_SPL_DEBUG_H

/*
 * Common DEBUG functionality.
 */
#define	__printflike(a, b)	__printf(a, b)

#ifndef __maybe_unused
#define	__maybe_unused __attribute__((unused))
#endif

int spl_panic(const char *file, const char *func, int line,
    const char *fmt, ...);
void spl_dumpstack(void);

/* BEGIN CSTYLED */
#define	PANIC(fmt, a...)						\
	spl_panic(__FILE__, __FUNCTION__, __LINE__, fmt, ## a)

#define	VERIFY(cond)										\
	(void) (unlikely(!(cond)) &&							\
	    spl_panic(__FILE__, __FUNCTION__, __LINE__,			\
	    "%s", "VERIFY(" #cond ") failed\n"))

#define	VERIFY3B(LEFT, OP, RIGHT)	do {					\
		const boolean_t _verify3_left = (boolean_t)(LEFT);	\
		const boolean_t _verify3_right = (boolean_t)(RIGHT);\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%d " #OP " %d)\n",						\
		    (boolean_t) (_verify3_left),					\
		    (boolean_t) (_verify3_right));					\
	} while (0)

#define	VERIFY3S(LEFT, OP, RIGHT)	do {					\
		const int64_t _verify3_left = (int64_t)(LEFT);		\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%lld " #OP " %lld)\n",					\
		    (long long) (_verify3_left),					\
		    (long long) (_verify3_right));					\
	} while (0)

#define	VERIFY3U(LEFT, OP, RIGHT)	do {					\
		const uint64_t _verify3_left = (uint64_t)(LEFT);	\
		const uint64_t _verify3_right = (uint64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%llu " #OP " %llu)\n",					\
		    (unsigned long long) (_verify3_left),			\
		    (unsigned long long) (_verify3_right));			\
	} while (0)

#define	VERIFY3P(LEFT, OP, RIGHT)	do {					\
		const uintptr_t _verify3_left = (uintptr_t)(LEFT);	\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%px " #OP " %px)\n",					\
		    (void *) (_verify3_left),						\
		    (void *) (_verify3_right));						\
	} while (0)

#define	VERIFY0(RIGHT)	do {								\
		const int64_t _verify3_left = (int64_t)(0);			\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left == _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(0 == " #RIGHT ") "						\
		    "failed (0 == %lld)\n",							\
		    (long long) (_verify3_right));					\
	} while (0)

#define	CTASSERT_GLOBAL(x)		_CTASSERT(x, __LINE__)
#define	CTASSERT(x)			{ _CTASSERT(x, __LINE__); }
#define	_CTASSERT(x, y)			__CTASSERT(x, y)
#define	__CTASSERT(x, y)						\
	typedef char __attribute__ ((unused))				\
	__compile_time_assertion__ ## y[(x) ? 1 : -1]

/*
 * Debugging disabled (--disable-debug)
 */
#ifdef NDEBUG

#define	ASSERT(x)		((void)0)
#define	ASSERT3B(x,y,z)		((void)0)
#define	ASSERT3S(x,y,z)		((void)0)
#define	ASSERT3U(x,y,z)		((void)0)
#define	ASSERT3P(x,y,z)		((void)0)
#define	ASSERT0(x)		((void)0)
#define	IMPLY(A, B)		((void)0)
#define	EQUIV(A, B)		((void)0)

/*
 * Debugging enabled (--enable-debug)
 */
#else

#define	ASSERT3B	VERIFY3B
#define	ASSERT3S	VERIFY3S
#define	ASSERT3U	VERIFY3U
#define	ASSERT3P	VERIFY3P
#define	ASSERT0		VERIFY0
#define	ASSERT		VERIFY
#define	IMPLY(A, B) \
	((void)(likely((!(A)) || (B)) || \
	    spl_panic(__FILE__, __FUNCTION__, __LINE__, \
	    "(" #A ") implies (" #B ")")))
#define	EQUIV(A, B) \
	((void)(likely(!!(A) == !!(B)) || \
	    spl_panic(__FILE__, __FUNCTION__, __LINE__, \
	    "(" #A ") is equivalent to (" #B ")")))
/* END CSTYLED */

#endif /* NDEBUG */

#endif /* SPL_DEBUG_H */
