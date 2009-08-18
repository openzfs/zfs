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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_DMU_H
#define	_SYS_DMU_H

/*
 * This file describes the interface that the DMU provides for its
 * consumers.
 *
 * The DMU also interacts with the SPA.  That interface is described in
 * dmu_spa.h.
 */

#include <sys/inttypes.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct uio;
struct page;
struct vnode;
struct spa;
struct zilog;
struct zio;
struct blkptr;
struct zap_cursor;
struct dsl_dataset;
struct dsl_pool;
struct dnode;
struct drr_begin;
struct drr_end;
struct zbookmark;
struct spa;
struct nvlist;
struct objset_impl;
struct arc_buf;

typedef struct objset objset_t;
typedef struct dmu_tx dmu_tx_t;
typedef struct dsl_dir dsl_dir_t;

typedef enum dmu_object_type {
	DMU_OT_NONE,
	/* general: */
	DMU_OT_OBJECT_DIRECTORY,	/* ZAP */
	DMU_OT_OBJECT_ARRAY,		/* UINT64 */
	DMU_OT_PACKED_NVLIST,		/* UINT8 (XDR by nvlist_pack/unpack) */
	DMU_OT_PACKED_NVLIST_SIZE,	/* UINT64 */
	DMU_OT_BPLIST,			/* UINT64 */
	DMU_OT_BPLIST_HDR,		/* UINT64 */
	/* spa: */
	DMU_OT_SPACE_MAP_HEADER,	/* UINT64 */
	DMU_OT_SPACE_MAP,		/* UINT64 */
	/* zil: */
	DMU_OT_INTENT_LOG,		/* UINT64 */
	/* dmu: */
	DMU_OT_DNODE,			/* DNODE */
	DMU_OT_OBJSET,			/* OBJSET */
	/* dsl: */
	DMU_OT_DSL_DIR,			/* UINT64 */
	DMU_OT_DSL_DIR_CHILD_MAP,	/* ZAP */
	DMU_OT_DSL_DS_SNAP_MAP,		/* ZAP */
	DMU_OT_DSL_PROPS,		/* ZAP */
	DMU_OT_DSL_DATASET,		/* UINT64 */
	/* zpl: */
	DMU_OT_ZNODE,			/* ZNODE */
	DMU_OT_OLDACL,			/* Old ACL */
	DMU_OT_PLAIN_FILE_CONTENTS,	/* UINT8 */
	DMU_OT_DIRECTORY_CONTENTS,	/* ZAP */
	DMU_OT_MASTER_NODE,		/* ZAP */
	DMU_OT_UNLINKED_SET,		/* ZAP */
	/* zvol: */
	DMU_OT_ZVOL,			/* UINT8 */
	DMU_OT_ZVOL_PROP,		/* ZAP */
	/* other; for testing only! */
	DMU_OT_PLAIN_OTHER,		/* UINT8 */
	DMU_OT_UINT64_OTHER,		/* UINT64 */
	DMU_OT_ZAP_OTHER,		/* ZAP */
	/* new object types: */
	DMU_OT_ERROR_LOG,		/* ZAP */
	DMU_OT_SPA_HISTORY,		/* UINT8 */
	DMU_OT_SPA_HISTORY_OFFSETS,	/* spa_his_phys_t */
	DMU_OT_POOL_PROPS,		/* ZAP */
	DMU_OT_DSL_PERMS,		/* ZAP */
	DMU_OT_ACL,			/* ACL */
	DMU_OT_SYSACL,			/* SYSACL */
	DMU_OT_FUID,			/* FUID table (Packed NVLIST UINT8) */
	DMU_OT_FUID_SIZE,		/* FUID table size UINT64 */
	DMU_OT_NEXT_CLONES,		/* ZAP */
	DMU_OT_SCRUB_QUEUE,		/* ZAP */
	DMU_OT_USERGROUP_USED,		/* ZAP */
	DMU_OT_USERGROUP_QUOTA,		/* ZAP */
	DMU_OT_USERREFS,		/* ZAP */
	DMU_OT_NUMTYPES
} dmu_object_type_t;

typedef enum dmu_objset_type {
	DMU_OST_NONE,
	DMU_OST_META,
	DMU_OST_ZFS,
	DMU_OST_ZVOL,
	DMU_OST_OTHER,			/* For testing only! */
	DMU_OST_ANY,			/* Be careful! */
	DMU_OST_NUMTYPES
} dmu_objset_type_t;

void byteswap_uint64_array(void *buf, size_t size);
void byteswap_uint32_array(void *buf, size_t size);
void byteswap_uint16_array(void *buf, size_t size);
void byteswap_uint8_array(void *buf, size_t size);
void zap_byteswap(void *buf, size_t size);
void zfs_oldacl_byteswap(void *buf, size_t size);
void zfs_acl_byteswap(void *buf, size_t size);
void zfs_znode_byteswap(void *buf, size_t size);

#define	DS_MODE_NOHOLD		0	/* internal use only */
#define	DS_MODE_USER		1	/* simple access, no special needs */
#define	DS_MODE_OWNER		2	/* the "main" access, e.g. a mount */
#define	DS_MODE_TYPE_MASK	0x3
#define	DS_MODE_TYPE(x)		((x) & DS_MODE_TYPE_MASK)
#define	DS_MODE_READONLY	0x8
#define	DS_MODE_IS_READONLY(x)	((x) & DS_MODE_READONLY)
#define	DS_MODE_INCONSISTENT	0x10
#define	DS_MODE_IS_INCONSISTENT(x)	((x) & DS_MODE_INCONSISTENT)

#define	DS_FIND_SNAPSHOTS	(1<<0)
#define	DS_FIND_CHILDREN	(1<<1)

/*
 * The maximum number of bytes that can be accessed as part of one
 * operation, including metadata.
 */
#define	DMU_MAX_ACCESS (10<<20) /* 10MB */
#define	DMU_MAX_DELETEBLKCNT (20480) /* ~5MB of indirect blocks */

#define	DMU_USERUSED_OBJECT	(-1ULL)
#define	DMU_GROUPUSED_OBJECT	(-2ULL)

/*
 * Public routines to create, destroy, open, and close objsets.
 */
int dmu_objset_open(const char *name, dmu_objset_type_t type, int mode,
    objset_t **osp);
int dmu_objset_open_ds(struct dsl_dataset *ds, dmu_objset_type_t type,
    objset_t **osp);
void dmu_objset_close(objset_t *os);
int dmu_objset_evict_dbufs(objset_t *os);
int dmu_objset_create(const char *name, dmu_objset_type_t type,
    objset_t *clone_parent, uint64_t flags,
    void (*func)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx), void *arg);
int dmu_objset_destroy(const char *name, boolean_t defer);
int dmu_snapshots_destroy(char *fsname, char *snapname, boolean_t defer);
int dmu_objset_rollback(objset_t *os);
int dmu_objset_snapshot(char *fsname, char *snapname, struct nvlist *props,
    boolean_t recursive);
int dmu_objset_rename(const char *name, const char *newname,
    boolean_t recursive);
int dmu_objset_find(char *name, int func(char *, void *), void *arg,
    int flags);
void dmu_objset_byteswap(void *buf, size_t size);

typedef struct dmu_buf {
	uint64_t db_object;		/* object that this buffer is part of */
	uint64_t db_offset;		/* byte offset in this object */
	uint64_t db_size;		/* size of buffer in bytes */
	void *db_data;			/* data in buffer */
} dmu_buf_t;

typedef void dmu_buf_evict_func_t(struct dmu_buf *db, void *user_ptr);

/*
 * The names of zap entries in the DIRECTORY_OBJECT of the MOS.
 */
#define	DMU_POOL_DIRECTORY_OBJECT	1
#define	DMU_POOL_CONFIG			"config"
#define	DMU_POOL_ROOT_DATASET		"root_dataset"
#define	DMU_POOL_SYNC_BPLIST		"sync_bplist"
#define	DMU_POOL_ERRLOG_SCRUB		"errlog_scrub"
#define	DMU_POOL_ERRLOG_LAST		"errlog_last"
#define	DMU_POOL_SPARES			"spares"
#define	DMU_POOL_DEFLATE		"deflate"
#define	DMU_POOL_HISTORY		"history"
#define	DMU_POOL_PROPS			"pool_props"
#define	DMU_POOL_L2CACHE		"l2cache"

/* 4x8 zbookmark_t */
#define	DMU_POOL_SCRUB_BOOKMARK		"scrub_bookmark"
/* 1x8 zap obj DMU_OT_SCRUB_QUEUE */
#define	DMU_POOL_SCRUB_QUEUE		"scrub_queue"
/* 1x8 txg */
#define	DMU_POOL_SCRUB_MIN_TXG		"scrub_min_txg"
/* 1x8 txg */
#define	DMU_POOL_SCRUB_MAX_TXG		"scrub_max_txg"
/* 1x4 enum scrub_func */
#define	DMU_POOL_SCRUB_FUNC		"scrub_func"
/* 1x8 count */
#define	DMU_POOL_SCRUB_ERRORS		"scrub_errors"

/*
 * Allocate an object from this objset.  The range of object numbers
 * available is (0, DN_MAX_OBJECT).  Object 0 is the meta-dnode.
 *
 * The transaction must be assigned to a txg.  The newly allocated
 * object will be "held" in the transaction (ie. you can modify the
 * newly allocated object in this transaction).
 *
 * dmu_object_alloc() chooses an object and returns it in *objectp.
 *
 * dmu_object_claim() allocates a specific object number.  If that
 * number is already allocated, it fails and returns EEXIST.
 *
 * Return 0 on success, or ENOSPC or EEXIST as specified above.
 */
uint64_t dmu_object_alloc(objset_t *os, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len, dmu_tx_t *tx);
int dmu_object_claim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len, dmu_tx_t *tx);
int dmu_object_reclaim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen);

/*
 * Free an object from this objset.
 *
 * The object's data will be freed as well (ie. you don't need to call
 * dmu_free(object, 0, -1, tx)).
 *
 * The object need not be held in the transaction.
 *
 * If there are any holds on this object's buffers (via dmu_buf_hold()),
 * or tx holds on the object (via dmu_tx_hold_object()), you can not
 * free it; it fails and returns EBUSY.
 *
 * If the object is not allocated, it fails and returns ENOENT.
 *
 * Return 0 on success, or EBUSY or ENOENT as specified above.
 */
int dmu_object_free(objset_t *os, uint64_t object, dmu_tx_t *tx);

/*
 * Find the next allocated or free object.
 *
 * The objectp parameter is in-out.  It will be updated to be the next
 * object which is allocated.  Ignore objects which have not been
 * modified since txg.
 *
 * XXX Can only be called on a objset with no dirty data.
 *
 * Returns 0 on success, or ENOENT if there are no more objects.
 */
int dmu_object_next(objset_t *os, uint64_t *objectp,
    boolean_t hole, uint64_t txg);

/*
 * Set the data blocksize for an object.
 *
 * The object cannot have any blocks allcated beyond the first.  If
 * the first block is allocated already, the new size must be greater
 * than the current block size.  If these conditions are not met,
 * ENOTSUP will be returned.
 *
 * Returns 0 on success, or EBUSY if there are any holds on the object
 * contents, or ENOTSUP as described above.
 */
int dmu_object_set_blocksize(objset_t *os, uint64_t object, uint64_t size,
    int ibs, dmu_tx_t *tx);

/*
 * Set the checksum property on a dnode.  The new checksum algorithm will
 * apply to all newly written blocks; existing blocks will not be affected.
 */
void dmu_object_set_checksum(objset_t *os, uint64_t object, uint8_t checksum,
    dmu_tx_t *tx);

/*
 * Set the compress property on a dnode.  The new compression algorithm will
 * apply to all newly written blocks; existing blocks will not be affected.
 */
void dmu_object_set_compress(objset_t *os, uint64_t object, uint8_t compress,
    dmu_tx_t *tx);

/*
 * Decide how many copies of a given block we should make.  Can be from
 * 1 to SPA_DVAS_PER_BP.
 */
int dmu_get_replication_level(struct objset_impl *, struct zbookmark *zb,
    dmu_object_type_t ot);
/*
 * The bonus data is accessed more or less like a regular buffer.
 * You must dmu_bonus_hold() to get the buffer, which will give you a
 * dmu_buf_t with db_offset==-1ULL, and db_size = the size of the bonus
 * data.  As with any normal buffer, you must call dmu_buf_read() to
 * read db_data, dmu_buf_will_dirty() before modifying it, and the
 * object must be held in an assigned transaction before calling
 * dmu_buf_will_dirty.  You may use dmu_buf_set_user() on the bonus
 * buffer as well.  You must release your hold with dmu_buf_rele().
 */
int dmu_bonus_hold(objset_t *os, uint64_t object, void *tag, dmu_buf_t **);
int dmu_bonus_max(void);
int dmu_set_bonus(dmu_buf_t *, int, dmu_tx_t *);

/*
 * Obtain the DMU buffer from the specified object which contains the
 * specified offset.  dmu_buf_hold() puts a "hold" on the buffer, so
 * that it will remain in memory.  You must release the hold with
 * dmu_buf_rele().  You musn't access the dmu_buf_t after releasing your
 * hold.  You must have a hold on any dmu_buf_t* you pass to the DMU.
 *
 * You must call dmu_buf_read, dmu_buf_will_dirty, or dmu_buf_will_fill
 * on the returned buffer before reading or writing the buffer's
 * db_data.  The comments for those routines describe what particular
 * operations are valid after calling them.
 *
 * The object number must be a valid, allocated object number.
 */
int dmu_buf_hold(objset_t *os, uint64_t object, uint64_t offset,
    void *tag, dmu_buf_t **);
void dmu_buf_add_ref(dmu_buf_t *db, void* tag);
void dmu_buf_rele(dmu_buf_t *db, void *tag);
uint64_t dmu_buf_refcount(dmu_buf_t *db);

/*
 * dmu_buf_hold_array holds the DMU buffers which contain all bytes in a
 * range of an object.  A pointer to an array of dmu_buf_t*'s is
 * returned (in *dbpp).
 *
 * dmu_buf_rele_array releases the hold on an array of dmu_buf_t*'s, and
 * frees the array.  The hold on the array of buffers MUST be released
 * with dmu_buf_rele_array.  You can NOT release the hold on each buffer
 * individually with dmu_buf_rele.
 */
int dmu_buf_hold_array_by_bonus(dmu_buf_t *db, uint64_t offset,
    uint64_t length, int read, void *tag, int *numbufsp, dmu_buf_t ***dbpp);
void dmu_buf_rele_array(dmu_buf_t **, int numbufs, void *tag);

/*
 * Returns NULL on success, or the existing user ptr if it's already
 * been set.
 *
 * user_ptr is for use by the user and can be obtained via dmu_buf_get_user().
 *
 * user_data_ptr_ptr should be NULL, or a pointer to a pointer which
 * will be set to db->db_data when you are allowed to access it.  Note
 * that db->db_data (the pointer) can change when you do dmu_buf_read(),
 * dmu_buf_tryupgrade(), dmu_buf_will_dirty(), or dmu_buf_will_fill().
 * *user_data_ptr_ptr will be set to the new value when it changes.
 *
 * If non-NULL, pageout func will be called when this buffer is being
 * excised from the cache, so that you can clean up the data structure
 * pointed to by user_ptr.
 *
 * dmu_evict_user() will call the pageout func for all buffers in a
 * objset with a given pageout func.
 */
void *dmu_buf_set_user(dmu_buf_t *db, void *user_ptr, void *user_data_ptr_ptr,
    dmu_buf_evict_func_t *pageout_func);
/*
 * set_user_ie is the same as set_user, but request immediate eviction
 * when hold count goes to zero.
 */
void *dmu_buf_set_user_ie(dmu_buf_t *db, void *user_ptr,
    void *user_data_ptr_ptr, dmu_buf_evict_func_t *pageout_func);
void *dmu_buf_update_user(dmu_buf_t *db_fake, void *old_user_ptr,
    void *user_ptr, void *user_data_ptr_ptr,
    dmu_buf_evict_func_t *pageout_func);
void dmu_evict_user(objset_t *os, dmu_buf_evict_func_t *func);

/*
 * Returns the user_ptr set with dmu_buf_set_user(), or NULL if not set.
 */
void *dmu_buf_get_user(dmu_buf_t *db);

/*
 * Indicate that you are going to modify the buffer's data (db_data).
 *
 * The transaction (tx) must be assigned to a txg (ie. you've called
 * dmu_tx_assign()).  The buffer's object must be held in the tx
 * (ie. you've called dmu_tx_hold_object(tx, db->db_object)).
 */
void dmu_buf_will_dirty(dmu_buf_t *db, dmu_tx_t *tx);

/*
 * Tells if the given dbuf is freeable.
 */
boolean_t dmu_buf_freeable(dmu_buf_t *);

/*
 * You must create a transaction, then hold the objects which you will
 * (or might) modify as part of this transaction.  Then you must assign
 * the transaction to a transaction group.  Once the transaction has
 * been assigned, you can modify buffers which belong to held objects as
 * part of this transaction.  You can't modify buffers before the
 * transaction has been assigned; you can't modify buffers which don't
 * belong to objects which this transaction holds; you can't hold
 * objects once the transaction has been assigned.  You may hold an
 * object which you are going to free (with dmu_object_free()), but you
 * don't have to.
 *
 * You can abort the transaction before it has been assigned.
 *
 * Note that you may hold buffers (with dmu_buf_hold) at any time,
 * regardless of transaction state.
 */

#define	DMU_NEW_OBJECT	(-1ULL)
#define	DMU_OBJECT_END	(-1ULL)

dmu_tx_t *dmu_tx_create(objset_t *os);
void dmu_tx_hold_write(dmu_tx_t *tx, uint64_t object, uint64_t off, int len);
void dmu_tx_hold_free(dmu_tx_t *tx, uint64_t object, uint64_t off,
    uint64_t len);
void dmu_tx_hold_zap(dmu_tx_t *tx, uint64_t object, int add, const char *name);
void dmu_tx_hold_bonus(dmu_tx_t *tx, uint64_t object);
void dmu_tx_abort(dmu_tx_t *tx);
int dmu_tx_assign(dmu_tx_t *tx, uint64_t txg_how);
void dmu_tx_wait(dmu_tx_t *tx);
void dmu_tx_commit(dmu_tx_t *tx);

/*
 * Free up the data blocks for a defined range of a file.  If size is
 * zero, the range from offset to end-of-file is freed.
 */
int dmu_free_range(objset_t *os, uint64_t object, uint64_t offset,
	uint64_t size, dmu_tx_t *tx);
int dmu_free_long_range(objset_t *os, uint64_t object, uint64_t offset,
	uint64_t size);
int dmu_free_object(objset_t *os, uint64_t object);

/*
 * Convenience functions.
 *
 * Canfail routines will return 0 on success, or an errno if there is a
 * nonrecoverable I/O error.
 */
#define	DMU_READ_PREFETCH	0 /* prefetch */
#define	DMU_READ_NO_PREFETCH	1 /* don't prefetch */
int dmu_read(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
	void *buf, uint32_t flags);
void dmu_write(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
	const void *buf, dmu_tx_t *tx);
void dmu_prealloc(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
	dmu_tx_t *tx);
int dmu_read_uio(objset_t *os, uint64_t object, struct uio *uio, uint64_t size);
int dmu_write_uio(objset_t *os, uint64_t object, struct uio *uio, uint64_t size,
    dmu_tx_t *tx);
int dmu_write_pages(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t size, struct page *pp, dmu_tx_t *tx);
struct arc_buf *dmu_request_arcbuf(dmu_buf_t *handle, int size);
void dmu_return_arcbuf(struct arc_buf *buf);
void dmu_assign_arcbuf(dmu_buf_t *handle, uint64_t offset, struct arc_buf *buf,
    dmu_tx_t *tx);

extern int zfs_prefetch_disable;

/*
 * Asynchronously try to read in the data.
 */
void dmu_prefetch(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t len);

typedef struct dmu_object_info {
	/* All sizes are in bytes. */
	uint32_t doi_data_block_size;
	uint32_t doi_metadata_block_size;
	uint64_t doi_bonus_size;
	dmu_object_type_t doi_type;
	dmu_object_type_t doi_bonus_type;
	uint8_t doi_indirection;		/* 2 = dnode->indirect->data */
	uint8_t doi_checksum;
	uint8_t doi_compress;
	uint8_t doi_pad[5];
	/* Values below are number of 512-byte blocks. */
	uint64_t doi_physical_blks;		/* data + metadata */
	uint64_t doi_max_block_offset;
} dmu_object_info_t;

typedef void arc_byteswap_func_t(void *buf, size_t size);

typedef struct dmu_object_type_info {
	arc_byteswap_func_t	*ot_byteswap;
	boolean_t		ot_metadata;
	char			*ot_name;
} dmu_object_type_info_t;

extern const dmu_object_type_info_t dmu_ot[DMU_OT_NUMTYPES];

/*
 * Get information on a DMU object.
 *
 * Return 0 on success or ENOENT if object is not allocated.
 *
 * If doi is NULL, just indicates whether the object exists.
 */
int dmu_object_info(objset_t *os, uint64_t object, dmu_object_info_t *doi);
void dmu_object_info_from_dnode(struct dnode *dn, dmu_object_info_t *doi);
void dmu_object_info_from_db(dmu_buf_t *db, dmu_object_info_t *doi);
void dmu_object_size_from_db(dmu_buf_t *db, uint32_t *blksize,
    u_longlong_t *nblk512);

typedef struct dmu_objset_stats {
	uint64_t dds_num_clones; /* number of clones of this */
	uint64_t dds_creation_txg;
	uint64_t dds_guid;
	dmu_objset_type_t dds_type;
	uint8_t dds_is_snapshot;
	uint8_t dds_inconsistent;
	char dds_origin[MAXNAMELEN];
} dmu_objset_stats_t;

/*
 * Get stats on a dataset.
 */
void dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat);

/*
 * Add entries to the nvlist for all the objset's properties.  See
 * zfs_prop_table[] and zfs(1m) for details on the properties.
 */
void dmu_objset_stats(objset_t *os, struct nvlist *nv);

/*
 * Get the space usage statistics for statvfs().
 *
 * refdbytes is the amount of space "referenced" by this objset.
 * availbytes is the amount of space available to this objset, taking
 * into account quotas & reservations, assuming that no other objsets
 * use the space first.  These values correspond to the 'referenced' and
 * 'available' properties, described in the zfs(1m) manpage.
 *
 * usedobjs and availobjs are the number of objects currently allocated,
 * and available.
 */
void dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp);

/*
 * The fsid_guid is a 56-bit ID that can change to avoid collisions.
 * (Contrast with the ds_guid which is a 64-bit ID that will never
 * change, so there is a small probability that it will collide.)
 */
uint64_t dmu_objset_fsid_guid(objset_t *os);

int dmu_objset_is_snapshot(objset_t *os);

extern struct spa *dmu_objset_spa(objset_t *os);
extern struct zilog *dmu_objset_zil(objset_t *os);
extern struct dsl_pool *dmu_objset_pool(objset_t *os);
extern struct dsl_dataset *dmu_objset_ds(objset_t *os);
extern void dmu_objset_name(objset_t *os, char *buf);
extern dmu_objset_type_t dmu_objset_type(objset_t *os);
extern uint64_t dmu_objset_id(objset_t *os);
extern int dmu_snapshot_list_next(objset_t *os, int namelen, char *name,
    uint64_t *id, uint64_t *offp, boolean_t *case_conflict);
extern int dmu_snapshot_realname(objset_t *os, char *name, char *real,
    int maxlen, boolean_t *conflict);
extern int dmu_dir_list_next(objset_t *os, int namelen, char *name,
    uint64_t *idp, uint64_t *offp);

typedef void objset_used_cb_t(objset_t *os, dmu_object_type_t bonustype,
    void *oldbonus, void *newbonus, uint64_t oldused, uint64_t newused,
    dmu_tx_t *tx);
extern void dmu_objset_register_type(dmu_objset_type_t ost,
    objset_used_cb_t *cb);
extern void dmu_objset_set_user(objset_t *os, void *user_ptr);
extern void *dmu_objset_get_user(objset_t *os);

/*
 * Return the txg number for the given assigned transaction.
 */
uint64_t dmu_tx_get_txg(dmu_tx_t *tx);

/*
 * Synchronous write.
 * If a parent zio is provided this function initiates a write on the
 * provided buffer as a child of the parent zio.
 * In the absence of a parent zio, the write is completed synchronously.
 * At write completion, blk is filled with the bp of the written block.
 * Note that while the data covered by this function will be on stable
 * storage when the write completes this new data does not become a
 * permanent part of the file until the associated transaction commits.
 */
typedef void dmu_sync_cb_t(dmu_buf_t *db, void *arg);
int dmu_sync(struct zio *zio, dmu_buf_t *db,
    struct blkptr *bp, uint64_t txg, dmu_sync_cb_t *done, void *arg);

/*
 * Find the next hole or data block in file starting at *off
 * Return found offset in *off. Return ESRCH for end of file.
 */
int dmu_offset_next(objset_t *os, uint64_t object, boolean_t hole,
    uint64_t *off);

/*
 * Initial setup and final teardown.
 */
extern void dmu_init(void);
extern void dmu_fini(void);

typedef void (*dmu_traverse_cb_t)(objset_t *os, void *arg, struct blkptr *bp,
    uint64_t object, uint64_t offset, int len);
void dmu_traverse_objset(objset_t *os, uint64_t txg_start,
    dmu_traverse_cb_t cb, void *arg);

int dmu_sendbackup(objset_t *tosnap, objset_t *fromsnap, boolean_t fromorigin,
    struct vnode *vp, offset_t *off);

typedef struct dmu_recv_cookie {
	/*
	 * This structure is opaque!
	 *
	 * If logical and real are different, we are recving the stream
	 * into the "real" temporary clone, and then switching it with
	 * the "logical" target.
	 */
	struct dsl_dataset *drc_logical_ds;
	struct dsl_dataset *drc_real_ds;
	struct drr_begin *drc_drrb;
	char *drc_tosnap;
	boolean_t drc_newfs;
	boolean_t drc_force;
} dmu_recv_cookie_t;

int dmu_recv_begin(char *tofs, char *tosnap, struct drr_begin *,
    boolean_t force, objset_t *origin, dmu_recv_cookie_t *);
int dmu_recv_stream(dmu_recv_cookie_t *drc, struct vnode *vp, offset_t *voffp);
int dmu_recv_end(dmu_recv_cookie_t *drc);

/* CRC64 table */
#define	ZFS_CRC64_POLY	0xC96C5795D7870F42ULL	/* ECMA-182, reflected form */
extern uint64_t zfs_crc64_table[256];

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DMU_H */
