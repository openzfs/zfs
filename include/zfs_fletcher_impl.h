#ifndef	_ZFS_FLETCHER_IMPL_H_
#define	_ZFS_FLETCHER_IMPL_H_

#include <zfs_fletcher.h>
#include <sys/simd.h>

struct fletcher_4_ctx
{
	zfs_kfpu_ctx_t *kfpu_ctx;
	union
	{
		zio_cksum_t scalar;
		zfs_fletcher_superscalar_t superscalar[4];

#if defined(HAVE_SSE2) || (defined(HAVE_SSE2) && defined(HAVE_SSSE3))
		zfs_fletcher_sse_t sse[4];
#endif
#if defined(HAVE_AVX) && defined(HAVE_AVX2)
		zfs_fletcher_avx_t avx[4];
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F)
		zfs_fletcher_avx512_t avx512[4];
#endif
#if defined(__aarch64__)
		zfs_fletcher_aarch64_neon_t aarch64_neon[4];
#endif
	};
};

static void inline
fletcher_4_ctx_init(fletcher_4_ctx_t *ctx, zfs_kfpu_ctx_t *kfpu_ctx)
{
	ctx->kfpu_ctx = kfpu_ctx;
}

static void inline
fletcher_4_kfpu_enter(fletcher_4_ctx_t *ctx)
{
	zfs_kfpu_enter(ctx->kfpu_ctx);
}

static void inline
fletcher_4_kfpu_exit(fletcher_4_ctx_t *ctx)
{
	zfs_kfpu_exit(ctx->kfpu_ctx);
}

#endif /* _ZFS_FLETCHER_IMPL_H_ */
