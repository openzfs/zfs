// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@compeng.uni-frankfurt.de>.
 */

#ifndef _SYS_VDEV_RAIDZ_H
#define	_SYS_VDEV_RAIDZ_H

#include <sys/types.h>
#include <sys/zfs_rlock.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct zio;
struct raidz_col;
struct raidz_row;
struct raidz_map;
struct vdev_raidz;
struct uberblock;
#if !defined(_KERNEL)
struct kernel_param {};
#endif

/*
 * vdev_raidz interface
 */
struct raidz_map *vdev_raidz_map_alloc(struct zio *, uint64_t, uint64_t,
    uint64_t);
struct raidz_map *vdev_raidz_map_alloc_expanded(struct zio *,
    uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, boolean_t);
void vdev_raidz_map_free(struct raidz_map *);
void vdev_raidz_free(struct vdev_raidz *);
void vdev_raidz_generate_parity_row(struct raidz_map *, struct raidz_row *);
void vdev_raidz_generate_parity(struct raidz_map *);
void vdev_raidz_reconstruct(struct raidz_map *, const int *, int);
void vdev_raidz_child_done(zio_t *);
void vdev_raidz_io_done(zio_t *);
void vdev_raidz_checksum_error(zio_t *, struct raidz_col *, abd_t *);
struct raidz_row *vdev_raidz_row_alloc(int, zio_t *);
void vdev_raidz_reflow_copy_scratch(spa_t *);
void raidz_dtl_reassessed(vdev_t *);

extern const zio_vsd_ops_t vdev_raidz_vsd_ops;

/*
 * vdev_raidz_math interface
 */
/* Required, but not used, by ZFS_MODULE_PARAM_CALL */
extern uint32_t zfs_vdev_raidz_impl;
void vdev_raidz_math_init(void);
void vdev_raidz_math_fini(void);
const struct raidz_impl_ops *vdev_raidz_math_get_ops(void);
int vdev_raidz_math_generate(struct raidz_map *, struct raidz_row *);
int vdev_raidz_math_reconstruct(struct raidz_map *, struct raidz_row *,
    const int *, const int *, const int);
int vdev_raidz_impl_set(const char *);
int vdev_raidz_impl_get(char *buffer, size_t size);

typedef struct vdev_raidz_expand {
	uint64_t vre_vdev_id;

	kmutex_t vre_lock;
	kcondvar_t vre_cv;

	/*
	 * How much i/o is outstanding (issued and not completed).
	 */
	uint64_t vre_outstanding_bytes;

	/*
	 * Next offset to issue i/o for.
	 */
	uint64_t vre_offset;

	/*
	 * Lowest offset of a failed expansion i/o.  The expansion will retry
	 * from here.  Once the expansion thread notices the failure and exits,
	 * vre_failed_offset is reset back to UINT64_MAX, and
	 * vre_waiting_for_resilver will be set.
	 */
	uint64_t vre_failed_offset;
	boolean_t vre_waiting_for_resilver;

	/*
	 * Offset that is completing each txg
	 */
	uint64_t vre_offset_pertxg[TXG_SIZE];

	/*
	 * Bytes copied in each txg.
	 */
	uint64_t vre_bytes_copied_pertxg[TXG_SIZE];

	/*
	 * The rangelock prevents normal read/write zio's from happening while
	 * there are expansion (reflow) i/os in progress to the same offsets.
	 */
	zfs_rangelock_t vre_rangelock;

	/*
	 * These fields are stored on-disk in the vdev_top_zap:
	 */
	dsl_scan_state_t vre_state;
	uint64_t vre_start_time;
	uint64_t vre_end_time;
	uint64_t vre_bytes_copied;
} vdev_raidz_expand_t;

typedef struct vdev_raidz {
	/*
	 * Number of child vdevs when this raidz vdev was created (i.e. before
	 * any raidz expansions).
	 */
	int vd_original_width;

	/*
	 * The current number of child vdevs, which may be more than the
	 * original width if an expansion is in progress or has completed.
	 */
	int vd_physical_width;

	int vd_nparity;

	/*
	 * Tree of reflow_node_t's.  The lock protects the avl tree only.
	 * The reflow_node_t's describe completed expansions, and are used
	 * to determine the logical width given a block's birth time.
	 */
	avl_tree_t vd_expand_txgs;
	kmutex_t vd_expand_lock;

	/*
	 * If this vdev is being expanded, spa_raidz_expand is set to this
	 */
	vdev_raidz_expand_t vn_vre;
} vdev_raidz_t;

extern int vdev_raidz_attach_check(vdev_t *);
extern void vdev_raidz_attach_sync(void *, dmu_tx_t *);
extern void spa_start_raidz_expansion_thread(spa_t *);
extern int spa_raidz_expand_get_stats(spa_t *, pool_raidz_expand_stat_t *);
extern int vdev_raidz_load(vdev_t *);

/* RAIDZ scratch area pause points (for testing) */
#define	RAIDZ_EXPAND_PAUSE_NONE	0
#define	RAIDZ_EXPAND_PAUSE_PRE_SCRATCH_1 1
#define	RAIDZ_EXPAND_PAUSE_PRE_SCRATCH_2 2
#define	RAIDZ_EXPAND_PAUSE_PRE_SCRATCH_3 3
#define	RAIDZ_EXPAND_PAUSE_SCRATCH_VALID 4
#define	RAIDZ_EXPAND_PAUSE_SCRATCH_REFLOWED 5
#define	RAIDZ_EXPAND_PAUSE_SCRATCH_POST_REFLOW_1 6
#define	RAIDZ_EXPAND_PAUSE_SCRATCH_POST_REFLOW_2 7

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_RAIDZ_H */
