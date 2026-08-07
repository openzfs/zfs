// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zio_crypt.h>

void
zio_crypt_key_close_os(zio_crypt_key_t *key)
{
	/* free crypto templates */
	crypto_destroy_ctx_template(key->zk_current_sess.zs_tmpl);
	crypto_destroy_ctx_template(key->zk_hmac_sess.zs_tmpl);
}

int
zio_crypt_key_open_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	crypto_mechanism_t mech = {0};
	int ret;

	/*
	 * Initialize the crypto templates. It's ok if this fails because
	 * this is just an optimization.
	 */
	mech.cm_type = crypto_mech2id(ci->ci_mechname);
	ret = crypto_create_ctx_template(&mech, &key->zk_current_key,
	    &key->zk_current_sess.zs_tmpl);
	if (ret != CRYPTO_SUCCESS)
		key->zk_current_sess.zs_tmpl = NULL;

	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	ret = crypto_create_ctx_template(&mech, &key->zk_hmac_key,
	    &key->zk_hmac_sess.zs_tmpl);
	if (ret != CRYPTO_SUCCESS)
		key->zk_hmac_sess.zs_tmpl = NULL;

	return (0);
}

int
zio_crypt_key_reopen_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	int ret;
	crypto_mechanism_t mech = {0};

	crypto_destroy_ctx_template(key->zk_current_sess.zs_tmpl);

	mech.cm_type = crypto_mech2id(ci->ci_mechname);
	ret = crypto_create_ctx_template(&mech, &key->zk_current_key,
	    &key->zk_current_sess.zs_tmpl);
	if (ret != CRYPTO_SUCCESS)
		key->zk_current_sess.zs_tmpl = NULL;
	return (0);
}

/*
 * Initialise a pair of uios with the requested number of data iovecs. An
 * additional iovec will be allocated for at the end for the ICP to use for the
 * MAC buffer.
 */
int
zio_crypt_uios_init_os(zfs_uio_t *u1, zfs_uio_t *u2, int iovcnt, int *idx)
{
	memset(u1, 0, sizeof (zfs_uio_t));
	memset(u2, 0, sizeof (zfs_uio_t));

	/* One extra for the MAC. */
	iovcnt++;

	zfs_uio_iov(u1) = kmem_zalloc(iovcnt * sizeof (iovec_t), KM_SLEEP);
	zfs_uio_iov(u2) = kmem_zalloc(iovcnt * sizeof (iovec_t), KM_SLEEP);

	zfs_uio_iovcnt(u1) = zfs_uio_iovcnt(u2) = iovcnt;
	zfs_uio_segflg(u1) = zfs_uio_segflg(u2) = UIO_SYSSPACE;

	*idx = 0;

	return (0);
}

void
zio_crypt_uios_fini_os(zfs_uio_t *u1, zfs_uio_t *u2) {
	ASSERT3U(zfs_uio_iovcnt(u1), ==, zfs_uio_iovcnt(u2));

	kmem_free(zfs_uio_iov(u1), zfs_uio_iovcnt(u1) * sizeof (iovec_t));
	kmem_free(zfs_uio_iov(u2), zfs_uio_iovcnt(u2) * sizeof (iovec_t));
}

static int
zio_encrypt_decrypt_os_common(boolean_t do_encrypt, const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *src, zfs_uio_t *dst, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	ASSERT3U(zfs_uio_iovcnt(src), ==, zfs_uio_iovcnt(dst));

	/* populate the source and dest structs */
	crypto_data_t src_cd = {
		.cd_format = CRYPTO_DATA_UIO,
		.cd_uio = src,
		.cd_offset = 0,
	};
	crypto_data_t dst_cd = {
		.cd_format = CRYPTO_DATA_UIO,
		.cd_uio = dst,
		.cd_offset = 0,
	};

	if (do_encrypt) {
		/*
		 * When encrypting, the MAC will be stored at the end of the
		 * encrypted data, so add the MAC buffer to the end of the dest
		 * UIO and extend the data length to accomodate it.
		 */
		zfs_uio_iovbase(dst, zfs_uio_iovcnt(dst)-1) = mac;
		zfs_uio_iovlen(dst, zfs_uio_iovcnt(dst)-1) = ZIO_DATA_MAC_LEN;
		src_cd.cd_length = datalen;
		dst_cd.cd_length = datalen + ZIO_DATA_MAC_LEN;
	} else {
		/*
		 * When decrypting, the MAC is presented at the end of the
		 * plaintext data, so add the MAC buffer to the end of the
		 * source UIO.
		 *
		 * Strangely, the ICP requires that the destination/plaintext
		 * must include the MAC length when decrypting, even though it
		 * will never write anything and does not need to have the
		 * extra space allocated. So we extend both source and data
		 * length to cover it.
		 */
		zfs_uio_iovbase(src, zfs_uio_iovcnt(src)-1) = mac;
		zfs_uio_iovlen(src, zfs_uio_iovcnt(src)-1) = ZIO_DATA_MAC_LEN;
		src_cd.cd_length = dst_cd.cd_length =
		    datalen + ZIO_DATA_MAC_LEN;
	}

	/* setup encryption mechanism */
	crypto_mechanism_t mech = {0};
	mech.cm_type = crypto_mech2id(ci->ci_mechname);

	/* setup encryption params */
	union {
		CK_AES_CCM_PARAMS ccm;
		CK_AES_GCM_PARAMS gcm;
	} params;

	if (ci->ci_crypt_type == ZC_TYPE_CCM) {
		CK_AES_CCM_PARAMS *ccm = &params.ccm;
		ccm->nonce = (uchar_t *)iv;
		ccm->ulNonceSize = ZIO_DATA_IV_LEN;
		ccm->authData = (uchar_t *)ad;
		ccm->ulAuthDataSize = adlen;
		ccm->ulDataSize = src_cd.cd_length;
		ccm->ulMACSize = ZIO_DATA_MAC_LEN;
		mech.cm_param = (caddr_t)ccm;
		mech.cm_param_len = sizeof (*ccm);
	} else {
		CK_AES_GCM_PARAMS *gcm = &params.gcm;
		gcm->pIv = (uchar_t *)iv;
		gcm->ulIvLen = ZIO_DATA_IV_LEN;
		gcm->ulIvBits = CRYPTO_BYTES2BITS(ZIO_DATA_IV_LEN);
		gcm->pAAD = (uchar_t *)ad;
		gcm->ulAADLen = adlen;
		gcm->ulTagBits = CRYPTO_BYTES2BITS(ZIO_DATA_MAC_LEN);
		mech.cm_param = (caddr_t)gcm;
		mech.cm_param_len = sizeof (*gcm);
	}

	/* perform the actual encryption */
	crypto_ctx_template_t *tmpl = sess != NULL ? sess->zs_tmpl : NULL;
	int err = 0;
	if (do_encrypt) {
		err = crypto_encrypt(&mech, &src_cd, key, tmpl, &dst_cd);
		if (err != CRYPTO_SUCCESS)
			err = SET_ERROR(EIO);
	} else {
		err = crypto_decrypt(&mech, &src_cd, key, tmpl, &dst_cd);
		if (err != CRYPTO_SUCCESS) {
			ASSERT3U(err, ==, CRYPTO_INVALID_MAC);
			err = SET_ERROR(ECKSUM);
		}
	}

	return (err);
}

int
zio_encrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *plaintext, zfs_uio_t *ciphertext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	return (zio_encrypt_decrypt_os_common(B_TRUE, ci, key, sess,
	    plaintext, ciphertext, datalen, iv, ad, adlen, mac));
}

int
zio_decrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *ciphertext, zfs_uio_t *plaintext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	return (zio_encrypt_decrypt_os_common(B_FALSE, ci, key, sess,
	    ciphertext, plaintext, datalen, iv, ad, adlen, mac));
}

int
zio_crypt_hmac_os(zio_crypt_key_t *key, const uint8_t *data, size_t datalen,
    uint8_t digest[SHA512_HMAC_LEN])
{
	/* initialize sha512-hmac mechanism */
	crypto_mechanism_t mech = {0};
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);

	/* initialize the crypto data */
	crypto_data_t in = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = datalen,
		.cd_raw.iov_base = (void *)data,
		.cd_raw.iov_len = datalen,
	};
	crypto_data_t out = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = SHA512_HMAC_LEN,
		.cd_raw.iov_base = (void *)digest,
		.cd_raw.iov_len = SHA512_HMAC_LEN,
	};

	/* generate the hmac */
	if (crypto_mac(&mech, &in, &key->zk_hmac_key,
	    key->zk_hmac_sess.zs_tmpl, &out) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

int
zio_crypt_hmac_init_os(zio_crypt_hmac_t *hmac, zio_crypt_key_t *key)
{
	/* initialize HMAC mechanism */
	crypto_mechanism_t mech = {0};
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);

	if (crypto_mac_init(&mech, &key->zk_hmac_key, NULL,
	    &hmac->zh_ctx) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

int
zio_crypt_hmac_update_os(zio_crypt_hmac_t *hmac, const uint8_t *data,
    size_t datalen)
{
	crypto_data_t cd = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = datalen,
		.cd_raw.iov_base = (char *)data,
		.cd_raw.iov_len = datalen,
	};

	if (crypto_mac_update(hmac->zh_ctx, &cd) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}

int
zio_crypt_hmac_final_os(zio_crypt_hmac_t *hmac, uint8_t digest[SHA512_HMAC_LEN])
{
	crypto_data_t cd = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_offset = 0,
		.cd_length = SHA512_HMAC_LEN,
		.cd_raw.iov_base = (char *)digest,
		.cd_raw.iov_len = SHA512_HMAC_LEN,
	};

	if (crypto_mac_final(hmac->zh_ctx, &cd) != CRYPTO_SUCCESS)
		return (SET_ERROR(EIO));

	return (0);
}
