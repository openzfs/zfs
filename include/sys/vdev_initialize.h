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
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _SYS_VDEV_INITIALIZE_H
#define	_SYS_VDEV_INITIALIZE_H

#include <sys/spa.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern void vdev_initialize(vdev_t *vd, uint64_t value,
    boolean_t value_provided);
extern void vdev_uninitialize(vdev_t *vd);
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
