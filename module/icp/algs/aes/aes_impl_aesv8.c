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

#include <aes/aes_impl.h>

/*
 * Copyright (c) 2023, Jorgen Lundman <lundman@lundman.net>
 */

#if defined(__aarch64__) && defined(HAVE_AESV8)

#include <sys/simd.h>
#include <sys/types.h>
#include <sys/asm_linkage.h>

/* These functions are used to execute AES-V8 instructions: */
#ifdef OPENSSL_INTERFACE
extern ASMABI int aes_v8_set_encrypt_key(const unsigned char *userKey,
    const int bits, AES_KEY *key);
extern ASMABI int aes_v8_set_decrypt_key(const unsigned char *userKey,
    const int bits, AES_KEY *key);
extern ASMABI void aes_v8_encrypt(const unsigned char *in,
    unsigned char *out, const AES_KEY *key, const unsigned int nround);
extern ASMABI void aes_v8_decrypt(const unsigned char *in,
    unsigned char *out, const AES_KEY *key, const unsigned int nround);
#endif

extern ASMABI int aes_v8_set_encrypt_key(const uint32_t rk[],
    uint64_t bits, const uint32_t cipherKey[]);
extern ASMABI int aes_v8_set_decrypt_key(const uint32_t rk[],
    uint64_t bits, const uint32_t cipherKey[]);
/* nround $10 (128), $12 (192), $14 (256) */
extern ASMABI void aes_v8_encrypt(const uint32_t pt[4],
    const uint32_t ct[4], const uint32_t rk[], const unsigned int nround);
extern ASMABI void aes_v8_decrypt(const uint32_t ct[4],
    const uint32_t pt[4], const uint32_t rk[], const unsigned int nround);

#define	AES_MAXNR 14
typedef struct aes_key_st {
	unsigned int	rd_key[4 *(AES_MAXNR + 1)];
	int		rounds;
	unsigned int	pad[3];
} AES_KEY;

/*
 * Expand the 32-bit AES cipher key array into the encryption and decryption
 * key schedules.
 *
 * Parameters:
 * key		AES key schedule to be initialized
 * keyarr32	User key
 * keyBits	AES key size (128, 192, or 256 bits)
 */
static void
aes_aesv8_generate(aes_key_t *key, const uint32_t *keyarr32, int keybits)
{
	kfpu_begin();
	key->nr = aes_v8_set_encrypt_key(keyarr32, keybits,
	    &(key->encr_ks.ks32[0]));
	key->nr = aes_v8_set_decrypt_key(keyarr32, keybits,
	    &(key->decr_ks.ks32[0]));
	kfpu_end();
}

/*
 * Encrypt one block of data. The block is assumed to be an array
 * of four uint32_t values, so copy for alignment (and byte-order
 * reversal for little endian systems might be necessary on the
 * input and output byte streams.
 * The size of the key schedule depends on the number of rounds
 * (which can be computed from the size of the key), i.e. 4*(Nr + 1).
 *
 * Parameters:
 * rk		Key schedule, of aes_ks_t (60 32-bit integers)
 * Nr		Number of rounds
 * pt		Input block (plain text)
 * ct		Output block (crypto text).  Can overlap with pt
 */
static void
aes_aesv8_encrypt(const uint32_t rk[], int Nr, const uint32_t pt[4],
    uint32_t ct[4])
{
	kfpu_begin();
	aes_v8_encrypt(pt, ct, rk, Nr);
	kfpu_end();
}

/*
 * Decrypt one block of data. The block is assumed to be an array
 * of four uint32_t values, so copy for alignment (and byte-order
 * reversal for little endian systems might be necessary on the
 * input and output byte streams.
 * The size of the key schedule depends on the number of rounds
 * (which can be computed from the size of the key), i.e. 4*(Nr + 1).
 *
 * Parameters:
 * rk		Key schedule, of aes_ks_t (60 32-bit integers)
 * Nr		Number of rounds
 * ct		Input block (crypto text)
 * pt		Output block (plain text). Can overlap with pt
 */
static void
aes_aesv8_decrypt(const uint32_t rk[], int Nr, const uint32_t ct[4],
    uint32_t pt[4])
{
	kfpu_begin();
	aes_v8_encrypt(ct, pt, rk, Nr);
	kfpu_end();
}

static boolean_t
aes_aesv8_will_work(void)
{
	/*
	 * so msr is a system register that returns 0 below
	 * EL1. But all APPLE M1 onwards support AES. We could
	 * fetch it from sysctl here for userland.
	 */
#ifndef _KERNEL
	return (B_TRUE);
#endif
	return (kfpu_allowed() && zfs_aesv8_available());
}

const aes_impl_ops_t aes_aesv8_impl = {
	.generate = &aes_aesv8_generate,
	.encrypt = &aes_aesv8_encrypt,
	.decrypt = &aes_aesv8_decrypt,
	.is_supported = &aes_aesv8_will_work,
	.needs_byteswap = B_FALSE,
	.name = "aesv8"
};

#endif /* defined(__aarch64__) && defined(HAVE_AESV8) */
