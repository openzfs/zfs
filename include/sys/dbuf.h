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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
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
#include <sys/refcount.h>
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
#define	DB_RF_CACHED_ONLY	(1 << 7)

/* BEGIN CSTYLED */
/*
 * The simplified state transition diagram for dbufs looks like:
 *
 *
 *		+-> PARTIAL_FILL <---> PARTIAL-+
 *		|		 	  |    |
 *		+---------->READ_FILL<----[----+
 *		|		^	  |
 *		|		|	  |
 *		|		V	  |
 *		+-----------> READ ------+[-------+
 *		|			 ||	  |
 *		|			 VV	  V
 *  (alloc)-->UNCACHED----------------->FILL--->CACHED----> EVICTING-->(free)
 *		|						^
 *		|						|
 *		+--------------------> NOFILL ------------------+
 *
 *
 * Reader State Transitions:
 * UNCACHED ->	READ:		Access to a block that does not have an
 *				active dbuf.  A read is issued to media
 *				upon an ARC or L2ARC miss.
 *
 * READ -> CACHED:		Data satisfied from the ARC, L2ARC, or
 *				a read of the media.  No writes occurred.
 *
 * PARTIAL -> READ:		Access to a block that has been partially
 *				written but has yet to have the read
 *				needed to resolve the COW fault issued.
 *				The read is issued to media.  The ARC and
 *				L2ARC are not involved since they were
 *				checked for a hit at the time of the first
 *				write to this buffer.
 *
 * Writer State Transitions:
 * UNCACHED ->	FILL:		Access to a block that does not have an
 *				active dbuf.  Writer is filling the entire
 *				block.
 *
 * UNCACHED -> PARTIAL_FILL:	Access to a block that does not have an
 *				active dbuf.  Writer is filling a portion
 *				of the block starting at the beginning or
 *				end.  The read needed to resolve the COW
 *				fault is deferred until we see that the
 *				writer will not fill this whole buffer.
 *
 * UNCACHED -> READ_FILL:	Access to a block that does not have an
 *				active dbuf.  Writer is filling a portion
 *				of the block and we have enough information
 *				to expect that the buffer will not be fully
 *				written.  The read needed to resolve the COW
 *				fault is issued asynchronously.
 *
 * READ -> READ_FILL:		Access to a block that has an active dbuf
 *				and a read has already been issued for the
 *				original buffer contents.  A COW fault may
 *				not have occurred, if the buffer was not
 *				already dirty.	Writer is filling a portion
 *				of the buffer.
 *
 * PARTIAL -> PARTIAL_FILL:	Access to a block that has an active dbuf
 *				with an outstanding COW fault.	Writer is
 *				filling a portion of the block and we have
 *				enough information to expect that the buffer
 *				will eventually be fully written.
 *
 * PARTIAL -> READ_FILL:	Access to a block that has an active dbuf
 *				with an outstanding COW fault.	Writer is
 *				filling a portion of the block and we have
 *				enough information to expect that the buffer
 *				will not be fully written, causing a read
 *				to be issued.
 *
 * PARTIAL -> FILL:		Access to a block that has an active dbuf
 *				with an outstanding COW fault.	Writer is
 *				filling enough of the buffer to avoid the
 *				read for this fault entirely.
 *
 * READ -> FILL:		Access to a block that has an active dbuf
 *				with an outstanding COW fault, and a read
 *				has been issued.  Write is filling enough of
 *				the buffer to obsolete the read.
 *
 * I/O Complete Transitions:
 * FILL -> CACHED:		The thread modifying the buffer has completed
 *				its work. The buffer can now be accessed by
 *				other threads.
 *
 * PARTIAL_FILL -> PARTIAL:	The write thread modifying the buffer has
 *				completed its work. The buffer can now be
 *				accessed by other threads.  No read has been
 *				issued to resolve the COW fault.
 * 
 * READ_FILL -> READ:		The write thread modifying the buffer has
 *				completed its work. The buffer can now be
 *				accessed by other threads. A read is
 *				outstanding to resolve the COW fault.
 *
 * The READ, PARITIAL_FILL, and READ_FILL states indicate the data associated
 * with a dbuf is volatile and a new client must wait for the current consumer
 * to exit the dbuf from that state prior to accessing the data.
 * 
 * The PARITIAL_FILL, PARTIAL, READ_FILL, and READ states are used for
 * deferring any reads required for resolution of Copy-On-Write faults.
 * A PARTIAL dbuf has accumulated write data in its dirty records
 * that must be merged into the existing data for the record once the
 * record is read.	A READ dbuf is a dbuf for which a synchronous or
 * async read has been issued.	If the dbuf has dirty records, this read
 * is required to resolve the COW fault before those dirty records can be
 * committed to disk.  The FILL variants of these two states indicate that
 * either new write data is being added to the dirty records for this dbuf,
 * or the read has completed and the write and read data are being merged.
 *
 * Writers must block on dbufs in any of the FILL states.
 *
 * Synchronous readers must block on dbufs in the READ,	 and any
 * of the FILL states.	Further, a reader must transition a dbuf from the
 * UNCACHED or PARTIAL state to the READ state by issuing a read, before
 * blocking.
 *
 * The transition from PARTIAL to READ is also triggered by writers that
 * perform a discontiguous write to the buffer, meaning that there is
 * little chance for a latter writer to completely fill the buffer.
 * Since the read cannot be avoided, it is issued immediately.
 */

typedef enum dbuf_states {
	DB_SEARCH 		= 0x1000,

        /*
	 * Dbuf has no valid data.
	 */
	DB_UNCACHED		= 0x01,

	/*
	 * The Dbuf's contents are being modified by an active thread.
	 * This state can be combined with PARTIAL or READ.  When
	 * just in the DB_FILL state, the entire buffer's contents are
	 * being supplied by the writer.  When combined with the other
	 * states, the buffer is only being partially dirtied.
	 */
	DB_FILL			= 0x02,

	/*
	 * Dbuf has been partially dirtied by writers.  No read has been
	 * issued to resolve the COW fault.
	 */
	DB_PARTIAL		= 0x04,

	/*
	 * A NULL DBuf associated with swap backing store.
	 */
	DB_NOFILL		= 0x08,

	/*
	 * A read has been issued for an uncached buffer with no
	 * outstanding dirty data (i.e. Not PARTIAL).
	 */
	DB_READ			= 0x10,

	/*
	 * The entire contents of this dbuf are valid.  The buffer
	 * may still be dirty.
	 */
	DB_CACHED		= 0x20,

	/*
	 * The Dbuf is in the process of being freed.
	 */
	DB_EVICTING		= 0x40,

	/*
	 * Dbuf has been partially dirtied by writers and a
	 * thread is actively modifying the dbuf.
	 */
	DB_PARTIAL_FILL		= DB_PARTIAL|DB_FILL,

	/*
	 * Dbuf has been partially dirtied by writers, a read
	 * has been issued to resolve the COW fault, and a
	 * thread is actively modifying the dbuf.
	 */
	DB_READ_FILL		= DB_READ|DB_FILL
} dbuf_states_t;

	/* END CSTYLED */

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


typedef struct dbuf_dirty_indirect_record {
	kmutex_t dr_mtx;	/* Protects the children. */
	list_t dr_children;	/* List of our dirty children. */
} dbuf_dirty_indirect_record_t;

typedef struct dbuf_dirty_range {
	list_node_t write_range_link;
	int start;
	int end;
	int size;
} dbuf_dirty_range_t;

typedef struct dbuf_dirty_leaf_record {
	/*
	 * dr_data is set when we dirty the buffer so that we can retain the
	 * pointer even if it gets COW'd in a subsequent transaction group.
	 */
	arc_buf_t *dr_data;
	blkptr_t dr_overridden_by;
	override_states_t dr_override_state;
	uint8_t dr_copies;

	/*
	 * List of the ranges that dr_data's contents are valid for.
	 * Used when not all of dr_data is valid, as it may be if writes
	 * only cover part of it, and no read has filled in the gaps yet.
	 */
	list_t write_ranges;
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
} dbuf_dirty_leaf_record_t;

typedef union dbuf_dirty_record_types {
	struct dbuf_dirty_indirect_record di;
	struct dbuf_dirty_leaf_record dl;
} dbuf_dirty_record_types_t;

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

	/* pointer to parent dirty record */
	struct dbuf_dirty_record *dr_parent;

	/* How much space was changed to dsl_pool_dirty_space() for this? */
	unsigned int dr_accounted;

	/* A copy of the bp that points to us */
	blkptr_t dr_bp_copy;
	union dbuf_dirty_record_types dt;
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

	uint8_t db_advice:3;
	uint8_t db_is_ephemeral:1;
} dmu_buf_impl_t;

/* Note: the dbuf hash table is exposed only for the mdb module */
#define	DBUF_MUTEXES 8192
#define	DBUF_HASH_MUTEX(h, idx) (&(h)->hash_mutexes[(idx) & (DBUF_MUTEXES-1)])
typedef struct dbuf_hash_table {
	uint64_t hash_table_mask;
	dmu_buf_impl_t **hash_table;
	kmutex_t hash_mutexes[DBUF_MUTEXES];
} dbuf_hash_table_t;

uint64_t dbuf_whichblock(const struct dnode *di, const int64_t level,
    const uint64_t offset);

void dbuf_create_bonus(struct dnode *dn);
void dbuf_dirty_record_cleanup_ranges(dbuf_dirty_record_t *dr);
int dbuf_spill_set_blksz(dmu_buf_t *db, uint64_t blksz, dmu_tx_t *tx);

void dbuf_rm_spill(struct dnode *dn, dmu_tx_t *tx);

dmu_buf_impl_t *dbuf_hold(struct dnode *dn, uint64_t blkid, void *tag);
dmu_buf_impl_t *dbuf_hold_level(struct dnode *dn, int level, uint64_t blkid,
    void *tag);
int dbuf_hold_impl(struct dnode *dn, uint8_t level, uint64_t blkid,
    boolean_t fail_sparse, boolean_t fail_uncached,
    void *tag, dmu_buf_impl_t **dbp);

void dbuf_prefetch(struct dnode *dn, int64_t level, uint64_t blkid,
    zio_priority_t prio, arc_flags_t aflags);

void dbuf_add_ref(dmu_buf_impl_t *db, void *tag);
boolean_t dbuf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t obj,
    uint64_t blkid, void *tag);
uint64_t dbuf_refcount(dmu_buf_impl_t *db);

void dbuf_rele(dmu_buf_impl_t *db, void *tag);
void dbuf_rele_and_unlock(dmu_buf_impl_t *db, void *tag, boolean_t evicting);

dmu_buf_impl_t *dbuf_find(struct objset *os, uint64_t object, uint8_t level,
    uint64_t blkid);

int dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags);
void dbuf_transition_to_read(dmu_buf_impl_t *db);
void dmu_buf_will_not_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_will_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_fill_done(dmu_buf_t *db, dmu_tx_t *tx);
void dbuf_assign_arcbuf(dmu_buf_impl_t *db, arc_buf_t *buf, dmu_tx_t *tx);
dbuf_dirty_record_t *dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
arc_buf_t *dbuf_loan_arcbuf(dmu_buf_impl_t *db);
void dmu_buf_write_embedded(dmu_buf_t *dbuf, void *data,
    bp_embedded_type_t etype, enum zio_compress comp,
    int uncompressed_size, int compressed_size, int byteorder, dmu_tx_t *tx);

void dmu_buf_redact(dmu_buf_t *dbuf, dmu_tx_t *tx);
void dbuf_destroy(dmu_buf_impl_t *db);

void dbuf_unoverride(dbuf_dirty_record_t *dr);
void dbuf_sync_list(list_t *list, int level, dmu_tx_t *tx);
void dbuf_release_bp(dmu_buf_impl_t *db);
db_lock_type_t dmu_buf_lock_parent(dmu_buf_impl_t *db, krw_t rw, void *tag);
void dmu_buf_unlock_parent(dmu_buf_impl_t *db, db_lock_type_t type, void *tag);

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
void dbuf_dirty_record_cleanup_ranges(dbuf_dirty_record_t *dr);

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

#define	DBUF_IS_L2CACHEABLE(_db)					\
	((_db)->db_objset->os_secondary_cache == ZFS_CACHE_ALL ||	\
	(dbuf_is_metadata(_db) &&					\
	((_db)->db_objset->os_secondary_cache == ZFS_CACHE_METADATA)))

#define	DNODE_LEVEL_IS_L2CACHEABLE(_dn, _level)				\
	((_dn)->dn_objset->os_secondary_cache == ZFS_CACHE_ALL ||	\
	(((_level) > 0 ||						\
	DMU_OT_IS_METADATA((_dn)->dn_handle->dnh_dnode->dn_type)) &&	\
	((_dn)->dn_objset->os_secondary_cache == ZFS_CACHE_METADATA)))

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
		(void) strcpy(__db_buf, "mdn"); \
	else \
		(void) snprintf(__db_buf, sizeof (__db_buf), "%lld", \
		    (u_longlong_t)__db_obj); \
	dprintf_ds((dbuf)->db_objset->os_dsl_dataset, \
	    "obj=%s lvl=%u blkid=%lld " fmt, \
	    __db_buf, (dbuf)->db_level, \
	    (u_longlong_t)(dbuf)->db_blkid, __VA_ARGS__); \
	} \
_NOTE(CONSTCOND) } while (0)

#define	dprintf_dbuf_bp(db, bp, fmt, ...) do {			\
	if (zfs_flags & ZFS_DEBUG_DPRINTF) {			\
	char *__blkbuf = kmem_alloc(BP_SPRINTF_LEN, KM_SLEEP);	\
	snprintf_blkptr(__blkbuf, BP_SPRINTF_LEN, bp);		\
	dprintf_dbuf(db, fmt " %s\n", __VA_ARGS__, __blkbuf);	\
	kmem_free(__blkbuf, BP_SPRINTF_LEN);			\
	}							\
_NOTE(CONSTCOND) } while (0)

#else

#define	dprintf_dbuf(db, fmt, ...)
#define	dprintf_dbuf_bp(db, bp, fmt, ...)

#endif


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DBUF_H */
