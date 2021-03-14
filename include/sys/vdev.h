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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2019, Datto Inc. All rights reserved.
 */

#ifndef _SYS_VDEV_H
#define	_SYS_VDEV_H

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/space_map.h>
#include <sys/metaslab.h>
#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum vdev_dtl_type {
	DTL_MISSING,	/* 0% replication: no copies of the data */
	DTL_PARTIAL,	/* less than 100% replication: some copies missing */
	DTL_SCRUB,	/* unable to fully repair during scrub/resilver */
	DTL_OUTAGE,	/* temporarily missing (used to attempt detach) */
	DTL_TYPES
} vdev_dtl_type_t;

extern int zfs_nocacheflush;

typedef boolean_t vdev_open_children_func_t(vdev_t *vd);

extern void vdev_dbgmsg(vdev_t *vd, const char *fmt, ...);
extern void vdev_dbgmsg_print_tree(vdev_t *, int);
extern int vdev_open(vdev_t *);
extern void vdev_open_children(vdev_t *);
extern void vdev_open_children_subset(vdev_t *, vdev_open_children_func_t *);
extern int vdev_validate(vdev_t *);
extern int vdev_copy_path_strict(vdev_t *, vdev_t *);
extern void vdev_copy_path_relaxed(vdev_t *, vdev_t *);
extern void vdev_close(vdev_t *);
extern int vdev_create(vdev_t *, uint64_t txg, boolean_t isreplace);
extern void vdev_reopen(vdev_t *);
extern int vdev_validate_aux(vdev_t *vd);
extern zio_t *vdev_probe(vdev_t *vd, zio_t *pio);
extern boolean_t vdev_is_concrete(vdev_t *vd);
extern boolean_t vdev_is_bootable(vdev_t *vd);
extern vdev_t *vdev_lookup_top(spa_t *spa, uint64_t vdev);
extern vdev_t *vdev_lookup_by_guid(vdev_t *vd, uint64_t guid);
extern int vdev_count_leaves(spa_t *spa);
extern void vdev_dtl_dirty(vdev_t *vd, vdev_dtl_type_t d,
    uint64_t txg, uint64_t size);
extern boolean_t vdev_dtl_contains(vdev_t *vd, vdev_dtl_type_t d,
    uint64_t txg, uint64_t size);
extern boolean_t vdev_dtl_empty(vdev_t *vd, vdev_dtl_type_t d);
extern boolean_t vdev_default_need_resilver(vdev_t *vd, const dva_t *dva,
    size_t psize, uint64_t phys_birth);
extern boolean_t vdev_dtl_need_resilver(vdev_t *vd, const dva_t *dva,
    size_t psize, uint64_t phys_birth);
extern void vdev_dtl_reassess(vdev_t *vd, uint64_t txg, uint64_t scrub_txg,
    boolean_t scrub_done, boolean_t rebuild_done);
extern boolean_t vdev_dtl_required(vdev_t *vd);
extern boolean_t vdev_resilver_needed(vdev_t *vd,
    uint64_t *minp, uint64_t *maxp);
extern void vdev_destroy_unlink_zap(vdev_t *vd, uint64_t zapobj,
    dmu_tx_t *tx);
extern uint64_t vdev_create_link_zap(vdev_t *vd, dmu_tx_t *tx);
extern void vdev_construct_zaps(vdev_t *vd, dmu_tx_t *tx);
extern void vdev_destroy_spacemaps(vdev_t *vd, dmu_tx_t *tx);
extern void vdev_indirect_mark_obsolete(vdev_t *vd, uint64_t offset,
    uint64_t size);
extern void spa_vdev_indirect_mark_obsolete(spa_t *spa, uint64_t vdev,
    uint64_t offset, uint64_t size, dmu_tx_t *tx);
extern boolean_t vdev_replace_in_progress(vdev_t *vdev);

extern void vdev_hold(vdev_t *);
extern void vdev_rele(vdev_t *);

extern int vdev_metaslab_init(vdev_t *vd, uint64_t txg);
extern void vdev_metaslab_fini(vdev_t *vd);
extern void vdev_metaslab_set_size(vdev_t *);
extern void vdev_expand(vdev_t *vd, uint64_t txg);
extern void vdev_split(vdev_t *vd);
extern void vdev_deadman(vdev_t *vd, char *tag);

typedef void vdev_xlate_func_t(void *arg, range_seg64_t *physical_rs);

extern boolean_t vdev_xlate_is_empty(range_seg64_t *rs);
extern void vdev_xlate(vdev_t *vd, const range_seg64_t *logical_rs,
    range_seg64_t *physical_rs, range_seg64_t *remain_rs);
extern void vdev_xlate_walk(vdev_t *vd, const range_seg64_t *logical_rs,
    vdev_xlate_func_t *func, void *arg);

extern void vdev_get_stats_ex(vdev_t *vd, vdev_stat_t *vs, vdev_stat_ex_t *vsx);

extern metaslab_group_t *vdev_get_mg(vdev_t *vd, metaslab_class_t *mc);

extern void vdev_get_stats(vdev_t *vd, vdev_stat_t *vs);
extern void vdev_clear_stats(vdev_t *vd);
extern void vdev_stat_update(zio_t *zio, uint64_t psize);
extern void vdev_scan_stat_init(vdev_t *vd);
extern void vdev_propagate_state(vdev_t *vd);
extern void vdev_set_state(vdev_t *vd, boolean_t isopen, vdev_state_t state,
    vdev_aux_t aux);
extern boolean_t vdev_children_are_offline(vdev_t *vd);

extern void vdev_space_update(vdev_t *vd,
    int64_t alloc_delta, int64_t defer_delta, int64_t space_delta);

extern int64_t vdev_deflated_space(vdev_t *vd, int64_t space);

extern uint64_t vdev_psize_to_asize(vdev_t *vd, uint64_t psize);

/*
 * Return the amount of space allocated for a gang block header.
 */
static inline uint64_t
vdev_gang_header_asize(vdev_t *vd)
{
	return (vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE));
}

extern int vdev_fault(spa_t *spa, uint64_t guid, vdev_aux_t aux);
extern int vdev_degrade(spa_t *spa, uint64_t guid, vdev_aux_t aux);
extern int vdev_online(spa_t *spa, uint64_t guid, uint64_t flags,
    vdev_state_t *);
extern int vdev_offline(spa_t *spa, uint64_t guid, uint64_t flags);
extern void vdev_clear(spa_t *spa, vdev_t *vd);

extern boolean_t vdev_is_dead(vdev_t *vd);
extern boolean_t vdev_readable(vdev_t *vd);
extern boolean_t vdev_writeable(vdev_t *vd);
extern boolean_t vdev_allocatable(vdev_t *vd);
extern boolean_t vdev_accessible(vdev_t *vd, zio_t *zio);
extern boolean_t vdev_is_spacemap_addressable(vdev_t *vd);

extern void vdev_cache_init(vdev_t *vd);
extern void vdev_cache_fini(vdev_t *vd);
extern boolean_t vdev_cache_read(zio_t *zio);
extern void vdev_cache_write(zio_t *zio);
extern void vdev_cache_purge(vdev_t *vd);

extern void vdev_queue_init(vdev_t *vd);
extern void vdev_queue_fini(vdev_t *vd);
extern zio_t *vdev_queue_io(zio_t *zio);
extern void vdev_queue_io_done(zio_t *zio);
extern void vdev_queue_change_io_priority(zio_t *zio, zio_priority_t priority);

extern int vdev_queue_length(vdev_t *vd);
extern uint64_t vdev_queue_last_offset(vdev_t *vd);

extern void vdev_config_dirty(vdev_t *vd);
extern void vdev_config_clean(vdev_t *vd);
extern int vdev_config_sync(vdev_t **svd, int svdcount, uint64_t txg);

extern void vdev_state_dirty(vdev_t *vd);
extern void vdev_state_clean(vdev_t *vd);

extern void vdev_defer_resilver(vdev_t *vd);
extern boolean_t vdev_clear_resilver_deferred(vdev_t *vd, dmu_tx_t *tx);

typedef enum vdev_config_flag {
	VDEV_CONFIG_SPARE = 1 << 0,
	VDEV_CONFIG_L2CACHE = 1 << 1,
	VDEV_CONFIG_REMOVING = 1 << 2,
	VDEV_CONFIG_MOS = 1 << 3,
	VDEV_CONFIG_MISSING = 1 << 4
} vdev_config_flag_t;

extern void vdev_top_config_generate(spa_t *spa, nvlist_t *config);
extern nvlist_t *vdev_config_generate(spa_t *spa, vdev_t *vd,
    boolean_t getstats, vdev_config_flag_t flags);

/*
 * Label routines
 */
struct uberblock;
extern uint64_t vdev_label_offset(uint64_t psize, int l, uint64_t offset);
extern int vdev_label_number(uint64_t psise, uint64_t offset);
extern nvlist_t *vdev_label_read_config(vdev_t *vd, uint64_t txg);
extern void vdev_uberblock_load(vdev_t *, struct uberblock *, nvlist_t **);
extern void vdev_config_generate_stats(vdev_t *vd, nvlist_t *nv);
extern void vdev_label_write(zio_t *zio, vdev_t *vd, int l, abd_t *buf, uint64_t
    offset, uint64_t size, zio_done_func_t *done, void *priv, int flags);
extern int vdev_label_read_bootenv(vdev_t *, nvlist_t *);
extern int vdev_label_write_bootenv(vdev_t *, nvlist_t *);

typedef enum {
	VDEV_LABEL_CREATE,	/* create/add a new device */
	VDEV_LABEL_REPLACE,	/* replace an existing device */
	VDEV_LABEL_SPARE,	/* add a new hot spare */
	VDEV_LABEL_REMOVE,	/* remove an existing device */
	VDEV_LABEL_L2CACHE,	/* add an L2ARC cache device */
	VDEV_LABEL_SPLIT	/* generating new label for split-off dev */
} vdev_labeltype_t;

extern int vdev_label_init(vdev_t *vd, uint64_t txg, vdev_labeltype_t reason);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_H */
