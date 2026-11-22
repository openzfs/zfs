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
 * Copyright (c) 2026, John White.
 */

#ifndef _SYS_VDEV_ASYNC_MIRROR_H
#define	_SYS_VDEV_ASYNC_MIRROR_H

#include <sys/spa.h>
#include <sys/vdev.h>
#include <sys/dmu_tx.h>
#include <sys/range_tree.h>
#include <sys/space_map.h>
#include <sys/metaslab.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The async mirror keeps one child of a top-level mirror, the lagging
 * child, behind the other one.  See module/zfs/vdev_async_mirror.c.
 */
struct vdev_async_mirror;
struct dsl_scan;

/*
 * Records are pool-wide; each lagging leaf has a separate child entry.
 * Label marks identify the lagging set. Deferred-block keys name a top-level
 * vdev, and lineage entries record children absent at a fork. Keep structure
 * sizes fixed. Reject unknown versions, flags, counts and set numbers. Give
 * each layout its own active feature flag; keep unused reserved words zero.
 */

/* A txg record stores its space map and the previous record's uberblock. */
#define	ASYNC_MIRROR_REC_VERSION	1
#define	ASYNC_MIRROR_REC_UB_WORDS	64
typedef struct async_mirror_rec_phys {
	uint64_t	amr_version;	/* ASYNC_MIRROR_REC_VERSION */
	uint64_t	amr_sm_obj;	/* space map of the txg */
	uint64_t	amr_flags;	/* 0 */
	uint64_t	amr_ub[ASYNC_MIRROR_REC_UB_WORDS];
} async_mirror_rec_phys_t;

#define	ASYNC_MIRROR_REC_WORDS	\
	(sizeof (async_mirror_rec_phys_t) / sizeof (uint64_t))

/*
 * The child GUID keys this value. An entry holds one feature reference and
 * exists while a flag is set. LAGGING tracks records; FROZEN stores the
 * freeze fields; OFF marks a completed switch with records left to release;
 * DTL_PENDING fences a failed leaf until its DTL-bearing uberblock is durable;
 * REPLACE keeps a pending replacement after leaf removal. Labels, not
 * amc_applied_txg, prove the child's applied txg. Reserved words stay zero.
 */
#define	ASYNC_MIRROR_CHILD_VERSION	1
#define	ASYNC_MIRROR_CHILD_SET		1	/* the one lagging set */
#define	ASYNC_MIRROR_CHILD_RESERVED	11
#define	ASYNC_MIRROR_CHILD_DTL_TXG_WORD	0

#define	ASYNC_MIRROR_CHILD_LAGGING	(1ULL << 0)
#define	ASYNC_MIRROR_CHILD_FROZEN	(1ULL << 1)
#define	ASYNC_MIRROR_CHILD_OFF		(1ULL << 2)
#define	ASYNC_MIRROR_CHILD_REPLACE	(1ULL << 3)
#define	ASYNC_MIRROR_CHILD_DTL_PENDING	(1ULL << 4)
#define	ASYNC_MIRROR_CHILD_FLAGS	\
	(ASYNC_MIRROR_CHILD_LAGGING | ASYNC_MIRROR_CHILD_FROZEN | \
	ASYNC_MIRROR_CHILD_OFF | ASYNC_MIRROR_CHILD_REPLACE | \
	ASYNC_MIRROR_CHILD_DTL_PENDING)
/* The flags of the leaf that the state of the pool is for. */
#define	ASYNC_MIRROR_CHILD_STATE	\
	(ASYNC_MIRROR_CHILD_LAGGING | ASYNC_MIRROR_CHILD_FROZEN | \
	ASYNC_MIRROR_CHILD_OFF | ASYNC_MIRROR_CHILD_DTL_PENDING)

typedef struct async_mirror_child_phys {
	uint64_t	amc_version;	/* ASYNC_MIRROR_CHILD_VERSION */
	uint64_t	amc_flags;	/* ASYNC_MIRROR_CHILD_* */
	uint64_t	amc_set;	/* number of the lagging set, not 0 */
	uint64_t	amc_start_txg;	/* txg of the first record */
	uint64_t	amc_applied_txg; /* applied txg at the last write */
	uint64_t	amc_off_txg;	/* first txg as a normal child */
	uint64_t	amc_frozen_txg;	/* the frozen txg */
	uint64_t	amc_frozen_reason; /* ASYNC_MIRROR_FROZEN_* */
	uint64_t	amc_frozen_from; /* oldest txg that the child lacks */
	uint64_t	amc_reserved_frozen_flags;
	uint64_t	amc_reserved_giveups;
	uint64_t	amc_reserved_frozen_time;
	uint64_t	amc_reserved_12;
	uint64_t	amc_reserved[ASYNC_MIRROR_CHILD_RESERVED];
} async_mirror_child_phys_t;

#define	ASYNC_MIRROR_CHILD_WORDS	\
	(sizeof (async_mirror_child_phys_t) / sizeof (uint64_t))

/* The reasons of a freeze (amc_frozen_reason). */
#define	ASYNC_MIRROR_FROZEN_METADATA	1
#define	ASYNC_MIRROR_FROZEN_HELD	2	/* a give-up */

/* The lineage entry lists children absent at an open fork. */
#define	ASYNC_MIRROR_LINEAGE_VERSION	1
#define	ASYNC_MIRROR_LINEAGE_ABSENT	4
#define	ASYNC_MIRROR_LINEAGE_RESERVED	5

/* aml_flags: a rewind import made the open fork. */
#define	ASYNC_MIRROR_FORK_REWIND	(1ULL << 0)
/* aml_flags: the entry holds a reference on the feature. */
#define	ASYNC_MIRROR_LINEAGE_ACTIVE	(1ULL << 1)
#define	ASYNC_MIRROR_LINEAGE_FLAGS	\
	(ASYNC_MIRROR_FORK_REWIND | ASYNC_MIRROR_LINEAGE_ACTIVE)

typedef struct async_mirror_lineage_phys {
	uint64_t	aml_version;	/* ASYNC_MIRROR_LINEAGE_VERSION */
	uint64_t	aml_lineage;	/* this line of history, not 0 */
	uint64_t	aml_parent;	/* lineage of the open fork, or 0 */
	uint64_t	aml_fork_txg;	/* last txg shared with aml_parent */
	uint64_t	aml_seen;	/* rewind fork: newest txg dropped */
	uint64_t	aml_flags;	/* ASYNC_MIRROR_FORK_REWIND, ... */
	uint64_t	aml_nabsent;	/* children missing at the fork */
	uint64_t	aml_absent[ASYNC_MIRROR_LINEAGE_ABSENT]; /* guids */
	uint64_t	aml_reserved[ASYNC_MIRROR_LINEAGE_RESERVED];	/* 0 */
} async_mirror_lineage_phys_t;

#define	ASYNC_MIRROR_LINEAGE_WORDS	\
	(sizeof (async_mirror_lineage_phys_t) / sizeof (uint64_t))

/* What one import of the pool decides about the async mirror. */
typedef struct async_mirror_import {
	uint64_t	ami_keep;	/* the copy that the operator keeps */
	uint64_t	ami_erased;	/* the child that this import erased */
	uint64_t	ami_resync[2];	/* children out of the async mirror */
	uint64_t	ami_skip_offline;	/* child to restore offline */
	uint64_t	ami_off;	/* child to set to write_mode=sync */
	const char	*ami_off_reason;	/* first-txg reason */
	boolean_t	ami_off_all;	/* more than one child to set to sync */
} async_mirror_import_t;

extern uint64_t spa_async_mirror_lineage(spa_t *spa);

/* Called for each record, in ZAP order; a nonzero return stops the walk. */
typedef int async_mirror_rec_cb_t(uint64_t txg,
    const async_mirror_rec_phys_t *rec, void *arg);
extern int async_mirror_rec_iterate(objset_t *mos, uint64_t zap,
    async_mirror_rec_cb_t *cb, void *arg);

/* Called for each per-child entry; a nonzero return stops the walk. */
typedef int async_mirror_child_cb_t(uint64_t guid,
    const async_mirror_child_phys_t *amc, void *arg);
extern int async_mirror_child_iterate(objset_t *mos,
    async_mirror_child_cb_t *cb, void *arg);

extern int vdev_async_mirror_activate(spa_t *spa);
extern void vdev_async_mirror_stop(spa_t *spa);
extern void vdev_async_mirror_fini(spa_t *spa);
extern void vdev_async_mirror_spawn(spa_t *spa);
extern void vdev_async_mirror_zthr_cancel(spa_t *spa);
extern void vdev_async_mirror_zthr_resume(spa_t *spa);

extern int vdev_async_mirror_set_check(spa_t *spa, vdev_t *vd,
    uint64_t value, uint64_t *otherp);
extern int vdev_async_mirror_read_mode_check(spa_t *spa, vdev_t *vd,
    uint64_t value);
extern int vdev_async_mirror_topology_check(vdev_t *vd);
extern int vdev_async_mirror_replace_check(vdev_t *vd, vdev_t *newvd);
extern int vdev_async_mirror_set(spa_t *spa, uint64_t guid, uint64_t value);
extern const char *vdev_async_mirror_state(vdev_t *vd);
extern const char *vdev_async_mirror_giveup_reason(uint64_t why);

extern int vdev_async_mirror_check_lineage(spa_t *spa);
extern int vdev_async_mirror_check_legacy_mark(spa_t *spa);
extern int vdev_async_mirror_lineage_load(spa_t *spa);
extern int vdev_async_mirror_online_check(spa_t *spa, vdev_t *vd);
extern void vdev_async_mirror_load_marked(spa_t *spa);
extern boolean_t vdev_async_mirror_stale(vdev_t *vd, uint64_t txg);
extern boolean_t vdev_async_mirror_read_fenced(vdev_t *vd,
    const zio_t *zio);
extern boolean_t vdev_async_mirror_repair_start(vdev_t *vd,
	const zio_t *zio, void **tokenp);
extern void vdev_async_mirror_repair_done(void *token);

extern boolean_t vdev_async_mirror_is_async(vdev_t *vd);
extern boolean_t vdev_async_mirror_dtl_fence(vdev_t *vd);
extern boolean_t vdev_async_mirror_dtl_fence_open(spa_t *spa);
extern boolean_t vdev_async_mirror_skip_labels(const vdev_t *vd);
extern boolean_t vdev_async_mirror_inject_dtl_label_flush(
	const vdev_t *vd);
extern boolean_t vdev_async_mirror_skip_uberblock(const vdev_t *vd);
extern void vdev_async_mirror_label_written(vdev_t *vd, uint64_t txg);
extern void vdev_async_mirror_label_flushed(vdev_t *vd, uint64_t txg);
extern void vdev_async_mirror_label_flush(spa_t *spa, uint64_t txg);
extern void vdev_async_mirror_uberblock_written(vdev_t *vd, uint64_t txg);
extern void vdev_async_mirror_uberblock_flushed(vdev_t *vd, uint64_t txg);
extern void vdev_async_mirror_uberblock_flush(spa_t *spa, uint64_t txg);
extern int vdev_async_mirror_label_sync(vdev_t *vd, uint64_t txg,
    uint64_t child_txg);
extern boolean_t vdev_async_mirror_intercept_write(zio_t *zio, vdev_t *vd);
extern boolean_t vdev_async_mirror_active(spa_t *spa);
extern uint64_t vdev_async_mirror_failed_guid(spa_t *spa);
extern void vdev_async_mirror_failed_clear(spa_t *spa, uint64_t guid);
extern boolean_t vdev_async_mirror_is_child(const vdev_t *vd);
extern boolean_t vdev_async_mirror_failed_lagging(vdev_t *vd);
extern int vdev_async_mirror_wait_off(spa_t *spa, uint64_t guid);
extern int vdev_async_mirror_replace_pending(spa_t *spa, uint64_t *guidp);
extern int vdev_async_mirror_replace_begin(spa_t *spa, uint64_t guid);
extern int vdev_async_mirror_replace_claim(spa_t *spa, uint64_t guid);
extern int vdev_async_mirror_replace_abort(spa_t *spa, uint64_t guid);
extern int vdev_async_mirror_replace_off(spa_t *spa, uint64_t guid);
extern void vdev_async_mirror_replace_release(spa_t *spa, uint64_t guid);
extern void vdev_async_mirror_replace_clear_sync(spa_t *spa, uint64_t guid,
    dmu_tx_t *tx);
extern boolean_t vdev_async_mirror_tracks(const vdev_t *vd);
extern boolean_t vdev_async_mirror_in_use(spa_t *spa);
extern boolean_t vdev_async_mirror_removal_refused(const vdev_t *vd);
/* True if a free from this top-level vdev must be held. */
extern boolean_t vdev_async_mirror_should_hold(const vdev_t *vd,
    metaslab_free_kind_t kind);

extern void vdev_async_mirror_claim_committed(spa_t *spa, const blkptr_t *bp,
    uint64_t txg);
extern void vdev_async_mirror_claimed(spa_t *spa);

extern void vdev_async_mirror_sync_start(spa_t *spa, dmu_tx_t *tx);
extern void vdev_async_mirror_sync_check(spa_t *spa);
extern void vdev_async_mirror_sync_record(spa_t *spa, dmu_tx_t *tx);
extern void vdev_async_mirror_sync_lineage(spa_t *spa, dmu_tx_t *tx);
extern void vdev_async_mirror_sync_write(spa_t *spa, vdev_t *vd,
    zfs_range_tree_t *rt, maptype_t type, dmu_tx_t *tx);
extern void vdev_async_mirror_sync_list(spa_t *spa, vdev_t *vd,
    zfs_range_tree_t *alloctree, zfs_range_tree_t *nolist, dmu_tx_t *tx);
extern void vdev_async_mirror_txg_synced(spa_t *spa, uint64_t txg);
extern hrtime_t vdev_async_mirror_delay(spa_t *spa);
extern void vdev_async_mirror_wait(spa_t *spa);
extern boolean_t vdev_async_mirror_wait_target(spa_t *spa, uint64_t limit,
    uint64_t *targetp);
extern int vdev_async_mirror_wait_pending(spa_t *spa, uint64_t target,
    boolean_t stop_early, boolean_t *pendingp);
extern boolean_t vdev_async_mirror_sync_stopped(spa_t *spa);

/* Erase every uberblock of a leaf, in two phases, before an import. */
extern int vdev_uberblock_erase(vdev_t *vd, uint64_t max_txg);
extern void vdev_label_sync(zio_t *zio, uint64_t *good_writes,
    vdev_t *vd, int l, uint64_t txg, int flags,
    boolean_t async_mirror_refresh);

extern uint64_t spa_get_async_mirror_held_space(spa_t *spa);

typedef struct vdev_async_mirror_replace_ctx {
	boolean_t	vamrc_replace;
	boolean_t	vamrc_pending;
	boolean_t	vamrc_claimed;
	boolean_t	vamrc_marker_owned;
} vdev_async_mirror_replace_ctx_t;

extern int vdev_async_mirror_replace_attach_check(spa_t *spa, uint64_t guid,
    vdev_t *vd, int replacing, vdev_async_mirror_replace_ctx_t *ctx);
extern int vdev_async_mirror_replace_attach_prepare(spa_t *spa,
    uint64_t guid, vdev_t *oldvd, vdev_t *newvd, int replacing, int rebuild,
    uint64_t txg, vdev_t *newrootvd,
    vdev_async_mirror_replace_ctx_t *ctx);
extern void vdev_async_mirror_replace_attach_commit(spa_t *spa,
    uint64_t guid, vdev_async_mirror_replace_ctx_t *ctx);
extern void vdev_async_mirror_replace_attach_abort(spa_t *spa,
    uint64_t guid, vdev_async_mirror_replace_ctx_t *ctx);
extern boolean_t vdev_async_mirror_accept_exported_label(const vdev_t *vd,
    uint64_t state);
extern boolean_t vdev_async_mirror_dtl_below_scan(uint64_t dtl_min_txg,
    uint64_t scan_min_txg);
extern int vdev_async_mirror_fault_check(vdev_t *vd, vdev_aux_t aux);
extern int vdev_async_mirror_online_restore(spa_t *spa, vdev_t *vd,
    vdev_t *tvd, boolean_t wasoffline, boolean_t was_tmpoffline);
extern int vdev_async_mirror_offline_check(vdev_t *vd);
extern void vdev_async_mirror_vdev_free(vdev_t *vd);
extern void vdev_async_mirror_import_reset(spa_t *spa);
extern void vdev_async_mirror_import_keep(spa_t *spa, nvlist_t *config);
extern void vdev_async_mirror_import_done(spa_t *spa);
extern void vdev_async_mirror_load_done(spa_t *spa);
extern void vdev_async_mirror_import_restore_offline(spa_t *spa,
    vdev_t *rvd);
extern int vdev_async_mirror_activate_if_enabled(spa_t *spa);
extern int vdev_async_mirror_add_check(spa_t *spa, vdev_t *vd);
extern boolean_t vdev_async_mirror_resilver_pending(spa_t *spa);
extern boolean_t vdev_async_mirror_resilver_wanted(vdev_t *vd);
extern void vdev_async_mirror_scan_setup_sync(struct dsl_scan *scn,
    dmu_tx_t *tx, spa_t *spa);
extern void vdev_async_mirror_metaslab_sync(spa_t *spa, vdev_t *vd,
    metaslab_t *msp, zfs_range_tree_t *alloctree, uint64_t txg,
    dmu_tx_t *tx);
extern void vdev_async_mirror_metaslab_sync_done(metaslab_t *msp,
    uint64_t txg);
extern void vdev_async_mirror_metaslab_nolist_alloc(spa_t *spa,
    const blkptr_t *bp, const dva_t *dva, int ndvas, uint64_t txg,
    int flags);
extern void vdev_async_mirror_metaslab_unalloc(metaslab_t *msp,
    uint64_t txg, uint64_t offset, uint64_t size);
extern boolean_t vdev_async_mirror_metaslab_hold_free(spa_t *spa,
    vdev_t *vd, metaslab_t *msp, uint64_t offset, uint64_t size,
    metaslab_free_kind_t kind);
extern metaslab_free_kind_t vdev_async_mirror_free_kind(spa_t *spa,
    const blkptr_t *bp);
extern void vdev_async_mirror_metaslab_check_free(metaslab_t *msp,
    uint64_t offset, uint64_t size);
extern zfs_range_tree_t *metaslab_async_mirror_rt_create(metaslab_t *msp,
    const char *name);
extern int vdev_async_mirror_props_set_check(void *arg, dmu_tx_t *tx);
extern void vdev_async_mirror_construct_zaps(vdev_t *vd, dmu_tx_t *tx);
extern void vdev_async_mirror_vdev_load(vdev_t *vd);
extern void vdev_async_mirror_vdev_config_generate(vdev_t *vd,
    nvlist_t *nvl, boolean_t getstats);
extern void vdev_async_mirror_config_generate(spa_t *spa, vdev_t *rvd,
    nvlist_t *nvroot, boolean_t getstats);
extern boolean_t vdev_async_mirror_prop_get(vdev_t *vd, vdev_prop_t prop,
    const char *propname, nvlist_t *outnvl, int *errorp);
extern int vdev_async_mirror_prop_set(spa_t *spa, vdev_t **vdp,
    uint64_t guid, vdev_prop_t prop, nvpair_t *elem, nvlist_t *outnvl);
extern void vdev_async_mirror_prop_set_sync(spa_t *spa, vdev_t *vd,
    vdev_prop_t prop, nvpair_t *elem, uint64_t objid, dmu_tx_t *tx);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_ASYNC_MIRROR_H */
