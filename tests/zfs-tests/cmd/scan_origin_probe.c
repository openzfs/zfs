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
 * Make a scrub suspend at the root of the $ORIGIN snapshot, which the scan
 * visits right after the MOS, and require it to find a damaged block in a
 * file system that descends from $ORIGIN.
 *
 * A scan may suspend at a level-0 or root block once it has run for the
 * minimum scrub time, 0 here, and at least a millisecond has passed.
 * Resuming, it skips every block before its bookmark without checking.
 * With the bookmark at the last level-0 slot of the MOS meta-dnode, the root
 * of $ORIGIN is the first place where the scan checks. Destroying a file
 * system asynchronously in the same TXG usually spends the millisecond first:
 * the scan frees destroyed data before it traverses, which takes several.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libzpool.h>
#include <sys/dbuf.h>
#include <sys/ddt.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dnode.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_synctask.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_context.h>

extern int zfs_scan_suspend_progress;

/*
 * Destroy the file system and resume the scan in one sync task, so that the
 * scan frees its blocks in the same TXG before it traverses.
 */
static void
scan_origin_probe_destroy_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_t *ds;
	VERIFY0(dsl_dataset_hold(dmu_tx_pool(tx), arg, FTAG, &ds));
	VERIFY0(dsl_destroy_head_check_impl(ds, 0));
	dsl_destroy_head_sync_impl(ds, tx);
	dsl_dataset_rele(ds, FTAG);
	zfs_scan_suspend_progress = 0;
}

static void
scan_origin_probe_bookmark_sync(void *arg, dmu_tx_t *tx)
{
	(void) arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_scan_t *scn = dp->dp_scan;
	dnode_t *mdn = DMU_META_DNODE(dp->dp_meta_objset);

	/*
	 * The last level-0 slot under the indirect block of the last dnode
	 * block, a hole, so that visiting it visits nothing else.
	 */
	int epbs = mdn->dn_indblkshift - SPA_BLKPTRSHIFT;
	uint64_t last = (((mdn->dn_maxblkid >> epbs) + 1) << epbs) - 1;
	VERIFY3U(mdn->dn_nlevels, >=, 2);
	VERIFY3U(last, >, mdn->dn_maxblkid);

	/* The dedup table walk precedes the MOS; it is done. */
	scn->scn_phys.scn_ddt_bookmark.ddb_class = DDT_CLASSES;
	SET_BOOKMARK(&scn->scn_phys.scn_bookmark, DMU_META_OBJSET,
	    DMU_META_DNODE_OBJECT, 0, last);
}

/*
 * Write a new object of random blocks to a file system and return the block
 * pointer of its first block.
 */
static blkptr_t
scan_origin_probe_write(const char *name, int blocks, char *buf, size_t size)
{
	objset_t *os;
	VERIFY0(dmu_objset_own(name, DMU_OST_ZFS, B_FALSE, B_TRUE, FTAG, &os));
	(void) random_get_pseudo_bytes((uint8_t *)buf, size);
	dmu_tx_t *tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, blocks * size);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	uint64_t obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, size,
	    DMU_OT_NONE, 0, tx);
	for (int b = 0; b < blocks; b++)
		dmu_write(os, obj, b * size, size, buf, tx, 0);
	dmu_tx_commit(tx);
	txg_wait_synced(dmu_objset_pool(os), 0);

	dmu_buf_t *db;
	VERIFY0(dmu_buf_hold(os, obj, 0, FTAG, &db, DMU_READ_NO_PREFETCH));
	blkptr_t bp = *((dmu_buf_impl_t *)db)->db_blkptr;
	dmu_buf_rele(db, FTAG);
	dmu_objset_disown(os, B_TRUE, FTAG);
	return (bp);
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		return (2);
	const char *dir = argv[1];
	char pool[] = "scan_origin_probe";
	char path[MAXPATHLEN], cachefile[MAXPATHLEN];
	const size_t size = SPA_OLD_MAXBLOCKSIZE;

	/* kernel_init() must not load or rewrite the machine's pool cache. */
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache", dir);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	(void) snprintf(path, sizeof (path), "%s/disk", dir);
	int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	VERIFY3S(fd, >=, 0);
	VERIFY0(ftruncate(fd, 256ULL << 20));
	nvlist_t *leaf = fnvlist_alloc();
	fnvlist_add_string(leaf, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(leaf, ZPOOL_CONFIG_PATH, path);
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&leaf, 1);
	/* The scan, not the destroy, frees an asynchronously destroyed fs. */
	nvlist_t *props = fnvlist_alloc();
	fnvlist_add_uint64(props, "feature@async_destroy", 0);
	VERIFY0(spa_create(pool, root, props, NULL, NULL, NULL));
	fnvlist_free(props);
	fnvlist_free(root);
	fnvlist_free(leaf);

	spa_t *spa;
	VERIFY0(spa_open(pool, &spa, FTAG));
	dsl_pool_t *dp = spa_get_dsl(spa);

	/*
	 * Grow the MOS until its meta-dnode has an indirect block, whose last
	 * level-0 slot is then a hole.
	 */
	for (int i = 0;
	    DMU_META_DNODE(dp->dp_meta_objset)->dn_nlevels < 2; i++) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		VERIFY3S(i, <, 256);
		(void) snprintf(name, sizeof (name), "%s/fs%d", pool, i);
		VERIFY0(dmu_objset_create(name, DMU_OST_ZFS, 0, NULL, NULL,
		    NULL));
		txg_wait_synced(dp, 0);
	}

	/*
	 * A block of the root file system, which descends from $ORIGIN, to
	 * damage, and a file system with blocks to destroy.
	 */
	char victim[ZFS_MAX_DATASET_NAME_LEN];
	(void) snprintf(victim, sizeof (victim), "%s/victim", pool);
	VERIFY0(dmu_objset_create(victim, DMU_OST_ZFS, 0, NULL, NULL, NULL));
	char *buf = umem_alloc(size, UMEM_NOFAIL);
	blkptr_t bp = scan_origin_probe_write(pool, 1, buf, size);
	(void) scan_origin_probe_write(victim, 64, buf, size);

	/* Damage the block and leave every cache cold. */
	spa_close(spa, FTAG);
	nvlist_t *config;
	VERIFY0(spa_export(pool, &config, B_FALSE, B_FALSE));
	(void) random_get_pseudo_bytes((uint8_t *)buf, size);
	VERIFY3S(pwrite(fd, buf, DVA_GET_ASIZE(&bp.blk_dva[0]),
	    DVA_GET_OFFSET(&bp.blk_dva[0]) + VDEV_LABEL_START_SIZE), ==,
	    DVA_GET_ASIZE(&bp.blk_dva[0]));
	VERIFY0(close(fd));
	umem_free(buf, size);
	VERIFY0(spa_import(pool, config, NULL, 0));
	fnvlist_free(config);
	VERIFY0(spa_open(pool, &spa, FTAG));
	dp = spa_get_dsl(spa);

	VERIFY0(handle_tunable_option("zfs_scan_legacy=1", B_TRUE));
	VERIFY0(handle_tunable_option("zfs_scrub_min_time_ms=0", B_TRUE));
	zfs_scan_suspend_progress = 1;
	for (int i = 0; i < 8; i++)
		txg_wait_synced(dp, 0);
	VERIFY0(spa_scan(spa, POOL_SCAN_SCRUB, 0));
	VERIFY0(dsl_sync_task(pool, NULL, scan_origin_probe_bookmark_sync,
	    NULL, 0, ZFS_SPACE_CHECK_NONE));
	VERIFY0(dsl_sync_task(pool, NULL, scan_origin_probe_destroy_sync,
	    victim, 0, ZFS_SPACE_CHECK_NONE));
	for (int i = 0; i < 1000 && dsl_scan_scrubbing(dp); i++)
		txg_wait_synced(dp, 0);

	(void) snprintf(path, sizeof (path), "%s/dbgmsg", dir);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	VERIFY3S(fd, >=, 0);
	zfs_dbgmsg_print(fd, "scan_origin_probe");
	VERIFY0(close(fd));

	VERIFY(!dsl_scan_scrubbing(dp));
	(void) printf("scrub found %llu errors\n",
	    (u_longlong_t)dp->dp_scan->scn_phys.scn_errors);
	VERIFY3U(dp->dp_scan->scn_phys.scn_errors, >, 0);

	spa_close(spa, FTAG);
	VERIFY0(spa_destroy(pool));
	kernel_fini();
	return (0);
}
