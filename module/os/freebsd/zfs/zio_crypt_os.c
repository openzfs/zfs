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

void
zio_crypt_key_close_os(zio_crypt_key_t *key)
{
	freebsd_crypt_freesession(&key->zk_current_sess.zs_sess);
}

int
zio_crypt_key_open_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	return (freebsd_crypt_newsession(&key->zk_current_sess.zs_sess, ci,
	    &key->zk_current_key));
}

int
zio_crypt_key_reopen_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci)
{
	freebsd_crypt_freesession(&key->zk_current_sess.zs_sess);
	return (freebsd_crypt_newsession(&key->zk_current_sess.zs_sess, ci,
	    &key->zk_current_key));
}

int
zio_crypt_uios_init_os(zfs_uio_t *u1, zfs_uio_t *u2, int iovcnt, int *idx)
{
	/*
	 * One additional iovec at start for a single AAD buffer, and one
	 * at end for the MAC.
	 */
	iovcnt += 2;

	zfs_uio_init(u1, allocuio(iovcnt));
	zfs_uio_init(u2, allocuio(iovcnt));

	zfs_uio_iovcnt(u1) = zfs_uio_iovcnt(u2) = iovcnt;
	zfs_uio_segflg(u1) = zfs_uio_segflg(u2) = UIO_SYSSPACE;

	/*
	 * freebsd_crypt_uio() expects AD in slot 0, so first data iovec is
	 * slot 1.
	 */
	*idx = 1;

	return (0);
}

void
zio_crypt_uios_fini_os(zfs_uio_t *u1, zfs_uio_t *u2) {
	ASSERT3U(zfs_uio_iovcnt(u1), ==, zfs_uio_iovcnt(u2));
	ASSERT3U(zfs_uio_iovcnt(u1), >=, 2);

	freeuio(GET_UIO_STRUCT(u1));
	freeuio(GET_UIO_STRUCT(u2));
}

static int
zio_encrypt_decrypt_os_common(boolean_t do_encrypt, const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *src, zfs_uio_t *dst, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN])
{
	ASSERT3U(zfs_uio_iovcnt(src), ==, zfs_uio_iovcnt(dst));
	ASSERT3U(zfs_uio_iovcnt(src), >=, 2);

	/*
	 * Our FreeBSD kernel crypto shim (crypto_os.c) currently only supports
	 * a single buffer for input and output. So we copy the input to the
	 * output here, and then work on the output buffers.
	 */
	for (int i = 1; i < zfs_uio_iovcnt(src)-1; i++) {
		ASSERT3U(zfs_uio_iovlen(src, i), ==,
		    zfs_uio_iovlen(dst, i));
		memcpy(zfs_uio_iovbase(dst, i),
		    zfs_uio_iovbase(src, i), zfs_uio_iovlen(src, i));
	}

	/* Shim expects AAD in the first slot */
	zfs_uio_iovbase(dst, 0) = (void *)(uintptr_t)ad;
	zfs_uio_iovlen(dst, 0) = adlen;

	/* And MAC in the last slot */
	zfs_uio_iovbase(dst, zfs_uio_iovcnt(dst)-1) = mac;
	zfs_uio_iovlen(dst, zfs_uio_iovcnt(dst)-1) = ZIO_DATA_MAC_LEN;

	freebsd_crypt_session_t *fsess = sess != NULL ? &sess->zs_sess : NULL;
	int ret = freebsd_crypt_uio(do_encrypt, fsess,
	    ci, dst, key, (uint8_t *)(uintptr_t)iv, datalen, adlen);
	if (ret != 0) {
#ifdef FCRYPTO_DEBUG
		printf("%s(%d):  Returning error %s\n",
		    __FUNCTION__, __LINE__, do_encrypt ? "EIO" : "ECKSUM");
#endif
		ret = SET_ERROR(do_encrypt ? EIO : ECKSUM);
	}

	return (ret);
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
	crypto_mac(&key->zk_hmac_key, data, datalen, digest, SHA512_HMAC_LEN);
	return (0);
}

int
zio_crypt_hmac_init_os(zio_crypt_hmac_t *hmac, zio_crypt_key_t *key)
{
	crypto_mac_init(&hmac->zh_ctx, &key->zk_hmac_key);
	return (0);
}

int
zio_crypt_hmac_update_os(zio_crypt_hmac_t *hmac, const uint8_t *data,
    size_t datalen)
{
	crypto_mac_update(&hmac->zh_ctx, data, datalen);
	return (0);
}

int
zio_crypt_hmac_final_os(zio_crypt_hmac_t *hmac,
    uint8_t digest[SHA512_HMAC_LEN])
{
	crypto_mac_final(&hmac->zh_ctx, digest, SHA512_HMAC_LEN);
	return (0);
}
