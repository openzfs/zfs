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
 * Selftests for allocator_t.
 *
 * Record contents come from selftest_fill() and a tag, so a test need only
 * remember the tag; tag 0 means "never written" and must read as zeros.
 * Reads go to a buffer with guard bytes after the record, which must
 * survive. Most tests call selftest_audit_group() after every operation.
 *
 * alloc_normalize: allocator_group_init() rounds and floors cluster sizes
 * and memory limits as documented, relative to the system page size.
 *
 * alloc_eviction_policy: a scripted series of writes to a two-allocator
 * group checks, step by step, which allocator yields memory, which cluster
 * it loses, where split points land, and that memory taken from another
 * allocator reads as zeros.
 *
 * alloc_model_{memory,disk,dual}: random stores, retrieves, appends and
 * skips on three allocators, checked against a model. In the dual-backed
 * run each allocator's records span four times the group's memory. Across
 * its groups, the run must see memory taken from another allocator, memory
 * taken from a higher cluster of the same allocator, and overwrites of
 * records on disk.
 *
 * alloc_far_index: a record whose index exceeds UINT32_MAX and whose byte
 * offset exceeds 4 GiB is distinct from its 32-bit truncation.
 *
 * alloc_enomem_spill (Linux only): when mprotect() fails with ENOMEM, a
 * dual-backed group spills to disk and a memory-only group exits.
 *
 * alloc_mem_only_limits: a memory-only group exits when it has no memory
 * left or a write falls beyond its memory; reads there return zeros.
 *
 * alloc_invalid_config: bad allocator_group_init() arguments are fatal.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/sysmacros.h>

#include "zstream_alloc.h"
#include "zstream_alloc_impl.h"
#include "zstream_selftest.h"
#include "zstream_util.h"

#define	GUARD_BYTES	16
#define	GUARD_VALUE	0xa5

static size_t
page_size(void)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	VERIFY3S(pagesize, >, 0);
	return ((size_t)pagesize);
}

static record_ix_t
rec_ix(const allocator_t *alloc, uint64_t cluster, uint64_t slot)
{
	return (cluster * alloc->a_records_per_cluster + slot);
}

static void
put(allocator_t *alloc, record_ix_t rec, uint64_t tag)
{
	uint8_t buf[MAX_RECORD_SIZE];

	selftest_fill(buf, alloc->a_record_size, tag);
	allocator_store(alloc, rec, buf);
}

static void
check(allocator_t *alloc, record_ix_t rec, uint64_t tag)
{
	uint8_t want[MAX_RECORD_SIZE];
	uint8_t got[MAX_RECORD_SIZE + GUARD_BYTES];
	size_t size = alloc->a_record_size;

	if (tag == 0)
		memset(want, 0, size);
	else
		selftest_fill(want, size, tag);
	memset(got, GUARD_VALUE, sizeof (got));
	allocator_retrieve(alloc, rec, got);
	VERIFY0(memcmp(got, want, size));
	for (size_t i = size; i < size + GUARD_BYTES; i++)
		VERIFY3U(got[i], ==, GUARD_VALUE);
}

/* Slot 0 of the cluster holds tag; the other slots must read as zeros */
static void
check_cluster(allocator_t *alloc, uint64_t cluster, uint64_t tag)
{
	for (uint64_t s = 0; s < alloc->a_records_per_cluster; s++)
		check(alloc, rec_ix(alloc, cluster, s), (s == 0) ? tag : 0);
}

void
selftest_audit_group(allocator_t *member)
{
	allocator_group_t *g = member->a_group;
	uint64_t max = g->ag_clusters_max;
	size_t cs = g->ag_cluster_size;
	/* listed[c] == i + 1 once allocator i's heap has listed cluster c */
	unsigned *listed = safe_calloc(MAX(max, 1) * sizeof (unsigned));
	/* Physical clusters in use */
	boolean_t *used = safe_calloc(MAX(max, 1) * sizeof (boolean_t));
	uint64_t total = 0;
	boolean_t found = B_FALSE;

	VERIFY3U(g->ag_num_allocators, >, 0);
	VERIFY3U(g->ag_clusters_current, <=, max);
	VERIFY_EQUIV(g->ag_base_addr == NULL, max == 0);
	for (unsigned i = 0; i < g->ag_num_allocators; i++) {
		allocator_t *a = g->ag_allocators[i];
		uint64_t owned = clusters_owned(a);
		uint64_t mapped = 0;

		found |= (a == member);
		VERIFY3P(a->a_group, ==, g);
		VERIFY_EQUIV(a->a_fd >= 0, g->ag_dir != NULL);
		VERIFY_EQUIV(a->a_clusters == NULL, max == 0);
		VERIFY3U(a->a_records_per_cluster, >=, 1);
		VERIFY3U(a->a_records_per_cluster, ==, cs / a->a_record_size);
		VERIFY3U(a->a_first_disk_cluster, <=, max);

		/* The heap lists distinct memory clusters below the split */
		for (uint64_t j = 0; j < owned; j++) {
			uint64_t c = a->a_cluster_heap.values[j];
			VERIFY3U(c, <, a->a_first_disk_cluster);
			VERIFY3U(listed[c], !=, i + 1);
			listed[c] = i + 1;
			VERIFY3P(a->a_clusters[c], !=, NULL);
		}
		/* No other clusters are mapped, and no memory is shared */
		for (uint64_t c = 0; c < max; c++) {
			if (a->a_clusters[c] == NULL)
				continue;
			uintptr_t off = (uintptr_t)a->a_clusters[c] -
			    (uintptr_t)g->ag_base_addr;
			VERIFY0(off % cs);
			VERIFY3U(off / cs, <, g->ag_clusters_current);
			VERIFY(!used[off / cs]);
			used[off / cs] = B_TRUE;
			mapped++;
		}
		VERIFY3U(mapped, ==, owned);
		total += owned;

		allocator_stats_t stats = allocator_get_stats(a);
		VERIFY3U(stats.as_memory_used, ==, owned * cs);
		VERIFY3U(stats.as_record_size, ==, a->a_record_size);
		VERIFY3U(stats.as_num_records, ==, a->a_record_count);
	}
	VERIFY(found);
	VERIFY3U(total, ==, g->ag_clusters_current);
	free(listed);
	free(used);
}

/*
 * Each group's allocators have record sizes 1 and the listed size, so the
 * cluster must be sized for a record that isn't in allocator 0.
 */
static void
alloc_normalize(void)
{
	size_t pg = page_size();
	size_t big = MAX(DEFAULT_CLUSTER_SIZE, pg);
	size_t max_rec = roundup(MAX_RECORD_SIZE, pg);
	const struct {
		size_t		cluster;	/* Requested */
		size_t		record;
		size_t		mem;
		boolean_t	disk;
		size_t		want_cluster;
		uint64_t	want_max;
	} cases[] = {
		{ 0,	  512,		   4 * big,	B_FALSE, big,	  4 },
		{ 1,	  512,		   4 * pg,	B_FALSE, pg,	  4 },
		{ pg + 1, 512,		   8 * pg,	B_FALSE, 2 * pg,  4 },
		{ 1,	  MAX_RECORD_SIZE, 4 * max_rec, B_FALSE, max_rec, 4 },
		{ 1,	  512,		   1,		B_FALSE, pg,	  2 },
		{ 1,	  512,		   4 * pg + 1,	B_FALSE, pg,	  5 },
		{ 1,	  512,		   0,		B_TRUE,	 pg,	  0 },
	};

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		size_t sizes[2] = { 1, cases[i].record };
		allocator_t **g = allocator_group_init(sizes, 2,
		    cases[i].cluster, cases[i].mem,
		    cases[i].disk ? selftest_scratch_dir() : NULL);

		VERIFY3U(g[0]->a_group->ag_cluster_size, ==,
		    cases[i].want_cluster);
		VERIFY3U(g[0]->a_group->ag_clusters_max, ==, cases[i].want_max);
		VERIFY0(g[0]->a_group->ag_clusters_current);
		selftest_audit_group(g[1]);
		for (int j = 0; j < 2; j++) {
			allocator_stats_t stats = allocator_get_stats(g[j]);
			VERIFY0(stats.as_memory_used);
			VERIFY3U(stats.as_record_size, ==, sizes[j]);
			VERIFY0(stats.as_num_records);
			VERIFY3U(g[j]->a_first_disk_cluster, ==,
			    cases[i].want_max);
		}
		allocator_group_fini(g[0]);
	}
}

#define	POLICY_CLUSTERS	6

/*
 * Each step writes slot 0 of one cluster, then checks every allocator's
 * owned-cluster count and split point. Allocator 0's records are twice the
 * size of allocator 1's, so if allocator 1 takes a cluster from allocator
 * 0 and it isn't zeroed, allocator 0's data shows up in slot 1.
 */
static void
alloc_eviction_policy(void)
{
	static const size_t sizes[] = { 1024, 512 };
	static const struct {
		unsigned	alloc;
		uint64_t	cluster;
		uint64_t	owned[2];
		uint64_t	split[2];
	} steps[] = {
		/* Memory is free for the asking until it runs out */
		{ 0, 2, { 1, 0 }, { 6, 6 } },
		{ 0, 0, { 2, 0 }, { 6, 6 } },
		{ 1, 5, { 2, 1 }, { 6, 6 } },
		{ 1, 3, { 2, 2 }, { 6, 6 } },
		{ 1, 4, { 2, 3 }, { 6, 6 } },
		{ 1, 1, { 2, 4 }, { 6, 6 } },
		/* 0 has nothing above 4 and can't take from 1; 4 is on disk */
		{ 0, 4, { 2, 4 }, { 4, 6 } },
		/* 0 owns 2, which is above 1, so it gives up 2 */
		{ 0, 1, { 2, 4 }, { 2, 6 } },
		/* 1 owns 3 and up, but 0 yields its highest, then its last */
		{ 1, 2, { 1, 5 }, { 1, 6 } },
		{ 1, 0, { 0, 6 }, { 0, 6 } },
	};
	uint64_t tags[2][POLICY_CLUSTERS] = {{ 0 }};
	allocator_t **g = allocator_group_init(sizes, 2, 1,
	    POLICY_CLUSTERS * page_size(), selftest_scratch_dir());

	VERIFY3U(g[0]->a_group->ag_clusters_max, ==, POLICY_CLUSTERS);
	for (int i = 0; i < ARRAY_SIZE(steps); i++) {
		allocator_t *a = g[steps[i].alloc];
		uint64_t c = steps[i].cluster;

		tags[steps[i].alloc][c] = i + 1;
		put(a, rec_ix(a, c, 0), i + 1);
		for (int j = 0; j < 2; j++) {
			VERIFY3U(clusters_owned(g[j]), ==, steps[i].owned[j]);
			VERIFY3U(g[j]->a_first_disk_cluster, ==,
			    steps[i].split[j]);
		}
		check_cluster(a, c, i + 1);
		selftest_audit_group(g[i % 2]);
	}
	for (int j = 0; j < 2; j++) {
		for (uint64_t c = 0; c < POLICY_CLUSTERS; c++)
			check_cluster(g[j], c, tags[j][c]);
	}
	allocator_group_fini(g[1]);
}

#define	MODEL_ALLOCS	3
#define	MODEL_SPAN	24	/* Each allocator's index range, in clusters */
#define	MODEL_DUAL_MEM	6	/* Group memory for the dual run, in clusters */
#define	MODEL_OPS	1000	/* Per group */

typedef struct {
	allocator_t	*ma_alloc;
	uint64_t	ma_nrec;	/* Size of the index range */
	uint64_t	ma_count;	/* Expected as_num_records */
	uint64_t	ma_split;	/* Last a_first_disk_cluster seen */
	uint64_t	*ma_tags;	/* Per record; 0 = never written */
} model_alloc_t;

/* Events are counted across all of a run's groups */
typedef struct {
	model_alloc_t	m_allocs[MODEL_ALLOCS];
	uint64_t	m_last_tag;
	uint64_t	m_stolen;	/* Memory from another allocator */
	uint64_t	m_self;		/* Memory from a higher cluster */
	uint64_t	m_disk_overwrites;
} model_t;

/*
 * Store or append a record and note where its memory, if any, came from:
 * a cluster that appeared without the group growing was taken from
 * somewhere, and if the writer's own count didn't rise, from itself.
 */
static void
model_store(model_t *m, model_alloc_t *ma, record_ix_t rec, boolean_t append)
{
	allocator_t *a = ma->ma_alloc;
	uint64_t c = rec / a->a_records_per_cluster;
	boolean_t on_disk = (c >= a->a_first_disk_cluster);
	boolean_t mapped = !on_disk && a->a_clusters[c] != NULL;
	uint64_t owned = clusters_owned(a);
	uint64_t current = a->a_group->ag_clusters_current;
	uint64_t tag = ++m->m_last_tag;
	uint8_t buf[MAX_RECORD_SIZE];

	if (on_disk && ma->ma_tags[rec] != 0)
		m->m_disk_overwrites++;
	selftest_fill(buf, a->a_record_size, tag);
	if (append)
		VERIFY3U(allocator_append(a, buf), ==, rec);
	else
		allocator_store(a, rec, buf);
	ma->ma_tags[rec] = tag;
	ma->ma_count = MAX(ma->ma_count, rec + 1);

	if (!on_disk && !mapped && a->a_clusters[c] != NULL &&
	    a->a_group->ag_clusters_current == current) {
		if (clusters_owned(a) == owned)
			m->m_self++;
		else
			m->m_stolen++;
	}
}

static void
model_group(model_t *m, uint64_t mem_clusters, boolean_t disk,
    uint64_t stream)
{
	/* 600 doesn't divide the cluster size, so clusters have waste */
	static const size_t sizes[] = { 600, MAX_RECORD_SIZE, 512 };
	size_t cluster = roundup(MAX_RECORD_SIZE, page_size());
	allocator_t **g = allocator_group_init(sizes, MODEL_ALLOCS, 1,
	    mem_clusters * cluster, disk ? selftest_scratch_dir() : NULL);
	allocator_group_t *group = g[0]->a_group;
	uint64_t current = 0;
	selftest_rng_t rng;

	selftest_rng_init(&rng, stream);
	for (int i = 0; i < MODEL_ALLOCS; i++) {
		uint64_t nrec = MODEL_SPAN * g[i]->a_records_per_cluster;
		m->m_allocs[i] = (model_alloc_t) {
			.ma_alloc = g[i],
			.ma_nrec = nrec,
			.ma_split = g[i]->a_first_disk_cluster,
			.ma_tags = safe_calloc(nrec * sizeof (uint64_t))
		};
	}

	for (int op = 0; op < MODEL_OPS; op++) {
		model_alloc_t *ma =
		    &m->m_allocs[selftest_rng_below(&rng, MODEL_ALLOCS)];
		allocator_t *a = ma->ma_alloc;
		record_ix_t rec = selftest_rng_below(&rng, ma->ma_nrec);
		uint64_t kind = selftest_rng_below(&rng, 4);

		/* Appends and skips must stay within the index range */
		if (kind >= 2 && ma->ma_count == ma->ma_nrec)
			kind = 0;
		switch (kind) {
		case 0:
			model_store(m, ma, rec, B_FALSE);
			break;
		case 1:
			check(a, rec, ma->ma_tags[rec]);
			break;
		case 2:
			model_store(m, ma, ma->ma_count, B_TRUE);
			break;
		default:
			VERIFY3U(allocator_skip(a), ==, ma->ma_count);
			check(a, ma->ma_count++, 0);
			break;
		}
		VERIFY3U(allocator_get_stats(a).as_num_records, ==,
		    ma->ma_count);

		selftest_audit_group(g[op % MODEL_ALLOCS]);
		for (int i = 0; i < MODEL_ALLOCS; i++) {
			model_alloc_t *mi = &m->m_allocs[i];
			VERIFY3U(g[i]->a_first_disk_cluster, <=, mi->ma_split);
			mi->ma_split = g[i]->a_first_disk_cluster;
		}
		VERIFY3U(group->ag_clusters_current, >=, current);
		current = group->ag_clusters_current;
	}

	for (int i = 0; i < MODEL_ALLOCS; i++) {
		model_alloc_t *ma = &m->m_allocs[i];
		for (record_ix_t rec = 0; rec < ma->ma_nrec; rec++)
			check(g[i], rec, ma->ma_tags[rec]);
		free(ma->ma_tags);
	}
	allocator_group_fini(g[stream % MODEL_ALLOCS]);
}

static void
model_run(uint64_t mem_clusters, boolean_t disk, int groups, uint64_t stream)
{
	model_t m = { 0 };

	for (int i = 0; i < groups; i++)
		model_group(&m, mem_clusters, disk, stream + i);
	if (mem_clusters > 0 && disk) {
		VERIFY3U(m.m_stolen, >, 0);
		VERIFY3U(m.m_self, >, 0);
		VERIFY3U(m.m_disk_overwrites, >, 0);
	}
}

/* Memory holds every record that can be written; exceeding it is fatal */
static void
alloc_model_memory(void)
{
	model_run(MODEL_ALLOCS * MODEL_SPAN, B_FALSE, 4, 100);
}

static void
alloc_model_disk(void)
{
	model_run(0, B_TRUE, 4, 200);
}

/*
 * Split points never rise, so a group sees at most one eviction per
 * allocator per memory cluster, and the run uses many short groups to see
 * enough of them. Taking memory from a higher cluster is the rarest event:
 * about one group in three has none, so the chance that none of 24 groups
 * does is under 1e-11.
 */
static void
alloc_model_dual(void)
{
	model_run(MODEL_DUAL_MEM, B_TRUE, 24, 300);
}

/*
 * With 8-byte records in a cluster that holds a whole number of them, the
 * byte offset of a record is 8 times its index. So, truncating either the
 * index or the offset to 32 bits lands on the same record, "alias". The
 * group is disk-only because a memory overlay could hide a stray write.
 */
static void
alloc_far_index(void)
{
	static const size_t size = 8;
	allocator_t **g = allocator_group_init(&size, 1, 1, 0,
	    selftest_scratch_dir());
	allocator_t *a = g[0];
	record_ix_t far = (1ULL << 32) + 5;
	record_ix_t alias = far & UINT32_MAX;

	put(a, alias, 1);
	put(a, far, 2);
	VERIFY3U(allocator_skip(a), ==, far + 1);
	check(a, alias, 1);
	check(a, far, 2);
	check(a, far - 1, 0);
	check(a, far + 1, 0);
	selftest_audit_group(a);
	allocator_group_fini(a);
}

#if defined(__linux__)
#define	SPILL_CLUSTERS	8

/*
 * Since Linux 4.7, RLIMIT_DATA covers private writable mappings, so a
 * tiny limit keeps mprotect() from making more clusters writable. A soft
 * limit of exactly 0 is exempt (a workaround for Valgrind), but 1 is not.
 * Returns the previous limits.
 */
static struct rlimit
limit_data(void)
{
	struct rlimit saved;
	struct rlimit tiny;

	VERIFY0(getrlimit(RLIMIT_DATA, &saved));
	tiny = saved;
	tiny.rlim_cur = 1;
	VERIFY0(setrlimit(RLIMIT_DATA, &tiny));
	return (saved);
}

/*
 * Clusters 4 and 5 are in memory before the limit. Clusters 0 and 1 then
 * take that memory from 5 and 4, and clusters 2 and 3 go to disk. Nothing
 * here may allocate while the limit is in force.
 */
static void
spill_child(void *arg)
{
	(void) arg;
	static const size_t size = 512;
	allocator_t **g = allocator_group_init(&size, 1, 1,
	    SPILL_CLUSTERS * page_size(), selftest_scratch_dir());
	allocator_t *a = g[0];

	put(a, rec_ix(a, 4, 0), 5);
	put(a, rec_ix(a, 5, 0), 6);
	struct rlimit saved = limit_data();
	for (uint64_t c = 0; c < 4; c++)
		put(a, rec_ix(a, c, 0), c + 1);
	VERIFY0(setrlimit(RLIMIT_DATA, &saved));

	VERIFY(a->a_group->ag_enomem_warned);
	VERIFY3U(a->a_group->ag_clusters_current, ==, 2);
	VERIFY3U(clusters_owned(a), ==, 2);
	VERIFY3U(a->a_first_disk_cluster, ==, 2);
	for (uint64_t c = 0; c < 6; c++)
		check_cluster(a, c, c + 1);
	selftest_audit_group(a);
	allocator_group_fini(a);
}

static void
no_spill_child(void *arg)
{
	(void) arg;
	static const size_t size = 512;
	allocator_t **g = allocator_group_init(&size, 1, 1,
	    SPILL_CLUSTERS * page_size(), NULL);

	(void) limit_data();
	put(g[0], 0, 1);
}

static void
alloc_enomem_spill(void)
{
	selftest_expect_success("dual-backed group with no memory",
	    spill_child, NULL, "out of memory: spilling to");
	selftest_expect_exit("memory-only group with no memory",
	    no_spill_child, NULL, "mprotect() failed");
}
#else
static void
alloc_enomem_spill(void)
{
	(void) printf("(Linux only) ");
}
#endif

#define	MEM_ONLY_CLUSTERS	3

static void
exhaust_memory(void *arg)
{
	allocator_t **g = arg;
	uint64_t max = g[0]->a_group->ag_clusters_max;

	for (uint64_t c = 0; c < max; c++)
		put(g[0], rec_ix(g[0], c, 0), c + 1);
	VERIFY3U(clusters_owned(g[0]), ==, max);
	put(g[1], 0, 1);
}

static void
write_past_end(void *arg)
{
	allocator_t **g = arg;

	put(g[1], rec_ix(g[1], g[1]->a_group->ag_clusters_max, 0), 1);
}

/* The children inherit a fresh group, with all of its memory free */
static void
alloc_mem_only_limits(void)
{
	static const size_t sizes[] = { 512, 512 };
	allocator_t **g = allocator_group_init(sizes, 2, 1,
	    MEM_ONLY_CLUSTERS * page_size(), NULL);

	check(g[1], rec_ix(g[1], g[1]->a_group->ag_clusters_max, 0), 0);
	selftest_expect_exit("write past end of memory", write_past_end, g,
	    "allocator memory limit exceeded");
	selftest_expect_exit("write with no memory left", exhaust_memory, g,
	    "allocator memory limit exceeded");
	selftest_audit_group(g[0]);
	allocator_group_fini(g[1]);
}

typedef struct {
	const char	*ic_what;
	size_t		ic_record;
	unsigned	ic_num;
	size_t		ic_cluster;
	size_t		ic_mem;
	const char	*ic_expect;
} invalid_config_t;

static void
init_child(void *arg)
{
	const invalid_config_t *ic = arg;

	(void) allocator_group_init(&ic->ic_record, ic->ic_num,
	    ic->ic_cluster, ic->ic_mem, NULL);
}

static void
alloc_invalid_config(void)
{
	static const invalid_config_t cases[] = {
		{ "no backing", 512, 1, 1, 0,
		    "needs either disk or memory backing" },
		{ "no allocators", 512, 0, 1, 1 << 20,
		    "invalid number of allocators" },
		{ "record size 0", 0, 1, 1, 1 << 20,
		    "record size is invalid" },
		{ "record size too large", MAX_RECORD_SIZE + 1, 1, 1, 1 << 20,
		    "record size is invalid" },
		{ "memory too large", 512, 1, 1, SIZE_MAX,
		    "allocator memory size" },
		{ "cluster size too large", 512, 1, SIZE_MAX, 1 << 20,
		    "allocator cluster size" },
	};

	for (int i = 0; i < ARRAY_SIZE(cases); i++) {
		selftest_expect_exit(cases[i].ic_what, init_child,
		    (void *)&cases[i], cases[i].ic_expect);
	}
}

const test_case_t selftest_alloc_cases[] = {
	{ "alloc_normalize",		alloc_normalize },
	{ "alloc_eviction_policy",	alloc_eviction_policy },
	{ "alloc_model_memory",		alloc_model_memory },
	{ "alloc_model_disk",		alloc_model_disk },
	{ "alloc_model_dual",		alloc_model_dual },
	{ "alloc_far_index",		alloc_far_index },
	{ "alloc_enomem_spill",		alloc_enomem_spill },
	{ "alloc_mem_only_limits",	alloc_mem_only_limits },
	{ "alloc_invalid_config",	alloc_invalid_config },
	{ NULL,				NULL },
};
