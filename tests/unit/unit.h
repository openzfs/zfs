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

/*
 * Generic scalar type comparisons and failure reporting.
 *
 * Our goal: we want to be able to have a single type comparison+assertion
 * helper like unit_eq(a, b) that is aware of the types of its arguments and
 * will perform the correct comparison for that type.
 *
 * C11's _Generic allows type-based dispatch, but the dispatch target must
 * be an expression, so we can't put any actual code there, which means we
 * can't use munit's type-specific assertion macros.
 *
 * Instead, we define our own implementations for each type, _unit_check_*(),
 * which _Generic can dispatch to. To those we pass down the wanted comparison
 * op (_unit_check_op_t), the two values, and string representations of the
 * call site, variables and operator. This is a lot, but it ensures that if
 * the comparison fails, we correctly get the location, expressions and the
 * type-correct representations of the values.
 */

/* comparison operators for _unit_check_*() */
typedef enum {
	_UNIT_CHECK_OP_EQ,
	_UNIT_CHECK_OP_NE,
	_UNIT_CHECK_OP_LE,
	_UNIT_CHECK_OP_GE,
	_UNIT_CHECK_OP_LT,
	_UNIT_CHECK_OP_GT,
} _unit_check_op_t;

/* declare _unit_check_*() for the named types */
#define	_UNIT_CHECK_DECLARE(name, type)				\
extern void _unit_check_##name(const char *file, int line,	\
    const char *astr, const char *bstr,				\
    const char *opstr, _unit_check_op_t op, type a, type b);

_UNIT_CHECK_DECLARE(int8, int8_t)
_UNIT_CHECK_DECLARE(uint8, uint8_t)
_UNIT_CHECK_DECLARE(int16, int16_t)
_UNIT_CHECK_DECLARE(uint16, uint16_t)
_UNIT_CHECK_DECLARE(int32, int32_t)
_UNIT_CHECK_DECLARE(uint32, uint32_t)
_UNIT_CHECK_DECLARE(int64, int64_t)
_UNIT_CHECK_DECLARE(uint64, uint64_t)
_UNIT_CHECK_DECLARE(double, double)

#undef _UNIT_CHECK_DECLARE

/*
 * _unit_cmp() is the dispatcher. Note that we only dispatch on the type
 * of the first arg, which matches what a normal comparison would do, and
 * also avoids a combinatorial explosion.
 *
 * Any type not explicitly listed here is falls back to uint64_t, which is
 * likely good-enough most of the time.
 */
#define	_unit_cmp(a, b, opstr, opcode)					\
	_Generic((a),							\
	    int8_t:	_unit_check_int8,				\
	    uint8_t:	_unit_check_uint8,				\
	    int16_t:	_unit_check_int16,				\
	    uint16_t:	_unit_check_uint16,				\
	    int32_t:	_unit_check_int32,				\
	    uint32_t:	_unit_check_uint32,				\
	    int64_t:	_unit_check_int64,				\
	    uint64_t:	_unit_check_uint64,				\
	    double:	_unit_check_double,				\
	    default:	_unit_check_uint64)				\
	(__FILE__, __LINE__, #a, #b, opstr, opcode, (a), (b))

/* shortcuts for scalar type comparisons */
#define	unit_eq(a, b)	_unit_cmp((a), (b), "==", _UNIT_CHECK_OP_EQ)
#define	unit_ne(a, b)	_unit_cmp((a), (b), "!=", _UNIT_CHECK_OP_NE)
#define	unit_le(a, b)	_unit_cmp((a), (b), "<=", _UNIT_CHECK_OP_LE)
#define	unit_ge(a, b)	_unit_cmp((a), (b), ">=", _UNIT_CHECK_OP_GE)
#define	unit_lt(a, b)	_unit_cmp((a), (b), "<",  _UNIT_CHECK_OP_LT)
#define	unit_gt(a, b)	_unit_cmp((a), (b), ">",  _UNIT_CHECK_OP_GT)

/* shortcut for truthy tests */
#define	unit_true(a)	munit_assert_true(a)
#define	unit_false(a)	munit_assert_false(a)

/* shortcut for zero test */
#define	unit_zero(a)	unit_eq((a), 0)

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
