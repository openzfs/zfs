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
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/sched_impl.h>

/*
 * Encryption and decryption routines.
 */


/*
 * crypto_encrypt()
 *
 * Arguments:
 *	sid:	session id
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	plaintext: The message to be encrypted
 *	ciphertext: Storage for the encrypted message. The length needed
 *		depends on the mechanism, and the plaintext's size.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of an
 *		encryption with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	single-part encryption of 'plaintext' with the mechanism 'mech', using
 *	the key 'key'.
 *	When complete and successful, 'ciphertext' will contain the encrypted
 *	message.
 *	Relies on the KCF scheduler to pick a provider.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_encrypt(crypto_mechanism_t *mech, crypto_data_t *plaintext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *ciphertext)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_ENCRYPT_ATOMIC)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	if (((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL))
		spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;

	crypto_mechanism_t lmech = *mech;
	KCF_SET_PROVIDER_MECHNUM(mech->cm_type, pd, &lmech);
	error = KCF_PROV_ENCRYPT_ATOMIC(pd, &lmech, key,
	    plaintext, ciphertext, spi_ctx_tmpl);

	if (error != CRYPTO_SUCCESS && IS_RECOVERABLE(error)) {
		/* Add pd to the linked list of providers tried. */
		if (kcf_insert_triedlist(&list, pd, KM_SLEEP) != NULL)
			goto retry;
	}

	if (list != NULL)
		kcf_free_triedlist(list);

	KCF_PROV_REFRELE(pd);
	return (error);
}

/*
 * crypto_decrypt_prov()
 *
 * Arguments:
 *	pd:	provider descriptor
 *	sid:	session id
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	ciphertext: The message to be encrypted
 *	plaintext: Storage for the encrypted message. The length needed
 *		depends on the mechanism, and the plaintext's size.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of an
 *		encryption with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	single-part decryption of 'ciphertext' with the mechanism 'mech', using
 *	the key 'key'.
 *	When complete and successful, 'plaintext' will contain the decrypted
 *	message.
 *	Relies on the KCF scheduler to choose a provider.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_decrypt(crypto_mechanism_t *mech, crypto_data_t *ciphertext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *plaintext)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_DECRYPT_ATOMIC)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	if (((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL))
		spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;

	crypto_mechanism_t lmech = *mech;
	KCF_SET_PROVIDER_MECHNUM(mech->cm_type, pd, &lmech);

	error = KCF_PROV_DECRYPT_ATOMIC(pd, &lmech, key,
	    ciphertext, plaintext, spi_ctx_tmpl);

	if (error != CRYPTO_SUCCESS && IS_RECOVERABLE(error)) {
		/* Add pd to the linked list of providers tried. */
		if (kcf_insert_triedlist(&list, pd, KM_SLEEP) != NULL)
			goto retry;
	}

	if (list != NULL)
		kcf_free_triedlist(list);

	KCF_PROV_REFRELE(pd);
	return (error);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(crypto_encrypt);
EXPORT_SYMBOL(crypto_decrypt);
#endif
