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
 * Copyright (c) 2020, 2021, 2022 by Pawel Jakub Dawidek
 */

#ifndef _SYS_BRT_IMPL_H
#define	_SYS_BRT_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * BRT - Block Reference Table.
 */
#define	BRT_OBJECT_VDEV_PREFIX	"com.fudosecurity:brt:vdev:"

/*
 * We divide each VDEV into 16MB chunks. Each chunk is represented in memory
 * by a 16bit counter, thus 1TB VDEV requires 128kB of memory: (1TB / 16MB) * 2B
 * Each element in this array represents how many BRT entries do we have in this
 * chunk of storage. We always load this entire array into memory and update as
 * needed. By having it in memory we can quickly tell (during zio_free()) if
 * there are any BRT entries that we might need to update.
 *
 * This value cannot be larger than 16MB, at least as long as we support
 * 512 byte block sizes. With 512 byte block size we can have exactly
 * 32768 blocks in 16MB. In 32MB we could have 65536 blocks, which is one too
 * many for a 16bit counter.
 */
#define	BRT_RANGESIZE	(16 * 1024 * 1024)
_Static_assert(BRT_RANGESIZE / SPA_MINBLOCKSIZE <= UINT16_MAX,
	"BRT_RANGESIZE is too large.");
/*
 * We don't want to update the whole structure every time. Maintain bitmap
 * of dirty blocks within the regions, so that a single bit represents a
 * block size of entcounts. For example if we have a 1PB vdev then all
 * entcounts take 128MB of memory ((64TB / 16MB) * 2B). We can divide this
 * 128MB array of entcounts into 32kB disk blocks, as we don't want to update
 * the whole 128MB on disk when we have updated only a single entcount.
 * We maintain a bitmap where each 32kB disk block within 128MB entcounts array
 * is represented by a single bit. This gives us 4096 bits. A set bit in the
 * bitmap means that we had a change in at least one of the 16384 entcounts
 * that reside on a 32kB disk block (32kB / sizeof (uint16_t)).
 */
#define	BRT_BLOCKSIZE	(32 * 1024)
#define	BRT_RANGESIZE_TO_NBLOCKS(size)					\
	(((size) - 1) / (BRT_BLOCKSIZE / sizeof (uint16_t)) + 1)

#define	BRT_LITTLE_ENDIAN	0
#define	BRT_BIG_ENDIAN		1
#ifdef _ZFS_LITTLE_ENDIAN
#define	BRT_NATIVE_BYTEORDER		BRT_LITTLE_ENDIAN
#define	BRT_NON_NATIVE_BYTEORDER	BRT_BIG_ENDIAN
#else
#define	BRT_NATIVE_BYTEORDER		BRT_BIG_ENDIAN
#define	BRT_NON_NATIVE_BYTEORDER	BRT_LITTLE_ENDIAN
#endif

typedef struct brt_vdev_phys {
	uint64_t	bvp_mos_entries;
	uint64_t	bvp_size;
	uint64_t	bvp_byteorder;
	uint64_t	bvp_totalcount;
	uint64_t	bvp_rangesize;
	uint64_t	bvp_usedspace;
	uint64_t	bvp_savedspace;
} brt_vdev_phys_t;

struct brt_vdev {
	/*
	 * Pending changes from open contexts.
	 */
	kmutex_t	bv_pending_lock;
	avl_tree_t	bv_pending_tree[TXG_SIZE];
	/*
	 * Protects bv_mos_*.
	 */
	krwlock_t	bv_mos_entries_lock ____cacheline_aligned;
	/*
	 * Protects all the fields starting from bv_initiated.
	 */
	krwlock_t	bv_lock ____cacheline_aligned;
	/*
	 * VDEV id.
	 */
	uint64_t	bv_vdevid ____cacheline_aligned;
	/*
	 * Object number in the MOS for the entcount array and brt_vdev_phys.
	 */
	uint64_t	bv_mos_brtvdev;
	/*
	 * Object number in the MOS and dnode for the entries table.
	 */
	uint64_t	bv_mos_entries;
	dnode_t		*bv_mos_entries_dnode;
	/*
	 * Is the structure initiated?
	 * (bv_entcount and bv_bitmap are allocated?)
	 */
	boolean_t	bv_initiated;
	/*
	 * Does the bv_entcount[] array needs byte swapping?
	 */
	boolean_t	bv_need_byteswap;
	/*
	 * Number of entries in the bv_entcount[] array.
	 */
	uint64_t	bv_size;
	/*
	 * This is the array with BRT entry count per BRT_RANGESIZE.
	 */
	uint16_t	*bv_entcount;
	/*
	 * bv_entcount[] potentially can be a bit too big to sychronize it all
	 * when we just changed few entcounts. The fields below allow us to
	 * track updates to bv_entcount[] array since the last sync.
	 * A single bit in the bv_bitmap represents as many entcounts as can
	 * fit into a single BRT_BLOCKSIZE.
	 * For example we have 65536 entcounts in the bv_entcount array
	 * (so the whole array is 128kB). We updated bv_entcount[2] and
	 * bv_entcount[5]. In that case only first bit in the bv_bitmap will
	 * be set and we will write only first BRT_BLOCKSIZE out of 128kB.
	 */
	ulong_t		*bv_bitmap;
	/*
	 * bv_entcount[] needs updating on disk.
	 */
	boolean_t	bv_entcount_dirty;
	/*
	 * brt_vdev_phys needs updating on disk.
	 */
	boolean_t	bv_meta_dirty;
	/*
	 * Sum of all bv_entcount[]s.
	 */
	uint64_t	bv_totalcount;
	/*
	 * Space on disk occupied by cloned blocks (without compression).
	 */
	uint64_t	bv_usedspace;
	/*
	 * How much additional space would be occupied without block cloning.
	 */
	uint64_t	bv_savedspace;
	/*
	 * Entries to sync.
	 */
	avl_tree_t	bv_tree;
};

/*
 * Clones of blocks with the DEDUP bit set reference the DDT instead of
 * the BRT.  Their pending entries are kept separately from the per-vdev
 * pending trees, sorted by the block checksum, matching the DDT ZAP hash
 * order (the first checksum word is the pre-hashed ZAP key), and sharded
 * by its top bits.  It allows syncing context to process the shards in
 * parallel, each walking a disjoint range of DDT ZAP leaves in the hash
 * order, so that each compressed leaf block is decompressed only once
 * and never by more than one thread.
 */
#define	BRT_DEDUP_SHARDS_SHIFT	4
#define	BRT_DEDUP_SHARDS	(1 << BRT_DEDUP_SHARDS_SHIFT)
#define	BRT_DEDUP_SHARD(bp)						\
	((bp)->blk_cksum.zc_word[0] >> (64 - BRT_DEDUP_SHARDS_SHIFT))

struct brt_dedup_shard {
	kmutex_t	bds_lock;
	avl_tree_t	bds_tree[TXG_SIZE];
};

/* Size of offset / sizeof (uint64_t). */
#define	BRT_KEY_WORDS	(1)

#define	BRE_OFFSET(bre)	(DVA_GET_OFFSET(&(bre)->bre_bp.blk_dva[0]))

/*
 * In-core brt entry.
 * On-disk we use ZAP with offset as the key and count as the value.
 */
typedef struct brt_entry {
	avl_node_t	bre_node;
	blkptr_t	bre_bp;
	uint64_t	bre_count;
	uint64_t	bre_pcount;
} brt_entry_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BRT_IMPL_H */
