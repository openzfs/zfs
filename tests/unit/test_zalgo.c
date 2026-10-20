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
 * Copyright (c) 2026, TrueNAS.
 */

#include "unit.h"

#include <sys/zalgo.h>

static MunitResult
test_zalgo_register(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const zalgo_dummy_ops_t ops1 = (void *)(uintptr_t)42;
	const zalgo_dummy_ops_t ops2 = (void *)(uintptr_t)84;

	/* registration works */
	unit_ok(zalgo_dummy_register(ZG_DUMMY_0,
	    "ops1", "test dummy ops #1", &ops1));

	/* can't register same thing twice */
	unit_err(zalgo_dummy_register(ZG_DUMMY_0,
	    "ops1", "test dummy ops #1", &ops1), EEXIST);

	/* ops can be reused with different subtype */
	unit_ok(zalgo_dummy_register(ZG_DUMMY_1,
	    "ops1", "test dummy ops #1", &ops1));

	/* new ops can be used with existing subtype */
	unit_ok(zalgo_dummy_register(ZG_DUMMY_0,
	    "ops2", "test dummy ops #2", &ops2));

	return (MUNIT_OK);
}

static MunitResult
test_zalgo_hold(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const zalgo_dummy_ops_t ops1 = (void *)(uintptr_t)42;
	const zalgo_dummy_ops_t ops2 = (void *)(uintptr_t)84;

	/* register ops for subtype 0, and two ops for subtype 1 */
	unit_ok(zalgo_dummy_register(ZG_DUMMY_1,
	    "ops2", "test dummy ops #2", &ops2));
	unit_ok(zalgo_dummy_register(ZG_DUMMY_0,
	    "ops1", "test dummy ops #1", &ops1));
	unit_ok(zalgo_dummy_register(ZG_DUMMY_1,
	    "ops1", "test dummy ops #1", &ops1));

	zalgo_dummy_hold_t *hold;

	/* subtype 0 returns only the one we registered */
	hold = zalgo_dummy_hold(ZG_DUMMY_0);
	unit_ptr_eq(zalgo_dummy_ops(hold), &ops1);
	zalgo_dummy_rele(hold);

	/* subtype 1 returns either */
	hold = zalgo_dummy_hold(ZG_DUMMY_1);
	unit_true(zalgo_dummy_ops(hold) == &ops1 ||
	    zalgo_dummy_ops(hold) == &ops2);
	zalgo_dummy_rele(hold);

	/* none registered for subtype 2 */
	unit_ptr_null(zalgo_dummy_hold(ZG_DUMMY_2));

	return (MUNIT_OK);
}

static MunitResult
test_zalgo_select(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const zalgo_dummy_ops_t ops1 = (void *)(uintptr_t)42;
	const zalgo_dummy_ops_t ops2 = (void *)(uintptr_t)84;

	/* register two for subtype 0 */
	unit_ok(zalgo_dummy_register(ZG_DUMMY_0,
	    "ops1", "test dummy ops #1", &ops1));
	unit_ok(zalgo_dummy_register(ZG_DUMMY_0,
	    "ops2", "test dummy ops #2", &ops2));

	/* can't select an id that hasn't been registered */
	unit_err(zalgo_dummy_select(ZG_DUMMY_0, "opsX"), ENOENT);

	zalgo_dummy_hold_t *hold;

	/* selecting an id then hold yields those ops */
	unit_ok(zalgo_dummy_select(ZG_DUMMY_0, "ops1"));
	hold = zalgo_dummy_hold(ZG_DUMMY_0);
	unit_ptr_eq(zalgo_dummy_ops(hold), &ops1);
	zalgo_dummy_rele(hold);

	/* same, for different id */
	unit_ok(zalgo_dummy_select(ZG_DUMMY_0, "ops2"));
	hold = zalgo_dummy_hold(ZG_DUMMY_0);
	unit_ptr_eq(zalgo_dummy_ops(hold), &ops2);
	zalgo_dummy_rele(hold);

	/* next hold continues to get the selected id */
	hold = zalgo_dummy_hold(ZG_DUMMY_0);
	unit_ptr_eq(zalgo_dummy_ops(hold), &ops2);
	zalgo_dummy_rele(hold);

	/* selecting the currently selected id is a no-op */
	unit_ok(zalgo_dummy_select(ZG_DUMMY_0, "ops2"));
	hold = zalgo_dummy_hold(ZG_DUMMY_0);
	unit_ptr_eq(zalgo_dummy_ops(hold), &ops2);
	zalgo_dummy_rele(hold);

	return (MUNIT_OK);
}

/* ========== */

/* Test suite definition and boilerplate. */

static const MunitTest zalgo_tests[] = {
	UNIT_TEST("zalgo_register",	test_zalgo_register),
	UNIT_TEST("zalgo_hold",		test_zalgo_hold),

	UNIT_TEST("zalgo_select",	test_zalgo_select),

	{ 0 },
};

static const MunitSuite zalgo_test_suite = {
	"zalgo.",
	zalgo_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	zalgo_init();

	int rc = munit_suite_main(&zalgo_test_suite, NULL, argc, argv);

	zalgo_fini();

	return (rc);
}
