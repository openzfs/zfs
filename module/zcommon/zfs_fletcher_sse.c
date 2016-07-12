/*
 * Implement fast Fletcher4 with SSE2,SSSE3 instructions. (x86)
 *
 * Use the 128-bit SSE2/SSSE3 SIMD instructions and registers to compute
 * Fletcher4 in four incremental 64-bit parallel accumulator streams,
 * and then combine the streams to form the final four checksum words.
 * This implementation is a derivative of the AVX SIMD implementation by
 * James Guilford and Jinshan Xiong from Intel (see zfs_fletcher_intel.c).
 *
 * Copyright (C) 2016 Tyler J. Stachecki.
 *
 * Authors:
 *	Tyler J. Stachecki <stachecki.tyler@gmail.com>
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
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#if defined(HAVE_SSE2)

#include <linux/simd_x86.h>
#include <sys/spa_checksum.h>
#include <zfs_fletcher.h>

struct zfs_fletcher_sse_array {
	uint64_t v[2] __attribute__((aligned(16)));
};

static void
fletcher_4_sse2_init(zio_cksum_t *zcp)
{
	kfpu_begin();

	/* clear sse registers */
	asm volatile("pxor %xmm0, %xmm0");
	asm volatile("pxor %xmm1, %xmm1");
	asm volatile("pxor %xmm2, %xmm2");
	asm volatile("pxor %xmm3, %xmm3");
}

static void
fletcher_4_sse2_fini(zio_cksum_t *zcp)
{
	struct zfs_fletcher_sse_array a, b, c, d;
	uint64_t A, B, C, D;

	asm volatile("movdqu %%xmm0, %0":"=m" (a.v));
	asm volatile("movdqu %%xmm1, %0":"=m" (b.v));
	asm volatile("psllq $0x2, %xmm2");
	asm volatile("movdqu %%xmm2, %0":"=m" (c.v));
	asm volatile("psllq $0x3, %xmm3");
	asm volatile("movdqu %%xmm3, %0":"=m" (d.v));

	kfpu_end();

	/*
	 * The mixing matrix for checksum calculation is:
	 * a = a0 + a1
	 * b = 2b0 + 2b1 - a1
	 * c = 4c0 - b0 + 4c1 -3b1
	 * d = 8d0 - 4c0 + 8d1 - 8c1 + b1;
	 *
	 * c and d are multiplied by 4 and 8, respectively,
	 * before spilling the vectors out to memory.
	 */
	A = a.v[0] + a.v[1];
	B = 2*b.v[0] + 2*b.v[1] - a.v[1];
	C = c.v[0] - b.v[0] + c.v[1] - 3*b.v[1];
	D = d.v[0] - c.v[0] + d.v[1] - 2*c.v[1] + b.v[1];

	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
}

static void
fletcher_4_sse2_native(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	asm volatile("pxor %xmm4, %xmm4");

	for (; ip < ipend; ip += 2) {
		asm volatile("movdqu %0, %%xmm5" :: "m"(*ip));
		asm volatile("movdqa %xmm5, %xmm6");
		asm volatile("punpckldq %xmm4, %xmm5");
		asm volatile("punpckhdq %xmm4, %xmm6");
		asm volatile("paddq %xmm5, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
		asm volatile("paddq %xmm6, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
	}
}

static void
fletcher_4_sse2_byteswap(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = (uint32_t *)((uint8_t *)ip + size);

	for (; ip < ipend; ip += 2) {
		uint32_t scratch;

		asm volatile("bswapl %0" : "=r"(scratch) : "0"(*ip));
		asm volatile("movd %0, %%xmm5" :: "r"(scratch));
		asm volatile("bswapl %0" : "=r"(scratch) : "0"(*(ip + 1)));
		asm volatile("movd %0, %%xmm6" :: "r"(scratch));
		asm volatile("punpcklqdq %xmm6, %xmm5");
		asm volatile("paddq %xmm5, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
	}
}

static boolean_t fletcher_4_sse2_valid(void)
{
	return (zfs_sse2_available());
}

const fletcher_4_ops_t fletcher_4_sse2_ops = {
	.init_native = fletcher_4_sse2_init,
	.fini_native = fletcher_4_sse2_fini,
	.compute_native = fletcher_4_sse2_native,
	.init_byteswap = fletcher_4_sse2_init,
	.fini_byteswap = fletcher_4_sse2_fini,
	.compute_byteswap = fletcher_4_sse2_byteswap,
	.valid = fletcher_4_sse2_valid,
	.name = "sse2"
};

#endif /* defined(HAVE_SSE2) */

#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
static void
fletcher_4_ssse3_byteswap(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	static const struct zfs_fletcher_sse_array mask = {
		.v = { 0x0405060700010203, 0x0C0D0E0F08090A0B }
	};

	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	asm volatile("movdqu %0, %%xmm7"::"m" (mask));
	asm volatile("pxor %xmm4, %xmm4");

	for (; ip < ipend; ip += 2) {
		asm volatile("movdqu %0, %%xmm5"::"m" (*ip));
		asm volatile("pshufb %xmm7, %xmm5");
		asm volatile("movdqa %xmm5, %xmm6");
		asm volatile("punpckldq %xmm4, %xmm5");
		asm volatile("punpckhdq %xmm4, %xmm6");
		asm volatile("paddq %xmm5, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
		asm volatile("paddq %xmm6, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
	}
}

static boolean_t fletcher_4_ssse3_valid(void)
{
	return (zfs_sse2_available() && zfs_ssse3_available());
}

const fletcher_4_ops_t fletcher_4_ssse3_ops = {
	.init_native = fletcher_4_sse2_init,
	.fini_native = fletcher_4_sse2_fini,
	.compute_native = fletcher_4_sse2_native,
	.init_byteswap = fletcher_4_sse2_init,
	.fini_byteswap = fletcher_4_sse2_fini,
	.compute_byteswap = fletcher_4_ssse3_byteswap,
	.valid = fletcher_4_ssse3_valid,
	.name = "ssse3"
};

#endif /* defined(HAVE_SSE2) && defined(HAVE_SSSE3) */
