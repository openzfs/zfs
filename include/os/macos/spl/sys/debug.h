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
 * VERIFY()	- Verify X is true, if not panic.
 * VERIFY3B()	- Verify boolean X OP Y is true, if not panic.
 * VERIFY3S()	- Verify signed X OP Y is true, if not panic.
 * VERIFY3U()	- Verify unsigned X OP Y is true, if not panic.
 * VERIFY3P()	- Verify pointer X OP Y is true, if not panic.
 * VERIFY0()	- Verify value is zero, if not panic.
 */

#ifndef _SPL_DEBUG_H
#define	_SPL_DEBUG_H

#include <sys/types.h>

/* SPL has own 'dprintf' as zfs_debug.c version uses mutex */
#ifdef  __cplusplus
extern "C" {
#endif

extern int zfs_flags;

/* Simple dprintf for SPL only */
#ifndef dprintf
#define	dprintf(...)   \
	if (zfs_flags & 1) \
		printf(__VA_ARGS__)
#endif

#ifndef __printflike
#define	__printflike(a, b)	__attribute__((__format__(__printf__, a, b)))
#endif

/*
 * Common DEBUG functionality.
 */
int spl_panic(const char *file, const char *func, int line,
    const char *fmt, ...);
void spl_dumpstack(void);

void spl_backtrace(char *thesignal);
int getpcstack(uintptr_t *pcstack, int pcstack_limit);
void print_symbol(uintptr_t symbol);

#ifndef expect
#define	expect(expr, value) (__builtin_expect((expr), (value)))
#endif
#define	likely(x)		__builtin_expect(!!(x), 1)
#define	unlikely(x)		__builtin_expect(!!(x), 0)

#ifndef	__maybe_unused
#define	__maybe_unused	__attribute__((unused))
#endif

/* BEGIN CSTYLED */
#define	PANIC(fmt, a...)						\
	spl_panic(__FILE__, __FUNCTION__, __LINE__, fmt, ## a)

#define	VERIFY(cond)							\
	(void) (unlikely(!(cond)) &&					\
	    spl_panic(__FILE__, __FUNCTION__, __LINE__,			\
	    "%s", "VERIFY(" #cond ") failed\n"))

#define	VERIFY3B(LEFT, OP, RIGHT)	do {				\
		boolean_t _verify3_left = (boolean_t)(LEFT);		\
		boolean_t _verify3_right = (boolean_t)(RIGHT);		\
		if (!(_verify3_left OP _verify3_right))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%d " #OP " %d)\n",				\
		    (boolean_t) (_verify3_left),			\
		    (boolean_t) (_verify3_right));			\
	} while (0)

#define	VERIFY3S(LEFT, OP, RIGHT)	do {				\
		int64_t _verify3_left = (int64_t)(LEFT);		\
		int64_t _verify3_right = (int64_t)(RIGHT);		\
		if (!(_verify3_left OP _verify3_right))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%lld " #OP " %lld)\n",			\
		    (long long) (_verify3_left),			\
		    (long long) (_verify3_right));			\
	} while (0)

#define	VERIFY3U(LEFT, OP, RIGHT)	do {				\
		uint64_t _verify3_left = (uint64_t)(LEFT);		\
		uint64_t _verify3_right = (uint64_t)(RIGHT);		\
		if (!(_verify3_left OP _verify3_right))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%llu " #OP " %llu)\n",			\
		    (unsigned long long) (_verify3_left),		\
		    (unsigned long long) (_verify3_right));		\
	} while (0)

#define	VERIFY3P(LEFT, OP, RIGHT)	do {				\
		uintptr_t _verify3_left = (uintptr_t)(LEFT);		\
		uintptr_t _verify3_right = (uintptr_t)(RIGHT);		\
		if (!(_verify3_left OP _verify3_right))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(" #LEFT " "  #OP " "  #RIGHT ") "		\
		    "failed (%px " #OP " %px)\n",			\
		    (void *) (_verify3_left),				\
		    (void *) (_verify3_right));				\
	} while (0)

#define	VERIFY0(RIGHT)	do {				\
		int64_t _verify3_left = (int64_t)(0);		\
		int64_t _verify3_right = (int64_t)(RIGHT);		\
		if (!(_verify3_left == _verify3_right))			\
		    spl_panic(__FILE__, __FUNCTION__, __LINE__,		\
		    "VERIFY3(0 == " #RIGHT ") "				\
		    "failed (0 == %lld)\n",				\
		    (long long) (_verify3_right));			\
	} while (0)

#define	CTASSERT_GLOBAL(x)		_CTASSERT(x, __LINE__)
#define	CTASSERT(x)				{ _CTASSERT(x, __LINE__); }
#define	_CTASSERT(x, y)			__CTASSERT(x, y)
#define	__CTASSERT(x, y)											\
	typedef char __attribute__ ((unused))                           \
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
#define	ASSERTV(x)		((void)0)
#define	IMPLY(A, B)		((void)0)
#define	EQUIV(A, B)		((void)0)

/*
 * Debugging enabled (--enable-debug)
 */
#else

#ifdef MACOS_ASSERT_SHOULD_PANIC
#define	ASSERT3B	VERIFY3B
#define	ASSERT3S	VERIFY3S
#define	ASSERT3U	VERIFY3U
#define	ASSERT3P	VERIFY3P
#define	ASSERT0		VERIFY0
#define	ASSERT		VERIFY
#define	ASSERTV(X)	X
#define	IMPLY(A, B) \
	((void)(((!(A)) || (B)) || \
	    spl_panic(__FILE__, __FUNCTION__, __LINE__, \
	    "(" #A ") implies (" #B ")")))
#define	EQUIV(A, B) \
	((void)((!!(A) == !!(B)) || \
	    spl_panic(__FILE__, __FUNCTION__, __LINE__, \
	    "(" #A ") is equivalent to (" #B ")")))
/* END CSTYLED */
#else /* MACOS_ASSERT_SHOULD_PANIC */

#define	PRINT printf

__attribute__((noinline)) int assfail(const char *str, const char *file,
    unsigned int line) __attribute__((optnone));

#define	ASSERT(cond)							\
	(void) (unlikely(!(cond)) && assfail(#cond, __FILE__, __LINE__) && \
	    PRINT("ZFS: %s %s %d : %s\n", __FILE__, __FUNCTION__, __LINE__, \
		"ASSERTION(" #cond ") failed\n"))

#define	ASSERT3_IMPL(LEFT, OP, RIGHT, TYPE, FMT, CAST)			\
	do {								\
		if (!((TYPE)(LEFT) OP(TYPE)(RIGHT)) &&	\
		    assfail(#LEFT #OP #RIGHT, __FILE__, __LINE__))	\
			PRINT("ZFS: %s %s %d : ASSERT3( %s " #OP " %s) " \
			    "failed (" FMT " " #OP " " FMT ")\n",	\
			    __FILE__, __FUNCTION__, \
				__LINE__, #LEFT, #RIGHT,	\
			    CAST(LEFT), CAST(RIGHT));			\
	} while (0)


#define	ASSERTF(cond, fmt, a...)					\
	do {								\
		if (unlikely(!(cond)))					\
			panic("ZFS: ASSERTION(" #cond ") failed: " fmt, ## a); \
	} while (0)


#define	ASSERT3B(x, y, z) ASSERT3_IMPL(x, y, z, int64_t, "%u", \
		(boolean_t))
#define	ASSERT3S(x, y, z) ASSERT3_IMPL(x, y, z, int64_t, "%lld", \
		(long long))
#define	ASSERT3U(x, y, z) ASSERT3_IMPL(x, y, z, uint64_t, "%llu", \
		(unsigned long long))

#define	ASSERT3P(x, y, z) ASSERT3_IMPL(x, y, z, uintptr_t, "%p", (void *))
#define	ASSERT0(x)	ASSERT3_IMPL(0, ==, x, int64_t, "%lld", (long long))
#define	ASSERTV(x)	x


/*
 * IMPLY and EQUIV are assertions of the form:
 *
 *      if (a) then (b)
 * and
 *      if (a) then (b) *AND* if (b) then (a)
 */
#define	IMPLY(A, B)						\
	((void)(((!(A)) || (B)) ||				\
	    printf("%s:%d (" #A ") implies (" #B "): failed\n",	\
		__FILE__, __LINE__)))

#define	EQUIV(A, B)							\
	((void)((!!(A) == !!(B)) ||					\
	    printf("%s:%d (" #A ") is equivalent to (" #B "): failed\n", \
		__FILE__, __LINE__)))

#endif /* MACOS_ASSERT_SHOULD_PANIC */
#endif /* NDEBUG */

#define	zfs_fallthrough		__attribute__((__fallthrough__))

#ifdef  __cplusplus
}
#endif

#endif /* SPL_DEBUG_H */
