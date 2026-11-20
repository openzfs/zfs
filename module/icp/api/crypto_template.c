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

int
crypto_create_ctx_template(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t *template)
{
	crypto_ctx_template_impl_t *ctx_template;
	crypto_mechanism_t provider_mech;
	icp_provider_t provider;
	int error;

	if (template == NULL)
		return (CRYPTO_ARGUMENTS_BAD);
	if (mech == NULL)
		return (CRYPTO_MECHANISM_INVALID);

	provider_mech = *mech;
	error = icp_mech_to_provider(mech->cm_type, &provider,
	    &provider_mech.cm_type);
	if (error != CRYPTO_SUCCESS)
		return (error);

	ctx_template = kmem_alloc(sizeof (*ctx_template), KM_SLEEP);

	switch (provider) {
	case ICP_PROVIDER_AES:
		error = icp_aes_create_ctx_template(&provider_mech, key,
		    &ctx_template->ct_prov_tmpl, &ctx_template->ct_size);
		break;
	case ICP_PROVIDER_SHA2:
		error = icp_sha2_create_ctx_template(&provider_mech, key,
		    &ctx_template->ct_prov_tmpl, &ctx_template->ct_size);
		break;
	default:
		__builtin_unreachable();
	}

	if (error == CRYPTO_SUCCESS)
		*template = ctx_template;
	else
		kmem_free(ctx_template, sizeof (*ctx_template));

	return (error);
}

void
crypto_destroy_ctx_template(crypto_ctx_template_t template)
{
	crypto_ctx_template_impl_t *ctx_template = template;

	if (ctx_template == NULL)
		return;

	ASSERT3P(ctx_template->ct_prov_tmpl, !=, NULL);
	memset(ctx_template->ct_prov_tmpl, 0, ctx_template->ct_size);
	kmem_free(ctx_template->ct_prov_tmpl, ctx_template->ct_size);
	memset(ctx_template, 0, sizeof (*ctx_template));
	kmem_free(ctx_template, sizeof (*ctx_template));
}

EXPORT_SYMBOL(crypto_create_ctx_template);
EXPORT_SYMBOL(crypto_destroy_ctx_template);
