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
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>

extern int zfs_scan_suspend_progress;

static nvlist_t *
mirror(const char *dir, int index, int count)
{
	nvlist_t **children = kmem_alloc(count * sizeof (*children), KM_SLEEP);
	for (int i = 0; i < count; i++) {
		char path[MAXPATHLEN];
		(void) snprintf(path, sizeof (path), "%s/disk-%d", dir,
		    index + i);
		int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
		VERIFY3S(fd, >=, 0);
		VERIFY0(ftruncate(fd, 512ULL << 20));
		VERIFY0(close(fd));
		children[i] = fnvlist_alloc();
		fnvlist_add_string(children[i], ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_FILE);
		fnvlist_add_string(children[i], ZPOOL_CONFIG_PATH, path);
	}
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_MIRROR);
	fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)children, count);
	for (int i = 0; i < count; i++)
		fnvlist_free(children[i]);
	kmem_free(children, count * sizeof (*children));
	return (nvl);
}

/*
 * Leave the saved state as software which does not maintain its healing copy
 * would: without the copy, or with progress the copy does not include.
 */
static void
resilver_probe_age_sync(void *arg, dmu_tx_t *tx)
{
	objset_t *mos = dmu_tx_pool(tx)->dp_meta_objset;
	dsl_scan_phys_t phys;

	if (strcmp(arg, "legacy") == 0) {
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_SCAN_HEALING, tx));
		return;
	}
	VERIFY0(zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SCAN,
	    sizeof (uint64_t), SCAN_PHYS_NUMINTS, &phys));
	phys.scn_examined++;
	VERIFY0(zap_update(mos, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SCAN,
	    sizeof (uint64_t), SCAN_PHYS_NUMINTS, &phys, tx));
}

int
main(int argc, char **argv)
{
	if (argc != 3)
		return (2);
	const char *dir = argv[1];
	const char *mode = argv[2];
	char pool[] = "resilver_probe";
	boolean_t import = strcmp(mode, "resume") == 0 ||
	    strcmp(mode, "legacy") == 0 || strcmp(mode, "advanced") == 0;
	char cachefile[MAXPATHLEN];
	VERIFY(import || strcmp(mode, "reopen") == 0 ||
	    strcmp(mode, "clear") == 0 || strcmp(mode, "probe") == 0 ||
	    strcmp(mode, "complete") == 0);

	/* kernel_init() must not load or rewrite the machine's pool cache. */
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache", dir);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	/*
	 * The first mirror keeps one writable source while two leaves miss
	 * participation. The second mirror supplies a newer healing range.
	 */
	nvlist_t *children[] = { mirror(dir, 0, 3), mirror(dir, 3, 2) };
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)children, 2);
	VERIFY0(spa_create(pool, root, NULL, NULL, NULL, NULL));
	fnvlist_free(root);
	for (int i = 0; i < 2; i++)
		fnvlist_free(children[i]);

	spa_t *spa;
	VERIFY0(spa_open(pool, &spa, FTAG));
	dsl_pool_t *dp = spa_get_dsl(spa);
	vdev_t **old = &spa->spa_root_vdev->vdev_child[0]->vdev_child[1];
	vdev_t *new = spa->spa_root_vdev->vdev_child[1]->vdev_child[1];

	/*
	 * Hold async work while constructing and recovering the interval
	 * with healthy enum state but failed probes. Syncing and scan setup
	 * still run normally.
	 */
	spa_async_suspend(spa);
	zfs_scan_suspend_progress = 1;
	for (int i = 0; i < 4; i++)
		txg_wait_synced(dp, 0);
	uint64_t txg = spa_last_synced_txg(spa);

	zinject_record_t record = {
		.zi_cmd = ZINJECT_DEVICE_FAULT,
		.zi_error = EIO,
		.zi_iotype = ZINJECT_IOTYPE_PROBE,
		.zi_freq = ZI_PERCENTAGE_MAX
	};
	/*
	 * Construct distinct missing ranges to isolate participation from
	 * data reconstruction, which the kernel reopen fixture exercises.
	 * The failed leaves' ranges precede the range on the writable mirror,
	 * so the active pass excludes their data despite their healthy enums.
	 */
	for (int i = 0; i < 2 && !import; i++) {
		int id;
		record.zi_guid = old[i]->vdev_guid;
		VERIFY0(zio_inject_fault(pool, 0, &id, &record));
		spa_vdev_state_enter(spa, SCL_NONE);
		VERIFY3S(zio_wait(vdev_probe(old[i], NULL)), ==, ENXIO);
		VERIFY3U(old[i]->vdev_state, ==, VDEV_STATE_HEALTHY);
		VERIFY(old[i]->vdev_cant_write);
		vdev_dtl_dirty(old[i], DTL_MISSING, txg - 3 + i, 1);
		VERIFY0(spa_vdev_state_exit(spa, NULL, 0));
		VERIFY0(zio_clear_fault(id));
	}
	spa_vdev_state_enter(spa, SCL_NONE);
	vdev_dtl_dirty(new, DTL_MISSING, txg, 1);
	VERIFY0(spa_vdev_state_exit(spa, NULL, 0));

	dsl_scan_schedule_resilver(dp, 0);
	txg_wait_synced(dp, 0);
	dsl_scan_t *scn = dp->dp_scan;
	VERIFY(dsl_scan_resilvering(dp));
	VERIFY3U(scn->scn_phys.scn_min_txg, ==, txg - 1);
	VERIFY0(scn->scn_phys.scn_errors);
	VERIFY(scn->scn_coverage_valid);

	if (import) {
		/*
		 * Without the failed leaves, only the saved state decides
		 * whether import resumes the pass. Suspended progress keeps
		 * the scan from rewriting either record before export.
		 */
		boolean_t resume = strcmp(mode, "resume") == 0;
		VERIFY0(zap_contains(dp->dp_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SCAN_HEALING));
		if (!resume) {
			VERIFY0(dsl_sync_task(pool, NULL,
			    resilver_probe_age_sync, (void *)mode, 0,
			    ZFS_SPACE_CHECK_NONE));
		}
		spa_close(spa, FTAG);
		nvlist_t *config;
		VERIFY0(spa_export(pool, &config, B_FALSE, B_FALSE));
		VERIFY0(spa_import(pool, config, NULL, 0));
		fnvlist_free(config);
		VERIFY0(spa_open(pool, &spa, FTAG));
		dp = spa_get_dsl(spa);
		scn = dp->dp_scan;
		txg_wait_synced(dp, 0);
		/*
		 * The in-core missing range was never saved, so a restart
		 * finds nothing to heal and ends the pass.
		 */
		VERIFY3B(dsl_scan_resilvering(dp), ==, resume);
		(void) printf("%s: imported healing pass %s\n", mode,
		    resume ? "resumed" : "not resumed");

		(void) dsl_scan_cancel(dp);
		spa_close(spa, FTAG);
		VERIFY0(spa_destroy(pool));
		kernel_fini();
		return (0);
	}

	for (int i = 0; i < 2; i++) {
		VERIFY3U(old[i]->vdev_state, ==, VDEV_STATE_HEALTHY);
		VERIFY(!vdev_writeable(old[i]));
	}

	/* Reopening a participating mirror must not invalidate its coverage. */
	spa_vdev_state_enter(spa, SCL_NONE);
	vdev_reopen(new->vdev_top);
	VERIFY0(spa_vdev_state_exit(spa, new, 0));
	VERIFY(scn->scn_coverage_valid);
	VERIFY0(scn->scn_phys.scn_errors);

	if (strcmp(mode, "complete") == 0) {
		/*
		 * Completing without the unwritable leaves must not retire
		 * their older missing ranges, which the pass never visited.
		 * A suspended async worker also suspends scanning.
		 */
		spa_async_resume(spa);
		zfs_scan_suspend_progress = 0;
		for (int i = 0; i < 100 && dsl_scan_resilvering(dp); i++)
			txg_wait_synced(dp, 0);
		VERIFY(!dsl_scan_resilvering(dp));
		for (int i = 0; i < 2; i++) {
			VERIFY3U(old[i]->vdev_state, ==, VDEV_STATE_HEALTHY);
			VERIFY(!vdev_writeable(old[i]));
			VERIFY(vdev_dtl_contains(old[i], DTL_MISSING,
			    txg - 3 + i, 1));
		}

		/* Their return must start healing that covers them. */
		spa_vdev_state_enter(spa, SCL_NONE);
		vdev_reopen(old[0]->vdev_top);
		VERIFY0(spa_vdev_state_exit(spa, old[0], 0));
		for (int i = 0; i < 2; i++)
			VERIFY(vdev_writeable(old[i]));
		for (int i = 0; i < 100 && (vdev_dtl_contains(old[0],
		    DTL_MISSING, txg - 3, 1) || vdev_dtl_contains(old[1],
		    DTL_MISSING, txg - 2, 1)); i++)
			txg_wait_synced(dp, 0);
		for (int i = 0; i < 2; i++) {
			VERIFY(!vdev_dtl_contains(old[i], DTL_MISSING,
			    txg - 3 + i, 1));
		}
		VERIFY3U(scn->scn_phys.scn_func, ==, POOL_SCAN_RESILVER);
		VERIFY3U(scn->scn_phys.scn_state, ==, DSS_FINISHED);
		VERIFY3U(scn->scn_phys.scn_min_txg, <, txg - 3);
		(void) printf("%s: unwritable leaves kept their missing "
		    "ranges until healed\n", mode);

		spa_close(spa, FTAG);
		VERIFY0(spa_destroy(pool));
		kernel_fini();
		return (0);
	}

	if (strcmp(mode, "probe") == 0) {
		/* EEXIST means the successful probe kept the device present. */
		for (int i = 0; i < 2; i++)
			VERIFY3S(vdev_remove_wanted(spa, old[i]->vdev_guid),
			    ==, EEXIST);
	} else {
		/*
		 * Return both leaves through parallel child opens. The parent's
		 * state lock excludes scan transitions, not sibling callbacks.
		 */
		spa_vdev_state_enter(spa, SCL_NONE);
		if (strcmp(mode, "clear") == 0)
			vdev_clear(spa, old[0]);
		else
			vdev_reopen(old[0]->vdev_top);
		VERIFY0(spa_vdev_state_exit(spa, old[0], 0));
	}
	for (int i = 0; i < 2; i++) {
		VERIFY(vdev_writeable(old[i]));
		VERIFY3U(old[i]->vdev_state, ==, VDEV_STATE_HEALTHY);
	}
	VERIFY(!scn->scn_coverage_valid);
	/* Lost coverage is not a scan error that users should chase. */
	VERIFY0(scn->scn_phys.scn_errors);
	VERIFY(dsl_scan_resilver_scheduled(dp));

	/*
	 * Traversal is still suspended. The availability/configuration sync
	 * must persist the lost coverage without waiting for scan progress.
	 */
	txg_wait_synced(dp, 0);
	dsl_scan_phys_t saved;
	VERIFY0(zap_lookup(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCAN, sizeof (uint64_t), SCAN_PHYS_NUMINTS, &saved));
	VERIFY3S(zap_contains(dp->dp_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_SCAN_HEALING), ==, ENOENT);
	VERIFY0(saved.scn_errors);
	(void) printf("%s: both writable recoveries invalidated and persisted "
	    "coverage\n", mode);

	VERIFY0(dsl_scan_cancel(dp));
	spa_async_resume(spa);
	spa_close(spa, FTAG);
	VERIFY0(spa_destroy(pool));
	kernel_fini();
	return (0);
}
