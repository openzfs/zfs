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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/icp.h>
#include <sys/crypto/impl.h>
#include <sys/byteorder.h>
#include <sys/simd.h>
#include <modes/gcm_impl.h>
#ifdef CAN_USE_GCM_ASM
#include <aes/aes_impl.h>
#endif

#define	GHASH(c, d, t, o) \
	xor_block((uint8_t *)(d), (uint8_t *)(c)->gcm_ghash); \
	(o)->mul((uint64_t *)(void *)(c)->gcm_ghash, (c)->gcm_H, \
	(uint64_t *)(void *)(t));

/* Select GCM implementation */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX-1)
#ifdef CAN_USE_GCM_ASM
#define	IMPL_AVX	(UINT32_MAX-2)
#endif
#define	GCM_IMPL_READ(i) (*(volatile uint32_t *) &(i))
static uint32_t icp_gcm_impl = IMPL_FASTEST;
static uint32_t user_sel_impl = IMPL_FASTEST;

#ifdef CAN_USE_GCM_ASM
/* Does the architecture we run on support the MOVBE instruction? */
boolean_t gcm_avx_can_use_movbe = B_FALSE;
/*
 * Whether to use the optimized openssl gcm and ghash implementations.
 * Set to true if module parameter icp_gcm_impl == "avx".
 */
static boolean_t gcm_use_avx = B_FALSE;
#define	GCM_IMPL_USE_AVX	(*(volatile boolean_t *)&gcm_use_avx)

extern boolean_t atomic_toggle_boolean_nv(volatile boolean_t *);

static inline boolean_t gcm_avx_will_work(void);
static inline void gcm_set_avx(boolean_t);
static inline boolean_t gcm_toggle_avx(void);
static inline size_t gcm_simd_get_htab_size(boolean_t);

static int gcm_mode_encrypt_contiguous_blocks_avx(gcm_ctx_t *, char *, size_t,
    crypto_data_t *, size_t);

static int gcm_encrypt_final_avx(gcm_ctx_t *, crypto_data_t *, size_t);
static int gcm_decrypt_final_avx(gcm_ctx_t *, crypto_data_t *, size_t);
static int gcm_init_avx(gcm_ctx_t *, unsigned char *, size_t, unsigned char *,
    size_t, size_t);
#endif /* ifdef CAN_USE_GCM_ASM */

/*
 * Encrypt multiple blocks of data in GCM mode.  Decrypt for GCM mode
 * is done in another function.
 */
int
gcm_mode_encrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
#ifdef CAN_USE_GCM_ASM
	if (ctx->gcm_use_avx == B_TRUE)
		return (gcm_mode_encrypt_contiguous_blocks_avx(
		    ctx, data, length, out, block_size));
#endif

	const gcm_impl_ops_t *gops;
	size_t remainder = length;
	size_t need = 0;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *lastp;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);

	if (length + ctx->gcm_remainder_len < block_size) {
		/* accumulate bytes here and return */
		memcpy((uint8_t *)ctx->gcm_remainder + ctx->gcm_remainder_len,
		    datap,
		    length);
		ctx->gcm_remainder_len += length;
		if (ctx->gcm_copy_to == NULL) {
			ctx->gcm_copy_to = datap;
		}
		return (CRYPTO_SUCCESS);
	}

	crypto_init_ptrs(out, &iov_or_mp, &offset);

	gops = gcm_impl_get_ops();
	do {
		/* Unprocessed data from last call. */
		if (ctx->gcm_remainder_len > 0) {
			need = block_size - ctx->gcm_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			memcpy(&((uint8_t *)ctx->gcm_remainder)
			    [ctx->gcm_remainder_len], datap, need);

			blockp = (uint8_t *)ctx->gcm_remainder;
		} else {
			blockp = datap;
		}

		/*
		 * Increment counter. Counter bits are confined
		 * to the bottom 32 bits of the counter block.
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb,
		    (uint8_t *)ctx->gcm_tmp);
		xor_block(blockp, (uint8_t *)ctx->gcm_tmp);

		lastp = (uint8_t *)ctx->gcm_tmp;

		ctx->gcm_processed_data_len += block_size;

		crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
		    &out_data_1_len, &out_data_2, block_size);

		/* copy block to where it belongs */
		if (out_data_1_len == block_size) {
			copy_block(lastp, out_data_1);
		} else {
			memcpy(out_data_1, lastp, out_data_1_len);
			if (out_data_2 != NULL) {
				memcpy(out_data_2,
				    lastp + out_data_1_len,
				    block_size - out_data_1_len);
			}
		}
		/* update offset */
		out->cd_offset += block_size;

		/* add ciphertext to the hash */
		GHASH(ctx, ctx->gcm_tmp, ctx->gcm_ghash, gops);

		/* Update pointer to next block of data to be processed. */
		if (ctx->gcm_remainder_len != 0) {
			datap += need;
			ctx->gcm_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			memcpy(ctx->gcm_remainder, datap, remainder);
			ctx->gcm_remainder_len = remainder;
			ctx->gcm_copy_to = datap;
			goto out;
		}
		ctx->gcm_copy_to = NULL;

	} while (remainder > 0);
out:
	return (CRYPTO_SUCCESS);
}

int
gcm_encrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	(void) copy_block;
#ifdef CAN_USE_GCM_ASM
	if (ctx->gcm_use_avx == B_TRUE)
		return (gcm_encrypt_final_avx(ctx, out, block_size));
#endif

	const gcm_impl_ops_t *gops;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	uint8_t *ghash, *macp = NULL;
	int i, rv;

	if (out->cd_length <
	    (ctx->gcm_remainder_len + ctx->gcm_tag_len)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	gops = gcm_impl_get_ops();
	ghash = (uint8_t *)ctx->gcm_ghash;

	if (ctx->gcm_remainder_len > 0) {
		uint64_t counter;
		uint8_t *tmpp = (uint8_t *)ctx->gcm_tmp;

		/*
		 * Here is where we deal with data that is not a
		 * multiple of the block size.
		 */

		/*
		 * Increment counter.
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb,
		    (uint8_t *)ctx->gcm_tmp);

		macp = (uint8_t *)ctx->gcm_remainder;
		memset(macp + ctx->gcm_remainder_len, 0,
		    block_size - ctx->gcm_remainder_len);

		/* XOR with counter block */
		for (i = 0; i < ctx->gcm_remainder_len; i++) {
			macp[i] ^= tmpp[i];
		}

		/* add ciphertext to the hash */
		GHASH(ctx, macp, ghash, gops);

		ctx->gcm_processed_data_len += ctx->gcm_remainder_len;
	}

	ctx->gcm_len_a_len_c[1] =
	    htonll(CRYPTO_BYTES2BITS(ctx->gcm_processed_data_len));
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash, gops);
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0);
	xor_block((uint8_t *)ctx->gcm_J0, ghash);

	if (ctx->gcm_remainder_len > 0) {
		rv = crypto_put_output_data(macp, out, ctx->gcm_remainder_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}
	out->cd_offset += ctx->gcm_remainder_len;
	ctx->gcm_remainder_len = 0;
	rv = crypto_put_output_data(ghash, out, ctx->gcm_tag_len);
	if (rv != CRYPTO_SUCCESS)
		return (rv);
	out->cd_offset += ctx->gcm_tag_len;

	return (CRYPTO_SUCCESS);
}

/*
 * This will only deal with decrypting the last block of the input that
 * might not be a multiple of block length.
 */
static void
gcm_decrypt_incomplete_block(gcm_ctx_t *ctx, size_t block_size, size_t index,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *datap, *outp, *counterp;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	int i;

	/*
	 * Increment counter.
	 * Counter bits are confined to the bottom 32 bits
	 */
	counter = ntohll(ctx->gcm_cb[1] & counter_mask);
	counter = htonll(counter + 1);
	counter &= counter_mask;
	ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

	datap = (uint8_t *)ctx->gcm_remainder;
	outp = &((ctx->gcm_pt_buf)[index]);
	counterp = (uint8_t *)ctx->gcm_tmp;

	/* authentication tag */
	memset((uint8_t *)ctx->gcm_tmp, 0, block_size);
	memcpy((uint8_t *)ctx->gcm_tmp, datap, ctx->gcm_remainder_len);

	/* add ciphertext to the hash */
	GHASH(ctx, ctx->gcm_tmp, ctx->gcm_ghash, gcm_impl_get_ops());

	/* decrypt remaining ciphertext */
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb, counterp);

	/* XOR with counter block */
	for (i = 0; i < ctx->gcm_remainder_len; i++) {
		outp[i] = datap[i] ^ counterp[i];
	}
}

int
gcm_mode_decrypt_contiguous_blocks(gcm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	(void) out, (void) block_size, (void) encrypt_block, (void) copy_block,
	    (void) xor_block;
	size_t new_len;
	uint8_t *new;

	/*
	 * Copy contiguous ciphertext input blocks to plaintext buffer.
	 * Ciphertext will be decrypted in the final.
	 */
	if (length > 0) {
		new_len = ctx->gcm_pt_buf_len + length;
		new = vmem_alloc(new_len, KM_SLEEP);
		if (new == NULL) {
			vmem_free(ctx->gcm_pt_buf, ctx->gcm_pt_buf_len);
			ctx->gcm_pt_buf = NULL;
			return (CRYPTO_HOST_MEMORY);
		}

		if (ctx->gcm_pt_buf != NULL) {
			memcpy(new, ctx->gcm_pt_buf, ctx->gcm_pt_buf_len);
			vmem_free(ctx->gcm_pt_buf, ctx->gcm_pt_buf_len);
		} else {
			ASSERT0(ctx->gcm_pt_buf_len);
		}

		ctx->gcm_pt_buf = new;
		ctx->gcm_pt_buf_len = new_len;
		memcpy(&ctx->gcm_pt_buf[ctx->gcm_processed_data_len], data,
		    length);
		ctx->gcm_processed_data_len += length;
	}

	ctx->gcm_remainder_len = 0;
	return (CRYPTO_SUCCESS);
}

int
gcm_decrypt_final(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
#ifdef CAN_USE_GCM_ASM
	if (ctx->gcm_use_avx == B_TRUE)
		return (gcm_decrypt_final_avx(ctx, out, block_size));
#endif

	const gcm_impl_ops_t *gops;
	size_t pt_len;
	size_t remainder;
	uint8_t *ghash;
	uint8_t *blockp;
	uint8_t *cbp;
	uint64_t counter;
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	int processed = 0, rv;

	ASSERT(ctx->gcm_processed_data_len == ctx->gcm_pt_buf_len);

	gops = gcm_impl_get_ops();
	pt_len = ctx->gcm_processed_data_len - ctx->gcm_tag_len;
	ghash = (uint8_t *)ctx->gcm_ghash;
	blockp = ctx->gcm_pt_buf;
	remainder = pt_len;
	while (remainder > 0) {
		/* Incomplete last block */
		if (remainder < block_size) {
			memcpy(ctx->gcm_remainder, blockp, remainder);
			ctx->gcm_remainder_len = remainder;
			/*
			 * not expecting anymore ciphertext, just
			 * compute plaintext for the remaining input
			 */
			gcm_decrypt_incomplete_block(ctx, block_size,
			    processed, encrypt_block, xor_block);
			ctx->gcm_remainder_len = 0;
			goto out;
		}
		/* add ciphertext to the hash */
		GHASH(ctx, blockp, ghash, gops);

		/*
		 * Increment counter.
		 * Counter bits are confined to the bottom 32 bits
		 */
		counter = ntohll(ctx->gcm_cb[1] & counter_mask);
		counter = htonll(counter + 1);
		counter &= counter_mask;
		ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;

		cbp = (uint8_t *)ctx->gcm_tmp;
		encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_cb, cbp);

		/* XOR with ciphertext */
		xor_block(cbp, blockp);

		processed += block_size;
		blockp += block_size;
		remainder -= block_size;
	}
out:
	ctx->gcm_len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(pt_len));
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash, gops);
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0);
	xor_block((uint8_t *)ctx->gcm_J0, ghash);

	/* compare the input authentication tag with what we calculated */
	if (memcmp(&ctx->gcm_pt_buf[pt_len], ghash, ctx->gcm_tag_len)) {
		/* They don't match */
		return (CRYPTO_INVALID_MAC);
	} else {
		rv = crypto_put_output_data(ctx->gcm_pt_buf, out, pt_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
		out->cd_offset += pt_len;
	}
	return (CRYPTO_SUCCESS);
}

static int
gcm_validate_args(CK_AES_GCM_PARAMS *gcm_param)
{
	size_t tag_len;

	/*
	 * Check the length of the authentication tag (in bits).
	 */
	tag_len = gcm_param->ulTagBits;
	switch (tag_len) {
	case 32:
	case 64:
	case 96:
	case 104:
	case 112:
	case 120:
	case 128:
		break;
	default:
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	if (gcm_param->ulIvLen == 0)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	return (CRYPTO_SUCCESS);
}

static void
gcm_format_initial_blocks(uchar_t *iv, ulong_t iv_len,
    gcm_ctx_t *ctx, size_t block_size,
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	const gcm_impl_ops_t *gops;
	uint8_t *cb;
	ulong_t remainder = iv_len;
	ulong_t processed = 0;
	uint8_t *datap, *ghash;
	uint64_t len_a_len_c[2];

	gops = gcm_impl_get_ops();
	ghash = (uint8_t *)ctx->gcm_ghash;
	cb = (uint8_t *)ctx->gcm_cb;
	if (iv_len == 12) {
		memcpy(cb, iv, 12);
		cb[12] = 0;
		cb[13] = 0;
		cb[14] = 0;
		cb[15] = 1;
		/* J0 will be used again in the final */
		copy_block(cb, (uint8_t *)ctx->gcm_J0);
	} else {
		/* GHASH the IV */
		do {
			if (remainder < block_size) {
				memset(cb, 0, block_size);
				memcpy(cb, &(iv[processed]), remainder);
				datap = (uint8_t *)cb;
				remainder = 0;
			} else {
				datap = (uint8_t *)(&(iv[processed]));
				processed += block_size;
				remainder -= block_size;
			}
			GHASH(ctx, datap, ghash, gops);
		} while (remainder > 0);

		len_a_len_c[0] = 0;
		len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(iv_len));
		GHASH(ctx, len_a_len_c, ctx->gcm_J0, gops);

		/* J0 will be used again in the final */
		copy_block((uint8_t *)ctx->gcm_J0, (uint8_t *)cb);
	}
}

static int
gcm_init(gcm_ctx_t *ctx, unsigned char *iv, size_t iv_len,
    unsigned char *auth_data, size_t auth_data_len, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	const gcm_impl_ops_t *gops;
	uint8_t *ghash, *datap, *authp;
	size_t remainder, processed;

	/* encrypt zero block to get subkey H */
	memset(ctx->gcm_H, 0, sizeof (ctx->gcm_H));
	encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_H,
	    (uint8_t *)ctx->gcm_H);

	gcm_format_initial_blocks(iv, iv_len, ctx, block_size,
	    copy_block, xor_block);

	gops = gcm_impl_get_ops();
	authp = (uint8_t *)ctx->gcm_tmp;
	ghash = (uint8_t *)ctx->gcm_ghash;
	memset(authp, 0, block_size);
	memset(ghash, 0, block_size);

	processed = 0;
	remainder = auth_data_len;
	do {
		if (remainder < block_size) {
			/*
			 * There's not a block full of data, pad rest of
			 * buffer with zero
			 */

			if (auth_data != NULL) {
				memset(authp, 0, block_size);
				memcpy(authp, &(auth_data[processed]),
				    remainder);
			} else {
				ASSERT0(remainder);
			}

			datap = (uint8_t *)authp;
			remainder = 0;
		} else {
			datap = (uint8_t *)(&(auth_data[processed]));
			processed += block_size;
			remainder -= block_size;
		}

		/* add auth data to the hash */
		GHASH(ctx, datap, ghash, gops);

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}

/*
 * The following function is called at encrypt or decrypt init time
 * for AES GCM mode.
 *
 * Init the GCM context struct. Handle the cycle and avx implementations here.
 */
int
gcm_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	int rv;
	CK_AES_GCM_PARAMS *gcm_param;

	if (param != NULL) {
		gcm_param = (CK_AES_GCM_PARAMS *)(void *)param;

		if ((rv = gcm_validate_args(gcm_param)) != 0) {
			return (rv);
		}

		gcm_ctx->gcm_tag_len = gcm_param->ulTagBits;
		gcm_ctx->gcm_tag_len >>= 3;
		gcm_ctx->gcm_processed_data_len = 0;

		/* these values are in bits */
		gcm_ctx->gcm_len_a_len_c[0]
		    = htonll(CRYPTO_BYTES2BITS(gcm_param->ulAADLen));

		rv = CRYPTO_SUCCESS;
		gcm_ctx->gcm_flags |= GCM_MODE;
	} else {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

#ifdef CAN_USE_GCM_ASM
	if (GCM_IMPL_READ(icp_gcm_impl) != IMPL_CYCLE) {
		gcm_ctx->gcm_use_avx = GCM_IMPL_USE_AVX;
	} else {
		/*
		 * Handle the "cycle" implementation by creating avx and
		 * non-avx contexts alternately.
		 */
		gcm_ctx->gcm_use_avx = gcm_toggle_avx();
		/*
		 * We don't handle byte swapped key schedules in the avx
		 * code path.
		 */
		aes_key_t *ks = (aes_key_t *)gcm_ctx->gcm_keysched;
		if (ks->ops->needs_byteswap == B_TRUE) {
			gcm_ctx->gcm_use_avx = B_FALSE;
		}
		/* Use the MOVBE and the BSWAP variants alternately. */
		if (gcm_ctx->gcm_use_avx == B_TRUE &&
		    zfs_movbe_available() == B_TRUE) {
			(void) atomic_toggle_boolean_nv(
			    (volatile boolean_t *)&gcm_avx_can_use_movbe);
		}
	}
	/* Allocate Htab memory as needed. */
	if (gcm_ctx->gcm_use_avx == B_TRUE) {
		size_t htab_len = gcm_simd_get_htab_size(gcm_ctx->gcm_use_avx);

		if (htab_len == 0) {
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		gcm_ctx->gcm_htab_len = htab_len;
		gcm_ctx->gcm_Htable =
		    (uint64_t *)kmem_alloc(htab_len, KM_SLEEP);

		if (gcm_ctx->gcm_Htable == NULL) {
			return (CRYPTO_HOST_MEMORY);
		}
	}
	/* Avx and non avx context initialization differs from here on. */
	if (gcm_ctx->gcm_use_avx == B_FALSE) {
#endif /* ifdef CAN_USE_GCM_ASM */
		if (gcm_init(gcm_ctx, gcm_param->pIv, gcm_param->ulIvLen,
		    gcm_param->pAAD, gcm_param->ulAADLen, block_size,
		    encrypt_block, copy_block, xor_block) != 0) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
		}
#ifdef CAN_USE_GCM_ASM
	} else {
		if (gcm_init_avx(gcm_ctx, gcm_param->pIv, gcm_param->ulIvLen,
		    gcm_param->pAAD, gcm_param->ulAADLen, block_size) != 0) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
		}
	}
#endif /* ifdef CAN_USE_GCM_ASM */

	return (rv);
}

int
gmac_init_ctx(gcm_ctx_t *gcm_ctx, char *param, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	int rv;
	CK_AES_GMAC_PARAMS *gmac_param;

	if (param != NULL) {
		gmac_param = (CK_AES_GMAC_PARAMS *)(void *)param;

		gcm_ctx->gcm_tag_len = CRYPTO_BITS2BYTES(AES_GMAC_TAG_BITS);
		gcm_ctx->gcm_processed_data_len = 0;

		/* these values are in bits */
		gcm_ctx->gcm_len_a_len_c[0]
		    = htonll(CRYPTO_BYTES2BITS(gmac_param->ulAADLen));

		rv = CRYPTO_SUCCESS;
		gcm_ctx->gcm_flags |= GMAC_MODE;
	} else {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

#ifdef CAN_USE_GCM_ASM
	/*
	 * Handle the "cycle" implementation by creating avx and non avx
	 * contexts alternately.
	 */
	if (GCM_IMPL_READ(icp_gcm_impl) != IMPL_CYCLE) {
		gcm_ctx->gcm_use_avx = GCM_IMPL_USE_AVX;
	} else {
		gcm_ctx->gcm_use_avx = gcm_toggle_avx();
	}
	/* We don't handle byte swapped key schedules in the avx code path. */
	aes_key_t *ks = (aes_key_t *)gcm_ctx->gcm_keysched;
	if (ks->ops->needs_byteswap == B_TRUE) {
		gcm_ctx->gcm_use_avx = B_FALSE;
	}
	/* Allocate Htab memory as needed. */
	if (gcm_ctx->gcm_use_avx == B_TRUE) {
		size_t htab_len = gcm_simd_get_htab_size(gcm_ctx->gcm_use_avx);

		if (htab_len == 0) {
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		gcm_ctx->gcm_htab_len = htab_len;
		gcm_ctx->gcm_Htable =
		    (uint64_t *)kmem_alloc(htab_len, KM_SLEEP);

		if (gcm_ctx->gcm_Htable == NULL) {
			return (CRYPTO_HOST_MEMORY);
		}
	}

	/* Avx and non avx context initialization differs from here on. */
	if (gcm_ctx->gcm_use_avx == B_FALSE) {
#endif	/* ifdef CAN_USE_GCM_ASM */
		if (gcm_init(gcm_ctx, gmac_param->pIv, AES_GMAC_IV_LEN,
		    gmac_param->pAAD, gmac_param->ulAADLen, block_size,
		    encrypt_block, copy_block, xor_block) != 0) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
		}
#ifdef CAN_USE_GCM_ASM
	} else {
		if (gcm_init_avx(gcm_ctx, gmac_param->pIv, AES_GMAC_IV_LEN,
		    gmac_param->pAAD, gmac_param->ulAADLen, block_size) != 0) {
			rv = CRYPTO_MECHANISM_PARAM_INVALID;
		}
	}
#endif /* ifdef CAN_USE_GCM_ASM */

	return (rv);
}

void *
gcm_alloc_ctx(int kmflag)
{
	gcm_ctx_t *gcm_ctx;

	if ((gcm_ctx = kmem_zalloc(sizeof (gcm_ctx_t), kmflag)) == NULL)
		return (NULL);

	gcm_ctx->gcm_flags = GCM_MODE;
	return (gcm_ctx);
}

void *
gmac_alloc_ctx(int kmflag)
{
	gcm_ctx_t *gcm_ctx;

	if ((gcm_ctx = kmem_zalloc(sizeof (gcm_ctx_t), kmflag)) == NULL)
		return (NULL);

	gcm_ctx->gcm_flags = GMAC_MODE;
	return (gcm_ctx);
}

/* GCM implementation that contains the fastest methods */
static gcm_impl_ops_t gcm_fastest_impl = {
	.name = "fastest"
};

/* All compiled in implementations */
static const gcm_impl_ops_t *gcm_all_impl[] = {
	&gcm_generic_impl,
#if defined(__x86_64) && defined(HAVE_PCLMULQDQ)
	&gcm_pclmulqdq_impl,
#endif
};

/* Indicate that benchmark has been completed */
static boolean_t gcm_impl_initialized = B_FALSE;

/* Hold all supported implementations */
static size_t gcm_supp_impl_cnt = 0;
static gcm_impl_ops_t *gcm_supp_impl[ARRAY_SIZE(gcm_all_impl)];

/*
 * Returns the GCM operations for encrypt/decrypt/key setup.  When a
 * SIMD implementation is not allowed in the current context, then
 * fallback to the fastest generic implementation.
 */
const gcm_impl_ops_t *
gcm_impl_get_ops(void)
{
	if (!kfpu_allowed())
		return (&gcm_generic_impl);

	const gcm_impl_ops_t *ops = NULL;
	const uint32_t impl = GCM_IMPL_READ(icp_gcm_impl);

	switch (impl) {
	case IMPL_FASTEST:
		ASSERT(gcm_impl_initialized);
		ops = &gcm_fastest_impl;
		break;
	case IMPL_CYCLE:
		/* Cycle through supported implementations */
		ASSERT(gcm_impl_initialized);
		ASSERT3U(gcm_supp_impl_cnt, >, 0);
		static size_t cycle_impl_idx = 0;
		size_t idx = (++cycle_impl_idx) % gcm_supp_impl_cnt;
		ops = gcm_supp_impl[idx];
		break;
#ifdef CAN_USE_GCM_ASM
	case IMPL_AVX:
		/*
		 * Make sure that we return a valid implementation while
		 * switching to the avx implementation since there still
		 * may be unfinished non-avx contexts around.
		 */
		ops = &gcm_generic_impl;
		break;
#endif
	default:
		ASSERT3U(impl, <, gcm_supp_impl_cnt);
		ASSERT3U(gcm_supp_impl_cnt, >, 0);
		if (impl < ARRAY_SIZE(gcm_all_impl))
			ops = gcm_supp_impl[impl];
		break;
	}

	ASSERT3P(ops, !=, NULL);

	return (ops);
}

/*
 * Initialize all supported implementations.
 */
void
gcm_impl_init(void)
{
	gcm_impl_ops_t *curr_impl;
	int i, c;

	/* Move supported implementations into gcm_supp_impls */
	for (i = 0, c = 0; i < ARRAY_SIZE(gcm_all_impl); i++) {
		curr_impl = (gcm_impl_ops_t *)gcm_all_impl[i];

		if (curr_impl->is_supported())
			gcm_supp_impl[c++] = (gcm_impl_ops_t *)curr_impl;
	}
	gcm_supp_impl_cnt = c;

	/*
	 * Set the fastest implementation given the assumption that the
	 * hardware accelerated version is the fastest.
	 */
#if defined(__x86_64) && defined(HAVE_PCLMULQDQ)
	if (gcm_pclmulqdq_impl.is_supported()) {
		memcpy(&gcm_fastest_impl, &gcm_pclmulqdq_impl,
		    sizeof (gcm_fastest_impl));
	} else
#endif
	{
		memcpy(&gcm_fastest_impl, &gcm_generic_impl,
		    sizeof (gcm_fastest_impl));
	}

	strlcpy(gcm_fastest_impl.name, "fastest", GCM_IMPL_NAME_MAX);

#ifdef CAN_USE_GCM_ASM
	/*
	 * Use the avx implementation if it's available and the implementation
	 * hasn't changed from its default value of fastest on module load.
	 */
	if (gcm_avx_will_work()) {
#ifdef HAVE_MOVBE
		if (zfs_movbe_available() == B_TRUE) {
			atomic_swap_32(&gcm_avx_can_use_movbe, B_TRUE);
		}
#endif
		if (GCM_IMPL_READ(user_sel_impl) == IMPL_FASTEST) {
			gcm_set_avx(B_TRUE);
		}
	}
#endif
	/* Finish initialization */
	atomic_swap_32(&icp_gcm_impl, user_sel_impl);
	gcm_impl_initialized = B_TRUE;
}

static const struct {
	const char *name;
	uint32_t sel;
} gcm_impl_opts[] = {
		{ "cycle",	IMPL_CYCLE },
		{ "fastest",	IMPL_FASTEST },
#ifdef CAN_USE_GCM_ASM
		{ "avx",	IMPL_AVX },
#endif
};

/*
 * Function sets desired gcm implementation.
 *
 * If we are called before init(), user preference will be saved in
 * user_sel_impl, and applied in later init() call. This occurs when module
 * parameter is specified on module load. Otherwise, directly update
 * icp_gcm_impl.
 *
 * @val		Name of gcm implementation to use
 * @param	Unused.
 */
int
gcm_impl_set(const char *val)
{
	int err = -EINVAL;
	char req_name[GCM_IMPL_NAME_MAX];
	uint32_t impl = GCM_IMPL_READ(user_sel_impl);
	size_t i;

	/* sanitize input */
	i = strnlen(val, GCM_IMPL_NAME_MAX);
	if (i == 0 || i >= GCM_IMPL_NAME_MAX)
		return (err);

	strlcpy(req_name, val, GCM_IMPL_NAME_MAX);
	while (i > 0 && isspace(req_name[i-1]))
		i--;
	req_name[i] = '\0';

	/* Check mandatory options */
	for (i = 0; i < ARRAY_SIZE(gcm_impl_opts); i++) {
#ifdef CAN_USE_GCM_ASM
		/* Ignore avx implementation if it won't work. */
		if (gcm_impl_opts[i].sel == IMPL_AVX && !gcm_avx_will_work()) {
			continue;
		}
#endif
		if (strcmp(req_name, gcm_impl_opts[i].name) == 0) {
			impl = gcm_impl_opts[i].sel;
			err = 0;
			break;
		}
	}

	/* check all supported impl if init() was already called */
	if (err != 0 && gcm_impl_initialized) {
		/* check all supported implementations */
		for (i = 0; i < gcm_supp_impl_cnt; i++) {
			if (strcmp(req_name, gcm_supp_impl[i]->name) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}
#ifdef CAN_USE_GCM_ASM
	/*
	 * Use the avx implementation if available and the requested one is
	 * avx or fastest.
	 */
	if (gcm_avx_will_work() == B_TRUE &&
	    (impl == IMPL_AVX || impl == IMPL_FASTEST)) {
		gcm_set_avx(B_TRUE);
	} else {
		gcm_set_avx(B_FALSE);
	}
#endif

	if (err == 0) {
		if (gcm_impl_initialized)
			atomic_swap_32(&icp_gcm_impl, impl);
		else
			atomic_swap_32(&user_sel_impl, impl);
	}

	return (err);
}

#if defined(_KERNEL) && defined(__linux__)

static int
icp_gcm_impl_set(const char *val, zfs_kernel_param_t *kp)
{
	return (gcm_impl_set(val));
}

static int
icp_gcm_impl_get(char *buffer, zfs_kernel_param_t *kp)
{
	int i, cnt = 0;
	char *fmt;
	const uint32_t impl = GCM_IMPL_READ(icp_gcm_impl);

	ASSERT(gcm_impl_initialized);

	/* list mandatory options */
	for (i = 0; i < ARRAY_SIZE(gcm_impl_opts); i++) {
#ifdef CAN_USE_GCM_ASM
		/* Ignore avx implementation if it won't work. */
		if (gcm_impl_opts[i].sel == IMPL_AVX && !gcm_avx_will_work()) {
			continue;
		}
#endif
		fmt = (impl == gcm_impl_opts[i].sel) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, gcm_impl_opts[i].name);
	}

	/* list all supported implementations */
	for (i = 0; i < gcm_supp_impl_cnt; i++) {
		fmt = (i == impl) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, gcm_supp_impl[i]->name);
	}

	return (cnt);
}

module_param_call(icp_gcm_impl, icp_gcm_impl_set, icp_gcm_impl_get,
    NULL, 0644);
MODULE_PARM_DESC(icp_gcm_impl, "Select gcm implementation.");
#endif /* defined(__KERNEL) */

#ifdef CAN_USE_GCM_ASM
#define	GCM_BLOCK_LEN 16
/*
 * The openssl asm routines are 6x aggregated and need that many bytes
 * at minimum.
 */
#define	GCM_AVX_MIN_DECRYPT_BYTES (GCM_BLOCK_LEN * 6)
#define	GCM_AVX_MIN_ENCRYPT_BYTES (GCM_BLOCK_LEN * 6 * 3)
/*
 * Ensure the chunk size is reasonable since we are allocating a
 * GCM_AVX_MAX_CHUNK_SIZEd buffer and disabling preemption and interrupts.
 */
#define	GCM_AVX_MAX_CHUNK_SIZE \
	(((128*1024)/GCM_AVX_MIN_DECRYPT_BYTES) * GCM_AVX_MIN_DECRYPT_BYTES)

/* Clear the FPU registers since they hold sensitive internal state. */
#define	clear_fpu_regs() clear_fpu_regs_avx()
#define	GHASH_AVX(ctx, in, len) \
    gcm_ghash_avx((ctx)->gcm_ghash, (const uint64_t *)(ctx)->gcm_Htable, \
    in, len)

#define	gcm_incr_counter_block(ctx) gcm_incr_counter_block_by(ctx, 1)

/* Get the chunk size module parameter. */
#define	GCM_CHUNK_SIZE_READ *(volatile uint32_t *) &gcm_avx_chunk_size

/*
 * Module parameter: number of bytes to process at once while owning the FPU.
 * Rounded down to the next GCM_AVX_MIN_DECRYPT_BYTES byte boundary and is
 * ensured to be greater or equal than GCM_AVX_MIN_DECRYPT_BYTES.
 */
static uint32_t gcm_avx_chunk_size =
	((32 * 1024) / GCM_AVX_MIN_DECRYPT_BYTES) * GCM_AVX_MIN_DECRYPT_BYTES;

extern void clear_fpu_regs_avx(void);
extern void gcm_xor_avx(const uint8_t *src, uint8_t *dst);
extern void aes_encrypt_intel(const uint32_t rk[], int nr,
    const uint32_t pt[4], uint32_t ct[4]);

extern void gcm_init_htab_avx(uint64_t *Htable, const uint64_t H[2]);
extern void gcm_ghash_avx(uint64_t ghash[2], const uint64_t *Htable,
    const uint8_t *in, size_t len);

extern size_t aesni_gcm_encrypt(const uint8_t *, uint8_t *, size_t,
    const void *, uint64_t *, uint64_t *);

extern size_t aesni_gcm_decrypt(const uint8_t *, uint8_t *, size_t,
    const void *, uint64_t *, uint64_t *);

static inline boolean_t
gcm_avx_will_work(void)
{
	/* Avx should imply aes-ni and pclmulqdq, but make sure anyhow. */
	return (kfpu_allowed() &&
	    zfs_avx_available() && zfs_aes_available() &&
	    zfs_pclmulqdq_available());
}

static inline void
gcm_set_avx(boolean_t val)
{
	if (gcm_avx_will_work() == B_TRUE) {
		atomic_swap_32(&gcm_use_avx, val);
	}
}

static inline boolean_t
gcm_toggle_avx(void)
{
	if (gcm_avx_will_work() == B_TRUE) {
		return (atomic_toggle_boolean_nv(&GCM_IMPL_USE_AVX));
	} else {
		return (B_FALSE);
	}
}

static inline size_t
gcm_simd_get_htab_size(boolean_t simd_mode)
{
	switch (simd_mode) {
	case B_TRUE:
		return (2 * 6 * 2 * sizeof (uint64_t));

	default:
		return (0);
	}
}

/*
 * Clear sensitive data in the context.
 *
 * ctx->gcm_remainder may contain a plaintext remainder. ctx->gcm_H and
 * ctx->gcm_Htable contain the hash sub key which protects authentication.
 *
 * Although extremely unlikely, ctx->gcm_J0 and ctx->gcm_tmp could be used for
 * a known plaintext attack, they consists of the IV and the first and last
 * counter respectively. If they should be cleared is debatable.
 */
static inline void
gcm_clear_ctx(gcm_ctx_t *ctx)
{
	memset(ctx->gcm_remainder, 0, sizeof (ctx->gcm_remainder));
	memset(ctx->gcm_H, 0, sizeof (ctx->gcm_H));
	memset(ctx->gcm_J0, 0, sizeof (ctx->gcm_J0));
	memset(ctx->gcm_tmp, 0, sizeof (ctx->gcm_tmp));
}

/* Increment the GCM counter block by n. */
static inline void
gcm_incr_counter_block_by(gcm_ctx_t *ctx, int n)
{
	uint64_t counter_mask = ntohll(0x00000000ffffffffULL);
	uint64_t counter = ntohll(ctx->gcm_cb[1] & counter_mask);

	counter = htonll(counter + n);
	counter &= counter_mask;
	ctx->gcm_cb[1] = (ctx->gcm_cb[1] & ~counter_mask) | counter;
}

/*
 * Encrypt multiple blocks of data in GCM mode.
 * This is done in gcm_avx_chunk_size chunks, utilizing AVX assembler routines
 * if possible. While processing a chunk the FPU is "locked".
 */
static int
gcm_mode_encrypt_contiguous_blocks_avx(gcm_ctx_t *ctx, char *data,
    size_t length, crypto_data_t *out, size_t block_size)
{
	size_t bleft = length;
	size_t need = 0;
	size_t done = 0;
	uint8_t *datap = (uint8_t *)data;
	size_t chunk_size = (size_t)GCM_CHUNK_SIZE_READ;
	const aes_key_t *key = ((aes_key_t *)ctx->gcm_keysched);
	uint64_t *ghash = ctx->gcm_ghash;
	uint64_t *cb = ctx->gcm_cb;
	uint8_t *ct_buf = NULL;
	uint8_t *tmp = (uint8_t *)ctx->gcm_tmp;
	int rv = CRYPTO_SUCCESS;

	ASSERT(block_size == GCM_BLOCK_LEN);
	/*
	 * If the last call left an incomplete block, try to fill
	 * it first.
	 */
	if (ctx->gcm_remainder_len > 0) {
		need = block_size - ctx->gcm_remainder_len;
		if (length < need) {
			/* Accumulate bytes here and return. */
			memcpy((uint8_t *)ctx->gcm_remainder +
			    ctx->gcm_remainder_len, datap, length);

			ctx->gcm_remainder_len += length;
			if (ctx->gcm_copy_to == NULL) {
				ctx->gcm_copy_to = datap;
			}
			return (CRYPTO_SUCCESS);
		} else {
			/* Complete incomplete block. */
			memcpy((uint8_t *)ctx->gcm_remainder +
			    ctx->gcm_remainder_len, datap, need);

			ctx->gcm_copy_to = NULL;
		}
	}

	/* Allocate a buffer to encrypt to if there is enough input. */
	if (bleft >= GCM_AVX_MIN_ENCRYPT_BYTES) {
		ct_buf = vmem_alloc(chunk_size, KM_SLEEP);
		if (ct_buf == NULL) {
			return (CRYPTO_HOST_MEMORY);
		}
	}

	/* If we completed an incomplete block, encrypt and write it out. */
	if (ctx->gcm_remainder_len > 0) {
		kfpu_begin();
		aes_encrypt_intel(key->encr_ks.ks32, key->nr,
		    (const uint32_t *)cb, (uint32_t *)tmp);

		gcm_xor_avx((const uint8_t *) ctx->gcm_remainder, tmp);
		GHASH_AVX(ctx, tmp, block_size);
		clear_fpu_regs();
		kfpu_end();
		rv = crypto_put_output_data(tmp, out, block_size);
		out->cd_offset += block_size;
		gcm_incr_counter_block(ctx);
		ctx->gcm_processed_data_len += block_size;
		bleft -= need;
		datap += need;
		ctx->gcm_remainder_len = 0;
	}

	/* Do the bulk encryption in chunk_size blocks. */
	for (; bleft >= chunk_size; bleft -= chunk_size) {
		kfpu_begin();
		done = aesni_gcm_encrypt(
		    datap, ct_buf, chunk_size, key, cb, ghash);

		clear_fpu_regs();
		kfpu_end();
		if (done != chunk_size) {
			rv = CRYPTO_FAILED;
			goto out_nofpu;
		}
		rv = crypto_put_output_data(ct_buf, out, chunk_size);
		if (rv != CRYPTO_SUCCESS) {
			goto out_nofpu;
		}
		out->cd_offset += chunk_size;
		datap += chunk_size;
		ctx->gcm_processed_data_len += chunk_size;
	}
	/* Check if we are already done. */
	if (bleft == 0) {
		goto out_nofpu;
	}
	/* Bulk encrypt the remaining data. */
	kfpu_begin();
	if (bleft >= GCM_AVX_MIN_ENCRYPT_BYTES) {
		done = aesni_gcm_encrypt(datap, ct_buf, bleft, key, cb, ghash);
		if (done == 0) {
			rv = CRYPTO_FAILED;
			goto out;
		}
		rv = crypto_put_output_data(ct_buf, out, done);
		if (rv != CRYPTO_SUCCESS) {
			goto out;
		}
		out->cd_offset += done;
		ctx->gcm_processed_data_len += done;
		datap += done;
		bleft -= done;

	}
	/* Less than GCM_AVX_MIN_ENCRYPT_BYTES remain, operate on blocks. */
	while (bleft > 0) {
		if (bleft < block_size) {
			memcpy(ctx->gcm_remainder, datap, bleft);
			ctx->gcm_remainder_len = bleft;
			ctx->gcm_copy_to = datap;
			goto out;
		}
		/* Encrypt, hash and write out. */
		aes_encrypt_intel(key->encr_ks.ks32, key->nr,
		    (const uint32_t *)cb, (uint32_t *)tmp);

		gcm_xor_avx(datap, tmp);
		GHASH_AVX(ctx, tmp, block_size);
		rv = crypto_put_output_data(tmp, out, block_size);
		if (rv != CRYPTO_SUCCESS) {
			goto out;
		}
		out->cd_offset += block_size;
		gcm_incr_counter_block(ctx);
		ctx->gcm_processed_data_len += block_size;
		datap += block_size;
		bleft -= block_size;
	}
out:
	clear_fpu_regs();
	kfpu_end();
out_nofpu:
	if (ct_buf != NULL) {
		vmem_free(ct_buf, chunk_size);
	}
	return (rv);
}

/*
 * Finalize the encryption: Zero fill, encrypt, hash and write out an eventual
 * incomplete last block. Encrypt the ICB. Calculate the tag and write it out.
 */
static int
gcm_encrypt_final_avx(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size)
{
	uint8_t *ghash = (uint8_t *)ctx->gcm_ghash;
	uint32_t *J0 = (uint32_t *)ctx->gcm_J0;
	uint8_t *remainder = (uint8_t *)ctx->gcm_remainder;
	size_t rem_len = ctx->gcm_remainder_len;
	const void *keysched = ((aes_key_t *)ctx->gcm_keysched)->encr_ks.ks32;
	int aes_rounds = ((aes_key_t *)keysched)->nr;
	int rv;

	ASSERT(block_size == GCM_BLOCK_LEN);

	if (out->cd_length < (rem_len + ctx->gcm_tag_len)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	kfpu_begin();
	/* Pad last incomplete block with zeros, encrypt and hash. */
	if (rem_len > 0) {
		uint8_t *tmp = (uint8_t *)ctx->gcm_tmp;
		const uint32_t *cb = (uint32_t *)ctx->gcm_cb;

		aes_encrypt_intel(keysched, aes_rounds, cb, (uint32_t *)tmp);
		memset(remainder + rem_len, 0, block_size - rem_len);
		for (int i = 0; i < rem_len; i++) {
			remainder[i] ^= tmp[i];
		}
		GHASH_AVX(ctx, remainder, block_size);
		ctx->gcm_processed_data_len += rem_len;
		/* No need to increment counter_block, it's the last block. */
	}
	/* Finish tag. */
	ctx->gcm_len_a_len_c[1] =
	    htonll(CRYPTO_BYTES2BITS(ctx->gcm_processed_data_len));
	GHASH_AVX(ctx, (const uint8_t *)ctx->gcm_len_a_len_c, block_size);
	aes_encrypt_intel(keysched, aes_rounds, J0, J0);

	gcm_xor_avx((uint8_t *)J0, ghash);
	clear_fpu_regs();
	kfpu_end();

	/* Output remainder. */
	if (rem_len > 0) {
		rv = crypto_put_output_data(remainder, out, rem_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}
	out->cd_offset += rem_len;
	ctx->gcm_remainder_len = 0;
	rv = crypto_put_output_data(ghash, out, ctx->gcm_tag_len);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	out->cd_offset += ctx->gcm_tag_len;
	/* Clear sensitive data in the context before returning. */
	gcm_clear_ctx(ctx);
	return (CRYPTO_SUCCESS);
}

/*
 * Finalize decryption: We just have accumulated crypto text, so now we
 * decrypt it here inplace.
 */
static int
gcm_decrypt_final_avx(gcm_ctx_t *ctx, crypto_data_t *out, size_t block_size)
{
	ASSERT3U(ctx->gcm_processed_data_len, ==, ctx->gcm_pt_buf_len);
	ASSERT3U(block_size, ==, 16);

	size_t chunk_size = (size_t)GCM_CHUNK_SIZE_READ;
	size_t pt_len = ctx->gcm_processed_data_len - ctx->gcm_tag_len;
	uint8_t *datap = ctx->gcm_pt_buf;
	const aes_key_t *key = ((aes_key_t *)ctx->gcm_keysched);
	uint32_t *cb = (uint32_t *)ctx->gcm_cb;
	uint64_t *ghash = ctx->gcm_ghash;
	uint32_t *tmp = (uint32_t *)ctx->gcm_tmp;
	int rv = CRYPTO_SUCCESS;
	size_t bleft, done;

	/*
	 * Decrypt in chunks of gcm_avx_chunk_size, which is asserted to be
	 * greater or equal than GCM_AVX_MIN_ENCRYPT_BYTES, and a multiple of
	 * GCM_AVX_MIN_DECRYPT_BYTES.
	 */
	for (bleft = pt_len; bleft >= chunk_size; bleft -= chunk_size) {
		kfpu_begin();
		done = aesni_gcm_decrypt(datap, datap, chunk_size,
		    (const void *)key, ctx->gcm_cb, ghash);
		clear_fpu_regs();
		kfpu_end();
		if (done != chunk_size) {
			return (CRYPTO_FAILED);
		}
		datap += done;
	}
	/* Decrypt remainder, which is less than chunk size, in one go. */
	kfpu_begin();
	if (bleft >= GCM_AVX_MIN_DECRYPT_BYTES) {
		done = aesni_gcm_decrypt(datap, datap, bleft,
		    (const void *)key, ctx->gcm_cb, ghash);
		if (done == 0) {
			clear_fpu_regs();
			kfpu_end();
			return (CRYPTO_FAILED);
		}
		datap += done;
		bleft -= done;
	}
	ASSERT(bleft < GCM_AVX_MIN_DECRYPT_BYTES);

	/*
	 * Now less than GCM_AVX_MIN_DECRYPT_BYTES bytes remain,
	 * decrypt them block by block.
	 */
	while (bleft > 0) {
		/* Incomplete last block. */
		if (bleft < block_size) {
			uint8_t *lastb = (uint8_t *)ctx->gcm_remainder;

			memset(lastb, 0, block_size);
			memcpy(lastb, datap, bleft);
			/* The GCM processing. */
			GHASH_AVX(ctx, lastb, block_size);
			aes_encrypt_intel(key->encr_ks.ks32, key->nr, cb, tmp);
			for (size_t i = 0; i < bleft; i++) {
				datap[i] = lastb[i] ^ ((uint8_t *)tmp)[i];
			}
			break;
		}
		/* The GCM processing. */
		GHASH_AVX(ctx, datap, block_size);
		aes_encrypt_intel(key->encr_ks.ks32, key->nr, cb, tmp);
		gcm_xor_avx((uint8_t *)tmp, datap);
		gcm_incr_counter_block(ctx);

		datap += block_size;
		bleft -= block_size;
	}
	if (rv != CRYPTO_SUCCESS) {
		clear_fpu_regs();
		kfpu_end();
		return (rv);
	}
	/* Decryption done, finish the tag. */
	ctx->gcm_len_a_len_c[1] = htonll(CRYPTO_BYTES2BITS(pt_len));
	GHASH_AVX(ctx, (uint8_t *)ctx->gcm_len_a_len_c, block_size);
	aes_encrypt_intel(key->encr_ks.ks32, key->nr, (uint32_t *)ctx->gcm_J0,
	    (uint32_t *)ctx->gcm_J0);

	gcm_xor_avx((uint8_t *)ctx->gcm_J0, (uint8_t *)ghash);

	/* We are done with the FPU, restore its state. */
	clear_fpu_regs();
	kfpu_end();

	/* Compare the input authentication tag with what we calculated. */
	if (memcmp(&ctx->gcm_pt_buf[pt_len], ghash, ctx->gcm_tag_len)) {
		/* They don't match. */
		return (CRYPTO_INVALID_MAC);
	}
	rv = crypto_put_output_data(ctx->gcm_pt_buf, out, pt_len);
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}
	out->cd_offset += pt_len;
	gcm_clear_ctx(ctx);
	return (CRYPTO_SUCCESS);
}

/*
 * Initialize the GCM params H, Htabtle and the counter block. Save the
 * initial counter block.
 */
static int
gcm_init_avx(gcm_ctx_t *ctx, unsigned char *iv, size_t iv_len,
    unsigned char *auth_data, size_t auth_data_len, size_t block_size)
{
	uint8_t *cb = (uint8_t *)ctx->gcm_cb;
	uint64_t *H = ctx->gcm_H;
	const void *keysched = ((aes_key_t *)ctx->gcm_keysched)->encr_ks.ks32;
	int aes_rounds = ((aes_key_t *)ctx->gcm_keysched)->nr;
	uint8_t *datap = auth_data;
	size_t chunk_size = (size_t)GCM_CHUNK_SIZE_READ;
	size_t bleft;

	ASSERT(block_size == GCM_BLOCK_LEN);

	/* Init H (encrypt zero block) and create the initial counter block. */
	memset(ctx->gcm_ghash, 0, sizeof (ctx->gcm_ghash));
	memset(H, 0, sizeof (ctx->gcm_H));
	kfpu_begin();
	aes_encrypt_intel(keysched, aes_rounds,
	    (const uint32_t *)H, (uint32_t *)H);

	gcm_init_htab_avx(ctx->gcm_Htable, H);

	if (iv_len == 12) {
		memcpy(cb, iv, 12);
		cb[12] = 0;
		cb[13] = 0;
		cb[14] = 0;
		cb[15] = 1;
		/* We need the ICB later. */
		memcpy(ctx->gcm_J0, cb, sizeof (ctx->gcm_J0));
	} else {
		/*
		 * Most consumers use 12 byte IVs, so it's OK to use the
		 * original routines for other IV sizes, just avoid nesting
		 * kfpu_begin calls.
		 */
		clear_fpu_regs();
		kfpu_end();
		gcm_format_initial_blocks(iv, iv_len, ctx, block_size,
		    aes_copy_block, aes_xor_block);
		kfpu_begin();
	}

	/* Openssl post increments the counter, adjust for that. */
	gcm_incr_counter_block(ctx);

	/* Ghash AAD in chunk_size blocks. */
	for (bleft = auth_data_len; bleft >= chunk_size; bleft -= chunk_size) {
		GHASH_AVX(ctx, datap, chunk_size);
		datap += chunk_size;
		clear_fpu_regs();
		kfpu_end();
		kfpu_begin();
	}
	/* Ghash the remainder and handle possible incomplete GCM block. */
	if (bleft > 0) {
		size_t incomp = bleft % block_size;

		bleft -= incomp;
		if (bleft > 0) {
			GHASH_AVX(ctx, datap, bleft);
			datap += bleft;
		}
		if (incomp > 0) {
			/* Zero pad and hash incomplete last block. */
			uint8_t *authp = (uint8_t *)ctx->gcm_tmp;

			memset(authp, 0, block_size);
			memcpy(authp, datap, incomp);
			GHASH_AVX(ctx, authp, block_size);
		}
	}
	clear_fpu_regs();
	kfpu_end();
	return (CRYPTO_SUCCESS);
}

#if defined(_KERNEL)
static int
icp_gcm_avx_set_chunk_size(const char *buf, zfs_kernel_param_t *kp)
{
	unsigned long val;
	char val_rounded[16];
	int error = 0;

	error = kstrtoul(buf, 0, &val);
	if (error)
		return (error);

	val = (val / GCM_AVX_MIN_DECRYPT_BYTES) * GCM_AVX_MIN_DECRYPT_BYTES;

	if (val < GCM_AVX_MIN_ENCRYPT_BYTES || val > GCM_AVX_MAX_CHUNK_SIZE)
		return (-EINVAL);

	snprintf(val_rounded, 16, "%u", (uint32_t)val);
	error = param_set_uint(val_rounded, kp);
	return (error);
}

module_param_call(icp_gcm_avx_chunk_size, icp_gcm_avx_set_chunk_size,
    param_get_uint, &gcm_avx_chunk_size, 0644);

MODULE_PARM_DESC(icp_gcm_avx_chunk_size,
	"How many bytes to process while owning the FPU");

#endif /* defined(__KERNEL) */
#endif /* ifdef CAN_USE_GCM_ASM */
