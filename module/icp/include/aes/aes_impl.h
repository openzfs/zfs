/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_AES_IMPL_H
#define	_AES_IMPL_H

/*
 * Common definitions used by AES.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

/* Similar to sysmacros.h IS_P2ALIGNED, but checks two pointers: */
#define	IS_P2ALIGNED2(v, w, a) \
	((((uintptr_t)(v) | (uintptr_t)(w)) & ((uintptr_t)(a) - 1)) == 0)

#define	AES_BLOCK_LEN	16	/* bytes */
/* Round constant length, in number of 32-bit elements: */
#define	RC_LENGTH	(5 * ((AES_BLOCK_LEN) / 4 - 2))

#define	AES_COPY_BLOCK(src, dst) \
	(dst)[0] = (src)[0]; \
	(dst)[1] = (src)[1]; \
	(dst)[2] = (src)[2]; \
	(dst)[3] = (src)[3]; \
	(dst)[4] = (src)[4]; \
	(dst)[5] = (src)[5]; \
	(dst)[6] = (src)[6]; \
	(dst)[7] = (src)[7]; \
	(dst)[8] = (src)[8]; \
	(dst)[9] = (src)[9]; \
	(dst)[10] = (src)[10]; \
	(dst)[11] = (src)[11]; \
	(dst)[12] = (src)[12]; \
	(dst)[13] = (src)[13]; \
	(dst)[14] = (src)[14]; \
	(dst)[15] = (src)[15]

#define	AES_XOR_BLOCK(src, dst) \
	(dst)[0] ^= (src)[0]; \
	(dst)[1] ^= (src)[1]; \
	(dst)[2] ^= (src)[2]; \
	(dst)[3] ^= (src)[3]; \
	(dst)[4] ^= (src)[4]; \
	(dst)[5] ^= (src)[5]; \
	(dst)[6] ^= (src)[6]; \
	(dst)[7] ^= (src)[7]; \
	(dst)[8] ^= (src)[8]; \
	(dst)[9] ^= (src)[9]; \
	(dst)[10] ^= (src)[10]; \
	(dst)[11] ^= (src)[11]; \
	(dst)[12] ^= (src)[12]; \
	(dst)[13] ^= (src)[13]; \
	(dst)[14] ^= (src)[14]; \
	(dst)[15] ^= (src)[15]

/* AES key size definitions */
#define	AES_MINBITS		128
#define	AES_MINBYTES		((AES_MINBITS) >> 3)
#define	AES_MAXBITS		256
#define	AES_MAXBYTES		((AES_MAXBITS) >> 3)

#define	AES_MIN_KEY_BYTES	((AES_MINBITS) >> 3)
#define	AES_MAX_KEY_BYTES	((AES_MAXBITS) >> 3)
#define	AES_192_KEY_BYTES	24
#define	AES_IV_LEN		16

/* AES key schedule may be implemented with 32- or 64-bit elements: */
#define	AES_32BIT_KS		32
#define	AES_64BIT_KS		64

#define	MAX_AES_NR		14 /* Maximum number of rounds */
#define	MAX_AES_NB		4  /* Number of columns comprising a state */

typedef union {
#ifdef	sun4u
	uint64_t	ks64[((MAX_AES_NR) + 1) * (MAX_AES_NB)];
#endif
	uint32_t	ks32[((MAX_AES_NR) + 1) * (MAX_AES_NB)];
} aes_ks_t;

typedef struct aes_impl_ops aes_impl_ops_t;

/*
 * The absolute offset of the encr_ks (0) and the nr (504) fields are hard
 * coded in aesni-gcm-x86_64, so please don't change (or adjust accordingly).
 */
typedef struct aes_key aes_key_t;
struct aes_key {
	aes_ks_t	encr_ks;  /* encryption key schedule */
	aes_ks_t	decr_ks;  /* decryption key schedule */
#ifdef __amd64
	long double	align128; /* Align fields above for Intel AES-NI */
#endif	/* __amd64 */
	const aes_impl_ops_t	*ops;	/* ops associated with this schedule */
	int		nr;	  /* number of rounds (10, 12, or 14) */
	int		type;	  /* key schedule size (32 or 64 bits) */
};

/*
 * Core AES functions.
 * ks and keysched are pointers to aes_key_t.
 * They are declared void* as they are intended to be opaque types.
 * Use function aes_alloc_keysched() to allocate memory for ks and keysched.
 */
extern void *aes_alloc_keysched(size_t *size, int kmflag);
extern void aes_init_keysched(const uint8_t *cipherKey, uint_t keyBits,
	void *keysched);
extern int aes_encrypt_block(const void *ks, const uint8_t *pt, uint8_t *ct);
extern int aes_decrypt_block(const void *ks, const uint8_t *ct, uint8_t *pt);

/*
 * AES mode functions.
 * The first 2 functions operate on 16-byte AES blocks.
 */
extern void aes_copy_block(uint8_t *in, uint8_t *out);
extern void aes_xor_block(uint8_t *data, uint8_t *dst);

/* Note: ctx is a pointer to aes_ctx_t defined in modes.h */
extern int aes_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out);
extern int aes_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out);

/*
 * The following definitions and declarations are only used by AES FIPS POST
 */
#ifdef _AES_IMPL

typedef enum aes_mech_type {
	AES_ECB_MECH_INFO_TYPE,		/* SUN_CKM_AES_ECB */
	AES_CBC_MECH_INFO_TYPE,		/* SUN_CKM_AES_CBC */
	AES_CBC_PAD_MECH_INFO_TYPE,	/* SUN_CKM_AES_CBC_PAD */
	AES_CTR_MECH_INFO_TYPE,		/* SUN_CKM_AES_CTR */
	AES_CCM_MECH_INFO_TYPE,		/* SUN_CKM_AES_CCM */
	AES_GCM_MECH_INFO_TYPE,		/* SUN_CKM_AES_GCM */
	AES_GMAC_MECH_INFO_TYPE		/* SUN_CKM_AES_GMAC */
} aes_mech_type_t;

#endif /* _AES_IMPL */

/*
 * Methods used to define AES implementation
 *
 * @aes_gen_f Key generation
 * @aes_enc_f Function encrypts one block
 * @aes_dec_f Function decrypts one block
 * @aes_will_work_f Function tests whether method will function
 */
typedef void 		(*aes_generate_f)(aes_key_t *, const uint32_t *, int);
typedef void		(*aes_encrypt_f)(const uint32_t[], int,
    const uint32_t[4], uint32_t[4]);
typedef void		(*aes_decrypt_f)(const uint32_t[], int,
    const uint32_t[4], uint32_t[4]);
typedef boolean_t	(*aes_will_work_f)(void);

#define	AES_IMPL_NAME_MAX (16)

struct aes_impl_ops {
	aes_generate_f generate;
	aes_encrypt_f encrypt;
	aes_decrypt_f decrypt;
	aes_will_work_f is_supported;
	boolean_t needs_byteswap;
	char name[AES_IMPL_NAME_MAX];
};

extern const aes_impl_ops_t aes_generic_impl;
#if defined(__x86_64)
extern const aes_impl_ops_t aes_x86_64_impl;

/* These functions are used to execute amd64 instructions for AMD or Intel: */
extern int rijndael_key_setup_enc_amd64(uint32_t rk[],
	const uint32_t cipherKey[], int keyBits);
extern int rijndael_key_setup_dec_amd64(uint32_t rk[],
	const uint32_t cipherKey[], int keyBits);
extern void aes_encrypt_amd64(const uint32_t rk[], int Nr,
	const uint32_t pt[4], uint32_t ct[4]);
extern void aes_decrypt_amd64(const uint32_t rk[], int Nr,
	const uint32_t ct[4], uint32_t pt[4]);
#endif
#if defined(__x86_64) && defined(HAVE_AES)
extern const aes_impl_ops_t aes_aesni_impl;
#endif

/*
 * Initializes fastest implementation
 */
void aes_impl_init(void);

/*
 * Returns optimal allowed AES implementation
 */
const struct aes_impl_ops *aes_impl_get_ops(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _AES_IMPL_H */
