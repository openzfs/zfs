/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 */

#include <sys/zio_crypt.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dnode.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/sha2.h>
#include <sys/hkdf.h>
#include <sys/qat.h>

/*
 * This file is responsible for handling all of the details of generating
 * encryption parameters and performing encryption and authentication.
 *
 * BLOCK ENCRYPTION PARAMETERS:
 * Encryption /Authentication Algorithm Suite (crypt):
 * The encryption algorithm, mode, and key length we are going to use. We
 * currently support AES in either GCM or CCM modes with 128, 192, and 256 bit
 * keys. All authentication is currently done with SHA512-HMAC.
 *
 * Plaintext:
 * The unencrypted data that we want to encrypt.
 *
 * Initialization Vector (IV):
 * An initialization vector for the encryption algorithms. This is used to
 * "tweak" the encryption algorithms so that two blocks of the same data are
 * encrypted into different ciphertext outputs, thus obfuscating block patterns.
 * The supported encryption modes (AES-GCM and AES-CCM) require that an IV is
 * never reused with the same encryption key. This value is stored unencrypted
 * and must simply be provided to the decryption function. We use a 96 bit IV
 * (as recommended by NIST) for all block encryption. For non-dedup blocks we
 * derive the IV randomly. The first 64 bits of the IV are stored in the second
 * word of DVA[2] and the remaining 32 bits are stored in the upper 32 bits of
 * blk_fill. This is safe because encrypted blocks can't use the upper 32 bits
 * of blk_fill. We only encrypt level 0 blocks, which normally have a fill count
 * of 1. The only exception is for DMU_OT_DNODE objects, where the fill count of
 * level 0 blocks is the number of allocated dnodes in that block. The on-disk
 * format supports at most 2^15 slots per L0 dnode block, because the maximum
 * block size is 16MB (2^24). In either case, for level 0 blocks this number
 * will still be smaller than UINT32_MAX so it is safe to store the IV in the
 * top 32 bits of blk_fill, while leaving the bottom 32 bits of the fill count
 * for the dnode code.
 *
 * Master key:
 * This is the most important secret data of an encrypted dataset. It is used
 * along with the salt to generate that actual encryption keys via HKDF. We
 * do not use the master key to directly encrypt any data because there are
 * theoretical limits on how much data can actually be safely encrypted with
 * any encryption mode. The master key is stored encrypted on disk with the
 * user's wrapping key. Its length is determined by the encryption algorithm.
 * For details on how this is stored see the block comment in dsl_crypt.c
 *
 * Salt:
 * Used as an input to the HKDF function, along with the master key. We use a
 * 64 bit salt, stored unencrypted in the first word of DVA[2]. Any given salt
 * can be used for encrypting many blocks, so we cache the current salt and the
 * associated derived key in zio_crypt_t so we do not need to derive it again
 * needlessly.
 *
 * Encryption Key:
 * A secret binary key, generated from an HKDF function used to encrypt and
 * decrypt data.
 *
 * Message Authentication Code (MAC)
 * The MAC is an output of authenticated encryption modes such as AES-GCM and
 * AES-CCM. Its purpose is to ensure that an attacker cannot modify encrypted
 * data on disk and return garbage to the application. Effectively, it is a
 * checksum that can not be reproduced by an attacker. We store the MAC in the
 * second 128 bits of blk_cksum, leaving the first 128 bits for a truncated
 * regular checksum of the ciphertext which can be used for scrubbing.
 *
 * OBJECT AUTHENTICATION:
 * Some object types, such as DMU_OT_MASTER_NODE cannot be encrypted because
 * they contain some info that always needs to be readable. To prevent this
 * data from being altered, we authenticate this data using SHA512-HMAC. This
 * will produce a MAC (similar to the one produced via encryption) which can
 * be used to verify the object was not modified. HMACs do not require key
 * rotation or IVs, so we can keep up to the full 3 copies of authenticated
 * data.
 *
 * ZIL ENCRYPTION:
 * ZIL blocks have their bp written to disk ahead of the associated data, so we
 * cannot store the MAC there as we normally do. For these blocks the MAC is
 * stored in the embedded checksum within the zil_chain_t header. The salt and
 * IV are generated for the block on bp allocation instead of at encryption
 * time. In addition, ZIL blocks have some pieces that must be left in plaintext
 * for claiming even though all of the sensitive user data still needs to be
 * encrypted. The function zio_crypt_init_uios_zil() handles parsing which
 * pieces of the block need to be encrypted. All data that is not encrypted is
 * authenticated using the AAD mechanisms that the supported encryption modes
 * provide for. In order to preserve the semantics of the ZIL for encrypted
 * datasets, the ZIL is not protected at the objset level as described below.
 *
 * DNODE ENCRYPTION:
 * Similarly to ZIL blocks, the core part of each dnode_phys_t needs to be left
 * in plaintext for scrubbing and claiming, but the bonus buffers might contain
 * sensitive user data. The function zio_crypt_init_uios_dnode() handles parsing
 * which pieces of the block need to be encrypted. For more details about
 * dnode authentication and encryption, see zio_crypt_init_uios_dnode().
 *
 * OBJECT SET AUTHENTICATION:
 * Up to this point, everything we have encrypted and authenticated has been
 * at level 0 (or -2 for the ZIL). If we did not do any further work the
 * on-disk format would be susceptible to attacks that deleted or rearranged
 * the order of level 0 blocks. Ideally, the cleanest solution would be to
 * maintain a tree of authentication MACs going up the bp tree. However, this
 * presents a problem for raw sends. Send files do not send information about
 * indirect blocks so there would be no convenient way to transfer the MACs and
 * they cannot be recalculated on the receive side without the master key which
 * would defeat one of the purposes of raw sends in the first place. Instead,
 * for the indirect levels of the bp tree, we use a regular SHA512 of the MACs
 * from the level below. We also include some portable fields from blk_prop such
 * as the lsize and compression algorithm to prevent the data from being
 * misinterpreted.
 *
 * At the objset level, we maintain 2 separate 256 bit MACs in the
 * objset_phys_t. The first one is "portable" and is the logical root of the
 * MAC tree maintained in the metadnode's bps. The second, is "local" and is
 * used as the root MAC for the user accounting objects, which are also not
 * transferred via "zfs send". The portable MAC is sent in the DRR_BEGIN payload
 * of the send file. The useraccounting code ensures that the useraccounting
 * info is not present upon a receive, so the local MAC can simply be cleared
 * out at that time. For more info about objset_phys_t authentication, see
 * zio_crypt_do_objset_hmacs().
 *
 * CONSIDERATIONS FOR DEDUP:
 * In order for dedup to work, blocks that we want to dedup with one another
 * need to use the same IV and encryption key, so that they will have the same
 * ciphertext. Normally, one should never reuse an IV with the same encryption
 * key or else AES-GCM and AES-CCM can both actually leak the plaintext of both
 * blocks. In this case, however, since we are using the same plaintext as
 * well all that we end up with is a duplicate of the original ciphertext we
 * already had. As a result, an attacker with read access to the raw disk will
 * be able to tell which blocks are the same but this information is given away
 * by dedup anyway. In order to get the same IVs and encryption keys for
 * equivalent blocks of data we use an HMAC of the plaintext. We use an HMAC
 * here so that a reproducible checksum of the plaintext is never available to
 * the attacker. The HMAC key is kept alongside the master key, encrypted on
 * disk. The first 64 bits of the HMAC are used in place of the random salt, and
 * the next 96 bits are used as the IV. As a result of this mechanism, dedup
 * will only work within a clone family since encrypted dedup requires use of
 * the same master and HMAC keys.
 */

/*
 * After encrypting many blocks with the same key we may start to run up
 * against the theoretical limits of how much data can securely be encrypted
 * with a single key using the supported encryption modes. The most obvious
 * limitation is that our risk of generating 2 equivalent 96 bit IVs increases
 * the more IVs we generate (which both GCM and CCM modes strictly forbid).
 * This risk actually grows surprisingly quickly over time according to the
 * Birthday Problem. With a total IV space of 2^(96 bits), and assuming we have
 * generated n IVs with a cryptographically secure RNG, the approximate
 * probability p(n) of a collision is given as:
 *
 * p(n) ~= e^(-n*(n-1)/(2*(2^96)))
 *
 * [http://www.math.cornell.edu/~mec/2008-2009/TianyiZheng/Birthday.html]
 *
 * Assuming that we want to ensure that p(n) never goes over 1 / 1 trillion
 * we must not write more than 398,065,730 blocks with the same encryption key.
 * Therefore, we rotate our keys after 400,000,000 blocks have been written by
 * generating a new random 64 bit salt for our HKDF encryption key generation
 * function.
 */
#define	ZFS_KEY_MAX_SALT_USES_DEFAULT	400000000
#define	ZFS_CURRENT_MAX_SALT_USES	\
	(MIN(zfs_key_max_salt_uses, ZFS_KEY_MAX_SALT_USES_DEFAULT))
unsigned long zfs_key_max_salt_uses = ZFS_KEY_MAX_SALT_USES_DEFAULT;

typedef struct blkptr_auth_buf {
	uint64_t bab_prop;			/* blk_prop - portable mask */
	uint8_t bab_mac[ZIO_DATA_MAC_LEN];	/* MAC from blk_cksum */
	uint64_t bab_pad;			/* reserved for future use */
} blkptr_auth_buf_t;

zio_crypt_info_t zio_crypt_table[ZIO_CRYPT_FUNCTIONS] = {
	{"",			ZC_TYPE_NONE,	0,	"inherit"},
	{"",			ZC_TYPE_NONE,	0,	"on"},
	{"",			ZC_TYPE_NONE,	0,	"off"},
	{SUN_CKM_AES_CCM,	ZC_TYPE_CCM,	16,	"aes-128-ccm"},
	{SUN_CKM_AES_CCM,	ZC_TYPE_CCM,	24,	"aes-192-ccm"},
	{SUN_CKM_AES_CCM,	ZC_TYPE_CCM,	32,	"aes-256-ccm"},
	{SUN_CKM_AES_GCM,	ZC_TYPE_GCM,	16,	"aes-128-gcm"},
	{SUN_CKM_AES_GCM,	ZC_TYPE_GCM,	24,	"aes-192-gcm"},
	{SUN_CKM_AES_GCM,	ZC_TYPE_GCM,	32,	"aes-256-gcm"}
};

void
zio_crypt_key_destroy(zio_crypt_key_t *key)
{
	rw_destroy(&key->zk_salt_lock);

	/* free crypto templates */
	crypto_destroy_ctx_template(key->zk_current_tmpl);
	crypto_destroy_ctx_template(key->zk_hmac_tmpl);

	/* zero out sensitive data */
	bzero(key, sizeof (zio_crypt_key_t));
}

int
zio_crypt_key_init(uint64_t crypt, zio_crypt_key_t *key)
{
	int ret;
	crypto_mechanism_t mech;
	uint_t keydata_len;

	ASSERT(key != NULL);
	ASSERT3U(crypt, <, ZIO_CRYPT_FUNCTIONS);

	keydata_len = zio_crypt_table[crypt].ci_keylen;
	bzero(key, sizeof (zio_crypt_key_t));

	/* fill keydata buffers and salt with random data */
	ret = random_get_bytes((uint8_t *)&key->zk_guid, sizeof (uint64_t));
	if (ret != 0)
		goto error;

	ret = random_get_bytes(key->zk_master_keydata, keydata_len);
	if (ret != 0)
		goto error;

	ret = random_get_bytes(key->zk_hmac_keydata, SHA512_HMAC_KEYLEN);
	if (ret != 0)
		goto error;

	ret = random_get_bytes(key->zk_salt, ZIO_DATA_SALT_LEN);
	if (ret != 0)
		goto error;

	/* derive the current key from the master key */
	ret = hkdf_sha512(key->zk_master_keydata, keydata_len, NULL, 0,
	    key->zk_salt, ZIO_DATA_SALT_LEN, key->zk_current_keydata,
	    keydata_len);
	if (ret != 0)
		goto error;

	/* initialize keys for the ICP */
	key->zk_current_key.ck_format = CRYPTO_KEY_RAW;
	key->zk_current_key.ck_data = key->zk_current_keydata;
	key->zk_current_key.ck_length = CRYPTO_BYTES2BITS(keydata_len);

	key->zk_hmac_key.ck_format = CRYPTO_KEY_RAW;
	key->zk_hmac_key.ck_data = &key->zk_hmac_key;
	key->zk_hmac_key.ck_length = CRYPTO_BYTES2BITS(SHA512_HMAC_KEYLEN);

	/*
	 * Initialize the crypto templates. It's ok if this fails because
	 * this is just an optimization.
	 */
	mech.cm_type = crypto_mech2id(zio_crypt_table[crypt].ci_mechname);
	ret = crypto_create_ctx_template(&mech, &key->zk_current_key,
	    &key->zk_current_tmpl, KM_SLEEP);
	if (ret != CRYPTO_SUCCESS)
		key->zk_current_tmpl = NULL;

	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	ret = crypto_create_ctx_template(&mech, &key->zk_hmac_key,
	    &key->zk_hmac_tmpl, KM_SLEEP);
	if (ret != CRYPTO_SUCCESS)
		key->zk_hmac_tmpl = NULL;

	key->zk_crypt = crypt;
	key->zk_version = ZIO_CRYPT_KEY_CURRENT_VERSION;
	key->zk_salt_count = 0;
	rw_init(&key->zk_salt_lock, NULL, RW_DEFAULT, NULL);

	return (0);

error:
	zio_crypt_key_destroy(key);
	return (ret);
}

static int
zio_crypt_key_change_salt(zio_crypt_key_t *key)
{
	int ret = 0;
	uint8_t salt[ZIO_DATA_SALT_LEN];
	crypto_mechanism_t mech;
	uint_t keydata_len = zio_crypt_table[key->zk_crypt].ci_keylen;

	/* generate a new salt */
	ret = random_get_bytes(salt, ZIO_DATA_SALT_LEN);
	if (ret != 0)
		goto error;

	rw_enter(&key->zk_salt_lock, RW_WRITER);

	/* someone beat us to the salt rotation, just unlock and return */
	if (key->zk_salt_count < ZFS_CURRENT_MAX_SALT_USES)
		goto out_unlock;

	/* derive the current key from the master key and the new salt */
	ret = hkdf_sha512(key->zk_master_keydata, keydata_len, NULL, 0,
	    salt, ZIO_DATA_SALT_LEN, key->zk_current_keydata, keydata_len);
	if (ret != 0)
		goto out_unlock;

	/* assign the salt and reset the usage count */
	bcopy(salt, key->zk_salt, ZIO_DATA_SALT_LEN);
	key->zk_salt_count = 0;

	/* destroy the old context template and create the new one */
	crypto_destroy_ctx_template(key->zk_current_tmpl);
	ret = crypto_create_ctx_template(&mech, &key->zk_current_key,
	    &key->zk_current_tmpl, KM_SLEEP);
	if (ret != CRYPTO_SUCCESS)
		key->zk_current_tmpl = NULL;

	rw_exit(&key->zk_salt_lock);

	return (0);

out_unlock:
	rw_exit(&key->zk_salt_lock);
error:
	return (ret);
}

/* See comment above zfs_key_max_salt_uses definition for details */
int
zio_crypt_key_get_salt(zio_crypt_key_t *key, uint8_t *salt)
{
	int ret;
	boolean_t salt_change;

	rw_enter(&key->zk_salt_lock, RW_READER);

	bcopy(key->zk_salt, salt, ZIO_DATA_SALT_LEN);
	salt_change = (atomic_inc_64_nv(&key->zk_salt_count) >=
	    ZFS_CURRENT_MAX_SALT_USES);

	rw_exit(&key->zk_salt_lock);

	if (salt_change) {
		ret = zio_crypt_key_change_salt(key);
		if (ret != 0)
			goto error;
	}

	return (0);

error:
	return (ret);
}

/*
 * This function handles all encryption and decryption in zfs. When
 * encrypting it expects puio to reference the plaintext and cuio to
 * reference the ciphertext. cuio must have enough space for the
 * ciphertext + room for a MAC. datalen should be the length of the
 * plaintext / ciphertext alone.
 */
static int
zio_do_crypt_uio(boolean_t encrypt, uint64_t crypt, crypto_key_t *key,
    crypto_ctx_template_t tmpl, uint8_t *ivbuf, uint_t datalen,
    zfs_uio_t *puio, zfs_uio_t *cuio, uint8_t *authbuf, uint_t auth_len)
{
	int ret;
	crypto_data_t plaindata, cipherdata;
	CK_AES_CCM_PARAMS ccmp;
	CK_AES_GCM_PARAMS gcmp;
	crypto_mechanism_t mech;
	zio_crypt_info_t crypt_info;
	uint_t plain_full_len, maclen;

	ASSERT3U(crypt, <, ZIO_CRYPT_FUNCTIONS);
	ASSERT3U(key->ck_format, ==, CRYPTO_KEY_RAW);

	/* lookup the encryption info */
	crypt_info = zio_crypt_table[crypt];

	/* the mac will always be the last iovec_t in the cipher uio */
	maclen = cuio->uio_iov[cuio->uio_iovcnt - 1].iov_len;

	ASSERT(maclen <= ZIO_DATA_MAC_LEN);

	/* setup encryption mechanism (same as crypt) */
	mech.cm_type = crypto_mech2id(crypt_info.ci_mechname);

	/*
	 * Strangely, the ICP requires that plain_full_len must include
	 * the MAC length when decrypting, even though the UIO does not
	 * need to have the extra space allocated.
	 */
	if (encrypt) {
		plain_full_len = datalen;
	} else {
		plain_full_len = datalen + maclen;
	}

	/*
	 * setup encryption params (currently only AES CCM and AES GCM
	 * are supported)
	 */
	if (crypt_info.ci_crypt_type == ZC_TYPE_CCM) {
		ccmp.ulNonceSize = ZIO_DATA_IV_LEN;
		ccmp.ulAuthDataSize = auth_len;
		ccmp.authData = authbuf;
		ccmp.ulMACSize = maclen;
		ccmp.nonce = ivbuf;
		ccmp.ulDataSize = plain_full_len;

		mech.cm_param = (char *)(&ccmp);
		mech.cm_param_len = sizeof (CK_AES_CCM_PARAMS);
	} else {
		gcmp.ulIvLen = ZIO_DATA_IV_LEN;
		gcmp.ulIvBits = CRYPTO_BYTES2BITS(ZIO_DATA_IV_LEN);
		gcmp.ulAADLen = auth_len;
		gcmp.pAAD = authbuf;
		gcmp.ulTagBits = CRYPTO_BYTES2BITS(maclen);
		gcmp.pIv = ivbuf;

		mech.cm_param = (char *)(&gcmp);
		mech.cm_param_len = sizeof (CK_AES_GCM_PARAMS);
	}

	/* populate the cipher and plain data structs. */
	plaindata.cd_format = CRYPTO_DATA_UIO;
	plaindata.cd_offset = 0;
	plaindata.cd_uio = puio;
	plaindata.cd_miscdata = NULL;
	plaindata.cd_length = plain_full_len;

	cipherdata.cd_format = CRYPTO_DATA_UIO;
	cipherdata.cd_offset = 0;
	cipherdata.cd_uio = cuio;
	cipherdata.cd_miscdata = NULL;
	cipherdata.cd_length = datalen + maclen;

	/* perform the actual encryption */
	if (encrypt) {
		ret = crypto_encrypt(&mech, &plaindata, key, tmpl, &cipherdata,
		    NULL);
		if (ret != CRYPTO_SUCCESS) {
			ret = SET_ERROR(EIO);
			goto error;
		}
	} else {
		ret = crypto_decrypt(&mech, &cipherdata, key, tmpl, &plaindata,
		    NULL);
		if (ret != CRYPTO_SUCCESS) {
			ASSERT3U(ret, ==, CRYPTO_INVALID_MAC);
			ret = SET_ERROR(ECKSUM);
			goto error;
		}
	}

	return (0);

error:
	return (ret);
}

int
zio_crypt_key_wrap(crypto_key_t *cwkey, zio_crypt_key_t *key, uint8_t *iv,
    uint8_t *mac, uint8_t *keydata_out, uint8_t *hmac_keydata_out)
{
	int ret;
	zfs_uio_t puio, cuio;
	uint64_t aad[3];
	iovec_t plain_iovecs[2], cipher_iovecs[3];
	uint64_t crypt = key->zk_crypt;
	uint_t enc_len, keydata_len, aad_len;

	ASSERT3U(crypt, <, ZIO_CRYPT_FUNCTIONS);
	ASSERT3U(cwkey->ck_format, ==, CRYPTO_KEY_RAW);

	keydata_len = zio_crypt_table[crypt].ci_keylen;

	/* generate iv for wrapping the master and hmac key */
	ret = random_get_pseudo_bytes(iv, WRAPPING_IV_LEN);
	if (ret != 0)
		goto error;

	/* initialize zfs_uio_ts */
	plain_iovecs[0].iov_base = key->zk_master_keydata;
	plain_iovecs[0].iov_len = keydata_len;
	plain_iovecs[1].iov_base = key->zk_hmac_keydata;
	plain_iovecs[1].iov_len = SHA512_HMAC_KEYLEN;

	cipher_iovecs[0].iov_base = keydata_out;
	cipher_iovecs[0].iov_len = keydata_len;
	cipher_iovecs[1].iov_base = hmac_keydata_out;
	cipher_iovecs[1].iov_len = SHA512_HMAC_KEYLEN;
	cipher_iovecs[2].iov_base = mac;
	cipher_iovecs[2].iov_len = WRAPPING_MAC_LEN;

	/*
	 * Although we don't support writing to the old format, we do
	 * support rewrapping the key so that the user can move and
	 * quarantine datasets on the old format.
	 */
	if (key->zk_version == 0) {
		aad_len = sizeof (uint64_t);
		aad[0] = LE_64(key->zk_guid);
	} else {
		ASSERT3U(key->zk_version, ==, ZIO_CRYPT_KEY_CURRENT_VERSION);
		aad_len = sizeof (uint64_t) * 3;
		aad[0] = LE_64(key->zk_guid);
		aad[1] = LE_64(crypt);
		aad[2] = LE_64(key->zk_version);
	}

	enc_len = zio_crypt_table[crypt].ci_keylen + SHA512_HMAC_KEYLEN;
	puio.uio_iov = plain_iovecs;
	puio.uio_iovcnt = 2;
	puio.uio_segflg = UIO_SYSSPACE;
	cuio.uio_iov = cipher_iovecs;
	cuio.uio_iovcnt = 3;
	cuio.uio_segflg = UIO_SYSSPACE;

	/* encrypt the keys and store the resulting ciphertext and mac */
	ret = zio_do_crypt_uio(B_TRUE, crypt, cwkey, NULL, iv, enc_len,
	    &puio, &cuio, (uint8_t *)aad, aad_len);
	if (ret != 0)
		goto error;

	return (0);

error:
	return (ret);
}

int
zio_crypt_key_unwrap(crypto_key_t *cwkey, uint64_t crypt, uint64_t version,
    uint64_t guid, uint8_t *keydata, uint8_t *hmac_keydata, uint8_t *iv,
    uint8_t *mac, zio_crypt_key_t *key)
{
	crypto_mechanism_t mech;
	zfs_uio_t puio, cuio;
	uint64_t aad[3];
	iovec_t plain_iovecs[2], cipher_iovecs[3];
	uint_t enc_len, keydata_len, aad_len;
	int ret;

	ASSERT3U(crypt, <, ZIO_CRYPT_FUNCTIONS);
	ASSERT3U(cwkey->ck_format, ==, CRYPTO_KEY_RAW);

	rw_init(&key->zk_salt_lock, NULL, RW_DEFAULT, NULL);

	keydata_len = zio_crypt_table[crypt].ci_keylen;

	/* initialize zfs_uio_ts */
	plain_iovecs[0].iov_base = key->zk_master_keydata;
	plain_iovecs[0].iov_len = keydata_len;
	plain_iovecs[1].iov_base = key->zk_hmac_keydata;
	plain_iovecs[1].iov_len = SHA512_HMAC_KEYLEN;

	cipher_iovecs[0].iov_base = keydata;
	cipher_iovecs[0].iov_len = keydata_len;
	cipher_iovecs[1].iov_base = hmac_keydata;
	cipher_iovecs[1].iov_len = SHA512_HMAC_KEYLEN;
	cipher_iovecs[2].iov_base = mac;
	cipher_iovecs[2].iov_len = WRAPPING_MAC_LEN;

	if (version == 0) {
		aad_len = sizeof (uint64_t);
		aad[0] = LE_64(guid);
	} else {
		ASSERT3U(version, ==, ZIO_CRYPT_KEY_CURRENT_VERSION);
		aad_len = sizeof (uint64_t) * 3;
		aad[0] = LE_64(guid);
		aad[1] = LE_64(crypt);
		aad[2] = LE_64(version);
	}

	enc_len = keydata_len + SHA512_HMAC_KEYLEN;
	puio.uio_iov = plain_iovecs;
	puio.uio_segflg = UIO_SYSSPACE;
	puio.uio_iovcnt = 2;
	cuio.uio_iov = cipher_iovecs;
	cuio.uio_iovcnt = 3;
	cuio.uio_segflg = UIO_SYSSPACE;

	/* decrypt the keys and store the result in the output buffers */
	ret = zio_do_crypt_uio(B_FALSE, crypt, cwkey, NULL, iv, enc_len,
	    &puio, &cuio, (uint8_t *)aad, aad_len);
	if (ret != 0)
		goto error;

	/* generate a fresh salt */
	ret = random_get_bytes(key->zk_salt, ZIO_DATA_SALT_LEN);
	if (ret != 0)
		goto error;

	/* derive the current key from the master key */
	ret = hkdf_sha512(key->zk_master_keydata, keydata_len, NULL, 0,
	    key->zk_salt, ZIO_DATA_SALT_LEN, key->zk_current_keydata,
	    keydata_len);
	if (ret != 0)
		goto error;

	/* initialize keys for ICP */
	key->zk_current_key.ck_format = CRYPTO_KEY_RAW;
	key->zk_current_key.ck_data = key->zk_current_keydata;
	key->zk_current_key.ck_length = CRYPTO_BYTES2BITS(keydata_len);

	key->zk_hmac_key.ck_format = CRYPTO_KEY_RAW;
	key->zk_hmac_key.ck_data = key->zk_hmac_keydata;
	key->zk_hmac_key.ck_length = CRYPTO_BYTES2BITS(SHA512_HMAC_KEYLEN);

	/*
	 * Initialize the crypto templates. It's ok if this fails because
	 * this is just an optimization.
	 */
	mech.cm_type = crypto_mech2id(zio_crypt_table[crypt].ci_mechname);
	ret = crypto_create_ctx_template(&mech, &key->zk_current_key,
	    &key->zk_current_tmpl, KM_SLEEP);
	if (ret != CRYPTO_SUCCESS)
		key->zk_current_tmpl = NULL;

	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	ret = crypto_create_ctx_template(&mech, &key->zk_hmac_key,
	    &key->zk_hmac_tmpl, KM_SLEEP);
	if (ret != CRYPTO_SUCCESS)
		key->zk_hmac_tmpl = NULL;

	key->zk_crypt = crypt;
	key->zk_version = version;
	key->zk_guid = guid;
	key->zk_salt_count = 0;

	return (0);

error:
	zio_crypt_key_destroy(key);
	return (ret);
}

int
zio_crypt_generate_iv(uint8_t *ivbuf)
{
	int ret;

	/* randomly generate the IV */
	ret = random_get_pseudo_bytes(ivbuf, ZIO_DATA_IV_LEN);
	if (ret != 0)
		goto error;

	return (0);

error:
	bzero(ivbuf, ZIO_DATA_IV_LEN);
	return (ret);
}

int
zio_crypt_do_hmac(zio_crypt_key_t *key, uint8_t *data, uint_t datalen,
    uint8_t *digestbuf, uint_t digestlen)
{
	int ret;
	crypto_mechanism_t mech;
	crypto_data_t in_data, digest_data;
	uint8_t raw_digestbuf[SHA512_DIGEST_LENGTH];

	ASSERT3U(digestlen, <=, SHA512_DIGEST_LENGTH);

	/* initialize sha512-hmac mechanism and crypto data */
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	mech.cm_param = NULL;
	mech.cm_param_len = 0;

	/* initialize the crypto data */
	in_data.cd_format = CRYPTO_DATA_RAW;
	in_data.cd_offset = 0;
	in_data.cd_length = datalen;
	in_data.cd_raw.iov_base = (char *)data;
	in_data.cd_raw.iov_len = in_data.cd_length;

	digest_data.cd_format = CRYPTO_DATA_RAW;
	digest_data.cd_offset = 0;
	digest_data.cd_length = SHA512_DIGEST_LENGTH;
	digest_data.cd_raw.iov_base = (char *)raw_digestbuf;
	digest_data.cd_raw.iov_len = digest_data.cd_length;

	/* generate the hmac */
	ret = crypto_mac(&mech, &in_data, &key->zk_hmac_key, key->zk_hmac_tmpl,
	    &digest_data, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	bcopy(raw_digestbuf, digestbuf, digestlen);

	return (0);

error:
	bzero(digestbuf, digestlen);
	return (ret);
}

int
zio_crypt_generate_iv_salt_dedup(zio_crypt_key_t *key, uint8_t *data,
    uint_t datalen, uint8_t *ivbuf, uint8_t *salt)
{
	int ret;
	uint8_t digestbuf[SHA512_DIGEST_LENGTH];

	ret = zio_crypt_do_hmac(key, data, datalen,
	    digestbuf, SHA512_DIGEST_LENGTH);
	if (ret != 0)
		return (ret);

	bcopy(digestbuf, salt, ZIO_DATA_SALT_LEN);
	bcopy(digestbuf + ZIO_DATA_SALT_LEN, ivbuf, ZIO_DATA_IV_LEN);

	return (0);
}

/*
 * The following functions are used to encode and decode encryption parameters
 * into blkptr_t and zil_header_t. The ICP wants to use these parameters as
 * byte strings, which normally means that these strings would not need to deal
 * with byteswapping at all. However, both blkptr_t and zil_header_t may be
 * byteswapped by lower layers and so we must "undo" that byteswap here upon
 * decoding and encoding in a non-native byteorder. These functions require
 * that the byteorder bit is correct before being called.
 */
void
zio_crypt_encode_params_bp(blkptr_t *bp, uint8_t *salt, uint8_t *iv)
{
	uint64_t val64;
	uint32_t val32;

	ASSERT(BP_IS_ENCRYPTED(bp));

	if (!BP_SHOULD_BYTESWAP(bp)) {
		bcopy(salt, &bp->blk_dva[2].dva_word[0], sizeof (uint64_t));
		bcopy(iv, &bp->blk_dva[2].dva_word[1], sizeof (uint64_t));
		bcopy(iv + sizeof (uint64_t), &val32, sizeof (uint32_t));
		BP_SET_IV2(bp, val32);
	} else {
		bcopy(salt, &val64, sizeof (uint64_t));
		bp->blk_dva[2].dva_word[0] = BSWAP_64(val64);

		bcopy(iv, &val64, sizeof (uint64_t));
		bp->blk_dva[2].dva_word[1] = BSWAP_64(val64);

		bcopy(iv + sizeof (uint64_t), &val32, sizeof (uint32_t));
		BP_SET_IV2(bp, BSWAP_32(val32));
	}
}

void
zio_crypt_decode_params_bp(const blkptr_t *bp, uint8_t *salt, uint8_t *iv)
{
	uint64_t val64;
	uint32_t val32;

	ASSERT(BP_IS_PROTECTED(bp));

	/* for convenience, so callers don't need to check */
	if (BP_IS_AUTHENTICATED(bp)) {
		bzero(salt, ZIO_DATA_SALT_LEN);
		bzero(iv, ZIO_DATA_IV_LEN);
		return;
	}

	if (!BP_SHOULD_BYTESWAP(bp)) {
		bcopy(&bp->blk_dva[2].dva_word[0], salt, sizeof (uint64_t));
		bcopy(&bp->blk_dva[2].dva_word[1], iv, sizeof (uint64_t));

		val32 = (uint32_t)BP_GET_IV2(bp);
		bcopy(&val32, iv + sizeof (uint64_t), sizeof (uint32_t));
	} else {
		val64 = BSWAP_64(bp->blk_dva[2].dva_word[0]);
		bcopy(&val64, salt, sizeof (uint64_t));

		val64 = BSWAP_64(bp->blk_dva[2].dva_word[1]);
		bcopy(&val64, iv, sizeof (uint64_t));

		val32 = BSWAP_32((uint32_t)BP_GET_IV2(bp));
		bcopy(&val32, iv + sizeof (uint64_t), sizeof (uint32_t));
	}
}

void
zio_crypt_encode_mac_bp(blkptr_t *bp, uint8_t *mac)
{
	uint64_t val64;

	ASSERT(BP_USES_CRYPT(bp));
	ASSERT3U(BP_GET_TYPE(bp), !=, DMU_OT_OBJSET);

	if (!BP_SHOULD_BYTESWAP(bp)) {
		bcopy(mac, &bp->blk_cksum.zc_word[2], sizeof (uint64_t));
		bcopy(mac + sizeof (uint64_t), &bp->blk_cksum.zc_word[3],
		    sizeof (uint64_t));
	} else {
		bcopy(mac, &val64, sizeof (uint64_t));
		bp->blk_cksum.zc_word[2] = BSWAP_64(val64);

		bcopy(mac + sizeof (uint64_t), &val64, sizeof (uint64_t));
		bp->blk_cksum.zc_word[3] = BSWAP_64(val64);
	}
}

void
zio_crypt_decode_mac_bp(const blkptr_t *bp, uint8_t *mac)
{
	uint64_t val64;

	ASSERT(BP_USES_CRYPT(bp) || BP_IS_HOLE(bp));

	/* for convenience, so callers don't need to check */
	if (BP_GET_TYPE(bp) == DMU_OT_OBJSET) {
		bzero(mac, ZIO_DATA_MAC_LEN);
		return;
	}

	if (!BP_SHOULD_BYTESWAP(bp)) {
		bcopy(&bp->blk_cksum.zc_word[2], mac, sizeof (uint64_t));
		bcopy(&bp->blk_cksum.zc_word[3], mac + sizeof (uint64_t),
		    sizeof (uint64_t));
	} else {
		val64 = BSWAP_64(bp->blk_cksum.zc_word[2]);
		bcopy(&val64, mac, sizeof (uint64_t));

		val64 = BSWAP_64(bp->blk_cksum.zc_word[3]);
		bcopy(&val64, mac + sizeof (uint64_t), sizeof (uint64_t));
	}
}

void
zio_crypt_encode_mac_zil(void *data, uint8_t *mac)
{
	zil_chain_t *zilc = data;

	bcopy(mac, &zilc->zc_eck.zec_cksum.zc_word[2], sizeof (uint64_t));
	bcopy(mac + sizeof (uint64_t), &zilc->zc_eck.zec_cksum.zc_word[3],
	    sizeof (uint64_t));
}

void
zio_crypt_decode_mac_zil(const void *data, uint8_t *mac)
{
	/*
	 * The ZIL MAC is embedded in the block it protects, which will
	 * not have been byteswapped by the time this function has been called.
	 * As a result, we don't need to worry about byteswapping the MAC.
	 */
	const zil_chain_t *zilc = data;

	bcopy(&zilc->zc_eck.zec_cksum.zc_word[2], mac, sizeof (uint64_t));
	bcopy(&zilc->zc_eck.zec_cksum.zc_word[3], mac + sizeof (uint64_t),
	    sizeof (uint64_t));
}

/*
 * This routine takes a block of dnodes (src_abd) and copies only the bonus
 * buffers to the same offsets in the dst buffer. datalen should be the size
 * of both the src_abd and the dst buffer (not just the length of the bonus
 * buffers).
 */
void
zio_crypt_copy_dnode_bonus(abd_t *src_abd, uint8_t *dst, uint_t datalen)
{
	uint_t i, max_dnp = datalen >> DNODE_SHIFT;
	uint8_t *src;
	dnode_phys_t *dnp, *sdnp, *ddnp;

	src = abd_borrow_buf_copy(src_abd, datalen);

	sdnp = (dnode_phys_t *)src;
	ddnp = (dnode_phys_t *)dst;

	for (i = 0; i < max_dnp; i += sdnp[i].dn_extra_slots + 1) {
		dnp = &sdnp[i];
		if (dnp->dn_type != DMU_OT_NONE &&
		    DMU_OT_IS_ENCRYPTED(dnp->dn_bonustype) &&
		    dnp->dn_bonuslen != 0) {
			bcopy(DN_BONUS(dnp), DN_BONUS(&ddnp[i]),
			    DN_MAX_BONUS_LEN(dnp));
		}
	}

	abd_return_buf(src_abd, src, datalen);
}

/*
 * This function decides what fields from blk_prop are included in
 * the on-disk various MAC algorithms.
 */
static void
zio_crypt_bp_zero_nonportable_blkprop(blkptr_t *bp, uint64_t version)
{
	/*
	 * Version 0 did not properly zero out all non-portable fields
	 * as it should have done. We maintain this code so that we can
	 * do read-only imports of pools on this version.
	 */
	if (version == 0) {
		BP_SET_DEDUP(bp, 0);
		BP_SET_CHECKSUM(bp, 0);
		BP_SET_PSIZE(bp, SPA_MINBLOCKSIZE);
		return;
	}

	ASSERT3U(version, ==, ZIO_CRYPT_KEY_CURRENT_VERSION);

	/*
	 * The hole_birth feature might set these fields even if this bp
	 * is a hole. We zero them out here to guarantee that raw sends
	 * will function with or without the feature.
	 */
	if (BP_IS_HOLE(bp)) {
		bp->blk_prop = 0ULL;
		return;
	}

	/*
	 * At L0 we want to verify these fields to ensure that data blocks
	 * can not be reinterpreted. For instance, we do not want an attacker
	 * to trick us into returning raw lz4 compressed data to the user
	 * by modifying the compression bits. At higher levels, we cannot
	 * enforce this policy since raw sends do not convey any information
	 * about indirect blocks, so these values might be different on the
	 * receive side. Fortunately, this does not open any new attack
	 * vectors, since any alterations that can be made to a higher level
	 * bp must still verify the correct order of the layer below it.
	 */
	if (BP_GET_LEVEL(bp) != 0) {
		BP_SET_BYTEORDER(bp, 0);
		BP_SET_COMPRESS(bp, 0);

		/*
		 * psize cannot be set to zero or it will trigger
		 * asserts, but the value doesn't really matter as
		 * long as it is constant.
		 */
		BP_SET_PSIZE(bp, SPA_MINBLOCKSIZE);
	}

	BP_SET_DEDUP(bp, 0);
	BP_SET_CHECKSUM(bp, 0);
}

static void
zio_crypt_bp_auth_init(uint64_t version, boolean_t should_bswap, blkptr_t *bp,
    blkptr_auth_buf_t *bab, uint_t *bab_len)
{
	blkptr_t tmpbp = *bp;

	if (should_bswap)
		byteswap_uint64_array(&tmpbp, sizeof (blkptr_t));

	ASSERT(BP_USES_CRYPT(&tmpbp) || BP_IS_HOLE(&tmpbp));
	ASSERT0(BP_IS_EMBEDDED(&tmpbp));

	zio_crypt_decode_mac_bp(&tmpbp, bab->bab_mac);

	/*
	 * We always MAC blk_prop in LE to ensure portability. This
	 * must be done after decoding the mac, since the endianness
	 * will get zero'd out here.
	 */
	zio_crypt_bp_zero_nonportable_blkprop(&tmpbp, version);
	bab->bab_prop = LE_64(tmpbp.blk_prop);
	bab->bab_pad = 0ULL;

	/* version 0 did not include the padding */
	*bab_len = sizeof (blkptr_auth_buf_t);
	if (version == 0)
		*bab_len -= sizeof (uint64_t);
}

static int
zio_crypt_bp_do_hmac_updates(crypto_context_t ctx, uint64_t version,
    boolean_t should_bswap, blkptr_t *bp)
{
	int ret;
	uint_t bab_len;
	blkptr_auth_buf_t bab;
	crypto_data_t cd;

	zio_crypt_bp_auth_init(version, should_bswap, bp, &bab, &bab_len);
	cd.cd_format = CRYPTO_DATA_RAW;
	cd.cd_offset = 0;
	cd.cd_length = bab_len;
	cd.cd_raw.iov_base = (char *)&bab;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_update(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	return (0);

error:
	return (ret);
}

static void
zio_crypt_bp_do_indrect_checksum_updates(SHA2_CTX *ctx, uint64_t version,
    boolean_t should_bswap, blkptr_t *bp)
{
	uint_t bab_len;
	blkptr_auth_buf_t bab;

	zio_crypt_bp_auth_init(version, should_bswap, bp, &bab, &bab_len);
	SHA2Update(ctx, &bab, bab_len);
}

static void
zio_crypt_bp_do_aad_updates(uint8_t **aadp, uint_t *aad_len, uint64_t version,
    boolean_t should_bswap, blkptr_t *bp)
{
	uint_t bab_len;
	blkptr_auth_buf_t bab;

	zio_crypt_bp_auth_init(version, should_bswap, bp, &bab, &bab_len);
	bcopy(&bab, *aadp, bab_len);
	*aadp += bab_len;
	*aad_len += bab_len;
}

static int
zio_crypt_do_dnode_hmac_updates(crypto_context_t ctx, uint64_t version,
    boolean_t should_bswap, dnode_phys_t *dnp)
{
	int ret, i;
	dnode_phys_t *adnp, tmp_dncore;
	size_t dn_core_size = offsetof(dnode_phys_t, dn_blkptr);
	boolean_t le_bswap = (should_bswap == ZFS_HOST_BYTEORDER);
	crypto_data_t cd;

	cd.cd_format = CRYPTO_DATA_RAW;
	cd.cd_offset = 0;

	/*
	 * Authenticate the core dnode (masking out non-portable bits).
	 * We only copy the first 64 bytes we operate on to avoid the overhead
	 * of copying 512-64 unneeded bytes. The compiler seems to be fine
	 * with that.
	 */
	bcopy(dnp, &tmp_dncore, dn_core_size);
	adnp = &tmp_dncore;

	if (le_bswap) {
		adnp->dn_datablkszsec = BSWAP_16(adnp->dn_datablkszsec);
		adnp->dn_bonuslen = BSWAP_16(adnp->dn_bonuslen);
		adnp->dn_maxblkid = BSWAP_64(adnp->dn_maxblkid);
		adnp->dn_used = BSWAP_64(adnp->dn_used);
	}
	adnp->dn_flags &= DNODE_CRYPT_PORTABLE_FLAGS_MASK;
	adnp->dn_used = 0;

	cd.cd_length = dn_core_size;
	cd.cd_raw.iov_base = (char *)adnp;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_update(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	for (i = 0; i < dnp->dn_nblkptr; i++) {
		ret = zio_crypt_bp_do_hmac_updates(ctx, version,
		    should_bswap, &dnp->dn_blkptr[i]);
		if (ret != 0)
			goto error;
	}

	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		ret = zio_crypt_bp_do_hmac_updates(ctx, version,
		    should_bswap, DN_SPILL_BLKPTR(dnp));
		if (ret != 0)
			goto error;
	}

	return (0);

error:
	return (ret);
}

/*
 * objset_phys_t blocks introduce a number of exceptions to the normal
 * authentication process. objset_phys_t's contain 2 separate HMACS for
 * protecting the integrity of their data. The portable_mac protects the
 * metadnode. This MAC can be sent with a raw send and protects against
 * reordering of data within the metadnode. The local_mac protects the user
 * accounting objects which are not sent from one system to another.
 *
 * In addition, objset blocks are the only blocks that can be modified and
 * written to disk without the key loaded under certain circumstances. During
 * zil_claim() we need to be able to update the zil_header_t to complete
 * claiming log blocks and during raw receives we need to write out the
 * portable_mac from the send file. Both of these actions are possible
 * because these fields are not protected by either MAC so neither one will
 * need to modify the MACs without the key. However, when the modified blocks
 * are written out they will be byteswapped into the host machine's native
 * endianness which will modify fields protected by the MAC. As a result, MAC
 * calculation for objset blocks works slightly differently from other block
 * types. Where other block types MAC the data in whatever endianness is
 * written to disk, objset blocks always MAC little endian version of their
 * values. In the code, should_bswap is the value from BP_SHOULD_BYTESWAP()
 * and le_bswap indicates whether a byteswap is needed to get this block
 * into little endian format.
 */
int
zio_crypt_do_objset_hmacs(zio_crypt_key_t *key, void *data, uint_t datalen,
    boolean_t should_bswap, uint8_t *portable_mac, uint8_t *local_mac)
{
	int ret;
	crypto_mechanism_t mech;
	crypto_context_t ctx;
	crypto_data_t cd;
	objset_phys_t *osp = data;
	uint64_t intval;
	boolean_t le_bswap = (should_bswap == ZFS_HOST_BYTEORDER);
	uint8_t raw_portable_mac[SHA512_DIGEST_LENGTH];
	uint8_t raw_local_mac[SHA512_DIGEST_LENGTH];

	/* initialize HMAC mechanism */
	mech.cm_type = crypto_mech2id(SUN_CKM_SHA512_HMAC);
	mech.cm_param = NULL;
	mech.cm_param_len = 0;

	cd.cd_format = CRYPTO_DATA_RAW;
	cd.cd_offset = 0;

	/* calculate the portable MAC from the portable fields and metadnode */
	ret = crypto_mac_init(&mech, &key->zk_hmac_key, NULL, &ctx, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	/* add in the os_type */
	intval = (le_bswap) ? osp->os_type : BSWAP_64(osp->os_type);
	cd.cd_length = sizeof (uint64_t);
	cd.cd_raw.iov_base = (char *)&intval;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_update(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	/* add in the portable os_flags */
	intval = osp->os_flags;
	if (should_bswap)
		intval = BSWAP_64(intval);
	intval &= OBJSET_CRYPT_PORTABLE_FLAGS_MASK;
	if (!ZFS_HOST_BYTEORDER)
		intval = BSWAP_64(intval);

	cd.cd_length = sizeof (uint64_t);
	cd.cd_raw.iov_base = (char *)&intval;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_update(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	/* add in fields from the metadnode */
	ret = zio_crypt_do_dnode_hmac_updates(ctx, key->zk_version,
	    should_bswap, &osp->os_meta_dnode);
	if (ret)
		goto error;

	/* store the final digest in a temporary buffer and copy what we need */
	cd.cd_length = SHA512_DIGEST_LENGTH;
	cd.cd_raw.iov_base = (char *)raw_portable_mac;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_final(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	bcopy(raw_portable_mac, portable_mac, ZIO_OBJSET_MAC_LEN);

	/*
	 * This is necessary here as we check next whether
	 * OBJSET_FLAG_USERACCOUNTING_COMPLETE is set in order to
	 * decide if the local_mac should be zeroed out. That flag will always
	 * be set by dmu_objset_id_quota_upgrade_cb() and
	 * dmu_objset_userspace_upgrade_cb() if useraccounting has been
	 * completed.
	 */
	intval = osp->os_flags;
	if (should_bswap)
		intval = BSWAP_64(intval);
	boolean_t uacct_incomplete =
	    !(intval & OBJSET_FLAG_USERACCOUNTING_COMPLETE);

	/*
	 * The local MAC protects the user, group and project accounting.
	 * If these objects are not present, the local MAC is zeroed out.
	 */
	if (uacct_incomplete ||
	    (datalen >= OBJSET_PHYS_SIZE_V3 &&
	    osp->os_userused_dnode.dn_type == DMU_OT_NONE &&
	    osp->os_groupused_dnode.dn_type == DMU_OT_NONE &&
	    osp->os_projectused_dnode.dn_type == DMU_OT_NONE) ||
	    (datalen >= OBJSET_PHYS_SIZE_V2 &&
	    osp->os_userused_dnode.dn_type == DMU_OT_NONE &&
	    osp->os_groupused_dnode.dn_type == DMU_OT_NONE) ||
	    (datalen <= OBJSET_PHYS_SIZE_V1)) {
		bzero(local_mac, ZIO_OBJSET_MAC_LEN);
		return (0);
	}

	/* calculate the local MAC from the userused and groupused dnodes */
	ret = crypto_mac_init(&mech, &key->zk_hmac_key, NULL, &ctx, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	/* add in the non-portable os_flags */
	intval = osp->os_flags;
	if (should_bswap)
		intval = BSWAP_64(intval);
	intval &= ~OBJSET_CRYPT_PORTABLE_FLAGS_MASK;
	if (!ZFS_HOST_BYTEORDER)
		intval = BSWAP_64(intval);

	cd.cd_length = sizeof (uint64_t);
	cd.cd_raw.iov_base = (char *)&intval;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_update(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	/* add in fields from the user accounting dnodes */
	if (osp->os_userused_dnode.dn_type != DMU_OT_NONE) {
		ret = zio_crypt_do_dnode_hmac_updates(ctx, key->zk_version,
		    should_bswap, &osp->os_userused_dnode);
		if (ret)
			goto error;
	}

	if (osp->os_groupused_dnode.dn_type != DMU_OT_NONE) {
		ret = zio_crypt_do_dnode_hmac_updates(ctx, key->zk_version,
		    should_bswap, &osp->os_groupused_dnode);
		if (ret)
			goto error;
	}

	if (osp->os_projectused_dnode.dn_type != DMU_OT_NONE &&
	    datalen >= OBJSET_PHYS_SIZE_V3) {
		ret = zio_crypt_do_dnode_hmac_updates(ctx, key->zk_version,
		    should_bswap, &osp->os_projectused_dnode);
		if (ret)
			goto error;
	}

	/* store the final digest in a temporary buffer and copy what we need */
	cd.cd_length = SHA512_DIGEST_LENGTH;
	cd.cd_raw.iov_base = (char *)raw_local_mac;
	cd.cd_raw.iov_len = cd.cd_length;

	ret = crypto_mac_final(ctx, &cd, NULL);
	if (ret != CRYPTO_SUCCESS) {
		ret = SET_ERROR(EIO);
		goto error;
	}

	bcopy(raw_local_mac, local_mac, ZIO_OBJSET_MAC_LEN);

	return (0);

error:
	bzero(portable_mac, ZIO_OBJSET_MAC_LEN);
	bzero(local_mac, ZIO_OBJSET_MAC_LEN);
	return (ret);
}

static void
zio_crypt_destroy_uio(zfs_uio_t *uio)
{
	if (uio->uio_iov)
		kmem_free(uio->uio_iov, uio->uio_iovcnt * sizeof (iovec_t));
}

/*
 * This function parses an uncompressed indirect block and returns a checksum
 * of all the portable fields from all of the contained bps. The portable
 * fields are the MAC and all of the fields from blk_prop except for the dedup,
 * checksum, and psize bits. For an explanation of the purpose of this, see
 * the comment block on object set authentication.
 */
static int
zio_crypt_do_indirect_mac_checksum_impl(boolean_t generate, void *buf,
    uint_t datalen, uint64_t version, boolean_t byteswap, uint8_t *cksum)
{
	blkptr_t *bp;
	int i, epb = datalen >> SPA_BLKPTRSHIFT;
	SHA2_CTX ctx;
	uint8_t digestbuf[SHA512_DIGEST_LENGTH];

	/* checksum all of the MACs from the layer below */
	SHA2Init(SHA512, &ctx);
	for (i = 0, bp = buf; i < epb; i++, bp++) {
		zio_crypt_bp_do_indrect_checksum_updates(&ctx, version,
		    byteswap, bp);
	}
	SHA2Final(digestbuf, &ctx);

	if (generate) {
		bcopy(digestbuf, cksum, ZIO_DATA_MAC_LEN);
		return (0);
	}

	if (bcmp(digestbuf, cksum, ZIO_DATA_MAC_LEN) != 0)
		return (SET_ERROR(ECKSUM));

	return (0);
}

int
zio_crypt_do_indirect_mac_checksum(boolean_t generate, void *buf,
    uint_t datalen, boolean_t byteswap, uint8_t *cksum)
{
	int ret;

	/*
	 * Unfortunately, callers of this function will not always have
	 * easy access to the on-disk format version. This info is
	 * normally found in the DSL Crypto Key, but the checksum-of-MACs
	 * is expected to be verifiable even when the key isn't loaded.
	 * Here, instead of doing a ZAP lookup for the version for each
	 * zio, we simply try both existing formats.
	 */
	ret = zio_crypt_do_indirect_mac_checksum_impl(generate, buf,
	    datalen, ZIO_CRYPT_KEY_CURRENT_VERSION, byteswap, cksum);
	if (ret == ECKSUM) {
		ASSERT(!generate);
		ret = zio_crypt_do_indirect_mac_checksum_impl(generate,
		    buf, datalen, 0, byteswap, cksum);
	}

	return (ret);
}

int
zio_crypt_do_indirect_mac_checksum_abd(boolean_t generate, abd_t *abd,
    uint_t datalen, boolean_t byteswap, uint8_t *cksum)
{
	int ret;
	void *buf;

	buf = abd_borrow_buf_copy(abd, datalen);
	ret = zio_crypt_do_indirect_mac_checksum(generate, buf, datalen,
	    byteswap, cksum);
	abd_return_buf(abd, buf, datalen);

	return (ret);
}

/*
 * Special case handling routine for encrypting / decrypting ZIL blocks.
 * We do not check for the older ZIL chain because the encryption feature
 * was not available before the newer ZIL chain was introduced. The goal
 * here is to encrypt everything except the blkptr_t of a lr_write_t and
 * the zil_chain_t header. Everything that is not encrypted is authenticated.
 */
static int
zio_crypt_init_uios_zil(boolean_t encrypt, uint8_t *plainbuf,
    uint8_t *cipherbuf, uint_t datalen, boolean_t byteswap, zfs_uio_t *puio,
    zfs_uio_t *cuio, uint_t *enc_len, uint8_t **authbuf, uint_t *auth_len,
    boolean_t *no_crypt)
{
	int ret;
	uint64_t txtype, lr_len;
	uint_t nr_src, nr_dst, crypt_len;
	uint_t aad_len = 0, nr_iovecs = 0, total_len = 0;
	iovec_t *src_iovecs = NULL, *dst_iovecs = NULL;
	uint8_t *src, *dst, *slrp, *dlrp, *blkend, *aadp;
	zil_chain_t *zilc;
	lr_t *lr;
	uint8_t *aadbuf = zio_buf_alloc(datalen);

	/* cipherbuf always needs an extra iovec for the MAC */
	if (encrypt) {
		src = plainbuf;
		dst = cipherbuf;
		nr_src = 0;
		nr_dst = 1;
	} else {
		src = cipherbuf;
		dst = plainbuf;
		nr_src = 1;
		nr_dst = 0;
	}
	bzero(dst, datalen);

	/* find the start and end record of the log block */
	zilc = (zil_chain_t *)src;
	slrp = src + sizeof (zil_chain_t);
	aadp = aadbuf;
	blkend = src + ((byteswap) ? BSWAP_64(zilc->zc_nused) : zilc->zc_nused);

	/* calculate the number of encrypted iovecs we will need */
	for (; slrp < blkend; slrp += lr_len) {
		lr = (lr_t *)slrp;

		if (!byteswap) {
			txtype = lr->lrc_txtype;
			lr_len = lr->lrc_reclen;
		} else {
			txtype = BSWAP_64(lr->lrc_txtype);
			lr_len = BSWAP_64(lr->lrc_reclen);
		}

		nr_iovecs++;
		if (txtype == TX_WRITE && lr_len != sizeof (lr_write_t))
			nr_iovecs++;
	}

	nr_src += nr_iovecs;
	nr_dst += nr_iovecs;

	/* allocate the iovec arrays */
	if (nr_src != 0) {
		src_iovecs = kmem_alloc(nr_src * sizeof (iovec_t), KM_SLEEP);
		if (src_iovecs == NULL) {
			ret = SET_ERROR(ENOMEM);
			goto error;
		}
	}

	if (nr_dst != 0) {
		dst_iovecs = kmem_alloc(nr_dst * sizeof (iovec_t), KM_SLEEP);
		if (dst_iovecs == NULL) {
			ret = SET_ERROR(ENOMEM);
			goto error;
		}
	}

	/*
	 * Copy the plain zil header over and authenticate everything except
	 * the checksum that will store our MAC. If we are writing the data
	 * the embedded checksum will not have been calculated yet, so we don't
	 * authenticate that.
	 */
	bcopy(src, dst, sizeof (zil_chain_t));
	bcopy(src, aadp, sizeof (zil_chain_t) - sizeof (zio_eck_t));
	aadp += sizeof (zil_chain_t) - sizeof (zio_eck_t);
	aad_len += sizeof (zil_chain_t) - sizeof (zio_eck_t);

	/* loop over records again, filling in iovecs */
	nr_iovecs = 0;
	slrp = src + sizeof (zil_chain_t);
	dlrp = dst + sizeof (zil_chain_t);

	for (; slrp < blkend; slrp += lr_len, dlrp += lr_len) {
		lr = (lr_t *)slrp;

		if (!byteswap) {
			txtype = lr->lrc_txtype;
			lr_len = lr->lrc_reclen;
		} else {
			txtype = BSWAP_64(lr->lrc_txtype);
			lr_len = BSWAP_64(lr->lrc_reclen);
		}

		/* copy the common lr_t */
		bcopy(slrp, dlrp, sizeof (lr_t));
		bcopy(slrp, aadp, sizeof (lr_t));
		aadp += sizeof (lr_t);
		aad_len += sizeof (lr_t);

		ASSERT3P(src_iovecs, !=, NULL);
		ASSERT3P(dst_iovecs, !=, NULL);

		/*
		 * If this is a TX_WRITE record we want to encrypt everything
		 * except the bp if exists. If the bp does exist we want to
		 * authenticate it.
		 */
		if (txtype == TX_WRITE) {
			crypt_len = sizeof (lr_write_t) -
			    sizeof (lr_t) - sizeof (blkptr_t);
			src_iovecs[nr_iovecs].iov_base = slrp + sizeof (lr_t);
			src_iovecs[nr_iovecs].iov_len = crypt_len;
			dst_iovecs[nr_iovecs].iov_base = dlrp + sizeof (lr_t);
			dst_iovecs[nr_iovecs].iov_len = crypt_len;

			/* copy the bp now since it will not be encrypted */
			bcopy(slrp + sizeof (lr_write_t) - sizeof (blkptr_t),
			    dlrp + sizeof (lr_write_t) - sizeof (blkptr_t),
			    sizeof (blkptr_t));
			bcopy(slrp + sizeof (lr_write_t) - sizeof (blkptr_t),
			    aadp, sizeof (blkptr_t));
			aadp += sizeof (blkptr_t);
			aad_len += sizeof (blkptr_t);
			nr_iovecs++;
			total_len += crypt_len;

			if (lr_len != sizeof (lr_write_t)) {
				crypt_len = lr_len - sizeof (lr_write_t);
				src_iovecs[nr_iovecs].iov_base =
				    slrp + sizeof (lr_write_t);
				src_iovecs[nr_iovecs].iov_len = crypt_len;
				dst_iovecs[nr_iovecs].iov_base =
				    dlrp + sizeof (lr_write_t);
				dst_iovecs[nr_iovecs].iov_len = crypt_len;
				nr_iovecs++;
				total_len += crypt_len;
			}
		} else {
			crypt_len = lr_len - sizeof (lr_t);
			src_iovecs[nr_iovecs].iov_base = slrp + sizeof (lr_t);
			src_iovecs[nr_iovecs].iov_len = crypt_len;
			dst_iovecs[nr_iovecs].iov_base = dlrp + sizeof (lr_t);
			dst_iovecs[nr_iovecs].iov_len = crypt_len;
			nr_iovecs++;
			total_len += crypt_len;
		}
	}

	*no_crypt = (nr_iovecs == 0);
	*enc_len = total_len;
	*authbuf = aadbuf;
	*auth_len = aad_len;

	if (encrypt) {
		puio->uio_iov = src_iovecs;
		puio->uio_iovcnt = nr_src;
		cuio->uio_iov = dst_iovecs;
		cuio->uio_iovcnt = nr_dst;
	} else {
		puio->uio_iov = dst_iovecs;
		puio->uio_iovcnt = nr_dst;
		cuio->uio_iov = src_iovecs;
		cuio->uio_iovcnt = nr_src;
	}

	return (0);

error:
	zio_buf_free(aadbuf, datalen);
	if (src_iovecs != NULL)
		kmem_free(src_iovecs, nr_src * sizeof (iovec_t));
	if (dst_iovecs != NULL)
		kmem_free(dst_iovecs, nr_dst * sizeof (iovec_t));

	*enc_len = 0;
	*authbuf = NULL;
	*auth_len = 0;
	*no_crypt = B_FALSE;
	puio->uio_iov = NULL;
	puio->uio_iovcnt = 0;
	cuio->uio_iov = NULL;
	cuio->uio_iovcnt = 0;
	return (ret);
}

/*
 * Special case handling routine for encrypting / decrypting dnode blocks.
 */
static int
zio_crypt_init_uios_dnode(boolean_t encrypt, uint64_t version,
    uint8_t *plainbuf, uint8_t *cipherbuf, uint_t datalen, boolean_t byteswap,
    zfs_uio_t *puio, zfs_uio_t *cuio, uint_t *enc_len, uint8_t **authbuf,
    uint_t *auth_len, boolean_t *no_crypt)
{
	int ret;
	uint_t nr_src, nr_dst, crypt_len;
	uint_t aad_len = 0, nr_iovecs = 0, total_len = 0;
	uint_t i, j, max_dnp = datalen >> DNODE_SHIFT;
	iovec_t *src_iovecs = NULL, *dst_iovecs = NULL;
	uint8_t *src, *dst, *aadp;
	dnode_phys_t *dnp, *adnp, *sdnp, *ddnp;
	uint8_t *aadbuf = zio_buf_alloc(datalen);

	if (encrypt) {
		src = plainbuf;
		dst = cipherbuf;
		nr_src = 0;
		nr_dst = 1;
	} else {
		src = cipherbuf;
		dst = plainbuf;
		nr_src = 1;
		nr_dst = 0;
	}

	sdnp = (dnode_phys_t *)src;
	ddnp = (dnode_phys_t *)dst;
	aadp = aadbuf;

	/*
	 * Count the number of iovecs we will need to do the encryption by
	 * counting the number of bonus buffers that need to be encrypted.
	 */
	for (i = 0; i < max_dnp; i += sdnp[i].dn_extra_slots + 1) {
		/*
		 * This block may still be byteswapped. However, all of the
		 * values we use are either uint8_t's (for which byteswapping
		 * is a noop) or a * != 0 check, which will work regardless
		 * of whether or not we byteswap.
		 */
		if (sdnp[i].dn_type != DMU_OT_NONE &&
		    DMU_OT_IS_ENCRYPTED(sdnp[i].dn_bonustype) &&
		    sdnp[i].dn_bonuslen != 0) {
			nr_iovecs++;
		}
	}

	nr_src += nr_iovecs;
	nr_dst += nr_iovecs;

	if (nr_src != 0) {
		src_iovecs = kmem_alloc(nr_src * sizeof (iovec_t), KM_SLEEP);
		if (src_iovecs == NULL) {
			ret = SET_ERROR(ENOMEM);
			goto error;
		}
	}

	if (nr_dst != 0) {
		dst_iovecs = kmem_alloc(nr_dst * sizeof (iovec_t), KM_SLEEP);
		if (dst_iovecs == NULL) {
			ret = SET_ERROR(ENOMEM);
			goto error;
		}
	}

	nr_iovecs = 0;

	/*
	 * Iterate through the dnodes again, this time filling in the uios
	 * we allocated earlier. We also concatenate any data we want to
	 * authenticate onto aadbuf.
	 */
	for (i = 0; i < max_dnp; i += sdnp[i].dn_extra_slots + 1) {
		dnp = &sdnp[i];

		/* copy over the core fields and blkptrs (kept as plaintext) */
		bcopy(dnp, &ddnp[i], (uint8_t *)DN_BONUS(dnp) - (uint8_t *)dnp);

		if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
			bcopy(DN_SPILL_BLKPTR(dnp), DN_SPILL_BLKPTR(&ddnp[i]),
			    sizeof (blkptr_t));
		}

		/*
		 * Handle authenticated data. We authenticate everything in
		 * the dnode that can be brought over when we do a raw send.
		 * This includes all of the core fields as well as the MACs
		 * stored in the bp checksums and all of the portable bits
		 * from blk_prop. We include the dnode padding here in case it
		 * ever gets used in the future. Some dn_flags and dn_used are
		 * not portable so we mask those out values out of the
		 * authenticated data.
		 */
		crypt_len = offsetof(dnode_phys_t, dn_blkptr);
		bcopy(dnp, aadp, crypt_len);
		adnp = (dnode_phys_t *)aadp;
		adnp->dn_flags &= DNODE_CRYPT_PORTABLE_FLAGS_MASK;
		adnp->dn_used = 0;
		aadp += crypt_len;
		aad_len += crypt_len;

		for (j = 0; j < dnp->dn_nblkptr; j++) {
			zio_crypt_bp_do_aad_updates(&aadp, &aad_len,
			    version, byteswap, &dnp->dn_blkptr[j]);
		}

		if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
			zio_crypt_bp_do_aad_updates(&aadp, &aad_len,
			    version, byteswap, DN_SPILL_BLKPTR(dnp));
		}

		/*
		 * If this bonus buffer needs to be encrypted, we prepare an
		 * iovec_t. The encryption / decryption functions will fill
		 * this in for us with the encrypted or decrypted data.
		 * Otherwise we add the bonus buffer to the authenticated
		 * data buffer and copy it over to the destination. The
		 * encrypted iovec extends to DN_MAX_BONUS_LEN(dnp) so that
		 * we can guarantee alignment with the AES block size
		 * (128 bits).
		 */
		crypt_len = DN_MAX_BONUS_LEN(dnp);
		if (dnp->dn_type != DMU_OT_NONE &&
		    DMU_OT_IS_ENCRYPTED(dnp->dn_bonustype) &&
		    dnp->dn_bonuslen != 0) {
			ASSERT3U(nr_iovecs, <, nr_src);
			ASSERT3U(nr_iovecs, <, nr_dst);
			ASSERT3P(src_iovecs, !=, NULL);
			ASSERT3P(dst_iovecs, !=, NULL);
			src_iovecs[nr_iovecs].iov_base = DN_BONUS(dnp);
			src_iovecs[nr_iovecs].iov_len = crypt_len;
			dst_iovecs[nr_iovecs].iov_base = DN_BONUS(&ddnp[i]);
			dst_iovecs[nr_iovecs].iov_len = crypt_len;

			nr_iovecs++;
			total_len += crypt_len;
		} else {
			bcopy(DN_BONUS(dnp), DN_BONUS(&ddnp[i]), crypt_len);
			bcopy(DN_BONUS(dnp), aadp, crypt_len);
			aadp += crypt_len;
			aad_len += crypt_len;
		}
	}

	*no_crypt = (nr_iovecs == 0);
	*enc_len = total_len;
	*authbuf = aadbuf;
	*auth_len = aad_len;

	if (encrypt) {
		puio->uio_iov = src_iovecs;
		puio->uio_iovcnt = nr_src;
		cuio->uio_iov = dst_iovecs;
		cuio->uio_iovcnt = nr_dst;
	} else {
		puio->uio_iov = dst_iovecs;
		puio->uio_iovcnt = nr_dst;
		cuio->uio_iov = src_iovecs;
		cuio->uio_iovcnt = nr_src;
	}

	return (0);

error:
	zio_buf_free(aadbuf, datalen);
	if (src_iovecs != NULL)
		kmem_free(src_iovecs, nr_src * sizeof (iovec_t));
	if (dst_iovecs != NULL)
		kmem_free(dst_iovecs, nr_dst * sizeof (iovec_t));

	*enc_len = 0;
	*authbuf = NULL;
	*auth_len = 0;
	*no_crypt = B_FALSE;
	puio->uio_iov = NULL;
	puio->uio_iovcnt = 0;
	cuio->uio_iov = NULL;
	cuio->uio_iovcnt = 0;
	return (ret);
}

static int
zio_crypt_init_uios_normal(boolean_t encrypt, uint8_t *plainbuf,
    uint8_t *cipherbuf, uint_t datalen, zfs_uio_t *puio, zfs_uio_t *cuio,
    uint_t *enc_len)
{
	(void) encrypt;
	int ret;
	uint_t nr_plain = 1, nr_cipher = 2;
	iovec_t *plain_iovecs = NULL, *cipher_iovecs = NULL;

	/* allocate the iovecs for the plain and cipher data */
	plain_iovecs = kmem_alloc(nr_plain * sizeof (iovec_t),
	    KM_SLEEP);
	if (!plain_iovecs) {
		ret = SET_ERROR(ENOMEM);
		goto error;
	}

	cipher_iovecs = kmem_alloc(nr_cipher * sizeof (iovec_t),
	    KM_SLEEP);
	if (!cipher_iovecs) {
		ret = SET_ERROR(ENOMEM);
		goto error;
	}

	plain_iovecs[0].iov_base = plainbuf;
	plain_iovecs[0].iov_len = datalen;
	cipher_iovecs[0].iov_base = cipherbuf;
	cipher_iovecs[0].iov_len = datalen;

	*enc_len = datalen;
	puio->uio_iov = plain_iovecs;
	puio->uio_iovcnt = nr_plain;
	cuio->uio_iov = cipher_iovecs;
	cuio->uio_iovcnt = nr_cipher;

	return (0);

error:
	if (plain_iovecs != NULL)
		kmem_free(plain_iovecs, nr_plain * sizeof (iovec_t));
	if (cipher_iovecs != NULL)
		kmem_free(cipher_iovecs, nr_cipher * sizeof (iovec_t));

	*enc_len = 0;
	puio->uio_iov = NULL;
	puio->uio_iovcnt = 0;
	cuio->uio_iov = NULL;
	cuio->uio_iovcnt = 0;
	return (ret);
}

/*
 * This function builds up the plaintext (puio) and ciphertext (cuio) uios so
 * that they can be used for encryption and decryption by zio_do_crypt_uio().
 * Most blocks will use zio_crypt_init_uios_normal(), with ZIL and dnode blocks
 * requiring special handling to parse out pieces that are to be encrypted. The
 * authbuf is used by these special cases to store additional authenticated
 * data (AAD) for the encryption modes.
 */
static int
zio_crypt_init_uios(boolean_t encrypt, uint64_t version, dmu_object_type_t ot,
    uint8_t *plainbuf, uint8_t *cipherbuf, uint_t datalen, boolean_t byteswap,
    uint8_t *mac, zfs_uio_t *puio, zfs_uio_t *cuio, uint_t *enc_len,
    uint8_t **authbuf, uint_t *auth_len, boolean_t *no_crypt)
{
	int ret;
	iovec_t *mac_iov;

	ASSERT(DMU_OT_IS_ENCRYPTED(ot) || ot == DMU_OT_NONE);

	/* route to handler */
	switch (ot) {
	case DMU_OT_INTENT_LOG:
		ret = zio_crypt_init_uios_zil(encrypt, plainbuf, cipherbuf,
		    datalen, byteswap, puio, cuio, enc_len, authbuf, auth_len,
		    no_crypt);
		break;
	case DMU_OT_DNODE:
		ret = zio_crypt_init_uios_dnode(encrypt, version, plainbuf,
		    cipherbuf, datalen, byteswap, puio, cuio, enc_len, authbuf,
		    auth_len, no_crypt);
		break;
	default:
		ret = zio_crypt_init_uios_normal(encrypt, plainbuf, cipherbuf,
		    datalen, puio, cuio, enc_len);
		*authbuf = NULL;
		*auth_len = 0;
		*no_crypt = B_FALSE;
		break;
	}

	if (ret != 0)
		goto error;

	/* populate the uios */
	puio->uio_segflg = UIO_SYSSPACE;
	cuio->uio_segflg = UIO_SYSSPACE;

	mac_iov = ((iovec_t *)&cuio->uio_iov[cuio->uio_iovcnt - 1]);
	mac_iov->iov_base = mac;
	mac_iov->iov_len = ZIO_DATA_MAC_LEN;

	return (0);

error:
	return (ret);
}

/*
 * Primary encryption / decryption entrypoint for zio data.
 */
int
zio_do_crypt_data(boolean_t encrypt, zio_crypt_key_t *key,
    dmu_object_type_t ot, boolean_t byteswap, uint8_t *salt, uint8_t *iv,
    uint8_t *mac, uint_t datalen, uint8_t *plainbuf, uint8_t *cipherbuf,
    boolean_t *no_crypt)
{
	int ret;
	boolean_t locked = B_FALSE;
	uint64_t crypt = key->zk_crypt;
	uint_t keydata_len = zio_crypt_table[crypt].ci_keylen;
	uint_t enc_len, auth_len;
	zfs_uio_t puio, cuio;
	uint8_t enc_keydata[MASTER_KEY_MAX_LEN];
	crypto_key_t tmp_ckey, *ckey = NULL;
	crypto_ctx_template_t tmpl;
	uint8_t *authbuf = NULL;

	/*
	 * If the needed key is the current one, just use it. Otherwise we
	 * need to generate a temporary one from the given salt + master key.
	 * If we are encrypting, we must return a copy of the current salt
	 * so that it can be stored in the blkptr_t.
	 */
	rw_enter(&key->zk_salt_lock, RW_READER);
	locked = B_TRUE;

	if (bcmp(salt, key->zk_salt, ZIO_DATA_SALT_LEN) == 0) {
		ckey = &key->zk_current_key;
		tmpl = key->zk_current_tmpl;
	} else {
		rw_exit(&key->zk_salt_lock);
		locked = B_FALSE;

		ret = hkdf_sha512(key->zk_master_keydata, keydata_len, NULL, 0,
		    salt, ZIO_DATA_SALT_LEN, enc_keydata, keydata_len);
		if (ret != 0)
			goto error;

		tmp_ckey.ck_format = CRYPTO_KEY_RAW;
		tmp_ckey.ck_data = enc_keydata;
		tmp_ckey.ck_length = CRYPTO_BYTES2BITS(keydata_len);

		ckey = &tmp_ckey;
		tmpl = NULL;
	}

	/*
	 * Attempt to use QAT acceleration if we can. We currently don't
	 * do this for metadnode and ZIL blocks, since they have a much
	 * more involved buffer layout and the qat_crypt() function only
	 * works in-place.
	 */
	if (qat_crypt_use_accel(datalen) &&
	    ot != DMU_OT_INTENT_LOG && ot != DMU_OT_DNODE) {
		uint8_t *srcbuf, *dstbuf;

		if (encrypt) {
			srcbuf = plainbuf;
			dstbuf = cipherbuf;
		} else {
			srcbuf = cipherbuf;
			dstbuf = plainbuf;
		}

		ret = qat_crypt((encrypt) ? QAT_ENCRYPT : QAT_DECRYPT, srcbuf,
		    dstbuf, NULL, 0, iv, mac, ckey, key->zk_crypt, datalen);
		if (ret == CPA_STATUS_SUCCESS) {
			if (locked) {
				rw_exit(&key->zk_salt_lock);
				locked = B_FALSE;
			}

			return (0);
		}
		/* If the hardware implementation fails fall back to software */
	}

	bzero(&puio, sizeof (zfs_uio_t));
	bzero(&cuio, sizeof (zfs_uio_t));

	/* create uios for encryption */
	ret = zio_crypt_init_uios(encrypt, key->zk_version, ot, plainbuf,
	    cipherbuf, datalen, byteswap, mac, &puio, &cuio, &enc_len,
	    &authbuf, &auth_len, no_crypt);
	if (ret != 0)
		goto error;

	/* perform the encryption / decryption in software */
	ret = zio_do_crypt_uio(encrypt, key->zk_crypt, ckey, tmpl, iv, enc_len,
	    &puio, &cuio, authbuf, auth_len);
	if (ret != 0)
		goto error;

	if (locked) {
		rw_exit(&key->zk_salt_lock);
		locked = B_FALSE;
	}

	if (authbuf != NULL)
		zio_buf_free(authbuf, datalen);
	if (ckey == &tmp_ckey)
		bzero(enc_keydata, keydata_len);
	zio_crypt_destroy_uio(&puio);
	zio_crypt_destroy_uio(&cuio);

	return (0);

error:
	if (locked)
		rw_exit(&key->zk_salt_lock);
	if (authbuf != NULL)
		zio_buf_free(authbuf, datalen);
	if (ckey == &tmp_ckey)
		bzero(enc_keydata, keydata_len);
	zio_crypt_destroy_uio(&puio);
	zio_crypt_destroy_uio(&cuio);

	return (ret);
}

/*
 * Simple wrapper around zio_do_crypt_data() to work with abd's instead of
 * linear buffers.
 */
int
zio_do_crypt_abd(boolean_t encrypt, zio_crypt_key_t *key, dmu_object_type_t ot,
    boolean_t byteswap, uint8_t *salt, uint8_t *iv, uint8_t *mac,
    uint_t datalen, abd_t *pabd, abd_t *cabd, boolean_t *no_crypt)
{
	int ret;
	void *ptmp, *ctmp;

	if (encrypt) {
		ptmp = abd_borrow_buf_copy(pabd, datalen);
		ctmp = abd_borrow_buf(cabd, datalen);
	} else {
		ptmp = abd_borrow_buf(pabd, datalen);
		ctmp = abd_borrow_buf_copy(cabd, datalen);
	}

	ret = zio_do_crypt_data(encrypt, key, ot, byteswap, salt, iv, mac,
	    datalen, ptmp, ctmp, no_crypt);
	if (ret != 0)
		goto error;

	if (encrypt) {
		abd_return_buf(pabd, ptmp, datalen);
		abd_return_buf_copy(cabd, ctmp, datalen);
	} else {
		abd_return_buf_copy(pabd, ptmp, datalen);
		abd_return_buf(cabd, ctmp, datalen);
	}

	return (0);

error:
	if (encrypt) {
		abd_return_buf(pabd, ptmp, datalen);
		abd_return_buf_copy(cabd, ctmp, datalen);
	} else {
		abd_return_buf_copy(pabd, ptmp, datalen);
		abd_return_buf(cabd, ctmp, datalen);
	}

	return (ret);
}

#if defined(_KERNEL)
/* BEGIN CSTYLED */
module_param(zfs_key_max_salt_uses, ulong, 0644);
MODULE_PARM_DESC(zfs_key_max_salt_uses, "Max number of times a salt value "
	"can be used for generating encryption keys before it is rotated");
/* END CSTYLED */
#endif
