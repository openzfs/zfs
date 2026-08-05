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

#ifndef _SYS_ZALGO_H
#define	_SYS_ZALGO_H 1

#include <sys/zfs_context.h>
#include <sys/abd.h>		/* abd_t */
#include <sys/spa_checksum.h>	/* zio_cksum_t */

void zalgo_init(void);
void zalgo_fini(void);

/* ========== */

typedef enum {
	ZG_DUMMY_0,
	ZG_DUMMY_1,
	ZG_DUMMY_2,
	ZG_DUMMY_SUBTYPE_MAX,
} zalgo_dummy_subtype_t;

typedef void* zalgo_dummy_ops_t;

typedef struct {
	const zalgo_dummy_ops_t		*zgdh_ops;
	const char			*zgdh_id;
	const char			*zgdh_desc;
} zalgo_dummy_hold_t;

int zalgo_dummy_register(zalgo_dummy_subtype_t subtype, const char *id,
    const char *desc, const zalgo_dummy_ops_t *ops);
zalgo_dummy_hold_t *zalgo_dummy_hold(zalgo_dummy_subtype_t subtype);
void zalgo_dummy_rele(zalgo_dummy_hold_t *hold);

/* ========== */

typedef enum {
	ZG_MAC_HMAC_SHA512,
	ZG_MAC_SUBTYPE_MAX,
} zalgo_mac_subtype_t;

typedef struct {
	/* XXX keylen implied here? */
	int (*zgm_op_init)(void **ctx, const uint8_t *key, size_t keylen);
	int (*zgm_op_update)(void **ctx, const uint8_t *msg, size_t msglen);
	int (*zgm_op_final)(void **ctx, uint8_t *mac);
	int (*zgm_op_once)(const uint8_t *key, size_t keylen,
	    const uint8_t *msg, size_t msglen, uint8_t *mac);
	/* XXX abd etc */
} zalgo_mac_ops_t;

typedef struct {
	const zalgo_mac_ops_t		*zgmh_ops;
	const char			*zgmh_id;
	const char			*zgmh_desc;
} zalgo_mac_hold_t;

int zalgo_mac_register(zalgo_mac_subtype_t subtype, const char *id,
    const char *desc, const zalgo_mac_ops_t *ops);
zalgo_mac_hold_t *zalgo_mac_hold(zalgo_mac_subtype_t subtype);
void zalgo_mac_rele(zalgo_mac_hold_t *hold);

#define	zalgo_mac_init(hold, ...)	\
	(hold)->zh_ops->zgm_op_init(__VA_ARGS__)
#define	zalgo_mac_update(hold, ...)	\
	(hold)->zh_ops->zgm_op_update(__VA_ARGS__)
#define	zalgo_mac_final(hold, ...)	\
	(hold)->zh_ops->zgm_op_final(__VA_ARGS__)
#define	zalgo_mac_once(hold, ...)	\
	(hold)->zh_ops->zgm_op_once(__VA_ARGS__)

/* ========== */

typedef enum {
	ZG_DIGEST_SHA256,
	ZG_DIGEST_SHA512,
	ZG_DIGEST_SHA512_256,
	ZG_DIGEST_SUBTYPE_MAX,
} zalgo_digest_subtype_t;

typedef struct {
	void (*zgd_op_init)(void **ctx);
	void (*zgd_op_update)(void **ctx, const uint8_t *msg, size_t msglen);
	void (*zgd_op_final)(void **ctx, uint8_t *digest);
	void (*zgd_op_once)(const uint8_t *msg, size_t msglen, uint8_t *digest);
	/* XXX abd etc */
} zalgo_digest_ops_t;

typedef struct {
	const zalgo_digest_ops_t	*zgdh_ops;
	const char			*zgdh_id;
	const char			*zgdh_desc;
} zalgo_digest_hold_t;

int zalgo_digest_register(zalgo_digest_subtype_t subtype, const char *id,
    const char *desc, const zalgo_digest_ops_t *ops);
zalgo_digest_hold_t *zalgo_digest_hold(zalgo_digest_subtype_t subtype);
void zalgo_digest_rele(zalgo_digest_hold_t *hold);

#define	zalgo_digest_init(hold, ...)	\
	(hold)->zh_ops->zgd_op_init(__VA_ARGS__)
#define	zalgo_digest_update(hold, ...)	\
	(hold)->zh_ops->zgd_op_update(__VA_ARGS__)
#define	zalgo_digest_final(hold, ...)	\
	(hold)->zh_ops->zgd_op_final(__VA_ARGS__)
#define	zalgo_digest_once(hold, ...)	\
	(hold)->zh_ops->zgd_op_once(__VA_ARGS__)

/* ========== */

typedef enum {
	ZG_CHECKSUM_FLETCHER2,
	ZG_CHECKSUM_FLETCHER2_SWAP,
	ZG_CHECKSUM_FLETCHER4,
	ZG_CHECKSUM_FLETCHER4_SWAP,
	ZG_CHECKSUM_SHA256,
	ZG_CHECKSUM_SHA256_SWAP,
	ZG_CHECKSUM_SHA512,
	ZG_CHECKSUM_SHA512_SWAP,
	ZG_CHECKSUM_SKEIN,
	ZG_CHECKSUM_SKEIN_SWAP,
	ZG_CHECKSUM_EDONR,
	ZG_CHECKSUM_EDONR_SWAP,
	ZG_CHECKSUM_BLAKE3,
	ZG_CHECKSUM_BLAKE3_SWAP,
	ZG_CHECKSUM_SUBTYPE_MAX,
} zalgo_checksum_subtype_t;

typedef struct {
	int (*zgc_op_init)(void **ctx);
	int (*zgc_op_update)(void **ctx, const uint8_t *data, size_t datalen);
	int (*zgc_op_update_abd)(void **ctx, abd_t *abd, size_t datalen);
	int (*zgc_op_final)(void **ctx, zio_cksum_t *checksum);
	int (*zgc_op_once)(const uint8_t *data, size_t datalen,
	    zio_cksum_t *checksum);
	int (*zgc_op_once_abd)(abd_t *abd, size_t datalen,
	    zio_cksum_t *checksum);
	/* XXX abd etc */
} zalgo_checksum_ops_t;

typedef struct {
	const zalgo_checksum_ops_t	*zgch_ops;
	const char			*zgch_id;
	const char			*zgch_desc;
} zalgo_checksum_hold_t;

int zalgo_checksum_register(zalgo_checksum_subtype_t subtype, const char *id,
    const char *desc, const zalgo_checksum_ops_t *ops);
zalgo_checksum_hold_t *zalgo_checksum_hold(zalgo_checksum_subtype_t subtype);
void zalgo_checksum_rele(zalgo_checksum_hold_t *hold);

#define	zalgo_checksum_init(hold, ...)	\
	(hold)->zh_ops->zgc_op_init(__VA_ARGS__)
#define	zalgo_checksum_update(hold, ...)	\
	(hold)->zh_ops->zgc_op_update(__VA_ARGS__)
#define	zalgo_checksum_update_abd(hold, ...)	\
	(hold)->zh_ops->zgc_op_update_abd(__VA_ARGS__)
#define	zalgo_checksum_final(hold, ...)	\
	(hold)->zh_ops->zgc_op_final(__VA_ARGS__)
#define	zalgo_checksum_once(hold, ...)	\
	(hold)->zh_ops->zgc_op_once(__VA_ARGS__)
#define	zalgo_checksum_once_abd(hold, ...)	\
	(hold)->zh_ops->zgc_op_once_abd(__VA_ARGS__)

/* ========== */

typedef enum {
	ZG_CIPHER_AES_CCM,
	ZG_CIPHER_AES_GCM,
	ZG_CIPHER_SUBTYPE_MAX,
} zalgo_cipher_subtype_t;

typedef struct {
	int (*zgc_op_open)(void **ctx, const uint8_t *key, size_t keylen);
	void (*zgc_op_close)(void **ctx);
	int (*zgc_op_encrypt)(void **ctx,
	    const uint8_t *plaintext, uint8_t *ciphertext, size_t textlen,
	    const uint8_t *iv, const uint8_t *ad, size_t adlen,
	    uint8_t *mac);
	int (*zgc_op_decrypt)(void **ctx,
	    const uint8_t *ciphertext, uint8_t *plaintext, size_t textlen,
	    const uint8_t *iv, const uint8_t *ad, size_t adlen,
	    const uint8_t *mac);
	/* XXX iovec, abd, etc */
} zalgo_cipher_ops_t;

typedef struct {
	const zalgo_cipher_ops_t	*zgch_ops;
	const char			*zgch_id;
	const char			*zgch_desc;
} zalgo_cipher_hold_t;

int zalgo_cipher_register(zalgo_cipher_subtype_t subtype, const char *id,
    const char *desc, const zalgo_cipher_ops_t *ops);
zalgo_cipher_hold_t *zalgo_cipher_hold(zalgo_cipher_subtype_t subtype);
void zalgo_cipher_rele(zalgo_cipher_hold_t *hold);

#define	zalgo_cipher_open(hold, ...)	\
	(hold)->zh_ops->zgc_op_open(__VA_ARGS__)
#define	zalgo_cipher_close(hold, ...)	\
	(hold)->zh_ops->zgc_op_close(__VA_ARGS__)
#define	zalgo_cipher_encrypt(hold, ...)	\
	(hold)->zh_ops->zgc_op_encrypt(__VA_ARGS__)
#define	zalgo_cipher_decrypt(hold, ...)	\
	(hold)->zh_ops->zgc_op_decrypt(__VA_ARGS__)

/* ========== */

void zalgo_init(void);
void zalgo_fini(void);

int zalgo_shim_icp_register(void);
int zalgo_shim_openssl_register(void);

#endif
