// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the Common
 * Development and Distribution License ("CDDL"), version 1.0. You may only use
 * this file in accordance with the terms of version 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this source. A
 * copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
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
 * names, all of a module's tests run in order. -l lists the available
 * tests. -s replays a previous run's PRNG seed. -t sets the size of the
 * shared worker thread pool before any test runs.
 *
 * This subcommand is intentionally undocumented in zstream_usage() and the
 * man page; it exists to be driven by the ZFS test suite.
 */

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <libspl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_queue.h"
#include "zstream_selftest.h"

/*
 * Watchdog timeout in seconds. A test that hits this limit is almost
 * certainly deadlocked, and the watchdog converts the hang into a test
 * failure instead of a stuck test run.
 */
#define	SELFTEST_TIMEOUT_SECS	120

typedef struct {
	const char		*sm_name;
	const test_case_t	*sm_cases;
} selftest_module_t;

static const selftest_module_t modules[] = {
	{ "queue", selftest_queue_cases },
};

#define	NUM_MODULES	(sizeof (modules) / sizeof (modules[0]))

uint64_t selftest_seed;

static const char *current_test = "(startup)";

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

/*
 * SIGALRM handler; only async-signal-safe operations allowed here.
 */
static void
watchdog_fire(int sig)
{
	(void) sig;
	const char msg[] = "\nselftest: watchdog timeout in ";
	size_t len = 0;
	while (current_test[len] != '\0') {
		len++;
	}
	if (write(STDERR_FILENO, msg, sizeof (msg) - 1) < 0 ||
	    write(STDERR_FILENO, current_test, len) < 0 ||
	    write(STDERR_FILENO, "\n", 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

static void
run_case(const test_case_t *tc)
{
	(void) printf("Running %-20s ... ", tc->tc_name);
	(void) fflush(stdout);
	current_test = tc->tc_name;
	(void) alarm(SELFTEST_TIMEOUT_SECS);
	tc->tc_func();
	(void) alarm(0);
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
	uint_t nthreads = 0;
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
			if (sscanf(optarg, "%u", &nthreads) != 1 ||
			    nthreads == 0) {
				warnx("failed to parse num_threads '%s'",
				    optarg);
				selftest_usage();
			}
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

	if (!have_seed)
		random_get_pseudo_bytes((uint8_t *)&selftest_seed,
		    sizeof (selftest_seed));
	(void) printf("Using seed 0x%016jx (replay with -s 0x%jx)\n",
	    (uintmax_t)selftest_seed, (uintmax_t)selftest_seed);

	if (nthreads > 0)
		zstream_queue_set_num_threads(nthreads);

	struct sigaction sa = { .sa_handler = watchdog_fire };
	(void) sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL) != 0)
		err(1, "failed to install watchdog signal handler");

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
