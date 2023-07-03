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
/* No flags yet. */
#define	DDT_FLAG_MASK	(0)

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
 * Note that an entry has an array of four ddt_phys_t, one for each number of
 * DVAs (copies= property) and another for additional "ditto" copies. Most
 * users of ddt_phys_t will handle indexing into or counting the phys they
 * want.
 */
typedef struct {
	dva_t		ddp_dva[SPA_DVAS_PER_BP];
	uint64_t	ddp_refcnt;
	uint64_t	ddp_phys_birth;
} ddt_phys_t;

#define	DDT_PHYS_MAX			(4)
#define	DDT_NPHYS(ddt)			((ddt) ? DDT_PHYS_MAX : DDT_PHYS_MAX)
#define	DDT_PHYS_IS_DITTO(ddt, p)	((ddt) && p == 0)
#define	DDT_PHYS_FOR_COPIES(ddt, p)	((ddt) ? (p) : (p))

/*
 * A "live" entry, holding changes to an entry made this txg, and other data to
 * support loading, updating and repairing the entry.
 */

/* State flags for dde_flags */
#define	DDE_FLAG_LOADED		(1 << 0)	/* entry ready for use */
#define	DDE_FLAG_OVERQUOTA	(1 << 1)	/* entry unusable, no space */

typedef struct {
	/* key must be first for ddt_key_compare */
	ddt_key_t	dde_key;		/* ddt_tree key */
	ddt_phys_t	dde_phys[DDT_PHYS_MAX];	/* on-disk data */

	/* in-flight update IOs */
	zio_t		*dde_lead_zio[DDT_PHYS_MAX];

	/* copy of data after a repair read, to be rewritten */
	struct abd	*dde_repair_abd;

	/* storage type and class the entry was loaded from */
	ddt_type_t	dde_type;
	ddt_class_t	dde_class;

	uint8_t		dde_flags;	/* load state flags */
	kcondvar_t	dde_cv;		/* signaled when load completes */
	uint64_t	dde_waiters;	/* count of waiters on dde_cv */

	avl_node_t	dde_node;	/* ddt_tree node */
} ddt_entry_t;

/*
 * In-core DDT object. This covers all entries and stats for a the whole pool
 * for a given checksum type.
 */
typedef struct {
	kmutex_t	ddt_lock;	/* protects changes to all fields */

	avl_tree_t	ddt_tree;	/* "live" (changed) entries this txg */

	avl_tree_t	ddt_repair_tree; /* entries being repaired */

	enum zio_checksum ddt_checksum;	/* checksum algorithm in use */
	spa_t		*ddt_spa;	/* pool this ddt is on */
	objset_t	*ddt_os;	/* ddt objset (always MOS) */

	uint64_t	ddt_dir_object;	/* MOS dir holding ddt objects */
	uint64_t	ddt_version;	/* DDT version */
	uint64_t	ddt_flags;	/* FDT option flags */

	/* per-type/per-class entry store objects */
	uint64_t	ddt_object[DDT_TYPES][DDT_CLASSES];

	/* object ids for whole-ddt and per-type/per-class stats */
	uint64_t	ddt_stat_object;
	ddt_object_t	ddt_object_stats[DDT_TYPES][DDT_CLASSES];

	/* type/class stats by power-2-sized referenced blocks */
	ddt_histogram_t	ddt_histogram[DDT_TYPES][DDT_CLASSES];
	ddt_histogram_t	ddt_histogram_cache[DDT_TYPES][DDT_CLASSES];
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

extern void ddt_bp_fill(const ddt_phys_t *ddp, blkptr_t *bp,
    uint64_t txg);
extern void ddt_bp_create(enum zio_checksum checksum, const ddt_key_t *ddk,
    const ddt_phys_t *ddp, blkptr_t *bp);

extern void ddt_phys_fill(ddt_phys_t *ddp, const blkptr_t *bp);
extern void ddt_phys_clear(ddt_phys_t *ddp);
extern void ddt_phys_addref(ddt_phys_t *ddp);
extern void ddt_phys_decref(ddt_phys_t *ddp);
extern ddt_phys_t *ddt_phys_select(const ddt_t *ddt, const ddt_entry_t *dde,
    const blkptr_t *bp);

extern void ddt_histogram_add(ddt_histogram_t *dst, const ddt_histogram_t *src);
extern void ddt_histogram_stat(ddt_stat_t *dds, const ddt_histogram_t *ddh);
extern boolean_t ddt_histogram_empty(const ddt_histogram_t *ddh);
extern void ddt_get_dedup_object_stats(spa_t *spa, ddt_object_t *ddo);
extern uint64_t ddt_get_ddt_dsize(spa_t *spa);
extern void ddt_get_dedup_histogram(spa_t *spa, ddt_histogram_t *ddh);
extern void ddt_get_dedup_stats(spa_t *spa, ddt_stat_t *dds_total);

extern uint64_t ddt_get_dedup_dspace(spa_t *spa);
extern uint64_t ddt_get_pool_dedup_ratio(spa_t *spa);
extern int ddt_get_pool_dedup_cached(spa_t *spa, uint64_t *psize);

extern ddt_t *ddt_select(spa_t *spa, const blkptr_t *bp);
extern ddt_t *ddt_select_checksum(spa_t *spa, enum zio_checksum checksum);
extern void ddt_enter(ddt_t *ddt);
extern void ddt_exit(ddt_t *ddt);
extern void ddt_init(void);
extern void ddt_fini(void);
extern ddt_entry_t *ddt_lookup(ddt_t *ddt, const blkptr_t *bp, boolean_t add);
extern void ddt_remove(ddt_t *ddt, ddt_entry_t *dde);
extern void ddt_prefetch(spa_t *spa, const blkptr_t *bp);
extern void ddt_prefetch_all(spa_t *spa);

extern boolean_t ddt_class_contains(spa_t *spa, ddt_class_t max_class,
    const blkptr_t *bp);

extern ddt_entry_t *ddt_repair_start(ddt_t *ddt, const blkptr_t *bp);
extern void ddt_repair_done(ddt_t *ddt, ddt_entry_t *dde);

extern int ddt_key_compare(const void *x1, const void *x2);

extern void ddt_create(spa_t *spa);
extern int ddt_load(spa_t *spa);
extern void ddt_unload(spa_t *spa);
extern void ddt_sync(spa_t *spa, uint64_t txg);
extern int ddt_walk(spa_t *spa, ddt_bookmark_t *ddb, ddt_entry_t *dde);

extern boolean_t ddt_addref(spa_t *spa, const blkptr_t *bp);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDT_H */
