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
 * Copyright (c) 2026, James Hilliard.
 */

/*
 * Exercise the private accounting and TRIM submission checks directly, without
 * exporting test entry points from the module.  Replace only the transaction
 * and I/O boundaries; no pool is opened and no I/O reaches a device.  Prefix
 * the mocks so they do not interpose on calls within libzpool.
 */
#define	dmu_tx_create_dd		metaslab_test_tx_create
#define	dmu_tx_assign		metaslab_test_tx_assign
#define	dmu_tx_commit		metaslab_test_tx_commit
#define	spa_config_enter	metaslab_test_config_enter
#define	spa_config_exit		metaslab_test_config_exit
#define	spa_iostats_trim_add	metaslab_test_trim_stats
#define	zio_trim		metaslab_test_trim
#define	zio_nowait		metaslab_test_nowait

#include "../../module/zfs/metaslab.c"
#include "../../module/zfs/vdev_trim.c"
#include <sys/dsl_dir.h>
#include "unit.h"

#define	UNIT_SIZE	(64ULL << 10)
#define	TEST_TXG	42

typedef struct {
	spa_t spa;
	dsl_pool_t dp;
	dsl_dir_t dd;
	vdev_t vd;
	metaslab_group_t *mg;
	metaslab_t ms;
	space_map_t sm;
	space_map_phys_t phys;
	dmu_tx_t tx;
	zio_t zio;
	unsigned int assigned;
	unsigned int committed;
	unsigned int submitted;
	unsigned int completed;
	unsigned int config_held;
	unsigned int invalidate_at;
	uint64_t next_offset;
} fixture_t;

static fixture_t *fixture;

static zfs_range_tree_t *
new_tree(void)
{
	return (zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0));
}

static fixture_t *
fixture_create(void)
{
	fixture_t *f = munit_calloc(1, sizeof (*f));
	f->mg = munit_calloc(1, sizeof (*f->mg));
	fixture = f;
	f->spa.spa_dsl_pool = &f->dp;
	f->spa.spa_syncing_txg = TEST_TXG;
	f->spa.spa_sync_pass = 2;
	f->spa.spa_autotrim = SPA_AUTOTRIM_ON;
	f->dp.dp_mos_dir = &f->dd;
	f->vd.vdev_spa = &f->spa;
	f->vd.vdev_top = &f->vd;
	f->vd.vdev_state = VDEV_STATE_HEALTHY;
	f->mg->mg_vd = &f->vd;
	f->ms.ms_group = f->mg;
	f->ms.ms_size = 16 * UNIT_SIZE;
	f->ms.ms_sm = &f->sm;
	f->ms.ms_loaded = B_TRUE;
	f->ms.ms_disabled = 1;
	f->sm.sm_size = f->ms.ms_size;
	f->sm.sm_phys = &f->phys;
	f->tx.tx_txg = TEST_TXG;
	mutex_init(&f->ms.ms_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&f->vd.vdev_trim_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&f->vd.vdev_trim_io_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&f->vd.vdev_trim_io_cv, NULL, CV_DEFAULT, NULL);
	f->ms.ms_allocatable = new_tree();
	f->ms.ms_unflushed_allocs = new_tree();
	f->ms.ms_unflushed_frees = new_tree();
	f->ms.ms_freed = new_tree();
	f->ms.ms_trim = new_tree();
	for (int i = 0; i < TXG_SIZE; i++)
		f->ms.ms_allocating[i] = new_tree();
	for (int i = 0; i < TXG_DEFER_SIZE; i++)
		f->ms.ms_defer[i] = new_tree();
	return (f);
}

static void
free_tree(zfs_range_tree_t *rt)
{
	zfs_range_tree_vacate(rt, NULL, NULL);
	zfs_range_tree_destroy(rt);
}

static void
fixture_destroy(fixture_t *f)
{
	munit_assert_uint(f->config_held, ==, 0);
	munit_assert_uint(f->assigned, ==, f->committed);
	munit_assert_uint(f->submitted, ==, f->completed);
	munit_assert_uint64(f->vd.vdev_trim_inflight[TRIM_TYPE_AUTO], ==, 0);
	unit_false(MUTEX_HELD(&f->ms.ms_lock));
	free_tree(f->ms.ms_allocatable);
	free_tree(f->ms.ms_unflushed_allocs);
	free_tree(f->ms.ms_unflushed_frees);
	free_tree(f->ms.ms_freed);
	free_tree(f->ms.ms_trim);
	for (int i = 0; i < TXG_SIZE; i++)
		free_tree(f->ms.ms_allocating[i]);
	for (int i = 0; i < TXG_DEFER_SIZE; i++)
		free_tree(f->ms.ms_defer[i]);
	cv_destroy(&f->vd.vdev_trim_io_cv);
	mutex_destroy(&f->vd.vdev_trim_io_lock);
	mutex_destroy(&f->vd.vdev_trim_lock);
	mutex_destroy(&f->ms.ms_lock);
	free(f->mg);
	free(f);
	fixture = NULL;
}

/* A late-pass load leaves its reconstructed baseline pending condensation. */
static MunitResult
test_accounting(const MunitParameter params[], void *data)
{
	(void) data;
	boolean_t freeing = strcmp(munit_parameters_get(params, "change"),
	    "free") == 0;
	boolean_t invalid = strcmp(munit_parameters_get(params, "summary"),
	    "invalid") == 0;
	boolean_t logged = strcmp(munit_parameters_get(params, "map"),
	    "logged") == 0;
	fixture_t *f = fixture_create();
	metaslab_t *ms = &f->ms;
	int64_t delta = freeing ? -(int64_t)UNIT_SIZE : UNIT_SIZE;
	ms->ms_repair.msr_entries = invalid ? 0 : 1;
	ms->ms_smp_alloc_invalid = invalid;
	ms->ms_condense_wanted = B_TRUE;
	ms->ms_repair.msr_smp_alloc = invalid ? -(int64_t)UNIT_SIZE : UNIT_SIZE;
	ms->ms_repair.msr_allocated = 4 * UNIT_SIZE;
	ms->ms_allocated_space = ms->ms_repair.msr_allocated + delta;
	f->phys.smp_alloc = ms->ms_repair.msr_smp_alloc;
	if (logged) {
		zfs_range_tree_add(freeing ? ms->ms_unflushed_frees :
		    ms->ms_unflushed_allocs, 0, UNIT_SIZE);
	} else {
		f->phys.smp_alloc += delta;
	}
	zfs_range_tree_add(ms->ms_allocatable, ms->ms_allocated_space,
	    ms->ms_size - ms->ms_allocated_space);
	zfs_flags |= ZFS_DEBUG_METASLAB_VERIFY;
	mutex_enter(&ms->ms_lock);
	metaslab_verify_space(ms, TEST_TXG);
	mutex_exit(&ms->ms_lock);
	fixture_destroy(f);
	return (MUNIT_OK);
}

static MunitParameterEnum accounting_params[] = {
	UNIT_PARAM("change", "alloc", "free"),
	UNIT_PARAM("summary", "overlap", "invalid"),
	UNIT_PARAM("map", "direct", "logged"),
	{ NULL, NULL },
};

/*
 * The batch is detached before a repair.  Invalidation during tx assignment
 * must cancel submission, including later chunks of an already-started batch.
 */
static MunitResult
test_trim(const MunitParameter params[], void *data)
{
	(void) data;
	const char *when = munit_parameters_get(params, "invalidate");
	fixture_t *f = fixture_create();
	metaslab_t *ms = &f->ms;
	trim_args_t ta = {
		.trim_vdev = &f->vd,
		.trim_msp = ms,
		.trim_type = TRIM_TYPE_AUTO,
		.trim_extent_bytes_max = UNIT_SIZE,
		.trim_generation = ms->ms_trim_generation,
		.trim_tree = ms->ms_trim,
	};
	zfs_range_tree_add(ms->ms_trim, 4 * UNIT_SIZE, 2 * UNIT_SIZE);
	ms->ms_trim = new_tree();
	f->next_offset = VDEV_LABEL_START_SIZE + 4 * UNIT_SIZE;
	unsigned int expected = 2;
	if (strcmp(when, "before") == 0) {
		mutex_enter(&ms->ms_lock);
		metaslab_trim_invalidate(ms);
		/* Clearing the repair record must not revive this batch. */
		ms->ms_repair.msr_entries = 1;
		metaslab_repair_clear(ms);
		mutex_exit(&ms->ms_lock);
		expected = 0;
	} else if (strcmp(when, "assign") == 0) {
		f->invalidate_at = 1;
		expected = 0;
	} else if (strcmp(when, "second-chunk") == 0) {
		f->invalidate_at = 2;
		expected = 1;
	}
	munit_assert_int(vdev_trim_ranges(&ta), ==, expected == 2 ? 0 : EINTR);
	munit_assert_uint(f->submitted, ==, expected);
	munit_assert_uint(f->assigned, ==, expected == 2 ? 2 : expected + 1);
	munit_assert_uint64(f->vd.vdev_trim_inflight[TRIM_TYPE_AUTO], ==, 0);
	munit_assert_uint(f->config_held, ==, 0);
	munit_assert_uint(f->assigned, ==, f->committed);
	unit_false(MUTEX_HELD(&ms->ms_lock));

	/* A fresh batch must still be able to submit after cancellation. */
	free_tree(ta.trim_tree);
	ta.trim_tree = new_tree();
	zfs_range_tree_add(ta.trim_tree, 8 * UNIT_SIZE, 2 * UNIT_SIZE);
	f->next_offset = VDEV_LABEL_START_SIZE + 8 * UNIT_SIZE;
	ta.trim_generation = ms->ms_trim_generation;
	unit_ok(vdev_trim_ranges(&ta));
	munit_assert_uint(f->submitted, ==, expected + 2);
	free_tree(ta.trim_tree);
	fixture_destroy(f);
	return (MUNIT_OK);
}

static MunitParameterEnum trim_params[] = {
	UNIT_PARAM("invalidate", "never", "before", "assign", "second-chunk"),
	{ NULL, NULL },
};

dmu_tx_t *
metaslab_test_tx_create(dsl_dir_t *dd)
{
	unit_true(dd == &fixture->dd);
	munit_assert_uint(fixture->assigned, ==, fixture->committed);
	return (&fixture->tx);
}

int
metaslab_test_tx_assign(dmu_tx_t *tx, dmu_tx_flag_t flags)
{
	unit_true(tx == &fixture->tx);
	munit_assert_int(flags, ==, DMU_TX_WAIT | DMU_TX_SUSPEND);
	if (++fixture->assigned == fixture->invalidate_at) {
		metaslab_t *ms = &fixture->ms;
		mutex_enter(&ms->ms_lock);
		metaslab_trim_invalidate(ms);
		mutex_exit(&ms->ms_lock);
	}
	return (0);
}

void
metaslab_test_tx_commit(dmu_tx_t *tx)
{
	unit_true(tx == &fixture->tx);
	munit_assert_uint(fixture->assigned, ==, fixture->committed + 1);
	fixture->committed++;
}

void
metaslab_test_config_enter(spa_t *spa, int locks, const void *tag, krw_t rw)
{
	unit_true(spa == &fixture->spa);
	munit_assert_int(locks, ==, SCL_STATE_ALL);
	unit_true(tag == &fixture->vd);
	munit_assert_int(rw, ==, RW_READER);
	munit_assert_uint(fixture->config_held, ==, 0);
	fixture->config_held++;
}

void
metaslab_test_config_exit(spa_t *spa, int locks, const void *tag)
{
	unit_true(spa == &fixture->spa);
	munit_assert_int(locks, ==, SCL_STATE_ALL);
	unit_true(tag == &fixture->vd);
	munit_assert_uint(fixture->config_held, ==, 1);
	fixture->config_held--;
}

zio_t *
metaslab_test_trim(zio_t *pio, vdev_t *vd, uint64_t offset, uint64_t size,
    zio_done_func_t *done, void *private, zio_priority_t priority,
    zio_flag_t flags, enum trim_flag trim_flags)
{
	unit_null(pio);
	unit_true(vd == &fixture->vd);
	munit_assert_uint64(offset, ==, fixture->next_offset);
	munit_assert_uint64(size, ==, UNIT_SIZE);
	unit_true(done == vdev_autotrim_cb);
	unit_null(private);
	munit_assert_int(priority, ==, ZIO_PRIORITY_TRIM);
	munit_assert_int(flags, ==, ZIO_FLAG_CANFAIL);
	munit_assert_int(trim_flags, ==, 0);
	unit_true(MUTEX_HELD(&fixture->ms.ms_lock));
	munit_assert_uint(fixture->config_held, ==, 1);
	munit_assert_uint64(vd->vdev_trim_inflight[TRIM_TYPE_AUTO], ==, 1);
	fixture->submitted++;
	fixture->next_offset += size;
	fixture->zio = (zio_t) {
		.io_vd = vd,
		.io_orig_size = size,
		.io_done = done,
	};
	return (&fixture->zio);
}

void
metaslab_test_nowait(zio_t *zio)
{
	unit_true(zio == &fixture->zio);
	zio->io_done(zio);
}

void
metaslab_test_trim_stats(spa_t *spa, trim_type_t type, uint64_t written,
    uint64_t bytes_written, uint64_t skipped, uint64_t bytes_skipped,
    uint64_t failed, uint64_t bytes_failed)
{
	unit_true(spa == &fixture->spa);
	munit_assert_int(type, ==, TRIM_TYPE_AUTO);
	munit_assert_uint64(written, ==, 1);
	munit_assert_uint64(bytes_written, ==, UNIT_SIZE);
	munit_assert_uint64(skipped, ==, 0);
	munit_assert_uint64(bytes_skipped, ==, 0);
	munit_assert_uint64(failed, ==, 0);
	munit_assert_uint64(bytes_failed, ==, 0);
	fixture->completed++;
}

static MunitTest tests[] = {
	UNIT_TEST("/accounting", test_accounting, accounting_params),
	UNIT_TEST("/trim", test_trim, trim_params),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static MunitSuite suite = {
	"/metaslab", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	zfs_btree_init();
	int ret = munit_suite_main(&suite, NULL, argc, argv);
	zfs_btree_fini();
	return (ret);
}
