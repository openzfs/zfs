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
 * This is the internal header for the linear hash table. It holds the
 * #defines and struct definitions that back the implementation but that
 * shouldn't be part of the public API in zstream_hash.h.
 */

#ifndef _ZSTREAM_HASH_IMPL_H
#define	_ZSTREAM_HASH_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "zstream_alloc.h"
#include "zstream_hash.h"
#include "zstream_minheap.h"

#define	ENTRIES_PER_BUCKET		6
#define	FREEHEAP_INITIAL_SIZE		64  /* Small -> better test coverage */

/*
 * Entry in a bucket: hash value + locator to data
 */
typedef struct {
	uint64_t	be_hash;
	record_ix_t	be_record;	/* Index of actual data */
} bucket_entry_t;

/*
 * Bucket structure: fixed array of entries + overflow pointer. Overflow
 * buckets have a separate allocator.
 *
 * Invariants:
 *
 * - An entry whose be_record is zero marks the logical end of a bucket
 *   chain. If a bucket contains such an entry, it must be the last bucket in
 *   its chain (and its b_overflow value must therefore be zero).
 *
 * - If a bucket contains an entry whose be_record is zero, no element of
 *   that entry or of any subsequent entry may be nonzero.
 *
 * - If a bucket's b_overflow pointer is nonzero, it must point to an
 *   overflow bucket that contains at least one nonzero entry.
 *
 * That last invariant is needed to avoid unnecessary reads when transiting
 * full buckets. If empty overflow buckets were allowed, it would be necessary
 * to read the empty bucket to determine whether more entries existed.
 */
typedef struct {
	bucket_entry_t	b_entries[ENTRIES_PER_BUCKET];
	record_ix_t	b_overflow;			/* 0 == no overflow */
} bucket_t;

/*
 * These allocators are listed in inverse memory priority order. The data
 * allocator is first to be asked to yield memory, and so on. (The actual
 * memory management is handled by the allocator group.)
 */
typedef struct {
	allocator_t	*data;		/* Data records */
	allocator_t	*overflow;	/* Overflow buckets */
	allocator_t	*bucket;	/* Main buckets */
} lh_allocators_t;

typedef struct {
	uint64_t	top_buckets;
	uint64_t	top_entries;
	uint64_t	max_top_entries;
} lh_occupancy_t;

/*
 * Reduce a 32-byte key (zero-padded if necessary) to a 64-bit hash value.
 * lh_init() uses CityHash by default. Selftests replace the hash function
 * to control hash values, which is safe only before the first record
 * operation on the table.
 */
typedef uint64_t lh_hash_func_f(const uint64_t key[MAX_KEY_SIZE /
    sizeof (uint64_t)]);

struct linear_hash {
	size_t		lh_record_size;		  /* Param */
	size_t		lh_key_size;		  /* Record prefix <= 32 */
	lh_hash_func_f	*lh_hash_func;		  /* Key -> 64-bit hash */
	lh_allocators_t	lh_alloc;		  /* Three allocators */
	unsigned int	lh_hash_suffix_length;	  /* The short mask length */
	record_ix_t	lh_split_pointer;	  /* Next bucket to split */
	lh_occupancy_t	lh_occupancy;		  /* Occupancy */
	minheap_t	lh_freeheap;		  /* Available overflow bkts */
};

/*
 * Internal iterator for bucket entries
 */
typedef struct {
	linear_hash_t	*ei_lh;		/* The hash that owns this iterator */
	record_ix_t	ei_bucket_ix;	/* Index of bucket within allocator */
	int		ei_entry_ix;	/* Ix within bucket; -1 == not read */
	bucket_t	ei_bucket;	/* Working copy of bucket */
	boolean_t	ei_in_overflow;	/* Which allocator: main or overflow? */
	boolean_t	ei_dirty;	/* Needs writeback? */
} entry_iterator_t;

#ifdef __cplusplus
}
#endif

#endif /* _ZSTREAM_HASH_IMPL_H */
