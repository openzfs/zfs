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
 * Copyright (c) 2026, Walter McKelvie.
 */

/*
 * zio_crypt_os backend using the Linux kernel crypto API. Unlike the ICP,
 * the kernel has accelerated AES-GCM/CCM implementations for non-x86
 * platforms, and can route to hardware crypto drivers. The necessary
 * symbols are exported GPL-only, so this backend is only compiled when the
 * configure checks found them usable (see config/kernel-crypto.m4);
 * otherwise the ICP backend (zio_crypt_os_icp.c) is used.
 */

#include <sys/zio_crypt.h>

#ifdef HAVE_KERNEL_CRYPTO

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/scatterlist.h>
#include <crypto/aead.h>
#include <crypto/hash.h>

/*
 * Allocate a transform for the wanted crypto suite and key it. The kernel
 * precomputes the key schedule at setkey time, so a keyed transform is the
 * equivalent of an ICP session template.
 */
static struct crypto_aead *
zio_crypt_aead_create(const zio_crypt_info_t *ci, crypto_key_t *key)
{
	struct crypto_aead *aead;
	int err;

	aead = crypto_alloc_aead(
	    ci->ci_crypt_type == ZC_TYPE_GCM ? "gcm(aes)" : "ccm(aes)", 0, 0);
	if (IS_ERR(aead))
		return (aead);

	err = crypto_aead_setkey(aead, key->ck_data,
	    CRYPTO_BITS2BYTES(key->ck_length));
	if (err == 0)
		err = crypto_aead_setauthsize(aead, ZIO_DATA_MAC_LEN);
	if (err != 0) {
		crypto_free_aead(aead);
		return (ERR_PTR(err));
	}

	return (aead);
}

void
zio_crypt_key_close_os(zio_crypt_key_t *key)
{
	if (key->zk_current_sess.zs_aead != NULL)
		crypto_free_aead(key->zk_current_sess.zs_aead);
	if (key->zk_hmac_sess.zs_shash != NULL)
		crypto_free_shash(key->zk_hmac_sess.zs_shash);
}

int
zio_crypt_key_open_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	struct crypto_aead *aead;
	struct crypto_shash *shash;
	int err;

	aead = zio_crypt_aead_create(ci, &key->zk_current_key);
	if (IS_ERR(aead))
		return (SET_ERROR(EIO));

	shash = crypto_alloc_shash("hmac(sha512)", 0, 0);
	if (IS_ERR(shash)) {
		crypto_free_aead(aead);
		return (SET_ERROR(EIO));
	}

	err = crypto_shash_setkey(shash, key->zk_hmac_key.ck_data,
	    CRYPTO_BITS2BYTES(key->zk_hmac_key.ck_length));
	if (err != 0) {
		crypto_free_shash(shash);
		crypto_free_aead(aead);
		return (SET_ERROR(EIO));
	}

	key->zk_current_sess.zs_aead = aead;
	key->zk_hmac_sess.zs_shash = shash;

	return (0);
}

int
zio_crypt_key_reopen_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	(void) ci;

	/* The suite never changes, so just re-key the existing transform. */
	if (crypto_aead_setkey(key->zk_current_sess.zs_aead,
	    key->zk_current_key.ck_data,
	    CRYPTO_BITS2BYTES(key->zk_current_key.ck_length)) != 0)
		return (SET_ERROR(EIO));

	return (0);
}

/*
 * Initialise a pair of uios with the requested number of data iovecs. The
 * AAD and MAC are carried separately (see below), so no extra iovecs are
 * needed.
 */
int
zio_crypt_uios_init_os(zfs_uio_t *u1, zfs_uio_t *u2, int iovcnt, int *idx)
{
	memset(u1, 0, sizeof (zfs_uio_t));
	memset(u2, 0, sizeof (zfs_uio_t));

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

/*
 * The number of scatterlist entries needed to map buf: one for physically
 * contiguous (kmalloc) memory, or one per page for vmalloc memory.
 */
static int
zio_crypt_sg_nents(const uint8_t *buf, size_t len)
{
	if (!is_vmalloc_addr(buf))
		return (1);
	return (DIV_ROUND_UP(offset_in_page(buf) + len, PAGE_SIZE));
}

static struct scatterlist *
zio_crypt_sg_add(struct scatterlist *sg, const uint8_t *buf, size_t len)
{
	if (!is_vmalloc_addr(buf)) {
		sg_set_buf(sg, buf, len);
		return (sg_next(sg));
	}

	while (len > 0) {
		size_t n = MIN(len, PAGE_SIZE - offset_in_page(buf));
		sg_set_page(sg, vmalloc_to_page(buf), n, offset_in_page(buf));
		sg = sg_next(sg);
		buf += n;
		len -= n;
	}

	return (sg);
}

static int
zio_encrypt_decrypt_os_common(boolean_t do_encrypt, const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *src, zfs_uio_t *dst, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	struct crypto_aead *aead;
	struct aead_request *req;
	struct scatterlist *sgl, *sg;
	uint8_t *buf, *adbuf, *macbuf, *ivbuf;
	size_t buflen, sgllen, total = 0;
	int nents, i, err;
	DECLARE_CRYPTO_WAIT(wait);

	ASSERT3U(zfs_uio_iovcnt(src), ==, zfs_uio_iovcnt(dst));

	/*
	 * If the caller has a session, use its transform, otherwise create
	 * a single-use transform for this operation (eg for the wrapping
	 * key, or a temporary key derived from an older salt).
	 */
	if (sess != NULL) {
		aead = sess->zs_aead;
	} else {
		aead = zio_crypt_aead_create(ci, key);
		if (IS_ERR(aead))
			return (SET_ERROR(do_encrypt ? EIO : ECKSUM));
	}

	/*
	 * The AAD, MAC and IV may be in memory that can't be mapped into a
	 * scatterlist (eg the caller's stack), so we carry copies in a
	 * private buffer.
	 */
	buflen = adlen + ZIO_DATA_MAC_LEN + crypto_aead_ivsize(aead);
	buf = kmem_zalloc(buflen, KM_SLEEP);
	adbuf = buf;
	macbuf = adbuf + adlen;
	ivbuf = macbuf + ZIO_DATA_MAC_LEN;

	memcpy(adbuf, ad, adlen);
	if (!do_encrypt)
		memcpy(macbuf, mac, ZIO_DATA_MAC_LEN);

	if (ci->ci_crypt_type == ZC_TYPE_CCM) {
		/*
		 * CCM wants the nonce in counter block layout (RFC 3610):
		 * iv[0] = L-1, where L is the size of the length field, then
		 * the nonce itself. Our 12-byte nonce leaves L = 3.
		 */
		ivbuf[0] = 2;
		memcpy(&ivbuf[1], iv, ZIO_DATA_IV_LEN);
	} else {
		memcpy(ivbuf, iv, ZIO_DATA_IV_LEN);
	}

	/*
	 * The kernel crypto API can work out-of-place, but then requires the
	 * AAD to be copied from source to dest as part of the operation.
	 * It's simpler to copy the input to the output and work in-place.
	 */
	for (i = 0; i < zfs_uio_iovcnt(src); i++) {
		ASSERT3U(zfs_uio_iovlen(src, i), ==, zfs_uio_iovlen(dst, i));
		memcpy(zfs_uio_iovbase(dst, i), zfs_uio_iovbase(src, i),
		    zfs_uio_iovlen(src, i));
		total += zfs_uio_iovlen(dst, i);
	}
	ASSERT3U(total, ==, datalen);

	/*
	 * Build a single scatterlist covering the AAD, the data and the MAC,
	 * which is the layout the kernel AEAD transforms expect.
	 */
	nents = (adlen > 0 ? 1 : 0) + 1;
	for (i = 0; i < zfs_uio_iovcnt(dst); i++) {
		if (zfs_uio_iovlen(dst, i) > 0) {
			nents += zio_crypt_sg_nents(zfs_uio_iovbase(dst, i),
			    zfs_uio_iovlen(dst, i));
		}
	}

	sgllen = nents * sizeof (struct scatterlist);
	sgl = vmem_alloc(sgllen, KM_SLEEP);
	sg_init_table(sgl, nents);

	sg = sgl;
	if (adlen > 0)
		sg = zio_crypt_sg_add(sg, adbuf, adlen);
	for (i = 0; i < zfs_uio_iovcnt(dst); i++) {
		if (zfs_uio_iovlen(dst, i) > 0) {
			sg = zio_crypt_sg_add(sg, zfs_uio_iovbase(dst, i),
			    zfs_uio_iovlen(dst, i));
		}
	}
	sg = zio_crypt_sg_add(sg, macbuf, ZIO_DATA_MAC_LEN);
	ASSERT3P(sg, ==, NULL);

	req = aead_request_alloc(aead, GFP_KERNEL);
	if (req == NULL) {
		err = SET_ERROR(do_encrypt ? EIO : ECKSUM);
		goto out;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
	    CRYPTO_TFM_REQ_MAY_SLEEP, crypto_req_done, &wait);
	aead_request_set_ad(req, adlen);
	aead_request_set_crypt(req, sgl, sgl,
	    do_encrypt ? datalen : datalen + ZIO_DATA_MAC_LEN, ivbuf);

	if (do_encrypt) {
		err = crypto_wait_req(crypto_aead_encrypt(req), &wait);
		if (err == 0)
			memcpy(mac, macbuf, ZIO_DATA_MAC_LEN);
		else
			err = SET_ERROR(EIO);
	} else {
		err = crypto_wait_req(crypto_aead_decrypt(req), &wait);
		if (err != 0)
			err = SET_ERROR(ECKSUM);
	}

	aead_request_free(req);
out:
	vmem_free(sgl, sgllen);
	kmem_free(buf, buflen);
	if (sess == NULL)
		crypto_free_aead(aead);

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
	struct crypto_shash *shash = key->zk_hmac_sess.zs_shash;
	SHASH_DESC_ON_STACK(desc, shash);
	int err;

	desc->tfm = shash;
	err = crypto_shash_digest(desc, data, datalen, digest);
	shash_desc_zero(desc);

	if (err != 0)
		return (SET_ERROR(EIO));

	return (0);
}

static void
zio_crypt_hmac_free(zio_crypt_hmac_t *hmac)
{
	kmem_free(hmac->zh_desc, sizeof (struct shash_desc) +
	    crypto_shash_descsize(hmac->zh_desc->tfm));
	hmac->zh_desc = NULL;
}

int
zio_crypt_hmac_init_os(zio_crypt_hmac_t *hmac, zio_crypt_key_t *key)
{
	struct crypto_shash *shash = key->zk_hmac_sess.zs_shash;
	struct shash_desc *desc;

	desc = kmem_alloc(sizeof (struct shash_desc) +
	    crypto_shash_descsize(shash), KM_SLEEP);
	desc->tfm = shash;
	hmac->zh_desc = desc;

	if (crypto_shash_init(desc) != 0) {
		zio_crypt_hmac_free(hmac);
		return (SET_ERROR(EIO));
	}

	return (0);
}

int
zio_crypt_hmac_update_os(zio_crypt_hmac_t *hmac, const uint8_t *data,
    size_t datalen)
{
	if (crypto_shash_update(hmac->zh_desc, data, datalen) != 0) {
		/* the caller won't call final after an error, clean up now */
		zio_crypt_hmac_free(hmac);
		return (SET_ERROR(EIO));
	}

	return (0);
}

int
zio_crypt_hmac_final_os(zio_crypt_hmac_t *hmac, uint8_t digest[SHA512_HMAC_LEN])
{
	int err = crypto_shash_final(hmac->zh_desc, digest);

	zio_crypt_hmac_free(hmac);

	if (err != 0)
		return (SET_ERROR(EIO));

	return (0);
}

#endif /* HAVE_KERNEL_CRYPTO */
