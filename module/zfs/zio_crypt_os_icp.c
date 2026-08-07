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
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zio_crypt.h>
#include <sys/zalgo.h>

/*
 * XXX this is the remnants of zio_crypt_os_icp, just to handle UIO<->buffer
 *     conversion and temp session establishment for zalgo. in the longer
 *     term, zalgo_cipher would gain a scatterbuf API, and session management
 *     would be moved up into zio_crypt.
 *       -- robn, 2026-08-13
 */

/* Initialise a pair of uios with the requested number of data iovecs. */
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

static size_t
uio_to_buf(zfs_uio_t *uio, uint8_t *buf)
{
	size_t p = 0;
	for (int i = 0; i < zfs_uio_iovcnt(uio); i++) {
		size_t len = zfs_uio_iovlen(uio, i);
		if (len == 0)
			continue;
		memcpy(&buf[p], zfs_uio_iovbase(uio, i), len);
		p += len;
	}
	return (p);
}

static size_t
uio_from_buf(zfs_uio_t *uio, uint8_t *buf)
{
	size_t p = 0;
	for (int i = 0; i < zfs_uio_iovcnt(uio); i++) {
		size_t len = zfs_uio_iovlen(uio, i);
		if (len == 0)
			continue;
		memcpy(zfs_uio_iovbase(uio, i), &buf[p], len);
		p += len;
	}
	return (p);
}

int
zio_encrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *plaintext, zfs_uio_t *ciphertext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	zalgo_cipher_hold_t *hold;
	void *ctx;
	if (sess) {
		hold = sess->zs_hold;
		ctx = sess->zs_ctx;
	} else {
		hold = zalgo_cipher_hold(ci->ci_cipher);
		int err = zalgo_cipher_open(hold, &ctx, key->ck_data,
		    CRYPTO_BITS2BYTES(key->ck_length));
		if (err != 0) {
			zalgo_cipher_rele(hold);
			return (err);
		}
	}

	uint8_t *pbuf = vmem_alloc(datalen, KM_SLEEP);
	uint8_t *cbuf = vmem_alloc(datalen, KM_SLEEP);

	VERIFY3U(uio_to_buf(plaintext, pbuf), ==, datalen);

	int err = zalgo_cipher_encrypt(hold, &ctx, pbuf, cbuf, datalen,
	    iv, ad, adlen, mac);
	if (err != 0)
		goto out;

	VERIFY3U(uio_from_buf(ciphertext, cbuf), ==, datalen);

out:
	if (!sess) {
		zalgo_cipher_close(hold, &ctx);
		zalgo_cipher_rele(hold);
	}

	kmem_free(pbuf, datalen);
	kmem_free(cbuf, datalen);

	return (err);
}

int
zio_decrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *ciphertext, zfs_uio_t *plaintext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	zalgo_cipher_hold_t *hold;
	void *ctx;
	if (sess) {
		hold = sess->zs_hold;
		ctx = sess->zs_ctx;
	} else {
		hold = zalgo_cipher_hold(ci->ci_cipher);
		int err = zalgo_cipher_open(hold, &ctx, key->ck_data,
		    CRYPTO_BITS2BYTES(key->ck_length));
		if (err != 0) {
			zalgo_cipher_rele(hold);
			return (err);
		}
	}

	uint8_t *cbuf = vmem_alloc(datalen, KM_SLEEP);
	uint8_t *pbuf = vmem_alloc(datalen, KM_SLEEP);

	VERIFY3U(uio_to_buf(ciphertext, cbuf), ==, datalen);

	int err = zalgo_cipher_decrypt(hold, &ctx, cbuf, pbuf, datalen,
	    iv, ad, adlen, mac);
	if (err != 0)
		goto out;

	VERIFY3U(uio_from_buf(plaintext, pbuf), ==, datalen);

out:
	kmem_free(cbuf, datalen);
	kmem_free(pbuf, datalen);

	if (!sess) {
		zalgo_cipher_close(hold, &ctx);
		zalgo_cipher_rele(hold);
	}

	return (err);
}
