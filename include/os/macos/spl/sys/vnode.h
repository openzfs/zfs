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
 *
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_VNODE_H
#define	_SPL_VNODE_H

#include <sys/fcntl.h>

#include <sys/mount.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/ubc.h>

#include <kern/locks.h>


// Be aware that Apple defines "typedef struct vnode *vnode_t" and
// ZFS uses "typedef struct vnode vnode_t".
// uio and vnode wrappers can be removed now.
// uio_t -> zfs_uio_t
// vnode_t -> struct vnode (as it is only used in os/macos/
// proc_t is to work around vn_rdwr( ..., proc_t p)
#undef uio_t
#undef vnode_t
#undef proc_t
#define	proc_t struct proc *
#include_next <sys/vnode.h>
#undef proc_t
#define	proc_t struct proc
#define	vnode_t struct vnode
#define	uio_t struct uio

#define	LOOKUP_XATTR 0x02 /* lookup up extended attr dir */

struct caller_context;
typedef struct caller_context caller_context_t;
typedef int vcexcl_t;

enum vcexcl { NONEXCL, EXCL };

#define	B_INVAL		0x01
#define	B_TRUNC		0x02

#define	CREATE_XATTR_DIR	0x04	/* Create extended attr dir */
#define	ATTR_NOACLCHECK		0x20

#define	IS_DEVVP(vp)    \
	(vnode_ischr(vp) || vnode_isblk(vp) || vnode_isfifo(vp))

enum rm		{ RMFILE, RMDIRECTORY };	/* rm or rmdir (remove) */
enum create	{ CRCREAT, CRMKNOD, CRMKDIR };	/* reason for create */

#define	va_mask			va_active
#define	va_nodeid		va_fileid
#define	va_nblocks		va_filerev

/*
 * vnode attr translations
 */
#define	ATTR_TYPE		VNODE_ATTR_va_type
#define	ATTR_MODE		VNODE_ATTR_va_mode
#define	ATTR_ACL		VNODE_ATTR_va_acl
#define	ATTR_UID		VNODE_ATTR_va_uid
#define	ATTR_GID		VNODE_ATTR_va_gid
#define	ATTR_ATIME		VNODE_ATTR_va_access_time
#define	ATTR_MTIME		VNODE_ATTR_va_modify_time
#define	ATTR_CTIME		VNODE_ATTR_va_change_time
#define	ATTR_CRTIME		VNODE_ATTR_va_create_time
#define	ATTR_SIZE		VNODE_ATTR_va_data_size
#define	ATTR_NOSET		0
/*
 * OSX uses separate vnop getxattr and setxattr to deal with XATTRs, so
 * we never get vop&XVATTR set from VFS. All internal checks for it in
 * ZFS is not required.
 */
#define	ATTR_XVATTR	0
#define	AT_XVATTR ATTR_XVATTR

#define	va_size			va_data_size
#define	va_atime		va_access_time
#define	va_mtime		va_modify_time
#define	va_ctime		va_change_time
#define	va_crtime		va_create_time
#define	va_bytes		va_data_size

typedef struct vnode_attr vattr;
typedef struct vnode_attr vattr_t;

/* vsa_mask values */
#define	VSA_ACL			0x0001
#define	VSA_ACLCNT		0x0002
#define	VSA_DFACL		0x0004
#define	VSA_DFACLCNT		0x0008
#define	VSA_ACE			0x0010
#define	VSA_ACECNT		0x0020
#define	VSA_ACE_ALLTYPES	0x0040
#define	VSA_ACE_ACLFLAGS	0x0080  /* get/set ACE ACL flags */


extern struct vnode *vn_alloc(int flag);

extern int vn_open(char *pnamep, enum uio_seg seg, int filemode,
    int createmode, struct vnode **vpp, enum create crwhy, mode_t umask);
extern int vn_openat(char *pnamep, enum uio_seg seg, int filemode,
    int createmode, struct vnode **vpp, enum create crwhy,
    mode_t umask, struct vnode *startvp);

#define	vn_renamepath(tdvp, svp, tnm, lentnm)	do { } while (0)
#define	vn_free(vp)				do { } while (0)
#define	vn_pages_remove(vp, fl, op)		do { } while (0)

/* XNU is a vn_rdwr, so we work around it to match arguments */
/* This should be deprecated, if not now, soon. */
extern int  zfs_vn_rdwr(enum uio_rw rw, struct vnode *vp, caddr_t base,
    ssize_t len, offset_t offset, enum uio_seg seg, int ioflag,
    rlim64_t ulimit, cred_t *cr, ssize_t *residp);

#define	vn_rdwr(rw, vp, b, l, o, s, flg, li, cr, resid)     \
    zfs_vn_rdwr((rw), (vp), (b), (l), (o), (s), (flg), (li), (cr), (resid))

/* Other vn_rdwr for zfs_file_t ops */
struct spl_fileproc;
extern int spl_vn_rdwr(enum uio_rw rw, struct spl_fileproc *, caddr_t base,
    ssize_t len, offset_t offset, enum uio_seg seg, int ioflag,
	rlim64_t ulimit, cred_t *cr, ssize_t *residp);

extern int vn_remove(char *fnamep, enum uio_seg seg, enum rm dirflag);
extern int vn_rename(char *from, char *to, enum uio_seg seg);

#define	LK_RETRY  0
#define	LK_SHARED 0
#define	VN_UNLOCK(vp)
static inline int
vn_lock(struct vnode *vp, int fl)
{
	return (0);
}

#define	VN_HOLD(vp)		vnode_getwithref(vp)
#define	VN_RELE(vp)		vnode_put(vp)

void spl_rele_async(void *arg);
void vn_rele_async(struct vnode *vp, void *taskq);

extern int vnode_iocount(struct vnode *);

#define	F_SEEK_HOLE SEEK_HOLE

#define	VN_RELE_ASYNC(vp, tq) vn_rele_async((vp), (tq))

#define	vn_exists(vp)
#define	vn_is_readonly(vp)  vnode_vfsisrdonly(vp)

#define	vnode_pager_setsize(vp, sz)  ubc_setsize((vp), (sz))

#define	VATTR_NULL(v) do { } while (0)

extern int VOP_CLOSE(struct vnode *vp, int flag, int count,
    offset_t off, void *cr, void *);
extern int VOP_FSYNC(struct vnode *vp, int flags, void* unused, void *);
extern int VOP_SPACE(struct vnode *vp, int cmd, struct flock *fl,
    int flags, offset_t off, cred_t *cr, void *ctx);

extern int VOP_GETATTR(struct vnode *vp, vattr_t *vap, int flags,
    void *x3, void *x4);

#define	VOP_UNLOCK(vp, fl)	do { } while (0)

void vfs_mountedfrom(struct mount *vfsp, char *osname);

#define	build_path(A, B, C, D, E, F) spl_build_path(A, B, C, D, E, F)
extern int spl_build_path(struct vnode *vp, char *buff, int buflen,
    int *outlen, int flags, vfs_context_t ctx);

extern struct vnode *rootdir;

static inline int chklock(struct vnode *vp, int iomode,
	unsigned long long offset, ssize_t len, int fmode, void *ct)
{
	return (0);
}

#define	vn_ismntpt(vp)   (vnode_mountedhere(vp) != NULL)

extern errno_t VOP_LOOKUP   (struct vnode *, struct vnode **,
    struct componentname *, vfs_context_t);
extern errno_t VOP_MKDIR    (struct vnode *, struct vnode **,
    struct componentname *, struct vnode_attr *,
    vfs_context_t);
extern errno_t VOP_REMOVE   (struct vnode *, struct vnode *,
    struct componentname *, int, vfs_context_t);
extern errno_t VOP_SYMLINK  (struct vnode *, struct vnode **,
    struct componentname *, struct vnode_attr *,
    char *, vfs_context_t);

void spl_vnode_fini(void);
int  spl_vnode_init(void);

extern void spl_cache_purgevfs(struct mount *mp);
#define	cache_purgevfs spl_cache_purgevfs

vfs_context_t vfs_context_kernel(void);
vfs_context_t spl_vfs_context_kernel(void);
extern int spl_vnode_notify(struct vnode *vp, uint32_t type,
    struct vnode_attr *vap);
extern int spl_vfs_get_notify_attributes(struct vnode_attr *vap);
extern void spl_hijack_mountroot(void *func);
extern void spl_setrootvnode(struct vnode *vp);

struct vnode *getrootdir(void);
void spl_vfs_start(void);

#endif /* SPL_VNODE_H */
