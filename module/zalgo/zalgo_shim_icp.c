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

/* XXX zio.h */
#define	ZIO_DATA_IV_LEN		12
#define	ZIO_DATA_MAC_LEN	16

static int
zg_icp_aes_open(void **ctx, const uint8_t *key, size_t keylen,
    const char *mechname)
{
	crypto_key_t ck = {
		.ck_data = (void *)key,
		.ck_length = CRYPTO_BYTES2BITS(keylen),
	};

	crypto_mechanism_t mech = {0};
	mech.cm_type = crypto_mech2id(mechname);

	if (crypto_create_ctx_template(&mech, &ck,
	    (crypto_ctx_template_t *)ctx) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static void
zg_icp_aes_close(void **ctx)
{
	crypto_destroy_ctx_template(*(crypto_ctx_template_t *)ctx);
}

static int
zg_icp_aes_encrypt(void **ctx, crypto_mechanism_t *mech,
    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
    uint8_t *mac)
{
	crypto_data_t cdplain = {
	    .cd_format = CRYPTO_DATA_RAW,
	    .cd_offset = 0,
	    .cd_length = textlen,
	    .cd_raw.iov_base = (char *)plaintext,
	    .cd_raw.iov_len = textlen,
	};

	/* XXX tedious mismatch in zfs_uio_t init -- robn, 2026-08-13 */
	iovec_t ciov[2];
	zfs_uio_t cuio = {0};
#if defined(__FreeBSD__) && defined(_KERNEL)
	struct uio uio_s;
	zfs_uio_init(&cuio, &uio_s);
#endif
	zfs_uio_iov(&cuio) = ciov;
	zfs_uio_iovcnt(&cuio) = 2;
	zfs_uio_segflg(&cuio) = UIO_SYSSPACE;

	ciov[0].iov_base = ciphertext;
	ciov[0].iov_len = textlen;
	ciov[1].iov_base = mac;
	ciov[1].iov_len = ZIO_DATA_MAC_LEN;

	crypto_data_t cdcipher = {
	    .cd_format = CRYPTO_DATA_UIO,
	    .cd_offset = 0,
	    .cd_length = textlen + ZIO_DATA_MAC_LEN,
	    .cd_uio = &cuio,
	};

	if (crypto_encrypt(mech, &cdplain, NULL,
	    *(crypto_ctx_template_t *)ctx, &cdcipher) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

static int
zg_icp_aes_decrypt(void **ctx, crypto_mechanism_t *mech,
    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
    const uint8_t *mac)
{
	iovec_t ciov[2];
	zfs_uio_t cuio = {0};
#if defined(__FreeBSD__) && defined(_KERNEL)
	struct uio uio_s;
	zfs_uio_init(&cuio, &uio_s);
#endif
	zfs_uio_iov(&cuio) = ciov;
	zfs_uio_iovcnt(&cuio) = 2;
	zfs_uio_segflg(&cuio) = UIO_SYSSPACE;

	ciov[0].iov_base = (char *)ciphertext;
	ciov[0].iov_len = textlen;
	ciov[1].iov_base = (char *)mac;
	ciov[1].iov_len = ZIO_DATA_MAC_LEN;

	crypto_data_t cdcipher = {
	    .cd_format = CRYPTO_DATA_UIO,
	    .cd_offset = 0,
	    .cd_length = textlen + ZIO_DATA_MAC_LEN,
	    .cd_uio = &cuio,
	};

	crypto_data_t cdplain = {
	    .cd_format = CRYPTO_DATA_RAW,
	    .cd_offset = 0,
	    .cd_length = textlen + ZIO_DATA_MAC_LEN,
	    .cd_raw.iov_base = plaintext,
	    .cd_raw.iov_len = textlen,
	};

	int err = crypto_decrypt(mech, &cdcipher, NULL,
	    *(crypto_ctx_template_t *)ctx, &cdplain);
	if (err != CRYPTO_SUCCESS) {
		if (err == CRYPTO_INVALID_MAC)
			return (SET_ERROR(ECKSUM));
		return (SET_ERROR(EIO));
	}

	return (0);
}

static int
zg_icp_aes_ccm_encrypt(void **ctx,
    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    uint8_t *mac)
{
	CK_AES_CCM_PARAMS ccm = {
		.nonce = (uchar_t *)iv,
		.ulNonceSize = ZIO_DATA_IV_LEN,
		.authData = (uchar_t *)ad,
		.ulAuthDataSize = adlen,
		.ulDataSize = textlen,
		.ulMACSize = ZIO_DATA_MAC_LEN,
	};

	crypto_mechanism_t mech = {
	    .cm_type = crypto_mech2id(SUN_CKM_AES_CCM),
	    .cm_param = (caddr_t)&ccm,
	    .cm_param_len = sizeof (CK_AES_CCM_PARAMS),
	};

	return (zg_icp_aes_encrypt(ctx, &mech, plaintext, ciphertext, textlen,
	    mac));
}

static int
zg_icp_aes_ccm_decrypt(void **ctx,
    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    const uint8_t *mac)
{
	CK_AES_CCM_PARAMS ccm = {
		.nonce = (uchar_t *)iv,
		.ulNonceSize = ZIO_DATA_IV_LEN,
		.authData = (uchar_t *)ad,
		.ulAuthDataSize = adlen,
		.ulDataSize = textlen + ZIO_DATA_MAC_LEN,
		.ulMACSize = ZIO_DATA_MAC_LEN,
	};

	crypto_mechanism_t mech = {
	    .cm_type = crypto_mech2id(SUN_CKM_AES_CCM),
	    .cm_param = (caddr_t)&ccm,
	    .cm_param_len = sizeof (CK_AES_CCM_PARAMS),
	};

	return (zg_icp_aes_decrypt(ctx, &mech, ciphertext, plaintext, textlen,
	    mac));
}

static int
zg_icp_aes_ccm_open(void **ctx, const uint8_t *key, size_t keylen)
{
	return (zg_icp_aes_open(ctx, key, keylen, SUN_CKM_AES_CCM));
}

static const zalgo_cipher_ops_t zg_icp_aes_ccm_ops = {
	.zgc_op_open = zg_icp_aes_ccm_open,
	.zgc_op_close = zg_icp_aes_close,
	.zgc_op_encrypt = zg_icp_aes_ccm_encrypt,
	.zgc_op_decrypt = zg_icp_aes_ccm_decrypt,
};

static int
zg_icp_aes_gcm_encrypt(void **ctx,
    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    uint8_t *mac)
{
	CK_AES_GCM_PARAMS gcm = {
	    .pIv = (uchar_t *)iv,
	    .ulIvLen = ZIO_DATA_IV_LEN,
	    .ulIvBits = CRYPTO_BYTES2BITS(ZIO_DATA_IV_LEN),
	    .pAAD = (uchar_t *)ad,
	    .ulAADLen = adlen,
	    .ulTagBits = CRYPTO_BYTES2BITS(ZIO_DATA_MAC_LEN),
	};

	crypto_mechanism_t mech = {
	    .cm_type = crypto_mech2id(SUN_CKM_AES_GCM),
	    .cm_param = (caddr_t)&gcm,
	    .cm_param_len = sizeof (CK_AES_GCM_PARAMS),
	};

	return (zg_icp_aes_encrypt(ctx, &mech, plaintext, ciphertext, textlen,
	    mac));
}

static int
zg_icp_aes_gcm_decrypt(void **ctx,
    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    const uint8_t *mac)
{
	CK_AES_GCM_PARAMS gcm = {
	    .pIv = (uchar_t *)iv,
	    .ulIvLen = ZIO_DATA_IV_LEN,
	    .ulIvBits = CRYPTO_BYTES2BITS(ZIO_DATA_IV_LEN),
	    .pAAD = (uchar_t *)ad,
	    .ulAADLen = adlen,
	    .ulTagBits = CRYPTO_BYTES2BITS(ZIO_DATA_MAC_LEN),
	};

	crypto_mechanism_t mech = {
	    .cm_type = crypto_mech2id(SUN_CKM_AES_GCM),
	    .cm_param = (caddr_t)&gcm,
	    .cm_param_len = sizeof (CK_AES_GCM_PARAMS),
	};

	return (zg_icp_aes_decrypt(ctx, &mech, ciphertext, plaintext, textlen,
	    mac));
}

static int
zg_icp_aes_gcm_open(void **ctx, const uint8_t *key, size_t keylen)
{
	return (zg_icp_aes_open(ctx, key, keylen, SUN_CKM_AES_GCM));
}

static const zalgo_cipher_ops_t zg_icp_aes_gcm_ops = {
	.zgc_op_open = zg_icp_aes_gcm_open,
	.zgc_op_close = zg_icp_aes_close,
	.zgc_op_encrypt = zg_icp_aes_gcm_encrypt,
	.zgc_op_decrypt = zg_icp_aes_gcm_decrypt,
};

int
zalgo_shim_icp_register(void)
{
	int ret = 0, err;

	err = zalgo_mac_register(ZG_MAC_HMAC_SHA512, "icp", "ICP HMAC-SHA512",
	    &zg_icp_hmac_sha512_ops);
	if (err != 0 && ret == 0)
		ret = err;

	err = zalgo_cipher_register(ZG_CIPHER_AES_CCM, "icp", "ICP AES-CCM",
	    &zg_icp_aes_ccm_ops);
	if (err != 0 && ret == 0)
		ret = err;
	err = zalgo_cipher_register(ZG_CIPHER_AES_GCM, "icp", "ICP AES-GCM",
	    &zg_icp_aes_gcm_ops);
	if (err != 0 && ret == 0)
		ret = err;

	return (ret);
}
