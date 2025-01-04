// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 */

#include <assert.h>
#include <pthread.h>
#include <sys/backtrace.h>

#if defined(__linux__)
#include <errno.h>
#include <sys/prctl.h>
#ifdef HAVE_GETTID
#define	libspl_gettid()		gettid()
#else
#include <sys/syscall.h>
#define	libspl_gettid()		((pid_t)syscall(__NR_gettid))
#endif
#define	libspl_getprogname()	(program_invocation_short_name)
#define	libspl_getthreadname(buf, len)	\
	prctl(PR_GET_NAME, (unsigned long)(buf), 0, 0, 0)
#elif defined(__FreeBSD__) || defined(__APPLE__)
#if !defined(__APPLE__)
#include <pthread_np.h>
#define	libspl_gettid()		pthread_getthreadid_np()
#endif
#define	libspl_getprogname()	getprogname()
#define	libspl_getthreadname(buf, len)	\
	pthread_getname_np(pthread_self(), buf, len);
#endif

#if defined(__APPLE__)
static inline uint64_t
libspl_gettid(void)
{
	uint64_t tid;

	if (pthread_threadid_np(NULL, &tid) != 0)
		tid = 0;

	return (tid);
}
#endif

static boolean_t libspl_assert_ok = B_FALSE;

void
libspl_set_assert_ok(boolean_t val)
{
	libspl_assert_ok = val;
}

static pthread_mutex_t assert_lock = PTHREAD_MUTEX_INITIALIZER;

/* printf version of libspl_assert */
void
libspl_assertf(const char *file, const char *func, int line,
    const char *format, ...)
{
	pthread_mutex_lock(&assert_lock);

	va_list args;
	char tname[64];

	libspl_getthreadname(tname, sizeof (tname));

	fprintf(stderr, "ASSERT at %s:%d:%s()\n", file, line, func);

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n"
	    "  PID: %-8u  COMM: %s\n"
#if defined(__APPLE__)
	    "  TID: %-8" PRIu64 "  NAME: %s\n",
#else
	    "  TID: %-8u  NAME: %s\n",
#endif
	    getpid(), libspl_getprogname(),
	    libspl_gettid(), tname);

	libspl_backtrace(STDERR_FILENO);

#if !__has_feature(attribute_analyzer_noreturn) && !defined(__COVERITY__)
	if (libspl_assert_ok) {
		pthread_mutex_unlock(&assert_lock);
		return;
	}
#endif
	abort();
}
