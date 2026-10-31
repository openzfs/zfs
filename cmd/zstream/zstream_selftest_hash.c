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
 * Self-tests for linear_hash_t and minheap_t.
 *
 * A linear_hash_t is a map from fixed-size keys, which are prefixes of the
 * records themselves, to records. The tests run tables against a shadow
 * model that tracks a range of "key numbers". For each one, the model
 * knows whether the key is present and which version of its record the
 * table should hold. Keys are pure functions of key numbers, and records
 * are pure functions of (key number, version), so the model is small and
 * any record can be regenerated for comparison.
 *
 * Every record operation goes through ht_op(), which checks the result
 * code against the model and also checks the buffer contract in
 * zstream_hash.h: on LH_FOUND or LH_UPDATED, the buffer holds the
 * table's previous record, and otherwise it must be untouched.
 *
 * lh_record() reduces keys to 64-bit hashes with lh->lh_hash_func, which
 * lh_init() sets to CityHash. Tests that need to choose hash values install
 * word0_hash() instead, which uses the key's first eight bytes as its hash.
 * Keys in that mode are { chosen hash, key number, ... }, so distinct keys
 * can be made to share low-order bits (bucket collisions) or entire hashes
 * (full collisions, which only the full-key comparison can resolve).
 *
 * ht_validate() complements this black-box view with a structural audit.
 * It walks bucket chains with its own code rather than the table's
 * entry_iterator_t, so a bug in the iterator can't hide from it, and it
 * checks every stored record against the model.
 *
 * The tests also run tables under memory pressure. The table itself does
 * no memory management; its three allocators form one allocator group,
 * which must give memory up in priority order (data first, then overflow
 * buckets, then main buckets) while the table remains fully correct.
 * Group state is read through zstream_alloc_impl.h.
 *
 * The fatal paths (bad parameters, a bad opcode, a free heap given bucket
 * 0, and a memory-only table that outgrows its budget) run in child
 * processes that must exit with the expected message.
 *
 * The free heap of overflow buckets is also tested on its own, through
 * minheap_push() and minheap_pop().
 */

#include <cityhash.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/sysmacros.h>

#include "zstream_alloc_impl.h"
#include "zstream_hash_impl.h"
#include "zstream_selftest.h"
#include "zstream_util.h"

#define	MAX_ABSENT_PROBES	1000	/* Never-inserted key numbers */
#define	POISON			0xa5	/* Fill for non-key bytes of a FIND */

typedef enum {
	HM_CITY,	/* The table's own CityHash */
	HM_WORD0,	/* The key's first eight bytes are its hash */
} hash_mode_t;

typedef struct {
	size_t		hc_record_size;
	size_t		hc_key_size;
	size_t		hc_max_memory;
	boolean_t	hc_no_disk;	/* Pass a NULL spill directory */
	hash_mode_t	hc_hash_mode;
	uint64_t	hc_hash_space;	/* HM_WORD0: distinct hashes; 0 = any */
	uint64_t	hc_fixed_mask;	/* HM_WORD0: hash bits forced to... */
	uint64_t	hc_fixed_bits;	/* ...these values... */
	uint64_t	hc_fixed_every;	/* ...for every Nth key; 0 = all */
	unsigned	hc_initial_suffix; /* Start: 2^N buckets; 0 = default */
	uint64_t	hc_rng_stream;
} htest_config_t;

typedef struct {
	linear_hash_t	*ht_lh;
	htest_config_t	ht_cfg;
	uint64_t	ht_nkeys;	/* Tracked: key numbers [0, ht_nkeys) */
	uint64_t	ht_nprobes;	/* Absent key numbers after ht_nkeys */
	uint32_t	*ht_version;	/* Per key number; 0 = absent */
	uint32_t	ht_next_version;
	uint64_t	ht_present;	/* Number of keys in the table */
	uint64_t	*ht_hashes;	/* HM_WORD0: hash of each key number */
	uint64_t	ht_salt;	/* Varies HM_CITY keys by seed */
	uint8_t		*ht_buf;	/* Scratch: the buffer passed in */
	uint8_t		*ht_before;	/* Scratch: ht_buf before the call */
	uint8_t		*ht_expect;	/* Scratch: an expected record */
	selftest_rng_t	ht_rng;
} htest_t;

static const char *op_names[] = {
	"LH_FIND", "LH_INSERT", "LH_UPDATE", "LH_UPSERT"
};

static const char *result_names[] = {
	"LH_NOT_FOUND", "LH_FOUND", "LH_INSERTED", "LH_UPDATED"
};

/*
 * The hash function that test tables install in HM_WORD0 mode
 */
static uint64_t
word0_hash(const uint64_t key[MAX_KEY_SIZE / 8])
{
	return (key[0]);
}

/*
 * The cluster size lh_init() should choose, restated: budgets up to 1MB get
 * the smallest clusters that hold a data record or a bucket, and bigger
 * ones get the default, 1MB or one page if pages are bigger.
 */
static size_t
expected_lh_cluster(size_t record_size, size_t max_memory)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);

	if (max_memory > (1 << 20))
		return (MAX(1 << 20, page));
	return (roundup(MAX(record_size, sizeof (bucket_t)), page));
}

/*
 * The hash a key should have, restated independently of the table
 */
static uint64_t
expected_hash(const htest_t *ht, const uint8_t *key)
{
	uint64_t words[MAX_KEY_SIZE / 8] = { 0 };

	memcpy(words, key, ht->ht_cfg.hc_key_size);
	if (ht->ht_cfg.hc_hash_mode == HM_WORD0)
		return (words[0]);
	return (cityhash4(words[0], words[1], words[2], words[3]));
}

/*
 * The destination bucket for a hash, restated independently of
 * bucket_for_hash() in zstream_hash.c. Below the split pointer the mask is
 * one bit longer than above it.
 */
static uint64_t
expected_bucket(linear_hash_t *lh, uint64_t hash)
{
	uint64_t mask = (1ULL << lh->lh_hash_suffix_length) - 1;

	if ((hash & mask) < lh->lh_split_pointer)
		mask = (mask << 1) | 1;
	return (hash & mask);
}

/*
 * Little-endian byte encoding, so that keys mean the same thing on every
 * platform no matter how many of their bytes are used
 */
static void
put_le(uint8_t *buf, size_t len, uint64_t val)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (uint8_t)(val >> (8 * i));
}

static uint64_t
get_le(const uint8_t *buf, size_t len)
{
	uint64_t val = 0;
	for (size_t i = 0; i < len; i++)
		val |= (uint64_t)buf[i] << (8 * i);
	return (val);
}

/*
 * The bytes of a key that hold its identity: the whole key number in
 * HM_WORD0 mode, and up to eight bytes of it in HM_CITY mode
 */
static size_t
id_offset(const htest_t *ht)
{
	return ((ht->ht_cfg.hc_hash_mode == HM_WORD0) ? 8 : 0);
}

static size_t
id_length(const htest_t *ht)
{
	return ((ht->ht_cfg.hc_hash_mode == HM_WORD0) ? 8 :
	    MIN(ht->ht_cfg.hc_key_size, 8));
}

/*
 * Number of distinct key numbers that the key encoding can represent
 */
static uint64_t
key_space(const htest_config_t *cfg)
{
	if (cfg->hc_hash_mode == HM_WORD0 || cfg->hc_key_size >= 8)
		return (UINT64_MAX);
	return (1ULL << (8 * cfg->hc_key_size));
}

static void
make_key(const htest_t *ht, uint64_t n, uint8_t *key)
{
	size_t ks = ht->ht_cfg.hc_key_size;
	size_t off = id_offset(ht);
	size_t len = id_length(ht);

	if (ht->ht_cfg.hc_hash_mode == HM_WORD0) {
		/* The table's hash function reads this as a native word */
		memcpy(key, &ht->ht_hashes[n], sizeof (uint64_t));
		put_le(key + off, len, n);
	} else {
		put_le(key, len, n ^ ht->ht_salt);
	}
	selftest_fill(key + off + len, ks - off - len, selftest_mix64(n));
}

/*
 * Recovers the key number from a key, or returns UINT64_MAX if the key
 * isn't one that make_key() could have produced
 */
static uint64_t
key_number(const htest_t *ht, const uint8_t *key)
{
	size_t len = id_length(ht);
	uint64_t n = get_le(key + id_offset(ht), len);
	uint8_t check[MAX_KEY_SIZE];

	if (ht->ht_cfg.hc_hash_mode == HM_CITY) {
		n ^= ht->ht_salt;
		if (len < 8)
			n &= (1ULL << (8 * len)) - 1;
	}
	if (n >= ht->ht_nkeys + ht->ht_nprobes)
		return (UINT64_MAX);
	make_key(ht, n, check);
	if (memcmp(check, key, ht->ht_cfg.hc_key_size) != 0)
		return (UINT64_MAX);
	return (n);
}

static void
make_record(const htest_t *ht, uint64_t n, uint32_t version, uint8_t *rec)
{
	size_t ks = ht->ht_cfg.hc_key_size;

	make_key(ht, n, rec);
	selftest_fill(rec + ks, ht->ht_cfg.hc_record_size - ks,
	    selftest_mix64(n) ^ ((uint64_t)version << 32));
}

/*
 * A new table has 2^INITIAL_HASH_SUFFIX_LENGTH buckets and doesn't split
 * until they are 75% full, so a table of a few thousand keys never splits
 * at all. Like the hash function, the starting size can be changed before
 * the first record operation, while every bucket is still empty and
 * unwritten. A table started at 2^suffix buckets splits early and passes
 * through many split cycles. The split threshold is proportional to the
 * bucket count, and split_bucket() recomputes it after every split anyway.
 */
static void
shrink_new_table(linear_hash_t *lh, unsigned suffix)
{
	lh_occupancy_t *occ = &lh->lh_occupancy;
	unsigned shift = lh->lh_hash_suffix_length - suffix;

	VERIFY3U(suffix, >, 0);
	VERIFY3U(suffix, <, lh->lh_hash_suffix_length);
	VERIFY0(occ->top_entries);
	VERIFY0(lh->lh_split_pointer);
	VERIFY0(lh->lh_alloc.bucket->a_count);
	lh->lh_hash_suffix_length = suffix;
	occ->top_buckets >>= shift;
	occ->max_top_entries >>= shift;
}

/*
 * Create a table and its model, with nkeys tracked key numbers followed by
 * (up to) MAX_ABSENT_PROBES key numbers that are never inserted
 */
static htest_t *
ht_init(const htest_config_t *cfg, uint64_t nkeys)
{
	htest_t *ht = safe_calloc(sizeof (htest_t));
	size_t rs = cfg->hc_record_size;
	uint64_t space = key_space(cfg);

	VERIFY(cfg->hc_hash_mode == HM_CITY || cfg->hc_key_size >= 16);
	VERIFY3U(nkeys, <=, space);
	ht->ht_cfg = *cfg;
	ht->ht_nkeys = nkeys;
	ht->ht_nprobes = MIN(MAX_ABSENT_PROBES, space - nkeys);
	ht->ht_next_version = 1;
	selftest_rng_init(&ht->ht_rng, cfg->hc_rng_stream);
	ht->ht_salt = selftest_rng_next(&ht->ht_rng);
	ht->ht_version = safe_calloc((nkeys + 1) * sizeof (uint32_t));
	ht->ht_buf = safe_malloc(rs);
	ht->ht_before = safe_malloc(rs);
	ht->ht_expect = safe_malloc(rs);

	if (cfg->hc_hash_mode == HM_WORD0) {
		uint64_t total = nkeys + ht->ht_nprobes;
		uint64_t space_n = cfg->hc_hash_space;
		uint64_t *pool = NULL;
		if (space_n > 0) {
			pool = safe_malloc(space_n * sizeof (uint64_t));
			for (uint64_t i = 0; i < space_n; i++)
				pool[i] = selftest_rng_next(&ht->ht_rng);
		}
		ht->ht_hashes = safe_malloc(total * sizeof (uint64_t));
		for (uint64_t n = 0; n < total; n++) {
			uint64_t h = (pool != NULL) ?
			    pool[selftest_rng_below(&ht->ht_rng, space_n)] :
			    selftest_rng_next(&ht->ht_rng);
			boolean_t fix = cfg->hc_fixed_mask != 0 &&
			    (cfg->hc_fixed_every == 0 ||
			    n % cfg->hc_fixed_every == 0);
			if (fix) {
				h = (h & ~cfg->hc_fixed_mask) |
				    (cfg->hc_fixed_bits & cfg->hc_fixed_mask);
			}
			ht->ht_hashes[n] = h;
		}
		free(pool);
	}

	ht->ht_lh = lh_init(rs, cfg->hc_key_size, cfg->hc_max_memory,
	    cfg->hc_no_disk ? NULL : selftest_scratch_dir());
	VERIFY3U(ht->ht_lh->lh_alloc.data->a_group->ag_cluster_size, ==,
	    expected_lh_cluster(rs, cfg->hc_max_memory));
	if (cfg->hc_hash_mode == HM_WORD0)
		ht->ht_lh->lh_hash_func = word0_hash;
	if (cfg->hc_initial_suffix != 0)
		shrink_new_table(ht->ht_lh, cfg->hc_initial_suffix);
	return (ht);
}

static void
ht_fini(htest_t *ht)
{
	lh_fini(ht->ht_lh);
	free(ht->ht_version);
	free(ht->ht_hashes);
	free(ht->ht_buf);
	free(ht->ht_before);
	free(ht->ht_expect);
	free(ht);
}

/*
 * Perform one operation on key number n and check the outcome against the
 * model. Write operations store a new version of the key's record. FIND
 * passes the key with poisoned non-key bytes, which the table must ignore
 * and, if the key is absent, leave alone. Key numbers past ht_nkeys are
 * never-inserted probes and may only be used with LH_FIND.
 */
static lh_result_t
ht_op(htest_t *ht, lh_op_t op, uint64_t n)
{
	size_t rs = ht->ht_cfg.hc_record_size;
	uint32_t old = (n < ht->ht_nkeys) ? ht->ht_version[n] : 0;
	uint32_t version = 0;

	VERIFY3U(n, <, ht->ht_nkeys + ht->ht_nprobes);
	if (op == LH_FIND) {
		memset(ht->ht_buf, POISON, rs);
		make_key(ht, n, ht->ht_buf);
	} else {
		VERIFY3U(n, <, ht->ht_nkeys);
		version = ht->ht_next_version++;
		make_record(ht, n, version, ht->ht_buf);
	}
	memcpy(ht->ht_before, ht->ht_buf, rs);

	lh_result_t got = lh_record(ht->ht_lh, op, ht->ht_buf);
	VERIFY3U(got, <=, LH_UPDATED);

	boolean_t present = (old != 0);
	lh_result_t want;
	switch (op) {
	case LH_FIND:
		want = present ? LH_FOUND : LH_NOT_FOUND;
		break;
	case LH_INSERT:
		want = present ? LH_FOUND : LH_INSERTED;
		break;
	case LH_UPDATE:
		want = present ? LH_UPDATED : LH_NOT_FOUND;
		break;
	default:
		VERIFY3S(op, ==, LH_UPSERT);
		want = present ? LH_UPDATED : LH_INSERTED;
		break;
	}
	if (got != want) {
		errx(1, "%s of key %llu returned %s, expected %s",
		    op_names[op], (u_longlong_t)n, result_names[got],
		    result_names[want]);
	}

	if (got == LH_FOUND || got == LH_UPDATED) {
		make_record(ht, n, old, ht->ht_expect);
		if (memcmp(ht->ht_buf, ht->ht_expect, rs) != 0) {
			errx(1, "%s of key %llu returned the wrong record",
			    op_names[op], (u_longlong_t)n);
		}
	} else if (memcmp(ht->ht_buf, ht->ht_before, rs) != 0) {
		errx(1, "%s of key %llu (%s) modified the buffer",
		    op_names[op], (u_longlong_t)n, result_names[got]);
	}

	if (got == LH_INSERTED) {
		ht->ht_version[n] = version;
		ht->ht_present++;
	} else if (got == LH_UPDATED) {
		ht->ht_version[n] = version;
	}
	return (got);
}

/*
 * A uniformly chosen tracked key number, present or not
 */
static uint64_t
ht_random_key(htest_t *ht)
{
	return (selftest_rng_below(&ht->ht_rng, ht->ht_nkeys));
}

/*
 * A uniformly chosen never-inserted key number
 */
static uint64_t
ht_random_probe(htest_t *ht)
{
	VERIFY3U(ht->ht_nprobes, >, 0);
	return (ht->ht_nkeys + selftest_rng_below(&ht->ht_rng,
	    ht->ht_nprobes));
}

/*
 * Insert key numbers [first, first + count). Every spot_every inserts, look
 * up a random tracked key and a random probe to catch damage early.
 */
static void
ht_insert_range(htest_t *ht, uint64_t first, uint64_t count,
    uint64_t spot_every)
{
	for (uint64_t n = first; n < first + count; n++) {
		VERIFY3S(ht_op(ht, LH_INSERT, n), ==, LH_INSERTED);
		if (spot_every != 0 && (n + 1) % spot_every == 0) {
			(void) ht_op(ht, LH_FIND, ht_random_key(ht));
			if (ht->ht_nprobes > 0)
				(void) ht_op(ht, LH_FIND, ht_random_probe(ht));
		}
	}
}

/*
 * Write a new version of count randomly chosen keys, alternating between
 * LH_UPDATE and LH_UPSERT
 */
static void
ht_rewrite_random(htest_t *ht, uint64_t count)
{
	for (uint64_t i = 0; i < count; i++)
		(void) ht_op(ht, (i & 1) ? LH_UPSERT : LH_UPDATE,
		    ht_random_key(ht));
}

/*
 * Look up every tracked key and every probe
 */
static void
ht_verify_all(htest_t *ht)
{
	for (uint64_t n = 0; n < ht->ht_nkeys + ht->ht_nprobes; n++)
		(void) ht_op(ht, LH_FIND, n);
}

/*
 * Count (and describe) violations of the free heap's shape: more elements
 * than slots, a zero index, or an element larger than one of its children.
 * Pops take the root, so any of these can hand out the wrong bucket.
 */
static uint64_t
heap_problems(const minheap_t *heap)
{
	uint64_t problems = 0;

	if (heap->count > heap->size) {
		warnx("free heap holds %llu buckets in %llu slots",
		    (u_longlong_t)heap->count, (u_longlong_t)heap->size);
		return (1);
	}
	for (uint64_t i = 0; i < heap->count; i++) {
		if (heap->buckets[i] == 0) {
			warnx("free heap slot %llu holds bucket 0",
			    (u_longlong_t)i);
			problems++;
		}
		if (i > 0 && heap->buckets[(i - 1) / 2] > heap->buckets[i]) {
			warnx("free heap slot %llu (bucket %llu) is smaller "
			    "than its parent (bucket %llu)", (u_longlong_t)i,
			    (u_longlong_t)heap->buckets[i],
			    (u_longlong_t)heap->buckets[(i - 1) / 2]);
			problems++;
		}
	}
	return (problems);
}

/*
 * Structural audit of an entire table. Walks every top-level bucket and its
 * overflow chain directly through the allocators, and checks that:
 *
 * - Entries are packed at the front of each chain, with no live entry
 *   following an empty one. (Scans stop at the first empty entry, so a gap
 *   would strand everything after it.)
 *
 * - No linked overflow bucket is empty.
 *
 * - Every entry sits in the bucket its stored hash maps to, and the stored
 *   hash is the hash of the stored record's key.
 *
 * - Every stored record is the current version of a present key, and no
 *   key is stored twice.
 *
 * - The entry total matches the model, and so do the data allocator's
 *   record count (updates rewrite records in place rather than appending)
 *   and the record count from lh_get_stats().
 *
 * - lh_occupancy.top_entries and lh_occupancy.top_buckets agree with what
 *   is actually stored. Those drive split decisions, so drift in either one
 *   silently stops the table from growing.
 *
 * - Every overflow bucket ever allocated is either in exactly one chain or
 *   on the free heap, exactly once.
 *
 * - The free heap has the min-heap shape (see heap_problems()).
 */
static void
ht_validate(htest_t *ht)
{
	linear_hash_t *lh = ht->ht_lh;
	size_t rs = ht->ht_cfg.hc_record_size;
	uint64_t novf = lh->lh_alloc.overflow->a_count;
	uint64_t ndata = lh->lh_alloc.data->a_count;
	uint8_t *ovf_seen = safe_calloc(novf);
	uint8_t *key_seen = safe_calloc(ht->ht_nkeys + 1);
	uint8_t *rec = safe_malloc(rs);
	uint64_t total = 0;
	uint64_t total_top = 0;
	uint64_t problems = 0;

	for (uint64_t b = 0; b < lh->lh_occupancy.top_buckets; b++) {
		bucket_t bucket;
		boolean_t top_level = B_TRUE;
		boolean_t seen_empty = B_FALSE;

		allocator_retrieve(lh->lh_alloc.bucket, b, &bucket);
		while (B_TRUE) {
			for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
				bucket_entry_t *e = &bucket.b_entries[i];
				if (e->be_record == 0) {
					seen_empty = B_TRUE;
					continue;
				}
				total++;
				if (top_level)
					total_top++;
				if (seen_empty) {
					warnx("bucket %llu has uncompacted "
					    "entries", (u_longlong_t)b);
					problems++;
				}
				if (expected_bucket(lh, e->be_hash) != b) {
					warnx("bucket %llu holds hash %llx, "
					    "which belongs elsewhere",
					    (u_longlong_t)b,
					    (u_longlong_t)e->be_hash);
					problems++;
				}
				if (e->be_record >= ndata) {
					warnx("bucket %llu points to record "
					    "%llu, past the end of the data",
					    (u_longlong_t)b,
					    (u_longlong_t)e->be_record);
					problems++;
					continue;
				}
				allocator_retrieve(lh->lh_alloc.data,
				    e->be_record, rec);
				if (expected_hash(ht, rec) != e->be_hash) {
					warnx("record %llu is stored under the "
					    "wrong hash",
					    (u_longlong_t)e->be_record);
					problems++;
				}
				uint64_t n = key_number(ht, rec);
				if (n >= ht->ht_nkeys ||
				    ht->ht_version[n] == 0) {
					warnx("record %llu has an unknown or "
					    "absent key",
					    (u_longlong_t)e->be_record);
					problems++;
					continue;
				}
				if (key_seen[n]++ != 0) {
					warnx("key %llu is stored twice",
					    (u_longlong_t)n);
					problems++;
				}
				make_record(ht, n, ht->ht_version[n],
				    ht->ht_expect);
				if (memcmp(rec, ht->ht_expect, rs) != 0) {
					warnx("record for key %llu is not the "
					    "current version", (u_longlong_t)n);
					problems++;
				}
			}
			record_ix_t next = bucket.b_overflow;
			if (next == 0)
				break;
			if (seen_empty) {
				warnx("bucket %llu continues past an empty "
				    "entry", (u_longlong_t)b);
				problems++;
			}
			if (next >= novf || ovf_seen[next]++ != 0) {
				warnx("bucket %llu links to overflow bucket "
				    "%llu, which is invalid or already in use",
				    (u_longlong_t)b, (u_longlong_t)next);
				problems++;
				break;
			}
			allocator_retrieve(lh->lh_alloc.overflow, next,
			    &bucket);
			top_level = B_FALSE;
			if (bucket.b_entries[0].be_record == 0) {
				warnx("bucket %llu links to an empty overflow "
				    "bucket", (u_longlong_t)b);
				problems++;
			}
		}
	}

	problems += heap_problems(&lh->lh_freeheap);
	for (uint64_t i = 0; i < lh->lh_freeheap.count; i++) {
		record_ix_t ix = lh->lh_freeheap.buckets[i];
		if (ix == 0 || ix >= novf || ovf_seen[ix]++ != 0) {
			warnx("free overflow bucket %llu is invalid, linked, "
			    "or free twice", (u_longlong_t)ix);
			problems++;
		}
	}
	for (uint64_t ix = 1; ix < novf; ix++) {
		if (ovf_seen[ix] == 0) {
			warnx("overflow bucket %llu was leaked",
			    (u_longlong_t)ix);
			problems++;
		}
	}

	if (total != ht->ht_present) {
		warnx("table holds %llu entries, but %llu keys are present",
		    (u_longlong_t)total, (u_longlong_t)ht->ht_present);
		problems++;
	}
	if (ndata != ht->ht_present + 1) {
		warnx("data allocator holds %llu records for %llu keys",
		    (u_longlong_t)ndata - 1, (u_longlong_t)ht->ht_present);
		problems++;
	}
	lh_stats_t stats = lh_get_stats(lh);
	if (stats.ls_total_records != ht->ht_present) {
		warnx("table reports %llu records for %llu keys",
		    (u_longlong_t)stats.ls_total_records,
		    (u_longlong_t)ht->ht_present);
		problems++;
	}
	if (total_top != lh->lh_occupancy.top_entries) {
		warnx("table claims %llu top-level entries, but has %llu",
		    (u_longlong_t)lh->lh_occupancy.top_entries,
		    (u_longlong_t)total_top);
		problems++;
	}

	/*
	 * The table is 2^suffix buckets wide at the start of each split
	 * cycle and has grown by one bucket for each split performed since.
	 */
	uint64_t want_buckets = (1ULL << lh->lh_hash_suffix_length) +
	    lh->lh_split_pointer;
	if (lh->lh_occupancy.top_buckets != want_buckets) {
		warnx("table has %llu top-level buckets, but its split state "
		    "implies %llu", (u_longlong_t)lh->lh_occupancy.top_buckets,
		    (u_longlong_t)want_buckets);
		problems++;
	}

	free(ovf_seen);
	free(key_seen);
	free(rec);
	if (problems != 0)
		errx(1, "table failed validation (%llu problems)",
		    (u_longlong_t)problems);
}

/*
 * The table's allocator group, audited. lh_init() must create the
 * allocators in eviction priority order: data first, then overflow, then
 * main buckets. lh_get_stats() must report the group's memory use.
 */
static allocator_group_t *
hash_group(linear_hash_t *lh)
{
	allocator_group_t *g = lh->lh_alloc.data->a_group;

	selftest_audit_group(lh->lh_alloc.data);
	VERIFY3U(g->ag_num_allocators, ==, 3);
	VERIFY3P(g->ag_allocators[0], ==, lh->lh_alloc.data);
	VERIFY3P(g->ag_allocators[1], ==, lh->lh_alloc.overflow);
	VERIFY3P(g->ag_allocators[2], ==, lh->lh_alloc.bucket);
	VERIFY3U(lh->lh_alloc.data->a_record_size, ==, lh->lh_record_size);
	VERIFY3U(lh->lh_alloc.overflow->a_record_size, ==, sizeof (bucket_t));
	VERIFY3U(lh->lh_alloc.bucket->a_record_size, ==, sizeof (bucket_t));
	VERIFY3U(lh_get_stats(lh).ls_memory_use, ==, g->ag_mem_current +
	    lh->lh_freeheap.size * sizeof (record_ix_t));
	return (g);
}

static size_t
alloc_frontier(allocator_t *a)
{
	return ((size_t)a->a_writable_frontier);
}

static size_t
alloc_fod(allocator_t *a)
{
	return ((size_t)a->a_first_on_disk);
}

/*
 * The standard workload: insert every tracked key with spot checks,
 * rewrite a fraction of them, then verify and validate
 */
static void
ht_workload(htest_t *ht, uint64_t rewrites)
{
	ht_insert_range(ht, 0, ht->ht_nkeys, 512);
	ht_rewrite_random(ht, rewrites);
	ht_verify_all(ht);
	ht_validate(ht);
	(void) hash_group(ht->ht_lh);
}

static void
raw_record(uint8_t *rec, size_t rs, size_t ks, uint8_t key_fill,
    uint64_t payload_tag)
{
	memset(rec, key_fill, ks);
	selftest_fill(rec + ks, rs - ks, payload_tag);
}

/*
 * The basic operations, by hand and then at modest scale. The by-hand
 * section works without the model, to make sure the model's notion of
 * correct behavior isn't itself wrong. It also stores keys made entirely of
 * zero bytes and of 0xff bytes.
 */
static void
hash_basic(void)
{
	const size_t rs = 40;
	const size_t ks = 32;
	linear_hash_t *lh = lh_init(rs, ks, 64 << 20, selftest_scratch_dir());
	uint8_t zero[40], ones[40], buf[40];

	/* A new table holds nothing and uses memory only for the free heap */
	lh_stats_t stats = lh_get_stats(lh);
	size_t freeheap_size = lh->lh_freeheap.size * sizeof (record_ix_t);
	VERIFY0(stats.ls_total_records);
	VERIFY3U(stats.ls_memory_use, ==, freeheap_size);

	raw_record(zero, rs, ks, 0x00, 1);
	raw_record(ones, rs, ks, 0xff, 2);

	memcpy(buf, zero, rs);
	VERIFY3S(lh_find(lh, buf), ==, LH_NOT_FOUND);
	VERIFY0(memcmp(buf, zero, rs));
	VERIFY3S(lh_update(lh, buf), ==, LH_NOT_FOUND);
	VERIFY3S(lh_find(lh, buf), ==, LH_NOT_FOUND);
	VERIFY3S(lh_insert(lh, buf), ==, LH_INSERTED);
	VERIFY0(memcmp(buf, zero, rs));

	/* A second insert under the same key returns the first record */
	raw_record(buf, rs, ks, 0x00, 3);
	VERIFY3S(lh_insert(lh, buf), ==, LH_FOUND);
	VERIFY0(memcmp(buf, zero, rs));

	/* An update stores the new record and returns the old one */
	raw_record(buf, rs, ks, 0x00, 4);
	VERIFY3S(lh_update(lh, buf), ==, LH_UPDATED);
	VERIFY0(memcmp(buf, zero, rs));
	memset(buf + ks, POISON, rs - ks);
	VERIFY3S(lh_find(lh, buf), ==, LH_FOUND);
	raw_record(zero, rs, ks, 0x00, 4);
	VERIFY0(memcmp(buf, zero, rs));

	/* Upserts insert or update */
	memcpy(buf, ones, rs);
	VERIFY3S(lh_upsert(lh, buf), ==, LH_INSERTED);
	VERIFY0(memcmp(buf, ones, rs));
	raw_record(buf, rs, ks, 0xff, 5);
	VERIFY3S(lh_upsert(lh, buf), ==, LH_UPDATED);
	VERIFY0(memcmp(buf, ones, rs));
	memset(buf + ks, 0, rs - ks);
	VERIFY3S(lh_find(lh, buf), ==, LH_FOUND);
	raw_record(ones, rs, ks, 0xff, 5);
	VERIFY0(memcmp(buf, ones, rs));

	/* The zero key is still intact */
	memset(buf, 0, rs);
	VERIFY3S(lh_find(lh, buf), ==, LH_FOUND);
	VERIFY0(memcmp(buf, zero, rs));
	VERIFY3U(lh->lh_alloc.data->a_count, ==, 3);

	/* Two records, in one cluster of data and one of top buckets */
	allocator_group_t *g = hash_group(lh);
	VERIFY3U(g->ag_mem_limit, ==, roundup(64 << 20, g->ag_cluster_size));
	stats = lh_get_stats(lh);
	size_t heap_size = lh->lh_freeheap.size * sizeof (record_ix_t);
	VERIFY3U(stats.ls_total_records, ==, 2);
	VERIFY3U(stats.ls_memory_use, ==, 2 * g->ag_cluster_size + heap_size);
	lh_fini(lh);

	/* The same sequence through the model, which ht_op() checks */
	htest_config_t cfg = {
		.hc_record_size = 64,
		.hc_key_size = 16,
		.hc_max_memory = 64 << 20,
		.hc_rng_stream = 1,
	};
	htest_t *ht = ht_init(&cfg, 8);
	VERIFY3S(ht_op(ht, LH_FIND, 0), ==, LH_NOT_FOUND);
	VERIFY3S(ht_op(ht, LH_UPDATE, 0), ==, LH_NOT_FOUND);
	VERIFY3S(ht_op(ht, LH_INSERT, 0), ==, LH_INSERTED);
	VERIFY3S(ht_op(ht, LH_INSERT, 0), ==, LH_FOUND);
	VERIFY3S(ht_op(ht, LH_UPDATE, 0), ==, LH_UPDATED);
	VERIFY3S(ht_op(ht, LH_UPSERT, 0), ==, LH_UPDATED);
	VERIFY3S(ht_op(ht, LH_UPSERT, 1), ==, LH_INSERTED);
	VERIFY3S(ht_op(ht, LH_FIND, 1), ==, LH_FOUND);
	ht_verify_all(ht);
	ht_validate(ht);
	ht_fini(ht);

	/* And at modest scale */
	cfg.hc_rng_stream = 2;
	ht = ht_init(&cfg, 20000);
	ht_workload(ht, 5000);
	ht_fini(ht);
}

/*
 * Every operation against present and absent keys, in a table where every
 * key has the same 64-bit hash. Each operation must find the right record
 * among many candidates that differ only past the hash, and must leave the
 * buffer alone when it doesn't find one. The chain is long enough to span
 * several overflow buckets, and keys at its start, middle, and end are
 * exercised. Record size equal to key size is covered too.
 */
static void
hash_ops(void)
{
	static const size_t record_sizes[] = { 48, 16 };

	for (int r = 0; r < 2; r++) {
		htest_config_t cfg = {
			.hc_record_size = record_sizes[r],
			.hc_key_size = 16,
			.hc_max_memory = 16 << 20,
			.hc_hash_mode = HM_WORD0,
			.hc_hash_space = 1,
			.hc_rng_stream = 10 + r,
		};
		const uint64_t nkeys = 6 * ENTRIES_PER_BUCKET + 5;
		htest_t *ht = ht_init(&cfg, nkeys);

		/* Absent keys, then insert every other key */
		for (uint64_t n = 0; n < nkeys; n++) {
			VERIFY3S(ht_op(ht, LH_FIND, n), ==, LH_NOT_FOUND);
			VERIFY3S(ht_op(ht, LH_UPDATE, n), ==, LH_NOT_FOUND);
		}
		for (uint64_t n = 0; n < nkeys; n += 2)
			VERIFY3S(ht_op(ht, LH_INSERT, n), ==, LH_INSERTED);
		ht_validate(ht);

		/* Each operation on present and absent keys */
		for (uint64_t n = 0; n < nkeys; n++) {
			boolean_t present = (n % 2 == 0);
			VERIFY3S(ht_op(ht, LH_FIND, n), ==,
			    present ? LH_FOUND : LH_NOT_FOUND);
			VERIFY3S(ht_op(ht, LH_UPDATE, n), ==,
			    present ? LH_UPDATED : LH_NOT_FOUND);
			VERIFY3S(ht_op(ht, LH_INSERT, n), ==,
			    present ? LH_FOUND : LH_INSERTED);
			VERIFY3S(ht_op(ht, LH_UPSERT, n), ==, LH_UPDATED);
			VERIFY3S(ht_op(ht, LH_FIND, n), ==, LH_FOUND);
		}
		for (uint64_t n = 0; n < ht->ht_nprobes; n++)
			VERIFY3S(ht_op(ht, LH_FIND, nkeys + n), ==,
			    LH_NOT_FOUND);
		ht_validate(ht);
		VERIFY3U(ht->ht_present, ==, nkeys);
		VERIFY3U(ht->ht_lh->lh_alloc.overflow->a_count, >, 5);
		ht_fini(ht);
	}
}

/*
 * Key sizes from 1 to MAX_KEY_SIZE bytes, including sizes that aren't
 * multiples of 8, with records equal to the key, one byte longer, and
 * much longer. One-byte and two-byte keys use up most of their key
 * spaces.
 *
 * Also checks by hand that the last byte of the key is significant and
 * that the first byte past it is not. For keys longer than eight bytes,
 * this uses word0_hash(), so the two keys have the same hash and only the
 * full-key comparison can tell them apart.
 */
static void
hash_key_sizes(void)
{
	static const size_t key_sizes[] = { 1, 2, 3, 7, 8, 9, 15, 16, 17, 24,
	    31, MAX_KEY_SIZE };

	for (int k = 0; k < ARRAY_SIZE(key_sizes); k++) {
		size_t ks = key_sizes[k];
		size_t record_sizes[] = { ks, ks + 1, ks + 100 };
		for (int r = 0; r < ARRAY_SIZE(record_sizes); r++) {
			htest_config_t cfg = {
				.hc_record_size = record_sizes[r],
				.hc_key_size = ks,
				.hc_max_memory = 32 << 20,
				.hc_rng_stream = 100 + 4 * k + r,
			};
			uint64_t space = key_space(&cfg);
			uint64_t nkeys = MIN(3000, space - space / 8);
			htest_t *ht = ht_init(&cfg, nkeys);
			ht_workload(ht, nkeys / 3);
			ht_fini(ht);
		}

		size_t rs = ks + 8;
		linear_hash_t *lh = lh_init(rs, ks, 16 << 20, NULL);
		if (ks > 8)
			lh->lh_hash_func = word0_hash;
		uint8_t a[MAX_KEY_SIZE + 8], b[MAX_KEY_SIZE + 8];
		uint8_t buf[MAX_KEY_SIZE + 8];
		raw_record(a, rs, ks, 0x11, 1);
		raw_record(b, rs, ks, 0x11, 2);
		b[ks - 1] = 0x12;
		memcpy(buf, a, rs);
		VERIFY3S(lh_insert(lh, buf), ==, LH_INSERTED);
		memcpy(buf, b, rs);
		VERIFY3S(lh_insert(lh, buf), ==, LH_INSERTED);
		memcpy(buf, a, rs);
		buf[ks] ^= 0xff;
		VERIFY3S(lh_insert(lh, buf), ==, LH_FOUND);
		VERIFY0(memcmp(buf, a, rs));
		memcpy(buf, b, ks);
		VERIFY3S(lh_find(lh, buf), ==, LH_FOUND);
		VERIFY0(memcmp(buf, b, rs));
		lh_fini(lh);
	}
}

/*
 * Different record sizes, from 8 bytes up to the allocator's maximum, with
 * default clusters and with the page-sized clusters of a 1MB budget
 */
static void
hash_record_sizes(void)
{
	static const size_t sizes[] = { 8, 24, 40, 72, 512, 4096, 4097,
	    MAX_RECORD_SIZE };
	static const size_t budgets[] = { 256 << 20, 1 << 20 };

	for (size_t b = 0; b < ARRAY_SIZE(budgets); b++) {
		for (size_t s = 0; s < ARRAY_SIZE(sizes); s++) {
			htest_config_t cfg = {
				.hc_record_size = sizes[s],
				.hc_key_size = 8,
				.hc_max_memory = budgets[b],
				.hc_rng_stream = 200 + 10 * b + s,
			};
			htest_t *ht = ht_init(&cfg, 5000);
			ht_workload(ht, 1000);
			ht_fini(ht);
		}
	}
}

/*
 * lh_init() gives budgets of up to 1MB the smallest clusters it can, and
 * bigger budgets the default. Either way, the allocator raises a nonzero
 * budget to a cluster per allocator, and a zero budget stays disk-only.
 * The values here are worked out by hand, not by expected_lh_cluster().
 * Each table then splits through a few cycles.
 */
static void
hash_cluster_size(void)
{
	const size_t page = (size_t)sysconf(_SC_PAGESIZE);
	const size_t def = MAX(1 << 20, page);
	const size_t big = MAX(page, MAX_RECORD_SIZE);
	const struct {
		size_t	rs;
		size_t	mem;
		size_t	cluster;
		size_t	limit;
	} cases[] = {
		{ 64, 0, page, 0 },
		{ 64, 1, page, 3 * page },
		{ 64, (1 << 20) - 1, page, 1 << 20 },
		{ 64, 1 << 20, page, 1 << 20 },
		{ 64, (1 << 20) + 1, def, 3 * def },
		{ 64, 3 * def + 1, def, 4 * def },
		{ MAX_RECORD_SIZE, 1, big, 3 * big },
		{ MAX_RECORD_SIZE, 1 << 20, big, roundup(1 << 20, big) },
	};

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		htest_config_t cfg = {
			.hc_record_size = cases[i].rs,
			.hc_key_size = 8,
			.hc_max_memory = cases[i].mem,
			.hc_initial_suffix = 4,
			.hc_rng_stream = 280 + i,
		};
		htest_t *ht = ht_init(&cfg, 2000);
		allocator_group_t *g = hash_group(ht->ht_lh);
		VERIFY3U(g->ag_cluster_size, ==, cases[i].cluster);
		VERIFY3U(g->ag_mem_limit, ==, cases[i].limit);
		ht_workload(ht, 500);
		VERIFY3U(ht->ht_lh->lh_hash_suffix_length, >=, 4 + 2);
		ht_fini(ht);
	}
}

/*
 * Enough keys to force thousands of bucket splits and to carry the table
 * through several complete split cycles (suffix-length increments)
 */
static void
hash_splits(void)
{
	htest_config_t cfg = {
		.hc_record_size = 32,
		.hc_key_size = 24,
		.hc_max_memory = 256 << 20,
		.hc_rng_stream = 300,
	};
	htest_t *ht = ht_init(&cfg, 60000);
	ht_workload(ht, 10000);

	/*
	 * 60000 entries at 75% occupancy of 6-slot buckets require at
	 * least ~13000 top-level buckets, so the table must have grown
	 * from 2^10 through 2^13: at least three full split cycles.
	 */
	linear_hash_t *lh = ht->ht_lh;
	VERIFY3U(lh->lh_hash_suffix_length, >=, 13);
	VERIFY3U(lh->lh_occupancy.top_buckets, >, 1ULL << 13);

	/*
	 * Splits free overflow buckets faster than new chains reuse them, so
	 * the free heap must have outgrown its initial size.
	 */
	VERIFY3U(lh->lh_freeheap.size, >, FREEHEAP_INITIAL_SIZE);
	ht_fini(ht);
}

/*
 * A free heap and a model of its contents: which buckets in [1, hm_range)
 * it holds
 */
typedef struct {
	minheap_t	hm_heap;
	uint8_t		*hm_present;	/* Indexed by bucket */
	uint64_t	hm_range;
	uint64_t	hm_count;
} heap_model_t;

static void
hm_init(heap_model_t *hm, uint64_t initial_size, uint64_t range)
{
	*hm = (heap_model_t) {
		.hm_heap = minheap_init(initial_size),
		.hm_present = safe_calloc(range),
		.hm_range = range,
	};
}

static void
hm_fini(heap_model_t *hm)
{
	VERIFY0(hm->hm_count);
	minheap_fini(&hm->hm_heap);
	free(hm->hm_present);
}

/*
 * Push a bucket the heap doesn't hold. The heap must double its allocation
 * exactly when it is already full, and keep its shape.
 */
static void
hm_push(heap_model_t *hm, record_ix_t ix)
{
	minheap_t *heap = &hm->hm_heap;
	uint64_t size = heap->size;

	VERIFY(ix > 0 && ix < hm->hm_range && !hm->hm_present[ix]);
	minheap_push(heap, ix);
	hm->hm_present[ix] = 1;
	hm->hm_count++;
	VERIFY3U(heap->count, ==, hm->hm_count);
	VERIFY3U(heap->size, ==, (hm->hm_count > size) ? 2 * size : size);
	VERIFY0(heap_problems(heap));
}

/*
 * Pop, which must return the smallest bucket the heap holds, or 0 if it is
 * empty. The allocation never shrinks.
 */
static record_ix_t
hm_pop(heap_model_t *hm)
{
	minheap_t *heap = &hm->hm_heap;
	uint64_t size = heap->size;
	record_ix_t want = 0;

	for (record_ix_t ix = 1; hm->hm_count > 0 && want == 0; ix++) {
		if (hm->hm_present[ix])
			want = ix;
	}
	record_ix_t got = minheap_pop(heap);
	if (got != want) {
		errx(1, "free heap popped bucket %llu, expected %llu",
		    (u_longlong_t)got, (u_longlong_t)want);
	}
	if (want != 0) {
		hm->hm_present[want] = 0;
		hm->hm_count--;
	}
	VERIFY3U(heap->count, ==, hm->hm_count);
	VERIFY3U(heap->size, ==, size);
	VERIFY0(heap_problems(heap));
	return (got);
}

/*
 * Pop everything (necessarily in ascending order), then pop the empty heap
 */
static void
hm_drain(heap_model_t *hm)
{
	while (hm->hm_count > 0)
		(void) hm_pop(hm);
	VERIFY0(hm_pop(hm));
}

/*
 * The free heap on its own, against the model: pushes in ascending,
 * descending and shuffled order, each drained, then random interleavings
 * of pushes and pops that grow and shrink the heap. Every step checks the
 * popped bucket, the count, the allocation and the heap's shape.
 */
static void
heap_alone(uint64_t initial_size, uint64_t stream)
{
	const uint64_t range = 3000;
	record_ix_t *order = safe_malloc((range - 1) * sizeof (record_ix_t));
	heap_model_t hm;
	selftest_rng_t rng;

	selftest_rng_init(&rng, stream);
	hm_init(&hm, initial_size, range);
	VERIFY0(hm_pop(&hm));

	for (int pass = 0; pass < 3; pass++) {
		for (uint64_t i = 0; i < range - 1; i++)
			order[i] = (pass == 1) ? range - 1 - i : i + 1;
		for (uint64_t i = range - 2; pass == 2 && i > 0; i--) {
			uint64_t j = selftest_rng_below(&rng, i + 1);
			record_ix_t t = order[i];
			order[i] = order[j];
			order[j] = t;
		}
		for (uint64_t i = 0; i < range - 1; i++)
			hm_push(&hm, order[i]);
		hm_drain(&hm);
	}

	for (int phase = 0; phase < 4; phase++) {
		uint64_t push_percent = (phase % 2 == 0) ? 70 : 30;
		for (int op = 0; op < 5000; op++) {
			record_ix_t ix = 1 +
			    selftest_rng_below(&rng, range - 1);
			if (selftest_rng_below(&rng, 100) < push_percent &&
			    !hm.hm_present[ix])
				hm_push(&hm, ix);
			else
				(void) hm_pop(&hm);
		}
	}
	hm_drain(&hm);
	hm_fini(&hm);
	free(order);
}

/*
 * The last overflow bucket in top-level bucket b's chain, or 0 if it has
 * no overflow buckets
 */
static record_ix_t
chain_tail(linear_hash_t *lh, uint64_t b)
{
	bucket_t bucket;
	record_ix_t tail = 0;

	allocator_retrieve(lh->lh_alloc.bucket, b, &bucket);
	while (bucket.b_overflow != 0) {
		tail = bucket.b_overflow;
		allocator_retrieve(lh->lh_alloc.overflow, tail, &bucket);
	}
	return (tail);
}

/*
 * Insert key number n, which must land in an overflow bucket of top-level
 * bucket b's chain, so that the insert can't split anything. If it links
 * a new overflow bucket to the chain, that must be the smallest free one
 * (found by scanning the heap, not by trusting its root), or a fresh one
 * if none was free.
 */
static void
insert_checking_reuse(htest_t *ht, uint64_t n, uint64_t b)
{
	linear_hash_t *lh = ht->ht_lh;
	minheap_t *heap = &lh->lh_freeheap;
	uint64_t count = heap->count;
	uint64_t top_entries = lh->lh_occupancy.top_entries;
	record_ix_t fresh = lh->lh_alloc.overflow->a_count;
	record_ix_t tail = chain_tail(lh, b);
	record_ix_t min = 0;

	for (uint64_t i = 0; i < count; i++) {
		if (min == 0 || heap->buckets[i] < min)
			min = heap->buckets[i];
	}
	VERIFY3S(ht_op(ht, LH_INSERT, n), ==, LH_INSERTED);
	VERIFY3U(lh->lh_occupancy.top_entries, ==, top_entries);

	record_ix_t new_tail = chain_tail(lh, b);
	if (new_tail == tail) {
		VERIFY3U(heap->count, ==, count);
		VERIFY3U(lh->lh_alloc.overflow->a_count, ==, fresh);
	} else if (count > 0) {
		if (new_tail != min) {
			errx(1, "chain %llu reused overflow bucket %llu, but "
			    "%llu was free", (u_longlong_t)b,
			    (u_longlong_t)new_tail, (u_longlong_t)min);
		}
		VERIFY3U(heap->count, ==, count - 1);
		VERIFY3U(lh->lh_alloc.overflow->a_count, ==, fresh);
		for (uint64_t i = 0; i < heap->count; i++)
			VERIFY3U(heap->buckets[i], !=, new_tail);
	} else {
		VERIFY3U(new_tail, ==, fresh);
		VERIFY3U(lh->lh_alloc.overflow->a_count, ==, fresh + 1);
	}
	VERIFY0(heap_problems(heap));
}

/*
 * The free heap through the table, with keys placed by word0_hash(). Keys
 * that all hash to 6 build a chain of nfree full overflow buckets in
 * bucket 0 of a two-bucket table. Keys hashing to 1 then fill bucket 1
 * until bucket 0 splits, and the whole chain moves to bucket 2, freeing
 * buckets 1 to nfree. Extending the moved chain must reuse them smallest
 * first; a stack would hand out nfree first.
 *
 * Filler keys then drive the table on until bucket 2 splits too. Its keys
 * all move to bucket 6, freeing a chain whose buckets are out of order
 * (nfree + 1 to 2 * nfree, then 1 to nfree / 2). Extending the new chain
 * must again reuse buckets smallest first, and then, once none are free,
 * allocate fresh ones. Fillers never hash to 2 mod 4, so that they stay
 * out of the chain that moves.
 */
static void
heap_in_table(void)
{
	htest_config_t cfg = {
		.hc_record_size = 32,
		.hc_key_size = 16,
		.hc_max_memory = 16 << 20,
		.hc_hash_mode = HM_WORD0,
		.hc_initial_suffix = 1,
		.hc_rng_stream = 320,
	};
	const uint64_t nfree = 40;
	htest_t *ht = ht_init(&cfg, 2000);
	linear_hash_t *lh = ht->ht_lh;
	minheap_t *heap = &lh->lh_freeheap;
	uint64_t n = 0;

	VERIFY3U(heap->size, ==, FREEHEAP_INITIAL_SIZE);
	VERIFY0(heap->count);

	for (; n < (nfree + 1) * ENTRIES_PER_BUCKET; n++) {
		ht->ht_hashes[n] = 6;
		VERIFY3S(ht_op(ht, LH_INSERT, n), ==, LH_INSERTED);
	}
	VERIFY0(lh->lh_split_pointer);
	VERIFY3U(chain_tail(lh, 0), ==, nfree);

	while (lh->lh_split_pointer == 0) {
		ht->ht_hashes[n] = 1;
		VERIFY3S(ht_op(ht, LH_INSERT, n++), ==, LH_INSERTED);
	}
	VERIFY3U(lh->lh_hash_suffix_length, ==, 1);
	VERIFY0(chain_tail(lh, 0));
	VERIFY3U(chain_tail(lh, 2), ==, 2 * nfree);
	VERIFY3U(heap->count, ==, nfree);
	ht_validate(ht);

	for (uint64_t i = 0; i < nfree / 2 * ENTRIES_PER_BUCKET; i++) {
		ht->ht_hashes[n] = 6;
		insert_checking_reuse(ht, n++, 2);
	}
	VERIFY3U(chain_tail(lh, 2), ==, nfree / 2);
	VERIFY3U(heap->count, ==, nfree / 2);

	while (lh->lh_hash_suffix_length < 2 ||
	    (lh->lh_hash_suffix_length == 2 && lh->lh_split_pointer <= 2)) {
		uint64_t h = selftest_rng_next(&ht->ht_rng);
		ht->ht_hashes[n] = ((h & 3) == 2) ? h ^ 1 : h;
		VERIFY3S(ht_op(ht, LH_INSERT, n++), ==, LH_INSERTED);
	}
	VERIFY3U(lh->lh_hash_suffix_length, ==, 2);
	VERIFY0(chain_tail(lh, 2));
	VERIFY3U(chain_tail(lh, 6), !=, 0);
	VERIFY3U(heap->count, >=, nfree + nfree / 2);
	ht_validate(ht);

	for (uint64_t i = (heap->count + 5) * ENTRIES_PER_BUCKET; i > 0; i--) {
		ht->ht_hashes[n] = 6;
		insert_checking_reuse(ht, n++, 6);
	}
	VERIFY0(heap->count);

	ht_verify_all(ht);
	ht_validate(ht);
	ht_fini(ht);
}

/*
 * The free heap of overflow buckets, on its own (from initial sizes of 1,
 * which doubles through every power of two, and of FREEHEAP_INITIAL_SIZE)
 * and through the table
 */
static void
hash_free_heap(void)
{
	heap_alone(1, 310);
	heap_alone(FREEHEAP_INITIAL_SIZE, 311);
	heap_in_table();
}

/*
 * Keys whose hashes collide, through the HM_WORD0 hash:
 *
 * - Full 64-bit collisions in three huge groups, so that a handful of
 *   chains grow into the hundreds of buckets and every lookup must sort
 *   through hundreds of candidates by full key.
 *
 * - Moderate full collisions across a bounded hash space.
 *
 * - Distinct hashes that share their low 22 bits, so that splitting can
 *   never separate them and one bucket's chain grows without bound.
 *
 * - Half of the keys like that and half random, so that splits must
 *   repeatedly repartition a monster chain while normal traffic continues
 *   around it. The table starts with two buckets, so the chain is split
 *   once per cycle; whenever the new mask bit of its shared low bits is
 *   set, the whole chain moves to the buddy bucket at once.
 */
static void
hash_collisions(void)
{
	htest_config_t cfg = {
		.hc_record_size = 32,
		.hc_key_size = 16,
		.hc_max_memory = 256 << 20,
		.hc_hash_mode = HM_WORD0,
		.hc_hash_space = 3,
		.hc_rng_stream = 400,
	};
	htest_t *ht = ht_init(&cfg, 3000);
	ht_workload(ht, 1000);
	ht_fini(ht);

	cfg.hc_hash_space = 500;
	cfg.hc_rng_stream = 401;
	ht = ht_init(&cfg, 25000);
	ht_workload(ht, 5000);
	ht_fini(ht);

	cfg.hc_hash_space = 0;
	cfg.hc_fixed_mask = (1ULL << 22) - 1;
	cfg.hc_fixed_bits = 0x2a5;
	cfg.hc_rng_stream = 402;
	ht = ht_init(&cfg, 4000);
	ht_workload(ht, 1000);
	ht_fini(ht);

	cfg.hc_fixed_every = 2;
	cfg.hc_initial_suffix = 1;
	cfg.hc_rng_stream = 403;
	ht = ht_init(&cfg, 20000);
	ht_workload(ht, 5000);
	ht_fini(ht);
}

/*
 * A table constructed with no memory budget at all: every allocator is
 * disk-only from the first insert. The table starts small, so that bucket
 * splits run on disk too, through several cycles.
 */
static void
hash_no_memory(void)
{
	htest_config_t cfg = {
		.hc_record_size = 64,
		.hc_key_size = 16,
		.hc_max_memory = 0,
		.hc_initial_suffix = 4,
		.hc_rng_stream = 500,
	};
	htest_t *ht = ht_init(&cfg, 5000);
	ht_workload(ht, 2000);
	VERIFY3U(ht->ht_lh->lh_hash_suffix_length, >=, 4 + 5);
	allocator_group_t *g = hash_group(ht->ht_lh);
	VERIFY0(g->ag_mem_current);
	ht_fini(ht);
}

/*
 * The mirror image: no spill directory, so the table is memory-only and
 * can never spill. The budget has to be generous, because a memory-only
 * table that reaches its budget exits rather than degrading.
 */
static void
hash_no_disk(void)
{
	htest_config_t cfg = {
		.hc_record_size = 64,
		.hc_key_size = 16,
		.hc_max_memory = 256 << 20,
		.hc_no_disk = B_TRUE,
		.hc_rng_stream = 550,
	};
	htest_t *ht = ht_init(&cfg, 20000);
	ht_workload(ht, 5000);

	/* Nothing was ever pushed toward disk */
	allocator_group_t *g = hash_group(ht->ht_lh);
	VERIFY3U(g->ag_mem_current, >, 0);
	VERIFY3P(g->ag_dir, ==, NULL);
	for (int i = 0; i < 3; i++)
		VERIFY3U(alloc_fod(g->ag_allocators[i]), ==, g->ag_mem_limit);
	ht_fini(ht);
}

/*
 * Heavy memory pressure: the data alone is more than ten times the memory
 * budget, and even the top-level buckets outgrow it. The 1MB budget gets
 * page-sized clusters. The table must stay
 * correct throughout and its group must never exceed the budget; accounting
 * is exact, so there is no slack. The data allocator, as the first eviction
 * target, must end up fully evicted, then the overflow buckets, and updates
 * must then rewrite records in place on disk.
 */
static void
hash_memory_pressure(void)
{
	htest_config_t cfg = {
		.hc_record_size = 256,
		.hc_key_size = 16,
		.hc_max_memory = 1 << 20,
		.hc_rng_stream = 600,
	};
	const uint64_t count = 120000;
	htest_t *ht = ht_init(&cfg, count);
	allocator_group_t *g = hash_group(ht->ht_lh);
	VERIFY3U(g->ag_cluster_size, ==, (size_t)sysconf(_SC_PAGESIZE));
	VERIFY3U(g->ag_mem_limit, ==, 1 << 20);

	for (uint64_t first = 0; first < count; first += 1024) {
		ht_insert_range(ht, first, MIN(1024, count - first), 256);
		(void) hash_group(ht->ht_lh);
	}
	/*
	 * The overflow buckets are evicted too, and the main buckets end up
	 * with the whole budget. They need more than that, so the bucket
	 * array straddles its own split: later splits read and write buckets
	 * on both sides of it.
	 */
	linear_hash_t *lh = ht->ht_lh;
	allocator_t *bucket = lh->lh_alloc.bucket;
	VERIFY0(alloc_frontier(lh->lh_alloc.data));
	VERIFY0(alloc_frontier(lh->lh_alloc.overflow));
	VERIFY3U(alloc_frontier(bucket), ==, g->ag_mem_limit);
	VERIFY3U(g->ag_mem_current, ==, g->ag_mem_limit);
	VERIFY3U(lh->lh_occupancy.top_buckets, >, alloc_fod(bucket) /
	    g->ag_cluster_size * bucket->a_records_per_cluster);

	ht_rewrite_random(ht, 20000);
	ht_verify_all(ht);
	ht_validate(ht);
	ht_fini(ht);
}

/*
 * Moderate memory pressure: the budget is big enough for the bucket array
 * but not for the data. Only the data allocator may give memory up. Its
 * split moves down, but it keeps part of the budget (gradual degradation),
 * while the splits of the overflow and bucket allocators never move.
 * Updates then rewrite records on both sides of the data allocator's split.
 */
static void
hash_pressure_priority(void)
{
	htest_config_t cfg = {
		.hc_record_size = 1024,		/* 40MB of data */
		.hc_key_size = 16,
		.hc_max_memory = 24 << 20,
		.hc_rng_stream = 700,
	};
	htest_t *ht = ht_init(&cfg, 40000);
	ht_insert_range(ht, 0, ht->ht_nkeys, 1024);

	linear_hash_t *lh = ht->ht_lh;
	allocator_group_t *g = hash_group(lh);
	VERIFY3U(g->ag_mem_current, ==, g->ag_mem_limit);
	VERIFY3U(alloc_fod(lh->lh_alloc.data), <, g->ag_mem_limit);
	VERIFY3U(alloc_frontier(lh->lh_alloc.data), >, 0);
	VERIFY3U(alloc_fod(lh->lh_alloc.overflow), ==, g->ag_mem_limit);
	VERIFY3U(alloc_fod(lh->lh_alloc.bucket), ==, g->ag_mem_limit);
	VERIFY3U(alloc_frontier(lh->lh_alloc.bucket), >, 0);

	ht_rewrite_random(ht, 10000);
	ht_verify_all(ht);
	ht_validate(ht);
	ht_fini(ht);
}

/*
 * A random configuration for hash_stress()
 */
static htest_config_t
random_config(selftest_rng_t *rng, uint64_t stream)
{
	htest_config_t cfg = { .hc_rng_stream = stream };

	cfg.hc_key_size = 1 + selftest_rng_below(rng, MAX_KEY_SIZE);
	switch (selftest_rng_below(rng, 4)) {
	case 0:
		cfg.hc_record_size = cfg.hc_key_size;
		break;
	case 1:
		cfg.hc_record_size = cfg.hc_key_size +
		    selftest_rng_below(rng, MAX_RECORD_SIZE -
		    cfg.hc_key_size + 1);
		break;
	default:
		cfg.hc_record_size = cfg.hc_key_size +
		    selftest_rng_below(rng, 200);
		break;
	}
	/* hash_stress() sizes a nonzero hash space to the key count */
	if (cfg.hc_key_size >= 16 && selftest_rng_below(rng, 2) == 0) {
		cfg.hc_hash_mode = HM_WORD0;
		cfg.hc_hash_space = selftest_rng_below(rng, 2);
	}

	/* Most tables start small enough to split many times */
	if (selftest_rng_below(rng, 4) != 0)
		cfg.hc_initial_suffix = 1 + selftest_rng_below(rng, 7);

	/*
	 * A memory-only table can never spill, so it needs a big budget.
	 * Other tables get budgets that are disk-only, tight, or generous,
	 * and usually not a whole number of clusters.
	 */
	cfg.hc_no_disk = (selftest_rng_below(rng, 5) == 0);
	uint64_t k = selftest_rng_below(rng, 3);
	if (cfg.hc_no_disk || k == 2) {
		cfg.hc_max_memory = (64 + selftest_rng_below(rng, 192)) << 20;
	} else if (k == 1) {
		cfg.hc_max_memory = (1 + selftest_rng_below(rng, 4)) << 20;
	}
	cfg.hc_max_memory += selftest_rng_below(rng, 1 << 20);
	if (!cfg.hc_no_disk && k == 0)
		cfg.hc_max_memory = 0;
	return (cfg);
}

/*
 * Seeded randomization of key sizes, record sizes, hash modes, collision
 * rates, budgets, and starting table sizes, under a random mix of all four
 * operations on present and absent keys. Whatever the targeted tests miss,
 * this net should catch over many CI runs; failures replay with -s.
 */
static void
hash_stress(void)
{
	selftest_rng_t rng;
	selftest_rng_init(&rng, 900);

	for (int round = 0; round < 6; round++) {
		htest_config_t cfg = random_config(&rng, 1000 + round);
		uint64_t space = key_space(&cfg);
		uint64_t want = 1000 + selftest_rng_below(&rng, 4000);
		if (cfg.hc_record_size > 1024)
			want /= 4;
		uint64_t nkeys = MIN(want, space - space / 8);
		/* Full collisions, but chains of tens of keys, not thousands */
		if (cfg.hc_hash_space != 0) {
			cfg.hc_hash_space = 1 + nkeys /
			    (2 + selftest_rng_below(&rng, 60));
		}
		htest_t *ht = ht_init(&cfg, nkeys);

		for (uint64_t op = 0; op < 3 * nkeys; op++) {
			uint64_t k = selftest_rng_below(&rng, 100);
			if (k < 40) {
				(void) ht_op(ht, LH_INSERT, ht_random_key(ht));
			} else if (k < 60) {
				(void) ht_op(ht, LH_FIND, ht_random_key(ht));
			} else if (k < 70) {
				(void) ht_op(ht, LH_UPDATE, ht_random_key(ht));
			} else if (k < 85) {
				(void) ht_op(ht, LH_UPSERT, ht_random_key(ht));
			} else if (ht->ht_nprobes > 0) {
				(void) ht_op(ht, LH_FIND, ht_random_probe(ht));
			}
			if (op % 8192 == 8191)
				ht_validate(ht);
		}
		ht_verify_all(ht);
		ht_validate(ht);
		(void) hash_group(ht->ht_lh);
		ht_fini(ht);
	}
}

/*
 * Many tables live at once, with varied configurations, used in
 * interleaved fashion, and destroyed in a scrambled order. Most start small
 * enough to split. Tables share nothing, so none of this may disturb any
 * of them.
 */
#define	MANY_TABLES	16

static void
hash_many_tables(void)
{
	const uint64_t nkeys = 2000;
	const uint64_t chunk = 100;
	htest_t *tables[MANY_TABLES];
	selftest_rng_t rng;

	selftest_rng_init(&rng, 950);
	for (int round = 0; round < 2; round++) {
		for (int t = 0; t < MANY_TABLES; t++) {
			htest_config_t cfg = {
				.hc_record_size = 16 + 8 * t,
				.hc_key_size = 16,
				.hc_max_memory = (t % 3 == 0) ? 0 : 16 << 20,
				.hc_no_disk = (t % 3 == 2),
				.hc_hash_mode = (t % 2) ? HM_WORD0 : HM_CITY,
				.hc_hash_space = (t % 4 == 1) ? 50 : 0,
				.hc_initial_suffix =
				    (t % 4 == 3) ? 0 : 1 + t % 7,
				.hc_rng_stream = 960 + 100 * round + t,
			};
			tables[t] = ht_init(&cfg, nkeys);
			allocator_group_t *g = hash_group(tables[t]->ht_lh);
			for (int u = 0; u < t; u++)
				VERIFY3P(g, !=, hash_group(tables[u]->ht_lh));
		}

		for (uint64_t first = 0; first < nkeys; first += chunk) {
			for (int t = 0; t < MANY_TABLES; t++)
				ht_insert_range(tables[t], first, chunk, 0);
		}
		for (int t = 0; t < MANY_TABLES; t++)
			ht_rewrite_random(tables[t], 500);

		for (int left = MANY_TABLES; left > 0; left--) {
			int t = selftest_rng_below(&rng, left);
			ht_verify_all(tables[t]);
			ht_validate(tables[t]);
			ht_fini(tables[t]);
			tables[t] = tables[left - 1];
		}
	}
}

typedef struct {
	size_t		fa_record_size;
	size_t		fa_key_size;
	size_t		fa_max_memory;
	boolean_t	fa_disk;
} fatal_args_t;

static void
child_lh_init(void *arg)
{
	fatal_args_t *fa = arg;
	(void) lh_init(fa->fa_record_size, fa->fa_key_size, fa->fa_max_memory,
	    fa->fa_disk ? selftest_scratch_dir() : NULL);
}

static void
child_bad_op(void *arg)
{
	linear_hash_t *lh = lh_init(16, 8, 16 << 20, NULL);
	uint8_t buf[16] = { 0 };

	(void) arg;
	(void) lh_record(lh, LH_NUM_OPS, buf);
}

static void
child_heap_push_zero(void *arg)
{
	(void) arg;
	minheap_t heap = minheap_init(1);
	minheap_push(&heap, 0);
}

/*
 * A memory-only table that outgrows its budget. With 64-byte records and
 * 8MB, the data allocator runs out first, and it has no one to take memory
 * from. The smallest budget is three page-sized clusters, one per
 * allocator. Keys whose top-level buckets are all in the first bucket
 * cluster, at most two to a bucket, fill the budget with data and that
 * one bucket cluster, leaving room in the last data cluster. A key in the
 * first bucket of the second cluster then stores its data record, but its
 * bucket needs another cluster (within the limit, so not on the disk
 * side), which the data allocator can give up only by losing data.
 */
static void
child_memory_only_overflow(void *arg)
{
	boolean_t minimal = *(boolean_t *)arg;
	linear_hash_t *lh = lh_init(64, 8, minimal ? 1 : 8 << 20, NULL);
	uint8_t buf[64] = { 0 };

	if (minimal) {
		allocator_group_t *g = lh->lh_alloc.data->a_group;
		uint64_t nb = g->ag_cluster_size / sizeof (bucket_t);

		VERIFY3U(nb, <, lh->lh_occupancy.top_buckets);
		lh->lh_hash_func = word0_hash;
		for (uint64_t i = 0; g->ag_mem_current < g->ag_mem_limit;
		    i++) {
			uint64_t key = (i << 32) | (i % nb);
			memcpy(buf, &key, sizeof (key));
			VERIFY3S(lh_insert(lh, buf), ==, LH_INSERTED);
		}
		memcpy(buf, &nb, sizeof (nb));
		(void) lh_insert(lh, buf);
		return;
	}
	for (uint64_t i = 1; i < (1ULL << 24); i++) {
		uint64_t key = selftest_mix64(i);
		memcpy(buf, &key, sizeof (key));
		(void) lh_insert(lh, buf);
	}
}

/*
 * The fatal paths, each run in a child process that must exit with the
 * expected message. A memory-only table can run out while either the data
 * allocator (which has no one to take from) or a bucket allocator (whose
 * victim has nowhere to put its data) is growing; both are tested.
 */
static void
hash_fatal(void)
{
	fatal_args_t fa = {
		.fa_record_size = 16,
		.fa_key_size = 8,
		.fa_max_memory = 0,
		.fa_disk = B_FALSE,
	};
	selftest_expect_exit("no backing", child_lh_init, &fa,
	    "requires memory or disk backing");

	fa.fa_max_memory = 16 << 20;
	fa.fa_record_size = 0;
	selftest_expect_exit("zero-size record", child_lh_init, &fa,
	    "record size must be specified");
	fa.fa_record_size = MAX_RECORD_SIZE + 1;
	selftest_expect_exit("oversized record", child_lh_init, &fa,
	    "record size must be specified");

	fa.fa_record_size = 64;
	fa.fa_key_size = 0;
	selftest_expect_exit("zero-size key", child_lh_init, &fa,
	    "key size must be specified");
	fa.fa_key_size = MAX_KEY_SIZE + 1;
	selftest_expect_exit("oversized key", child_lh_init, &fa,
	    "key size must be specified");
	fa.fa_record_size = 16;
	fa.fa_key_size = 24;
	selftest_expect_exit("key longer than record", child_lh_init, &fa,
	    "key size cannot exceed record size");

	selftest_expect_exit("invalid opcode", child_bad_op, NULL,
	    "LH_NUM_OPS");
	selftest_expect_exit("free bucket 0", child_heap_push_zero, NULL,
	    "VERIFY3U(bucket, !=, 0)");
	boolean_t minimal = B_FALSE;
	selftest_expect_exit("memory-only overflow",
	    child_memory_only_overflow, &minimal, "memory limit exceeded");
	minimal = B_TRUE;
	selftest_expect_exit("memory-only eviction",
	    child_memory_only_overflow, &minimal, "memory limit exceeded");
}

const test_case_t selftest_hash_cases[] = {
	{ "hash_basic",			hash_basic },
	{ "hash_ops",			hash_ops },
	{ "hash_key_sizes",		hash_key_sizes },
	{ "hash_record_sizes",		hash_record_sizes },
	{ "hash_cluster_size",		hash_cluster_size },
	{ "hash_splits",		hash_splits },
	{ "hash_free_heap",		hash_free_heap },
	{ "hash_collisions",		hash_collisions },
	{ "hash_no_memory",		hash_no_memory },
	{ "hash_no_disk",		hash_no_disk },
	{ "hash_memory_pressure",	hash_memory_pressure },
	{ "hash_pressure_priority",	hash_pressure_priority },
	{ "hash_stress",		hash_stress },
	{ "hash_many_tables",		hash_many_tables },
	{ "hash_fatal",			hash_fatal },
	{ NULL,				NULL },
};
