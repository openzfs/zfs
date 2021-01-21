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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_VNOPS_H
#define	_SYS_FS_ZFS_VNOPS_H
#include <sys/zfs_vnops_os.h>

extern int zfs_fsync(znode_t *, int, cred_t *);
extern int zfs_read(znode_t *, zfs_uio_t *, int, cred_t *);
extern int zfs_write(znode_t *, zfs_uio_t *, int, cred_t *);
extern int zfs_holey(znode_t *, ulong_t, loff_t *);
extern int zfs_access(znode_t *, int, int, cred_t *);

extern int zfs_getsecattr(znode_t *, vsecattr_t *, int, cred_t *);
extern int zfs_setsecattr(znode_t *, vsecattr_t *, int, cred_t *);

extern int mappedread(znode_t *, int, zfs_uio_t *);
extern int mappedread_sf(znode_t *, int, zfs_uio_t *);
extern void update_pages(znode_t *, int64_t, int, objset_t *);

/*
 * Platform code that asynchronously drops zp's inode / vnode_t.
 *
 * Asynchronous dropping ensures that the caller will never drop the
 * last reference on an inode / vnode_t in the current context.
 * Doing so while holding open a tx could result in a deadlock if
 * the platform calls into filesystem again in the implementation
 * of inode / vnode_t dropping (e.g. call from iput_final()).
 */
extern void zfs_zrele_async(znode_t *zp);

extern zil_get_data_t zfs_get_data;

#endif
