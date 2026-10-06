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
	size_t size;
	size_t compressed_len;
} zstd_fixture_t;

static void *
zstd_setup(const MunitParameter params[], void *data)
{
	(void) data;
	zstd_fixture_t *f = munit_malloc(sizeof (*f));
	f->size = strtoul(munit_parameters_get(params, "size"), NULL, 10);
	int level = atoi(munit_parameters_get(params, "level"));

	f->plain = munit_malloc(f->size);
	f->compressed = munit_malloc(f->size);
	f->decoded = munit_malloc(f->size);
	for (size_t i = 0; i < f->size; i++)
		f->plain[i] = (uint8_t)(i / 1024);

	libspl_init();
	lz4_init();
	unit_ok(zstd_init());
	f->plain_abd = mock_abd_create(f->plain, f->size);
	f->compressed_abd = mock_abd_create(f->compressed, f->size);
	f->decoded_abd = mock_abd_create(f->decoded, f->size);

	f->compressed_len = zfs_zstd_compress((abd_t *)f->plain_abd,
	    (abd_t *)f->compressed_abd, f->size, f->size, level);
	unit_gt(f->compressed_len, sizeof (zfs_zstdhdr_t));
	unit_lt(f->compressed_len, f->size);
	return (f);
}

static void
zstd_teardown(void *data)
{
	zstd_fixture_t *f = data;

	mock_abd_destroy(f->plain_abd);
	mock_abd_destroy(f->compressed_abd);
	mock_abd_destroy(f->decoded_abd);
	zstd_fini();
	lz4_fini();
	libspl_fini();
	free(f->plain);
	free(f->compressed);
	free(f->decoded);
	free(f);
}

static void
zstd_verify_decode(zstd_fixture_t *f)
{
	memset(f->decoded, 0xff, f->size);
	unit_ok(zfs_zstd_decompress((abd_t *)f->compressed_abd,
	    (abd_t *)f->decoded_abd,
	    f->compressed_len, f->size, 0));
	munit_assert_memory_equal(f->size, f->plain, f->decoded);
}

/* Repeated independent frames must decode correctly with a cached context. */
static MunitResult
test_zstd_reuse(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;

	zstd_verify_decode(f);
	zstd_verify_decode(f);
	return (MUNIT_OK);
}

/* A decoder error must not prevent the next valid frame from decoding. */
static MunitResult
test_zstd_decoder_error(const MunitParameter params[], void *data)
{
	(void) params;
	zstd_fixture_t *f = data;
	zfs_zstdhdr_t *header = (zfs_zstdhdr_t *)f->compressed;
	uint32_t payload_len = BE_32(header->c_len);
	unit_gt(payload_len, 1);

	zstd_verify_decode(f);
	/* Shorten the declared frame to force a ZSTD decoder error. */
	header->c_len = BE_32(payload_len - 1);
	unit_ne(zfs_zstd_decompress((abd_t *)f->compressed_abd,
	    (abd_t *)f->decoded_abd,
	    f->compressed_len, f->size, 0), 0);

	header->c_len = BE_32(payload_len);
	zstd_verify_decode(f);
	return (MUNIT_OK);
}

static const MunitParameterEnum zstd_params[] = {
	UNIT_PARAM("size", "131072", "1048576"),
	UNIT_PARAM("level", "1", "3", "19"),
	{ 0 },
};

static const MunitTest zstd_tests[] = {
	{ "reuse", test_zstd_reuse, zstd_setup, zstd_teardown,
	    MUNIT_TEST_OPTION_NONE, (MunitParameterEnum *)zstd_params },
	{ "decoder_error", test_zstd_decoder_error, zstd_setup, zstd_teardown,
	    MUNIT_TEST_OPTION_NONE, (MunitParameterEnum *)zstd_params },
	{ 0 },
};

static const MunitSuite zstd_test_suite = {
	"zstd.",
	zstd_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&zstd_test_suite, NULL, argc, argv));
}
