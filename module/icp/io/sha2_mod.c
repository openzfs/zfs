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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/icp.h>
#define	_SHA2_IMPL
#include <sys/sha2.h>
#include <sha2/sha2_impl.h>

/*
 * Macros to access the SHA2 or SHA2-HMAC contexts from a context passed
 * by KCF to one of the entry points.
 */

#define	PROV_SHA2_CTX(ctx)	((sha2_ctx_t *)(ctx)->cc_provider_private)
#define	PROV_SHA2_HMAC_CTX(ctx)	((sha2_hmac_ctx_t *)(ctx)->cc_provider_private)

/* to extract the digest length passed as mechanism parameter */
#define	PROV_SHA2_GET_DIGEST_LEN(m, len) {				\
	if (IS_P2ALIGNED((m)->cm_param, sizeof (ulong_t)))		\
		(len) = (uint32_t)*((ulong_t *)(m)->cm_param);	\
	else {								\
		ulong_t tmp_ulong;					\
		memcpy(&tmp_ulong, (m)->cm_param, sizeof (ulong_t));	\
		(len) = (uint32_t)tmp_ulong;				\
	}								\
}

#define	PROV_SHA2_DIGEST_KEY(mech, ctx, key, len, digest) {	\
	SHA2Init(mech, ctx);				\
	SHA2Update(ctx, key, len);			\
	SHA2Final(digest, ctx);				\
}

/*
 * Mechanism info structure passed to KCF during registration.
 */
static const crypto_mech_info_t sha2_mech_info_tab[] = {
	/* SHA256 */
	{SUN_CKM_SHA256, SHA256_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC},
	/* SHA256-HMAC */
	{SUN_CKM_SHA256_HMAC, SHA256_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC},
	/* SHA256-HMAC GENERAL */
	{SUN_CKM_SHA256_HMAC_GENERAL, SHA256_HMAC_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC},
	/* SHA384 */
	{SUN_CKM_SHA384, SHA384_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC},
	/* SHA384-HMAC */
	{SUN_CKM_SHA384_HMAC, SHA384_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC},
	/* SHA384-HMAC GENERAL */
	{SUN_CKM_SHA384_HMAC_GENERAL, SHA384_HMAC_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC},
	/* SHA512 */
	{SUN_CKM_SHA512, SHA512_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC},
	/* SHA512-HMAC */
	{SUN_CKM_SHA512_HMAC, SHA512_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC},
	/* SHA512-HMAC GENERAL */
	{SUN_CKM_SHA512_HMAC_GENERAL, SHA512_HMAC_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC},
};

static int sha2_digest_init(crypto_ctx_t *, crypto_mechanism_t *);
static int sha2_digest(crypto_ctx_t *, crypto_data_t *, crypto_data_t *);
static int sha2_digest_update(crypto_ctx_t *, crypto_data_t *);
static int sha2_digest_final(crypto_ctx_t *, crypto_data_t *);
static int sha2_digest_atomic(crypto_mechanism_t *, crypto_data_t *,
    crypto_data_t *);

static const crypto_digest_ops_t sha2_digest_ops = {
	.digest_init = sha2_digest_init,
	.digest = sha2_digest,
	.digest_update = sha2_digest_update,
	.digest_final = sha2_digest_final,
	.digest_atomic = sha2_digest_atomic
};

static int sha2_mac_init(crypto_ctx_t *, crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t);
static int sha2_mac_update(crypto_ctx_t *, crypto_data_t *);
static int sha2_mac_final(crypto_ctx_t *, crypto_data_t *);
static int sha2_mac_atomic(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
static int sha2_mac_verify_atomic(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);

static const crypto_mac_ops_t sha2_mac_ops = {
	.mac_init = sha2_mac_init,
	.mac = NULL,
	.mac_update = sha2_mac_update,
	.mac_final = sha2_mac_final,
	.mac_atomic = sha2_mac_atomic,
	.mac_verify_atomic = sha2_mac_verify_atomic
};

static int sha2_create_ctx_template(crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t *, size_t *);
static int sha2_free_context(crypto_ctx_t *);

static const crypto_ctx_ops_t sha2_ctx_ops = {
	.create_ctx_template = sha2_create_ctx_template,
	.free_context = sha2_free_context
};

static const crypto_ops_t sha2_crypto_ops = {
	&sha2_digest_ops,
	NULL,
	&sha2_mac_ops,
	&sha2_ctx_ops,
};

static const crypto_provider_info_t sha2_prov_info = {
	"SHA2 Software Provider",
	&sha2_crypto_ops,
	sizeof (sha2_mech_info_tab) / sizeof (crypto_mech_info_t),
	sha2_mech_info_tab
};

static crypto_kcf_provider_handle_t sha2_prov_handle = 0;

int
sha2_mod_init(void)
{
	int ret;

	/*
	 * Register with KCF. If the registration fails, log an
	 * error but do not uninstall the module, since the functionality
	 * provided by misc/sha2 should still be available.
	 */
	if ((ret = crypto_register_provider(&sha2_prov_info,
	    &sha2_prov_handle)) != CRYPTO_SUCCESS)
		cmn_err(CE_WARN, "sha2 _init: "
		    "crypto_register_provider() failed (0x%x)", ret);

	return (0);
}

int
sha2_mod_fini(void)
{
	int ret = 0;

	if (sha2_prov_handle != 0) {
		if ((ret = crypto_unregister_provider(sha2_prov_handle)) !=
		    CRYPTO_SUCCESS) {
			cmn_err(CE_WARN,
			    "sha2 _fini: crypto_unregister_provider() "
			    "failed (0x%x)", ret);
			return (EBUSY);
		}
		sha2_prov_handle = 0;
	}

	return (ret);
}

/*
 * KCF software provider digest entry points.
 */

static int
sha2_digest_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism)
{

	/*
	 * Allocate and initialize SHA2 context.
	 */
	ctx->cc_provider_private = kmem_alloc(sizeof (sha2_ctx_t), KM_SLEEP);
	if (ctx->cc_provider_private == NULL)
		return (CRYPTO_HOST_MEMORY);

	PROV_SHA2_CTX(ctx)->sc_mech_type = mechanism->cm_type;
	SHA2Init(mechanism->cm_type, &PROV_SHA2_CTX(ctx)->sc_sha2_ctx);

	return (CRYPTO_SUCCESS);
}

/*
 * Helper SHA2 digest update function for uio data.
 */
static int
sha2_digest_update_uio(SHA2_CTX *sha2_ctx, crypto_data_t *data)
{
	off_t offset = data->cd_offset;
	size_t length = data->cd_length;
	uint_t vec_idx = 0;
	size_t cur_len;

	/* we support only kernel buffer */
	if (zfs_uio_segflg(data->cd_uio) != UIO_SYSSPACE)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Jump to the first iovec containing data to be
	 * digested.
	 */
	offset = zfs_uio_index_at_offset(data->cd_uio, offset, &vec_idx);
	if (vec_idx == zfs_uio_iovcnt(data->cd_uio)) {
		/*
		 * The caller specified an offset that is larger than the
		 * total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Now do the digesting on the iovecs.
	 */
	while (vec_idx < zfs_uio_iovcnt(data->cd_uio) && length > 0) {
		cur_len = MIN(zfs_uio_iovlen(data->cd_uio, vec_idx) -
		    offset, length);

		SHA2Update(sha2_ctx, (uint8_t *)zfs_uio_iovbase(data->cd_uio,
		    vec_idx) + offset, cur_len);
		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == zfs_uio_iovcnt(data->cd_uio) && length > 0) {
		/*
		 * The end of the specified iovec's was reached but
		 * the length requested could not be processed, i.e.
		 * The caller requested to digest more data than it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	return (CRYPTO_SUCCESS);
}

/*
 * Helper SHA2 digest final function for uio data.
 * digest_len is the length of the desired digest. If digest_len
 * is smaller than the default SHA2 digest length, the caller
 * must pass a scratch buffer, digest_scratch, which must
 * be at least the algorithm's digest length bytes.
 */
static int
sha2_digest_final_uio(SHA2_CTX *sha2_ctx, crypto_data_t *digest,
    ulong_t digest_len, uchar_t *digest_scratch)
{
	off_t offset = digest->cd_offset;
	uint_t vec_idx = 0;

	/* we support only kernel buffer */
	if (zfs_uio_segflg(digest->cd_uio) != UIO_SYSSPACE)
		return (CRYPTO_ARGUMENTS_BAD);

	/*
	 * Jump to the first iovec containing ptr to the digest to
	 * be returned.
	 */
	offset = zfs_uio_index_at_offset(digest->cd_uio, offset, &vec_idx);
	if (vec_idx == zfs_uio_iovcnt(digest->cd_uio)) {
		/*
		 * The caller specified an offset that is
		 * larger than the total size of the buffers
		 * it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	if (offset + digest_len <=
	    zfs_uio_iovlen(digest->cd_uio, vec_idx)) {
		/*
		 * The computed SHA2 digest will fit in the current
		 * iovec.
		 */
		if (((sha2_ctx->algotype <= SHA256_HMAC_GEN_MECH_INFO_TYPE) &&
		    (digest_len != SHA256_DIGEST_LENGTH)) ||
		    ((sha2_ctx->algotype > SHA256_HMAC_GEN_MECH_INFO_TYPE) &&
		    (digest_len != SHA512_DIGEST_LENGTH))) {
			/*
			 * The caller requested a short digest. Digest
			 * into a scratch buffer and return to
			 * the user only what was requested.
			 */
			SHA2Final(digest_scratch, sha2_ctx);

			memcpy((uchar_t *)
			    zfs_uio_iovbase(digest->cd_uio, vec_idx) + offset,
			    digest_scratch, digest_len);
		} else {
			SHA2Final((uchar_t *)zfs_uio_iovbase(digest->
			    cd_uio, vec_idx) + offset,
			    sha2_ctx);

		}
	} else {
		/*
		 * The computed digest will be crossing one or more iovec's.
		 * This is bad performance-wise but we need to support it.
		 * Allocate a small scratch buffer on the stack and
		 * copy it piece meal to the specified digest iovec's.
		 */
		uchar_t digest_tmp[SHA512_DIGEST_LENGTH];
		off_t scratch_offset = 0;
		size_t length = digest_len;
		size_t cur_len;

		SHA2Final(digest_tmp, sha2_ctx);

		while (vec_idx < zfs_uio_iovcnt(digest->cd_uio) && length > 0) {
			cur_len =
			    MIN(zfs_uio_iovlen(digest->cd_uio, vec_idx) -
			    offset, length);
			memcpy(
			    zfs_uio_iovbase(digest->cd_uio, vec_idx) + offset,
			    digest_tmp + scratch_offset,
			    cur_len);

			length -= cur_len;
			vec_idx++;
			scratch_offset += cur_len;
			offset = 0;
		}

		if (vec_idx == zfs_uio_iovcnt(digest->cd_uio) && length > 0) {
			/*
			 * The end of the specified iovec's was reached but
			 * the length requested could not be processed, i.e.
			 * The caller requested to digest more data than it
			 * provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	return (CRYPTO_SUCCESS);
}

static int
sha2_digest(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *digest)
{
	int ret = CRYPTO_SUCCESS;
	uint_t sha_digest_len;

	ASSERT(ctx->cc_provider_private != NULL);

	switch (PROV_SHA2_CTX(ctx)->sc_mech_type) {
	case SHA256_MECH_INFO_TYPE:
		sha_digest_len = SHA256_DIGEST_LENGTH;
		break;
	case SHA384_MECH_INFO_TYPE:
		sha_digest_len = SHA384_DIGEST_LENGTH;
		break;
	case SHA512_MECH_INFO_TYPE:
		sha_digest_len = SHA512_DIGEST_LENGTH;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following cases.
	 */
	if ((digest->cd_length == 0) ||
	    (digest->cd_length < sha_digest_len)) {
		digest->cd_length = sha_digest_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/*
	 * Do the SHA2 update on the specified input data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Update(&PROV_SHA2_CTX(ctx)->sc_sha2_ctx,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_update_uio(&PROV_SHA2_CTX(ctx)->sc_sha2_ctx,
		    data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret != CRYPTO_SUCCESS) {
		/* the update failed, free context and bail */
		kmem_free(ctx->cc_provider_private, sizeof (sha2_ctx_t));
		ctx->cc_provider_private = NULL;
		digest->cd_length = 0;
		return (ret);
	}

	/*
	 * Do a SHA2 final, must be done separately since the digest
	 * type can be different than the input data type.
	 */
	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Final((unsigned char *)digest->cd_raw.iov_base +
		    digest->cd_offset, &PROV_SHA2_CTX(ctx)->sc_sha2_ctx);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_final_uio(&PROV_SHA2_CTX(ctx)->sc_sha2_ctx,
		    digest, sha_digest_len, NULL);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	/* all done, free context and return */

	if (ret == CRYPTO_SUCCESS)
		digest->cd_length = sha_digest_len;
	else
		digest->cd_length = 0;

	kmem_free(ctx->cc_provider_private, sizeof (sha2_ctx_t));
	ctx->cc_provider_private = NULL;
	return (ret);
}

static int
sha2_digest_update(crypto_ctx_t *ctx, crypto_data_t *data)
{
	int ret = CRYPTO_SUCCESS;

	ASSERT(ctx->cc_provider_private != NULL);

	/*
	 * Do the SHA2 update on the specified input data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Update(&PROV_SHA2_CTX(ctx)->sc_sha2_ctx,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_update_uio(&PROV_SHA2_CTX(ctx)->sc_sha2_ctx,
		    data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	return (ret);
}

static int
sha2_digest_final(crypto_ctx_t *ctx, crypto_data_t *digest)
{
	int ret = CRYPTO_SUCCESS;
	uint_t sha_digest_len;

	ASSERT(ctx->cc_provider_private != NULL);

	switch (PROV_SHA2_CTX(ctx)->sc_mech_type) {
	case SHA256_MECH_INFO_TYPE:
		sha_digest_len = SHA256_DIGEST_LENGTH;
		break;
	case SHA384_MECH_INFO_TYPE:
		sha_digest_len = SHA384_DIGEST_LENGTH;
		break;
	case SHA512_MECH_INFO_TYPE:
		sha_digest_len = SHA512_DIGEST_LENGTH;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following cases.
	 */
	if ((digest->cd_length == 0) ||
	    (digest->cd_length < sha_digest_len)) {
		digest->cd_length = sha_digest_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/*
	 * Do a SHA2 final.
	 */
	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Final((unsigned char *)digest->cd_raw.iov_base +
		    digest->cd_offset, &PROV_SHA2_CTX(ctx)->sc_sha2_ctx);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_final_uio(&PROV_SHA2_CTX(ctx)->sc_sha2_ctx,
		    digest, sha_digest_len, NULL);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	/* all done, free context and return */

	if (ret == CRYPTO_SUCCESS)
		digest->cd_length = sha_digest_len;
	else
		digest->cd_length = 0;

	kmem_free(ctx->cc_provider_private, sizeof (sha2_ctx_t));
	ctx->cc_provider_private = NULL;

	return (ret);
}

static int
sha2_digest_atomic(crypto_mechanism_t *mechanism, crypto_data_t *data,
    crypto_data_t *digest)
{
	int ret = CRYPTO_SUCCESS;
	SHA2_CTX sha2_ctx;
	uint32_t sha_digest_len;

	/*
	 * Do the SHA inits.
	 */

	SHA2Init(mechanism->cm_type, &sha2_ctx);

	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Update(&sha2_ctx, (uint8_t *)data->
		    cd_raw.iov_base + data->cd_offset, data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_update_uio(&sha2_ctx, data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	/*
	 * Do the SHA updates on the specified input data.
	 */

	if (ret != CRYPTO_SUCCESS) {
		/* the update failed, bail */
		digest->cd_length = 0;
		return (ret);
	}

	if (mechanism->cm_type <= SHA256_HMAC_GEN_MECH_INFO_TYPE)
		sha_digest_len = SHA256_DIGEST_LENGTH;
	else
		sha_digest_len = SHA512_DIGEST_LENGTH;

	/*
	 * Do a SHA2 final, must be done separately since the digest
	 * type can be different than the input data type.
	 */
	switch (digest->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Final((unsigned char *)digest->cd_raw.iov_base +
		    digest->cd_offset, &sha2_ctx);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_final_uio(&sha2_ctx, digest,
		    sha_digest_len, NULL);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS)
		digest->cd_length = sha_digest_len;
	else
		digest->cd_length = 0;

	return (ret);
}

/*
 * KCF software provider mac entry points.
 *
 * SHA2 HMAC is: SHA2(key XOR opad, SHA2(key XOR ipad, text))
 *
 * Init:
 * The initialization routine initializes what we denote
 * as the inner and outer contexts by doing
 * - for inner context: SHA2(key XOR ipad)
 * - for outer context: SHA2(key XOR opad)
 *
 * Update:
 * Each subsequent SHA2 HMAC update will result in an
 * update of the inner context with the specified data.
 *
 * Final:
 * The SHA2 HMAC final will do a SHA2 final operation on the
 * inner context, and the resulting digest will be used
 * as the data for an update on the outer context. Last
 * but not least, a SHA2 final on the outer context will
 * be performed to obtain the SHA2 HMAC digest to return
 * to the user.
 */

/*
 * Initialize a SHA2-HMAC context.
 */
static void
sha2_mac_init_ctx(sha2_hmac_ctx_t *ctx, void *keyval, uint_t length_in_bytes)
{
	uint64_t ipad[SHA512_HMAC_BLOCK_SIZE / sizeof (uint64_t)] = {0};
	uint64_t opad[SHA512_HMAC_BLOCK_SIZE / sizeof (uint64_t)] = {0};
	int i, block_size, blocks_per_int64;

	/* Determine the block size */
	if (ctx->hc_mech_type <= SHA256_HMAC_GEN_MECH_INFO_TYPE) {
		block_size = SHA256_HMAC_BLOCK_SIZE;
		blocks_per_int64 = SHA256_HMAC_BLOCK_SIZE / sizeof (uint64_t);
	} else {
		block_size = SHA512_HMAC_BLOCK_SIZE;
		blocks_per_int64 = SHA512_HMAC_BLOCK_SIZE / sizeof (uint64_t);
	}

	(void) memset(ipad, 0, block_size);
	(void) memset(opad, 0, block_size);

	if (keyval != NULL) {
		(void) memcpy(ipad, keyval, length_in_bytes);
		(void) memcpy(opad, keyval, length_in_bytes);
	} else {
		ASSERT0(length_in_bytes);
	}

	/* XOR key with ipad (0x36) and opad (0x5c) */
	for (i = 0; i < blocks_per_int64; i ++) {
		ipad[i] ^= 0x3636363636363636;
		opad[i] ^= 0x5c5c5c5c5c5c5c5c;
	}

	/* perform SHA2 on ipad */
	SHA2Init(ctx->hc_mech_type, &ctx->hc_icontext);
	SHA2Update(&ctx->hc_icontext, (uint8_t *)ipad, block_size);

	/* perform SHA2 on opad */
	SHA2Init(ctx->hc_mech_type, &ctx->hc_ocontext);
	SHA2Update(&ctx->hc_ocontext, (uint8_t *)opad, block_size);
}

/*
 */
static int
sha2_mac_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template)
{
	int ret = CRYPTO_SUCCESS;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);
	uint_t sha_digest_len, sha_hmac_block_size;

	/*
	 * Set the digest length and block size to values appropriate to the
	 * mechanism
	 */
	switch (mechanism->cm_type) {
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = SHA256_DIGEST_LENGTH;
		sha_hmac_block_size = SHA256_HMAC_BLOCK_SIZE;
		break;
	case SHA384_HMAC_MECH_INFO_TYPE:
	case SHA384_HMAC_GEN_MECH_INFO_TYPE:
	case SHA512_HMAC_MECH_INFO_TYPE:
	case SHA512_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = SHA512_DIGEST_LENGTH;
		sha_hmac_block_size = SHA512_HMAC_BLOCK_SIZE;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	ctx->cc_provider_private =
	    kmem_alloc(sizeof (sha2_hmac_ctx_t), KM_SLEEP);
	if (ctx->cc_provider_private == NULL)
		return (CRYPTO_HOST_MEMORY);

	PROV_SHA2_HMAC_CTX(ctx)->hc_mech_type = mechanism->cm_type;
	if (ctx_template != NULL) {
		/* reuse context template */
		memcpy(PROV_SHA2_HMAC_CTX(ctx), ctx_template,
		    sizeof (sha2_hmac_ctx_t));
	} else {
		/* no context template, compute context */
		if (keylen_in_bytes > sha_hmac_block_size) {
			uchar_t digested_key[SHA512_DIGEST_LENGTH];
			sha2_hmac_ctx_t *hmac_ctx = ctx->cc_provider_private;

			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA2_DIGEST_KEY(mechanism->cm_type / 3,
			    &hmac_ctx->hc_icontext,
			    key->ck_data, keylen_in_bytes, digested_key);
			sha2_mac_init_ctx(PROV_SHA2_HMAC_CTX(ctx),
			    digested_key, sha_digest_len);
		} else {
			sha2_mac_init_ctx(PROV_SHA2_HMAC_CTX(ctx),
			    key->ck_data, keylen_in_bytes);
		}
	}

	/*
	 * Get the mechanism parameters, if applicable.
	 */
	if (mechanism->cm_type % 3 == 2) {
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (ulong_t))
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
		PROV_SHA2_GET_DIGEST_LEN(mechanism,
		    PROV_SHA2_HMAC_CTX(ctx)->hc_digest_len);
		if (PROV_SHA2_HMAC_CTX(ctx)->hc_digest_len > sha_digest_len)
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
	}

	if (ret != CRYPTO_SUCCESS) {
		memset(ctx->cc_provider_private, 0, sizeof (sha2_hmac_ctx_t));
		kmem_free(ctx->cc_provider_private, sizeof (sha2_hmac_ctx_t));
		ctx->cc_provider_private = NULL;
	}

	return (ret);
}

static int
sha2_mac_update(crypto_ctx_t *ctx, crypto_data_t *data)
{
	int ret = CRYPTO_SUCCESS;

	ASSERT(ctx->cc_provider_private != NULL);

	/*
	 * Do a SHA2 update of the inner context using the specified
	 * data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Update(&PROV_SHA2_HMAC_CTX(ctx)->hc_icontext,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_update_uio(
		    &PROV_SHA2_HMAC_CTX(ctx)->hc_icontext, data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	return (ret);
}

static int
sha2_mac_final(crypto_ctx_t *ctx, crypto_data_t *mac)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA512_DIGEST_LENGTH];
	uint32_t digest_len, sha_digest_len;

	ASSERT(ctx->cc_provider_private != NULL);

	/* Set the digest lengths to values appropriate to the mechanism */
	switch (PROV_SHA2_HMAC_CTX(ctx)->hc_mech_type) {
	case SHA256_HMAC_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA256_DIGEST_LENGTH;
		break;
	case SHA384_HMAC_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA384_DIGEST_LENGTH;
		break;
	case SHA512_HMAC_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA512_DIGEST_LENGTH;
		break;
	case SHA256_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = SHA256_DIGEST_LENGTH;
		digest_len = PROV_SHA2_HMAC_CTX(ctx)->hc_digest_len;
		break;
	case SHA384_HMAC_GEN_MECH_INFO_TYPE:
	case SHA512_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = SHA512_DIGEST_LENGTH;
		digest_len = PROV_SHA2_HMAC_CTX(ctx)->hc_digest_len;
		break;
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * We need to just return the length needed to store the output.
	 * We should not destroy the context for the following cases.
	 */
	if ((mac->cd_length == 0) || (mac->cd_length < digest_len)) {
		mac->cd_length = digest_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/*
	 * Do a SHA2 final on the inner context.
	 */
	SHA2Final(digest, &PROV_SHA2_HMAC_CTX(ctx)->hc_icontext);

	/*
	 * Do a SHA2 update on the outer context, feeding the inner
	 * digest as data.
	 */
	SHA2Update(&PROV_SHA2_HMAC_CTX(ctx)->hc_ocontext, digest,
	    sha_digest_len);

	/*
	 * Do a SHA2 final on the outer context, storing the computing
	 * digest in the users buffer.
	 */
	switch (mac->cd_format) {
	case CRYPTO_DATA_RAW:
		if (digest_len != sha_digest_len) {
			/*
			 * The caller requested a short digest. Digest
			 * into a scratch buffer and return to
			 * the user only what was requested.
			 */
			SHA2Final(digest,
			    &PROV_SHA2_HMAC_CTX(ctx)->hc_ocontext);
			memcpy((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, digest, digest_len);
		} else {
			SHA2Final((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset,
			    &PROV_SHA2_HMAC_CTX(ctx)->hc_ocontext);
		}
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_final_uio(
		    &PROV_SHA2_HMAC_CTX(ctx)->hc_ocontext, mac,
		    digest_len, digest);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS)
		mac->cd_length = digest_len;
	else
		mac->cd_length = 0;

	memset(ctx->cc_provider_private, 0, sizeof (sha2_hmac_ctx_t));
	kmem_free(ctx->cc_provider_private, sizeof (sha2_hmac_ctx_t));
	ctx->cc_provider_private = NULL;

	return (ret);
}

#define	SHA2_MAC_UPDATE(data, ctx, ret) {				\
	switch (data->cd_format) {					\
	case CRYPTO_DATA_RAW:						\
		SHA2Update(&(ctx).hc_icontext,				\
		    (uint8_t *)data->cd_raw.iov_base +			\
		    data->cd_offset, data->cd_length);			\
		break;							\
	case CRYPTO_DATA_UIO:						\
		ret = sha2_digest_update_uio(&(ctx).hc_icontext, data);	\
		break;							\
	default:							\
		ret = CRYPTO_ARGUMENTS_BAD;				\
	}								\
}

static int
sha2_mac_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t ctx_template)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA512_DIGEST_LENGTH];
	sha2_hmac_ctx_t sha2_hmac_ctx;
	uint32_t sha_digest_len, digest_len, sha_hmac_block_size;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);

	/*
	 * Set the digest length and block size to values appropriate to the
	 * mechanism
	 */
	switch (mechanism->cm_type) {
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA256_DIGEST_LENGTH;
		sha_hmac_block_size = SHA256_HMAC_BLOCK_SIZE;
		break;
	case SHA384_HMAC_MECH_INFO_TYPE:
	case SHA384_HMAC_GEN_MECH_INFO_TYPE:
	case SHA512_HMAC_MECH_INFO_TYPE:
	case SHA512_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA512_DIGEST_LENGTH;
		sha_hmac_block_size = SHA512_HMAC_BLOCK_SIZE;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	if (ctx_template != NULL) {
		/* reuse context template */
		memcpy(&sha2_hmac_ctx, ctx_template, sizeof (sha2_hmac_ctx_t));
	} else {
		sha2_hmac_ctx.hc_mech_type = mechanism->cm_type;
		/* no context template, initialize context */
		if (keylen_in_bytes > sha_hmac_block_size) {
			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA2_DIGEST_KEY(mechanism->cm_type / 3,
			    &sha2_hmac_ctx.hc_icontext,
			    key->ck_data, keylen_in_bytes, digest);
			sha2_mac_init_ctx(&sha2_hmac_ctx, digest,
			    sha_digest_len);
		} else {
			sha2_mac_init_ctx(&sha2_hmac_ctx, key->ck_data,
			    keylen_in_bytes);
		}
	}

	/* get the mechanism parameters, if applicable */
	if ((mechanism->cm_type % 3) == 2) {
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (ulong_t)) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
		PROV_SHA2_GET_DIGEST_LEN(mechanism, digest_len);
		if (digest_len > sha_digest_len) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
	}

	/* do a SHA2 update of the inner context using the specified data */
	SHA2_MAC_UPDATE(data, sha2_hmac_ctx, ret);
	if (ret != CRYPTO_SUCCESS)
		/* the update failed, free context and bail */
		goto bail;

	/*
	 * Do a SHA2 final on the inner context.
	 */
	SHA2Final(digest, &sha2_hmac_ctx.hc_icontext);

	/*
	 * Do an SHA2 update on the outer context, feeding the inner
	 * digest as data.
	 *
	 * HMAC-SHA384 needs special handling as the outer hash needs only 48
	 * bytes of the inner hash value.
	 */
	if (mechanism->cm_type == SHA384_HMAC_MECH_INFO_TYPE ||
	    mechanism->cm_type == SHA384_HMAC_GEN_MECH_INFO_TYPE)
		SHA2Update(&sha2_hmac_ctx.hc_ocontext, digest,
		    SHA384_DIGEST_LENGTH);
	else
		SHA2Update(&sha2_hmac_ctx.hc_ocontext, digest, sha_digest_len);

	/*
	 * Do a SHA2 final on the outer context, storing the computed
	 * digest in the users buffer.
	 */
	switch (mac->cd_format) {
	case CRYPTO_DATA_RAW:
		if (digest_len != sha_digest_len) {
			/*
			 * The caller requested a short digest. Digest
			 * into a scratch buffer and return to
			 * the user only what was requested.
			 */
			SHA2Final(digest, &sha2_hmac_ctx.hc_ocontext);
			memcpy((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, digest, digest_len);
		} else {
			SHA2Final((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, &sha2_hmac_ctx.hc_ocontext);
		}
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_final_uio(&sha2_hmac_ctx.hc_ocontext, mac,
		    digest_len, digest);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS) {
		mac->cd_length = digest_len;
		return (CRYPTO_SUCCESS);
	}
bail:
	memset(&sha2_hmac_ctx, 0, sizeof (sha2_hmac_ctx_t));
	mac->cd_length = 0;
	return (ret);
}

static int
sha2_mac_verify_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *mac,
    crypto_spi_ctx_template_t ctx_template)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA512_DIGEST_LENGTH];
	sha2_hmac_ctx_t sha2_hmac_ctx;
	uint32_t sha_digest_len, digest_len, sha_hmac_block_size;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);

	/*
	 * Set the digest length and block size to values appropriate to the
	 * mechanism
	 */
	switch (mechanism->cm_type) {
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA256_DIGEST_LENGTH;
		sha_hmac_block_size = SHA256_HMAC_BLOCK_SIZE;
		break;
	case SHA384_HMAC_MECH_INFO_TYPE:
	case SHA384_HMAC_GEN_MECH_INFO_TYPE:
	case SHA512_HMAC_MECH_INFO_TYPE:
	case SHA512_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA512_DIGEST_LENGTH;
		sha_hmac_block_size = SHA512_HMAC_BLOCK_SIZE;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	if (ctx_template != NULL) {
		/* reuse context template */
		memcpy(&sha2_hmac_ctx, ctx_template, sizeof (sha2_hmac_ctx_t));
	} else {
		sha2_hmac_ctx.hc_mech_type = mechanism->cm_type;
		/* no context template, initialize context */
		if (keylen_in_bytes > sha_hmac_block_size) {
			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA2_DIGEST_KEY(mechanism->cm_type / 3,
			    &sha2_hmac_ctx.hc_icontext,
			    key->ck_data, keylen_in_bytes, digest);
			sha2_mac_init_ctx(&sha2_hmac_ctx, digest,
			    sha_digest_len);
		} else {
			sha2_mac_init_ctx(&sha2_hmac_ctx, key->ck_data,
			    keylen_in_bytes);
		}
	}

	/* get the mechanism parameters, if applicable */
	if (mechanism->cm_type % 3 == 2) {
		if (mechanism->cm_param == NULL ||
		    mechanism->cm_param_len != sizeof (ulong_t)) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
		PROV_SHA2_GET_DIGEST_LEN(mechanism, digest_len);
		if (digest_len > sha_digest_len) {
			ret = CRYPTO_MECHANISM_PARAM_INVALID;
			goto bail;
		}
	}

	if (mac->cd_length != digest_len) {
		ret = CRYPTO_INVALID_MAC;
		goto bail;
	}

	/* do a SHA2 update of the inner context using the specified data */
	SHA2_MAC_UPDATE(data, sha2_hmac_ctx, ret);
	if (ret != CRYPTO_SUCCESS)
		/* the update failed, free context and bail */
		goto bail;

	/* do a SHA2 final on the inner context */
	SHA2Final(digest, &sha2_hmac_ctx.hc_icontext);

	/*
	 * Do an SHA2 update on the outer context, feeding the inner
	 * digest as data.
	 *
	 * HMAC-SHA384 needs special handling as the outer hash needs only 48
	 * bytes of the inner hash value.
	 */
	if (mechanism->cm_type == SHA384_HMAC_MECH_INFO_TYPE ||
	    mechanism->cm_type == SHA384_HMAC_GEN_MECH_INFO_TYPE)
		SHA2Update(&sha2_hmac_ctx.hc_ocontext, digest,
		    SHA384_DIGEST_LENGTH);
	else
		SHA2Update(&sha2_hmac_ctx.hc_ocontext, digest, sha_digest_len);

	/*
	 * Do a SHA2 final on the outer context, storing the computed
	 * digest in the users buffer.
	 */
	SHA2Final(digest, &sha2_hmac_ctx.hc_ocontext);

	/*
	 * Compare the computed digest against the expected digest passed
	 * as argument.
	 */

	switch (mac->cd_format) {

	case CRYPTO_DATA_RAW:
		if (memcmp(digest, (unsigned char *)mac->cd_raw.iov_base +
		    mac->cd_offset, digest_len) != 0)
			ret = CRYPTO_INVALID_MAC;
		break;

	case CRYPTO_DATA_UIO: {
		off_t offset = mac->cd_offset;
		uint_t vec_idx = 0;
		off_t scratch_offset = 0;
		size_t length = digest_len;
		size_t cur_len;

		/* we support only kernel buffer */
		if (zfs_uio_segflg(mac->cd_uio) != UIO_SYSSPACE)
			return (CRYPTO_ARGUMENTS_BAD);

		/* jump to the first iovec containing the expected digest */
		offset = zfs_uio_index_at_offset(mac->cd_uio, offset, &vec_idx);
		if (vec_idx == zfs_uio_iovcnt(mac->cd_uio)) {
			/*
			 * The caller specified an offset that is
			 * larger than the total size of the buffers
			 * it provided.
			 */
			ret = CRYPTO_DATA_LEN_RANGE;
			break;
		}

		/* do the comparison of computed digest vs specified one */
		while (vec_idx < zfs_uio_iovcnt(mac->cd_uio) && length > 0) {
			cur_len = MIN(zfs_uio_iovlen(mac->cd_uio, vec_idx) -
			    offset, length);

			if (memcmp(digest + scratch_offset,
			    zfs_uio_iovbase(mac->cd_uio, vec_idx) + offset,
			    cur_len) != 0) {
				ret = CRYPTO_INVALID_MAC;
				break;
			}

			length -= cur_len;
			vec_idx++;
			scratch_offset += cur_len;
			offset = 0;
		}
		break;
	}

	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	return (ret);
bail:
	memset(&sha2_hmac_ctx, 0, sizeof (sha2_hmac_ctx_t));
	mac->cd_length = 0;
	return (ret);
}

/*
 * KCF software provider context management entry points.
 */

static int
sha2_create_ctx_template(crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t *ctx_template, size_t *ctx_template_size)
{
	sha2_hmac_ctx_t *sha2_hmac_ctx_tmpl;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);
	uint32_t sha_digest_len, sha_hmac_block_size;

	/*
	 * Set the digest length and block size to values appropriate to the
	 * mechanism
	 */
	switch (mechanism->cm_type) {
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = SHA256_DIGEST_LENGTH;
		sha_hmac_block_size = SHA256_HMAC_BLOCK_SIZE;
		break;
	case SHA384_HMAC_MECH_INFO_TYPE:
	case SHA384_HMAC_GEN_MECH_INFO_TYPE:
	case SHA512_HMAC_MECH_INFO_TYPE:
	case SHA512_HMAC_GEN_MECH_INFO_TYPE:
		sha_digest_len = SHA512_DIGEST_LENGTH;
		sha_hmac_block_size = SHA512_HMAC_BLOCK_SIZE;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	/*
	 * Allocate and initialize SHA2 context.
	 */
	sha2_hmac_ctx_tmpl = kmem_alloc(sizeof (sha2_hmac_ctx_t), KM_SLEEP);
	if (sha2_hmac_ctx_tmpl == NULL)
		return (CRYPTO_HOST_MEMORY);

	sha2_hmac_ctx_tmpl->hc_mech_type = mechanism->cm_type;

	if (keylen_in_bytes > sha_hmac_block_size) {
		uchar_t digested_key[SHA512_DIGEST_LENGTH];

		/*
		 * Hash the passed-in key to get a smaller key.
		 * The inner context is used since it hasn't been
		 * initialized yet.
		 */
		PROV_SHA2_DIGEST_KEY(mechanism->cm_type / 3,
		    &sha2_hmac_ctx_tmpl->hc_icontext,
		    key->ck_data, keylen_in_bytes, digested_key);
		sha2_mac_init_ctx(sha2_hmac_ctx_tmpl, digested_key,
		    sha_digest_len);
	} else {
		sha2_mac_init_ctx(sha2_hmac_ctx_tmpl, key->ck_data,
		    keylen_in_bytes);
	}

	*ctx_template = (crypto_spi_ctx_template_t)sha2_hmac_ctx_tmpl;
	*ctx_template_size = sizeof (sha2_hmac_ctx_t);

	return (CRYPTO_SUCCESS);
}

static int
sha2_free_context(crypto_ctx_t *ctx)
{
	uint_t ctx_len;

	if (ctx->cc_provider_private == NULL)
		return (CRYPTO_SUCCESS);

	/*
	 * We have to free either SHA2 or SHA2-HMAC contexts, which
	 * have different lengths.
	 *
	 * Note: Below is dependent on the mechanism ordering.
	 */

	if (PROV_SHA2_CTX(ctx)->sc_mech_type % 3 == 0)
		ctx_len = sizeof (sha2_ctx_t);
	else
		ctx_len = sizeof (sha2_hmac_ctx_t);

	memset(ctx->cc_provider_private, 0, ctx_len);
	kmem_free(ctx->cc_provider_private, ctx_len);
	ctx->cc_provider_private = NULL;

	return (CRYPTO_SUCCESS);
}
