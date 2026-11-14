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
	kfpu_ctx_t kctx; kfpu_begin(&kctx); E(s, d, b); kfpu_end(&kctx); \
}

#if defined(__x86_64) || defined(__aarch64__) || defined(__arm__) || \
    defined(__PPC64__)
/* some implementation is always okay */
static inline boolean_t sha2_is_supported(void)
{
	return (B_TRUE);
}
#endif

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

#if HAVE_SIMD(SSSE3)
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

#if HAVE_SIMD(AVX)
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

#if HAVE_SIMD(AVX2)
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

#if HAVE_SIMD(SSE4_1)
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

#elif defined(__aarch64__) || defined(__arm__)
extern void zfs_sha256_block_armv7(uint32_t s[8], const void *, size_t);

static inline void
tf_sha256_armv7(uint32_t s[8], const void *d, size_t b)
{
	zfs_sha256_block_armv7(s, d, b);
}

const sha256_ops_t sha256_armv7_impl = {
	.is_supported = sha2_is_supported,
	.transform = tf_sha256_armv7,
	.name = "armv7"
};

#if __ARM_ARCH > 6
static boolean_t sha256_have_neon(void)
{
	return (kfpu_allowed() && zfs_neon_available());
}

static boolean_t sha256_have_armv8ce(void)
{
	return (kfpu_allowed() && zfs_sha256_available());
}

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
#endif

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
#if defined(__x86_64) && HAVE_SIMD(SSSE3)
	&sha256_ssse3_impl,
#endif
#if defined(__x86_64) && HAVE_SIMD(AVX)
	&sha256_avx_impl,
#endif
#if defined(__x86_64) && HAVE_SIMD(AVX2)
	&sha256_avx2_impl,
#endif
#if defined(__x86_64) && HAVE_SIMD(SSE4_1)
	&sha256_shani_impl,
#endif
#if defined(__aarch64__) || defined(__arm__)
	&sha256_armv7_impl,
#if __ARM_ARCH > 6
	&sha256_neon_impl,
	&sha256_armv8_impl,
#endif
#endif
#if defined(__PPC64__)
	&sha256_ppc_impl,
	&sha256_power8_impl,
#endif /* __PPC64__ */
};

/*
 * Known answers. "abc" and the 448-bit message are the examples in NIST's
 * SHA256.pdf (csrc.nist.gov, Cryptographic Standards and Guidelines,
 * Examples with Intermediate Values). The 262-byte message is the Len = 2096
 * entry of SHA256LongMsg.rsp in the NIST CAVP shabytetestvectors.zip; it
 * hands the transform four blocks at once, as the SIMD implementations
 * process several blocks together.
 */
static const uint8_t sha256_selftest_long[] = {
	0x6b, 0x91, 0x8f, 0xb1, 0xa5, 0xad, 0x1f, 0x9c,
	0x5e, 0x5d, 0xbd, 0xf1, 0x0a, 0x93, 0xa9, 0xc8,
	0xf6, 0xbc, 0xa8, 0x9f, 0x37, 0xe7, 0x9c, 0x9f,
	0xe1, 0x2a, 0x57, 0x22, 0x79, 0x41, 0xb1, 0x73,
	0xac, 0x79, 0xd8, 0xd4, 0x40, 0xcd, 0xe8, 0xc6,
	0x4c, 0x4e, 0xbc, 0x84, 0xa4, 0xc8, 0x03, 0xd1,
	0x98, 0xa2, 0x96, 0xf3, 0xde, 0x06, 0x09, 0x00,
	0xcc, 0x42, 0x7f, 0x58, 0xca, 0x6e, 0xc3, 0x73,
	0x08, 0x4f, 0x95, 0xdd, 0x6c, 0x7c, 0x42, 0x7e,
	0xcf, 0xbf, 0x78, 0x1f, 0x68, 0xbe, 0x57, 0x2a,
	0x88, 0xdb, 0xcb, 0xb1, 0x88, 0x58, 0x1a, 0xb2,
	0x00, 0xbf, 0xb9, 0x9a, 0x3a, 0x81, 0x64, 0x07,
	0xe7, 0xdd, 0x6d, 0xd2, 0x10, 0x03, 0x55, 0x4d,
	0x4f, 0x7a, 0x99, 0xc9, 0x3e, 0xbf, 0xce, 0x5c,
	0x30, 0x2f, 0xf0, 0xe1, 0x1f, 0x26, 0xf8, 0x3f,
	0xe6, 0x69, 0xac, 0xef, 0xb0, 0xc1, 0xbb, 0xb8,
	0xb1, 0xe9, 0x09, 0xbd, 0x14, 0xaa, 0x48, 0xba,
	0x34, 0x45, 0xc8, 0x8b, 0x0e, 0x11, 0x90, 0xee,
	0xf7, 0x65, 0xad, 0x89, 0x8a, 0xb8, 0xca, 0x2f,
	0xe5, 0x07, 0x01, 0x5f, 0x15, 0x78, 0xf1, 0x0d,
	0xce, 0x3c, 0x11, 0xa5, 0x5f, 0xb9, 0x43, 0x4e,
	0xe6, 0xe9, 0xad, 0x6c, 0xc0, 0xfd, 0xc4, 0x68,
	0x44, 0x47, 0xa9, 0xb3, 0xb1, 0x56, 0xb9, 0x08,
	0x64, 0x63, 0x60, 0xf2, 0x4f, 0xec, 0x2d, 0x8f,
	0xa6, 0x9e, 0x2c, 0x93, 0xdb, 0x78, 0x70, 0x8f,
	0xcd, 0x2e, 0xef, 0x74, 0x3d, 0xcb, 0x93, 0x53,
	0x81, 0x9b, 0x8d, 0x66, 0x7c, 0x48, 0xed, 0x54,
	0xcd, 0x43, 0x6f, 0xb1, 0x47, 0x65, 0x98, 0xc4,
	0xa1, 0xd7, 0x02, 0x8e, 0x6f, 0x2f, 0xf5, 0x07,
	0x51, 0xdb, 0x36, 0xab, 0x6b, 0xc3, 0x24, 0x35,
	0x15, 0x2a, 0x00, 0xab, 0xd3, 0xd5, 0x8d, 0x9a,
	0x87, 0x70, 0xd9, 0xa3, 0xe5, 0x2d, 0x5a, 0x36,
	0x28, 0xae, 0x3c, 0x9e, 0x03, 0x25
};

static const uint8_t sha256_selftest_digests[][32] = {
	{
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
		0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
		0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
	},
	{
		0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
		0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
		0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
		0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
	},
	{
		0x46, 0x50, 0x0b, 0x6a, 0xe1, 0xab, 0x40, 0xbd,
		0xe0, 0x97, 0xef, 0x16, 0x8b, 0x0f, 0x31, 0x99,
		0x04, 0x9b, 0x55, 0x54, 0x5a, 0x15, 0x88, 0x79,
		0x2d, 0x39, 0xd5, 0x94, 0xf4, 0x93, 0xdc, 0xa7
	}
};

boolean_t
sha256_impl_selftest(const sha256_ops_t *ops)
{
	const struct {
		const void *msg;
		size_t len;
	} msgs[] = {
		{ SELFTEST_MSG_ABC, sizeof (SELFTEST_MSG_ABC) - 1 },
		{ SELFTEST_MSG_448, sizeof (SELFTEST_MSG_448) - 1 },
		{ sha256_selftest_long, sizeof (sha256_selftest_long) }
	};
	uint8_t digest[32];
	SHA2_CTX ctx;
	boolean_t ok = B_TRUE;

	/* Each message in one update, then split across several. */
	for (int split = 0; split <= 1; split++) {
		for (int i = 0; i < ARRAY_SIZE(msgs); i++) {
			SHA2Init(SHA256, &ctx);
			ctx.sha256.ops = ops;
			if (split) {
				sha2_selftest_update_split(&ctx, msgs[i].msg,
				    msgs[i].len);
			} else {
				SHA2Update(&ctx, msgs[i].msg, msgs[i].len);
			}
			SHA2Final(digest, &ctx);
			if (memcmp(digest, sha256_selftest_digests[i],
			    sizeof (digest)) != 0)
				ok = B_FALSE;
		}
	}
	return (ok);
}

/* use the generic implementation functions */
#define	IMPL_NAME		"sha256"
#define	IMPL_SELFTEST		sha256_impl_selftest
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
