/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef	_SYS_SHA2_H
#define	_SYS_SHA2_H

#ifdef  _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	SHA224_BLOCK_LENGTH		64
#define	SHA256_BLOCK_LENGTH		64
#define	SHA384_BLOCK_LENGTH		128
#define	SHA512_BLOCK_LENGTH		128

#define	SHA224_DIGEST_LENGTH		28
#define	SHA256_DIGEST_LENGTH		32
#define	SHA384_DIGEST_LENGTH		48
#define	SHA512_DIGEST_LENGTH		64

#define	SHA512_224_DIGEST_LENGTH	28
#define	SHA512_256_DIGEST_LENGTH	32

#define	SHA256_HMAC_BLOCK_SIZE		64
#define	SHA512_HMAC_BLOCK_SIZE		128

/* sha256 context */
typedef struct {
	uint32_t state[8];
	uint64_t count[2];
	uint8_t wbuf[64];

	/* const sha256_ops_t *ops */
	const void *ops;
} sha256_ctx;

/* sha512 context */
typedef struct {
	uint64_t state[8];
	uint64_t count[2];
	uint8_t wbuf[128];

	/* const sha256_ops_t *ops */
	const void *ops;
} sha512_ctx;

/* SHA2 context */
typedef struct {
	union {
		sha256_ctx sha256;
		sha512_ctx sha512;
	};

	/* algorithm type */
	int algotype;
} SHA2_CTX;

/* SHA2 algorithm types */
typedef enum sha2_mech_type {
	SHA256_MECH_INFO_TYPE,		/* SUN_CKM_SHA256 */
	SHA256_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA256_HMAC */
	SHA256_HMAC_GEN_MECH_INFO_TYPE,	/* SUN_CKM_SHA256_HMAC_GENERAL */
	SHA384_MECH_INFO_TYPE,		/* SUN_CKM_SHA384 */
	SHA384_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA384_HMAC */
	SHA384_HMAC_GEN_MECH_INFO_TYPE,	/* SUN_CKM_SHA384_HMAC_GENERAL */
	SHA512_MECH_INFO_TYPE,		/* SUN_CKM_SHA512 */
	SHA512_HMAC_MECH_INFO_TYPE,	/* SUN_CKM_SHA512_HMAC */
	SHA512_HMAC_GEN_MECH_INFO_TYPE,	/* SUN_CKM_SHA512_HMAC_GENERAL */
	SHA512_224_MECH_INFO_TYPE,	/* SUN_CKM_SHA512_224 */
	SHA512_256_MECH_INFO_TYPE	/* SUN_CKM_SHA512_256 */
} sha2_mech_type_t;

#define	SHA256			0
#define	SHA256_HMAC		1
#define	SHA256_HMAC_GEN		2
#define	SHA384			3
#define	SHA384_HMAC		4
#define	SHA384_HMAC_GEN		5
#define	SHA512			6
#define	SHA512_HMAC		7
#define	SHA512_HMAC_GEN		8
#define	SHA512_224		9
#define	SHA512_256		10

/* SHA2 Init function */
extern void SHA2Init(int algotype, SHA2_CTX *ctx);

/* SHA2 Update function */
extern void SHA2Update(SHA2_CTX *ctx, const void *data, size_t len);

/* SHA2 Final function */
extern void SHA2Final(void *digest, SHA2_CTX *ctx);

#ifdef __cplusplus
}
#endif

#endif	/* SYS_SHA2_H */
