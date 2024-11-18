/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2019, 2024, Klara, Inc.
 * Copyright (c) 2019, Allan Jude
 * Copyright (c) 2021, 2024 by George Melikov. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zfeature.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/zstd/zstd.h>

/*
 * If nonzero, every 1/X decompression attempts will fail, simulating
 * an undetected memory error.
 */
static unsigned long zio_decompress_fail_fraction = 0;

/*
 * Compression vectors.
 *
 * NOTE: DO NOT CHANGE THE NAMES OF THESE COMPRESSION FUNCTIONS.
 * THEY ARE USED AS ZAP KEY NAMES BY FAST DEDUP AND THEREFORE
 * PART OF THE ON-DISK FORMAT.
 */
zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS] = {
	{"inherit",	0,	NULL,	NULL, NULL},
	{"on",		0,	NULL,	NULL, NULL},
	{"uncompressed", 0,	NULL,	NULL, NULL},
	{"lzjb",	0,
	    zfs_lzjb_compress,	zfs_lzjb_decompress, NULL},
	{"empty",	0,	NULL,	NULL, NULL},
	{"gzip-1",	1,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-2",	2,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-3",	3,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-4",	4,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-5",	5,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-6",	6,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-7",	7,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-8",	8,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"gzip-9",	9,
	    zfs_gzip_compress,	zfs_gzip_decompress, NULL},
	{"zle",		64,
	    zfs_zle_compress,	zfs_zle_decompress, NULL},
	{"lz4",		0,
	    zfs_lz4_compress,	zfs_lz4_decompress, NULL},
	{"zstd",	ZIO_ZSTD_LEVEL_DEFAULT,
	    zfs_zstd_compress,	zfs_zstd_decompress, zfs_zstd_decompress_level},
};

uint8_t
zio_complevel_select(spa_t *spa, enum zio_compress compress, uint8_t child,
    uint8_t parent)
{
	(void) spa;
	uint8_t result;

	if (!ZIO_COMPRESS_HASLEVEL(compress))
		return (0);

	result = child;
	if (result == ZIO_COMPLEVEL_INHERIT)
		result = parent;

	return (result);
}

enum zio_compress
zio_compress_select(spa_t *spa, enum zio_compress child,
    enum zio_compress parent)
{
	enum zio_compress result;

	ASSERT(child < ZIO_COMPRESS_FUNCTIONS);
	ASSERT(parent < ZIO_COMPRESS_FUNCTIONS);
	ASSERT(parent != ZIO_COMPRESS_INHERIT);

	result = child;
	if (result == ZIO_COMPRESS_INHERIT)
		result = parent;

	if (result == ZIO_COMPRESS_ON) {
		if (spa_feature_is_active(spa, SPA_FEATURE_LZ4_COMPRESS))
			result = ZIO_COMPRESS_LZ4_ON_VALUE;
		else
			result = ZIO_COMPRESS_LEGACY_ON_VALUE;
	}

	return (result);
}

/*
 * Compress `s_len` bytes of `src` using compression method `c`. Returns the
 * length of the compressed data.
 *
 * If the returned value == `s_len`, then compression was not possible.  If the
 * returned value is < `s_len`, compression succeeded, and `*dst` must be
 * inspected to get the result.  Note that a return value of 0 is valid,
 * indicating that the data can be compressed to a "hole".
 *
 * If `*dst` not NULL, it must point to an ABD large enough to hold the
 * compressed data. An ABD of size `s_len` is guaranteed to always be large
 * enough. Note that even if compression is not possible (return == `s_len`),
 * the ABD in `*dst` may have been modified; don't rely on its contents after
 * a call to zio_compress_data().
 *
 * If `*dst` is NULL, and the compression method requires a destination buffer,
 * then an ABD of size `s_len` will be allocated and the compressed data
 * placed there. It is the caller's responsibility to free this ABD.
 *
 * If `*dst` is NULL and the compression method does not require a destination
 * buffer (ZIO_COMPRESS_INPLACE(c) is true), then on successful return *dst
 * will be NULL and the result will be in `sabd`. The original data is lost.
 *
 * Note that ABD supplied in `*dst` will _always_ be used for output, and will
 * leave `sabd` untouched.
 *
 * Typical use (dest ABD created on demand):
 *
 *     abd_t *sabd;
 *     size_t s_len = get_uncompressed_data(&sabd, ...);
 *
 *     abd_t *dabd = NULL;
 *     size_t c_len = zio_compress_data(c, sabd, &dabd, s_len, ..., ...);
 *     if (c_len == s_len) {
 *         // Data uncompressable
 *     } else if (dabd != NULL) {
 *         // Compressed, ABD was allocated for us, discard the original.
 *         sabd = dabd;
 *         s_len = c_len;
 *         abd_free(sabd);
 *     } else {
 *         // Compressed in place, use original ABD with new size.
 *         s_len = c_len;
 *     }
 *
 *     use_compressed_data(sabd, s_len);
 *
 * Typical use (dest ABD supplied by caller):
 *
 *     abd_t *sabd;
 *     size_t s_len = get_uncompressed_data(&sabd, ...);
 *
 *     abd_t *dabd = abd_alloc_sametype(sabd, s_len);
 *     size_t c_len = zio_compress_data(c, sabd, &dabd, s_len, ..., ...);
 *     if (c_len == s_len) {
 *        // Data uncompressable
 *        abd_free(dabd, s_len);
 *     } else {
 *        // Compressed data available, discard the original.
 *        sabd = dabd;
 *        s_len = c_len;
 *        abd_free(sabd);
 *     }
 *
 *     use_compressed_data(sabd, s_len);
 */
size_t
zio_compress_data(enum zio_compress c, abd_t *src, abd_t **dstp, size_t s_len,
    size_t d_len, uint8_t level)
{
	size_t c_len;
	uint8_t complevel;
	zio_compress_info_t *ci = &zio_compress_table[c];

	ASSERT3U(ci->ci_compress, !=, NULL);
	ASSERT3U(s_len, >, 0);

	complevel = ci->ci_level;

	if (c == ZIO_COMPRESS_ZSTD) {
		/* If we don't know the level, we can't compress it */
		if (level == ZIO_COMPLEVEL_INHERIT)
			return (s_len);

		if (level == ZIO_COMPLEVEL_DEFAULT)
			complevel = ZIO_ZSTD_LEVEL_DEFAULT;
		else
			complevel = level;

		ASSERT3U(complevel, !=, ZIO_COMPLEVEL_INHERIT);
	}

	abd_t *dst = *dstp;
	if (dst == NULL)
		dst = abd_alloc_sametype(src, s_len);

	c_len = ci->ci_compress(src, dst, s_len, d_len, complevel);

	if (c_len > d_len) {
		if (*dstp == NULL)
			abd_free(dst);
		return (s_len);
	}

	if (*dstp == NULL)
		*dstp = dst;

	return (c_len);
}

int
zio_decompress_data(enum zio_compress c, abd_t *src, abd_t *dst,
    size_t s_len, size_t d_len, uint8_t *level)
{
	zio_compress_info_t *ci = &zio_compress_table[c];
	if ((uint_t)c >= ZIO_COMPRESS_FUNCTIONS || ci->ci_decompress == NULL)
		return (SET_ERROR(EINVAL));

	int err;
	if (ci->ci_decompress_level != NULL && level != NULL)
		err = ci->ci_decompress_level(src, dst, s_len, d_len, level);
	else
		err = ci->ci_decompress(src, dst, s_len, d_len, ci->ci_level);

	/*
	 * Decompression shouldn't fail, because we've already verified
	 * the checksum.  However, for extra protection (e.g. against bitflips
	 * in non-ECC RAM), we handle this error (and test it).
	 */
	if (zio_decompress_fail_fraction != 0 &&
	    random_in_range(zio_decompress_fail_fraction) == 0)
		err = SET_ERROR(EINVAL);

	return (err);
}

int
zio_compress_to_feature(enum zio_compress comp)
{
	switch (comp) {
	case ZIO_COMPRESS_ZSTD:
		return (SPA_FEATURE_ZSTD_COMPRESS);
	default:
		break;
	}
	return (SPA_FEATURE_NONE);
}
