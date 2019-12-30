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
extern void vdev_autotrim_stop_all(spa_t *spa);
extern void vdev_autotrim_stop_wait(vdev_t *vd);
extern void vdev_autotrim_restart(spa_t *spa);
extern int vdev_trim_simple(vdev_t *vd, uint64_t start, uint64_t size);
extern void vdev_trim_l2arc(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_TRIM_H */
