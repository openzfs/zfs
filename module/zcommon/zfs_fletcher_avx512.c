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
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#if defined(__x86_64) && defined(HAVE_AVX512F)

#include <linux/simd_x86.h>
#include <sys/byteorder.h>
#include <sys/spa_checksum.h>
#include <zfs_fletcher.h>

#define	__asm __asm__ __volatile__

typedef struct {
	uint64_t v[8] __attribute__((aligned(64)));
} zfs_avx512_t;

static void
fletcher_4_avx512f_init(zio_cksum_t *zcp)
{
	kfpu_begin();

	/* clear registers */
	__asm("vpxorq %zmm0, %zmm0, %zmm0");
	__asm("vpxorq %zmm1, %zmm1, %zmm1");
	__asm("vpxorq %zmm2, %zmm2, %zmm2");
	__asm("vpxorq %zmm3, %zmm3, %zmm3");
}

static void
fletcher_4_avx512f_native(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = (uint32_t *)((uint8_t *)ip + size);

	for (; ip < ipend; ip += 8) {
		__asm("vpmovzxdq %0, %%zmm4"::"m" (*ip));
		__asm("vpaddq %zmm4, %zmm0, %zmm0");
		__asm("vpaddq %zmm0, %zmm1, %zmm1");
		__asm("vpaddq %zmm1, %zmm2, %zmm2");
		__asm("vpaddq %zmm2, %zmm3, %zmm3");
	}
}

static void
fletcher_4_avx512f_byteswap(const void *buf, uint64_t size, zio_cksum_t *unused)
{
	static const uint64_t byteswap_mask = 0xFFULL;
	const uint32_t *ip = buf;
	const uint32_t *ipend = (uint32_t *)((uint8_t *)ip + size);

	__asm("vpbroadcastq %0, %%zmm8" :: "r" (byteswap_mask));
	__asm("vpsllq $8, %zmm8, %zmm9");
	__asm("vpsllq $16, %zmm8, %zmm10");
	__asm("vpsllq $24, %zmm8, %zmm11");

	for (; ip < ipend; ip += 8) {
		__asm("vpmovzxdq %0, %%zmm5"::"m" (*ip));

		__asm("vpsrlq $24, %zmm5, %zmm6");
		__asm("vpandd %zmm8, %zmm6, %zmm6");
		__asm("vpsrlq $8, %zmm5, %zmm7");
		__asm("vpandd %zmm9, %zmm7, %zmm7");
		__asm("vpord %zmm6, %zmm7, %zmm4");
		__asm("vpsllq $8, %zmm5, %zmm6");
		__asm("vpandd %zmm10, %zmm6, %zmm6");
		__asm("vpord %zmm6, %zmm4, %zmm4");
		__asm("vpsllq $24, %zmm5, %zmm5");
		__asm("vpandd %zmm11, %zmm5, %zmm5");
		__asm("vpord %zmm5, %zmm4, %zmm4");

		__asm("vpaddq %zmm4, %zmm0, %zmm0");
		__asm("vpaddq %zmm0, %zmm1, %zmm1");
		__asm("vpaddq %zmm1, %zmm2, %zmm2");
		__asm("vpaddq %zmm2, %zmm3, %zmm3");
	}
}

static void
fletcher_4_avx512f_fini(zio_cksum_t *zcp)
{
	static const uint64_t
	CcA[] = {   0,   0,   1,   3,   6,  10,  15,  21 },
	CcB[] = {  28,  36,  44,  52,  60,  68,  76,  84 },
	DcA[] = {   0,   0,   0,   1,   4,  10,  20,  35 },
	DcB[] = {  56,  84, 120, 164, 216, 276, 344, 420 },
	DcC[] = { 448, 512, 576, 640, 704, 768, 832, 896 };

	zfs_avx512_t a, b, c, b8, c64, d512;
	uint64_t A, B, C, D;
	uint64_t i;

	__asm("vmovdqu64 %%zmm0, %0":"=m" (a));
	__asm("vmovdqu64 %%zmm1, %0":"=m" (b));
	__asm("vmovdqu64 %%zmm2, %0":"=m" (c));
	__asm("vpsllq $3, %zmm1, %zmm1");
	__asm("vpsllq $6, %zmm2, %zmm2");
	__asm("vpsllq $9, %zmm3, %zmm3");

	__asm("vmovdqu64 %%zmm1, %0":"=m" (b8));
	__asm("vmovdqu64 %%zmm2, %0":"=m" (c64));
	__asm("vmovdqu64 %%zmm3, %0":"=m" (d512));

	kfpu_end();

	A = a.v[0];
	B = b8.v[0];
	C = c64.v[0] - CcB[0] * b.v[0];
	D = d512.v[0] - DcC[0] * c.v[0] + DcB[0] * b.v[0];

	for (i = 1; i < 8; i++) {
		A += a.v[i];
		B += b8.v[i] - i * a.v[i];
		C += c64.v[i] - CcB[i] * b.v[i] + CcA[i] * a.v[i];
		D += d512.v[i] - DcC[i] * c.v[i] + DcB[i] * b.v[i] -
		    DcA[i] * a.v[i];
	}

	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
}

static boolean_t
fletcher_4_avx512f_valid(void)
{
	return (zfs_avx512f_available());
}

const fletcher_4_ops_t fletcher_4_avx512f_ops = {
	.init_native = fletcher_4_avx512f_init,
	.fini_native = fletcher_4_avx512f_fini,
	.compute_native = fletcher_4_avx512f_native,
	.init_byteswap = fletcher_4_avx512f_init,
	.fini_byteswap = fletcher_4_avx512f_fini,
	.compute_byteswap = fletcher_4_avx512f_byteswap,
	.valid = fletcher_4_avx512f_valid,
	.name = "avx512f"
};

#endif /* defined(__x86_64) && defined(HAVE_AVX512F) */
