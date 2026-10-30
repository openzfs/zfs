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
#include <opencrypto/cryptodev.h>

/* XXX zio.h */
#define	ZIO_DATA_IV_LEN		12
#define	ZIO_DATA_MAC_LEN	16

static int
zg_freebsd_aes_open(void **ctx, const uint8_t *key, size_t keylen, int alg)
{
	struct crypto_session_params csp = {
		.csp_mode = CSP_MODE_AEAD,
		.csp_flags = CSP_F_SEPARATE_OUTPUT|CSP_F_SEPARATE_AAD,
		.csp_ivlen = ZIO_DATA_IV_LEN,
		.csp_cipher_alg = alg,
		.csp_cipher_key = key,
		.csp_cipher_klen = keylen,
	};
	return (crypto_newsession((crypto_session_t *)ctx, &csp,
	    CRYPTOCAP_F_SOFTWARE));
}

static int
zg_freebsd_aes_ccm_open(void **ctx, const uint8_t *key, size_t keylen)
{
	return (zg_freebsd_aes_open(ctx, key, keylen, CRYPTO_AES_CCM_16));
}

static int
zg_freebsd_aes_gcm_open(void **ctx, const uint8_t *key, size_t keylen)
{
	return (zg_freebsd_aes_open(ctx, key, keylen, CRYPTO_AES_NIST_GCM_16));
}

static void
zg_freebsd_aes_close(void **ctx)
{
	crypto_freesession(*(crypto_session_t *)ctx);
}

/*
 * crypto_done() will always call the callback, even if NULL. That tends
 * not to go so well.
 */
static int
zg_freebsd_crypto_done(struct cryptop *crp)
{
	return (0);
}

static int
zg_freebsd_aes_encrypt(void **ctx,
    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    uint8_t *mac)
{
	crypto_session_t cses = *(crypto_session_t *)ctx;

	struct cryptop *crp = crypto_getreq(cses, M_WAITOK);
	crp->crp_op = CRYPTO_OP_ENCRYPT|CRYPTO_OP_COMPUTE_DIGEST;
	crp->crp_flags = CRYPTO_F_IV_SEPARATE|CRYPTO_F_CBIMM;

	crypto_use_buf(crp, __DECONST(void *, plaintext), textlen);

	struct iovec iov[2] = {
		{
			.iov_base = ciphertext,
			.iov_len = textlen,
		}, {
			.iov_base = mac,
			.iov_len = ZIO_DATA_MAC_LEN,
		},
	};
	struct uio uio = {
		.uio_iov = iov,
		.uio_iovcnt = 2,
	};
	crypto_use_output_uio(crp, &uio);

	crp->crp_payload_start = 0;
	crp->crp_payload_output_start = 0;
	crp->crp_payload_length = textlen;

	crp->crp_aad = __DECONST(void *, ad);
	crp->crp_aad_length = adlen;

	crp->crp_digest_start = textlen;

	memcpy(crp->crp_iv, iv, ZIO_DATA_IV_LEN);

	crp->crp_callback = zg_freebsd_crypto_done;
	int err = crypto_dispatch(crp);
	if (err == 0 && crp->crp_etype != 0)
		err = crp->crp_etype;

	crypto_freereq(crp);

	return (err);
}

static int
zg_freebsd_aes_decrypt(void **ctx,
    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
    const uint8_t *iv, const uint8_t *ad, size_t adlen,
    const uint8_t *mac)
{
	crypto_session_t cses = *(crypto_session_t *)ctx;

	struct cryptop *crp = crypto_getreq(cses, M_WAITOK);
	crp->crp_op = CRYPTO_OP_DECRYPT|CRYPTO_OP_VERIFY_DIGEST;
	crp->crp_flags = CRYPTO_F_IV_SEPARATE|CRYPTO_F_CBIMM;

	struct iovec iov[2] = {
		{
			.iov_base = __DECONST(void *, ciphertext),
			.iov_len = textlen,
		}, {
			.iov_base = __DECONST(void *, mac),
			.iov_len = ZIO_DATA_MAC_LEN,
		},
	};
	struct uio uio = {
		.uio_iov = iov,
		.uio_iovcnt = 2,
	};
	crypto_use_uio(crp, &uio);

	crypto_use_output_buf(crp, plaintext, textlen);

	crp->crp_payload_start = 0;
	crp->crp_payload_output_start = 0;
	crp->crp_payload_length = textlen;

	crp->crp_aad = __DECONST(void *, ad);
	crp->crp_aad_length = adlen;

	crp->crp_digest_start = textlen;

	memcpy(crp->crp_iv, iv, ZIO_DATA_IV_LEN);

	crp->crp_callback = zg_freebsd_crypto_done;
	int err = crypto_dispatch(crp);
	if (err == 0 && crp->crp_etype != 0)
		err = crp->crp_etype;

	crypto_freereq(crp);

	return (err);
}

static const zalgo_cipher_ops_t zg_freebsd_aes_ccm_ops = {
	.zgc_op_open = zg_freebsd_aes_ccm_open,
	.zgc_op_close = zg_freebsd_aes_close,
	.zgc_op_encrypt = zg_freebsd_aes_encrypt,
	.zgc_op_decrypt = zg_freebsd_aes_decrypt,
};

static const zalgo_cipher_ops_t zg_freebsd_aes_gcm_ops = {
	.zgc_op_open = zg_freebsd_aes_gcm_open,
	.zgc_op_close = zg_freebsd_aes_close,
	.zgc_op_encrypt = zg_freebsd_aes_encrypt,
	.zgc_op_decrypt = zg_freebsd_aes_decrypt,
};

int
zalgo_shim_freebsd_register(void)
{
	int ret = 0, err;

	err = zalgo_cipher_register(ZG_CIPHER_AES_CCM, "freebsd",
	    "FreeBSD AES-CCM", &zg_freebsd_aes_ccm_ops);
	if (err != 0 && ret == 0)
		ret = err;
	err = zalgo_cipher_register(ZG_CIPHER_AES_GCM, "freebsd",
	    "FreeBSD AES-GCM", &zg_freebsd_aes_gcm_ops);
	if (err != 0 && ret == 0)
		ret = err;

	return (ret);
}
