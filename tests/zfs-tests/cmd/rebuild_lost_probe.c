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

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libzpool.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/fs/zfs.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_rebuild.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/zio.h>

extern int zfs_scan_suspend_progress;

#define	PROBE_BLKSZ	(128ULL << 10)
#define	PROBE_DATA	(16ULL << 20)

static nvlist_t *
file_vdev(const char *dir, int index)
{
	char path[MAXPATHLEN];
	(void) snprintf(path, sizeof (path), "%s/disk-%d", dir, index);
	int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	VERIFY3S(fd, >=, 0);
	VERIFY0(ftruncate(fd, 512ULL << 20));
	VERIFY0(close(fd));
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(nvl, ZPOOL_CONFIG_PATH, path);
	return (nvl);
}

static nvlist_t *
root_vdev(nvlist_t **children, int count)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)children, count);
	for (int i = 0; i < count; i++)
		fnvlist_free(children[i]);
	return (nvl);
}

static void
attach(spa_t *spa, vdev_t *vd, const char *dir, int index)
{
	nvlist_t *child = file_vdev(dir, index);
	nvlist_t *root = root_vdev(&child, 1);
	VERIFY0(spa_vdev_attach(spa, vd->vdev_guid, root, B_FALSE, B_TRUE));
	fnvlist_free(root);
}

static void
fill(uint8_t *buf, uint64_t off)
{
	uint64_t x = off | 1;
	for (uint64_t i = 0; i < PROBE_BLKSZ; i++) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		buf[i] = x;
	}
}

static void
rebuild_wait(vdev_t *vd)
{
	mutex_enter(&vd->vdev_rebuild_lock);
	while (vd->vdev_rebuilding)
		cv_wait(&vd->vdev_rebuild_cv, &vd->vdev_rebuild_lock);
	mutex_exit(&vd->vdev_rebuild_lock);
	txg_wait_synced(spa_get_dsl(vd->vdev_spa), 0);
}

static vdev_rebuild_phys_t
rebuild_saved(vdev_t *vd)
{
	vdev_rebuild_phys_t vrp;
	VERIFY0(zap_lookup(vd->vdev_spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, &vrp));
	return (vrp);
}

static uint64_t
queued_rebuild_offset(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;
	uint64_t offset = UINT64_MAX;
	mutex_enter(&vq->vq_lock);
	zio_t *zio = avl_first(&vq->vq_class[ZIO_PRIORITY_REBUILD].vqc_tree);
	if (zio != NULL)
		offset = zio->io_offset - VDEV_LABEL_START_SIZE;
	mutex_exit(&vq->vq_lock);
	return (offset);
}

static void
tunable(const char *setting)
{
	VERIFY0(handle_tunable_option(setting, B_TRUE));
}

int
main(int argc, char **argv)
{
	if (argc != 3)
		return (2);
	const char *dir = argv[1];
	const char *mode = argv[2];
	char pool[] = "rebuild_lost";
	char cachefile[MAXPATHLEN];
	VERIFY(strcmp(mode, "import") == 0);

	/* kernel_init() must not load or rewrite the machine's pool cache. */
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache", dir);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	/* The new leaf alone must hold the data afterwards. */
	tunable("zfs_rebuild_scrub_enabled=0");
	/* Keep ranges unissued behind the reads that are lost. */
	tunable("zfs_rebuild_vdev_limit=0");

	nvlist_t *props = fnvlist_alloc();
	fnvlist_add_uint64(props, "feature@device_rebuild", 0);
	/* Otherwise a read failing with ENXIO suspends the pool instead. */
	fnvlist_add_uint64(props, zpool_prop_to_name(ZPOOL_PROP_FAILUREMODE),
	    ZIO_FAILURE_MODE_CONTINUE);
	nvlist_t *children[] = { file_vdev(dir, 0), file_vdev(dir, 3) };
	nvlist_t *root = root_vdev(children, 2);
	VERIFY0(spa_create(pool, root, props, NULL, NULL, NULL));
	fnvlist_free(root);
	fnvlist_free(props);

	spa_t *spa;
	VERIFY0(spa_open(pool, &spa, FTAG));
	dsl_pool_t *dp = spa_get_dsl(spa);
	objset_t *os;
	VERIFY0(dmu_objset_own(pool, DMU_OST_ZFS, B_FALSE, B_TRUE, FTAG, &os));
	uint8_t *buf = kmem_alloc(PROBE_BLKSZ, KM_SLEEP);
	dmu_tx_t *tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	uint64_t obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, PROBE_BLKSZ,
	    DMU_OT_NONE, 0, tx);
	dmu_tx_commit(tx);
	uint64_t off;
	for (off = 0; off < PROBE_DATA; off += PROBE_BLKSZ) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, obj, off, PROBE_BLKSZ);
		VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
		fill(buf, off);
		dmu_write(os, obj, off, PROBE_BLKSZ, buf, tx, 0);
		dmu_tx_commit(tx);
	}
	txg_wait_synced(dp, 0);

	/*
	 * A leaf's first DTL object dirties its top-level configuration, and
	 * a configuration without a single good label write suspends the
	 * pool. Rebuild onto the source first, so that it has one.
	 */
	attach(spa, spa->spa_root_vdev->vdev_child[0], dir, 1);
	vdev_t *top = spa->spa_root_vdev->vdev_child[0];
	rebuild_wait(top);
	VERIFY3U(rebuild_saved(top).vrp_rebuild_state, ==,
	    VDEV_REBUILD_COMPLETE);
	VERIFY0(spa_vdev_detach(spa, top->vdev_child[0]->vdev_guid, 0,
	    B_FALSE));
	char path[MAXPATHLEN];
	(void) snprintf(path, sizeof (path), "%s/disk-0", dir);
	VERIFY0(unlink(path));

	zfs_scan_suspend_progress = 1;
	attach(spa, spa->spa_root_vdev->vdev_child[0], dir, 2);
	top = spa->spa_root_vdev->vdev_child[0];
	vdev_t *src = top->vdev_child[0];
	vdev_t *dst = top->vdev_child[1];
	vdev_rebuild_t *vr = &top->vdev_rebuild_config;
	VERIFY(top->vdev_rebuilding);
	spa_async_suspend(spa);

	/* Hold the rebuild's reads in the source's queue. */
	tunable("zfs_vdev_nia_delay=0");
	tunable("zfs_vdev_rebuild_min_active=0");
	tunable("zfs_vdev_rebuild_max_active=0");

	/*
	 * Hold the sync of a txg with writes to the mirror, so that they fail
	 * once the reads are issued and the leaves are gone. A rollback
	 * indexed by the reads' synthetic birth txg rather than their issuing
	 * txg would miss the lost segment when both share a slot.
	 */
	uint64_t txg;
	zio_t *hold = NULL;
	while (hold == NULL) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, obj, off, PROBE_BLKSZ);
		VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
		txg = dmu_tx_get_txg(tx);
		if (((txg + 1) & TXG_MASK) != (TXG_INITIAL & TXG_MASK)) {
			hold = zio_null(spa->spa_txg_zio[txg & TXG_MASK], spa,
			    NULL, NULL, NULL, 0);
			fill(buf, off);
			dmu_write(os, obj, off, PROBE_BLKSZ, buf, tx, 0);
		}
		dmu_tx_commit(tx);
		if (hold == NULL)
			txg_wait_synced(dp, txg);
	}
	dmu_objset_disown(os, B_TRUE, FTAG);
	kmem_free(buf, PROBE_BLKSZ);
	txg_wait_open(dp, txg + 1, B_TRUE);

	zfs_scan_suspend_progress = 0;
	for (int i = 0; queued_rebuild_offset(src) == UINT64_MAX; i++) {
		VERIFY3S(i, <, 600);
		delay(MSEC_TO_TICK(100));
	}
	uint64_t lost = queued_rebuild_offset(src);
	VERIFY3U(vr->vr_last_txg, ==, txg + 1);

	/*
	 * Lose both leaves as the block device layer reports a removal: I/O
	 * to them fails with ENXIO before the pool changes their state. The
	 * held writes then fail and make the mirror unwritable.
	 */
	src->vdev_remove_wanted = B_TRUE;
	dst->vdev_remove_wanted = B_TRUE;
	zio_nowait(hold);
	txg_wait_synced(dp, txg);
	VERIFY(!vdev_writeable(top));
	VERIFY(!spa_suspended(spa));
	VERIFY3U(queued_rebuild_offset(src), ==, lost);

	tunable("zfs_vdev_nia_delay=5");
	tunable("zfs_vdev_rebuild_min_active=1");
	tunable("zfs_vdev_rebuild_max_active=3");
	/* Nothing reissues from the queue until an I/O enters or leaves it. */
	abd_t *abd = abd_alloc_linear(SPA_MINBLOCKSIZE, B_FALSE);
	spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_READER);
	VERIFY3S(zio_wait(zio_read_phys(NULL, src, 0, SPA_MINBLOCKSIZE, abd,
	    ZIO_CHECKSUM_OFF, NULL, NULL, ZIO_PRIORITY_SYNC_READ,
	    ZIO_FLAG_CANFAIL, B_TRUE)), ==, ENXIO);
	spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	abd_free(abd);

	rebuild_wait(top);
	vdev_rebuild_phys_t vrp = rebuild_saved(top);
	(void) printf("%s: reads issued in txg %llu, lowest at %llu, "
	    "saved %llu, state %llu, errors %llu\n", mode,
	    (u_longlong_t)(txg + 1), (u_longlong_t)lost,
	    (u_longlong_t)vrp.vrp_last_offset,
	    (u_longlong_t)vrp.vrp_rebuild_state,
	    (u_longlong_t)vrp.vrp_errors);
	if (vrp.vrp_rebuild_state != VDEV_REBUILD_ACTIVE ||
	    vrp.vrp_last_offset > lost) {
		(void) fprintf(stderr, "%s: rebuild saved progress past a "
		    "segment it lost\n", mode);
		return (1);
	}
	VERIFY0(vrp.vrp_errors);

	/* The leaves return. */
	spa_vdev_state_enter(spa, SCL_NONE);
	vdev_clear(spa, top);
	VERIFY0(spa_vdev_state_exit(spa, top, 0));
	VERIFY(vdev_writeable(top));
	spa_async_resume(spa);

	spa_close(spa, FTAG);
	nvlist_t *config;
	VERIFY0(spa_export(pool, &config, B_FALSE, B_FALSE));
	VERIFY0(spa_import(pool, config, NULL, 0));
	fnvlist_free(config);
	VERIFY0(spa_open(pool, &spa, FTAG));
	top = spa->spa_root_vdev->vdev_child[0];
	rebuild_wait(top);
	VERIFY3U(rebuild_saved(top).vrp_rebuild_state, !=,
	    VDEV_REBUILD_ACTIVE);
	(void) printf("%s: rebuild resumed at the lost segment and finished\n",
	    mode);

	spa_close(spa, FTAG);
	VERIFY0(spa_export(pool, NULL, B_FALSE, B_FALSE));
	kernel_fini();
	return (0);
}
