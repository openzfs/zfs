// SPDX-License-Identifier: BSD-2-Clause
/*
 * Software SHA512-HMAC plus AES-GCM/CCM sessions for OpenZFS on NetBSD.
 * AEAD is ICP's portable AES + modes (icp_aead.c), not opencrypto(9).
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sha2.h>
#include <sys/kmem.h>
#include <sys/zio_crypt.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/freebsd_crypto.h>

#define	SHA512_HMAC_BLOCK_SIZE	128
#define	ZFS_AEAD_TAG_LEN	16

extern void zfs_icp_aead_init(void);
extern int zfs_icp_aead(boolean_t, int, const uint8_t *, size_t,
    const uint8_t *, size_t, uint8_t *, size_t, size_t);
extern int zfs_icp_aead_selftest(void);

static int crypt_selftest_done;
static int crypt_selftest_err;

void
crypto_mac_init(struct hmac_ctx *ctx, const crypto_key_t *c_key)
{
	uint8_t *pads;
	uint8_t *k_ipad, *k_opad, *key;
	SHA2_CTX lctx;
	size_t cl_bytes = CRYPTO_BITS2BYTES(c_key->ck_length);
	int i;

	/* 3 * 128-byte HMAC pads; stack would miss -Wframe-larger-than=512. */
	pads = kmem_alloc(SHA512_HMAC_BLOCK_SIZE * 3, KM_SLEEP);
	k_ipad = pads;
	k_opad = pads + SHA512_HMAC_BLOCK_SIZE;
	key = pads + SHA512_HMAC_BLOCK_SIZE * 2;

	memset(key, 0, SHA512_HMAC_BLOCK_SIZE);
	if (c_key->ck_length == 0) {
		/* empty key */
	} else if (cl_bytes <= SHA512_HMAC_BLOCK_SIZE) {
		/*
		 * memcpy ck_data as FreeBSD crypto_os.c does.
		 * zio_crypt.c is compiled unchanged:
		 * key->zk_hmac_key.ck_data = &key->zk_hmac_key
		 * Do not retarget ck_data at zk_hmac_keydata.
		 * Linux ICP is a different API.
		 */
		memcpy(key, c_key->ck_data, cl_bytes);
	} else {
		SHA2Init(SHA512, &lctx);
		SHA2Update(&lctx, c_key->ck_data, cl_bytes);
		SHA2Final(key, &lctx);
	}

	for (i = 0; i < (int)SHA512_HMAC_BLOCK_SIZE; i++) {
		k_ipad[i] = key[i] ^ 0x36;
		k_opad[i] = key[i] ^ 0x5c;
	}

	SHA2Init(SHA512, &ctx->innerctx);
	SHA2Update(&ctx->innerctx, k_ipad, SHA512_HMAC_BLOCK_SIZE);
	SHA2Init(SHA512, &ctx->outerctx);
	SHA2Update(&ctx->outerctx, k_opad, SHA512_HMAC_BLOCK_SIZE);
	memset(pads, 0, SHA512_HMAC_BLOCK_SIZE * 3);
	kmem_free(pads, SHA512_HMAC_BLOCK_SIZE * 3);
}

void
crypto_mac_update(struct hmac_ctx *ctx, const void *data, size_t datasize)
{
	SHA2Update(&ctx->innerctx, data, datasize);
}

void
crypto_mac_final(struct hmac_ctx *ctx, void *md, size_t mdsize)
{
	uint8_t digest[SHA512_DIGEST_LENGTH];

	SHA2Final(digest, &ctx->innerctx);
	SHA2Update(&ctx->outerctx, digest, sizeof (digest));
	SHA2Final(digest, &ctx->outerctx);
	memset(ctx, 0, sizeof (*ctx));
	if (mdsize == 0)
		mdsize = SHA512_DIGEST_LENGTH;
	if (mdsize > sizeof (digest))
		mdsize = sizeof (digest);
	memcpy(md, digest, mdsize);
	memset(digest, 0, sizeof (digest));
}

void
crypto_mac(const crypto_key_t *key, const void *in_data, size_t in_data_size,
    void *out_data, size_t out_data_size)
{
	struct hmac_ctx ctx;

	crypto_mac_init(&ctx, key);
	crypto_mac_update(&ctx, in_data, in_data_size);
	crypto_mac_final(&ctx, out_data, out_data_size);
}

int
freebsd_crypt_newsession(freebsd_crypt_session_t *sessp,
    const struct zio_crypt_info *ci, crypto_key_t *key)
{
	size_t klen;

	if (sessp == NULL || ci == NULL || key == NULL || key->ck_data == NULL)
		return (EINVAL);
	if (ci->ci_crypt_type != ZC_TYPE_GCM &&
	    ci->ci_crypt_type != ZC_TYPE_CCM)
		return (ENOTSUP);
	klen = CRYPTO_BITS2BYTES(key->ck_length);
	if (klen != 16 && klen != 24 && klen != 32)
		return (EINVAL);
	if (!crypt_selftest_done) {
		crypt_selftest_err = zfs_icp_aead_selftest();
		crypt_selftest_done = 1;
	}
	if (crypt_selftest_err != 0)
		return (ENOTSUP);
	sessp->_unused = 1;
	return (0);
}

void
freebsd_crypt_freesession(freebsd_crypt_session_t *sessp)
{
	if (sessp != NULL)
		memset(sessp, 0, sizeof (*sessp));
}

int
freebsd_crypt_uio(boolean_t encrypt, freebsd_crypt_session_t *sess,
    const struct zio_crypt_info *ci, zfs_uio_t *uio, crypto_key_t *key,
    uint8_t *ivbuf, size_t datalen, size_t auth_len)
{
	size_t total, off, n, klen;
	uint8_t *buf;
	int i, err;

	(void) sess;
	if (ci == NULL || key == NULL || key->ck_data == NULL || ivbuf == NULL)
		return (EINVAL);
	klen = CRYPTO_BITS2BYTES(key->ck_length);
	if (klen != 16 && klen != 24 && klen != 32)
		return (EINVAL);
	if (ci->ci_crypt_type != ZC_TYPE_GCM &&
	    ci->ci_crypt_type != ZC_TYPE_CCM)
		return (ENOTSUP);

	total = 0;
	for (i = 0; i < zfs_uio_iovcnt(uio); i++)
		total += zfs_uio_iovlen(uio, i);
	if (total < auth_len + datalen + ZFS_AEAD_TAG_LEN)
		return (EINVAL);

	buf = kmem_alloc(total, KM_SLEEP);
	off = 0;
	for (i = 0; i < zfs_uio_iovcnt(uio); i++) {
		n = zfs_uio_iovlen(uio, i);
		if (n > 0)
			memcpy(buf + off, zfs_uio_iovbase(uio, i), n);
		off += n;
	}

	err = zfs_icp_aead(encrypt, (int)ci->ci_crypt_type, key->ck_data,
	    klen, ivbuf, ZIO_DATA_IV_LEN, buf, auth_len, datalen);

	off = 0;
	for (i = 0; i < zfs_uio_iovcnt(uio); i++) {
		n = zfs_uio_iovlen(uio, i);
		if (n > 0)
			memcpy(zfs_uio_iovbase(uio, i), buf + off, n);
		off += n;
	}
	memset(buf, 0, total);
	kmem_free(buf, total);
	return (err);
}
