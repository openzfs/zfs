/*
 * Implement fast Fletcher4 with AVX2 instructions. (x86_64)
 *
 * Use the 256-bit AVX2 SIMD instructions and registers to compute
 * Fletcher4 in four incremental 64-bit parallel accumulator streams,
 * and then combine the streams to form the final four checksum words.
 *
 * Copyright (C) 2015 Intel Corporation.
 *
 * Authors:
 *      James Guilford <james.guilford@intel.com>
 *      Jinshan Xiong <jinshan.xiong@intel.com>
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

#if defined(HAVE_AVX) && defined(HAVE_AVX2)

#include <linux/simd_x86.h>
#include <sys/spa_checksum.h>
#include <zfs_fletcher.h>

static void
fletcher_4_avx2_init(zio_cksum_t *zcp)
{
	kfpu_begin();

	/* clear avx2 registers */
	asm volatile("vpxor %ymm0, %ymm0, %ymm0");
	asm volatile("vpxor %ymm1, %ymm1, %ymm1");
	asm volatile("vpxor %ymm2, %ymm2, %ymm2");
	asm volatile("vpxor %ymm3, %ymm3, %ymm3");
}

static void
fletcher_4_avx2_fini(zio_cksum_t *zcp)
{
	uint64_t __attribute__((aligned(32))) a[4];
	uint64_t __attribute__((aligned(32))) b[4];
	uint64_t __attribute__((aligned(32))) c[4];
	uint64_t __attribute__((aligned(32))) d[4];
	uint64_t A, B, C, D;

	asm volatile("vmovdqu %%ymm0, %0":"=m" (a));
	asm volatile("vmovdqu %%ymm1, %0":"=m" (b));
	asm volatile("vmovdqu %%ymm2, %0":"=m" (c));
	asm volatile("vmovdqu %%ymm3, %0":"=m" (d));
	asm volatile("vzeroupper");

	kfpu_end();

	A = a[0] + a[1] + a[2] + a[3];
	B = 0 - a[1] - 2*a[2] - 3*a[3]
	    + 4*b[0] + 4*b[1] + 4*b[2] + 4*b[3];

	C = a[2] + 3*a[3]
	    -  6*b[0] - 10*b[1] - 14*b[2] - 18*b[3]
	    + 16*c[0] + 16*c[1] + 16*c[2] + 16*c[3];

	D = 0 - a[3]
	    +  4*b[0] + 10*b[1] + 20*b[2] + 34*b[3]
	    - 48*c[0] - 64*c[1] - 80*c[2] - 96*c[3]
	    + 64*d[0] + 64*d[1] + 64*d[2] + 64*d[3];

	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
}

static void
fletcher_4_avx2_native(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	for (; ip < ipend; ip += 2) {
		asm volatile("vpmovzxdq %0, %%ymm4"::"m" (*ip));
		asm volatile("vpaddq %ymm4, %ymm0, %ymm0");
		asm volatile("vpaddq %ymm0, %ymm1, %ymm1");
		asm volatile("vpaddq %ymm1, %ymm2, %ymm2");
		asm volatile("vpaddq %ymm2, %ymm3, %ymm3");
	}
}

static void
fletcher_4_avx2_byteswap(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	static const struct {
		uint64_t v[4] __attribute__((aligned(32)));
	} mask = {
		.v = { 0xFFFFFFFF00010203, 0xFFFFFFFF08090A0B,
		    0xFFFFFFFF00010203, 0xFFFFFFFF08090A0B }
	};
	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	asm volatile("vmovdqa %0, %%ymm5"::"m"(mask));

	for (; ip < ipend; ip += 2) {
		asm volatile("vpmovzxdq %0, %%ymm4"::"m" (*ip));
		asm volatile("vpshufb %ymm5, %ymm4, %ymm4");

		asm volatile("vpaddq %ymm4, %ymm0, %ymm0");
		asm volatile("vpaddq %ymm0, %ymm1, %ymm1");
		asm volatile("vpaddq %ymm1, %ymm2, %ymm2");
		asm volatile("vpaddq %ymm2, %ymm3, %ymm3");
	}
}

static boolean_t fletcher_4_avx2_valid(void)
{
	return (zfs_avx_available() && zfs_avx2_available());
}

const fletcher_4_ops_t fletcher_4_avx2_ops = {
	.init_native = fletcher_4_avx2_init,
	.fini_native = fletcher_4_avx2_fini,
	.compute_native = fletcher_4_avx2_native,
	.init_byteswap = fletcher_4_avx2_init,
	.fini_byteswap = fletcher_4_avx2_fini,
	.compute_byteswap = fletcher_4_avx2_byteswap,
	.valid = fletcher_4_avx2_valid,
	.name = "avx2"
};

#endif /* defined(HAVE_AVX) && defined(HAVE_AVX2) */
