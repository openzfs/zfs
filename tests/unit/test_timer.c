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
 * Copyright (c) 2026, Tigerblue77.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/timer.h>

#include "unit.h"

/* ========== */

/*
 * usleep_range() is a macro around nanosleep() in libspl.  nanosleep()
 * rejects a tv_nsec of a second or more with EINVAL and returns at once,
 * so any sleep of a second or longer must be split into tv_sec and tv_nsec.
 * nanosleep() never returns early without a signal, so the elapsed time
 * must be at least what was asked for.
 */

/* Sleep 'usec' microseconds and return how many nanoseconds that took. */
static hrtime_t
timed_usleep(unsigned long usec)
{
	hrtime_t start = gethrtime();
	usleep_range(usec, usec + 100);
	return (gethrtime() - start);
}

/* A sub-second sleep must sleep for at least as long as requested. */
static MunitResult
test_usleep_range_short(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	unit_ge(timed_usleep(100000), USEC2NSEC(100000));

	return (MUNIT_OK);
}

/* A sleep of more than a second must not fail with EINVAL and return. */
static MunitResult
test_usleep_range_long(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	unit_ge(timed_usleep(1100000), USEC2NSEC(1100000));

	return (MUNIT_OK);
}

static int arg_evals;

static unsigned long
counted_usec(unsigned long usec)
{
	arg_evals++;
	return (usec);
}

/* The minimum argument may have side effects, so it is evaluated once. */
static MunitResult
test_usleep_range_eval_once(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	arg_evals = 0;
	usleep_range(counted_usec(1000), 2000);
	unit_eq(arg_evals, 1);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest timer_tests[] = {
	UNIT_TEST("usleep_range_short",		test_usleep_range_short),
	UNIT_TEST("usleep_range_long",		test_usleep_range_long),
	UNIT_TEST("usleep_range_eval_once",	test_usleep_range_eval_once),
	{ 0 },
};

static const MunitSuite timer_test_suite = {
	"timer.",
	timer_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&timer_test_suite, NULL, argc, argv));
}
