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

#ifndef _ZSTREAM_ALLOC_H
#define	_ZSTREAM_ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/*
 * An allocator_t defines a thin record storage API that can be backed by
 * memory, a disk file, or both; the API is identical.
 *
 * Dual-backed allocators keep the first N records in memory and later
 * records on disk. For data that grows linearly but is accessed randomly
 * (e.g., linear hash tables), this arrangement allows for gradual
 * performance degradation after memory becomes full.
 *
 * The goal is to use memory as long as it's available but not give up
 * arbitrarily when memory has been exhausted. With sufficient disk space,
 * it's possible to process streams of arbitrary size.
 *
 *   - Blocks are of uniform fixed size (sizes may vary per allocator).
 *   - Every block lives at a 64-bit record_ix_t address.
 *   - Record indexes are in ordinal units, not bytes.
 *   - Allocators are not thread-safe.
 *   - You may read a block at any valid index, even if never written.
 *   - Uninitialized records read as zeros.
 *
 * Internal bookkeeping uses signed 64-bit arithmetic, so the largest
 * valid record index is roughly INT64_MAX / record_size.
 *
 * All allocators are members of an allocator group, which may be a group
 * of 1. Allocators in a group share a collective memory limit. Regardless
 * of the data stored in the individual allocators, the group will never use
 * more memory than the limit specified at the time the group was created
 * (modulo rounding; see below).
 */

#define	MAX_RECORD_SIZE		(8 << 10)	/* 8KB */
#define	DEFAULT_CLUSTER_SIZE	(1 << 20)	/* Default cluster size, 1MB */

typedef uint64_t record_ix_t;

typedef struct allocator allocator_t;

typedef struct {
	size_t		as_memory_used;
	size_t		as_record_size;
	uint64_t	as_num_records;
} allocator_stats_t;

/*
 * Create a set of num_allocators allocator_t's. One allocator_t is created
 * for each value in the record_sizes array.
 *
 * Over time, a group may internally reassign memory, forcing some records
 * to disk. (If there is no disk backup, this becomes a fatal error.) Memory
 * rebalancing is transparent to the client.
 *
 * Records are evicted from memory one allocator at a time.
 * allocator_group_init()[0] is the first allocator asked to yield memory.
 * When it has no more memory to surrender, memory will be drawn from
 * allocator_group_init()[1], and so on. The ultimate end state under high
 * load is that the last allocator holds all the group's memory, with other
 * allocators having functionally become disk-only. If this is not the
 * behavior you want, create multiple groups.
 *
 * - Memory is managed in page clusters. You can set cluster_size to 0 to
 *   use the default cluster size, which is 1MB or one page, whichever
 *   is larger.
 *
 * - If a nonzero cluster_size value is specified, it will be raised to
 *   hold the largest record and rounded up to a multiple of the system
 *   page size. So, you can use a size of 1 to mean "as small as possible."
 *
 * - A mem_limit of zero means the group is disk-only. A nonzero mem_limit
 *   should be at least num_allocators * cluster_size; this floor is
 *   silently enforced. The memory limit will be rounded up to the nearest
 *   multiple of the cluster size.
 *
 * - No record size can be larger than MAX_RECORD_SIZE (8KB).
 *
 * - The returned array of allocator_t's is stable and valid until the
 *   allocator group is finalized.
 */
allocator_t **
allocator_group_init(const size_t *record_sizes, unsigned num_allocators,
    size_t cluster_size, size_t mem_limit, const char *dir_path);

void
allocator_retrieve(allocator_t *alloc, record_ix_t record, void *buff);

void
allocator_store(allocator_t *alloc, record_ix_t record, const void *buff);

/*
 * Append a new record and return its index.
 */
record_ix_t
allocator_append(allocator_t *alloc, const void *data);

/*
 * Skip ahead one record. Returns the index that was skipped. It's guaranteed
 * that this record has never been written, so it reads as zeros.
 */
record_ix_t
allocator_skip(allocator_t *alloc);

allocator_stats_t
allocator_get_stats(allocator_t *alloc);

/*
 * Destroy an allocator group and all of the allocators it contains. You
 * can name any allocator in the group.
 */
void
allocator_group_fini(allocator_t *alloc);

#ifdef __cplusplus
}
#endif

#endif /* _ZSTREAM_ALLOC_H */
