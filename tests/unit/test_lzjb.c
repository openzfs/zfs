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
 * Copyright (c) 2026, Tigerblue77.
 */

#include <sys/zfs_context.h>
#include <sys/abd.h>
#include <sys/abd_impl.h>
#include <sys/zio_compress.h>
#include <sys/mman.h>
#include <unistd.h>

#include "unit.h"

/*
 * lzjb.c only borrows linear buffers from the ABDs it is given, so these
 * stand-ins are all of the ABD interface it needs.
 */
void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	(void) n;
	return (ABD_LINEAR_BUF(abd));
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t n)
{
	(void) n;
	return (ABD_LINEAR_BUF(abd));
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

static abd_t *
buf_abd(abd_t *abd, void *buf, size_t size)
{
	memset(abd, 0, sizeof (*abd));
	abd->abd_flags = ABD_FLAG_LINEAR;
	abd->abd_size = size;
	ABD_LINEAR_BUF(abd) = buf;
	return (abd);
}

static size_t
lzjb_compress(void *src, size_t s_len, void *dst, size_t d_len)
{
	abd_t sabd, dabd;

	return (zfs_lzjb_compress(buf_abd(&sabd, src, s_len),
	    buf_abd(&dabd, dst, d_len), s_len, d_len, 0));
}

static int
lzjb_decompress(void *src, size_t s_len, void *dst, size_t d_len)
{
	abd_t sabd, dabd;

	return (zfs_lzjb_decompress(buf_abd(&sabd, src, s_len),
	    buf_abd(&dabd, dst, d_len), s_len, d_len, 0));
}

/*
 * A source buffer whose last byte sits just before an inaccessible page,
 * so that reading past the length the decompressor was given faults.
 */
typedef struct {
	uint8_t	*g_map;
	size_t	g_maplen;
	uint8_t	*g_buf;
} guarded_t;

static void
guarded_alloc(guarded_t *g, const void *data, size_t len)
{
	size_t pg = sysconf(_SC_PAGESIZE);
	size_t datapages = P2ROUNDUP(MAX(len, 1), pg);

	g->g_maplen = datapages + pg;
	g->g_map = mmap(NULL, g->g_maplen, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	unit_true(g->g_map != MAP_FAILED);
	unit_ok(mprotect(g->g_map + datapages, pg, PROT_NONE));
	g->g_buf = g->g_map + datapages - len;
	memcpy(g->g_buf, data, len);
}

static void
guarded_free(guarded_t *g)
{
	unit_ok(munmap(g->g_map, g->g_maplen));
}

/* Data that compresses: runs of repeated text with some noise in it. */
static void
fill_compressible(uint8_t *buf, size_t len)
{
	static const char text[] = "the quick brown fox jumps over the lazy";

	for (size_t i = 0; i < len; i++) {
		buf[i] = text[i % (sizeof (text) - 1)];
		if (munit_rand_int_range(0, 15) == 0)
			buf[i] = (uint8_t)munit_rand_uint32();
	}
}

static const size_t sizes[] = {
	512, 1000, 4096, 4097, 65536, 131072, 1048576
};

/*
 * Whatever the compressor produces decompresses from exactly its own
 * length: the decoder never needs a byte beyond it.
 */
static MunitResult
test_lzjb_roundtrip(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
		size_t len = sizes[i];
		uint8_t *orig = kmem_alloc(len, KM_SLEEP);
		uint8_t *comp = kmem_alloc(len, KM_SLEEP);
		uint8_t *out = kmem_alloc(len, KM_SLEEP);
		guarded_t g;

		fill_compressible(orig, len);
		size_t c_len = lzjb_compress(orig, len, comp, len);
		unit_lt(c_len, len);

		guarded_alloc(&g, comp, c_len);
		unit_ok(lzjb_decompress(g.g_buf, c_len, out, len));
		unit_zero(memcmp(orig, out, len));

		/* a shorter output needs no more than a prefix of it */
		unit_ok(lzjb_decompress(g.g_buf, c_len, out, len / 2));
		unit_zero(memcmp(orig, out, len / 2));
		guarded_free(&g);

		kmem_free(out, len);
		kmem_free(comp, len);
		kmem_free(orig, len);
	}

	return (MUNIT_OK);
}

/* A stream cut short is an error, not a read past its end. */
static MunitResult
test_lzjb_truncated(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
		size_t len = sizes[i];
		uint8_t *orig = kmem_alloc(len, KM_SLEEP);
		uint8_t *comp = kmem_alloc(len, KM_SLEEP);
		uint8_t *out = kmem_alloc(len, KM_SLEEP);

		fill_compressible(orig, len);
		size_t c_len = lzjb_compress(orig, len, comp, len);
		unit_lt(c_len, len);

		for (size_t cut = 1; cut <= MIN(c_len, 24); cut++) {
			guarded_t g;

			guarded_alloc(&g, comp, c_len - cut);
			unit_eq(lzjb_decompress(g.g_buf, c_len - cut, out,
			    len), -1);
			guarded_free(&g);
		}

		kmem_free(out, len);
		kmem_free(comp, len);
		kmem_free(orig, len);
	}

	return (MUNIT_OK);
}

/*
 * A well-formed stream of literals that is longer than the source length
 * claims must not be read beyond that length.
 */
static MunitResult
test_lzjb_overlong(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	size_t s_len = 4096, d_len = 2 * s_len;
	uint8_t *src = kmem_alloc(s_len, KM_SLEEP);
	uint8_t *out = kmem_alloc(d_len, KM_SLEEP);
	guarded_t g;

	/* a copymap byte announcing eight literals, then the literals */
	for (size_t i = 0; i < s_len; i++)
		src[i] = (i % 9 == 0) ? 0 : 'A';

	guarded_alloc(&g, src, s_len);
	unit_eq(lzjb_decompress(g.g_buf, s_len, out, d_len), -1);
	unit_eq(lzjb_decompress(g.g_buf, 0, out, 1), -1);
	guarded_free(&g);

	kmem_free(out, d_len);
	kmem_free(src, s_len);

	return (MUNIT_OK);
}

/* A match at offset 0 would copy output bytes not written yet. */
static MunitResult
test_lzjb_offset_zero(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	/* copymap: literal, then match; the match has offset 0 */
	uint8_t src[] = { 0x02, 'A', 0x00, 0x00 };
	uint8_t out[4];

	memset(out, 0xaa, sizeof (out));
	unit_eq(lzjb_decompress(src, sizeof (src), out, sizeof (out)), -1);

	/* the same match at offset 1 repeats the literal */
	src[3] = 0x01;
	unit_ok(lzjb_decompress(src, sizeof (src), out, sizeof (out)));
	unit_zero(memcmp(out, "AAAA", sizeof (out)));

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest lzjb_tests[] = {
	UNIT_TEST("roundtrip",		test_lzjb_roundtrip),
	UNIT_TEST("truncated",		test_lzjb_truncated),
	UNIT_TEST("overlong",		test_lzjb_overlong),
	UNIT_TEST("offset_zero",	test_lzjb_offset_zero),
	{ 0 },
};

static const MunitSuite lzjb_test_suite = {
	"lzjb.",
	lzjb_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&lzjb_test_suite, NULL, argc, argv));
}
