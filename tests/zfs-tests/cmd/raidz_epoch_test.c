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
 * Userspace driver for the RAIDZ parity-epoch tests.  A pool lives on the
 * file vdevs <dir>/v0 .. v<N-1> and is driven through libzpool, so no kernel
 * module is involved.  Every step is a separate process, which is what keeps
 * the ARC from hiding what was written to the files; the test scripts put
 * zhack (the parity-epoch table writer) and file corruption in between.
 *
 *   create <dir> <pool> <children> <parity> [<marker-parity>]
 *	Create an empty raidz pool with every feature enabled.  With a marker
 *	parity, also give the vdev a parity-epoch table that ends at that
 *	parity, plus a completion marker for it with a matching digest, the way
 *	a label written by a build that supports in-place re-parity would.
 *   write <dir> <pool> <blocksize>
 *	Write one block of reproducible data to a new MOS object and print its
 *	object number, the offsets of its DVAs on vdev 0 and its birth txg.
 *	The MOS gives every block several copies.
 *   corrupt <dir> <offsets> <psize> <width> <parity> <ashift> <column>...
 *	Overwrite the given data columns of the block copies at the
 *	comma-separated <offsets> on the file vdevs, as laid out for <width>
 *	children and <parity> parity columns.
 *   read <dir> <pool> <object> <blocksize>
 *	Import the pool, read the object back and compare it with what
 *	"write" stored.  Exits 0 if the read ran to completion, whatever its
 *	result, so that a failed assertion is told apart from a read error.
 *   state <dir> <pool> open|offline|fault
 *	Import the pool and report the state of the top-level vdev: as the
 *	pool was opened, after offlining two children, or after faulting two
 *	children.
 *
 * Results are printed as "RESULT ..." lines for the test scripts to read.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/txg.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zfeature.h>
#include <zfeature_common.h>
#include <libzutil.h>
#include <libzpool.h>

#define	VDEV_FILE_SIZE	(128ULL << 20)
#define	ASHIFT		12
#define	PATH_BUF	512

/* The first parity-epoch boundary of a forged table, far past any txg. */
#define	FORGED_EPOCH_START	1000000ULL

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: raidz_epoch_test create <dir> <pool> <children> <parity> "
	    "[<marker-parity>]\n"
	    "       raidz_epoch_test write <dir> <pool> <blocksize>\n"
	    "       raidz_epoch_test corrupt <dir> <offsets> <psize> <width> "
	    "<parity> <ashift> <column>...\n"
	    "       raidz_epoch_test read <dir> <pool> <object> <blocksize>\n"
	    "       raidz_epoch_test state <dir> <pool> open|offline|fault\n");
	exit(2);
}

static void
vdev_path(char *buf, size_t len, const char *dir, int child)
{
	(void) snprintf(buf, len, "%s/v%d", dir, child);
}

static nvlist_t *
file_vdev(const char *path)
{
	nvlist_t *nv = fnvlist_alloc();

	fnvlist_add_string(nv, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(nv, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_ASHIFT, ASHIFT);
	return (nv);
}

static int
make_file(const char *path, uint64_t size)
{
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);

	if (fd == -1)
		return (errno);
	if (ftruncate(fd, size) != 0) {
		int e = errno;
		(void) close(fd);
		return (e);
	}
	(void) close(fd);
	return (0);
}

/*
 * Import <pool> from the files in <dir> and open it.
 */
static int
open_pool(const char *dir, const char *pool, boolean_t readonly, spa_t **spap)
{
	importargs_t ia;
	char *paths[1];
	nvlist_t *config = NULL, *props = NULL;
	int err;

	libpc_handle_t lpch = {
		.lpc_lib_handle = NULL,
		.lpc_ops = &libzpool_config_ops,
		.lpc_printerr = B_TRUE
	};

	memset(&ia, 0, sizeof (ia));
	paths[0] = (char *)dir;
	ia.path = paths;
	ia.paths = 1;
	ia.can_be_active = B_TRUE;

	err = zpool_find_config(&lpch, pool, &config, &ia);
	if (err != 0 || config == NULL)
		return (err != 0 ? err : ENOENT);

	if (readonly) {
		props = fnvlist_alloc();
		fnvlist_add_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_READONLY), 1);
	}
	err = spa_import((char *)pool, config, props,
	    readonly ? ZFS_IMPORT_SKIP_MMP : ZFS_IMPORT_NORMAL);
	fnvlist_free(config);
	if (props != NULL)
		fnvlist_free(props);
	if (err != 0 && err != EEXIST)
		return (err);

	return (spa_open(pool, spap, FTAG));
}

static void
close_pool(spa_t *spa, const char *pool)
{
	spa_close(spa, FTAG);
	(void) spa_export(pool, NULL, B_FALSE, B_FALSE);
}

/*
 * Reproducible, incompressible block contents: the reader recomputes them.
 */
static void
fill_pattern(uint64_t *buf, size_t size)
{
	uint64_t x = 0x9e3779b97f4a7c15ULL;

	for (size_t i = 0; i < size / sizeof (uint64_t); i++) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		buf[i] = x;
	}
}

/*
 * The digest a build with in-place re-parity stores next to its completion
 * marker: FNV-1a over the table entries whose parity is at most the marker's.
 */
static uint64_t
epoch_digest(const uint64_t *tbl, uint64_t nent, uint64_t max_parity)
{
	uint64_t h = 1469598103934665603ULL;

	for (uint64_t i = 0; i < nent; i++) {
		if (tbl[3 * i + 2] > max_parity)
			continue;
		for (int k = 0; k < 3; k++) {
			h ^= tbl[3 * i + k];
			h *= 1099511628211ULL;
		}
	}
	return (h);
}

static int
do_create(const char *dir, const char *pool, int children, int parity,
    int marker)
{
	nvlist_t **child, *raid, *root, *props;
	char path[PATH_BUF];
	spa_t *spa;
	int err;

	child = calloc(children, sizeof (nvlist_t *));
	for (int c = 0; c < children; c++) {
		vdev_path(path, sizeof (path), dir, c);
		err = make_file(path, VDEV_FILE_SIZE);
		if (err != 0) {
			(void) fprintf(stderr, "make_file %s: %d\n", path, err);
			return (err);
		}
		child[c] = file_vdev(path);
	}

	raid = fnvlist_alloc();
	fnvlist_add_string(raid, ZPOOL_CONFIG_TYPE, VDEV_TYPE_RAIDZ);
	fnvlist_add_uint64(raid, ZPOOL_CONFIG_NPARITY, parity);
	fnvlist_add_uint64(raid, ZPOOL_CONFIG_IS_LOG, 0);
	fnvlist_add_nvlist_array(raid, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)child, children);

	if (marker != 0) {
		uint64_t tbl[6] = { 0, children, parity,
		    FORGED_EPOCH_START, children, marker };

		fnvlist_add_uint64_array(raid, ZPOOL_CONFIG_RAIDZ_PARITY_EPOCHS,
		    tbl, 6);
		fnvlist_add_uint64(raid, "com.rrmvp:raidz_reparity_parity",
		    marker);
		fnvlist_add_uint64(raid,
		    "com.rrmvp:raidz_reparity_completion_txg", 1);
		fnvlist_add_uint64(raid,
		    "com.rrmvp:raidz_reparity_epoch_digest",
		    epoch_digest(tbl, 2, marker));
	}

	root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)&raid, 1);

	/* spa_create() enables no feature by itself; ask for all of them. */
	props = fnvlist_alloc();
	for (int i = 0; i < SPA_FEATURES; i++) {
		char buf[256];

		if (!spa_feature_table[i].fi_zfs_mod_supported)
			continue;
		(void) snprintf(buf, sizeof (buf), "feature@%s",
		    spa_feature_table[i].fi_uname);
		fnvlist_add_uint64(props, buf, 0);
	}

	err = spa_create(pool, root, props, NULL, NULL, NULL);
	fnvlist_free(root);
	fnvlist_free(raid);
	for (int c = 0; c < children; c++)
		fnvlist_free(child[c]);
	free(child);
	fnvlist_free(props);
	(void) printf("RESULT create rc=%d\n", err);
	if (err != 0)
		return (err);

	VERIFY0(spa_open(pool, &spa, FTAG));
	txg_wait_synced(spa_get_dsl(spa), 0);
	close_pool(spa, pool);
	return (0);
}

static int
do_write(const char *dir, const char *pool, uint64_t blksz)
{
	spa_t *spa;
	objset_t *mos;
	dmu_tx_t *tx;
	dmu_buf_t *db;
	blkptr_t bp;
	uint64_t obj;
	uint64_t *buf;
	int err;

	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err != 0) {
		(void) printf("RESULT write rc=%d\n", err);
		return (err);
	}
	mos = spa_meta_objset(spa);

	buf = malloc(blksz);
	fill_pattern(buf, blksz);

	tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, blksz);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	obj = dmu_object_alloc(mos, DMU_OT_PLAIN_FILE_CONTENTS, blksz,
	    DMU_OT_NONE, 0, tx);
	dmu_write(mos, obj, 0, blksz, buf, tx, DMU_READ_NO_PREFETCH);
	dmu_tx_commit(tx);
	free(buf);
	txg_wait_synced(spa_get_dsl(spa), 0);

	VERIFY0(dmu_buf_hold(mos, obj, 0, FTAG, &db, DMU_READ_NO_PREFETCH));
	bp = *dmu_buf_get_blkptr(db);
	dmu_buf_rele(db, FTAG);

	(void) printf("RESULT write rc=0 object=%llu psize=%llu birth=%llu "
	    "offsets=", (u_longlong_t)obj, (u_longlong_t)BP_GET_PSIZE(&bp),
	    (u_longlong_t)BP_GET_PHYSICAL_BIRTH(&bp));
	for (int d = 0; d < BP_GET_NDVAS(&bp); d++) {
		VERIFY0(DVA_GET_VDEV(&bp.blk_dva[d]));
		(void) printf("%s%llu", d == 0 ? "" : ",",
		    (u_longlong_t)DVA_GET_OFFSET(&bp.blk_dva[d]));
	}
	(void) printf("\n");

	close_pool(spa, pool);
	return (0);
}

static int
corrupt_column(const char *dir, raidz_col_t *rc)
{
	char path[PATH_BUF];
	uint8_t *data = malloc(rc->rc_size);
	uint64_t off = rc->rc_offset + VDEV_LABEL_START_SIZE;
	int fd, err = 0;

	vdev_path(path, sizeof (path), dir, rc->rc_devidx);
	fd = open(path, O_RDWR);
	if (fd == -1 || pread(fd, data, rc->rc_size, off) != rc->rc_size) {
		(void) fprintf(stderr, "cannot read %s\n", path);
		err = 1;
	} else {
		for (uint32_t b = 0; b < rc->rc_size; b++)
			data[b] = ~data[b];
		if (pwrite(fd, data, rc->rc_size, off) != rc->rc_size) {
			(void) fprintf(stderr, "cannot write %s\n", path);
			err = 1;
		}
	}
	if (fd != -1)
		(void) close(fd);
	free(data);
	return (err);
}

static int
do_corrupt(const char *dir, char *offsets, uint64_t psize, uint64_t width,
    uint64_t parity, uint64_t ashift, int ncols, char **cols)
{
	char *save = NULL;

	kernel_init(SPA_MODE_READ);

	for (char *o = strtok_r(offsets, ",", &save); o != NULL;
	    o = strtok_r(NULL, ",", &save)) {
		zio_t zio = {0};
		raidz_map_t *rm;
		raidz_row_t *rr;

		zio.io_size = P2ROUNDUP_TYPED(psize, 1ULL << ashift, uint64_t);
		zio.io_offset = strtoull(o, NULL, 10);
		zio.io_type = ZIO_TYPE_READ;
		zio.io_abd = abd_alloc_for_io(zio.io_size, B_FALSE);

		rm = vdev_raidz_map_alloc(&zio, ashift, width, parity);
		if (rm->rm_nrows != 1) {
			(void) fprintf(stderr, "block spans %d rows, want 1\n",
			    (int)rm->rm_nrows);
			return (1);
		}
		rr = rm->rm_row[0];

		for (int i = 0; i < ncols; i++) {
			int d = atoi(cols[i]);

			if (d < 0 || d >= rr->rr_cols - rr->rr_firstdatacol) {
				(void) fprintf(stderr, "no column %d\n", d);
				return (1);
			}
			raidz_col_t *rc = &rr->rr_col[rr->rr_firstdatacol + d];

			if (corrupt_column(dir, rc) != 0)
				return (1);
			(void) printf("RESULT corrupt offset=%s data=%d "
			    "child=%d\n", o, d, rc->rc_devidx);
		}

		vdev_raidz_map_free(rm);
		abd_free(zio.io_abd);
	}
	return (0);
}

static int
do_read(const char *dir, const char *pool, uint64_t obj, uint64_t blksz)
{
	spa_t *spa;
	uint64_t *buf, *want;
	int err, match;

	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err != 0) {
		(void) printf("RESULT read rc=%d import=1\n", err);
		return (err);
	}

	buf = malloc(blksz);
	want = malloc(blksz);
	fill_pattern(want, blksz);
	memset(buf, 0, blksz);

	err = dmu_read(spa_meta_objset(spa), obj, 0, blksz, buf,
	    DMU_READ_NO_PREFETCH);
	match = (err == 0 && memcmp(buf, want, blksz) == 0);
	(void) printf("RESULT read rc=%d match=%d\n", err, match);

	free(buf);
	free(want);
	close_pool(spa, pool);
	return (0);
}

static const char *
state_name(vdev_state_t state)
{
	static const char *const names[] = { "UNKNOWN", "CLOSED", "OFFLINE",
	    "REMOVED", "CANT_OPEN", "FAULTED", "DEGRADED", "ONLINE" };

	return (state < ARRAY_SIZE(names) ? names[state] : "?");
}

static void
report_state(spa_t *spa, const char *what)
{
	vdev_t *tvd;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	tvd = spa->spa_root_vdev->vdev_child[0];
	(void) printf("RESULT state %s top=%s", what,
	    state_name(tvd->vdev_state));
	for (int c = 0; c < tvd->vdev_children; c++) {
		vdev_t *cvd = tvd->vdev_child[c];

		if (cvd->vdev_state != VDEV_STATE_HEALTHY) {
			(void) printf(" child%d=%s", c,
			    state_name(cvd->vdev_state));
		}
	}
	(void) printf("\n");
	spa_config_exit(spa, SCL_STATE, FTAG);
}

static int
do_state(const char *dir, const char *pool, const char *what)
{
	spa_t *spa;
	vdev_t *tvd;
	uint64_t guid[2];
	int err;

	/*
	 * Offlining needs a writable pool.  Faulting more children than the
	 * vdev tolerates would leave a writable pool with nowhere to sync to,
	 * so everything else is done read-only.
	 */
	err = open_pool(dir, pool, strcmp(what, "offline") != 0, &spa);
	(void) printf("RESULT state import rc=%d\n", err);
	if (err != 0)
		return (0);

	tvd = spa->spa_root_vdev->vdev_child[0];
	guid[0] = tvd->vdev_child[0]->vdev_guid;
	guid[1] = tvd->vdev_child[1]->vdev_guid;

	if (strcmp(what, "open") == 0) {
		report_state(spa, "open");
	} else if (strcmp(what, "offline") == 0) {
		int e0 = vdev_offline(spa, guid[0], 0);
		int e1 = vdev_offline(spa, guid[1], 0);

		(void) printf("RESULT state offline first=%d second=%d\n",
		    e0, e1);
		report_state(spa, "offline");
	} else if (strcmp(what, "fault") == 0) {
		for (int c = 0; c < 2; c++) {
			(void) spa_vdev_state_enter(spa, SCL_NONE);
			vdev_set_state(tvd->vdev_child[c], B_FALSE,
			    VDEV_STATE_FAULTED, VDEV_AUX_ERR_EXCEEDED);
			(void) spa_vdev_state_exit(spa, NULL, 0);
			report_state(spa, c == 0 ? "fault1" : "fault2");
		}
	} else {
		usage();
	}

	close_pool(spa, pool);
	return (0);
}

int
main(int argc, char **argv)
{
	int rv;

	if (argc < 2)
		usage();

	/* Keep the pools of the test out of the host's cache file. */
	if (argc > 2 && asprintf((char **)&spa_config_path,
	    "%s/zpool.cache", argv[2]) == -1)
		return (1);

	if (strcmp(argv[1], "corrupt") == 0) {
		if (argc < 9)
			usage();
		rv = do_corrupt(argv[2], argv[3],
		    strtoull(argv[4], NULL, 10), strtoull(argv[5], NULL, 10),
		    strtoull(argv[6], NULL, 10), strtoull(argv[7], NULL, 10),
		    argc - 8, argv + 8);
		kernel_fini();
		return (rv != 0);
	}

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	if (strcmp(argv[1], "create") == 0 && argc >= 6) {
		rv = do_create(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]),
		    argc >= 7 ? atoi(argv[6]) : 0);
	} else if (strcmp(argv[1], "write") == 0 && argc == 5) {
		rv = do_write(argv[2], argv[3], strtoull(argv[4], NULL, 10));
	} else if (strcmp(argv[1], "read") == 0 && argc == 6) {
		rv = do_read(argv[2], argv[3], strtoull(argv[4], NULL, 10),
		    strtoull(argv[5], NULL, 10));
	} else if (strcmp(argv[1], "state") == 0 && argc == 5) {
		rv = do_state(argv[2], argv[3], argv[4]);
	} else {
		usage();
	}
	kernel_fini();
	return (rv != 0);
}
