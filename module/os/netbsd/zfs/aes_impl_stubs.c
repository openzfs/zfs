// SPDX-License-Identifier: CDDL-1.0
/*
 * Unselected AES impl symbols so aes_impl.c links without AES-NI /
 * amd64 asm.  First cut delegates to the portable C generic impl.
 * SIMD AES is a later kfpu patch.
 */

#include <sys/zfs_context.h>
#include <aes/aes_impl.h>

#if defined(__x86_64)

extern const aes_impl_ops_t aes_generic_impl;

static boolean_t
aes_stub_supported(void)
{
	return (B_TRUE);
}

static void
aes_stub_generate(aes_key_t *key, const uint32_t *in, int nr)
{
	aes_generic_impl.generate(key, in, nr);
}

static void
aes_stub_encrypt(const uint32_t rk[], int nr, const uint32_t in[4],
    uint32_t out[4])
{
	aes_generic_impl.encrypt(rk, nr, in, out);
}

static void
aes_stub_decrypt(const uint32_t rk[], int nr, const uint32_t in[4],
    uint32_t out[4])
{
	aes_generic_impl.decrypt(rk, nr, in, out);
}

/*
 * aes_impl_init() on x86_64 copies this as "fastest" without
 * checking is_supported.  Delegate to generic so that path is
 * real AES, not a no-op.
 */
const aes_impl_ops_t aes_x86_64_impl = {
	.generate = aes_stub_generate,
	.encrypt = aes_stub_encrypt,
	.decrypt = aes_stub_decrypt,
	.is_supported = aes_stub_supported,
#if defined(_ZFS_LITTLE_ENDIAN)
	.needs_byteswap = B_TRUE,
#else
	.needs_byteswap = B_FALSE,
#endif
	.name = "x86_64"
};

#if defined(HAVE_SIMD_AES) || (defined(HAVE_TOOLCHAIN_AES) && \
    HAVE_TOOLCHAIN_AES)
/*
 * Do not advertise AES-NI until a real AES-NI impl is wired
 * (kfpu + aes_impl_aesni).  A stub that always returns B_TRUE
 * makes kstats/benchmarks lie and a later kfpu patch can silently
 * "enable AES-NI" that is still generic C.
 */
static boolean_t
aes_aesni_supported(void)
{
	return (B_FALSE);
}

const aes_impl_ops_t aes_aesni_impl = {
	.generate = aes_stub_generate,
	.encrypt = aes_stub_encrypt,
	.decrypt = aes_stub_decrypt,
	.is_supported = aes_aesni_supported,
#if defined(_ZFS_LITTLE_ENDIAN)
	.needs_byteswap = B_TRUE,
#else
	.needs_byteswap = B_FALSE,
#endif
	.name = "aesni"
};
#endif
#endif
