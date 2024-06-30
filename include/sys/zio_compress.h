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
 * Copyright (c) 2019, Allan Jude
 * Copyright (c) 2019, 2024, Klara, Inc.
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

/* Compression algorithms that have levels */
#define	ZIO_COMPRESS_HASLEVEL(compress)	((compress == ZIO_COMPRESS_ZSTD || \
					(compress >= ZIO_COMPRESS_GZIP_1 && \
					compress <= ZIO_COMPRESS_GZIP_9)))

#define	ZIO_COMPLEVEL_INHERIT	0
#define	ZIO_COMPLEVEL_DEFAULT	255

enum zio_zstd_levels {
	ZIO_ZSTD_LEVEL_INHERIT = 0,
	ZIO_ZSTD_LEVEL_1,
#define	ZIO_ZSTD_LEVEL_MIN	ZIO_ZSTD_LEVEL_1
	ZIO_ZSTD_LEVEL_2,
	ZIO_ZSTD_LEVEL_3,
#define	ZIO_ZSTD_LEVEL_DEFAULT	ZIO_ZSTD_LEVEL_3
	ZIO_ZSTD_LEVEL_4,
	ZIO_ZSTD_LEVEL_5,
	ZIO_ZSTD_LEVEL_6,
	ZIO_ZSTD_LEVEL_7,
	ZIO_ZSTD_LEVEL_8,
	ZIO_ZSTD_LEVEL_9,
	ZIO_ZSTD_LEVEL_10,
	ZIO_ZSTD_LEVEL_11,
	ZIO_ZSTD_LEVEL_12,
	ZIO_ZSTD_LEVEL_13,
	ZIO_ZSTD_LEVEL_14,
	ZIO_ZSTD_LEVEL_15,
	ZIO_ZSTD_LEVEL_16,
	ZIO_ZSTD_LEVEL_17,
	ZIO_ZSTD_LEVEL_18,
	ZIO_ZSTD_LEVEL_19,
#define	ZIO_ZSTD_LEVEL_MAX	ZIO_ZSTD_LEVEL_19
	ZIO_ZSTD_LEVEL_RESERVE = 101, /* Leave room for new positive levels */
	ZIO_ZSTD_LEVEL_FAST, /* Fast levels are negative */
	ZIO_ZSTD_LEVEL_FAST_1,
#define	ZIO_ZSTD_LEVEL_FAST_DEFAULT	ZIO_ZSTD_LEVEL_FAST_1
	ZIO_ZSTD_LEVEL_FAST_2,
	ZIO_ZSTD_LEVEL_FAST_3,
	ZIO_ZSTD_LEVEL_FAST_4,
	ZIO_ZSTD_LEVEL_FAST_5,
	ZIO_ZSTD_LEVEL_FAST_6,
	ZIO_ZSTD_LEVEL_FAST_7,
	ZIO_ZSTD_LEVEL_FAST_8,
	ZIO_ZSTD_LEVEL_FAST_9,
	ZIO_ZSTD_LEVEL_FAST_10,
	ZIO_ZSTD_LEVEL_FAST_20,
	ZIO_ZSTD_LEVEL_FAST_30,
	ZIO_ZSTD_LEVEL_FAST_40,
	ZIO_ZSTD_LEVEL_FAST_50,
	ZIO_ZSTD_LEVEL_FAST_60,
	ZIO_ZSTD_LEVEL_FAST_70,
	ZIO_ZSTD_LEVEL_FAST_80,
	ZIO_ZSTD_LEVEL_FAST_90,
	ZIO_ZSTD_LEVEL_FAST_100,
	ZIO_ZSTD_LEVEL_FAST_500,
	ZIO_ZSTD_LEVEL_FAST_1000,
#define	ZIO_ZSTD_LEVEL_FAST_MAX	ZIO_ZSTD_LEVEL_FAST_1000
	ZIO_ZSTD_LEVEL_AUTO = 251, /* Reserved for future use */
	ZIO_ZSTD_LEVEL_LEVELS
};

/* Forward Declaration to avoid visibility problems */
struct zio_prop;

/* Common signature for all zio compress functions. */
typedef size_t zio_compress_func_t(abd_t *src, abd_t *dst,
    size_t s_len, size_t d_len, int);
/* Common signature for all zio decompress functions. */
typedef int zio_decompress_func_t(abd_t *src, abd_t *dst,
    size_t s_len, size_t d_len, int);
/* Common signature for all zio decompress and get level functions. */
typedef int zio_decompresslevel_func_t(abd_t *src, abd_t *dst,
    size_t s_len, size_t d_len, uint8_t *level);

/*
 * Information about each compression function.
 */
typedef const struct zio_compress_info {
	const char			*ci_name;
	int				ci_level;
	zio_compress_func_t		*ci_compress;
	zio_decompress_func_t		*ci_decompress;
	zio_decompresslevel_func_t	*ci_decompress_level;
} zio_compress_info_t;

extern zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS];

/*
 * lz4 compression init & free
 */
extern void lz4_init(void);
extern void lz4_fini(void);

/*
 * Compression routines.
 */
extern size_t zfs_lzjb_compress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern int zfs_lzjb_decompress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern size_t zfs_gzip_compress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern int zfs_gzip_decompress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern size_t zfs_zle_compress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern int zfs_zle_decompress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern size_t zfs_lz4_compress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);
extern int zfs_lz4_decompress(abd_t *src, abd_t *dst, size_t s_len,
    size_t d_len, int level);

/*
 * Compress and decompress data if necessary.
 */
extern size_t zio_compress_data(enum zio_compress c, abd_t *src, void **dst,
    size_t s_len, uint8_t level);
extern int zio_decompress_data(enum zio_compress c, abd_t *src, void *dst,
    size_t s_len, size_t d_len, uint8_t *level);
extern int zio_compress_to_feature(enum zio_compress comp);

#define	ZFS_COMPRESS_WRAP_DECL(name)					\
size_t									\
name(abd_t *src, abd_t *dst, size_t s_len, size_t d_len, int n)		\
{									\
	void *s_buf = abd_borrow_buf_copy(src, s_len);			\
	void *d_buf = abd_borrow_buf(dst, d_len);			\
	size_t c_len = name##_buf(s_buf, d_buf, s_len, d_len, n);	\
	abd_return_buf(src, s_buf, s_len);				\
	abd_return_buf_copy(dst, d_buf, d_len);				\
	return (c_len);							\
}
#define	ZFS_DECOMPRESS_WRAP_DECL(name)					\
int									\
name(abd_t *src, abd_t *dst, size_t s_len, size_t d_len, int n)		\
{									\
	void *s_buf = abd_borrow_buf_copy(src, s_len);			\
	void *d_buf = abd_borrow_buf(dst, d_len);			\
	int err = name##_buf(s_buf, d_buf, s_len, d_len, n);		\
	abd_return_buf(src, s_buf, s_len);				\
	abd_return_buf_copy(dst, d_buf, d_len);				\
	return (err);							\
}
#define	ZFS_DECOMPRESS_LEVEL_WRAP_DECL(name)				\
int									\
name(abd_t *src, abd_t *dst, size_t s_len, size_t d_len, uint8_t *n)	\
{									\
	void *s_buf = abd_borrow_buf_copy(src, s_len);			\
	void *d_buf = abd_borrow_buf(dst, d_len);			\
	int err = name##_buf(s_buf, d_buf, s_len, d_len, n);		\
	abd_return_buf(src, s_buf, s_len);				\
	abd_return_buf_copy(dst, d_buf, d_len);				\
	return (err);							\
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIO_COMPRESS_H */
