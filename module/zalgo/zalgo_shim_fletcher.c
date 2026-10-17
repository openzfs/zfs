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
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zalgo.h>
#include <sys/simd.h>
#include <sys/abd.h>
#include <zfs_fletcher.h>

/*
 * XXX "subtle" missing piece: the scalar fallback for small and unaligned
 *     sizes.
 *      -- robn, 2026-08-10
 */

/*
 * XXX figure out how to make all of this "generic" -- robn, 2026-08-10
 */

#define	_DEFINE_FLETCHER4_OPS(name, sym, type)				\
static int								\
zg_fletcher4_##sym##_init(void **ctx)					\
{									\
	*ctx = kmem_alloc(sizeof (fletcher_4_ctx_t), KM_SLEEP);		\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_begin();						\
	fletcher_4_##name##_ops.init_##type((fletcher_4_ctx_t *)*ctx);	\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_end();						\
	return (0);							\
}									\
static int								\
zg_fletcher4_##sym##_update(void **ctx, const uint8_t *data, size_t datalen) \
{									\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_begin();						\
	fletcher_4_##name##_ops.compute_##type((fletcher_4_ctx_t *)*ctx, \
	    data, datalen);						\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_end();						\
	return (0);							\
}									\
static int								\
zg_fletcher4_##sym##_iter(void *data, size_t datalen, void *ctx)	\
{									\
	fletcher_4_##name##_ops.compute_##type(ctx, data, datalen);	\
	return (0);							\
}									\
static int								\
zg_fletcher4_##sym##_update_abd(void **ctx, abd_t *abd, size_t datalen)	\
{									\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_begin();						\
	abd_iterate_func(abd, 0, datalen, zg_fletcher4_##sym##_iter, &ctx); \
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_end();						\
	return (0);							\
}									\
static int								\
zg_fletcher4_##sym##_final(void **ctx, zio_cksum_t *checksum)		\
{									\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_begin();						\
	fletcher_4_##name##_ops.fini_##type((fletcher_4_ctx_t *)*ctx,	\
	    checksum);							\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_end();						\
	kmem_free(*ctx, sizeof (fletcher_4_ctx_t));			\
	return (0);							\
}									\
static int								\
zg_fletcher4_##sym##_once(const uint8_t *data, size_t datalen,		\
    zio_cksum_t *checksum)						\
{									\
	fletcher_4_ctx_t ctx;						\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_begin();						\
	fletcher_4_##name##_ops.init_##type(&ctx);			\
	fletcher_4_##name##_ops.compute_##type(&ctx, data, datalen);	\
	fletcher_4_##name##_ops.fini_##type(&ctx, checksum);		\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_end();						\
	return (0);							\
}									\
static int								\
zg_fletcher4_##sym##_once_abd(abd_t *abd, size_t datalen,		\
    zio_cksum_t *checksum)						\
{									\
	fletcher_4_ctx_t ctx;						\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_begin();						\
	fletcher_4_##name##_ops.init_##type(&ctx);			\
	abd_iterate_func(abd, 0, datalen, zg_fletcher4_##sym##_iter, &ctx); \
	fletcher_4_##name##_ops.fini_##type(&ctx, checksum);		\
	if (fletcher_4_##name##_ops.uses_fpu)				\
		kfpu_end();						\
	return (0);							\
}									\
static const zalgo_checksum_ops_t zg_fletcher4_##sym##_ops = {		\
	.zgc_op_init = zg_fletcher4_##sym##_init,			\
	.zgc_op_update = zg_fletcher4_##sym##_update,			\
	.zgc_op_update_abd = zg_fletcher4_##sym##_update_abd,		\
	.zgc_op_final = zg_fletcher4_##sym##_final,			\
	.zgc_op_once = zg_fletcher4_##sym##_once,			\
	.zgc_op_once_abd = zg_fletcher4_##sym##_once_abd,		\
};									\

#define	DEFINE_FLETCHER4_OPS(name, desc)				\
	_DEFINE_FLETCHER4_OPS(name, name##_byteswap, byteswap)		\
	_DEFINE_FLETCHER4_OPS(name, name, native)			\
static									\
int zg_fletcher4_##name##_register(void)				\
{									\
	if (!fletcher_4_##name##_ops.valid())				\
		return (0);						\
	int ret = 0, err;						\
	err = zalgo_checksum_register(ZG_CHECKSUM_FLETCHER4,		\
	    #name, "OpenZFS fletcher4 [" desc "]",			\
	    &zg_fletcher4_##name##_ops);				\
	if (err != 0 && ret == 0)					\
		ret = err;						\
	err = zalgo_checksum_register(ZG_CHECKSUM_FLETCHER4_SWAP,	\
	    #name, "OpenZFS fletcher4 [" desc "]",			\
	    &zg_fletcher4_##name##_byteswap_ops);			\
	if (err != 0 && ret == 0)					\
		ret = err;						\
	return (ret);							\
}

DEFINE_FLETCHER4_OPS(scalar, "generic")
DEFINE_FLETCHER4_OPS(superscalar, "2x")
DEFINE_FLETCHER4_OPS(superscalar4, "4x")

#if HAVE_SIMD(SSE2)
DEFINE_FLETCHER4_OPS(sse2, "SSE2")
#endif

#if HAVE_SIMD(SSE2) && HAVE_SIMD(SSSE3)
DEFINE_FLETCHER4_OPS(ssse3, "SSSE3")
#endif

#if HAVE_SIMD(AVX) && HAVE_SIMD(AVX2)
DEFINE_FLETCHER4_OPS(avx2, "AVX2")
#endif

#if defined(__x86_64) && HAVE_SIMD(AVX512F)
DEFINE_FLETCHER4_OPS(avx512f, "AVX-512")
#endif

#if defined(__x86_64) && HAVE_SIMD(AVX512BW)
DEFINE_FLETCHER4_OPS(avx512bw, "AVX-512 BW")
#endif

#if defined(__aarch64__)
DEFINE_FLETCHER4_OPS(aarch64_neoni, "Neon")
#endif

int
zalgo_shim_fletcher_register(void)
{
	int ret = 0, err;

	err = zg_fletcher4_scalar_register();
	if (err != 0 && ret == 0)
		ret = err;
	err = zg_fletcher4_superscalar_register();
	if (err != 0 && ret == 0)
		ret = err;
	err = zg_fletcher4_superscalar4_register();
	if (err != 0 && ret == 0)
		ret = err;

#if HAVE_SIMD(SSE2)
	err = zg_fletcher4_sse2_register();
	if (err != 0 && ret == 0)
		ret = err;
#endif

#if HAVE_SIMD(SSE2) && HAVE_SIMD(SSSE3)
	err = zg_fletcher4_ssse3_register();
	if (err != 0 && ret == 0)
		ret = err;
#endif

#if HAVE_SIMD(AVX) && HAVE_SIMD(AVX2)
	err = zg_fletcher4_avx2_register();
	if (err != 0 && ret == 0)
		ret = err;
#endif

#if defined(__x86_64) && HAVE_SIMD(AVX512F)
	err = zg_fletcher4_avx512f_register();
	if (err != 0 && ret == 0)
		ret = err;
#endif

#if defined(__x86_64) && HAVE_SIMD(AVX512BW)
	err = zg_fletcher4_avx512bw_register();
	if (err != 0 && ret == 0)
		ret = err;
#endif

#if defined(__aarch64__)
	err = zg_fletcher4_aarch64_neon_register();
	if (err != 0 && ret == 0)
		ret = err;
#endif

	return (err);
}
