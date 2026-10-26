// SPDX-License-Identifier: BSD-2-Clause
/*
 * LZ4 - Fast LZ compression algorithm
 * Header File
 * Copyright (C) 2011-2013, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
 * - LZ4 source repository : http://code.google.com/p/lz4/
 */

/*
 * OpenZFS glue for the LZ4 library in lib/, which is the unmodified
 * upstream release (see README.md).
 *
 * The library is built in this one translation unit, with internal
 * linkage throughout, so that it cannot clash with another copy of LZ4
 * in the same kernel, and so that the compiler drops what ZFS does not
 * call.  Its functions that allocate memory are compiled out: the
 * compressor's state comes from a kmem cache, as it always has.
 */

#include <sys/zfs_context.h>
#include <sys/zio_compress.h>

#define	LZ4LIB_VISIBILITY	static
#define	LZ4_STATIC_LINKING_ONLY
#define	LZ4_PUBLISH_STATIC_FUNCTIONS
#define	LZ4_STATIC_LINKING_ONLY_DISABLE_MEMORY_ALLOCATION
#define	LZ4_DISABLE_DEPRECATE_WARNINGS

/*
 * Most of the library goes unused here, and the functions that allocate
 * memory are declared but compiled out.
 */
#pragma GCC diagnostic ignored "-Wunused-function"

/*
 * The library defines these few functions outside of LZ4LIB_API.  A
 * static declaration ahead of its own gives them internal linkage too.
 */
static int LZ4_compress_destSize_extState(void *, const char *, char *,
    int *, int, int);
#include "lib/lz4.h"
static int LZ4_compress_forceExtDict(LZ4_stream_t *, const char *, char *,
    int);
static int LZ4_decompress_safe_forceExtDict(const char *, char *, int, int,
    const void *, size_t);
static int LZ4_decompress_safe_partial_forceExtDict(const char *, char *,
    int, int, int, const void *, size_t);
/* This one takes a type private to lz4.c, so it is renamed instead. */
#define	LZ4_loadDict_internal	zfs_LZ4_loadDict_internal

/*
 * The library's own assert()s are built out, as in its release builds,
 * and it defines expect() as the SPL does, but unconditionally.  Linux
 * defines current as a macro, and the library names variables current.
 */
#undef assert
#undef expect
#pragma push_macro("current")
#undef current
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include "lib/lz4.c"
#pragma GCC diagnostic pop
#pragma pop_macro("current")

static kmem_cache_t *lz4_cache;

static size_t
zfs_lz4_compress_buf(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	(void) n;
	uint32_t bufsiz;
	char *dest = d_start;
	void *state;

	ASSERT(d_len >= sizeof (bufsiz));
	ASSERT3U(s_len, <=, LZ4_MAX_INPUT_SIZE);

	/*
	 * Acceleration 1 is LZ4's default compressor.  _extState() clears
	 * the whole state first, so that the output depends on the input
	 * alone; its _fastReset() variant would let a reused state change
	 * the output.
	 */
	state = kmem_cache_alloc(lz4_cache, KM_SLEEP);
	bufsiz = LZ4_compress_fast_extState(state, s_start,
	    &dest[sizeof (bufsiz)], s_len, d_len - sizeof (bufsiz), 1);
	kmem_cache_free(lz4_cache, state);

	/* Signal an error if the compression routine returned zero. */
	if (bufsiz == 0)
		return (s_len);

	/*
	 * The exact compressed size is needed by the decompression routine,
	 * so it is stored at the start of the buffer. Note that this may be
	 * less than the compressed block size, which is rounded up to a
	 * multiple of 1<<ashift.
	 */
	*(uint32_t *)dest = BE_32(bufsiz);

	return (bufsiz + sizeof (bufsiz));
}

static int
zfs_lz4_decompress_buf(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	(void) n;
	const char *src = s_start;
	uint32_t bufsiz;
	int decoded;

	/* too short to hold the compressed buffer size */
	if (s_len < sizeof (bufsiz))
		return (1);

	/* invalid compressed buffer size encoded at start */
	bufsiz = BE_IN32(src);
	if (bufsiz > s_len - sizeof (bufsiz))
		return (1);

	/*
	 * LZ4_decompress_safe returns the number of bytes decoded on
	 * success, or a negative value on failure. An OpenZFS block must
	 * expand to exactly d_len bytes
	 */
	decoded = LZ4_decompress_safe(&src[sizeof (bufsiz)], d_start, bufsiz,
	    d_len);
	if (decoded < 0)
		return (1);
	if (d_len != (size_t)decoded)
		return (1);
	return (0);
}

ZFS_COMPRESS_WRAP_DECL(zfs_lz4_compress)
ZFS_DECOMPRESS_WRAP_DECL(zfs_lz4_decompress)

void
lz4_init(void)
{
	lz4_cache = kmem_cache_create("lz4_cache",
	    sizeof (LZ4_stream_t), 0, NULL, NULL, NULL, NULL, NULL,
	    KMC_RECLAIMABLE);
}

void
lz4_fini(void)
{
	if (lz4_cache) {
		kmem_cache_destroy(lz4_cache);
		lz4_cache = NULL;
	}
}
