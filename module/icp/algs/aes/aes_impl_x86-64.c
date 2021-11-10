/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

static boolean_t
aes_x86_64_will_work(void)
{
	return (B_TRUE);
}

const aes_impl_ops_t aes_x86_64_impl = {
	.generate = &aes_x86_64_generate,
	.encrypt = &aes_encrypt_amd64,
	.decrypt = &aes_decrypt_amd64,
	.is_supported = &aes_x86_64_will_work,
	.needs_byteswap = B_FALSE,
	.name = "x86_64"
};

#endif /* defined(__x86_64) */
