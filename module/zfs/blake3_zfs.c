/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/zio_checksum.h>
#include <sys/blake3.h>
#include <sys/abd.h>

static int
blake3_incremental(void *buf, size_t size, void *arg)
{
	BLAKE3_CTX *ctx = arg;

	Blake3_Update(ctx, buf, size);

	return (0);
}

/*
 * Computes a native 256-bit BLAKE3 MAC checksum. Please note that this
 * function requires the presence of a ctx_template that should be allocated
 * using abd_checksum_blake3_tmpl_init.
 */
void
abd_checksum_blake3_native(abd_t *abd, uint64_t size, const void *ctx_template,
    zio_cksum_t *zcp)
{
	ASSERT(ctx_template != 0);

#if defined(_KERNEL)
	BLAKE3_CTX *ctx = blake3_per_cpu_ctx[CPU_SEQID_UNSTABLE];
#else
	BLAKE3_CTX *ctx = kmem_alloc(sizeof (*ctx), KM_SLEEP);
#endif

	memcpy(ctx, ctx_template, sizeof (*ctx));
	(void) abd_iterate_func(abd, 0, size, blake3_incremental, ctx);
	Blake3_Final(ctx, (uint8_t *)zcp);

#if !defined(_KERNEL)
	memset(ctx, 0, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));
#endif
}

/*
 * Byteswapped version of abd_checksum_blake3_native. This just invokes
 * the native checksum function and byteswaps the resulting checksum (since
 * BLAKE3 is internally endian-insensitive).
 */
void
abd_checksum_blake3_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t tmp;

	ASSERT(ctx_template != 0);

	abd_checksum_blake3_native(abd, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(tmp.zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(tmp.zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(tmp.zc_word[3]);
}

/*
 * Allocates a BLAKE3 MAC template suitable for using in BLAKE3 MAC checksum
 * computations and returns a pointer to it.
 */
void *
abd_checksum_blake3_tmpl_init(const zio_cksum_salt_t *salt)
{
	BLAKE3_CTX *ctx;

	ASSERT(sizeof (salt->zcs_bytes) == 32);

	/* init reference object */
	ctx = kmem_zalloc(sizeof (*ctx), KM_SLEEP);
	Blake3_InitKeyed(ctx, salt->zcs_bytes);

	return (ctx);
}

/*
 * Frees a BLAKE3 context template previously allocated using
 * zio_checksum_blake3_tmpl_init.
 */
void
abd_checksum_blake3_tmpl_free(void *ctx_template)
{
	BLAKE3_CTX *ctx = ctx_template;

	memset(ctx, 0, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));
}
