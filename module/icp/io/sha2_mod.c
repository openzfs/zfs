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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/sha2.h>
#include <sha2/sha2_impl.h>

#define	PROV_SHA2_DIGEST_KEY(mech, ctx, key, len, digest) {	\
	SHA2Init(mech, ctx);				\
	SHA2Update(ctx, key, len);			\
	SHA2Final(digest, ctx);				\
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
		ASSERT3U(sha2_ctx->algotype, ==, SHA512_HMAC_MECH_INFO_TYPE);
		if (digest_len != SHA512_DIGEST_LENGTH) {
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

/*
 * SHA2 MAC entry points.
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
	ASSERT3U(ctx->hc_mech_type, ==, SHA512_HMAC_MECH_INFO_TYPE);
	block_size = SHA512_HMAC_BLOCK_SIZE;
	blocks_per_int64 = SHA512_HMAC_BLOCK_SIZE / sizeof (uint64_t);

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

int
icp_sha2_mac_init(crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t ctx_template, crypto_context_t *ctxp)
{
	sha2_hmac_ctx_t *ctx;
	uint_t keylen_in_bytes = CRYPTO_BITS2BYTES(key->ck_length);
	uint_t sha_digest_len, sha_hmac_block_size;

	/*
	 * Set the digest length and block size to values appropriate to the
	 * mechanism
	 */
	switch (mechanism->cm_type) {
	case SHA512_HMAC_MECH_INFO_TYPE:
		sha_digest_len = SHA512_DIGEST_LENGTH;
		sha_hmac_block_size = SHA512_HMAC_BLOCK_SIZE;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	ctx = kmem_alloc(sizeof (*ctx), KM_SLEEP);

	ctx->hc_mech_type = mechanism->cm_type;
	if (ctx_template != NULL) {
		/* reuse context template */
		memcpy(ctx, ctx_template, sizeof (*ctx));
	} else {
		/* no context template, compute context */
		if (keylen_in_bytes > sha_hmac_block_size) {
			uchar_t digested_key[SHA512_DIGEST_LENGTH];

			/*
			 * Hash the passed-in key to get a smaller key.
			 * The inner context is used since it hasn't been
			 * initialized yet.
			 */
			PROV_SHA2_DIGEST_KEY(mechanism->cm_type / 3,
			    &ctx->hc_icontext,
			    key->ck_data, keylen_in_bytes, digested_key);
			sha2_mac_init_ctx(ctx,
			    digested_key, sha_digest_len);
		} else {
			sha2_mac_init_ctx(ctx,
			    key->ck_data, keylen_in_bytes);
		}
	}

	*ctxp = ctx;
	return (CRYPTO_SUCCESS);
}

int
icp_sha2_mac_update(sha2_hmac_ctx_t *ctx, crypto_data_t *data)
{
	int ret = CRYPTO_SUCCESS;

	/*
	 * Do a SHA2 update of the inner context using the specified
	 * data.
	 */
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		SHA2Update(&ctx->hc_icontext,
		    (uint8_t *)data->cd_raw.iov_base + data->cd_offset,
		    data->cd_length);
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_update_uio(
		    &ctx->hc_icontext, data);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	return (ret);
}

int
icp_sha2_mac_final(sha2_hmac_ctx_t *ctx, crypto_data_t *mac)
{
	int ret = CRYPTO_SUCCESS;
	uchar_t digest[SHA512_DIGEST_LENGTH];
	uint32_t digest_len, sha_digest_len;

	/* Set the digest lengths to values appropriate to the mechanism */
	switch (ctx->hc_mech_type) {
	case SHA512_HMAC_MECH_INFO_TYPE:
		sha_digest_len = digest_len = SHA512_DIGEST_LENGTH;
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
	SHA2Final(digest, &ctx->hc_icontext);

	/*
	 * Do a SHA2 update on the outer context, feeding the inner
	 * digest as data.
	 */
	SHA2Update(&ctx->hc_ocontext, digest,
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
			    &ctx->hc_ocontext);
			memcpy((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset, digest, digest_len);
		} else {
			SHA2Final((unsigned char *)mac->cd_raw.iov_base +
			    mac->cd_offset,
			    &ctx->hc_ocontext);
		}
		break;
	case CRYPTO_DATA_UIO:
		ret = sha2_digest_final_uio(
		    &ctx->hc_ocontext, mac,
		    digest_len, digest);
		break;
	default:
		ret = CRYPTO_ARGUMENTS_BAD;
	}

	if (ret == CRYPTO_SUCCESS)
		mac->cd_length = digest_len;
	else
		mac->cd_length = 0;

	memset(ctx, 0, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));

	return (ret);
}

int
icp_sha2_mac_atomic(crypto_mechanism_t *mechanism,
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
	case SHA512_HMAC_MECH_INFO_TYPE:
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

	/* do a SHA2 update of the inner context using the specified data */
	ret = icp_sha2_mac_update(&sha2_hmac_ctx, data);
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
	 */
	ASSERT3U(mechanism->cm_type, ==, SHA512_HMAC_MECH_INFO_TYPE);
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

/* SHA2 context management entry points. */

int
icp_sha2_create_ctx_template(crypto_mechanism_t *mechanism, crypto_key_t *key,
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
	case SHA512_HMAC_MECH_INFO_TYPE:
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
