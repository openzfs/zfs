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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_ZFS_CTLDIR_H
#define	_ZFS_CTLDIR_H

#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>

#define	ZFS_CTLDIR_NAME		".zfs"
#define	ZFS_SNAPDIR_NAME	"snapshot"
#define	ZFS_SHAREDIR_NAME	"shares"

#define	zfs_has_ctldir(zdp)	\
	((zdp)->z_id == ZTOZSB(zdp)->z_root && \
	(ZTOZSB(zdp)->z_ctldir != NULL))
#define	zfs_show_ctldir(zdp)	\
	(zfs_has_ctldir(zdp) && \
	(ZTOZSB(zdp)->z_show_ctldir == ZFS_SNAPDIR_VISIBLE))

extern int zfs_expire_snapshot;

/* zfsctl generic functions */
extern int zfsctl_create(zfsvfs_t *);
extern void zfsctl_destroy(zfsvfs_t *);
extern struct inode *zfsctl_root(znode_t *);
extern boolean_t zfsctl_is_node(struct inode *ip);
extern boolean_t zfsctl_is_snapdir(struct inode *ip);
extern int zfsctl_fid(struct inode *ip, fid_t *fidp);

/* zfsctl '.zfs' functions */
extern int zfsctl_root_lookup(struct inode *dip, const char *name,
    struct inode **ipp, int flags, cred_t *cr, int *direntflags,
    pathname_t *realpnp);

/* zfsctl '.zfs/snapshot' functions */
typedef struct zfs_snapentry zfs_snapentry_t;
extern int zfsctl_snapdir_lookup(struct inode *dip, const char *name,
    struct inode **ipp, int flags, cred_t *cr, int *direntflags,
    pathname_t *realpnp);
extern int zfsctl_snapdir_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry, cred_t *cr);
extern int zfsctl_snapdir_remove(struct inode *dip, struct dentry *dentry,
    cred_t *cr);
extern int zfsctl_snapdir_mkdir(struct inode *dip, const char *dirname,
    vattr_t *vap, struct inode **ipp, cred_t *cr, int flags);
extern int zfsctl_snapshot_mount(struct path *path, struct vfsmount **mntp);
extern void zfsctl_snapshot_finish_mount(zfs_snapentry_t *se,
    struct vfsmount *mnt);
extern int zfsctl_snapshot_unmount(const char *snapname);
extern int zfsctl_snapdir_vget(struct super_block *sb, uint64_t objsetid,
    int gen, struct inode **ipp);

/* zfsctl '.zfs/shares' functions */
extern int zfsctl_shares_lookup(struct inode *dip, char *name,
    struct inode **ipp, int flags, cred_t *cr, int *direntflags,
    pathname_t *realpnp);

/*
 * These inodes numbers are reserved for the .zfs control directory.
 * It is important that they be no larger that 48-bits because only
 * 6 bytes are reserved in the NFS file handle for the object number.
 * However, they should be as large as possible to avoid conflicts
 * with the objects which are assigned monotonically by the dmu.
 */
#define	ZFSCTL_INO_ROOT		0x0000FFFFFFFFFFFFULL
#define	ZFSCTL_INO_SHARES	0x0000FFFFFFFFFFFEULL
#define	ZFSCTL_INO_SNAPDIR	0x0000FFFFFFFFFFFDULL
#define	ZFSCTL_INO_SNAPDIRS	0x0000FFFFFFFFFFFCULL

#define	ZFSCTL_EXPIRE_SNAPSHOT	300

#endif	/* _ZFS_CTLDIR_H */
