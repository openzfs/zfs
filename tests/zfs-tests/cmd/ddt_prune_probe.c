// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Prune a dedup table entry whose second reference is still in the log when
 * the prune walk reads it, and is flushed into the stored table before the
 * prune clears it. The entry must survive, and the exported pool is left for
 * zdb to check that no block pointer references freed space.
 *
 * The walk reads candidates from the stored unique class only, and syncs
 * them in batches. With one entry per batch, the sha256 entry comes first.
 * The walk reads the sha512 entry before syncing that first batch, which
 * flushes its log update. The sha512 entry's own batch follows. The caller
 * checks from the debug log that the prune took two batches.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libzpool.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_prop.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/ddt.h>
#include <sys/ddt_impl.h>
#include <sys/zfs_context.h>

#define	FILLER_SIZE	512
#define	ENTRY_SIZE	(128 << 10)

extern uint32_t zfs_ddt_prunes_per_txg;

static uint64_t
ddt_prune_probe_write(objset_t *os, uint64_t blksz, const char *buf)
{
	dmu_tx_t *tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	uint64_t obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, blksz,
	    DMU_OT_NONE, 0, tx);
	dmu_tx_commit(tx);

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, obj, 0, blksz);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	dmu_write(os, obj, 0, blksz, buf, tx, 0);
	dmu_tx_commit(tx);
	return (obj);
}

static uint64_t
ddt_prune_probe_logged(spa_t *spa, enum zio_checksum c)
{
	ddt_t *ddt = spa->spa_ddt[c];
	return (avl_numnodes(&ddt->ddt_log_active->ddl_tree) +
	    avl_numnodes(&ddt->ddt_log_flushing->ddl_tree));
}

static uint64_t
ddt_prune_probe_stored(spa_t *spa, ddt_class_t class)
{
	uint64_t count;
	/* The table object of an empty class is destroyed. */
	int error = ddt_object_count(spa->spa_ddt[ZIO_CHECKSUM_SHA512],
	    DDT_TYPE_ZAP, class, &count);
	if (error == ENOENT)
		return (0);
	VERIFY0(error);
	return (count);
}

static void
ddt_prune_probe_drain(spa_t *spa)
{
	for (int i = 0; i < 50; i++) {
		txg_wait_synced(spa_get_dsl(spa), 0);
		if (ddt_prune_probe_logged(spa, ZIO_CHECKSUM_SHA256) == 0 &&
		    ddt_prune_probe_logged(spa, ZIO_CHECKSUM_SHA512) == 0)
			return;
	}
	VERIFY(!"dedup logs did not drain");
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return (2);
	const char *dir = argv[1];
	char pool[] = "ddt_prune_probe";
	char path[MAXPATHLEN], cachefile[MAXPATHLEN];

	/* A missing cache makes kernel_init() fall back to the boot cache. */
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache", dir);
	nvlist_t *empty = fnvlist_alloc();
	size_t cachelen;
	char *cache = fnvlist_pack(empty, &cachelen);
	int fd = open(cachefile, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	VERIFY3S(fd, >=, 0);
	VERIFY3S(write(fd, cache, cachelen), ==, cachelen);
	VERIFY0(close(fd));
	fnvlist_pack_free(cache, cachelen);
	fnvlist_free(empty);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	zfs_ddt_prunes_per_txg = 1;
	/* Suppress periodic syncs while controlling the log transitions. */
	VERIFY0(handle_tunable_option("zfs_txg_timeout=3600", B_TRUE));
	VERIFY0(handle_tunable_option("zfs_dedup_log_txg_max=1", B_TRUE));
	VERIFY0(handle_tunable_option(
	    "zfs_dedup_log_flush_entries_min=1000000", B_TRUE));

	(void) snprintf(path, sizeof (path), "%s/disk0", dir);
	fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	VERIFY3S(fd, >=, 0);
	VERIFY0(ftruncate(fd, 512ULL << 20));
	VERIFY0(close(fd));
	nvlist_t *leaf = fnvlist_alloc();
	fnvlist_add_string(leaf, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(leaf, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(leaf, ZPOOL_CONFIG_ASHIFT, SPA_MINBLOCKSHIFT);
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&leaf, 1);
	nvlist_t *props = fnvlist_alloc();
	fnvlist_add_uint64(props, "feature@fast_dedup", 0);
	fnvlist_add_uint64(props, "feature@sha512", 0);
	VERIFY0(spa_create(pool, root, props, NULL, NULL, NULL));
	fnvlist_free(props);
	fnvlist_free(root);
	fnvlist_free(leaf);

	spa_t *spa;
	VERIFY0(spa_open(pool, &spa, FTAG));
	dsl_pool_t *dp = spa_get_dsl(spa);
	objset_t *os;
	VERIFY0(dmu_objset_own(pool, DMU_OST_ZFS, B_FALSE, B_TRUE, FTAG, &os));
	VERIFY0(dsl_prop_set_int(pool, "compression", ZPROP_SRC_LOCAL,
	    ZIO_COMPRESS_OFF));

	/* One sha256 entry fills the first prune batch. */
	char fill[FILLER_SIZE];
	(void) memset(fill, 0x41, sizeof (fill));
	VERIFY0(dsl_prop_set_int(pool, "dedup", ZPROP_SRC_LOCAL,
	    ZIO_CHECKSUM_SHA256));
	(void) ddt_prune_probe_write(os, FILLER_SIZE, fill);

	/* The entry, referenced once, stored in the unique class. */
	char *buf = umem_alloc(ENTRY_SIZE, UMEM_NOFAIL);
	(void) memset(buf, 0x5a, ENTRY_SIZE);
	VERIFY0(dsl_prop_set_int(pool, "dedup", ZPROP_SRC_LOCAL,
	    ZIO_CHECKSUM_SHA512));
	(void) ddt_prune_probe_write(os, ENTRY_SIZE, buf);
	ddt_prune_probe_drain(spa);
	VERIFY3U(ddt_prune_probe_stored(spa, DDT_CLASS_UNIQUE), ==, 1);

	/* A second reference, moved to the flushing log but not flushed. */
	VERIFY0(handle_tunable_option("zfs_dedup_log_txg_max=1000000", B_TRUE));
	uint64_t second = ddt_prune_probe_write(os, ENTRY_SIZE, buf);
	umem_free(buf, ENTRY_SIZE);
	txg_wait_synced(dp, 0);
	VERIFY0(handle_tunable_option("zfs_dedup_log_txg_max=1", B_TRUE));
	txg_wait_synced(dp, spa_last_synced_txg(spa) + 1);
	ddt_t *ddt = spa->spa_ddt[ZIO_CHECKSUM_SHA512];
	VERIFY3U(avl_numnodes(&ddt->ddt_log_flushing->ddl_tree), ==, 1);

	/*
	 * Select all stored entries without depending on wall-clock aging.
	 * Preserve the prune entry point's recursive I/O lock handling.
	 */
	VERIFY(!spa->spa_active_ddt_prune);
	spa->spa_active_ddt_prune = B_TRUE;
	ddt_prune_walk(spa, UINT64_MAX, NULL);
	spa->spa_active_ddt_prune = B_FALSE;
	ddt_prune_probe_drain(spa);
	uint64_t unique = ddt_prune_probe_stored(spa, DDT_CLASS_UNIQUE);
	uint64_t duplicate = ddt_prune_probe_stored(spa, DDT_CLASS_DUPLICATE);
	(void) printf("sha512 entries: unique %llu, duplicate %llu\n",
	    (u_longlong_t)unique, (u_longlong_t)duplicate);
	(void) fflush(stdout);

	/* Freeing the second reference leaves the first one in use. */
	VERIFY0(dmu_free_long_object(os, second));
	ddt_prune_probe_drain(spa);

	(void) snprintf(path, sizeof (path), "%s/dbgmsg", dir);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	VERIFY3S(fd, >=, 0);
	zfs_dbgmsg_print(fd, "ddt_prune_probe");
	VERIFY0(close(fd));

	dmu_objset_disown(os, B_TRUE, FTAG);
	spa_close(spa, FTAG);
	VERIFY0(spa_export(pool, NULL, B_FALSE, B_FALSE));
	kernel_fini();

	VERIFY0(unique);
	VERIFY3U(duplicate, ==, 1);
	return (0);
}
