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
 * Copyright (c) 2019 Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_VDEV_TRIM_H
#define	_SYS_VDEV_TRIM_H

#include <sys/spa.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern unsigned int zfs_trim_metaslab_skip;

extern void vdev_trim(vdev_t *vd, uint64_t rate, boolean_t partial,
    boolean_t secure);
extern void vdev_trim_stop(vdev_t *vd, vdev_trim_state_t tgt, list_t *vd_list);
extern void vdev_trim_stop_all(vdev_t *vd, vdev_trim_state_t tgt_state);
extern void vdev_trim_stop_wait(spa_t *spa, list_t *vd_list);
extern void vdev_trim_restart(vdev_t *vd);
extern void vdev_autotrim(spa_t *spa);
extern void vdev_autotrim_kick(spa_t *spa);
extern void vdev_autotrim_stop_all(spa_t *spa);
extern void vdev_autotrim_stop_wait(vdev_t *vd);
extern void vdev_autotrim_restart(spa_t *spa);
extern int vdev_trim_simple(vdev_t *vd, uint64_t start, uint64_t size);
extern void vdev_trim_l2arc(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_TRIM_H */
