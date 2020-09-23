/*
 * BSD 3-Clause New License (https://spdx.org/licenses/BSD-3-Clause.html)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2016-2018, Klara Inc.
 * Copyright (c) 2016-2018, Allan Jude
 * Copyright (c) 2018-2020, Sebastian Gottschall
 * Copyright (c) 2019-2020, Michael Niewöhner
 * Copyright (c) 2020, The FreeBSD Foundation [1]
 *
 * [1] Portions of this software were developed by Allan Jude
 *     under sponsorship from the FreeBSD Foundation.
 */

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/zio_compress.h>
#include <sys/zstd/zstd.h>
#include <sys/zstd/zstd_impl.h>

#define	ZSTD_STATIC_LINKING_ONLY
#include "lib/zstd.h"
#include "lib/zstd_errors.h"

kstat_t *zstd_ksp = NULL;

zstd_stats_t zstd_stats __maybe_unused = {
	{ "alloc_fail",			KSTAT_DATA_UINT64 },
	{ "alloc_fallback",		KSTAT_DATA_UINT64 },
	{ "compress_alloc_fail",	KSTAT_DATA_UINT64 },
	{ "decompress_alloc_fail",	KSTAT_DATA_UINT64 },
	{ "compress_level_invalid",	KSTAT_DATA_UINT64 },
	{ "decompress_level_invalid",	KSTAT_DATA_UINT64 },
	{ "decompress_header_invalid",	KSTAT_DATA_UINT64 },
	{ "compress_failed",		KSTAT_DATA_UINT64 },
	{ "decompress_failed",		KSTAT_DATA_UINT64 },
	{ "buffers",			KSTAT_DATA_UINT64 },
	{ "size",			KSTAT_DATA_UINT64 },
};

struct zstd_levelmap {
	int16_t zstd_level;
	enum zio_zstd_levels level;
};

/* Compression memory handler */
static const ZSTD_customMem zstd_malloc = {
	zstd_alloc,
	zstd_free,
	NULL,
};

/* Decompression memory handler */
static const ZSTD_customMem zstd_dctx_malloc = {
	zstd_dctx_alloc,
	zstd_free,
	NULL,
};

/* Level map for converting ZFS internal levels to ZSTD levels and vice versa */
static struct zstd_levelmap zstd_levels[] = {
	{ZIO_ZSTD_LEVEL_1, ZIO_ZSTD_LEVEL_1},
	{ZIO_ZSTD_LEVEL_2, ZIO_ZSTD_LEVEL_2},
	{ZIO_ZSTD_LEVEL_3, ZIO_ZSTD_LEVEL_3},
	{ZIO_ZSTD_LEVEL_4, ZIO_ZSTD_LEVEL_4},
	{ZIO_ZSTD_LEVEL_5, ZIO_ZSTD_LEVEL_5},
	{ZIO_ZSTD_LEVEL_6, ZIO_ZSTD_LEVEL_6},
	{ZIO_ZSTD_LEVEL_7, ZIO_ZSTD_LEVEL_7},
	{ZIO_ZSTD_LEVEL_8, ZIO_ZSTD_LEVEL_8},
	{ZIO_ZSTD_LEVEL_9, ZIO_ZSTD_LEVEL_9},
	{ZIO_ZSTD_LEVEL_10, ZIO_ZSTD_LEVEL_10},
	{ZIO_ZSTD_LEVEL_11, ZIO_ZSTD_LEVEL_11},
	{ZIO_ZSTD_LEVEL_12, ZIO_ZSTD_LEVEL_12},
	{ZIO_ZSTD_LEVEL_13, ZIO_ZSTD_LEVEL_13},
	{ZIO_ZSTD_LEVEL_14, ZIO_ZSTD_LEVEL_14},
	{ZIO_ZSTD_LEVEL_15, ZIO_ZSTD_LEVEL_15},
	{ZIO_ZSTD_LEVEL_16, ZIO_ZSTD_LEVEL_16},
	{ZIO_ZSTD_LEVEL_17, ZIO_ZSTD_LEVEL_17},
	{ZIO_ZSTD_LEVEL_18, ZIO_ZSTD_LEVEL_18},
	{ZIO_ZSTD_LEVEL_19, ZIO_ZSTD_LEVEL_19},
	{-1, ZIO_ZSTD_LEVEL_FAST_1},
	{-2, ZIO_ZSTD_LEVEL_FAST_2},
	{-3, ZIO_ZSTD_LEVEL_FAST_3},
	{-4, ZIO_ZSTD_LEVEL_FAST_4},
	{-5, ZIO_ZSTD_LEVEL_FAST_5},
	{-6, ZIO_ZSTD_LEVEL_FAST_6},
	{-7, ZIO_ZSTD_LEVEL_FAST_7},
	{-8, ZIO_ZSTD_LEVEL_FAST_8},
	{-9, ZIO_ZSTD_LEVEL_FAST_9},
	{-10, ZIO_ZSTD_LEVEL_FAST_10},
	{-20, ZIO_ZSTD_LEVEL_FAST_20},
	{-30, ZIO_ZSTD_LEVEL_FAST_30},
	{-40, ZIO_ZSTD_LEVEL_FAST_40},
	{-50, ZIO_ZSTD_LEVEL_FAST_50},
	{-60, ZIO_ZSTD_LEVEL_FAST_60},
	{-70, ZIO_ZSTD_LEVEL_FAST_70},
	{-80, ZIO_ZSTD_LEVEL_FAST_80},
	{-90, ZIO_ZSTD_LEVEL_FAST_90},
	{-100, ZIO_ZSTD_LEVEL_FAST_100},
	{-500, ZIO_ZSTD_LEVEL_FAST_500},
	{-1000, ZIO_ZSTD_LEVEL_FAST_1000},
};

/* Convert ZFS internal enum to ZSTD level */
static int
zstd_enum_to_level(enum zio_zstd_levels level, int16_t *zstd_level)
{
	if (level > 0 && level <= ZIO_ZSTD_LEVEL_19) {
		*zstd_level = zstd_levels[level - 1].zstd_level;
		return (0);
	}
	if (level >= ZIO_ZSTD_LEVEL_FAST_1 &&
	    level <= ZIO_ZSTD_LEVEL_FAST_1000) {
		*zstd_level = zstd_levels[level - ZIO_ZSTD_LEVEL_FAST_1
		    + ZIO_ZSTD_LEVEL_19].zstd_level;
		return (0);
	}

	/* Invalid/unknown zfs compression enum - this should never happen. */
	return (1);
}

/* Compress block using zstd */
size_t
zfs_zstd_compress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int level)
{
	size_t c_len;
	int16_t zstd_level;
	zfs_zstdhdr_t *hdr;
	ZSTD_CCtx *cctx;

	hdr = (zfs_zstdhdr_t *)d_start;

	/* Skip compression if the specified level is invalid */
	if (zstd_enum_to_level(level, &zstd_level)) {
		ZSTDSTAT_BUMP(zstd_stat_com_inval);
		return (s_len);
	}

	ASSERT3U(d_len, >=, sizeof (*hdr));
	ASSERT3U(d_len, <=, s_len);
	ASSERT3U(zstd_level, !=, 0);

	cctx = ZSTD_createCCtx_advanced(zstd_malloc);

	/*
	 * Out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (!cctx) {
		ZSTDSTAT_BUMP(zstd_stat_com_alloc_fail);
		return (s_len);
	}

	/* Set the compression level */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, zstd_level);

	/* Use the "magicless" zstd header which saves us 4 header bytes */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless);

	/*
	 * Disable redundant checksum calculation and content size storage since
	 * this is already done by ZFS itself.
	 */
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 0);

	c_len = ZSTD_compress2(cctx,
	    hdr->data,
	    d_len - sizeof (*hdr),
	    s_start, s_len);

	ZSTD_freeCCtx(cctx);

	/* Error in the compression routine, disable compression. */
	if (ZSTD_isError(c_len)) {
		/*
		 * If we are aborting the compression because the saves are
		 * too small, that is not a failure. Everything else is a
		 * failure, so increment the compression failure counter.
		 */
		if (ZSTD_getErrorCode(c_len) != ZSTD_error_dstSize_tooSmall) {
			ZSTDSTAT_BUMP(zstd_stat_com_fail);
		}
		return (s_len);
	}

	/*
	 * Encode the compressed buffer size at the start. We'll need this in
	 * decompression to counter the effects of padding which might be added
	 * to the compressed buffer and which, if unhandled, would confuse the
	 * hell out of our decompression function.
	 */
	hdr->c_len = BE_32(c_len);

	/*
	 * Check version for overflow.
	 * The limit of 24 bits must not be exceeded. This allows a maximum
	 * version 1677.72.15 which we don't expect to be ever reached.
	 */
	ASSERT3U(ZSTD_VERSION_NUMBER, <=, 0xFFFFFF);

	/*
	 * Encode the compression level as well. We may need to know the
	 * original compression level if compressed_arc is disabled, to match
	 * the compression settings to write this block to the L2ARC.
	 *
	 * Encode the actual level, so if the enum changes in the future, we
	 * will be compatible.
	 *
	 * The upper 24 bits store the ZSTD version to be able to provide
	 * future compatibility, since new versions might enhance the
	 * compression algorithm in a way, where the compressed data will
	 * change.
	 *
	 * As soon as such incompatibility occurs, handling code needs to be
	 * added, differentiating between the versions.
	 */
	hdr->version = ZSTD_VERSION_NUMBER;
	hdr->level = level;
	hdr->raw_version_level = BE_32(hdr->raw_version_level);

	return (c_len + sizeof (*hdr));
}

/* Decompress block using zstd and return its stored level */
int
zfs_zstd_decompress_level(void *s_start, void *d_start, size_t s_len,
    size_t d_len, uint8_t *level)
{
	ZSTD_DCtx *dctx;
	size_t result;
	int16_t zstd_level;
	uint32_t c_len;
	const zfs_zstdhdr_t *hdr;
	zfs_zstdhdr_t hdr_copy;

	hdr = (const zfs_zstdhdr_t *)s_start;
	c_len = BE_32(hdr->c_len);

	/*
	 * Make a copy instead of directly converting the header, since we must
	 * not modify the original data that may be used again later.
	 */
	hdr_copy.raw_version_level = BE_32(hdr->raw_version_level);

	/*
	 * NOTE: We ignore the ZSTD version for now. As soon as any
	 * incompatibility occurrs, it has to be handled accordingly.
	 * The version can be accessed via `hdr_copy.version`.
	 */

	/*
	 * Convert and check the level
	 * An invalid level is a strong indicator for data corruption! In such
	 * case return an error so the upper layers can try to fix it.
	 */
	if (zstd_enum_to_level(hdr_copy.level, &zstd_level)) {
		ZSTDSTAT_BUMP(zstd_stat_dec_inval);
		return (1);
	}

	ASSERT3U(d_len, >=, s_len);
	ASSERT3U(hdr_copy.level, !=, ZIO_COMPLEVEL_INHERIT);

	/* Invalid compressed buffer size encoded at start */
	if (c_len + sizeof (*hdr) > s_len) {
		ZSTDSTAT_BUMP(zstd_stat_dec_header_inval);
		return (1);
	}

	dctx = ZSTD_createDCtx_advanced(zstd_dctx_malloc);
	if (!dctx) {
		ZSTDSTAT_BUMP(zstd_stat_dec_alloc_fail);
		return (1);
	}

	/* Set header type to "magicless" */
	ZSTD_DCtx_setParameter(dctx, ZSTD_d_format, ZSTD_f_zstd1_magicless);

	/* Decompress the data and release the context */
	result = ZSTD_decompressDCtx(dctx, d_start, d_len, hdr->data, c_len);
	ZSTD_freeDCtx(dctx);

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	if (ZSTD_isError(result)) {
		ZSTDSTAT_BUMP(zstd_stat_dec_fail);
		return (1);
	}

	if (level) {
		*level = hdr_copy.level;
	}

	return (0);
}

/* Decompress datablock using zstd */
int
zfs_zstd_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len,
    int level __maybe_unused)
{

	return (zfs_zstd_decompress_level(s_start, d_start, s_len, d_len,
	    NULL));
}

extern int __init
zstd_init(void)
{
	int ret;

	ret = zstd_init_os();
	if (ret)
		return (ret);

	/* Initialize kstat */
	zstd_ksp = kstat_create("zfs", 0, "zstd", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zstd_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (zstd_ksp != NULL) {
		zstd_ksp->ks_data = &zstd_stats;
		kstat_install(zstd_ksp);
	}

	return (0);
}

extern void __exit
zstd_fini(void)
{
	/* Deinitialize kstat */
	if (zstd_ksp != NULL) {
		kstat_delete(zstd_ksp);
		zstd_ksp = NULL;
	}

	zstd_fini_os();
}

#if defined(_KERNEL)
module_init(zstd_init);
module_exit(zstd_fini);

ZFS_MODULE_DESCRIPTION("ZSTD Compression for ZFS");
ZFS_MODULE_LICENSE("Dual BSD/GPL");
ZFS_MODULE_VERSION(ZSTD_VERSION_STRING);

EXPORT_SYMBOL(zfs_zstd_compress);
EXPORT_SYMBOL(zfs_zstd_decompress_level);
EXPORT_SYMBOL(zfs_zstd_decompress);
EXPORT_SYMBOL(zfs_zstd_cache_reap_now);
#endif
