/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_VNODE_H_
#define	_OPENSOLARIS_SYS_VNODE_H_

struct vnode;
struct vattr;
struct xucred;

typedef struct flock	flock64_t;
typedef	struct vnode	vnode_t;
typedef	struct vattr	vattr_t;
typedef enum vtype vtype_t;

#include <sys/types.h>
#include <sys/queue.h>
#include_next <sys/sdt.h>
#include <sys/namei.h>
enum symfollow { NO_FOLLOW = NOFOLLOW };

#define	NOCRED	((struct ucred *)0)	/* no credential available */
#define	F_FREESP	11 	/* Free file space */

#include <sys/proc.h>
#include <sys/vnode_impl.h>
#ifndef IN_BASE
#include_next <sys/vnode.h>
#endif
#include <sys/mount.h>
#include <sys/cred.h>
#include <sys/fcntl.h>
#include <sys/refcount.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/syscallsubr.h>

typedef	struct vop_vector	vnodeops_t;
#define	VOP_FID		VOP_VPTOFH
#define	vop_fid		vop_vptofh
#define	vop_fid_args	vop_vptofh_args
#define	a_fid		a_fhp

#define	IS_XATTRDIR(dvp)	(0)

#define	v_count	v_usecount

#define	rootvfs		(rootvnode == NULL ? NULL : rootvnode->v_mount)


#ifndef IN_BASE
static __inline int
vn_is_readonly(vnode_t *vp)
{
	return (vp->v_mount->mnt_flag & MNT_RDONLY);
}
#endif
#define	vn_vfswlock(vp)		(0)
#define	vn_vfsunlock(vp)	do { } while (0)
#define	vn_ismntpt(vp)	   \
	((vp)->v_type == VDIR && (vp)->v_mountedhere != NULL)
#define	vn_mountedvfs(vp)	((vp)->v_mountedhere)
#define	vn_has_cached_data(vp)	\
	((vp)->v_object != NULL && \
	(vp)->v_object->resident_page_count > 0)
#define	vn_exists(vp)		do { } while (0)
#define	vn_invalid(vp)		do { } while (0)
#define	vn_renamepath(tdvp, svp, tnm, lentnm)	do { } while (0)
#define	vn_free(vp)		do { } while (0)
#define	vn_matchops(vp, vops)	((vp)->v_op == &(vops))

#define	VN_HOLD(v)	vref(v)
#define	VN_RELE(v)	vrele(v)
#define	VN_URELE(v)	vput(v)

#define	vnevent_create(vp, ct)			do { } while (0)
#define	vnevent_link(vp, ct)			do { } while (0)
#define	vnevent_remove(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rmdir(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rename_src(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rename_dest(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rename_dest_dir(vp, ct)		do { } while (0)

#define	specvp(vp, rdev, type, cr)	(VN_HOLD(vp), (vp))
#define	MANDLOCK(vp, mode)	(0)

/*
 * We will use va_spare is place of Solaris' va_mask.
 * This field is initialized in zfs_setattr().
 */
#define	va_mask		va_spare
/* TODO: va_fileid is shorter than va_nodeid !!! */
#define	va_nodeid	va_fileid
/* TODO: This field needs conversion! */
#define	va_nblocks	va_bytes
#define	va_blksize	va_blocksize
#define	va_seq		va_gen

#define	MAXOFFSET_T	OFF_MAX
#define	EXCL		0

#define	FCREAT		O_CREAT
#define	FTRUNC		O_TRUNC
#define	FEXCL		O_EXCL
#ifndef FDSYNC
#define	FDSYNC		FFSYNC
#endif
#define	FRSYNC		FFSYNC
#define	FSYNC		FFSYNC
#define	FOFFMAX		0x00
#define	FIGNORECASE	0x00

/*
 * Attributes of interest to the caller of setattr or getattr.
 */
#define	AT_MODE		0x00002
#define	AT_UID		0x00004
#define	AT_GID		0x00008
#define	AT_FSID		0x00010
#define	AT_NODEID	0x00020
#define	AT_NLINK	0x00040
#define	AT_SIZE		0x00080
#define	AT_ATIME	0x00100
#define	AT_MTIME	0x00200
#define	AT_CTIME	0x00400
#define	AT_RDEV		0x00800
#define	AT_BLKSIZE	0x01000
#define	AT_NBLOCKS	0x02000
/*			0x04000 */	/* unused */
#define	AT_SEQ		0x08000
/*
 * If AT_XVATTR is set then there are additional bits to process in
 * the xvattr_t's attribute bitmap.  If this is not set then the bitmap
 * MUST be ignored.  Note that this bit must be set/cleared explicitly.
 * That is, setting AT_ALL will NOT set AT_XVATTR.
 */
#define	AT_XVATTR	0x10000

#define	AT_ALL		(AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|\
			AT_NLINK|AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|\
			AT_RDEV|AT_BLKSIZE|AT_NBLOCKS|AT_SEQ)

#define	AT_STAT		(AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|AT_NLINK|\
			AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|AT_RDEV)

#define	AT_TIMES	(AT_ATIME|AT_MTIME|AT_CTIME)

#define	AT_NOSET	(AT_NLINK|AT_RDEV|AT_FSID|AT_NODEID|\
			AT_BLKSIZE|AT_NBLOCKS|AT_SEQ)

#ifndef IN_BASE
static __inline void
vattr_init_mask(vattr_t *vap)
{

	vap->va_mask = 0;

	if (vap->va_uid != (uid_t)VNOVAL)
		vap->va_mask |= AT_UID;
	if (vap->va_gid != (gid_t)VNOVAL)
		vap->va_mask |= AT_GID;
	if (vap->va_size != (u_quad_t)VNOVAL)
		vap->va_mask |= AT_SIZE;
	if (vap->va_atime.tv_sec != VNOVAL)
		vap->va_mask |= AT_ATIME;
	if (vap->va_mtime.tv_sec != VNOVAL)
		vap->va_mask |= AT_MTIME;
	if (vap->va_mode != (uint16_t)VNOVAL)
		vap->va_mask |= AT_MODE;
	if (vap->va_flags != VNOVAL)
		vap->va_mask |= AT_XVATTR;
}
#endif

#define		RLIM64_INFINITY 0

static __inline int
vn_rename(char *from, char *to, enum uio_seg seg)
{

	ASSERT(seg == UIO_SYSSPACE);

	return (kern_renameat(curthread, AT_FDCWD, from, AT_FDCWD, to, seg));
}

#include <sys/vfs.h>

#endif	/* _OPENSOLARIS_SYS_VNODE_H_ */
