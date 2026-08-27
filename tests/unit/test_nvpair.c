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

#include <sys/kmem.h>
#include <sys/nvpair.h>
#include <libnvpair.h>

#include "unit.h"

/* ========== */

/* Test-specific helpers. */

/*
 * Almost every test needs a plain NV_UNIQUE_NAME nvlist to work with, and
 * the flag is essentially never the point of the test (the tests that do
 * care about alloc flags, or about nvlist_alloc() itself, call it directly).
 */
static nvlist_t *
nvl_create_type(uint_t type)
{
	nvlist_t *nvl;
	unit_ok(nvlist_alloc(&nvl, type, KM_SLEEP));
	return (nvl);
}
#define	nvl_create()	nvl_create_type(NV_UNIQUE_NAME)

/*
 * Canned data for the "simple" and "deep" test nvlists. Here so that test
 * cases can compare with the nvlist contents.
 */

/* u64 pair, top level */
static const char nvl_simple_u64_key[] = "simple";
static const uint64_t nvl_simple_u64_val = 0xfedcba9876543210;

/* string pair, top level */
static const char nvl_simple_str_key[] = "fish";
static const char nvl_simple_str_val[] = "swim";

/* u64 pair, top level */
static const char nvl_deep_u64_key[] = "deep";
static const uint64_t nvl_deep_u64_val = 0x76543210fedcba98;

/* string pair, top level */
static const char nvl_deep_str_key[] = "chips";
static const char nvl_deep_str_val[] = "yum";

/* u64 array, top level */
static const char nvl_deep_u64_array_key[] = "array";
static const uint64_t nvl_deep_u64_array_val[] = { 1, 2, 3 };

/* embedded nvlist, top level */
static const char nvl_deep_nvlist_key[] = "sub";

/* string pair inside the embedded nvlist */
static const char nvl_deep_nvlist_str_key[] = "greeting";
static const char nvl_deep_nvlist_str_val[] = "hello";

/*
 * embedded nvlist array, top level. number of elements included here, the
 * array contents are dynamic so a static readonly shared array wouldn't be at
 * all useful.
 */
static const char nvl_deep_nvlist_array_key[] = "subs";
static const uint_t nvl_deep_nvlist_array_nelems = 2;

/*
 * each nvlist in the array has one u64 pair with this key, value of its
 * index in the array.
 */
static const char nvl_deep_nvlist_array_u64_key[] = "id";

/*
 * some extras for testing NV_UNIQUE_NAME_TYPE nvlists, where a key can have
 * values for different types.
 */
static const char nvl_unique_key[] = "a_nice_round_number";
static const uint64_t nvl_unique_u64_val = 2048;
static const char nvl_unique_str_val[] = "two thousand and forty-eight";

/* a key that doesn't exist in the test data */
static const char nvl_nonexistent_key[] = "guy_fleegman";

static nvlist_t *
nvl_create_simple_type(uint_t type)
{
	nvlist_t *nvl = nvl_create_type(type);
	unit_notnull(nvl);

	unit_ok(nvlist_add_uint64(nvl, nvl_simple_u64_key, nvl_simple_u64_val));
	unit_ok(nvlist_add_string(nvl, nvl_simple_str_key, nvl_simple_str_val));

	return (nvl);
}
#define	nvl_create_simple()	nvl_create_simple_type(NV_UNIQUE_NAME)

static nvlist_t *
nvl_create_deep_type(uint_t type)
{
	nvlist_t *nvl = nvl_create_type(type);
	unit_notnull(nvl);

	unit_ok(nvlist_add_uint64(nvl, nvl_deep_u64_key, nvl_deep_u64_val));
	unit_ok(nvlist_add_string(nvl, nvl_deep_str_key, nvl_deep_str_val));

	unit_ok(nvlist_add_uint64_array(nvl, nvl_deep_u64_array_key,
	    nvl_deep_u64_array_val, ARRAY_SIZE(nvl_deep_u64_array_val)));

	nvlist_t *sub = nvl_create();
	unit_ok(nvlist_add_string(sub,
	    nvl_deep_nvlist_str_key, nvl_deep_nvlist_str_val));
	unit_ok(nvlist_add_nvlist(nvl, nvl_deep_nvlist_key, sub));
	nvlist_free(sub);

	nvlist_t *subs[nvl_deep_nvlist_array_nelems];
	for (uint_t i = 0; i < nvl_deep_nvlist_array_nelems; i++) {
		subs[i] = nvl_create();
		unit_ok(nvlist_add_uint64(subs[i],
		    nvl_deep_nvlist_array_u64_key, i));
	}
	unit_ok(nvlist_add_nvlist_array(nvl, nvl_deep_nvlist_array_key,
	    (const nvlist_t * const *)subs, nvl_deep_nvlist_array_nelems));
	for (uint_t i = 0; i < nvl_deep_nvlist_array_nelems; i++)
		nvlist_free(subs[i]);

	return (nvl);
}
#define	nvl_create_deep()	nvl_create_deep_type(NV_UNIQUE_NAME)

/* ========== */

/*
 * alloc/free round-trip on an empty nvlist.
 */
static MunitResult
test_nv_alloc(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl;
	unit_ok(nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP));
	unit_notnull(nvl);
	nvlist_free(nvl);

	return (MUNIT_OK);
}

/*
 * Fixed allocator: nvlist backed by a pre-allocated buffer via nv_fixed_ops.
 * Once the buffer is exhausted, further adds fail with ENOMEM.
 */
static MunitResult
test_nv_alloc_fixed(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	char buf[512];
	nv_alloc_t nva;
	unit_ok(nv_alloc_init(&nva, nv_fixed_ops, buf, sizeof (buf)));

	nvlist_t *nvl;
	unit_ok(nvlist_xalloc(&nvl, NV_UNIQUE_NAME, &nva));

	unit_ok(nvlist_add_uint64(nvl, "a", 1));
	uint64_t val;
	unit_ok(nvlist_lookup_uint64(nvl, "a", &val));
	unit_eq(val, 1);

	/* keep adding until the fixed buffer is exhausted */
	int err = 0;
	char name[32];
	for (int i = 0; i < 1000 && err == 0; i++) {
		(void) snprintf(name, sizeof (name), "k%d", i);
		err = nvlist_add_uint64(nvl, name, i);
	}
	unit_err(err, ENOMEM);

	nvlist_free(nvl);
	nv_alloc_fini(&nva);
	return (MUNIT_OK);
}


/*
 * nvlist_nvflag: returns the unique-name flags the list was allocated with.
 */
static MunitResult
test_nv_nvflag(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl;
	unit_ok(nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, KM_SLEEP));
	unit_eq(nvlist_nvflag(nvl), NV_UNIQUE_NAME_TYPE);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/*
 * nvlist_empty: true only when there are no pairs.
 */
static MunitResult
test_nv_empty(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();
	unit_true(nvlist_empty(nvl));

	unit_ok(nvlist_add_uint64(nvl, "x", 1));
	unit_false(nvlist_empty(nvl));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/*
 * nvlist_exists: true for a present name, false otherwise.
 */
static MunitResult
test_nv_exists(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();
	unit_ok(nvlist_add_uint64(nvl, "x", 1));

	unit_true(nvlist_exists(nvl, "x"));
	unit_false(nvlist_exists(nvl, "y"));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/*
 * For each scalar type, ensure we can add a value and read it back, and also
 * that a non-existent value of the same type is not found.
 */

static MunitResult
test_nv_boolean(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_boolean(nvl, "yes"));

	unit_ok(nvlist_lookup_boolean(nvl, "yes"));
	unit_true(nvlist_exists(nvl, "yes"));

	unit_err(nvlist_lookup_boolean(nvl, "no"), ENOENT);
	unit_false(nvlist_exists(nvl, "no"));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_boolean_value(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_boolean_value(nvl, "yes", B_TRUE));
	unit_ok(nvlist_add_boolean_value(nvl, "no", B_FALSE));

	unit_true(nvlist_exists(nvl, "yes"));
	unit_true(nvlist_exists(nvl, "no"));

	boolean_t val;

	unit_ok(nvlist_lookup_boolean_value(nvl, "yes", &val));
	unit_true(val);

	unit_ok(nvlist_lookup_boolean_value(nvl, "no", &val));
	unit_false(val);

	unit_err(nvlist_lookup_boolean_value(nvl, "maybe", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_byte(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_byte(nvl, "byte", 0x42));
	unit_true(nvlist_exists(nvl, "byte"));

	uchar_t val;
	unit_ok(nvlist_lookup_byte(nvl, "byte", &val));
	unit_eq(val, 0x42);

	unit_err(nvlist_lookup_byte(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int8(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_int8(nvl, "i8", -12));
	unit_true(nvlist_exists(nvl, "i8"));

	int8_t val;
	unit_ok(nvlist_lookup_int8(nvl, "i8", &val));
	unit_eq(val, -12);

	unit_err(nvlist_lookup_int8(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint8(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_uint8(nvl, "u8", 200));
	unit_true(nvlist_exists(nvl, "u8"));

	uint8_t val;
	unit_ok(nvlist_lookup_uint8(nvl, "u8", &val));
	unit_eq(val, 200);

	unit_err(nvlist_lookup_uint8(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int16(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_int16(nvl, "i16", -1234));
	unit_true(nvlist_exists(nvl, "i16"));

	int16_t val;
	unit_ok(nvlist_lookup_int16(nvl, "i16", &val));
	unit_eq(val, -1234);

	unit_err(nvlist_lookup_int16(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint16(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_uint16(nvl, "u16", 54321));
	unit_true(nvlist_exists(nvl, "u16"));

	uint16_t val;
	unit_ok(nvlist_lookup_uint16(nvl, "u16", &val));
	unit_eq(val, 54321);

	unit_err(nvlist_lookup_uint16(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int32(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_int32(nvl, "i32", -123456));
	unit_true(nvlist_exists(nvl, "i32"));

	int32_t val;
	unit_ok(nvlist_lookup_int32(nvl, "i32", &val));
	unit_eq(val, -123456);

	unit_err(nvlist_lookup_int32(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint32(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_uint32(nvl, "u32", 3000000000));
	unit_true(nvlist_exists(nvl, "u32"));

	uint32_t val;
	unit_ok(nvlist_lookup_uint32(nvl, "u32", &val));
	unit_eq(val, 3000000000);

	unit_err(nvlist_lookup_uint32(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int64(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_int64(nvl, "i64", -9876546410LL));
	unit_true(nvlist_exists(nvl, "i64"));

	int64_t val;
	unit_ok(nvlist_lookup_int64(nvl, "i64", &val));
	unit_eq(val, -9876546410LL);

	unit_err(nvlist_lookup_int64(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint64(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_uint64(nvl, "u64", 12345678901234567890ULL));
	unit_true(nvlist_exists(nvl, "u64"));

	uint64_t val;
	unit_ok(nvlist_lookup_uint64(nvl, "u64", &val));
	unit_eq(val, 12345678901234567890ULL);

	unit_err(nvlist_lookup_uint64(nvl, "no", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_string(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_string(nvl, "str", "hello"));

	const char *val;
	unit_ok(nvlist_lookup_string(nvl, "str", &val));
	unit_str_eq(val, "hello");

	unit_err(nvlist_lookup_string(nvl, "missing", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_hrtime(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_hrtime(nvl, "ts", SEC2NSEC(10)));

	hrtime_t val;
	unit_ok(nvlist_lookup_hrtime(nvl, "ts", &val));
	unit_eq(val, SEC2NSEC(10));

	unit_err(nvlist_lookup_hrtime(nvl, "missing", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_double(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	unit_ok(nvlist_add_double(nvl, "d", 3.5));

	double val;
	unit_ok(nvlist_lookup_double(nvl, "d", &val));
	unit_eq(val, 3.5);

	unit_err(nvlist_lookup_double(nvl, "missing", &val), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/*
 * For each array type, ensure we can add an array, read it back, and check
 * the contents match.
 */

static MunitResult
test_nv_boolean_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	const boolean_t in[] = { B_TRUE, B_FALSE, B_TRUE };
	unit_ok(nvlist_add_boolean_array(nvl, "array", in, ARRAY_SIZE(in)));

	boolean_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_boolean_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_byte_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	uchar_t in[] = { 1, 2, 3, 4 };
	unit_ok(nvlist_add_byte_array(nvl, "array", in, ARRAY_SIZE(in)));

	uchar_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_byte_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int8_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	int8_t in[] = { -1, 0, 1 };
	unit_ok(nvlist_add_int8_array(nvl, "array", in, ARRAY_SIZE(in)));

	int8_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_int8_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint8_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	uint8_t in[] = { 10, 20, 30 };
	unit_ok(nvlist_add_uint8_array(nvl, "array", in, ARRAY_SIZE(in)));

	uint8_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_uint8_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int16_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	int16_t in[] = { -100, 0, 100 };
	unit_ok(nvlist_add_int16_array(nvl, "array", in, ARRAY_SIZE(in)));

	int16_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_int16_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint16_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	uint16_t in[] = { 1000, 2000, 3000 };
	unit_ok(nvlist_add_uint16_array(nvl, "array", in, ARRAY_SIZE(in)));

	uint16_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_uint16_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int32_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	int32_t in[] = { -100000, 0, 100000 };
	unit_ok(nvlist_add_int32_array(nvl, "array", in, ARRAY_SIZE(in)));

	int32_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_int32_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint32_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	uint32_t in[] = { 100000, 200000, 300000 };
	unit_ok(nvlist_add_uint32_array(nvl, "array", in, ARRAY_SIZE(in)));

	uint32_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_uint32_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_int64_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	int64_t in[] = { -1000000000LL, 0, 1000000000LL };
	unit_ok(nvlist_add_int64_array(nvl, "array", in, ARRAY_SIZE(in)));

	int64_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_int64_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_uint64_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	uint64_t in[] = { 1, 2, 3, 4, 5 };
	unit_ok(nvlist_add_uint64_array(nvl, "array", in, ARRAY_SIZE(in)));

	uint64_t *out;
	uint_t n;
	unit_ok(nvlist_lookup_uint64_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_string_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create();

	const char *in[] = { "alpha", "beta", "gamma" };
	unit_ok(nvlist_add_string_array(nvl, "array", in, ARRAY_SIZE(in)));

	char **out;
	uint_t n;
	unit_ok(nvlist_lookup_string_array(nvl, "array", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));
	for (uint_t i = 0; i < n; i++)
		unit_str_eq(in[i], out[i]);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/* Add an nvlist to another nvlist, read it back, check its contents. */
static MunitResult
test_nv_nvlist(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *top = nvl_create();
	nvlist_t *sub = nvl_create();

	unit_ok(nvlist_add_uint64(sub, "key", 1));
	unit_ok(nvlist_add_nvlist(top, "sub", sub));
	nvlist_free(sub);

	nvlist_t *out;
	unit_ok(nvlist_lookup_nvlist(top, "sub", &out));

	uint64_t val = 0;
	unit_ok(nvlist_lookup_uint64(out, "key", &val));
	unit_eq(val, 1);

	nvlist_free(top);
	return (MUNIT_OK);
}

/* Add an array of nvlists to another, read it back. */
static MunitResult
test_nv_nvlist_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *top = nvl_create();

	nvlist_t *sub0 = nvl_create();
	nvlist_t *sub1 = nvl_create();
	unit_ok(nvlist_add_uint64(sub0, "key", 0));
	unit_ok(nvlist_add_uint64(sub1, "key", 1));

	const nvlist_t *in[] = { sub0, sub1 };
	unit_ok(nvlist_add_nvlist_array(top, "subs", in, ARRAY_SIZE(in)));
	nvlist_free(sub0);
	nvlist_free(sub1);

	nvlist_t **out;
	uint_t n;
	unit_ok(nvlist_lookup_nvlist_array(top, "subs", &out, &n));
	unit_eq(n, ARRAY_SIZE(in));

	uint64_t val;
	unit_ok(nvlist_lookup_uint64(out[0], "key", &val));
	unit_eq(val, 0);
	unit_ok(nvlist_lookup_uint64(out[1], "key", &val));
	unit_eq(val, 1);

	nvlist_free(top);
	return (MUNIT_OK);
}

/* ========== */

/* lookup and return pair, check it */
static MunitResult
test_nv_lookup_nvpair(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_simple();

	nvpair_t *pair = NULL;
	unit_ok(nvlist_lookup_nvpair(nvl, nvl_simple_u64_key, &pair));
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_simple_u64_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_UINT64);

	/* weirdly, the pair not found returns EINVAL, not ENOENT */
	unit_err(nvlist_lookup_nvpair(nvl, nvl_nonexistent_key, &pair), EINVAL);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* forward pair iterator */
static MunitResult
test_nv_next_nvpair(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_deep();

	nvpair_t *pair = NULL;

	pair = nvlist_next_nvpair(nvl, NULL);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_u64_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_UINT64);

	pair = nvlist_next_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_str_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_STRING);

	pair = nvlist_next_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_u64_array_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_UINT64_ARRAY);

	pair = nvlist_next_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_nvlist_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_NVLIST);

	pair = nvlist_next_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_nvlist_array_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_NVLIST_ARRAY);

	unit_null(nvlist_next_nvpair(nvl, pair));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* reverse pair iterator */
static MunitResult
test_nv_prev_nvpair(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_deep();

	nvpair_t *pair = NULL;

	pair = nvlist_prev_nvpair(nvl, NULL);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_nvlist_array_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_NVLIST_ARRAY);

	pair = nvlist_prev_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_nvlist_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_NVLIST);

	pair = nvlist_prev_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_u64_array_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_UINT64_ARRAY);

	pair = nvlist_prev_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_str_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_STRING);

	pair = nvlist_prev_nvpair(nvl, pair);
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), nvl_deep_u64_key);
	unit_eq(nvpair_type(pair), DATA_TYPE_UINT64);

	unit_null(nvlist_prev_nvpair(nvl, pair));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/* Lookup is by name+type, so lookup with wrong type returns ENOENT. */
static MunitResult
test_nv_type_mismatch(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_simple();

	const char *str;
	unit_err(nvlist_lookup_string(nvl, nvl_simple_u64_key, &str), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/* Lookup multiple pairs in a single call. */
static MunitResult
test_nv_lookup_pairs(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_simple();

	uint64_t val, noval;
	const char *str;
	unit_ok(nvlist_lookup_pairs(nvl, 0,
	    nvl_simple_u64_key, DATA_TYPE_UINT64, &val,
	    nvl_simple_str_key, DATA_TYPE_STRING, &str,
	    NULL));
	unit_eq(val, nvl_simple_u64_val);
	unit_str_eq(str, nvl_simple_str_val);

	/* without NOENTOK, a missing name fails the whole call */
	unit_err(nvlist_lookup_pairs(nvl, 0,
	    nvl_simple_u64_key, DATA_TYPE_UINT64, &val,
	    nvl_simple_str_key, DATA_TYPE_STRING, &str,
	    nvl_nonexistent_key, DATA_TYPE_UINT64, &noval,
	    NULL), ENOENT);

	/* with NOENTOK, a missing name is tolerated */
	unit_ok(nvlist_lookup_pairs(nvl, NV_FLAG_NOENTOK,
	    nvl_simple_u64_key, DATA_TYPE_UINT64, &val,
	    nvl_simple_str_key, DATA_TYPE_STRING, &str,
	    nvl_nonexistent_key, DATA_TYPE_UINT64, &noval,
	    NULL));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/*
 * Lookup an array element specified in the key. This function can actually
 * do a deep lookup through multiple embedded lists and arrays; this is just
 * a sanity check, since OpenZFS doesn't use it directly.
 */
static MunitResult
test_nv_lookup_nvpair_embedded(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_deep();

	nvpair_t *pair;
	int idx;
	const char *ep;

	char pattern[128];
	snprintf(pattern, sizeof (pattern),
	    "%s[1]", nvl_deep_nvlist_array_key);

	/* returns a pair for the object, and an index if its an array */
	unit_ok(nvlist_lookup_nvpair_embedded_index(nvl, pattern,
	    &pair, &idx, &ep));
	unit_notnull(pair);
	unit_eq(idx, 1);
	unit_eq(nvpair_type(pair), DATA_TYPE_NVLIST_ARRAY);

	/* without an index, the returned index is -1 */
	unit_ok(nvlist_lookup_nvpair_embedded_index(nvl,
	    nvl_deep_nvlist_array_key, &pair, &idx, &ep));
	unit_notnull(pair);
	unit_eq(idx, -1);
	unit_eq(nvpair_type(pair), DATA_TYPE_NVLIST_ARRAY);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/*
 * Remove the first pair matching both name and type, leaving any other pairs
 * of the same name.
 */
static MunitResult
test_nv_remove(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME_TYPE);

	/* add two values for the same key */
	unit_ok(nvlist_add_uint64(nvl, nvl_unique_key, nvl_unique_u64_val));
	unit_ok(nvlist_add_string(nvl, nvl_unique_key, nvl_unique_str_val));

	/* remove one */
	unit_ok(nvlist_remove(nvl, nvl_unique_key, DATA_TYPE_UINT64));

	/* make sure its really gone */
	uint64_t val;
	unit_err(nvlist_lookup_uint64(nvl, nvl_unique_key, &val), ENOENT);

	/* but the other is still there */
	const char *str;
	unit_ok(nvlist_lookup_string(nvl, nvl_unique_key, &str));
	unit_str_eq(str, nvl_unique_str_val);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* Remove all pairs matching the name, regardless of type. */
static MunitResult
test_nv_remove_all(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME_TYPE);

	/* add two values for the same key */
	unit_ok(nvlist_add_uint64(nvl, nvl_unique_key, nvl_unique_u64_val));
	unit_ok(nvlist_add_string(nvl, nvl_unique_key, nvl_unique_str_val));

	/* and another with some other key */
	unit_ok(nvlist_add_uint64(nvl, nvl_simple_u64_key, nvl_simple_u64_val));

	/* remove the double key */
	unit_ok(nvlist_remove_all(nvl, nvl_unique_key));

	/* make sure they're all gone, but the other remains */
	unit_false(nvlist_exists(nvl, nvl_unique_key));
	unit_true(nvlist_exists(nvl, nvl_simple_u64_key));

	/* removing all instances of a nonexistent key fails */
	unit_err(nvlist_remove_all(nvl, nvl_unique_key), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* Find a pair, remove it. */
static MunitResult
test_nv_remove_nvpair(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_simple();

	nvpair_t *pair;
	unit_ok(nvlist_lookup_nvpair(nvl, nvl_simple_u64_key, &pair));
	unit_ok(nvlist_remove_nvpair(nvl, pair));

	unit_false(nvlist_exists(nvl, nvl_simple_u64_key));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/* Re-adding a key to a NV_UNIQUE_NAME nvlist replaces the existing pair. */
static MunitResult
test_nv_replace_name(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME);

	uint64_t val;

	/* add a pair and confirm it */
	unit_ok(nvlist_add_uint64(nvl, nvl_unique_key, nvl_unique_u64_val));
	unit_ok(nvlist_lookup_uint64(nvl, nvl_unique_key, &val));
	unit_eq(val, nvl_unique_u64_val);

	/* replace it */
	unit_ok(nvlist_add_uint64(nvl, nvl_unique_key, ~nvl_unique_u64_val));

	/* check we got the new value */
	unit_ok(nvlist_lookup_uint64(nvl, nvl_unique_key, &val));
	unit_eq(val, ~nvl_unique_u64_val);

	/* replace it with a totally different type */
	unit_ok(nvlist_add_string(nvl, nvl_unique_key, nvl_unique_str_val));

	/* not found with the previous type */
	unit_err(nvlist_lookup_uint64(nvl, nvl_unique_key, &val), ENOENT);

	/* but is with the new type */
	const char *str;
	unit_ok(nvlist_lookup_string(nvl, nvl_unique_key, &str));
	unit_str_eq(str, nvl_unique_str_val);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/*
 * Reusing a key in a NV_UNIQUE_NAME_TYPE nvlist only replaces pairs with
 * the same type.
 */
static MunitResult
test_nv_replace_name_type(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME_TYPE);

	uint64_t val;

	/* add a pair and confirm it */
	unit_ok(nvlist_add_uint64(nvl, nvl_unique_key, nvl_unique_u64_val));
	unit_ok(nvlist_lookup_uint64(nvl, nvl_unique_key, &val));
	unit_eq(val, nvl_unique_u64_val);

	/* replace it */
	unit_ok(nvlist_add_uint64(nvl, nvl_unique_key, ~nvl_unique_u64_val));

	/* check we got the new value */
	unit_ok(nvlist_lookup_uint64(nvl, nvl_unique_key, &val));
	unit_eq(val, ~nvl_unique_u64_val);

	/* add a different type with the same key */
	unit_ok(nvlist_add_string(nvl, nvl_unique_key, nvl_unique_str_val));

	/* original exists */
	unit_ok(nvlist_lookup_uint64(nvl, nvl_unique_key, &val));
	unit_eq(val, ~nvl_unique_u64_val);

	/* new exists */
	const char *str;
	unit_ok(nvlist_lookup_string(nvl, nvl_unique_key, &str));
	unit_str_eq(str, nvl_unique_str_val);

	/* but not with any other type that wasn't added */
	boolean_t bval;
	unit_err(nvlist_lookup_boolean_value(nvl,
	    nvl_unique_key, &bval), ENOENT);

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/* Adding a pair taken from one list to another makes a copy. */
static MunitResult
test_nv_add_nvpair(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *src = nvl_create_simple();
	nvlist_t *dst = nvl_create();

	nvpair_t *pair;

	unit_ok(nvlist_lookup_nvpair(src, nvl_simple_u64_key, &pair));
	unit_ok(nvlist_add_nvpair(dst, pair));
	unit_ok(nvlist_lookup_nvpair(src, nvl_simple_str_key, &pair));
	unit_ok(nvlist_add_nvpair(dst, pair));

	uint64_t val;
	unit_ok(nvlist_lookup_uint64(dst, nvl_simple_u64_key, &val));
	unit_eq(val, nvl_simple_u64_val);

	const char *str;
	unit_ok(nvlist_lookup_string(src, nvl_simple_str_key, &str));
	unit_str_eq(str, nvl_simple_str_val);

	nvlist_free(src);
	nvlist_free(dst);
	return (MUNIT_OK);
}

/* Create a new list with the same contents as the first. */
static MunitResult
test_nv_dup(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *orig = nvl_create_simple();

	nvlist_t *copy = NULL;
	unit_ok(nvlist_dup(orig, &copy, KM_SLEEP));
	unit_notnull(copy);

	uint64_t val;
	const char *str;

	/* copy has the same keys */
	unit_ok(nvlist_lookup_uint64(orig, nvl_simple_u64_key, &val));
	unit_eq(val, nvl_simple_u64_val);
	unit_ok(nvlist_lookup_string(orig, nvl_simple_str_key, &str));
	unit_str_eq(str, nvl_simple_str_val);

	/* a key overwritten in the original does not change the copy */
	unit_ok(nvlist_add_uint64(orig,
	    nvl_simple_u64_key, ~nvl_simple_u64_val));
	unit_ok(nvlist_lookup_uint64(copy, nvl_simple_u64_key, &val));
	unit_eq(val, nvl_simple_u64_val);
	unit_ok(nvlist_lookup_uint64(orig, nvl_simple_u64_key, &val));
	unit_eq(val, ~nvl_simple_u64_val);

	nvlist_free(orig);
	nvlist_free(copy);
	return (MUNIT_OK);
}

/* Copy pairs from src to dst. */
static MunitResult
test_nv_merge(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *dst = nvl_create_deep();
	nvlist_t *src = nvl_create_simple();

	unit_ok(nvlist_merge(dst, src, 0));

	uint64_t val;

	/* dst should now have the keys from deep and simple */
	unit_ok(nvlist_lookup_uint64(dst, nvl_simple_u64_key, &val));
	unit_eq(val, nvl_simple_u64_val);
	unit_ok(nvlist_lookup_uint64(dst, nvl_deep_u64_key, &val));
	unit_eq(val, nvl_deep_u64_val);

	/* src only has its original key */
	unit_ok(nvlist_lookup_uint64(src, nvl_simple_u64_key, &val));
	unit_eq(val, nvl_simple_u64_val);
	unit_err(nvlist_lookup_uint64(src, nvl_deep_u64_key, &val), ENOENT);

	nvlist_free(dst);
	nvlist_free(src);
	return (MUNIT_OK);
}

/* ========== */

static int
encoding_from_params(const MunitParameter params[])
{
	const char *enc = munit_parameters_get(params, "encoding");
	if (enc == NULL)
		munit_error("encoding_from_params: missing encoding param");
	else if (strcmp(enc, "native") == 0)
		return (NV_ENCODE_NATIVE);
	else if (strcmp(enc, "xdr") == 0)
		return (NV_ENCODE_XDR);
	munit_errorf("encoding_from_params: invalid encoding '%s'", enc);
}

static const MunitParameterEnum nv_encoding_params[] = {
	UNIT_PARAM("encoding", "native", "xdr"),
	{ 0 },
};

/* Confirm pack & unpack round-trips correctly. */
static MunitResult
test_nv_pack_unpack_simple(const MunitParameter params[], void *data)
{
	(void) data;

	int encoding = encoding_from_params(params);

	nvlist_t *nvl = nvl_create_simple();

	char *buf = NULL;
	size_t buflen = 0;
	unit_ok(nvlist_pack(nvl, &buf, &buflen, encoding, KM_SLEEP));
	unit_notnull(buf);
	unit_gt(buflen, 0);

	nvlist_t *nvl2 = NULL;
	unit_ok(nvlist_unpack(buf, buflen, &nvl2, KM_SLEEP));
	unit_notnull(nvl2);

	uint64_t val;
	unit_ok(nvlist_lookup_uint64(nvl2, nvl_simple_u64_key, &val));
	unit_eq(val, nvl_simple_u64_val);

	const char *str;
	unit_ok(nvlist_lookup_string(nvl2, nvl_simple_str_key, &str));
	unit_str_eq(str, nvl_simple_str_val);

	nvlist_free(nvl);
	nvlist_free(nvl2);
	free(buf);
	return (MUNIT_OK);
}

/* Pack/unpack round-trip, with mixed nested types. */
static MunitResult
test_nv_pack_unpack_deep(const MunitParameter params[], void *data)
{
	(void) data;

	int encoding = encoding_from_params(params);

	nvlist_t *nvl = nvl_create_deep();

	size_t sz = 0;
	unit_ok(nvlist_size(nvl, &sz, encoding));

	char *buf = NULL;
	size_t buflen = 0;
	unit_ok(nvlist_pack(nvl, &buf, &buflen, encoding, KM_SLEEP));
	unit_notnull(buf);

	unit_eq(sz, buflen);

	nvlist_t *nvl2 = NULL;
	unit_ok(nvlist_unpack(buf, buflen, &nvl2, KM_SLEEP));
	unit_notnull(nvl2);

	uint64_t val;
	unit_ok(nvlist_lookup_uint64(nvl2, nvl_deep_u64_key, &val));
	unit_eq(val, nvl_deep_u64_val);

	const char *str;
	unit_ok(nvlist_lookup_string(nvl2, nvl_deep_str_key, &str));
	unit_str_eq(str, nvl_deep_str_val);

	uint64_t *arr;
	uint_t n;
	unit_ok(nvlist_lookup_uint64_array(nvl2,
	    nvl_deep_u64_array_key, &arr, &n));
	unit_eq(n, ARRAY_SIZE(nvl_deep_u64_array_val));
	for (uint_t i = 0; i < n; i++)
		unit_eq(nvl_deep_u64_array_val[i], arr[i]);

	nvlist_t *sub;
	unit_ok(nvlist_lookup_nvlist(nvl2, nvl_deep_nvlist_key, &sub));
	unit_ok(nvlist_lookup_string(sub, nvl_deep_nvlist_str_key, &str));
	unit_str_eq(str, nvl_deep_nvlist_str_val);

	nvlist_t **subs;
	unit_ok(nvlist_lookup_nvlist_array(nvl2,
	    nvl_deep_nvlist_array_key, &subs, &n));
	unit_eq(n, nvl_deep_nvlist_array_nelems);
	for (uint_t i = 0; i < n; i++) {
		unit_ok(nvlist_lookup_uint64(subs[i],
		    nvl_deep_nvlist_array_u64_key, &val));
		unit_eq(val, i);
	}

	nvlist_free(nvl);
	nvlist_free(nvl2);
	free(buf);
	return (MUNIT_OK);
}

/* Check computed size matches length of the packed buffer. */
static MunitResult
test_nv_pack_size(const MunitParameter params[], void *data)
{
	(void) data;

	int encoding = encoding_from_params(params);

	nvlist_t *nvl = nvl_create_simple();

	size_t sz = 0;
	unit_ok(nvlist_size(nvl, &sz, encoding));
	unit_gt(sz, 0);

	char *buf = NULL;
	size_t buflen = 0;
	unit_ok(nvlist_pack(nvl, &buf, &buflen, encoding, KM_SLEEP));

	unit_eq(sz, buflen);

	nvlist_free(nvl);
	free(buf);
	return (MUNIT_OK);
}

/* ========== */

/* Basic scalar output to a FILE stream. */
static MunitResult
test_nv_print_simple(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_simple();

	char *out = NULL;
	size_t outsz = 0;
	FILE *fp = open_memstream(&out, &outsz);
	unit_notnull(fp);

	nvlist_print(fp, nvl);
	fclose(fp);

	unit_str_eq(out,
	    "nvlist version: 0\n"
	    "\tsimple = 0xfedcba9876543210\n"
	    "\tfish = swim\n");

	free(out);
	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* Output of arrays and embedded-nvlist members get additional rendering. */
static MunitResult
test_nv_print_deep(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_deep();

	char *out = NULL;
	size_t outsz = 0;
	FILE *fp = open_memstream(&out, &outsz);
	unit_notnull(fp);

	nvlist_print(fp, nvl);
	fclose(fp);

	unit_str_eq(out,
	    "nvlist version: 0\n"
	    "\tdeep = 0x76543210fedcba98\n"
	    "\tchips = yum\n"
	    "\tarray = 0x1 0x2 0x3\n"
	    "\tsub = (embedded nvlist)\n"
	    "\tnvlist version: 0\n"
	    "\t\tgreeting = hello\n"
	    "\t(end sub)\n"
	    "\n"
	    "\tsubs = (array of embedded nvlists)\n"
	    "\t(start subs[0])\n"
	    "\tnvlist version: 0\n"
	    "\t\tid = 0x0\n"
	    "\t(end subs[0])\n"
	    "\t(start subs[1])\n"
	    "\tnvlist version: 0\n"
	    "\t\tid = 0x1\n"
	    "\t(end subs[1])\n"
	    "\n");

	free(out);
	nvlist_free(nvl);
	return (MUNIT_OK);
}

/*
 * nvlist_snprintf() has a different output format, and will also return the
 * length that would have been written if the output was truncated.
 */
static MunitResult
test_nv_snprintf_simple(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_simple();

	/* no buffer, gets the required buffer size */
	int outsz = nvlist_snprintf(NULL, 0, nvl, 0);
	unit_gt(outsz, 0);

	/* too-small buffer returns the required size */
	char small[2];
	unit_eq(nvlist_snprintf(small, sizeof (small), nvl, 0), outsz);

	char *out = malloc(outsz + 1);
	unit_notnull(out);
	unit_eq(nvlist_snprintf(out, outsz + 1, nvl, 0), outsz);

	unit_str_eq(out,
	    "simple: 18364758544493064720\n"
	    "fish: 'swim'\n");

	free(out);
	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_snprintf_deep(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_deep();

	/* no buffer, gets the required buffer size */
	int outsz = nvlist_snprintf(NULL, 0, nvl, 0);
	unit_gt(outsz, 0);

	/* too-small buffer returns the required size */
	char small[2];
	unit_eq(nvlist_snprintf(small, sizeof (small), nvl, 0), outsz);

	char *out = malloc(outsz + 1);
	unit_notnull(out);
	unit_eq(nvlist_snprintf(out, outsz + 1, nvl, 0), outsz);

	unit_str_eq(out,
	    "deep: 8526495043095935640\n"
	    "chips: 'yum'\n"
	    "array[0]: 1\n"
	    "array[1]: 2\n"
	    "array[2]: 3\n"
	    "sub:\n"
	    "    greeting: 'hello'\n"
	    "subs[0]:\n"
	    "    id: 0\n"
	    "subs[1]:\n"
	    "    id: 1\n");

	free(out);
	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

/*
 * The fnv* functions are infallible convience wrappers - they will succeed or
 * panic (via VERIFY() -> abort()). Because they're all one-line wrappers over
 * things we already tested properly, these are just basic sanity checks by
 * rough groups of functions.
 *
 * Notably, we can't catch the panic-on-failure behaviour, because our test
 * framework doesn't yet have the ability to catch a panic. That makes these
 * tests currently of dubious value, but they're not nothing, and we can
 * extend them later when we can catch a panic.
 */

static MunitResult
test_nv_fnvlist_types(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = fnvlist_alloc();
	unit_notnull(nvl);

	fnvlist_add_boolean(nvl, "flag");
	unit_true(fnvlist_lookup_boolean(nvl, "flag"));

	fnvlist_add_boolean_value(nvl, "bool", B_TRUE);
	unit_true(fnvlist_lookup_boolean_value(nvl, "bool"));

	fnvlist_add_byte(nvl, "byte", 0x7f);
	unit_eq(fnvlist_lookup_byte(nvl, "byte"), 0x7f);

	fnvlist_add_int8(nvl, "i8", -5);
	unit_eq(fnvlist_lookup_int8(nvl, "i8"), -5);

	fnvlist_add_uint8(nvl, "u8", 200);
	unit_eq(fnvlist_lookup_uint8(nvl, "u8"), 200);

	fnvlist_add_int16(nvl, "i16", -1234);
	unit_eq(fnvlist_lookup_int16(nvl, "i16"), -1234);

	fnvlist_add_uint16(nvl, "u16", 54321);
	unit_eq(fnvlist_lookup_uint16(nvl, "u16"), 54321);

	fnvlist_add_int32(nvl, "i32", -123456);
	unit_eq(fnvlist_lookup_int32(nvl, "i32"), -123456);

	fnvlist_add_uint32(nvl, "u32", 3000000000U);
	unit_eq(fnvlist_lookup_uint32(nvl, "u32"), 3000000000U);

	fnvlist_add_int64(nvl, "i64", -9876543210LL);
	unit_eq(fnvlist_lookup_int64(nvl, "i64"), -9876543210LL);

	fnvlist_add_uint64(nvl, "u64", 12345678901234567890ULL);
	unit_eq(fnvlist_lookup_uint64(nvl, "u64"), 12345678901234567890ULL);

	fnvlist_add_string(nvl, "string", "abc");
	unit_str_eq(fnvlist_lookup_string(nvl, "string"), "abc");

	fnvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_fnvlist_misc(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = fnvlist_alloc();

	fnvlist_add_boolean(nvl, "flag");
	unit_true(nvlist_exists(nvl, "flag"));

	nvlist_t *child = fnvlist_alloc();
	fnvlist_add_uint64(child, "depth", 1);
	fnvlist_add_nvlist(nvl, "child", child);
	fnvlist_free(child);

	unit_eq(fnvlist_num_pairs(nvl), 2);

	nvpair_t *pair = fnvlist_lookup_nvpair(nvl, "child");
	unit_notnull(pair);
	unit_str_eq(nvpair_name(pair), "child");

	fnvlist_remove(nvl, "flag");
	unit_eq(fnvlist_num_pairs(nvl), 1);

	fnvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nv_fnvlist_copy(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_uint64(nvl, "x", 7);

	size_t sz = 0;
	char *buf = fnvlist_pack(nvl, &sz);
	unit_notnull(buf);
	unit_gt(sz, 0);

	nvlist_t *unpacked = fnvlist_unpack(buf, sz);
	unit_eq(fnvlist_lookup_uint64(unpacked, "x"), 7);
	fnvlist_pack_free(buf, sz);

	nvlist_t *copy = fnvlist_dup(nvl);
	unit_eq(fnvlist_lookup_uint64(copy, "x"), 7);

	nvlist_t *extra = fnvlist_alloc();
	fnvlist_add_uint64(extra, "y", 8);
	fnvlist_merge(copy, extra);
	unit_eq(fnvlist_lookup_uint64(copy, "x"), 7);
	unit_eq(fnvlist_lookup_uint64(copy, "y"), 8);

	fnvlist_free(nvl);
	fnvlist_free(unpacked);
	fnvlist_free(copy);
	fnvlist_free(extra);
	return (MUNIT_OK);
}

static MunitResult
test_nv_fnvpair_value(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_uint64(nvl, "num", 42);
	fnvlist_add_string(nvl, "str", "hi");

	nvlist_t *child = fnvlist_alloc();
	fnvlist_add_uint64(child, "depth", 1);
	fnvlist_add_nvlist(nvl, "child", child);
	fnvlist_free(child);

	unit_eq(fnvpair_value_uint64(fnvlist_lookup_nvpair(nvl, "num")), 42);
	unit_str_eq(fnvpair_value_string(fnvlist_lookup_nvpair(nvl, "str")),
	    "hi");

	nvlist_t *got = fnvpair_value_nvlist(fnvlist_lookup_nvpair(nvl,
	    "child"));
	unit_eq(fnvlist_lookup_uint64(got, "depth"), 1);

	fnvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest nvpair_tests[] = {
	/* creation, destruction, configuration */
	UNIT_TEST("nv_alloc",		test_nv_alloc),
	UNIT_TEST("nv_alloc_fixed",	test_nv_alloc_fixed),
	UNIT_TEST("nv_nvflag",		test_nv_nvflag),

	/* data existence */
	UNIT_TEST("nv_empty",		test_nv_empty),
	UNIT_TEST("nv_exists",		test_nv_exists),

	/* add/lookup by type */
	UNIT_TEST("nv_boolean",		test_nv_boolean),
	UNIT_TEST("nv_boolean_value",	test_nv_boolean_value),
	UNIT_TEST("nv_byte",		test_nv_byte),
	UNIT_TEST("nv_int8",		test_nv_int8),
	UNIT_TEST("nv_uint8",		test_nv_uint8),
	UNIT_TEST("nv_int16",		test_nv_int16),
	UNIT_TEST("nv_uint16",		test_nv_uint16),
	UNIT_TEST("nv_int32",		test_nv_int32),
	UNIT_TEST("nv_uint32",		test_nv_uint32),
	UNIT_TEST("nv_int64",		test_nv_int64),
	UNIT_TEST("nv_uint64",		test_nv_uint64),
	UNIT_TEST("nv_string",		test_nv_string),
	UNIT_TEST("nv_hrtime",		test_nv_hrtime),
	UNIT_TEST("nv_double",		test_nv_double),

	/* add/lookup array by type */
	UNIT_TEST("nv_boolean_array",	test_nv_boolean_array),
	UNIT_TEST("nv_byte_array",	test_nv_byte_array),
	UNIT_TEST("nv_int8_array",	test_nv_int8_array),
	UNIT_TEST("nv_uint8_array",	test_nv_uint8_array),
	UNIT_TEST("nv_int16_array",	test_nv_int16_array),
	UNIT_TEST("nv_uint16_array",	test_nv_uint16_array),
	UNIT_TEST("nv_int32_array",	test_nv_int32_array),
	UNIT_TEST("nv_uint32_array",	test_nv_uint32_array),
	UNIT_TEST("nv_int64_array",	test_nv_int64_array),
	UNIT_TEST("nv_uint64_array",	test_nv_uint64_array),
	UNIT_TEST("nv_string_array",	test_nv_string_array),

	/* add/lookup sublist / array of sublists */
	UNIT_TEST("nv_nvlist",		test_nv_nvlist),
	UNIT_TEST("nv_nvlist_array",	test_nv_nvlist_array),

	/* lookup and traverse by pair */
	UNIT_TEST("nv_lookup_nvpair",	test_nv_lookup_nvpair),
	UNIT_TEST("nv_next_nvpair",	test_nv_next_nvpair),
	UNIT_TEST("nv_prev_nvpair",	test_nv_prev_nvpair),

	/* lookup error cases */
	UNIT_TEST("nv_type_mismatch",	test_nv_type_mismatch),

	/* boutique lookup variants */
	UNIT_TEST("nv_lookup_pairs",	test_nv_lookup_pairs),
	UNIT_TEST("nv_lookup_nvpair_embedded", test_nv_lookup_nvpair_embedded),

	/* remove by name, type, pair */
	UNIT_TEST("nv_remove",		test_nv_remove),
	UNIT_TEST("nv_remove_all",	test_nv_remove_all),
	UNIT_TEST("nv_remove_nvpair",	test_nv_remove_nvpair),

	/* replace existing pairs, according to config */
	UNIT_TEST("nv_replace_name",		test_nv_replace_name),
	UNIT_TEST("nv_replace_name_type",	test_nv_replace_name_type),

	/* copy between nvlists */
	UNIT_TEST("nv_add_nvpair",	test_nv_add_nvpair),
	UNIT_TEST("nv_dup",		test_nv_dup),
	UNIT_TEST("nv_merge",		test_nv_merge),

	/* pack, unpack, encodings of same */
	UNIT_TEST("nv_pack_unpack_simple",
	    test_nv_pack_unpack_simple, nv_encoding_params),
	UNIT_TEST("nv_pack_unpack_deep",
	    test_nv_pack_unpack_deep, nv_encoding_params),
	UNIT_TEST("nv_pack_size",
	    test_nv_pack_size, nv_encoding_params),

	/* output and formatting */
	UNIT_TEST("nv_print_simple",	test_nv_print_simple),
	UNIT_TEST("nv_print_deep",	test_nv_print_deep),
	UNIT_TEST("nv_snprintf_simple",	test_nv_snprintf_simple),
	UNIT_TEST("nv_snprintf_deep",	test_nv_snprintf_deep),

	/* infallible convenience wrappers */
	UNIT_TEST("nv_fnvlist_types",	test_nv_fnvlist_types),
	UNIT_TEST("nv_fnvlist_misc",	test_nv_fnvlist_misc),
	UNIT_TEST("nv_fnvlist_copy",	test_nv_fnvlist_copy),
	UNIT_TEST("nv_fnvpair_value",	test_nv_fnvpair_value),

	{ 0 },
};

static const MunitSuite nvpair_test_suite = {
	"nvpair.",
	nvpair_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&nvpair_test_suite, NULL, argc, argv));
}
