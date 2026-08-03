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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef	_SYS_FS_ZFS_VNOPS_H
#define	_SYS_FS_ZFS_VNOPS_H

#include <sys/zfs_vnops_os.h>

extern int zfs_bclone_enabled;

extern int zfs_fsync(znode_t *, int, cred_t *);
extern int zfs_read(znode_t *, zfs_uio_t *, int, cred_t *);
extern int zfs_write(znode_t *, zfs_uio_t *, int, cred_t *);
extern int zfs_holey(znode_t *, ulong_t, loff_t *);
extern int zfs_access(znode_t *, int, int, cred_t *);
extern int zfs_clone_range(znode_t *, uint64_t *, znode_t *, uint64_t *,
    uint64_t *, cred_t *);
extern int zfs_clone_range_replay(znode_t *, uint64_t, uint64_t, uint64_t,
    const blkptr_t *, size_t);
extern int zfs_dedupe_range(znode_t *, uint64_t, znode_t *, uint64_t,
    uint64_t *, cred_t *, boolean_t, boolean_t *);
extern int zfs_rewrite(znode_t *, uint64_t, uint64_t, uint64_t, uint64_t);

extern int zfs_getsecattr(znode_t *, vsecattr_t *, int, cred_t *);
extern int zfs_setsecattr(znode_t *, vsecattr_t *, int, cred_t *);

extern int zfs_get_direct_alignment(znode_t *, uint64_t *);

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
