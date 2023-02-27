/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (C) 2022 Zettabyte Software, LLC. All rights reserved.
 */

/*
 * Implement fast Fletcher4 with generic vectorization.
 *
 * Use generic GNU C vectors to generate optimized SIMD code through the
 * compiler. This has the advantage of allowing SIMD on various architectures
 * to be supported with ease.
 */

#include <sys/spa_checksum.h>
#include <sys/string.h>
#include <sys/simd.h>
#include <zfs_fletcher.h>

#ifndef	__has_builtin
#define	__has_builtin(x) 0
#endif

/*
 * GCC 4.9.0 is the minimum that supports the inline vpmovzxdq that we use.
 * Give builds that use older compilers superscalar8 instead.
 *
 * Clang reports itself as an old version of gcc, so we must explicitly exclude
 * it or else it will produce superscalar8 too.
 */
#if defined(HAVE_AVX2) && defined(__GNUC__) && defined(__GNUC_MINOR__) && \
	!defined(__clang_major__)
#if (__GNUC__ == 4 && __GNUC_MINOR__ < 9) || __GNUC__ < 4
#undef HAVE_AVX2
#endif
#endif

ZFS_NO_SANITIZE_UNDEFINED
static void
fletcher_4_generic_init(fletcher_4_ctx_t *ctx)
{
#if defined(HAVE_AVX2) || defined(__powerpc64__) || defined(__aarch64__)
	kfpu_begin();
#endif
	memset(ctx->generic, 0, 4 * sizeof (zfs_fletcher_generic_t));
}

#pragma GCC push_options

#if defined(HAVE_AVX2)

#ifdef __clang_major__
#pragma clang attribute push(__attribute__((target("avx2"))), \
    apply_to = function)
#else
#pragma GCC target("avx2")
#endif

#elif defined(__aarch64__)
#ifdef __clang_major__
#pragma clang attribute \
    push(__attribute__((target("+fp+simd"))), \
    apply_to = function)
#else
#pragma GCC target("+fp+simd")
#endif


#elif defined(__powerpc64__)
#ifdef __clang_major__
#pragma clang attribute \
    push(__attribute__((target("vsx,power8-vector"))), \
    apply_to = function)
#else
#pragma GCC target("vsx,power8-vector")
#endif

#endif

ZFS_NO_SANITIZE_UNDEFINED
static void
fletcher_4_generic_native(fletcher_4_ctx_t *ctx, const void *buf,
    uint64_t size)
{
	const u32x4 *ip = buf;
	const u32x4 *ipend = (u32x4 *)((uint8_t *)ip + size);

	v512 a = ctx->generic[0];
	v512 b = ctx->generic[1];
	v512 c = ctx->generic[2];
	v512 d = ctx->generic[3];

	do {
#if defined(HAVE_AVX2) && !defined(__clang_major__)
		/*
		 * GCC 12.2 is not smart enough to generate vpmovzxdq, so we
		 * must do it manually.
		 *
		 * We also implement 256-bit vector operations in generic GNU C
		 * code to workaround the following GCC vector lowering bug:
		 *
		 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=107916
		 */
		v512 t;
		asm("vpmovzxdq %1, %0" : "=x" (t.u64h[0]) : "m" (*ip));
		a.u64h[0] += t.u64h[0];
		b.u64h[0] += a.u64h[0];
		c.u64h[0] += b.u64h[0];
		d.u64h[0] += c.u64h[0];
		asm("vpmovzxdq %1, %0" : "=x" (t.u64h[1]) : "m" (*(ip+1)));
		a.u64h[1] += t.u64h[1];
		b.u64h[1] += a.u64h[1];
		c.u64h[1] += b.u64h[1];
		d.u64h[1] += c.u64h[1];
#elif (defined(__powerpc64__) || defined(__aarch64__)) && \
	!defined(__clang_major__)
		/*
		 * GCC has a vector lowering bug that makes using vector
		 * operations larger than the hardware SIMD width extremely
		 * inefficient, so we manually do generic 128-bit operations.
		 * This code could be reused for relatively efficient assembly
		 * output from GCC on other architectures that do 128-bit SIMD
		 * operations.
		 */
		v512 t;
		t.u64[0] = (*ip)[0];
		t.u64[1] = (*ip)[1];
		t.u64[2] = (*ip)[2];
		t.u64[3] = (*ip)[3];
		t.u64[4] = (*(ip+1))[0];
		t.u64[5] = (*(ip+1))[1];
		t.u64[6] = (*(ip+1))[2];
		t.u64[7] = (*(ip+1))[3];

		a.u64q[0] += t.u64q[0];
		a.u64q[1] += t.u64q[1];
		a.u64q[2] += t.u64q[2];
		a.u64q[3] += t.u64q[3];
		b.u64q[0] += a.u64q[0];
		b.u64q[1] += a.u64q[1];
		b.u64q[2] += a.u64q[2];
		b.u64q[3] += a.u64q[3];
		c.u64q[0] += b.u64q[0];
		c.u64q[1] += b.u64q[1];
		c.u64q[2] += b.u64q[2];
		c.u64q[3] += b.u64q[3];
		d.u64q[0] += c.u64q[0];
		d.u64q[1] += c.u64q[1];
		d.u64q[2] += c.u64q[2];
		d.u64q[3] += c.u64q[3];
#else
/* GCC versions before 9.1.0 do not support __builtin_convertvector() */
#if __has_builtin(__builtin_convertvector)
		a.u64h[0] += __builtin_convertvector(*ip, u64x4);
		a.u64h[1] += __builtin_convertvector(*(ip+1), u64x4);
#else
		v512 t;
		t.u64[0] = (*ip)[0];
		t.u64[1] = (*ip)[1];
		t.u64[2] = (*ip)[2];
		t.u64[3] = (*ip)[3];
		t.u64[4] = (*(ip+1))[0];
		t.u64[5] = (*(ip+1))[1];
		t.u64[6] = (*(ip+1))[2];
		t.u64[7] = (*(ip+1))[3];
		a.u64 += t.u64;
#endif
		b.u64 += a.u64;
		c.u64 += b.u64;
		d.u64 += c.u64;
#endif
	} while ((ip += 2) < ipend);

	ctx->generic[0] = a;
	ctx->generic[1] = b;
	ctx->generic[2] = c;
	ctx->generic[3] = d;
}

ZFS_NO_SANITIZE_UNDEFINED
static void
fletcher_4_generic_byteswap(fletcher_4_ctx_t *ctx, const void *buf,
    uint64_t size)
{
	const u32x4 *ip = buf;
	const u32x4 *ipend = (u32x4 *)((uint8_t *)ip + size);


	v512 a = ctx->generic[0];
	v512 b = ctx->generic[1];
	v512 c = ctx->generic[2];
	v512 d = ctx->generic[3];

	do {
#if defined(HAVE_AVX2) && !defined(__clang_major__)
		/*
		 * GCC does not optimize the generic version well, so we need
		 * to give it a version that it does optimize well. That means
		 * an inline instruction on x86_64 and the shuffle instead of
		 * __builtin_byteswap64().
		 *
		 * We also implement 256-bit vector operations in generic GNU C
		 * code to workaround the following GCC vector lowering bug:
		 *
		 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=107916
		 */
		v512 t;

		u8x32 mask = {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10,
		    9, 8, 23, 22, 21, 20, 19, 18, 17, 16, 31, 30, 29, 28, 27,
		    26, 25, 24};
		asm("vpmovzxdq %1, %0" : "=x" (t.u64h[0]) : "m" (*ip));
		t.u8h[0] = __builtin_shuffle(t.u8h[0], mask);
		a.u64h[0] += t.u64h[0];
		b.u64h[0] += a.u64h[0];
		c.u64h[0] += b.u64h[0];
		d.u64h[0] += c.u64h[0];
		asm("vpmovzxdq %1, %0" : "=x" (t.u64h[1]) : "m" (*(ip+1)));
		t.u8h[1] = __builtin_shuffle(t.u8h[1], mask);
		a.u64h[1] += t.u64h[1];
		b.u64h[1] += a.u64h[1];
		c.u64h[1] += b.u64h[1];
		d.u64h[1] += c.u64h[1];
#elif (defined(__powerpc64__) || defined(__aarch64__)) && \
	!defined(__clang_major__)
		/*
		 * GCC has a vector lowering bug that makes using vector
		 * operations larger than the hardware SIMD width extremely
		 * inefficient, so we manually do generic 128-bit operations.
		 * This code could be reused for relatively efficient assembly
		 * output from GCC on other architectures that do 128-bit SIMD
		 * operations.
		 *
		 * Note that this version should not be compiled by Clang. It
		 * generates inefficient scalar byteswaps rather than doing
		 * vector byteswaps for this.
		 */
		v512 t;
		t.u64[0] = __builtin_bswap32((*ip)[0]);
		t.u64[1] = __builtin_bswap32((*ip)[1]);
		t.u64[2] = __builtin_bswap32((*ip)[2]);
		t.u64[3] = __builtin_bswap32((*ip)[3]);
		t.u64[4] = __builtin_bswap32((*(ip+1))[0]);
		t.u64[5] = __builtin_bswap32((*(ip+1))[1]);
		t.u64[6] = __builtin_bswap32((*(ip+1))[2]);
		t.u64[7] = __builtin_bswap32((*(ip+1))[3]);

		a.u64q[0] += t.u64q[0];
		a.u64q[1] += t.u64q[1];
		a.u64q[2] += t.u64q[2];
		a.u64q[3] += t.u64q[3];
		b.u64q[0] += a.u64q[0];
		b.u64q[1] += a.u64q[1];
		b.u64q[2] += a.u64q[2];
		b.u64q[3] += a.u64q[3];
		c.u64q[0] += b.u64q[0];
		c.u64q[1] += b.u64q[1];
		c.u64q[2] += b.u64q[2];
		c.u64q[3] += b.u64q[3];
		d.u64q[0] += c.u64q[0];
		d.u64q[1] += c.u64q[1];
		d.u64q[2] += c.u64q[2];
		d.u64q[3] += c.u64q[3];
#else
/* GCC versions before 9.1.0 do not support __builtin_convertvector() */
#if __has_builtin(__builtin_convertvector)
		v512 t;
		t.u64h[0] = __builtin_convertvector(*ip, u64x4);
		t.u64h[1] = __builtin_convertvector(*(ip+1), u64x4);
#else
		v512 t;
		t.u64[0] = (*ip)[0];
		t.u64[1] = (*ip)[1];
		t.u64[2] = (*ip)[2];
		t.u64[3] = (*ip)[3];
		t.u64[4] = (*(ip+1))[0];
		t.u64[5] = (*(ip+1))[1];
		t.u64[6] = (*(ip+1))[2];
		t.u64[7] = (*(ip+1))[3];
#endif

		/*
		 * Clang had no problem with earlier versions of this code, but
		 * it has trouble optimizing the latest version that used
		 * `__builtin_byteswap64()`, so we use
		 * `__builtin_shufflevector()`.
		 *
		 * GCC versions prior to 12 have no hope of generating good
		 * code with `__builtin_byteswap64()`.
		 */
#ifdef __clang_major__
		t.u8h[0] = __builtin_shufflevector(t.u8h[0], t.u8h[0], 7, 6, 5,
		    4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 23, 22, 21,
		    20, 19, 18, 17, 16, 31, 30, 29, 28, 27, 26, 25, 24);
		t.u8h[1] = __builtin_shufflevector(t.u8h[1], t.u8h[1], 7, 6, 5,
		    4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 23, 22, 21,
		    20, 19, 18, 17, 16, 31, 30, 29, 28, 27, 26, 25, 24);
#else
		u8x32 mask = {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10,
		    9, 8, 23, 22, 21, 20, 19, 18, 17, 16, 31, 30, 29, 28, 27,
		    26, 25, 24};
		t.u8h[0] = __builtin_shuffle(t.u8h[0], mask);
		t.u8h[1] = __builtin_shuffle(t.u8h[1], mask);
#endif
		a.u64 += t.u64;
		b.u64 += a.u64;
		c.u64 += b.u64;
		d.u64 += c.u64;
#endif
	} while ((ip += 2) < ipend);

	ctx->generic[0] = a;
	ctx->generic[1] = b;
	ctx->generic[2] = c;
	ctx->generic[3] = d;
}

#ifdef __clang_major__
#if defined(HAVE_AVX2) || defined(__powerpc64__) || defined(__aarch64__)
#pragma clang attribute pop
#endif
#else
#pragma GCC pop_options
#endif

ZFS_NO_SANITIZE_UNDEFINED
static void
fletcher_4_generic_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp)
{
	static const uint8_t
	CcA[] = {   0,   0,   1,   3,   6,  10,  15,  21 },
	CcB[] = {  28,  36,  44,  52,  60,  68,  76,  84 },
	DcA[] = {   0,   0,   0,   1,   4,  10,  20,  35 };
	static const uint16_t
	DcB[] = {  56,  84, 120, 164, 216, 276, 344, 420 },
	DcC[] = { 448, 512, 576, 640, 704, 768, 832, 896 };

	uint64_t A, B, C, D;
	uint64_t i;

	A = ctx->generic[0].u64[0];
	B = 8 * ctx->generic[1].u64[0];
	C = 64 * ctx->generic[2].u64[0] - CcB[0] * ctx->generic[1].u64[0];
	D = 512 * ctx->generic[3].u64[0] - DcC[0] * ctx->generic[2].u64[0] +
	    DcB[0] * ctx->generic[1].u64[0];

	for (i = 1; i < 8; i++) {
		A += ctx->generic[0].u64[i];
		B += 8 * ctx->generic[1].u64[i] - i * ctx->generic[0].u64[i];
		C += 64 * ctx->generic[2].u64[i] - CcB[i] *
		    ctx->generic[1].u64[i] + CcA[i] * ctx->generic[0].u64[i];
		D += 512 * ctx->generic[3].u64[i] - DcC[i] *
		    ctx->generic[2].u64[i] + DcB[i] * ctx->generic[1].u64[i] -
		    DcA[i] * ctx->generic[0].u64[i];
	}

	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
#if defined(HAVE_AVX2) || defined(__powerpc64__) || defined(__aarch64__)
	kfpu_end();
#endif
}

static boolean_t fletcher_4_generic_valid(void)
{
#if defined(HAVE_AVX2)
	return (kfpu_allowed() && zfs_avx_available() && zfs_avx2_available());
#elif defined(__powerpc64__)
	return (kfpu_allowed() && zfs_vsx_available() &&
	    zfs_isa207_available());
#elif defined(__aarch64__)
	return (kfpu_allowed());
#else
	return (B_TRUE);
#endif
}

const fletcher_4_ops_t fletcher_4_generic_ops = {
	.init_native = fletcher_4_generic_init,
	.fini_native = fletcher_4_generic_fini,
	.compute_native = fletcher_4_generic_native,
	.init_byteswap = fletcher_4_generic_init,
	.fini_byteswap = fletcher_4_generic_fini,
	.compute_byteswap = fletcher_4_generic_byteswap,
	.valid = fletcher_4_generic_valid,
#if defined(HAVE_AVX2)
	.name = "generic-avx2"
#elif defined(__powerpc64__)
	.name = "generic-vsx"
#elif defined(__aarch64__)
	.name = "generic-aarch64_neon"
#else
	.name = "superscalar8"
#endif
};
