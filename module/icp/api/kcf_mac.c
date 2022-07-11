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
 * Message authentication codes routines.
 */

/*
 * The following are the possible returned values common to all the routines
 * below. The applicability of some of these return values depends on the
 * presence of the arguments.
 *
 *	CRYPTO_SUCCESS:	The operation completed successfully.
 *	CRYPTO_INVALID_MECH_NUMBER, CRYPTO_INVALID_MECH_PARAM, or
 *	CRYPTO_INVALID_MECH for problems with the 'mech'.
 *	CRYPTO_INVALID_DATA for bogus 'data'
 *	CRYPTO_HOST_MEMORY for failure to allocate memory to handle this work.
 *	CRYPTO_INVALID_CONTEXT: Not a valid context.
 *	CRYPTO_BUSY:	Cannot process the request now. Try later.
 *	CRYPTO_NOT_SUPPORTED and CRYPTO_MECH_NOT_SUPPORTED: No provider is
 *			capable of a function or a mechanism.
 *	CRYPTO_INVALID_KEY: bogus 'key' argument.
 *	CRYPTO_INVALID_MAC: bogus 'mac' argument.
 */

/*
 * crypto_mac_prov()
 *
 * Arguments:
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	data:	The message to compute the MAC for.
 *	mac: Storage for the MAC. The length needed depends on the mechanism.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of a
 *		MAC with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs a
 *	single-part message authentication of 'data' with the mechanism
 *	'mech', using *	the key 'key', on the specified provider with
 *	the specified session id.
 *	When complete and successful, 'mac' will contain the message
 *	authentication code.
 *	Relies on the KCF scheduler to choose a provider.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *mac)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_MAC_ATOMIC)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	if (((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL))
		spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;

	crypto_mechanism_t lmech = *mech;
	KCF_SET_PROVIDER_MECHNUM(mech->cm_type, pd, &lmech);
	error = KCF_PROV_MAC_ATOMIC(pd, &lmech, key, data,
	    mac, spi_ctx_tmpl);

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
 * crypto_mac_init_prov()
 *
 * Arguments:
 *	pd:	pointer to the descriptor of the provider to use for this
 *		operation.
 *	mech:	crypto_mechanism_t pointer.
 *		mech_type is a valid value previously returned by
 *		crypto_mech2id();
 *		When the mech's parameter is not NULL, its definition depends
 *		on the standard definition of the mechanism.
 *	key:	pointer to a crypto_key_t structure.
 *	tmpl:	a crypto_ctx_template_t, opaque template of a context of a
 *		MAC with the 'mech' using 'key'. 'tmpl' is created by
 *		a previous call to crypto_create_ctx_template().
 *	ctxp:	Pointer to a crypto_context_t.
 *
 * Description:
 *	Asynchronously submits a request for, or synchronously performs the
 *	initialization of a MAC operation on the specified provider with
 *	the specified session.
 *	When possible and applicable, will internally use the pre-computed MAC
 *	context from the context template, tmpl.
 *	When complete and successful, 'ctxp' will contain a crypto_context_t
 *	valid for later calls to mac_update() and mac_final().
 *	The caller should hold a reference on the specified provider
 *	descriptor before calling this function.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
static int
crypto_mac_init_prov(kcf_provider_desc_t *pd,
    crypto_mechanism_t *mech, crypto_key_t *key, crypto_spi_ctx_template_t tmpl,
    crypto_context_t *ctxp)
{
	int rv;
	crypto_ctx_t *ctx;
	kcf_provider_desc_t *real_provider = pd;

	ASSERT(KCF_PROV_REFHELD(pd));

	/* Allocate and initialize the canonical context */
	if ((ctx = kcf_new_ctx(real_provider)) == NULL)
		return (CRYPTO_HOST_MEMORY);

	crypto_mechanism_t lmech = *mech;
	KCF_SET_PROVIDER_MECHNUM(mech->cm_type, real_provider, &lmech);
	rv = KCF_PROV_MAC_INIT(real_provider, ctx, &lmech, key, tmpl);

	if (rv == CRYPTO_SUCCESS)
		*ctxp = (crypto_context_t)ctx;
	else {
		/* Release the hold done in kcf_new_ctx(). */
		KCF_CONTEXT_REFRELE((kcf_context_t *)ctx->cc_framework_private);
	}

	return (rv);
}

/*
 * Same as crypto_mac_init_prov(), but relies on the KCF scheduler to
 * choose a provider. See crypto_mac_init_prov() comments for more
 * information.
 */
int
crypto_mac_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp)
{
	int error;
	kcf_mech_entry_t *me;
	kcf_provider_desc_t *pd;
	kcf_ctx_template_t *ctx_tmpl;
	crypto_spi_ctx_template_t spi_ctx_tmpl = NULL;
	kcf_prov_tried_t *list = NULL;

retry:
	/* The pd is returned held */
	if ((pd = kcf_get_mech_provider(mech->cm_type, &me, &error,
	    list, CRYPTO_FG_MAC)) == NULL) {
		if (list != NULL)
			kcf_free_triedlist(list);
		return (error);
	}

	/*
	 * Check the validity of the context template
	 * It is very rare that the generation number mis-matches, so
	 * is acceptable to fail here, and let the consumer recover by
	 * freeing this tmpl and create a new one for the key and new provider
	 */

	if (((ctx_tmpl = (kcf_ctx_template_t *)tmpl) != NULL))
		spi_ctx_tmpl = ctx_tmpl->ct_prov_tmpl;

	error = crypto_mac_init_prov(pd, mech, key,
	    spi_ctx_tmpl, ctxp);
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
 * crypto_mac_update()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by mac_init().
 *	data: The message part to be MAC'ed
 *
 * Description:
 *	Synchronously performs a part of a MAC operation.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac_update(crypto_context_t context, crypto_data_t *data)
{
	crypto_ctx_t *ctx = (crypto_ctx_t *)context;
	kcf_context_t *kcf_ctx;
	kcf_provider_desc_t *pd;

	if ((ctx == NULL) ||
	    ((kcf_ctx = (kcf_context_t *)ctx->cc_framework_private) == NULL) ||
	    ((pd = kcf_ctx->kc_prov_desc) == NULL)) {
		return (CRYPTO_INVALID_CONTEXT);
	}

	return (KCF_PROV_MAC_UPDATE(pd, ctx, data));
}

/*
 * crypto_mac_final()
 *
 * Arguments:
 *	context: A crypto_context_t initialized by mac_init().
 *	mac: Storage for the message authentication code.
 *
 * Description:
 *	Synchronously performs a part of a message authentication operation.
 *
 * Returns:
 *	See comment in the beginning of the file.
 */
int
crypto_mac_final(crypto_context_t context, crypto_data_t *mac)
{
	crypto_ctx_t *ctx = (crypto_ctx_t *)context;
	kcf_context_t *kcf_ctx;
	kcf_provider_desc_t *pd;

	if ((ctx == NULL) ||
	    ((kcf_ctx = (kcf_context_t *)ctx->cc_framework_private) == NULL) ||
	    ((pd = kcf_ctx->kc_prov_desc) == NULL)) {
		return (CRYPTO_INVALID_CONTEXT);
	}

	int rv = KCF_PROV_MAC_FINAL(pd, ctx, mac);

	/* Release the hold done in kcf_new_ctx() during init step. */
	KCF_CONTEXT_COND_RELEASE(rv, kcf_ctx);
	return (rv);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(crypto_mac);
EXPORT_SYMBOL(crypto_mac_init);
EXPORT_SYMBOL(crypto_mac_update);
EXPORT_SYMBOL(crypto_mac_final);
#endif
