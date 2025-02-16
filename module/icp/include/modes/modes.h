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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_COMMON_CRYPTO_MODES_H
#define	_COMMON_CRYPTO_MODES_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * Does the build chain support all instructions needed for the GCM assembler
 * routines. AVX support should imply AES-NI and PCLMULQDQ, but make sure
 * anyhow.
 */
#if defined(__x86_64__) && defined(HAVE_AVX) && \
    defined(HAVE_AES) && defined(HAVE_PCLMULQDQ)
#define	CAN_USE_GCM_ASM (HAVE_VAES && HAVE_VPCLMULQDQ ? 2 : 1)
extern boolean_t gcm_avx_can_use_movbe;
#endif

#define	CCM_MODE			0x00000010
#define	GCM_MODE			0x00000020

/*
 * cc_keysched:		Pointer to key schedule.
 *
 * cc_keysched_len:	Length of the key schedule.
 *
 * cc_remainder:	This is for residual data, i.e. data that can't
 *			be processed because there are too few bytes.
 *			Must wait until more data arrives.
 *
 * cc_remainder_len:	Number of bytes in cc_remainder.
 *
 * cc_iv:		Scratch buffer that sometimes contains the IV.
 *
 * cc_lastp:		Pointer to previous block of ciphertext.
 *
 * cc_copy_to:		Pointer to where encrypted residual data needs
 *			to be copied.
 *
 * cc_flags:		PROVIDER_OWNS_KEY_SCHEDULE
 *			When a context is freed, it is necessary
 *			to know whether the key schedule was allocated
 *			by the caller, or internally, e.g. an init routine.
 *			If allocated by the latter, then it needs to be freed.
 *
 *			CCM_MODE
 */
struct common_ctx {
	void *cc_keysched;
	size_t cc_keysched_len;
	uint64_t cc_iv[2];
	uint64_t cc_remainder[2];
	size_t cc_remainder_len;
	uint8_t *cc_lastp;
	uint8_t *cc_copy_to;
	uint32_t cc_flags;
};

typedef struct common_ctx common_ctx_t;

/*
 *
 * ccm_mac_len:		Stores length of the MAC in CCM mode.
 * ccm_mac_buf:		Stores the intermediate value for MAC in CCM encrypt.
 *			In CCM decrypt, stores the input MAC value.
 * ccm_data_len:	Length of the plaintext for CCM mode encrypt, or
 *			length of the ciphertext for CCM mode decrypt.
 * ccm_processed_data_len:
 *			Length of processed plaintext in CCM mode encrypt,
 *			or length of processed ciphertext for CCM mode decrypt.
 * ccm_processed_mac_len:
 *			Length of MAC data accumulated in CCM mode decrypt.
 *
 * ccm_pt_buf:		Only used in CCM mode decrypt.  It stores the
 *			decrypted plaintext to be returned when
 *			MAC verification succeeds in decrypt_final.
 *			Memory for this should be allocated in the AES module.
 *
 */
typedef struct ccm_ctx {
	struct common_ctx ccm_common;
	uint32_t ccm_tmp[4];
	size_t ccm_mac_len;
	uint64_t ccm_mac_buf[2];
	size_t ccm_data_len;
	size_t ccm_processed_data_len;
	size_t ccm_processed_mac_len;
	uint8_t *ccm_pt_buf;
	uint64_t ccm_mac_input_buf[2];
	uint64_t ccm_counter_mask;
} ccm_ctx_t;

#define	ccm_keysched		ccm_common.cc_keysched
#define	ccm_keysched_len	ccm_common.cc_keysched_len
#define	ccm_cb			ccm_common.cc_iv
#define	ccm_remainder		ccm_common.cc_remainder
#define	ccm_remainder_len	ccm_common.cc_remainder_len
#define	ccm_lastp		ccm_common.cc_lastp
#define	ccm_copy_to		ccm_common.cc_copy_to
#define	ccm_flags		ccm_common.cc_flags

#ifdef CAN_USE_GCM_ASM
typedef enum gcm_impl {
	GCM_IMPL_GENERIC = 0,
	GCM_IMPL_AVX,
	GCM_IMPL_AVX2,
	GCM_IMPL_MAX,
} gcm_impl;
#endif

/*
 * gcm_tag_len:		Length of authentication tag.
 *
 * gcm_ghash:		Stores output from the GHASH function.
 *
 * gcm_processed_data_len:
 *			Length of processed plaintext (encrypt) or
 *			length of processed ciphertext (decrypt).
 *
 * gcm_pt_buf:		Stores the decrypted plaintext returned by
 *			decrypt_final when the computed authentication
 *			tag matches the	user supplied tag.
 *
 * gcm_pt_buf_len:	Length of the plaintext buffer.
 *
 * gcm_H:		Subkey.
 *
 * gcm_Htable:		Pre-computed and pre-shifted H, H^2, ... H^6 for the
 *			Karatsuba Algorithm in host byte order.
 *
 * gcm_J0:		Pre-counter block generated from the IV.
 *
 * gcm_len_a_len_c:	64-bit representations of the bit lengths of
 *			AAD and ciphertext.
 */
typedef struct gcm_ctx {
	struct common_ctx gcm_common;
	size_t gcm_tag_len;
	size_t gcm_processed_data_len;
	size_t gcm_pt_buf_len;
	uint32_t gcm_tmp[4];
	/*
	 * The offset of gcm_Htable relative to gcm_ghash, (32), is hard coded
	 * in aesni-gcm-x86_64.S, so please don't change (or adjust there).
	 */
	uint64_t gcm_ghash[2];
	uint64_t gcm_H[2];
#ifdef CAN_USE_GCM_ASM
	uint64_t *gcm_Htable;
	size_t gcm_htab_len;
#endif
	uint64_t gcm_J0[2];
	uint64_t gcm_len_a_len_c[2];
	uint8_t *gcm_pt_buf;
#ifdef CAN_USE_GCM_ASM
	enum gcm_impl impl;
#endif
} gcm_ctx_t;

#define	gcm_keysched		gcm_common.cc_keysched
#define	gcm_keysched_len	gcm_common.cc_keysched_len
#define	gcm_cb			gcm_common.cc_iv
#define	gcm_remainder		gcm_common.cc_remainder
#define	gcm_remainder_len	gcm_common.cc_remainder_len
#define	gcm_lastp		gcm_common.cc_lastp
#define	gcm_copy_to		gcm_common.cc_copy_to
#define	gcm_flags		gcm_common.cc_flags

void gcm_clear_ctx(gcm_ctx_t *ctx);

typedef struct aes_ctx {
	union {
		ccm_ctx_t acu_ccm;
		gcm_ctx_t acu_gcm;
	} acu;
} aes_ctx_t;

#define	ac_flags		acu.acu_ccm.ccm_common.cc_flags
#define	ac_remainder_len	acu.acu_ccm.ccm_common.cc_remainder_len
#define	ac_keysched		acu.acu_ccm.ccm_common.cc_keysched
#define	ac_keysched_len		acu.acu_ccm.ccm_common.cc_keysched_len
#define	ac_iv			acu.acu_ccm.ccm_common.cc_iv
#define	ac_lastp		acu.acu_ccm.ccm_common.cc_lastp
#define	ac_pt_buf		acu.acu_ccm.ccm_pt_buf
#define	ac_mac_len		acu.acu_ccm.ccm_mac_len
#define	ac_data_len		acu.acu_ccm.ccm_data_len
#define	ac_processed_mac_len	acu.acu_ccm.ccm_processed_mac_len
#define	ac_processed_data_len	acu.acu_ccm.ccm_processed_data_len
#define	ac_tag_len		acu.acu_gcm.gcm_tag_len

extern int ccm_mode_encrypt_contiguous_blocks(ccm_ctx_t *, char *, size_t,
    crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int ccm_mode_decrypt_contiguous_blocks(ccm_ctx_t *, char *, size_t,
    crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int gcm_mode_encrypt_contiguous_blocks(gcm_ctx_t *, char *, size_t,
    crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int gcm_mode_decrypt_contiguous_blocks(gcm_ctx_t *, char *, size_t,
    crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

int ccm_encrypt_final(ccm_ctx_t *, crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

int gcm_encrypt_final(gcm_ctx_t *, crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int ccm_decrypt_final(ccm_ctx_t *, crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int gcm_decrypt_final(gcm_ctx_t *, crypto_data_t *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int ccm_init_ctx(ccm_ctx_t *, char *, int, boolean_t, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern int gcm_init_ctx(gcm_ctx_t *, char *, size_t,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *));

extern void calculate_ccm_mac(ccm_ctx_t *, uint8_t *,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *));

extern void gcm_mul(uint64_t *, uint64_t *, uint64_t *);

extern void crypto_init_ptrs(crypto_data_t *, void **, offset_t *);
extern void crypto_get_ptrs(crypto_data_t *, void **, offset_t *,
    uint8_t **, size_t *, uint8_t **, size_t);

extern void *ccm_alloc_ctx(int);
extern void *gcm_alloc_ctx(int);
extern void crypto_free_mode_ctx(void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _COMMON_CRYPTO_MODES_H */
