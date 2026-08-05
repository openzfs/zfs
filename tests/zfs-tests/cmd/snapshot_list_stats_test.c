// SPDX-License-Identifier: CDDL-1.0
/*
 * Verify projected snapshot-stat error handling against a real pool opened
 * through libzpool.  A link-time wrapper injects dmu_bonus_hold() errors for
 * exact MOS objects, avoiding persistent pool damage and broad I/O faults.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static __thread objset_t *fault_objset;
static __thread uint64_t fault_object;
static __thread boolean_t fault_reached;

int __real_dmu_bonus_hold(objset_t *, uint64_t, const void *, dmu_buf_t **);
int __wrap_dmu_bonus_hold(objset_t *, uint64_t, const void *, dmu_buf_t **);

/*
 * Inject only the selected MOS object while preserving every other real read.
 */
int
__wrap_dmu_bonus_hold(objset_t *os, uint64_t object, const void *tag,
    dmu_buf_t **dbp)
{
	if (os == fault_objset && fault_object != 0 && object == fault_object) {
		fault_reached = B_TRUE;
		return (EIO);
	}

	return (__real_dmu_bonus_hold(os, object, tag, dbp));
}

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
check_snapshot_stats(dsl_pool_t *dp, uint64_t dsobj, const char *description,
    uint64_t injected_object, uint64_t min_txg, uint64_t max_txg,
    int expected_error, boolean_t expected_valid,
    boolean_t expected_injection)
{
	dsl_dataset_snapshot_stats_t stats;
	boolean_t injected;
	int error;

	fault_objset = dp->dp_meta_objset;
	fault_object = injected_object;
	fault_reached = B_FALSE;
	error = dsl_dataset_snapshot_stats(dp, dsobj, B_FALSE, B_FALSE,
	    B_TRUE, min_txg, max_txg, &stats);
	fault_objset = NULL;
	fault_object = 0;
	injected = fault_reached;

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

	if (argc != 3) {
		(void) fprintf(stderr,
		    "usage: %s snapshot search-path\n", argv[0]);
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
	if (ds != NULL)
		dsl_dataset_rele(ds, FTAG);
	if (dp != NULL)
		dsl_pool_rele(dp, FTAG);
	kernel_fini();
	return (error == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
