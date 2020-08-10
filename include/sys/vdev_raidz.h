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
struct raidz_row;
struct raidz_map;
struct vdev_raidz;
#if !defined(_KERNEL)
struct kernel_param {};
#endif

/*
 * vdev_raidz interface
 */
struct raidz_map *vdev_raidz_map_alloc(struct zio *, uint64_t, uint64_t,
    uint64_t);
struct raidz_map *vdev_raidz_map_alloc_expanded(abd_t *, uint64_t, uint64_t,
    uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
void vdev_raidz_map_free(struct raidz_map *);
void vdev_raidz_free(struct vdev_raidz *);
void vdev_raidz_generate_parity(struct raidz_map *);
void vdev_raidz_reconstruct(struct raidz_map *, const int *, int);

/*
 * vdev_raidz_math interface
 */
void vdev_raidz_math_init(void);
void vdev_raidz_math_fini(void);
const struct raidz_impl_ops *vdev_raidz_math_get_ops(void);
int vdev_raidz_math_generate(struct raidz_map *, struct raidz_row *);
int vdev_raidz_math_reconstruct(struct raidz_map *, struct raidz_row *,
    const int *, const int *, const int);
int vdev_raidz_impl_set(const char *);

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
	 * Next offset to issue i/o for which has been synced to disk.
	 */
	uint64_t vre_offset_phys;

	uint64_t vre_offset_pertxg[TXG_SIZE];

	/*
	 * Last reflow txg per attached device.
	 */
	avl_tree_t vre_txgs;

	dsl_scan_state_t vre_state;
	time_t vre_start_time;
	time_t vre_end_time;

	zfs_rangelock_t vre_rangelock;
} vdev_raidz_expand_t;

typedef struct vdev_raidz {
	int vd_logical_width;
	int vd_physical_width;
	int vd_nparity;
	/*
	 * If this vdev is being expanded, spa_raidz_expand is set to this
	 */
	vdev_raidz_expand_t vn_vre;
} vdev_raidz_t;

extern void vdev_raidz_attach_sync(void *, dmu_tx_t *);
extern void vdev_raidz_config_generate(vdev_t *, nvlist_t *);
extern void *vdev_raidz_get_tsd(spa_t *, nvlist_t *);
extern void spa_start_raidz_expansion_thread(spa_t *);
extern int spa_raidz_expand_get_stats(spa_t *, pool_raidz_expand_stat_t *);
extern int vdev_raidz_load(vdev_t *);
#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_RAIDZ_H */
