// SPDX-License-Identifier: CDDL-1.0
/*	$NetBSD: vnode.h,v 1.21 2021/12/17 08:07:12 simonb Exp $	*/

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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

/*
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
 */

#ifndef _OPENSOLARIS_SYS_VNODE_H_
#define	_OPENSOLARIS_SYS_VNODE_H_

#ifdef _KERNEL

struct vattr;
typedef	struct vattr	vattr_t;
typedef enum vtype vtype_t;

#include <sys/namei.h>
enum symfollow { NO_FOLLOW = NOFOLLOW };

/*
 * We'll use a 1-or-2 argument VOP_UNLOCK() in ZFS code.
 * Native NetBSD VOP_UNLOCK is 1-arg; FreeBSD/OpenZFS calls are 2-arg.
 */
#include_next <sys/vnode.h>
#include_next <sys/vnode_impl.h>
#include <sys/vnode_impl.h>

#ifndef FSYNC_CACHE
#define	FSYNC_CACHE	0x0100
#endif

#include <uvm/uvm_extern.h>
#include <uvm/uvm_pager.h>
#ifndef PGO_CLEANIT
#error "PGO_CLEANIT required (uvm/uvm_pager.h) — do not invent pager flags"
#endif
#ifndef PGO_SYNCIO
#error "PGO_SYNCIO required (uvm/uvm_pager.h)"
#endif
#ifndef PGO_FREE
#error "PGO_FREE required (uvm/uvm_pager.h)"
#endif
#ifndef PGO_ALLPAGES
#error "PGO_ALLPAGES required (uvm/uvm_pager.h)"
#endif
#ifndef PGO_LOCKED
#error "PGO_LOCKED required (uvm/uvm_pager.h)"
#endif
#ifndef PGO_RECLAIM
#error "PGO_RECLAIM required (uvm/uvm_pager.h)"
#endif
#ifndef PGO_DONTCARE
#error "PGO_DONTCARE required (uvm/uvm_pager.h)"
#endif

#ifndef _ZFS_NETBSD_VOP_UNLOCK_WRAP
#define	_ZFS_NETBSD_VOP_UNLOCK_WRAP
static inline int
netbsd_vop_unlock(struct vnode *vp)
{
	return (VOP_UNLOCK(vp));
}
#undef VOP_UNLOCK
#define	VOP_UNLOCK(vp, ...)	netbsd_vop_unlock(vp)
#endif

#include <sys/cred.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/buf.h>

#include <sys/vfs_syscalls.h>

typedef int (**vnodeops_t)(void *);

#define	vop_fid		vop_vptofh
#define	vop_fid_args	vop_vptofh_args
#define	a_fid		a_fhp

#define	v_object	v_uobj

struct vop_vptofh_args {
	struct vnode *a_vp;
	struct fid *a_fid;
};

#define	SAVENAME	0

int	vn_is_readonly(vnode_t *);

#define	vn_ismntpt(vp)		\
	((vp)->v_type == VDIR && (vp)->v_mountedhere != NULL)
#define	vn_mountedvfs(vp)	((vp)->v_mountedhere)
#define	vn_has_cached_data(vp)	(((vp)->v_iflag & VI_PAGES) != 0)
#define	vn_matchops(vp, vops)	((vp)->v_op == &(vops))

static inline void
vn_flush_cached_data(vnode_t *vp, boolean_t sync)
{
	int flags;

	if (vp->v_type != VREG)
		return;
	flags = PGO_CLEANIT | PGO_ALLPAGES;
	if (sync)
		flags |= PGO_SYNCIO;
	rw_enter(vp->v_uobj.vmobjlock, RW_WRITER);
	(void) VOP_PUTPAGES(vp, 0, 0, flags);
}

/*
 * Grow: store v_size / v_writesize only.  uvm_vnp_setsize on a grow
 * walks UVM and has panicked in amap_wipeout on residual file-vdev
 * mappings after multi-pool zpool create.  Shrink of a VREG: toss
 * pages past the new EOF.  uvm_vnp_setsize shrinks with
 * PGO_FREE|PGO_SYNCIO, not PGO_CLEANIT, so zfs_netbsd_putpages does
 * not re-enter the rangelock.
 */
static inline void
vnode_pager_setsize(vnode_t *vp, uint64_t sz)
{
	voff_t nsize = (voff_t)sz;

	if (vp->v_type == VREG && nsize < vp->v_size) {
		uvm_vnp_setsize(vp, nsize);
		return;
	}
	vp->v_size = nsize;
	vp->v_writesize = nsize;
}

#define	VN_HOLD(v)	vref(v)
#define	VN_RELE(v)						      \
do {								      \
	if (vrefcnt(v) == 0) {					      \
		printf("VN_RELE(%s,%d): %p unused\n", __FILE__, __LINE__, v); \
		vprint("VN_RELE", (v));				      \
		panic("VN_RELE");				      \
	} else {						      \
		vrele(v);					      \
	}							      \
} while (/*CONSTCOND*/0)
#define	VN_URELE(v)	vput(v)
#undef VN_RELE_ASYNC
/*
 * Teardown waits on dsl_pool_zrele_taskq().  Native vrele_async()
 * queues on a different workqueue, so that wait never observes the
 * release and zpool destroy -f spins.  Dispatch vrele onto the
 * taskq the caller passed -- same contract as FreeBSD vn_rele_async().
 */
struct taskq;
void	vn_rele_async(vnode_t *, struct taskq *);
#define	VN_RELE_ASYNC(vp, taskq)	vn_rele_async((vp), (taskq))
#define	VN_RELE_CLEANER(vp, taskq)	/* nothing */

#define	specvp(vp, rdev, type, cr)	(VN_HOLD(vp), (vp))

/*
 * Solaris vattr overlays on NetBSD struct vattr (sys/vnode.h):
 *
 *   va_mask   -> va_spare     (long; native VFS never reads it)
 *   va_nodeid -> va_fileid    (ino_t, 64-bit on NetBSD 11)
 *   va_nblocks-> va_bytes     (u_quad_t; ZIL replay side-channel)
 *   va_blksize-> va_blocksize
 *   va_seq    -> va_gen
 *
 * Replay (module/zfs/zfs_replay.c, same on Linux/FreeBSD) stuffs
 * object/gen/dnodesize into va_nodeid / va_nblocks / va_fsid.
 * That is the upstream contract, not a NetBSD shortcut.  Do not
 * invent a parallel replay struct unless common replay.c changes.
 *
 * vattr_init_mask() is the native→ZFS conversion at the VOP
 * boundary.  Internal ZFS paths set va_mask explicitly.
 */
#define	va_mask		va_spare
#define	va_nodeid	va_fileid
#define	va_nblocks	va_bytes
#define	va_blksize	va_blocksize
#define	va_seq		va_gen

#ifndef CTASSERT
#define	CTASSERT(x)	_Static_assert(x, #x)
#endif
CTASSERT(sizeof (((struct vattr *)0)->va_spare) >= sizeof (uint32_t));
CTASSERT(sizeof (ino_t) >= sizeof (uint64_t));
CTASSERT(sizeof (u_quad_t) >= sizeof (uint64_t));

#define	MAXOFFSET_T	((off_t)(((uint64_t)1 << 63) - 1))

#define	EXCL		0

#ifndef VA_UTIMES_NULL
#define	VA_UTIMES_NULL	0x01
#endif

#ifndef AT_TYPE
#define	AT_TYPE		0x0001
#define	AT_MODE		0x0002
#define	AT_UID		0x0004
#define	AT_GID		0x0008
#define	AT_FSID		0x0010
#define	AT_NODEID	0x0020
#define	AT_NLINK	0x0040
#define	AT_SIZE		0x0080
#define	AT_ATIME	0x0100
#define	AT_MTIME	0x0200
#define	AT_CTIME	0x0400
#define	AT_RDEV		0x0800
#define	AT_BLKSIZE	0x1000
#define	AT_NBLOCKS	0x2000
#define	AT_SEQ		0x4000
#define	AT_XVATTR	0x8000
#define	AT_ALL		(AT_TYPE | AT_MODE | AT_UID | AT_GID | AT_FSID | \
	AT_NODEID | AT_NLINK | AT_SIZE | AT_ATIME | AT_MTIME | AT_CTIME | \
	AT_RDEV | AT_BLKSIZE | AT_NBLOCKS | AT_SEQ)
#endif

static inline void
vattr_init_mask(vattr_t *vap)
{

	vap->va_mask = 0;

	if (vap->va_type != VNON)
		vap->va_mask |= AT_TYPE;
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
	if (vap->va_mode != (mode_t)VNOVAL)
		vap->va_mask |= AT_MODE;
	/*
	 * A stack struct vattr is not an xvattr_t.  Setting
	 * AT_XVATTR here made zfs_setattr cast the caller's
	 * vattr and walk off the end.  chflags is handled in
	 * zfs_netbsd_setattr by building a real xvattr_t.
	 */
}

#define	FCREAT	O_CREAT
#define	FTRUNC	O_TRUNC
#define	FSYNC	FFSYNC

static inline int
zfs_vn_open(const char *pnamep, enum uio_seg seg, int filemode, int createmode,
    vnode_t **vpp, enum create crwhy, mode_t umask)
{
	struct pathbuf *pb;
	int error;

	ASSERT(seg == UIO_SYSSPACE);
	ASSERT((filemode & (FWRITE | FCREAT | FTRUNC | FOFFMAX)) != 0);
	ASSERT(crwhy == CRCREAT);
	ASSERT(umask == 0);

	pb = pathbuf_create(pnamep);
	error = vn_open(NULL, pb, 0, filemode, createmode, vpp, NULL, NULL);
	if (error == 0) {
		VOP_UNLOCK(*vpp, 0);
	}
	pathbuf_destroy(pb);
	return (error);
}
#define	vn_open(a, b, c, d, e, f, g, ...)	\
	zfs_vn_open((a), (b), (c), (d), (e), (f), (g))

/* CSTYLED */
#define	vn_openat(pnamep, seg, filemode, createmode, vpp, crwhy, umask, \
    rootvn, unk) \
	zfs_vn_open((pnamep), (seg), (filemode), (createmode), (vpp), \
	    (crwhy), (umask))

#ifdef RLIM64_INFINITY
#undef RLIM64_INFINITY
#endif
#define	RLIM64_INFINITY	0
static inline int
zfs_vn_rdwr(enum uio_rw rw, vnode_t *vp, caddr_t base, ssize_t len,
    offset_t offset, enum uio_seg seg, int ioflag, uint64_t ulimit, cred_t *cr,
    ssize_t *residp)
{
	int error;
	size_t resid;

	ASSERT(rw == UIO_READ || rw == UIO_WRITE);
	ASSERT(ulimit == RLIM64_INFINITY);
	/*
	 * osnet vnode.h: caller passes ioflag 0; the wrapper
	 * forces IO_UNIT before native vn_rdwr.  vdev_file.c
	 * relies on IO_UNIT; passing 0 through is not equivalent.
	 */
	ioflag = IO_UNIT;

	/*
	 * vn_rdwr only consults the lwp for UIO_USERSPACE
	 * (uio_vmspace = l->l_proc->p_vmspace).  UIO_SYSSPACE
	 * sets uio_vmspace to vmspace_kernel(); the lwp
	 * argument is unused.  Pass curlwp.
	 */
	error = vn_rdwr(rw, vp, base, len, offset, seg, ioflag, cr,
	    &resid, curlwp);
	if (residp != NULL)
		*residp = (ssize_t)resid;
	return (error);
}
/* CSTYLED */
#define	vn_rdwr(rw, vp, base, len, offset, seg, ioflag, ulimit, cr, \
    residp, ...) \
	zfs_vn_rdwr((rw), (vp), (base), (len), (offset), (seg), (ioflag), \
	    (ulimit), (cr), (residp))

static inline int
zfs_vop_fsync(vnode_t *vp, int flag, cred_t *cr)
{
	int error;
	int fsflags;

	(void) flag;
	/*
	 * File vdev only.  FSYNC_CACHE is DIOCCACHESYNC on the FFS
	 * backing disk -- not asked for.  FSYNC_WAIT alone also runs
	 * wapbl_flush of the whole mount.  DATAONLY: PUTPAGES this
	 * file.  Disks do not come through here: zfs_file_fsync
	 * issues DIOCCACHESYNC itself (spec_fsync is a no-op on
	 * VCHR and does not cache-flush VBLK).
	 */
	fsflags = FSYNC_WAIT | FSYNC_DATAONLY;
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0)
		return (error);
	error = VOP_FSYNC(vp, cr, fsflags, 0, 0);
	VOP_UNLOCK(vp);
	return (error);
}
/*
 * Load-bearing: these #defines must come AFTER the inlines that
 * call the native VOPs.  Defining them first would recurse.
 */
#define	VOP_FSYNC(vp, a, b, ...)	zfs_vop_fsync((vp), (a), (b))

static inline int
zfs_vop_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr)
{

	ASSERT(flag == (FWRITE | FCREAT | FTRUNC | FOFFMAX));
	ASSERT(count == 1);
	ASSERT(offset == 0);

	return (vn_close(vp, flag, cr));
}
#define	VOP_CLOSE(vp, oflags, count, offset, cr, unk) \
	zfs_vop_close((vp), (oflags), (count), (offset), (cr))

static inline int
zfs_vop_getattr(vnode_t *vp, vattr_t *ap, int flag, cred_t *cr)
{
	int error;

	(void) flag;
	/*
	 * Callers of this wrapper do not hold the vnode.
	 * GETATTR is a read; shared is enough.
	 */
	error = vn_lock(vp, LK_SHARED | LK_RETRY);
	if (error != 0)
		return (error);
	error = VOP_GETATTR(vp, ap, cr != NULL ? cr : CRED());
	VOP_UNLOCK(vp);
	if (error == 0)
		ap->va_spare = 0;
	return (error);
}
/* Same load-bearing order as VOP_FSYNC above. */
#define	VOP_GETATTR(vp, ap, cr, ...)	zfs_vop_getattr((vp), (ap), 0, (cr))

static inline int
zfs_vop_seek(vnode_t *vp, off_t off, off_t *offp)
{
	int error;

	error = vn_lock(vp, LK_SHARED | LK_RETRY);
	if (error != 0)
		return (error);
	error = VOP_SEEK(vp, off, *offp, CRED());
	VOP_UNLOCK(vp);
	return (error);
}
#define	VOP_SEEK(vp, off, offp, unk)	zfs_vop_seek(vp, off, offp)

#define	B_INVAL		BC_INVAL

static inline int
zfs_vop_putpage(vnode_t *vp, off_t off, size_t len, int flag)
{
	int nbflag;

	nbflag = 0;
	if (len == 0) {
		nbflag |= PGO_ALLPAGES;
	}
	if ((flag & B_ASYNC) == 0) {
		nbflag |= PGO_SYNCIO;
	}
	if ((flag & B_INVAL) != 0) {
		nbflag |= PGO_FREE;
	} else {
		nbflag |= PGO_CLEANIT;
	}

	rw_enter(vp->v_uobj.vmobjlock, RW_WRITER);
	return (VOP_PUTPAGES(vp, off, len, nbflag));
}
/* CSTYLED */
#define	VOP_PUTPAGE(vp, off, len, flag, cr, ct)	\
	zfs_vop_putpage((vp), (off), (len), (flag))

static inline int
vn_rename(char *from, char *to, enum uio_seg seg)
{

	ASSERT(seg == UIO_SYSSPACE);

	return (do_sys_rename(from, to, seg, 0));
}

static inline int
vn_remove(char *fnamep, enum uio_seg seg, enum rm dirflag)
{

	ASSERT(seg == UIO_SYSSPACE);
	ASSERT(dirflag == RMFILE);

	return (do_sys_unlink(fnamep, seg));
}

/*
 * VOP_ACCESS flags
 */
#define	V_APPEND	0x2	/* want to do append only check */

#define	VV_NOSYNC	0

#define	VI_LOCK(vp)	mutex_enter((vp)->v_interlock)
#define	VI_UNLOCK(vp)	mutex_exit((vp)->v_interlock)

#define	VATTR_NULL(x)	vattr_null(x)

#define	cache_purge_negative(vp)	cache_purge(vp)

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_VNODE_H_ */
