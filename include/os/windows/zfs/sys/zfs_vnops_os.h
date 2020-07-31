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
 * Copyright (c) 2015, Jorgen Lundman <lundman@lundman.net>
 */

#ifndef	_SYS_FS_ZFS_VNOPS_OS_H
#define	_SYS_FS_ZFS_VNOPS_OS_H

#include <sys/vnode.h>
#include <sys/xvattr.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/fcntl.h>
#include <sys/pathname.h>
#include <sys/zfs_windows.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	KAUTH_WKG_NOT	0	/* not a well-known GUID */
#define	KAUTH_WKG_OWNER	1
#define	KAUTH_WKG_GROUP	2
#define	KAUTH_WKG_NOBODY	3
#define	KAUTH_WKG_EVERYBODY	4

struct emitdir_ptr {
	char *alloc_buf; /* output buffer */
	char *bufptr; /* starts at alloc_buf, increments */
	int bufsize; /* total size of alloc_buf */
	int outcount; /* starts at 0, approaches bufsize */
	ULONG *next_offset; /* ptr to previous nextoffset */
	int last_alignment; /* How much was last alignment */
	uint64_t offset; /* dirindex, 0=".", 1="..", 2=".zfs" */
	int numdirent;
	int dirlisttype; /* Win struct to use */
};

typedef struct emitdir_ptr emitdir_ptr_t;

extern int zfs_remove(znode_t *dzp, char *name, cred_t *cr, int flags);
extern int zfs_mkdir(znode_t *dzp, char *dirname, vattr_t *vap,
	znode_t **zpp, cred_t *cr, int flags, vsecattr_t *vsecp,
	zidmap_t *mnt_ns);
extern int zfs_rmdir(znode_t *dzp, char *name, znode_t *cwd,
	cred_t *cr, int flags);
extern int zfs_setattr(znode_t *zp, vattr_t *vap, int flag, cred_t *cr,
	zidmap_t *mnt_ns);
extern int zfs_rename(znode_t *sdzp, char *snm, znode_t *tdzp,
	char *tnm, cred_t *cr, int flags, uint64_t rflags, vattr_t *wo_vap,
	zidmap_t *mnt_ns);
extern int zfs_symlink(znode_t *dzp, char *name, vattr_t *vap,
	char *link, znode_t **zpp, cred_t *cr, int flags, zidmap_t *mnt_ns);
extern int zfs_link(znode_t *tdzp, znode_t *sp,
	char *name, cred_t *cr, int flags);
extern int zfs_space(znode_t *zp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr);
extern int zfs_create(znode_t *dzp, char *name, vattr_t *vap, int excl,
	int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp,
	zidmap_t *mnt_ns);
extern int zfs_setsecattr(znode_t *zp, vsecattr_t *vsecp, int flag,
	cred_t *cr);
extern int zfs_write_simple(znode_t *zp, const void *data, size_t len,
	loff_t pos, size_t *resid);

extern int zfs_open(struct vnode *ip, int mode, int flag, cred_t *cr);
extern int zfs_close(struct vnode *ip, int flag, cred_t *cr);
extern int zfs_lookup(znode_t *dzp, char *nm, znode_t **zpp,
    int flags, cred_t *cr, int *direntflags, struct componentname *realpnp);
extern int zfs_ioctl(vnode_t *vp, ulong_t com, intptr_t data, int flag,
    cred_t *cred, int *rvalp, caller_context_t *ct);
extern int zfs_readdir(vnode_t *vp, emitdir_ptr_t *, cred_t *cr,
	zfs_dirlist_t *zccb, int flags);
extern int zfs_readdir_emitdir(zfsvfs_t *zfsvfs, const char *name,
    emitdir_ptr_t *ctx, zfs_dirlist_t *zccb, ino64_t objnum);
extern void zfs_readdir_complete(emitdir_ptr_t *ctx);

extern int zfs_fsync(znode_t *zp, int syncflag, cred_t *cr);
extern int zfs_getattr(vnode_t *vp, vattr_t *vap, int flags,
    cred_t *cr, caller_context_t *ct);
extern int zfs_readlink(vnode_t *vp, zfs_uio_t *uio, cred_t *cr);

extern void   zfs_inactive(vnode_t *vp);

/* zfs_vops_windows.c calls */
extern int zfs_znode_getvnode(znode_t *zp, znode_t *dzp, zfsvfs_t *zfsvfs);

extern void   getnewvnode_reserve(int num);
extern void   getnewvnode_drop_reserve(void);
extern int    zfs_vfsops_init(void);
extern int    zfs_vfsops_fini(void);
extern int    zfs_znode_asyncgetvnode(znode_t *zp, zfsvfs_t *zfsvfs);
extern void   zfs_znode_asyncput(znode_t *zp);
extern int    zfs_znode_asyncwait(znode_t *zp);

/* zfs_vnops_windows_lib calls */
extern int    zfs_ioflags(int ap_ioflag);
extern int    zfs_getattr_znode_unlocked(struct vnode *vp, vattr_t *vap);
extern int    ace_trivial_common(void *acep, int aclcnt,
    uintptr_t (*walk)(void *, uintptr_t, int aclcnt,
    uint16_t *, uint16_t *, uint32_t *));

extern int zpl_obtain_xattr(struct znode *, const char *name, mode_t mode,
    cred_t *cr, struct vnode **vpp, int flag);
extern int zpl_xattr_filldir(struct vnode *, zfs_uio_t *uio, const char *,
    int name_len, FILE_FULL_EA_INFORMATION **previous_ea);
extern int zpl_xattr_list(struct vnode *, zfs_uio_t *, ssize_t *, cred_t *);
extern int zpl_xattr_get(struct vnode *ip, const char *name, zfs_uio_t *uio,
    ssize_t *retsize, cred_t *cr);
extern int zpl_xattr_set(struct vnode *, const char *, zfs_uio_t *uio,
    int flags, cred_t *cr);

extern uint32_t getuseraccess(znode_t *zp, vfs_context_t ctx);
extern void zfs_zrele_async(znode_t *zp);

extern int zfsctl_readdir(vnode_t *vp, emitdir_ptr_t *ctx, cred_t *cr,
    zfs_dirlist_t *zccb, int flags);

/*
 * Windows ACL Helper funcions
 */
#define	KAUTH_WKG_NOT	0	/* not a well-known GUID */
#define	KAUTH_WKG_OWNER	1
#define	KAUTH_WKG_GROUP	2
#define	KAUTH_WKG_NOBODY	3
#define	KAUTH_WKG_EVERYBODY	4

extern int kauth_wellknown_guid(guid_t *guid);
extern void aces_from_acl(ace_t *aces, int *nentries, struct kauth_acl *k_acl,
    int *seen_type);
extern void nfsacl_set_wellknown(int wkg, guid_t *guid);
extern int  zfs_addacl_trivial(znode_t *zp, ace_t *aces, int *nentries,
    int seen_type);

extern struct vnodeopv_desc zfs_dvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_fvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_symvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_xdvnodeop_opv_desc;
extern struct vnodeopv_desc zfs_evnodeop_opv_desc;
extern struct vnodeopv_desc zfs_fifonodeop_opv_desc;
extern struct vnodeopv_desc zfs_ctldir_opv_desc;
extern int (**zfs_ctldirops)(void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_VNOPS_H */
