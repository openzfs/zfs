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
 * Copyright (c) 2026, Tigerblue77.
 */

/*
 * Known-answer tests for the derivation of the salt and IV of encrypted dedup
 * blocks, through the ICP backend. The expected values were computed with
 * Python's hmac and hashlib modules, for an HMAC key of bytes 0 to 63 and data
 * whose byte i is i % 251.
 */

#include <sys/zfs_context.h>
#include <sys/zio_crypt.h>
#include <sys/crypto/icp.h>
#include <sys/abd.h>
#include <sys/zfs_impl.h>
#include <libspl.h>

#include "unit.h"

/*
 * zio_crypt.c also holds the block encryption code, which needs these. The
 * tests do not reach it.
 */
const dmu_object_type_info_t dmu_ot[DMU_OT_NUMTYPES];

void
byteswap_uint64_array(void *buf, size_t size)
{
	(void) buf, (void) size;
	abort();
}

void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	(void) abd, (void) n;
	abort();
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t n)
{
	(void) abd, (void) n;
	abort();
}

void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	(void) abd, (void) buf, (void) n;
	abort();
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	(void) abd, (void) buf, (void) n;
	abort();
}

void *
zio_buf_alloc(size_t size)
{
	(void) size;
	abort();
}

void
zio_buf_free(void *buf, size_t size)
{
	(void) buf, (void) size;
	abort();
}

/* ========== */

static const uint8_t wrapping_keydata[WRAPPING_KEY_LEN] = { 0xa5 };

static uint8_t databuf[128 * 1024];

static void
crypt_init(void)
{
	libspl_init();
	unit_ok(icp_init());
}

static void
fill_pattern(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = i % 251;
}

/*
 * Build a loaded key whose HMAC key is bytes 0 to 63, the way the DSL does:
 * create a key, wrap it, and unwrap it.
 */
static void
make_key(zio_crypt_key_t *key)
{
	zio_crypt_key_t newkey;
	crypto_key_t cwkey = {
		.ck_data = (void *)wrapping_keydata,
		.ck_length = CRYPTO_BYTES2BITS(WRAPPING_KEY_LEN),
	};
	uint8_t keydata[MASTER_KEY_MAX_LEN];
	uint8_t hmac_keydata[SHA512_HMAC_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_256_GCM, &newkey));
	for (int i = 0; i < SHA512_HMAC_KEYLEN; i++)
		newkey.zk_hmac_keydata[i] = i;
	unit_ok(zio_crypt_key_wrap(&cwkey, &newkey, iv, mac, keydata,
	    hmac_keydata));

	unit_ok(zio_crypt_key_unwrap(&cwkey, ZIO_CRYPT_AES_256_GCM,
	    ZIO_CRYPT_KEY_CURRENT_VERSION, newkey.zk_guid, keydata,
	    hmac_keydata, iv, mac, key));

	zio_crypt_key_destroy(&newkey);
}

/* ========== */

/*
 * A key fresh from zio_crypt_key_init() computes the same MACs as the same
 * key unwrapped from disk.
 */
static MunitResult
test_key_init_hmac(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_crypt_key_t newkey, key;
	crypto_key_t cwkey = {
		.ck_data = (void *)wrapping_keydata,
		.ck_length = CRYPTO_BYTES2BITS(WRAPPING_KEY_LEN),
	};
	uint8_t keydata[MASTER_KEY_MAX_LEN];
	uint8_t hmac_keydata[SHA512_HMAC_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];
	uint8_t new_mac[SHA512_HMAC_LEN], mac_out[SHA512_HMAC_LEN];

	crypt_init();
	fill_pattern(databuf, 512);

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_256_GCM, &newkey));
	unit_ok(zio_crypt_key_wrap(&cwkey, &newkey, iv, mac, keydata,
	    hmac_keydata));
	unit_ok(zio_crypt_key_unwrap(&cwkey, ZIO_CRYPT_AES_256_GCM,
	    ZIO_CRYPT_KEY_CURRENT_VERSION, newkey.zk_guid, keydata,
	    hmac_keydata, iv, mac, &key));

	unit_ok(zio_crypt_do_hmac(&newkey, databuf, 512, new_mac,
	    sizeof (new_mac)));
	unit_ok(zio_crypt_do_hmac(&key, databuf, 512, mac_out,
	    sizeof (mac_out)));
	munit_assert_memory_equal(sizeof (mac_out), new_mac, mac_out);

	zio_crypt_key_destroy(&newkey);
	zio_crypt_key_destroy(&key);
	return (MUNIT_OK);
}

typedef struct {
	uint_t		dv_len;
	uint8_t		dv_hmac[ZIO_DATA_SALT_LEN + ZIO_DATA_IV_LEN];
} dedup_vector_t;

/* salt || IV: the first 20 bytes of the HMAC of the data */
static const dedup_vector_t dedup_vectors[] = {
	{ 512,
	    { 0xd6, 0xe3, 0xa1, 0x23, 0x65, 0xa8, 0xb1, 0x48,
	    0x6c, 0x55, 0x7a, 0xd2, 0x5a, 0xb4, 0x4c, 0xb8,
	    0x24, 0xbb, 0x21, 0x21 } },
	{ 4096,
	    { 0x9d, 0xc6, 0xf2, 0x2f, 0xfd, 0xf8, 0x79, 0x10,
	    0x9b, 0xd1, 0xbf, 0x18, 0xc3, 0x3c, 0x8d, 0xfe,
	    0xbe, 0x4a, 0xaf, 0xb5 } },
	{ 131072,
	    { 0x9a, 0x10, 0xce, 0x97, 0x41, 0x3b, 0xd0, 0xfe,
	    0x9b, 0x49, 0xd3, 0xb4, 0x5d, 0x1e, 0x09, 0x8e,
	    0x0f, 0x0b, 0xc5, 0x2d } },
};

/* Check the vectors with every implementation of SHA-512. */
static MunitResult
test_dedup_iv_hmac(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	const zfs_impl_t *ops = zfs_impl_get_ops("sha512");
	zio_crypt_key_t key;
	uint8_t salt[ZIO_DATA_SALT_LEN], iv[ZIO_DATA_IV_LEN];
	uint32_t saved;

	crypt_init();
	unit_notnull(ops);
	saved = ops->getid();

	for (uint32_t id = 0; id < ops->getcnt(); id++) {
		ops->setid(id);
		munit_logf(MUNIT_LOG_DEBUG, "%s", ops->getname());
		make_key(&key);

		for (int i = 0; i < ARRAY_SIZE(dedup_vectors); i++) {
			const dedup_vector_t *dv = &dedup_vectors[i];
			const uint8_t *want = dv->dv_hmac;

			ASSERT3U(dv->dv_len, <=, sizeof (databuf));
			fill_pattern(databuf, dv->dv_len);
			unit_ok(zio_crypt_generate_iv_salt_dedup(&key,
			    databuf, dv->dv_len, iv, salt));
			munit_assert_memory_equal(ZIO_DATA_SALT_LEN, salt,
			    want);
			munit_assert_memory_equal(ZIO_DATA_IV_LEN, iv,
			    want + ZIO_DATA_SALT_LEN);
		}

		zio_crypt_key_destroy(&key);
	}

	ops->setid(saved);
	return (MUNIT_OK);
}

static const MunitTest zio_crypt_tests[] = {
	UNIT_TEST("key_init_hmac",	test_key_init_hmac),
	UNIT_TEST("dedup_iv_hmac",	test_dedup_iv_hmac),
	{ 0 },
};

static const MunitSuite zio_crypt_test_suite = {
	"zio_crypt.",
	zio_crypt_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&zio_crypt_test_suite, NULL, argc, argv));
}
