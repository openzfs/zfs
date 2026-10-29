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
 * Copyright (c) 2026, Patrick Domack. All rights reserved.
 */

#ifndef	_SYS_DSL_CLONEDUP_H
#define	_SYS_DSL_CLONEDUP_H

#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/bitops.h>
#include <sys/zthr.h>
#include <sys/wmsum.h>
#include <sys/txg.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_pool;
struct dnode_phys;
struct dsl_scan_phys;

typedef pool_clonedup_phase_t dsl_clonedup_phase_t;

/*
 * Progress of a clonedup run, stored as a uint64 array under
 * DMU_POOL_CLONEDUP_SCAN in the pool directory.  Every member is a
 * uint64_t so the array byteswaps as a whole.  Readers accept a
 * shorter or longer stored array and only use the members they know.
 */
typedef struct dsl_clonedup_phys {
	uint64_t dclp_version;
	uint64_t dclp_state;		/* dsl_scan_state_t */
	uint64_t dclp_flags;		/* DSF_CLONEDUP_* */
	uint64_t dclp_phase;		/* pool_clonedup_phase_t */
	uint64_t dclp_partition_shift;	/* log2 of partition count */
	uint64_t dclp_partition;	/* partition being worked */
	uint64_t dclp_min_txg;
	uint64_t dclp_max_txg;
	uint64_t dclp_start_time;
	uint64_t dclp_end_time;
	/* max_txg of the last complete run */
	uint64_t dclp_last_txg;
	uint64_t dclp_blocks_examined;
	uint64_t dclp_blocks_indexed;
	uint64_t dclp_groups;
	uint64_t dclp_candidates;
	uint64_t dclp_applied;
	uint64_t dclp_bytes_saved;
	uint64_t dclp_bytes_saved_snapheld;
	uint64_t dclp_skipped_stale;
	uint64_t dclp_skipped_dirty;
	uint64_t dclp_skipped_differs;
	uint64_t dclp_skipped_busy;
	uint64_t dclp_skipped_policy;
	uint64_t dclp_errors;
	/* end of the scrub whose scan record a run took over */
	uint64_t dclp_scrub_end;
} dsl_clonedup_phys_t;

#define	DSL_CLONEDUP_PHYS_VERSION	1
#define	DSL_CLONEDUP_PHYS_NUMINTS \
	(sizeof (dsl_clonedup_phys_t) / sizeof (uint64_t))

/*
 * The block properties a clone must match, in one word, so that
 * entries with equal checksum and equal properties sort next to each
 * other.  The word is blk_prop with only its LSIZE, PSIZE, COMPRESS,
 * CHECKSUM and BYTEORDER fields kept.  The DVA count goes in the TYPE
 * field, which the mask clears.
 */
#define	DCE_PROP_FIELD(lo, len)	BF64_ENCODE(UINT64_MAX, lo, len)
#define	DCE_PROP_BP_MASK \
	(DCE_PROP_FIELD(0, SPA_LSIZEBITS) | \
	DCE_PROP_FIELD(16, SPA_PSIZEBITS) | \
	DCE_PROP_FIELD(32, SPA_COMPRESSBITS) | \
	DCE_PROP_FIELD(40, 8) | DCE_PROP_FIELD(63, 1))
#define	DCE_PROP_SET_NDVAS(p, x)	BF64_SET((p), 48, 2, x)

/* dce_flags and dcs_flags */
#define	DCE_F_MAYBE_SHARED	0x04	/* brt_maybe_exists() true */
#define	DCE_F_ZVOL		0x08	/* DMU_OT_ZVOL object */
#define	DCE_F_SPECIAL		0x10	/* DVA[0] on special class */
#define	DCE_F_REPEAT		0x20	/* block counted already */
#define	DCE_F_REMOVED		0x40	/* DVA[0] on a removed vdev */

/*
 * A block older than the run's window that matched a group.  It is
 * the preferred clone source for that group, unless a copy in the
 * group outranks it as dsl_clonedup_src_serves() says, and hangs off
 * the group's first entry.
 */
typedef struct dsl_clonedup_src {
	dva_t		dcs_dva;	/* DVA[0] */
	uint64_t	dcs_birth;	/* physical birth */
	uint64_t	dcs_objset;
	uint64_t	dcs_object;
	uint64_t	dcs_blkid;
	uint32_t	dcs_blkszsec;
	uint8_t		dcs_dntype;
	uint8_t		dcs_flags;
	uint16_t	dcs_pad;
} dsl_clonedup_src_t;

/*
 * One block born inside the run's window.  Sorted by key, prop, DVA,
 * birth and locator, so a group of equal-content candidates is a run
 * of adjacent entries.
 */
typedef struct dsl_clonedup_entry {
	avl_node_t	dce_node;
	uint64_t	dce_key;	/* cityhash4 of blk_cksum */
	uint64_t	dce_prop;	/* DCE_PROP_* */
	dva_t		dce_dva;	/* DVA[0] */
	uint64_t	dce_birth;	/* physical birth */
	uint64_t	dce_objset;
	uint64_t	dce_object;
	uint64_t	dce_blkid;
	uint32_t	dce_blkszsec;
	uint8_t		dce_dntype;
	uint8_t		dce_flags;
	uint16_t	dce_pad;
	/*
	 * Group source.  In the index only a group's first entry
	 * holds it; in the apply tree every entry holds its own copy.
	 */
	dsl_clonedup_src_t *dce_src;
} dsl_clonedup_entry_t;

struct zfs_clonedup_dst;
struct zfs_clonedup_spin;
struct dsl_dataset;

/* counters exported as the kstat zfs/<pool>/clonedup */
typedef enum dsl_clonedup_kstat_id {
	DCK_VERIFY_READS,	/* blocks read to verify candidates */
	DCK_VERIFY_BYTES,
	DCK_CLONES,		/* blocks redirected to a survivor */
	DCK_YIELDS,		/* yield requests that found a hold */
	DCK_YIELD_WAITS,	/* holds delayed by a yield */
	DCK_INDEX_WALKS,
	DCK_MATCH_WALKS,
	DCK_DST_MOUNTED,	/* destinations opened as files */
	DCK_DST_ZVOL,		/* destinations opened as volumes */
	DCK_DST_OWNED,		/* destinations opened by ownership */
	DCK_BATCHES,		/* transactions carrying clones */
	DCK_KEY_COLLISIONS,	/* key matched, checksum did not */
	DCK_CKSUM_COLLISIONS,	/* checksum matched, data did not */
	DCK_MEM_PEAK,		/* most index bytes */
	DCK_CANCELED_RUNS,	/* runs ended before finishing */
	DCK_WALK_RESUMES,	/* walks resumed in a later txg */
	DCK_NUM
} dsl_clonedup_kstat_id_t;

/* where a block lives, for the apply's debug lines */
typedef struct dsl_clonedup_loc {
	uint64_t	l_objset;
	uint64_t	l_object;
	uint64_t	l_blkid;
} dsl_clonedup_loc_t;

/* one destination block waiting for its batch transaction */
typedef struct dsl_clonedup_pend {
	struct zfs_clonedup_dst *dcp_dst;
	void		*dcp_lock;	/* range lock, or NULL */
	void		*dcp_slock;	/* source lock, or NULL */
	uint64_t	dcp_blkid;
	uint64_t	dcp_blksz;
	uint64_t	dcp_sobject;
	uint64_t	dcp_sblkid;
	blkptr_t	dcp_sbp;
	uint64_t	dcp_dasize;
	uint64_t	dcp_dobject;
	blkptr_t	dcp_dbp;
	struct abd	*dcp_sabd;	/* source, one per group */
	struct abd	*dcp_dabd;
	int		dcp_serr;
	int		dcp_derr;
	boolean_t	dcp_sowner;	/* issued the source read */
	boolean_t	dcp_trusted;	/* checksums prove equality */
	boolean_t	dcp_drop;	/* did not verify */
} dsl_clonedup_pend_t;

/*
 * Clones waiting on one transaction.  A batch stays within one
 * destination dataset and one source dataset, so the pair the yield
 * handshake publishes cannot change while it is open, and the whole
 * batch costs one previous-txg wait instead of one per block.
 */
typedef struct dsl_clonedup_batch {
	uint64_t	dcb_dsobj;	/* destination dataset */
	uint64_t	dcb_srcobj;	/* source dataset */
	objset_t	*dcb_os;	/* destination objset */
	objset_t	*dcb_sos;	/* source objset */
	boolean_t	dcb_dryrun;	/* count, never clone */
	uint_t		dcb_count;
	uint_t		dcb_ndst;	/* handles to close */
	uint_t		dcb_max;
	dsl_clonedup_pend_t *dcb_pend;
	struct zfs_clonedup_dst **dcb_dsts;
	uint64_t	*dcb_dstobj;	/* object of each handle */
	uint32_t	*dcb_dsthash;	/* bucket to slot + 1 */
	uint32_t	*dcb_dstnext;	/* slot to next slot + 1 */
	uint_t		dcb_dstmask;	/* buckets - 1 */
} dsl_clonedup_batch_t;

#define	DCL_MAX_WORKERS	64

typedef struct dsl_clonedup_worker {
	dsl_clonedup_batch_t dcw_batch;
	struct zfs_clonedup_dst *dcw_dst; /* cached destination */
	uint64_t	dcw_dst_objset;
	uint64_t	dcw_dst_object;
	struct dsl_dataset *dcw_src_ds; /* cached source dataset */
	objset_t	*dcw_src_os;
	uint64_t	dcw_src_objset;
	struct zfs_clonedup_spin *dcw_spin; /* source pin */
	uint64_t	dcw_spin_for;	/* kept for this dst */
	/* source for a split group */
	boolean_t	dcw_group_src_valid;
	uint64_t	dcw_group_key;
	uint64_t	dcw_group_prop;
	dsl_clonedup_src_t dcw_group_src;
	uint64_t	dcw_held_src;	/* dataset held, or being */
	uint64_t	dcw_held_dst;	/* acquired, by this worker */
	uint64_t	dcw_claim_dst;	/* claimed dataset */
	avl_tree_t	dcw_dscache;	/* per-dataset answers */
	uint64_t	dcw_dscache_txg;
	boolean_t	dcw_dscache_ready;
	uint64_t	dcw_gen;	/* dcl_gen it serves */
} dsl_clonedup_worker_t;

typedef struct dsl_clonedup {
	struct dsl_pool	*dcl_dp;
	kmutex_t	dcl_lock;
	dsl_clonedup_phys_t dcl_phys;
	dsl_clonedup_phys_t dcl_phys_written;	/* the copy on disk */
	boolean_t	dcl_index_active;
	uint64_t	dcl_gen;	/* bumped by index_destroy */
	uint_t		dcl_apply_busy;	/* workers holding work */
	avl_tree_t	dcl_index;
	uint64_t	dcl_nentries;
	uint64_t	dcl_apply_total;
	uint64_t	dcl_nsrc;
	uint64_t	dcl_mem_used;
	uint64_t	dcl_mem_max;
	/* index entries after the first of their group */
	uint64_t	dcl_nrepeat;
	uint64_t	dcl_mem_peak;	/* DCK_MEM_PEAK */
	uint64_t	dcl_splits;

	/*
	 * What one apply worker holds while it works: its batch, the
	 * handles it has open and the source it picked for a split
	 * group.  These belong to the worker, not the run, so more
	 * than one worker can apply at once.
	 */
	dsl_clonedup_worker_t dcl_w;
	/*
	 * Live apply workers.  A fixed array, so a reader never races
	 * a reallocation.  dcl_nworkers changes under dcl_yield_lock,
	 * which a yield holds while it walks the array, and a worker
	 * is freed only once the count no longer covers it.  Workers
	 * read the array under dcl_lock while it stands still.
	 */
	dsl_clonedup_worker_t *dcl_workers[DCL_MAX_WORKERS];
	uint_t		dcl_nworkers;

	/* apply state the workers share, written under dcl_lock */
	uint64_t	dcl_apply_txg;	/* newest txg counted */
	uint64_t	dcl_apply_in_txg; /* blocks counted in it */
	avl_tree_t	dcl_apply;	/* entries in apply order */
	boolean_t	dcl_apply_ready; /* order pass has run */
	/* per group, see dsl_clonedup_gsrc_t */
	avl_tree_t	dcl_gsrc;
	/* last block taken; birth 0 is none */
	dva_t		dcl_take_dva;
	uint64_t	dcl_take_birth;
	/* apply entries at a locator another already holds */
	uint64_t	dcl_apply_shared;
	struct zthr	*dcl_zthr;
	uint_t		dcl_apply_stop;	/* the zthr saw its cancel */
	uint_t		dcl_apply_running; /* live taskq workers */
	kcondvar_t	dcl_apply_cv;	/* one of them has finished */

	/* yield handshake, see dsl_clonedup_yield_begin() */
	kmutex_t	dcl_yield_lock;
	kcondvar_t	dcl_yield_cv;
	uint_t		dcl_yield_pause; /* operations in progress */
	int64_t		dcl_yield_lease; /* nudged off until */
	int64_t		dcl_yield_since; /* nudges began at */

	kstat_t		*dcl_ksp;
	kstat_named_t	dcl_kstat[DCK_NUM];
	wmsum_t		dcl_wsums[DCK_NUM];
	uint32_t	dcl_dbg_left;	/* dbgmsg lines left in run */
} dsl_clonedup_t;

/* outcome of one destination block */
typedef enum zfs_clonedup_result {
	ZCR_APPLIED,
	ZCR_SRC_STALE,
	ZCR_SRC_DIRTY,
	ZCR_DST_STALE,
	ZCR_DST_DIRTY,
	ZCR_ERROR,
	ZCR_QUEUED,
} zfs_clonedup_result_t;

/*
 * Whether an error from reading a block pointer means that the block
 * moved.  ENOENT is an object that is gone, and ESTALE one that no
 * longer holds the block at the size the walk saw.  Any other error,
 * an I/O error included, counts as an error.
 */
static inline boolean_t
dsl_clonedup_err_moved(int err)
{
	return (err == ENOENT || err == ESTALE);
}

/*
 * Destination side of the apply, implemented in zfs_clonedup_apply.c
 * (kernel only; libzpool stubs return ENOTSUP).  A handle is a file
 * in a mounted filesystem, a block device of an open volume, or an
 * object of a dataset the apply thread owns.
 */
typedef struct zfs_clonedup_dst zfs_clonedup_dst_t;
int zfs_clonedup_dst_open(spa_t *spa, uint64_t dsobj, uint64_t object,
    zfs_clonedup_dst_t **dstp);
int zfs_clonedup_dst_share(zfs_clonedup_dst_t *dst, uint64_t object,
    zfs_clonedup_dst_t **dstp);
boolean_t zfs_clonedup_bclone_enabled(void);
void zfs_clonedup_dst_close(zfs_clonedup_dst_t *dst);
objset_t *zfs_clonedup_dst_objset(zfs_clonedup_dst_t *dst);
uint64_t zfs_clonedup_dst_object(zfs_clonedup_dst_t *dst);
boolean_t zfs_clonedup_dst_exclusive(zfs_clonedup_dst_t *dst);
dsl_clonedup_kstat_id_t zfs_clonedup_dst_kind(
    zfs_clonedup_dst_t *dst);
int zfs_clonedup_dst_prepare(zfs_clonedup_dst_t *dst, uint64_t blkid,
    const blkptr_t *dexp, boolean_t nowait, boolean_t dryrun,
    boolean_t *readyp, void **lockp, zfs_clonedup_result_t *resp);
int zfs_clonedup_dst_clone_check(zfs_clonedup_dst_t *dst,
    objset_t *sos);
void zfs_clonedup_dst_tx_hold(dmu_tx_t *tx, zfs_clonedup_dst_t *dst,
    uint64_t blkid, uint64_t blksz);
int zfs_clonedup_src_validate(objset_t *sos, uint64_t sobj,
    uint64_t sblkid, const blkptr_t *sexp, blkptr_t *bp,
    zfs_clonedup_result_t *resp);
int zfs_clonedup_dst_finish(zfs_clonedup_dst_t *dst, uint64_t blkid,
    const blkptr_t *dexp, objset_t *sos, uint64_t sobj,
    uint64_t sblkid, const blkptr_t *sexp, const blkptr_t *sval,
    dmu_tx_t *tx, zfs_clonedup_result_t *resp);
void zfs_clonedup_dst_unlock(void *lock);
int zfs_clonedup_src_pin(objset_t *sos, zfs_clonedup_dst_t *dst,
    struct zfs_clonedup_spin **pinp);
void zfs_clonedup_src_unpin(struct zfs_clonedup_spin *pin);
int zfs_clonedup_src_lock(struct zfs_clonedup_spin *pin,
    zfs_clonedup_dst_t *dst, void *dlock, objset_t *sos,
    uint64_t sobj, uint64_t sblkid, const blkptr_t *sexp,
    boolean_t nowait, void **lockp, zfs_clonedup_result_t *resp);
void zfs_clonedup_src_unlock(void *lock);

/* what dsl_scan_sync() should do after a phase completes */
typedef enum dsl_clonedup_next {
	DCLN_WALK,	/* start another walk from *min_txgp */
	DCLN_APPLY,	/* hand the index to the apply thread */
	DCLN_FINISH,	/* the run is complete */
} dsl_clonedup_next_t;

void dsl_clonedup_global_init(void);
void dsl_clonedup_global_fini(void);
int dsl_clonedup_init(struct dsl_pool *dp);
void dsl_clonedup_fini(struct dsl_pool *dp);
uint64_t dsl_clonedup_last_txg(struct dsl_pool *dp);
void dsl_clonedup_walk_resumed(struct dsl_pool *dp);
void dsl_clonedup_sync_state(dsl_clonedup_t *dcl, dmu_tx_t *tx);
void dsl_clonedup_note_scrub(dsl_clonedup_t *dcl,
    const struct dsl_scan_phys *sp);

void dsl_clonedup_run_setup(dsl_clonedup_t *dcl, uint64_t flags,
    uint64_t min_txg, uint64_t max_txg, boolean_t restart,
    dmu_tx_t *tx);
void dsl_clonedup_run_done(dsl_clonedup_t *dcl, boolean_t complete,
    boolean_t restart, dmu_tx_t *tx);
boolean_t dsl_clonedup_bp_eligible(spa_t *spa, const blkptr_t *bp,
    const struct dnode_phys *dnp, const zbookmark_phys_t *zb,
    uint8_t *flagsp);
boolean_t dsl_clonedup_wanted(dsl_clonedup_t *dcl,
    const blkptr_t *bp);
void dsl_clonedup_visit(dsl_clonedup_t *dcl, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const struct dnode_phys *dnp,
    uint8_t flags);
dsl_clonedup_next_t dsl_clonedup_walk_done(dsl_clonedup_t *dcl,
    uint64_t *min_txgp, dmu_tx_t *tx);
boolean_t dsl_clonedup_apply_done(dsl_clonedup_t *dcl, uint64_t txg);
boolean_t dsl_clonedup_apply_paced(dmu_tx_t *tx, uint_t n);
boolean_t dsl_clonedup_apply_check(void *arg, struct zthr *zthr);
void dsl_clonedup_apply_thread(void *arg, struct zthr *zthr);
void dsl_clonedup_apply_wakeup(spa_t *spa);
boolean_t dsl_clonedup_src_held(struct dsl_pool *dp, uint64_t dsobj);
int dsl_clonedup_src_in_range(objset_t *os, uint64_t object,
    uint64_t blkid, uint64_t blksz);
dsl_clonedup_next_t dsl_clonedup_partition_done(dsl_clonedup_t *dcl,
    uint64_t *min_txgp, dmu_tx_t *tx);
dsl_clonedup_t *dsl_clonedup_yield_begin(spa_t *spa);
dsl_clonedup_t *dsl_clonedup_yield_begin_name(const char *name);
dsl_clonedup_t *dsl_clonedup_yield_idle(void);
void dsl_clonedup_yield_end(dsl_clonedup_t *dcl);
boolean_t dsl_clonedup_yield_nudge(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DSL_CLONEDUP_H */
