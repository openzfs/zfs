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
crypto_mac_mech(crypto_mechanism_t *mech, crypto_mechanism_t *provider_mech)
{
	icp_provider_t provider;
	int error;

	*provider_mech = *mech;
	error = icp_mech_to_provider(mech->cm_type, &provider,
	    &provider_mech->cm_type);
	if (error != CRYPTO_SUCCESS)
		return (error);
	if (provider != ICP_PROVIDER_SHA2)
		return (CRYPTO_MECH_NOT_SUPPORTED);

	return (CRYPTO_SUCCESS);
}

static crypto_spi_ctx_template_t
crypto_mac_template(crypto_ctx_template_t template)
{
	crypto_ctx_template_impl_t *ctx_template = template;

	return (ctx_template == NULL ? NULL : ctx_template->ct_prov_tmpl);
}

int
crypto_mac(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t template, crypto_data_t *mac)
{
	crypto_mechanism_t provider_mech;
	int error = crypto_mac_mech(mech, &provider_mech);

	if (error != CRYPTO_SUCCESS)
		return (error);

	return (icp_sha2_mac_atomic(&provider_mech, key, data, mac,
	    crypto_mac_template(template)));
}

int
crypto_mac_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t template, crypto_context_t *ctxp)
{
	crypto_mechanism_t provider_mech;
	int error;

	error = crypto_mac_mech(mech, &provider_mech);
	if (error != CRYPTO_SUCCESS)
		return (error);

	return (icp_sha2_mac_init(&provider_mech, key,
	    crypto_mac_template(template), ctxp));
}

int
crypto_mac_update(crypto_context_t context, crypto_data_t *data)
{
	if (context == NULL)
		return (CRYPTO_INVALID_CONTEXT);

	return (icp_sha2_mac_update(context, data));
}

int
crypto_mac_final(crypto_context_t context, crypto_data_t *mac)
{
	if (context == NULL)
		return (CRYPTO_INVALID_CONTEXT);

	return (icp_sha2_mac_final(context, mac));
}

EXPORT_SYMBOL(crypto_mac);
EXPORT_SYMBOL(crypto_mac_init);
EXPORT_SYMBOL(crypto_mac_update);
EXPORT_SYMBOL(crypto_mac_final);
