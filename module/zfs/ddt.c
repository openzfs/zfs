// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2019, 2023, Klara Inc.
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
#include <sys/zfeature.h>

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
 * Traditionally, each ddt_phys_t slot in the entry represents a separate dedup
 * block for the same content/checksum. The slot is selected based on the
 * zp_copies parameter the block is written with, that is, the number of DVAs
 * in the block. The "ditto" slot (DDT_PHYS_DITTO) used to be used for
 * now-removed "dedupditto" feature. These are no longer written, and will be
 * freed if encountered on old pools.
 *
 * If the "fast_dedup" feature is enabled, new dedup tables will be created
 * with the "flat phys" option. In this mode, there is only one ddt_phys_t
 * slot. If a write is issued for an entry that exists, but has fewer DVAs,
 * then only as many new DVAs are allocated and written to make up the
 * shortfall. The existing entry is then extended (ddt_phys_extend()) with the
 * new DVAs.
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
 * ## Dedup quota
 *
 * A maximum size for all DDTs on the pool can be set with the
 * dedup_table_quota property. This is determined in ddt_over_quota() and
 * enforced during ddt_lookup(). If the pool is at or over its quota limit,
 * ddt_lookup() will only return entries for existing blocks, as updates are
 * still possible. New entries will not be created; instead, ddt_lookup() will
 * return NULL. In response, the DDT write stage (zio_ddt_write()) will remove
 * the D bit on the block and reissue the IO as a regular write. The block will
 * not be deduplicated.
 *
 * Note that this is based on the on-disk size of the dedup store. Reclaiming
 * this space after deleting entries relies on the ZAP "shrinking" behaviour,
 * without which, no space would be recovered and the DDT would continue to be
 * considered "over quota". See zap_shrink_enabled.
 *
 * ## Dedup table pruning
 *
 * As a complement to the dedup quota feature, ddtprune allows removal of older
 * non-duplicate entries to make room for newer duplicate entries. The amount
 * to prune can be based on a target percentage of the unique entries or based
 * on the age (i.e., prune unique entry older than N days).
 *
 * ## Dedup log
 *
 * Historically, all entries modified on a txg were written back to dedup
 * storage objects at the end of every txg. This could cause significant
 * overheads, as each entry only takes up a tiny portion of a ZAP leaf node,
 * and so required reading the whole node, updating the entry, and writing it
 * back. On busy pools, this could add serious IO and memory overheads.
 *
 * To address this, the dedup log was added. If the "fast_dedup" feature is
 * enabled, at the end of each txg, modified entries will be copied to an
 * in-memory "log" object (ddt_log_t), and appended to an on-disk log. If the
 * same block is requested again, the in-memory object will be checked first,
 * and if its there, the entry inflated back onto the live tree without going
 * to storage. The on-disk log is only read at pool import time, to reload the
 * in-memory log.
 *
 * Each txg, some amount of the in-memory log will be flushed out to a DDT
 * storage object (ie ZAP) as normal. OpenZFS will try hard to flush enough to
 * keep up with the rate of change on dedup entries, but not so much that it
 * would impact overall throughput, and not using too much memory. See the
 * zfs_dedup_log_* tuneables in zfs(4) for more details.
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
 * If the "fast_dedup" feature is enabled, the "flat phys" option will be in
 * use, so there is only ever one ddt_phys_t slot. The repair process will
 * still happen in this case, though it is unlikely to succeed as there will
 * usually be no other equivalent blocks to fall back on (though there might
 * be, if this was an early version of a dedup'd block that has since been
 * extended).
 *
 * Note that this repair mechanism is in addition to and separate from the
 * regular OpenZFS scrub and self-healing mechanisms.
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
 * If the "fast_dedup" feature is enabled and the table has a log, the scan
 * cannot begin until entries on the log are flushed, as the on-disk log has no
 * concept of a "stable position". Instead, the log flushing process will enter
 * a more aggressive mode, to flush out as much as is necesary as soon as
 * possible, in order to begin the scan as soon as possible.
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

static kmem_cache_t *ddt_entry_flat_cache;
static kmem_cache_t *ddt_entry_trad_cache;

#define	DDT_ENTRY_FLAT_SIZE	(sizeof (ddt_entry_t) + DDT_FLAT_PHYS_SIZE)
#define	DDT_ENTRY_TRAD_SIZE	(sizeof (ddt_entry_t) + DDT_TRAD_PHYS_SIZE)

#define	DDT_ENTRY_SIZE(ddt)	\
	_DDT_PHYS_SWITCH(ddt, DDT_ENTRY_FLAT_SIZE, DDT_ENTRY_TRAD_SIZE)

/*
 * Enable/disable prefetching of dedup-ed blocks which are going to be freed.
 */
int zfs_dedup_prefetch = 0;

/*
 * If the dedup class cannot satisfy a DDT allocation, treat as over quota
 * for this many TXGs.
 */
uint_t dedup_class_wait_txgs = 5;

/*
 * How many DDT prune entries to add to the DDT sync AVL tree.
 * Note these addtional entries have a memory footprint of a
 * ddt_entry_t (216 bytes).
 */
static uint32_t zfs_ddt_prunes_per_txg = 50000;

/*
 * For testing, synthesize aged DDT entries
 * (in global scope for ztest)
 */
boolean_t ddt_prune_artificial_age = B_FALSE;
boolean_t ddt_dump_prune_histogram = B_FALSE;

/*
 * Don't do more than this many incremental flush passes per txg.
 */
uint_t zfs_dedup_log_flush_passes_max = 8;

/*
 * Minimum time to flush per txg.
 */
uint_t zfs_dedup_log_flush_min_time_ms = 1000;

/*
 * Minimum entries to flush per txg.
 */
uint_t zfs_dedup_log_flush_entries_min = 1000;

/*
 * Number of txgs to average flow rates across.
 */
uint_t zfs_dedup_log_flush_flow_rate_txgs = 10;

static const ddt_ops_t *const ddt_ops[DDT_TYPES] = {
	&ddt_zap_ops,
};

static const char *const ddt_class_name[DDT_CLASSES] = {
	"ditto",
	"duplicate",
	"unique",
};

/*
 * DDT feature flags automatically enabled for each on-disk version. Note that
 * versions >0 cannot exist on disk without SPA_FEATURE_FAST_DEDUP enabled.
 */
static const uint64_t ddt_version_flags[] = {
	[DDT_VERSION_LEGACY] = 0,
	[DDT_VERSION_FDT] = DDT_FLAG_FLAT | DDT_FLAG_LOG,
};

/* per-DDT kstats */
typedef struct {
	/* total lookups and whether they returned new or existing entries */
	kstat_named_t dds_lookup;
	kstat_named_t dds_lookup_new;
	kstat_named_t dds_lookup_existing;

	/* entries found on live tree, and if we had to wait for load */
	kstat_named_t dds_lookup_live_hit;
	kstat_named_t dds_lookup_live_wait;
	kstat_named_t dds_lookup_live_miss;

	/* entries found on log trees */
	kstat_named_t dds_lookup_log_hit;
	kstat_named_t dds_lookup_log_active_hit;
	kstat_named_t dds_lookup_log_flushing_hit;
	kstat_named_t dds_lookup_log_miss;

	/* entries found on store objects */
	kstat_named_t dds_lookup_stored_hit;
	kstat_named_t dds_lookup_stored_miss;

	/* number of entries on log trees */
	kstat_named_t dds_log_active_entries;
	kstat_named_t dds_log_flushing_entries;

	/* avg updated/flushed entries per txg */
	kstat_named_t dds_log_ingest_rate;
	kstat_named_t dds_log_flush_rate;
	kstat_named_t dds_log_flush_time_rate;
} ddt_kstats_t;

static const ddt_kstats_t ddt_kstats_template = {
	{ "lookup",			KSTAT_DATA_UINT64 },
	{ "lookup_new",			KSTAT_DATA_UINT64 },
	{ "lookup_existing",		KSTAT_DATA_UINT64 },
	{ "lookup_live_hit",		KSTAT_DATA_UINT64 },
	{ "lookup_live_wait",		KSTAT_DATA_UINT64 },
	{ "lookup_live_miss",		KSTAT_DATA_UINT64 },
	{ "lookup_log_hit",		KSTAT_DATA_UINT64 },
	{ "lookup_log_active_hit",	KSTAT_DATA_UINT64 },
	{ "lookup_log_flushing_hit",	KSTAT_DATA_UINT64 },
	{ "lookup_log_miss",		KSTAT_DATA_UINT64 },
	{ "lookup_stored_hit",		KSTAT_DATA_UINT64 },
	{ "lookup_stored_miss",		KSTAT_DATA_UINT64 },
	{ "log_active_entries",		KSTAT_DATA_UINT64 },
	{ "log_flushing_entries",	KSTAT_DATA_UINT64 },
	{ "log_ingest_rate",		KSTAT_DATA_UINT32 },
	{ "log_flush_rate",		KSTAT_DATA_UINT32 },
	{ "log_flush_time_rate",	KSTAT_DATA_UINT32 },
};

#ifdef _KERNEL
#define	_DDT_KSTAT_STAT(ddt, stat) \
	&((ddt_kstats_t *)(ddt)->ddt_ksp->ks_data)->stat.value.ui64
#define	DDT_KSTAT_BUMP(ddt, stat) \
	do { atomic_inc_64(_DDT_KSTAT_STAT(ddt, stat)); } while (0)
#define	DDT_KSTAT_ADD(ddt, stat, val) \
	do { atomic_add_64(_DDT_KSTAT_STAT(ddt, stat), val); } while (0)
#define	DDT_KSTAT_SUB(ddt, stat, val) \
	do { atomic_sub_64(_DDT_KSTAT_STAT(ddt, stat), val); } while (0)
#define	DDT_KSTAT_SET(ddt, stat, val) \
	do { atomic_store_64(_DDT_KSTAT_STAT(ddt, stat), val); } while (0)
#define	DDT_KSTAT_ZERO(ddt, stat) DDT_KSTAT_SET(ddt, stat, 0)
#else
#define	DDT_KSTAT_BUMP(ddt, stat) do {} while (0)
#define	DDT_KSTAT_ADD(ddt, stat, val) do {} while (0)
#define	DDT_KSTAT_SUB(ddt, stat, val) do {} while (0)
#define	DDT_KSTAT_SET(ddt, stat, val) do {} while (0)
#define	DDT_KSTAT_ZERO(ddt, stat) do {} while (0)
#endif /* _KERNEL */


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

	ASSERT3U(ddt->ddt_dir_object, >, 0);

	ddt_object_name(ddt, type, class, name);

	ASSERT3U(*objectp, ==, 0);
	VERIFY0(ddt_ops[type]->ddt_op_create(os, objectp, tx, prehash));
	ASSERT3U(*objectp, !=, 0);

	ASSERT3U(ddt->ddt_version, !=, DDT_VERSION_UNCONFIGURED);

	VERIFY0(zap_add(os, ddt->ddt_dir_object, name, sizeof (uint64_t), 1,
	    objectp, tx));

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

	ASSERT3U(ddt->ddt_dir_object, >, 0);

	ddt_object_name(ddt, type, class, name);

	ASSERT3U(*objectp, !=, 0);
	ASSERT(ddt_histogram_empty(&ddt->ddt_histogram[type][class]));
	VERIFY0(ddt_object_count(ddt, type, class, &count));
	VERIFY0(count);
	VERIFY0(zap_remove(os, ddt->ddt_dir_object, name, tx));
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

	if (ddt->ddt_dir_object == 0) {
		/*
		 * If we're configured but the containing dir doesn't exist
		 * yet, then this object can't possibly exist either.
		 */
		ASSERT3U(ddt->ddt_version, !=, DDT_VERSION_UNCONFIGURED);
		return (SET_ERROR(ENOENT));
	}

	ddt_object_name(ddt, type, class, name);

	error = zap_lookup(ddt->ddt_os, ddt->ddt_dir_object, name,
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
	    dde->dde_phys, DDT_PHYS_SIZE(ddt)));
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

static void
ddt_object_prefetch_all(ddt_t *ddt, ddt_type_t type, ddt_class_t class)
{
	if (!ddt_object_exists(ddt, type, class))
		return;

	ddt_ops[type]->ddt_op_prefetch_all(ddt->ddt_os,
	    ddt->ddt_object[type][class]);
}

static int
ddt_object_update(ddt_t *ddt, ddt_type_t type, ddt_class_t class,
    const ddt_lightweight_entry_t *ddlwe, dmu_tx_t *tx)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_update(ddt->ddt_os,
	    ddt->ddt_object[type][class], &ddlwe->ddlwe_key,
	    &ddlwe->ddlwe_phys, DDT_PHYS_SIZE(ddt), tx));
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
    uint64_t *walk, ddt_lightweight_entry_t *ddlwe)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	int error = ddt_ops[type]->ddt_op_walk(ddt->ddt_os,
	    ddt->ddt_object[type][class], walk, &ddlwe->ddlwe_key,
	    &ddlwe->ddlwe_phys, DDT_PHYS_SIZE(ddt));
	if (error == 0) {
		ddlwe->ddlwe_type = type;
		ddlwe->ddlwe_class = class;
		return (0);
	}
	return (error);
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
ddt_bp_fill(const ddt_univ_phys_t *ddp, ddt_phys_variant_t v,
    blkptr_t *bp, uint64_t txg)
{
	ASSERT3U(txg, !=, 0);
	ASSERT3U(v, <, DDT_PHYS_NONE);
	uint64_t phys_birth;
	const dva_t *dvap;

	if (v == DDT_PHYS_FLAT) {
		phys_birth = ddp->ddp_flat.ddp_phys_birth;
		dvap = ddp->ddp_flat.ddp_dva;
	} else {
		phys_birth = ddp->ddp_trad[v].ddp_phys_birth;
		dvap = ddp->ddp_trad[v].ddp_dva;
	}

	for (int d = 0; d < SPA_DVAS_PER_BP; d++)
		bp->blk_dva[d] = dvap[d];
	BP_SET_BIRTH(bp, txg, phys_birth);
}

/*
 * The bp created via this function may be used for repairs and scrub, but it
 * will be missing the salt / IV required to do a full decrypting read.
 */
void
ddt_bp_create(enum zio_checksum checksum, const ddt_key_t *ddk,
    const ddt_univ_phys_t *ddp, ddt_phys_variant_t v, blkptr_t *bp)
{
	BP_ZERO(bp);

	if (ddp != NULL)
		ddt_bp_fill(ddp, v, bp, ddt_phys_birth(ddp, v));

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
ddt_phys_extend(ddt_univ_phys_t *ddp, ddt_phys_variant_t v, const blkptr_t *bp)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);
	int bp_ndvas = BP_GET_NDVAS(bp);
	int ddp_max_dvas = BP_IS_ENCRYPTED(bp) ?
	    SPA_DVAS_PER_BP - 1 : SPA_DVAS_PER_BP;
	dva_t *dvas = (v == DDT_PHYS_FLAT) ?
	    ddp->ddp_flat.ddp_dva : ddp->ddp_trad[v].ddp_dva;

	int s = 0, d = 0;
	while (s < bp_ndvas && d < ddp_max_dvas) {
		if (DVA_IS_VALID(&dvas[d])) {
			d++;
			continue;
		}
		dvas[d] = bp->blk_dva[s];
		s++; d++;
	}

	/*
	 * If the caller offered us more DVAs than we can fit, something has
	 * gone wrong in their accounting. zio_ddt_write() should never ask for
	 * more than we need.
	 */
	ASSERT3U(s, ==, bp_ndvas);

	if (BP_IS_ENCRYPTED(bp))
		dvas[2] = bp->blk_dva[2];

	if (ddt_phys_birth(ddp, v) == 0) {
		if (v == DDT_PHYS_FLAT)
			ddp->ddp_flat.ddp_phys_birth = BP_GET_BIRTH(bp);
		else
			ddp->ddp_trad[v].ddp_phys_birth = BP_GET_BIRTH(bp);
	}
}

void
ddt_phys_copy(ddt_univ_phys_t *dst, const ddt_univ_phys_t *src,
    ddt_phys_variant_t v)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	if (v == DDT_PHYS_FLAT)
		dst->ddp_flat = src->ddp_flat;
	else
		dst->ddp_trad[v] = src->ddp_trad[v];
}

void
ddt_phys_clear(ddt_univ_phys_t *ddp, ddt_phys_variant_t v)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	if (v == DDT_PHYS_FLAT)
		memset(&ddp->ddp_flat, 0, DDT_FLAT_PHYS_SIZE);
	else
		memset(&ddp->ddp_trad[v], 0, DDT_TRAD_PHYS_SIZE / DDT_PHYS_MAX);
}

static uint64_t
ddt_class_start(void)
{
	uint64_t start = gethrestime_sec();

	if (ddt_prune_artificial_age) {
		/*
		 * debug aide -- simulate a wider distribution
		 * so we don't have to wait for an aged DDT
		 * to test prune.
		 */
		int range = 1 << 21;
		int percent = random_in_range(100);
		if (percent < 50) {
			range = range >> 4;
		} else if (percent > 75) {
			range /= 2;
		}
		start -= random_in_range(range);
	}

	return (start);
}

void
ddt_phys_addref(ddt_univ_phys_t *ddp, ddt_phys_variant_t v)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	if (v == DDT_PHYS_FLAT)
		ddp->ddp_flat.ddp_refcnt++;
	else
		ddp->ddp_trad[v].ddp_refcnt++;
}

uint64_t
ddt_phys_decref(ddt_univ_phys_t *ddp, ddt_phys_variant_t v)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	uint64_t *refcntp;

	if (v == DDT_PHYS_FLAT)
		refcntp = &ddp->ddp_flat.ddp_refcnt;
	else
		refcntp = &ddp->ddp_trad[v].ddp_refcnt;

	ASSERT3U(*refcntp, >, 0);
	(*refcntp)--;
	return (*refcntp);
}

static void
ddt_phys_free(ddt_t *ddt, ddt_key_t *ddk, ddt_univ_phys_t *ddp,
    ddt_phys_variant_t v, uint64_t txg)
{
	blkptr_t blk;

	ddt_bp_create(ddt->ddt_checksum, ddk, ddp, v, &blk);

	/*
	 * We clear the dedup bit so that zio_free() will actually free the
	 * space, rather than just decrementing the refcount in the DDT.
	 */
	BP_SET_DEDUP(&blk, 0);

	ddt_phys_clear(ddp, v);
	zio_free(ddt->ddt_spa, txg, &blk);
}

uint64_t
ddt_phys_birth(const ddt_univ_phys_t *ddp, ddt_phys_variant_t v)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	if (v == DDT_PHYS_FLAT)
		return (ddp->ddp_flat.ddp_phys_birth);
	else
		return (ddp->ddp_trad[v].ddp_phys_birth);
}

int
ddt_phys_dva_count(const ddt_univ_phys_t *ddp, ddt_phys_variant_t v,
    boolean_t encrypted)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	const dva_t *dvas = (v == DDT_PHYS_FLAT) ?
	    ddp->ddp_flat.ddp_dva : ddp->ddp_trad[v].ddp_dva;

	return (DVA_IS_VALID(&dvas[0]) +
	    DVA_IS_VALID(&dvas[1]) +
	    DVA_IS_VALID(&dvas[2]) * !encrypted);
}

ddt_phys_variant_t
ddt_phys_select(const ddt_t *ddt, const ddt_entry_t *dde, const blkptr_t *bp)
{
	if (dde == NULL)
		return (DDT_PHYS_NONE);

	const ddt_univ_phys_t *ddp = dde->dde_phys;

	if (ddt->ddt_flags & DDT_FLAG_FLAT) {
		if (DVA_EQUAL(BP_IDENTITY(bp), &ddp->ddp_flat.ddp_dva[0]) &&
		    BP_GET_BIRTH(bp) == ddp->ddp_flat.ddp_phys_birth) {
			return (DDT_PHYS_FLAT);
		}
	} else /* traditional phys */ {
		for (int p = 0; p < DDT_PHYS_MAX; p++) {
			if (DVA_EQUAL(BP_IDENTITY(bp),
			    &ddp->ddp_trad[p].ddp_dva[0]) &&
			    BP_GET_BIRTH(bp) ==
			    ddp->ddp_trad[p].ddp_phys_birth) {
				return (p);
			}
		}
	}
	return (DDT_PHYS_NONE);
}

uint64_t
ddt_phys_refcnt(const ddt_univ_phys_t *ddp, ddt_phys_variant_t v)
{
	ASSERT3U(v, <, DDT_PHYS_NONE);

	if (v == DDT_PHYS_FLAT)
		return (ddp->ddp_flat.ddp_refcnt);
	else
		return (ddp->ddp_trad[v].ddp_refcnt);
}

uint64_t
ddt_phys_total_refcnt(const ddt_t *ddt, const ddt_univ_phys_t *ddp)
{
	uint64_t refcnt = 0;

	if (ddt->ddt_flags & DDT_FLAG_FLAT)
		refcnt = ddp->ddp_flat.ddp_refcnt;
	else
		for (int v = DDT_PHYS_SINGLE; v <= DDT_PHYS_TRIPLE; v++)
			refcnt += ddp->ddp_trad[v].ddp_refcnt;

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
	ddt_entry_flat_cache = kmem_cache_create("ddt_entry_flat_cache",
	    DDT_ENTRY_FLAT_SIZE, 0, NULL, NULL, NULL, NULL, NULL, 0);
	ddt_entry_trad_cache = kmem_cache_create("ddt_entry_trad_cache",
	    DDT_ENTRY_TRAD_SIZE, 0, NULL, NULL, NULL, NULL, NULL, 0);

	ddt_log_init();
}

void
ddt_fini(void)
{
	ddt_log_fini();

	kmem_cache_destroy(ddt_entry_trad_cache);
	kmem_cache_destroy(ddt_entry_flat_cache);
	kmem_cache_destroy(ddt_cache);
}

static ddt_entry_t *
ddt_alloc(const ddt_t *ddt, const ddt_key_t *ddk)
{
	ddt_entry_t *dde;

	if (ddt->ddt_flags & DDT_FLAG_FLAT) {
		dde = kmem_cache_alloc(ddt_entry_flat_cache, KM_SLEEP);
		memset(dde, 0, DDT_ENTRY_FLAT_SIZE);
	} else {
		dde = kmem_cache_alloc(ddt_entry_trad_cache, KM_SLEEP);
		memset(dde, 0, DDT_ENTRY_TRAD_SIZE);
	}

	cv_init(&dde->dde_cv, NULL, CV_DEFAULT, NULL);

	dde->dde_key = *ddk;

	return (dde);
}

void
ddt_alloc_entry_io(ddt_entry_t *dde)
{
	if (dde->dde_io != NULL)
		return;

	dde->dde_io = kmem_zalloc(sizeof (ddt_entry_io_t), KM_SLEEP);
}

static void
ddt_free(const ddt_t *ddt, ddt_entry_t *dde)
{
	if (dde->dde_io != NULL) {
		for (int p = 0; p < DDT_NPHYS(ddt); p++)
			ASSERT3P(dde->dde_io->dde_lead_zio[p], ==, NULL);

		if (dde->dde_io->dde_repair_abd != NULL)
			abd_free(dde->dde_io->dde_repair_abd);

		kmem_free(dde->dde_io, sizeof (ddt_entry_io_t));
	}

	cv_destroy(&dde->dde_cv);
	kmem_cache_free(ddt->ddt_flags & DDT_FLAG_FLAT ?
	    ddt_entry_flat_cache : ddt_entry_trad_cache, dde);
}

void
ddt_remove(ddt_t *ddt, ddt_entry_t *dde)
{
	ASSERT(MUTEX_HELD(&ddt->ddt_lock));

	/* Entry is still in the log, so charge the entry back to it */
	if (dde->dde_flags & DDE_FLAG_LOGGED) {
		ddt_lightweight_entry_t ddlwe;
		DDT_ENTRY_TO_LIGHTWEIGHT(ddt, dde, &ddlwe);
		ddt_histogram_add_entry(ddt, &ddt->ddt_log_histogram, &ddlwe);
	}

	avl_remove(&ddt->ddt_tree, dde);
	ddt_free(ddt, dde);
}

static boolean_t
ddt_special_over_quota(spa_t *spa, metaslab_class_t *mc)
{
	if (mc != NULL && metaslab_class_get_space(mc) > 0) {
		/* Over quota if allocating outside of this special class */
		if (spa_syncing_txg(spa) <= spa->spa_dedup_class_full_txg +
		    dedup_class_wait_txgs) {
			/* Waiting for some deferred frees to be processed */
			return (B_TRUE);
		}

		/*
		 * We're considered over quota when we hit 85% full, or for
		 * larger drives, when there is less than 8GB free.
		 */
		uint64_t allocated = metaslab_class_get_alloc(mc);
		uint64_t capacity = metaslab_class_get_space(mc);
		uint64_t limit = MAX(capacity * 85 / 100,
		    (capacity > (1LL<<33)) ? capacity - (1LL<<33) : 0);

		return (allocated >= limit);
	}
	return (B_FALSE);
}

/*
 * Check if the DDT is over its quota.  This can be due to a few conditions:
 *   1. 'dedup_table_quota' property is not 0 (none) and the dedup dsize
 *       exceeds this limit
 *
 *   2. 'dedup_table_quota' property is set to automatic and
 *      a. the dedup or special allocation class could not satisfy a DDT
 *         allocation in a recent transaction
 *      b. the dedup or special allocation class has exceeded its 85% limit
 */
static boolean_t
ddt_over_quota(spa_t *spa)
{
	if (spa->spa_dedup_table_quota == 0)
		return (B_FALSE);

	if (spa->spa_dedup_table_quota != UINT64_MAX)
		return (ddt_get_ddt_dsize(spa) > spa->spa_dedup_table_quota);

	/*
	 * For automatic quota, table size is limited by dedup or special class
	 */
	if (ddt_special_over_quota(spa, spa_dedup_class(spa)))
		return (B_TRUE);
	else if (spa_special_has_ddt(spa) &&
	    ddt_special_over_quota(spa, spa_special_class(spa)))
		return (B_TRUE);

	return (B_FALSE);
}

void
ddt_prefetch_all(spa_t *spa)
{
	/*
	 * Load all DDT entries for each type/class combination. This is
	 * indended to perform a prefetch on all such blocks. For the same
	 * reason that ddt_prefetch isn't locked, this is also not locked.
	 */
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (!ddt)
			continue;

		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				ddt_object_prefetch_all(ddt, type, class);
			}
		}
	}
}

static int ddt_configure(ddt_t *ddt, boolean_t new);

/*
 * If the BP passed to ddt_lookup has valid DVAs, then we need to compare them
 * to the ones in the entry. If they're different, then the passed-in BP is
 * from a previous generation of this entry (ie was previously pruned) and we
 * have to act like the entry doesn't exist at all.
 *
 * This should only happen during a lookup to free the block (zio_ddt_free()).
 *
 * XXX this is similar in spirit to ddt_phys_select(), maybe can combine
 *       -- robn, 2024-02-09
 */
static boolean_t
ddt_entry_lookup_is_valid(ddt_t *ddt, const blkptr_t *bp, ddt_entry_t *dde)
{
	/* If the BP has no DVAs, then this entry is good */
	uint_t ndvas = BP_GET_NDVAS(bp);
	if (ndvas == 0)
		return (B_TRUE);

	/*
	 * Only checking the phys for the copies. For flat, there's only one;
	 * for trad it'll be the one that has the matching set of DVAs.
	 */
	const dva_t *dvas = (ddt->ddt_flags & DDT_FLAG_FLAT) ?
	    dde->dde_phys->ddp_flat.ddp_dva :
	    dde->dde_phys->ddp_trad[ndvas].ddp_dva;

	/*
	 * Compare entry DVAs with the BP. They should all be there, but
	 * there's not really anything we can do if its only partial anyway,
	 * that's an error somewhere else, maybe long ago.
	 */
	uint_t d;
	for (d = 0; d < ndvas; d++)
		if (!DVA_EQUAL(&dvas[d], &bp->blk_dva[d]))
			return (B_FALSE);
	ASSERT3U(d, ==, ndvas);

	return (B_TRUE);
}

ddt_entry_t *
ddt_lookup(ddt_t *ddt, const blkptr_t *bp)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_key_t search;
	ddt_entry_t *dde;
	ddt_type_t type;
	ddt_class_t class;
	avl_index_t where;
	int error;

	ASSERT(MUTEX_HELD(&ddt->ddt_lock));

	if (ddt->ddt_version == DDT_VERSION_UNCONFIGURED) {
		/*
		 * This is the first use of this DDT since the pool was
		 * created; finish getting it ready for use.
		 */
		VERIFY0(ddt_configure(ddt, B_TRUE));
		ASSERT3U(ddt->ddt_version, !=, DDT_VERSION_UNCONFIGURED);
	}

	DDT_KSTAT_BUMP(ddt, dds_lookup);

	ddt_key_fill(&search, bp);

	/* Find an existing live entry */
	dde = avl_find(&ddt->ddt_tree, &search, &where);
	if (dde != NULL) {
		/* If we went over quota, act like we didn't find it */
		if (dde->dde_flags & DDE_FLAG_OVERQUOTA)
			return (NULL);

		/* If it's already loaded, we can just return it. */
		DDT_KSTAT_BUMP(ddt, dds_lookup_live_hit);
		if (dde->dde_flags & DDE_FLAG_LOADED) {
			if (ddt_entry_lookup_is_valid(ddt, bp, dde))
				return (dde);
			return (NULL);
		}

		/* Someone else is loading it, wait for it. */
		dde->dde_waiters++;
		DDT_KSTAT_BUMP(ddt, dds_lookup_live_wait);
		while (!(dde->dde_flags & DDE_FLAG_LOADED))
			cv_wait(&dde->dde_cv, &ddt->ddt_lock);
		dde->dde_waiters--;

		/* Loaded but over quota, forget we were ever here */
		if (dde->dde_flags & DDE_FLAG_OVERQUOTA) {
			if (dde->dde_waiters == 0) {
				avl_remove(&ddt->ddt_tree, dde);
				ddt_free(ddt, dde);
			}
			return (NULL);
		}

		DDT_KSTAT_BUMP(ddt, dds_lookup_existing);

		/* Make sure the loaded entry matches the BP */
		if (ddt_entry_lookup_is_valid(ddt, bp, dde))
			return (dde);
		return (NULL);
	} else
		DDT_KSTAT_BUMP(ddt, dds_lookup_live_miss);

	/* Time to make a new entry. */
	dde = ddt_alloc(ddt, &search);

	/* Record the time this class was created (used by ddt prune) */
	if (ddt->ddt_flags & DDT_FLAG_FLAT)
		dde->dde_phys->ddp_flat.ddp_class_start = ddt_class_start();

	avl_insert(&ddt->ddt_tree, dde, where);

	/* If its in the log tree, we can "load" it from there */
	if (ddt->ddt_flags & DDT_FLAG_LOG) {
		ddt_lightweight_entry_t ddlwe;

		if (ddt_log_find_key(ddt, &search, &ddlwe)) {
			/*
			 * See if we have the key first, and if so, set up
			 * the entry.
			 */
			dde->dde_type = ddlwe.ddlwe_type;
			dde->dde_class = ddlwe.ddlwe_class;
			memcpy(dde->dde_phys, &ddlwe.ddlwe_phys,
			    DDT_PHYS_SIZE(ddt));
			/* Whatever we found isn't valid for this BP, eject */
			if (!ddt_entry_lookup_is_valid(ddt, bp, dde)) {
				avl_remove(&ddt->ddt_tree, dde);
				ddt_free(ddt, dde);
				return (NULL);
			}

			/* Remove it and count it */
			if (ddt_log_remove_key(ddt,
			    ddt->ddt_log_active, &search)) {
				DDT_KSTAT_BUMP(ddt, dds_lookup_log_active_hit);
			} else {
				VERIFY(ddt_log_remove_key(ddt,
				    ddt->ddt_log_flushing, &search));
				DDT_KSTAT_BUMP(ddt,
				    dds_lookup_log_flushing_hit);
			}

			dde->dde_flags = DDE_FLAG_LOADED | DDE_FLAG_LOGGED;

			DDT_KSTAT_BUMP(ddt, dds_lookup_log_hit);
			DDT_KSTAT_BUMP(ddt, dds_lookup_existing);

			return (dde);
		}

		DDT_KSTAT_BUMP(ddt, dds_lookup_log_miss);
	}

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

	boolean_t valid = B_TRUE;

	if (dde->dde_type == DDT_TYPES &&
	    dde->dde_class == DDT_CLASSES &&
	    ddt_over_quota(spa)) {
		/* Over quota. If no one is waiting, clean up right now. */
		if (dde->dde_waiters == 0) {
			avl_remove(&ddt->ddt_tree, dde);
			ddt_free(ddt, dde);
			return (NULL);
		}

		/* Flag cleanup required */
		dde->dde_flags |= DDE_FLAG_OVERQUOTA;
	} else if (error == 0) {
		/*
		 * If what we loaded is no good for this BP and there's no one
		 * waiting for it, we can just remove it and get out. If its no
		 * good but there are waiters, we have to leave it, because we
		 * don't know what they want. If its not needed we'll end up
		 * taking an entry log/sync, but it can only happen if more
		 * than one previous version of this block is being deleted at
		 * the same time. This is extremely unlikely to happen and not
		 * worth the effort to deal with without taking an entry
		 * update.
		 */
		valid = ddt_entry_lookup_is_valid(ddt, bp, dde);
		if (!valid && dde->dde_waiters == 0) {
			avl_remove(&ddt->ddt_tree, dde);
			ddt_free(ddt, dde);
			return (NULL);
		}

		DDT_KSTAT_BUMP(ddt, dds_lookup_stored_hit);
		DDT_KSTAT_BUMP(ddt, dds_lookup_existing);

		/*
		 * The histograms only track inactive (stored or logged) blocks.
		 * We've just put an entry onto the live list, so we need to
		 * remove its counts. When its synced back, it'll be re-added
		 * to the right one.
		 *
		 * We only do this when we successfully found it in the store.
		 * error == ENOENT means this is a new entry, and so its already
		 * not counted.
		 */
		ddt_histogram_t *ddh =
		    &ddt->ddt_histogram[dde->dde_type][dde->dde_class];

		ddt_lightweight_entry_t ddlwe;
		DDT_ENTRY_TO_LIGHTWEIGHT(ddt, dde, &ddlwe);
		ddt_histogram_sub_entry(ddt, ddh, &ddlwe);
	} else {
		DDT_KSTAT_BUMP(ddt, dds_lookup_stored_miss);
		DDT_KSTAT_BUMP(ddt, dds_lookup_new);
	}

	/* Entry loaded, everyone can proceed now */
	dde->dde_flags |= DDE_FLAG_LOADED;
	cv_broadcast(&dde->dde_cv);

	if ((dde->dde_flags & DDE_FLAG_OVERQUOTA) || !valid)
		return (NULL);

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
 * ddt_key_t comparison. Any struct wanting to make use of this function must
 * have the key as the first element. Casts it to N uint64_ts, and checks until
 * we find there's a difference. This is intended to match how ddt_zap.c drives
 * the ZAPs (first uint64_t as the key prehash), which will minimise the number
 * of ZAP blocks touched when flushing logged entries from an AVL walk. This is
 * not an invariant for this function though, should you wish to change it.
 */
int
ddt_key_compare(const void *x1, const void *x2)
{
	const uint64_t *k1 = (const uint64_t *)x1;
	const uint64_t *k2 = (const uint64_t *)x2;

	int cmp;
	for (int i = 0; i < (sizeof (ddt_key_t) / sizeof (uint64_t)); i++)
		if (likely((cmp = TREE_CMP(k1[i], k2[i])) != 0))
			return (cmp);

	return (0);
}

/* Create the containing dir for this DDT and bump the feature count */
static void
ddt_create_dir(ddt_t *ddt, dmu_tx_t *tx)
{
	ASSERT3U(ddt->ddt_dir_object, ==, 0);
	ASSERT3U(ddt->ddt_version, ==, DDT_VERSION_FDT);

	char name[DDT_NAMELEN];
	snprintf(name, DDT_NAMELEN, DMU_POOL_DDT_DIR,
	    zio_checksum_table[ddt->ddt_checksum].ci_name);

	ddt->ddt_dir_object = zap_create_link(ddt->ddt_os,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT, name, tx);

	VERIFY0(zap_add(ddt->ddt_os, ddt->ddt_dir_object, DDT_DIR_VERSION,
	    sizeof (uint64_t), 1, &ddt->ddt_version, tx));
	VERIFY0(zap_add(ddt->ddt_os, ddt->ddt_dir_object, DDT_DIR_FLAGS,
	    sizeof (uint64_t), 1, &ddt->ddt_flags, tx));

	spa_feature_incr(ddt->ddt_spa, SPA_FEATURE_FAST_DEDUP, tx);
}

/* Destroy the containing dir and deactivate the feature */
static void
ddt_destroy_dir(ddt_t *ddt, dmu_tx_t *tx)
{
	ASSERT3U(ddt->ddt_dir_object, !=, 0);
	ASSERT3U(ddt->ddt_dir_object, !=, DMU_POOL_DIRECTORY_OBJECT);
	ASSERT3U(ddt->ddt_version, ==, DDT_VERSION_FDT);

	char name[DDT_NAMELEN];
	snprintf(name, DDT_NAMELEN, DMU_POOL_DDT_DIR,
	    zio_checksum_table[ddt->ddt_checksum].ci_name);

	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			ASSERT(!ddt_object_exists(ddt, type, class));
		}
	}

	ddt_log_destroy(ddt, tx);

	uint64_t count;
	ASSERT0(zap_count(ddt->ddt_os, ddt->ddt_dir_object, &count));
	ASSERT0(zap_contains(ddt->ddt_os, ddt->ddt_dir_object,
	    DDT_DIR_VERSION));
	ASSERT0(zap_contains(ddt->ddt_os, ddt->ddt_dir_object, DDT_DIR_FLAGS));
	ASSERT3U(count, ==, 2);

	VERIFY0(zap_remove(ddt->ddt_os, DMU_POOL_DIRECTORY_OBJECT, name, tx));
	VERIFY0(zap_destroy(ddt->ddt_os, ddt->ddt_dir_object, tx));

	ddt->ddt_dir_object = 0;

	spa_feature_decr(ddt->ddt_spa, SPA_FEATURE_FAST_DEDUP, tx);
}

/*
 * Determine, flags and on-disk layout from what's already stored. If there's
 * nothing stored, then if new is false, returns ENOENT, and if true, selects
 * based on pool config.
 */
static int
ddt_configure(ddt_t *ddt, boolean_t new)
{
	spa_t *spa = ddt->ddt_spa;
	char name[DDT_NAMELEN];
	int error;

	ASSERT3U(spa_load_state(spa), !=, SPA_LOAD_CREATE);

	boolean_t fdt_enabled =
	    spa_feature_is_enabled(spa, SPA_FEATURE_FAST_DEDUP);
	boolean_t fdt_active =
	    spa_feature_is_active(spa, SPA_FEATURE_FAST_DEDUP);

	/*
	 * First, look for the global DDT stats object. If its not there, then
	 * there's never been a DDT written before ever, and we know we're
	 * starting from scratch.
	 */
	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_DDT_STATS, sizeof (uint64_t), 1,
	    &spa->spa_ddt_stat_object);
	if (error != 0) {
		if (error != ENOENT)
			return (error);
		goto not_found;
	}

	if (fdt_active) {
		/*
		 * Now look for a DDT directory. If it exists, then it has
		 * everything we need.
		 */
		snprintf(name, DDT_NAMELEN, DMU_POOL_DDT_DIR,
		    zio_checksum_table[ddt->ddt_checksum].ci_name);

		error = zap_lookup(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, name, sizeof (uint64_t), 1,
		    &ddt->ddt_dir_object);
		if (error == 0) {
			ASSERT3U(spa->spa_meta_objset, ==, ddt->ddt_os);

			error = zap_lookup(ddt->ddt_os, ddt->ddt_dir_object,
			    DDT_DIR_VERSION, sizeof (uint64_t), 1,
			    &ddt->ddt_version);
			if (error != 0)
				return (error);

			error = zap_lookup(ddt->ddt_os, ddt->ddt_dir_object,
			    DDT_DIR_FLAGS, sizeof (uint64_t), 1,
			    &ddt->ddt_flags);
			if (error != 0)
				return (error);

			if (ddt->ddt_version != DDT_VERSION_FDT) {
				zfs_dbgmsg("ddt_configure: spa=%s ddt_dir=%s "
				    "unknown version %llu", spa_name(spa),
				    name, (u_longlong_t)ddt->ddt_version);
				return (SET_ERROR(EINVAL));
			}

			if ((ddt->ddt_flags & ~DDT_FLAG_MASK) != 0) {
				zfs_dbgmsg("ddt_configure: spa=%s ddt_dir=%s "
				    "version=%llu unknown flags %llx",
				    spa_name(spa), name,
				    (u_longlong_t)ddt->ddt_flags,
				    (u_longlong_t)ddt->ddt_version);
				return (SET_ERROR(EINVAL));
			}

			return (0);
		}
		if (error != ENOENT)
			return (error);
	}

	/* Any object in the root indicates a traditional setup. */
	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			ddt_object_name(ddt, type, class, name);
			uint64_t obj;
			error = zap_lookup(spa->spa_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT, name, sizeof (uint64_t),
			    1, &obj);
			if (error == ENOENT)
				continue;
			if (error != 0)
				return (error);

			ddt->ddt_version = DDT_VERSION_LEGACY;
			ddt->ddt_flags = ddt_version_flags[ddt->ddt_version];
			ddt->ddt_dir_object = DMU_POOL_DIRECTORY_OBJECT;

			return (0);
		}
	}

not_found:
	if (!new)
		return (SET_ERROR(ENOENT));

	/* Nothing on disk, so set up for the best version we can */
	if (fdt_enabled) {
		ddt->ddt_version = DDT_VERSION_FDT;
		ddt->ddt_flags = ddt_version_flags[ddt->ddt_version];
		ddt->ddt_dir_object = 0; /* create on first use */
	} else {
		ddt->ddt_version = DDT_VERSION_LEGACY;
		ddt->ddt_flags = ddt_version_flags[ddt->ddt_version];
		ddt->ddt_dir_object = DMU_POOL_DIRECTORY_OBJECT;
	}

	return (0);
}

static void
ddt_table_alloc_kstats(ddt_t *ddt)
{
	char *mod = kmem_asprintf("zfs/%s", spa_name(ddt->ddt_spa));
	char *name = kmem_asprintf("ddt_stats_%s",
	    zio_checksum_table[ddt->ddt_checksum].ci_name);

	ddt->ddt_ksp = kstat_create(mod, 0, name, "misc", KSTAT_TYPE_NAMED,
	    sizeof (ddt_kstats_t) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (ddt->ddt_ksp != NULL) {
		ddt_kstats_t *dds = kmem_alloc(sizeof (ddt_kstats_t), KM_SLEEP);
		memcpy(dds, &ddt_kstats_template, sizeof (ddt_kstats_t));
		ddt->ddt_ksp->ks_data = dds;
		kstat_install(ddt->ddt_ksp);
	}

	kmem_strfree(name);
	kmem_strfree(mod);
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
	ddt->ddt_version = DDT_VERSION_UNCONFIGURED;

	ddt_log_alloc(ddt);
	ddt_table_alloc_kstats(ddt);

	return (ddt);
}

static void
ddt_table_free(ddt_t *ddt)
{
	if (ddt->ddt_ksp != NULL) {
		kmem_free(ddt->ddt_ksp->ks_data, sizeof (ddt_kstats_t));
		ddt->ddt_ksp->ks_data = NULL;
		kstat_delete(ddt->ddt_ksp);
	}

	ddt_log_free(ddt);
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
		error = ddt_configure(ddt, B_FALSE);
		if (error == ENOENT)
			continue;
		if (error != 0)
			return (error);

		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0; class < DDT_CLASSES;
			    class++) {
				error = ddt_object_load(ddt, type, class);
				if (error != 0 && error != ENOENT)
					return (error);
			}
		}

		error = ddt_log_load(ddt);
		if (error != 0 && error != ENOENT)
			return (error);

		DDT_KSTAT_SET(ddt, dds_log_active_entries,
		    avl_numnodes(&ddt->ddt_log_active->ddl_tree));
		DDT_KSTAT_SET(ddt, dds_log_flushing_entries,
		    avl_numnodes(&ddt->ddt_log_flushing->ddl_tree));

		/*
		 * Seed the cached histograms.
		 */
		memcpy(&ddt->ddt_histogram_cache, ddt->ddt_histogram,
		    sizeof (ddt->ddt_histogram));
	}

	spa->spa_dedup_dspace = ~0ULL;
	spa->spa_dedup_dsize = ~0ULL;

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

	dde = ddt_alloc(ddt, &ddk);
	ddt_alloc_entry_io(dde);

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

	memset(dde->dde_phys, 0, DDT_PHYS_SIZE(ddt));

	return (dde);
}

void
ddt_repair_done(ddt_t *ddt, ddt_entry_t *dde)
{
	avl_index_t where;

	ddt_enter(ddt);

	if (dde->dde_io->dde_repair_abd != NULL &&
	    spa_writeable(ddt->ddt_spa) &&
	    avl_find(&ddt->ddt_repair_tree, dde, &where) == NULL)
		avl_insert(&ddt->ddt_repair_tree, dde, where);
	else
		ddt_free(ddt, dde);

	ddt_exit(ddt);
}

static void
ddt_repair_entry_done(zio_t *zio)
{
	ddt_t *ddt = ddt_select(zio->io_spa, zio->io_bp);
	ddt_entry_t *rdde = zio->io_private;

	ddt_free(ddt, rdde);
}

static void
ddt_repair_entry(ddt_t *ddt, ddt_entry_t *dde, ddt_entry_t *rdde, zio_t *rio)
{
	ddt_key_t *ddk = &dde->dde_key;
	ddt_key_t *rddk = &rdde->dde_key;
	zio_t *zio;
	blkptr_t blk;

	zio = zio_null(rio, rio->io_spa, NULL,
	    ddt_repair_entry_done, rdde, rio->io_flags);

	for (int p = 0; p < DDT_NPHYS(ddt); p++) {
		ddt_univ_phys_t *ddp = dde->dde_phys;
		ddt_univ_phys_t *rddp = rdde->dde_phys;
		ddt_phys_variant_t v = DDT_PHYS_VARIANT(ddt, p);
		uint64_t phys_birth = ddt_phys_birth(ddp, v);
		const dva_t *dvas, *rdvas;

		if (ddt->ddt_flags & DDT_FLAG_FLAT) {
			dvas = ddp->ddp_flat.ddp_dva;
			rdvas = rddp->ddp_flat.ddp_dva;
		} else {
			dvas = ddp->ddp_trad[p].ddp_dva;
			rdvas = rddp->ddp_trad[p].ddp_dva;
		}

		if (phys_birth == 0 ||
		    phys_birth != ddt_phys_birth(rddp, v) ||
		    memcmp(dvas, rdvas, sizeof (dva_t) * SPA_DVAS_PER_BP))
			continue;

		ddt_bp_create(ddt->ddt_checksum, ddk, ddp, v, &blk);
		zio_nowait(zio_rewrite(zio, zio->io_spa, 0, &blk,
		    rdde->dde_io->dde_repair_abd, DDK_GET_PSIZE(rddk),
		    NULL, NULL, ZIO_PRIORITY_SYNC_WRITE,
		    ZIO_DDT_CHILD_FLAGS(zio), NULL));
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
		ddt_bp_create(ddt->ddt_checksum, &rdde->dde_key, NULL,
		    DDT_PHYS_NONE, &blk);
		dde = ddt_repair_start(ddt, &blk);
		ddt_repair_entry(ddt, dde, rdde, rio);
		ddt_repair_done(ddt, dde);
		ddt_enter(ddt);
	}
	ddt_exit(ddt);
}

static void
ddt_sync_update_stats(ddt_t *ddt, dmu_tx_t *tx)
{
	/*
	 * Count all the entries stored for each type/class, and updates the
	 * stats within (ddt_object_sync()). If there's no entries for the
	 * type/class, the whole object is removed. If all objects for the DDT
	 * are removed, its containing dir is removed, effectively resetting
	 * the entire DDT to an empty slate.
	 */
	uint64_t count = 0;
	for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
		uint64_t add, tcount = 0;
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			if (ddt_object_exists(ddt, type, class)) {
				ddt_object_sync(ddt, type, class, tx);
				VERIFY0(ddt_object_count(ddt, type, class,
				    &add));
				tcount += add;
			}
		}
		for (ddt_class_t class = 0; class < DDT_CLASSES; class++) {
			if (tcount == 0 && ddt_object_exists(ddt, type, class))
				ddt_object_destroy(ddt, type, class, tx);
		}
		count += tcount;
	}

	if (ddt->ddt_flags & DDT_FLAG_LOG) {
		/* Include logged entries in the total count */
		count += avl_numnodes(&ddt->ddt_log_active->ddl_tree);
		count += avl_numnodes(&ddt->ddt_log_flushing->ddl_tree);
	}

	if (count == 0) {
		/*
		 * No entries left on the DDT, so reset the version for next
		 * time. This allows us to handle the feature being changed
		 * since the DDT was originally created. New entries should get
		 * whatever the feature currently demands.
		 */
		if (ddt->ddt_version == DDT_VERSION_FDT)
			ddt_destroy_dir(ddt, tx);

		ddt->ddt_version = DDT_VERSION_UNCONFIGURED;
		ddt->ddt_flags = 0;
	}

	memcpy(&ddt->ddt_histogram_cache, ddt->ddt_histogram,
	    sizeof (ddt->ddt_histogram));
	ddt->ddt_spa->spa_dedup_dspace = ~0ULL;
	ddt->ddt_spa->spa_dedup_dsize = ~0ULL;
}

static void
ddt_sync_scan_entry(ddt_t *ddt, ddt_lightweight_entry_t *ddlwe, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ddt->ddt_spa->spa_dsl_pool;

	/*
	 * Compute the target class, so we can decide whether or not to inform
	 * the scrub traversal (below). Note that we don't store this in the
	 * entry, as it might change multiple times before finally being
	 * committed (if we're logging). Instead, we recompute it in
	 * ddt_sync_entry().
	 */
	uint64_t refcnt = ddt_phys_total_refcnt(ddt, &ddlwe->ddlwe_phys);
	ddt_class_t nclass =
	    (refcnt > 1) ? DDT_CLASS_DUPLICATE : DDT_CLASS_UNIQUE;

	/*
	 * If the class changes, the order that we scan this bp changes. If it
	 * decreases, we could miss it, so scan it right now. (This covers both
	 * class changing while we are doing ddt_walk(), and when we are
	 * traversing.)
	 *
	 * We also do this when the refcnt goes to zero, because that change is
	 * only in the log so far; the blocks on disk won't be freed until
	 * the log is flushed, and the refcnt might increase before that. If it
	 * does, then we could miss it in the same way.
	 */
	if (refcnt == 0 || nclass < ddlwe->ddlwe_class)
		dsl_scan_ddt_entry(dp->dp_scan, ddt->ddt_checksum, ddt,
		    ddlwe, tx);
}

static void
ddt_sync_flush_entry(ddt_t *ddt, ddt_lightweight_entry_t *ddlwe,
    ddt_type_t otype, ddt_class_t oclass, dmu_tx_t *tx)
{
	ddt_key_t *ddk = &ddlwe->ddlwe_key;
	ddt_type_t ntype = DDT_TYPE_DEFAULT;
	uint64_t refcnt = 0;

	/*
	 * Compute the total refcnt. Along the way, issue frees for any DVAs
	 * we no longer want.
	 */
	for (int p = 0; p < DDT_NPHYS(ddt); p++) {
		ddt_univ_phys_t *ddp = &ddlwe->ddlwe_phys;
		ddt_phys_variant_t v = DDT_PHYS_VARIANT(ddt, p);
		uint64_t phys_refcnt = ddt_phys_refcnt(ddp, v);

		if (ddt_phys_birth(ddp, v) == 0) {
			ASSERT0(phys_refcnt);
			continue;
		}
		if (DDT_PHYS_IS_DITTO(ddt, p)) {
			/*
			 * We don't want to keep any obsolete slots (eg ditto),
			 * regardless of their refcount, but we don't want to
			 * leak them either. So, free them.
			 */
			ddt_phys_free(ddt, ddk, ddp, v, tx->tx_txg);
			continue;
		}
		if (phys_refcnt == 0)
			/* No remaining references, free it! */
			ddt_phys_free(ddt, ddk, ddp, v, tx->tx_txg);
		refcnt += phys_refcnt;
	}

	/* Select the best class for the entry. */
	ddt_class_t nclass =
	    (refcnt > 1) ? DDT_CLASS_DUPLICATE : DDT_CLASS_UNIQUE;

	/*
	 * If an existing entry changed type or class, or its refcount reached
	 * zero, delete it from the DDT object
	 */
	if (otype != DDT_TYPES &&
	    (otype != ntype || oclass != nclass || refcnt == 0)) {
		VERIFY0(ddt_object_remove(ddt, otype, oclass, ddk, tx));
		ASSERT(ddt_object_contains(ddt, otype, oclass, ddk) == ENOENT);
	}

	/*
	 * Add or update the entry
	 */
	if (refcnt != 0) {
		ddt_histogram_t *ddh =
		    &ddt->ddt_histogram[ntype][nclass];

		ddt_histogram_add_entry(ddt, ddh, ddlwe);

		if (!ddt_object_exists(ddt, ntype, nclass))
			ddt_object_create(ddt, ntype, nclass, tx);
		VERIFY0(ddt_object_update(ddt, ntype, nclass, ddlwe, tx));
	}
}

/* Calculate an exponential weighted moving average, lower limited to zero */
static inline int32_t
_ewma(int32_t val, int32_t prev, uint32_t weight)
{
	ASSERT3U(val, >=, 0);
	ASSERT3U(prev, >=, 0);
	const int32_t new =
	    MAX(0, prev + (val-prev) / (int32_t)MAX(weight, 1));
	ASSERT3U(new, >=, 0);
	return (new);
}

/* Returns true if done for this txg */
static boolean_t
ddt_sync_flush_log_incremental(ddt_t *ddt, dmu_tx_t *tx)
{
	if (ddt->ddt_flush_pass == 0) {
		if (spa_sync_pass(ddt->ddt_spa) == 1) {
			/* First run this txg, get set up */
			ddt->ddt_flush_start = gethrtime();
			ddt->ddt_flush_count = 0;

			/*
			 * How many entries we need to flush. We want to at
			 * least match the ingest rate.
			 */
			ddt->ddt_flush_min = MAX(
			    ddt->ddt_log_ingest_rate,
			    zfs_dedup_log_flush_entries_min);

			/*
			 * If we've been asked to flush everything in a hurry,
			 * try to dump as much as possible on this txg. In
			 * this case we're only limited by time, not amount.
			 */
			if (ddt->ddt_flush_force_txg > 0)
				ddt->ddt_flush_min =
				    MAX(ddt->ddt_flush_min, avl_numnodes(
				    &ddt->ddt_log_flushing->ddl_tree));
		} else {
			/* We already decided we're done for this txg */
			return (B_FALSE);
		}
	} else if (ddt->ddt_flush_pass == spa_sync_pass(ddt->ddt_spa)) {
		/*
		 * We already did some flushing on this pass, skip it. This
		 * happens when dsl_process_async_destroys() runs during a scan
		 * (on pass 1) and does an additional ddt_sync() to update
		 * freed blocks.
		 */
		return (B_FALSE);
	}

	if (spa_sync_pass(ddt->ddt_spa) >
	    MAX(zfs_dedup_log_flush_passes_max, 1)) {
		/* Too many passes this txg, defer until next. */
		ddt->ddt_flush_pass = 0;
		return (B_TRUE);
	}

	if (avl_is_empty(&ddt->ddt_log_flushing->ddl_tree)) {
		/* Nothing to flush, done for this txg. */
		ddt->ddt_flush_pass = 0;
		return (B_TRUE);
	}

	uint64_t target_time = txg_sync_waiting(ddt->ddt_spa->spa_dsl_pool) ?
	    MIN(MSEC2NSEC(zfs_dedup_log_flush_min_time_ms),
	    SEC2NSEC(zfs_txg_timeout)) : SEC2NSEC(zfs_txg_timeout);

	uint64_t elapsed_time = gethrtime() - ddt->ddt_flush_start;

	if (elapsed_time >= target_time) {
		/* Too long since we started, done for this txg. */
		ddt->ddt_flush_pass = 0;
		return (B_TRUE);
	}

	ddt->ddt_flush_pass++;
	ASSERT3U(spa_sync_pass(ddt->ddt_spa), ==, ddt->ddt_flush_pass);

	/*
	 * Estimate how much time we'll need to flush the remaining entries
	 * based on how long it normally takes.
	 */
	uint32_t want_time;
	if (ddt->ddt_flush_pass == 1) {
		/* First pass, use the average time/entries */
		if (ddt->ddt_log_flush_rate == 0)
			/* Zero rate, just assume the whole time */
			want_time = target_time;
		else
			want_time = ddt->ddt_flush_min *
			    ddt->ddt_log_flush_time_rate /
			    ddt->ddt_log_flush_rate;
	} else {
		/* Later pass, calculate from this txg so far */
		want_time = ddt->ddt_flush_min *
		    elapsed_time / ddt->ddt_flush_count;
	}

	/* Figure out how much time we have left */
	uint32_t remain_time = target_time - elapsed_time;

	/* Smear the remaining entries over the remaining passes. */
	uint32_t nentries = ddt->ddt_flush_min /
	    (MAX(1, zfs_dedup_log_flush_passes_max) + 1 - ddt->ddt_flush_pass);
	if (want_time > remain_time) {
		/*
		 * We're behind; try to catch up a bit by doubling the amount
		 * this pass. If we're behind that means we're in a later
		 * pass and likely have most of the remaining time to
		 * ourselves. If we're in the last couple of passes, then
		 * doubling might just take us over the timeout, but probably
		 * not be much, and it stops us falling behind. If we're
		 * in the middle passes, there'll be more to do, but it
		 * might just help us catch up a bit and we'll recalculate on
		 * the next pass anyway.
		 */
		nentries = MIN(ddt->ddt_flush_min, nentries*2);
	}

	ddt_lightweight_entry_t ddlwe;
	uint32_t count = 0;
	while (ddt_log_take_first(ddt, ddt->ddt_log_flushing, &ddlwe)) {
		ddt_sync_flush_entry(ddt, &ddlwe,
		    ddlwe.ddlwe_type, ddlwe.ddlwe_class, tx);

		/* End this pass if we've synced as much as we need to. */
		if (++count >= nentries)
			break;
	}
	ddt->ddt_flush_count += count;
	ddt->ddt_flush_min -= count;

	if (avl_is_empty(&ddt->ddt_log_flushing->ddl_tree)) {
		/* We emptied it, so truncate on-disk */
		DDT_KSTAT_ZERO(ddt, dds_log_flushing_entries);
		ddt_log_truncate(ddt, tx);
		/* No more passes needed this txg */
		ddt->ddt_flush_pass = 0;
	} else {
		/* More to do next time, save checkpoint */
		DDT_KSTAT_SUB(ddt, dds_log_flushing_entries, count);
		ddt_log_checkpoint(ddt, &ddlwe, tx);
	}

	ddt_sync_update_stats(ddt, tx);

	return (ddt->ddt_flush_pass == 0);
}

static inline void
ddt_flush_force_update_txg(ddt_t *ddt, uint64_t txg)
{
	/*
	 * If we're not forcing flush, and not being asked to start, then
	 * there's nothing more to do.
	 */
	if (txg == 0) {
		/* Update requested, are we currently forcing flush? */
		if (ddt->ddt_flush_force_txg == 0)
			return;
		txg = ddt->ddt_flush_force_txg;
	}

	/*
	 * If either of the logs have entries unflushed entries before
	 * the wanted txg, set the force txg, otherwise clear it.
	 */

	if ((!avl_is_empty(&ddt->ddt_log_active->ddl_tree) &&
	    ddt->ddt_log_active->ddl_first_txg <= txg) ||
	    (!avl_is_empty(&ddt->ddt_log_flushing->ddl_tree) &&
	    ddt->ddt_log_flushing->ddl_first_txg <= txg)) {
		ddt->ddt_flush_force_txg = txg;
		return;
	}

	/*
	 * Nothing to flush behind the given txg, so we can clear force flush
	 * state.
	 */
	ddt->ddt_flush_force_txg = 0;
}

static void
ddt_sync_flush_log(ddt_t *ddt, dmu_tx_t *tx)
{
	ASSERT(avl_is_empty(&ddt->ddt_tree));

	/* Don't do any flushing when the pool is ready to shut down */
	if (tx->tx_txg > spa_final_dirty_txg(ddt->ddt_spa))
		return;

	/* Try to flush some. */
	if (!ddt_sync_flush_log_incremental(ddt, tx))
		/* More to do next time */
		return;

	/* No more flushing this txg, so we can do end-of-txg housekeeping */

	if (avl_is_empty(&ddt->ddt_log_flushing->ddl_tree) &&
	    !avl_is_empty(&ddt->ddt_log_active->ddl_tree)) {
		/*
		 * No more to flush, and the active list has stuff, so
		 * try to swap the logs for next time.
		 */
		if (ddt_log_swap(ddt, tx)) {
			DDT_KSTAT_ZERO(ddt, dds_log_active_entries);
			DDT_KSTAT_SET(ddt, dds_log_flushing_entries,
			    avl_numnodes(&ddt->ddt_log_flushing->ddl_tree));
		}
	}

	/* If force flush is no longer necessary, turn it off. */
	ddt_flush_force_update_txg(ddt, 0);

	/*
	 * Update flush rate. This is an exponential weighted moving average of
	 * the number of entries flushed over recent txgs.
	 */
	ddt->ddt_log_flush_rate = _ewma(
	    ddt->ddt_flush_count, ddt->ddt_log_flush_rate,
	    zfs_dedup_log_flush_flow_rate_txgs);
	DDT_KSTAT_SET(ddt, dds_log_flush_rate, ddt->ddt_log_flush_rate);

	/*
	 * Update flush time rate. This is an exponential weighted moving
	 * average of the total time taken to flush over recent txgs.
	 */
	ddt->ddt_log_flush_time_rate = _ewma(
	    ddt->ddt_log_flush_time_rate,
	    ((int32_t)(NSEC2MSEC(gethrtime() - ddt->ddt_flush_start))),
	    zfs_dedup_log_flush_flow_rate_txgs);
	DDT_KSTAT_SET(ddt, dds_log_flush_time_rate,
	    ddt->ddt_log_flush_time_rate);
}

static void
ddt_sync_table_log(ddt_t *ddt, dmu_tx_t *tx)
{
	uint64_t count = avl_numnodes(&ddt->ddt_tree);

	if (count > 0) {
		ddt_log_update_t dlu = {0};
		ddt_log_begin(ddt, count, tx, &dlu);

		ddt_entry_t *dde;
		void *cookie = NULL;
		ddt_lightweight_entry_t ddlwe;
		while ((dde =
		    avl_destroy_nodes(&ddt->ddt_tree, &cookie)) != NULL) {
			ASSERT(dde->dde_flags & DDE_FLAG_LOADED);
			DDT_ENTRY_TO_LIGHTWEIGHT(ddt, dde, &ddlwe);
			ddt_log_entry(ddt, &ddlwe, &dlu);
			ddt_sync_scan_entry(ddt, &ddlwe, tx);
			ddt_free(ddt, dde);
		}

		ddt_log_commit(ddt, &dlu);

		DDT_KSTAT_SET(ddt, dds_log_active_entries,
		    avl_numnodes(&ddt->ddt_log_active->ddl_tree));

		/*
		 * Sync the stats for the store objects. Even though we haven't
		 * modified anything on those objects, they're no longer the
		 * source of truth for entries that are now in the log, and we
		 * need the on-disk counts to reflect that, otherwise we'll
		 * miscount later when importing.
		 */
		for (ddt_type_t type = 0; type < DDT_TYPES; type++) {
			for (ddt_class_t class = 0;
			    class < DDT_CLASSES; class++) {
				if (ddt_object_exists(ddt, type, class))
					ddt_object_sync(ddt, type, class, tx);
			}
		}

		memcpy(&ddt->ddt_histogram_cache, ddt->ddt_histogram,
		    sizeof (ddt->ddt_histogram));
		ddt->ddt_spa->spa_dedup_dspace = ~0ULL;
		ddt->ddt_spa->spa_dedup_dsize = ~0ULL;
	}

	if (spa_sync_pass(ddt->ddt_spa) == 1) {
		/*
		 * Update ingest rate. This is an exponential weighted moving
		 * average of the number of entries changed over recent txgs.
		 * The ramp-up cost shouldn't matter too much because the
		 * flusher will be trying to take at least the minimum anyway.
		 */
		ddt->ddt_log_ingest_rate = _ewma(
		    count, ddt->ddt_log_ingest_rate,
		    zfs_dedup_log_flush_flow_rate_txgs);
		DDT_KSTAT_SET(ddt, dds_log_ingest_rate,
		    ddt->ddt_log_ingest_rate);
	}
}

static void
ddt_sync_table_flush(ddt_t *ddt, dmu_tx_t *tx)
{
	if (avl_numnodes(&ddt->ddt_tree) == 0)
		return;

	ddt_entry_t *dde;
	void *cookie = NULL;
	while ((dde = avl_destroy_nodes(
	    &ddt->ddt_tree, &cookie)) != NULL) {
		ASSERT(dde->dde_flags & DDE_FLAG_LOADED);

		ddt_lightweight_entry_t ddlwe;
		DDT_ENTRY_TO_LIGHTWEIGHT(ddt, dde, &ddlwe);
		ddt_sync_flush_entry(ddt, &ddlwe,
		    dde->dde_type, dde->dde_class, tx);
		ddt_sync_scan_entry(ddt, &ddlwe, tx);
		ddt_free(ddt, dde);
	}

	memcpy(&ddt->ddt_histogram_cache, ddt->ddt_histogram,
	    sizeof (ddt->ddt_histogram));
	ddt->ddt_spa->spa_dedup_dspace = ~0ULL;
	ddt->ddt_spa->spa_dedup_dsize = ~0ULL;
	ddt_sync_update_stats(ddt, tx);
}

static void
ddt_sync_table(ddt_t *ddt, dmu_tx_t *tx)
{
	spa_t *spa = ddt->ddt_spa;

	if (ddt->ddt_version == UINT64_MAX)
		return;

	if (spa->spa_uberblock.ub_version < SPA_VERSION_DEDUP) {
		ASSERT0(avl_numnodes(&ddt->ddt_tree));
		return;
	}

	if (spa->spa_ddt_stat_object == 0) {
		spa->spa_ddt_stat_object = zap_create_link(ddt->ddt_os,
		    DMU_OT_DDT_STATS, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_DDT_STATS, tx);
	}

	if (ddt->ddt_version == DDT_VERSION_FDT && ddt->ddt_dir_object == 0)
		ddt_create_dir(ddt, tx);

	if (ddt->ddt_flags & DDT_FLAG_LOG)
		ddt_sync_table_log(ddt, tx);
	else
		ddt_sync_table_flush(ddt, tx);
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
		ddt_sync_table(ddt, tx);
		if (ddt->ddt_flags & DDT_FLAG_LOG)
			ddt_sync_flush_log(ddt, tx);
		ddt_repair_table(ddt, rio);
	}

	(void) zio_wait(rio);
	scn->scn_zio_root = NULL;

	dmu_tx_commit(tx);
}

void
ddt_walk_init(spa_t *spa, uint64_t txg)
{
	if (txg == 0)
		txg = spa_syncing_txg(spa);

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (ddt == NULL || !(ddt->ddt_flags & DDT_FLAG_LOG))
			continue;

		ddt_enter(ddt);
		ddt_flush_force_update_txg(ddt, txg);
		ddt_exit(ddt);
	}
}

boolean_t
ddt_walk_ready(spa_t *spa)
{
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (ddt == NULL || !(ddt->ddt_flags & DDT_FLAG_LOG))
			continue;

		if (ddt->ddt_flush_force_txg > 0)
			return (B_FALSE);
	}

	return (B_TRUE);
}

static int
ddt_walk_impl(spa_t *spa, ddt_bookmark_t *ddb, ddt_lightweight_entry_t *ddlwe,
    uint64_t flags, boolean_t wait)
{
	do {
		do {
			do {
				ddt_t *ddt = spa->spa_ddt[ddb->ddb_checksum];
				if (ddt == NULL)
					continue;

				if (flags != 0 &&
				    (ddt->ddt_flags & flags) != flags)
					continue;

				if (wait && ddt->ddt_flush_force_txg > 0)
					return (EAGAIN);

				int error = ENOENT;
				if (ddt_object_exists(ddt, ddb->ddb_type,
				    ddb->ddb_class)) {
					error = ddt_object_walk(ddt,
					    ddb->ddb_type, ddb->ddb_class,
					    &ddb->ddb_cursor, ddlwe);
				}
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

int
ddt_walk(spa_t *spa, ddt_bookmark_t *ddb, ddt_lightweight_entry_t *ddlwe)
{
	return (ddt_walk_impl(spa, ddb, ddlwe, 0, B_TRUE));
}

/*
 * This function is used by Block Cloning (brt.c) to increase reference
 * counter for the DDT entry if the block is already in DDT.
 *
 * Return false if the block, despite having the D bit set, is not present
 * in the DDT. This is possible when the DDT has been pruned by an admin
 * or by the DDT quota mechanism.
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

	dde = ddt_lookup(ddt, bp);

	/* Can be NULL if the entry for this block was pruned. */
	if (dde == NULL) {
		ddt_exit(ddt);
		spa_config_exit(spa, SCL_ZIO, FTAG);
		return (B_FALSE);
	}

	if ((dde->dde_type < DDT_TYPES) || (dde->dde_flags & DDE_FLAG_LOGGED)) {
		/*
		 * This entry was either synced to a store object (dde_type is
		 * real) or was logged. It must be properly on disk at this
		 * point, so we can just bump its refcount.
		 */
		int p = DDT_PHYS_FOR_COPIES(ddt, BP_GET_NDVAS(bp));
		ddt_phys_variant_t v = DDT_PHYS_VARIANT(ddt, p);

		ddt_phys_addref(dde->dde_phys, v);
		result = B_TRUE;
	} else {
		/*
		 * If the block has the DEDUP flag set it still might not
		 * exist in the DEDUP table due to DDT pruning of entries
		 * where refcnt=1.
		 */
		ddt_remove(ddt, dde);
		result = B_FALSE;
	}

	ddt_exit(ddt);
	spa_config_exit(spa, SCL_ZIO, FTAG);

	return (result);
}

typedef struct ddt_prune_entry {
	ddt_t		*dpe_ddt;
	ddt_key_t	dpe_key;
	list_node_t	dpe_node;
	ddt_univ_phys_t	dpe_phys[];
} ddt_prune_entry_t;

typedef struct ddt_prune_info {
	spa_t		*dpi_spa;
	uint64_t	dpi_txg_syncs;
	uint64_t	dpi_pruned;
	list_t		dpi_candidates;
} ddt_prune_info_t;

/*
 * Add prune candidates for ddt_sync during spa_sync
 */
static void
prune_candidates_sync(void *arg, dmu_tx_t *tx)
{
	(void) tx;
	ddt_prune_info_t *dpi = arg;
	ddt_prune_entry_t *dpe;

	spa_config_enter(dpi->dpi_spa, SCL_ZIO, FTAG, RW_READER);

	/* Process the prune candidates collected so far */
	while ((dpe = list_remove_head(&dpi->dpi_candidates)) != NULL) {
		blkptr_t blk;
		ddt_t *ddt = dpe->dpe_ddt;

		ddt_enter(ddt);

		/*
		 * If it's on the live list, then it was loaded for update
		 * this txg and is no longer stale; skip it.
		 */
		if (avl_find(&ddt->ddt_tree, &dpe->dpe_key, NULL)) {
			ddt_exit(ddt);
			kmem_free(dpe, sizeof (*dpe));
			continue;
		}

		ddt_bp_create(ddt->ddt_checksum, &dpe->dpe_key,
		    dpe->dpe_phys, DDT_PHYS_FLAT, &blk);

		ddt_entry_t *dde = ddt_lookup(ddt, &blk);
		if (dde != NULL && !(dde->dde_flags & DDE_FLAG_LOGGED)) {
			ASSERT(dde->dde_flags & DDE_FLAG_LOADED);
			/*
			 * Zero the physical, so we don't try to free DVAs
			 * at flush nor try to reuse this entry.
			 */
			ddt_phys_clear(dde->dde_phys, DDT_PHYS_FLAT);

			dpi->dpi_pruned++;
		}

		ddt_exit(ddt);
		kmem_free(dpe, sizeof (*dpe));
	}

	spa_config_exit(dpi->dpi_spa, SCL_ZIO, FTAG);
	dpi->dpi_txg_syncs++;
}

/*
 * Prune candidates are collected in open context and processed
 * in sync context as part of ddt_sync_table().
 */
static void
ddt_prune_entry(list_t *list, ddt_t *ddt, const ddt_key_t *ddk,
    const ddt_univ_phys_t *ddp)
{
	ASSERT(ddt->ddt_flags & DDT_FLAG_FLAT);

	size_t dpe_size = sizeof (ddt_prune_entry_t) + DDT_FLAT_PHYS_SIZE;
	ddt_prune_entry_t *dpe = kmem_alloc(dpe_size, KM_SLEEP);

	dpe->dpe_ddt = ddt;
	dpe->dpe_key = *ddk;
	memcpy(dpe->dpe_phys, ddp, DDT_FLAT_PHYS_SIZE);
	list_insert_head(list, dpe);
}

/*
 * Interate over all the entries in the DDT unique class.
 * The walk will perform one of the following operations:
 *  (a) build a histogram than can be used when pruning
 *  (b) prune entries older than the cutoff
 *
 *  Also called by zdb(8) to dump the age histogram
 */
void
ddt_prune_walk(spa_t *spa, uint64_t cutoff, ddt_age_histo_t *histogram)
{
	ddt_bookmark_t ddb = {
		.ddb_class = DDT_CLASS_UNIQUE,
		.ddb_type = 0,
		.ddb_checksum = 0,
		.ddb_cursor = 0
	};
	ddt_lightweight_entry_t ddlwe = {0};
	int error;
	int valid = 0;
	int candidates = 0;
	uint64_t now = gethrestime_sec();
	ddt_prune_info_t dpi;
	boolean_t pruning = (cutoff != 0);

	if (pruning) {
		dpi.dpi_txg_syncs = 0;
		dpi.dpi_pruned = 0;
		dpi.dpi_spa = spa;
		list_create(&dpi.dpi_candidates, sizeof (ddt_prune_entry_t),
		    offsetof(ddt_prune_entry_t, dpe_node));
	}

	if (histogram != NULL)
		memset(histogram, 0, sizeof (ddt_age_histo_t));

	while ((error =
	    ddt_walk_impl(spa, &ddb, &ddlwe, DDT_FLAG_FLAT, B_FALSE)) == 0) {
		ddt_t *ddt = spa->spa_ddt[ddb.ddb_checksum];
		VERIFY(ddt);

		if (spa_shutting_down(spa) || issig())
			break;

		ASSERT(ddt->ddt_flags & DDT_FLAG_FLAT);
		ASSERT3U(ddlwe.ddlwe_phys.ddp_flat.ddp_refcnt, <=, 1);

		uint64_t class_start =
		    ddlwe.ddlwe_phys.ddp_flat.ddp_class_start;

		/*
		 * If this entry is on the log, then the stored entry is stale
		 * and we should skip it.
		 */
		if (ddt_log_find_key(ddt, &ddlwe.ddlwe_key, NULL))
			continue;

		/* prune older entries */
		if (pruning && class_start < cutoff) {
			if (candidates++ >= zfs_ddt_prunes_per_txg) {
				/* sync prune candidates in batches */
				VERIFY0(dsl_sync_task(spa_name(spa),
				    NULL, prune_candidates_sync,
				    &dpi, 0, ZFS_SPACE_CHECK_NONE));
				candidates = 1;
			}
			ddt_prune_entry(&dpi.dpi_candidates, ddt,
			    &ddlwe.ddlwe_key, &ddlwe.ddlwe_phys);
		}

		/* build a histogram */
		if (histogram != NULL) {
			uint64_t age = MAX(1, (now - class_start) / 3600);
			int bin = MIN(highbit64(age) - 1, HIST_BINS - 1);
			histogram->dah_entries++;
			histogram->dah_age_histo[bin]++;
		}

		valid++;
	}

	if (pruning && valid > 0) {
		if (!list_is_empty(&dpi.dpi_candidates)) {
			/* sync out final batch of prune candidates */
			VERIFY0(dsl_sync_task(spa_name(spa), NULL,
			    prune_candidates_sync, &dpi, 0,
			    ZFS_SPACE_CHECK_NONE));
		}
		list_destroy(&dpi.dpi_candidates);

		zfs_dbgmsg("pruned %llu entries (%d%%) across %llu txg syncs",
		    (u_longlong_t)dpi.dpi_pruned,
		    (int)((dpi.dpi_pruned * 100) / valid),
		    (u_longlong_t)dpi.dpi_txg_syncs);
	}
}

static uint64_t
ddt_total_entries(spa_t *spa)
{
	ddt_object_t ddo;
	ddt_get_dedup_object_stats(spa, &ddo);

	return (ddo.ddo_count);
}

int
ddt_prune_unique_entries(spa_t *spa, zpool_ddt_prune_unit_t unit,
    uint64_t amount)
{
	uint64_t cutoff;
	uint64_t start_time = gethrtime();

	if (spa->spa_active_ddt_prune)
		return (SET_ERROR(EALREADY));
	if (ddt_total_entries(spa) == 0)
		return (0);

	spa->spa_active_ddt_prune = B_TRUE;

	zfs_dbgmsg("prune %llu %s", (u_longlong_t)amount,
	    unit == ZPOOL_DDT_PRUNE_PERCENTAGE ? "%" : "seconds old or older");

	if (unit == ZPOOL_DDT_PRUNE_PERCENTAGE) {
		ddt_age_histo_t histogram;
		uint64_t oldest = 0;

		/* Make a pass over DDT to build a histogram */
		ddt_prune_walk(spa, 0, &histogram);

		int target = (histogram.dah_entries * amount) / 100;

		/*
		 * Figure out our cutoff date
		 * (i.e., which bins to prune from)
		 */
		for (int i = HIST_BINS - 1; i >= 0 && target > 0; i--) {
			if (histogram.dah_age_histo[i] != 0) {
				/* less than this bucket remaining */
				if (target < histogram.dah_age_histo[i]) {
					oldest = MAX(1, (1<<i) * 3600);
					target = 0;
				} else {
					target -= histogram.dah_age_histo[i];
				}
			}
		}
		cutoff = gethrestime_sec() - oldest;

		if (ddt_dump_prune_histogram)
			ddt_dump_age_histogram(&histogram, cutoff);
	} else if (unit == ZPOOL_DDT_PRUNE_AGE) {
		cutoff = gethrestime_sec() - amount;
	} else {
		return (EINVAL);
	}

	if (cutoff > 0 && !spa_shutting_down(spa) && !issig()) {
		/* Traverse DDT to prune entries older that our cuttoff */
		ddt_prune_walk(spa, cutoff, NULL);
	}

	zfs_dbgmsg("%s: prune completed in %llu ms",
	    spa_name(spa), (u_longlong_t)NSEC2MSEC(gethrtime() - start_time));

	spa->spa_active_ddt_prune = B_FALSE;
	return (0);
}

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, prefetch, INT, ZMOD_RW,
	"Enable prefetching dedup-ed blks");

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_flush_passes_max, UINT, ZMOD_RW,
	"Max number of incremental dedup log flush passes per transaction");

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_flush_min_time_ms, UINT, ZMOD_RW,
	"Min time to spend on incremental dedup log flush each transaction");

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_flush_entries_min, UINT, ZMOD_RW,
	"Min number of log entries to flush each transaction");

ZFS_MODULE_PARAM(zfs_dedup, zfs_dedup_, log_flush_flow_rate_txgs, UINT, ZMOD_RW,
	"Number of txgs to average flow rates across");
