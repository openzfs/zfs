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
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/space_map.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/zfeature.h>

#include "unit.h"

/* ========== */

#define	MAX_ENTRIES	8
#define	SECTOR		(1ULL << SPA_MINBLOCKSHIFT)

/*
 * A mapping held in memory only.  The lookup only reads the entries and
 * vimp_num_entries; the other fields just have to satisfy
 * vdev_indirect_mapping_verify().
 */
typedef struct {
	vdev_indirect_mapping_t		m_vim;
	vdev_indirect_mapping_phys_t	m_phys;
	vdev_indirect_mapping_entry_phys_t m_entries[MAX_ENTRIES];
} mapping_t;

/*
 * Entry i covers sectors [3i + 1, 3i + 3), so there is a gap of one sector
 * before every entry, including the first.
 */
static void
mapping_init(mapping_t *m, uint64_t n)
{
	memset(m, 0, sizeof (*m));
	for (uint64_t i = 0; i < n; i++) {
		DVA_MAPPING_SET_SRC_OFFSET(&m->m_entries[i],
		    (3 * i + 1) * SECTOR);
		DVA_SET_ASIZE(&m->m_entries[i].vimep_dst, 2 * SECTOR);
	}
	m->m_phys.vimp_num_entries = n;
	m->m_phys.vimp_max_offset = (3 * n) * SECTOR;
	m->m_vim.vim_object = 1;
	m->m_vim.vim_objset = (objset_t *)m;
	m->m_vim.vim_dbuf = (dmu_buf_t *)m;
	m->m_vim.vim_phys = &m->m_phys;
	/* vdev_indirect_mapping_verify() wants no array for an empty table. */
	m->m_vim.vim_entries = n > 0 ? m->m_entries : NULL;
}

/* The entry containing the offset, or else the first one after it. */
static vdev_indirect_mapping_entry_phys_t *
mapping_find_linear(mapping_t *m, uint64_t offset, boolean_t next)
{
	for (uint64_t i = 0; i < m->m_phys.vimp_num_entries; i++) {
		vdev_indirect_mapping_entry_phys_t *e = &m->m_entries[i];
		uint64_t start = DVA_MAPPING_GET_SRC_OFFSET(e);

		if (offset < start)
			return (next ? e : NULL);
		if (offset < start + DVA_GET_ASIZE(&e->vimep_dst))
			return (e);
	}
	return (NULL);
}

/*
 * Compare both lookups with a linear search on every sector, from below
 * the first entry to past the last one, for each table size, starting
 * with an empty table.
 */
static MunitResult
test_indirect_mapping_lookup(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	mapping_t *m = malloc(sizeof (*m));
	unit_notnull(m);

	for (uint64_t n = 0; n <= MAX_ENTRIES; n++) {
		mapping_init(m, n);
		for (uint64_t s = 0; s <= 3 * n + 1; s++) {
			uint64_t offset = s * SECTOR;

			munit_assert_ptr_equal(
			    vdev_indirect_mapping_entry_for_offset(&m->m_vim,
			    offset), mapping_find_linear(m, offset, B_FALSE));
			munit_assert_ptr_equal(
			    vdev_indirect_mapping_entry_for_offset_or_next(
			    &m->m_vim, offset),
			    mapping_find_linear(m, offset, B_TRUE));
		}
	}

	free(m);
	return (MUNIT_OK);
}

/* ========== */

/*
 * Stubs for the calls of vdev_indirect_mapping.c that mock_dmu.c does not
 * provide.  Only the lookup is tested, and it calls none of them.
 */

int
dmu_bonus_hold(objset_t *os, uint64_t object, const void *tag,
    dmu_buf_t **dbp)
{
	(void) os; (void) object; (void) tag; (void) dbp;
	return (EIO);
}

uint64_t
dmu_object_alloc(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonus_type, int bonus_len, dmu_tx_t *tx)
{
	(void) os; (void) ot; (void) blocksize; (void) bonus_type;
	(void) bonus_len; (void) tx;
	return (0);
}

int
dmu_read(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    void *buf, dmu_flags_t flags)
{
	(void) os; (void) object; (void) offset; (void) size; (void) buf;
	(void) flags;
	return (EIO);
}

void
dmu_write(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    const void *buf, dmu_tx_t *tx, dmu_flags_t flags)
{
	(void) os; (void) object; (void) offset; (void) size; (void) buf;
	(void) tx; (void) flags;
}

uint64_t
dmu_tx_get_txg(dmu_tx_t *tx)
{
	(void) tx;
	return (0);
}

int
dmu_tx_is_syncing(dmu_tx_t *tx)
{
	(void) tx;
	return (0);
}

dsl_pool_t *
dmu_tx_pool(dmu_tx_t *tx)
{
	(void) tx;
	return (NULL);
}

int
dsl_pool_sync_context(dsl_pool_t *dp)
{
	(void) dp;
	return (0);
}

void
spa_feature_incr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	(void) spa; (void) fid; (void) tx;
}

void
spa_feature_decr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	(void) spa; (void) fid; (void) tx;
}

boolean_t
spa_feature_is_active(spa_t *spa, spa_feature_t fid)
{
	(void) spa; (void) fid;
	return (B_FALSE);
}

int
space_map_iterate(space_map_t *sm, uint64_t length, sm_cb_t callback,
    void *arg)
{
	(void) sm; (void) length; (void) callback; (void) arg;
	return (EIO);
}

uint64_t
space_map_length(space_map_t *sm)
{
	(void) sm;
	return (0);
}

/* ========== */

static const MunitTest indirect_mapping_tests[] = {
	UNIT_TEST("lookup",	test_indirect_mapping_lookup),
	{ 0 },
};

static const MunitSuite indirect_mapping_test_suite = {
	"indirect_mapping.",
	indirect_mapping_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&indirect_mapping_test_suite, NULL, argc,
	    argv));
}
