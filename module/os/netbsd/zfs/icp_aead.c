// SPDX-License-Identifier: CDDL-1.0
/*
 * Software AES-GCM / AES-CCM for OpenZFS on NetBSD.
 *
 * Calls ICP's portable AES + modes (same transform Linux uses) so
 * on-disk ciphertext matches Linux/FreeBSD.  No KCF, no AES-NI on
 * this path -- kfpu is a later patch.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <aes/aes_impl.h>
#include <modes/modes.h>
#include <modes/gcm_impl.h>

#define	ZFS_AEAD_TAG_LEN	16
#define	ZFS_AEAD_IV_LEN		12

#ifndef	AES_BLOCK_LEN
#define	AES_BLOCK_LEN		16
#endif

static volatile unsigned icp_aead_state;	/* 0 idle, 1 initing, 2 ready */

void
zfs_icp_aead_init(void)
{
	unsigned s;

	for (;;) {
		s = atomic_cas_uint(&icp_aead_state, 0, 1);
		if (s == 2)
			return;
		if (s == 0)
			break;
		kpause("icpinit", false, 1, NULL);
	}
	aes_impl_init();
	gcm_impl_init();
	membar_producer();
	(void) atomic_swap_uint(&icp_aead_state, 2);
}

/*
 * In-place AEAD over [AAD | payload | tag].
 * type: 1 = CCM, 2 = GCM (zio_crypt_type_t).
 * keylen is in bytes (16/24/32).  ivlen must be 12.
 * Returns 0, EINVAL, ENOTSUP, or ECKSUM (tag mismatch on decrypt).
 */
int
zfs_icp_aead(boolean_t encrypt, int type, const uint8_t *key, size_t keylen,
    const uint8_t *iv, size_t ivlen, uint8_t *buf, size_t auth_len,
    size_t datalen)
{
	aes_ctx_t *actx;
	crypto_data_t out;
	void *ks;
	size_t kslen = 0;
	int rv;
	uint_t keybits;

	if (atomic_cas_uint(&icp_aead_state, 2, 2) != 2)
		zfs_icp_aead_init();

	if (ivlen != ZFS_AEAD_IV_LEN)
		return (EINVAL);
	if (keylen != 16 && keylen != 24 && keylen != 32)
		return (EINVAL);
	if (type != 1 && type != 2)
		return (ENOTSUP);

	keybits = (uint_t)(keylen * 8);
	ks = aes_alloc_keysched(&kslen, KM_SLEEP);
	aes_init_keysched(key, keybits, ks);

	actx = kmem_zalloc(sizeof (*actx), KM_SLEEP);
	actx->ac_keysched = ks;
	actx->ac_keysched_len = kslen;
	actx->ac_flags |= PROVIDER_OWNS_KEY_SCHEDULE;

	if (type == 2) {
		CK_AES_GCM_PARAMS gp;

		memset(&gp, 0, sizeof (gp));
		gp.pIv = (uchar_t *)(uintptr_t)iv;
		gp.ulIvLen = ivlen;
		gp.ulIvBits = CRYPTO_BYTES2BITS(ivlen);
		gp.pAAD = (auth_len > 0) ? buf : NULL;
		gp.ulAADLen = auth_len;
		gp.ulTagBits = CRYPTO_BYTES2BITS(ZFS_AEAD_TAG_LEN);
		rv = gcm_init_ctx((gcm_ctx_t *)actx, (char *)&gp,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
	} else {
		CK_AES_CCM_PARAMS cp;

		memset(&cp, 0, sizeof (cp));
		cp.ulMACSize = ZFS_AEAD_TAG_LEN;
		cp.ulNonceSize = ivlen;
		cp.ulAuthDataSize = auth_len;
		/*
		 * ICP CCM wants plaintext length on encrypt, and
		 * plaintext+MAC on decrypt (same as Linux zio_crypt).
		 */
		cp.ulDataSize = encrypt ? datalen :
		    datalen + ZFS_AEAD_TAG_LEN;
		cp.nonce = (uchar_t *)(uintptr_t)iv;
		cp.authData = (auth_len > 0) ? buf : NULL;
		rv = ccm_init_ctx((ccm_ctx_t *)actx, (char *)&cp, KM_SLEEP,
		    encrypt, AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
	}
	if (rv != CRYPTO_SUCCESS) {
		/*
		 * Same unwind as the success path.  Generic
		 * gcm_init / ccm_init do not heap-alloc before
		 * returning an error (CAN_USE_GCM_ASM is off),
		 * but GCM_MODE / CCM_MODE may already be set and
		 * decrypt CCM may have a pt_buf.
		 */
		if (actx->ac_flags & GCM_MODE)
			gcm_clear_ctx((gcm_ctx_t *)actx);
		else if (actx->ac_flags & CCM_MODE)
			ccm_clear_ctx((ccm_ctx_t *)actx);
		kmem_free(actx, sizeof (*actx));
		memset(ks, 0, kslen);
		kmem_free(ks, kslen);
		return (EINVAL);
	}

	memset(&out, 0, sizeof (out));
	out.cd_format = CRYPTO_DATA_RAW;
	out.cd_offset = 0;
	out.cd_raw.iov_base = (void *)(buf + auth_len);
	if (encrypt) {
		out.cd_length = datalen + ZFS_AEAD_TAG_LEN;
		out.cd_raw.iov_len = datalen + ZFS_AEAD_TAG_LEN;
		if (type == 2) {
			rv = gcm_mode_encrypt_contiguous_blocks(
			    (gcm_ctx_t *)actx, (char *)(buf + auth_len),
			    datalen, &out, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_copy_block, aes_xor_block);
			if (rv == CRYPTO_SUCCESS) {
				rv = gcm_encrypt_final((gcm_ctx_t *)actx,
				    &out, AES_BLOCK_LEN, aes_encrypt_block,
				    aes_copy_block, aes_xor_block);
			}
		} else {
			rv = ccm_mode_encrypt_contiguous_blocks(
			    (ccm_ctx_t *)actx, (char *)(buf + auth_len),
			    datalen, &out, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_copy_block, aes_xor_block);
			if (rv == CRYPTO_SUCCESS) {
				rv = ccm_encrypt_final((ccm_ctx_t *)actx,
				    &out, AES_BLOCK_LEN, aes_encrypt_block,
				    aes_xor_block);
			}
		}
	} else {
		/*
		 * Decrypt consumes ciphertext of datalen; tag lives
		 * at buf[auth_len + datalen].  GCM final reads the
		 * tag from the input via the leftover length.
		 */
		out.cd_length = datalen;
		out.cd_raw.iov_len = datalen + ZFS_AEAD_TAG_LEN;
		if (type == 2) {
			rv = gcm_mode_decrypt_contiguous_blocks(
			    (gcm_ctx_t *)actx, (char *)(buf + auth_len),
			    datalen + ZFS_AEAD_TAG_LEN, &out, AES_BLOCK_LEN,
			    aes_encrypt_block, aes_copy_block, aes_xor_block);
			if (rv == CRYPTO_SUCCESS) {
				rv = gcm_decrypt_final((gcm_ctx_t *)actx,
				    &out, AES_BLOCK_LEN, aes_encrypt_block,
				    aes_xor_block);
			}
		} else {
			rv = ccm_mode_decrypt_contiguous_blocks(
			    (ccm_ctx_t *)actx, (char *)(buf + auth_len),
			    datalen + ZFS_AEAD_TAG_LEN, &out, AES_BLOCK_LEN,
			    aes_encrypt_block, aes_copy_block, aes_xor_block);
			if (rv == CRYPTO_SUCCESS) {
				rv = ccm_decrypt_final((ccm_ctx_t *)actx,
				    &out, AES_BLOCK_LEN, aes_encrypt_block,
				    aes_copy_block, aes_xor_block);
			}
		}
		if (rv != CRYPTO_SUCCESS)
			memset(buf + auth_len, 0, datalen);
	}

	if (actx->ac_flags & GCM_MODE)
		gcm_clear_ctx((gcm_ctx_t *)actx);
	else if (actx->ac_flags & CCM_MODE)
		ccm_clear_ctx((ccm_ctx_t *)actx);

	kmem_free(actx, sizeof (*actx));
	memset(ks, 0, kslen);
	kmem_free(ks, kslen);

	if (rv == CRYPTO_SUCCESS)
		return (0);
	if (rv == CRYPTO_INVALID_MAC)
		return (ECKSUM);
	return (EINVAL);
}

/*
 * NIST SP 800-38D empty-PT tags plus FIPS-197 AES-ECB of a zero
 * block.  CCM round-trips a one-block payload and an empty
 * plaintext (AAD only).  A flipped tag must return ECKSUM.
 * Empty CCM plaintext is required: NetBSD kmem_alloc(0) is NULL,
 * and dnode blocks with no encrypted bonus still MAC the AAD.
 */
int
zfs_icp_aead_selftest(void)
{
	static const uint8_t tag128[16] = {
		0x58, 0xe2, 0xfc, 0xce, 0xfa, 0x7e, 0x30, 0x61,
		0x36, 0x7f, 0x1d, 0x57, 0xa4, 0xe7, 0x45, 0x5a
	};
	static const uint8_t tag192[16] = {
		0xcd, 0x33, 0xb2, 0x8a, 0xc7, 0x73, 0xf7, 0x4b,
		0xa0, 0x0e, 0xd1, 0xf3, 0x12, 0x57, 0x24, 0x35
	};
	static const uint8_t tag256[16] = {
		0x53, 0x0f, 0x8a, 0xfb, 0xc7, 0x45, 0x36, 0xb9,
		0xa9, 0x63, 0xb4, 0xf1, 0xc4, 0xcb, 0x73, 0x8b
	};
	static const uint8_t aes_zero_ct[16] = {
		0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b,
		0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e
	};
	uint8_t key[32], iv[12], buf[64], pt[16], block[16];
	void *ks;
	size_t kslen = 0;
	int err, type, klen;

	zfs_icp_aead_init();

	/* Raw AES-128-ECB of a zero block. */
	memset(key, 0, sizeof (key));
	memset(block, 0, sizeof (block));
	ks = aes_alloc_keysched(&kslen, KM_SLEEP);
	aes_init_keysched(key, 128, ks);
	err = aes_encrypt_block(ks, block, block);
	if (err != CRYPTO_SUCCESS || memcmp(block, aes_zero_ct, 16) != 0) {
		memset(ks, 0, kslen);
		kmem_free(ks, kslen);
		return (ECKSUM);
	}
	memset(ks, 0, kslen);
	kmem_free(ks, kslen);

	memset(key, 0, sizeof (key));
	memset(iv, 0, sizeof (iv));

	/* AES-128-GCM empty AAD/PT */
	memset(buf, 0, sizeof (buf));
	err = zfs_icp_aead(B_TRUE, 2, key, 16, iv, 12, buf, 0, 0);
	if (err != 0 || memcmp(buf, tag128, 16) != 0)
		return (ECKSUM);
	memset(buf, 0, sizeof (buf));
	memcpy(buf, tag128, 16);
	err = zfs_icp_aead(B_FALSE, 2, key, 16, iv, 12, buf, 0, 0);
	if (err != 0)
		return (err);

	/* AES-192-GCM empty AAD/PT */
	memset(buf, 0, sizeof (buf));
	err = zfs_icp_aead(B_TRUE, 2, key, 24, iv, 12, buf, 0, 0);
	if (err != 0 || memcmp(buf, tag192, 16) != 0)
		return (ECKSUM);

	/* AES-256-GCM empty AAD/PT */
	memset(buf, 0, sizeof (buf));
	err = zfs_icp_aead(B_TRUE, 2, key, 32, iv, 12, buf, 0, 0);
	if (err != 0 || memcmp(buf, tag256, 16) != 0)
		return (ECKSUM);
	memset(buf, 0, sizeof (buf));
	memcpy(buf, tag256, 16);
	err = zfs_icp_aead(B_FALSE, 2, key, 32, iv, 12, buf, 0, 0);
	if (err != 0)
		return (err);

	/* GCM + CCM, all key sizes: AAD + PT round-trip and bad tag */
	for (type = 1; type <= 2; type++) {
		for (klen = 16; klen <= 32; klen += 8) {
			memset(key, 0xa5, sizeof (key));
			memset(iv, 0x11, sizeof (iv));
			memset(buf, 0, sizeof (buf));
			memcpy(buf, "aad-bytes-0123", 14);
			memcpy(buf + 14, "payload-16bytes", 16);
			err = zfs_icp_aead(B_TRUE, type, key, klen, iv, 12,
			    buf, 14, 16);
			if (err != 0)
				return (err);
			memcpy(pt, buf + 14, 16);
			err = zfs_icp_aead(B_FALSE, type, key, klen, iv, 12,
			    buf, 14, 16);
			if (err != 0 || memcmp(buf + 14, "payload-16bytes",
			    16) != 0)
				return (ECKSUM);
			memcpy(buf + 14, pt, 16);
			buf[14 + 16]++;
			err = zfs_icp_aead(B_FALSE, type, key, klen, iv, 12,
			    buf, 14, 16);
			if (err != ECKSUM)
				return (EINVAL);
		}
	}

	/* CCM empty plaintext, all key sizes, with and without AAD. */
	for (klen = 16; klen <= 32; klen += 8) {
		memset(key, 0x5a, sizeof (key));
		memset(iv, 0x22, sizeof (iv));
		memset(buf, 0, sizeof (buf));
		memcpy(buf, "aad-bytes-0123", 14);
		err = zfs_icp_aead(B_TRUE, 1, key, klen, iv, 12, buf, 14, 0);
		if (err != 0)
			return (err);
		err = zfs_icp_aead(B_FALSE, 1, key, klen, iv, 12, buf, 14, 0);
		if (err != 0)
			return (err);
		buf[14]++;
		err = zfs_icp_aead(B_FALSE, 1, key, klen, iv, 12, buf, 14, 0);
		if (err != ECKSUM)
			return (EINVAL);

		memset(buf, 0, sizeof (buf));
		err = zfs_icp_aead(B_TRUE, 1, key, klen, iv, 12, buf, 0, 0);
		if (err != 0)
			return (err);
		err = zfs_icp_aead(B_FALSE, 1, key, klen, iv, 12, buf, 0, 0);
		if (err != 0)
			return (err);
	}

	return (0);
}

/*
 * GCM/CCM write the result through this KCF helper.  Match the
 * Linux kcf_prov_lib.c contract for CRYPTO_DATA_RAW.
 */
int
crypto_put_output_data(uchar_t *buf, crypto_data_t *output, int len)
{
	size_t off, room;

	if (output == NULL || (len > 0 && buf == NULL))
		return (CRYPTO_ARGUMENTS_BAD);
	if (output->cd_format != CRYPTO_DATA_RAW)
		return (CRYPTO_ARGUMENTS_BAD);
	if (output->cd_offset < 0)
		return (CRYPTO_ARGUMENTS_BAD);
	off = (size_t)output->cd_offset;
	if (off > output->cd_raw.iov_len) {
		output->cd_length = off + (size_t)len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}
	room = output->cd_raw.iov_len - off;
	if (room < (size_t)len) {
		output->cd_length = off + (size_t)len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}
	if (len > 0) {
		memcpy((uchar_t *)output->cd_raw.iov_base + off, buf,
		    (size_t)len);
	}
	return (CRYPTO_SUCCESS);
}
