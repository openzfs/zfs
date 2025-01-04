// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
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
 * ASSERT0P()	- Assert pointer is null, if not panic.
 * VERIFY()	- Verify X is true, if not panic.
 * VERIFY3B()	- Verify boolean X OP Y is true, if not panic.
 * VERIFY3S()	- Verify signed X OP Y is true, if not panic.
 * VERIFY3U()	- Verify unsigned X OP Y is true, if not panic.
 * VERIFY3P()	- Verify pointer X OP Y is true, if not panic.
 * VERIFY0()	- Verify value is zero, if not panic.
 * VERIFY0P()	- Verify pointer is null, if not panic.
 */

#ifndef _SPL_DEBUG_H
#define	_SPL_DEBUG_H


/*
 * Common DEBUG functionality.
 */
#ifdef __FreeBSD__
#include <linux/compiler.h>
#endif

#ifndef __printflike
#define	__printflike(a, b)	__printf(a, b)
#endif

#ifndef __maybe_unused
#define	__maybe_unused __attribute__((unused))
#endif

/*
 * Without this, we see warnings from objtool during normal Linux builds when
 * the kernel is built with CONFIG_STACK_VALIDATION=y:
 *
 * warning: objtool: tsd_create() falls through to next function __list_add()
 * warning: objtool: .text: unexpected end of section
 *
 * Until the toolchain stops doing this, we must only define this attribute on
 * spl_panic() when doing static analysis.
 */
#if defined(__COVERITY__) || defined(__clang_analyzer__)
__attribute__((__noreturn__))
#endif
extern void spl_panic(const char *file, const char *func, int line,
    const char *fmt, ...);
extern void spl_dumpstack(void);

static inline int
spl_assert(const char *buf, const char *file, const char *func, int line)
{
	spl_panic(file, func, line, "%s", buf);
	return (0);
}

#ifndef expect
#define	expect(expr, value) (__builtin_expect((expr), (value)))
#endif

#define	PANIC(fmt, a...)						\
	spl_panic(__FILE__, __FUNCTION__, __LINE__, fmt, ## a)

#define	VERIFY(cond)							\
	(void) (unlikely(!(cond)) &&					\
	    spl_assert("VERIFY(" #cond ") failed\n",			\
	    __FILE__, __FUNCTION__, __LINE__))

#define	VERIFYF(cond, str, ...)		do {				\
		if (unlikely(!(cond)))					\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY(" #cond ") failed " str "\n", __VA_ARGS__);\
	} while (0)

#define	VERIFY3B(LEFT, OP, RIGHT)	do {				\
		const boolean_t _verify3_left = (boolean_t)(LEFT);	\
		const boolean_t _verify3_right = (boolean_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%d " #OP " %d)\n",				\
		    (boolean_t)_verify3_left,				\
		    (boolean_t)_verify3_right);				\
	} while (0)

#define	VERIFY3S(LEFT, OP, RIGHT)	do {				\
		const int64_t _verify3_left = (int64_t)(LEFT);		\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%lld " #OP " %lld)\n",			\
		    (long long)_verify3_left,				\
		    (long long)_verify3_right);				\
	} while (0)

#define	VERIFY3U(LEFT, OP, RIGHT)	do {				\
		const uint64_t _verify3_left = (uint64_t)(LEFT);	\
		const uint64_t _verify3_right = (uint64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%llu " #OP " %llu)\n",			\
		    (unsigned long long)_verify3_left,			\
		    (unsigned long long)_verify3_right);		\
	} while (0)

#define	VERIFY3P(LEFT, OP, RIGHT)	do {				\
		const uintptr_t _verify3_left = (uintptr_t)(LEFT);	\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%px " #OP " %px)\n",			\
		    (void *)_verify3_left,				\
		    (void *)_verify3_right);				\
	} while (0)

#define	VERIFY0(RIGHT)	do {						\
		const int64_t _verify0_right = (int64_t)(RIGHT);	\
		if (unlikely(!(0 == _verify0_right)))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0(" #RIGHT ") "				\
		    "failed (0 == %lld)\n",				\
		    (long long)_verify0_right);				\
	} while (0)

#define	VERIFY0P(RIGHT)	do {						\
		const uintptr_t _verify0_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(0 == _verify0_right)))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0P(" #RIGHT ") "				\
		    "failed (NULL == %px)\n",				\
		    (void *)_verify0_right);				\
	} while (0)

/*
 * Note that you should not put any operations you want to always happen
 * in the print section for ASSERTs unless you only want them to run on
 * debug builds!
 * e.g. ASSERT3UF(2, <, 3, "%s", foo(x)), foo(x) won't run on non-debug
 * builds.
 */

#define	VERIFY3BF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const boolean_t _verify3_left = (boolean_t)(LEFT);	\
		const boolean_t _verify3_right = (boolean_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%d " #OP " %d) " STR "\n",			\
		    (boolean_t)(_verify3_left),				\
		    (boolean_t)(_verify3_right),			\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY3SF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const int64_t _verify3_left = (int64_t)(LEFT);		\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%lld " #OP " %lld) " STR "\n",		\
		    (long long)(_verify3_left),				\
		    (long long)(_verify3_right),			\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY3UF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const uint64_t _verify3_left = (uint64_t)(LEFT);	\
		const uint64_t _verify3_right = (uint64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%llu " #OP " %llu) " STR "\n",		\
		    (unsigned long long)(_verify3_left),		\
		    (unsigned long long)(_verify3_right),		\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY3PF(LEFT, OP, RIGHT, STR, ...)	do {			\
		const uintptr_t _verify3_left = (uintptr_t)(LEFT);	\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(_verify3_left OP _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%px " #OP " %px) " STR "\n",		\
		    (void *) (_verify3_left),				\
		    (void *) (_verify3_right),				\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY0PF(RIGHT, STR, ...)	do {				\
		const uintptr_t _verify3_left = (uintptr_t)(0);		\
		const uintptr_t _verify3_right = (uintptr_t)(RIGHT);	\
		if (unlikely(!(_verify3_left == _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0(0 == " #RIGHT ") "				\
		    "failed (0 == %px) " STR "\n",			\
		    (long long) (_verify3_right),			\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY0F(RIGHT, STR, ...)	do {				\
		const int64_t _verify3_left = (int64_t)(0);		\
		const int64_t _verify3_right = (int64_t)(RIGHT);	\
		if (unlikely(!(_verify3_left == _verify3_right)))	\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY0(0 == " #RIGHT ") "				\
		    "failed (0 == %lld) " STR "\n",			\
		    (long long) (_verify3_right),			\
		    __VA_ARGS__);					\
	} while (0)

#define	VERIFY_IMPLY(A, B) \
	((void)(likely((!(A)) || (B)) ||				\
	    spl_assert("(" #A ") implies (" #B ")",			\
	    __FILE__, __FUNCTION__, __LINE__)))

#define	VERIFY_EQUIV(A, B) \
	((void)(likely(!!(A) == !!(B)) || 				\
	    spl_assert("(" #A ") is equivalent to (" #B ")",		\
	    __FILE__, __FUNCTION__, __LINE__)))

/*
 * Debugging disabled (--disable-debug)
 */
#ifdef NDEBUG

#define	ASSERT(x)		((void) sizeof ((uintptr_t)(x)))
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
#define	ASSERT0PF(x, str, ...)		ASSERT0P(x)
#define	ASSERT0F(x, str, ...)		ASSERT0(x)
#define	ASSERTF(x, str, ...)		ASSERT(x)
#define	IMPLY(A, B)							\
	((void) sizeof ((uintptr_t)(A)), (void) sizeof ((uintptr_t)(B)))
#define	EQUIV(A, B)		\
	((void) sizeof ((uintptr_t)(A)), (void) sizeof ((uintptr_t)(B)))

/*
 * Debugging enabled (--enable-debug)
 */
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
#define	ASSERTF		VERIFYF
#define	ASSERT		VERIFY
#define	IMPLY		VERIFY_IMPLY
#define	EQUIV		VERIFY_EQUIV

#endif /* NDEBUG */

#endif /* SPL_DEBUG_H */
