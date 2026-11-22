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

/*
 * Private definitions of the async mirror, shared by the files
 * module/zfs/vdev_async_mirror*.c.  The public interface is in
 * sys/vdev_async_mirror.h.
 */

#ifndef _SYS_VDEV_ASYNC_MIRROR_IMPL_H
#define	_SYS_VDEV_ASYNC_MIRROR_IMPL_H

#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/vdev_async_mirror.h>
#include <sys/dmu_tx.h>
#include <sys/range_tree.h>
#include <sys/space_map.h>
#include <sys/zthr.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* The I/O state of the copies of one commit; see vdev_async_mirror_copy.c. */
typedef struct async_mirror_io async_mirror_io_t;

/* The kind of error that stopped the copy of a txg. */
typedef enum async_mirror_err {
	ASYNC_MIRROR_ERR_WRITE,	/* a write to the lagging child */
	ASYNC_MIRROR_ERR_META,	/* a read of the walk: a metadata block */
	ASYNC_MIRROR_ERR_BLKPTR, /* a block pointer that the walk refused */
	ASYNC_MIRROR_ERR_STOP,	/* the pool stops: copy again at import */
	ASYNC_MIRROR_ERR_HELD,	/* the async mirror gave up on the child */
} async_mirror_err_t;

/*
 * True for an error that ends the copy at once, whatever the txg: a write
 * error, or the give-up, which ends the I/O of the copy as a write error
 * does.
 */
static inline boolean_t
async_mirror_err_ends(async_mirror_err_t kind)
{
	return (kind == ASYNC_MIRROR_ERR_WRITE ||
	    kind == ASYNC_MIRROR_ERR_HELD);
}

/*
 * Why the async mirror gave up on the lagging child; see "The give-up" in
 * vdev_async_mirror.c.  The value is stored in the per-child entry
 * (amc_frozen_reason).
 */
typedef enum async_mirror_giveup {
	AMG_NONE = 0,
	AMG_HELD,		/* the held space is at its hard limit */
	AMG_SPACE,		/* the pool is short of space */
	AMG_STALL,		/* the copy made no progress at its limit */
} async_mirror_giveup_t;

/*
 * A data block of a record that no copy could supply on the last try of the
 * copy, and that is tried again before the record is released.  See
 * "Deferred blocks" below.
 */
typedef struct async_mirror_defer {
	list_node_t	ad_node;
	blkptr_t	ad_bp;
	zbookmark_phys_t ad_zb;
	boolean_t	ad_done;	/* recovered, or reported as lost */
} async_mirror_defer_t;

/*
 * The MOS entry of a deferred block: the txg, the bp and the bookmark.  Its
 * name is "<txg>-<vdev>-<offset>" in hexadecimal: the txg of the record, and
 * the top-level vdev and the offset of the first copy of the block.
 */
#define	ASYNC_MIRROR_DEFER_WORDS	(1 + \
	sizeof (blkptr_t) / sizeof (uint64_t) + \
	sizeof (zbookmark_phys_t) / sizeof (uint64_t))

/* One record per txg that has changes. */
typedef struct async_mirror_txg {
	list_node_t	at_node;
	uint64_t	at_txg;
	uint64_t	at_obj;		/* space map object */
	space_map_t	*at_sm;		/* open only while being written */
	uint64_t	at_sm_len;	/* its length, once at_ready is set */
	uint64_t	at_held;	/* bytes held in this txg */
	uint64_t	at_held_dspace;	/* deflated bytes that it still holds */
	uint64_t	at_bytes;	/* bytes to copy, from SM_ALLOC */
	boolean_t	at_ready;	/* the txg has fully synced */
	boolean_t	at_release;	/* only a release changed this txg */
	uberblock_t	at_ub;		/* valid once at_ready is set */
	hrtime_t	at_ready_time;
	list_t		at_defer;	/* the deferred blocks of the txg */
	uint_t		at_ndefer_count; /* deferred blocks in at_defer */
	uint_t		at_ndefer;	/* deferred blocks not yet done */
	uint_t		at_defer_tries;	/* tries of them made so far */
} async_mirror_txg_t;

/*
 * The life cycle of the async mirror.  The states from AMS_ACTIVE to AMS_FAILED
 * have a lagging child: am_lagging and am_leader are set.
 *
 *	OFF -> STARTING -> ACTIVE -> PASSTHROUGH -> DRAINING
 *	                     |           |
 *	                     +--> FROZEN, FAILED -> DRAINING
 *
 * The child freezes after a read error, and when the async mirror gives up
 * on it; see "The give-up" in vdev_async_mirror.c.  The copy fails after a
 * write error.
 *
 * A frozen child becomes a failed one when the property turns the async mirror
 * off.  An import enters ACTIVE, FROZEN or DRAINING directly.
 */
typedef enum async_mirror_state {
	AMS_OFF = 0,		/* no lagging child, no record */
	AMS_STARTING,		/* the switch on waits for am_on_txg */
	AMS_ACTIVE,		/* writes skip the child; the worker copies */
	AMS_PASSTHROUGH,	/* writes reach the child; the copy ends */
	AMS_FROZEN,		/* the child stays at am_applied_txg, no I/O */
	AMS_FAILED,		/* the copy stopped; the child is resilvered */
	AMS_DRAINING,		/* ended; the records are released */
} async_mirror_state_t;

/* What the worker does with the deferred blocks of the txg it copies. */
typedef enum async_mirror_persist {
	AMP_NONE = 0,
	AMP_WAIT,		/* it waits for the pool threads to run */
	AMP_SYNC,		/* it waits for the txg that stores them */
} async_mirror_persist_t;

/*
 * am_lock protects mutable state. am_spa is constant; thread pointers change
 * only before start or after stop. am_progress_time uses atomic access.
 * am_state uses am_lock and atomic readers. am_leader and am_lagging change
 * only at load or txg start, so sync and free paths read them without a lock.
 * Throttle hints and am_giveup allow stale lockless reads; am_avail is also
 * lockless. MOS objects, claim state, am_last_ub and at_sm change at load or
 * in sync context; hold am_lock when another thread can read them.
 */
typedef struct vdev_async_mirror {
	spa_t		*am_spa;
	kmutex_t	am_lock;
	kcondvar_t	am_cv;		/* the worker waits for work */
	kcondvar_t	am_drain_cv;	/* waits for progress of the worker */
	uint32_t	am_state;	/* async_mirror_state_t */
	/*
	 * The leaf whose copy failed, or 0.  The failed stat and the
	 * async_mirror_state of that leaf say "failed" until the operator sets
	 * the property of the leaf or the copy starts again, also after the
	 * records are released; see async_mirror_set_state().
	 */
	uint64_t	am_failed_guid;
	boolean_t	am_off;		/* the property asks to leave */
	vdev_t		*am_leader;
	vdev_t		*am_lagging;
	uint64_t	am_on_guid;	/* AMS_STARTING: the leaf to start */
	uint64_t	am_on_txg;	/* AMS_STARTING: txg of the switch */
	uint64_t	am_pass_txg;	/* newest txg that can miss a write */
	uint64_t	am_replace_cleanup_guid;	/* stale marker */
	uint64_t	am_replace_guid;	/* leaf being replaced */
	boolean_t	am_replace_in_progress;

	/* The records and the copy. */
	list_t		am_txgs;	/* sorted by txg */
	uint64_t	am_repair_count;
	uint64_t	am_zap_obj;	/* the record ZAP, or 0 */
	uberblock_t	am_last_ub;	/* uberblock of the newest record */
	uberblock_t	am_child_ub;	/* uberblock on the lagging child */
	uint64_t	am_applied_txg; /* newest txg on the lagging child */
	uint64_t	am_synced_txg;	/* newest txg on disk */
	uint64_t	am_pending_bytes;
	uint64_t	am_pending_txgs;
	uint64_t	am_held_bytes;
	uint64_t	am_held_dspace;
	uint64_t	am_ready_dspace; /* of it, a release can free now */
	uint64_t	am_held_log_bytes; /* held on log devices */
	zfs_range_tree_t *am_claim_rt;	/* log blocks to list at the claim */
	uint64_t	am_claim_txg;	/* the claim txg */
	kthread_t	*am_thread;	/* the worker; it clears this at exit */
	boolean_t	am_stop;	/* the worker must exit */

	/* The end of a copy that froze or failed. */
	uint64_t	am_fail_from;	/* oldest txg that the child lacks */
	uint64_t	am_fail_open_txg; /* newest txg that can miss a write */
	uint64_t	am_halt_txg;	/* txg that records the halt, or 0 */
	uint64_t	am_dtl_txg;	/* txg that sets the DTLs, or 0 */
	uint64_t	am_dtl_range_txg;	/* newest txg in the DTL */
	uint64_t	am_dtl_label_txg;	/* later label txg */
	uint64_t	am_dtl_label_write_txg;	/* label write awaiting flush */
	uint64_t	am_dtl_ub_txg;	/* later uberblock txg */
	uint64_t	am_dtl_ub_write_txg;	/* pending uberblock write */
	uint64_t	am_dtl_child_txg;	/* later uberblock txg */
	uint64_t	am_dtl_child_guid;	/* fenced child GUID */
	boolean_t	am_dtl_child_clearing;	/* clear in progress */
	uint64_t	am_entry_guid;	/* leaf of the per-child entry, or 0 */
	boolean_t	am_frozen_entry; /* its entry has the freeze */
	boolean_t	am_off_entry;	/* its entry has the switch off */

	/* The give-up; see "The give-up" in vdev_async_mirror.c. */
	boolean_t	am_giveup;	/* a give-up ends the copy */
	uint_t		am_giveup_why;	/* async_mirror_giveup_t */
	boolean_t	am_freeze_read;	/* the freeze is a read error */
	hrtime_t	am_limit_since;	/* held space at its limit since */
	uint64_t	am_limit_target; /* txg that the child must pass */
	hrtime_t	am_space_since;	/* pool short of space since */
	uint64_t	am_space_target; /* txg that the child must pass */
	uint64_t	am_avail;	/* space left for the datasets */
	uint64_t	am_progress_time; /* hrtime of the last copy progress */
	uint64_t	am_giveups;	/* give-ups during this pool open */
	uint64_t	am_frozen_from;	/* oldest txg a frozen child lacks */
	boolean_t	am_frozen_stale; /* write the freeze entry again */
	uint64_t	am_entry_txg;	/* txg that wrote the freeze entry */

	/* Check give-up limits when a full pool cannot sync a txg. */
	zthr_t		*am_giveup_zthr;

	/* The release thread. */
	zthr_t		*am_release_zthr;
	uint64_t	am_last_release_txg;
	uint64_t	am_release_only_txg; /* txg changed only by a release */
	uint64_t	am_release_queued_txg; /* txg of the queued release */
	boolean_t	am_release_more; /* the last release hit its limit */
	boolean_t	am_release_capped; /* and it was the entry limit */
	uint64_t	am_release_futile_txg; /* forced release, no gain */

	/* The deferred blocks. */
	uint64_t	am_defer_zap;	/* the deferred blocks in the MOS */
	uint64_t	am_defer_txg;	/* oldest txg with them, 0 for none */
	uint32_t	am_defer_pending;	/* local deferred list */
	uint32_t	am_persist;	/* async_mirror_persist_t */
	uint_t		am_quiesce;	/* the pool threads are suspended */
	boolean_t	am_defer_io;	/* deferred blocks are being written */
	boolean_t	am_defer_failed; /* a deferred block write failed */
	boolean_t	am_defer_quick;	/* defer a lost data block at once */
} vdev_async_mirror_t;

/* True in the states that have a lagging child. */
static inline boolean_t
async_mirror_has_child(uint32_t state)
{
	return (state == AMS_ACTIVE || state == AMS_PASSTHROUGH ||
	    state == AMS_FROZEN || state == AMS_FAILED);
}

/* True while the worker copies the records to the lagging child. */
static inline boolean_t
async_mirror_copying(uint32_t state)
{
	return (state == AMS_ACTIVE || state == AMS_PASSTHROUGH);
}

/* True when every record is released, copied or not. */
static inline boolean_t
async_mirror_draining(uint32_t state)
{
	return (state == AMS_FROZEN || state == AMS_FAILED ||
	    state == AMS_DRAINING);
}

/* True if the lineage lists the leaf guid as absent at its open fork. */
static inline boolean_t
async_mirror_lineage_absent(const async_mirror_lineage_phys_t *lin,
    uint64_t guid)
{
	for (uint64_t i = 0; i < lin->aml_nabsent &&
	    i < ASYNC_MIRROR_LINEAGE_ABSENT; i++) {
		if (lin->aml_absent[i] == guid)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/* True once the txg is on disk.  A txg of 0 has not happened. */
static inline boolean_t
async_mirror_durable(const vdev_async_mirror_t *am, uint64_t txg)
{
	return (txg != 0 && txg <= am->am_synced_txg);
}

/*
 * Note that the copy made progress now; see async_mirror_stall_check() in
 * vdev_async_mirror.c.  Any thread calls this, with or without am_lock.
 */
static inline void
async_mirror_progress(vdev_async_mirror_t *am)
{
	atomic_store_64(&am->am_progress_time, (uint64_t)gethrtime());
}

/* The names of the threads; Linux cuts a name to 15 characters. */
#define	ASYNC_MIRROR_RELEASE_THREAD	"z_amirror_rel"
#define	ASYNC_MIRROR_GIVEUP_THREAD	"z_amirror_give"

_Static_assert(sizeof (ASYNC_MIRROR_RELEASE_THREAD) <= 16 &&
    sizeof (ASYNC_MIRROR_GIVEUP_THREAD) <= 16,
	"a thread name has at most 15 characters");

/* The delay before each try of a copy after a read error. */
#define	ASYNC_MIRROR_RETRY_MS	3000

/* Set in vdev_async_mirror.c. */
extern uint_t zfs_vdev_async_mirror_read_retries;
extern uint_t zfs_vdev_async_mirror_copy_delay_ms;
extern uint64_t zfs_vdev_async_mirror_copy_limit;
extern uint64_t zfs_vdev_async_mirror_sort_limit;

/* In vdev_async_mirror.c: the state, the stats, the threads, the marks. */
extern void async_mirror_set_state(vdev_async_mirror_t *,
    async_mirror_state_t);
extern void async_mirror_update_stats(vdev_async_mirror_t *);
extern __attribute__((noreturn)) void async_mirror_thread(void *);
extern boolean_t async_mirror_release_thread_check(void *, zthr_t *);
extern void async_mirror_release_thread(void *, zthr_t *);
extern zthr_t *async_mirror_giveup_zthr_create(vdev_async_mirror_t *);
extern void async_mirror_giveup(vdev_async_mirror_t *, async_mirror_giveup_t);
extern const char *async_mirror_giveup_text(uint_t);
extern void async_mirror_freeze_note(vdev_async_mirror_t *);
extern vdev_async_mirror_t *async_mirror_create(spa_t *);
extern void async_mirror_mark(spa_t *, vdev_t *, uint64_t);
extern void async_mirror_fail_sync(vdev_async_mirror_t *, dmu_tx_t *);
extern void async_mirror_publish_txg(vdev_async_mirror_t *);
extern void async_mirror_mark_dtl(spa_t *, vdev_t *, uint64_t, uint64_t);
extern void async_mirror_note_resync(spa_t *, vdev_t *);
extern boolean_t async_mirror_child_usable(vdev_t *);
extern boolean_t async_mirror_child_needs_resilver(spa_t *, vdev_t *);
extern void async_mirror_read_leaf_ub(spa_t *, vdev_t *, uberblock_t *,
    uberblock_t *, boolean_t *);
extern int async_mirror_can_start(spa_t *, vdev_t *, vdev_t **);
extern boolean_t async_mirror_child_get(spa_t *, uint64_t,
    async_mirror_child_phys_t *);
extern void async_mirror_child_put(spa_t *, uint64_t,
    async_mirror_child_phys_t *, dmu_tx_t *);
extern void async_mirror_child_flags(spa_t *, uint64_t, uint64_t, uint64_t,
    dmu_tx_t *);
extern int async_mirror_child_replace_lookup(spa_t *, uint64_t *);

/* In vdev_async_mirror_copy.c: the copy, the deferred blocks, the halt. */
extern async_mirror_io_t *async_mirror_io_create(vdev_async_mirror_t *);
extern void async_mirror_io_destroy(async_mirror_io_t *);
extern int async_mirror_copy(vdev_async_mirror_t *, async_mirror_io_t *,
    async_mirror_txg_t *, boolean_t, async_mirror_err_t *);
extern int async_mirror_copy_wait(async_mirror_io_t *, async_mirror_err_t *,
    uint64_t *, list_t *, uint_t *);
extern int async_mirror_commit(vdev_async_mirror_t *, async_mirror_txg_t *,
    boolean_t, list_t *, uint_t, async_mirror_err_t *);
extern void async_mirror_retry_event(vdev_async_mirror_t *, uint64_t, int);
extern boolean_t async_mirror_retry_wait(vdev_async_mirror_t *);
extern void async_mirror_halt(vdev_async_mirror_t *, uint64_t, int,
    async_mirror_err_t);
extern void async_mirror_defer_list_create(list_t *);
extern void async_mirror_defer_list_free(list_t *);
extern void async_mirror_defer_floor(vdev_async_mirror_t *);
extern void async_mirror_defer_drop(vdev_async_mirror_t *,
    async_mirror_txg_t *);
extern void async_mirror_defer_clear(vdev_async_mirror_t *, uint64_t,
    dmu_tx_t *);
extern async_mirror_txg_t *async_mirror_defer_next(vdev_async_mirror_t *);
extern void async_mirror_defer_retry(vdev_async_mirror_t *, zthr_t *);

/* In vdev_async_mirror_lineage.c: the lineage, the keep point, the erase. */
extern void async_mirror_lineage_start(spa_t *);
extern void async_mirror_lineage_update(spa_t *, boolean_t);
extern int async_mirror_erase_skipped(spa_t *);
extern boolean_t async_mirror_label_has_legacy_mark(spa_t *, vdev_t *);
extern void async_mirror_mark_erased(spa_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_ASYNC_MIRROR_IMPL_H */
