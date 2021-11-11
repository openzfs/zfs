/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 */

/*
 * zhack is a debugging tool that can write changes to ZFS pool using libzpool
 * for testing purposes. Altering pools with zhack is unsupported and may
 * result in corrupted pools.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/zfs_znode.h>
#include <sys/dsl_synctask.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/zfeature.h>
#include <sys/dmu_tx.h>
#include <zfeature_common.h>
#include <libzfs.h>
#include <libzutil.h>

extern int reference_tracking_enable;
extern int zfs_txg_timeout;
extern boolean_t zfeature_checks_disable;

const char cmdname[] = "zhack";
static importargs_t g_importargs;
static char *g_pool;
static boolean_t g_readonly;

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: %s [-c cachefile] [-d dir] <subcommand> <args> ...\n"
	    "where <subcommand> <args> is one of the following:\n"
	    "\n", cmdname);

	(void) fprintf(stderr,
	    "    feature stat <pool>\n"
	    "        print information about enabled features\n"
	    "    feature enable [-r] [-d desc] <pool> <feature>\n"
	    "        add a new enabled feature to the pool\n"
	    "        -d <desc> sets the feature's description\n"
	    "        -r set read-only compatible flag for feature\n"
	    "    feature ref [-md] <pool> <feature>\n"
	    "        change the refcount on the given feature\n"
	    "        -d decrease instead of increase the refcount\n"
	    "        -m add the feature to the label if increasing refcount\n"
	    "\n"
	    "      <feature> : should be a feature guid\n"
	    "\n"
	    "    scrub [-EPRTnprsv] [-D ddt_class]\n"
	    "          [-G gap] [-H hard_factor] [-M physmem]\n"
	    "          [-O optkey=value]* [-S soft_fact] [-i ckpt_interval]\n"
	    "          [-t scan_op_time] [-x txg_timeout] <pool>\n");
	exit(1);
}


static __attribute__((noreturn)) __attribute__((format(printf, 3, 4))) void
fatal(spa_t *spa, void *tag, const char *fmt, ...)
{
	va_list ap;

	if (spa != NULL) {
		spa_close(spa, tag);
		(void) spa_export(g_pool, NULL, B_TRUE, B_FALSE);
	}

	va_start(ap, fmt);
	(void) fprintf(stderr, "%s: ", cmdname);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void) fprintf(stderr, "\n");

	exit(1);
}

/* ARGSUSED */
static int
space_delta_cb(dmu_object_type_t bonustype, const void *data,
    zfs_file_info_t *zoi)
{
	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (ENOENT);
	(void) fprintf(stderr, "modifying object that needs user accounting");
	abort();
}

/*
 * Target is the dataset whose pool we want to open.
 */
static void
zhack_import(char *target, boolean_t readonly)
{
	nvlist_t *config;
	nvlist_t *props;
	int error;

	kernel_init(readonly ? SPA_MODE_READ :
	    (SPA_MODE_READ | SPA_MODE_WRITE));

	dmu_objset_register_type(DMU_OST_ZFS, space_delta_cb);

	g_readonly = readonly;
	g_importargs.can_be_active = readonly;
	g_pool = strdup(target);

	error = zpool_find_config(NULL, target, &config, &g_importargs,
	    &libzpool_config_ops);
	if (error)
		fatal(NULL, FTAG, "cannot import '%s'", target);

	props = NULL;
	if (readonly) {
		VERIFY(nvlist_alloc(&props, NV_UNIQUE_NAME, 0) == 0);
		VERIFY(nvlist_add_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_READONLY), 1) == 0);
	}

	zfeature_checks_disable = B_TRUE;
	error = spa_import(target, config, props,
	    (readonly ?  ZFS_IMPORT_SKIP_MMP : ZFS_IMPORT_NORMAL));
	fnvlist_free(config);
	zfeature_checks_disable = B_FALSE;
	if (error == EEXIST)
		error = 0;

	if (error)
		fatal(NULL, FTAG, "can't import '%s': %s", target,
		    strerror(error));
}

static void
zhack_spa_open(char *target, boolean_t readonly, void *tag, spa_t **spa)
{
	int err;

	zhack_import(target, readonly);

	zfeature_checks_disable = B_TRUE;
	err = spa_open(target, spa, tag);
	zfeature_checks_disable = B_FALSE;

	if (err != 0)
		fatal(*spa, FTAG, "cannot open '%s': %s", target,
		    strerror(err));
	if (spa_version(*spa) < SPA_VERSION_FEATURES) {
		fatal(*spa, FTAG, "'%s' has version %d, features not enabled",
		    target, (int)spa_version(*spa));
	}
}

static void
dump_obj(objset_t *os, uint64_t obj, const char *name)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	(void) printf("%s_obj:\n", name);

	for (zap_cursor_init(&zc, os, obj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		if (za.za_integer_length == 8) {
			ASSERT(za.za_num_integers == 1);
			(void) printf("\t%s = %llu\n",
			    za.za_name, (u_longlong_t)za.za_first_integer);
		} else {
			ASSERT(za.za_integer_length == 1);
			char val[1024];
			VERIFY(zap_lookup(os, obj, za.za_name,
			    1, sizeof (val), val) == 0);
			(void) printf("\t%s = %s\n", za.za_name, val);
		}
	}
	zap_cursor_fini(&zc);
}

static void
dump_mos(spa_t *spa)
{
	nvlist_t *nv = spa->spa_label_features;
	nvpair_t *pair;

	(void) printf("label config:\n");
	for (pair = nvlist_next_nvpair(nv, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(nv, pair)) {
		(void) printf("\t%s\n", nvpair_name(pair));
	}
}

static void
zhack_do_feature_stat(int argc, char **argv)
{
	spa_t *spa;
	objset_t *os;
	char *target;

	argc--;
	argv++;

	if (argc < 1) {
		(void) fprintf(stderr, "error: missing pool name\n");
		usage();
	}
	target = argv[0];

	zhack_spa_open(target, B_TRUE, FTAG, &spa);
	os = spa->spa_meta_objset;

	dump_obj(os, spa->spa_feat_for_read_obj, "for_read");
	dump_obj(os, spa->spa_feat_for_write_obj, "for_write");
	dump_obj(os, spa->spa_feat_desc_obj, "descriptions");
	if (spa_feature_is_active(spa, SPA_FEATURE_ENABLED_TXG)) {
		dump_obj(os, spa->spa_feat_enabled_txg_obj, "enabled_txg");
	}
	dump_mos(spa);

	spa_close(spa, FTAG);
}

static void
zhack_feature_enable_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zfeature_info_t *feature = arg;

	feature_enable_sync(spa, feature, tx);

	spa_history_log_internal(spa, "zhack enable feature", tx,
	    "name=%s flags=%u",
	    feature->fi_guid, feature->fi_flags);
}

static void
zhack_do_feature_enable(int argc, char **argv)
{
	int c;
	char *desc, *target;
	spa_t *spa;
	objset_t *mos;
	zfeature_info_t feature;
	spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	/*
	 * Features are not added to the pool's label until their refcounts
	 * are incremented, so fi_mos can just be left as false for now.
	 */
	desc = NULL;
	feature.fi_uname = "zhack";
	feature.fi_flags = 0;
	feature.fi_depends = nodeps;
	feature.fi_feature = SPA_FEATURE_NONE;

	optind = 1;
	while ((c = getopt(argc, argv, "+rd:")) != -1) {
		switch (c) {
		case 'r':
			feature.fi_flags |= ZFEATURE_FLAG_READONLY_COMPAT;
			break;
		case 'd':
			desc = strdup(optarg);
			break;
		default:
			usage();
			break;
		}
	}

	if (desc == NULL)
		desc = strdup("zhack injected");
	feature.fi_desc = desc;

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr, "error: missing feature or pool name\n");
		usage();
	}
	target = argv[0];
	feature.fi_guid = argv[1];

	if (!zfeature_is_valid_guid(feature.fi_guid))
		fatal(NULL, FTAG, "invalid feature guid: %s", feature.fi_guid);

	zhack_spa_open(target, B_FALSE, FTAG, &spa);
	mos = spa->spa_meta_objset;

	if (zfeature_is_supported(feature.fi_guid))
		fatal(spa, FTAG, "'%s' is a real feature, will not enable",
		    feature.fi_guid);
	if (0 == zap_contains(mos, spa->spa_feat_desc_obj, feature.fi_guid))
		fatal(spa, FTAG, "feature already enabled: %s",
		    feature.fi_guid);

	VERIFY0(dsl_sync_task(spa_name(spa), NULL,
	    zhack_feature_enable_sync, &feature, 5, ZFS_SPACE_CHECK_NORMAL));

	spa_close(spa, FTAG);

	free(desc);
}

static void
feature_incr_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zfeature_info_t *feature = arg;
	uint64_t refcount;

	VERIFY0(feature_get_refcount_from_disk(spa, feature, &refcount));
	feature_sync(spa, feature, refcount + 1, tx);
	spa_history_log_internal(spa, "zhack feature incr", tx,
	    "name=%s", feature->fi_guid);
}

static void
feature_decr_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zfeature_info_t *feature = arg;
	uint64_t refcount;

	VERIFY0(feature_get_refcount_from_disk(spa, feature, &refcount));
	feature_sync(spa, feature, refcount - 1, tx);
	spa_history_log_internal(spa, "zhack feature decr", tx,
	    "name=%s", feature->fi_guid);
}

static void
zhack_do_feature_ref(int argc, char **argv)
{
	int c;
	char *target;
	boolean_t decr = B_FALSE;
	spa_t *spa;
	objset_t *mos;
	zfeature_info_t feature;
	spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	/*
	 * fi_desc does not matter here because it was written to disk
	 * when the feature was enabled, but we need to properly set the
	 * feature for read or write based on the information we read off
	 * disk later.
	 */
	feature.fi_uname = "zhack";
	feature.fi_flags = 0;
	feature.fi_desc = NULL;
	feature.fi_depends = nodeps;
	feature.fi_feature = SPA_FEATURE_NONE;

	optind = 1;
	while ((c = getopt(argc, argv, "+md")) != -1) {
		switch (c) {
		case 'm':
			feature.fi_flags |= ZFEATURE_FLAG_MOS;
			break;
		case 'd':
			decr = B_TRUE;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr, "error: missing feature or pool name\n");
		usage();
	}
	target = argv[0];
	feature.fi_guid = argv[1];

	if (!zfeature_is_valid_guid(feature.fi_guid))
		fatal(NULL, FTAG, "invalid feature guid: %s", feature.fi_guid);

	zhack_spa_open(target, B_FALSE, FTAG, &spa);
	mos = spa->spa_meta_objset;

	if (zfeature_is_supported(feature.fi_guid)) {
		fatal(spa, FTAG,
		    "'%s' is a real feature, will not change refcount",
		    feature.fi_guid);
	}

	if (0 == zap_contains(mos, spa->spa_feat_for_read_obj,
	    feature.fi_guid)) {
		feature.fi_flags &= ~ZFEATURE_FLAG_READONLY_COMPAT;
	} else if (0 == zap_contains(mos, spa->spa_feat_for_write_obj,
	    feature.fi_guid)) {
		feature.fi_flags |= ZFEATURE_FLAG_READONLY_COMPAT;
	} else {
		fatal(spa, FTAG, "feature is not enabled: %s", feature.fi_guid);
	}

	if (decr) {
		uint64_t count;
		if (feature_get_refcount_from_disk(spa, &feature,
		    &count) == 0 && count == 0) {
			fatal(spa, FTAG, "feature refcount already 0: %s",
			    feature.fi_guid);
		}
	}

	VERIFY0(dsl_sync_task(spa_name(spa), NULL,
	    decr ? feature_decr_sync : feature_incr_sync, &feature,
	    5, ZFS_SPACE_CHECK_NORMAL));

	spa_close(spa, FTAG);
}

static int
zhack_do_feature(int argc, char **argv)
{
	char *subcommand;

	argc--;
	argv++;
	if (argc == 0) {
		(void) fprintf(stderr,
		    "error: no feature operation specified\n");
		usage();
	}

	subcommand = argv[0];
	if (strcmp(subcommand, "stat") == 0) {
		zhack_do_feature_stat(argc, argv);
	} else if (strcmp(subcommand, "enable") == 0) {
		zhack_do_feature_enable(argc, argv);
	} else if (strcmp(subcommand, "ref") == 0) {
		zhack_do_feature_ref(argc, argv);
	} else {
		(void) fprintf(stderr, "error: unknown subcommand: %s\n",
		    subcommand);
		usage();
	}

	return (0);
}

static void
zhack_print_vdev(spa_t *spa, const char *name, nvlist_t *nv, int depth)
{
	nvlist_t **child;
	uint_t c, children;

	vdev_stat_t *vs;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	if (nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0) {
		(void) fprintf(stderr, "  %*s%s %s\t"
		    "er=%" PRIu64 " ew=%" PRIu64 " ec=%" PRIu64,
		    depth, "", name,
		    zpool_state_to_name(vs->vs_state, vs->vs_aux),
		    vs->vs_read_errors, vs->vs_write_errors,
		    vs->vs_checksum_errors);
	} else {
		(void) fprintf(stderr, "\t%*s%s (No status)",
		    depth, "", name);
	}

	if (children == 0) {
		uint64_t guid;

		VERIFY0(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid));

		spa_vdev_state_enter(spa, SCL_NONE);
		vdev_t *vd = spa_lookup_by_guid(spa, guid, B_TRUE);
		VERIFY(vd != NULL);

		/*
		 * See vdev_resilver_needed; we skip the vdev_writeable test
		 * because this is just for printout (and it might be useful to
		 * show the DTLs for OFFLINE/UNAVAIL devices).
		 */
		mutex_enter(&vd->vdev_dtl_lock);

		boolean_t hasdtl =
		    !range_tree_is_empty(vd->vdev_dtl[DTL_MISSING]);

		if (hasdtl) {
			uint64_t dtlmin, dtlmax;

			dtlmin = range_tree_min(vd->vdev_dtl[DTL_MISSING]) - 1;
			dtlmax = range_tree_max(vd->vdev_dtl[DTL_MISSING]);

			(void) fprintf(stderr, " dtl=[%" PRIu64 ",%" PRIu64 "]",
			    dtlmin, dtlmax);
		}

		mutex_exit(&vd->vdev_dtl_lock);
		spa_vdev_state_exit(spa, NULL, 0);
	}

	(void) fprintf(stderr, "\n");

	for (c = 0; c < children; c++) {
		char *vname;

		vname = zpool_vdev_name(NULL, NULL, child[c],
		    VDEV_NAME_TYPE_ID);
		zhack_print_vdev(spa, vname, child[c], depth + 2);
		free(vname);
	}
}

static void
zhack_print_spa_vdevs(spa_t *spa)
{
	nvlist_t *nvroot;
	nvlist_t *config;

	config = spa_config_generate(spa, NULL, -1, 1);
	VERIFY(config);

	VERIFY0(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvroot));
	zhack_print_vdev(spa, g_importargs.poolname, nvroot, 0);

	nvlist_free(config);
}

static boolean_t
zhack_scrub_optu64(char *arg, uint64_t *v)
{
	char *endptr = NULL;

	*v = strtoul(arg, &endptr, 0);
	return (errno == 0) && (*endptr == '\0');
}

static int
zhack_do_scrub(int argc, char **argv)
{
	int verbose = 0;
	int do_ddt_reset = 0;
	int do_resilver = 0;
	int do_restart = 0;
	int do_pause_stop = 0;
	int no_spawn = 0;
	spa_t *spa = NULL;
	int c;
	uint64_t scan_op_time = 0;

	(void) setlocale(LC_ALL, "");

	/* Disable reference tracking debugging */
	reference_tracking_enable = B_FALSE;

	/* Disable prefetch during scan */
	zfs_no_scrub_prefetch = B_TRUE;

	while ((c = getopt(argc, argv, "D:EG:H:M:O:PRS:Ti:nprst:vx:")) != -1) {
		switch (c) {
		case 'D':
			/* How much of the DDT are we scanning? */
		{
			uint64_t class;
			if (zhack_scrub_optu64(optarg, &class) &&
			    (class < DDT_CLASSES)) {
				zfs_scrub_ddt_class_max = class;
			} else {
				fatal(NULL, FTAG, "DDT class must be between "
				    "0 and %d, inclusive", DDT_CLASSES-1);
			}
		}
		break;
		case 'E':
			/* Forcibly reset DDT class max after import */
			do_ddt_reset++;
			break;
		case 'G':
		{
			uint64_t gap;
			if (zhack_scrub_optu64(optarg, &gap)) {
				zfs_scan_max_ext_gap = gap;
			} else {
				fatal(NULL, FTAG, "Bad range tree gap (-G)");
			}
		}
		break;
		case 'H':
		{
			uint64_t fact;
			if (zhack_scrub_optu64(optarg, &fact) &&
			    (fact >= 1) && (fact <= 1000)) {
				zfs_scan_mem_lim_fact = fact;
			} else {
				fatal(NULL, FTAG, "Bad hard factor (-H)");
			}
		}
		break;
		case 'M':
		{
			uint64_t mem;
			if (zhack_scrub_optu64(optarg, &mem) &&
			    (mem >= (1 << 15)) &&
			    (mem <= sysconf(_SC_PHYS_PAGES))) {
				physmem = mem;
			} else {
				fatal(NULL, FTAG,
				    "Bad physical memory override (-M)");
			}
		}
		break;
		case 'O':
			if (set_global_var(optarg) != 0)
				usage();
			break;
		case 'P':
			zfs_no_scrub_prefetch = B_FALSE;
			break;
		case 'R':
			do_restart++;
			break;
		case 'S':
		{
			uint64_t fact;
			if (zhack_scrub_optu64(optarg, &fact) &&
			    (fact >= 1) &&
			    (fact <= 1000)) {
				zfs_scan_mem_lim_soft_fact = fact;
			} else {
				fatal(NULL, FTAG, "Bad soft factor (-S)");
			}
		}
		break;
		case 'T':
			reference_tracking_enable = B_TRUE;
			break;
		case 'i':
		{
			extern int zfs_scan_checkpoint_intval;
			uint64_t intval;
			if (zhack_scrub_optu64(optarg, &intval)) {
				zfs_scan_checkpoint_intval = intval;
			} else {
				fatal(NULL, FTAG, "Bad scan interval (-i)");
			}
		}
		break;
		case 'n':
			no_spawn++;
			break;
		case 'p':
			do_pause_stop = 1;
			break;
		case 'r':
			do_resilver++;
			break;
		case 's':
			do_pause_stop = 2;
			break;
		case 't':
			if (!zhack_scrub_optu64(optarg, &scan_op_time)) {
				fatal(NULL, FTAG, "Bad scan op time (-t)");
			}
		case 'v':
			verbose++;
			break;
		case 'x':
		{
			uint64_t intval;
			if (zhack_scrub_optu64(optarg, &intval) &&
			    (intval > 4)) {
				zfs_txg_timeout = intval;
			} else {
				fatal(NULL, FTAG, "Bad txg timeout (-x)");
			}
		}
		break;
		case '?':
			fatal(NULL, FTAG, "invalid option '%c'", optopt);
		}
	}

	if (optind == argc) {
		fatal(NULL, FTAG, "Need pool name");
	}
	if (optind + 1 < argc) {
		(void) fprintf(stderr,
		    "WARNING: Discarding excess arguments\n");
	}
	if (no_spawn && (do_resilver || do_restart)) {
		fatal(NULL, FTAG, "-n is incompatible with -[Rr]");
	}

	if ((scan_op_time != 0) && (((int)scan_op_time) < 1000)) {
		fatal(NULL, FTAG, "Bad scan op time (-t)");
	}

	if (verbose && (g_importargs.paths != 0)) {
		int sdix = 0;
		(void) fprintf(stderr, "Will search:\n");
		for (sdix = 0; sdix < g_importargs.paths; sdix++) {
			(void) fprintf(stderr, "\t%s\n",
			    g_importargs.path[sdix]);
		}
	}

	g_importargs.poolname = argv[optind];
	zhack_spa_open(argv[optind], B_FALSE, FTAG, &spa);

	if (verbose) {
		(void) fprintf(stderr, "Found pool; vdev tree:\n");
		zhack_print_spa_vdevs(spa);
	}

	if (do_pause_stop) {
		int err = 0;
		switch (do_pause_stop) {
		case 1:
			err = spa_scrub_pause_resume(spa, POOL_SCRUB_PAUSE);
			break;
		case 2:
			if (do_resilver) {
				err = dsl_scan_cancel(spa_get_dsl(spa));
			} else {
				err = spa_scan_stop(spa);
			}
		}
		if (err != 0) {
			(void) fprintf(stderr,
			    "Cannot stop/pause; error %d\n", err);
		}
		goto out;
	}

	if (do_restart) {
		if (verbose) {
			(void) fprintf(stderr,
			    "First, cancelling any existing scrub...\n");
		}
		dsl_scan_cancel(spa_get_dsl(spa));
	}

	if (no_spawn) {
		if (spa_get_dsl(spa)->dp_scan->scn_phys.scn_state ==
		    DSS_FINISHED) {
			(void) fprintf(stderr, "No scrub to resume.\n");
			goto out;
		}
	} else {
		if (verbose) {
			(void) fprintf(stderr, "Kicking off %s...\n",
			    do_resilver ? "resilver" : "scrub");
		}
		spa_scan(spa,
		    do_resilver ? POOL_SCAN_RESILVER : POOL_SCAN_SCRUB);
	}

	if (verbose)
		(void) fprintf(stderr, "Awaiting initial txg sync...\n");
	txg_wait_synced(spa_get_dsl(spa), 0);

	/*
	 * Let the first few transactions happen with the default times; this
	 * likely lets us get through the initial sync faster.
	 */
	if (scan_op_time != 0) {
		extern int zfs_scrub_min_time_ms;
		extern int zfs_resilver_min_time_ms;

		zfs_scrub_min_time_ms = zfs_resilver_min_time_ms
		    = scan_op_time;
	}

	/*
	 * dsl_scan_setup_sync() has its own ideas about DDT behavior and
	 * doesn't expose hooks for much second-guessing, which means we might
	 * start off running the wrong flavor for a little while, but since the
	 * scan code bases its behavior off scn_ddt_class_max dynamically, it
	 * should soon jump to doing the right thing.
	 */
	if (do_ddt_reset) {
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_WRITER);

		dsl_scan_t *scn = spa_get_dsl(spa)->dp_scan;
		if (scn->scn_phys.scn_ddt_class_max < zfs_scrub_ddt_class_max) {
			if (verbose) {
				(void) fprintf(stderr,
				    "Forcibly resetting DDT scan class\n");
			}
			scn->scn_phys.scn_ddt_class_max =
			    zfs_scrub_ddt_class_max;
		} else if (scn->scn_phys.scn_ddt_class_max ==
		    zfs_scrub_ddt_class_max) {
			if (verbose) {
				(void) fprintf(stderr,
				    "No need to reset DDT scan class\n");
			}
		} else {
			if (verbose) {
				(void) fprintf(stderr,
				    "Unsafe to reset DDT scan class; won't!\n");
			}
		}

		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
	}

	{
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);

		dsl_scan_t *scn = spa_get_dsl(spa)->dp_scan;
		dsl_scan_phys_t *scnp = &scn->scn_phys;
		uint64_t funcix = scnp->scn_func;

		char *func;
		switch (funcix) {
		case POOL_SCAN_NONE:
			func = "none";
			break;
		case POOL_SCAN_SCRUB:
			func = "scrub";
			break;
		case POOL_SCAN_RESILVER:
			func = "resilver";
			break;
		default:
			func = "unknown";
			break;
		}

		(void) fprintf(stderr, "Info: func=%s toex=%'" PRIu64
		    " mintxg=%" PRIu64 " maxtxg=%" PRIu64 " ddtclass=%" PRIu64
		    "\n",
		    func, scnp->scn_to_examine,
		    scnp->scn_min_txg, scnp->scn_max_txg,
		    scnp->scn_ddt_class_max);

		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
	}

	uint64_t state;
	do {
		/*
		 * While not strictly necessary, grabbing a transaction here
		 * seems to give us a more even pacing of outputs.
		 */
		dmu_tx_t *tx;
		tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
		VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

		uint64_t now = (uint64_t)(time(NULL));
		uint64_t txg = dmu_tx_get_txg(tx);

		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);

		dsl_pool_t *dp = spa_get_dsl(spa);
		dsl_scan_t *scn = dp->dp_scan;
		dsl_scan_phys_t *scnp = &scn->scn_phys;

		uint64_t issued = scn->scn_issued_before_pass
		    + spa->spa_scan_pass_issued;

		state = scnp->scn_state;

		/*
		 * Announce almost everything of possible interest.
		 *
		 * examined, issued, and repair are monotone nondecreasing, so
		 * we do not pad with whitespace (by contrast to pending).
		 */
		(void) fprintf(stderr,
		    "Scan: time=%" PRIu64 " txg=%-6" PRIu64
		    " clr=%d ckpt=%d err=%" PRIu64
		    " exd=%'" PRIu64 " (%.2f%%)"
		    " pend=%'-16" PRIu64 " (%05.2f%%)"
		    " iss=%'" PRIu64 " (%.2f%%)"
		    " repair=%'" PRIu64 " (%.2f%%)"
		    " ddtbk=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIx64
		    " bk=%" PRIu64 "/%" PRIu64 "/%" PRId64 "/%" PRIu64
		    "\n",
		    now, txg,
		    scn->scn_clearing, scn->scn_checkpointing, scnp->scn_errors,
		    scnp->scn_examined,
		    (float)scnp->scn_examined * 100 / scnp->scn_to_examine,
		    scn->scn_bytes_pending,
		    (float)scn->scn_bytes_pending * 100 / scnp->scn_to_examine,
		    issued,
		    (float)issued * 100 / scnp->scn_to_examine,
		    scnp->scn_processed,
		    (float)scnp->scn_processed * 100 / scnp->scn_to_examine,
		    scnp->scn_ddt_bookmark.ddb_class,
		    scnp->scn_ddt_bookmark.ddb_type,
		    scnp->scn_ddt_bookmark.ddb_checksum,
		    scnp->scn_ddt_bookmark.ddb_cursor,
		    scnp->scn_bookmark.zb_objset,
		    scnp->scn_bookmark.zb_object,
		    scnp->scn_bookmark.zb_level,
		    scnp->scn_bookmark.zb_blkid);

		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);

		/*
		 * Report at most once per txg, as the state cannot change
		 * without a txg finishing sync phase.  Because we don't have
		 * txs in every txg, we probably won't end up reporting as
		 * frequently as txgs sync, but it's just diagnostics.
		 *
		 * Either the pipeline is driving itself due to the ongoing scan
		 * or this will signal the sync thread to wake up.
		 */
		dmu_tx_commit(tx);
		txg_wait_synced(dp, txg);

	} while (state == DSS_SCANNING);

	if (verbose) {
		(void) fprintf(stderr, "Shutting down; pool state is now...\n");
		zhack_print_spa_vdevs(spa);
	}

out:
	spa_close(spa, FTAG);

	return (0);
}

#define	MAX_NUM_PATHS 1024

int
main(int argc, char **argv)
{
	extern void zfs_prop_init(void);

	char *path[MAX_NUM_PATHS];
	const char *subcommand;
	int rv = 0;
	int c;

	g_importargs.path = path;

	dprintf_setup(&argc, argv);
	zfs_prop_init();

	while ((c = getopt(argc, argv, "+c:d:")) != -1) {
		switch (c) {
		case 'c':
			g_importargs.cachefile = optarg;
			break;
		case 'd':
			assert(g_importargs.paths < MAX_NUM_PATHS);
			g_importargs.path[g_importargs.paths++] = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv += optind;
	optind = 1;

	if (argc == 0) {
		(void) fprintf(stderr, "error: no command specified\n");
		usage();
	}

	subcommand = argv[0];

	if (strcmp(subcommand, "feature") == 0) {
		rv = zhack_do_feature(argc, argv);
	} else if (strcmp(subcommand, "scrub") == 0) {
		rv = zhack_do_scrub(argc, argv);
	} else {
		(void) fprintf(stderr, "error: unknown subcommand: %s\n",
		    subcommand);
		usage();
	}

	if (!g_readonly && spa_export(g_pool, NULL, B_TRUE, B_FALSE) != 0) {
		fatal(NULL, FTAG, "pool export failed; "
		    "changes may not be committed to disk\n");
	}

	kernel_fini();

	return (rv);
}
