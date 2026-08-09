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
 * Copyright (c) 2018, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2024-2026, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _SYS_SPA_LOG_SPACEMAP_H
#define	_SYS_SPA_LOG_SPACEMAP_H

#include <sys/avl.h>

typedef struct log_summary_entry {
	uint64_t lse_start;	/* start TXG */
	uint64_t lse_end;	/* last TXG */
	uint64_t lse_txgcount;	/* # of TXGs */
	uint64_t lse_mscount;	/* # of metaslabs needed to be flushed */
	uint64_t lse_msdcount;	/* # of dirty metaslabs needed to be flushed */
	uint64_t lse_blkcount;	/* blocks held by this entry  */
	list_node_t lse_node;
} log_summary_entry_t;

typedef struct spa_unflushed_stats  {
	/* used for memory heuristic */
	uint64_t sus_memused;	/* current memory used for unflushed trees */
	uint64_t sus_nmetaslabs;	/* # metaslabs with unflushed trees */

	/* used for block heuristic */
	uint64_t sus_blocklimit;	/* max # of log blocks allowed */
	uint64_t sus_nblocks;	/* # of blocks in log space maps currently */
} spa_unflushed_stats_t;

typedef struct spa_log_sm {
	uint64_t sls_sm_obj;	/* space map object ID */
	uint64_t sls_txg;	/* txg logged on the space map */
	uint64_t sls_nblocks;	/* number of blocks in this log */
	uint64_t sls_mscount;	/* # of metaslabs flushed in the log's txg */
	avl_node_t sls_node;	/* node in spa_sm_logs_by_txg */
	space_map_t *sls_sm;	/* space map pointer, if open */
} spa_log_sm_t;

typedef enum spa_log_flushall_mode {
	SPA_LOG_FLUSHALL_NONE = 0,	/* flushall inactive */
	SPA_LOG_FLUSHALL_REQUEST,	/* flushall active by admin request */
	SPA_LOG_FLUSHALL_EXPORT,	/* flushall active for pool export */
} spa_log_flushall_mode_t;

int spa_ld_log_spacemaps(spa_t *);

void spa_generate_syncing_log_sm(spa_t *, dmu_tx_t *);
void spa_flush_metaslabs(spa_t *, dmu_tx_t *);
void spa_sync_close_syncing_log_sm(spa_t *);

void spa_cleanup_old_sm_logs(spa_t *, dmu_tx_t *);

uint64_t spa_log_sm_blocklimit(spa_t *);
void spa_log_sm_set_blocklimit(spa_t *);
uint64_t spa_log_sm_nblocks(spa_t *);
uint64_t spa_log_sm_memused(spa_t *);

uint64_t spa_log_sm_unflushed_metaslabs(spa_t *);
void spa_log_sm_increment_unflushed_metaslabs(spa_t *);
void spa_log_sm_decrement_unflushed_metaslabs(spa_t *);

void spa_log_sm_decrement_mscount(spa_t *, uint64_t);
void spa_log_sm_increment_current_mscount(spa_t *);

void spa_log_summary_add_flushed_metaslab(spa_t *, boolean_t);
void spa_log_summary_dirty_flushed_metaslab(spa_t *, uint64_t);
void spa_log_summary_decrement_mscount(spa_t *, uint64_t, boolean_t);
void spa_log_summary_decrement_blkcount(spa_t *, uint64_t);

void spa_log_flushall_start(spa_t *spa, spa_log_flushall_mode_t mode,
    uint64_t txg);
void spa_log_flushall_done(spa_t *spa);
void spa_log_flushall_cancel(spa_t *spa);
boolean_t spa_log_flushall_active(spa_t *spa);

extern int zfs_keep_log_spacemaps_at_export;

#endif /* _SYS_SPA_LOG_SPACEMAP_H */
