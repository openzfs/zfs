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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/abd.h>
#include <sys/bitops.h>
#include <sys/zio_compress.h>
#include <sys/zstd/zstd.h>
#include <libspl.h>

#include "unit.h"

#define	TEST_SIZE	(1024 * 1024)

/* The compression wrappers only borrow and return linear ABD buffers. */
typedef struct {
	abd_t abd;
} mock_abd_t;

static mock_abd_t *
mock_abd_create(void *buf, size_t size)
{
	mock_abd_t *m = munit_malloc(sizeof (*m));
	memset(m, 0, sizeof (*m));
	m->abd.abd_flags = ABD_FLAG_LINEAR;
	m->abd.abd_size = size;
	m->abd.abd_u.abd_linear.abd_buf = buf;
	return (m);
}

static void
mock_abd_destroy(mock_abd_t *m)
{
	free(m);
}

abd_t *
abd_get_from_buf_struct(abd_t *abd, void *buf, size_t size)
{
	memset(abd, 0, sizeof (*abd));
	abd->abd_flags = ABD_FLAG_LINEAR;
	abd->abd_size = size;
	abd->abd_u.abd_linear.abd_buf = buf;
	return (abd);
}

void
abd_free(abd_t *abd)
{
	unit_true(abd->abd_flags == ABD_FLAG_LINEAR);
}

void *
abd_borrow_buf(abd_t *abd, size_t size)
{
	unit_le(size, abd->abd_size);
	return (abd->abd_u.abd_linear.abd_buf);
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t size)
{
	return (abd_borrow_buf(abd, size));
}

void
abd_return_buf(abd_t *abd, void *buf, size_t size)
{
	unit_le(size, abd->abd_size);
	unit_true(buf == abd->abd_u.abd_linear.abd_buf);
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t size)
{
	abd_return_buf(abd, buf, size);
}

typedef struct {
	uint8_t *plain;
	uint8_t *compressed;
	uint8_t *decoded;
	mock_abd_t *plain_abd;
	mock_abd_t *compressed_abd;
	mock_abd_t *decoded_abd;
	boolean_t zstd_ready;
} zstd_fixture_t;

static void *
zstd_setup(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zstd_fixture_t *f = munit_malloc(sizeof (*f));

	f->plain = munit_malloc(TEST_SIZE);
	f->compressed = munit_malloc(TEST_SIZE);
	f->decoded = munit_malloc(TEST_SIZE);
	for (size_t i = 0; i < TEST_SIZE; i++)
		f->plain[i] = (uint8_t)((i / 1024) % 31);
	f->plain_abd = mock_abd_create(f->plain, TEST_SIZE);
	f->compressed_abd = mock_abd_create(f->compressed, TEST_SIZE);
	f->decoded_abd = mock_abd_create(f->decoded, TEST_SIZE);

	libspl_init();
	lz4_init();
	unit_ok(zstd_init());
	f->zstd_ready = B_TRUE;
	return (f);
}

static void
zstd_teardown(void *data)
{
	zstd_fixture_t *f = data;

	if (f->zstd_ready)
		zstd_fini();
	lz4_fini();
	libspl_fini();
	mock_abd_destroy(f->plain_abd);
	mock_abd_destroy(f->compressed_abd);
	mock_abd_destroy(f->decoded_abd);
	free(f->plain);
	free(f->compressed);
	free(f->decoded);
	free(f);
}

static void
round_trip(zstd_fixture_t *f, size_t size, int level)
{
	size_t compressed_len = zfs_zstd_compress((abd_t *)f->plain_abd,
	    (abd_t *)f->compressed_abd, size, size, level);
	unit_gt(compressed_len, sizeof (zfs_zstdhdr_t));
	unit_lt(compressed_len, size);

	memset(f->decoded, 0xa5, size);
	unit_ok(zfs_zstd_decompress((abd_t *)f->compressed_abd,
	    (abd_t *)f->decoded_abd, compressed_len, size, 0));
	munit_assert_memory_equal(size, f->plain, f->decoded);
}

/* Changing sizes and levels must reset the retained session and parameters. */
static MunitResult
test_cctx_reuse(const MunitParameter params[], void *data)
{
	zstd_fixture_t *f = data;
	size_t size = strtoul(munit_parameters_get(params, "size"), NULL, 10);
	int level = atoi(munit_parameters_get(params, "level"));

	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	uint64_t before = zfs_zstd_cctx_cache_reuse_count();
	round_trip(f, size, level);
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_19);
	unit_gt(zfs_zstd_cctx_cache_reuse_count(), before);
	return (MUNIT_OK);
}

static MunitResult
test_cctx_small_destination(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;

	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	uint64_t before = zfs_zstd_cctx_cache_reuse_count();
	unit_eq(zfs_zstd_compress((abd_t *)f->plain_abd,
	    (abd_t *)f->compressed_abd, TEST_SIZE,
	    sizeof (zfs_zstdhdr_t) + 1, ZIO_ZSTD_LEVEL_3), TEST_SIZE);
	unit_eq(zfs_zstd_cctx_cache_reuse_count(), before + 1);
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_19);
	unit_eq(zfs_zstd_cctx_cache_reuse_count(), before + 2);
	return (MUNIT_OK);
}

static MunitResult
test_cctx_reap(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;
	uint64_t buffers;
	uint64_t size;
	uint64_t reaps;

	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_gt(buffers, 0);
	unit_gt(size, 0);
	uint64_t before = reaps;
	zfs_zstd_cctx_cache_test_expire();
	zfs_zstd_cache_reap_now();
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_zero(buffers);
	unit_zero(size);
	unit_eq(reaps, before + 1);
	zfs_zstd_cache_reap_now();
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_eq(reaps, before + 1);
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	return (MUNIT_OK);
}

static MunitResult
test_cctx_allocation_failure(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;
	uint64_t buffers;
	uint64_t size;
	uint64_t reaps;
	uint64_t before = zfs_zstd_cctx_cache_test_populate_attempts();

	zfs_zstd_cctx_cache_test_set_alloc_fail(1);
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_1);
	unit_eq(zfs_zstd_cctx_cache_test_populate_attempts(), before + 1);
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_zero(buffers);
	unit_zero(size);
	zfs_zstd_cctx_cache_test_set_alloc_fail(0);
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_1);
	return (MUNIT_OK);
}

static MunitResult
test_cctx_disabled(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;
	uint64_t buffers;
	uint64_t size;
	uint64_t reaps;
	uint64_t before = zfs_zstd_cctx_cache_test_populate_attempts();

	zfs_zstd_cctx_cache_test_disable();
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_7);
	unit_eq(zfs_zstd_cctx_cache_test_populate_attempts(), before);
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_zero(buffers);
	unit_zero(size);
	zfs_zstd_cctx_cache_test_enable();
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_7);
	return (MUNIT_OK);
}

static MunitResult
test_cctx_stats_reset(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;
	uint64_t buffers;
	uint64_t size;
	uint64_t reaps;
	uint64_t before_buffers;
	uint64_t before_size;

	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	zfs_zstd_cctx_cache_test_expire();
	zfs_zstd_cache_reap_now();
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	zfs_zstd_cctx_cache_get_stats(&before_buffers, &before_size, &reaps);
	unit_gt(before_buffers, 0);
	unit_gt(reaps, 0);
	unit_gt(zfs_zstd_cctx_cache_reuse_count(), 0);
	zfs_zstd_cctx_cache_reset_stats();
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_eq(buffers, before_buffers);
	unit_eq(size, before_size);
	unit_zero(reaps);
	unit_zero(zfs_zstd_cctx_cache_reuse_count());
	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_3);
	unit_gt(zfs_zstd_cctx_cache_reuse_count(), 0);
	return (MUNIT_OK);
}

static MunitResult
test_cctx_teardown(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;
	uint64_t buffers;
	uint64_t size;
	uint64_t reaps;

	round_trip(f, TEST_SIZE, ZIO_ZSTD_LEVEL_19);
	zstd_fini();
	f->zstd_ready = B_FALSE;
	zfs_zstd_cctx_cache_get_stats(&buffers, &size, &reaps);
	unit_zero(buffers);
	unit_zero(size);
	return (MUNIT_OK);
}

static const MunitParameterEnum cctx_params[] = {
	UNIT_PARAM("size", "4096", "16384", "131072", "524288", "1048576"),
	UNIT_PARAM("level", "1", "3", "7", "19", "103"),
	{ 0 },
};

static const MunitTest cctx_tests[] = {
	{ "reuse", test_cctx_reuse, zstd_setup, zstd_teardown,
	    MUNIT_TEST_OPTION_NONE, (MunitParameterEnum *)cctx_params },
	{ "small_destination", test_cctx_small_destination,
	    zstd_setup, zstd_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "reap", test_cctx_reap,
	    zstd_setup, zstd_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "allocation_failure", test_cctx_allocation_failure,
	    zstd_setup, zstd_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "disabled", test_cctx_disabled,
	    zstd_setup, zstd_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "stats_reset", test_cctx_stats_reset,
	    zstd_setup, zstd_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ "teardown", test_cctx_teardown,
	    zstd_setup, zstd_teardown, MUNIT_TEST_OPTION_NONE, NULL },
	{ 0 },
};

static const MunitSuite cctx_test_suite = {
	"zstd_cctx.",
	cctx_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&cctx_test_suite, NULL, argc, argv));
}
