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
 * Crash the ZIL while it fills a log block, then resume the pool and check
 * that the ZIL frees the log block and restarts.
 *
 * The pool suspends while a write record is copied into the log block: the
 * record cannot be logged, and the wait for its txg fails, so zil_crash()
 * takes the log block before it is issued.  The txg of the record is held
 * open meanwhile, so that it cannot sync before the pool suspends.
 *
 * Usage: zil_crash_issue <directory>
 * The pool is created on a file in the directory, and destroyed at the end.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/txg.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <libzpool.h>

#define	POOL	"zil_crash_issue"
#define	VDEV_SIZE	(256ULL << 20)
#define	BLKSZ	4096

static spa_t *spa;
static boolean_t suspend_in_get_data;

/*
 * Only the record of the write that the pool suspends on asks for its data:
 * the others carry it.
 */
static int
get_data(void *arg, uint64_t gen, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio)
{
	(void) arg, (void) gen, (void) lr, (void) buf, (void) lwb, (void) zio;

	VERIFY(suspend_in_get_data);
	zio_suspend(spa, NULL, ZIO_SUSPEND_IOERR);
	return (SET_ERROR(EIO));
}

/*
 * Write a block of obj in a new tx, and log it.  Return the tx, which the
 * caller commits.
 */
static dmu_tx_t *
write_block(objset_t *os, zilog_t *zilog, uint64_t obj, itx_wr_state_t state)
{
	static uint64_t data[BLKSZ / sizeof (uint64_t)];
	size_t copied = (state == WR_COPIED ? BLKSZ : 0);
	dmu_tx_t *tx;
	itx_t *itx;
	lr_write_t *lr;

	data[0]++;
	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, obj, 0, BLKSZ);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	dmu_write(os, obj, 0, BLKSZ, data, tx, DMU_READ_NO_PREFETCH);

	itx = zil_itx_create(TX_WRITE, sizeof (*lr) + copied);
	lr = (lr_write_t *)&itx->itx_lr;
	lr->lr_foid = obj;
	lr->lr_offset = 0;
	lr->lr_length = BLKSZ;
	BP_ZERO(&lr->lr_blkptr);
	if (copied != 0)
		memcpy(lr->lr_data, data, copied);
	itx->itx_wr_state = state;
	itx->itx_sync = B_TRUE;
	zil_itx_assign(zilog, itx, tx);

	return (tx);
}

int
main(int argc, char **argv)
{
	char path[MAXPATHLEN];
	char cache[MAXPATHLEN];
	objset_t *os;
	zilog_t *zilog;
	dsl_pool_t *dp;
	dmu_tx_t *tx;
	uint64_t obj;
	int fd;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: %s <directory>\n", argv[0]);
		return (2);
	}
	(void) snprintf(path, sizeof (path), "%s/vdev", argv[1]);
	(void) snprintf(cache, sizeof (cache), "%s/zpool.cache", argv[1]);

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd == -1 || ftruncate(fd, VDEV_SIZE) != 0) {
		perror(path);
		return (1);
	}
	(void) close(fd);

	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	spa_config_path = cache;

	/* failmode=continue: zil_commit() fails instead of waiting. */
	nvlist_t *vdev = fnvlist_alloc();
	fnvlist_add_string(vdev, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(vdev, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(vdev, ZPOOL_CONFIG_ASHIFT, SPA_MINBLOCKSHIFT);
	nvlist_t *root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)&vdev, 1);
	nvlist_t *props = fnvlist_alloc();
	fnvlist_add_uint64(props, zpool_prop_to_name(ZPOOL_PROP_FAILUREMODE),
	    ZIO_FAILURE_MODE_CONTINUE);
	VERIFY0(spa_create(POOL, root, props, NULL, NULL, NULL));
	fnvlist_free(props);
	fnvlist_free(root);
	fnvlist_free(vdev);

	VERIFY0(dmu_objset_own(POOL, DMU_OST_ANY, B_FALSE, B_TRUE, FTAG,
	    &os));
	spa = dmu_objset_spa(os);
	dp = dmu_objset_pool(os);
	zilog = zil_open(os, get_data, NULL);

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, BLKSZ, DMU_OT_NONE,
	    0, tx);
	dmu_tx_commit(tx);

	/* Create the log chain first: that waits for a txg to sync. */
	dmu_tx_commit(write_block(os, zilog, obj, WR_COPIED));
	VERIFY0(zil_commit(zilog, obj));
	txg_wait_synced(dp, 0);

	/*
	 * Log a write whose record needs its data from get_data(), and commit
	 * it with its txg still open.  get_data() suspends the pool and
	 * fails, so the ZIL crashes while it fills the log block.
	 */
	tx = write_block(os, zilog, obj, WR_INDIRECT);
	suspend_in_get_data = B_TRUE;
	VERIFY3S(zil_commit(zilog, obj), ==, EIO);
	suspend_in_get_data = B_FALSE;
	VERIFY3U(zilog->zl_restart_txg, >, 0);
	dmu_tx_commit(tx);

	/* Resume the pool, and sync txgs until the ZIL restarts. */
	VERIFY0(zio_resume(spa));
	for (int i = 0; i < 10; i++) {
		txg_wait_synced(dp, 0);
		mutex_enter(&zilog->zl_lock);
		uint64_t restart_txg = zilog->zl_restart_txg;
		mutex_exit(&zilog->zl_lock);
		if (restart_txg == 0)
			break;
	}
	if (zilog->zl_restart_txg != 0) {
		(void) fprintf(stderr, "the ZIL did not restart\n");
		return (1);
	}

	/* Sync writes go through the ZIL again. */
	dmu_tx_commit(write_block(os, zilog, obj, WR_COPIED));
	VERIFY0(zil_commit(zilog, obj));
	VERIFY(!list_is_empty(&zilog->zl_lwb_list));

	zil_close(zilog);
	dmu_objset_disown(os, B_TRUE, FTAG);
	VERIFY0(spa_destroy(POOL));
	kernel_fini();
	(void) unlink(path);
	(void) unlink(cache);

	(void) printf("the ZIL restarted after it crashed while filling a "
	    "log block\n");
	return (0);
}
