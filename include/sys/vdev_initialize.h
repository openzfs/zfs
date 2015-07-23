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
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _SYS_VDEV_INITIALIZE_H
#define	_SYS_VDEV_INITIALIZE_H

#include <sys/spa.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern void vdev_initialize(vdev_t *vd);
extern void vdev_initialize_stop(vdev_t *vd,
    vdev_initializing_state_t tgt_state, list_t *vd_list);
extern void vdev_initialize_stop_all(vdev_t *vd,
    vdev_initializing_state_t tgt_state);
extern void vdev_initialize_stop_wait(spa_t *spa, list_t *vd_list);
extern void vdev_initialize_restart(vdev_t *vd);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_INITIALIZE_H */
