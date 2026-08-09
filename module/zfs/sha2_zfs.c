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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/zio_checksum.h>
#include <sys/sha2.h>
#include <sys/abd.h>
#include <sys/qat.h>

static int
sha_incremental(void *buf, size_t size, void *arg)
{
	SHA2_CTX *ctx = arg;
	SHA2Update(ctx, buf, size);
	return (0);
}

void
abd_checksum_sha256(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) ctx_template;
	int ret;
	SHA2_CTX ctx;
	zio_cksum_t tmp;

	if (qat_checksum_use_accel(size)) {
		uint8_t *buf = abd_borrow_buf_copy(abd, size);
		ret = qat_checksum(ZIO_CHECKSUM_SHA256, buf, size, &tmp);
		abd_return_buf(abd, buf, size);
		if (ret == CPA_STATUS_SUCCESS)
			goto bswap;

		/* If the hardware implementation fails fall back to software */
	}

	SHA2Init(SHA256, &ctx);
	(void) abd_iterate_func(abd, 0, size, sha_incremental, &ctx);
	SHA2Final(&tmp, &ctx);

bswap:
	/*
	 * A prior implementation of this function had a
	 * private SHA256 implementation always wrote things out in
	 * Big Endian and there wasn't a byteswap variant of it.
	 * To preserve on disk compatibility we need to force that
	 * behavior.
	 */
	zcp->zc_word[0] = BE_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BE_64(tmp.zc_word[1]);
	zcp->zc_word[2] = BE_64(tmp.zc_word[2]);
	zcp->zc_word[3] = BE_64(tmp.zc_word[3]);
}

void
abd_checksum_sha512_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	(void) ctx_template;
	SHA2_CTX	ctx;

	SHA2Init(SHA512_256, &ctx);
	(void) abd_iterate_func(abd, 0, size, sha_incremental, &ctx);
	SHA2Final(zcp, &ctx);
}

void
abd_checksum_sha512_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t	tmp;

	abd_checksum_sha512_native(abd, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(tmp.zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(tmp.zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(tmp.zc_word[3]);
}
