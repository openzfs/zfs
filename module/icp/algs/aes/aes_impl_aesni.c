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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(__x86_64) && defined(HAVE_AES)

#include <sys/simd.h>
#include <sys/types.h>
#include <sys/asm_linkage.h>

/* These functions are used to execute AES-NI instructions: */
extern ASMABI int rijndael_key_setup_enc_intel(uint32_t rk[],
	const uint32_t cipherKey[], uint64_t keyBits);
extern ASMABI int rijndael_key_setup_dec_intel(uint32_t rk[],
	const uint32_t cipherKey[], uint64_t keyBits);
extern ASMABI void aes_encrypt_intel(const uint32_t rk[], int Nr,
	const uint32_t pt[4], uint32_t ct[4]);
extern ASMABI void aes_decrypt_intel(const uint32_t rk[], int Nr,
	const uint32_t ct[4], uint32_t pt[4]);


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
aes_aesni_generate(aes_key_t *key, const uint32_t *keyarr32, int keybits)
{
	kfpu_begin();
	key->nr = rijndael_key_setup_enc_intel(&(key->encr_ks.ks32[0]),
	    keyarr32, keybits);
	key->nr = rijndael_key_setup_dec_intel(&(key->decr_ks.ks32[0]),
	    keyarr32, keybits);
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
aes_aesni_encrypt(const uint32_t rk[], int Nr, const uint32_t pt[4],
    uint32_t ct[4])
{
	kfpu_begin();
	aes_encrypt_intel(rk, Nr, pt, ct);
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
aes_aesni_decrypt(const uint32_t rk[], int Nr, const uint32_t ct[4],
    uint32_t pt[4])
{
	kfpu_begin();
	aes_decrypt_intel(rk, Nr, ct, pt);
	kfpu_end();
}

static boolean_t
aes_aesni_will_work(void)
{
	return (kfpu_allowed() && zfs_aes_available());
}

const aes_impl_ops_t aes_aesni_impl = {
	.generate = &aes_aesni_generate,
	.encrypt = &aes_aesni_encrypt,
	.decrypt = &aes_aesni_decrypt,
	.is_supported = &aes_aesni_will_work,
	.needs_byteswap = B_FALSE,
	.name = "aesni"
};

#endif /* defined(__x86_64) && defined(HAVE_AES) */
