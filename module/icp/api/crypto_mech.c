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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/sha2.h>
#include <aes/aes_impl.h>

#define	ICP_MECH(class, index)					\
	(((crypto_mech_type_t)(class) << 32) | (index))
#define	ICP_MECH_CLASS(type)	((uint32_t)((type) >> 32))
#define	ICP_MECH_INDEX(type)	((uint32_t)(type))

#define	ICP_CIPHER_CLASS	1
#define	ICP_MAC_CLASS		2
#define	ICP_MAX_CIPHER		32
#define	ICP_MAX_MAC		40

/* Preserve the existing mechanism IDs and valid ID ranges. */
typedef struct icp_mech {
	const char		*im_name;
	crypto_mech_type_t	im_framework_type;
	icp_provider_t		im_provider;
	crypto_mech_type_t	im_provider_type;
} icp_mech_t;

static const icp_mech_t icp_mechs[] = {
	{
		SUN_CKM_AES_CCM,
		ICP_MECH(ICP_CIPHER_CLASS, 0),
		ICP_PROVIDER_AES,
		AES_CCM_MECH_INFO_TYPE,
	},
	{
		SUN_CKM_AES_GCM,
		ICP_MECH(ICP_CIPHER_CLASS, 1),
		ICP_PROVIDER_AES,
		AES_GCM_MECH_INFO_TYPE,
	},
	{
		SUN_CKM_SHA512_HMAC,
		ICP_MECH(ICP_MAC_CLASS, 0),
		ICP_PROVIDER_SHA2,
		SHA512_HMAC_MECH_INFO_TYPE,
	},
};

static int
icp_mech_error(crypto_mech_type_t type)
{
	uint32_t class = ICP_MECH_CLASS(type);
	uint32_t index = ICP_MECH_INDEX(type);

	if ((class == ICP_CIPHER_CLASS && index < ICP_MAX_CIPHER) ||
	    (class == ICP_MAC_CLASS && index < ICP_MAX_MAC))
		return (CRYPTO_MECH_NOT_SUPPORTED);

	return (CRYPTO_MECHANISM_INVALID);
}

crypto_mech_type_t
crypto_mech2id(const char *name)
{
	if (!icp_is_ready())
		return (CRYPTO_MECH_INVALID);

	for (size_t i = 0; i < ARRAY_SIZE(icp_mechs); i++) {
		if (strcmp(name, icp_mechs[i].im_name) == 0)
			return (icp_mechs[i].im_framework_type);
	}

	return (CRYPTO_MECH_INVALID);
}

int
icp_mech_to_provider(crypto_mech_type_t type, icp_provider_t *provider,
    crypto_mech_type_t *provider_type)
{
	if (!icp_is_ready())
		return (icp_mech_error(type));

	for (size_t i = 0; i < ARRAY_SIZE(icp_mechs); i++) {
		if (type == icp_mechs[i].im_framework_type) {
			*provider = icp_mechs[i].im_provider;
			*provider_type = icp_mechs[i].im_provider_type;
			return (CRYPTO_SUCCESS);
		}
	}

	return (icp_mech_error(type));
}

EXPORT_SYMBOL(crypto_mech2id);
