/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2013 David Hoeppner.  All rights reserved.
 */

/*
 * Queue maximum number of signals and test if we can queue more signals then
 * allowed.
 */

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define	SIGQUEUE_SIGNAL		SIGRTMIN	/* Signal used for testing */

int nreceived = 0;

static void
test_start(const char *test_name, const char *format, ...)
{
	va_list args;

	(void) printf("TEST STARTING %s: ", test_name);

	va_start(args, format);
	(void) vprintf(format, args);
	va_end(args);
	(void) fflush(stdout);
}

static void
test_failed(const char *test_name, const char *format, ...)
{
	va_list args;

	(void) printf("TEST FAILED %s: ", test_name);

	va_start(args, format);
	(void) vprintf(format, args);
	va_end(args);

	(void) exit(-1);
}

static void
test_passed(const char *test_name)
{
	(void) printf("TEST PASS: %s\n", test_name);
	(void) fflush(stdout);
}

/* ARGSUSED */
static void
maximum_test_handler(int signal, siginfo_t *siginfo, void *context)
{
	nreceived++;
}

static void
sigqueue_maximum_test(void)
{
	const char *test_name = __func__;
	struct sigaction action;
	long sigqueue_max, i;
	pid_t pid;
	union sigval value;
	int error;

	test_start(test_name, "queue maximum number of signals\n");

	/*
	 * Get the maximum size of the queue.
	 */
	sigqueue_max = sysconf(_SC_SIGQUEUE_MAX);
	if (sigqueue_max == -1) {
		test_failed(test_name, "sysconf\n");
	}

	/*
	 * Put the signal on hold.
	 */
	error = sighold(SIGQUEUE_SIGNAL);
	if (error == -1) {
		test_failed(test_name, "sighold\n");
	}

	pid = getpid();
	value.sival_int = 0;

	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = maximum_test_handler;

	error = sigemptyset(&action.sa_mask);
	if (error == -1) {
		test_failed(test_name, "sigemptyset\n");
	}

	/*
	 * Set signal handler.
	 */
	error = sigaction(SIGQUEUE_SIGNAL, &action, 0);
	if (error == -1) {
		test_failed(test_name, "sigaction\n");
	}

	/*
	 * Fill the signal queue to the maximum.
	 */
	for (i = 0; i < sigqueue_max; i++) {
		error = sigqueue(pid, SIGQUEUE_SIGNAL, value);
		if (error == -1) {
			test_failed(test_name, "sigqueue\n");
		}
	}

	/*
	 * Send a further signal and test if we get the expected
	 * error.
	 */
	error = sigqueue(pid, SIGQUEUE_SIGNAL, value);
	if (error != -1) {
		test_failed(test_name, "sigqueue\n");
	}

	/*
	 * Unblock the signals and check if we received all messages
	 * from the signal queue.
	 */
	error = sigrelse(SIGQUEUE_SIGNAL);
	if (error == -1) {
		test_failed(test_name, "sigrelse\n");
	}

	if (nreceived != sigqueue_max) {
		test_failed(test_name, "nreceived != sigqueue_max\n");
	}

	test_passed(test_name);
}

static void
run_tests(void)
{
	sigqueue_maximum_test();
}

/* ARGSUSED */
int
main(int argc, char *argv[])
{
	run_tests();

	return (EXIT_SUCCESS);
}
