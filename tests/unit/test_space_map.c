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

#include <sys/zfs_context.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/space_map.h>
#include <sys/zfeature.h>

#include "mock_dmu.h"
#include "unit.h"

#define	BLOCKSIZE	512
#define	UNIT_SHIFT	9
#define	UNIT_SIZE	(1ULL << UNIT_SHIFT)
#define	MAP_START	0x1000
#define	DEBUG_WORD	SM_PREFIX_ENCODE(SM_DEBUG_PREFIX)
#define	DOUBLE_WORD	SM_PREFIX_ENCODE(SM2_PREFIX)
#define	ALLOC_WORD(o, r)	(SM_OFFSET_ENCODE(o) | SM_RUN_ENCODE(r))
#define	FREE_WORD(o, r)	(ALLOC_WORD(o, r) | SM_TYPE_ENCODE(SM_FREE))

typedef struct {
	space_map_t sm;
	space_map_phys_t phys;
	mock_dnode_t *dnode;
} map_t;

static void
map_init(map_t *map, const uint64_t *words, size_t nwords, uint64_t units)
{
	memset(map, 0, sizeof (*map));
	map->dnode = mock_dnode_create(BLOCKSIZE, DMU_OT_SPACE_MAP);
	map->phys.smp_length = nwords * sizeof (*words);
	map->sm = (space_map_t) {
		.sm_start = MAP_START,
		.sm_size = units << UNIT_SHIFT,
		.sm_shift = UNIT_SHIFT,
		.sm_os = (objset_t *)map->dnode,
		.sm_object = 1,
		.sm_blksz = BLOCKSIZE,
		.sm_phys = &map->phys,
	};

	for (size_t i = 0; i < nwords; i += BLOCKSIZE / sizeof (*words)) {
		dmu_buf_t *db;
		unit_ok(dmu_buf_hold_by_dnode((dnode_t *)map->dnode,
		    i * sizeof (*words), FTAG, &db, DMU_READ_NO_PREFETCH));
		memcpy(db->db_data, &words[i],
		    MIN((nwords - i) * sizeof (*words), BLOCKSIZE));
		dmu_buf_rele(db, FTAG);
	}
}

/* Check both readers against the same free-space complement. */
static void
check_load(map_t *map, const MunitParameter params[], int error,
    uint64_t offset_units, uint64_t run_units)
{
	zfs_range_tree_t *rt = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64,
	    NULL, 0, 0);
	space_map_load_result_t result;
	boolean_t repair = strcmp(munit_parameters_get(params, "reader"),
	    "repair") == 0;
	int err = repair ? space_map_load_length_repair(&map->sm, rt,
	    space_map_length(&map->sm), &result) :
	    space_map_load_checked(&map->sm, rt, SM_FREE);
	unit_eq(err, error);
	if (err == 0) {
		uint64_t size = map->sm.sm_size;
		uint64_t offset = offset_units << UNIT_SHIFT;
		uint64_t run = run_units << UNIT_SHIFT;
		unit_eq(zfs_range_tree_space(rt), size - run);
		if (offset != 0) {
			unit_true(zfs_range_tree_contains(rt, MAP_START,
			    offset));
		}
		if (offset + run < size) {
			unit_true(zfs_range_tree_contains(rt,
			    MAP_START + offset + run, size - offset - run));
		}
		if (repair) {
			unit_eq(result.smlr_allocated, run);
			unit_zero(result.smlr_repaired_entries);
			unit_zero(result.smlr_affected_bytes);
		}
	} else {
		unit_true(zfs_range_tree_is_empty(rt));
	}
	zfs_range_tree_vacate(rt, NULL, NULL);
	zfs_range_tree_destroy(rt);
}

static MunitResult
test_decode(const MunitParameter params[], void *data)
{
	(void) data;

	const struct {
		uint64_t words[2];
		size_t nwords;
		uint64_t offset;
		uint64_t run;
	} cases[] = {
		/* A zero run field decodes to one, not zero. */
		{ { 0 }, 1, 0, 1 },
		{ { SM_PREFIX_ENCODE(1) }, 1, 1ULL << 46, 1 },
		{ { SM_OFFSET_ENCODE(SM_OFFSET_MAX) |
		    SM_RUN_ENCODE(SM_RUN_MAX) }, 1, SM_OFFSET_MAX, SM_RUN_MAX },
		{ { DOUBLE_WORD, 0 }, 2, 0, 1 },
		{ { DOUBLE_WORD | SM2_RUN_ENCODE(SM2_RUN_MAX),
		    SM2_OFFSET_ENCODE(1) }, 2, 1, SM2_RUN_MAX },
		{ { DEBUG_WORD }, 1, 0, 0 },
		{ { DEBUG_WORD | SM_DEBUG_TXG_ENCODE(1) |
		    SM_DEBUG_SYNCPASS_ENCODE(1) }, 1, 0, 0 },
		{ { DEBUG_WORD | SM_DEBUG_ACTION_ENCODE(SM_FREE) |
		    SM_DEBUG_TXG_ENCODE(1) }, 1, 0, 0 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		map_t map;
		map_init(&map, cases[i].words, cases[i].nwords,
		    cases[i].offset + cases[i].run + 1);
		check_load(&map, params, 0, cases[i].offset, cases[i].run);
		mock_dnode_destroy(map.dnode);
	}
	return (MUNIT_OK);
}

static MunitResult
test_bounds(const MunitParameter params[], void *data)
{
	(void) data;

	const struct {
		uint64_t words[2];
		size_t nwords;
		int error;
	} cases[] = {
		{ { SM_OFFSET_ENCODE(14) | SM_RUN_ENCODE(2) }, 1, 0 },
		{ { SM_OFFSET_ENCODE(15) | SM_RUN_ENCODE(2) }, 1, ECKSUM },
		{ { SM_OFFSET_ENCODE(16) }, 1, ECKSUM },
		{ { SM_RUN_ENCODE(SM_RUN_MAX) }, 1, ECKSUM },
		{ { DOUBLE_WORD | SM2_RUN_ENCODE(2),
		    SM2_OFFSET_ENCODE(14) }, 2, 0 },
		{ { DOUBLE_WORD | SM2_RUN_ENCODE(2),
		    SM2_OFFSET_ENCODE(15) }, 2, ECKSUM },
		{ { DOUBLE_WORD, SM2_OFFSET_ENCODE(16) }, 2, ECKSUM },
		{ { DOUBLE_WORD | SM2_RUN_ENCODE(SM2_RUN_MAX), 0 }, 2, ECKSUM },
		/* Shifting this offset to bytes would overflow. */
		{ { DOUBLE_WORD, SM2_OFFSET_ENCODE(SM2_OFFSET_MAX) },
		    2, ECKSUM },
		{ { DOUBLE_WORD | SM2_VDEV_ENCODE(1), 0 }, 2, ECKSUM },
		{ { DEBUG_WORD | SM_DEBUG_SYNCPASS_ENCODE(1) }, 1, ECKSUM },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		map_t map;
		map_init(&map, cases[i].words, cases[i].nwords, 16);
		check_load(&map, params, cases[i].error, 14, 2);
		mock_dnode_destroy(map.dnode);
	}
	return (MUNIT_OK);
}

static MunitResult
test_truncated(const MunitParameter params[], void *data)
{
	(void) data;

	/* Include a valid allocation before the error to check unwinding. */
	uint64_t words[BLOCKSIZE / sizeof (uint64_t) + 1];
	for (size_t i = 0; i < ARRAY_SIZE(words); i++)
		words[i] = DEBUG_WORD;
	words[0] = 0;
	const struct {
		size_t pos;
		size_t nwords;
	} cases[] = {
		{ 1, 2 },
		{ ARRAY_SIZE(words) - 2, ARRAY_SIZE(words) - 1 },
		{ ARRAY_SIZE(words) - 2, ARRAY_SIZE(words) },
	};
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		size_t pos = cases[i].pos;
		words[pos] = DOUBLE_WORD;
		/* Truncated at the map end, or crossing a block boundary. */
		map_t map;
		map_init(&map, words, cases[i].nwords, 16);
		check_load(&map, params, ECKSUM, 0, 0);
		mock_dnode_destroy(map.dnode);
		words[pos] = DEBUG_WORD;
	}
	return (MUNIT_OK);
}

static MunitResult
test_padding(const MunitParameter params[], void *data)
{
	(void) data;

	uint64_t words[BLOCKSIZE / sizeof (uint64_t) + 2];
	for (size_t i = 0; i < ARRAY_SIZE(words) - 2; i++)
		words[i] = DEBUG_WORD;
	words[0] = 0;
	words[ARRAY_SIZE(words) - 2] = DOUBLE_WORD;
	words[ARRAY_SIZE(words) - 1] = SM2_OFFSET_ENCODE(1);
	/* Padding must have zero action and sync pass, as well as zero TXG. */
	for (uint64_t action = 0; action < 4; action++) {
		for (uint64_t pass = 0; pass < 2; pass++) {
			words[ARRAY_SIZE(words) - 3] = DEBUG_WORD |
			    SM_DEBUG_ACTION_ENCODE(action) |
			    SM_DEBUG_SYNCPASS_ENCODE(pass);
			map_t map;
			map_init(&map, words, ARRAY_SIZE(words), 16);
			check_load(&map, params,
			    action == 0 && pass == 0 ? 0 : ECKSUM, 0, 2);
			mock_dnode_destroy(map.dnode);
		}
	}
	return (MUNIT_OK);
}

static MunitResult
test_layout(const MunitParameter params[], void *data)
{
	(void) data;

	const struct {
		uint32_t blksz;
		uint64_t length;
	} cases[] = {
		{ 0, 8 },
		{ BLOCKSIZE - 1, 8 },
		{ BLOCKSIZE * 2, 8 },
		{ BLOCKSIZE, 9 },
	};
	uint64_t word = 0;
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		map_t map;
		map_init(&map, &word, 1, 16);
		map.sm.sm_blksz = cases[i].blksz;
		map.phys.smp_length = cases[i].length;
		check_load(&map, params, ECKSUM, 0, 0);
		mock_dnode_destroy(map.dnode);
	}
	return (MUNIT_OK);
}

typedef struct {
	const space_map_entry_t *expected;
	size_t count;
	size_t seen;
} entries_t;

static int
check_entry(space_map_entry_t *sme, void *arg)
{
	entries_t *entries = arg;
	unit_lt(entries->seen, entries->count);
	const space_map_entry_t *expected = &entries->expected[entries->seen++];
	unit_eq(sme->sme_type, expected->sme_type);
	unit_eq(sme->sme_vdev, expected->sme_vdev);
	unit_eq(sme->sme_offset, expected->sme_offset);
	unit_eq(sme->sme_run, expected->sme_run);
	unit_eq(sme->sme_txg, expected->sme_txg);
	unit_eq(sme->sme_sync_pass, expected->sme_sync_pass);
	return (0);
}

static MunitResult
test_iterate(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	const uint64_t words[] = {
		DEBUG_WORD | SM_DEBUG_TXG_ENCODE(42) |
		    SM_DEBUG_SYNCPASS_ENCODE(3),
		0,
		DEBUG_WORD,
		DOUBLE_WORD | SM2_VDEV_ENCODE(7),
		SM2_OFFSET_ENCODE(2) | SM2_TYPE_ENCODE(SM_FREE),
	};
	const space_map_entry_t expected[] = {
		{ SM_ALLOC, SM_NO_VDEVID, MAP_START, UNIT_SIZE, 42, 3 },
		{ SM_FREE, 7, MAP_START + 2 * UNIT_SIZE, UNIT_SIZE, 42, 3 },
	};
	entries_t entries = { expected, ARRAY_SIZE(expected), 0 };
	map_t map;
	map_init(&map, words, ARRAY_SIZE(words), 16);
	unit_ok(space_map_iterate(&map.sm, space_map_length(&map.sm),
	    check_entry, &entries));
	unit_eq(entries.seen, entries.count);
	mock_dnode_destroy(map.dnode);
	return (MUNIT_OK);
}

static MunitResult
test_record(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	space_map_phys_t phys = { 0 };
	space_map_t sm = {
		.sm_start = MAP_START,
		.sm_size = 16 * UNIT_SIZE,
		.sm_shift = UNIT_SHIFT,
		.sm_blksz = BLOCKSIZE,
		.sm_phys = &phys,
	};
	const uint64_t words[] = { DOUBLE_WORD, SM2_OFFSET_ENCODE(2) };
	space_map_entry_t sme;
	unit_eq(space_map_decode(&sm, words, 0, &sme), ECKSUM);
	unit_eq(space_map_decode(&sm, words, 1, &sme), ECKSUM);
	unit_ok(space_map_decode(&sm, words, 2, &sme));
	unit_eq(sme.sme_offset, MAP_START + 2 * UNIT_SIZE);
	unit_eq(sme.sme_run, UNIT_SIZE);
	zfs_range_tree_t *rt = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64,
	    NULL, 0, 0);
	unit_ok(space_map_load_checked(&sm, rt, SM_ALLOC));

	/* Validate geometry even when the map has no records. */
	sm.sm_shift = 64;
	unit_eq(space_map_decode(&sm, words, 2, &sme), ECKSUM);
	unit_eq(space_map_load_checked(&sm, rt, SM_ALLOC), ECKSUM);
	sm.sm_shift = UNIT_SHIFT;
	sm.sm_start = UINT64_MAX - sm.sm_size + 1;
	unit_eq(space_map_decode(&sm, words, 2, &sme), ECKSUM);
	unit_eq(space_map_load_checked(&sm, rt, SM_ALLOC), ECKSUM);
	sm.sm_start = MAP_START + 1;
	unit_eq(space_map_decode(&sm, words, 2, &sme), ECKSUM);
	unit_eq(space_map_load_checked(&sm, rt, SM_ALLOC), ECKSUM);
	unit_true(zfs_range_tree_is_empty(rt));
	zfs_range_tree_destroy(rt);
	return (MUNIT_OK);
}

static MunitResult
test_geometry(const MunitParameter params[], void *data)
{
	(void) data;
	const char *reader = munit_parameters_get(params, "reader");
	boolean_t repair = strcmp(reader, "repair") == 0;
	maptype_t maptype = strcmp(reader, "alloc") == 0 ? SM_ALLOC : SM_FREE;
	zfs_range_seg_type_t type =
	    strcmp(munit_parameters_get(params, "segments"), "32") == 0 ?
	    ZFS_RANGE_SEG32 : ZFS_RANGE_SEG64;
	size_t nwords = strcmp(munit_parameters_get(params, "records"),
	    "empty") == 0 ? 0 : 1;
	const struct {
		uint64_t start;
		uint64_t size;
		uint8_t shift;
	} cases[] = {
		{ MAP_START, 16 * UNIT_SIZE + 1, UNIT_SHIFT },
		{ MAP_START, 0, UNIT_SHIFT },
		{ MAP_START, UNIT_SIZE - 1, UNIT_SHIFT },
		{ MAP_START + 1, 16 * UNIT_SIZE, UNIT_SHIFT },
		{ UINT64_MAX - 16 * UNIT_SIZE + 1, 16 * UNIT_SIZE, UNIT_SHIFT },
		{ MAP_START, 16 * UNIT_SIZE, 64 },
	};
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		uint64_t word = 0;
		map_t map;
		map_init(&map, &word, nwords, 16);
		map.sm.sm_start = cases[i].start;
		map.sm.sm_size = cases[i].size;
		map.sm.sm_shift = cases[i].shift;
		zfs_range_tree_t *rt = zfs_range_tree_create(NULL, type, NULL,
		    MAP_START, UNIT_SHIFT);
		space_map_load_result_t result;
		memset(&result, 0xff, sizeof (result));
		int error = repair ? space_map_load_length_repair(&map.sm, rt,
		    space_map_length(&map.sm), &result) :
		    space_map_load_checked(&map.sm, rt, maptype);
		unit_eq(error, ECKSUM);
		unit_true(zfs_range_tree_is_empty(rt));
		zfs_range_tree_stat_verify(rt);
		if (repair) {
			unit_zero(result.smlr_allocated);
			unit_zero(result.smlr_repaired_entries);
		}
		space_map_entry_t sme;
		unit_eq(space_map_decode(&map.sm, &word, 1, &sme), ECKSUM);
		zfs_range_tree_destroy(rt);
		mock_dnode_destroy(map.dnode);
	}
	return (MUNIT_OK);
}

static MunitResult
test_seed_range(const MunitParameter params[], void *data)
{
	(void) data;
	boolean_t repair = strcmp(munit_parameters_get(params, "reader"),
	    "repair") == 0;
	map_t map;
	map_init(&map, NULL, 0, 1ULL << 32);
	zfs_range_tree_t *rt = zfs_range_tree_create(NULL, ZFS_RANGE_SEG32,
	    NULL, MAP_START, UNIT_SHIFT);
	space_map_load_result_t result;
	/* Valid map geometry, but the free range cannot fit in this tree. */
	int error = repair ? space_map_load_length_repair(&map.sm, rt, 0,
	    &result) : space_map_load_checked(&map.sm, rt, SM_FREE);
	unit_eq(error, ECKSUM);
	unit_true(zfs_range_tree_is_empty(rt));
	zfs_range_tree_stat_verify(rt);
	map.sm.sm_size -= UNIT_SIZE;
	error = repair ? space_map_load_length_repair(&map.sm, rt, 0,
	    &result) : space_map_load_checked(&map.sm, rt, SM_FREE);
	unit_ok(error);
	unit_true(zfs_range_tree_contains(rt, MAP_START, map.sm.sm_size));
	zfs_range_tree_stat_verify(rt);
	zfs_range_tree_vacate(rt, NULL, NULL);
	zfs_range_tree_destroy(rt);
	mock_dnode_destroy(map.dnode);
	return (MUNIT_OK);
}

static MunitResult
test_log_geometry(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	/* Raw log dumps use an unbounded size, not a metaslab's geometry. */
	space_map_phys_t phys = { 0 };
	space_map_t sm = {
		.sm_size = UINT64_MAX,
		.sm_shift = UNIT_SHIFT,
		.sm_phys = &phys,
		.sm_blksz = BLOCKSIZE,
	};
	uint64_t words[] = { DOUBLE_WORD | SM2_VDEV_ENCODE(7),
	    SM2_OFFSET_ENCODE((UINT64_MAX >> UNIT_SHIFT) - 1) };
	space_map_entry_t sme;
	unit_ok(space_map_decode(&sm, words, ARRAY_SIZE(words), &sme));
	unit_eq(sme.sme_vdev, 7);
	unit_eq(sme.sme_run, UNIT_SIZE);
	unit_eq(sme.sme_offset, (UINT64_MAX >> UNIT_SHIFT) * UNIT_SIZE -
	    UNIT_SIZE);
	words[1] = SM2_OFFSET_ENCODE(UINT64_MAX >> UNIT_SHIFT);
	unit_eq(space_map_decode(&sm, words, ARRAY_SIZE(words), &sme), ECKSUM);
	/* The unbounded sentinel must not bypass checked replay alignment. */
	zfs_range_tree_t *rt = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64,
	    NULL, 0, 0);
	space_map_load_result_t result;
	unit_eq(space_map_load_checked(&sm, rt, SM_ALLOC), ECKSUM);
	unit_eq(space_map_load_checked(&sm, rt, SM_FREE), ECKSUM);
	unit_eq(space_map_load_length_repair(&sm, rt, 0, &result), ECKSUM);
	unit_true(zfs_range_tree_is_empty(rt));
	zfs_range_tree_destroy(rt);
	return (MUNIT_OK);
}

static MunitResult
test_quarantine(const MunitParameter params[], void *data)
{
	(void) data;
	boolean_t double_word = strcmp(munit_parameters_get(params, "encoding"),
	    "double") == 0;
	zfs_range_seg_type_t type =
	    strcmp(munit_parameters_get(params, "segments"), "32") == 0 ?
	    ZFS_RANGE_SEG32 : ZFS_RANGE_SEG64;
	const struct {
		uint64_t words[5];
		size_t nwords;
		uint64_t allocated_mask;
		uint64_t affected;
		uint64_t repairs;
		size_t first, last;
		uint64_t first_cleared, last_cleared;
	} cases[] = {
		{ { ALLOC_WORD(4, 2), FREE_WORD(4, 2), FREE_WORD(4, 2) },
		    3, 0x30, 2, 1, 2, 2, 2, 2 },
		{ { ALLOC_WORD(4, 2), ALLOC_WORD(4, 2), FREE_WORD(4, 2) },
		    3, 0x30, 2, 2, 1, 2, 0, 0 },
		{ { ALLOC_WORD(4, 4), FREE_WORD(3, 3) },
		    2, 0xf8, 3, 1, 1, 1, 1, 1 },
		{ { ALLOC_WORD(4, 2), ALLOC_WORD(3, 4) },
		    2, 0x78, 4, 1, 1, 1, 2, 2 },
		{ { ALLOC_WORD(4, 2), ALLOC_WORD(4, 2), FREE_WORD(5, 3),
		    ALLOC_WORD(2, 4), FREE_WORD(1, 2) },
		    5, 0xfe, 7, 4, 1, 4, 0, 1 },
		/* Merge quarantines without affecting unrelated valid I/O. */
		{ { FREE_WORD(1, 2), FREE_WORD(6, 2), ALLOC_WORD(2, 5),
		    ALLOC_WORD(10, 1), FREE_WORD(10, 1) },
		    5, 0xfe, 7, 3, 0, 2, 2, 3 },
		{ { FREE_WORD(5, 1), ALLOC_WORD(3, 5), FREE_WORD(3, 5) },
		    3, 0xf8, 5, 3, 0, 2, 1, 0 },
		{ { ALLOC_WORD(12, 2), FREE_WORD(4, 2), ALLOC_WORD(8, 1) },
		    3, 0x3130, 2, 1, 1, 1, 2, 2 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		uint64_t words[3 * ARRAY_SIZE(cases[i].words)];
		size_t nwords = 0;
		space_map_entry_t expected[ARRAY_SIZE(cases[i].words)];
		for (size_t j = 0; j < cases[i].nwords; j++) {
			uint64_t word = cases[i].words[j];
			expected[j] = (space_map_entry_t) {
				.sme_type = SM_TYPE_DECODE(word),
				.sme_vdev = double_word ? 0 : SM_NO_VDEVID,
				.sme_offset = MAP_START +
				    (SM_OFFSET_DECODE(word) << UNIT_SHIFT),
				.sme_run = SM_RUN_DECODE(word) << UNIT_SHIFT,
				.sme_txg = 100 + j,
				.sme_sync_pass = j + 1,
			};
			words[nwords++] = DEBUG_WORD |
			    SM_DEBUG_TXG_ENCODE(100 + j) |
			    SM_DEBUG_SYNCPASS_ENCODE(j + 1);
			if (double_word) {
				words[nwords++] = DOUBLE_WORD |
				    SM2_RUN_ENCODE(SM_RUN_DECODE(word));
				words[nwords++] =
				    SM2_OFFSET_ENCODE(SM_OFFSET_DECODE(word)) |
				    SM2_TYPE_ENCODE(SM_TYPE_DECODE(word));
			} else {
				words[nwords++] = word;
			}
		}
		map_t map;
		map_init(&map, words, nwords, 16);
		zfs_range_tree_t *rt = zfs_range_tree_create(NULL, type, NULL,
		    MAP_START, UNIT_SHIFT);
		space_map_load_result_t result;
		unit_ok(space_map_load_length_repair(&map.sm, rt,
		    space_map_length(&map.sm), &result));
		uint64_t allocated = 0;
		for (unsigned j = 0; j < 16; j++) {
			boolean_t used = (cases[i].allocated_mask & (1ULL << j))
			    != 0;
			unit_eq(zfs_range_tree_contains(rt,
			    MAP_START + j * UNIT_SIZE, UNIT_SIZE), !used);
			allocated += used ? UNIT_SIZE : 0;
		}
		unit_eq(result.smlr_allocated, allocated);
		unit_eq(zfs_range_tree_space(rt), map.sm.sm_size - allocated);
		unit_eq(result.smlr_affected_bytes,
		    cases[i].affected * UNIT_SIZE);
		unit_eq(result.smlr_repaired_entries, cases[i].repairs);
		unit_eq(result.smlr_first_cleared_free_bytes,
		    cases[i].first_cleared * UNIT_SIZE);
		unit_eq(result.smlr_last_cleared_free_bytes,
		    cases[i].last_cleared * UNIT_SIZE);
		entries_t first = { &expected[cases[i].first], 1, 0 };
		entries_t last = { &expected[cases[i].last], 1, 0 };
		unit_ok(check_entry(&result.smlr_first_entry, &first));
		unit_ok(check_entry(&result.smlr_last_entry, &last));
		zfs_range_tree_stat_verify(rt);
		zfs_range_tree_vacate(rt, NULL, NULL);
		unit_eq(space_map_load_checked(&map.sm, rt, SM_FREE), ECKSUM);
		unit_true(zfs_range_tree_is_empty(rt));
		zfs_range_tree_destroy(rt);
		mock_dnode_destroy(map.dnode);
	}
	return (MUNIT_OK);
}

static MunitResult
test_quarantine_error(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	const uint64_t invalid[] = {
		DOUBLE_WORD, /* Missing second word. */
		DEBUG_WORD | SM_DEBUG_ACTION_ENCODE(SM_FREE),
		ALLOC_WORD(16, 1),
	};
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		uint64_t words[] = { ALLOC_WORD(4, 2), ALLOC_WORD(4, 2),
		    invalid[i] };
		map_t map;
		map_init(&map, words, ARRAY_SIZE(words), 16);
		zfs_range_tree_t *rt = zfs_range_tree_create(NULL,
		    ZFS_RANGE_SEG64, NULL, 0, 0);
		space_map_load_result_t result;
		unit_eq(space_map_load_length_repair(&map.sm, rt,
		    space_map_length(&map.sm), &result), ECKSUM);
		unit_eq(result.smlr_repaired_entries, 1);
		unit_true(zfs_range_tree_is_empty(rt));
		zfs_range_tree_stat_verify(rt);
		zfs_range_tree_destroy(rt);
		mock_dnode_destroy(map.dnode);
	}
	return (MUNIT_OK);
}

static MunitResult
test_absent(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	unit_zero(space_map_object(NULL));
	unit_zero(space_map_allocated(NULL));
	unit_zero(space_map_length(NULL));
	unit_zero(space_map_nblocks(NULL));
	return (MUNIT_OK);
}

/* Adapt the objset-based reader to the in-memory mock dnode. */
int
dmu_buf_hold(objset_t *os, uint64_t object, uint64_t offset,
    const void *tag, dmu_buf_t **dbp, dmu_flags_t flags)
{
	unit_eq(object, 1);
	return (dmu_buf_hold_by_dnode((dnode_t *)os, offset, tag, dbp, flags));
}

void
dmu_prefetch_stream(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t len, boolean_t start_now)
{
	(void) os; (void) object; (void) offset; (void) len; (void) start_now;
}

/* These entry points are only used by the untested write/open paths. */
int
dmu_bonus_hold(objset_t *os, uint64_t object, const void *tag, dmu_buf_t **dbp)
{
	(void) os; (void) object; (void) tag; (void) dbp;
	unit_unreachable();
}

int
dmu_bonus_max(void)
{
	unit_unreachable();
}

uint64_t
dmu_object_alloc_ibs(objset_t *os, dmu_object_type_t ot, int blocksize,
    int indirect_blockshift, dmu_object_type_t bonustype, int bonuslen,
    dmu_tx_t *tx)
{
	(void) os; (void) ot; (void) blocksize; (void) indirect_blockshift;
	(void) bonustype; (void) bonuslen; (void) tx;
	unit_unreachable();
}

void
dmu_object_info_from_db(dmu_buf_t *db, dmu_object_info_t *doi)
{
	(void) db; (void) doi;
	unit_unreachable();
}

void
dmu_object_size_from_db(dmu_buf_t *db, uint32_t *blksize,
    u_longlong_t *nblk512)
{
	(void) db; (void) blksize; (void) nblk512;
	unit_unreachable();
}

dsl_pool_t *
dmu_objset_pool(objset_t *os)
{
	(void) os;
	unit_unreachable();
}

void
dmu_write(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    const void *buf, dmu_tx_t *tx, dmu_flags_t flags)
{
	(void) os; (void) object; (void) offset; (void) size; (void) buf;
	(void) tx; (void) flags;
	unit_unreachable();
}

uint64_t
dmu_tx_get_txg(dmu_tx_t *tx)
{
	(void) tx;
	unit_unreachable();
}

int
dmu_tx_is_syncing(dmu_tx_t *tx)
{
	(void) tx;
	unit_unreachable();
}

int
dsl_pool_sync_context(dsl_pool_t *dp)
{
	(void) dp;
	unit_unreachable();
}

void
spa_feature_incr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	(void) spa; (void) fid; (void) tx;
	unit_unreachable();
}

void
spa_feature_decr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	(void) spa; (void) fid; (void) tx;
	unit_unreachable();
}

boolean_t
spa_feature_is_active(spa_t *spa, spa_feature_t fid)
{
	(void) spa; (void) fid;
	unit_unreachable();
}

uint64_t
spa_final_dirty_txg(spa_t *spa)
{
	(void) spa;
	unit_unreachable();
}

char *
spa_name(spa_t *spa)
{
	(void) spa;
	unit_unreachable();
}

uint32_t
spa_sync_pass(spa_t *spa)
{
	(void) spa;
	unit_unreachable();
}

void *
zio_buf_alloc(size_t size)
{
	(void) size;
	unit_unreachable();
}

void
zio_buf_free(void *buf, size_t size)
{
	(void) buf; (void) size;
	unit_unreachable();
}

static const MunitParameterEnum reader_params[] = {
	UNIT_PARAM("reader", "checked", "repair"),
	{ 0 },
};

static const MunitParameterEnum quarantine_params[] = {
	UNIT_PARAM("encoding", "single", "double"),
	UNIT_PARAM("segments", "32", "64"),
	{ 0 },
};

static const MunitParameterEnum geometry_params[] = {
	UNIT_PARAM("reader", "alloc", "free", "repair"),
	UNIT_PARAM("segments", "32", "64"),
	UNIT_PARAM("records", "empty", "nonempty"),
	{ 0 },
};

static const MunitTest space_map_tests[] = {
	UNIT_TEST("decode", test_decode, reader_params),
	UNIT_TEST("bounds", test_bounds, reader_params),
	UNIT_TEST("truncated", test_truncated, reader_params),
	UNIT_TEST("padding", test_padding, reader_params),
	UNIT_TEST("layout", test_layout, reader_params),
	UNIT_TEST("iterate", test_iterate),
	UNIT_TEST("record", test_record),
	UNIT_TEST("geometry", test_geometry, geometry_params),
	UNIT_TEST("seed_range", test_seed_range, reader_params),
	UNIT_TEST("log_geometry", test_log_geometry),
	UNIT_TEST("quarantine", test_quarantine, quarantine_params),
	UNIT_TEST("quarantine_error", test_quarantine_error),
	UNIT_TEST("absent", test_absent),
	{ 0 },
};

static const MunitSuite space_map_test_suite = {
	"space_map.",
	space_map_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	zfs_btree_init();
	int ret = munit_suite_main(&space_map_test_suite, NULL, argc, argv);
	zfs_btree_fini();
	return (ret);
}
