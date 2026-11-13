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
 * Unit tests for the ZFS framing of LZ4 blocks (module/zfs/lz4_zfs.c): a
 * 4-byte big-endian length, then the LZ4 block.
 */

#include <sys/zfs_context.h>
#include <sys/abd.h>
#include <sys/zio_compress.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "unit.h"

/* ========== */

/*
 * The compression wrappers only borrow and return linear buffers, so a
 * mock ABD is a buffer and its size.
 */
typedef struct mock_abd {
	void	*ma_buf;
	size_t	ma_size;
} mock_abd_t;

void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	mock_abd_t *ma = (mock_abd_t *)abd;
	unit_le(n, ma->ma_size);
	return (ma->ma_buf);
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t n)
{
	return (abd_borrow_buf(abd, n));
}

void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	(void) abd, (void) buf, (void) n;
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	(void) abd, (void) buf, (void) n;
}

/*
 * Place len bytes at the very end of a page whose successor is unmapped,
 * so that any read past them faults.
 */
static uint8_t *
guarded_alloc(size_t len)
{
	size_t pg = (size_t)sysconf(_SC_PAGESIZE);
	uint8_t *p = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	unit_true(p != MAP_FAILED);
	unit_ok(mprotect(p + pg, pg, PROT_NONE));
	return (p + pg - len);
}

static void
guarded_free(uint8_t *buf, size_t len)
{
	size_t pg = (size_t)sysconf(_SC_PAGESIZE);
	unit_ok(munmap(buf + len - pg, 2 * pg));
}

static int
decompress(uint8_t *src, size_t s_len, uint8_t *dst, size_t d_len)
{
	mock_abd_t sabd = { src, s_len };
	mock_abd_t dabd = { dst, d_len };
	return (zfs_lz4_decompress((abd_t *)&sabd, (abd_t *)&dabd,
	    s_len, d_len, 0));
}

#define	BLKSZ	4096

static uint8_t lbuf[BLKSZ], cbuf[BLKSZ], obuf[BLKSZ];

static size_t
compress_block(void)
{
	for (size_t i = 0; i < BLKSZ; i++)
		lbuf[i] = (uint8_t)(i % 7 == 0 ? i : i % 3);
	mock_abd_t sabd = { lbuf, BLKSZ };
	mock_abd_t dabd = { cbuf, BLKSZ };
	abd_t *dp = (abd_t *)&dabd;
	size_t c_len = zfs_lz4_compress((abd_t *)&sabd, dp, BLKSZ,
	    BLKSZ, 0);
	unit_lt(c_len, BLKSZ);
	return (c_len);
}

/* ========== */

/* A source shorter than the length header is rejected without reading it. */
static MunitResult
test_lz4_short_source(const MunitParameter params[], void *data)
{
	(void) data;
	size_t s_len = strtoul(munit_parameters_get(params, "s_len"),
	    NULL, 10);

	uint8_t *src = guarded_alloc(s_len);
	memset(src, 0, s_len);
	unit_ne(decompress(src, s_len, obuf, BLKSZ), 0);
	guarded_free(src, s_len);

	return (MUNIT_OK);
}

/* A block decompresses from exactly the bytes the compressor produced. */
static MunitResult
test_lz4_roundtrip(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	lz4_init();
	size_t c_len = compress_block();

	uint8_t *src = guarded_alloc(c_len);
	memcpy(src, cbuf, c_len);
	memset(obuf, 0xa5, BLKSZ);
	unit_ok(decompress(src, c_len, obuf, BLKSZ));
	unit_ok(memcmp(obuf, lbuf, BLKSZ));
	guarded_free(src, c_len);

	lz4_fini();
	return (MUNIT_OK);
}

/* A length header claiming more than the source holds is rejected. */
static MunitResult
test_lz4_header_too_long(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	lz4_init();
	size_t c_len = compress_block();

	/* Truncate the source by one byte, header unchanged. */
	uint8_t *src = guarded_alloc(c_len - 1);
	memcpy(src, cbuf, c_len - 1);
	unit_ne(decompress(src, c_len - 1, obuf, BLKSZ), 0);
	guarded_free(src, c_len - 1);

	/* A header of UINT32_MAX must not wrap around the length check. */
	src = guarded_alloc(c_len);
	memcpy(src, cbuf, c_len);
	memset(src, 0xff, sizeof (uint32_t));
	unit_ne(decompress(src, c_len, obuf, BLKSZ), 0);
	guarded_free(src, c_len);

	lz4_fini();
	return (MUNIT_OK);
}

/* ========== */

static const MunitParameterEnum short_source_params[] = {
	UNIT_PARAM("s_len", "0", "1", "2", "3"),
	{ 0 },
};

static const MunitTest lz4_tests[] = {
	UNIT_TEST("short_source",	test_lz4_short_source,
	    short_source_params),
	UNIT_TEST("roundtrip",		test_lz4_roundtrip),
	UNIT_TEST("header_too_long",	test_lz4_header_too_long),
	{ 0 },
};

static const MunitSuite lz4_test_suite = {
	"lz4.",
	lz4_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&lz4_test_suite, NULL, argc, argv));
}
