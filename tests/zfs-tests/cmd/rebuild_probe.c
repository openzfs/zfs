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
#include <sys/fs/zfs.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_rebuild.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>

extern int zfs_scan_suspend_progress;

#define	REBUILD_PROBE_ERRORS	7

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
root_vdev(nvlist_t *child)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&child, 1);
	fnvlist_free(child);
	return (nvl);
}

typedef struct rebuild_probe {
	const char	*rp_mode;
	uint64_t	rp_offset;	/* saved offset of the edited state */
} rebuild_probe_t;

/*
 * Edit the saved rebuild state as other software could leave it. "resume"
 * advances the offset and saves an error count with the matching copy, as
 * software which counts them would. "legacy" saves the same state without
 * the copy, and "advanced" moves only the state's offset past the copy.
 * "zero" removes the copy and starts the new leaf's DTL at txg 0.
 */
static void
rebuild_probe_age_sync(void *arg, dmu_tx_t *tx)
{
	rebuild_probe_t *rp = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	objset_t *mos = spa->spa_meta_objset;
	vdev_t *vd = spa->spa_root_vdev->vdev_child[0];
	vdev_rebuild_phys_t vrp;

	VERIFY0P(vd->vdev_rebuild_thread);
	VERIFY0(zap_lookup(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, &vrp));
	VERIFY3U(vrp.vrp_rebuild_state, ==, VDEV_REBUILD_ACTIVE);
	if (strcmp(rp->rp_mode, "zero") == 0) {
		vdev_t *leaf = vd->vdev_child[1];
		vdev_dtl_dirty(leaf, DTL_MISSING, 0, TXG_INITIAL);
		vdev_dirty(vd, VDD_DTL, leaf, dmu_tx_get_txg(tx));
	} else {
		/* Stay within the held range so the rebuild cannot finish. */
		vrp.vrp_last_offset += 1ULL << vd->vdev_ashift;
		if (strcmp(rp->rp_mode, "advanced") != 0)
			vrp.vrp_errors = REBUILD_PROBE_ERRORS;
		VERIFY0(zap_update(mos, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
		    REBUILD_PHYS_ENTRIES, &vrp, tx));
	}
	rp->rp_offset = vrp.vrp_last_offset;
	if (strcmp(rp->rp_mode, "resume") == 0) {
		VERIFY0(zap_update(mos, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_VDEV_REBUILD_ACCOUNTED, sizeof (uint64_t),
		    REBUILD_PHYS_ENTRIES, &vrp, tx));
	} else if (strcmp(rp->rp_mode, "advanced") != 0) {
		VERIFY0(zap_remove(mos, vd->vdev_top_zap,
		    VDEV_TOP_ZAP_VDEV_REBUILD_ACCOUNTED, tx));
	}
}

/* Read both saved entries, which must match after a resume or reset. */
static void
rebuild_probe_saved(vdev_t *vd, vdev_rebuild_phys_t *vrp)
{
	objset_t *mos = vd->vdev_spa->spa_meta_objset;
	vdev_rebuild_phys_t accounted;

	VERIFY0(zap_lookup(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_PHYS, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, vrp));
	VERIFY0(zap_lookup(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_ACCOUNTED, sizeof (uint64_t),
	    REBUILD_PHYS_ENTRIES, &accounted));
	VERIFY0(memcmp(vrp, &accounted, sizeof (accounted)));
}

int
main(int argc, char **argv)
{
	if (argc != 3)
		return (2);
	const char *dir = argv[1];
	const char *mode = argv[2];
	char pool[] = "rebuild_probe";
	char cachefile[MAXPATHLEN];
	boolean_t resume = strcmp(mode, "resume") == 0;
	boolean_t zero = strcmp(mode, "zero") == 0;
	VERIFY(resume || zero || strcmp(mode, "legacy") == 0 ||
	    strcmp(mode, "advanced") == 0);
	rebuild_probe_t rp = { .rp_mode = mode };

	/* kernel_init() must not load or rewrite the machine's pool cache. */
	(void) snprintf(cachefile, sizeof (cachefile), "%s/zpool.cache", dir);
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	nvlist_t *props = fnvlist_alloc();
	fnvlist_add_uint64(props, "feature@device_rebuild", 0);
	nvlist_t *root = root_vdev(file_vdev(dir, 0));
	VERIFY0(spa_create(pool, root, props, NULL, NULL, NULL));
	fnvlist_free(root);
	fnvlist_free(props);

	spa_t *spa;
	VERIFY0(spa_open(pool, &spa, FTAG));
	dsl_pool_t *dp = spa_get_dsl(spa);
	vdev_t *vd = spa->spa_root_vdev->vdev_child[0];

	/* Hold the rebuild at its first range, with both entries saved. */
	zfs_scan_suspend_progress = 1;
	root = root_vdev(file_vdev(dir, 1));
	VERIFY0(spa_vdev_attach(spa, vd->vdev_guid, root, B_FALSE, B_TRUE));
	fnvlist_free(root);
	txg_wait_synced(dp, 0);
	vd = spa->spa_root_vdev->vdev_child[0];
	VERIFY(vd->vdev_rebuilding);
	VERIFY0(zap_contains(spa->spa_meta_objset, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_VDEV_REBUILD_ACCOUNTED));
	/*
	 * A stopping rebuild saves its progress once more. Let that sync
	 * before editing the saved state, as older software would find it.
	 */
	spa_namespace_enter(FTAG);
	vdev_rebuild_stop_wait(vd);
	spa_namespace_exit(FTAG);
	txg_wait_synced(dp, 0);
	VERIFY0(dsl_sync_task(pool, NULL, rebuild_probe_age_sync,
	    &rp, 0, ZFS_SPACE_CHECK_NONE));

	spa_close(spa, FTAG);
	nvlist_t *config;
	VERIFY0(spa_export(pool, &config, B_FALSE, B_FALSE));
	VERIFY0(spa_import(pool, config, NULL, 0));
	fnvlist_free(config);
	VERIFY0(spa_open(pool, &spa, FTAG));
	dp = spa_get_dsl(spa);
	vd = spa->spa_root_vdev->vdev_child[0];

	/*
	 * Import decides whether to reset while loading, and may already have
	 * synced the reset, which restarts from offset zero with no errors.
	 * A resumed rebuild keeps its offset and count with no reset pending.
	 */
	vdev_rebuild_phys_t *vrp = &vd->vdev_rebuild_config.vr_rebuild_phys;
	for (int i = 0; ; i++) {
		mutex_enter(&vd->vdev_rebuild_lock);
		uint64_t offset = vrp->vrp_last_offset;
		boolean_t pending = vd->vdev_rebuild_reset_wanted;
		mutex_exit(&vd->vdev_rebuild_lock);
		if (resume) {
			VERIFY(!pending);
			break;
		}
		if ((offset == 0 && !pending) || i == 100)
			break;
		txg_wait_synced(dp, 0);
	}
	txg_wait_synced(dp, 0);
	vdev_rebuild_phys_t saved;
	rebuild_probe_saved(vd, &saved);
	VERIFY(vd->vdev_rebuilding);
	VERIFY3U(saved.vrp_rebuild_state, ==, VDEV_REBUILD_ACTIVE);
	VERIFY3U(saved.vrp_last_offset, ==, resume ? rp.rp_offset : 0);
	VERIFY3U(saved.vrp_errors, ==, resume ? REBUILD_PROBE_ERRORS : 0);

	/*
	 * The reset rebuild covers the DTL from txg 0 and must complete. Its
	 * lower bound may not wrap below that txg.
	 */
	if (zero) {
		vdev_t *leaf = vd->vdev_child[1];
		mutex_enter(&leaf->vdev_dtl_lock);
		VERIFY(!zfs_range_tree_is_empty(leaf->vdev_dtl[DTL_MISSING]));
		VERIFY0(zfs_range_tree_min(leaf->vdev_dtl[DTL_MISSING]));
		mutex_exit(&leaf->vdev_dtl_lock);
		zfs_scan_suspend_progress = 0;
		hrtime_t deadline = gethrtime() + SEC2NSEC(60);
		for (;;) {
			mutex_enter(&vd->vdev_rebuild_lock);
			uint64_t state = vrp->vrp_rebuild_state;
			mutex_exit(&vd->vdev_rebuild_lock);
			if (state == VDEV_REBUILD_COMPLETE)
				break;
			VERIFY3S(gethrtime(), <, deadline);
			txg_wait_synced(dp, 0);
		}
		txg_wait_synced(dp, 0);
		rebuild_probe_saved(vd, &saved);
		VERIFY0(saved.vrp_min_txg);
		VERIFY0(saved.vrp_errors);
		mutex_enter(&leaf->vdev_dtl_lock);
		VERIFY(zfs_range_tree_is_empty(leaf->vdev_dtl[DTL_MISSING]));
		mutex_exit(&leaf->vdev_dtl_lock);
	}
	(void) printf("%s: imported rebuild %s\n", mode,
	    resume ? "resumed" : zero ? "reset and completed" : "reset");

	zfs_scan_suspend_progress = 0;
	spa_close(spa, FTAG);
	VERIFY0(spa_destroy(pool));
	kernel_fini();
	return (0);
}
