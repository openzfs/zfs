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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2022 by Pawel Jakub Dawidek
 * Copyright (c) 2023, Klara Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/ddt.h>
#include <sys/ddt_impl.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/dsl_pool.h>
#include <sys/zio_checksum.h>
#include <sys/dsl_scan.h>
#include <sys/abd.h>

/*
 * # DDT: Deduplication tables
 *
 * The dedup subsystem provides block-level deduplication. When enabled, blocks
 * to be written will have the dedup (D) bit set, which causes them to be
 * tracked in a "dedup table", or DDT. If a block has been seen before (exists
 * in the DDT), instead of being written, it will instead be made to reference
 * the existing on-disk data, and a refcount bumped in the DDT instead.
 *
 * ## Dedup tables and entries
 *
 * Conceptually, a DDT is a dictionary or map. Each entry has a "key"
 * (ddt_key_t) made up a block's checksum and certian properties, and a "value"
 * (one or more ddt_phys_t) containing valid DVAs for the block's data, birth
 * time and refcount. Together these are enough to track references to a
 * specific block, to build a valid block pointer to reference that block (for
 * freeing, scrubbing, etc), and to fill a new block pointer with the missing
 * pieces to make it seem like it was written.
 *
 * There's a single DDT (ddt_t) for each checksum type, held in spa_ddt[].
 * Within each DDT, there can be multiple storage "types" (ddt_type_t, on-disk
 * object data formats, each with their own implementations) and "classes"
 * (ddt_class_t, instance of a storage type object, for entries with a specific
 * characteristic). An entry (key) will only ever exist on one of these objects
 * at any given time, but may be moved from one to another if their type or
 * class changes.
 *
 * The DDT is driven by the write IO pipeline (zio_ddt_write()). When a block
 * is to be written, before DVAs have been allocated, ddt_lookup() is called to
 * see if the block has been seen before. If its not found, the write proceeds
 * as normal, and after it succeeds, a new entry is created. If it is found, we
 * fill the BP with the DVAs from the entry, increment the refcount and cause
 * the write IO to return immediately.
 *
 * Each ddt_phys_t slot in the entry represents a separate dedup block for the
 * same content/checksum. The slot is selected based on the zp_copies parameter
 * the block is written with, that is, the number of DVAs in the block. The
 * "ditto" slot (DDT_PHYS_DITTO) used to be used for now-removed "dedupditto"
 * feature. These are no longer written, and will be freed if encountered on
 * old pools.
 *
 * ## Lifetime of an entry
 *
 * A DDT can be enormous, and typically is not held in memory all at once.
 * Instead, the changes to an entry are tracked in memory, and written down to
 * disk at the end of each txg.
 *
 * A "live" in-memory entry (ddt_entry_t) is a node on the live tree
 * (ddt_tree).  At the start of a txg, ddt_tree is empty. When an entry is
 * required for IO, ddt_lookup() is called. If an entry already exists on
 * ddt_tree, it is returned. Otherwise, a new one is created, and the
 * type/class objects for the DDT are searched for that key. If its found, its
 * value is copied into the live entry. If not, an empty entry is created.
 *
 * The live entry will be modified during the txg, usually by modifying the
 * refcount, but sometimes by adding or updating DVAs. At the end of the txg
 * (during spa_sync()), type and class are recalculated for entry (see
 * ddt_sync_entry()), and the entry is written to the appropriate storage
 * object and (if necessary), removed from an old one. ddt_tree is cleared and
 * the next txg can start.
 *
 * ## Repair IO
 *
 * If a read on a dedup block fails, but there are other copies of the block in
 * the other ddt_phys_t slots, reads will be issued for those instead
 * (zio_ddt_read_start()). If one of those succeeds, the read is returned to
 * the caller, and a copy is stashed on the entry's dde_repair_abd.
 *
 * During the end-of-txg sync, any entries with a dde_repair_abd get a
 * "rewrite" write issued for the original block pointer, with the data read
 * from the alternate block. If the block is actually damaged, this will invoke
 * the pool's "self-healing" mechanism, and repair the block.
 *
 * ## Scanning (scrub/resilver)
 *
 * If dedup is active, the scrub machinery will walk the dedup table first, and
 * scrub all blocks with refcnt > 1 first. After that it will move on to the
 * regular top-down scrub, and exclude the refcnt > 1 blocks when it sees them.
 * In this way, heavily deduplicated blocks are only scrubbed once. See the
 * commentary on dsl_scan_ddt() for more details.
 *
 * Walking the DDT is done via ddt_walk(). The current position is stored in a
 * ddt_bookmark_t, which represents a stable position in the storage object.
 * This bookmark is stored by the scan machinery, and must reference the same
 * position on the object even if the object changes, the pool is exported, or
 * OpenZFS is upgraded.
 *
 * ## Interaction with block cloning
 *
 * If block cloning and dedup are both enabled on a pool, BRT will look for the
 * dedup bit on an incoming block pointer. If set, it will call into the DDT
 * (ddt_addref()) to add a reference to the block, instead of adding a
 * reference to the BRT. See brt_pending_apply().
 */

/*
 * These are the only checksums valid for dedup. They must match the list
 * from dedup_table in zfs_prop.c
 */
#define	DDT_CHECKSUM_VALID(c)	\
	(c == ZIO_CHECKSUM_SHA256 || c == ZIO_CHECKSUM_SHA512 || \
	c == ZIO_CHECKSUM_SKEIN || c == ZIO_CHECKSUM_EDONR || \
	c == ZIO_CHECKSUM_BLAKE3)

static kmem_cache_t *ddt_cache;
static kmem_cache_t *ddt_entry_cache;

/*
 * Enable/disable prefetching of dedup-ed blocks which are going to be freed.
 */
int zfs_dedup_prefetch = 0;

static const ddt_ops_t *const ddt_ops[DDT_TYPES] = {
	&ddt_zap_ops,
};

static const char *const ddt_class_name[DDT_CLASSES] = {
	"ditto",
	"duplicate",
	"unique",
};

static void
ddt_object_create(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    dmu_tx_t *tx)
{
	spa_t *spa = ddt->ddt_spa;
	objset_t *os = ddt->ddt_os;
	uint64_t *objectp = &ddt->ddt_object[type][class];
	boolean_t prehash = zio_checksum_table[ddt->ddt_checksum].ci_flags &
	    ZCHECKSUM_FLAG_DEDUP;
	char name[DDT_NAMELEN];

	ddt_object_name(ddt, type, class, name);

	ASSERT3U(*objectp, ==, 0);
	VERIFY0(ddt_ops[type]->ddt_op_create(os, objectp, tx, prehash));
	ASSERT3U(*objectp, !=, 0);

	VERIFY0(zap_add(os, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, objectp, tx));

	VERIFY0(zap_add(os, spa->spa_ddt_stat_object, name,
	    sizeof (uint64_t), sizeof (ddt_histogram_t) / sizeof (uint64_t),
	    &ddt->ddt_histogram[type][class], tx));
}

static void
ddt_object_destroy(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    dmu_tx_t *tx)
{
	spa_t *spa = ddt->ddt_spa;
	objset_t *os = ddt->ddt_os;
	uint64_t *objectp = &ddt->ddt_object[type][class];
	uint64_t count;
	char name[DDT_NAMELEN];

	ddt_object_name(ddt, type, class, name);

	ASSERT3U(*objectp, !=, 0);
	ASSERT(ddt_histogram_empty(&ddt->ddt_histogram[type][class]));
	VERIFY0(ddt_object_count(ddt, type, class, &count));
	VERIFY0(count);
	VERIFY0(zap_remove(os, DMU_POOL_DIRECTORY_OBJECT, name, tx));
	VERIFY0(zap_remove(os, spa->spa_ddt_stat_object, name, tx));
	VERIFY0(ddt_ops[type]->ddt_op_destroy(os, *objectp, tx));
	memset(&ddt->ddt_object_stats[type][class], 0, sizeof (ddt_object_t));

	*objectp = 0;
}

static int
ddt_object_load(ddt_t *ddt, ddt_type_t type, ddt_class_t class)
{
	ddt_object_t *ddo = &ddt->ddt_object_stats[type][class];
	dmu_object_info_t doi;
	uint64_t count;
	char name[DDT_NAMELEN];
	int error;

	ddt_object_name(ddt, type, class, name);

	error = zap_lookup(ddt->ddt_os, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, &ddt->ddt_object[type][class]);
	if (error != 0)
		return (error);

	error = zap_lookup(ddt->ddt_os, ddt->ddt_spa->spa_ddt_stat_object, name,
	    sizeof (uint64_t), sizeof (ddt_histogram_t) / sizeof (uint64_t),
	    &ddt->ddt_histogram[type][class]);
	if (error != 0)
		return (error);

	/*
	 * Seed the cached statistics.
	 */
	error = ddt_object_info(ddt, type, class, &doi);
	if (error)
		return (error);

	error = ddt_object_count(ddt, type, class, &count);
	if (error)
		return (error);

	ddo->ddo_count = count;
	ddo->ddo_dspace = doi.doi_physical_blocks_512 << 9;
	ddo->ddo_mspace = doi.doi_fill_count * doi.doi_data_block_size;

	return (0);
}

static void
ddt_object_sync(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    dmu_tx_t *tx)
{
	ddt_object_t *ddo = &ddt->ddt_object_stats[type][class];
	dmu_object_info_t doi;
	uint64_t count;
	char name[DDT_NAMELEN];

	ddt_object_name(ddt, type, class, name);

	VERIFY0(zap_update(ddt->ddt_os, ddt->ddt_spa->spa_ddt_stat_object, name,
	    sizeof (uint64_t), sizeof (ddt_histogram_t) / sizeof (uint64_t),
	    &ddt->ddt_histogram[type][class], tx));

	/*
	 * Cache DDT statistics; this is the only time they'll change.
	 */
	VERIFY0(ddt_object_info(ddt, type, class, &doi));
	VERIFY0(ddt_object_count(ddt, type, class, &count));

	ddo->ddo_count = count;
	ddo->ddo_dspace = doi.doi_physical_blocks_512 << 9;
	ddo->ddo_mspace = doi.doi_fill_count * doi.doi_data_block_size;
}

static boolean_t
ddt_object_exists(ddt_t *ddt, ddt_type_t type, ddt_class_t class)
{
	return (!!ddt->ddt_object[type][class]);
}

static int
ddt_object_lookup(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    ddt_entry_t *dde)
{
	if (!ddt_object_exists(ddt, type, class))
		return (SET_ERROR(ENOENT));

	return (ddt_ops[type]->ddt_op_lookup(ddt->ddt_os,
	    ddt->ddt_object[type][class], &dde->dde_key,
	    dde->dde_phys, sizeof (dde->dde_phys)));
}

static int
ddt_object_contains(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    const ddt_key_t *ddk)
{
	if (!ddt_object_exists(ddt, type, class))
		return (SET_ERROR(ENOENT));

	return (ddt_ops[type]->ddt_op_contains(ddt->ddt_os,
	    ddt->ddt_object[type][class], ddk));
}

static void
ddt_object_prefetch(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    const ddt_key_t *ddk)
{
	if (!ddt_object_exists(ddt, type, class))
		return;

	ddt_ops[type]->ddt_op_prefetch(ddt->ddt_os,
	    ddt->ddt_object[type][class], ddk);
}

static int
ddt_object_update(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    ddt_entry_t *dde, dmu_tx_t *tx)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_update(ddt->ddt_os,
	    ddt->ddt_object[type][class], &dde->dde_key, dde->dde_phys,
	    sizeof (dde->dde_phys), tx));
}

static int
ddt_object_remove(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    const ddt_key_t *ddk, dmu_tx_t *tx)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_remove(ddt->ddt_os,
	    ddt->ddt_object[type][class], ddk, tx));
}

int
ddt_object_walk(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    uint64_t *walk, ddt_entry_t *dde)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_walk(ddt->ddt_os,
	    ddt->ddt_object[type][class], walk, &dde->dde_key,
	    dde->dde_phys, sizeof (dde->dde_phys)));
}

int
ddt_object_count(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    uint64_t *count)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_count(ddt->ddt_os,
	    ddt->ddt_object[type][class], count));
}

int
ddt_object_info(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    dmu_object_info_t *doi)
{
	if (!ddt_object_exists(ddt, type, class))
		return (SET_ERROR(ENOENT));

	return (dmu_object_info(ddt->ddt_os, ddt->ddt_object[type][class],
	    doi));
}

void
ddt_object_name(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    char *name)
{
	(void) snprintf(name, DDT_NAMELEN, DMU_POOL_DDT,
	    zio_checksum_table[ddt->ddt_checksum].ci_name,
	    ddt_ops[type]->ddt_op_name, ddt_class_name[class]);
}

void
ddt_bp_fill(const ddt_phys_t *ddp, blkptr_t *bp, uint64_t txg)
{
	ASSERT3U(txg, !=, 0);

	for (int d = 0; d < SPA_DVAS_PER_BP; d++)
		bp->blk_dva[d] = ddp->ddp_dva[d];
	BP_SET_BIRTH(bp, txg, ddp->ddp_phys_birth);
}

/*
 * The bp created via this function may be used for repairs and scrub, but it
 * will be missing the salt / IV required to do a full decrypting read.
 */
void
ddt_bp_create(enum zio_checksum checksum,
    const ddt_key_t *ddk, const ddt_phys_t *ddp, blkptr_t *bp)
{
	BP_ZERO(bp);

	if (ddp != NULL)
		ddt_bp_fill(ddp, bp, ddp->ddp_phys_birth);

	bp->blk_cksum = ddk->ddk_cksum;

	BP_SET_LSIZE(bp, DDK_GET_LSIZE(ddk));
	BP_SET_PSIZE(bp, DDK_GET_PSIZE(ddk));
	BP_SET_COMPRESS(bp, DDK_GET_COMPRESS(ddk));
	BP_SET_CRYPT(bp, DDK_GET_CRYPT(ddk));
	BP_SET_FILL(bp, 1);
	BP_SET_CHECKSUM(bp, checksum);
	BP_SET_TYPE(bp, DMU_OT_DEDUP);
	BP_SET_LEVEL(bp, 0);
	BP_SET_DEDUP(bp, 1);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);
}

void
ddt_key_fill(ddt_key_t *ddk, const blkptr_t *bp)
{
	ddk->ddk_cksum = bp->blk_cksum;
	ddk->ddk_prop = 0;

	ASSERT(BP_IS_ENCRYPTED(bp) || !BP_USES_CRYPT(bp));

	DDK_SET_LSIZE(ddk, BP_GET_LSIZE(bp));
	DDK_SET_PSIZE(ddk, BP_GET_PSIZE(bp));
	DDK_SET_COMPRESS(ddk, BP_GET_COMPRESS(bp));
	DDK_SET_CRYPT(ddk, BP_USES_CRYPT(bp));
}

void
ddt_phys_fill(ddt_phys_t *ddp, const blkptr_t *bp)
{
	ASSERT0(ddp->ddp_phys_birth);

	for (int d = 0; d < SPA_DVAS_PER_BP; d++)
		ddp->ddp_dva[d] = bp->blk_dva[d];
	ddp->ddp_phys_birth = BP_PHYSICAL_BIRTH(bp);
}

void
ddt_phys_clear(ddt_phys_t *ddp)
{
	memset(ddp, 0, sizeof (*ddp));
}

void
ddt_phys_addref(ddt_phys_t *ddp)
{
	ddp->ddp_refcnt++;
}

void
ddt_phys_decref(ddt_phys_t *ddp)
{
	if (ddp) {
		ASSERT3U(ddp->ddp_refcnt, >, 0);
		ddp->ddp_refcnt--;
	}
}

static void
ddt_phys_free(ddt_t *ddt, ddt_key_t *ddk, ddt_phys_t *ddp, uint64_t txg)
{
	blkptr_t blk;

	ddt_bp_create(ddt->ddt_checksum, ddk, ddp, &blk);

	/*
	 * We clear the dedup bit so that zio_free() will actually free the
	 * space, rather than just decrementing the refcount in the DDT.
	 */
	BP_SET_DEDUP(&blk, 0);

	ddt_phys_clear(ddp);
	zio_free(ddt->ddt_spa, txg, &blk);
}

ddt_phys_t *
ddt_phys_select(const ddt_entry_t *dde, const blkptr_t *bp)
{
	ddt_phys_t *ddp = (ddt_phys_t *)dde->dde_phys;

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		if (DVA_EQUAL(BP_IDENTITY(bp), &ddp->ddp_dva[0]) &&
		    BP_PHYSICAL_BIRTH(bp) == ddp->ddp_phys_birth)
			return (ddp);
	}
	return (NULL);
}

uint64_t
ddt_phys_total_refcnt(const ddt_entry_t *dde)
{
	uint64_t refcnt = 0;

	for (int p = DDT_PHYS_SINGLE; p <= DDT_PHYS_TRIPLE; p++)
		refcnt += dde->dde_phys[p].ddp_refcnt;

	return (refcnt);
}

ddt_t *
ddt_select(spa_t *spa, const blkptr_t *bp)
{
	ASSERT(DDT_CHECKSUM_VALID(BP_GET_CHECKSUM(bp)));
	return (spa->spa_ddt[BP_GET_CHECKSUM(bp)]);
}

void
ddt_enter(ddt_t *ddt)
{
	mutex_enter(&ddt->ddt_lock);
}

void
ddt_exit(ddt_t *ddt)
{
	mutex_exit(&ddt->ddt_lock);
}

void
ddt_init(void)
{
	ddt_cache = kmem_cache_create("ddt_cache",
	    sizeof (ddt_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	ddt_entry_cache = kmem_cache_create("ddt_entry_cache",
	    sizeof (ddt_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
ddt_fini(void)
{
	kmem_cache_destroy(ddt_entry_cache);
	kmem_cache_destroy(ddt_cache);
}

static ddt_entry_t *
ddt_alloc(const ddt_key_t *ddk)
{
	ddt_entry_t *dde;

	dde = kmem_cache_alloc(ddt_entry_cache, KM_SLEEP);
	memset(dde, 0, sizeof (ddt_entry_t));
	cv_init(&dde->dde_cv, NULL, CV_DEFAULT, NULL);

	dde->dde_key = *ddk;

	return (dde);
}

static void
ddt_free(ddt_entry_t *dde)
{
	ASSERT(dde->dde_flags & DDE_FLAG_LOADED);

	for (int p = 0; p < DDT_PHYS_TYPES; p++)
		ASSERT3P(dde->dde_lead_zio[p], ==, NULL);

	if (dde->dde_repair_abd != NULL)
		abd_free(dde->dde_repair_abd);

	cv_destroy(&dde->dde_cv);
	kmem_cache_free(ddt_entry_cache, dde);
}

void
ddt_remove(ddt_t *ddt, ddt_entry_t *dde)
{
	ASSERT(MUTEX_HELD(&ddt->ddt_lock));

	avl_remove(&ddt->ddt_tree, dde);
	ddt_free(dde);
}

ddt_entry_t *
ddt_lookup(ddt_t *ddt, const blkptr_t *bp, boolean_t add)
{
	ddt_key_t search;
	ddt_entry_t *dde;
	ddt_type_t type;
	ddt_class_t class;
	avl_index_t where;
	int error;

	ASSERT(MUTEX_HELD(&ddt->ddt_lock));

	ddt_key_fill(&search, bp);

	/* Find an existing live entry */
	dde = avl_find(&ddt->ddt_tree, &search, &where);
	if (dde != NULL) {
		/* Found it. If it's already loaded, we can just return it. */
		if (dde->dde_flags & DDE_FLAG_LOADED)
			return (dde);

		/* Someone else is loading it, wait for it. */
		while (!(dde->dde_flags & DDE_FLAG_LOADED))
			cv_wait(&dde->dde_cv, &ddt->ddt_lock);

		return (dde);
	}

	/* Not found. */
	if (!add)
		return (NULL);

	/* Time to make a new entry. */
	dde = ddt_alloc(&search);
	avl_insert(&ddt->ddt_tree, dde, where);

	/*
	 * ddt_tree is now stable, so unlock and let everyone else keep moving.
	 * Anyone landing on this entry will find it without DDE_FLAG_LOADED,
	 * and go to sleep waiting for it above.
	 */
	ddt_exit(ddt);

	/* Search all store objects for the entry. */
	error = ENOENT;
	for (type = 0; type < DDT_TYPES; type++) {
		for (class = 0; class < DDT_CLASSES; class++) {
			error = ddt_object_lookup(ddt, type, class, dde);
			if (error != ENOENT) {
				ASSERT0(error);
				break;
			}
		}
		if (error != ENOENT)
			break;
	}

	ddt_enter(ddt);

	ASSERT(!(dde->dde_flags & DDE_FLAG_LOADED));

	dde->dde_type = type;	/* will be DDT_TYPES if no entry found */
	dde->dde_class = class;	/* will be DDT_CLASSES if no entry found */

	if (error == 0)
		ddt_stat_update(ddt, dde, -1ULL);

	/* Entry loaded, everyone can proceed now */
	dde->dde_flags |= DDE_FLAG_LOADED;
	cv_broadcast(&dde->dde_cv);

	return (dde);
}

void
ddt_prefetch(spa_t *spa, const blkptr_t *bp)
{
	ddt_t *ddt;
	ddt_key_t ddk;

	if (!zfs_dedup_prefetch || bp == NULL || !BP_GET_DEDUP(bp))
		return;

	/*
	 * We only remove the DDT once all tables are empty and only
	 * prefetch dedup blocks when there are entries in the DDT.
	 * Thus no locking is required as the DDT can't disappear on us.
	 */
	ddt = ddt_select(spa, bp);
	ddt_key_fill(&ddk, bp);

	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			ddt_object_prefetch(ddt, type, class, &ddk);
		}
	}
}

/*
 * Key comparison. Any struct wanting to make use of this function must have
 * the key as the first element.
 */
#define	DDT_KEY_CMP_LEN	(sizeof (ddt_key_t) / sizeof (uint16_t))

typedef struct ddt_key_cmp {
	uint16_t	u16[DDT_KEY_CMP_LEN];
} ddt_key_cmp_t;

int
ddt_key_compare(const void *x1, const void *x2)
{
	const ddt_key_cmp_t *k1 = (const ddt_key_cmp_t *)x1;
	const ddt_key_cmp_t *k2 = (const ddt_key_cmp_t *)x2;
	int32_t cmp = 0;

	for (int i = 0; i < DDT_KEY_CMP_LEN; i++) {
		cmp = (int32_t)k1->u16[i] - (int32_t)k2->u16[i];
		if (likely(cmp))
			break;
	}

	return (TREE_ISIGN(cmp));
}

static ddt_t *
ddt_table_alloc(spa_t *spa, enum zio_checksum c)
{
	ddt_t *ddt;

	ddt = kmem_cache_alloc(ddt_cache, KM_SLEEP);
	memset(ddt, 0, sizeof (ddt_t));

	mutex_init(&ddt->ddt_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&ddt->ddt_tree, ddt_key_compare,
	    sizeof (ddt_entry_t), offsetof(ddt_entry_t, dde_node));
	avl_create(&ddt->ddt_repair_tree, ddt_key_compare,
	    sizeof (ddt_entry_t), offsetof(ddt_entry_t, dde_node));
	ddt->ddt_checksum = c;
	ddt->ddt_spa = spa;
	ddt->ddt_os = spa->spa_meta_objset;

	return (ddt);
}

static void
ddt_table_free(ddt_t *ddt)
{
	ASSERT0(avl_numnodes(&ddt->ddt_tree));
	ASSERT0(avl_numnodes(&ddt->ddt_repair_tree));
	avl_destroy(&ddt->ddt_tree);
	avl_destroy(&ddt->ddt_repair_tree);
	mutex_destroy(&ddt->ddt_lock);
	kmem_cache_free(ddt_cache, ddt);
}

void
ddt_create(spa_t *spa)
{
	spa->spa_dedup_checksum = ZIO_DEDUPCHECKSUM;

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		if (DDT_CHECKSUM_VALID(c))
			spa->spa_ddt[c] = ddt_table_alloc(spa, c);
	}
}

int
ddt_load(spa_t *spa)
{
	int error;

	ddt_create(spa);

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_DDT_STATS, sizeof (uint64_t), 1,
	    &spa->spa_ddt_stat_object);

	if (error)
		return (error == ENOENT ? 0 : error);

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		if (!DDT_CHECKSUM_VALID(c))
			continue;

		ddt_t *ddt = spa->spa_ddt[c];
		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				error = ddt_object_load(ddt, type, class);
				if (error != 0 && error != ENOENT)
					return (error);
			}
		}

		/*
		 * Seed the cached histograms.
		 */
		memcpy(&ddt->ddt_histogram_cache, ddt->ddt_histogram,
		    sizeof (ddt->ddt_histogram));
		spa->spa_dedup_dspace = ~0ULL;
	}

	return (0);
}

void
ddt_unload(spa_t *spa)
{
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		if (spa->spa_ddt[c]) {
			ddt_table_free(spa->spa_ddt[c]);
			spa->spa_ddt[c] = NULL;
		}
	}
}

boolean_t
ddt_class_contains(spa_t *spa, ddt_class_t max_class, const blkptr_t *bp)
{
	ddt_t *ddt;
	ddt_key_t ddk;

	if (!BP_GET_DEDUP(bp))
		return (B_FALSE);

	if (max_class == DDT_CLASS_UNIQUE)
		return (B_TRUE);

	ddt = spa->spa_ddt[BP_GET_CHECKSUM(bp)];

	ddt_key_fill(&ddk, bp);

	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		for (ddt_class_t class = 0; class <= max_class; class++) {
			if (ddt_object_contains(ddt, type, class, &ddk) == 0)
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}

ddt_entry_t *
ddt_repair_start(ddt_t *ddt, const blkptr_t *bp)
{
	ddt_key_t ddk;
	ddt_entry_t *dde;

	ddt_key_fill(&ddk, bp);

	dde = ddt_alloc(&ddk);

	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			/*
			 * We can only do repair if there are multiple copies
			 * of the block.  For anything in the UNIQUE class,
			 * there's definitely only one copy, so don't even try.
			 */
			if (class != DDT_CLASS_UNIQUE &&
			    ddt_object_lookup(ddt, type, class, dde) == 0)
				return (dde);
		}
	}

	memset(dde->dde_phys, 0, sizeof (dde->dde_phys));

	return (dde);
}

void
ddt_repair_done(ddt_t *ddt, ddt_entry_t *dde)
{
	avl_index_t where;

	ddt_enter(ddt);

	if (dde->dde_repair_abd != NULL && spa_writeable(ddt->ddt_spa) &&
	    avl_find(&ddt->ddt_repair_tree, dde, &where) == NULL)
		avl_insert(&ddt->ddt_repair_tree, dde, where);
	else
		ddt_free(dde);

	ddt_exit(ddt);
}

static void
ddt_repair_entry_done(zio_t *zio)
{
	ddt_entry_t *rdde = zio->io_private;

	ddt_free(rdde);
}

static void
ddt_repair_entry(ddt_t *ddt, ddt_entry_t *dde, ddt_entry_t *rdde, zio_t *rio)
{
	ddt_phys_t *ddp = dde->dde_phys;
	ddt_phys_t *rddp = rdde->dde_phys;
	ddt_key_t *ddk = &dde->dde_key;
	ddt_key_t *rddk = &rdde->dde_key;
	zio_t *zio;
	blkptr_t blk;

	zio = zio_null(rio, rio->io_spa, NULL,
	    ddt_repair_entry_done, rdde, rio->io_flags);

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++, rddp++) {
		if (ddp->ddp_phys_birth == 0 ||
		    ddp->ddp_phys_birth != rddp->ddp_phys_birth ||
		    memcmp(ddp->ddp_dva, rddp->ddp_dva, sizeof (ddp->ddp_dva)))
			continue;
		ddt_bp_create(ddt->ddt_checksum, ddk, ddp, &blk);
		zio_nowait(zio_rewrite(zio, zio->io_spa, 0, &blk,
		    rdde->dde_repair_abd, DDK_GET_PSIZE(rddk), NULL, NULL,
		    ZIO_PRIORITY_SYNC_WRITE, ZIO_DDT_CHILD_FLAGS(zio), NULL));
	}

	zio_nowait(zio);
}

static void
ddt_repair_table(ddt_t *ddt, zio_t *rio)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_entry_t *dde, *rdde_next, *rdde;
	avl_tree_t *t = &ddt->ddt_repair_tree;
	blkptr_t blk;

	if (spa_sync_pass(spa) > 1)
		return;

	ddt_enter(ddt);
	for (rdde = avl_first(t); rdde != NULL; rdde = rdde_next) {
		rdde_next = AVL_NEXT(t, rdde);
		avl_remove(&ddt->ddt_repair_tree, rdde);
		ddt_exit(ddt);
		ddt_bp_create(ddt->ddt_checksum, &rdde->dde_key, NULL, &blk);
		dde = ddt_repair_start(ddt, &blk);
		ddt_repair_entry(ddt, dde, rdde, rio);
		ddt_repair_done(ddt, dde);
		ddt_enter(ddt);
	}
	ddt_exit(ddt);
}

static void
ddt_sync_entry(ddt_t *ddt, ddt_entry_t *dde, dmu_tx_t *tx, uint64_t txg)
{
	dsl_pool_t *dp = ddt->ddt_spa->spa_dsl_pool;
	ddt_phys_t *ddp = dde->dde_phys;
	ddt_key_t *ddk = &dde->dde_key;
	ddt_type_t otype = dde->dde_type;
	ddt_type_t ntype = DDT_TYPE_DEFAULT;
	ddt_class_t oclass = dde->dde_class;
	ddt_class_t nclass;
	uint64_t total_refcnt = 0;

	ASSERT(dde->dde_flags & DDE_FLAG_LOADED);

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		ASSERT3P(dde->dde_lead_zio[p], ==, NULL);
		if (ddp->ddp_phys_birth == 0) {
			ASSERT0(ddp->ddp_refcnt);
			continue;
		}
		if (p == DDT_PHYS_DITTO) {
			/*
			 * Note, we no longer create DDT-DITTO blocks, but we
			 * don't want to leak any written by older software.
			 */
			ddt_phys_free(ddt, ddk, ddp, txg);
			continue;
		}
		if (ddp->ddp_refcnt == 0)
			ddt_phys_free(ddt, ddk, ddp, txg);
		total_refcnt += ddp->ddp_refcnt;
	}

	/* We do not create new DDT-DITTO blocks. */
	ASSERT0(dde->dde_phys[DDT_PHYS_DITTO].ddp_phys_birth);
	if (total_refcnt > 1)
		nclass = DDT_CLASS_DUPLICATE;
	else
		nclass = DDT_CLASS_UNIQUE;

	if (otype != DDT_TYPES &&
	    (otype != ntype || oclass != nclass || total_refcnt == 0)) {
		VERIFY0(ddt_object_remove(ddt, otype, oclass, ddk, tx));
		ASSERT3U(
		    ddt_object_contains(ddt, otype, oclass, ddk), ==, ENOENT);
	}

	if (total_refcnt != 0) {
		dde->dde_type = ntype;
		dde->dde_class = nclass;
		ddt_stat_update(ddt, dde, 0);
		if (!ddt_object_exists(ddt, ntype, nclass))
			ddt_object_create(ddt, ntype, nclass, tx);
		VERIFY0(ddt_object_update(ddt, ntype, nclass, dde, tx));

		/*
		 * If the class changes, the order that we scan this bp
		 * changes.  If it decreases, we could miss it, so
		 * scan it right now.  (This covers both class changing
		 * while we are doing ddt_walk(), and when we are
		 * traversing.)
		 */
		if (nclass < oclass) {
			dsl_scan_ddt_entry(dp->dp_scan,
			    ddt->ddt_checksum, dde, tx);
		}
	}
}

static void
ddt_sync_table(ddt_t *ddt, dmu_tx_t *tx, uint64_t txg)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_entry_t *dde;
	void *cookie = NULL;

	if (avl_numnodes(&ddt->ddt_tree) == 0)
		return;

	ASSERT3U(spa->spa_uberblock.ub_version, >=, SPA_VERSION_DEDUP);

	if (spa->spa_ddt_stat_object == 0) {
		spa->spa_ddt_stat_object = zap_create_link(ddt->ddt_os,
		    DMU_OT_DDT_STATS, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_DDT_STATS, tx);
	}

	while ((dde = avl_destroy_nodes(&ddt->ddt_tree, &cookie)) != NULL) {
		ddt_sync_entry(ddt, dde, tx, txg);
		ddt_free(dde);
	}

	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		uint64_t add, count = 0;
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			if (ddt_object_exists(ddt, type, class)) {
				ddt_object_sync(ddt, type, class, tx);
				VERIFY0(ddt_object_count(ddt, type, class,
				    &add));
				count += add;
			}
		}
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			if (count == 0 && ddt_object_exists(ddt, type, class))
				ddt_object_destroy(ddt, type, class, tx);
		}
	}

	memcpy(&ddt->ddt_histogram_cache, ddt->ddt_histogram,
	    sizeof (ddt->ddt_histogram));
	spa->spa_dedup_dspace = ~0ULL;
}

void
ddt_sync(spa_t *spa, uint64_t txg)
{
	dsl_scan_t *scn = spa->spa_dsl_pool->dp_scan;
	dmu_tx_t *tx;
	zio_t *rio;

	ASSERT3U(spa_syncing_txg(spa), ==, txg);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	rio = zio_root(spa, NULL, NULL,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SELF_HEAL);

	/*
	 * This function may cause an immediate scan of ddt blocks (see
	 * the comment above dsl_scan_ddt() for details). We set the
	 * scan's root zio here so that we can wait for any scan IOs in
	 * addition to the regular ddt IOs.
	 */
	ASSERT3P(scn->scn_zio_root, ==, NULL);
	scn->scn_zio_root = rio;

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (ddt == NULL)
			continue;
		ddt_sync_table(ddt, tx, txg);
		ddt_repair_table(ddt, rio);
	}

	(void) zio_wait(rio);
	scn->scn_zio_root = NULL;

	dmu_tx_commit(tx);
}

int
ddt_walk(spa_t *spa, ddt_bookmark_t *ddb, ddt_entry_t *dde)
{
	do {
		do {
			do {
				ddt_t *ddt = spa->spa_ddt[ddb->ddb_checksum];
				if (ddt == NULL)
					continue;
				int error = ENOENT;
				if (ddt_object_exists(ddt, ddb->ddb_type,
				    ddb->ddb_class)) {
					error = ddt_object_walk(ddt,
					    ddb->ddb_type, ddb->ddb_class,
					    &ddb->ddb_cursor, dde);
				}
				dde->dde_type = ddb->ddb_type;
				dde->dde_class = ddb->ddb_class;
				if (error == 0)
					return (0);
				if (error != ENOENT)
					return (error);
				ddb->ddb_cursor = 0;
			} while (++ddb->ddb_checksum < ZIO_CHECKSUM_FUNCTIONS);
			ddb->ddb_checksum = 0;
		} while (++ddb->ddb_type < DDT_TYPES);
		ddb->ddb_type = 0;
	} while (++ddb->ddb_class < DDT_CLASSES);

	return (SET_ERROR(ENOENT));
}

/*
 * This function is used by Block Cloning (brt.c) to increase reference
 * counter for the DDT entry if the block is already in DDT.
 *
 * Return false if the block, despite having the D bit set, is not present
 * in the DDT. Currently this is not possible but might be in the future.
 * See the comment below.
 */
boolean_t
ddt_addref(spa_t *spa, const blkptr_t *bp)
{
	ddt_t *ddt;
	ddt_entry_t *dde;
	boolean_t result;

	spa_config_enter(spa, SCL_ZIO, FTAG, RW_READER);
	ddt = ddt_select(spa, bp);
	ddt_enter(ddt);

	dde = ddt_lookup(ddt, bp, B_TRUE);
	ASSERT3P(dde, !=, NULL);

	if (dde->dde_type < DDT_TYPES) {
		ddt_phys_t *ddp;

		ASSERT3S(dde->dde_class, <, DDT_CLASSES);

		ddp = &dde->dde_phys[BP_GET_NDVAS(bp)];

		/*
		 * This entry already existed (dde_type is real), so it must
		 * have refcnt >0 at the start of this txg. We are called from
		 * brt_pending_apply(), before frees are issued, so the refcnt
		 * can't be lowered yet. Therefore, it must be >0. We assert
		 * this because if the order of BRT and DDT interactions were
		 * ever to change and the refcnt was ever zero here, then
		 * likely further action is required to fill out the DDT entry,
		 * and this is a place that is likely to be missed in testing.
		 */
		ASSERT3U(ddp->ddp_refcnt, >, 0);

		ddt_phys_addref(ddp);
		result = B_TRUE;
	} else {
		/*
		 * At the time of implementating this if the block has the
		 * DEDUP flag set it must exist in the DEDUP table, but
		 * there are many advocates that want ability to remove
		 * entries from DDT with refcnt=1. If this will happen,
		 * we may have a block with the DEDUP set, but which doesn't
		 * have a corresponding entry in the DDT. Be ready.
		 */
		ASSERT3S(dde->dde_class, ==, DDT_CLASSES);
		ddt_remove(ddt, dde);
		result = B_FALSE;
	}

	ddt_exit(ddt);
	spa_config_exit(spa, SCL_ZIO, FTAG);

	return (result);
}

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, prefetch, INT, ZMOD_RW,
	"Enable prefetching dedup-ed blks");
