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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/simd.h>
#include <sys/zfs_context.h>
#include <sys/zfs_impl.h>
#include <sys/sha2.h>

#include <sha2/sha2_impl.h>
#include <sys/asm_linkage.h>

#define	TF(E, N) \
	extern void ASMABI E(uint32_t s[8], const void *, size_t); \
	static inline void N(uint32_t s[8], const void *d, size_t b) { \
	kfpu_begin(); E(s, d, b); kfpu_end(); \
}

/* some implementation is always okay */
static inline boolean_t sha2_is_supported(void)
{
	return (B_TRUE);
}

#if defined(__x86_64)

/* Users of ASMABI requires all calls to be from wrappers */
extern void ASMABI
zfs_sha256_transform_x64(uint32_t s[8], const void *, size_t);

static inline void
tf_sha256_transform_x64(uint32_t s[8], const void *d, size_t b)
{
	zfs_sha256_transform_x64(s, d, b);
}

const sha256_ops_t sha256_x64_impl = {
	.is_supported = sha2_is_supported,
	.transform = tf_sha256_transform_x64,
	.name = "x64"
};

#if defined(HAVE_SSSE3)
static boolean_t sha2_have_ssse3(void)
{
	return (kfpu_allowed() && zfs_ssse3_available());
}

TF(zfs_sha256_transform_ssse3, tf_sha256_ssse3);
const sha256_ops_t sha256_ssse3_impl = {
	.is_supported = sha2_have_ssse3,
	.transform = tf_sha256_ssse3,
	.name = "ssse3"
};
#endif

#if defined(HAVE_AVX)
static boolean_t sha2_have_avx(void)
{
	return (kfpu_allowed() && zfs_avx_available());
}

TF(zfs_sha256_transform_avx, tf_sha256_avx);
const sha256_ops_t sha256_avx_impl = {
	.is_supported = sha2_have_avx,
	.transform = tf_sha256_avx,
	.name = "avx"
};
#endif

#if defined(HAVE_AVX2)
static boolean_t sha2_have_avx2(void)
{
	return (kfpu_allowed() && zfs_avx2_available());
}

TF(zfs_sha256_transform_avx2, tf_sha256_avx2);
const sha256_ops_t sha256_avx2_impl = {
	.is_supported = sha2_have_avx2,
	.transform = tf_sha256_avx2,
	.name = "avx2"
};
#endif

#if defined(HAVE_SSE4_1)
static boolean_t sha2_have_shani(void)
{
	return (kfpu_allowed() && zfs_sse4_1_available() && \
	    zfs_shani_available());
}

TF(zfs_sha256_transform_shani, tf_sha256_shani);
const sha256_ops_t sha256_shani_impl = {
	.is_supported = sha2_have_shani,
	.transform = tf_sha256_shani,
	.name = "shani"
};
#endif

#elif defined(__aarch64__) || (defined(__arm__) && __ARM_ARCH > 6)
static boolean_t sha256_have_neon(void)
{
	return (kfpu_allowed() && zfs_neon_available());
}

static boolean_t sha256_have_armv8ce(void)
{
	return (kfpu_allowed() && zfs_sha256_available());
}

extern void zfs_sha256_block_armv7(uint32_t s[8], const void *, size_t);
const sha256_ops_t sha256_armv7_impl = {
	.is_supported = sha2_is_supported,
	.transform = zfs_sha256_block_armv7,
	.name = "armv7"
};

TF(zfs_sha256_block_neon, tf_sha256_neon);
const sha256_ops_t sha256_neon_impl = {
	.is_supported = sha256_have_neon,
	.transform = tf_sha256_neon,
	.name = "neon"
};

TF(zfs_sha256_block_armv8, tf_sha256_armv8ce);
const sha256_ops_t sha256_armv8_impl = {
	.is_supported = sha256_have_armv8ce,
	.transform = tf_sha256_armv8ce,
	.name = "armv8-ce"
};

#elif defined(__PPC64__)
static boolean_t sha256_have_isa207(void)
{
	return (kfpu_allowed() && zfs_isa207_available());
}

TF(zfs_sha256_ppc, tf_sha256_ppc);
const sha256_ops_t sha256_ppc_impl = {
	.is_supported = sha2_is_supported,
	.transform = tf_sha256_ppc,
	.name = "ppc"
};

TF(zfs_sha256_power8, tf_sha256_power8);
const sha256_ops_t sha256_power8_impl = {
	.is_supported = sha256_have_isa207,
	.transform = tf_sha256_power8,
	.name = "power8"
};
#endif /* __PPC64__ */

/* the two generic ones */
extern const sha256_ops_t sha256_generic_impl;

/* array with all sha256 implementations */
static const sha256_ops_t *const sha256_impls[] = {
	&sha256_generic_impl,
#if defined(__x86_64)
	&sha256_x64_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSSE3)
	&sha256_ssse3_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX)
	&sha256_avx_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX2)
	&sha256_avx2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSE4_1)
	&sha256_shani_impl,
#endif
#if defined(__aarch64__) || (defined(__arm__) && __ARM_ARCH > 6)
	&sha256_armv7_impl,
	&sha256_neon_impl,
	&sha256_armv8_impl,
#endif
#if defined(__PPC64__)
	&sha256_ppc_impl,
	&sha256_power8_impl,
#endif /* __PPC64__ */
};

/* use the generic implementation functions */
#define	IMPL_NAME		"sha256"
#define	IMPL_OPS_T		sha256_ops_t
#define	IMPL_ARRAY		sha256_impls
#define	IMPL_GET_OPS		sha256_get_ops
#define	ZFS_IMPL_OPS		zfs_sha256_ops
#include <generic_impl.c>

#ifdef _KERNEL

#define	IMPL_FMT(impl, i)	(((impl) == (i)) ? "[%s] " : "%s ")

#if defined(__linux__)

static int
sha256_param_get(char *buffer, zfs_kernel_param_t *unused)
{
	const uint32_t impl = IMPL_READ(generic_impl_chosen);
	char *fmt;
	int cnt = 0;

	/* cycling */
	fmt = IMPL_FMT(impl, IMPL_CYCLE);
	cnt += sprintf(buffer + cnt, fmt, "cycle");

	/* list fastest */
	fmt = IMPL_FMT(impl, IMPL_FASTEST);
	cnt += sprintf(buffer + cnt, fmt, "fastest");

	/* list all supported implementations */
	generic_impl_init();
	for (uint32_t i = 0; i < generic_supp_impls_cnt; ++i) {
		fmt = IMPL_FMT(impl, i);
		cnt += sprintf(buffer + cnt, fmt,
		    generic_supp_impls[i]->name);
	}

	return (cnt);
}

static int
sha256_param_set(const char *val, zfs_kernel_param_t *unused)
{
	(void) unused;
	return (generic_impl_setname(val));
}

#elif defined(__FreeBSD__)

#include <sys/sbuf.h>

static int
sha256_param(ZFS_MODULE_PARAM_ARGS)
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

ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs, zfs_, sha256_impl,
    sha256_param_set, sha256_param_get, ZMOD_RW, \
	"Select SHA256 implementation.");
#endif

#undef TF
