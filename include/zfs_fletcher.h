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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

#ifndef	_ZFS_FLETCHER_H
#define	_ZFS_FLETCHER_H extern __attribute__((visibility("default")))

#include <sys/types.h>
#include <sys/spa_checksum.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * fletcher checksum functions
 *
 * Note: Fletcher checksum methods expect buffer size to be 4B aligned. This
 * limitation stems from the algorithm design. Performing incremental checksum
 * without said alignment would yield different results. Therefore, the code
 * includes assertions for the size alignment.
 * For compatibility, it is required that some code paths calculate checksum of
 * non-aligned buffer sizes. For this purpose, `fletcher_4_native_varsize()`
 * checksum method is added. This method will ignore last (size % 4) bytes of
 * the data buffer.
 */
_ZFS_FLETCHER_H void fletcher_init(zio_cksum_t *);
_ZFS_FLETCHER_H void fletcher_2_native(const void *, uint64_t, const void *,
    zio_cksum_t *);
_ZFS_FLETCHER_H void fletcher_2_byteswap(const void *, uint64_t, const void *,
    zio_cksum_t *);
_ZFS_FLETCHER_H void fletcher_4_native(const void *, uint64_t, const void *,
    zio_cksum_t *);
_ZFS_FLETCHER_H int fletcher_2_incremental_native(void *, size_t, void *);
_ZFS_FLETCHER_H int fletcher_2_incremental_byteswap(void *, size_t, void *);
_ZFS_FLETCHER_H void fletcher_4_native_varsize(const void *, uint64_t,
    zio_cksum_t *);
_ZFS_FLETCHER_H void fletcher_4_byteswap_varsize(const void *, uint64_t,
    zio_cksum_t *);
_ZFS_FLETCHER_H void fletcher_4_byteswap(const void *, uint64_t, const void *,
    zio_cksum_t *);
_ZFS_FLETCHER_H int fletcher_4_incremental_native(void *, size_t, void *);
_ZFS_FLETCHER_H int fletcher_4_incremental_byteswap(void *, size_t, void *);
_ZFS_FLETCHER_H int fletcher_4_impl_set(const char *selector);
_ZFS_FLETCHER_H void fletcher_4_init(void);
_ZFS_FLETCHER_H void fletcher_4_fini(void);



/* Internal fletcher ctx */

typedef struct zfs_fletcher_superscalar {
	uint64_t v[4];
} zfs_fletcher_superscalar_t;

typedef struct zfs_fletcher_sse {
	uint64_t v[2];
} zfs_fletcher_sse_t;

typedef struct zfs_fletcher_avx {
	uint64_t v[4];
} zfs_fletcher_avx_t;

typedef struct zfs_fletcher_avx512 {
	uint64_t v[8];
} zfs_fletcher_avx512_t;

typedef struct zfs_fletcher_aarch64_neon {
	uint64_t v[2];
} zfs_fletcher_aarch64_neon_t;


typedef union fletcher_4_ctx {
	zio_cksum_t scalar;
	zfs_fletcher_superscalar_t superscalar[4];

#if HAVE_SIMD(SSE2) || (HAVE_SIMD(SSE2) && HAVE_SIMD(SSSE3))
	zfs_fletcher_sse_t sse[4];
#endif
#if HAVE_SIMD(AVX) && HAVE_SIMD(AVX2)
	zfs_fletcher_avx_t avx[4];
#endif
#if defined(__x86_64) && HAVE_SIMD(AVX512F)
	zfs_fletcher_avx512_t avx512[4];
#endif
#if defined(__aarch64__)
	zfs_fletcher_aarch64_neon_t aarch64_neon[4];
#endif
} fletcher_4_ctx_t;

/*
 * fletcher checksum struct
 */
typedef void (*fletcher_4_init_f)(fletcher_4_ctx_t *);
typedef void (*fletcher_4_fini_f)(fletcher_4_ctx_t *, zio_cksum_t *);
typedef void (*fletcher_4_compute_f)(fletcher_4_ctx_t *,
    const void *, uint64_t);

typedef struct fletcher_4_func {
	fletcher_4_init_f init_native;
	fletcher_4_fini_f fini_native;
	fletcher_4_compute_f compute_native;
	fletcher_4_init_f init_byteswap;
	fletcher_4_fini_f fini_byteswap;
	fletcher_4_compute_f compute_byteswap;
	boolean_t (*valid)(void);
	boolean_t uses_fpu;
	const char *name;
} __attribute__((aligned(64))) fletcher_4_ops_t;

_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_superscalar_ops;
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_superscalar4_ops;

#if HAVE_SIMD(SSE2)
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_sse2_ops;
#endif

#if HAVE_SIMD(SSE2) && HAVE_SIMD(SSSE3)
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_ssse3_ops;
#endif

#if HAVE_SIMD(AVX) && HAVE_SIMD(AVX2)
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_avx2_ops;
#endif

#if defined(__x86_64) && HAVE_SIMD(AVX512F)
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_avx512f_ops;
#endif

#if defined(__x86_64) && HAVE_SIMD(AVX512BW)
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_avx512bw_ops;
#endif

#if defined(__aarch64__)
_ZFS_FLETCHER_H const fletcher_4_ops_t fletcher_4_aarch64_neon_ops;
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_FLETCHER_H */
