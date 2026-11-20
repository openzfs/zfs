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
 * Tests of the cache of keys derived for salts other than a key's current
 * one, through zio_do_crypt_data() and the ICP backend: what it admits and
 * evicts, as counted by its kstat, and that every block still decrypts
 * correctly, from one thread or several.
 */

#include <sys/zfs_context.h>
#include <sys/zio_crypt.h>
#include <sys/crypto/icp.h>
#include <sys/abd.h>
#include <libspl.h>
#include <pthread.h>

#include "unit.h"

/*
 * zio_crypt.c also handles dnode and ZIL blocks, and abds, which need these.
 * The tests only use data blocks in linear buffers.
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

/*
 * libspl creates no kstats. These take the place of its functions, so that
 * the tests can read the counters that zio_crypt_init() registers.
 */
static kstat_t crypt_ksp;

kstat_t *
kstat_create(const char *module, int instance, const char *name,
    const char *class, uchar_t type, ulong_t ndata, uchar_t ks_flag)
{
	(void) module, (void) instance, (void) class, (void) type,
	    (void) ks_flag;
	if (strcmp(name, "zio_crypt_stats") != 0)
		return (NULL);
	memset(&crypt_ksp, 0, sizeof (crypt_ksp));
	crypt_ksp.ks_ndata = ndata;
	return (&crypt_ksp);
}

void
kstat_install(kstat_t *ksp)
{
	(void) ksp;
}

void
kstat_delete(kstat_t *ksp)
{
	(void) ksp;
}

/* ========== */

#define	BLKSZ		4096
#define	NSALTS		24
#define	NTHREADS	8
#define	NREADS		2000

typedef struct {
	uint64_t	hits;
	uint64_t	misses;
	uint64_t	admissions;
	uint64_t	evictions;
} counts_t;

static uint64_t
stat_value(const char *name)
{
	kstat_named_t *kn = crypt_ksp.ks_data;
	uint_t n = crypt_ksp.ks_ndata;

	for (uint_t i = 0; i < n; i++) {
		if (strcmp(kn[i].name, name) == 0)
			return (kn[i].value.ui64);
	}
	munit_errorf("no %s in zio_crypt_stats", name);
	return (0);
}

static void
counts_get(counts_t *c)
{
	unit_notnull(crypt_ksp.ks_update);
	unit_ok(crypt_ksp.ks_update(&crypt_ksp, KSTAT_READ));
	c->hits = stat_value("salt_cache_hits");
	c->misses = stat_value("salt_cache_misses");
	c->admissions = stat_value("salt_cache_admissions");
	c->evictions = stat_value("salt_cache_evictions");
}

/* Check the counters against those saved in *base, and save them there. */
static void
counts_check(counts_t *base, uint64_t hits, uint64_t misses,
    uint64_t admissions, uint64_t evictions)
{
	counts_t c;

	counts_get(&c);
	unit_eq(c.hits - base->hits, hits);
	unit_eq(c.misses - base->misses, misses);
	unit_eq(c.admissions - base->admissions, admissions);
	unit_eq(c.evictions - base->evictions, evictions);
	*base = c;
}

/* A block encrypted under a salt other than the key's current one. */
typedef struct {
	uint8_t		b_salt[ZIO_DATA_SALT_LEN];
	uint8_t		b_iv[ZIO_DATA_IV_LEN];
	uint8_t		b_mac[ZIO_DATA_MAC_LEN];
	uint8_t		b_plain[BLKSZ];
	uint8_t		b_cipher[BLKSZ];
} block_t;

static block_t blocks[NSALTS];

static void
fill_pattern(uint8_t *buf, size_t len, uint64_t seed)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (i * 7 + seed * 131) % 251;
}

static int
block_crypt(zio_crypt_key_t *key, boolean_t encrypt, block_t *b,
    uint8_t *plain, uint8_t *mac)
{
	boolean_t no_crypt = B_FALSE;

	return (zio_do_crypt_data(encrypt, key, DMU_OTN_UINT8_ENC_DATA, B_FALSE,
	    b->b_salt, b->b_iv, mac, BLKSZ, plain, b->b_cipher, &no_crypt));
}

/* Encrypt block i under a salt of its own, which is not the current one. */
static void
block_make(zio_crypt_key_t *key, int i)
{
	block_t *b = &blocks[i];

	do {
		unit_ok(random_get_pseudo_bytes(b->b_salt, ZIO_DATA_SALT_LEN));
	} while (memcmp(b->b_salt, key->zk_salt, ZIO_DATA_SALT_LEN) == 0);
	unit_ok(zio_crypt_generate_iv(b->b_iv));
	fill_pattern(b->b_plain, BLKSZ, i);
	unit_ok(block_crypt(key, B_TRUE, b, b->b_plain, b->b_mac));
}

/* Decrypt block i. Returns B_TRUE if it reads back as written. */
static boolean_t
block_read(zio_crypt_key_t *key, int i)
{
	block_t *b = &blocks[i];
	uint8_t plain[BLKSZ];
	uint8_t mac[ZIO_DATA_MAC_LEN];

	memcpy(mac, b->b_mac, sizeof (mac));
	memset(plain, 0, sizeof (plain));
	if (block_crypt(key, B_FALSE, b, plain, mac) != 0)
		return (B_FALSE);
	return (memcmp(plain, b->b_plain, BLKSZ) == 0);
}

static void
block_check(zio_crypt_key_t *key, int i)
{
	unit_true(block_read(key, i));
}

static const uint8_t wrapping_keydata[WRAPPING_KEY_LEN] = { 0x5a };

/* Unload and load key: a copy of it, with a new current salt. */
static void
key_reload(zio_crypt_key_t *key, zio_crypt_key_t *newkey)
{
	crypto_key_t cwkey = {
		.ck_data = (void *)wrapping_keydata,
		.ck_length = CRYPTO_BYTES2BITS(WRAPPING_KEY_LEN),
	};
	uint8_t keydata[MASTER_KEY_MAX_LEN];
	uint8_t hmac_keydata[SHA512_HMAC_KEYLEN];
	uint8_t iv[WRAPPING_IV_LEN];
	uint8_t mac[WRAPPING_MAC_LEN];

	unit_ok(zio_crypt_key_wrap(&cwkey, key, iv, mac, keydata,
	    hmac_keydata));
	unit_ok(zio_crypt_key_unwrap(&cwkey, key->zk_crypt, key->zk_version,
	    key->zk_guid, keydata, hmac_keydata, iv, mac, newkey));
}

/* ========== */

/*
 * A salt is cached on its second decryption miss. Encryption only looks up,
 * and the key it uses once the salt is cached gives the same ciphertext.
 */
static MunitResult
test_admission(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_crypt_key_t key;
	counts_t base;
	block_t *b = &blocks[0];
	uint8_t cipher[BLKSZ], mac[ZIO_DATA_MAC_LEN];

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_256_GCM, &key));
	counts_get(&base);

	block_make(&key, 0);
	block_make(&key, 1);
	block_check(&key, 1);
	counts_check(&base, 0, 3, 0, 0);

	/* encryption admits nothing */
	for (int i = 0; i < 3; i++)
		unit_ok(block_crypt(&key, B_TRUE, b, b->b_plain, mac));
	counts_check(&base, 0, 3, 0, 0);

	block_check(&key, 0);
	counts_check(&base, 0, 1, 0, 0);
	block_check(&key, 0);
	counts_check(&base, 0, 1, 1, 0);
	block_check(&key, 0);
	block_check(&key, 0);
	counts_check(&base, 2, 0, 0, 0);

	/* the cached key encrypts as the derived one did */
	memcpy(cipher, b->b_cipher, sizeof (cipher));
	unit_ok(block_crypt(&key, B_TRUE, b, b->b_plain, mac));
	counts_check(&base, 1, 0, 0, 0);
	munit_assert_memory_equal(BLKSZ, b->b_cipher, cipher);
	munit_assert_memory_equal(ZIO_DATA_MAC_LEN, mac, b->b_mac);

	/* the other salt was only missed once */
	block_check(&key, 1);
	counts_check(&base, 0, 1, 1, 0);

	zio_crypt_key_destroy(&key);
	return (MUNIT_OK);
}

/*
 * Salts that are only read once, as those of dedup blocks, are never
 * cached.
 */
static MunitResult
test_single_reads(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_crypt_key_t key;
	counts_t base;

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_128_CCM, &key));
	counts_get(&base);

	for (int round = 0; round < 3; round++) {
		for (int i = 0; i < NSALTS; i++) {
			block_make(&key, i);
			block_check(&key, i);
		}
	}
	counts_check(&base, 0, 6 * NSALTS, 0, 0);

	zio_crypt_key_destroy(&key);
	return (MUNIT_OK);
}

/*
 * More salts than the cache holds, read again and again: entries are
 * evicted, and every block still reads back.
 */
static MunitResult
test_eviction(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_crypt_key_t key;
	counts_t base, c;

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_256_GCM, &key));
	for (int i = 0; i < NSALTS; i++)
		block_make(&key, i);
	counts_get(&base);

	/* the first pass fills the ghost list, the second admits them all */
	for (int i = 0; i < NSALTS; i++)
		block_check(&key, i);
	counts_check(&base, 0, NSALTS, 0, 0);
	for (int i = 0; i < NSALTS; i++)
		block_check(&key, i);
	counts_check(&base, 0, NSALTS, NSALTS, NSALTS - 16);

	for (int pass = 0; pass < 4; pass++) {
		for (int i = 0; i < NSALTS; i++)
			block_check(&key, i);
	}
	counts_get(&c);
	unit_eq(c.hits + c.misses - base.hits - base.misses, 4 * NSALTS);
	unit_gt(c.hits - base.hits, 0);

	zio_crypt_key_destroy(&key);
	return (MUNIT_OK);
}

/*
 * After the key is unloaded and loaded again, the blocks written under its
 * last current salt are cached like any other.
 */
static MunitResult
test_reload(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_crypt_key_t key, key2;
	counts_t base;
	block_t *b = &blocks[0];

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_256_GCM, &key));
	unit_ok(zio_crypt_key_get_salt(&key, b->b_salt));
	unit_ok(zio_crypt_generate_iv(b->b_iv));
	fill_pattern(b->b_plain, BLKSZ, 0);
	counts_get(&base);
	unit_ok(block_crypt(&key, B_TRUE, b, b->b_plain, b->b_mac));
	block_check(&key, 0);
	counts_check(&base, 0, 0, 0, 0);

	key_reload(&key, &key2);
	zio_crypt_key_destroy(&key);
	munit_assert_memory_not_equal(ZIO_DATA_SALT_LEN, key2.zk_salt,
	    b->b_salt);

	for (int i = 0; i < 4; i++)
		block_check(&key2, 0);
	counts_check(&base, 2, 2, 1, 0);

	zio_crypt_key_destroy(&key2);
	return (MUNIT_OK);
}

typedef struct {
	zio_crypt_key_t	*ta_key;
	uint64_t	ta_seed;
	uint64_t	ta_bad;
} thread_arg_t;

/* The munit assertions are only used from the main thread. */
static void *
reader(void *arg)
{
	thread_arg_t *ta = arg;
	uint64_t x = ta->ta_seed;

	for (int n = 0; n < NREADS; n++) {
		/* xorshift64: a fixed, cheap sequence per thread */
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		if (!block_read(ta->ta_key, x % NSALTS))
			ta->ta_bad++;
	}
	return (NULL);
}

/*
 * Several threads read more salts than the cache holds, in random order,
 * so that entries are evicted while others use them. Every block reads back,
 * and destroying the key finds no entry still held.
 */
static MunitResult
test_threads(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zio_crypt_key_t key;
	pthread_t tids[NTHREADS];
	thread_arg_t args[NTHREADS];
	counts_t base, c;

	unit_ok(zio_crypt_key_init(ZIO_CRYPT_AES_256_GCM, &key));
	for (int i = 0; i < NSALTS; i++)
		block_make(&key, i);
	counts_get(&base);

	for (int t = 0; t < NTHREADS; t++) {
		args[t].ta_key = &key;
		args[t].ta_seed = 0x9e3779b97f4a7c15ULL * (t + 1);
		args[t].ta_bad = 0;
		unit_ok(pthread_create(&tids[t], NULL, reader, &args[t]));
	}
	for (int t = 0; t < NTHREADS; t++) {
		unit_ok(pthread_join(tids[t], NULL));
		unit_eq(args[t].ta_bad, 0);
	}

	counts_get(&c);
	unit_eq(c.hits + c.misses - base.hits - base.misses,
	    NTHREADS * NREADS);
	unit_gt(c.hits - base.hits, 0);
	unit_gt(c.evictions - base.evictions, 0);
	unit_ge(c.admissions - base.admissions, 16);

	zio_crypt_key_destroy(&key);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest zio_crypt_salt_cache_tests[] = {
	UNIT_TEST("admission",		test_admission),
	UNIT_TEST("single_reads",	test_single_reads),
	UNIT_TEST("eviction",		test_eviction),
	UNIT_TEST("reload",		test_reload),
	UNIT_TEST("threads",		test_threads),
	{ 0 },
};

static const MunitSuite zio_crypt_salt_cache_test_suite = {
	"zio_crypt_salt_cache.",
	zio_crypt_salt_cache_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	libspl_init();
	VERIFY0(icp_init());
	zio_crypt_init();

	int ret = munit_suite_main(&zio_crypt_salt_cache_test_suite, NULL,
	    argc, argv);

	zio_crypt_fini();
	icp_fini();
	return (ret);
}
