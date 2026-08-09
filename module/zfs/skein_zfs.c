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
 * Copyright 2013 Saso Kiselkov.  All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/skein.h>

#include <sys/abd.h>

static int
skein_incremental(void *buf, size_t size, void *arg)
{
	Skein_512_Ctxt_t *ctx = arg;
	(void) Skein_512_Update(ctx, buf, size);
	return (0);
}
/*
 * Computes a native 256-bit skein MAC checksum. Please note that this
 * function requires the presence of a ctx_template that should be allocated
 * using abd_checksum_skein_tmpl_init.
 */
void
abd_checksum_skein_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	Skein_512_Ctxt_t ctx;

	ASSERT(ctx_template != NULL);
	memcpy(&ctx, ctx_template, sizeof (ctx));
	(void) abd_iterate_func(abd, 0, size, skein_incremental, &ctx);
	(void) Skein_512_Final(&ctx, (uint8_t *)zcp);
	memset(&ctx, 0, sizeof (ctx));
}

/*
 * Byteswapped version of abd_checksum_skein_native. This just invokes
 * the native checksum function and byteswaps the resulting checksum (since
 * skein is internally endian-insensitive).
 */
void
abd_checksum_skein_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t	tmp;

	abd_checksum_skein_native(abd, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(tmp.zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(tmp.zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(tmp.zc_word[3]);
}

/*
 * Allocates a skein MAC template suitable for using in skein MAC checksum
 * computations and returns a pointer to it.
 */
void *
abd_checksum_skein_tmpl_init(const zio_cksum_salt_t *salt)
{
	Skein_512_Ctxt_t *ctx = kmem_zalloc(sizeof (*ctx), KM_SLEEP);

	(void) Skein_512_InitExt(ctx, sizeof (zio_cksum_t) * 8, 0,
	    salt->zcs_bytes, sizeof (salt->zcs_bytes));
	return (ctx);
}

/*
 * Frees a skein context template previously allocated using
 * zio_checksum_skein_tmpl_init.
 */
void
abd_checksum_skein_tmpl_free(void *ctx_template)
{
	Skein_512_Ctxt_t *ctx = ctx_template;

	memset(ctx, 0, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));
}
