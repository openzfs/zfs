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

#include <sys/arc_growth.h>
#include <pthread.h>

#include "unit.h"

static MunitResult
test_expiry(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	arc_growth_t growth = { 0 };

	unit_false(arc_growth_paused(&growth));
	arc_growth_pause(&growth, 100);
	unit_true(arc_growth_paused(&growth));
	arc_growth_resume(&growth, 99);
	unit_true(arc_growth_paused(&growth));
	arc_growth_resume(&growth, 100);
	unit_false(arc_growth_paused(&growth));
	return (MUNIT_OK);
}

static MunitResult
test_extension(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	arc_growth_t growth = { 0 };

	arc_growth_pause(&growth, 100);
	arc_growth_pause(&growth, 200);
	arc_growth_pause(&growth, 150);
	arc_growth_resume(&growth, 199);
	unit_true(arc_growth_paused(&growth));
	arc_growth_resume(&growth, 200);
	unit_false(arc_growth_paused(&growth));
	return (MUNIT_OK);
}

typedef struct growth_race {
	arc_growth_t growth;
	pthread_barrier_t start;
	pthread_barrier_t finish;
	boolean_t expire;
} growth_race_t;

static void
barrier_wait(pthread_barrier_t *barrier)
{
	int error = pthread_barrier_wait(barrier);

	VERIFY(error == 0 || error == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void *
pause_thread(void *arg)
{
	growth_race_t *race = arg;

	for (int i = 0; i < 1000; i++) {
		barrier_wait(&race->start);
		arc_growth_pause(&race->growth, 200);
		barrier_wait(&race->finish);
	}
	return (NULL);
}

static void *
update_thread(void *arg)
{
	growth_race_t *race = arg;

	for (int i = 0; i < 1000; i++) {
		barrier_wait(&race->start);
		if (race->expire)
			arc_growth_resume(&race->growth, 100);
		else
			arc_growth_pause(&race->growth, 150);
		barrier_wait(&race->finish);
	}
	return (NULL);
}

static MunitResult
test_concurrent(const MunitParameter params[], void *data)
{
	(void) data;
	growth_race_t race = { 0 };
	pthread_t pause;
	pthread_t update;
	boolean_t valid = B_TRUE;

	race.expire = strcmp(munit_parameters_get(params, "update"),
	    "expire") == 0;
	unit_ok(pthread_barrier_init(&race.start, NULL, 3));
	unit_ok(pthread_barrier_init(&race.finish, NULL, 3));
	unit_ok(pthread_create(&pause, NULL, pause_thread, &race));
	unit_ok(pthread_create(&update, NULL, update_thread, &race));
	for (int i = 0; i < 1000; i++) {
		arc_growth_pause(&race.growth, 100);
		barrier_wait(&race.start);
		barrier_wait(&race.finish);
		arc_growth_resume(&race.growth, 199);
		valid &= arc_growth_paused(&race.growth);
		arc_growth_resume(&race.growth, 200);
		valid &= !arc_growth_paused(&race.growth);
	}
	unit_ok(pthread_join(pause, NULL));
	unit_ok(pthread_join(update, NULL));
	unit_ok(pthread_barrier_destroy(&race.start));
	unit_ok(pthread_barrier_destroy(&race.finish));
	unit_true(valid);
	return (MUNIT_OK);
}

static const MunitParameterEnum update_params[] = {
	UNIT_PARAM("update", "extend", "expire"),
	{ NULL, NULL },
};

static const MunitTest growth_tests[] = {
	UNIT_TEST("expiry", test_expiry),
	UNIT_TEST("extension", test_extension),
	UNIT_TEST("concurrent", test_concurrent, update_params),
	{ 0 },
};

static const MunitSuite growth_suite = {
	"arc_growth.",
	growth_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&growth_suite, NULL, argc, argv));
}
