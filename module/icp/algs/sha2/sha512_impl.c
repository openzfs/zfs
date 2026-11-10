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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 * Copyright (c) 2026, TrueNAS.
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
	kfpu_ctx_t kctx; kfpu_begin(&kctx); E(s, d, b); kfpu_end(&kctx); \
}

#if defined(__x86_64) || defined(__aarch64__) || defined(__arm__) || \
    defined(__aarch64__) || defined(__arm__) || defined(__PPC64__)
/* some implementation is always okay */
static inline boolean_t sha2_is_supported(void)
{
	return (B_TRUE);
}
#endif

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

#if HAVE_SIMD(AVX)
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

#if HAVE_SIMD(AVX2)
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

#if HAVE_SIMD(SHA512EXT)
static boolean_t sha2_have_sha512ext(void)
{
	return (kfpu_allowed() && zfs_sha512ext_available());
}

TF(zfs_sha512_transform_sha512ext, tf_sha512_sha512ext);
const sha512_ops_t sha512_sha512ext_impl = {
	.is_supported = sha2_have_sha512ext,
	.transform = tf_sha512_sha512ext,
	.name = "sha512ext"
};
#endif

#elif defined(__aarch64__) || defined(__arm__)
extern void zfs_sha512_block_armv7(uint64_t s[8], const void *, size_t);

static inline void
tf_sha512_armv7(uint64_t s[8], const void *d, size_t b)
{
	zfs_sha512_block_armv7(s, d, b);
}

const sha512_ops_t sha512_armv7_impl = {
	.is_supported = sha2_is_supported,
	.transform = tf_sha512_armv7,
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
#if defined(__x86_64) && HAVE_SIMD(AVX)
	&sha512_avx_impl,
#endif
#if defined(__x86_64) && HAVE_SIMD(AVX2)
	&sha512_avx2_impl,
#endif
#if defined(__x86_64) && HAVE_SIMD(SHA512EXT)
	&sha512_sha512ext_impl,
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

/*
 * Known answers for SHA-512 and SHA-512/256, which share the transform. "abc"
 * and the 896-bit message are the examples in NIST's SHA512.pdf and
 * SHA512_256.pdf (csrc.nist.gov, Cryptographic Standards and Guidelines,
 * Examples with Intermediate Values). The 326-byte message is the Len = 2608
 * entry of SHA512LongMsg.rsp in the NIST CAVP shabytetestvectors.zip; it
 * hands the transform two blocks at once, as the SIMD implementations
 * process several blocks together.
 */
static const uint8_t sha512_selftest_long[] = {
	0xd5, 0xe3, 0x78, 0xae, 0x9f, 0xc2, 0x64, 0x8f,
	0x4a, 0x13, 0xbb, 0xec, 0x4b, 0x09, 0x35, 0xaf,
	0xb4, 0xf8, 0x22, 0xf5, 0xfe, 0x0d, 0x50, 0x63,
	0x05, 0x3d, 0x2f, 0xbd, 0x54, 0x7b, 0x33, 0xb4,
	0xa3, 0x2e, 0x7a, 0x00, 0x9e, 0xe2, 0xaf, 0xaf,
	0xe8, 0x3d, 0x2e, 0xbd, 0x60, 0x35, 0x68, 0xe4,
	0xa3, 0x81, 0x89, 0xb5, 0xd2, 0x4d, 0x59, 0xe8,
	0x95, 0x32, 0x60, 0xf1, 0x5f, 0x65, 0x4e, 0xd4,
	0xf4, 0x2f, 0x9a, 0x39, 0x29, 0x9d, 0x68, 0xc3,
	0xeb, 0x78, 0xb0, 0x9e, 0x83, 0x77, 0x9d, 0x57,
	0x18, 0xb4, 0x33, 0xf1, 0x76, 0x5d, 0x35, 0x35,
	0x0e, 0xac, 0x46, 0x49, 0x3d, 0x19, 0x4e, 0x84,
	0xd1, 0xce, 0x1f, 0x81, 0xc9, 0x5b, 0x59, 0x72,
	0x5c, 0xab, 0x8a, 0xb7, 0x3d, 0x36, 0x9a, 0xb0,
	0x1e, 0x79, 0x67, 0xcf, 0x73, 0xa3, 0xac, 0xf1,
	0x78, 0x92, 0x27, 0xee, 0x75, 0xfd, 0xfb, 0x6e,
	0x40, 0xf3, 0x53, 0xff, 0x04, 0x84, 0x48, 0x65,
	0x42, 0xbe, 0x05, 0x31, 0x15, 0xdb, 0x28, 0x96,
	0xba, 0xb8, 0x6c, 0x77, 0x4f, 0x89, 0x85, 0xc4,
	0xdb, 0xcc, 0x4c, 0x07, 0x8f, 0x7b, 0x1c, 0x3a,
	0x4c, 0x86, 0x7c, 0xdc, 0x65, 0x80, 0xfe, 0x44,
	0xa5, 0x98, 0x67, 0x34, 0x94, 0xcc, 0x0f, 0xb1,
	0xf6, 0x59, 0x8b, 0x12, 0x95, 0x76, 0x8a, 0x58,
	0x40, 0x41, 0xfd, 0xbd, 0x14, 0xfa, 0x7b, 0x90,
	0xfa, 0x6f, 0xe3, 0x3f, 0x71, 0xb7, 0x43, 0xb6,
	0x8e, 0x23, 0xf8, 0xe7, 0x40, 0x72, 0x17, 0xaa,
	0xd9, 0x44, 0x0c, 0xc8, 0xca, 0xd2, 0x81, 0x52,
	0xae, 0xdb, 0x82, 0x38, 0x8b, 0xe2, 0xde, 0x16,
	0x54, 0x96, 0xd0, 0x51, 0xb2, 0x92, 0xde, 0x63,
	0x03, 0x46, 0x02, 0x73, 0xa4, 0x35, 0x08, 0x29,
	0x6b, 0x62, 0x37, 0xc0, 0x78, 0x04, 0x33, 0x5d,
	0x2e, 0x81, 0x22, 0x9f, 0x7c, 0x9a, 0x0e, 0x77,
	0x61, 0xe3, 0x8a, 0x3a, 0xaf, 0x77, 0x99, 0xf4,
	0x0f, 0xe9, 0xcb, 0x00, 0x45, 0x7e, 0xa9, 0xd5,
	0xb5, 0x99, 0x53, 0x23, 0x26, 0x76, 0x68, 0x1f,
	0xc7, 0x1b, 0x26, 0x1a, 0x6f, 0x8c, 0xd3, 0x59,
	0x29, 0x3f, 0x5b, 0x21, 0xf0, 0xcf, 0x3a, 0x11,
	0xb7, 0xf4, 0x9c, 0xb5, 0xad, 0xb3, 0xc3, 0x57,
	0xbe, 0xd2, 0xaa, 0x18, 0x5d, 0x8f, 0xe8, 0x40,
	0x81, 0x92, 0xd6, 0xd3, 0xed, 0x1f, 0xf4, 0x65,
	0xb5, 0x90, 0x89, 0x2e, 0xfe, 0x03
};

static const uint8_t sha512_selftest_digests[][64] = {
	{
		0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
		0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
		0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
		0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
		0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
		0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
		0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
		0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
	},
	{
		0x8e, 0x95, 0x9b, 0x75, 0xda, 0xe3, 0x13, 0xda,
		0x8c, 0xf4, 0xf7, 0x28, 0x14, 0xfc, 0x14, 0x3f,
		0x8f, 0x77, 0x79, 0xc6, 0xeb, 0x9f, 0x7f, 0xa1,
		0x72, 0x99, 0xae, 0xad, 0xb6, 0x88, 0x90, 0x18,
		0x50, 0x1d, 0x28, 0x9e, 0x49, 0x00, 0xf7, 0xe4,
		0x33, 0x1b, 0x99, 0xde, 0xc4, 0xb5, 0x43, 0x3a,
		0xc7, 0xd3, 0x29, 0xee, 0xb6, 0xdd, 0x26, 0x54,
		0x5e, 0x96, 0xe5, 0x5b, 0x87, 0x4b, 0xe9, 0x09
	},
	{
		0xa7, 0x0c, 0x75, 0xb9, 0xb1, 0xf0, 0xac, 0x2e,
		0xd2, 0xc2, 0x79, 0x77, 0x63, 0xac, 0x9a, 0x66,
		0x01, 0xd9, 0x5f, 0x46, 0x88, 0x9b, 0x00, 0xfc,
		0x3d, 0xda, 0xe4, 0xd0, 0xac, 0x69, 0x23, 0x75,
		0x0a, 0x10, 0x8d, 0x79, 0xeb, 0x76, 0x4e, 0x77,
		0xac, 0x07, 0xb7, 0xcb, 0x5c, 0x01, 0xcb, 0x4b,
		0x37, 0x47, 0xdc, 0xf6, 0x9b, 0xa3, 0xb3, 0x5c,
		0x51, 0xfb, 0x99, 0x5d, 0xa2, 0x63, 0x2e, 0x70
	}
};

static const uint8_t sha512_256_selftest_digests[][32] = {
	{
		0x53, 0x04, 0x8e, 0x26, 0x81, 0x94, 0x1e, 0xf9,
		0x9b, 0x2e, 0x29, 0xb7, 0x6b, 0x4c, 0x7d, 0xab,
		0xe4, 0xc2, 0xd0, 0xc6, 0x34, 0xfc, 0x6d, 0x46,
		0xe0, 0xe2, 0xf1, 0x31, 0x07, 0xe7, 0xaf, 0x23
	},
	{
		0x39, 0x28, 0xe1, 0x84, 0xfb, 0x86, 0x90, 0xf8,
		0x40, 0xda, 0x39, 0x88, 0x12, 0x1d, 0x31, 0xbe,
		0x65, 0xcb, 0x9d, 0x3e, 0xf8, 0x3e, 0xe6, 0x14,
		0x6f, 0xea, 0xc8, 0x61, 0xe1, 0x9b, 0x56, 0x3a
	}
};

/* Hash each message with ops, and compare with digests, each len long. */
static boolean_t
sha512_selftest_one(const sha512_ops_t *ops, int algotype, int nmsgs,
    const uint8_t *digests, size_t len)
{
	const struct {
		const void *msg;
		size_t len;
	} msgs[] = {
		{ SELFTEST_MSG_ABC, sizeof (SELFTEST_MSG_ABC) - 1 },
		{ SELFTEST_MSG_896, sizeof (SELFTEST_MSG_896) - 1 },
		{ sha512_selftest_long, sizeof (sha512_selftest_long) }
	};
	uint8_t digest[64];
	SHA2_CTX ctx;
	boolean_t ok = B_TRUE;

	ASSERT3S(nmsgs, <=, ARRAY_SIZE(msgs));
	/* Each message in one update, then split across several. */
	for (int split = 0; split <= 1; split++) {
		for (int i = 0; i < nmsgs; i++) {
			SHA2Init(algotype, &ctx);
			ctx.sha512.ops = ops;
			if (split) {
				sha2_selftest_update_split(&ctx, msgs[i].msg,
				    msgs[i].len);
			} else {
				SHA2Update(&ctx, msgs[i].msg, msgs[i].len);
			}
			SHA2Final(digest, &ctx);
			if (memcmp(digest, digests + i * len, len) != 0)
				ok = B_FALSE;
		}
	}
	return (ok);
}

boolean_t
sha512_impl_selftest(const sha512_ops_t *ops)
{
	boolean_t ok;

	ok = sha512_selftest_one(ops, SHA512,
	    ARRAY_SIZE(sha512_selftest_digests),
	    &sha512_selftest_digests[0][0], 64);
	if (!sha512_selftest_one(ops, SHA512_256,
	    ARRAY_SIZE(sha512_256_selftest_digests),
	    &sha512_256_selftest_digests[0][0], 32))
		ok = B_FALSE;
	return (ok);
}

/* use the generic implementation functions */
#define	IMPL_NAME		"sha512"
#define	IMPL_SELFTEST		sha512_impl_selftest
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
