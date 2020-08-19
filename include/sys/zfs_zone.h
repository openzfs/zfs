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
 * Copyright 2015, Joyent, Inc. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_ZONE_H
#define	_SYS_FS_ZFS_ZONE_H

#ifdef _KERNEL
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	ZFS_ZONE_IOP_READ = 0,
	ZFS_ZONE_IOP_WRITE,
	ZFS_ZONE_IOP_LOGICAL_WRITE,
} zfs_zone_iop_type_t;

extern void zfs_zone_io_throttle(zfs_zone_iop_type_t);

extern void zfs_zone_zio_init(zio_t *);
extern void zfs_zone_zio_start(zio_t *);
extern void zfs_zone_zio_done(zio_t *);
extern void zfs_zone_zio_dequeue(zio_t *);
extern void zfs_zone_zio_enqueue(zio_t *);
extern void zfs_zone_report_txg_sync(void *);
extern hrtime_t zfs_zone_txg_delay();
#ifdef _KERNEL
extern zio_t *zfs_zone_schedule(vdev_queue_t *, zio_priority_t, avl_index_t,
    avl_tree_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_ZONE_H */
