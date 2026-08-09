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
	/* scan ranges (in metaslab) */
	zfs_range_tree_t	*vr_scan_tree;
	kmutex_t	vr_io_lock;		/* inflight IO lock */
	kcondvar_t	vr_io_cv;		/* inflight IO cv */
	uint64_t	vr_last_txg;		/* last used txg */

	/* In-core state and progress */
	uint64_t	vr_scan_offset[TXG_SIZE];
	uint64_t	vr_prev_scan_time_ms;	/* any previous scan time */
	uint64_t	vr_bytes_inflight_max;	/* maximum bytes inflight */
	uint64_t	vr_bytes_inflight;	/* current bytes inflight */

	/* Per-rebuild pass statistics for calculating bandwidth */
	uint64_t	vr_pass_start_time;
	uint64_t	vr_pass_bytes_scanned;
	uint64_t	vr_pass_bytes_issued;
	uint64_t	vr_pass_bytes_skipped;

	/* On-disk state updated by vdev_rebuild_zap_update_sync() */
	vdev_rebuild_phys_t vr_rebuild_phys;
} vdev_rebuild_t;

boolean_t vdev_rebuild_active(vdev_t *);

int vdev_rebuild_load(vdev_t *);
void vdev_rebuild(vdev_t *, uint64_t);
void vdev_rebuild_txgs(vdev_t *, uint64_t *, uint64_t *);
void vdev_rebuild_stop_wait(vdev_t *);
void vdev_rebuild_stop_all(spa_t *);
void vdev_rebuild_restart(spa_t *);
void vdev_rebuild_clear_sync(void *, dmu_tx_t *);
int vdev_rebuild_get_stats(vdev_t *, vdev_rebuild_stat_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_REBUILD_H */
