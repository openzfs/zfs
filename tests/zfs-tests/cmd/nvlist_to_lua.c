/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libzfs_core.h>
#include <sys/nvpair.h>

static nvlist_t *nvl;
static const char *pool;
static boolean_t unexpected_failures;

static boolean_t
nvlist_equal(nvlist_t *nvla, nvlist_t *nvlb)
{
	if (fnvlist_num_pairs(nvla) != fnvlist_num_pairs(nvlb))
		return (B_FALSE);
	/*
	 * The nvlists have the same number of pairs and keys are unique, so
	 * if every key in A is also in B and assigned to the same value, the
	 * lists are identical.
	 */
	for (nvpair_t *pair = nvlist_next_nvpair(nvla, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(nvla, pair)) {
		char *key = nvpair_name(pair);

		if (!nvlist_exists(nvlb, key))
			return (B_FALSE);

		if (nvpair_type(pair) !=
		    nvpair_type(fnvlist_lookup_nvpair(nvlb, key)))
			return (B_FALSE);

		switch (nvpair_type(pair)) {
		case DATA_TYPE_BOOLEAN_VALUE:
			if (fnvpair_value_boolean_value(pair) !=
			    fnvlist_lookup_boolean_value(nvlb, key)) {
				return (B_FALSE);
			}
			break;
		case DATA_TYPE_STRING:
			if (strcmp(fnvpair_value_string(pair),
			    fnvlist_lookup_string(nvlb, key))) {
				return (B_FALSE);
			}
			break;
		case DATA_TYPE_INT64:
			if (fnvpair_value_int64(pair) !=
			    fnvlist_lookup_int64(nvlb, key)) {
				return (B_FALSE);
			}
			break;
		case DATA_TYPE_NVLIST:
			if (!nvlist_equal(fnvpair_value_nvlist(pair),
			    fnvlist_lookup_nvlist(nvlb, key))) {
				return (B_FALSE);
			}
			break;
		default:
			(void) printf("Unexpected type for nvlist_equal\n");
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

static void
test(const char *testname, boolean_t expect_success, boolean_t expect_match)
{
	const char *progstr = "input = ...; return {output=input}";

	nvlist_t *outnvl;

	(void) printf("\nrunning test '%s'; input:\n", testname);
	dump_nvlist(nvl, 4);

	int err = lzc_channel_program(pool, progstr,
	    10 * 1000 * 1000, 10 * 1024 * 1024, nvl, &outnvl);

	(void) printf("lzc_channel_program returned %u\n", err);
	dump_nvlist(outnvl, 5);

	if (err == 0 && expect_match) {
		/*
		 * Verify that outnvl is the same as input nvl, if we expect
		 * them to be. The input and output will never match if the
		 * input contains an array (since arrays are converted to lua
		 * tables), so this is only asserted for some test cases.
		 */
		nvlist_t *real_outnvl = fnvlist_lookup_nvlist(outnvl, "return");
		real_outnvl = fnvlist_lookup_nvlist(real_outnvl, "output");
		if (!nvlist_equal(nvl, real_outnvl)) {
			unexpected_failures = B_TRUE;
			(void) printf("unexpected input/output mismatch for "
			    "case: %s\n", testname);
		}
	}
	if (err != 0 && expect_success) {
		unexpected_failures = B_TRUE;
		(void) printf("unexpected FAIL of case: %s\n", testname);
	}

	fnvlist_free(nvl);
	nvl = fnvlist_alloc();
}

static void
run_tests(void)
{
	const char *key = "key";

	/* Note: maximum nvlist key length is 32KB */
	int len = 1024 * 31;
	char *bigstring = malloc(len);
	if (bigstring == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < len; i++)
		bigstring[i] = 'a' + i % 26;
	bigstring[len - 1] = '\0';

	nvl = fnvlist_alloc();

	fnvlist_add_boolean(nvl, key);
	test("boolean", B_TRUE, B_FALSE);

	fnvlist_add_boolean_value(nvl, key, B_TRUE);
	test("boolean_value", B_FALSE, B_FALSE);

	fnvlist_add_byte(nvl, key, 1);
	test("byte", B_FALSE, B_FALSE);

	fnvlist_add_int8(nvl, key, 1);
	test("int8", B_FALSE, B_FALSE);

	fnvlist_add_uint8(nvl, key, 1);
	test("uint8", B_FALSE, B_FALSE);

	fnvlist_add_int16(nvl, key, 1);
	test("int16", B_FALSE, B_FALSE);

	fnvlist_add_uint16(nvl, key, 1);
	test("uint16", B_FALSE, B_FALSE);

	fnvlist_add_int32(nvl, key, 1);
	test("int32", B_FALSE, B_FALSE);

	fnvlist_add_uint32(nvl, key, 1);
	test("uint32", B_FALSE, B_FALSE);

	fnvlist_add_int64(nvl, key, 1);
	test("int64", B_TRUE, B_TRUE);

	fnvlist_add_uint64(nvl, key, 1);
	test("uint64", B_FALSE, B_FALSE);

	fnvlist_add_string(nvl, key, "1");
	test("string", B_TRUE, B_TRUE);


	{
		nvlist_t *val = fnvlist_alloc();
		fnvlist_add_string(val, "subkey", "subvalue");
		fnvlist_add_nvlist(nvl, key, val);
		fnvlist_free(val);
		test("nvlist", B_TRUE, B_TRUE);
	}
	{
		boolean_t val[2] = { B_FALSE, B_TRUE };
		fnvlist_add_boolean_array(nvl, key, val, 2);
		test("boolean_array", B_FALSE, B_FALSE);
	}
	{
		uchar_t val[2] = { 0, 1 };
		fnvlist_add_byte_array(nvl, key, val, 2);
		test("byte_array", B_FALSE, B_FALSE);
	}
	{
		int8_t val[2] = { 0, 1 };
		fnvlist_add_int8_array(nvl, key, val, 2);
		test("int8_array", B_FALSE, B_FALSE);
	}
	{
		uint8_t val[2] = { 0, 1 };
		fnvlist_add_uint8_array(nvl, key, val, 2);
		test("uint8_array", B_FALSE, B_FALSE);
	}
	{
		int16_t val[2] = { 0, 1 };
		fnvlist_add_int16_array(nvl, key, val, 2);
		test("int16_array", B_FALSE, B_FALSE);
	}
	{
		uint16_t val[2] = { 0, 1 };
		fnvlist_add_uint16_array(nvl, key, val, 2);
		test("uint16_array", B_FALSE, B_FALSE);
	}
	{
		int32_t val[2] = { 0, 1 };
		fnvlist_add_int32_array(nvl, key, val, 2);
		test("int32_array", B_FALSE, B_FALSE);
	}
	{
		uint32_t val[2] = { 0, 1 };
		fnvlist_add_uint32_array(nvl, key, val, 2);
		test("uint32_array", B_FALSE, B_FALSE);
	}
	{
		int64_t val[2] = { 0, 1 };
		fnvlist_add_int64_array(nvl, key, val, 2);
		test("int64_array", B_TRUE, B_FALSE);
	}
	{
		uint64_t val[2] = { 0, 1 };
		fnvlist_add_uint64_array(nvl, key, val, 2);
		test("uint64_array", B_FALSE, B_FALSE);
	}
	{
		const char *val[2] = { "0", "1" };
		fnvlist_add_string_array(nvl, key, val, 2);
		test("string_array", B_TRUE, B_FALSE);
	}
	{
		nvlist_t *val[2];
		val[0] = fnvlist_alloc();
		fnvlist_add_string(val[0], "subkey", "subvalue");
		val[1] = fnvlist_alloc();
		fnvlist_add_string(val[1], "subkey2", "subvalue2");
		fnvlist_add_nvlist_array(nvl, key, (const nvlist_t **)val, 2);
		fnvlist_free(val[0]);
		fnvlist_free(val[1]);
		test("nvlist_array", B_FALSE, B_FALSE);
	}
	{
		fnvlist_add_string(nvl, bigstring, "1");
		test("large_key", B_TRUE, B_TRUE);
	}
	{
		fnvlist_add_string(nvl, key, bigstring);
		test("large_value", B_TRUE, B_TRUE);
	}
	{
		for (int i = 0; i < 1024; i++) {
			char buf[32];
			(void) snprintf(buf, sizeof (buf), "key-%u", i);
			fnvlist_add_int64(nvl, buf, i);
		}
		test("many_keys", B_TRUE, B_TRUE);
	}
#ifndef __sparc__
	{
		for (int i = 0; i < 10; i++) {
			nvlist_t *newval = fnvlist_alloc();
			fnvlist_add_nvlist(newval, "key", nvl);
			fnvlist_free(nvl);
			nvl = newval;
		}
		test("deeply_nested_pos", B_TRUE, B_TRUE);
	}
	{
		for (int i = 0; i < 90; i++) {
			nvlist_t *newval = fnvlist_alloc();
			fnvlist_add_nvlist(newval, "key", nvl);
			fnvlist_free(nvl);
			nvl = newval;
		}
		test("deeply_nested_neg", B_FALSE, B_FALSE);
	}
#endif
	free(bigstring);
	fnvlist_free(nvl);
}

int
main(int argc, const char *argv[])
{
	(void) libzfs_core_init();

	if (argc != 2) {
		(void) printf("usage: %s <pool>\n",
		    argv[0]);
		exit(2);
	}
	pool = argv[1];

	run_tests();

	libzfs_core_fini();
	return (unexpected_failures);
}
