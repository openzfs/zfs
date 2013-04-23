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
 * Copyright (c) 2013 by Delphix. All rights reserved.
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
	zbookmark_t scn_bookmark;
	uint64_t scn_flags; /* dsl_scan_flags_t */
} dsl_scan_phys_t;

#define	SCAN_PHYS_NUMINTS (sizeof (dsl_scan_phys_t) / sizeof (uint64_t))

typedef enum dsl_scan_flags {
	DSF_VISIT_DS_AGAIN = 1<<0,
} dsl_scan_flags_t;

typedef struct dsl_scan {
	struct dsl_pool *scn_dp;

	boolean_t scn_pausing;
	uint64_t scn_restart_txg;
	uint64_t scn_sync_start_time;
	zio_t *scn_zio_root;

	/* for freeing blocks */
	boolean_t scn_is_bptree;
	boolean_t scn_async_destroying;

	/* for debugging / information */
	uint64_t scn_visited_this_txg;

	dsl_scan_phys_t scn_phys;
} dsl_scan_t;

int dsl_scan_init(struct dsl_pool *dp, uint64_t txg);
void dsl_scan_fini(struct dsl_pool *dp);
void dsl_scan_sync(struct dsl_pool *, dmu_tx_t *);
int dsl_scan_cancel(struct dsl_pool *);
int dsl_scan(struct dsl_pool *, pool_scan_func_t);
void dsl_resilver_restart(struct dsl_pool *, uint64_t txg);
boolean_t dsl_scan_resilvering(struct dsl_pool *dp);
boolean_t dsl_dataset_unstable(struct dsl_dataset *ds);
void dsl_scan_ddt_entry(dsl_scan_t *scn, enum zio_checksum checksum,
    ddt_entry_t *dde, dmu_tx_t *tx);
void dsl_scan_ds_destroyed(struct dsl_dataset *ds, struct dmu_tx *tx);
void dsl_scan_ds_snapshotted(struct dsl_dataset *ds, struct dmu_tx *tx);
void dsl_scan_ds_clone_swapped(struct dsl_dataset *ds1, struct dsl_dataset *ds2,
    struct dmu_tx *tx);
boolean_t dsl_scan_active(dsl_scan_t *scn);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_SCAN_H */
