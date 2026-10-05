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
	(ZTOZSB(zdp)->z_show_ctldir))

struct path;

extern int zfs_expire_snapshot;

/* zfsctl generic functions */
extern int zfsctl_create(zfsvfs_t *);
extern void zfsctl_destroy(zfsvfs_t *);
extern struct vnode *zfsctl_root(znode_t *);
extern void zfsctl_init(void);
extern void zfsctl_fini(void);
extern boolean_t zfsctl_is_node(struct vnode *ip);
extern boolean_t zfsctl_is_snapdir(struct vnode *ip);
extern int zfsctl_fid(struct vnode *ip, fid_t *fidp);

/* zfsctl '.zfs' functions */
extern int zfsctl_root_lookup(struct vnode *dvp, char *name,
    struct vnode **vpp, int flags, int *direntflags,
    struct componentname *realpnp);

/* zfsctl '.zfs/snapshot' functions */
extern int zfsctl_snapdir_lookup(struct vnode *dip, char *name,
    struct vnode **ipp, int flags, cred_t *cr, int *direntflags,
    struct componentname *realpnp);
extern int zfsctl_snapdir_rename(struct vnode *sdip, char *sname,
    struct vnode *tdip, char *tname, cred_t *cr, int flags);
extern int zfsctl_snapdir_remove(struct vnode *dip, char *name, cred_t *cr,
    int flags);
extern int zfsctl_snapdir_mkdir(struct vnode *dip, char *dirname, vattr_t *vap,
    struct vnode **ipp, cred_t *cr, int flags);
extern int zfsctl_snapshot_mount(struct vnode *, int flags);
extern int zfsctl_snapshot_unmount(const char *);
extern int zfsctl_snapshot_unmount_node(struct vnode *, const char *);
extern int zfsctl_snapshot_unmount_delay(spa_t *spa, uint64_t objsetid,
    int delay);
extern int zfsctl_snapdir_vget(struct mount *sb, uint64_t objsetid,
    int gen, struct vnode **ipp);

/* zfsctl '.zfs/shares' functions */
extern int zfsctl_shares_lookup(struct vnode *dip, char *name,
    struct vnode **ipp, int flags, cred_t *cr, int *direntflags,
    struct componentname *realpnp);

extern int zfsctl_vnop_lookup(struct vnop_lookup_args *);
extern int zfsctl_vnop_getattr(struct vnop_getattr_args *);
extern int zfsctl_vnop_readdir(struct vnop_readdir_args *);
extern int zfsctl_vnop_mkdir(struct vnop_mkdir_args *);
extern int zfsctl_vnop_rmdir(struct vnop_rmdir_args *);
extern int zfsctl_vnop_access(struct vnop_access_args *);
extern int zfsctl_vnop_open(struct vnop_open_args *);
extern int zfsctl_vnop_close(struct vnop_close_args *);
extern int zfsctl_vnop_inactive(struct vnop_inactive_args *);
extern int zfsctl_vnop_reclaim(struct vnop_reclaim_args *);

extern void zfs_ereport_snapshot_post(const char *subclass, spa_t *spa,
    const char *name);

extern void	zfsctl_mount_signal(char *, boolean_t);


/*
 * These vnodes numbers are reserved for the .zfs control directory.
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
