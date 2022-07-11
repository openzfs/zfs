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
 * Copyright (c) 2018, Intel Corporation.
 * Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
 */

#ifndef	_SYS_VDEV_REBUILD_H
#define	_SYS_VDEV_REBUILD_H

#include <sys/spa.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Number of entries in the physical vdev_rebuild_phys structure.  This
 * state is stored per top-level as VDEV_ZAP_TOP_VDEV_REBUILD_PHYS.
 */
#define	REBUILD_PHYS_ENTRIES	12

/*
 * On-disk rebuild configuration and state.  When adding new fields they
 * must be added to the end of the structure.
 */
typedef struct vdev_rebuild_phys {
	uint64_t	vrp_rebuild_state;	/* vdev_rebuild_state_t */
	uint64_t	vrp_last_offset;	/* last rebuilt offset */
	uint64_t	vrp_min_txg;		/* minimum missing txg */
	uint64_t	vrp_max_txg;		/* maximum missing txg */
	uint64_t	vrp_start_time;		/* start time */
	uint64_t	vrp_end_time;		/* end time */
	uint64_t	vrp_scan_time_ms;	/* total run time in ms */
	uint64_t	vrp_bytes_scanned;	/* alloc bytes scanned */
	uint64_t	vrp_bytes_issued;	/* read bytes rebuilt */
	uint64_t	vrp_bytes_rebuilt;	/* rebuilt bytes */
	uint64_t	vrp_bytes_est;		/* total bytes to scan */
	uint64_t	vrp_errors;		/* errors during rebuild */
} vdev_rebuild_phys_t;

/*
 * The vdev_rebuild_t describes the current state and how a top-level vdev
 * should be rebuilt.  The core elements are the top-vdev, the metaslab being
 * rebuilt, range tree containing the allocated extents and the on-disk state.
 */
typedef struct vdev_rebuild {
	vdev_t		*vr_top_vdev;		/* top-level vdev to rebuild */
	metaslab_t	*vr_scan_msp;		/* scanning disabled metaslab */
	range_tree_t	*vr_scan_tree;		/* scan ranges (in metaslab) */
	kmutex_t	vr_io_lock;		/* inflight IO lock */
	kcondvar_t	vr_io_cv;		/* inflight IO cv */

	/* In-core state and progress */
	uint64_t	vr_scan_offset[TXG_SIZE];
	uint64_t	vr_prev_scan_time_ms;	/* any previous scan time */
	uint64_t	vr_bytes_inflight_max;	/* maximum bytes inflight */
	uint64_t	vr_bytes_inflight;	/* current bytes inflight */

	/* Per-rebuild pass statistics for calculating bandwidth */
	uint64_t	vr_pass_start_time;
	uint64_t	vr_pass_bytes_scanned;
	uint64_t	vr_pass_bytes_issued;

	/* On-disk state updated by vdev_rebuild_zap_update_sync() */
	vdev_rebuild_phys_t vr_rebuild_phys;
} vdev_rebuild_t;

boolean_t vdev_rebuild_active(vdev_t *);

int vdev_rebuild_load(vdev_t *);
void vdev_rebuild(vdev_t *);
void vdev_rebuild_stop_wait(vdev_t *);
void vdev_rebuild_stop_all(spa_t *);
void vdev_rebuild_restart(spa_t *);
void vdev_rebuild_clear_sync(void *, dmu_tx_t *);
int vdev_rebuild_get_stats(vdev_t *, vdev_rebuild_stat_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_REBUILD_H */
