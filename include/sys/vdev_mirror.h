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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2025, Klara Inc.
 */

#ifndef _SYS_VDEV_MIRROR_H
#define	_SYS_VDEV_MIRROR_H

#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/vdev.h>
#include <sys/abd.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Virtual device vector for mirroring.
 */
typedef struct mirror_child {
	vdev_t		*mc_vd;
	abd_t		*mc_abd;
	uint64_t	mc_offset;
	int		mc_error;
	int		mc_load;
	uint8_t		mc_tried;
	uint8_t		mc_skipped;
	uint8_t		mc_speculative;
	uint8_t		mc_rebuilding;
} mirror_child_t;

typedef struct mirror_map {
	int		*mm_preferred;
	int		mm_preferred_cnt;
	int		mm_children;
	boolean_t	mm_resilvering;
	boolean_t	mm_rebuilding;
	boolean_t	mm_root;
	mirror_child_t	mm_child[];
} mirror_map_t;

mirror_map_t *vdev_mirror_map_alloc(int children, boolean_t resilvering,
    boolean_t root);
void vdev_mirror_io_start_impl(zio_t *zio, mirror_map_t *mm);
void vdev_mirror_io_done(zio_t *zio);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_MIRROR_H */
