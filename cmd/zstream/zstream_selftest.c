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
 * zstream selftest: in-process unit tests for zstream's internal machinery.
 *
 *   zstream selftest [-l] [-s seed] [-t nthreads] module [test ...]
 *
 * Tests are grouped into modules (see zstream_selftest.h). With no test
 * names, all of a module's tests run in order.
 *
 * -l lists the available tests
 * -s replays a previous run by using its PRNG seed
 * -t sets the size of the shared worker thread pool before any test runs
 *
 * This subcommand is intentionally undocumented in zstream_usage() and the
 * man page. It exists to be driven by the ZFS test suite.
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <libspl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/resource.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <sys/stdtypes.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_backtrace.h"
#include "zstream_queue.h"
#include "zstream_selftest.h"
#include "zstream_util.h"

#define	NUM_MODULES	(sizeof (modules) / sizeof (modules[0]))

typedef struct {
	const char		*sm_name;
	const test_case_t	*sm_cases;
} selftest_module_t;

static const selftest_module_t modules[] = {
	{ "queue",	selftest_queue_cases },
	{ "allocator",	selftest_alloc_cases },
	{ "hash",	selftest_hash_cases  },
};

uint64_t selftest_seed;

const char *
selftest_scratch_dir(void)
{
	const char *dir = getenv("ZSTREAM_SELFTEST_DIR");
	return ((dir != NULL && dir[0] != '\0') ? dir : "/var/tmp");
}

/*
 * Does the output contain any of the '|'-separated alternatives?
 */
static boolean_t
output_matches(const char *output, const char *expect)
{
	const char *alt = expect;
	while (B_TRUE) {
		const char *bar = strchr(alt, '|');
		size_t len = (bar != NULL) ? bar - alt : strlen(alt);
		for (const char *p = output; *p != '\0'; p++) {
			if (strncmp(p, alt, len) == 0)
				return (B_TRUE);
		}
		if (bar == NULL)
			return (B_FALSE);
		alt = bar + 1;
	}
}

static int64_t
monotonic_msec(void)
{
	struct timespec ts;
	VERIFY0(clock_gettime(CLOCK_MONOTONIC, &ts));
	return ((int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/*
 * Exit status for a child that can't redirect its output. It's distinct from
 * both 0 and err()'s 1 so that neither kind of test can mistake it for the
 * outcome it expects.
 */
#define	CHILD_SETUP_FAILED	125

/*
 * Common implementation of selftest_expect_exit() and
 * selftest_expect_success()
 */
static void
run_child(const char *what, selftest_child_f *fn, void *arg,
    const char *expect, boolean_t want_success)
{
	char output[4096];
	size_t len = 0;
	int fds[2];

	if (pipe(fds) != 0)
		err(1, "pipe");
	(void) fflush(NULL);
	pid_t pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid == 0) {
		/*
		 * Expected aborts shouldn't leave core files behind. On
		 * Linux, a piped core_pattern (apport, systemd-coredump)
		 * still runs at RLIMIT_CORE 0; only non-dumpable stops it.
		 */
		struct rlimit no_core = { 0, 0 };
		(void) setrlimit(RLIMIT_CORE, &no_core);
#if defined(__linux__)
		(void) prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
		(void) close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0 ||
		    dup2(fds[1], STDERR_FILENO) < 0)
			_exit(CHILD_SETUP_FAILED);
		(void) close(fds[1]);
		fn(arg);
		(void) fflush(NULL);
		_exit(0);
	}

	(void) close(fds[1]);
	struct pollfd pfd = { .fd = fds[0], .events = POLLIN };
	int64_t deadline = monotonic_msec() + EXPECT_EXIT_TIMEOUT_SECS * 1000;
	while (B_TRUE) {
		int64_t remaining = deadline - monotonic_msec();
		if (remaining <= 0) {
			(void) kill(pid, SIGKILL);
			(void) waitpid(pid, NULL, 0);
			errx(1, "%s: child still running after %d seconds",
			    what, EXPECT_EXIT_TIMEOUT_SECS);
		}
		int rc = poll(&pfd, 1, (int)remaining);
		if (rc < 0 && errno != EINTR)
			err(1, "poll");
		if (rc <= 0)
			continue;
		char chunk[512];
		ssize_t n = read(fds[0], chunk, sizeof (chunk));
		if (n < 0 && errno != EINTR)
			err(1, "read from child");
		if (n == 0)
			break;		/* EOF: the child has exited */
		if (n > 0) {
			size_t keep = MIN((size_t)n, sizeof (output) - 1 - len);
			memcpy(output + len, chunk, keep);
			len += keep;
		}
	}
	(void) close(fds[0]);
	output[len] = '\0';

	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			err(1, "waitpid");
	}

	const char *problem = NULL;
	boolean_t clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_SETUP_FAILED)
		problem = "could not redirect its output";
	else if (want_success && !clean)
		problem = "failed instead of returning normally";
	else if (!want_success && clean)
		problem = "returned normally instead of exiting";
	else if (WIFSIGNALED(status) && WTERMSIG(status) != SIGABRT)
		problem = "was killed by an unexpected signal";
	else if (expect != NULL && !output_matches(output, expect))
		problem = "did not print the expected message";
	if (problem != NULL) {
		(void) fprintf(stderr, "\n%s: child %s\n", what, problem);
		if (expect != NULL)
			(void) fprintf(stderr, "Expected: %s\n", expect);
		(void) fprintf(stderr, "Output:\n%s\n", output);
		errx(1, "%s", want_success ? "child test failed" :
		    "expected exit did not occur");
	}
}

void
selftest_expect_exit(const char *what, selftest_child_f *fn, void *arg,
    const char *expect)
{
	run_child(what, fn, arg, expect, B_FALSE);
}

void
selftest_expect_success(const char *what, selftest_child_f *fn, void *arg,
    const char *expect)
{
	run_child(what, fn, arg, expect, B_TRUE);
}

static void
selftest_usage(void)
{
	(void) fprintf(stderr,
	    "usage: zstream selftest [-l] [-s seed] [-t nthreads] "
	    "module [test ...]\n"
	    "\n"
	    "\t-l         list available tests\n"
	    "\t-s seed    seed for pseudo-random workloads (for replays)\n"
	    "\t-t num     size of the shared worker thread pool\n"
	    "\n"
	    "Available modules:");
	for (int i = 0; i < NUM_MODULES; i++)
		(void) fprintf(stderr, " %s", modules[i].sm_name);
	(void) fprintf(stderr, "\n");
	exit(1);
}

static const selftest_module_t *
find_module(const char *name)
{
	for (int i = 0; i < NUM_MODULES; i++) {
		if (strcmp(name, modules[i].sm_name) == 0)
			return (&modules[i]);
	}
	warnx("unknown module '%s'", name);
	selftest_usage();
	return (NULL);	/* NOTREACHED */
}

static void
list_tests(const selftest_module_t *module)
{
	for (int i = 0; i < NUM_MODULES; i++) {
		if (module != NULL && module != &modules[i])
			continue;
		(void) printf("%s:\n", modules[i].sm_name);
		for (const test_case_t *tc = modules[i].sm_cases;
		    tc->tc_name != NULL; tc++) {
			(void) printf("\t%s\n", tc->tc_name);
		}
	}
}

static void
run_case(const test_case_t *tc)
{
	(void) printf("Running %-22s ... ", tc->tc_name);
	(void) fflush(stdout);
	watchdog_arm();
	tc->tc_func();
	watchdog_disarm();
	(void) printf("OK\n");
}

static const test_case_t *
find_case(const selftest_module_t *module, const char *name)
{
	for (const test_case_t *tc = module->sm_cases;
	    tc->tc_name != NULL; tc++) {
		if (strcmp(name, tc->tc_name) == 0)
			return (tc);
	}
	errx(2, "module '%s' has no test named '%s' (try -l)",
	    module->sm_name, name);
	return (NULL);	/* NOTREACHED */
}

int
zstream_do_selftest(int argc, char *argv[])
{
	boolean_t list_only = B_FALSE;
	boolean_t have_seed = B_FALSE;
	int nthreads = 0;
	char *end;
	int c;

	while ((c = getopt(argc, argv, "ls:t:")) != -1) {
		switch (c) {
		case 'l':
			list_only = B_TRUE;
			break;
		case 's':
			selftest_seed = strtoull(optarg, &end, 0);
			if (*optarg == '\0' || *end != '\0') {
				warnx("failed to parse seed '%s'", optarg);
				selftest_usage();
			}
			have_seed = B_TRUE;
			break;
		case 't':
			if (sscanf(optarg, "%d", &nthreads) != 1) {
				warnx("failed to parse num_threads '%s'",
				    optarg);
				selftest_usage();
			}
			zstream_queue_set_num_threads(nthreads);
			break;
		case '?':
			warnx("invalid option '%c'", optopt);
			selftest_usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (list_only) {
		list_tests(argc > 0 ? find_module(argv[0]) : NULL);
		return (0);
	}
	if (argc < 1)
		selftest_usage();

	const selftest_module_t *module = find_module(argv[0]);

	/* Needed for random_get_pseudo_bytes() */
	libspl_init();
	watchdog_init();

	if (!have_seed)
		random_get_pseudo_bytes((uint8_t *)&selftest_seed,
		    sizeof (selftest_seed));
	(void) printf("Using seed 0x%016jx (replay with -s 0x%jx)\n",
	    (uintmax_t)selftest_seed, (uintmax_t)selftest_seed);

	int count = 0;
	if (argc == 1) {
		for (const test_case_t *tc = module->sm_cases;
		    tc->tc_name != NULL; tc++) {
			run_case(tc);
			count++;
		}
	} else {
		for (int i = 1; i < argc; i++) {
			run_case(find_case(module, argv[i]));
			count++;
		}
	}
	(void) printf("All %d %s selftest%s passed\n", count, module->sm_name,
	    count == 1 ? "" : "s");
	libspl_fini();
	return (0);
}
