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
 * Copyright (c) 2015, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2015 by Witaut Bajaryn. All rights reserved.
 */

#ifndef _SYS_ZIO_COMPRESS_H
#define	_SYS_ZIO_COMPRESS_H

#include <sys/abd.h>
#include <zfeature_common.h>

#ifdef	__cplusplus
extern "C" {
#endif

enum zio_compress {
	ZIO_COMPRESS_INHERIT = 0,
	ZIO_COMPRESS_ON,
	ZIO_COMPRESS_OFF,
	ZIO_COMPRESS_LZJB,
	ZIO_COMPRESS_EMPTY,
	ZIO_COMPRESS_GZIP_1,
	ZIO_COMPRESS_GZIP_2,
	ZIO_COMPRESS_GZIP_3,
	ZIO_COMPRESS_GZIP_4,
	ZIO_COMPRESS_GZIP_5,
	ZIO_COMPRESS_GZIP_6,
	ZIO_COMPRESS_GZIP_7,
	ZIO_COMPRESS_GZIP_8,
	ZIO_COMPRESS_GZIP_9,
	ZIO_COMPRESS_ZLE,
	ZIO_COMPRESS_LZ4,
	ZIO_COMPRESS_LZ4HC_1,
	ZIO_COMPRESS_LZ4HC_2,
	ZIO_COMPRESS_LZ4HC_3,
	ZIO_COMPRESS_LZ4HC_4,
	ZIO_COMPRESS_LZ4HC_5,
	ZIO_COMPRESS_LZ4HC_6,
	ZIO_COMPRESS_LZ4HC_7,
	ZIO_COMPRESS_LZ4HC_8,
	ZIO_COMPRESS_LZ4HC_9,
	ZIO_COMPRESS_LZ4HC_10,
	ZIO_COMPRESS_LZ4HC_11,
	ZIO_COMPRESS_LZ4HC_12,
	ZIO_COMPRESS_LZ4HC_13,
	ZIO_COMPRESS_LZ4HC_14,
	ZIO_COMPRESS_LZ4HC_15,
	ZIO_COMPRESS_LZ4HC_16,
	ZIO_COMPRESS_FUNCTIONS
};

/* Stored in BP, used for selecting decompression function */
enum bp_compress {
	BP_COMPRESS_INHERIT = 0,	/* invalid */
	BP_COMPRESS_ON,			/* invalid */
	BP_COMPRESS_OFF,
	BP_COMPRESS_LZJB,
	BP_COMPRESS_EMPTY,
	BP_COMPRESS_GZIP_1,
	BP_COMPRESS_GZIP_2,
	BP_COMPRESS_GZIP_3,
	BP_COMPRESS_GZIP_4,
	BP_COMPRESS_GZIP_5,
	BP_COMPRESS_GZIP_6,
	BP_COMPRESS_GZIP_7,
	BP_COMPRESS_GZIP_8,
	BP_COMPRESS_GZIP_9,
	BP_COMPRESS_ZLE,
	BP_COMPRESS_LZ4,
	BP_COMPRESS_VALUES
};

/* Common signature for all zio compress functions. */
typedef size_t zio_compress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);
/* Common signature for all zio decompress functions. */
typedef int zio_decompress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);

/*
 * Common signature for all zio decompress functions using an ABD as input.
 * This is helpful if you have both compressed ARC and scatter ABDs enabled,
 * but is not a requirement for all compression algorithms.
 */
typedef int zio_decompress_abd_func_t(abd_t *src, void *dst,
    size_t s_len, size_t d_len, int);
/*
 * Information about each compression function.
 */
typedef const struct zio_compress_info {
	char				*ci_name;
	int				ci_level;
	zio_compress_func_t		*ci_compress;
	enum bp_compress		ci_bp_compress_value;
	spa_feature_t			ci_feature;
} zio_compress_info_t;

typedef const struct zio_decompress_info {
	char			*di_name;
	int			di_level;
	zio_decompress_func_t	*di_decompress;
} zio_decompress_info_t;

extern zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS];
extern zio_decompress_info_t zio_decompress_table[BP_COMPRESS_VALUES];

#define	BP_COMPRESS_VALUE(C)	(zio_compress_table[C].ci_bp_compress_value)

/*
 * lz4 and lz4hc compression init & free
 */
extern void lz4_init(void);
extern void lz4_fini(void);
extern void lz4hc_init(void);
extern void lz4hc_fini(void);

/*
 * Compression routines.
 */
extern size_t lzjb_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int lzjb_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t gzip_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int gzip_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t zle_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int zle_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t lz4_compress_zfs(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int lz4_decompress_zfs(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int lz4_decompress_abd(abd_t *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern size_t lz4hc_compress_zfs(void *src, void *dst, size_t s_len,
    size_t d_len, int level);

/*
 * Compress and decompress data if necessary.
 */
extern size_t zio_compress_data(enum zio_compress c, abd_t *src, void *dst,
    size_t s_len);
extern int zio_decompress_data(enum bp_compress c, abd_t *src, void *dst,
    size_t s_len, size_t d_len);
extern int zio_decompress_data_buf(enum bp_compress c, void *src, void *dst,
    size_t s_len, size_t d_len);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIO_COMPRESS_H */
