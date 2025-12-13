// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2013 Saso Kiselkov.  All rights reserved.
 */

#ifndef _SYS_CRYPTO_COMMON_H
#define	_SYS_CRYPTO_COMMON_H

/*
 * Header file for the common data structures of the cryptographic framework
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/types.h>
#include <sys/uio.h>

/* Cryptographic Mechanisms */

#define	CRYPTO_MAX_MECH_NAME 32
typedef char crypto_mech_name_t[CRYPTO_MAX_MECH_NAME];

typedef uint64_t crypto_mech_type_t;

typedef struct crypto_mechanism {
	crypto_mech_type_t	cm_type;	/* mechanism type */
	caddr_t			cm_param;	/* mech. parameter */
	size_t			cm_param_len;	/* mech. parameter len */
} crypto_mechanism_t;

/* CK_AES_CCM_PARAMS provides parameters to the CKM_AES_CCM mechanism */
typedef struct CK_AES_CCM_PARAMS {
	ulong_t ulMACSize;
	ulong_t ulNonceSize;
	ulong_t ulAuthDataSize;
	ulong_t ulDataSize; /* used for plaintext or ciphertext */
	uchar_t *nonce;
	uchar_t *authData;
} CK_AES_CCM_PARAMS;

/* CK_AES_GCM_PARAMS provides parameters to the CKM_AES_GCM mechanism */
typedef struct CK_AES_GCM_PARAMS {
	uchar_t *pIv;
	ulong_t ulIvLen;
	ulong_t ulIvBits;
	uchar_t *pAAD;
	ulong_t ulAADLen;
	ulong_t ulTagBits;
} CK_AES_GCM_PARAMS;

/*
 * The measurement unit bit flag for a mechanism's minimum or maximum key size.
 * The unit are mechanism dependent.  It can be in bits or in bytes.
 */
typedef uint32_t crypto_keysize_unit_t;


/* Mechanisms supported out-of-the-box */
#define	SUN_CKM_SHA512_HMAC		"CKM_SHA512_HMAC"
#define	SUN_CKM_AES_CCM			"CKM_AES_CCM"
#define	SUN_CKM_AES_GCM			"CKM_AES_GCM"
#define	SUN_CKM_CHACHA20_POLY1305	"CKM_CHACHA20_POLY1305"

/* Data arguments of cryptographic operations */

typedef enum crypto_data_format {
	CRYPTO_DATA_RAW = 1,
	CRYPTO_DATA_UIO,
} crypto_data_format_t;

typedef struct crypto_data {
	crypto_data_format_t	cd_format;	/* Format identifier	*/
	off_t			cd_offset;	/* Offset from the beginning */
	size_t			cd_length;	/* # of bytes in use */
	union {
		/* Raw format */
		iovec_t cd_raw;		/* Pointer and length	    */

		/* uio scatter-gather format */
		zfs_uio_t	*cd_uio;
	};	/* Crypto Data Union */
} crypto_data_t;

/* The keys, and their contents */

typedef struct {
	uint_t	ck_length;	/* # of bits in ck_data   */
	void	*ck_data;	/* ptr to key value */
} crypto_key_t;

/*
 * Raw key lengths are expressed in number of bits.
 * The following macro returns the minimum number of
 * bytes that can contain the specified number of bits.
 * Round up without overflowing the integer type.
 */
#define	CRYPTO_BITS2BYTES(n) ((n) == 0 ? 0 : (((n) - 1) >> 3) + 1)
#define	CRYPTO_BYTES2BITS(n) ((n) << 3)

/* Providers */

typedef uint32_t 	crypto_provider_id_t;
#define	KCF_PROVID_INVALID	((uint32_t)-1)

/* session data structure opaque to the consumer */
typedef void *crypto_session_t;

#define	PROVIDER_OWNS_KEY_SCHEDULE	0x00000001

/*
 * Common cryptographic status and error codes.
 */
#define	CRYPTO_SUCCESS				0x00000000
#define	CRYPTO_HOST_MEMORY			0x00000002
#define	CRYPTO_FAILED				0x00000004
#define	CRYPTO_ARGUMENTS_BAD			0x00000005
#define	CRYPTO_DATA_LEN_RANGE			0x0000000C
#define	CRYPTO_ENCRYPTED_DATA_LEN_RANGE		0x00000011
#define	CRYPTO_KEY_SIZE_RANGE			0x00000013
#define	CRYPTO_KEY_TYPE_INCONSISTENT		0x00000014
#define	CRYPTO_MECHANISM_INVALID		0x0000001C
#define	CRYPTO_MECHANISM_PARAM_INVALID		0x0000001D
#define	CRYPTO_SIGNATURE_INVALID		0x0000002D
#define	CRYPTO_BUFFER_TOO_SMALL			0x00000042
#define	CRYPTO_NOT_SUPPORTED			0x00000044

#define	CRYPTO_INVALID_CONTEXT			0x00000047
#define	CRYPTO_INVALID_MAC			0x00000048
#define	CRYPTO_MECH_NOT_SUPPORTED		0x00000049
#define	CRYPTO_INVALID_PROVIDER_ID		0x0000004C
#define	CRYPTO_BUSY				0x0000004E
#define	CRYPTO_UNKNOWN_PROVIDER			0x0000004F

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_COMMON_H */
