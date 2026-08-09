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

#ifndef	_SYS_FS_ZFS_DIR_H
#define	_SYS_FS_ZFS_DIR_H

#include <sys/pathname.h>
#include <sys/dmu.h>
#include <sys/zfs_znode.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* zfs_dirent_lock() flags */
#define	ZNEW		0x0001		/* entry should not exist */
#define	ZEXISTS		0x0002		/* entry should exist */
#define	ZSHARED		0x0004		/* shared access (zfs_dirlook()) */
#define	ZXATTR		0x0008		/* we want the xattr dir */
#define	ZRENAMING	0x0010		/* znode is being renamed */
#define	ZCILOOK		0x0020		/* case-insensitive lookup requested */
#define	ZCIEXACT	0x0040		/* c-i requires c-s match (rename) */
#define	ZHAVELOCK	0x0080		/* z_name_lock is already held */

/* mknode flags */
#define	IS_ROOT_NODE	0x01		/* create a root node */
#define	IS_XATTR	0x02		/* create an extended attribute node */
#define	IS_TMPFILE	0x04		/* create a tmpfile */

extern int zfs_dirent_lock(zfs_dirlock_t **, znode_t *, char *, znode_t **,
    int, int *, pathname_t *);
extern void zfs_dirent_unlock(zfs_dirlock_t *);
extern int zfs_drop_nlink(znode_t *, dmu_tx_t *, boolean_t *);
extern int zfs_link_create(zfs_dirlock_t *, znode_t *, dmu_tx_t *, int);
extern int zfs_link_destroy(zfs_dirlock_t *, znode_t *, dmu_tx_t *, int,
    boolean_t *);
extern int zfs_dirlook(znode_t *, char *, znode_t **, int, int *,
    pathname_t *);
extern void zfs_mknode(znode_t *, vattr_t *, dmu_tx_t *, cred_t *,
    uint_t, znode_t **, zfs_acl_ids_t *);
extern void zfs_rmnode(znode_t *);
extern void zfs_dl_name_switch(zfs_dirlock_t *dl, char *new, char **old);
extern boolean_t zfs_dirempty(znode_t *);
extern void zfs_unlinked_add(znode_t *, dmu_tx_t *);
extern void zfs_unlinked_drain(zfsvfs_t *zfsvfs);
extern void zfs_unlinked_drain_stop_wait(zfsvfs_t *zfsvfs);
extern int zfs_sticky_remove_access(znode_t *, znode_t *, cred_t *cr);
extern int zfs_get_xattrdir(znode_t *, znode_t **, cred_t *, int);
extern int zfs_make_xattrdir(znode_t *, vattr_t *, znode_t **, cred_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_DIR_H */
