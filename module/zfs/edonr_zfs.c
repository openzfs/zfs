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
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/edonr.h>
#include <sys/abd.h>

#define	EDONR_MODE		512
#define	EDONR_BLOCK_SIZE	EdonR512_BLOCK_SIZE

static int
edonr_incremental(void *buf, size_t size, void *arg)
{
	EdonRState *ctx = arg;
	EdonRUpdate(ctx, buf, size * 8);
	return (0);
}

/*
 * Native zio_checksum interface for the Edon-R hash function.
 */
void
abd_checksum_edonr_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	uint8_t		digest[EDONR_MODE / 8];
	EdonRState	ctx;

	ASSERT(ctx_template != NULL);
	memcpy(&ctx, ctx_template, sizeof (ctx));
	(void) abd_iterate_func(abd, 0, size, edonr_incremental, &ctx);
	EdonRFinal(&ctx, digest);
	memcpy(zcp->zc_word, digest, sizeof (zcp->zc_word));
}

/*
 * Byteswapped zio_checksum interface for the Edon-R hash function.
 */
void
abd_checksum_edonr_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t	tmp;

	abd_checksum_edonr_native(abd, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(zcp->zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(zcp->zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(zcp->zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(zcp->zc_word[3]);
}

void *
abd_checksum_edonr_tmpl_init(const zio_cksum_salt_t *salt)
{
	EdonRState	*ctx;
	uint8_t		salt_block[EDONR_BLOCK_SIZE];

	/*
	 * Edon-R needs all but the last hash invocation to be on full-size
	 * blocks, but the salt is too small. Rather than simply padding it
	 * with zeros, we expand the salt into a new salt block of proper
	 * size by double-hashing it (the new salt block will be composed of
	 * H(salt) || H(H(salt))).
	 */
	_Static_assert(EDONR_BLOCK_SIZE == 2 * (EDONR_MODE / 8),
	    "Edon-R block size mismatch");
	EdonRHash(salt->zcs_bytes, sizeof (salt->zcs_bytes) * 8, salt_block);
	EdonRHash(salt_block, EDONR_MODE, salt_block + EDONR_MODE / 8);

	/*
	 * Feed the new salt block into the hash function - this will serve
	 * as our MAC key.
	 */
	ctx = kmem_zalloc(sizeof (*ctx), KM_SLEEP);
	EdonRInit(ctx);
	EdonRUpdate(ctx, salt_block, sizeof (salt_block) * 8);
	return (ctx);
}

void
abd_checksum_edonr_tmpl_free(void *ctx_template)
{
	EdonRState *ctx = ctx_template;

	memset(ctx, 0, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));
}
