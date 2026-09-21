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
#include <sys/zfs_debug.h>
#include <openssl/evp.h>
#include <openssl/err.h>

/* ========== */

/* XXX zio_crypt.h */
#define	SHA512_HMAC_LEN		64
#define	SHA512_HMAC_KEYLEN	64

static EVP_MAC *zg_ossl_hmac_sha512_mac = NULL;
static OSSL_PARAM zg_ossl_hmac_sha512_params[2];

static int
zg_ossl_hmac_sha512_init(void **ctxp, const uint8_t *key, size_t keylen)
{
	if (keylen != SHA512_HMAC_KEYLEN)
		return (EINVAL);

	EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(zg_ossl_hmac_sha512_mac);
	if (!ctx)
		goto error;

	if (EVP_MAC_init(ctx, key, keylen, zg_ossl_hmac_sha512_params) != 1)
		goto error;

	*ctxp = ctx;
	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
	ERR_print_errors_fp(stderr);
	if (ctx)
		EVP_MAC_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

static int
zg_ossl_hmac_sha512_update(void **ctxp, const uint8_t *msg, size_t msglen)
{
	EVP_MAC_CTX *ctx = *ctxp;

	if (EVP_MAC_update(ctx, msg, msglen) != 1)
		goto error;

	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure; msglen=%lu\n",
	    __FUNCTION__, msglen);
	ERR_print_errors_fp(stderr);
	EVP_MAC_CTX_free(ctx);
	return (SET_ERROR(EIO));
}

static int
zg_ossl_hmac_sha512_final(void **ctxp, uint8_t *mac)
{
	EVP_MAC_CTX *ctx = *ctxp;

	size_t len;
	int rc = EVP_MAC_final(ctx, mac, &len, SHA512_HMAC_LEN);
	if (rc != 1) {
		fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
		ERR_print_errors_fp(stderr);
	}

	EVP_MAC_CTX_free(ctx);

	return ((rc == 1) ? 0 : SET_ERROR(EIO));
}

static int
zg_ossl_hmac_sha512_once(const uint8_t *key, size_t keylen,
    const uint8_t *msg, size_t msglen, uint8_t *mac)
{
	void *ctx;
	int err = zg_ossl_hmac_sha512_init(&ctx, key, keylen);
	if (err != 0)
		return (err);
	err = zg_ossl_hmac_sha512_update(&ctx, msg, msglen);
	if (err != 0)
		return (err);
	return (zg_ossl_hmac_sha512_final(&ctx, mac));
}

static const zalgo_mac_ops_t zg_ossl_hmac_sha512_ops = {
	.zgm_op_init = zg_ossl_hmac_sha512_init,
	.zgm_op_update = zg_ossl_hmac_sha512_update,
	.zgm_op_final = zg_ossl_hmac_sha512_final,
	.zgm_op_once = zg_ossl_hmac_sha512_once,
};

static int
zg_ossl_mac_register(void)
{
	zg_ossl_hmac_sha512_mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
	if (zg_ossl_hmac_sha512_mac == NULL) {
		fprintf(stderr, "%s: couldn't fetch algorithm: HMAC\n",
		    __FUNCTION__);
		ERR_print_errors_fp(stderr);
		return (0);
	}

	zg_ossl_hmac_sha512_params[0] =
	    OSSL_PARAM_construct_utf8_string("digest", (char *)"SHA512", 0);
	zg_ossl_hmac_sha512_params[1] = OSSL_PARAM_construct_end();

	return (zalgo_mac_register(ZG_MAC_HMAC_SHA512, "openssl",
	    "OpenSSL HMAC-SHA512", &zg_ossl_hmac_sha512_ops));
}

/* ========== */

/* XXX zio.h */
#define	ZIO_DATA_IV_LEN		12
#define	ZIO_DATA_MAC_LEN	16

static EVP_CIPHER *zg_ossl_aes_128_ccm_cipher = NULL;
static EVP_CIPHER *zg_ossl_aes_192_ccm_cipher = NULL;
static EVP_CIPHER *zg_ossl_aes_256_ccm_cipher = NULL;
static EVP_CIPHER *zg_ossl_aes_128_gcm_cipher = NULL;
static EVP_CIPHER *zg_ossl_aes_192_gcm_cipher = NULL;
static EVP_CIPHER *zg_ossl_aes_256_gcm_cipher = NULL;

typedef struct {
	EVP_CIPHER_CTX *s_encrypt;
	EVP_CIPHER_CTX *s_decrypt;
} zg_ossl_aes_session_t;

static int
zg_ossl_aes_open(void **ctx, const uint8_t *key, EVP_CIPHER *cipher)
{
	zg_ossl_aes_session_t *sess =
	    kmem_alloc(sizeof (zg_ossl_aes_session_t), KM_SLEEP);

	sess->s_decrypt = NULL;

	sess->s_encrypt = EVP_CIPHER_CTX_new();
	if (sess->s_encrypt == NULL)
		goto error;

	sess->s_decrypt = EVP_CIPHER_CTX_new();
	if (sess->s_decrypt == NULL)
		goto error;

	/* Initialise contexts for wanted cipher. */
	if (EVP_EncryptInit_ex(sess->s_encrypt, cipher, NULL, NULL, NULL) != 1)
		goto error;
	if (EVP_DecryptInit_ex(sess->s_decrypt, cipher, NULL, NULL, NULL) != 1)
		goto error;

	/*
	 * For CCM, the IV length is mixed into the key schedule, so we need
	 * to set it before setting the key.
	 *
	 * https://github.com/openssl/openssl/issues/23302
	 */
	if (EVP_CIPHER_CTX_ctrl(sess->s_encrypt, EVP_CTRL_AEAD_SET_IVLEN,
	    ZIO_DATA_IV_LEN, NULL) != 1)
		goto error;
	if (EVP_CIPHER_CTX_ctrl(sess->s_decrypt, EVP_CTRL_AEAD_SET_IVLEN,
	    ZIO_DATA_IV_LEN, NULL) != 1)
		goto error;

	/*
	 * Similarly, for CCM decrypt, the MAC len is mixed into the key
	 * schedule, so we need to set it before setting the key. We have to
	 * provide a MAC buffer as well, which will be copied into the context.
	 * Since we don't know the MAC yet, we just use an empty buffer, and
	 * then set it properly in the decrypt function.
	 */
	uint8_t mac[ZIO_DATA_MAC_LEN] = {};
	if (EVP_CIPHER_CTX_ctrl(sess->s_decrypt, EVP_CTRL_AEAD_SET_TAG,
	    ZIO_DATA_MAC_LEN, &mac) != 1)
		goto error;

	/*
	 * Same for CCM encrypt, but we have to check the mode first, because
	 * GCM will reject it.
	 */
	if (EVP_CIPHER_CTX_get_mode(sess->s_encrypt) == EVP_CIPH_CCM_MODE) {
		if (EVP_CIPHER_CTX_ctrl(sess->s_encrypt, EVP_CTRL_AEAD_SET_TAG,
		    ZIO_DATA_MAC_LEN, NULL) != 1)
			goto error;
	}

	/* All inputs to key schedule are ready, so set the key. */
	if (EVP_EncryptInit_ex(sess->s_encrypt, NULL, NULL, key, NULL) != 1)
		goto error;
	if (EVP_DecryptInit_ex(sess->s_decrypt, NULL, NULL, key, NULL) != 1)
		goto error;

	*ctx = sess;
	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
	ERR_print_errors_fp(stderr);
	if (sess->s_encrypt != NULL)
		EVP_CIPHER_CTX_free(sess->s_encrypt);
	if (sess->s_decrypt != NULL)
		EVP_CIPHER_CTX_free(sess->s_decrypt);
	kmem_free(sess, sizeof (zg_ossl_aes_session_t));
	return (SET_ERROR(EIO));
}

static void
zg_ossl_aes_close(void **ctx)
{
	zg_ossl_aes_session_t *sess = *ctx;
	EVP_CIPHER_CTX_free(sess->s_encrypt);
	EVP_CIPHER_CTX_free(sess->s_decrypt);
	kmem_free(sess, sizeof (zg_ossl_aes_session_t));
}

static int
zg_ossl_aes_ccm_encrypt(void **ctx,
    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    uint8_t *mac)
{
	zg_ossl_aes_session_t *sess = *ctx;

	EVP_CIPHER_CTX *ectx = EVP_CIPHER_CTX_dup(sess->s_encrypt);
	if (ectx == NULL)
		goto error;

	/*
	 * Set the IV. Length was set in zg_ossl_aes_open; resetting it would
	 * reset the key schedule, so we don't do that!
	 */
	if (EVP_EncryptInit_ex(ectx, NULL, NULL, NULL, iv) != 1)
		goto error;

	/* CCM requires that the data length be provided up front. */
	int len;
	if (EVP_EncryptUpdate(ectx, NULL, &len, NULL, textlen) != 1)
		goto error;

	/*
	 * If available, mix in the AAD. CCM will still mix the state even if
	 * the length is zero, so we skip it if we don't have any.
	 */
	if (adlen > 0 && EVP_EncryptUpdate(ectx, NULL, &len, ad, adlen) != 1)
		goto error;

	/* Encrypt the actual data. */
	if (EVP_EncryptUpdate(ectx, ciphertext, &len, plaintext, textlen) != 1)
		goto error;
	ASSERT3U(textlen, ==, len);

	/* Finalise the state. */
	if (EVP_EncryptFinal_ex(ectx, NULL, &len) != 1)
		goto error;
	ASSERT0(len);

	/* Compuite and fill the MAC. */
	if (EVP_CIPHER_CTX_ctrl(ectx, EVP_CTRL_AEAD_GET_TAG,
	    ZIO_DATA_MAC_LEN, mac) != 1)
		goto error;

	EVP_CIPHER_CTX_free(ectx);

	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
	ERR_print_errors_fp(stderr);
	if (ectx != NULL)
		EVP_CIPHER_CTX_free(ectx);
	return (SET_ERROR(EIO));
}

static int
zg_ossl_aes_ccm_decrypt(void **ctx,
    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    const uint8_t *mac)
{
	zg_ossl_aes_session_t *sess = *ctx;

	EVP_CIPHER_CTX *dctx = EVP_CIPHER_CTX_dup(sess->s_decrypt);
	if (dctx == NULL)
		goto error;

	/* Set a new MAC buffer. See zg_ossl_aes_open(). */
	if (EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_TAG,
	    ZIO_DATA_MAC_LEN, (void *)mac) != 1)
		goto error;

	/*
	 * Set the IV. Length was set in zg_ossl_aes_open; resetting it would
	 * reset the key schedule, so we don't do that!
	 */
	if (EVP_DecryptInit_ex(dctx, NULL, NULL, NULL, iv) != 1)
		goto error;

	/* CCM requires that the data length be provided up front. */
	int len;
	if (EVP_DecryptUpdate(dctx, NULL, &len, NULL, textlen) != 1)
		goto error;

	/*
	 * If available, mix in the AAD. CCM will still mix the state even if
	 * the length is zero, so we skip it if we don't have any.
	 */
	if (adlen > 0 && EVP_DecryptUpdate(dctx, NULL, &len, ad, adlen) != 1)
		goto error;

	/* Decrypt the actual data. MAC is verified internally. */
	if (EVP_DecryptUpdate(dctx, plaintext, &len, ciphertext, textlen) != 1)
		goto error;
	ASSERT3U(textlen, ==, len);

	EVP_CIPHER_CTX_free(dctx);

	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
	ERR_print_errors_fp(stderr);
	if (dctx != NULL)
		EVP_CIPHER_CTX_free(dctx);
	return (SET_ERROR(EIO));
}

static int
zg_ossl_aes_ccm_open(void **ctx, const uint8_t *key, size_t keylen)
{
	switch (keylen) {
	case 16:
		return (zg_ossl_aes_open(ctx, key, zg_ossl_aes_128_ccm_cipher));
	case 24:
		return (zg_ossl_aes_open(ctx, key, zg_ossl_aes_192_ccm_cipher));
	case 32:
		return (zg_ossl_aes_open(ctx, key, zg_ossl_aes_256_ccm_cipher));
	default:
		return (SET_ERROR(EINVAL));
	}

	__builtin_unreachable();
}
static const zalgo_cipher_ops_t zg_ossl_aes_ccm_ops = {
	.zgc_op_open = zg_ossl_aes_ccm_open,
	.zgc_op_close = zg_ossl_aes_close,
	.zgc_op_encrypt = zg_ossl_aes_ccm_encrypt,
	.zgc_op_decrypt = zg_ossl_aes_ccm_decrypt,
};

static int
zg_ossl_aes_gcm_encrypt(void **ctx,
    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    uint8_t *mac)
{
	zg_ossl_aes_session_t *sess = *ctx;

	EVP_CIPHER_CTX *ectx = EVP_CIPHER_CTX_dup(sess->s_encrypt);
	if (ectx == NULL)
		goto error;

	if (EVP_EncryptInit_ex(ectx, NULL, NULL, NULL, iv) != 1)
		goto error;

	int len;
	if (EVP_EncryptUpdate(ectx, NULL, &len, ad, adlen) != 1)
		goto error;

	if (EVP_EncryptUpdate(ectx, ciphertext, &len, plaintext, textlen) != 1)
		goto error;
	ASSERT3U(textlen, ==, len);

	if (EVP_EncryptFinal_ex(ectx, NULL, &len) != 1)
		goto error;
	ASSERT0(len);

	if (EVP_CIPHER_CTX_ctrl(ectx, EVP_CTRL_AEAD_GET_TAG,
	    ZIO_DATA_MAC_LEN, mac) != 1)
		goto error;

	EVP_CIPHER_CTX_free(ectx);

	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
	ERR_print_errors_fp(stderr);
	if (ectx != NULL)
		EVP_CIPHER_CTX_free(ectx);
	return (SET_ERROR(EIO));
}

static int
zg_ossl_aes_gcm_decrypt(void **ctx,
    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    const uint8_t *mac)
{
	zg_ossl_aes_session_t *sess = *ctx;

	EVP_CIPHER_CTX *dctx = EVP_CIPHER_CTX_dup(sess->s_decrypt);
	if (dctx == NULL)
		goto error;

	if (EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_GCM_SET_TAG,
	    ZIO_DATA_MAC_LEN, (void *)mac) != 1)
		goto error;

	if (EVP_DecryptInit_ex(dctx, NULL, NULL, NULL, iv) != 1)
		goto error;

	int len;
	if (EVP_DecryptUpdate(dctx, NULL, &len, ad, adlen) != 1)
		goto error;

	if (EVP_DecryptUpdate(dctx, plaintext, &len, ciphertext, textlen) != 1)
		goto error;
	ASSERT3U(textlen, ==, len);

	if (EVP_DecryptFinal_ex(dctx, NULL, &len) != 1)
		goto error;
	ASSERT0(len);

	EVP_CIPHER_CTX_free(dctx);

	return (0);

error:
	fprintf(stderr, "%s: internal OpenSSL failure\n", __FUNCTION__);
	ERR_print_errors_fp(stderr);
	if (dctx != NULL)
		EVP_CIPHER_CTX_free(dctx);
	return (SET_ERROR(EIO));
}

static int
zg_ossl_aes_gcm_open(void **ctx, const uint8_t *key, size_t keylen)
{
	switch (keylen) {
	case 16:
		return (zg_ossl_aes_open(ctx, key, zg_ossl_aes_128_gcm_cipher));
	case 24:
		return (zg_ossl_aes_open(ctx, key, zg_ossl_aes_192_gcm_cipher));
	case 32:
		return (zg_ossl_aes_open(ctx, key, zg_ossl_aes_256_gcm_cipher));
	default:
		return (SET_ERROR(EINVAL));
	}

	__builtin_unreachable();
}

static const zalgo_cipher_ops_t zg_ossl_aes_gcm_ops = {
	.zgc_op_open = zg_ossl_aes_gcm_open,
	.zgc_op_close = zg_ossl_aes_close,
	.zgc_op_encrypt = zg_ossl_aes_gcm_encrypt,
	.zgc_op_decrypt = zg_ossl_aes_gcm_decrypt,
};

static EVP_CIPHER *
zg_ossl_cipher_fetch(const char *alg)
{
	EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, alg, NULL);
	if (cipher != NULL)
		return (cipher);

	fprintf(stderr, "%s: couldn't fetch algorithm: %s\n",
	    __FUNCTION__, alg);
	ERR_print_errors_fp(stderr);
	return (NULL);
}

static int
zg_ossl_cipher_register(void)
{
	zg_ossl_aes_128_ccm_cipher = zg_ossl_cipher_fetch("AES-128-CCM");
	zg_ossl_aes_192_ccm_cipher = zg_ossl_cipher_fetch("AES-192-CCM");
	zg_ossl_aes_256_ccm_cipher = zg_ossl_cipher_fetch("AES-256-CCM");
	zg_ossl_aes_128_gcm_cipher = zg_ossl_cipher_fetch("AES-128-GCM");
	zg_ossl_aes_192_gcm_cipher = zg_ossl_cipher_fetch("AES-192-GCM");
	zg_ossl_aes_256_gcm_cipher = zg_ossl_cipher_fetch("AES-256-GCM");

	int ret = 0, err;

	err = zalgo_cipher_register(ZG_CIPHER_AES_CCM, "openssl",
	    "OpenSSL AES-CCM", &zg_ossl_aes_ccm_ops);
	if (err != 0 && ret == 0)
		ret = err;
	err = zalgo_cipher_register(ZG_CIPHER_AES_GCM, "openssl",
	    "OpenSSL AES-GCM", &zg_ossl_aes_gcm_ops);
	if (err != 0 && ret == 0)
		ret = err;

	return (ret);
}

/* ========== */

int
zalgo_shim_openssl_register(void)
{
	int ret = 0, err;
	err = zg_ossl_mac_register();
	if (err != 0 && ret == 0)
		ret = err;

	err = zg_ossl_cipher_register();
	if (err != 0 && ret == 0)
		ret = err;

	return (ret);
}
