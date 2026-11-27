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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

static int
crypto_cipher(crypto_mechanism_t *mech, crypto_data_t *input,
    crypto_key_t *key, crypto_ctx_template_t template, crypto_data_t *output,
    boolean_t encrypt)
{
	crypto_mechanism_t provider_mech = *mech;
	crypto_ctx_template_impl_t *ctx_template = template;
	crypto_spi_ctx_template_t provider_template = NULL;
	icp_provider_t provider;
	int error;

	error = icp_mech_to_provider(mech->cm_type, &provider,
	    &provider_mech.cm_type);
	if (error != CRYPTO_SUCCESS)
		return (error);
	if (provider != ICP_PROVIDER_AES)
		return (CRYPTO_MECH_NOT_SUPPORTED);

	if (ctx_template != NULL)
		provider_template = ctx_template->ct_prov_tmpl;

	if (encrypt) {
		return (icp_aes_encrypt(&provider_mech, key, input, output,
		    provider_template));
	}

	return (icp_aes_decrypt(&provider_mech, key, input, output,
	    provider_template));
}

int
crypto_encrypt(crypto_mechanism_t *mech, crypto_data_t *plaintext,
    crypto_key_t *key, crypto_ctx_template_t template,
    crypto_data_t *ciphertext)
{
	return (crypto_cipher(mech, plaintext, key, template, ciphertext,
	    B_TRUE));
}

int
crypto_decrypt(crypto_mechanism_t *mech, crypto_data_t *ciphertext,
    crypto_key_t *key, crypto_ctx_template_t template,
    crypto_data_t *plaintext)
{
	return (crypto_cipher(mech, ciphertext, key, template, plaintext,
	    B_FALSE));
}

EXPORT_SYMBOL(crypto_encrypt);
EXPORT_SYMBOL(crypto_decrypt);
