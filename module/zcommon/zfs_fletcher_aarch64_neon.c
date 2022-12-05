/*
 * Implement fast Fletcher4 with NEON instructions. (aarch64)
 *
 * Use the 128-bit NEON SIMD instructions and registers to compute
 * Fletcher4 in two incremental 64-bit parallel accumulator streams,
 * and then combine the streams to form the final four checksum words.
 * This implementation is a derivative of the AVX SIMD implementation by
 * James Guilford and Jinshan Xiong from Intel (see zfs_fletcher_intel.c).
 *
 * Copyright (C) 2016 Romain Dolbeau.
 *
 * Authors:
 *	Romain Dolbeau <romain.dolbeau@atos.net>
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if defined(__aarch64__)

#include <sys/simd.h>
#include <sys/spa_checksum.h>
#include <sys/string.h>
#include <zfs_fletcher.h>

extern void fletcher_4_aarch64_neon_native(
		fletcher_4_ctx_t *ctx, const void *buf, uint64_t size);

extern void fletcher_4_aarch64_neon_byteswap(
		fletcher_4_ctx_t *ctx, const void *buf, uint64_t size);

ZFS_NO_SANITIZE_UNDEFINED
static void
fletcher_4_aarch64_neon_init(fletcher_4_ctx_t *ctx)
{
	kfpu_begin();
	memset(ctx->aarch64_neon, 0, 4 * sizeof (zfs_fletcher_aarch64_neon_t));
}

ZFS_NO_SANITIZE_UNDEFINED
static void
fletcher_4_aarch64_neon_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp)
{
	uint64_t A, B, C, D;
	A = ctx->aarch64_neon[0].v[0] + ctx->aarch64_neon[0].v[1] +
	    ctx->aarch64_neon[0].v[2] + ctx->aarch64_neon[0].v[3];
	B = 0 - ctx->aarch64_neon[0].v[1] - 2 * ctx->aarch64_neon[0].v[2] -
	    3 * ctx->aarch64_neon[0].v[3] + 4 * ctx->aarch64_neon[1].v[0] +
	    4 * ctx->aarch64_neon[1].v[1] + 4 * ctx->aarch64_neon[1].v[2] +
	    4 * ctx->aarch64_neon[1].v[3];

	C = ctx->aarch64_neon[0].v[2] + 3 * ctx->aarch64_neon[0].v[3] -
	    6 * ctx->aarch64_neon[1].v[0] - 10 * ctx->aarch64_neon[1].v[1] -
	    14 * ctx->aarch64_neon[1].v[2] - 18 * ctx->aarch64_neon[1].v[3] +
	    16 * ctx->aarch64_neon[2].v[0] + 16 * ctx->aarch64_neon[2].v[1] +
	    16 * ctx->aarch64_neon[2].v[2] + 16 * ctx->aarch64_neon[2].v[3];

	D = 0 - ctx->aarch64_neon[0].v[3] + 4 * ctx->aarch64_neon[1].v[0] +
	    10 * ctx->aarch64_neon[1].v[1] + 20 * ctx->aarch64_neon[1].v[2] +
	    34 * ctx->aarch64_neon[1].v[3] - 48 * ctx->aarch64_neon[2].v[0] -
	    64 * ctx->aarch64_neon[2].v[1] - 80 * ctx->aarch64_neon[2].v[2] -
	    96 * ctx->aarch64_neon[2].v[3] + 64 * ctx->aarch64_neon[3].v[0] +
	    64 * ctx->aarch64_neon[3].v[1] + 64 * ctx->aarch64_neon[3].v[2] +
	    64 * ctx->aarch64_neon[3].v[3];
	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
	kfpu_end();
}

static boolean_t fletcher_4_aarch64_neon_valid(void)
{
	return (kfpu_allowed());
}

const fletcher_4_ops_t fletcher_4_aarch64_neon_ops = {
	.init_native = fletcher_4_aarch64_neon_init,
	.compute_native = fletcher_4_aarch64_neon_native,
	.fini_native = fletcher_4_aarch64_neon_fini,
	.init_byteswap = fletcher_4_aarch64_neon_init,
	.compute_byteswap = fletcher_4_aarch64_neon_byteswap,
	.fini_byteswap = fletcher_4_aarch64_neon_fini,
	.valid = fletcher_4_aarch64_neon_valid,
	.name = "aarch64_neon"
};

#endif /* defined(__aarch64__) */
