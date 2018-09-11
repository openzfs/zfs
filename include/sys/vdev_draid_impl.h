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
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef _VDEV_DRAID_IMPL_H
#define	_VDEV_DRAID_IMPL_H

#include <sys/types.h>
#include <sys/abd.h>
#include <sys/nvpair.h>
#include <sys/zio.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_raidz_impl.h>

#include "draid_config.h"

#ifdef  __cplusplus
extern "C" {
#endif

extern boolean_t vdev_draid_ms_mirrored(const vdev_t *, uint64_t);
extern boolean_t vdev_draid_group_degraded(vdev_t *, vdev_t *,
    uint64_t, uint64_t, boolean_t);
extern uint64_t vdev_draid_check_block(const vdev_t *, uint64_t, uint64_t *);
extern uint64_t vdev_draid_get_astart(const vdev_t *, const uint64_t);
extern uint64_t vdev_draid_offset2group(const vdev_t *, uint64_t, boolean_t);
extern uint64_t vdev_draid_group2offset(const vdev_t *, uint64_t, boolean_t);
extern boolean_t vdev_draid_is_remainder_group(const vdev_t *,
    uint64_t, boolean_t);
extern uint64_t vdev_draid_get_groupsz(const vdev_t *, boolean_t);
extern void vdev_draid_fix_skip_sectors(zio_t *);
extern int vdev_draid_hide_skip_sectors(raidz_map_t *);
extern void vdev_draid_restore_skip_sectors(raidz_map_t *, int);
extern boolean_t vdev_draid_readable(vdev_t *, uint64_t);
extern boolean_t vdev_draid_is_dead(vdev_t *, uint64_t);
extern boolean_t vdev_draid_missing(vdev_t *, uint64_t, uint64_t, uint64_t);
extern vdev_t *vdev_draid_spare_get_parent(vdev_t *);
extern nvlist_t *vdev_draid_spare_read_config(vdev_t *);
extern uint64_t vdev_draid_asize2psize(vdev_t *, uint64_t, uint64_t);
extern uint64_t vdev_draid_max_rebuildable_asize(vdev_t *, uint64_t);
extern void vdev_draid_debug_zio(zio_t *, boolean_t);

#ifdef  __cplusplus
}
#endif

#endif /* _VDEV_DRAID_IMPL_H */
