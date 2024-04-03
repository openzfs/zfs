/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2020, 2021, 2022 by Pawel Jakub Dawidek
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/brt.h>
#include <sys/brt_impl.h>
#include <sys/ddt.h>
#include <sys/bitmap.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/vdev_impl.h>
#include <sys/kstat.h>
#include <sys/wmsum.h>

/*
 * Block Cloning design.
 *
 * Block Cloning allows to manually clone a file (or a subset of its blocks)
 * into another (or the same) file by just creating additional references to
 * the data blocks without copying the data itself. Those references are kept
 * in the Block Reference Tables (BRTs).
 *
 * In many ways this is similar to the existing deduplication, but there are
 * some important differences:
 *
 * - Deduplication is automatic and Block Cloning is not - one has to use a
 *   dedicated system call(s) to clone the given file/blocks.
 * - Deduplication keeps all data blocks in its table, even those referenced
 *   just once. Block Cloning creates an entry in its tables only when there
 *   are at least two references to the given data block. If the block was
 *   never explicitly cloned or the second to last reference was dropped,
 *   there will be neither space nor performance overhead.
 * - Deduplication needs data to work - one needs to pass real data to the
 *   write(2) syscall, so hash can be calculated. Block Cloning doesn't require
 *   data, just block pointers to the data, so it is extremely fast, as we pay
 *   neither the cost of reading the data, nor the cost of writing the data -
 *   we operate exclusively on metadata.
 * - If the D (dedup) bit is not set in the block pointer, it means that
 *   the block is not in the dedup table (DDT) and we won't consult the DDT
 *   when we need to free the block. Block Cloning must be consulted on every
 *   free, because we cannot modify the source BP (eg. by setting something
 *   similar to the D bit), thus we have no hint if the block is in the
 *   Block Reference Table (BRT), so we need to look into the BRT. There is
 *   an optimization in place that allows us to eliminate the majority of BRT
 *   lookups which is described below in the "Minimizing free penalty" section.
 * - The BRT entry is much smaller than the DDT entry - for BRT we only store
 *   64bit offset and 64bit reference counter.
 * - Dedup keys are cryptographic hashes, so two blocks that are close to each
 *   other on disk are most likely in totally different parts of the DDT.
 *   The BRT entry keys are offsets into a single top-level VDEV, so data blocks
 *   from one file should have BRT entries close to each other.
 * - Scrub will only do a single pass over a block that is referenced multiple
 *   times in the DDT. Unfortunately it is not currently (if at all) possible
 *   with Block Cloning and block referenced multiple times will be scrubbed
 *   multiple times. The new, sorted scrub should be able to eliminate
 *   duplicated reads given enough memory.
 * - Deduplication requires cryptographically strong hash as a checksum or
 *   additional data verification. Block Cloning works with any checksum
 *   algorithm or even with checksumming disabled.
 *
 * As mentioned above, the BRT entries are much smaller than the DDT entries.
 * To uniquely identify a block we just need its vdev id and offset. We also
 * need to maintain a reference counter. The vdev id will often repeat, as there
 * is a small number of top-level VDEVs and a large number of blocks stored in
 * each VDEV. We take advantage of that to reduce the BRT entry size further by
 * maintaining one BRT for each top-level VDEV, so we can then have only offset
 * and counter as the BRT entry.
 *
 * Minimizing free penalty.
 *
 * Block Cloning allows creating additional references to any existing block.
 * When we free a block there is no hint in the block pointer whether the block
 * was cloned or not, so on each free we have to check if there is a
 * corresponding entry in the BRT or not. If there is, we need to decrease
 * the reference counter. Doing BRT lookup on every free can potentially be
 * expensive by requiring additional I/Os if the BRT doesn't fit into memory.
 * This is the main problem with deduplication, so we've learned our lesson and
 * try not to repeat the same mistake here. How do we do that? We divide each
 * top-level VDEV into 16MB regions. For each region we maintain a counter that
 * is a sum of all the BRT entries that have offsets within the region. This
 * creates the entries count array of 16bit numbers for each top-level VDEV.
 * The entries count array is always kept in memory and updated on disk in the
 * same transaction group as the BRT updates to keep everything in-sync. We can
 * keep the array in memory, because it is very small. With 16MB regions and
 * 1TB VDEV the array requires only 128kB of memory (we may decide to decrease
 * the region size even further in the future). Now, when we want to free
 * a block, we first consult the array. If the counter for the whole region is
 * zero, there is no need to look for the BRT entry, as there isn't one for
 * sure. If the counter for the region is greater than zero, only then we will
 * do a BRT lookup and if an entry is found we will decrease the reference
 * counter in the BRT entry and in the entry counters array.
 *
 * The entry counters array is small, but can potentially be larger for very
 * large VDEVs or smaller regions. In this case we don't want to rewrite entire
 * array on every change. We then divide the array into 32kB block and keep
 * a bitmap of dirty blocks within a transaction group. When we sync the
 * transaction group we can only update the parts of the entry counters array
 * that were modified. Note: Keeping track of the dirty parts of the entry
 * counters array is implemented, but updating only parts of the array on disk
 * is not yet implemented - for now we will update entire array if there was
 * any change.
 *
 * The implementation tries to be economic: if BRT is not used, or no longer
 * used, there will be no entries in the MOS and no additional memory used (eg.
 * the entry counters array is only allocated if needed).
 *
 * Interaction between Deduplication and Block Cloning.
 *
 * If both functionalities are in use, we could end up with a block that is
 * referenced multiple times in both DDT and BRT. When we free one of the
 * references we couldn't tell where it belongs, so we would have to decide
 * what table takes the precedence: do we first clear DDT references or BRT
 * references? To avoid this dilemma BRT cooperates with DDT - if a given block
 * is being cloned using BRT and the BP has the D (dedup) bit set, BRT will
 * lookup DDT entry instead and increase the counter there. No BRT entry
 * will be created for a block which has the D (dedup) bit set.
 * BRT may be more efficient for manual deduplication, but if the block is
 * already in the DDT, then creating additional BRT entry would be less
 * efficient. This clever idea was proposed by Allan Jude.
 *
 * Block Cloning across datasets.
 *
 * Block Cloning is not limited to cloning blocks within the same dataset.
 * It is possible (and very useful) to clone blocks between different datasets.
 * One use case is recovering files from snapshots. By cloning the files into
 * dataset we need no additional storage. Without Block Cloning we would need
 * additional space for those files.
 * Another interesting use case is moving the files between datasets
 * (copying the file content to the new dataset and removing the source file).
 * In that case Block Cloning will only be used briefly, because the BRT entries
 * will be removed when the source is removed.
 * Block Cloning across encrypted datasets is supported as long as both
 * datasets share the same master key (e.g. snapshots and clones)
 *
 * Block Cloning flow through ZFS layers.
 *
 * Note: Block Cloning can be used both for cloning file system blocks and ZVOL
 * blocks. As of this writing no interface is implemented that allows for block
 * cloning within a ZVOL.
 * FreeBSD and Linux provides copy_file_range(2) system call and we will use it
 * for blocking cloning.
 *
 *	ssize_t
 *	copy_file_range(int infd, off_t *inoffp, int outfd, off_t *outoffp,
 *	                size_t len, unsigned int flags);
 *
 * Even though offsets and length represent bytes, they have to be
 * block-aligned or we will return an error so the upper layer can
 * fallback to the generic mechanism that will just copy the data.
 * Using copy_file_range(2) will call OS-independent zfs_clone_range() function.
 * This function was implemented based on zfs_write(), but instead of writing
 * the given data we first read block pointers using the new dmu_read_l0_bps()
 * function from the source file. Once we have BPs from the source file we call
 * the dmu_brt_clone() function on the destination file. This function
 * allocates BPs for us. We iterate over all source BPs. If the given BP is
 * a hole or an embedded block, we just copy BP as-is. If it points to a real
 * data we place this BP on a BRT pending list using the brt_pending_add()
 * function.
 *
 * We use this pending list to keep track of all BPs that got new references
 * within this transaction group.
 *
 * Some special cases to consider and how we address them:
 * - The block we want to clone may have been created within the same
 *   transaction group that we are trying to clone. Such block has no BP
 *   allocated yet, so cannot be immediately cloned. We return EAGAIN.
 * - The block we want to clone may have been modified within the same
 *   transaction group. We return EAGAIN.
 * - A block may be cloned multiple times during one transaction group (that's
 *   why pending list is actually a tree and not an append-only list - this
 *   way we can figure out faster if this block is cloned for the first time
 *   in this txg or consecutive time).
 * - A block may be cloned and freed within the same transaction group
 *   (see dbuf_undirty()).
 * - A block may be cloned and within the same transaction group the clone
 *   can be cloned again (see dmu_read_l0_bps()).
 * - A file might have been deleted, but the caller still has a file descriptor
 *   open to this file and clones it.
 *
 * When we free a block we have an additional step in the ZIO pipeline where we
 * call the zio_brt_free() function. We then call the brt_entry_decref()
 * that loads the corresponding BRT entry (if one exists) and decreases
 * reference counter. If this is not the last reference we will stop ZIO
 * pipeline here. If this is the last reference or the block is not in the
 * BRT, we continue the pipeline and free the block as usual.
 *
 * At the beginning of spa_sync() where there can be no more block cloning,
 * but before issuing frees we call brt_pending_apply(). This function applies
 * all the new clones to the BRT table - we load BRT entries and update
 * reference counters. To sync new BRT entries to disk, we use brt_sync()
 * function. This function will sync all dirty per-top-level-vdev BRTs,
 * the entry counters arrays, etc.
 *
 * Block Cloning and ZIL.
 *
 * Every clone operation is divided into chunks (similar to write) and each
 * chunk is cloned in a separate transaction. The chunk size is determined by
 * how many BPs we can fit into a single ZIL entry.
 * Replaying clone operation is different from the regular clone operation,
 * as when we log clone operations we cannot use the source object - it may
 * reside on a different dataset, so we log BPs we want to clone.
 * The ZIL is replayed when we mount the given dataset, not when the pool is
 * imported. Taking this into account it is possible that the pool is imported
 * without mounting datasets and the source dataset is destroyed before the
 * destination dataset is mounted and its ZIL replayed.
 * To address this situation we leverage zil_claim() mechanism where ZFS will
 * parse all the ZILs on pool import. When we come across TX_CLONE_RANGE
 * entries, we will bump reference counters for their BPs in the BRT.  Then
 * on mount and ZIL replay we bump the reference counters once more, while the
 * first references are dropped during ZIL destroy by zil_free_clone_range().
 * It is possible that after zil_claim() we never mount the destination, so
 * we never replay its ZIL and just destroy it.  In this case the only taken
 * references will be dropped by zil_free_clone_range(), since the cloning is
 * not going to ever take place.
 */

static kmem_cache_t *brt_entry_cache;
static kmem_cache_t *brt_pending_entry_cache;

/*
 * Enable/disable prefetching of BRT entries that we are going to modify.
 */
static int brt_zap_prefetch = 1;

#ifdef ZFS_DEBUG
#define	BRT_DEBUG(...)	do {						\
	if ((zfs_flags & ZFS_DEBUG_BRT) != 0) {				\
		__dprintf(B_TRUE, __FILE__, __func__, __LINE__, __VA_ARGS__); \
	}								\
} while (0)
#else
#define	BRT_DEBUG(...)	do { } while (0)
#endif

static int brt_zap_default_bs = 12;
static int brt_zap_default_ibs = 12;

static kstat_t	*brt_ksp;

typedef struct brt_stats {
	kstat_named_t brt_addref_entry_in_memory;
	kstat_named_t brt_addref_entry_not_on_disk;
	kstat_named_t brt_addref_entry_on_disk;
	kstat_named_t brt_addref_entry_read_lost_race;
	kstat_named_t brt_decref_entry_in_memory;
	kstat_named_t brt_decref_entry_loaded_from_disk;
	kstat_named_t brt_decref_entry_not_in_memory;
	kstat_named_t brt_decref_entry_not_on_disk;
	kstat_named_t brt_decref_entry_read_lost_race;
	kstat_named_t brt_decref_entry_still_referenced;
	kstat_named_t brt_decref_free_data_later;
	kstat_named_t brt_decref_free_data_now;
	kstat_named_t brt_decref_no_entry;
} brt_stats_t;

static brt_stats_t brt_stats = {
	{ "addref_entry_in_memory",		KSTAT_DATA_UINT64 },
	{ "addref_entry_not_on_disk",		KSTAT_DATA_UINT64 },
	{ "addref_entry_on_disk",		KSTAT_DATA_UINT64 },
	{ "addref_entry_read_lost_race",	KSTAT_DATA_UINT64 },
	{ "decref_entry_in_memory",		KSTAT_DATA_UINT64 },
	{ "decref_entry_loaded_from_disk",	KSTAT_DATA_UINT64 },
	{ "decref_entry_not_in_memory",		KSTAT_DATA_UINT64 },
	{ "decref_entry_not_on_disk",		KSTAT_DATA_UINT64 },
	{ "decref_entry_read_lost_race",	KSTAT_DATA_UINT64 },
	{ "decref_entry_still_referenced",	KSTAT_DATA_UINT64 },
	{ "decref_free_data_later",		KSTAT_DATA_UINT64 },
	{ "decref_free_data_now",		KSTAT_DATA_UINT64 },
	{ "decref_no_entry",			KSTAT_DATA_UINT64 }
};

struct {
	wmsum_t brt_addref_entry_in_memory;
	wmsum_t brt_addref_entry_not_on_disk;
	wmsum_t brt_addref_entry_on_disk;
	wmsum_t brt_addref_entry_read_lost_race;
	wmsum_t brt_decref_entry_in_memory;
	wmsum_t brt_decref_entry_loaded_from_disk;
	wmsum_t brt_decref_entry_not_in_memory;
	wmsum_t brt_decref_entry_not_on_disk;
	wmsum_t brt_decref_entry_read_lost_race;
	wmsum_t brt_decref_entry_still_referenced;
	wmsum_t brt_decref_free_data_later;
	wmsum_t brt_decref_free_data_now;
	wmsum_t brt_decref_no_entry;
} brt_sums;

#define	BRTSTAT_BUMP(stat)	wmsum_add(&brt_sums.stat, 1)

static int brt_entry_compare(const void *x1, const void *x2);
static int brt_pending_entry_compare(const void *x1, const void *x2);

static void
brt_rlock(brt_t *brt)
{
	rw_enter(&brt->brt_lock, RW_READER);
}

static void
brt_wlock(brt_t *brt)
{
	rw_enter(&brt->brt_lock, RW_WRITER);
}

static void
brt_unlock(brt_t *brt)
{
	rw_exit(&brt->brt_lock);
}

static uint16_t
brt_vdev_entcount_get(const brt_vdev_t *brtvd, uint64_t idx)
{

	ASSERT3U(idx, <, brtvd->bv_size);

	if (unlikely(brtvd->bv_need_byteswap)) {
		return (BSWAP_16(brtvd->bv_entcount[idx]));
	} else {
		return (brtvd->bv_entcount[idx]);
	}
}

static void
brt_vdev_entcount_set(brt_vdev_t *brtvd, uint64_t idx, uint16_t entcnt)
{

	ASSERT3U(idx, <, brtvd->bv_size);

	if (unlikely(brtvd->bv_need_byteswap)) {
		brtvd->bv_entcount[idx] = BSWAP_16(entcnt);
	} else {
		brtvd->bv_entcount[idx] = entcnt;
	}
}

static void
brt_vdev_entcount_inc(brt_vdev_t *brtvd, uint64_t idx)
{
	uint16_t entcnt;

	ASSERT3U(idx, <, brtvd->bv_size);

	entcnt = brt_vdev_entcount_get(brtvd, idx);
	ASSERT(entcnt < UINT16_MAX);

	brt_vdev_entcount_set(brtvd, idx, entcnt + 1);
}

static void
brt_vdev_entcount_dec(brt_vdev_t *brtvd, uint64_t idx)
{
	uint16_t entcnt;

	ASSERT3U(idx, <, brtvd->bv_size);

	entcnt = brt_vdev_entcount_get(brtvd, idx);
	ASSERT(entcnt > 0);

	brt_vdev_entcount_set(brtvd, idx, entcnt - 1);
}

#ifdef ZFS_DEBUG
static void
brt_vdev_dump(brt_vdev_t *brtvd)
{
	uint64_t idx;

	zfs_dbgmsg("  BRT vdevid=%llu meta_dirty=%d entcount_dirty=%d "
	    "size=%llu totalcount=%llu nblocks=%llu bitmapsize=%zu\n",
	    (u_longlong_t)brtvd->bv_vdevid,
	    brtvd->bv_meta_dirty, brtvd->bv_entcount_dirty,
	    (u_longlong_t)brtvd->bv_size,
	    (u_longlong_t)brtvd->bv_totalcount,
	    (u_longlong_t)brtvd->bv_nblocks,
	    (size_t)BT_SIZEOFMAP(brtvd->bv_nblocks));
	if (brtvd->bv_totalcount > 0) {
		zfs_dbgmsg("    entcounts:");
		for (idx = 0; idx < brtvd->bv_size; idx++) {
			uint16_t entcnt = brt_vdev_entcount_get(brtvd, idx);
			if (entcnt > 0) {
				zfs_dbgmsg("      [%04llu] %hu",
				    (u_longlong_t)idx, entcnt);
			}
		}
	}
	if (brtvd->bv_entcount_dirty) {
		char *bitmap;

		bitmap = kmem_alloc(brtvd->bv_nblocks + 1, KM_SLEEP);
		for (idx = 0; idx < brtvd->bv_nblocks; idx++) {
			bitmap[idx] =
			    BT_TEST(brtvd->bv_bitmap, idx) ? 'x' : '.';
		}
		bitmap[idx] = '\0';
		zfs_dbgmsg("    dirty: %s", bitmap);
		kmem_free(bitmap, brtvd->bv_nblocks + 1);
	}
}
#endif

static brt_vdev_t *
brt_vdev(brt_t *brt, uint64_t vdevid)
{
	brt_vdev_t *brtvd;

	ASSERT(RW_LOCK_HELD(&brt->brt_lock));

	if (vdevid < brt->brt_nvdevs) {
		brtvd = &brt->brt_vdevs[vdevid];
	} else {
		brtvd = NULL;
	}

	return (brtvd);
}

static void
brt_vdev_create(brt_t *brt, brt_vdev_t *brtvd, dmu_tx_t *tx)
{
	char name[64];

	ASSERT(RW_WRITE_HELD(&brt->brt_lock));
	ASSERT0(brtvd->bv_mos_brtvdev);
	ASSERT0(brtvd->bv_mos_entries);
	ASSERT(brtvd->bv_entcount != NULL);
	ASSERT(brtvd->bv_size > 0);
	ASSERT(brtvd->bv_bitmap != NULL);
	ASSERT(brtvd->bv_nblocks > 0);

	brtvd->bv_mos_entries = zap_create_flags(brt->brt_mos, 0,
	    ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY, DMU_OTN_ZAP_METADATA,
	    brt_zap_default_bs, brt_zap_default_ibs, DMU_OT_NONE, 0, tx);
	VERIFY(brtvd->bv_mos_entries != 0);
	BRT_DEBUG("MOS entries created, object=%llu",
	    (u_longlong_t)brtvd->bv_mos_entries);

	/*
	 * We allocate DMU buffer to store the bv_entcount[] array.
	 * We will keep array size (bv_size) and cummulative count for all
	 * bv_entcount[]s (bv_totalcount) in the bonus buffer.
	 */
	brtvd->bv_mos_brtvdev = dmu_object_alloc(brt->brt_mos,
	    DMU_OTN_UINT64_METADATA, BRT_BLOCKSIZE,
	    DMU_OTN_UINT64_METADATA, sizeof (brt_vdev_phys_t), tx);
	VERIFY(brtvd->bv_mos_brtvdev != 0);
	BRT_DEBUG("MOS BRT VDEV created, object=%llu",
	    (u_longlong_t)brtvd->bv_mos_brtvdev);

	snprintf(name, sizeof (name), "%s%llu", BRT_OBJECT_VDEV_PREFIX,
	    (u_longlong_t)brtvd->bv_vdevid);
	VERIFY0(zap_add(brt->brt_mos, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, &brtvd->bv_mos_brtvdev, tx));
	BRT_DEBUG("Pool directory object created, object=%s", name);

	spa_feature_incr(brt->brt_spa, SPA_FEATURE_BLOCK_CLONING, tx);
}

static void
brt_vdev_realloc(brt_t *brt, brt_vdev_t *brtvd)
{
	vdev_t *vd;
	uint16_t *entcount;
	ulong_t *bitmap;
	uint64_t nblocks, size;

	ASSERT(RW_WRITE_HELD(&brt->brt_lock));

	spa_config_enter(brt->brt_spa, SCL_VDEV, FTAG, RW_READER);
	vd = vdev_lookup_top(brt->brt_spa, brtvd->bv_vdevid);
	size = (vdev_get_min_asize(vd) - 1) / brt->brt_rangesize + 1;
	spa_config_exit(brt->brt_spa, SCL_VDEV, FTAG);

	entcount = vmem_zalloc(sizeof (entcount[0]) * size, KM_SLEEP);
	nblocks = BRT_RANGESIZE_TO_NBLOCKS(size);
	bitmap = kmem_zalloc(BT_SIZEOFMAP(nblocks), KM_SLEEP);

	if (!brtvd->bv_initiated) {
		ASSERT0(brtvd->bv_size);
		ASSERT(brtvd->bv_entcount == NULL);
		ASSERT(brtvd->bv_bitmap == NULL);
		ASSERT0(brtvd->bv_nblocks);

		avl_create(&brtvd->bv_tree, brt_entry_compare,
		    sizeof (brt_entry_t), offsetof(brt_entry_t, bre_node));
	} else {
		ASSERT(brtvd->bv_size > 0);
		ASSERT(brtvd->bv_entcount != NULL);
		ASSERT(brtvd->bv_bitmap != NULL);
		ASSERT(brtvd->bv_nblocks > 0);
		/*
		 * TODO: Allow vdev shrinking. We only need to implement
		 * shrinking the on-disk BRT VDEV object.
		 * dmu_free_range(brt->brt_mos, brtvd->bv_mos_brtvdev, offset,
		 *     size, tx);
		 */
		ASSERT3U(brtvd->bv_size, <=, size);

		memcpy(entcount, brtvd->bv_entcount,
		    sizeof (entcount[0]) * MIN(size, brtvd->bv_size));
		memcpy(bitmap, brtvd->bv_bitmap, MIN(BT_SIZEOFMAP(nblocks),
		    BT_SIZEOFMAP(brtvd->bv_nblocks)));
		vmem_free(brtvd->bv_entcount,
		    sizeof (entcount[0]) * brtvd->bv_size);
		kmem_free(brtvd->bv_bitmap, BT_SIZEOFMAP(brtvd->bv_nblocks));
	}

	brtvd->bv_size = size;
	brtvd->bv_entcount = entcount;
	brtvd->bv_bitmap = bitmap;
	brtvd->bv_nblocks = nblocks;
	if (!brtvd->bv_initiated) {
		brtvd->bv_need_byteswap = FALSE;
		brtvd->bv_initiated = TRUE;
		BRT_DEBUG("BRT VDEV %llu initiated.",
		    (u_longlong_t)brtvd->bv_vdevid);
	}
}

static void
brt_vdev_load(brt_t *brt, brt_vdev_t *brtvd)
{
	char name[64];
	dmu_buf_t *db;
	brt_vdev_phys_t *bvphys;
	int error;

	snprintf(name, sizeof (name), "%s%llu", BRT_OBJECT_VDEV_PREFIX,
	    (u_longlong_t)brtvd->bv_vdevid);
	error = zap_lookup(brt->brt_mos, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, &brtvd->bv_mos_brtvdev);
	if (error != 0)
		return;
	ASSERT(brtvd->bv_mos_brtvdev != 0);

	error = dmu_bonus_hold(brt->brt_mos, brtvd->bv_mos_brtvdev, FTAG, &db);
	ASSERT0(error);
	if (error != 0)
		return;

	bvphys = db->db_data;
	if (brt->brt_rangesize == 0) {
		brt->brt_rangesize = bvphys->bvp_rangesize;
	} else {
		ASSERT3U(brt->brt_rangesize, ==, bvphys->bvp_rangesize);
	}

	ASSERT(!brtvd->bv_initiated);
	brt_vdev_realloc(brt, brtvd);

	/* TODO: We don't support VDEV shrinking. */
	ASSERT3U(bvphys->bvp_size, <=, brtvd->bv_size);

	/*
	 * If VDEV grew, we will leave new bv_entcount[] entries zeroed out.
	 */
	error = dmu_read(brt->brt_mos, brtvd->bv_mos_brtvdev, 0,
	    MIN(brtvd->bv_size, bvphys->bvp_size) * sizeof (uint16_t),
	    brtvd->bv_entcount, DMU_READ_NO_PREFETCH);
	ASSERT0(error);

	brtvd->bv_mos_entries = bvphys->bvp_mos_entries;
	ASSERT(brtvd->bv_mos_entries != 0);
	brtvd->bv_need_byteswap =
	    (bvphys->bvp_byteorder != BRT_NATIVE_BYTEORDER);
	brtvd->bv_totalcount = bvphys->bvp_totalcount;
	brtvd->bv_usedspace = bvphys->bvp_usedspace;
	brtvd->bv_savedspace = bvphys->bvp_savedspace;
	brt->brt_usedspace += brtvd->bv_usedspace;
	brt->brt_savedspace += brtvd->bv_savedspace;

	dmu_buf_rele(db, FTAG);

	BRT_DEBUG("MOS BRT VDEV %s loaded: mos_brtvdev=%llu, mos_entries=%llu",
	    name, (u_longlong_t)brtvd->bv_mos_brtvdev,
	    (u_longlong_t)brtvd->bv_mos_entries);
}

static void
brt_vdev_dealloc(brt_t *brt, brt_vdev_t *brtvd)
{

	ASSERT(RW_WRITE_HELD(&brt->brt_lock));
	ASSERT(brtvd->bv_initiated);

	vmem_free(brtvd->bv_entcount, sizeof (uint16_t) * brtvd->bv_size);
	brtvd->bv_entcount = NULL;
	kmem_free(brtvd->bv_bitmap, BT_SIZEOFMAP(brtvd->bv_nblocks));
	brtvd->bv_bitmap = NULL;
	ASSERT0(avl_numnodes(&brtvd->bv_tree));
	avl_destroy(&brtvd->bv_tree);

	brtvd->bv_size = 0;
	brtvd->bv_nblocks = 0;

	brtvd->bv_initiated = FALSE;
	BRT_DEBUG("BRT VDEV %llu deallocated.", (u_longlong_t)brtvd->bv_vdevid);
}

static void
brt_vdev_destroy(brt_t *brt, brt_vdev_t *brtvd, dmu_tx_t *tx)
{
	char name[64];
	uint64_t count;
	dmu_buf_t *db;
	brt_vdev_phys_t *bvphys;

	ASSERT(RW_WRITE_HELD(&brt->brt_lock));
	ASSERT(brtvd->bv_mos_brtvdev != 0);
	ASSERT(brtvd->bv_mos_entries != 0);

	VERIFY0(zap_count(brt->brt_mos, brtvd->bv_mos_entries, &count));
	VERIFY0(count);
	VERIFY0(zap_destroy(brt->brt_mos, brtvd->bv_mos_entries, tx));
	BRT_DEBUG("MOS entries destroyed, object=%llu",
	    (u_longlong_t)brtvd->bv_mos_entries);
	brtvd->bv_mos_entries = 0;

	VERIFY0(dmu_bonus_hold(brt->brt_mos, brtvd->bv_mos_brtvdev, FTAG, &db));
	bvphys = db->db_data;
	ASSERT0(bvphys->bvp_totalcount);
	ASSERT0(bvphys->bvp_usedspace);
	ASSERT0(bvphys->bvp_savedspace);
	dmu_buf_rele(db, FTAG);

	VERIFY0(dmu_object_free(brt->brt_mos, brtvd->bv_mos_brtvdev, tx));
	BRT_DEBUG("MOS BRT VDEV destroyed, object=%llu",
	    (u_longlong_t)brtvd->bv_mos_brtvdev);
	brtvd->bv_mos_brtvdev = 0;

	snprintf(name, sizeof (name), "%s%llu", BRT_OBJECT_VDEV_PREFIX,
	    (u_longlong_t)brtvd->bv_vdevid);
	VERIFY0(zap_remove(brt->brt_mos, DMU_POOL_DIRECTORY_OBJECT, name, tx));
	BRT_DEBUG("Pool directory object removed, object=%s", name);

	brt_vdev_dealloc(brt, brtvd);

	spa_feature_decr(brt->brt_spa, SPA_FEATURE_BLOCK_CLONING, tx);
}

static void
brt_vdevs_expand(brt_t *brt, uint64_t nvdevs)
{
	brt_vdev_t *brtvd, *vdevs;
	uint64_t vdevid;

	ASSERT(RW_WRITE_HELD(&brt->brt_lock));
	ASSERT3U(nvdevs, >, brt->brt_nvdevs);

	vdevs = kmem_zalloc(sizeof (vdevs[0]) * nvdevs, KM_SLEEP);
	if (brt->brt_nvdevs > 0) {
		ASSERT(brt->brt_vdevs != NULL);

		memcpy(vdevs, brt->brt_vdevs,
		    sizeof (brt_vdev_t) * brt->brt_nvdevs);
		kmem_free(brt->brt_vdevs,
		    sizeof (brt_vdev_t) * brt->brt_nvdevs);
	}
	for (vdevid = brt->brt_nvdevs; vdevid < nvdevs; vdevid++) {
		brtvd = &vdevs[vdevid];

		brtvd->bv_vdevid = vdevid;
		brtvd->bv_initiated = FALSE;
	}

	BRT_DEBUG("BRT VDEVs expanded from %llu to %llu.",
	    (u_longlong_t)brt->brt_nvdevs, (u_longlong_t)nvdevs);

	brt->brt_vdevs = vdevs;
	brt->brt_nvdevs = nvdevs;
}

static boolean_t
brt_vdev_lookup(brt_t *brt, brt_vdev_t *brtvd, const brt_entry_t *bre)
{
	uint64_t idx;

	ASSERT(RW_LOCK_HELD(&brt->brt_lock));

	idx = bre->bre_offset / brt->brt_rangesize;
	if (brtvd->bv_entcount != NULL && idx < brtvd->bv_size) {
		/* VDEV wasn't expanded. */
		return (brt_vdev_entcount_get(brtvd, idx) > 0);
	}

	return (FALSE);
}

static void
brt_vdev_addref(brt_t *brt, brt_vdev_t *brtvd, const brt_entry_t *bre,
    uint64_t dsize)
{
	uint64_t idx;

	ASSERT(RW_LOCK_HELD(&brt->brt_lock));
	ASSERT(brtvd != NULL);
	ASSERT(brtvd->bv_entcount != NULL);

	brt->brt_savedspace += dsize;
	brtvd->bv_savedspace += dsize;
	brtvd->bv_meta_dirty = TRUE;

	if (bre->bre_refcount > 1) {
		return;
	}

	brt->brt_usedspace += dsize;
	brtvd->bv_usedspace += dsize;

	idx = bre->bre_offset / brt->brt_rangesize;
	if (idx >= brtvd->bv_size) {
		/* VDEV has been expanded. */
		brt_vdev_realloc(brt, brtvd);
	}

	ASSERT3U(idx, <, brtvd->bv_size);

	brtvd->bv_totalcount++;
	brt_vdev_entcount_inc(brtvd, idx);
	brtvd->bv_entcount_dirty = TRUE;
	idx = idx / BRT_BLOCKSIZE / 8;
	BT_SET(brtvd->bv_bitmap, idx);

#ifdef ZFS_DEBUG
	if (zfs_flags & ZFS_DEBUG_BRT)
		brt_vdev_dump(brtvd);
#endif
}

static void
brt_vdev_decref(brt_t *brt, brt_vdev_t *brtvd, const brt_entry_t *bre,
    uint64_t dsize)
{
	uint64_t idx;

	ASSERT(RW_WRITE_HELD(&brt->brt_lock));
	ASSERT(brtvd != NULL);
	ASSERT(brtvd->bv_entcount != NULL);

	brt->brt_savedspace -= dsize;
	brtvd->bv_savedspace -= dsize;
	brtvd->bv_meta_dirty = TRUE;

	if (bre->bre_refcount > 0) {
		return;
	}

	brt->brt_usedspace -= dsize;
	brtvd->bv_usedspace -= dsize;

	idx = bre->bre_offset / brt->brt_rangesize;
	ASSERT3U(idx, <, brtvd->bv_size);

	ASSERT(brtvd->bv_totalcount > 0);
	brtvd->bv_totalcount--;
	brt_vdev_entcount_dec(brtvd, idx);
	brtvd->bv_entcount_dirty = TRUE;
	idx = idx / BRT_BLOCKSIZE / 8;
	BT_SET(brtvd->bv_bitmap, idx);

#ifdef ZFS_DEBUG
	if (zfs_flags & ZFS_DEBUG_BRT)
		brt_vdev_dump(brtvd);
#endif
}

static void
brt_vdev_sync(brt_t *brt, brt_vdev_t *brtvd, dmu_tx_t *tx)
{
	dmu_buf_t *db;
	brt_vdev_phys_t *bvphys;

	ASSERT(brtvd->bv_meta_dirty);
	ASSERT(brtvd->bv_mos_brtvdev != 0);
	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(dmu_bonus_hold(brt->brt_mos, brtvd->bv_mos_brtvdev, FTAG, &db));

	if (brtvd->bv_entcount_dirty) {
		/*
		 * TODO: Walk brtvd->bv_bitmap and write only the dirty blocks.
		 */
		dmu_write(brt->brt_mos, brtvd->bv_mos_brtvdev, 0,
		    brtvd->bv_size * sizeof (brtvd->bv_entcount[0]),
		    brtvd->bv_entcount, tx);
		memset(brtvd->bv_bitmap, 0, BT_SIZEOFMAP(brtvd->bv_nblocks));
		brtvd->bv_entcount_dirty = FALSE;
	}

	dmu_buf_will_dirty(db, tx);
	bvphys = db->db_data;
	bvphys->bvp_mos_entries = brtvd->bv_mos_entries;
	bvphys->bvp_size = brtvd->bv_size;
	if (brtvd->bv_need_byteswap) {
		bvphys->bvp_byteorder = BRT_NON_NATIVE_BYTEORDER;
	} else {
		bvphys->bvp_byteorder = BRT_NATIVE_BYTEORDER;
	}
	bvphys->bvp_totalcount = brtvd->bv_totalcount;
	bvphys->bvp_rangesize = brt->brt_rangesize;
	bvphys->bvp_usedspace = brtvd->bv_usedspace;
	bvphys->bvp_savedspace = brtvd->bv_savedspace;
	dmu_buf_rele(db, FTAG);

	brtvd->bv_meta_dirty = FALSE;
}

static void
brt_vdevs_alloc(brt_t *brt, boolean_t load)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid;

	brt_wlock(brt);

	brt_vdevs_expand(brt, brt->brt_spa->spa_root_vdev->vdev_children);

	if (load) {
		for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
			brtvd = &brt->brt_vdevs[vdevid];
			ASSERT(brtvd->bv_entcount == NULL);

			brt_vdev_load(brt, brtvd);
		}
	}

	if (brt->brt_rangesize == 0) {
		brt->brt_rangesize = BRT_RANGESIZE;
	}

	brt_unlock(brt);
}

static void
brt_vdevs_free(brt_t *brt)
{
	brt_vdev_t *brtvd;
	uint64_t vdevid;

	brt_wlock(brt);

	for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
		brtvd = &brt->brt_vdevs[vdevid];
		if (brtvd->bv_initiated)
			brt_vdev_dealloc(brt, brtvd);
	}
	kmem_free(brt->brt_vdevs, sizeof (brt_vdev_t) * brt->brt_nvdevs);

	brt_unlock(brt);
}

static void
brt_entry_fill(const blkptr_t *bp, brt_entry_t *bre, uint64_t *vdevidp)
{

	bre->bre_offset = DVA_GET_OFFSET(&bp->blk_dva[0]);
	bre->bre_refcount = 0;

	*vdevidp = DVA_GET_VDEV(&bp->blk_dva[0]);
}

static int
brt_entry_compare(const void *x1, const void *x2)
{
	const brt_entry_t *bre1 = x1;
	const brt_entry_t *bre2 = x2;

	return (TREE_CMP(bre1->bre_offset, bre2->bre_offset));
}

static int
brt_entry_lookup(brt_t *brt, brt_vdev_t *brtvd, brt_entry_t *bre)
{
	uint64_t mos_entries;
	int error;

	ASSERT(RW_LOCK_HELD(&brt->brt_lock));

	if (!brt_vdev_lookup(brt, brtvd, bre))
		return (SET_ERROR(ENOENT));

	/*
	 * Remember mos_entries object number. After we reacquire the BRT lock,
	 * the brtvd pointer may be invalid.
	 */
	mos_entries = brtvd->bv_mos_entries;
	if (mos_entries == 0)
		return (SET_ERROR(ENOENT));

	brt_unlock(brt);

	error = zap_lookup_uint64(brt->brt_mos, mos_entries, &bre->bre_offset,
	    BRT_KEY_WORDS, 1, sizeof (bre->bre_refcount), &bre->bre_refcount);

	brt_wlock(brt);

	return (error);
}

static void
brt_entry_prefetch(brt_t *brt, uint64_t vdevid, brt_entry_t *bre)
{
	brt_vdev_t *brtvd;
	uint64_t mos_entries = 0;

	brt_rlock(brt);
	brtvd = brt_vdev(brt, vdevid);
	if (brtvd != NULL)
		mos_entries = brtvd->bv_mos_entries;
	brt_unlock(brt);

	if (mos_entries == 0)
		return;

	(void) zap_prefetch_uint64(brt->brt_mos, mos_entries,
	    (uint64_t *)&bre->bre_offset, BRT_KEY_WORDS);
}

/*
 * Return TRUE if we _can_ have BRT entry for this bp. It might be false
 * positive, but gives us quick answer if we should look into BRT, which
 * may require reads and thus will be more expensive.
 */
boolean_t
brt_maybe_exists(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt = spa->spa_brt;
	brt_vdev_t *brtvd;
	brt_entry_t bre_search;
	boolean_t mayexists = FALSE;
	uint64_t vdevid;

	brt_entry_fill(bp, &bre_search, &vdevid);

	brt_rlock(brt);

	brtvd = brt_vdev(brt, vdevid);
	if (brtvd != NULL && brtvd->bv_initiated) {
		if (!avl_is_empty(&brtvd->bv_tree) ||
		    brt_vdev_lookup(brt, brtvd, &bre_search)) {
			mayexists = TRUE;
		}
	}

	brt_unlock(brt);

	return (mayexists);
}

uint64_t
brt_get_dspace(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	if (brt == NULL)
		return (0);

	return (brt->brt_savedspace);
}

uint64_t
brt_get_used(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	if (brt == NULL)
		return (0);

	return (brt->brt_usedspace);
}

uint64_t
brt_get_saved(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	if (brt == NULL)
		return (0);

	return (brt->brt_savedspace);
}

uint64_t
brt_get_ratio(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	if (brt->brt_usedspace == 0)
		return (100);

	return ((brt->brt_usedspace + brt->brt_savedspace) * 100 /
	    brt->brt_usedspace);
}

static int
brt_kstats_update(kstat_t *ksp, int rw)
{
	brt_stats_t *bs = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	bs->brt_addref_entry_in_memory.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_in_memory);
	bs->brt_addref_entry_not_on_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_not_on_disk);
	bs->brt_addref_entry_on_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_on_disk);
	bs->brt_addref_entry_read_lost_race.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_read_lost_race);
	bs->brt_decref_entry_in_memory.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_in_memory);
	bs->brt_decref_entry_loaded_from_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_loaded_from_disk);
	bs->brt_decref_entry_not_in_memory.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_not_in_memory);
	bs->brt_decref_entry_not_on_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_not_on_disk);
	bs->brt_decref_entry_read_lost_race.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_read_lost_race);
	bs->brt_decref_entry_still_referenced.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_still_referenced);
	bs->brt_decref_free_data_later.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_free_data_later);
	bs->brt_decref_free_data_now.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_free_data_now);
	bs->brt_decref_no_entry.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_no_entry);

	return (0);
}

static void
brt_stat_init(void)
{

	wmsum_init(&brt_sums.brt_addref_entry_in_memory, 0);
	wmsum_init(&brt_sums.brt_addref_entry_not_on_disk, 0);
	wmsum_init(&brt_sums.brt_addref_entry_on_disk, 0);
	wmsum_init(&brt_sums.brt_addref_entry_read_lost_race, 0);
	wmsum_init(&brt_sums.brt_decref_entry_in_memory, 0);
	wmsum_init(&brt_sums.brt_decref_entry_loaded_from_disk, 0);
	wmsum_init(&brt_sums.brt_decref_entry_not_in_memory, 0);
	wmsum_init(&brt_sums.brt_decref_entry_not_on_disk, 0);
	wmsum_init(&brt_sums.brt_decref_entry_read_lost_race, 0);
	wmsum_init(&brt_sums.brt_decref_entry_still_referenced, 0);
	wmsum_init(&brt_sums.brt_decref_free_data_later, 0);
	wmsum_init(&brt_sums.brt_decref_free_data_now, 0);
	wmsum_init(&brt_sums.brt_decref_no_entry, 0);

	brt_ksp = kstat_create("zfs", 0, "brtstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (brt_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (brt_ksp != NULL) {
		brt_ksp->ks_data = &brt_stats;
		brt_ksp->ks_update = brt_kstats_update;
		kstat_install(brt_ksp);
	}
}

static void
brt_stat_fini(void)
{
	if (brt_ksp != NULL) {
		kstat_delete(brt_ksp);
		brt_ksp = NULL;
	}

	wmsum_fini(&brt_sums.brt_addref_entry_in_memory);
	wmsum_fini(&brt_sums.brt_addref_entry_not_on_disk);
	wmsum_fini(&brt_sums.brt_addref_entry_on_disk);
	wmsum_fini(&brt_sums.brt_addref_entry_read_lost_race);
	wmsum_fini(&brt_sums.brt_decref_entry_in_memory);
	wmsum_fini(&brt_sums.brt_decref_entry_loaded_from_disk);
	wmsum_fini(&brt_sums.brt_decref_entry_not_in_memory);
	wmsum_fini(&brt_sums.brt_decref_entry_not_on_disk);
	wmsum_fini(&brt_sums.brt_decref_entry_read_lost_race);
	wmsum_fini(&brt_sums.brt_decref_entry_still_referenced);
	wmsum_fini(&brt_sums.brt_decref_free_data_later);
	wmsum_fini(&brt_sums.brt_decref_free_data_now);
	wmsum_fini(&brt_sums.brt_decref_no_entry);
}

void
brt_init(void)
{
	brt_entry_cache = kmem_cache_create("brt_entry_cache",
	    sizeof (brt_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	brt_pending_entry_cache = kmem_cache_create("brt_pending_entry_cache",
	    sizeof (brt_pending_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	brt_stat_init();
}

void
brt_fini(void)
{
	brt_stat_fini();

	kmem_cache_destroy(brt_entry_cache);
	kmem_cache_destroy(brt_pending_entry_cache);
}

static brt_entry_t *
brt_entry_alloc(const brt_entry_t *bre_init)
{
	brt_entry_t *bre;

	bre = kmem_cache_alloc(brt_entry_cache, KM_SLEEP);
	bre->bre_offset = bre_init->bre_offset;
	bre->bre_refcount = bre_init->bre_refcount;

	return (bre);
}

static void
brt_entry_free(brt_entry_t *bre)
{

	kmem_cache_free(brt_entry_cache, bre);
}

static void
brt_entry_addref(brt_t *brt, const blkptr_t *bp)
{
	brt_vdev_t *brtvd;
	brt_entry_t *bre, *racebre;
	brt_entry_t bre_search;
	avl_index_t where;
	uint64_t vdevid;
	int error;

	ASSERT(!RW_WRITE_HELD(&brt->brt_lock));

	brt_entry_fill(bp, &bre_search, &vdevid);

	brt_wlock(brt);

	brtvd = brt_vdev(brt, vdevid);
	if (brtvd == NULL) {
		ASSERT3U(vdevid, >=, brt->brt_nvdevs);

		/* New VDEV was added. */
		brt_vdevs_expand(brt, vdevid + 1);
		brtvd = brt_vdev(brt, vdevid);
	}
	ASSERT(brtvd != NULL);
	if (!brtvd->bv_initiated)
		brt_vdev_realloc(brt, brtvd);

	bre = avl_find(&brtvd->bv_tree, &bre_search, NULL);
	if (bre != NULL) {
		BRTSTAT_BUMP(brt_addref_entry_in_memory);
	} else {
		/*
		 * brt_entry_lookup() may drop the BRT (read) lock and
		 * reacquire it (write).
		 */
		error = brt_entry_lookup(brt, brtvd, &bre_search);
		/* bre_search now contains correct bre_refcount */
		ASSERT(error == 0 || error == ENOENT);
		if (error == 0)
			BRTSTAT_BUMP(brt_addref_entry_on_disk);
		else
			BRTSTAT_BUMP(brt_addref_entry_not_on_disk);
		/*
		 * When the BRT lock was dropped, brt_vdevs[] may have been
		 * expanded and reallocated, we need to update brtvd's pointer.
		 */
		brtvd = brt_vdev(brt, vdevid);
		ASSERT(brtvd != NULL);

		racebre = avl_find(&brtvd->bv_tree, &bre_search, &where);
		if (racebre == NULL) {
			bre = brt_entry_alloc(&bre_search);
			ASSERT(RW_WRITE_HELD(&brt->brt_lock));
			avl_insert(&brtvd->bv_tree, bre, where);
			brt->brt_nentries++;
		} else {
			/*
			 * The entry was added when the BRT lock was dropped in
			 * brt_entry_lookup().
			 */
			BRTSTAT_BUMP(brt_addref_entry_read_lost_race);
			bre = racebre;
		}
	}
	bre->bre_refcount++;
	brt_vdev_addref(brt, brtvd, bre, bp_get_dsize(brt->brt_spa, bp));

	brt_unlock(brt);
}

/* Return TRUE if block should be freed immediately. */
boolean_t
brt_entry_decref(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt = spa->spa_brt;
	brt_vdev_t *brtvd;
	brt_entry_t *bre, *racebre;
	brt_entry_t bre_search;
	avl_index_t where;
	uint64_t vdevid;
	int error;

	brt_entry_fill(bp, &bre_search, &vdevid);

	brt_wlock(brt);

	brtvd = brt_vdev(brt, vdevid);
	ASSERT(brtvd != NULL);

	bre = avl_find(&brtvd->bv_tree, &bre_search, NULL);
	if (bre != NULL) {
		BRTSTAT_BUMP(brt_decref_entry_in_memory);
		goto out;
	} else {
		BRTSTAT_BUMP(brt_decref_entry_not_in_memory);
	}

	/*
	 * brt_entry_lookup() may drop the BRT lock and reacquire it.
	 */
	error = brt_entry_lookup(brt, brtvd, &bre_search);
	/* bre_search now contains correct bre_refcount */
	ASSERT(error == 0 || error == ENOENT);
	/*
	 * When the BRT lock was dropped, brt_vdevs[] may have been expanded
	 * and reallocated, we need to update brtvd's pointer.
	 */
	brtvd = brt_vdev(brt, vdevid);
	ASSERT(brtvd != NULL);

	if (error == ENOENT) {
		BRTSTAT_BUMP(brt_decref_entry_not_on_disk);
		bre = NULL;
		goto out;
	}

	racebre = avl_find(&brtvd->bv_tree, &bre_search, &where);
	if (racebre != NULL) {
		/*
		 * The entry was added when the BRT lock was dropped in
		 * brt_entry_lookup().
		 */
		BRTSTAT_BUMP(brt_decref_entry_read_lost_race);
		bre = racebre;
		goto out;
	}

	BRTSTAT_BUMP(brt_decref_entry_loaded_from_disk);
	bre = brt_entry_alloc(&bre_search);
	ASSERT(RW_WRITE_HELD(&brt->brt_lock));
	avl_insert(&brtvd->bv_tree, bre, where);
	brt->brt_nentries++;

out:
	if (bre == NULL) {
		/*
		 * This is a free of a regular (not cloned) block.
		 */
		brt_unlock(brt);
		BRTSTAT_BUMP(brt_decref_no_entry);
		return (B_TRUE);
	}
	if (bre->bre_refcount == 0) {
		brt_unlock(brt);
		BRTSTAT_BUMP(brt_decref_free_data_now);
		return (B_TRUE);
	}

	ASSERT(bre->bre_refcount > 0);
	bre->bre_refcount--;
	if (bre->bre_refcount == 0)
		BRTSTAT_BUMP(brt_decref_free_data_later);
	else
		BRTSTAT_BUMP(brt_decref_entry_still_referenced);
	brt_vdev_decref(brt, brtvd, bre, bp_get_dsize(brt->brt_spa, bp));

	brt_unlock(brt);

	return (B_FALSE);
}

uint64_t
brt_entry_get_refcount(spa_t *spa, const blkptr_t *bp)
{
	brt_t *brt = spa->spa_brt;
	brt_vdev_t *brtvd;
	brt_entry_t bre_search, *bre;
	uint64_t vdevid, refcnt;
	int error;

	brt_entry_fill(bp, &bre_search, &vdevid);

	brt_rlock(brt);

	brtvd = brt_vdev(brt, vdevid);
	ASSERT(brtvd != NULL);

	bre = avl_find(&brtvd->bv_tree, &bre_search, NULL);
	if (bre == NULL) {
		error = brt_entry_lookup(brt, brtvd, &bre_search);
		ASSERT(error == 0 || error == ENOENT);
		if (error == ENOENT)
			refcnt = 0;
		else
			refcnt = bre_search.bre_refcount;
	} else
		refcnt = bre->bre_refcount;

	brt_unlock(brt);
	return (refcnt);
}

static void
brt_prefetch(brt_t *brt, const blkptr_t *bp)
{
	brt_entry_t bre;
	uint64_t vdevid;

	ASSERT(bp != NULL);

	if (!brt_zap_prefetch)
		return;

	brt_entry_fill(bp, &bre, &vdevid);

	brt_entry_prefetch(brt, vdevid, &bre);
}

static int
brt_pending_entry_compare(const void *x1, const void *x2)
{
	const brt_pending_entry_t *bpe1 = x1, *bpe2 = x2;
	const blkptr_t *bp1 = &bpe1->bpe_bp, *bp2 = &bpe2->bpe_bp;
	int cmp;

	cmp = TREE_CMP(DVA_GET_VDEV(&bp1->blk_dva[0]),
	    DVA_GET_VDEV(&bp2->blk_dva[0]));
	if (cmp == 0) {
		cmp = TREE_CMP(DVA_GET_OFFSET(&bp1->blk_dva[0]),
		    DVA_GET_OFFSET(&bp2->blk_dva[0]));
		if (unlikely(cmp == 0)) {
			cmp = TREE_CMP(BP_GET_BIRTH(bp1), BP_GET_BIRTH(bp2));
		}
	}

	return (cmp);
}

void
brt_pending_add(spa_t *spa, const blkptr_t *bp, dmu_tx_t *tx)
{
	brt_t *brt;
	avl_tree_t *pending_tree;
	kmutex_t *pending_lock;
	brt_pending_entry_t *bpe, *newbpe;
	avl_index_t where;
	uint64_t txg;

	brt = spa->spa_brt;
	txg = dmu_tx_get_txg(tx);
	ASSERT3U(txg, !=, 0);
	pending_tree = &brt->brt_pending_tree[txg & TXG_MASK];
	pending_lock = &brt->brt_pending_lock[txg & TXG_MASK];

	newbpe = kmem_cache_alloc(brt_pending_entry_cache, KM_SLEEP);
	newbpe->bpe_bp = *bp;
	newbpe->bpe_count = 1;

	mutex_enter(pending_lock);

	bpe = avl_find(pending_tree, newbpe, &where);
	if (bpe == NULL) {
		avl_insert(pending_tree, newbpe, where);
		newbpe = NULL;
	} else {
		bpe->bpe_count++;
	}

	mutex_exit(pending_lock);

	if (newbpe != NULL) {
		ASSERT(bpe != NULL);
		ASSERT(bpe != newbpe);
		kmem_cache_free(brt_pending_entry_cache, newbpe);
	} else {
		ASSERT(bpe == NULL);

		/* Prefetch BRT entry for the syncing context. */
		brt_prefetch(brt, bp);
	}
}

void
brt_pending_remove(spa_t *spa, const blkptr_t *bp, dmu_tx_t *tx)
{
	brt_t *brt;
	avl_tree_t *pending_tree;
	kmutex_t *pending_lock;
	brt_pending_entry_t *bpe, bpe_search;
	uint64_t txg;

	brt = spa->spa_brt;
	txg = dmu_tx_get_txg(tx);
	ASSERT3U(txg, !=, 0);
	pending_tree = &brt->brt_pending_tree[txg & TXG_MASK];
	pending_lock = &brt->brt_pending_lock[txg & TXG_MASK];

	bpe_search.bpe_bp = *bp;

	mutex_enter(pending_lock);

	bpe = avl_find(pending_tree, &bpe_search, NULL);
	/* I believe we should always find bpe when this function is called. */
	if (bpe != NULL) {
		ASSERT(bpe->bpe_count > 0);

		bpe->bpe_count--;
		if (bpe->bpe_count == 0) {
			avl_remove(pending_tree, bpe);
			kmem_cache_free(brt_pending_entry_cache, bpe);
		}
	}

	mutex_exit(pending_lock);
}

void
brt_pending_apply(spa_t *spa, uint64_t txg)
{
	brt_t *brt = spa->spa_brt;
	brt_pending_entry_t *bpe;
	avl_tree_t *pending_tree;
	void *c;

	ASSERT3U(txg, !=, 0);

	/*
	 * We are in syncing context, so no other brt_pending_tree accesses
	 * are possible for the TXG. Don't need to acquire brt_pending_lock.
	 */
	pending_tree = &brt->brt_pending_tree[txg & TXG_MASK];

	c = NULL;
	while ((bpe = avl_destroy_nodes(pending_tree, &c)) != NULL) {
		boolean_t added_to_ddt;

		for (int i = 0; i < bpe->bpe_count; i++) {
			/*
			 * If the block has DEDUP bit set, it means that it
			 * already exists in the DEDUP table, so we can just
			 * use that instead of creating new entry in
			 * the BRT table.
			 */
			if (BP_GET_DEDUP(&bpe->bpe_bp)) {
				added_to_ddt = ddt_addref(spa, &bpe->bpe_bp);
			} else {
				added_to_ddt = B_FALSE;
			}
			if (!added_to_ddt)
				brt_entry_addref(brt, &bpe->bpe_bp);
		}

		kmem_cache_free(brt_pending_entry_cache, bpe);
	}
}

static void
brt_sync_entry(dnode_t *dn, brt_entry_t *bre, dmu_tx_t *tx)
{
	if (bre->bre_refcount == 0) {
		int error = zap_remove_uint64_by_dnode(dn, &bre->bre_offset,
		    BRT_KEY_WORDS, tx);
		VERIFY(error == 0 || error == ENOENT);
	} else {
		VERIFY0(zap_update_uint64_by_dnode(dn, &bre->bre_offset,
		    BRT_KEY_WORDS, 1, sizeof (bre->bre_refcount),
		    &bre->bre_refcount, tx));
	}
}

static void
brt_sync_table(brt_t *brt, dmu_tx_t *tx)
{
	brt_vdev_t *brtvd;
	brt_entry_t *bre;
	dnode_t *dn;
	uint64_t vdevid;
	void *c;

	brt_wlock(brt);

	for (vdevid = 0; vdevid < brt->brt_nvdevs; vdevid++) {
		brtvd = &brt->brt_vdevs[vdevid];

		if (!brtvd->bv_initiated)
			continue;

		if (!brtvd->bv_meta_dirty) {
			ASSERT(!brtvd->bv_entcount_dirty);
			ASSERT0(avl_numnodes(&brtvd->bv_tree));
			continue;
		}

		ASSERT(!brtvd->bv_entcount_dirty ||
		    avl_numnodes(&brtvd->bv_tree) != 0);

		if (brtvd->bv_mos_brtvdev == 0)
			brt_vdev_create(brt, brtvd, tx);

		VERIFY0(dnode_hold(brt->brt_mos, brtvd->bv_mos_entries,
		    FTAG, &dn));

		c = NULL;
		while ((bre = avl_destroy_nodes(&brtvd->bv_tree, &c)) != NULL) {
			brt_sync_entry(dn, bre, tx);
			brt_entry_free(bre);
			ASSERT(brt->brt_nentries > 0);
			brt->brt_nentries--;
		}

		dnode_rele(dn, FTAG);

		brt_vdev_sync(brt, brtvd, tx);

		if (brtvd->bv_totalcount == 0)
			brt_vdev_destroy(brt, brtvd, tx);
	}

	ASSERT0(brt->brt_nentries);

	brt_unlock(brt);
}

void
brt_sync(spa_t *spa, uint64_t txg)
{
	dmu_tx_t *tx;
	brt_t *brt;

	ASSERT(spa_syncing_txg(spa) == txg);

	brt = spa->spa_brt;
	brt_rlock(brt);
	if (brt->brt_nentries == 0) {
		/* No changes. */
		brt_unlock(brt);
		return;
	}
	brt_unlock(brt);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	brt_sync_table(brt, tx);

	dmu_tx_commit(tx);
}

static void
brt_table_alloc(brt_t *brt)
{

	for (int i = 0; i < TXG_SIZE; i++) {
		avl_create(&brt->brt_pending_tree[i],
		    brt_pending_entry_compare,
		    sizeof (brt_pending_entry_t),
		    offsetof(brt_pending_entry_t, bpe_node));
		mutex_init(&brt->brt_pending_lock[i], NULL, MUTEX_DEFAULT,
		    NULL);
	}
}

static void
brt_table_free(brt_t *brt)
{

	for (int i = 0; i < TXG_SIZE; i++) {
		ASSERT(avl_is_empty(&brt->brt_pending_tree[i]));

		avl_destroy(&brt->brt_pending_tree[i]);
		mutex_destroy(&brt->brt_pending_lock[i]);
	}
}

static void
brt_alloc(spa_t *spa)
{
	brt_t *brt;

	ASSERT(spa->spa_brt == NULL);

	brt = kmem_zalloc(sizeof (*brt), KM_SLEEP);
	rw_init(&brt->brt_lock, NULL, RW_DEFAULT, NULL);
	brt->brt_spa = spa;
	brt->brt_rangesize = 0;
	brt->brt_nentries = 0;
	brt->brt_vdevs = NULL;
	brt->brt_nvdevs = 0;
	brt_table_alloc(brt);

	spa->spa_brt = brt;
}

void
brt_create(spa_t *spa)
{

	brt_alloc(spa);
	brt_vdevs_alloc(spa->spa_brt, B_FALSE);
}

int
brt_load(spa_t *spa)
{

	brt_alloc(spa);
	brt_vdevs_alloc(spa->spa_brt, B_TRUE);

	return (0);
}

void
brt_unload(spa_t *spa)
{
	brt_t *brt = spa->spa_brt;

	if (brt == NULL)
		return;

	brt_vdevs_free(brt);
	brt_table_free(brt);
	rw_destroy(&brt->brt_lock);
	kmem_free(brt, sizeof (*brt));
	spa->spa_brt = NULL;
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_brt, , brt_zap_prefetch, INT, ZMOD_RW,
	"Enable prefetching of BRT ZAP entries");
ZFS_MODULE_PARAM(zfs_brt, , brt_zap_default_bs, UINT, ZMOD_RW,
	"BRT ZAP leaf blockshift");
ZFS_MODULE_PARAM(zfs_brt, , brt_zap_default_ibs, UINT, ZMOD_RW,
	"BRT ZAP indirect blockshift");
/* END CSTYLED */
