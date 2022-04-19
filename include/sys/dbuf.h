/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#ifndef	_SYS_DBUF_H
#define	_SYS_DBUF_H

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/zfs_context.h>
#include <sys/zfs_refcount.h>
#include <sys/zrlock.h>
#include <sys/multilist.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	IN_DMU_SYNC 2

/*
 * define flags for dbuf_read
 */

#define	DB_RF_MUST_SUCCEED	(1 << 0)
#define	DB_RF_CANFAIL		(1 << 1)
#define	DB_RF_HAVESTRUCT	(1 << 2)
#define	DB_RF_NOPREFETCH	(1 << 3)
#define	DB_RF_NEVERWAIT		(1 << 4)
#define	DB_RF_CACHED		(1 << 5)
#define	DB_RF_NO_DECRYPT	(1 << 6)

/*
 * The simplified state transition diagram for dbufs looks like:
 *
 *		+----> READ ----+
 *		|		|
 *		|		V
 *  (alloc)-->UNCACHED	     CACHED-->EVICTING-->(free)
 *		|		^	 ^
 *		|		|	 |
 *		+----> FILL ----+	 |
 *		|			 |
 *		|			 |
 *		+--------> NOFILL -------+
 *
 * DB_SEARCH is an invalid state for a dbuf. It is used by dbuf_free_range
 * to find all dbufs in a range of a dnode and must be less than any other
 * dbuf_states_t (see comment on dn_dbufs in dnode.h).
 */
typedef enum dbuf_states {
	DB_SEARCH = -1,
	DB_UNCACHED,
	DB_FILL,
	DB_NOFILL,
	DB_READ,
	DB_CACHED,
	DB_EVICTING
} dbuf_states_t;

typedef enum dbuf_cached_state {
	DB_NO_CACHE = -1,
	DB_DBUF_CACHE,
	DB_DBUF_METADATA_CACHE,
	DB_CACHE_MAX
} dbuf_cached_state_t;

struct dnode;
struct dmu_tx;

/*
 * level = 0 means the user data
 * level = 1 means the single indirect block
 * etc.
 */

struct dmu_buf_impl;

typedef enum override_states {
	DR_NOT_OVERRIDDEN,
	DR_IN_DMU_SYNC,
	DR_OVERRIDDEN
} override_states_t;

typedef enum db_lock_type {
	DLT_NONE,
	DLT_PARENT,
	DLT_OBJSET
} db_lock_type_t;

typedef struct dbuf_dirty_record {
	/* link on our parents dirty list */
	list_node_t dr_dirty_node;

	/* transaction group this data will sync in */
	uint64_t dr_txg;

	/* zio of outstanding write IO */
	zio_t *dr_zio;

	/* pointer back to our dbuf */
	struct dmu_buf_impl *dr_dbuf;

	/* list link for dbuf dirty records */
	list_node_t dr_dbuf_node;

	/*
	 * The dnode we are part of.  Note that the dnode can not be moved or
	 * evicted due to the hold that's added by dnode_setdirty() or
	 * dmu_objset_sync_dnodes(), and released by dnode_rele_task() or
	 * userquota_updates_task().  This hold is necessary for
	 * dirty_lightweight_leaf-type dirty records, which don't have a hold
	 * on a dbuf.
	 */
	dnode_t *dr_dnode;

	/* pointer to parent dirty record */
	struct dbuf_dirty_record *dr_parent;

	/* How much space was changed to dsl_pool_dirty_space() for this? */
	unsigned int dr_accounted;

	/* A copy of the bp that points to us */
	blkptr_t dr_bp_copy;

	union dirty_types {
		struct dirty_indirect {

			/* protect access to list */
			kmutex_t dr_mtx;

			/* Our list of dirty children */
			list_t dr_children;
		} di;
		struct dirty_leaf {

			/*
			 * dr_data is set when we dirty the buffer
			 * so that we can retain the pointer even if it
			 * gets COW'd in a subsequent transaction group.
			 */
			arc_buf_t *dr_data;
			blkptr_t dr_overridden_by;
			override_states_t dr_override_state;
			uint8_t dr_copies;
			boolean_t dr_nopwrite;
			boolean_t dr_has_raw_params;

			/*
			 * If dr_has_raw_params is set, the following crypt
			 * params will be set on the BP that's written.
			 */
			boolean_t dr_byteorder;
			uint8_t	dr_salt[ZIO_DATA_SALT_LEN];
			uint8_t	dr_iv[ZIO_DATA_IV_LEN];
			uint8_t	dr_mac[ZIO_DATA_MAC_LEN];
		} dl;
		struct dirty_lightweight_leaf {
			/*
			 * This dirty record refers to a leaf (level=0)
			 * block, whose dbuf has not been instantiated for
			 * performance reasons.
			 */
			uint64_t dr_blkid;
			abd_t *dr_abd;
			zio_prop_t dr_props;
			enum zio_flag dr_flags;
		} dll;
	} dt;
} dbuf_dirty_record_t;

typedef struct dmu_buf_impl {
	/*
	 * The following members are immutable, with the exception of
	 * db.db_data, which is protected by db_mtx.
	 */

	/* the publicly visible structure */
	dmu_buf_t db;

	/* the objset we belong to */
	struct objset *db_objset;

	/*
	 * handle to safely access the dnode we belong to (NULL when evicted)
	 */
	struct dnode_handle *db_dnode_handle;

	/*
	 * our parent buffer; if the dnode points to us directly,
	 * db_parent == db_dnode_handle->dnh_dnode->dn_dbuf
	 * only accessed by sync thread ???
	 * (NULL when evicted)
	 * May change from NULL to non-NULL under the protection of db_mtx
	 * (see dbuf_check_blkptr())
	 */
	struct dmu_buf_impl *db_parent;

	/*
	 * link for hash table of all dmu_buf_impl_t's
	 */
	struct dmu_buf_impl *db_hash_next;

	/*
	 * Our link on the owner dnodes's dn_dbufs list.
	 * Protected by its dn_dbufs_mtx.  Should be on the same cache line
	 * as db_level and db_blkid for the best avl_add() performance.
	 */
	avl_node_t db_link;

	/* our block number */
	uint64_t db_blkid;

	/*
	 * Pointer to the blkptr_t which points to us. May be NULL if we
	 * don't have one yet. (NULL when evicted)
	 */
	blkptr_t *db_blkptr;

	/*
	 * Our indirection level.  Data buffers have db_level==0.
	 * Indirect buffers which point to data buffers have
	 * db_level==1. etc.  Buffers which contain dnodes have
	 * db_level==0, since the dnodes are stored in a file.
	 */
	uint8_t db_level;

	/*
	 * Protects db_buf's contents if they contain an indirect block or data
	 * block of the meta-dnode. We use this lock to protect the structure of
	 * the block tree. This means that when modifying this dbuf's data, we
	 * grab its rwlock. When modifying its parent's data (including the
	 * blkptr to this dbuf), we grab the parent's rwlock. The lock ordering
	 * for this lock is:
	 * 1) dn_struct_rwlock
	 * 2) db_rwlock
	 * We don't currently grab multiple dbufs' db_rwlocks at once.
	 */
	krwlock_t db_rwlock;

	/* buffer holding our data */
	arc_buf_t *db_buf;

	/* db_mtx protects the members below */
	kmutex_t db_mtx;

	/*
	 * Current state of the buffer
	 */
	dbuf_states_t db_state;

	/*
	 * Refcount accessed by dmu_buf_{hold,rele}.
	 * If nonzero, the buffer can't be destroyed.
	 * Protected by db_mtx.
	 */
	zfs_refcount_t db_holds;

	kcondvar_t db_changed;
	dbuf_dirty_record_t *db_data_pending;

	/* List of dirty records for the buffer sorted newest to oldest. */
	list_t db_dirty_records;

	/* Link in dbuf_cache or dbuf_metadata_cache */
	multilist_node_t db_cache_link;

	/* Tells us which dbuf cache this dbuf is in, if any */
	dbuf_cached_state_t db_caching_status;

	/* Data which is unique to data (leaf) blocks: */

	/* User callback information. */
	dmu_buf_user_t *db_user;

	/*
	 * Evict user data as soon as the dirty and reference
	 * counts are equal.
	 */
	uint8_t db_user_immediate_evict;

	/*
	 * This block was freed while a read or write was
	 * active.
	 */
	uint8_t db_freed_in_flight;

	/*
	 * dnode_evict_dbufs() or dnode_evict_bonus() tried to
	 * evict this dbuf, but couldn't due to outstanding
	 * references.  Evict once the refcount drops to 0.
	 */
	uint8_t db_pending_evict;

	uint8_t db_dirtycnt;
} dmu_buf_impl_t;

#define	DBUF_RWLOCKS 8192
#define	DBUF_HASH_RWLOCK(h, idx) (&(h)->hash_rwlocks[(idx) & (DBUF_RWLOCKS-1)])
typedef struct dbuf_hash_table {
	uint64_t hash_table_mask;
	dmu_buf_impl_t **hash_table;
	krwlock_t hash_rwlocks[DBUF_RWLOCKS] ____cacheline_aligned;
} dbuf_hash_table_t;

typedef void (*dbuf_prefetch_fn)(void *, uint64_t, uint64_t, boolean_t);

uint64_t dbuf_whichblock(const struct dnode *di, const int64_t level,
    const uint64_t offset);

void dbuf_create_bonus(struct dnode *dn);
int dbuf_spill_set_blksz(dmu_buf_t *db, uint64_t blksz, dmu_tx_t *tx);

void dbuf_rm_spill(struct dnode *dn, dmu_tx_t *tx);

dmu_buf_impl_t *dbuf_hold(struct dnode *dn, uint64_t blkid, const void *tag);
dmu_buf_impl_t *dbuf_hold_level(struct dnode *dn, int level, uint64_t blkid,
    const void *tag);
int dbuf_hold_impl(struct dnode *dn, uint8_t level, uint64_t blkid,
    boolean_t fail_sparse, boolean_t fail_uncached,
    const void *tag, dmu_buf_impl_t **dbp);

int dbuf_prefetch_impl(struct dnode *dn, int64_t level, uint64_t blkid,
    zio_priority_t prio, arc_flags_t aflags, dbuf_prefetch_fn cb,
    void *arg);
int dbuf_prefetch(struct dnode *dn, int64_t level, uint64_t blkid,
    zio_priority_t prio, arc_flags_t aflags);

void dbuf_add_ref(dmu_buf_impl_t *db, const void *tag);
boolean_t dbuf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t obj,
    uint64_t blkid, const void *tag);
uint64_t dbuf_refcount(dmu_buf_impl_t *db);

void dbuf_rele(dmu_buf_impl_t *db, const void *tag);
void dbuf_rele_and_unlock(dmu_buf_impl_t *db, const void *tag,
    boolean_t evicting);

dmu_buf_impl_t *dbuf_find(struct objset *os, uint64_t object, uint8_t level,
    uint64_t blkid);

int dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags);
void dmu_buf_will_not_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_will_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_fill_done(dmu_buf_t *db, dmu_tx_t *tx);
void dbuf_assign_arcbuf(dmu_buf_impl_t *db, arc_buf_t *buf, dmu_tx_t *tx);
dbuf_dirty_record_t *dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
dbuf_dirty_record_t *dbuf_dirty_lightweight(dnode_t *dn, uint64_t blkid,
    dmu_tx_t *tx);
arc_buf_t *dbuf_loan_arcbuf(dmu_buf_impl_t *db);
void dmu_buf_write_embedded(dmu_buf_t *dbuf, void *data,
    bp_embedded_type_t etype, enum zio_compress comp,
    int uncompressed_size, int compressed_size, int byteorder, dmu_tx_t *tx);

int dmu_lightweight_write_by_dnode(dnode_t *dn, uint64_t offset, abd_t *abd,
    const struct zio_prop *zp, enum zio_flag flags, dmu_tx_t *tx);

void dmu_buf_redact(dmu_buf_t *dbuf, dmu_tx_t *tx);
void dbuf_destroy(dmu_buf_impl_t *db);

void dbuf_unoverride(dbuf_dirty_record_t *dr);
void dbuf_sync_list(list_t *list, int level, dmu_tx_t *tx);
void dbuf_release_bp(dmu_buf_impl_t *db);
db_lock_type_t dmu_buf_lock_parent(dmu_buf_impl_t *db, krw_t rw,
    const void *tag);
void dmu_buf_unlock_parent(dmu_buf_impl_t *db, db_lock_type_t type,
    const void *tag);

void dbuf_free_range(struct dnode *dn, uint64_t start, uint64_t end,
    struct dmu_tx *);

void dbuf_new_size(dmu_buf_impl_t *db, int size, dmu_tx_t *tx);

void dbuf_stats_init(dbuf_hash_table_t *hash);
void dbuf_stats_destroy(void);

int dbuf_dnode_findbp(dnode_t *dn, uint64_t level, uint64_t blkid,
    blkptr_t *bp, uint16_t *datablkszsec, uint8_t *indblkshift);

#define	DB_DNODE(_db)		((_db)->db_dnode_handle->dnh_dnode)
#define	DB_DNODE_LOCK(_db)	((_db)->db_dnode_handle->dnh_zrlock)
#define	DB_DNODE_ENTER(_db)	(zrl_add(&DB_DNODE_LOCK(_db)))
#define	DB_DNODE_EXIT(_db)	(zrl_remove(&DB_DNODE_LOCK(_db)))
#define	DB_DNODE_HELD(_db)	(!zrl_is_zero(&DB_DNODE_LOCK(_db)))

void dbuf_init(void);
void dbuf_fini(void);

boolean_t dbuf_is_metadata(dmu_buf_impl_t *db);

static inline dbuf_dirty_record_t *
dbuf_find_dirty_lte(dmu_buf_impl_t *db, uint64_t txg)
{
	dbuf_dirty_record_t *dr;

	for (dr = list_head(&db->db_dirty_records);
	    dr != NULL && dr->dr_txg > txg;
	    dr = list_next(&db->db_dirty_records, dr))
		continue;
	return (dr);
}

static inline dbuf_dirty_record_t *
dbuf_find_dirty_eq(dmu_buf_impl_t *db, uint64_t txg)
{
	dbuf_dirty_record_t *dr;

	dr = dbuf_find_dirty_lte(db, txg);
	if (dr && dr->dr_txg == txg)
		return (dr);
	return (NULL);
}

#define	DBUF_GET_BUFC_TYPE(_db)	\
	(dbuf_is_metadata(_db) ? ARC_BUFC_METADATA : ARC_BUFC_DATA)

#define	DBUF_IS_CACHEABLE(_db)						\
	((_db)->db_objset->os_primary_cache == ZFS_CACHE_ALL ||		\
	(dbuf_is_metadata(_db) &&					\
	((_db)->db_objset->os_primary_cache == ZFS_CACHE_METADATA)))

boolean_t dbuf_is_l2cacheable(dmu_buf_impl_t *db);

#ifdef ZFS_DEBUG

/*
 * There should be a ## between the string literal and fmt, to make it
 * clear that we're joining two strings together, but gcc does not
 * support that preprocessor token.
 */
#define	dprintf_dbuf(dbuf, fmt, ...) do { \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { \
	char __db_buf[32]; \
	uint64_t __db_obj = (dbuf)->db.db_object; \
	if (__db_obj == DMU_META_DNODE_OBJECT) \
		(void) strlcpy(__db_buf, "mdn", sizeof (__db_buf));	\
	else \
		(void) snprintf(__db_buf, sizeof (__db_buf), "%lld", \
		    (u_longlong_t)__db_obj); \
	dprintf_ds((dbuf)->db_objset->os_dsl_dataset, \
	    "obj=%s lvl=%u blkid=%lld " fmt, \
	    __db_buf, (dbuf)->db_level, \
	    (u_longlong_t)(dbuf)->db_blkid, __VA_ARGS__); \
	} \
} while (0)

#define	dprintf_dbuf_bp(db, bp, fmt, ...) do {			\
	if (zfs_flags & ZFS_DEBUG_DPRINTF) {			\
	char *__blkbuf = kmem_alloc(BP_SPRINTF_LEN, KM_SLEEP);	\
	snprintf_blkptr(__blkbuf, BP_SPRINTF_LEN, bp);		\
	dprintf_dbuf(db, fmt " %s\n", __VA_ARGS__, __blkbuf);	\
	kmem_free(__blkbuf, BP_SPRINTF_LEN);			\
	}							\
} while (0)

#define	DBUF_VERIFY(db)	dbuf_verify(db)

#else

#define	dprintf_dbuf(db, fmt, ...)
#define	dprintf_dbuf_bp(db, bp, fmt, ...)
#define	DBUF_VERIFY(db)

#endif


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DBUF_H */
