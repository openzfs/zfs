// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2024, Klara, Inc.
 */

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <unistd.h>
#include <zfs_fletcher.h>

#include "zstream_util.h"

void *
safe_malloc(size_t size)
{
	void *rv = malloc(size);
	if (rv == NULL) {
		errx(1, "failed to allocate %zu bytes, aborting...", size);
	}
	return (rv);
}

void *
safe_calloc(size_t size)
{
	void *rv = calloc(1, size);
	if (rv == NULL) {
		errx(1, "failed to allocate %zu bytes, aborting...", size);
	}
	return (rv);
}

char *
checksum_str(zio_cksum_t *cksum, char *buff, size_t buff_size)
{
	snprintf(buff, buff_size, "%.16llx / %.16llx / %.16llx / %.16llx",
	    (long long unsigned int) cksum->zc_word[0],
	    (long long unsigned int) cksum->zc_word[1],
	    (long long unsigned int) cksum->zc_word[2],
	    (long long unsigned int) cksum->zc_word[3]);
	return (buff);
}

boolean_t
validate_checksum(zio_cksum_t *expected, zio_cksum_t *actual,
    boolean_t swap, const char *where, off_t stream_offset)
{
	static char buff[128];
	zio_cksum_t swapped_actual;

	if (swap) {
		swapped_actual = *actual;
		actual = &swapped_actual;
		ZIO_CHECKSUM_BSWAP(actual);
	}
	/* cppcheck-suppress uninitvar */
	if (ZIO_CHECKSUM_EQUAL(*expected, *actual)) {
		return (B_TRUE);
	}
	fflush(stdout);
	fprintf(stderr, "Incorrect checksum %s (stream offset %lld)\n", where,
	    (longlong_t)stream_offset);
	fprintf(stderr, "Expected = %s\n", checksum_str(expected, buff,
	    sizeof (buff)));
	fprintf(stderr, "  Actual = %s\n", checksum_str(actual, buff,
	    sizeof (buff)));
	return (B_FALSE);
}

boolean_t
write_is_encrypted(struct drr_write *drrw)
{
	for (int i = 0; i < ZIO_DATA_SALT_LEN; i++) {
		if (drrw->drr_salt[i] != 0) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}

/*
 * The specified compress_type must reflect the buffer's actual compression.
 * Returns an allocated buffer if decompression was successful, NULL
 * otherwise.
 */
uint8_t *
decompress_buffer(uint8_t *inbuff, size_t inbuff_size, size_t logical_size,
    enum zio_compress compress_type)
{
	uint8_t *outbuff = safe_malloc(logical_size);
	abd_t sabd, dabd;
	int ret;

	VERIFY3B(ctype_is_uncompressed(compress_type), ==, B_FALSE);

	abd_get_from_buf_struct(&sabd, inbuff, inbuff_size);
	abd_get_from_buf_struct(&dabd, outbuff, logical_size);
	ret = zio_decompress_data(compress_type, &sabd, &dabd,
	    inbuff_size, abd_get_size(&dabd), NULL);

	abd_free(&dabd);
	abd_free(&sabd);

	if (ret != 0) {
		free(outbuff);
		return (NULL);
	}

	return (outbuff);
}

/*
 * Returns an allocated buffer if compression was successful, NULL
 * otherwise.
 */
uint8_t *
compress_buffer(uint8_t *inbuff, size_t inbuff_size,
    compression_spec_t compress_type, size_t *compressed_size)
{
	uint8_t *outbuff = safe_malloc(inbuff_size);
	abd_t	sabd, dabd;
	size_t	csize, rounded;

	VERIFY3B(ctype_is_uncompressed(compress_type.cs_type), ==, B_FALSE);

	abd_t *pabd = abd_get_from_buf_struct(&dabd, outbuff, inbuff_size);
	abd_get_from_buf_struct(&sabd, inbuff, inbuff_size);
	csize = zio_compress_data(compress_type.cs_type, &sabd,
	    &pabd, inbuff_size, inbuff_size, compress_type.cs_level);

	rounded = P2ROUNDUP(csize, SPA_MINBLOCKSIZE);
	if (rounded < inbuff_size) {
		abd_zero_off(pabd, csize, rounded - csize);
		*compressed_size = rounded;
	} else {
		free(outbuff);
		outbuff = NULL;
	}

	abd_free(&sabd);
	abd_free(&dabd);

	return (outbuff);
}
