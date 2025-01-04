// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
#include <stdarg.h>
#include <sys/types.h>

/* Workaround for non-Clang compilers */
#ifndef __has_feature
#define	__has_feature(x) 0
#endif

/* We need to workaround libspl_set_assert_ok() that we have for zdb */
#if __has_feature(attribute_analyzer_noreturn) || defined(__COVERITY__)
#define	NORETURN	__attribute__((__noreturn__))
#else
#define	NORETURN
#endif

/* Set to non-zero to avoid abort()ing on an assertion failure */
extern void libspl_set_assert_ok(boolean_t val);

/* printf version of libspl_assert */
extern void libspl_assertf(const char *file, const char *func, int line,
    const char *format, ...) NORETURN __attribute__((format(printf, 4, 5)));

static inline int
libspl_assert(const char *buf, const char *file, const char *func, int line)
{
	libspl_assertf(file, func, line, "%s", buf);
	return (0);
}

#ifdef verify
#undef verify
#endif

#define	PANIC(fmt, a...)						\
	libspl_assertf(__FILE__, __FUNCTION__, __LINE__, fmt, ## a)

#define	VERIFY(cond)							\
	(void) ((!(cond)) &&						\
	    libspl_assert(#cond, __FILE__, __FUNCTION__, __LINE__))

#define	VERIFYF(cond, STR, ...)						\
do {									\
	if (!(cond))							\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s " STR, #cond,					\
		    __VA_ARGS__);					\
} while (0)

#define	verify(cond)							\
	(void) ((!(cond)) &&						\
	    libspl_assert(#cond, __FILE__, __FUNCTION__, __LINE__))

#define	VERIFY3B(LEFT, OP, RIGHT)					\
do {									\
	const boolean_t __left = (boolean_t)(LEFT);			\
	const boolean_t __right = (boolean_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx)", #LEFT, #OP, #RIGHT,	\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right);	\
} while (0)

#define	VERIFY3S(LEFT, OP, RIGHT)					\
do {									\
	const int64_t __left = (int64_t)(LEFT);				\
	const int64_t __right = (int64_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx)", #LEFT, #OP, #RIGHT,	\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right);	\
} while (0)

#define	VERIFY3U(LEFT, OP, RIGHT)					\
do {									\
	const uint64_t __left = (uint64_t)(LEFT);			\
	const uint64_t __right = (uint64_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx)", #LEFT, #OP, #RIGHT,	\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right);	\
} while (0)

#define	VERIFY3P(LEFT, OP, RIGHT)					\
do {									\
	const uintptr_t __left = (uintptr_t)(LEFT);			\
	const uintptr_t __right = (uintptr_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (%p %s %p)", #LEFT, #OP, #RIGHT,		\
		    (void *)__left, #OP, (void *)__right);		\
} while (0)

#define	VERIFY0(LEFT)							\
do {									\
	const uint64_t __left = (uint64_t)(LEFT);			\
	if (!(__left == 0))						\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s == 0 (0x%llx == 0)", #LEFT,			\
		    (u_longlong_t)__left);				\
} while (0)

#define	VERIFY0P(LEFT)							\
do {									\
	const uintptr_t __left = (uintptr_t)(LEFT);			\
	if (!(__left == 0))						\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s == 0 (%p == 0)", #LEFT,				\
		    (void *)__left);					\
} while (0)

/*
 * This is just here because cstyle gets upset about #LEFT
 * on a newline.
 */

/* BEGIN CSTYLED */
#define	VERIFY3BF(LEFT, OP, RIGHT, STR, ...)				\
do {									\
	const boolean_t __left = (boolean_t)(LEFT);			\
	const boolean_t __right = (boolean_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx) " STR,			\
		    #LEFT, #OP, #RIGHT,					\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right,	\
		    __VA_ARGS__);					\
} while (0)

#define	VERIFY3SF(LEFT, OP, RIGHT, STR, ...)				\
do {									\
	const int64_t __left = (int64_t)(LEFT);				\
	const int64_t __right = (int64_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx) " STR,			\
		    #LEFT, #OP, #RIGHT,					\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right,	\
		    __VA_ARGS__);					\
} while (0)

#define	VERIFY3UF(LEFT, OP, RIGHT, STR, ...)				\
do {									\
	const uint64_t __left = (uint64_t)(LEFT);			\
	const uint64_t __right = (uint64_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx) " STR,			\
		    #LEFT, #OP, #RIGHT,					\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right,	\
		    __VA_ARGS__);					\
} while (0)

#define	VERIFY3PF(LEFT, OP, RIGHT, STR, ...)				\
do {									\
	const uintptr_t __left = (uintptr_t)(LEFT);			\
	const uintptr_t __right = (uintptr_t)(RIGHT);			\
	if (!(__left OP __right))					\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s %s %s (0x%llx %s 0x%llx) " STR,			\
		    #LEFT, #OP, #RIGHT,					\
		    (u_longlong_t)__left, #OP, (u_longlong_t)__right,	\
		    __VA_ARGS__);					\
} while (0)
/* END CSTYLED */

#define	VERIFY0F(LEFT, STR, ...)					\
do {									\
	const uint64_t __left = (uint64_t)(LEFT);			\
	if (!(__left == 0))						\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s == 0 (0x%llx == 0) " STR, #LEFT,		\
		    (u_longlong_t)__left, __VA_ARGS__);			\
} while (0)

#define	VERIFY0PF(LEFT, STR, ...)					\
do {									\
	const uintptr_t __left = (uintptr_t)(LEFT);			\
	if (!(__left == 0))						\
		libspl_assertf(__FILE__, __FUNCTION__, __LINE__,	\
		    "%s == 0 (%p == 0) " STR, #LEFT,			\
		    (u_longlong_t)__left, __VA_ARGS__);			\
} while (0)

#ifdef assert
#undef assert
#endif

#ifdef NDEBUG
#define	ASSERT3B(x, y, z)						\
	((void) sizeof ((uintptr_t)(x)), (void) sizeof ((uintptr_t)(z)))
#define	ASSERT3S(x, y, z)						\
	((void) sizeof ((uintptr_t)(x)), (void) sizeof ((uintptr_t)(z)))
#define	ASSERT3U(x, y, z)						\
	((void) sizeof ((uintptr_t)(x)), (void) sizeof ((uintptr_t)(z)))
#define	ASSERT3P(x, y, z)						\
	((void) sizeof ((uintptr_t)(x)), (void) sizeof ((uintptr_t)(z)))
#define	ASSERT0(x)		((void) sizeof ((uintptr_t)(x)))
#define	ASSERT0P(x)		((void) sizeof ((uintptr_t)(x)))
#define	ASSERT3BF(x, y, z, str, ...)	ASSERT3B(x, y, z)
#define	ASSERT3SF(x, y, z, str, ...)	ASSERT3S(x, y, z)
#define	ASSERT3UF(x, y, z, str, ...)	ASSERT3U(x, y, z)
#define	ASSERT3PF(x, y, z, str, ...)	ASSERT3P(x, y, z)
#define	ASSERT0P(x)		((void) sizeof ((uintptr_t)(x)))
#define	ASSERT0PF(x, str, ...)		ASSERT0P(x)
#define	ASSERT0F(x, str, ...)		ASSERT0(x)
#define	ASSERT(x)		((void) sizeof ((uintptr_t)(x)))
#define	ASSERTF(x, str, ...)	ASSERT(x)
#define	assert(x)		((void) sizeof ((uintptr_t)(x)))
#define	IMPLY(A, B)							\
	((void) sizeof ((uintptr_t)(A)), (void) sizeof ((uintptr_t)(B)))
#define	EQUIV(A, B)							\
	((void) sizeof ((uintptr_t)(A)), (void) sizeof ((uintptr_t)(B)))
#else
#define	ASSERT3B	VERIFY3B
#define	ASSERT3S	VERIFY3S
#define	ASSERT3U	VERIFY3U
#define	ASSERT3P	VERIFY3P
#define	ASSERT0		VERIFY0
#define	ASSERT0P	VERIFY0P
#define	ASSERT3BF	VERIFY3BF
#define	ASSERT3SF	VERIFY3SF
#define	ASSERT3UF	VERIFY3UF
#define	ASSERT3PF	VERIFY3PF
#define	ASSERT0PF	VERIFY0PF
#define	ASSERT0F	VERIFY0F
#define	ASSERT		VERIFY
#define	ASSERTF		VERIFYF
#define	assert		VERIFY
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
