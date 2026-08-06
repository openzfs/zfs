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
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zalgo.h>
#include <sys/crypto/api.h>
#include <sys/zfs_debug.h>

/* XXX zio_crypt.h */
#define	SHA512_HMAC_LEN		64
#define	SHA512_HMAC_KEYLEN	64

static int
zg_icp_hmac_sha512_init(void **ctxp, const uint8_t *key, size_t keylen)
{
	if (keylen != SHA512_HMAC_KEYLEN)
		return (EINVAL);

	crypto_key_t ck = {
		.ck_data = (void *)key,
		.ck_length = CRYPTO_BYTES2BITS(keylen),
	};

	crypto_mechanism_t mech = {0};
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);

	if (crypto_mac_init(&mech, &ck, NULL,
	    (crypto_context_t *)ctxp) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static int
zg_icp_hmac_sha512_update(void **ctxp, const uint8_t *msg, size_t msglen)
{
	crypto_data_t cdmsg = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = msglen,
		.cd_raw.iov_base = (char *)msg,
		.cd_raw.iov_len = msglen,
	};

	if (crypto_mac_update(
	    *(crypto_context_t *)ctxp, &cdmsg) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static int
zg_icp_hmac_sha512_final(void **ctxp, uint8_t *mac)
{
	crypto_data_t cdmac = {
	    .cd_format = CRYPTO_DATA_RAW,
	    .cd_offset = 0,
	    .cd_length = SHA512_HMAC_LEN,
	    .cd_raw.iov_base = (char *)mac,
	    .cd_raw.iov_len = SHA512_HMAC_LEN,
	};

	if (crypto_mac_final(
	    *(crypto_context_t *)ctxp, &cdmac) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static int
zg_icp_hmac_sha512_once(const uint8_t *key, size_t keylen,
    const uint8_t *msg, size_t msglen, uint8_t *mac)
{
	if (keylen != SHA512_HMAC_KEYLEN)
		return (EINVAL);

	crypto_key_t ck = {
		.ck_data = (void *)key,
		.ck_length = CRYPTO_BYTES2BITS(keylen),
	};

	crypto_mechanism_t mech = {0};
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);

	crypto_data_t cdmsg = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = msglen,
		.cd_raw.iov_base = (char *)msg,
		.cd_raw.iov_len = msglen,
	};

	crypto_data_t cdmac = {
	    .cd_format = CRYPTO_DATA_RAW,
	    .cd_offset = 0,
	    .cd_length = SHA512_HMAC_LEN,
	    .cd_raw.iov_base = (char *)mac,
	    .cd_raw.iov_len = SHA512_HMAC_LEN,
	};

	if (crypto_mac(&mech, &cdmsg, &ck, NULL, &cdmac) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static const zalgo_mac_ops_t zg_icp_hmac_sha512_ops = {
	.zgm_op_init = zg_icp_hmac_sha512_init,
	.zgm_op_update = zg_icp_hmac_sha512_update,
	.zgm_op_final = zg_icp_hmac_sha512_final,
	.zgm_op_once = zg_icp_hmac_sha512_once,
};

int
zalgo_shim_icp_register(void)
{
	int ret = 0, err;

	err = zalgo_mac_register(ZG_MAC_HMAC_SHA512, "icp", "ICP HMAC-SHA512",
	    &zg_icp_hmac_sha512_ops);
	if (err != 0 && ret == 0)
		ret = err;

	return (ret);
}
