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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

/*
 * ChaCha20-Poly1305 (RFC 8439) provider
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/icp.h>
#include <monocypher.h>

/*
 * convenient constants for readability. you can't change these; they're fixed
 * to match defines and buffer sizes elsewhere.
 */
#define	CP_BLOCK_SIZE	(64)
#define	CP_KEY_SIZE	(32)
#define	CP_MAC_SIZE	(16)
#define	CP_IV_SIZE	(12)

typedef struct {
	/* pointers back to the callers key and iv */
	const uint8_t		*key;
	const uint8_t		*iv;

	/* poly1305 mac state */
	crypto_poly1305_ctx	poly;

	/* counter value for next block */
	uint32_t		counter;

	/* cipher output buffer and working space */
	uint8_t			temp[CP_BLOCK_SIZE];

	/* bytes waiting for a complete block before they can be encrypted */
	uint8_t			pending[CP_BLOCK_SIZE];
	size_t			npending;

	/* decrypt; data bytes remaining */
	size_t			datalen;

	/*
	 * decrypt; pointer to pre-auth holding buffer
	 * (extra allocation past end of chapoly_ctx)
	 */
	uint8_t			*unauthp;
} chapoly_ctx;

/* a bunch of zeroes for padding the poly1305 sequence */
static const uint8_t zero_pad[16] = {0};

static void
chapoly_init(chapoly_ctx *cpctx, const crypto_key_t *key, const uint8_t *iv)
{
	cpctx->key = (const uint8_t *) key->ck_data;
	cpctx->iv  = (const uint8_t *) iv;

	/* create the poly1305 key from the chacha block 0 keystream */
	cpctx->counter = crypto_chacha20_ietf(
	    cpctx->temp, NULL, CP_KEY_SIZE,
	    cpctx->key, cpctx->iv, 0);

	/* and intialise the context */
	crypto_poly1305_init(&cpctx->poly, cpctx->temp);
}


static int
chapoly_encrypt_contiguous_blocks(
	void *_cpctx, char *data, size_t length, crypto_data_t *out)
{
	chapoly_ctx *cpctx = (chapoly_ctx *) _cpctx;

	uint8_t *datap = (uint8_t *)data;
	size_t nremaining = length;

	size_t need;
	int rv;

	/* if there's anything in the pending buffer, try to empty it */
	if (cpctx->npending > 0) {
		/*
		 * take no more than we need to fill the temp buffer
		 * (one block), otherwise whatever is left
		 */
		need = nremaining > CP_BLOCK_SIZE - cpctx->npending ?
		    CP_BLOCK_SIZE - cpctx->npending : nremaining;

		/* try fill that buffer */
		memcpy(cpctx->pending + cpctx->npending, datap, need);
		datap += need;
		nremaining -= need;
		cpctx->npending += need;

		/*
		 * if we consumed everything and there's still not a full
		 * block then we've done all we can for now
		 */
		if (cpctx->npending < CP_BLOCK_SIZE) {
			ASSERT0(nremaining);
			return (CRYPTO_SUCCESS);
		}

		/* full block pending, process it */
		cpctx->counter = crypto_chacha20_ietf(
		    cpctx->temp, cpctx->pending, CP_BLOCK_SIZE,
		    cpctx->key, cpctx->iv, cpctx->counter);

		/* copy it to the output buffers */
		rv = crypto_put_output_data(cpctx->temp, out, CP_BLOCK_SIZE);
		if (rv != CRYPTO_SUCCESS)
			return (rv);

		/* update offset */
		out->cd_offset += CP_BLOCK_SIZE;

		/* update the mac */
		crypto_poly1305_update(
		    &cpctx->poly, cpctx->temp, CP_BLOCK_SIZE);

		/* pending buffer now drained */
		cpctx->npending = 0;
	}

	/* process as many complete blocks as we can */
	while (nremaining >= CP_BLOCK_SIZE) {

		/* process one block */
		cpctx->counter = crypto_chacha20_ietf(
		    cpctx->temp, datap, CP_BLOCK_SIZE,
		    cpctx->key, cpctx->iv, cpctx->counter);

		/* copy it to the output buffers */
		rv = crypto_put_output_data(cpctx->temp, out, CP_BLOCK_SIZE);
		if (rv != CRYPTO_SUCCESS)
			return (rv);

		/* update offset */
		out->cd_offset += CP_BLOCK_SIZE;

		/* update the mac */
		crypto_poly1305_update(
		    &cpctx->poly, cpctx->temp, CP_BLOCK_SIZE);

		/* done a block */
		datap += CP_BLOCK_SIZE;
		nremaining -= CP_BLOCK_SIZE;
	}

	/* buffer anything left over for next time */
	if (nremaining > 0) {
		ASSERT3U(nremaining, <, CP_BLOCK_SIZE);

		memcpy(cpctx->pending, datap, nremaining);
		cpctx->npending = nremaining;
	}

	return (CRYPTO_SUCCESS);
}

static int
chapoly_encrypt_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t __attribute__((unused)) template)
{
	int rv;

	/*
	 * We don't actually do GCM here, its just the default parameter
	 * option in zio_do_crypt_uio and has everything we need, so its
	 * easier to just take that instead of making our own thing.
	 */
	const CK_AES_GCM_PARAMS *gcmp =
	    (CK_AES_GCM_PARAMS *) mechanism->cm_param;
	const uint8_t *iv = gcmp->pIv;

	/* chacha20 invariants */
	if (CRYPTO_BITS2BYTES(key->ck_length) != CP_KEY_SIZE)
		return (CRYPTO_KEY_SIZE_RANGE);
	if (gcmp->ulIvLen != CP_IV_SIZE)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	chapoly_ctx *cpctx = kmem_alloc(sizeof (chapoly_ctx), KM_SLEEP);
	if (cpctx == NULL)
		return (CRYPTO_HOST_MEMORY);
	memset(cpctx, 0, sizeof (chapoly_ctx));

	chapoly_init(cpctx, key, iv);

	/* mix additional data into the mac */
	crypto_poly1305_update(&cpctx->poly, gcmp->pAAD, gcmp->ulAADLen);
	crypto_poly1305_update(
	    &cpctx->poly, zero_pad, (~(gcmp->ulAADLen) + 1) & 0xf);

	off_t saved_offset = ciphertext->cd_offset;
	size_t saved_length = ciphertext->cd_length;

	switch (plaintext->cd_format) {
	case CRYPTO_DATA_RAW:
		rv = crypto_update_iov(
		    cpctx, plaintext, ciphertext,
		    chapoly_encrypt_contiguous_blocks);
		break;
	case CRYPTO_DATA_UIO:
		rv = crypto_update_uio(
		    cpctx, plaintext, ciphertext,
		    chapoly_encrypt_contiguous_blocks);
		break;
	default:
		rv = CRYPTO_ARGUMENTS_BAD;
	}

	if (rv == CRYPTO_SUCCESS) {
		/* process and emit anything in the pending buffer */
		if (cpctx->npending > 0) {
			crypto_chacha20_ietf(
			    cpctx->temp, cpctx->pending, cpctx->npending,
			    cpctx->key, cpctx->iv, cpctx->counter);

			/* write the last bit of the ciphertext */
			rv = crypto_put_output_data(
			    cpctx->temp, ciphertext, cpctx->npending);
			if (rv != CRYPTO_SUCCESS)
				goto out;
			ciphertext->cd_offset += cpctx->npending;

			/* and update the mac */
			crypto_poly1305_update(
			    &cpctx->poly, cpctx->temp, cpctx->npending);
		}

		/* finish the mac */
		uint64_t sizes[2] = {
		    LE_64(gcmp->ulAADLen), LE_64(plaintext->cd_length)
		};
		crypto_poly1305_update(
		    &cpctx->poly, zero_pad,
		    (~(plaintext->cd_length) + 1) & 0xf);
		crypto_poly1305_update(&cpctx->poly, (uint8_t *)sizes, 16);
		crypto_poly1305_final(&cpctx->poly, cpctx->temp);

		/* and write it out */
		rv = crypto_put_output_data(
		    cpctx->temp, ciphertext, CP_MAC_SIZE);
		if (rv != CRYPTO_SUCCESS)
			goto out;
		ciphertext->cd_offset += CP_MAC_SIZE;

		ciphertext->cd_length = ciphertext->cd_offset - saved_offset;
	}
	else
		ciphertext->cd_length = saved_length;
	ciphertext->cd_offset = saved_offset;

out:
	crypto_wipe(cpctx, sizeof (chapoly_ctx));
	kmem_free(cpctx, sizeof (chapoly_ctx));
	return (rv);
}


static int
chapoly_decrypt_contiguous_blocks(
	void *_cpctx, char *data, size_t length,
	crypto_data_t __attribute__((unused)) *out)
{
	chapoly_ctx *cpctx = (chapoly_ctx *) _cpctx;
	size_t need;

	if (cpctx->datalen > 0) {
		/* these are data bytes */

		/* don't take more than we need; the mac might be on the end */
		need = length > cpctx->datalen ? cpctx->datalen : length;

		/* copy the ciphertext to a buffer we made for it */
		memcpy(cpctx->unauthp, data, need);
		cpctx->unauthp += need;
		cpctx->datalen -= need;

		/* update the mac */
		crypto_poly1305_update(&cpctx->poly, (uint8_t *)data, need);

		/* update how much we're still expecting */
		length -= need;
		data += need;

		/* if we consumed the whole buffer, we're done */
		if (length == 0)
			return (CRYPTO_SUCCESS);
	}

	/* these are mac bytes */

	/*
	 * assume that the mac always arrives in a single block, not split
	 * over blocks. this is true for OpenZFS at least
	 */
	if (length != CP_MAC_SIZE)
		return (CRYPTO_DATA_LEN_RANGE);

	/* leave the incoming mac in the temp buffer */
	memcpy(cpctx->temp, data, 16);

	return (CRYPTO_SUCCESS);
}

static int
chapoly_decrypt_finish(
	chapoly_ctx *cpctx, size_t length, crypto_data_t *out)
{
	uint8_t *datap = (uint8_t *)cpctx + sizeof (chapoly_ctx);
	size_t nremaining = length;

	size_t need;

	int rv;

	while (nremaining > 0) {
		/*
		 * take no more than we need to fill the temp buffer
		 * (one block), otherwise whatever is left
		 */
		need = nremaining > CP_BLOCK_SIZE ? CP_BLOCK_SIZE : nremaining;

		/* process a block */
		cpctx->counter = crypto_chacha20_ietf(
		    cpctx->temp, datap, need,
		    cpctx->key, cpctx->iv, cpctx->counter);

		/* copy it into the output buffers */
		rv = crypto_put_output_data(cpctx->temp, out, need);
		if (rv != CRYPTO_SUCCESS)
			return (rv);

		/* update offset */
		out->cd_offset += need;

		/* update remaining */
		nremaining -= need;
		datap += need;
	}

	return (CRYPTO_SUCCESS);
}

static int
chapoly_decrypt_atomic(crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t __attribute__((unused)) template)
{
	int rv;

	/*
	 * We don't actually do GCM here, its just the default parameter
	 * option in zio_do_crypt_uio and has everything we need, so its
	 * easier to just take that instead of making our own thing.
	 */
	const CK_AES_GCM_PARAMS *gcmp =
	    (CK_AES_GCM_PARAMS*) mechanism->cm_param;
	const uint8_t *iv = gcmp->pIv;

	/* chacha20 invariants */
	if (CRYPTO_BITS2BYTES(key->ck_length) != CP_KEY_SIZE)
		return (CRYPTO_KEY_SIZE_RANGE);
	if (gcmp->ulIvLen != CP_IV_SIZE ||
	    CRYPTO_BITS2BYTES(gcmp->ulTagBits) != CP_MAC_SIZE)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	size_t datalen = ciphertext->cd_length - CP_MAC_SIZE;

	chapoly_ctx *cpctx = vmem_alloc(
	    sizeof (chapoly_ctx) + datalen, KM_SLEEP);
	if (cpctx == NULL)
		return (CRYPTO_HOST_MEMORY);
	memset(cpctx, 0, sizeof (chapoly_ctx) + datalen);

	chapoly_init(cpctx, key, iv);

	/* mix additional data into the mac */
	crypto_poly1305_update(&cpctx->poly, gcmp->pAAD, gcmp->ulAADLen);
	crypto_poly1305_update(
	    &cpctx->poly, zero_pad, (~(gcmp->ulAADLen) + 1) & 0xf);

	cpctx->datalen = datalen;
	cpctx->unauthp = (uint8_t *)cpctx + sizeof (chapoly_ctx);

	off_t saved_offset = plaintext->cd_offset;
	size_t saved_length = plaintext->cd_length;

	switch (ciphertext->cd_format) {
	case CRYPTO_DATA_RAW:
		rv = crypto_update_iov(
		    cpctx, ciphertext, plaintext,
		    chapoly_decrypt_contiguous_blocks);
		break;
	case CRYPTO_DATA_UIO:
		rv = crypto_update_uio(
		    cpctx, ciphertext, plaintext,
		    chapoly_decrypt_contiguous_blocks);
		break;
	default:
		rv = CRYPTO_ARGUMENTS_BAD;
	}

	if (rv == CRYPTO_SUCCESS) {
		/*
		 * finish the mac. the incoming mac is at that start of the
		 * temp buffer, so we'll write the computed one after it
		 */
		uint64_t sizes[2] = {
		    LE_64(gcmp->ulAADLen), LE_64(datalen)
		};
		crypto_poly1305_update(
		    &cpctx->poly, zero_pad, (~(datalen) + 1) & 0xf);
		crypto_poly1305_update(&cpctx->poly, (uint8_t *)sizes, 16);
		crypto_poly1305_final(&cpctx->poly, cpctx->temp + CP_MAC_SIZE);

		/* now compare them */
		if (crypto_verify16(
		    cpctx->temp, cpctx->temp + CP_MAC_SIZE) != 0)
			rv = CRYPTO_INVALID_MAC;
	}

	/* mac checks out; we're ready to decrypt */
	if (rv == CRYPTO_SUCCESS)
		/* mac has been checked, now we can decrypt */
		rv = chapoly_decrypt_finish(cpctx, datalen, plaintext);

	if (rv == CRYPTO_SUCCESS)
		plaintext->cd_length = plaintext->cd_offset - saved_offset;
	else
		plaintext->cd_length = saved_length;
	plaintext->cd_offset = saved_offset;

	crypto_wipe(cpctx, sizeof (chapoly_ctx) + datalen);
	vmem_free(cpctx, sizeof (chapoly_ctx) + datalen);
	return (rv);
}


static const crypto_mech_info_t chapoly_mech_info_tab[] = {
	{SUN_CKM_CHACHA20_POLY1305, 0,
	    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC },
};

static const crypto_cipher_ops_t chapoly_cipher_ops = {
	.encrypt_atomic = chapoly_encrypt_atomic,
	.decrypt_atomic = chapoly_decrypt_atomic
};

static const crypto_ops_t chapoly_crypto_ops = {
	.co_cipher_ops = &chapoly_cipher_ops,
	.co_mac_ops = NULL,
	.co_ctx_ops = NULL,
};

static const crypto_provider_info_t chapoly_prov_info = {
	"Chacha20-Poly1305 Software Provider",
	&chapoly_crypto_ops,
	sizeof (chapoly_mech_info_tab) / sizeof (crypto_mech_info_t),
	chapoly_mech_info_tab
};

static crypto_kcf_provider_handle_t chapoly_prov_handle = 0;

int
chapoly_mod_init(void)
{
	/* Register with KCF.  If the registration fails, remove the module. */
	if (crypto_register_provider(&chapoly_prov_info, &chapoly_prov_handle))
		return (EACCES);

	return (0);
}

int
chapoly_mod_fini(void)
{
	/* Unregister from KCF if module is registered */
	if (chapoly_prov_handle != 0) {
		if (crypto_unregister_provider(chapoly_prov_handle))
			return (EBUSY);

		chapoly_prov_handle = 0;
	}

	return (0);
}
