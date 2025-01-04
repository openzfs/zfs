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
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright (c) 2023, Klara Inc.
 */

#ifndef _SYS_DDT_H
#define	_SYS_DDT_H

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct abd;

/*
 * DDT-wide feature flags. These are set in ddt_flags by ddt_configure().
 */
#define	DDT_FLAG_FLAT	(1 << 0)	/* single extensible phys */
#define	DDT_FLAG_LOG	(1 << 1)	/* dedup log (journal) */
#define	DDT_FLAG_MASK	(DDT_FLAG_FLAT|DDT_FLAG_LOG)

/*
 * DDT on-disk storage object types. Each one corresponds to specific
 * implementation, see ddt_ops_t. The value itself is not stored on disk.
 *
 * When searching for an entry, objects types will be searched in this order.
 *
 * Note that DDT_TYPES is used as the "no type" for new entries that have not
 * yet been written to a storage object.
 */
typedef enum {
	DDT_TYPE_ZAP = 0,	/* ZAP storage object, ddt_zap */
	DDT_TYPES
} ddt_type_t;

_Static_assert(DDT_TYPES <= UINT8_MAX,
	"ddt_type_t must fit in a uint8_t");

/* New and updated entries recieve this type, see ddt_sync_entry() */
#define	DDT_TYPE_DEFAULT	(DDT_TYPE_ZAP)

/*
 * DDT storage classes. Each class has a separate storage object for each type.
 * The value itself is not stored on disk.
 *
 * When search for an entry, object classes will be searched in this order.
 *
 * Note that DDT_CLASSES is used as the "no class" for new entries that have not
 * yet been written to a storage object.
 */
typedef enum {
	DDT_CLASS_DITTO = 0,	/* entry has ditto blocks (obsolete) */
	DDT_CLASS_DUPLICATE,	/* entry has multiple references */
	DDT_CLASS_UNIQUE,	/* entry has a single reference */
	DDT_CLASSES
} ddt_class_t;

_Static_assert(DDT_CLASSES < UINT8_MAX,
	"ddt_class_t must fit in a uint8_t");

/*
 * The "key" part of an on-disk entry. This is the unique "name" for a block,
 * that is, that parts of the block pointer that will always be the same for
 * the same data.
 */
typedef struct {
	zio_cksum_t	ddk_cksum;	/* 256-bit block checksum */
	/*
	 * Encoded with logical & physical size, encryption, and compression,
	 * as follows:
	 *   +-------+-------+-------+-------+-------+-------+-------+-------+
	 *   |   0   |   0   |   0   |X| comp|     PSIZE     |     LSIZE     |
	 *   +-------+-------+-------+-------+-------+-------+-------+-------+
	 */
	uint64_t	ddk_prop;
} ddt_key_t;

/*
 * Macros for accessing parts of a ddt_key_t. These are similar to their BP_*
 * counterparts.
 */
#define	DDK_GET_LSIZE(ddk)	\
	BF64_GET_SB((ddk)->ddk_prop, 0, 16, SPA_MINBLOCKSHIFT, 1)
#define	DDK_SET_LSIZE(ddk, x)	\
	BF64_SET_SB((ddk)->ddk_prop, 0, 16, SPA_MINBLOCKSHIFT, 1, x)

#define	DDK_GET_PSIZE(ddk)	\
	BF64_GET_SB((ddk)->ddk_prop, 16, 16, SPA_MINBLOCKSHIFT, 1)
#define	DDK_SET_PSIZE(ddk, x)	\
	BF64_SET_SB((ddk)->ddk_prop, 16, 16, SPA_MINBLOCKSHIFT, 1, x)

#define	DDK_GET_COMPRESS(ddk)		BF64_GET((ddk)->ddk_prop, 32, 7)
#define	DDK_SET_COMPRESS(ddk, x)	BF64_SET((ddk)->ddk_prop, 32, 7, x)

#define	DDK_GET_CRYPT(ddk)		BF64_GET((ddk)->ddk_prop, 39, 1)
#define	DDK_SET_CRYPT(ddk, x)	BF64_SET((ddk)->ddk_prop, 39, 1, x)

/*
 * The "value" part for an on-disk entry. These are the "physical"
 * characteristics of the stored block, such as its location on disk (DVAs),
 * birth txg and ref count.
 *
 * The "traditional" entry has an array of four, one for each number of DVAs
 * (copies= property) and another for additional "ditto" copies. Users of the
 * traditional struct will specify the variant (index) of the one they want.
 *
 * The newer "flat" entry has only a single form that is specified using the
 * DDT_PHYS_FLAT variant.
 *
 * Since the value size varies, use one of the size macros when interfacing
 * with the ddt zap.
 */

#define	DDT_PHYS_MAX	(4)

/*
 * Note - this can be used in a flexible array and allocated for
 * a specific size (ddp_trad or ddp_flat). So be careful not to
 * copy using "=" assignment but instead use ddt_phys_copy().
 */
typedef union {
	/*
	 * Traditional physical payload value for DDT zap (256 bytes)
	 */
	struct {
		dva_t		ddp_dva[SPA_DVAS_PER_BP];
		uint64_t	ddp_refcnt;
		uint64_t	ddp_phys_birth;
	} ddp_trad[DDT_PHYS_MAX];

	/*
	 * Flat physical payload value for DDT zap (72 bytes)
	 */
	struct {
		dva_t		ddp_dva[SPA_DVAS_PER_BP];
		uint64_t	ddp_refcnt;
		uint64_t	ddp_phys_birth; /* txg based from BP */
		uint64_t	ddp_class_start; /* in realtime seconds */
	} ddp_flat;
} ddt_univ_phys_t;

/*
 * This enum denotes which variant of a ddt_univ_phys_t to target. For
 * a traditional DDT entry, it represents the indexes into the ddp_trad
 * array. Any consumer of a ddt_univ_phys_t needs to know which variant
 * is being targeted.
 *
 * Note, we no longer generate new DDT_PHYS_DITTO-type blocks.  However,
 * we maintain the ability to free existing dedup-ditto blocks.
 */

typedef enum {
	DDT_PHYS_DITTO = 0,
	DDT_PHYS_SINGLE = 1,
	DDT_PHYS_DOUBLE = 2,
	DDT_PHYS_TRIPLE = 3,
	DDT_PHYS_FLAT = 4,
	DDT_PHYS_NONE = 5
} ddt_phys_variant_t;

#define	DDT_PHYS_VARIANT(ddt, p)	\
	(ASSERT((p) < DDT_PHYS_NONE),	\
	((ddt)->ddt_flags & DDT_FLAG_FLAT ? DDT_PHYS_FLAT : (p)))

#define	DDT_TRAD_PHYS_SIZE	sizeof (((ddt_univ_phys_t *)0)->ddp_trad)
#define	DDT_FLAT_PHYS_SIZE	sizeof (((ddt_univ_phys_t *)0)->ddp_flat)

#define	_DDT_PHYS_SWITCH(ddt, flat, trad)	\
	(((ddt)->ddt_flags & DDT_FLAG_FLAT) ? (flat) : (trad))

#define	DDT_PHYS_SIZE(ddt)		_DDT_PHYS_SWITCH(ddt,	\
	DDT_FLAT_PHYS_SIZE, DDT_TRAD_PHYS_SIZE)

#define	DDT_NPHYS(ddt)			_DDT_PHYS_SWITCH(ddt, 1, DDT_PHYS_MAX)
#define	DDT_PHYS_FOR_COPIES(ddt, p)	_DDT_PHYS_SWITCH(ddt, 0, p)
#define	DDT_PHYS_IS_DITTO(ddt, p)	_DDT_PHYS_SWITCH(ddt, 0, (p == 0))

/*
 * A "live" entry, holding changes to an entry made this txg, and other data to
 * support loading, updating and repairing the entry.
 */

/* State flags for dde_flags */
#define	DDE_FLAG_LOADED		(1 << 0)	/* entry ready for use */
#define	DDE_FLAG_OVERQUOTA	(1 << 1)	/* entry unusable, no space */
#define	DDE_FLAG_LOGGED		(1 << 2)	/* loaded from log */

/*
 * Additional data to support entry update or repair. This is fixed size
 * because its relatively rarely used.
 */
typedef struct {
	/* copy of data after a repair read, to be rewritten */
	abd_t		*dde_repair_abd;

	/* original phys contents before update, for error handling */
	ddt_univ_phys_t	dde_orig_phys;

	/* in-flight update IOs */
	zio_t		*dde_lead_zio[DDT_PHYS_MAX];
} ddt_entry_io_t;

typedef struct {
	/* key must be first for ddt_key_compare */
	ddt_key_t	dde_key;	/* ddt_tree key */
	avl_node_t	dde_node;	/* ddt_tree_node */

	/* storage type and class the entry was loaded from */
	ddt_type_t	dde_type;
	ddt_class_t	dde_class;

	uint8_t		dde_flags;	/* load state flags */
	kcondvar_t	dde_cv;		/* signaled when load completes */
	uint64_t	dde_waiters;	/* count of waiters on dde_cv */

	ddt_entry_io_t	*dde_io;	/* IO support, when required */

	ddt_univ_phys_t	dde_phys[];	/* flexible -- allocated size varies */
} ddt_entry_t;

/*
 * A lightweight entry is for short-lived or transient uses, like iterating or
 * inspecting, when you don't care where it came from.
 */
typedef struct {
	ddt_key_t	ddlwe_key;
	ddt_type_t	ddlwe_type;
	ddt_class_t	ddlwe_class;
	ddt_univ_phys_t	ddlwe_phys;
} ddt_lightweight_entry_t;

/*
 * In-core DDT log. A separate struct to make it easier to switch between the
 * appending and flushing logs.
 */
typedef struct {
	avl_tree_t	ddl_tree;	/* logged entries */
	uint32_t	ddl_flags;	/* flags for this log */
	uint64_t	ddl_object;	/* log object id */
	uint64_t	ddl_length;	/* on-disk log size */
	uint64_t	ddl_first_txg;	/* txg log became active */
	ddt_key_t	ddl_checkpoint;	/* last checkpoint */
} ddt_log_t;

/*
 * In-core DDT object. This covers all entries and stats for a the whole pool
 * for a given checksum type.
 */
typedef struct {
	kmutex_t	ddt_lock;	/* protects changes to all fields */

	avl_tree_t	ddt_tree;	/* "live" (changed) entries this txg */
	avl_tree_t	ddt_log_tree;	/* logged entries */

	avl_tree_t	ddt_repair_tree;	/* entries being repaired */

	ddt_log_t	ddt_log[2];		/* active/flushing logs */
	ddt_log_t	*ddt_log_active;	/* pointers into ddt_log */
	ddt_log_t	*ddt_log_flushing;	/* swapped when flush starts */

	hrtime_t	ddt_flush_start;	/* log flush start this txg */
	uint32_t	ddt_flush_pass;		/* log flush pass this txg */

	int32_t		ddt_flush_count;	/* entries flushed this txg */
	int32_t		ddt_flush_min;		/* min rem entries to flush */
	int32_t		ddt_log_ingest_rate;	/* rolling log ingest rate */
	int32_t		ddt_log_flush_rate;	/* rolling log flush rate */
	int32_t		ddt_log_flush_time_rate; /* avg time spent flushing */

	uint64_t	ddt_flush_force_txg;	/* flush hard before this txg */

	kstat_t		*ddt_ksp;	/* kstats context */

	enum zio_checksum ddt_checksum;	/* checksum algorithm in use */
	spa_t		*ddt_spa;	/* pool this ddt is on */
	objset_t	*ddt_os;	/* ddt objset (always MOS) */

	uint64_t	ddt_dir_object;	/* MOS dir holding ddt objects */
	uint64_t	ddt_version;	/* DDT version */
	uint64_t	ddt_flags;	/* FDT option flags */

	/* per-type/per-class entry store objects */
	uint64_t	ddt_object[DDT_TYPES][DDT_CLASSES];

	/* object ids for stored, logged and per-type/per-class stats */
	uint64_t	ddt_stat_object;
	ddt_object_t	ddt_log_stats;
	ddt_object_t	ddt_object_stats[DDT_TYPES][DDT_CLASSES];

	/* type/class stats by power-2-sized referenced blocks */
	ddt_histogram_t	ddt_histogram[DDT_TYPES][DDT_CLASSES];
	ddt_histogram_t	ddt_histogram_cache[DDT_TYPES][DDT_CLASSES];

	/* log stats power-2-sized referenced blocks */
	ddt_histogram_t	ddt_log_histogram;
} ddt_t;

/*
 * In-core and on-disk bookmark for DDT walks. This is a cursor for ddt_walk(),
 * and is stable across calls, even if the DDT is updated, the pool is
 * restarted or loaded on another system, or OpenZFS is upgraded.
 */
typedef struct {
	uint64_t	ddb_class;
	uint64_t	ddb_type;
	uint64_t	ddb_checksum;
	uint64_t	ddb_cursor;
} ddt_bookmark_t;

extern void ddt_bp_fill(const ddt_univ_phys_t *ddp, ddt_phys_variant_t v,
    blkptr_t *bp, uint64_t txg);
extern void ddt_bp_create(enum zio_checksum checksum, const ddt_key_t *ddk,
    const ddt_univ_phys_t *ddp, ddt_phys_variant_t v, blkptr_t *bp);

extern void ddt_phys_extend(ddt_univ_phys_t *ddp, ddt_phys_variant_t v,
    const blkptr_t *bp);
extern void ddt_phys_copy(ddt_univ_phys_t *dst, const ddt_univ_phys_t *src,
    ddt_phys_variant_t v);
extern void ddt_phys_clear(ddt_univ_phys_t *ddp, ddt_phys_variant_t v);
extern void ddt_phys_addref(ddt_univ_phys_t *ddp, ddt_phys_variant_t v);
extern uint64_t ddt_phys_decref(ddt_univ_phys_t *ddp, ddt_phys_variant_t v);
extern uint64_t ddt_phys_refcnt(const ddt_univ_phys_t *ddp,
    ddt_phys_variant_t v);
extern ddt_phys_variant_t ddt_phys_select(const ddt_t *ddt,
    const ddt_entry_t *dde, const blkptr_t *bp);
extern uint64_t ddt_phys_birth(const ddt_univ_phys_t *ddp,
    ddt_phys_variant_t v);
extern int ddt_phys_dva_count(const ddt_univ_phys_t *ddp, ddt_phys_variant_t v,
    boolean_t encrypted);

extern void ddt_histogram_add_entry(ddt_t *ddt, ddt_histogram_t *ddh,
    const ddt_lightweight_entry_t *ddlwe);
extern void ddt_histogram_sub_entry(ddt_t *ddt, ddt_histogram_t *ddh,
    const ddt_lightweight_entry_t *ddlwe);

extern void ddt_histogram_add(ddt_histogram_t *dst, const ddt_histogram_t *src);
extern void ddt_histogram_total(ddt_stat_t *dds, const ddt_histogram_t *ddh);
extern boolean_t ddt_histogram_empty(const ddt_histogram_t *ddh);

extern void ddt_get_dedup_object_stats(spa_t *spa, ddt_object_t *ddo);
extern uint64_t ddt_get_ddt_dsize(spa_t *spa);
extern void ddt_get_dedup_histogram(spa_t *spa, ddt_histogram_t *ddh);
extern void ddt_get_dedup_stats(spa_t *spa, ddt_stat_t *dds_total);

extern uint64_t ddt_get_dedup_dspace(spa_t *spa);
extern uint64_t ddt_get_pool_dedup_ratio(spa_t *spa);
extern int ddt_get_pool_dedup_cached(spa_t *spa, uint64_t *psize);

extern ddt_t *ddt_select(spa_t *spa, const blkptr_t *bp);
extern void ddt_enter(ddt_t *ddt);
extern void ddt_exit(ddt_t *ddt);
extern void ddt_init(void);
extern void ddt_fini(void);
extern ddt_entry_t *ddt_lookup(ddt_t *ddt, const blkptr_t *bp);
extern void ddt_remove(ddt_t *ddt, ddt_entry_t *dde);
extern void ddt_prefetch(spa_t *spa, const blkptr_t *bp);
extern void ddt_prefetch_all(spa_t *spa);

extern boolean_t ddt_class_contains(spa_t *spa, ddt_class_t max_class,
    const blkptr_t *bp);

extern void ddt_alloc_entry_io(ddt_entry_t *dde);

extern ddt_entry_t *ddt_repair_start(ddt_t *ddt, const blkptr_t *bp);
extern void ddt_repair_done(ddt_t *ddt, ddt_entry_t *dde);

extern int ddt_key_compare(const void *x1, const void *x2);

extern void ddt_create(spa_t *spa);
extern int ddt_load(spa_t *spa);
extern void ddt_unload(spa_t *spa);
extern void ddt_sync(spa_t *spa, uint64_t txg);

extern void ddt_walk_init(spa_t *spa, uint64_t txg);
extern boolean_t ddt_walk_ready(spa_t *spa);
extern int ddt_walk(spa_t *spa, ddt_bookmark_t *ddb,
    ddt_lightweight_entry_t *ddlwe);

extern boolean_t ddt_addref(spa_t *spa, const blkptr_t *bp);

extern int ddt_prune_unique_entries(spa_t *spa, zpool_ddt_prune_unit_t unit,
    uint64_t amount);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDT_H */
