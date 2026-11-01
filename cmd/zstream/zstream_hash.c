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

#include <assert.h>
#include <cityhash.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stdtypes.h>
#include <sys/types.h>

#include "zstream_alloc.h"
#include "zstream_hash.h"
#include "zstream_hash_impl.h"
#include "zstream_util.h"

/*
 * The Wikipedia article for linear hashing isn't exhaustive, so here are
 * a few more details on the basic concept.
 *
 * Record keys of length key_size are reduced to 64-bit values using CityHash.
 * Hash values are then further masked to reduce their effective length.
 *
 * At any given time, two mask lengths are in use, the longer being one bit
 * longer than the shorter. Buckets hashed with the long mask appear at the
 * beginning of the table, followed by a set of short-mask-hashed buckets
 * and then another batch of long-hashed buckets. A cursor, the split pointer,
 * points to the lowest-numbered bucket hashed with the shorter mask.
 *
 *      +----------------------------+ <- index 0
 *      |                            |
 *      |   long-mask (h+1) buckets  |
 *      |       already split        |
 *      |                            |
 *      +----------------------------+
 *      |                            | <- split pointer
 *      |   short-mask (h) buckets   |    (buckets to be split)
 *      |       not yet split        |
 *      |                            |
 *      +----------------------------+ <- power-of-2 boundary
 *      |                            |   (cycle restarts when split
 *      |   long-mask (h+1) buddies  |    pointer gets here)
 *      |    of buckets below the    |
 *      |       split pointer        |
 *      |                            |
 *      +----------------------------+ <- current end of table
 *      |                            |
 *      |      not yet allocated     |
 *      |    (will hold buddies of   |
 *      |      short-mask buckets    |
 *      |       at or above the      |
 *      |        split pointer)      |
 *      |                            |
 *      +----------------------------+ <- power-of-2 boundary
 *
 * When occupancy exceeds a given limit, the contents of the bucket pointed
 * to by the split pointer are rehashed using the longer key length. Since
 * there's only one bit's difference between the two masks, there are only
 * two possible destinations for rehashed entries: the same bucket they're
 * already in, or one that's 2^n slots later, at the very end of the table.
 * On average, half the entries are relocated. The split pointer is then
 * incremented.
 *
 * Eventually, all short-key buckets have been rehashed with the longer
 * mask. At that point, what was previously the long key becomes the short
 * key and a new one-bit-longer mask is introduced. The split pointer is
 * reset to the beginning of the hash table.
 *
 * This scheme grows the table linearly and incrementally. It's useful for
 * stream processing because it's space-efficient and because we often don't
 * know how long the input stream will be. When the table becomes too large
 * to keep in memory, the tail can spill over to disk storage without
 * abandoning or reshuffling the existing in-memory entries.
 *
 * This implementation uses open hashing (overflow buckets). Records can
 * be updated (overwritten) but not deleted.
 *
 * Each hash table has three allocator_t's underneath it: one for the data
 * being stored, one for top-level buckets, and one for overflow buckets.
 *
 * Struct definitions are in zstream_hash_impl.h to make them available
 * to selftests.
 */

#define	INITIAL_HASH_SUFFIX_LENGTH	10
#define	MAX_OCCUPANCY			0.75

#define	MAX_TOP_ENTRIES(top_buckets)	\
	    ((double)((top_buckets) * ENTRIES_PER_BUCKET) * MAX_OCCUPANCY)

#define	ITER_BUCKET(lh, bucket) {					\
		.ei_lh = lh,						\
		.ei_bucket_ix = bucket,					\
		.ei_entry_ix = -1					\
	}

/*
 * Determines the bucket allocator for the given entry_iterator_t
 */
#define	ALLOC_FOR(iter) ((iter)->ei_in_overflow ? \
	    (iter)->ei_lh->lh_alloc.overflow : (iter)->ei_lh->lh_alloc.bucket)

/*
 * Evaluates to a pointer to the iterator's current bucket_entry_t
 */
#define	BUCKET_ENTRY(iter) (&(iter)->ei_bucket.b_entries[(iter)->ei_entry_ix])

static uint64_t
hash_function(const uint64_t key[MAX_KEY_SIZE / sizeof (uint64_t)])
{
	return (cityhash4(key[0], key[1], key[2], key[3]));
}

linear_hash_t *
lh_init(size_t record_size, size_t key_size, size_t max_memory,
    const char *spill_dir)
{
	if (max_memory == 0 && spill_dir == NULL)
		errx(1, "linear_hash_t requires memory or disk backing");
	if (record_size > MAX_RECORD_SIZE || record_size == 0)
		errx(1, "linear_hash_t record size must be specified "
		    "and <= %d", MAX_RECORD_SIZE);
	if (key_size > MAX_KEY_SIZE || key_size == 0)
		errx(1, "linear_hash_t key size must be specified and <= %d",
		    MAX_KEY_SIZE);
	if (key_size > record_size)
		errx(1, "linear_hash_t key size cannot exceed record size");

	uint64_t top_buckets = 1ULL << INITIAL_HASH_SUFFIX_LENGTH;
	size_t record_sizes[] = {
		record_size, sizeof (bucket_t), sizeof (bucket_t)
	};
	size_t cluster_size = (max_memory <= DEFAULT_CLUSTER_SIZE) ? 1 : 0;
	allocator_t **allocators = allocator_group_init(record_sizes, 3,
	    cluster_size, max_memory, spill_dir);
	linear_hash_t *lh = safe_malloc(sizeof (linear_hash_t));
	*lh = (linear_hash_t) {
		.lh_record_size = record_size,
		.lh_key_size = key_size,
		.lh_hash_func = hash_function,
		.lh_alloc = {
		    .data = allocators[0],
		    .overflow = allocators[1],
		    .bucket = allocators[2],
		},
		.lh_hash_suffix_length = INITIAL_HASH_SUFFIX_LENGTH,
		.lh_occupancy = {
		    .top_buckets = top_buckets,
		    .max_top_entries = MAX_TOP_ENTRIES(top_buckets),
		},
		.lh_freeheap = minheap_init(FREEHEAP_INITIAL_SIZE)
	};
	/* The index 0 is a sentinel value for these allocators */
	allocator_skip(lh->lh_alloc.data);
	allocator_skip(lh->lh_alloc.overflow);
	return (lh);
}

/*
 * Recycle or create an overflow bucket. A recycled bucket from the free
 * heap is always the lowest-numbered one available. No guarantees are made
 * about the contents of the bucket.
 */
static inline record_ix_t
find_overflow_bucket(linear_hash_t *lh)
{
	record_ix_t ix = minheap_pop(&lh->lh_freeheap);
	if (ix != 0)
		return (ix);
	return (allocator_skip(lh->lh_alloc.overflow));
}

/*
 * Put the named overflow bucket and all its descendants on the free heap.
 */
static inline void
free_overflow_chain(linear_hash_t *lh, record_ix_t ix)
{
	bucket_t buff;
	while (ix != 0) {
		minheap_push(&lh->lh_freeheap, ix);
		allocator_retrieve(lh->lh_alloc.overflow, ix, &buff);
		ix = buff.b_overflow;
	}
}

/*
 * The next three functions are the internal API for iterating bucket chains.
 */
static inline void
read_bucket(entry_iterator_t *iter)
{
	allocator_retrieve(ALLOC_FOR(iter), iter->ei_bucket_ix,
	    &iter->ei_bucket);
	iter->ei_entry_ix = 0;
	iter->ei_dirty = B_FALSE;
}

static inline void
save_bucket(entry_iterator_t *iter, boolean_t force)
{
	if (!force && iter->ei_dirty == B_FALSE)
		return;
	allocator_store(ALLOC_FOR(iter), iter->ei_bucket_ix, &iter->ei_bucket);
	iter->ei_dirty = B_FALSE;
}

/*
 * Prepare an entry iterator to examine the next bucket entry. Updates the
 * entry_iterator_t struct and returns a pointer to the next (now current)
 * bucket entry. Returns NULL when there are no more entries, or,
 * alternately, extends the bucket chain by adding an overflow bucket.
 */
static inline bucket_entry_t *
entry_iterator_next(entry_iterator_t *iter, boolean_t extend)
{
	if (iter->ei_entry_ix < 0) {
		/* Bucket not yet read */
		read_bucket(iter);
	} else if (iter->ei_entry_ix == ENTRIES_PER_BUCKET - 1) {
		/* Moving past the last entry in the current bucket... */
		if (iter->ei_bucket.b_overflow != 0) {
			/* ...but there are more buckets in the chain */
			save_bucket(iter, B_FALSE);
			iter->ei_in_overflow = B_TRUE;
			iter->ei_bucket_ix = iter->ei_bucket.b_overflow;
			read_bucket(iter);
		} else if (!extend) {
			/* ...or not */
			save_bucket(iter, B_FALSE);
			return (NULL);
		} else {
			/* ...or not, but extend by adding an overflow bucket */
			record_ix_t over = find_overflow_bucket(iter->ei_lh);
			iter->ei_bucket.b_overflow = over;
			save_bucket(iter, B_TRUE);
			*iter = (entry_iterator_t) {
				.ei_lh = iter->ei_lh,
				.ei_bucket_ix = over,
				.ei_in_overflow = B_TRUE,
				.ei_dirty = B_TRUE
			};
		}
	} else {
		iter->ei_entry_ix++;
	}
	return (BUCKET_ENTRY(iter));
}

/*
 * Calculate the destination bucket for a given hash value. There are
 * three separate regions where an insertion might land, in this order:
 *
 *   Region 1: Long-mask buckets that used to be short-mask and were split
 *   Region 2: As-yet-unsplit short-mask buckets
 *   Region 3: Long-mask buckets created as buddies for region 1 buckets
 *             when they were split
 *
 * lh_hash_suffix_length is the mask length in effect within region 2.
 * Elsewhere, it is one bit longer. We don't need to distinguish between
 * regions 1 and 3 - the arithmetic works out automatically.
 */
static inline uint64_t
bucket_for_hash(linear_hash_t *lh, uint64_t hash)
{
	uint64_t mask = (1ULL << (lh->lh_hash_suffix_length)) - 1;
	if ((hash & mask) < lh->lh_split_pointer)
		mask = (mask << 1) | 1;
	return (hash & mask);
}

/*
 * Split the bucket pointed to by the split pointer, rehashing entries
 * according to the one-higher suffix length. Since only one bit is added,
 * existing entries either stay where they are or go to one new buddy bucket
 * at the end of the table.
 *
 * During the partition pass, the source bucket has both a read iterator and
 * a write iterator. This is fine; each iterator has its own copy of the
 * bucket. Reading will always precede writing, and the bucket is never
 * modified through the read iterator.
 *
 * At steady state, bucket entries are packed at the front of the chain and
 * all inactive entries are zeroed out. The first entry with a record number
 * of 0 marks the end of entries. After partitioning a bucket, we have to
 * zero out any tail. We also remove from the source chain any now-unused
 * overflow buckets and add those buckets to the free heap for reuse.
 */
static void
split_bucket(linear_hash_t *lh)
{
	record_ix_t bucket_ix = lh->lh_split_pointer;
	record_ix_t buddy_ix  = bucket_ix | (1ULL << lh->lh_hash_suffix_length);

	entry_iterator_t source = ITER_BUCKET(lh, bucket_ix);
	entry_iterator_t stay   = ITER_BUCKET(lh, bucket_ix);
	entry_iterator_t move   = ITER_BUCKET(lh, buddy_ix);

	/*
	 * Increment early so that bucket_for_hash() now uses the long mask
	 * for this bucket.
	 */
	lh->lh_split_pointer++;

	bucket_entry_t *source_be;
	boolean_t had_source_entries = B_FALSE;
	while ((source_be = entry_iterator_next(&source, B_FALSE))) {
		if (source_be->be_record == 0)
			break;
		had_source_entries = B_TRUE;
		boolean_t this_entry_stays =
		    bucket_for_hash(lh, source_be->be_hash) == bucket_ix;
		entry_iterator_t *dest = this_entry_stays ? &stay : &move;
		bucket_entry_t *dest_be = entry_iterator_next(dest, B_TRUE);
		/*
		 * Check whether this is actually a change. If all records
		 * stay, the source bucket needn't be rewritten.
		 */
		if (dest_be->be_record != source_be->be_record)
			dest->ei_dirty = B_TRUE;
		*dest_be = *source_be;

		/* Occupancy bookkeeping */
		if (!source.ei_in_overflow) lh->lh_occupancy.top_entries--;
		if (!dest->ei_in_overflow)  lh->lh_occupancy.top_entries++;
	}

	lh->lh_occupancy.top_buckets++;
	lh->lh_occupancy.max_top_entries =
	    MAX_TOP_ENTRIES(lh->lh_occupancy.top_buckets);

	/*
	 * Have we completed the full hashing cycle at this suffix length?
	 */
	record_ix_t buckets_this_cycle = 1ULL << (lh->lh_hash_suffix_length);
	if (lh->lh_split_pointer >= buckets_this_cycle) {
		lh->lh_hash_suffix_length++;
		lh->lh_split_pointer = 0;
	}

	/*
	 * If there were no source entries, then both the source bucket and
	 * the buddy bucket are already zeroed out. We just examined the
	 * source bucket, and the buddy bucket is as yet unwritten, ergo zero.
	 */
	if (!had_source_entries)
		return;

	/*
	 * Clean up the "stay" iterator. We may have to manually prime the
	 * iteration if no records stayed in the source bucket.
	 *
	 * If there's a following overflow bucket, that bucket and its
	 * descendants get moved to the free heap.
	 */
	if (stay.ei_entry_ix == -1) {
		bucket_entry_t *bent = entry_iterator_next(&stay, B_FALSE);
		stay.ei_dirty = bent->be_record != 0;
		*bent = (bucket_entry_t) {0};
	}
	if (stay.ei_bucket.b_overflow != 0) {
		free_overflow_chain(lh, stay.ei_bucket.b_overflow);
		stay.ei_bucket.b_overflow = 0;
		stay.ei_dirty = B_TRUE;
	}
	/*
	 * Also zero out the tail of the current stay bucket. We know the
	 * bucket started out fully consolidated (because that's a
	 * steady-state invariant), so as soon as we see a blank slot
	 * (record == 0), we know there are no more slots to zero out. We
	 * already set the overflow field to zero, so iteration will not
	 * continue beyond the current bucket.
	 */
	bucket_entry_t *ent;
	while ((ent = entry_iterator_next(&stay, B_FALSE)) && ent->be_record) {
		*ent = (bucket_entry_t) {0};
		stay.ei_dirty = B_TRUE;
	}
	save_bucket(&stay, B_FALSE);
	save_bucket(&move, B_FALSE);
}

/*
 * Helper for lh_record() that inserts a new record. The iterator must
 * already be teed up to an empty slot. If we insert into a top-level
 * bucket, check whether we need to trigger a split.
 */
static inline void
insert_record(entry_iterator_t *iter, uint64_t hash, const uint8_t *buff)
{
	linear_hash_t *lh = iter->ei_lh;
	*BUCKET_ENTRY(iter) = (bucket_entry_t) {
		.be_hash = hash,
		.be_record = allocator_append(lh->lh_alloc.data, buff)
	};
	save_bucket(iter, B_TRUE);
	if (!iter->ei_in_overflow) {
		lh_occupancy_t *occ = &lh->lh_occupancy;
		occ->top_entries++;
		while (occ->top_entries > occ->max_top_entries)
			split_bucket(lh);
	}
}

/*
 * All record-level operations: LH_FIND, LH_INSERT, LH_UPDATE, and LH_UPSERT
 */
lh_result_t
lh_record(linear_hash_t *lh, lh_op_t op, void *buff_in)
{
	VERIFY(lh != NULL && buff_in != NULL && op < LH_NUM_OPS);

	boolean_t insert = op == LH_INSERT || op == LH_UPSERT;
	boolean_t update = op == LH_UPDATE || op == LH_UPSERT;

	/* Zero-pad the key to MAX_KEY_SIZE and calculate hash */
	uint8_t *buff = buff_in;
	uint64_t key[MAX_KEY_SIZE / sizeof (uint64_t)] = {0};
	size_t key_size = lh->lh_key_size;
	memcpy((uint8_t *)key, buff, key_size);
	uint64_t hash = lh->lh_hash_func(key);

	/* Set up to walk the bucket chain */
	bucket_entry_t *entry;
	uint8_t scratch[lh->lh_record_size];
	entry_iterator_t iter = ITER_BUCKET(lh, bucket_for_hash(lh, hash));

	/* Walk */
	while ((entry = entry_iterator_next(&iter, insert))) {
		if (entry->be_record == 0) {
			/* End of chain, empty slot */
			if (!insert)
				return (LH_NOT_FOUND);
			insert_record(&iter, hash, buff);
			return (LH_INSERTED);
		} else if (entry->be_hash == hash) {
			/* Hashes match, but full key may not */
			allocator_retrieve(lh->lh_alloc.data, entry->be_record,
			    scratch);
			if (memcmp(scratch, key, key_size) == 0) {
				if (update) {
					allocator_store(lh->lh_alloc.data,
					    entry->be_record, buff);
				}
				memcpy(buff, scratch, lh->lh_record_size);
				return (update ? LH_UPDATED : LH_FOUND);
			}
		}
	}
	/* Only a non-extending walk can run off the end of the chain */
	VERIFY(!insert);
	return (LH_NOT_FOUND);
}

lh_stats_t
lh_get_stats(linear_hash_t *lh)
{
	allocator_stats_t data = allocator_get_stats(lh->lh_alloc.data);
	allocator_stats_t overflow = allocator_get_stats(lh->lh_alloc.overflow);
	allocator_stats_t top = allocator_get_stats(lh->lh_alloc.bucket);
	lh_stats_t stats = {
		.ls_memory_use = data.as_memory_used +
		    overflow.as_memory_used +
		    top.as_memory_used +
		    lh->lh_freeheap.size * sizeof (record_ix_t),
		/* Don't count the index-0 sentinel */
		.ls_total_records = data.as_num_records - 1
	};
	return (stats);
}

void
lh_fini(linear_hash_t *lh)
{
	VERIFY(lh != NULL);
	if (lh->lh_alloc.data != NULL)
		allocator_group_fini(lh->lh_alloc.data);
	minheap_fini(&lh->lh_freeheap);
	free(lh);
}
