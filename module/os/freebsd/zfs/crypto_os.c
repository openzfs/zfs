/*
 * Copyright (c) 2005-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2018 Sean Eric Fagan <sef@ixsystems.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Portions of this file are derived from sys/geom/eli/g_eli_hmac.c
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/errno.h>

#ifdef _KERNEL
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>
#else
#include <strings.h>
#endif

#include <sys/zio_crypt.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>

#include <sys/freebsd_crypto.h>

#define	SHA512_HMAC_BLOCK_SIZE	128

static int crypt_sessions = 0;
SYSCTL_DECL(_vfs_zfs);
SYSCTL_INT(_vfs_zfs, OID_AUTO, crypt_sessions, CTLFLAG_RD,
	&crypt_sessions, 0, "Number of cryptographic sessions created");

void
crypto_mac_init(struct hmac_ctx *ctx, const crypto_key_t *c_key)
{
	uint8_t k_ipad[SHA512_HMAC_BLOCK_SIZE],
	    k_opad[SHA512_HMAC_BLOCK_SIZE],
	    key[SHA512_HMAC_BLOCK_SIZE];
	SHA512_CTX lctx;
	int i;
	size_t cl_bytes = CRYPTO_BITS2BYTES(c_key->ck_length);

	/*
	 * This code is based on the similar code in geom/eli/g_eli_hmac.c
	 */
	explicit_bzero(key, sizeof (key));
	if (c_key->ck_length  == 0)
		/* do nothing */;
	else if (cl_bytes <= SHA512_HMAC_BLOCK_SIZE)
		bcopy(c_key->ck_data, key, cl_bytes);
	else {
		/*
		 * If key is longer than 128 bytes reset it to
		 * key = SHA512(key).
		 */
		SHA512_Init(&lctx);
		SHA512_Update(&lctx, c_key->ck_data, cl_bytes);
		SHA512_Final(key, &lctx);
	}

	/* XOR key with ipad and opad values. */
	for (i = 0; i < sizeof (key); i++) {
		k_ipad[i] = key[i] ^ 0x36;
		k_opad[i] = key[i] ^ 0x5c;
	}
	explicit_bzero(key, sizeof (key));

	/* Start inner SHA512. */
	SHA512_Init(&ctx->innerctx);
	SHA512_Update(&ctx->innerctx, k_ipad, sizeof (k_ipad));
	explicit_bzero(k_ipad, sizeof (k_ipad));
	/* Start outer SHA512. */
	SHA512_Init(&ctx->outerctx);
	SHA512_Update(&ctx->outerctx, k_opad, sizeof (k_opad));
	explicit_bzero(k_opad, sizeof (k_opad));
}

void
crypto_mac_update(struct hmac_ctx *ctx, const void *data, size_t datasize)
{
	SHA512_Update(&ctx->innerctx, data, datasize);
}

void
crypto_mac_final(struct hmac_ctx *ctx, void *md, size_t mdsize)
{
	uint8_t digest[SHA512_DIGEST_LENGTH];

	/* Complete inner hash */
	SHA512_Final(digest, &ctx->innerctx);

	/* Complete outer hash */
	SHA512_Update(&ctx->outerctx, digest, sizeof (digest));
	SHA512_Final(digest, &ctx->outerctx);

	explicit_bzero(ctx, sizeof (*ctx));
	/* mdsize == 0 means "Give me the whole hash!" */
	if (mdsize == 0)
		mdsize = SHA512_DIGEST_LENGTH;
	bcopy(digest, md, mdsize);
	explicit_bzero(digest, sizeof (digest));
}

void
crypto_mac(const crypto_key_t *key, const void *in_data, size_t in_data_size,
    void *out_data, size_t out_data_size)
{
	struct hmac_ctx ctx;

	crypto_mac_init(&ctx, key);
	crypto_mac_update(&ctx, in_data, in_data_size);
	crypto_mac_final(&ctx, out_data, out_data_size);
}

static int
freebsd_zfs_crypt_done(struct cryptop *crp)
{
	freebsd_crypt_session_t *ses;

	ses = crp->crp_opaque;
	mtx_lock(&ses->fs_lock);
	ses->fs_done = true;
	mtx_unlock(&ses->fs_lock);
	wakeup(crp);
	return (0);
}

void
freebsd_crypt_freesession(freebsd_crypt_session_t *sess)
{
	mtx_destroy(&sess->fs_lock);
	crypto_freesession(sess->fs_sid);
	explicit_bzero(sess, sizeof (*sess));
}

static int
zfs_crypto_dispatch(freebsd_crypt_session_t *session, 	struct cryptop *crp)
{
	int error;

	crp->crp_opaque = session;
	crp->crp_callback = freebsd_zfs_crypt_done;
	for (;;) {
		error = crypto_dispatch(crp);
		if (error)
			break;
		mtx_lock(&session->fs_lock);
		while (session->fs_done == false)
			msleep(crp, &session->fs_lock, 0,
			    "zfs_crypto", 0);
		mtx_unlock(&session->fs_lock);

		if (crp->crp_etype == ENOMEM) {
			pause("zcrnomem", 1);
		} else if (crp->crp_etype != EAGAIN) {
			error = crp->crp_etype;
			break;
		}
		crp->crp_etype = 0;
		crp->crp_flags &= ~CRYPTO_F_DONE;
		session->fs_done = false;
#if __FreeBSD_version < 1300087
		/*
		 * Session ID changed, so we should record that,
		 * and try again
		 */
		session->fs_sid = crp->crp_session;
#endif
	}
	return (error);
}
static void
freebsd_crypt_uio_debug_log(boolean_t encrypt,
    freebsd_crypt_session_t *input_sessionp,
    const struct zio_crypt_info *c_info,
    zfs_uio_t *data_uio,
    crypto_key_t *key,
    uint8_t *ivbuf,
    size_t datalen,
    size_t auth_len)
{
#ifdef FCRYPTO_DEBUG
	struct cryptodesc *crd;
	uint8_t *p = NULL;
	size_t total = 0;

	printf("%s(%s, %p, { %s, %d, %d, %s }, %p, { %p, %u }, "
	    "%p, %u, %u)\n",
	    __FUNCTION__, encrypt ? "encrypt" : "decrypt", input_sessionp,
	    c_info->ci_algname, c_info->ci_crypt_type,
	    (unsigned int)c_info->ci_keylen, c_info->ci_name,
	    data_uio, key->ck_data,
	    (unsigned int)key->ck_length,
	    ivbuf, (unsigned int)datalen, (unsigned int)auth_len);
	printf("\tkey = { ");
	for (int i = 0; i < key->ck_length / 8; i++) {
		uint8_t *b = (uint8_t *)key->ck_data;
		printf("%02x ", b[i]);
	}
	printf("}\n");
	for (int i = 0; i < zfs_uio_iovcnt(data_uio); i++) {
		printf("\tiovec #%d: <%p, %u>\n", i,
		    zfs_uio_iovbase(data_uio, i),
		    (unsigned int)zfs_uio_iovlen(data_uio, i));
		total += zfs_uio_iovlen(data_uio, i);
	}
	zfs_uio_resid(data_uio) = total;
#endif
}
/*
 * Create a new cryptographic session.  This should
 * happen every time the key changes (including when
 * it's first loaded).
 */
#if __FreeBSD_version >= 1300087
int
freebsd_crypt_newsession(freebsd_crypt_session_t *sessp,
    const struct zio_crypt_info *c_info, crypto_key_t *key)
{
	struct crypto_session_params csp;
	int error = 0;

#ifdef FCRYPTO_DEBUG
	printf("%s(%p, { %s, %d, %d, %s }, { %p, %u })\n",
	    __FUNCTION__, sessp,
	    c_info->ci_algname, c_info->ci_crypt_type,
	    (unsigned int)c_info->ci_keylen, c_info->ci_name,
	    key->ck_data, (unsigned int)key->ck_length);
	printf("\tkey = { ");
	for (int i = 0; i < key->ck_length / 8; i++) {
		uint8_t *b = (uint8_t *)key->ck_data;
		printf("%02x ", b[i]);
	}
	printf("}\n");
#endif
	bzero(&csp, sizeof (csp));
	csp.csp_mode = CSP_MODE_AEAD;
	csp.csp_cipher_key = key->ck_data;
	csp.csp_cipher_klen = key->ck_length / 8;
	switch (c_info->ci_crypt_type) {
		case ZC_TYPE_GCM:
		csp.csp_cipher_alg = CRYPTO_AES_NIST_GCM_16;
		csp.csp_ivlen = AES_GCM_IV_LEN;
		switch (key->ck_length/8) {
		case AES_128_GMAC_KEY_LEN:
		case AES_192_GMAC_KEY_LEN:
		case AES_256_GMAC_KEY_LEN:
			break;
		default:
			error = EINVAL;
			goto bad;
		}
		break;
	case ZC_TYPE_CCM:
		csp.csp_cipher_alg = CRYPTO_AES_CCM_16;
		csp.csp_ivlen = AES_CCM_IV_LEN;
		switch (key->ck_length/8) {
		case AES_128_CBC_MAC_KEY_LEN:
		case AES_192_CBC_MAC_KEY_LEN:
		case AES_256_CBC_MAC_KEY_LEN:
			break;
		default:
			error = EINVAL;
			goto bad;
			break;
		}
		break;
	default:
		error = ENOTSUP;
		goto bad;
	}

	/*
	 * Disable the use of hardware drivers on FreeBSD 13 and later since
	 * common crypto offload drivers impose constraints on AES-GCM AAD
	 * lengths that make them unusable for ZFS, and we currently do not have
	 * a mechanism to fall back to a software driver for requests not
	 * handled by a hardware driver.
	 *
	 * On 12 we continue to permit the use of hardware drivers since
	 * CPU-accelerated drivers such as aesni(4) register themselves as
	 * hardware drivers.
	 */
	error = crypto_newsession(&sessp->fs_sid, &csp, CRYPTOCAP_F_SOFTWARE);
	mtx_init(&sessp->fs_lock, "FreeBSD Cryptographic Session Lock",
	    NULL, MTX_DEF);
	crypt_sessions++;
bad:
#ifdef FCRYPTO_DEBUG
	if (error)
		printf("%s: returning error %d\n", __FUNCTION__, error);
#endif
	return (error);
}

int
freebsd_crypt_uio(boolean_t encrypt,
    freebsd_crypt_session_t *input_sessionp,
    const struct zio_crypt_info *c_info,
    zfs_uio_t *data_uio,
    crypto_key_t *key,
    uint8_t *ivbuf,
    size_t datalen,
    size_t auth_len)
{
	struct cryptop *crp;
	freebsd_crypt_session_t *session = NULL;
	int error = 0;
	size_t total = 0;

	freebsd_crypt_uio_debug_log(encrypt, input_sessionp, c_info, data_uio,
	    key, ivbuf, datalen, auth_len);
	for (int i = 0; i < zfs_uio_iovcnt(data_uio); i++)
		total += zfs_uio_iovlen(data_uio, i);
	zfs_uio_resid(data_uio) = total;
	if (input_sessionp == NULL) {
		session = kmem_zalloc(sizeof (*session), KM_SLEEP);
		error = freebsd_crypt_newsession(session, c_info, key);
		if (error)
			goto out;
	} else
		session = input_sessionp;

	crp = crypto_getreq(session->fs_sid, M_WAITOK);
	if (encrypt) {
		crp->crp_op = CRYPTO_OP_ENCRYPT |
		    CRYPTO_OP_COMPUTE_DIGEST;
	} else {
		crp->crp_op = CRYPTO_OP_DECRYPT |
		    CRYPTO_OP_VERIFY_DIGEST;
	}
	crp->crp_flags = CRYPTO_F_CBIFSYNC | CRYPTO_F_IV_SEPARATE;
	crypto_use_uio(crp, GET_UIO_STRUCT(data_uio));

	crp->crp_aad_start = 0;
	crp->crp_aad_length = auth_len;
	crp->crp_payload_start = auth_len;
	crp->crp_payload_length = datalen;
	crp->crp_digest_start = auth_len + datalen;

	bcopy(ivbuf, crp->crp_iv, ZIO_DATA_IV_LEN);
	error = zfs_crypto_dispatch(session, crp);
	crypto_freereq(crp);
out:
#ifdef FCRYPTO_DEBUG
	if (error)
		printf("%s: returning error %d\n", __FUNCTION__, error);
#endif
	if (input_sessionp == NULL) {
		freebsd_crypt_freesession(session);
		kmem_free(session, sizeof (*session));
	}
	return (error);
}

#else
int
freebsd_crypt_newsession(freebsd_crypt_session_t *sessp,
    const struct zio_crypt_info *c_info, crypto_key_t *key)
{
	struct cryptoini cria, crie, *crip;
	struct enc_xform *xform;
	struct auth_hash *xauth;
	int error = 0;
	crypto_session_t sid;

#ifdef FCRYPTO_DEBUG
	printf("%s(%p, { %s, %d, %d, %s }, { %p, %u })\n",
	    __FUNCTION__, sessp,
	    c_info->ci_algname, c_info->ci_crypt_type,
	    (unsigned int)c_info->ci_keylen, c_info->ci_name,
	    key->ck_data, (unsigned int)key->ck_length);
	printf("\tkey = { ");
	for (int i = 0; i < key->ck_length / 8; i++) {
		uint8_t *b = (uint8_t *)key->ck_data;
		printf("%02x ", b[i]);
	}
	printf("}\n");
#endif
	switch (c_info->ci_crypt_type) {
	case ZC_TYPE_GCM:
		xform = &enc_xform_aes_nist_gcm;
		switch (key->ck_length/8) {
		case AES_128_GMAC_KEY_LEN:
			xauth = &auth_hash_nist_gmac_aes_128;
			break;
		case AES_192_GMAC_KEY_LEN:
			xauth = &auth_hash_nist_gmac_aes_192;
			break;
		case AES_256_GMAC_KEY_LEN:
			xauth = &auth_hash_nist_gmac_aes_256;
			break;
		default:
			error = EINVAL;
			goto bad;
		}
		break;
	case ZC_TYPE_CCM:
		xform = &enc_xform_ccm;
		switch (key->ck_length/8) {
		case AES_128_CBC_MAC_KEY_LEN:
			xauth = &auth_hash_ccm_cbc_mac_128;
			break;
		case AES_192_CBC_MAC_KEY_LEN:
			xauth = &auth_hash_ccm_cbc_mac_192;
			break;
		case AES_256_CBC_MAC_KEY_LEN:
			xauth = &auth_hash_ccm_cbc_mac_256;
			break;
		default:
			error = EINVAL;
			goto bad;
			break;
		}
		break;
	default:
		error = ENOTSUP;
		goto bad;
	}
#ifdef FCRYPTO_DEBUG
	printf("%s(%d): Using crypt %s (key length %u [%u bytes]), "
	    "auth %s (key length %d)\n",
	    __FUNCTION__, __LINE__,
	    xform->name, (unsigned int)key->ck_length,
	    (unsigned int)key->ck_length/8,
	    xauth->name, xauth->keysize);
#endif

	bzero(&crie, sizeof (crie));
	bzero(&cria, sizeof (cria));

	crie.cri_alg = xform->type;
	crie.cri_key = key->ck_data;
	crie.cri_klen = key->ck_length;

	cria.cri_alg = xauth->type;
	cria.cri_key = key->ck_data;
	cria.cri_klen = key->ck_length;

	cria.cri_next = &crie;
	crie.cri_next = NULL;
	crip = &cria;
	// Everything else is bzero'd

	error = crypto_newsession(&sid, crip,
	    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE);
	if (error != 0) {
		printf("%s(%d):  crypto_newsession failed with %d\n",
		    __FUNCTION__, __LINE__, error);
		goto bad;
	}
	sessp->fs_sid = sid;
	mtx_init(&sessp->fs_lock, "FreeBSD Cryptographic Session Lock",
	    NULL, MTX_DEF);
	crypt_sessions++;
bad:
	return (error);
}

/*
 * The meat of encryption/decryption.
 * If sessp is NULL, then it will create a
 * temporary cryptographic session, and release
 * it when done.
 */
int
freebsd_crypt_uio(boolean_t encrypt,
    freebsd_crypt_session_t *input_sessionp,
    const struct zio_crypt_info *c_info,
    zfs_uio_t *data_uio,
    crypto_key_t *key,
    uint8_t *ivbuf,
    size_t datalen,
    size_t auth_len)
{
	struct cryptop *crp;
	struct cryptodesc *enc_desc, *auth_desc;
	struct enc_xform *xform;
	struct auth_hash *xauth;
	freebsd_crypt_session_t *session = NULL;
	int error;

	freebsd_crypt_uio_debug_log(encrypt, input_sessionp, c_info, data_uio,
	    key, ivbuf, datalen, auth_len);
	switch (c_info->ci_crypt_type) {
	case ZC_TYPE_GCM:
		xform = &enc_xform_aes_nist_gcm;
		switch (key->ck_length/8) {
		case AES_128_GMAC_KEY_LEN:
			xauth = &auth_hash_nist_gmac_aes_128;
			break;
		case AES_192_GMAC_KEY_LEN:
			xauth = &auth_hash_nist_gmac_aes_192;
			break;
		case AES_256_GMAC_KEY_LEN:
			xauth = &auth_hash_nist_gmac_aes_256;
			break;
		default:
			error = EINVAL;
			goto bad;
		}
		break;
	case ZC_TYPE_CCM:
		xform = &enc_xform_ccm;
		switch (key->ck_length/8) {
		case AES_128_CBC_MAC_KEY_LEN:
			xauth = &auth_hash_ccm_cbc_mac_128;
			break;
		case AES_192_CBC_MAC_KEY_LEN:
			xauth = &auth_hash_ccm_cbc_mac_192;
			break;
		case AES_256_CBC_MAC_KEY_LEN:
			xauth = &auth_hash_ccm_cbc_mac_256;
			break;
		default:
			error = EINVAL;
			goto bad;
			break;
		}
		break;
	default:
		error = ENOTSUP;
		goto bad;
	}

#ifdef FCRYPTO_DEBUG
	printf("%s(%d): Using crypt %s (key length %u [%u bytes]), "
	    "auth %s (key length %d)\n",
	    __FUNCTION__, __LINE__,
	    xform->name, (unsigned int)key->ck_length,
	    (unsigned int)key->ck_length/8,
	    xauth->name, xauth->keysize);
#endif

	if (input_sessionp == NULL) {
		session = kmem_zalloc(sizeof (*session), KM_SLEEP);
		error = freebsd_crypt_newsession(session, c_info, key);
		if (error)
			goto out;
	} else
		session = input_sessionp;

	crp = crypto_getreq(2);
	if (crp == NULL) {
		error = ENOMEM;
		goto bad;
	}

	auth_desc = crp->crp_desc;
	enc_desc = auth_desc->crd_next;

	crp->crp_session = session->fs_sid;
	crp->crp_ilen = auth_len + datalen;
	crp->crp_buf = (void*)GET_UIO_STRUCT(data_uio);
	crp->crp_flags = CRYPTO_F_IOV | CRYPTO_F_CBIFSYNC;

	auth_desc->crd_skip = 0;
	auth_desc->crd_len = auth_len;
	auth_desc->crd_inject = auth_len + datalen;
	auth_desc->crd_alg = xauth->type;
#ifdef FCRYPTO_DEBUG
	printf("%s: auth: skip = %u, len = %u, inject = %u\n",
	    __FUNCTION__, auth_desc->crd_skip, auth_desc->crd_len,
	    auth_desc->crd_inject);
#endif

	enc_desc->crd_skip = auth_len;
	enc_desc->crd_len = datalen;
	enc_desc->crd_inject = auth_len;
	enc_desc->crd_alg = xform->type;
	enc_desc->crd_flags = CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
	bcopy(ivbuf, enc_desc->crd_iv, ZIO_DATA_IV_LEN);
	enc_desc->crd_next = NULL;

#ifdef FCRYPTO_DEBUG
	printf("%s: enc: skip = %u, len = %u, inject = %u\n",
	    __FUNCTION__, enc_desc->crd_skip, enc_desc->crd_len,
	    enc_desc->crd_inject);
#endif

	if (encrypt)
		enc_desc->crd_flags |= CRD_F_ENCRYPT;

	error = zfs_crypto_dispatch(session, crp);
	crypto_freereq(crp);
out:
	if (input_sessionp == NULL) {
		freebsd_crypt_freesession(session);
		kmem_free(session, sizeof (*session));
	}
bad:
#ifdef FCRYPTO_DEBUG
	if (error)
		printf("%s: returning error %d\n", __FUNCTION__, error);
#endif
	return (error);
}
#endif
