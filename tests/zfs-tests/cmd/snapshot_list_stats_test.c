// SPDX-License-Identifier: CDDL-1.0
/*
 * Verify projected snapshot-stat error handling against a real pool opened
 * through libzpool.  A ZTS-only preload shim injects dmu_bonus_hold() errors
 * for exact MOS objects, avoiding persistent pool damage and broad I/O faults.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libnvpair.h>
#include <libzpool.h>
#include <libzutil.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/fs/zfs.h>
#include <sys/spa.h>
#include <sys/zfs_context.h>
#include <zfs_prop.h>

static const char *marker_path;

static int
get_file_info(dmu_object_type_t bonus_type, const void *data,
    zfs_file_info_t *zoi)
{
	(void) bonus_type;
	(void) data;
	(void) zoi;
	return (ENOENT);
}

static int
set_injected_object(uint64_t object)
{
	char value[32];

	(void) snprintf(value, sizeof (value), "%llu",
	    (u_longlong_t)object);
	if (setenv("ZFS_SNAPSHOT_LIST_TEST_MODE", "dmu_bonus_hold_eio", 1) !=
	    0 || setenv("ZFS_SNAPSHOT_LIST_TEST_OBJECT", value, 1) != 0) {
		(void) fprintf(stderr, "cannot configure error injection: %s\n",
		    strerror(errno));
		return (-1);
	}
	return (0);
}

static void
clear_injected_object(void)
{
	(void) unsetenv("ZFS_SNAPSHOT_LIST_TEST_OBJECT");
	(void) unsetenv("ZFS_SNAPSHOT_LIST_TEST_MODE");
}

static int
check_snapshot_stats(dsl_pool_t *dp, uint64_t dsobj, const char *description,
    uint64_t injected_object, uint64_t min_txg, uint64_t max_txg,
    int expected_error, boolean_t expected_valid,
    boolean_t expected_injection)
{
	dsl_dataset_snapshot_stats_t stats;
	boolean_t injected;
	int error;

	if (unlink(marker_path) != 0 && errno != ENOENT) {
		(void) fprintf(stderr, "cannot remove marker for %s: %s\n",
		    description, strerror(errno));
		return (-1);
	}
	if (injected_object != 0 && set_injected_object(injected_object) != 0)
		return (-1);

	error = dsl_dataset_snapshot_stats(dp, dsobj, B_FALSE, B_FALSE,
	    B_TRUE, min_txg, max_txg, &stats);
	clear_injected_object();
	injected = access(marker_path, F_OK) == 0;

	if (error != expected_error ||
	    stats.dss_written_valid != expected_valid ||
	    injected != expected_injection) {
		(void) fprintf(stderr,
		    "%s: error=%d written_valid=%u injected=%u; expected "
		    "error=%d written_valid=%u injected=%u\n",
		    description, error, stats.dss_written_valid, injected,
		    expected_error, expected_valid, expected_injection);
		return (-1);
	}
	return (0);
}

static int
import_pool_readonly(char *pool, const char *search_path)
{
	char *paths[] = { (char *)search_path };
	importargs_t args = { 0 };
	libpc_handle_t lpch = {
		.lpc_lib_handle = NULL,
		.lpc_ops = &libzpool_config_ops,
		.lpc_printerr = B_TRUE
	};
	nvlist_t *config = NULL;
	nvlist_t *props = NULL;
	int error;

	args.paths = 1;
	args.path = paths;
	args.can_be_active = B_TRUE;
	error = zpool_find_config(&lpch, pool, &config, &args);
	if (error != 0)
		return (error);

	error = nvlist_alloc(&props, NV_UNIQUE_NAME, 0);
	if (error == 0) {
		error = nvlist_add_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_READONLY), 1);
	}
	if (error == 0) {
		error = spa_import(pool, config, props, ZFS_IMPORT_SKIP_MMP);
		if (error == EEXIST)
			error = 0;
	}
	nvlist_free(props);
	nvlist_free(config);
	return (error);
}

int
main(int argc, char **argv)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	dsl_dataset_t *ds = NULL;
	dsl_pool_t *dp = NULL;
	dsl_dataset_phys_t *dsp;
	uint64_t dsobj, deadlist_obj, prev_obj, creation_txg;
	char *separator;
	int error = 0;

	if (argc != 4) {
		(void) fprintf(stderr,
		    "usage: %s snapshot search-path marker-path\n", argv[0]);
		return (EXIT_FAILURE);
	}
	marker_path = argv[3];
	if (setenv("ZFS_SNAPSHOT_LIST_TEST_MARKER", marker_path, 1) != 0) {
		(void) fprintf(stderr, "cannot configure marker path: %s\n",
		    strerror(errno));
		return (EXIT_FAILURE);
	}
	if (strlcpy(pool, argv[1], sizeof (pool)) >= sizeof (pool) ||
	    (separator = strpbrk(pool, "/@")) == NULL) {
		(void) fprintf(stderr, "invalid snapshot name: %s\n", argv[1]);
		return (EXIT_FAILURE);
	}
	*separator = '\0';

	zfs_prop_init();
	kernel_init(SPA_MODE_READ);
	dmu_objset_register_type(DMU_OST_ZFS, get_file_info);

	error = import_pool_readonly(pool, argv[2]);
	if (error != 0) {
		(void) fprintf(stderr, "cannot import %s: %s\n", pool,
		    strerror(error));
		goto out;
	}
	error = dsl_pool_hold(argv[1], FTAG, &dp);
	if (error != 0) {
		(void) fprintf(stderr, "cannot hold %s: %s\n", pool,
		    strerror(error));
		goto out;
	}
	error = dsl_dataset_hold(dp, argv[1], FTAG, &ds);
	if (error != 0) {
		(void) fprintf(stderr, "cannot hold %s: %s\n", argv[1],
		    strerror(error));
		goto out;
	}

	dsp = dsl_dataset_phys(ds);
	dsobj = ds->ds_object;
	deadlist_obj = dsp->ds_deadlist_obj;
	prev_obj = dsp->ds_prev_snap_obj;
	creation_txg = dsp->ds_creation_txg;
	dsl_dataset_rele(ds, FTAG);
	ds = NULL;
	if (deadlist_obj == 0 || prev_obj == 0) {
		(void) fprintf(stderr, "%s lacks a deadlist or predecessor\n",
		    argv[1]);
		error = EINVAL;
		goto out;
	}

	if (check_snapshot_stats(dp, dsobj, "normal written lookup", 0, 0, 0,
	    0, B_TRUE, B_FALSE) != 0 ||
	    check_snapshot_stats(dp, dsobj, "exact-TXG written lookup", 0,
	    creation_txg, creation_txg, 0, B_TRUE, B_FALSE) != 0 ||
	    check_snapshot_stats(dp, dsobj, "current deadlist EIO",
	    deadlist_obj, 0, 0, EIO, B_FALSE, B_TRUE) != 0 ||
	    check_snapshot_stats(dp, dsobj, "predecessor EIO", prev_obj, 0, 0,
	    0, B_FALSE, B_TRUE) != 0 ||
	    check_snapshot_stats(dp, dsobj,
	    "below-minimum current deadlist EIO", deadlist_obj,
	    creation_txg + 1, 0, EIO, B_FALSE, B_TRUE) != 0 ||
	    check_snapshot_stats(dp, dsobj, "below-minimum predecessor EIO",
	    prev_obj, creation_txg + 1, 0, 0, B_FALSE, B_FALSE) != 0 ||
	    check_snapshot_stats(dp, dsobj,
	    "above-maximum current deadlist EIO", deadlist_obj, 0,
	    creation_txg - 1, EIO, B_FALSE, B_TRUE) != 0 ||
	    check_snapshot_stats(dp, dsobj, "above-maximum predecessor EIO",
	    prev_obj, 0, creation_txg - 1, 0, B_FALSE, B_FALSE) != 0) {
		error = EPROTO;
	}

out:
	clear_injected_object();
	if (ds != NULL)
		dsl_dataset_rele(ds, FTAG);
	if (dp != NULL)
		dsl_pool_rele(dp, FTAG);
	kernel_fini();
	(void) unlink(marker_path);
	return (error == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
