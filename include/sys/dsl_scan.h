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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2017, 2019, Datto Inc. All rights reserved.
 */

#ifndef	_SYS_DSL_SCAN_H
#define	_SYS_DSL_SCAN_H

#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/ddt.h>
#include <sys/bplist.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct objset;
struct dsl_dir;
struct dsl_dataset;
struct dsl_pool;
struct dmu_tx;

extern int zfs_scan_suspend_progress;

/*
 * All members of this structure must be uint64_t, for byteswap
 * purposes.
 */
typedef struct dsl_scan_phys {
	uint64_t scn_func; /* pool_scan_func_t */
	uint64_t scn_state; /* dsl_scan_state_t */
	uint64_t scn_queue_obj;
	uint64_t scn_min_txg;
	uint64_t scn_max_txg;
	uint64_t scn_cur_min_txg;
	uint64_t scn_cur_max_txg;
	uint64_t scn_start_time;
	uint64_t scn_end_time;
	uint64_t scn_to_examine; /* total bytes to be scanned */
	uint64_t scn_examined; /* bytes scanned so far */
	uint64_t scn_to_process;
	uint64_t scn_processed;
	uint64_t scn_errors;	/* scan I/O error count */
	uint64_t scn_ddt_class_max;
	ddt_bookmark_t scn_ddt_bookmark;
	zbookmark_phys_t scn_bookmark;
	uint64_t scn_flags; /* dsl_scan_flags_t */
} dsl_scan_phys_t;

#define	SCAN_PHYS_NUMINTS (sizeof (dsl_scan_phys_t) / sizeof (uint64_t))

typedef enum dsl_scan_flags {
	DSF_VISIT_DS_AGAIN = 1<<0,
	DSF_SCRUB_PAUSED = 1<<1,
} dsl_scan_flags_t;

#define	DSL_SCAN_FLAGS_MASK (DSF_VISIT_DS_AGAIN)

/*
 * Every pool will have one dsl_scan_t and this structure will contain
 * in-memory information about the scan and a pointer to the on-disk
 * representation (i.e. dsl_scan_phys_t). Most of the state of the scan
 * is contained on-disk to allow the scan to resume in the event of a reboot
 * or panic. This structure maintains information about the behavior of a
 * running scan, some caching information, and how it should traverse the pool.
 *
 * The following members of this structure direct the behavior of the scan:
 *
 * scn_suspending -	a scan that cannot be completed in a single txg or
 *			has exceeded its allotted time will need to suspend.
 *			When this flag is set the scanner will stop traversing
 *			the pool and write out the current state to disk.
 *
 * scn_restart_txg -	directs the scanner to either restart or start a
 *			a scan at the specified txg value.
 *
 * scn_done_txg -	when a scan completes its traversal it will set
 *			the completion txg to the next txg. This is necessary
 *			to ensure that any blocks that were freed during
 *			the scan but have not yet been processed (i.e deferred
 *			frees) are accounted for.
 *
 * This structure also maintains information about deferred frees which are
 * a special kind of traversal. Deferred free can exist in either a bptree or
 * a bpobj structure. The scn_is_bptree flag will indicate the type of
 * deferred free that is in progress. If the deferred free is part of an
 * asynchronous destroy then the scn_async_destroying flag will be set.
 */
typedef struct dsl_scan {
	struct dsl_pool *scn_dp;
	uint64_t scn_restart_txg;
	uint64_t scn_done_txg;
	uint64_t scn_sync_start_time;
	uint64_t scn_issued_before_pass;

	/* for freeing blocks */
	boolean_t scn_is_bptree;
	boolean_t scn_async_destroying;
	boolean_t scn_async_stalled;
	uint64_t  scn_async_block_min_time_ms;

	/* flags and stats for controlling scan state */
	boolean_t scn_is_sorted;	/* doing sequential scan */
	boolean_t scn_clearing;		/* scan is issuing sequential extents */
	boolean_t scn_checkpointing;	/* scan is issuing all queued extents */
	boolean_t scn_suspending;	/* scan is suspending until next txg */
	uint64_t scn_last_checkpoint;	/* time of last checkpoint */

	/* members for thread synchronization */
	zio_t *scn_zio_root;		/* root zio for waiting on IO */
	taskq_t *scn_taskq;		/* task queue for issuing extents */

	/* for controlling scan prefetch, protected by spa_scrub_lock */
	boolean_t scn_prefetch_stop;	/* prefetch should stop */
	zbookmark_phys_t scn_prefetch_bookmark;	/* prefetch start bookmark */
	avl_tree_t scn_prefetch_queue;	/* priority queue of prefetch IOs */
	uint64_t scn_maxinflight_bytes; /* max bytes in flight for pool */

	/* per txg statistics */
	uint64_t scn_visited_this_txg;	/* total bps visited this txg */
	uint64_t scn_dedup_frees_this_txg;	/* dedup bps freed this txg */
	uint64_t scn_holes_this_txg;
	uint64_t scn_lt_min_this_txg;
	uint64_t scn_gt_max_this_txg;
	uint64_t scn_ddt_contained_this_txg;
	uint64_t scn_objsets_visited_this_txg;
	uint64_t scn_avg_seg_size_this_txg;
	uint64_t scn_segs_this_txg;
	uint64_t scn_avg_zio_size_this_txg;
	uint64_t scn_zios_this_txg;

	/* members needed for syncing scan status to disk */
	dsl_scan_phys_t scn_phys;	/* on disk representation of scan */
	dsl_scan_phys_t scn_phys_cached;
	avl_tree_t scn_queue;		/* queue of datasets to scan */
	uint64_t scn_bytes_pending;	/* outstanding data to issue */
} dsl_scan_t;

typedef struct dsl_scan_io_queue dsl_scan_io_queue_t;

void scan_init(void);
void scan_fini(void);
int dsl_scan_init(struct dsl_pool *dp, uint64_t txg);
int dsl_scan_setup_check(void *, dmu_tx_t *);
void dsl_scan_setup_sync(void *, dmu_tx_t *);
void dsl_scan_fini(struct dsl_pool *dp);
void dsl_scan_sync(struct dsl_pool *, dmu_tx_t *);
int dsl_scan_cancel(struct dsl_pool *);
int dsl_scan(struct dsl_pool *, pool_scan_func_t);
void dsl_scan_assess_vdev(struct dsl_pool *dp, vdev_t *vd);
boolean_t dsl_scan_scrubbing(const struct dsl_pool *dp);
int dsl_scrub_set_pause_resume(const struct dsl_pool *dp, pool_scrub_cmd_t cmd);
void dsl_scan_restart_resilver(struct dsl_pool *, uint64_t txg);
boolean_t dsl_scan_resilvering(struct dsl_pool *dp);
boolean_t dsl_scan_resilver_scheduled(struct dsl_pool *dp);
boolean_t dsl_dataset_unstable(struct dsl_dataset *ds);
void dsl_scan_ddt_entry(dsl_scan_t *scn, enum zio_checksum checksum,
    ddt_entry_t *dde, dmu_tx_t *tx);
void dsl_scan_ds_destroyed(struct dsl_dataset *ds, struct dmu_tx *tx);
void dsl_scan_ds_snapshotted(struct dsl_dataset *ds, struct dmu_tx *tx);
void dsl_scan_ds_clone_swapped(struct dsl_dataset *ds1, struct dsl_dataset *ds2,
    struct dmu_tx *tx);
boolean_t dsl_scan_active(dsl_scan_t *scn);
boolean_t dsl_scan_is_paused_scrub(const dsl_scan_t *scn);
void dsl_scan_freed(spa_t *spa, const blkptr_t *bp);
void dsl_scan_io_queue_destroy(dsl_scan_io_queue_t *queue);
void dsl_scan_io_queue_vdev_xfer(vdev_t *svd, vdev_t *tvd);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_SCAN_H */
