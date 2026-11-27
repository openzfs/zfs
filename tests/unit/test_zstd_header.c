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
 * Unit tests for the ZFS framing of ZSTD blocks: the 8-byte header
 * (zfs_zstdhdr_t) that zfs_zstd_decompress() reads before the ZSTD frame.
 */

#include <sys/zfs_context.h>
#include <sys/abd.h>
#include <sys/bitops.h>
#include <sys/zio_compress.h>
#include <sys/zstd/zstd.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "unit.h"

/* ========== */

/*
 * zfs_zstd_decompress() only borrows and returns linear buffers, so a mock
 * ABD is a buffer and its size.
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
 * The compress path of zfs_zstd.c (its early-abort LZ4 probe and the ABD
 * helpers it uses) is linked in but never reached by these tests, which
 * exercise only zfs_zstd_decompress(); stub it out so the test need not
 * pull in libzpool.
 */
abd_t *
abd_get_from_buf_struct(abd_t *abd, void *buf, size_t size)
{
	(void) buf, (void) size;
	return (abd);
}

void
abd_free(abd_t *abd)
{
	(void) abd;
}

size_t
zfs_lz4_compress(abd_t *src, abd_t *dst, size_t s_len, size_t d_len, int n)
{
	(void) src, (void) dst, (void) d_len, (void) n;
	return (s_len);
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
	return (zfs_zstd_decompress((abd_t *)&sabd, (abd_t *)&dabd,
	    s_len, d_len, 0));
}

#define	BLKSZ	4096

static uint8_t obuf[BLKSZ];

/* ========== */

/*
 * A source shorter than the 8-byte header is rejected without reading it.
 * sizeof (zfs_zstdhdr_t) is 8: c_len (4) then raw_version_level (4).
 */
static MunitResult
test_zstd_short_source(const MunitParameter params[], void *data)
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

/*
 * Write a header with a valid level, as the compressor does, so that only
 * the c_len check can reject it.  The decompressor ignores the version.
 */
static void
put_header(uint8_t *src, uint32_t c_len)
{
	zfs_zstdhdr_t hdr;

	hdr.c_len = BE_32(c_len);
	hdr.raw_version_level = 0;
	zfs_set_hdrlevel(&hdr, ZIO_ZSTD_LEVEL_3);
	hdr.raw_version_level = BE_32(hdr.raw_version_level);
	memcpy(src, &hdr, sizeof (hdr));
}

/*
 * An 8-byte header whose c_len claims more than the source holds is
 * rejected, and c_len == UINT32_MAX must not wrap around the length check.
 */
static MunitResult
test_zstd_header_too_long(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* c_len = 1, but no payload follows. */
	uint8_t *src = guarded_alloc(8);
	put_header(src, 1);
	unit_ne(decompress(src, 8, obuf, BLKSZ), 0);
	guarded_free(src, 8);

	src = guarded_alloc(8);
	put_header(src, UINT32_MAX);
	unit_ne(decompress(src, 8, obuf, BLKSZ), 0);
	guarded_free(src, 8);

	return (MUNIT_OK);
}

/* ========== */

static const MunitParameterEnum short_source_params[] = {
	UNIT_PARAM("s_len", "0", "1", "4", "7"),
	{ 0 },
};

static const MunitTest zstd_tests[] = {
	UNIT_TEST("short_source",	test_zstd_short_source,
	    short_source_params),
	UNIT_TEST("header_too_long",	test_zstd_header_too_long),
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
