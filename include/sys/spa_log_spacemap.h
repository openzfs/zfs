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
 * Copyright (c) 2018, 2019 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPA_LOG_SPACEMAP_H
#define	_SYS_SPA_LOG_SPACEMAP_H

#include <sys/avl.h>

typedef struct log_summary_entry {
	uint64_t lse_start;	/* start TXG */
	uint64_t lse_mscount;	/* # of metaslabs needed to be flushed */
	uint64_t lse_blkcount;	/* blocks held by this entry  */
	list_node_t lse_node;
} log_summary_entry_t;

typedef struct spa_unflushed_stats  {
	/* used for memory heuristic */
	uint64_t sus_memused;	/* current memory used for unflushed trees */

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
} spa_log_sm_t;

int spa_ld_log_spacemaps(spa_t *);

void spa_generate_syncing_log_sm(spa_t *, dmu_tx_t *);
void spa_flush_metaslabs(spa_t *, dmu_tx_t *);
void spa_sync_close_syncing_log_sm(spa_t *);

void spa_cleanup_old_sm_logs(spa_t *, dmu_tx_t *);

uint64_t spa_log_sm_blocklimit(spa_t *);
void spa_log_sm_set_blocklimit(spa_t *);
uint64_t spa_log_sm_nblocks(spa_t *);
uint64_t spa_log_sm_memused(spa_t *);

void spa_log_sm_decrement_mscount(spa_t *, uint64_t);
void spa_log_sm_increment_current_mscount(spa_t *);

void spa_log_summary_add_flushed_metaslab(spa_t *);
void spa_log_summary_decrement_mscount(spa_t *, uint64_t);
void spa_log_summary_decrement_blkcount(spa_t *, uint64_t);

boolean_t spa_flush_all_logs_requested(spa_t *);

extern int zfs_keep_log_spacemaps_at_export;

#endif /* _SYS_SPA_LOG_SPACEMAP_H */
