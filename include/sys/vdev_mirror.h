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
