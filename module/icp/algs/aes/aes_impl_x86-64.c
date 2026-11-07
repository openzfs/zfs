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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(__x86_64)

#include <sys/simd.h>
#include <aes/aes_impl.h>

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
aes_x86_64_generate(aes_key_t *key, const uint32_t *keyarr32, int keybits)
{
	key->nr = rijndael_key_setup_enc_amd64(&(key->encr_ks.ks32[0]),
	    keyarr32, keybits);
	key->nr = rijndael_key_setup_dec_amd64(&(key->decr_ks.ks32[0]),
	    keyarr32, keybits);
}

/* Users of ASMABI requires all calls to be from wrappers */
static void
aes_x86_64_encrypt(const uint32_t rk[], int Nr, const uint32_t pt[4],
    uint32_t ct[4])
{
	aes_encrypt_amd64(rk, Nr, pt, ct);
}

static void
aes_x86_64_decrypt(const uint32_t rk[], int Nr, const uint32_t ct[4],
    uint32_t pt[4])
{
	aes_decrypt_amd64(rk, Nr, ct, pt);
}

static boolean_t
aes_x86_64_will_work(void)
{
	return (B_TRUE);
}

const aes_impl_ops_t aes_x86_64_impl = {
	.generate = &aes_x86_64_generate,
	.encrypt = &aes_x86_64_encrypt,
	.decrypt = &aes_x86_64_decrypt,
	.is_supported = &aes_x86_64_will_work,
	.needs_byteswap = B_FALSE,
	.name = "x86_64"
};

#endif /* defined(__x86_64) */
