// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2021-2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/simd.h>
#include <sys/zfs_context.h>
#include <sys/zfs_impl.h>
#include <sys/blake3.h>

#include "blake3_impl.h"

#if defined(__aarch64__) || \
	(defined(__x86_64) && defined(HAVE_SSE2)) || \
	(defined(__PPC64__) && defined(__LITTLE_ENDIAN__))

extern void ASMABI zfs_blake3_compress_in_place_sse2(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags);

extern void ASMABI zfs_blake3_compress_xof_sse2(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]);

extern void ASMABI zfs_blake3_hash_many_sse2(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out);

static void blake3_compress_in_place_sse2(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags) {
	kfpu_begin();
	zfs_blake3_compress_in_place_sse2(cv, block, block_len, counter,
	    flags);
	kfpu_end();
}

static void blake3_compress_xof_sse2(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]) {
	kfpu_begin();
	zfs_blake3_compress_xof_sse2(cv, block, block_len, counter, flags,
	    out);
	kfpu_end();
}

static void blake3_hash_many_sse2(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out) {
	kfpu_begin();
	zfs_blake3_hash_many_sse2(inputs, num_inputs, blocks, key, counter,
	    increment_counter, flags, flags_start, flags_end, out);
	kfpu_end();
}

static boolean_t blake3_is_sse2_supported(void)
{
#if defined(__x86_64)
	return (kfpu_allowed() && zfs_sse2_available());
#elif defined(__PPC64__)
	return (kfpu_allowed() && zfs_vsx_available());
#else
	return (kfpu_allowed());
#endif
}

const blake3_ops_t blake3_sse2_impl = {
	.compress_in_place = blake3_compress_in_place_sse2,
	.compress_xof = blake3_compress_xof_sse2,
	.hash_many = blake3_hash_many_sse2,
	.is_supported = blake3_is_sse2_supported,
	.degree = 4,
	.name = "sse2"
};
#endif

#if defined(__aarch64__) || \
	(defined(__x86_64) && defined(HAVE_SSE2)) || \
	(defined(__PPC64__) && defined(__LITTLE_ENDIAN__))

extern void ASMABI zfs_blake3_compress_in_place_sse41(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags);

extern void ASMABI zfs_blake3_compress_xof_sse41(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]);

extern void ASMABI zfs_blake3_hash_many_sse41(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out);

static void blake3_compress_in_place_sse41(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags) {
	kfpu_begin();
	zfs_blake3_compress_in_place_sse41(cv, block, block_len, counter,
	    flags);
	kfpu_end();
}

static void blake3_compress_xof_sse41(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]) {
	kfpu_begin();
	zfs_blake3_compress_xof_sse41(cv, block, block_len, counter, flags,
	    out);
	kfpu_end();
}

static void blake3_hash_many_sse41(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out) {
	kfpu_begin();
	zfs_blake3_hash_many_sse41(inputs, num_inputs, blocks, key, counter,
	    increment_counter, flags, flags_start, flags_end, out);
	kfpu_end();
}

static boolean_t blake3_is_sse41_supported(void)
{
#if defined(__x86_64)
	return (kfpu_allowed() && zfs_sse4_1_available());
#elif defined(__PPC64__)
	return (kfpu_allowed() && zfs_vsx_available());
#else
	return (kfpu_allowed());
#endif
}

const blake3_ops_t blake3_sse41_impl = {
	.compress_in_place = blake3_compress_in_place_sse41,
	.compress_xof = blake3_compress_xof_sse41,
	.hash_many = blake3_hash_many_sse41,
	.is_supported = blake3_is_sse41_supported,
	.degree = 4,
	.name = "sse41"
};
#endif

#if defined(__x86_64) && defined(HAVE_SSE4_1) && defined(HAVE_AVX2)
extern void ASMABI zfs_blake3_hash_many_avx2(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out);

static void blake3_hash_many_avx2(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out) {
	kfpu_begin();
	zfs_blake3_hash_many_avx2(inputs, num_inputs, blocks, key, counter,
	    increment_counter, flags, flags_start, flags_end, out);
	kfpu_end();
}

static boolean_t blake3_is_avx2_supported(void)
{
	return (kfpu_allowed() && zfs_sse4_1_available() &&
	    zfs_avx2_available());
}

const blake3_ops_t
blake3_avx2_impl = {
	.compress_in_place = blake3_compress_in_place_sse41,
	.compress_xof = blake3_compress_xof_sse41,
	.hash_many = blake3_hash_many_avx2,
	.is_supported = blake3_is_avx2_supported,
	.degree = 8,
	.name = "avx2"
};
#endif

#if defined(__x86_64) && defined(HAVE_AVX512F) && defined(HAVE_AVX512VL)
extern void ASMABI zfs_blake3_compress_in_place_avx512(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags);

extern void ASMABI zfs_blake3_compress_xof_avx512(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]);

extern void ASMABI zfs_blake3_hash_many_avx512(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out);

static void blake3_compress_in_place_avx512(uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags) {
	kfpu_begin();
	zfs_blake3_compress_in_place_avx512(cv, block, block_len, counter,
	    flags);
	kfpu_end();
}

static void blake3_compress_xof_avx512(const uint32_t cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN], uint8_t block_len,
    uint64_t counter, uint8_t flags, uint8_t out[64]) {
	kfpu_begin();
	zfs_blake3_compress_xof_avx512(cv, block, block_len, counter, flags,
	    out);
	kfpu_end();
}

static void blake3_hash_many_avx512(const uint8_t * const *inputs,
    size_t num_inputs, size_t blocks, const uint32_t key[8],
    uint64_t counter, boolean_t increment_counter, uint8_t flags,
    uint8_t flags_start, uint8_t flags_end, uint8_t *out) {
	kfpu_begin();
	zfs_blake3_hash_many_avx512(inputs, num_inputs, blocks, key, counter,
	    increment_counter, flags, flags_start, flags_end, out);
	kfpu_end();
}

static boolean_t blake3_is_avx512_supported(void)
{
	return (kfpu_allowed() && zfs_avx512f_available() &&
	    zfs_avx512vl_available());
}

const blake3_ops_t blake3_avx512_impl = {
	.compress_in_place = blake3_compress_in_place_avx512,
	.compress_xof = blake3_compress_xof_avx512,
	.hash_many = blake3_hash_many_avx512,
	.is_supported = blake3_is_avx512_supported,
	.degree = 16,
	.name = "avx512"
};
#endif

extern const blake3_ops_t blake3_generic_impl;

static const blake3_ops_t *const blake3_impls[] = {
	&blake3_generic_impl,
#if defined(__aarch64__) || \
	(defined(__x86_64) && defined(HAVE_SSE2)) || \
	(defined(__PPC64__) && defined(__LITTLE_ENDIAN__))
	&blake3_sse2_impl,
#endif
#if defined(__aarch64__) || \
	(defined(__x86_64) && defined(HAVE_SSE4_1)) || \
	(defined(__PPC64__) && defined(__LITTLE_ENDIAN__))
	&blake3_sse41_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSE4_1) && defined(HAVE_AVX2)
	&blake3_avx2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F) && defined(HAVE_AVX512VL)
	&blake3_avx512_impl,
#endif
};

/* use the generic implementation functions */
#define	IMPL_NAME		"blake3"
#define	IMPL_OPS_T		blake3_ops_t
#define	IMPL_ARRAY		blake3_impls
#define	IMPL_GET_OPS		blake3_get_ops
#define	ZFS_IMPL_OPS		zfs_blake3_ops
#include <generic_impl.c>

#ifdef _KERNEL
void **blake3_per_cpu_ctx;

void
blake3_per_cpu_ctx_init(void)
{
	/*
	 * Create "The Godfather" ptr to hold all blake3 ctx
	 */
	blake3_per_cpu_ctx = kmem_alloc(max_ncpus * sizeof (void *), KM_SLEEP);
	for (int i = 0; i < max_ncpus; i++) {
		blake3_per_cpu_ctx[i] = kmem_alloc(sizeof (BLAKE3_CTX),
		    KM_SLEEP);
	}
}

void
blake3_per_cpu_ctx_fini(void)
{
	for (int i = 0; i < max_ncpus; i++) {
		memset(blake3_per_cpu_ctx[i], 0, sizeof (BLAKE3_CTX));
		kmem_free(blake3_per_cpu_ctx[i], sizeof (BLAKE3_CTX));
	}
	memset(blake3_per_cpu_ctx, 0, max_ncpus * sizeof (void *));
	kmem_free(blake3_per_cpu_ctx, max_ncpus * sizeof (void *));
}

#define	IMPL_FMT(impl, i)	(((impl) == (i)) ? "[%s] " : "%s ")

#if defined(__linux__)

static int
blake3_param_get(char *buffer, zfs_kernel_param_t *unused)
{
	const uint32_t impl = IMPL_READ(generic_impl_chosen);
	char *fmt;
	int cnt = 0;

	/* cycling */
	fmt = IMPL_FMT(impl, IMPL_CYCLE);
	cnt += kmem_scnprintf(buffer + cnt, PAGE_SIZE - cnt, fmt, "cycle");

	/* list fastest */
	fmt = IMPL_FMT(impl, IMPL_FASTEST);
	cnt += kmem_scnprintf(buffer + cnt, PAGE_SIZE - cnt, fmt, "fastest");

	/* list all supported implementations */
	generic_impl_init();
	for (uint32_t i = 0; i < generic_supp_impls_cnt; ++i) {
		fmt = IMPL_FMT(impl, i);
		cnt += kmem_scnprintf(buffer + cnt, PAGE_SIZE - cnt, fmt,
		    blake3_impls[i]->name);
	}

	return (cnt);
}

static int
blake3_param_set(const char *val, zfs_kernel_param_t *unused)
{
	(void) unused;
	return (generic_impl_setname(val));
}

#elif defined(__FreeBSD__)

#include <sys/sbuf.h>

static int
blake3_param(ZFS_MODULE_PARAM_ARGS)
{
	int err;

	generic_impl_init();
	if (req->newptr == NULL) {
		const uint32_t impl = IMPL_READ(generic_impl_chosen);
		const int init_buflen = 64;
		const char *fmt;
		struct sbuf *s;

		s = sbuf_new_for_sysctl(NULL, NULL, init_buflen, req);

		/* cycling */
		fmt = IMPL_FMT(impl, IMPL_CYCLE);
		(void) sbuf_printf(s, fmt, "cycle");

		/* list fastest */
		fmt = IMPL_FMT(impl, IMPL_FASTEST);
		(void) sbuf_printf(s, fmt, "fastest");

		/* list all supported implementations */
		for (uint32_t i = 0; i < generic_supp_impls_cnt; ++i) {
			fmt = IMPL_FMT(impl, i);
			(void) sbuf_printf(s, fmt, generic_supp_impls[i]->name);
		}

		err = sbuf_finish(s);
		sbuf_delete(s);

		return (err);
	}

	char buf[16];

	err = sysctl_handle_string(oidp, buf, sizeof (buf), req);
	if (err) {
		return (err);
	}

	return (-generic_impl_setname(buf));
}
#endif

#undef IMPL_FMT

ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs, zfs_, blake3_impl,
    blake3_param_set, blake3_param_get, ZMOD_RW, \
	"Select BLAKE3 implementation.");
#endif
