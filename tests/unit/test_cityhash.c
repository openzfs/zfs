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
 * Copyright (c) 2026, Christos Longros.
 */

#include <sys/types.h>
#include <cityhash.h>

#include "unit.h"

/* ========== */

/*
 * cityhash maps one to four uint64_t words to a single uint64_t hash.  The
 * output is fixed by the algorithm, so we can verify exact results.
 */
static MunitResult
test_cityhash_known(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	unit_eq(cityhash1(0), 0x7087061603e53293ULL);
	unit_eq(cityhash1(0x0123456789abcdefULL), 0x4d72820d4fcae8ffULL);
	unit_eq(cityhash2(1, 2), 0x8f1c7927f8b5dff2ULL);
	unit_eq(cityhash3(1, 2, 3), 0x4f6fe08120ecb540ULL);
	unit_eq(cityhash4(0x1111111111111111ULL, 0x2222222222222222ULL,
	    0x3333333333333333ULL, 0x4444444444444444ULL),
	    0xa6370a2070fdfd12ULL);

	return (MUNIT_OK);
}

/*
 * cityhash1/2/3 are specialized versions of cityhash4, so each must match
 * cityhash4 on the same arguments.  cityhash1 passes its word as the 2nd
 * argument.
 */
static MunitResult
test_cityhash_specialized(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	uint64_t a = 0xdeadbeefULL, b = 0xfeedfaceULL, c = 0x00c0ffeeULL;

	unit_eq(cityhash1(a), cityhash4(0, a, 0, 0));
	unit_eq(cityhash2(a, b), cityhash4(a, b, 0, 0));
	unit_eq(cityhash3(a, b, c), cityhash4(a, b, c, 0));

	return (MUNIT_OK);
}

/* Different arguments produce different hash results. */
static MunitResult
test_cityhash_distinct(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	uint64_t base = cityhash4(1, 2, 3, 4);
	unit_ne(base, cityhash4(9, 2, 3, 4));	/* first word */
	unit_ne(base, cityhash4(1, 9, 3, 4));	/* second word */
	unit_ne(base, cityhash4(1, 2, 9, 4));	/* third word */
	unit_ne(base, cityhash4(1, 2, 3, 9));	/* fourth word */
	unit_ne(cityhash1(0), cityhash1(1));

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest cityhash_tests[] = {
	UNIT_TEST("known",		test_cityhash_known),
	UNIT_TEST("specialized",	test_cityhash_specialized),
	UNIT_TEST("distinct",		test_cityhash_distinct),
	{ 0 },
};

static const MunitSuite cityhash_test_suite = {
	"cityhash.",
	cityhash_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&cityhash_test_suite, NULL, argc, argv));
}
