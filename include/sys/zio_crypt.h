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

#ifndef	_SYS_ZIO_CRYPT_H
#define	_SYS_ZIO_CRYPT_H

#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/zio_crypt_os.h>

#define	WRAPPING_KEY_LEN	32
#define	WRAPPING_IV_LEN		ZIO_DATA_IV_LEN
#define	WRAPPING_MAC_LEN	ZIO_DATA_MAC_LEN
#define	MASTER_KEY_MAX_LEN	32
#define	SHA512_HMAC_KEYLEN	64
#define	SHA512_HMAC_LEN		64

#define	ZIO_CRYPT_KEY_CURRENT_VERSION	1ULL

typedef enum zio_crypt_type {
	ZC_TYPE_NONE = 0,
	ZC_TYPE_CCM,
	ZC_TYPE_GCM
} zio_crypt_type_t;

/* table of supported crypto algorithms, modes and keylengths. */
typedef struct zio_crypt_info {
	/* mechanism/algorithm name for backend to select implementation */
	const char *ci_mechname;

	/* cipher mode type (GCM, CCM) */
	zio_crypt_type_t ci_crypt_type;

	/* length of the encryption key */
	size_t ci_keylen;

	/* human-readable name of the encryption algorithm */
	const char *ci_name;
} zio_crypt_info_t;

extern const zio_crypt_info_t zio_crypt_table[ZIO_CRYPT_FUNCTIONS];

/* in memory representation of an unwrapped key that is loaded into memory */
typedef struct zio_crypt_key {
	/* encryption algorithm */
	uint64_t zk_crypt;

	/* on-disk format version */
	uint64_t zk_version;

	/* GUID for uniquely identifying this key. Not encrypted on disk. */
	uint64_t zk_guid;

	/* buffer for master key */
	uint8_t zk_master_keydata[MASTER_KEY_MAX_LEN];

	/* buffer for hmac key */
	uint8_t zk_hmac_keydata[SHA512_HMAC_KEYLEN];

	/* buffer for current encryption key derived from master key */
	uint8_t zk_current_keydata[MASTER_KEY_MAX_LEN];

	/* current 64 bit salt for deriving an encryption key */
	uint8_t zk_salt[ZIO_DATA_SALT_LEN];

	/* count of how many times the current salt has been used */
	uint64_t zk_salt_count;

	/* current raw encryption key for backend */
	crypto_key_t zk_current_key;

	/* backend template (session) for current encryption key */
	zio_crypt_session_t zk_current_sess;

	/* current raw hmac key for backend */
	crypto_key_t zk_hmac_key;

	/* backend template (session) for current hmac key */
	zio_crypt_session_t zk_hmac_sess;

	/* lock for changing the salt and dependent values */
	krwlock_t zk_salt_lock;
} zio_crypt_key_t;

void zio_crypt_key_destroy(zio_crypt_key_t *key);
int zio_crypt_key_init(uint64_t crypt, zio_crypt_key_t *key);
int zio_crypt_key_get_salt(zio_crypt_key_t *key, uint8_t *salt_out);

int zio_crypt_key_wrap(crypto_key_t *cwkey, zio_crypt_key_t *key, uint8_t *iv,
    uint8_t *mac, uint8_t *keydata_out, uint8_t *hmac_keydata_out);
int zio_crypt_key_unwrap(crypto_key_t *cwkey, uint64_t crypt, uint64_t version,
    uint64_t guid, uint8_t *keydata, uint8_t *hmac_keydata, uint8_t *iv,
    uint8_t *mac, zio_crypt_key_t *key);
int zio_crypt_generate_iv(uint8_t *ivbuf);
int zio_crypt_generate_iv_salt_dedup(zio_crypt_key_t *key, uint8_t *data,
    uint_t datalen, uint8_t *ivbuf, uint8_t *salt);

void zio_crypt_encode_params_bp(blkptr_t *bp, uint8_t *salt, uint8_t *iv);
void zio_crypt_decode_params_bp(const blkptr_t *bp, uint8_t *salt, uint8_t *iv);
void zio_crypt_encode_mac_bp(blkptr_t *bp, uint8_t *mac);
void zio_crypt_decode_mac_bp(const blkptr_t *bp, uint8_t *mac);
void zio_crypt_encode_mac_zil(void *data, uint8_t *mac);
void zio_crypt_decode_mac_zil(const void *data, uint8_t *mac);
void zio_crypt_copy_dnode_bonus(abd_t *src_abd, uint8_t *dst, uint_t datalen);

int zio_crypt_do_indirect_mac_checksum(boolean_t generate, void *buf,
    uint_t datalen, boolean_t byteswap, uint8_t *cksum);
int zio_crypt_do_indirect_mac_checksum_abd(boolean_t generate, abd_t *abd,
    uint_t datalen, boolean_t byteswap, uint8_t *cksum);
int zio_crypt_do_hmac(zio_crypt_key_t *key, uint8_t *data, uint_t datalen,
    uint8_t *digestbuf, uint_t digestlen);
int zio_crypt_do_objset_hmacs(zio_crypt_key_t *key, void *data, uint_t datalen,
    boolean_t byteswap, uint8_t *portable_mac, uint8_t *local_mac);
int zio_do_crypt_data(boolean_t encrypt, zio_crypt_key_t *key,
    dmu_object_type_t ot, boolean_t byteswap, uint8_t *salt, uint8_t *iv,
    uint8_t *mac, uint_t datalen, uint8_t *plainbuf, uint8_t *cipherbuf,
    boolean_t *no_crypt);
int zio_do_crypt_abd(boolean_t encrypt, zio_crypt_key_t *key,
    dmu_object_type_t ot, boolean_t byteswap, uint8_t *salt, uint8_t *iv,
    uint8_t *mac, uint_t datalen, abd_t *pabd, abd_t *cabd,
    boolean_t *no_crypt);

/*
 * Platform/backend interface to an arbitrary crypto suite.
 */

/*
 * A key must be opened before use, so the backend can initialise the wanted
 * algorithm and set up the backend crypto suite/hardware.
 *
 * Reopen should be done after any of the keys internal properties change,
 * eg the salt is changed. It is logically equivalent to close+open, but the
 * backend may be able to do it more efficiently.
 */
int zio_crypt_key_open_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci);
void zio_crypt_key_close_os(zio_crypt_key_t *key);
int zio_crypt_key_reopen_os(zio_crypt_key_t *key, const zio_crypt_info_t *ci);

/*
 * Initialise/free a pair of UIOs with space for iovcnt data elements. This
 * will allocate/free zfs_uio_iov(u1)/zfs_uio_iov(u2) with at least iovcnt
 * elements. All other uio fields are private to the backend.
 *
 * The backend may allocate more elements than requested; the first
 * one available for caller data is returned in *idx, and the caller should
 * not try to use beyond the *idx+iovcnt-1 element.
 */
int zio_crypt_uios_init_os(zfs_uio_t *u1, zfs_uio_t *u2, int iovcnt, int *idx);
void zio_crypt_uios_fini_os(zfs_uio_t *u1, zfs_uio_t *u2);

/* Low-level encrypt/decrypt. See zio_do_crypt_data(). */
int zio_encrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *plaintext, zfs_uio_t *ciphertext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN]);
int zio_decrypt_os(const zio_crypt_info_t *ci,
    crypto_key_t *key, zio_crypt_session_t *sess,
    zfs_uio_t *ciphertext, zfs_uio_t *plaintext, size_t datalen,
    const uint8_t iv[ZIO_DATA_IV_LEN], const uint8_t *ad, size_t adlen,
    uint8_t mac[ZIO_DATA_MAC_LEN]);

/* Generate SHA512-HMAC digest of data using the given HMAC key. */
int zio_crypt_hmac_os(zio_crypt_key_t *key, const uint8_t *data,
    size_t datalen, uint8_t digest[SHA512_HMAC_LEN]);

/*
 * Generate SHA512-HMAC digest using given key. Data is passed incrementally,
 * and the final result generated at the end.
 */
int zio_crypt_hmac_init_os(zio_crypt_hmac_t *hmac, zio_crypt_key_t *key);
int zio_crypt_hmac_update_os(zio_crypt_hmac_t *hmac, const uint8_t *data,
    size_t datalen);
int zio_crypt_hmac_final_os(zio_crypt_hmac_t *hmac,
	uint8_t digest[SHA512_HMAC_LEN]);

#endif
