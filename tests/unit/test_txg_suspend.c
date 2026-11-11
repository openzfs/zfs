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

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dsl_pool.h>
#include <sys/txg.h>
#include <libzpool.h>
#include "unit.h"

static MunitResult
test_suspend(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	char dir[] = "/tmp/test_txg_suspend.XXXXXX";
	char path[MAXPATHLEN];
	char cachefile[MAXPATHLEN];
	nvlist_t *root = fnvlist_alloc();
	nvlist_t *child = fnvlist_alloc();
	spa_t *spa;

	unit_notnull(mkdtemp(dir));
	(void) snprintf(path, sizeof (path), "%s/vdev", dir);
	(void) snprintf(cachefile, sizeof (cachefile), "%s/cache", dir);
	int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
	munit_assert_int(fd, >=, 0);
	unit_ok(ftruncate(fd, 128 << 20));
	unit_ok(close(fd));
	spa_config_path = cachefile;
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	fnvlist_add_string(child, ZPOOL_CONFIG_TYPE, VDEV_TYPE_FILE);
	fnvlist_add_string(child, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t *const *)&child, 1);
	unit_ok(spa_create("suspend_test", root, NULL, NULL, NULL, NULL));
	unit_ok(spa_open("suspend_test", &spa, FTAG));
	spa_async_suspend(spa);
	dsl_pool_t *dp = spa_get_dsl(spa);
	tx_state_t *tx = &dp->dp_tx;
	txg_sync_stop(dp);
	uint_t timeout = zfs_txg_timeout;
	zfs_txg_timeout = 60;
	zio_suspend(spa, NULL, ZIO_SUSPEND_MMP);
	uint64_t target = tx->tx_open_txg;
	tx->tx_sync_txg_waiting = target;
	txg_sync_start(dp);

	/* Give the sync thread time to reach its suspended wait. */
	(void) sleep(1);
	(void) alarm(10);
	mutex_enter(&tx->tx_sync_lock);
	munit_assert_uint64(tx->tx_synced_txg, <, target);
	mutex_exit(&tx->tx_sync_lock);
	unit_ok(zio_resume(spa));

	hrtime_t end = gethrtime() + SEC2NSEC(5);
	boolean_t synced = B_FALSE;
	do {
		mutex_enter(&tx->tx_sync_lock);
		synced = tx->tx_synced_txg >= target;
		mutex_exit(&tx->tx_sync_lock);
		if (!synced)
			(void) usleep(10000);
	} while (!synced && gethrtime() < end);
	(void) alarm(0);
	zfs_txg_timeout = timeout;
	unit_true(synced);

	spa_async_resume(spa);
	spa_close(spa, FTAG);
	unit_ok(spa_destroy("suspend_test"));
	kernel_fini();
	fnvlist_free(child);
	fnvlist_free(root);
	unit_ok(unlink(path));
	unit_ok(rmdir(dir));
	return (MUNIT_OK);
}

static MunitTest tests[] = {
	UNIT_TEST("/suspend_resume", test_suspend),
	{ NULL }
};

static MunitSuite suite = { "/txg", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int
main(int argc, char *argv[])
{
	return (munit_suite_main(&suite, NULL, argc, argv));
}
