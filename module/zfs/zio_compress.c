/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 */
/*
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 */

/*
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2019, Klara Inc.
 * Copyright (c) 2019, Allan Jude
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
unsigned long zio_decompress_fail_fraction = 0;

/*
 * Compression vectors.
 */
const zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS] = {
	{"inherit",	0,	NULL,		NULL, NULL},
	{"on",		0,	NULL,		NULL, NULL},
	{"uncompressed", 0,	NULL,		NULL, NULL},
	{"lzjb",	0,	lzjb_compress,	lzjb_decompress, NULL},
	{"empty",	0,	NULL,		NULL, NULL},
	{"gzip-1",	1,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-2",	2,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-3",	3,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-4",	4,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-5",	5,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-6",	6,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-7",	7,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-8",	8,	gzip_compress,	gzip_decompress, NULL},
	{"gzip-9",	9,	gzip_compress,	gzip_decompress, NULL},
	{"zle",		64,	zle_compress,	zle_decompress, NULL},
	{"lz4",		0,	lz4_compress_zfs, lz4_decompress_zfs, NULL},
	{"zstd",	ZIO_ZSTD_LEVEL_DEFAULT,	zfs_zstd_compress_wrap,
	    zfs_zstd_decompress, zfs_zstd_decompress_level},
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

static int
zio_compress_zeroed_cb(void *data, size_t len, void *private)
{
	(void) private;

	uint64_t *end = (uint64_t *)((char *)data + len);
	for (uint64_t *word = (uint64_t *)data; word < end; word++)
		if (*word != 0)
			return (1);

	return (0);
}

size_t
zio_compress_data(enum zio_compress c, abd_t *src, void *dst, size_t s_len,
    uint8_t level)
{
	size_t c_len, d_len;
	uint8_t complevel;
	zio_compress_info_t *ci = &zio_compress_table[c];

	ASSERT((uint_t)c < ZIO_COMPRESS_FUNCTIONS);
	ASSERT((uint_t)c == ZIO_COMPRESS_EMPTY || ci->ci_compress != NULL);

	/*
	 * If the data is all zeroes, we don't even need to allocate
	 * a block for it.  We indicate this by returning zero size.
	 */
	if (abd_iterate_func(src, 0, s_len, zio_compress_zeroed_cb, NULL) == 0)
		return (0);

	if (c == ZIO_COMPRESS_EMPTY)
		return (s_len);

	/* Compress at least 12.5% */
	d_len = s_len - (s_len >> 3);

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

	/* No compression algorithms can read from ABDs directly */
	void *tmp = abd_borrow_buf_copy(src, s_len);
	c_len = ci->ci_compress(tmp, dst, s_len, d_len, complevel);
	abd_return_buf(src, tmp, s_len);

	if (c_len > d_len)
		return (s_len);

	ASSERT3U(c_len, <=, d_len);
	return (c_len);
}

int
zio_decompress_data_buf(enum zio_compress c, void *src, void *dst,
    size_t s_len, size_t d_len, uint8_t *level)
{
	zio_compress_info_t *ci = &zio_compress_table[c];
	if ((uint_t)c >= ZIO_COMPRESS_FUNCTIONS || ci->ci_decompress == NULL)
		return (SET_ERROR(EINVAL));

	if (ci->ci_decompress_level != NULL && level != NULL)
		return (ci->ci_decompress_level(src, dst, s_len, d_len, level));

	return (ci->ci_decompress(src, dst, s_len, d_len, ci->ci_level));
}

int
zio_decompress_data(enum zio_compress c, abd_t *src, void *dst,
    size_t s_len, size_t d_len, uint8_t *level)
{
	void *tmp = abd_borrow_buf_copy(src, s_len);
	int ret = zio_decompress_data_buf(c, tmp, dst, s_len, d_len, level);
	abd_return_buf(src, tmp, s_len);

	/*
	 * Decompression shouldn't fail, because we've already verified
	 * the checksum.  However, for extra protection (e.g. against bitflips
	 * in non-ECC RAM), we handle this error (and test it).
	 */
	if (zio_decompress_fail_fraction != 0 &&
	    random_in_range(zio_decompress_fail_fraction) == 0)
		ret = SET_ERROR(EINVAL);

	return (ret);
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
