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
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */

/*
 * Copyright (c) 2015 by Witaut Bajaryn. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/compress.h>
#include <sys/spa.h>
#include <sys/zfeature.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>

/*
 * Compression vectors.
 */
zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS] = {
	{"inherit",	0,	NULL,			BP_COMPRESS_INHERIT,
	    SPA_FEATURE_NONE},
	{"on",		0,	NULL,			BP_COMPRESS_ON,
	    SPA_FEATURE_NONE},
	{"uncompressed", 0,	NULL,			BP_COMPRESS_OFF,
	    SPA_FEATURE_NONE},
	{"lzjb",	0,	lzjb_compress,		BP_COMPRESS_LZJB,
	    SPA_FEATURE_NONE},
	{"empty",	0,	NULL,			BP_COMPRESS_EMPTY,
	    SPA_FEATURE_NONE},
	{"gzip-1",	1,	gzip_compress,		BP_COMPRESS_GZIP_1,
	    SPA_FEATURE_NONE},
	{"gzip-2",	2,	gzip_compress,		BP_COMPRESS_GZIP_2,
	    SPA_FEATURE_NONE},
	{"gzip-3",	3,	gzip_compress,		BP_COMPRESS_GZIP_3,
	    SPA_FEATURE_NONE},
	{"gzip-4",	4,	gzip_compress,		BP_COMPRESS_GZIP_4,
	    SPA_FEATURE_NONE},
	{"gzip-5",	5,	gzip_compress,		BP_COMPRESS_GZIP_5,
	    SPA_FEATURE_NONE},
	{"gzip-6",	6,	gzip_compress,		BP_COMPRESS_GZIP_6,
	    SPA_FEATURE_NONE},
	{"gzip-7",	7,	gzip_compress,		BP_COMPRESS_GZIP_7,
	    SPA_FEATURE_NONE},
	{"gzip-8",	8,	gzip_compress,		BP_COMPRESS_GZIP_8,
	    SPA_FEATURE_NONE},
	{"gzip-9",	9,	gzip_compress,		BP_COMPRESS_GZIP_9,
	    SPA_FEATURE_NONE},
	{"zle",		64,	zle_compress,		BP_COMPRESS_ZLE,
	    SPA_FEATURE_NONE},
	{"lz4",		0,	lz4_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-1",	1,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-2",	2,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-3",	3,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-4",	4,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-5",	5,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-6",	6,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-7",	7,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-8",	8,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-9",	9,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-10",	10,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-11",	11,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-12",	12,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-13",	13,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-14",	14,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-15",	15,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
	{"lz4hc-16",	16,	lz4hc_compress_zfs,	BP_COMPRESS_LZ4,
	    SPA_FEATURE_NONE},
};

zio_decompress_info_t zio_decompress_table[BP_COMPRESS_VALUES] = {
	{"inherit",		0,	NULL},
	{"on",			0,	NULL},
	{"uncompressed",	0,	NULL},
	{"lzjb",		0,	lzjb_decompress},
	{"empty",		0,	NULL},
	{"gzip-1",		1,	gzip_decompress},
	{"gzip-2",		2,	gzip_decompress},
	{"gzip-3",		3,	gzip_decompress},
	{"gzip-4",		4,	gzip_decompress},
	{"gzip-5",		5,	gzip_decompress},
	{"gzip-6",		6,	gzip_decompress},
	{"gzip-7",		7,	gzip_decompress},
	{"gzip-8",		8,	gzip_decompress},
	{"gzip-9",		9,	gzip_decompress},
	{"zle",			64,	zle_decompress},
	{"lz4",			0,	lz4_decompress_zfs},
};

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

/*ARGSUSED*/
static int
zio_compress_zeroed_cb(void *data, size_t len, void *private)
{
	uint64_t *end = (uint64_t *)((char *)data + len);
	uint64_t *word;

	for (word = data; word < end; word++)
		if (*word != 0)
			return (1);

	return (0);
}

size_t
zio_compress_data(enum zio_compress c, abd_t *src, void *dst, size_t s_len)
{
	size_t c_len, d_len;
	zio_compress_info_t *ci = &zio_compress_table[c];
	void *tmp;

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

	/* No compression algorithms can read from ABDs directly */
	tmp = abd_borrow_buf_copy(src, s_len);
	c_len = ci->ci_compress(tmp, dst, s_len, d_len, ci->ci_level);
	abd_return_buf(src, tmp, s_len);

	if (c_len > d_len)
		return (s_len);

	ASSERT3U(c_len, <=, d_len);
	return (c_len);
}

int
zio_decompress_data_buf(enum bp_compress c, void *src, void *dst,
    size_t s_len, size_t d_len)
{
	zio_decompress_info_t *di = &zio_decompress_table[c];
	if ((uint_t)c >= BP_COMPRESS_VALUES || di->di_decompress == NULL)
		return (SET_ERROR(EINVAL));

	return (di->di_decompress(src, dst, s_len, d_len, di->di_level));
}

int
zio_decompress_data(enum bp_compress c, abd_t *src, void *dst,
    size_t s_len, size_t d_len)
{
	void *tmp = abd_borrow_buf_copy(src, s_len);
	int ret = zio_decompress_data_buf(c, tmp, dst, s_len, d_len);
	abd_return_buf(src, tmp, s_len);

	return (ret);
}
