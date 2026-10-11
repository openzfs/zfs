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
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

/*
 * Selftests for linear_hash_t.
 *
 * Every operation goes through check_op(), which is told whether the key
 * is present and checks the result code, the buffer (the previous record
 * after LH_FOUND or LH_UPDATED, otherwise untouched), guard bytes after the
 * buffer, and the record count. audit_table() checks the bucket invariants
 * in zstream_hash_impl.h, that each entry is in the bucket its hash
 * selects, that each data record and overflow bucket is referenced at most
 * once and none is lost, that the next split's buddy is empty, the
 * occupancy and memory bookkeeping, and the allocator group.
 *
 * Except in hash_key_prefix, tables use test_hash(), which returns the
 * first 8 bytes of the 16-byte key; the other 8 bytes hold a key number.
 *
 * hash_invalid_config: bad lh_init() arguments are fatal.
 *
 * hash_key_prefix: with the real hash function and at several record and
 * key sizes, only a record's key prefix identifies it. Lookups with other
 * bytes in the rest of the buffer find the record and return all of it.
 * An all-zero key and a key that differs only in its last byte coexist.
 *
 * hash_chain_ends: in a disk-only table, keys that share one full hash,
 * zero, form a single chain that grows to more than three buckets. At each
 * length, every key is found and a missing key is not, so walks end at
 * every position, including past a full bucket with and without overflow;
 * only inserts may extend the chain. The keys differ only in their last
 * byte.
 *
 * hash_model_{memory,spill}: random operations on random keys, checked
 * against a model, through a complete split cycle. A hot set of keys
 * shares the low bits of its hashes, so long chains form and are split.
 * The run must see overflow buckets freed and reused, an insert that
 * causes several splits, and a chain of three or more buckets. In the
 * spill run, all three allocators must have records on disk.
 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/sysmacros.h>

#include "zstream_alloc.h"
#include "zstream_alloc_impl.h"
#include "zstream_hash.h"
#include "zstream_hash_impl.h"
#include "zstream_heap.h"
#include "zstream_selftest.h"
#include "zstream_util.h"

#define	GUARD_BYTES	16
#define	GUARD_VALUE	0xa5

#define	KEY_SIZE	16	/* Of test_hash() tables */
#define	REC_SIZE	32
#define	MEMORY_ONLY	(4 << 20)	/* Ample for every memory-only table */

/*
 * Run op on a copy of in. old is the record stored under in's key, or NULL
 * if there is none; *count tracks the number of records in the table.
 * Returns B_TRUE if the table now holds in.
 */
static boolean_t
check_op(linear_hash_t *lh, lh_op_t op, const uint8_t *in,
    const uint8_t *old, uint64_t *count)
{
	static const lh_result_t results[LH_NUM_OPS][2] = {
		[LH_FIND] = { LH_NOT_FOUND, LH_FOUND },
		[LH_INSERT] = { LH_INSERTED, LH_FOUND },
		[LH_UPDATE] = { LH_NOT_FOUND, LH_UPDATED },
		[LH_UPSERT] = { LH_INSERTED, LH_UPDATED },
	};
	lh_result_t want = results[op][old != NULL];
	size_t size = lh->lh_record_size;
	uint8_t buf[MAX_RECORD_SIZE + GUARD_BYTES];

	memcpy(buf, in, size);
	memset(buf + size, GUARD_VALUE, GUARD_BYTES);
	VERIFY3U(lh_record(lh, op, buf), ==, want);
	if (want == LH_FOUND || want == LH_UPDATED)
		VERIFY0(memcmp(buf, old, size));
	else
		VERIFY0(memcmp(buf, in, size));
	for (size_t i = size; i < size + GUARD_BYTES; i++)
		VERIFY3U(buf[i], ==, GUARD_VALUE);
	if (want == LH_INSERTED)
		(*count)++;
	VERIFY3U(lh_get_stats(lh).ls_total_records, ==, *count);
	return (want == LH_INSERTED || want == LH_UPDATED);
}

/* The same arithmetic as bucket_for_hash() */
static uint64_t
home_bucket(const linear_hash_t *lh, uint64_t hash)
{
	uint64_t mask = (1ULL << lh->lh_hash_suffix_length) - 1;

	if ((hash & mask) < lh->lh_split_pointer)
		mask = (mask << 1) | 1;
	return (hash & mask);
}

/*
 * Check a bucket in chain ix: live entries are packed at the front and
 * belong in the chain, the rest are zeros, and only a full bucket may have
 * an overflow bucket. Marks the entries' data records seen and returns the
 * number of live entries.
 */
static int
audit_bucket(const linear_hash_t *lh, const bucket_t *b, uint64_t ix,
    boolean_t *seen, uint64_t nrec)
{
	int n = 0;

	while (n < ENTRIES_PER_BUCKET && b->b_entries[n].be_record != 0) {
		const bucket_entry_t *be = &b->b_entries[n++];
		VERIFY3U(home_bucket(lh, be->be_hash), ==, ix);
		VERIFY3U(be->be_record, <, nrec);
		VERIFY(!seen[be->be_record]);
		seen[be->be_record] = B_TRUE;
	}
	for (int e = n; e < ENTRIES_PER_BUCKET; e++)
		VERIFY0(b->b_entries[e].be_hash | b->b_entries[e].be_record);
	VERIFY(n == ENTRIES_PER_BUCKET || b->b_overflow == 0);
	return (n);
}

/*
 * Check the whole table; see the comment at the top of the file. Returns
 * the length in buckets of the longest chain.
 */
static uint64_t
audit_table(linear_hash_t *lh)
{
	const lh_occupancy_t *occ = &lh->lh_occupancy;
	const heap_t *freeheap = &lh->lh_freeheap;
	allocator_group_t *g = lh->lh_alloc.data->a_group;
	uint64_t cycle = 1ULL << lh->lh_hash_suffix_length;
	uint64_t nrec = lh->lh_alloc.data->a_record_count;
	uint64_t nover = lh->lh_alloc.overflow->a_record_count;
	boolean_t *rec_seen = safe_calloc(nrec * sizeof (boolean_t));
	boolean_t *over_seen = safe_calloc(nover * sizeof (boolean_t));
	uint64_t top = 0;
	uint64_t records = 0;
	uint64_t chained = 0;
	uint64_t longest = 0;
	bucket_t b;

	VERIFY3U(lh->lh_split_pointer, <, cycle);
	VERIFY3U(occ->top_buckets, ==, cycle + lh->lh_split_pointer);
	for (uint64_t ix = 0; ix < occ->top_buckets; ix++) {
		uint64_t len;

		allocator_retrieve(lh->lh_alloc.bucket, ix, &b);
		int n = audit_bucket(lh, &b, ix, rec_seen, nrec);
		top += n;
		records += n;
		for (len = 1; b.b_overflow != 0; len++) {
			record_ix_t over = b.b_overflow;

			VERIFY3U(over, <, nover);
			VERIFY(!over_seen[over]);
			over_seen[over] = B_TRUE;
			chained++;
			allocator_retrieve(lh->lh_alloc.overflow, over, &b);
			n = audit_bucket(lh, &b, ix, rec_seen, nrec);
			VERIFY3S(n, >, 0);
			records += n;
		}
		longest = MAX(longest, len);
	}

	/* Free overflow buckets are unreferenced, and none are leaked */
	for (uint64_t i = 0; i < heap_count(freeheap); i++) {
		record_ix_t over = freeheap->values[i];

		VERIFY3U(over, >, 0);
		VERIFY3U(over, <, nover);
		VERIFY(!over_seen[over]);
		over_seen[over] = B_TRUE;
	}
	VERIFY3U(chained + heap_count(freeheap), ==, nover - 1);
	VERIFY3U(records, ==, lh_get_stats(lh).ls_total_records);
	VERIFY3U(top, ==, occ->top_entries);
	VERIFY3U(occ->top_entries, <=, occ->max_top_entries);

	/* The next split's buddy has never been written */
	allocator_retrieve(lh->lh_alloc.bucket, occ->top_buckets, &b);
	VERIFY0(audit_bucket(lh, &b, occ->top_buckets, rec_seen, nrec));

	VERIFY3U(lh_get_stats(lh).ls_memory_use, ==,
	    g->ag_clusters_current * g->ag_cluster_size +
	    freeheap->size * sizeof (uint64_t));
	selftest_audit_group(lh->lh_alloc.data);
	free(rec_seen);
	free(over_seen);
	return (longest);
}

static uint64_t
test_hash(const uint64_t key[MAX_KEY_SIZE / sizeof (uint64_t)])
{
	return (key[0]);
}

static linear_hash_t *
test_table(size_t memory, const char *dir)
{
	linear_hash_t *lh = lh_init(REC_SIZE, KEY_SIZE, memory, dir);

	lh->lh_hash_func = test_hash;
	return (lh);
}

/*
 * A record for a test_hash() table: the hash, then the key number in
 * big-endian order, so that small key numbers differ only in the last key
 * byte, then a payload made from tag.
 */
static void
make_record(uint8_t *rec, uint64_t hash, uint64_t keyno, uint64_t tag)
{
	selftest_fill(rec, REC_SIZE, tag);
	memcpy(rec, &hash, sizeof (hash));
	for (int i = 0; i < 8; i++)
		rec[KEY_SIZE - 1 - i] = (uint8_t)(keyno >> (8 * i));
}

typedef struct {
	const char	*ic_what;
	size_t		ic_record;
	size_t		ic_key;
	size_t		ic_memory;
	const char	*ic_expect;
} invalid_config_t;

static void
init_child(void *arg)
{
	const invalid_config_t *ic = arg;

	(void) lh_init(ic->ic_record, ic->ic_key, ic->ic_memory, NULL);
}

static void
hash_invalid_config(void)
{
	static const invalid_config_t cases[] = {
		{ "no backing", 32, 16, 0, "requires memory or disk backing" },
		{ "record size 0", 0, 1, 1 << 20, "record size must be" },
		{ "record size too large", MAX_RECORD_SIZE + 1, 16, 1 << 20,
		    "record size must be" },
		{ "key size 0", 32, 0, 1 << 20, "key size must be" },
		{ "key size too large", 64, MAX_KEY_SIZE + 1, 1 << 20,
		    "key size must be" },
		{ "key larger than record", 8, 9, 1 << 20,
		    "key size cannot exceed record size" },
	};

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		selftest_expect_exit(cases[i].ic_what, init_child,
		    (void *)&cases[i], cases[i].ic_expect);
	}
}

/*
 * Each table holds two records whose keys are zeros except for the last
 * key byte, which is 0 in one and 1 in the other. Their other bytes are
 * random, and the lookups use different random bytes there.
 */
static void
hash_key_prefix(void)
{
	static const struct {
		size_t	record;
		size_t	key;
	} sizes[] = {
		{ 1, 1 },
		{ 100, 5 },
		{ MAX_KEY_SIZE, MAX_KEY_SIZE },
		{ MAX_RECORD_SIZE, MAX_KEY_SIZE },
	};

	for (int i = 0; i < ARRAY_SIZE(sizes); i++) {
		size_t size = sizes[i].record;
		size_t key = sizes[i].key;
		linear_hash_t *lh = lh_init(size, key, MEMORY_ONLY, NULL);
		uint8_t recs[2][MAX_RECORD_SIZE];
		uint8_t probe[MAX_RECORD_SIZE];
		uint64_t count = 0;

		for (int r = 0; r < 2; r++) {
			selftest_fill(recs[r], size, r + 1);
			memset(recs[r], 0, key);
			recs[r][key - 1] = r;
			(void) check_op(lh, LH_INSERT, recs[r], NULL, &count);
		}
		for (int r = 0; r < 2; r++) {
			selftest_fill(probe, size, r + 3);
			memcpy(probe, recs[r], key);
			(void) check_op(lh, LH_FIND, probe, recs[r], &count);
		}
		(void) audit_table(lh);
		lh_fini(lh);
	}
}

#define	CHAIN_KEYS	(3 * ENTRIES_PER_BUCKET + 1)
#define	CHAIN_HASH	0	/* An ordinary hash value, not a sentinel */

/*
 * Once key n is inserted, the chain holds keys [0, n], and the lookup of
 * key n + 1 walks off its end: at lengths 6, 12 and 18 past a full bucket
 * with no overflow, which the lookup must not extend, and otherwise onto
 * an empty entry. The inserts that follow those lengths extend the chain.
 */
static void
hash_chain_ends(void)
{
	linear_hash_t *lh = test_table(0, selftest_scratch_dir());
	uint8_t recs[CHAIN_KEYS + 1][REC_SIZE];
	uint64_t count = 0;

	for (uint64_t k = 0; k <= CHAIN_KEYS; k++)
		make_record(recs[k], CHAIN_HASH, k, k + 1);
	for (uint64_t n = 0; n < CHAIN_KEYS; n++) {
		(void) check_op(lh, LH_INSERT, recs[n], NULL, &count);
		for (uint64_t k = 0; k <= n; k++)
			(void) check_op(lh, LH_FIND, recs[k], recs[k], &count);
		(void) check_op(lh, LH_FIND, recs[n + 1], NULL, &count);
		VERIFY3U(audit_table(lh), ==, n / ENTRIES_PER_BUCKET + 1);
	}
	lh_fini(lh);
}

#define	HOT_KEYS		64
#define	HOT_BUCKET		1
#define	MODEL_OPS_PER_KEY	5
#define	AUDIT_INTERVAL		250
#define	SPILL_MEMORY		(64 << 10)

typedef struct {
	linear_hash_t	*m_lh;
	unsigned	m_suffix;	/* Initial hash suffix length */
	uint64_t	*m_tags;	/* Per key number; 0 = absent */
	uint64_t	m_last_tag;
	uint64_t	m_count;	/* Records in the table */
} model_t;

/*
 * Key numbers below HOT_KEYS have hashes whose low m_suffix bits are
 * HOT_BUCKET, so they start out in one chain; other hashes are uniform.
 */
static void
model_op(model_t *m, lh_op_t op, uint64_t keyno)
{
	uint64_t hash = selftest_mix64(selftest_seed ^ keyno);
	uint64_t tag = ++m->m_last_tag;
	uint64_t *stored = &m->m_tags[keyno];
	uint8_t in[REC_SIZE];
	uint8_t old[REC_SIZE];

	if (keyno < HOT_KEYS)
		hash = (hash << m->m_suffix) | HOT_BUCKET;
	make_record(in, hash, keyno, tag);
	make_record(old, hash, keyno, *stored);
	if (check_op(m->m_lh, op, in, (*stored != 0) ? old : NULL,
	    &m->m_count))
		*stored = tag;
}

/*
 * Completing a split cycle takes 9 << m_suffix entries at the top level
 * plus those in overflow buckets, about 10 << m_suffix in all. Half of the
 * operations insert, so after 5 per key about 1 - e^-2.5, or 92%, of the
 * 12 << m_suffix keys are in the table.
 */
static void
model_run(size_t memory, const char *dir, uint64_t stream)
{
	linear_hash_t *lh = test_table(memory, dir);
	const heap_t *freeheap = &lh->lh_freeheap;
	model_t m = { .m_lh = lh, .m_suffix = lh->lh_hash_suffix_length };
	uint64_t nkeys = 12ULL << m.m_suffix;
	uint64_t frees = 0;
	uint64_t reuses = 0;
	uint64_t multisplits = 0;
	uint64_t longest = 0;
	selftest_rng_t rng;

	m.m_tags = safe_calloc(nkeys * sizeof (uint64_t));
	selftest_rng_init(&rng, stream);
	for (uint64_t i = 0; i < MODEL_OPS_PER_KEY * nkeys; i++) {
		lh_op_t op = (lh_op_t)selftest_rng_below(&rng, LH_NUM_OPS);
		uint64_t keyno = selftest_rng_below(&rng, nkeys);
		uint64_t nfree = heap_count(freeheap);
		uint64_t buckets = lh->lh_occupancy.top_buckets;

		model_op(&m, op, keyno);
		frees += heap_count(freeheap) > nfree;
		reuses += heap_count(freeheap) < nfree;
		multisplits += lh->lh_occupancy.top_buckets > buckets + 1;
		if (i % AUDIT_INTERVAL == 0)
			longest = MAX(longest, audit_table(lh));
	}
	for (uint64_t k = 0; k < nkeys; k++)
		model_op(&m, LH_FIND, k);
	longest = MAX(longest, audit_table(lh));

	VERIFY3U(lh->lh_hash_suffix_length, >, m.m_suffix);
	VERIFY3U(frees, >, 0);
	VERIFY3U(reuses, >, 0);
	VERIFY3U(multisplits, >, 0);
	VERIFY3U(longest, >=, 3);
	if (dir != NULL) {
		allocator_group_t *g = lh->lh_alloc.data->a_group;
		for (unsigned i = 0; i < g->ag_num_allocators; i++) {
			allocator_t *a = g->ag_allocators[i];
			VERIFY3U(a->a_first_disk_cluster *
			    a->a_records_per_cluster, <, a->a_record_count);
		}
	}
	free(m.m_tags);
	lh_fini(lh);
}

static void
hash_model_memory(void)
{
	model_run(MEMORY_ONLY, NULL, 1);
}

/* Memory is a small fraction of what the table needs */
static void
hash_model_spill(void)
{
	model_run(SPILL_MEMORY, selftest_scratch_dir(), 2);
}

const test_case_t selftest_hash_cases[] = {
	{ "hash_invalid_config",	hash_invalid_config },
	{ "hash_key_prefix",		hash_key_prefix },
	{ "hash_chain_ends",		hash_chain_ends },
	{ "hash_model_memory",		hash_model_memory },
	{ "hash_model_spill",		hash_model_spill },
	{ NULL,				NULL },
};
