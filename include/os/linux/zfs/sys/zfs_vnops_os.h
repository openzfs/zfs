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

#ifndef	_SYS_FS_ZFS_VNOPS_OS_H
#define	_SYS_FS_ZFS_VNOPS_OS_H

#include <sys/vnode.h>
#include <sys/xvattr.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/fcntl.h>
#include <sys/pathname.h>
#include <sys/zpl.h>
#include <sys/zfs_file.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int zfs_open(struct inode *ip, int mode, int flag, cred_t *cr);
extern int zfs_close(struct inode *ip, int flag, cred_t *cr);
extern int zfs_write_simple(znode_t *zp, const void *data, size_t len,
    loff_t pos, size_t *resid);
extern int zfs_lookup(znode_t *dzp, char *nm, znode_t **zpp, int flags,
    cred_t *cr, int *direntflags, pathname_t *realpnp);
extern int zfs_create(znode_t *dzp, char *name, vattr_t *vap, int excl,
    int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp);
extern int zfs_tmpfile(struct inode *dip, vattr_t *vapzfs, int excl,
    int mode, struct inode **ipp, cred_t *cr, int flag, vsecattr_t *vsecp);
extern int zfs_remove(znode_t *dzp, char *name, cred_t *cr, int flags);
extern int zfs_mkdir(znode_t *dzp, char *dirname, vattr_t *vap,
    znode_t **zpp, cred_t *cr, int flags, vsecattr_t *vsecp);
extern int zfs_rmdir(znode_t *dzp, char *name, znode_t *cwd,
    cred_t *cr, int flags);
extern int zfs_readdir(struct inode *ip, zpl_dir_context_t *ctx, cred_t *cr);
#ifdef HAVE_GENERIC_FILLATTR_IDMAP_REQMASK
extern int zfs_getattr_fast(zidmap_t *, u32 request_mask, struct inode *ip,
    struct kstat *sp);
#else
extern int zfs_getattr_fast(zidmap_t *, struct inode *ip, struct kstat *sp);
#endif
extern int zfs_setattr(znode_t *zp, vattr_t *vap, int flag, cred_t *cr);
extern int zfs_rename(znode_t *sdzp, char *snm, znode_t *tdzp,
    char *tnm, cred_t *cr, int flags);
extern int zfs_symlink(znode_t *dzp, char *name, vattr_t *vap,
    char *link, znode_t **zpp, cred_t *cr, int flags);
extern int zfs_readlink(struct inode *ip, zfs_uio_t *uio, cred_t *cr);
extern int zfs_link(znode_t *tdzp, znode_t *szp,
    char *name, cred_t *cr, int flags);
extern void zfs_inactive(struct inode *ip);
extern int zfs_space(znode_t *zp, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr);
extern int zfs_fid(struct inode *ip, fid_t *fidp);
extern int zfs_getpage(struct inode *ip, struct page *pp);
extern int zfs_putpage(struct inode *ip, struct page *pp,
    struct writeback_control *wbc, boolean_t for_sync);
extern int zfs_dirty_inode(struct inode *ip, int flags);
extern int zfs_map(struct inode *ip, offset_t off, caddr_t *addrp,
    size_t len, unsigned long vm_flags);
extern void zfs_zrele_async(znode_t *zp);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_VNOPS_H */
