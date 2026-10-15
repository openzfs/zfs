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
 *	parity, also store a parity-epoch table that ends at that parity, the
 *	way zhack does, and put a completion marker for that parity, with a
 *	matching digest, in the config the pool is created from, as software
 *	with an in-place re-parity would; this code keeps neither, so they
 *	never reach the labels or the MOS config.
 *   forge <dir> <pool> <children> <parity> create|add
 *	Give a raidz vdev whose config carries a parity-epoch table, as only a
 *	hand-made config can, to pool creation, or add it to the pool that
 *	"create" made in <dir>.  Print the result and the number of top-level
 *	vdevs of the pool.
 *   rootparse <dir> <pool>
 *	Parse the vdev tree of the label of <dir>/v0 the way the root pool
 *	import of FreeBSD does before it opens the pool, outside spa_load(),
 *	and print the result, the size of the parity-epoch table in the label
 *	and the number of entries the parse put in force.
 *   label <dir> <pool> [now]
 *	Give the raidz vdev of the pool a parity-epoch table that changes the
 *	parity at a txg the pool never reaches, in core and in the config only,
 *	so that the labels and the MOS config carry it and the vdev ZAP does
 *	not: no table the setter stores looks like that.  With "now", the
 *	parity changes at the first txg the setter would allow instead, and a
 *	block is written in that epoch.  Print the start and the birth txg of
 *	that block.
 *   label <dir> <pool> at <txgs> <width> <parity> <last>
 *	The same, with an epoch of that width and parity that starts <txgs>
 *	after the txg T of the sync task, and with the pool left as a crash
 *	right after T + <last> would leave it, with blocks written until then:
 *	that is the last synced txg the next import finds, unless a block went
 *	to a later txg.  Print T too.
 *   recover <dir> <pool>
 *	Import the pool writable as zpool import -F does, rewinding it by the
 *	few txgs that allows if the latest cannot be imported, and export it.
 *   checkpoint <dir> <pool>
 *	Take a checkpoint of the pool.
 *   crash <dir> <pool> [<txg>|checkpoint]
 *	Import the pool writable, rewound to <txg> or before as zpool import -T
 *	does, or to its checkpoint, if either is given, and leave the disks as
 *	a crash right after the first txg the import writes would: nothing
 *	written after it reaches the labels or the uberblocks, and the process
 *	ends without an export.  Print the result of the import and that txg,
 *	or 0 if the import synced it before it could be told.
 *   write <dir> <pool> <blocksize>
 *	Write one block of reproducible data to a new MOS object and print its
 *	object number, the offsets of its DVAs on vdev 0 and its birth txg.
 *	The MOS gives every block several copies.
 *   put <dir> <pool> <blocksize>
 *	Write one block of reproducible data to a new object of the root
 *	dataset of the pool, where zdb -f can show its layout, and print its
 *	object number and its birth txg.
 *   corrupt <dir> <offsets> <psize> <width> <parity> <ashift> <column>...
 *	Overwrite the given data columns of the block copies at the
 *	comma-separated <offsets> on the file vdevs, as laid out for <width>
 *	children and <parity> parity columns.
 *   read <dir> <pool> <object> <blocksize> [readonly]
 *	Import the pool, read the object back and compare it with what
 *	"write" stored.  Exits 0 if the read ran to completion, whatever its
 *	result, so that a failed assertion is told apart from a read error.
 *	With "readonly", import without writing the pool.
 *   state <dir> <pool> open|offline|fault
 *	Import the pool and report the state of the top-level vdev: as the
 *	pool was opened, after offlining two children, or after faulting two
 *	children.
 *   boundary <dir> <pool>
 *	In one sync task, on the raidz vdev of a pool without a parity-epoch
 *	table, add an epoch that starts at the txg T of the task, then at
 *	T+1, T+2 and T+3: print what the check of the setter says of each in
 *	syncing context, what the setter does with each in that order, and
 *	the table the vdev ends up with.  The first start allowed is T+3.
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
#include <sys/dsl_synctask.h>
#include <sys/txg.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zfeature.h>
#include <sys/spa_checkpoint.h>
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
	    "       raidz_epoch_test forge <dir> <pool> <children> <parity> "
	    "create|add\n"
	    "       raidz_epoch_test rootparse <dir> <pool>\n"
	    "       raidz_epoch_test label <dir> <pool> [now]\n"
	    "       raidz_epoch_test label <dir> <pool> at <txgs> <width> "
	    "<parity> <last>\n"
	    "       raidz_epoch_test recover <dir> <pool>\n"
	    "       raidz_epoch_test checkpoint <dir> <pool>\n"
	    "       raidz_epoch_test crash <dir> <pool> [<txg>|checkpoint]\n"
	    "       raidz_epoch_test write <dir> <pool> <blocksize>\n"
	    "       raidz_epoch_test put <dir> <pool> <blocksize>\n"
	    "       raidz_epoch_test corrupt <dir> <offsets> <psize> <width> "
	    "<parity> <ashift> <column>...\n"
	    "       raidz_epoch_test read <dir> <pool> <object> <blocksize> "
	    "[readonly]\n"
	    "       raidz_epoch_test state <dir> <pool> open|offline|fault\n"
	    "       raidz_epoch_test boundary <dir> <pool>\n");
	exit(2);
}

static void
vdev_path(char *buf, size_t len, const char *dir, int child)
{
	(void) snprintf(buf, len, "%s/v%d", dir, child);
}

/* The files of a raidz vdev added to the pool: <dir>/a0 .. a<N-1>. */
static void
added_path(char *buf, size_t len, const char *dir, int child)
{
	(void) snprintf(buf, len, "%s/a%d", dir, child);
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

/*
 * A raidz vdev of new files, named by pathf, with the table in its config
 * if there is one.
 */
static nvlist_t *
raidz_config(const char *dir, int children, int parity,
    void (*pathf)(char *, size_t, const char *, int), const uint64_t *tbl,
    uint_t tbl_len)
{
	nvlist_t **child;
	nvlist_t *raid;
	char path[PATH_BUF];
	int err;

	child = calloc(children, sizeof (nvlist_t *));
	for (int c = 0; c < children; c++) {
		pathf(path, sizeof (path), dir, c);
		err = make_file(path, VDEV_FILE_SIZE);
		if (err != 0) {
			(void) fprintf(stderr, "make_file %s: %d\n", path, err);
			exit(1);
		}
		child[c] = file_vdev(path);
	}

	raid = fnvlist_alloc();
	fnvlist_add_string(raid, ZPOOL_CONFIG_TYPE, VDEV_TYPE_RAIDZ);
	fnvlist_add_uint64(raid, ZPOOL_CONFIG_NPARITY, parity);
	fnvlist_add_uint64(raid, ZPOOL_CONFIG_IS_LOG, 0);
	fnvlist_add_nvlist_array(raid, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)child, children);
	if (tbl != NULL) {
		fnvlist_add_uint64_array(raid, ZPOOL_CONFIG_RAIDZ_PARITY_EPOCHS,
		    tbl, tbl_len);
	}

	for (int c = 0; c < children; c++)
		fnvlist_free(child[c]);
	free(child);
	return (raid);
}

static nvlist_t *
root_config(nvlist_t *raid)
{
	nvlist_t *root = fnvlist_alloc();

	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)&raid, 1);
	return (root);
}

static int
create_pool(const char *pool, nvlist_t *raid)
{
	nvlist_t *root = root_config(raid);
	nvlist_t *props = fnvlist_alloc();
	int err;

	/* spa_create() enables no feature by itself; ask for all of them. */
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
	fnvlist_free(props);
	return (err);
}

/*
 * Store a table on the first top-level vdev the way zhack(1) does: checked
 * in syncing context, where the setter's verdict is final, then set.
 */
typedef struct table_arg {
	const uint64_t	*ta_table;
	uint64_t	ta_entries;
} table_arg_t;

static int
store_table_check(void *arg, dmu_tx_t *tx)
{
	table_arg_t *ta = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (!dmu_tx_is_syncing(tx))
		return (0);
	return (vdev_raidz_parity_epochs_set_check_tx(
	    spa->spa_root_vdev->vdev_child[0], ta->ta_table, ta->ta_entries,
	    tx, NULL));
}

static void
store_table_sync(void *arg, dmu_tx_t *tx)
{
	table_arg_t *ta = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	VERIFY0(vdev_raidz_parity_epochs_set(spa->spa_root_vdev->vdev_child[0],
	    ta->ta_table, ta->ta_entries, tx));
}

static int
do_create(const char *dir, const char *pool, int children, int parity,
    int marker)
{
	uint64_t tbl[6] = { 0, children, parity,
	    FORGED_EPOCH_START, children, marker };
	table_arg_t ta = { tbl, 2 };
	nvlist_t *raid;
	spa_t *spa;
	int err;

	raid = raidz_config(dir, children, parity, vdev_path, NULL, 0);
	if (marker != 0) {
		fnvlist_add_uint64(raid, "com.rrmvp:raidz_reparity_parity",
		    marker);
		fnvlist_add_uint64(raid,
		    "com.rrmvp:raidz_reparity_completion_txg", 1);
		fnvlist_add_uint64(raid,
		    "com.rrmvp:raidz_reparity_epoch_digest",
		    epoch_digest(tbl, 2, marker));
	}

	err = create_pool(pool, raid);
	fnvlist_free(raid);
	(void) printf("RESULT create rc=%d\n", err);
	if (err != 0)
		return (err);

	VERIFY0(spa_open(pool, &spa, FTAG));
	if (marker != 0) {
		err = dsl_sync_task(pool, store_table_check, store_table_sync,
		    &ta, 5, ZFS_SPACE_CHECK_NORMAL);
		(void) printf("RESULT create table rc=%d\n", err);
	}
	txg_wait_synced(spa_get_dsl(spa), 0);
	close_pool(spa, pool);
	return (err);
}

static int
do_forge(const char *dir, const char *pool, int children, int parity,
    const char *how)
{
	uint64_t tbl[6] = { 0, children, parity,
	    FORGED_EPOCH_START, children, parity % 3 + 1 };
	nvlist_t *raid;
	nvlist_t *root;
	spa_t *spa = NULL;
	int err = 0;

	if (strcmp(how, "create") == 0) {
		raid = raidz_config(dir, children, parity, vdev_path, tbl, 6);
		err = create_pool(pool, raid);
		fnvlist_free(raid);
		if (err != 0) {
			(void) printf("RESULT forge create rc=%d top=0\n", err);
			return (0);
		}
		VERIFY0(spa_open(pool, &spa, FTAG));
	} else if (strcmp(how, "add") == 0) {
		err = open_pool(dir, pool, B_FALSE, &spa);
		if (err != 0) {
			(void) printf("RESULT forge add import rc=%d\n", err);
			return (err);
		}
		raid = raidz_config(dir, children, parity, added_path, tbl, 6);
		root = root_config(raid);
		err = spa_vdev_add(spa, root, B_FALSE);
		fnvlist_free(root);
		fnvlist_free(raid);
	} else {
		usage();
	}

	txg_wait_synced(spa_get_dsl(spa), 0);
	(void) printf("RESULT forge %s rc=%d top=%llu\n", how, err,
	    (u_longlong_t)spa->spa_root_vdev->vdev_children);
	close_pool(spa, pool);
	return (0);
}

/*
 * What spa_import_rootpool() does on FreeBSD before it opens the root pool,
 * from spa_generate_rootconf(), for a pool of one top-level vdev and from
 * one leaf: a config made of the label, whose vdev tree is parsed with
 * VDEV_ALLOC_ROOTPOOL on a spa that spa_add() has just made.
 */
static int
do_rootparse(const char *dir, const char *pool)
{
	nvlist_t *label;
	nvlist_t *config;
	nvlist_t *nvroot;
	nvlist_t *nvtop;
	char path[PATH_BUF];
	const char *pname;
	uint64_t *tbl;
	uint_t tbl_len = 0;
	uint64_t nent = 0;
	vdev_t *rvd = NULL;
	spa_t *spa;
	int fd;
	int num;
	int err;

	vdev_path(path, sizeof (path), dir, 0);
	fd = open(path, O_RDONLY);
	if (fd == -1 || zpool_read_label(fd, &label, &num) != 0 ||
	    label == NULL) {
		(void) fprintf(stderr, "no label on %s\n", path);
		return (1);
	}
	(void) close(fd);
	pname = fnvlist_lookup_string(label, ZPOOL_CONFIG_POOL_NAME);
	if (strcmp(pname, pool) != 0) {
		(void) fprintf(stderr, "%s is a vdev of %s\n", path, pname);
		nvlist_free(label);
		return (1);
	}

	nvtop = fnvlist_lookup_nvlist(label, ZPOOL_CONFIG_VDEV_TREE);
	if (nvlist_lookup_uint64_array(nvtop, ZPOOL_CONFIG_RAIDZ_PARITY_EPOCHS,
	    &tbl, &tbl_len) != 0)
		tbl_len = 0;
	nvroot = fnvlist_alloc();
	fnvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL);
	fnvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID,
	    fnvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_GUID));
	fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)&nvtop, 1);
	config = fnvlist_dup(label);
	fnvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot);
	fnvlist_remove(config, ZPOOL_CONFIG_GUID);
	fnvlist_remove(config, ZPOOL_CONFIG_TOP_GUID);
	fnvlist_free(nvroot);

	spa_namespace_enter(FTAG);
	spa = spa_add(pool, config, NULL);
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &spa->spa_ubsync.ub_version) != 0)
		spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;
	spa->spa_is_root = B_TRUE;
	spa->spa_import_flags = ZFS_IMPORT_VERBATIM;

	nvtop = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	err = spa_config_parse(spa, &rvd, nvtop, NULL, 0, VDEV_ALLOC_ROOTPOOL);
	if (err == 0) {
		vdev_t *tvd = rvd->vdev_child[0];

		if (tvd->vdev_ops == &vdev_raidz_ops) {
			vdev_raidz_t *vdrz = tvd->vdev_tsd;

			nent = vdrz->vd_parity_epoch_count;
		}
		vdev_free(rvd);
	}
	spa_config_exit(spa, SCL_ALL, FTAG);
	spa_remove(spa);
	spa_namespace_exit(FTAG);

	(void) printf("RESULT rootparse rc=%d label=%u table=%llu\n", err,
	    tbl_len, (u_longlong_t)nent);
	fnvlist_free(config);
	nvlist_free(label);
	return (0);
}

/*
 * Write one block of reproducible data to a new MOS object of the open pool,
 * wait for it to sync, and return the object; *bp is its block pointer.
 */
static uint64_t
write_block(spa_t *spa, uint64_t blksz, blkptr_t *bp)
{
	objset_t *mos = spa_meta_objset(spa);
	dmu_tx_t *tx;
	dmu_buf_t *db;
	uint64_t obj;
	uint64_t *buf;

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
	*bp = *dmu_buf_get_blkptr(db);
	dmu_buf_rele(db, FTAG);
	return (obj);
}

typedef struct label_arg {
	uint64_t	la_table[6];
	boolean_t	la_now;
	boolean_t	la_at;
	uint64_t	la_after;	/* the start, after the task's txg */
	uint64_t	la_last;	/* the last txg to reach the disks */
	uint64_t	la_txg;
} label_arg_t;

static void
label_table_sync(void *arg, dmu_tx_t *tx)
{
	label_arg_t *la = arg;
	vdev_t *vd = dmu_tx_pool(tx)->dp_spa->spa_root_vdev->vdev_child[0];

	la->la_txg = dmu_tx_get_txg(tx);
	if (la->la_now)
		la->la_table[3] = vdev_raidz_parity_epochs_first_start(tx);
	if (la->la_at) {
		la->la_table[3] = la->la_txg + la->la_after;
		dmu_tx_pool(tx)->dp_spa->spa_freeze_txg =
		    la->la_txg + la->la_last;
	}
	vdev_raidz_parity_epochs_install(vd, la->la_table, 2);
	vdev_config_dirty(vd);
}

static int
do_label(const char *dir, const char *pool, boolean_t now, char **at)
{
	label_arg_t la = { .la_now = now, .la_at = at != NULL };
	vdev_raidz_t *vdrz;
	blkptr_t bp;
	uint64_t birth = 0;
	uint64_t obj = 0;
	spa_t *spa;
	int err;

	/* No export follows "at": the cachefile must not list the pool. */
	if (at != NULL && asprintf((char **)&spa_config_path,
	    "%s/crash.cache", dir) == -1)
		return (1);
	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err != 0) {
		(void) printf("RESULT label rc=%d\n", err);
		return (err);
	}
	vdrz = spa->spa_root_vdev->vdev_child[0]->vdev_tsd;
	la.la_table[0] = 0;
	la.la_table[1] = la.la_table[4] = vdrz->vd_physical_width;
	la.la_table[2] = vdrz->vd_nparity;
	la.la_table[3] = FORGED_EPOCH_START;
	la.la_table[5] = vdrz->vd_nparity % 3 + 1;
	if (at != NULL) {
		la.la_after = strtoull(at[0], NULL, 10);
		la.la_table[4] = strtoull(at[1], NULL, 10);
		la.la_table[5] = strtoull(at[2], NULL, 10);
		la.la_last = strtoull(at[3], NULL, 10);
	}
	VERIFY0(dsl_sync_task(pool, NULL, label_table_sync, &la, 0,
	    ZFS_SPACE_CHECK_NONE));
	if (at != NULL) {
		/* A txg that changes nothing writes no uberblock. */
		while (spa_last_synced_txg(spa) < la.la_txg + la.la_last)
			(void) write_block(spa, 8192, &bp);
		(void) printf("RESULT label rc=0 start=%llu txg=%llu\n",
		    (u_longlong_t)la.la_table[3], (u_longlong_t)la.la_txg);
		(void) fflush(stdout);
		_exit(0);
	}
	if (now) {
		txg_wait_synced(spa_get_dsl(spa), la.la_table[3]);
		obj = write_block(spa, 8192, &bp);
		birth = BP_GET_PHYSICAL_BIRTH(&bp);
	}
	txg_wait_synced(spa_get_dsl(spa), 0);
	(void) printf("RESULT label rc=0 start=%llu birth=%llu "
	    "object=%llu\n", (u_longlong_t)la.la_table[3],
	    (u_longlong_t)birth, (u_longlong_t)obj);
	close_pool(spa, pool);
	return (0);
}

/*
 * For "crash": freeze the pool at the first txg its import writes as soon as
 * the import has chosen it, so that no later txg reaches the labels or the
 * uberblocks.  The import drops the namespace lock while it loads, so
 * spa_next() finds the pool then.  A freeze that comes once that txg has
 * synced is too late to tell, and is reported.
 */
static kmutex_t crash_lock;
static uint64_t crash_first;
static boolean_t crash_late;

static void
crash_freeze(void *arg)
{
	const char *pool = arg;

	for (;;) {
		spa_namespace_enter(FTAG);
		for (spa_t *spa = spa_next(NULL); spa != NULL;
		    spa = spa_next(spa)) {
			if (strcmp(spa_name(spa), pool) != 0 ||
			    spa->spa_first_txg == 0)
				continue;
			spa->spa_freeze_txg = spa->spa_first_txg;
			mutex_enter(&crash_lock);
			crash_first = spa->spa_first_txg;
			crash_late = spa_last_synced_txg(spa) >= crash_first;
			mutex_exit(&crash_lock);
		}
		spa_namespace_exit(FTAG);
		if (crash_first != 0)
			break;
		(void) usleep(1000);
	}
}

static __attribute__((noreturn)) void
do_crash(const char *dir, const char *pool, const char *txg)
{
	importargs_t ia;
	char *paths[1];
	nvlist_t *config = NULL;
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
	if (err != 0 || config == NULL) {
		(void) printf("RESULT crash rc=%d first=0\n",
		    err != 0 ? err : ENOENT);
		exit(1);
	}
	if (txg != NULL && strcmp(txg, "checkpoint") != 0) {
		nvlist_t *policy = fnvlist_alloc();

		fnvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY,
		    ZPOOL_DO_REWIND | ZPOOL_EXTREME_REWIND);
		fnvlist_add_uint64(policy, ZPOOL_LOAD_REQUEST_TXG,
		    strtoull(txg, NULL, 10));
		fnvlist_add_nvlist(config, ZPOOL_LOAD_POLICY, policy);
		fnvlist_free(policy);
	}

	/*
	 * spa_import() writes the cachefile once it is done, which a crash
	 * after the first txg would not let it do.
	 */
	if (asprintf((char **)&spa_config_path, "%s/crash.cache", dir) == -1)
		exit(1);
	mutex_init(&crash_lock, NULL, MUTEX_DEFAULT, NULL);
	(void) thread_create(NULL, 0, crash_freeze, (void *)pool, 0, NULL,
	    TS_RUN, defclsyspri);
	err = spa_import((char *)pool, config, NULL, txg != NULL &&
	    strcmp(txg, "checkpoint") == 0 ? ZFS_IMPORT_CHECKPOINT :
	    ZFS_IMPORT_NORMAL);

	mutex_enter(&crash_lock);
	(void) printf("RESULT crash rc=%d first=%llu\n", err,
	    (u_longlong_t)(crash_late ? 0 : crash_first));
	mutex_exit(&crash_lock);
	(void) fflush(stdout);
	_exit(err != 0);
}

static int
do_checkpoint(const char *dir, const char *pool)
{
	spa_t *spa;
	int err;

	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err == 0) {
		err = spa_checkpoint(pool);
		close_pool(spa, pool);
	}
	(void) printf("RESULT checkpoint rc=%d\n", err);
	return (err);
}

static int
do_recover(const char *dir, const char *pool)
{
	importargs_t ia;
	char *paths[1];
	nvlist_t *config = NULL;
	nvlist_t *policy;
	spa_t *spa;
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
	policy = fnvlist_alloc();
	fnvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY, ZPOOL_DO_REWIND);
	fnvlist_add_nvlist(config, ZPOOL_LOAD_POLICY, policy);
	fnvlist_free(policy);

	err = spa_import((char *)pool, config, NULL, ZFS_IMPORT_NORMAL);
	fnvlist_free(config);
	(void) printf("RESULT recover rc=%d\n", err);
	if (err != 0)
		return (err);
	VERIFY0(spa_open(pool, &spa, FTAG));
	close_pool(spa, pool);
	return (0);
}

static int
do_write(const char *dir, const char *pool, uint64_t blksz)
{
	spa_t *spa;
	blkptr_t bp;
	uint64_t obj;
	int err;

	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err != 0) {
		(void) printf("RESULT write rc=%d\n", err);
		return (err);
	}
	obj = write_block(spa, blksz, &bp);

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
do_put(const char *dir, const char *pool, uint64_t blksz)
{
	spa_t *spa;
	objset_t *os;
	dmu_tx_t *tx;
	dmu_buf_t *db;
	uint64_t obj;
	uint64_t birth;
	uint64_t *buf;
	int err;

	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err != 0) {
		(void) printf("RESULT put rc=%d\n", err);
		return (err);
	}
	VERIFY0(dmu_objset_own(pool, DMU_OST_ANY, B_FALSE, B_TRUE, FTAG,
	    &os));

	buf = malloc(blksz);
	fill_pattern(buf, blksz);

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, blksz);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	obj = dmu_object_alloc(os, DMU_OT_PLAIN_FILE_CONTENTS, blksz,
	    DMU_OT_NONE, 0, tx);
	dmu_write(os, obj, 0, blksz, buf, tx, DMU_READ_NO_PREFETCH);
	dmu_tx_commit(tx);
	free(buf);
	txg_wait_synced(spa_get_dsl(spa), 0);

	VERIFY0(dmu_buf_hold(os, obj, 0, FTAG, &db, DMU_READ_NO_PREFETCH));
	birth = BP_GET_PHYSICAL_BIRTH(dmu_buf_get_blkptr(db));
	dmu_buf_rele(db, FTAG);
	dmu_objset_disown(os, B_TRUE, FTAG);

	(void) printf("RESULT put rc=0 object=%llu birth=%llu\n",
	    (u_longlong_t)obj, (u_longlong_t)birth);
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
do_read(const char *dir, const char *pool, uint64_t obj, uint64_t blksz,
    boolean_t readonly)
{
	spa_t *spa;
	uint64_t *buf, *want;
	int err, match;

	err = open_pool(dir, pool, readonly, &spa);
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

/*
 * The starts "boundary" tries: the txg of its sync task and the three after
 * it, the last of which is the first start the setter allows.
 */
#define	BOUNDARY_TRIES	4

typedef struct boundary_arg {
	uint64_t	ba_txg;
	int		ba_check[BOUNDARY_TRIES];
	int		ba_set[BOUNDARY_TRIES];
} boundary_arg_t;

/* The vdev's own geometry from txg 0, then another parity from start. */
static void
boundary_table(vdev_t *vd, uint64_t start, uint64_t *tbl)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;

	tbl[0] = 0;
	tbl[1] = tbl[4] = vdrz->vd_physical_width;
	tbl[2] = vdrz->vd_nparity;
	tbl[3] = start;
	tbl[5] = vdrz->vd_nparity % 3 + 1;
}

static int
boundary_check(void *arg, dmu_tx_t *tx)
{
	boundary_arg_t *ba = arg;
	vdev_t *vd;
	uint64_t tbl[6];

	if (!dmu_tx_is_syncing(tx))
		return (0);
	vd = dmu_tx_pool(tx)->dp_spa->spa_root_vdev->vdev_child[0];
	ba->ba_txg = dmu_tx_get_txg(tx);
	for (int d = 0; d < BOUNDARY_TRIES; d++) {
		boundary_table(vd, ba->ba_txg + d, tbl);
		ba->ba_check[d] = vdev_raidz_parity_epochs_set_check_tx(vd,
		    tbl, 2, tx, NULL);
	}
	return (0);
}

static void
boundary_sync(void *arg, dmu_tx_t *tx)
{
	boundary_arg_t *ba = arg;
	vdev_t *vd = dmu_tx_pool(tx)->dp_spa->spa_root_vdev->vdev_child[0];
	uint64_t tbl[6];

	VERIFY3U(dmu_tx_get_txg(tx), ==, ba->ba_txg);
	for (int d = 0; d < BOUNDARY_TRIES; d++) {
		boundary_table(vd, ba->ba_txg + d, tbl);
		ba->ba_set[d] = vdev_raidz_parity_epochs_set(vd, tbl, 2, tx);
	}
}

static const char *
boundary_result(int err, const char *ok, char *buf, size_t len)
{
	if (err == 0)
		return (ok);
	if (err == ERANGE)
		return ("ERANGE");
	(void) snprintf(buf, len, "error%d", err);
	return (buf);
}

static int
do_boundary(const char *dir, const char *pool)
{
	boundary_arg_t ba = { 0 };
	vdev_raidz_t *vdrz;
	spa_t *spa;
	char b1[16];
	char b2[16];
	int err;

	err = open_pool(dir, pool, B_FALSE, &spa);
	if (err != 0) {
		(void) printf("RESULT boundary rc=%d\n", err);
		return (err);
	}
	err = dsl_sync_task(pool, boundary_check, boundary_sync, &ba, 5,
	    ZFS_SPACE_CHECK_NORMAL);
	txg_wait_synced(spa_get_dsl(spa), 0);
	(void) printf("RESULT boundary rc=%d txg=%llu\n", err,
	    (u_longlong_t)ba.ba_txg);
	for (int d = 0; d < BOUNDARY_TRIES; d++) {
		(void) printf("RESULT boundary start=T+%d check=%s set=%s\n",
		    d, boundary_result(ba.ba_check[d], "accepted", b1,
		    sizeof (b1)), boundary_result(ba.ba_set[d], "stored", b2,
		    sizeof (b2)));
	}

	vdrz = spa->spa_root_vdev->vdev_child[0]->vdev_tsd;
	mutex_enter(&vdrz->vd_expand_lock);
	(void) printf("RESULT boundary table=");
	for (uint64_t i = 0; i < vdrz->vd_parity_epoch_count; i++) {
		const uint64_t *e = &vdrz->vd_parity_epochs[3 * i];

		(void) printf("%s%llu:%llu:%llu", i == 0 ? "" : ",",
		    (u_longlong_t)e[0], (u_longlong_t)e[1],
		    (u_longlong_t)e[2]);
	}
	(void) printf("\n");
	mutex_exit(&vdrz->vd_expand_lock);

	close_pool(spa, pool);
	return (err);
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
	} else if (strcmp(argv[1], "forge") == 0 && argc == 7) {
		rv = do_forge(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]),
		    argv[6]);
	} else if (strcmp(argv[1], "rootparse") == 0 && argc == 4) {
		rv = do_rootparse(argv[2], argv[3]);
	} else if (strcmp(argv[1], "label") == 0 && (argc == 4 ||
	    (argc == 5 && strcmp(argv[4], "now") == 0))) {
		rv = do_label(argv[2], argv[3], argc == 5, NULL);
	} else if (strcmp(argv[1], "label") == 0 && argc == 9 &&
	    strcmp(argv[4], "at") == 0) {
		rv = do_label(argv[2], argv[3], B_FALSE, argv + 5);
	} else if (strcmp(argv[1], "recover") == 0 && argc == 4) {
		rv = do_recover(argv[2], argv[3]);
	} else if (strcmp(argv[1], "checkpoint") == 0 && argc == 4) {
		rv = do_checkpoint(argv[2], argv[3]);
	} else if (strcmp(argv[1], "crash") == 0 && (argc == 4 ||
	    argc == 5)) {
		do_crash(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
	} else if (strcmp(argv[1], "write") == 0 && argc == 5) {
		rv = do_write(argv[2], argv[3], strtoull(argv[4], NULL, 10));
	} else if (strcmp(argv[1], "put") == 0 && argc == 5) {
		rv = do_put(argv[2], argv[3], strtoull(argv[4], NULL, 10));
	} else if (strcmp(argv[1], "read") == 0 && (argc == 6 ||
	    (argc == 7 && strcmp(argv[6], "readonly") == 0))) {
		rv = do_read(argv[2], argv[3], strtoull(argv[4], NULL, 10),
		    strtoull(argv[5], NULL, 10), argc == 7);
	} else if (strcmp(argv[1], "state") == 0 && argc == 5) {
		rv = do_state(argv[2], argv[3], argv[4]);
	} else if (strcmp(argv[1], "boundary") == 0 && argc == 4) {
		rv = do_boundary(argv[2], argv[3]);
	} else {
		usage();
	}
	kernel_fini();
	return (rv != 0);
}
