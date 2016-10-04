/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include_next <assert.h>

#ifndef _LIBSPL_ASSERT_H
#define	_LIBSPL_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

static inline int
libspl_assert(const char *buf, const char *file, const char *func, int line)
{
	fprintf(stderr, "%s\n", buf);
	fprintf(stderr, "ASSERT at %s:%d:%s()", file, line, func);
	abort();
}

#ifdef verify
#undef verify
#endif

#define	VERIFY(cond)							\
	(void) ((!(cond)) &&						\
	    libspl_assert(#cond, __FILE__, __FUNCTION__, __LINE__))
#define	verify(cond)							\
	(void) ((!(cond)) &&						\
	    libspl_assert(#cond, __FILE__, __FUNCTION__, __LINE__))

#define	VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE)				\
do {									\
	const TYPE __left = (TYPE)(LEFT);				\
	const TYPE __right = (TYPE)(RIGHT);				\
	if (!(__left OP __right)) {					\
		char *__buf = alloca(256);				\
		(void) snprintf(__buf, 256,				\
		    "%s %s %s (0x%llx %s 0x%llx)", #LEFT, #OP, #RIGHT,	\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right);	\
		libspl_assert(__buf, __FILE__, __FUNCTION__, __LINE__);	\
	}								\
} while (0)

#define	VERIFY3S(x, y, z)	VERIFY3_IMPL(x, y, z, int64_t)
#define	VERIFY3U(x, y, z)	VERIFY3_IMPL(x, y, z, uint64_t)
#define	VERIFY3P(x, y, z)	VERIFY3_IMPL(x, y, z, uintptr_t)
#define	VERIFY0(x)		VERIFY3_IMPL(x, ==, 0, uint64_t)

#ifdef assert
#undef assert
#endif

/* Compile time assert */
#define	CTASSERT_GLOBAL(x)		_CTASSERT(x, __LINE__)
#define	CTASSERT(x)			{ _CTASSERT(x, __LINE__); }
#define	_CTASSERT(x, y)			__CTASSERT(x, y)
#define	__CTASSERT(x, y)						\
	typedef char __attribute__((unused))				\
	__compile_time_assertion__ ## y[(x) ? 1 : -1]

#ifdef NDEBUG
#define	ASSERT3S(x, y, z)	((void)0)
#define	ASSERT3U(x, y, z)	((void)0)
#define	ASSERT3P(x, y, z)	((void)0)
#define	ASSERT0(x)		((void)0)
#define	ASSERT(x)		((void)0)
#define	assert(x)		((void)0)
#define	ASSERTV(x)
#define	IMPLY(A, B)		((void)0)
#define	EQUIV(A, B)		((void)0)
#else
#define	ASSERT3S(x, y, z)	VERIFY3S(x, y, z)
#define	ASSERT3U(x, y, z)	VERIFY3U(x, y, z)
#define	ASSERT3P(x, y, z)	VERIFY3P(x, y, z)
#define	ASSERT0(x)		VERIFY0(x)
#define	ASSERT(x)		VERIFY(x)
#define	assert(x)		VERIFY(x)
#define	ASSERTV(x)		x
#define	IMPLY(A, B) \
	((void)(((!(A)) || (B)) || \
	    libspl_assert("(" #A ") implies (" #B ")", \
	    __FILE__, __FUNCTION__, __LINE__)))
#define	EQUIV(A, B) \
	((void)((!!(A) == !!(B)) || \
	    libspl_assert("(" #A ") is equivalent to (" #B ")", \
	    __FILE__, __FUNCTION__, __LINE__)))

#endif  /* NDEBUG */

#endif  /* _LIBSPL_ASSERT_H */
