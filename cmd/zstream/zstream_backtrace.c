// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

/*
 * This is a watchdog timer and multithread backtrace dumper that's used by
 * zstream selftest. libspl_backtrace() does all the real work, but it can
 * only dump the current thread's stack. We need to get every thread to call
 * libspl_backtrace() in an organized sequence. However, there's no "give me
 * a list of all pthreads" function in the POSIX API.
 *
 * Rather than constructing an ad-hoc thread registry, we can approach the
 * problem by unblocking THREAD_BACKTRACE_SIGNAL (an arbitrary choice) when
 * zstream first starts. All created threads then inherit this signal mask.
 *
 * In selftest mode, we add the backtrace dumper as a handler for
 * THREAD_BACKTRACE_SIGNAL. The handler calls libspl_backtrace(), which is
 * signal-handler safe. It then signals a semaphore to indicate that the
 * current thread has finished its dump and suspends itself.
 *
 * The watchdog supervisor is a separate thread that sigwait()s for SIGALRM.
 * It blocks THREAD_BACKTRACE_SIGNAL and runs its own libspl_backtrace(). It
 * then enters a loop in which it sends THREAD_BACKTRACE_SIGNAL to the
 * process as a whole and runs sem_timedwait() to see if any thread woke up
 * and dumped its backtrace. If that call times out, either the receiving
 * thread wedged while trying to backtrace or there were no more threads to
 * backtrace.
 *
 * These two scenarios can be distinguished by calling sigpending(). If
 * THREAD_BACKTRACE_SIGNAL still shows as being pending on the process, then
 * there was no thread to receive it and we are done. If there's no pending
 * signal, then some thread did receive the signal but failed to post to the
 * semaphore; we print a "thread wedged while backtracing" message and
 * continue the loop.
 */

#include <err.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/backtrace.h>
#include <sys/debug.h>
#include <sys/stdtypes.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_backtrace.h"
#include "zstream_util.h"

/*
 * Watchdog timeout in seconds. A test that hits this limit is almost
 * certainly deadlocked, and the watchdog converts the hang into a test
 * failure instead of a stuck test run.
 */
#define	WATCHDOG_TIMEOUT_SECS	120
#define	MAX_SECS_FOR_BACKTRACE	2

static sem_t sem_thread_bt_complete;	/* thread -> watchdog */

/*
 * Signal handler for THREAD_BACKTRACE_SIGNAL, run by all threads except the
 * watchdog thread
 */
static void
backtrace_self(int signal)
{
	(void) signal;
	ssize_t dummy __maybe_unused = write(STDERR_FILENO, "\n", 1);
	libspl_backtrace(STDERR_FILENO);
	sem_post(&sem_thread_bt_complete);

	sigset_t mask;
	sigfillset(&mask);
	sigsuspend(&mask);
}

static void
backtrace_all_threads(void)
{
	while (B_TRUE) {
		if (kill(getpid(), THREAD_BACKTRACE_SIGNAL) != 0)
			err(1, "failed to send thread backtrace signal");
		struct timespec deadline;
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += MAX_SECS_FOR_BACKTRACE;
		if (sem_timedwait(&sem_thread_bt_complete, &deadline) != 0) {
			sigset_t pending;
			if (sigpending(&pending) != 0)
				err(1, "sigpending failed");
			if (sigismember(&pending, THREAD_BACKTRACE_SIGNAL)) {
				return;
			} else {
				warnx("a thread failed to generate a backtrace,"
				    " continuing...");
			}
		}
	}
}

/*
 * Body of the watchdog thread
 */
static void *
watchdog(void *nope)
{
	(void) nope;
	int signal;
	sigset_t bt_mask, dog_mask;

	/*
	 * This thread does its own backtrace, so we block the
	 * backtrace signal.
	 */
	sigemptyset(&bt_mask);
	sigaddset(&bt_mask, THREAD_BACKTRACE_SIGNAL);
	if (pthread_sigmask(SIG_BLOCK, &bt_mask, NULL) != 0)
		err(1, "pthread_sigmask failed");

	sigemptyset(&dog_mask);
	sigaddset(&dog_mask, WATCHDOG_SIGNAL);

	int rc = sigwait(&dog_mask, &signal);
	if (rc != 0) {
		errno = rc;
		err(1, "watchdog sigwait failed");
	} else if (signal != WATCHDOG_SIGNAL) {
		errx(1, "unexpected signal %d received by watchdog", signal);
	}

	fprintf(stderr, "\n\nWATCHDOG TIMER EXPIRED\n"
	    "Dumping backtrace for all threads...\n\n");
	fflush(stderr);
	libspl_backtrace(STDERR_FILENO);
	backtrace_all_threads();
	fprintf(stderr, "\nAll threads backtraced, exiting.\n");
	exit(1);
}

void
watchdog_init(void)
{
	if (sem_init(&sem_thread_bt_complete, 0, 0) != 0)
		err(1, "watchdog sem_init failed");

	safe_create_thread(watchdog, NULL, "watchdog", B_TRUE);

	struct sigaction sa = {
		.sa_handler = backtrace_self,
		.sa_flags = SA_RESTART
	};
	if (sigaction(THREAD_BACKTRACE_SIGNAL, &sa, NULL) != 0)
		err(1, "backtrace sigaction failed");
}

void
watchdog_arm(void)
{
	(void) alarm(WATCHDOG_TIMEOUT_SECS);
}

void
watchdog_disarm(void)
{
	(void) alarm(0);
}
