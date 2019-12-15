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
 */

#ifndef _SYS_ZIO_COMPRESS_H
#define	_SYS_ZIO_COMPRESS_H

#include <sys/abd.h>

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
	ZIO_COMPRESS_ZSTD,
	ZIO_COMPRESS_FUNCTIONS
};

#define	ZIO_COMPLEVEL_INHERIT	0
#define	ZIO_COMPLEVEL_DEFAULT	255

enum zio_zstd_levels {
	ZIO_ZSTDLVL_INHERIT = 0,
	ZIO_ZSTDLVL_1,
#define	ZIO_ZSTD_LEVEL_MIN	ZIO_ZSTDLVL_1
	ZIO_ZSTDLVL_2,
	ZIO_ZSTDLVL_3,
#define	ZIO_ZSTD_LEVEL_DEFAULT	ZIO_ZSTDLVL_3
	ZIO_ZSTDLVL_4,
	ZIO_ZSTDLVL_5,
	ZIO_ZSTDLVL_6,
	ZIO_ZSTDLVL_7,
	ZIO_ZSTDLVL_8,
	ZIO_ZSTDLVL_9,
	ZIO_ZSTDLVL_10,
	ZIO_ZSTDLVL_11,
	ZIO_ZSTDLVL_12,
	ZIO_ZSTDLVL_13,
	ZIO_ZSTDLVL_14,
	ZIO_ZSTDLVL_15,
	ZIO_ZSTDLVL_16,
	ZIO_ZSTDLVL_17,
	ZIO_ZSTDLVL_18,
	ZIO_ZSTDLVL_19,
#define	ZIO_ZSTD_LEVEL_MAX	ZIO_ZSTDLVL_19
#define	ZIO_ZSTDLVL_MAX		ZIO_ZSTDLVL_19
	ZIO_ZSTDLVL_RESERVE = 101, /* Leave room for new positive levels */
	ZIO_ZSTDLVL_FAST, /* Fast levels are negative */
	ZIO_ZSTDLVL_FAST_1,
#define	ZIO_ZSTD_FAST_LEVEL_DEFAULT	ZIO_ZSTDLVL_FAST_1
	ZIO_ZSTDLVL_FAST_2,
	ZIO_ZSTDLVL_FAST_3,
	ZIO_ZSTDLVL_FAST_4,
	ZIO_ZSTDLVL_FAST_5,
	ZIO_ZSTDLVL_FAST_6,
	ZIO_ZSTDLVL_FAST_7,
	ZIO_ZSTDLVL_FAST_8,
	ZIO_ZSTDLVL_FAST_9,
	ZIO_ZSTDLVL_FAST_10,
	ZIO_ZSTDLVL_FAST_20,
	ZIO_ZSTDLVL_FAST_30,
	ZIO_ZSTDLVL_FAST_40,
	ZIO_ZSTDLVL_FAST_50,
	ZIO_ZSTDLVL_FAST_60,
	ZIO_ZSTDLVL_FAST_70,
	ZIO_ZSTDLVL_FAST_80,
	ZIO_ZSTDLVL_FAST_90,
	ZIO_ZSTDLVL_FAST_100,
	ZIO_ZSTDLVL_FAST_500,
	ZIO_ZSTDLVL_FAST_1000,
#define	ZIO_ZSTDLVL_FAST_MAX	ZIO_ZSTDLVL_FAST_1000
	ZIO_ZSTDLVL_DEFAULT = 250, /* Allow the default level to change */
	ZIO_ZSTDLVL_AUTO = 251, /* Reserved for future use */
	ZIO_ZSTDLVL_LEVELS
};

/* Forward Declaration to avoid visibility problems */
struct zio_prop;

/* Common signature for all zio compress functions. */
typedef size_t zio_compress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);
/* Common signature for all zio decompress functions. */
typedef int zio_decompress_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, int);
/* Common signature for all zio decompress and get level functions. */
typedef int zio_decompresslevel_func_t(void *src, void *dst,
    size_t s_len, size_t d_len, uint8_t *level);
/* Common signature for all zio get-compression-level functions. */
typedef int zio_getlevel_func_t(void *src, size_t s_len, uint8_t *level);


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
	zio_decompress_func_t		*ci_decompress;
	zio_decompresslevel_func_t	*ci_decompress_level;
	zio_getlevel_func_t		*ci_get_level;
} zio_compress_info_t;

extern zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS];

/*
 * lz4 compression init & free
 */
extern void lz4_init(void);
extern void lz4_fini(void);

/*
 * specific to zstd user space implementation only
 */
extern void zstd_fini(void);
extern int zstd_init(void);

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
extern size_t zstd_compress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int zstd_decompress(void *src, void *dst, size_t s_len, size_t d_len,
    int level);
extern int zstd_decompress_level(void *src, void *dst, size_t s_len,
    size_t d_len, uint8_t *level);
extern int zstd_get_level(void *src, size_t s_len, uint8_t *level);

/*
 * Compress and decompress data if necessary.
 */
extern size_t zio_compress_data(enum zio_compress c, abd_t *src, void *dst,
    size_t s_len, uint8_t level);
extern int zio_decompress_data(enum zio_compress c, abd_t *src, void *dst,
    size_t s_len, size_t d_len, uint8_t *level);
extern int zio_decompress_data_buf(enum zio_compress c, void *src, void *dst,
    size_t s_len, size_t d_len, uint8_t *level);
extern int zio_decompress_getcomplevel(enum zio_compress c, void *src,
    size_t s_len, uint8_t *level);
extern int zio_compress_to_feature(enum zio_compress comp);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIO_COMPRESS_H */
