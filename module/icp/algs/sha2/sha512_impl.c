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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/simd.h>
#include <sys/zfs_context.h>
#include <sys/zfs_impl.h>
#include <sys/sha2.h>

#include <sha2/sha2_impl.h>
#include <sys/asm_linkage.h>

#define	TF(E, N) \
	extern void ASMABI E(uint64_t s[8], const void *, size_t); \
	static inline void N(uint64_t s[8], const void *d, size_t b) { \
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
zfs_sha512_transform_x64(uint64_t s[8], const void *, size_t);

static inline void
tf_sha512_transform_x64(uint64_t s[8], const void *d, size_t b)
{
	zfs_sha512_transform_x64(s, d, b);
}
const sha512_ops_t sha512_x64_impl = {
	.is_supported = sha2_is_supported,
	.transform = tf_sha512_transform_x64,
	.name = "x64"
};

#if defined(HAVE_AVX)
static boolean_t sha2_have_avx(void)
{
	return (kfpu_allowed() && zfs_avx_available());
}

TF(zfs_sha512_transform_avx, tf_sha512_avx);
const sha512_ops_t sha512_avx_impl = {
	.is_supported = sha2_have_avx,
	.transform = tf_sha512_avx,
	.name = "avx"
};
#endif

#if defined(HAVE_AVX2)
static boolean_t sha2_have_avx2(void)
{
	return (kfpu_allowed() && zfs_avx2_available());
}

TF(zfs_sha512_transform_avx2, tf_sha512_avx2);
const sha512_ops_t sha512_avx2_impl = {
	.is_supported = sha2_have_avx2,
	.transform = tf_sha512_avx2,
	.name = "avx2"
};
#endif

#elif defined(__aarch64__) || defined(__arm__)
extern void zfs_sha512_block_armv7(uint64_t s[8], const void *, size_t);
const sha512_ops_t sha512_armv7_impl = {
	.is_supported = sha2_is_supported,
	.transform = zfs_sha512_block_armv7,
	.name = "armv7"
};

#if defined(__aarch64__)
static boolean_t sha512_have_armv8ce(void)
{
	return (kfpu_allowed() && zfs_sha512_available());
}

TF(zfs_sha512_block_armv8, tf_sha512_armv8ce);
const sha512_ops_t sha512_armv8_impl = {
	.is_supported = sha512_have_armv8ce,
	.transform = tf_sha512_armv8ce,
	.name = "armv8-ce"
};
#endif

#if defined(__arm__) && __ARM_ARCH > 6
static boolean_t sha512_have_neon(void)
{
	return (kfpu_allowed() && zfs_neon_available());
}

TF(zfs_sha512_block_neon, tf_sha512_neon);
const sha512_ops_t sha512_neon_impl = {
	.is_supported = sha512_have_neon,
	.transform = tf_sha512_neon,
	.name = "neon"
};
#endif

#elif defined(__PPC64__)
TF(zfs_sha512_ppc, tf_sha512_ppc);
const sha512_ops_t sha512_ppc_impl = {
	.is_supported = sha2_is_supported,
	.transform = tf_sha512_ppc,
	.name = "ppc"
};

static boolean_t sha512_have_isa207(void)
{
	return (kfpu_allowed() && zfs_isa207_available());
}

TF(zfs_sha512_power8, tf_sha512_power8);
const sha512_ops_t sha512_power8_impl = {
	.is_supported = sha512_have_isa207,
	.transform = tf_sha512_power8,
	.name = "power8"
};
#endif /* __PPC64__ */

/* the two generic ones */
extern const sha512_ops_t sha512_generic_impl;

/* array with all sha512 implementations */
static const sha512_ops_t *const sha512_impls[] = {
	&sha512_generic_impl,
#if defined(__x86_64)
	&sha512_x64_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX)
	&sha512_avx_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX2)
	&sha512_avx2_impl,
#endif
#if defined(__aarch64__) || defined(__arm__)
	&sha512_armv7_impl,
#if defined(__aarch64__)
	&sha512_armv8_impl,
#endif
#if defined(__arm__) && __ARM_ARCH > 6
	&sha512_neon_impl,
#endif
#endif
#if defined(__PPC64__)
	&sha512_ppc_impl,
	&sha512_power8_impl,
#endif /* __PPC64__ */
};

/* use the generic implementation functions */
#define	IMPL_NAME		"sha512"
#define	IMPL_OPS_T		sha512_ops_t
#define	IMPL_ARRAY		sha512_impls
#define	IMPL_GET_OPS		sha512_get_ops
#define	ZFS_IMPL_OPS		zfs_sha512_ops
#include <generic_impl.c>

#ifdef _KERNEL

#define	IMPL_FMT(impl, i)	(((impl) == (i)) ? "[%s] " : "%s ")

#if defined(__linux__)

static int
sha512_param_get(char *buffer, zfs_kernel_param_t *unused)
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
sha512_param_set(const char *val, zfs_kernel_param_t *unused)
{
	(void) unused;
	return (generic_impl_setname(val));
}

#elif defined(__FreeBSD__)

#include <sys/sbuf.h>

static int
sha512_param(ZFS_MODULE_PARAM_ARGS)
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

	/* we got module parameter */
	char buf[16];

	err = sysctl_handle_string(oidp, buf, sizeof (buf), req);
	if (err) {
		return (err);
	}

	return (-generic_impl_setname(buf));
}
#endif

#undef IMPL_FMT

ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs, zfs_, sha512_impl,
    sha512_param_set, sha512_param_get, ZMOD_RW, \
	"Select SHA512 implementation.");
#endif

#undef TF
