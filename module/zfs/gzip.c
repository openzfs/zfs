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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */



#include <sys/debug.h>
#include <sys/types.h>
#include <sys/qat.h>
#include <sys/zio_compress.h>

#ifdef _KERNEL

#include <sys/zmod.h>
typedef size_t zlen_t;
#define	compress_func	z_compress_level
#define	uncompress_func	z_uncompress

#else /* _KERNEL */

#include <zlib.h>
typedef uLongf zlen_t;
#define	compress_func	compress2
#define	uncompress_func	uncompress

#endif

static size_t
zfs_gzip_compress_buf(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	int ret;
	zlen_t dstlen = d_len;

	ASSERT(d_len <= s_len);

	/* check if hardware accelerator can be used */
	if (qat_dc_use_accel(s_len)) {
		ret = qat_compress(QAT_COMPRESS, s_start, s_len, d_start,
		    d_len, &dstlen);
		if (ret == CPA_STATUS_SUCCESS) {
			return ((size_t)dstlen);
		} else if (ret == CPA_STATUS_INCOMPRESSIBLE) {
			if (d_len != s_len)
				return (s_len);

			memcpy(d_start, s_start, s_len);
			return (s_len);
		}
		/* if hardware compression fails, do it again with software */
	}

	if (compress_func(d_start, &dstlen, s_start, s_len, n) != Z_OK) {
		if (d_len != s_len)
			return (s_len);

		memcpy(d_start, s_start, s_len);
		return (s_len);
	}

	return ((size_t)dstlen);
}

static int
zfs_gzip_decompress_buf(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	(void) n;
	zlen_t dstlen = d_len;

	ASSERT(d_len >= s_len);

	/* check if hardware accelerator can be used */
	if (qat_dc_use_accel(d_len)) {
		if (qat_compress(QAT_DECOMPRESS, s_start, s_len,
		    d_start, d_len, &dstlen) == CPA_STATUS_SUCCESS) {
			if ((size_t)dstlen == d_len)
				return (0);
		}
		/* if hardware de-compress fail, do it again with software */
	}

	if (uncompress_func(d_start, &dstlen, s_start, s_len) != Z_OK)
		return (-1);
	if ((size_t)dstlen != d_len)
		return (-1);

	return (0);
}

ZFS_COMPRESS_WRAP_DECL(zfs_gzip_compress)
ZFS_DECOMPRESS_WRAP_DECL(zfs_gzip_decompress)
