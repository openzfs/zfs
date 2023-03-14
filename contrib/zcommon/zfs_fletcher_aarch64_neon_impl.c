/*
 * GNU C source file that we manually compile with Clang for aarch64.
 * The compiled assembly goes into:
 *
 * module/zcommon/zfs_fletcher_aarch64_neon_impl.S
 *
 * This works around bad code generation from GCC.
 *
 * Copyright (C) 2022 Zettabyte Software, LLC.
 *
 * Authors:
 *	Richard Yao <richard.yao@alumni.stonybrook.edu>
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

#include <stdint.h>

typedef uint8_t u8x16 __attribute__((vector_size(16)));
typedef uint32_t u32x4 __attribute__((vector_size(16)));

typedef uint32_t u32x8 __attribute__((vector_size(32)));
typedef uint64_t u64x4 __attribute__((vector_size(32)));

typedef union {
    u8x16   u8;
    u32x4   u32;
} v128;

typedef union {
    u32x8   u32;
    u64x4   u64;
} v256;

typedef v256 zfs_fletcher_aarch64_neon_t;

typedef union fletcher_4_ctx {
    zfs_fletcher_aarch64_neon_t aarch64_neon[4];

} fletcher_4_ctx_t;

void
fletcher_4_aarch64_neon_native(
		fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	const v128 *ip = buf;
	const v128 *ipend = (const v128 *)((uint8_t *)ip + size);

	v256 a = ctx->aarch64_neon[0];
	v256 b = ctx->aarch64_neon[1];
	v256 c = ctx->aarch64_neon[2];
	v256 d = ctx->aarch64_neon[3];

	do {
		v128 t = *ip;
		a.u64 += __builtin_convertvector(t.u32, u64x4);
		b.u64 += a.u64;
		c.u64 += b.u64;
		d.u64 += c.u64;
	} while (++ip < ipend);

	ctx->aarch64_neon[0] = a;
	ctx->aarch64_neon[1] = b;
	ctx->aarch64_neon[2] = c;
	ctx->aarch64_neon[3] = d;
}

void
fletcher_4_aarch64_neon_byteswap(
    fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	const v128 *ip = buf;
	const v128 *ipend = (const v128 *)((uint8_t *)ip + size);

	v256 a = ctx->aarch64_neon[0];
	v256 b = ctx->aarch64_neon[1];
	v256 c = ctx->aarch64_neon[2];
	v256 d = ctx->aarch64_neon[3];

	do {
		v128 t = *ip;
		t.u8 = __builtin_shufflevector(
		    t.u8, t.u8, 3, 2, 1, 0, 7, 6,
		    5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
		a.u64 += __builtin_convertvector(t.u32, u64x4);
		b.u64 += a.u64;
		c.u64 += b.u64;
		d.u64 += c.u64;
	} while (++ip < ipend);

	ctx->aarch64_neon[0] = a;
	ctx->aarch64_neon[1] = b;
	ctx->aarch64_neon[2] = c;
	ctx->aarch64_neon[3] = d;
}
