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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ZFS_CTLDIR_H
#define	_ZFS_CTLDIR_H

#include <sys/vnode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_CTLDIR_NAME		".zfs"

#define	zfs_has_ctldir(zdp)	\
	((zdp)->z_id == (zdp)->z_zfsvfs->z_root && \
	((zdp)->z_zfsvfs->z_ctldir != NULL))
#define	zfs_show_ctldir(zdp)	\
	(zfs_has_ctldir(zdp) && \
	((zdp)->z_zfsvfs->z_show_ctldir == ZFS_SNAPDIR_VISIBLE))

void zfsctl_create(zfsvfs_t *);
void zfsctl_destroy(zfsvfs_t *);
int zfsctl_root(zfsvfs_t *, int, vnode_t **);
void zfsctl_init(void);
void zfsctl_fini(void);
boolean_t zfsctl_is_node(vnode_t *);
int zfsctl_snapshot_unmount(const char *snapname);
int zfsctl_rename_snapshot(const char *from, const char *to);
int zfsctl_destroy_snapshot(const char *snapname, int force);
int zfsctl_umount_snapshots(vfs_t *, int, cred_t *);

int zfsctl_lookup_objset(vfs_t *vfsp, uint64_t objsetid, zfsvfs_t **zfsvfsp);

#define	ZFSCTL_INO_ROOT		0x1
#define	ZFSCTL_INO_SNAPDIR	0x2

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_CTLDIR_H */
