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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef	_SYS_FS_ZFS_VNOPS_H
#define	_SYS_FS_ZFS_VNOPS_H

#include <sys/zfs_vnops_os.h>

extern int zfs_bclone_enabled;

/*
 * Direct I/O tunables.  zfs_dio_enabled can be set to 0 to force all
 * I/O through the ARC; zfs_dio_strict returns EINVAL for unaligned
 * DIO instead of falling back.
 */
extern int zfs_dio_enabled;
extern int zfs_dio_strict;

extern int zfs_fsync(znode_t *, int, cred_t *);
extern int zfs_read(znode_t *, zfs_uio_t *, int, cred_t *);
extern int zfs_write(znode_t *, zfs_uio_t *, int, cred_t *);

/*
 * Direct I/O page-pinning setup.  Pins user pages for O_DIRECT reads,
 * enforces alignment, and skips DIO for mmap'd or encrypted ranges.
 * Returns 0 and sets UIO_DIRECT in uio->uio_extflg on success.
 */
extern int zfs_setup_direct(struct znode *, zfs_uio_t *, zfs_uio_rw_t, int *);

/*
 * Clear the SUID/SGID bits after a write by non-owner.
 * Called from the async write completion path (zfs_vnops_os.c on Linux)
 * as well as from the synchronous zfs_write().
 */
extern void zfs_clear_setid_bits_if_necessary(zfsvfs_t *, znode_t *, cred_t *,
    uint64_t *, dmu_tx_t *);

extern int zfs_holey(znode_t *, ulong_t, loff_t *);
extern int zfs_access(znode_t *, int, int, cred_t *);
extern int zfs_clone_range(znode_t *, uint64_t *, znode_t *, uint64_t *,
    uint64_t *, cred_t *);
extern int zfs_clone_range_replay(znode_t *, uint64_t, uint64_t, uint64_t,
    const blkptr_t *, size_t);
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
