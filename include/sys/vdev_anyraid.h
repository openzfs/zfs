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
 * Copyright (c) 2025, Klara Inc.
 */

#ifndef _SYS_VDEV_ANYRAID_H
#define	_SYS_VDEV_ANYRAID_H

#include <sys/types.h>
#include <sys/vdev.h>
#include <sys/zfs_rlock.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct vdev_anyraid_node vdev_anyraid_node_t;

typedef enum vdev_anyraid_parity_type {
	VAP_MIRROR, // includes raid0, i.e. a 0-parity mirror
	VAP_RAIDZ,
	VAP_TYPES,
} vdev_anyraid_parity_type_t;

typedef struct vdev_anyraid_relocate_task {
	list_node_t	vart_node;
	uint8_t		vart_source_disk;
	uint8_t		vart_dest_disk;
	uint16_t	vart_source_idx;
	uint16_t	vart_dest_idx;
	uint32_t	vart_tile;
	uint32_t	vart_task;
	uint32_t	vart_dis_ms; // Only used during resume
} vdev_anyraid_relocate_task_t;

typedef struct vdev_anyraid_relocate {
	list_t 		var_list;
	list_t 		var_done_list;
	uint64_t	var_offset;
	uint64_t	var_task;
	uint64_t	var_synced_offset;
	uint64_t	var_synced_task;
	uint64_t	var_vd;

	dsl_scan_state_t var_state;
	uint64_t	var_start_time;
	uint64_t	var_end_time;
	uint64_t	var_bytes_copied;
	uint64_t	var_outstanding_bytes;

	uint64_t	var_failed_offset;
	uint64_t	var_failed_task;
	boolean_t	var_waiting_for_resilver;
	uint64_t	var_offset_pertxg[TXG_SIZE];
	uint64_t	var_task_pertxg[TXG_SIZE];
	uint64_t	var_bytes_copied_pertxg[TXG_SIZE];

	kmutex_t	var_lock;
	kcondvar_t	var_cv;
	uint64_t	var_nonalloc;
	uint64_t	var_object;
} vdev_anyraid_relocate_t;

typedef struct vdev_anyraid {
	vdev_anyraid_parity_type_t vd_parity_type;
	/*
	 * The parity of the mismatched vdev; 0 for raid0, or the number of
	 * mirrors.
	 */
	uint_t		vd_nparity;
	uint8_t		vd_ndata;
	uint8_t		vd_width;
	uint64_t	vd_tile_size;

	krwlock_t	vd_lock;
	avl_tree_t	vd_tile_map;
	avl_tree_t	vd_children_tree;
	uint32_t	vd_checkpoint_tile;
	vdev_anyraid_node_t **vd_children;
	vdev_anyraid_relocate_t vd_relocate;
	zfs_rangelock_t	vd_rangelock;
} vdev_anyraid_t;

#define	VDEV_ANYRAID_MAX_DISKS	(1 << 8)

/*
 * ==========================================================================
 * Externally-accessed function definitions
 * ==========================================================================
 */
extern void vdev_anyraid_write_map_sync(vdev_t *vd, zio_t *pio, uint64_t txg,
    uint64_t *good_writes, int flags, vdev_config_sync_status_t status);

extern void vdev_anyraid_expand(vdev_t *tvd, vdev_t *newvd);
extern boolean_t vdev_anyraid_mapped(vdev_t *vd, uint64_t offset, uint64_t txg);
uint64_t vdev_anyraid_child_num_tiles(vdev_t *vd, vdev_t *cvd);
uint64_t vdev_anyraid_child_capacity(vdev_t *vd, vdev_t *cvd);
int spa_anyraid_relocate_get_stats(spa_t *spa,
    pool_anyraid_relocate_stat_t *pars);
int vdev_anyraid_load(vdev_t *vd);
void anyraid_dtl_reassessed(vdev_t *vd);

vdev_anyraid_relocate_t *vdev_anyraid_relocate_status(vdev_t *vd);
void vdev_anyraid_setup_rebalance(vdev_t *vd, dmu_tx_t *tx);
void spa_start_anyraid_relocate_thread(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_ANYRAID_H */
