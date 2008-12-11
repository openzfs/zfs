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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/zfs_context.h>
#include <sys/compress.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>

/*
 * Compression vectors.
 */

zio_compress_info_t zio_compress_table[ZIO_COMPRESS_FUNCTIONS] = {
	{NULL,			NULL,			0,	"inherit"},
	{NULL,			NULL,			0,	"on"},
	{NULL,			NULL,			0,	"uncompressed"},
	{lzjb_compress,		lzjb_decompress,	0,	"lzjb"},
	{NULL,			NULL,			0,	"empty"},
	{gzip_compress,		gzip_decompress,	1,	"gzip-1"},
	{gzip_compress,		gzip_decompress,	2,	"gzip-2"},
	{gzip_compress,		gzip_decompress,	3,	"gzip-3"},
	{gzip_compress,		gzip_decompress,	4,	"gzip-4"},
	{gzip_compress,		gzip_decompress,	5,	"gzip-5"},
	{gzip_compress,		gzip_decompress,	6,	"gzip-6"},
	{gzip_compress,		gzip_decompress,	7,	"gzip-7"},
	{gzip_compress,		gzip_decompress,	8,	"gzip-8"},
	{gzip_compress,		gzip_decompress,	9,	"gzip-9"},
};

uint8_t
zio_compress_select(uint8_t child, uint8_t parent)
{
	ASSERT(child < ZIO_COMPRESS_FUNCTIONS);
	ASSERT(parent < ZIO_COMPRESS_FUNCTIONS);
	ASSERT(parent != ZIO_COMPRESS_INHERIT && parent != ZIO_COMPRESS_ON);

	if (child == ZIO_COMPRESS_INHERIT)
		return (parent);

	if (child == ZIO_COMPRESS_ON)
		return (ZIO_COMPRESS_ON_VALUE);

	return (child);
}

int
zio_compress_data(int cpfunc, void *src, uint64_t srcsize, void **destp,
    uint64_t *destsizep, uint64_t *destbufsizep)
{
	uint64_t *word, *word_end;
	uint64_t ciosize, gapsize, destbufsize;
	zio_compress_info_t *ci = &zio_compress_table[cpfunc];
	char *dest;
	uint_t allzero;

	ASSERT((uint_t)cpfunc < ZIO_COMPRESS_FUNCTIONS);
	ASSERT((uint_t)cpfunc == ZIO_COMPRESS_EMPTY || ci->ci_compress != NULL);

	/*
	 * If the data is all zeroes, we don't even need to allocate
	 * a block for it.  We indicate this by setting *destsizep = 0.
	 */
	allzero = 1;
	word = src;
	word_end = (uint64_t *)(uintptr_t)((uintptr_t)word + srcsize);
	while (word < word_end) {
		if (*word++ != 0) {
			allzero = 0;
			break;
		}
	}
	if (allzero) {
		*destp = NULL;
		*destsizep = 0;
		*destbufsizep = 0;
		return (1);
	}

	if (cpfunc == ZIO_COMPRESS_EMPTY)
		return (0);

	/* Compress at least 12.5% */
	destbufsize = P2ALIGN(srcsize - (srcsize >> 3), SPA_MINBLOCKSIZE);
	if (destbufsize == 0)
		return (0);
	dest = zio_buf_alloc(destbufsize);

	ciosize = ci->ci_compress(src, dest, (size_t)srcsize,
	    (size_t)destbufsize, ci->ci_level);
	if (ciosize > destbufsize) {
		zio_buf_free(dest, destbufsize);
		return (0);
	}

	/* Cool.  We compressed at least as much as we were hoping to. */

	/* For security, make sure we don't write random heap crap to disk */
	gapsize = P2ROUNDUP(ciosize, SPA_MINBLOCKSIZE) - ciosize;
	if (gapsize != 0) {
		bzero(dest + ciosize, gapsize);
		ciosize += gapsize;
	}

	ASSERT3U(ciosize, <=, destbufsize);
	ASSERT(P2PHASE(ciosize, SPA_MINBLOCKSIZE) == 0);
	*destp = dest;
	*destsizep = ciosize;
	*destbufsizep = destbufsize;

	return (1);
}

int
zio_decompress_data(int cpfunc, void *src, uint64_t srcsize,
	void *dest, uint64_t destsize)
{
	zio_compress_info_t *ci = &zio_compress_table[cpfunc];

	ASSERT((uint_t)cpfunc < ZIO_COMPRESS_FUNCTIONS);

	return (ci->ci_decompress(src, dest, srcsize, destsize, ci->ci_level));
}
