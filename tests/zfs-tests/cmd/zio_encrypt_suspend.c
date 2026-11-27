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
 * Copyright (c) 2026, Juanmi Taboada <juanmi@juanmitaboada.com>
 */

/*
 * A write that zio_encrypt() cannot encrypt must fail and suspend the pool,
 * not panic, and every zio_resume() must return.
 *
 * AES-CCM, with the 96-bit nonces ZFS uses, encrypts less than 16 MiB in one
 * operation, so a 16 MiB block that does not compress cannot be written on
 * an aes-256-ccm dataset.  The block is created through the DMU, so this
 * does not depend on the recordsize property.  That write can never
 * succeed, so the pool stays suspended; it lives in this process only.
 *
 * With -c ccm: the sync suspends the pool, then zio_resume() is called
 * twice; each call must return within the time limit and the pool must
 * suspend again.  With -c gcm: the same write must sync.
 *
 * Exit status: 0 pass, 1 fail, 2 usage.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_crypt.h>
#include <sys/dsl_pool.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/atomic.h>
#include <zfs_prop.h>
#include <zfeature_common.h>
#include <libzpool.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	POOL		"zio_encrypt_suspend"
#define	DATASET		POOL "/ds"
#define	BLOCKSIZE	(16ULL << 20)
#define	VDEV_SIZE	(512ULL << 20)
#define	ROUNDS		2
#define	TIME_LIMIT	30	/* seconds for each resume and each suspend */

static const char *wkey = "abcdefghijklmnopqrstuvwxyz012345";

/* Set by the resume thread, polled by main: accessed atomically. */
static volatile uint32_t resume_done;
/* Read only after pthread_join(). */
static int resume_rc;

static void
usage(void)
{
	(void) fprintf(stderr, "usage: zio_encrypt_suspend -d dir "
	    "-c ccm|gcm\n");
	exit(2);
}

static void
fail(const char *msg)
{
	(void) printf("FAIL: %s\n", msg);
	(void) fflush(stdout);
	/* The pool may be suspended for good: do not try to unload it. */
	_exit(1);
}

static void
create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	(void) os, (void) arg, (void) cr, (void) tx;
}

static void *
resume_thread(void *arg)
{
	resume_rc = zio_resume((spa_t *)arg);
	atomic_store_32_seq_cst(&resume_done, 1);
	return (NULL);
}

static boolean_t
wait_suspended(spa_t *spa)
{
	for (int i = 0; i < TIME_LIMIT * 10; i++) {
		if (spa_suspended(spa))
			return (B_TRUE);
		(void) usleep(100000);
	}
	return (B_FALSE);
}

static void
create_pool(const char *dir)
{
	char path[MAXPATHLEN];
	int fd;

	(void) snprintf(path, sizeof (path), "%s/vdev", dir);
	if ((fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0 ||
	    ftruncate(fd, VDEV_SIZE) != 0)
		fail("cannot create the vdev file");
	(void) close(fd);

	nvlist_t *leaf = fnvlist_alloc();
	fnvlist_add_string(leaf, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(leaf, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(leaf, ZPOOL_CONFIG_IS_LOG, 0);
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&leaf, 1);

	nvlist_t *props = fnvlist_alloc();
	for (int i = 0; i < SPA_FEATURES; i++) {
		char name[MAXNAMELEN];

		if (!spa_feature_table[i].fi_zfs_mod_supported)
			continue;
		(void) snprintf(name, sizeof (name), "feature@%s",
		    spa_feature_table[i].fi_uname);
		fnvlist_add_uint64(props, name, 0);
	}

	if (spa_create(POOL, root, props, NULL, NULL, NULL) != 0)
		fail("spa_create");
	fnvlist_free(props);
	fnvlist_free(root);
	fnvlist_free(leaf);
}

static void
create_dataset(uint64_t crypt)
{
	nvlist_t *props = fnvlist_alloc();
	nvlist_t *args = fnvlist_alloc();
	dsl_crypto_params_t *dcp;

	fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_ENCRYPTION), crypt);
	fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_KEYFORMAT),
	    ZFS_KEYFORMAT_RAW);
	fnvlist_add_string(props, zfs_prop_to_name(ZFS_PROP_KEYLOCATION),
	    "prompt");
	fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), 0);
	fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), 0);
	fnvlist_add_uint8_array(args, "wkeydata", (uint8_t *)wkey,
	    WRAPPING_KEY_LEN);

	if (dsl_crypto_params_create_nvlist(DCP_CMD_NONE, props, args,
	    &dcp) != 0)
		fail("dsl_crypto_params_create_nvlist");
	if (dmu_objset_create(DATASET, DMU_OST_OTHER, 0, dcp, create_cb,
	    NULL) != 0)
		fail("dmu_objset_create");
	dsl_crypto_params_free(dcp, B_FALSE);
	fnvlist_free(args);
	fnvlist_free(props);
}

int
main(int argc, char **argv)
{
	const char *dir = NULL;
	const char *cipher = NULL;
	int c;

	while ((c = getopt(argc, argv, "d:c:")) != -1) {
		switch (c) {
		case 'd':
			dir = optarg;
			break;
		case 'c':
			cipher = optarg;
			break;
		default:
			usage();
		}
	}
	if (dir == NULL || cipher == NULL ||
	    (strcmp(cipher, "ccm") != 0 && strcmp(cipher, "gcm") != 0))
		usage();
	boolean_t ccm = (strcmp(cipher, "ccm") == 0);

	/* Keep the pool out of the system's cache file. */
	char *cachefile = kmem_asprintf("%s/zpool.cache", dir);
	spa_config_path = cachefile;

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	create_pool(dir);
	create_dataset(ccm ? ZIO_CRYPT_AES_256_CCM : ZIO_CRYPT_AES_256_GCM);

	objset_t *os;
	if (dmu_objset_own(DATASET, DMU_OST_OTHER, B_FALSE, B_TRUE, FTAG,
	    &os) != 0)
		fail("dmu_objset_own");
	spa_t *spa = dmu_objset_spa(os);

	/* Random data, so that the block does not compress. */
	char *buf = kmem_alloc(BLOCKSIZE, KM_SLEEP);
	random_get_pseudo_bytes((uint8_t *)buf, BLOCKSIZE);

	dmu_tx_t *tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, BLOCKSIZE);
	if (dmu_tx_assign(tx, DMU_TX_WAIT) != 0)
		fail("dmu_tx_assign");
	uint64_t obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, BLOCKSIZE,
	    DMU_OT_NONE, 0, tx);
	dmu_write(os, obj, 0, BLOCKSIZE, buf, tx, DMU_READ_PREFETCH);
	uint64_t txg = dmu_tx_get_txg(tx);
	dmu_tx_commit(tx);

	(void) txg_wait_synced_flags(dmu_objset_pool(os), txg,
	    TXG_WAIT_SUSPEND);

	if (!ccm) {
		if (spa_suspended(spa))
			fail("gcm: the pool suspended");
		dmu_objset_disown(os, B_TRUE, FTAG);
		kmem_free(buf, BLOCKSIZE);
		if (spa_export(POOL, NULL, B_FALSE, B_FALSE) != 0)
			fail("gcm: spa_export");
		kernel_fini();
		kmem_strfree(cachefile);
		(void) printf("PASS: gcm: the 16 MiB block synced\n");
		return (0);
	}

	if (!spa_suspended(spa))
		fail("ccm: the write synced, or the pool did not suspend");
	(void) printf("ccm: the pool suspended\n");

	for (int round = 1; round <= ROUNDS; round++) {
		pthread_t t;

		atomic_store_32_seq_cst(&resume_done, 0);
		if (pthread_create(&t, NULL, resume_thread, spa) != 0)
			fail("pthread_create");
		for (int i = 0; i < TIME_LIMIT * 10 &&
		    !atomic_load_32_seq_cst(&resume_done); i++)
			(void) usleep(100000);
		if (!atomic_load_32_seq_cst(&resume_done)) {
			(void) printf("ccm: zio_resume() round %d did not "
			    "return within %d s\n", round, TIME_LIMIT);
			fail("ccm: zio_resume() hung");
		}
		(void) pthread_join(t, NULL);
		(void) printf("ccm: zio_resume() round %d returned %d\n",
		    round, resume_rc);
		if (!wait_suspended(spa))
			fail("ccm: the pool did not suspend again");
		(void) printf("ccm: the pool suspended again\n");
	}

	(void) printf("PASS: ccm: the write failed without a panic and "
	    "every zio_resume() returned\n");
	(void) fflush(stdout);
	/* The pool stays suspended: it cannot be exported. */
	_exit(0);
}
