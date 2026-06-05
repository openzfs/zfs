// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef UNIT_H
#define	UNIT_H

#include "munit.h"

/* test/suite definition helpers */

/* single element in a MunitTest array */
#define	_UNIT_TEST(name, func, params, ...)				\
	{ (name), (func), NULL, NULL, MUNIT_TEST_OPTION_NONE,	\
	(MunitParameterEnum*)(params)  }
#define	UNIT_TEST(name, func, ...)				\
	_UNIT_TEST(name, func, ##__VA_ARGS__, NULL)

/* single element in a MunitParameterEnum array */
#define	UNIT_PARAM(name, ...)	\
	{ (char *)(name), (char **)(const char *[]) { __VA_ARGS__, NULL } }

/* shortcut for truthy tests */
#define	unit_true(a)	munit_assert_true(a)
#define	unit_false(a)	munit_assert_false(a)

/* shortcut for zero test */
#define	unit_zero(a)	munit_assert_uint64((a), ==, 0)

/* shortcuts for integer comparisons */
#define	_unit_op(a, op, b)	munit_assert_uint64((a), op, (b))

#define	unit_eq(a, b)	_unit_op((a), ==, (b))
#define	unit_ne(a, b)	_unit_op((a), !=, (b))
#define	unit_le(a, b)	_unit_op((a), <=, (b))
#define	unit_ge(a, b)	_unit_op((a), >=, (b))
#define	unit_lt(a, b)	_unit_op((a), <,  (b))
#define	unit_gt(a, b)	_unit_op((a), >,  (b))

/* shortcuts for string comparisons */
#define	unit_str_eq(a, b)	munit_assert_string_equal(a, b)
#define	unit_str_ne(a, b)	munit_assert_string_not_equal(a, b)

/* shortcuts for error-returning function call */
#define	unit_ok(a)	munit_assert_int((a), ==, 0)
#define	unit_err(a, e)	munit_assert_int((a), ==, (e))

/* helpers to generate useful random data */
extern uint64_t unit_rand_uint64(void);
extern char *unit_rand_str(char *buf, size_t bufsz);

#endif /* UNIT_H */
