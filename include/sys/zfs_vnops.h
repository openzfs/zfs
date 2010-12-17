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

#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/cred.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int zfs_read(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr,
    caller_context_t *ct);
extern int zfs_write(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr,
    caller_context_t *ct);
extern int zfs_lookup(vnode_t *dvp, char *nm, vnode_t **vpp,
    struct pathname *pnp, int flags, vnode_t *rdir, cred_t *cr,
    caller_context_t *ct, int *direntflags, pathname_t *realpnp);
extern int zfs_create(vnode_t *dvp, char *name, vattr_t *vap,
    int excl, int mode, vnode_t **vpp, cred_t *cr, int flag,
    caller_context_t *ct, vsecattr_t *vsecp);
extern int zfs_remove(vnode_t *dvp, char *name, cred_t *cr,
    caller_context_t *ct, int flags);
extern int zfs_mkdir(vnode_t *dvp, char *dirname, vattr_t *vap,
    vnode_t **vpp, cred_t *cr, caller_context_t *ct, int flags,
    vsecattr_t *vsecp);
extern int zfs_rmdir(vnode_t *dvp, char *name, vnode_t *cwd, cred_t *cr,
    caller_context_t *ct, int flags);
extern int zfs_fsync(vnode_t *vp, int syncflag, cred_t *cr,
    caller_context_t *ct);
extern int zfs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct);
extern int zfs_setattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct);
extern int zfs_rename(vnode_t *sdvp, char *snm, vnode_t *tdvp, char *tnm,
    cred_t *cr, caller_context_t *ct, int flags);
extern int zfs_symlink(vnode_t *dvp, char *name, vattr_t *vap, char *link,
    cred_t *cr, caller_context_t *ct, int flags);
extern int zfs_readlink(vnode_t *vp, uio_t *uio, cred_t *cr,
    caller_context_t *ct);
extern int zfs_link(vnode_t *tdvp, vnode_t *svp, char *name, cred_t *cr,
    caller_context_t *ct, int flags);
extern void zfs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct);
extern int zfs_space(vnode_t *vp, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr, caller_context_t *ct);
extern int zfs_fid(vnode_t *vp, fid_t *fidp, caller_context_t *ct);
extern int zfs_getsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag,
    cred_t *cr, caller_context_t *ct);
extern int zfs_setsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag,
    cred_t *cr, caller_context_t *ct);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_VNOPS_H */
