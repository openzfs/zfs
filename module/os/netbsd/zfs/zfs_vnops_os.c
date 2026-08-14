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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2010 Robert Milkowski */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/kauth.h>
#include <sys/signal.h>
#include <sys/vfs.h>
#include <sys/endian.h>
#include <sys/vm.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/malloc.h>
#include <sys/taskq.h>
#include <sys/uio.h>
#include <sys/atomic.h>
#include <sys/namei.h>
#include <sys/mman.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/policy.h>
#include <sys/sunddi.h>
#include <sys/filio.h>
#include <sys/sid.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/zfs_quota.h>
#include <sys/zfs_sa.h>
#include <sys/nvpair.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_project.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/sched.h>
#include <sys/acl.h>
#include <sys/vmmeter.h>
#include <vm/vm_param.h>
#include <sys/zil.h>
#include <sys/zfs_vnops.h>
#include <sys/module.h>
#include <uvm/uvm.h>
#include <uvm/uvm_pager.h>
#include <dev/mm.h>
#include <sys/dmu_impl.h>
#include <sys/zfs_debug.h>
#include <sys/brt.h>
#include <sys/zfeature.h>

#include <vm/vm_object.h>

#include <sys/extattr.h>
#include <sys/priv.h>

#include <sys/mount.h>
#include <sys/fstrans.h>
#include <sys/kauth.h>
#include <sys/lockf.h>
#include <sys/xvattr.h>
#include <sys/zfs_context.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <miscfs/genfs/genfs.h>
#include <miscfs/genfs/genfs_node.h>
#include <miscfs/specfs/specdev.h>
#include <miscfs/fifofs/fifo.h>

#ifndef VN_OPEN_INVFS
#define	VN_OPEN_INVFS	0x0
#endif

#if defined(__NetBSD__)
typedef uint64_t cookie_t;
#endif

/*
 * Programming rules.
 *
 * Each vnode op performs some logical unit of work.  To do this, the ZPL must
 * properly lock its in-core state, create a DMU transaction, do the work,
 * record this work in the intent log (ZIL), commit the DMU transaction,
 * and wait for the intent log to commit if it is a synchronous operation.
 * Moreover, the vnode ops must work in both normal and log replay context.
 * The ordering of events is important to avoid deadlocks and references
 * to freed memory.  The example below illustrates the following Big Rules:
 *
 *  (1)	A check must be made in each zfs thread for a mounted file system.
 *	This is done avoiding races using zfs_enter(zfsvfs).
 *	A zfs_exit(zfsvfs) is needed before all returns.  Any znodes
 *	must be checked with zfs_verify_zp(zp).  Both of these macros
 *	can return EIO from the calling function.
 *
 *  (2)	VN_RELE() should always be the last thing except for zil_commit()
 *	(if necessary) and zfs_exit(). This is for 3 reasons:
 *	First, if it's the last reference, the vnode/znode
 *	can be freed, so the zp may point to freed memory.  Second, the last
 *	reference will call zfs_zinactive(), which may induce a lot of work --
 *	pushing cached pages (which acquires range locks) and syncing out
 *	cached atime changes.  Third, zfs_zinactive() may require a new tx,
 *	which could deadlock the system if you were already holding one.
 *	If you must call VN_RELE() within a tx then use VN_RELE_ASYNC().
 *
 *  (3)	All range locks must be grabbed before calling dmu_tx_assign(),
 *	as they can span dmu_tx_assign() calls.
 *
 *  (4) If ZPL locks are held, pass DMU_TX_NOWAIT as the second argument to
 *      dmu_tx_assign().  This is critical because we don't want to block
 *      while holding locks.
 *
 *	If no ZPL locks are held (aside from zfs_enter()), use DMU_TX_WAIT.
 *	This reduces lock contention and CPU usage when we must wait (note
 *	that if throughput is constrained by the storage, nearly every
 *	transaction must wait).
 *
 *      Note, in particular, that if a lock is sometimes acquired before
 *      the tx assigns, and sometimes after (e.g. z_lock), then failing
 *      to use a non-blocking assign can deadlock the system.  The scenario:
 *
 *	Thread A has grabbed a lock before calling dmu_tx_assign().
 *	Thread B is in an already-assigned tx, and blocks for this lock.
 *	Thread A calls dmu_tx_assign(DMU_TX_WAIT) and blocks in
 *	txg_wait_open() forever, because the previous txg can't quiesce
 *	until B's tx commits.
 *
 *	If dmu_tx_assign() returns ERESTART and zfsvfs->z_assign is
 *	DMU_TX_NOWAIT, then drop all locks, call dmu_tx_wait(), and try
 *	again.  On subsequent calls to dmu_tx_assign(), pass
 *	DMU_TX_NOTHROTTLE in addition to DMU_TX_NOWAIT, to indicate that
 *	this operation has already called dmu_tx_wait().  This will ensure
 *	that we don't retry forever, waiting a short bit each time.
 *
 *  (5)	If the operation succeeded, generate the intent log entry for it
 *	before dropping locks.  This ensures that the ordering of events
 *	in the intent log matches the order in which they actually occurred.
 *	During ZIL replay the zfs_log_* functions will update the sequence
 *	number to indicate the zil transaction has replayed.
 *
 *  (6)	At the end of each vnode op, the DMU tx must always commit,
 *	regardless of whether there were any errors.
 *
 *  (7)	After dropping all locks, invoke zil_commit(zilog, foid)
 *	to ensure that synchronous semantics are provided when necessary.
 *
 * In general, this is how things should be ordered in each vnode op:
 *
 *	zfs_enter(zfsvfs);		// exit if unmounted
 * top:
 *	zfs_dirent_lookup(&dl, ...)	// lock directory entry (may VN_HOLD())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx,
 *	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
 *	if (error) {
 *		rw_exit(...);		// drop locks
 *		zfs_dirent_unlock(dl);	// unlock directory entry
 *		VN_RELE(...);		// release held vnodes
 *		if (error == ERESTART) {
 *			waited = B_TRUE;
 *			dmu_tx_wait(tx);
 *			dmu_tx_abort(tx);
 *			goto top;
 *		}
 *		dmu_tx_abort(tx);	// abort DMU tx
 *		zfs_exit(zfsvfs);	// finished in zfs
 *		return (error);		// really out of space
 *	}
 *	error = do_real_work();		// do whatever this VOP does
 *	if (error == 0)
 *		zfs_log_*(...);		// on success, make ZIL entry
 *	dmu_tx_commit(tx);		// commit DMU tx -- error or not
 *	rw_exit(...);			// drop locks
 *	zfs_dirent_unlock(dl);		// unlock directory entry
 *	VN_RELE(...);			// release held vnodes
 *	zil_commit(zilog, foid);	// synchronous when necessary
 *	zfs_exit(zfsvfs);		// finished in zfs
 *	return (error);			// done, report error
 */
int
zfs_open(vnode_t **vpp, int flag, cred_t *cr)
{
	(void) cr;
	znode_t	*zp = VTOZ(*vpp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if ((flag & FWRITE) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((flag & FAPPEND) == 0)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	/*
	 * Keep a count of the synchronous opens in the znode.  On first
	 * synchronous open we must convert all previous async transactions
	 * into sync to keep correct ordering.
	 * Skip it for snapshot, as it won't have any transactions.
	 */
	if (!zfsvfs->z_issnap && (flag & O_SYNC)) {
		if (atomic_inc_32_nv(&zp->z_sync_cnt) == 1)
			zil_async_to_sync(zfsvfs->z_log, zp->z_id);
	}

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

int
zfs_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr)
{
	(void) offset, (void) cr;
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	/* Decrement the synchronous opens in the znode */
	if (!zfsvfs->z_issnap && (flag & O_SYNC) && (count == 1))
		atomic_dec_32(&zp->z_sync_cnt);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

#ifndef S_READ
#define	S_READ	1
#define	S_WRITE	2
#endif

/*
 * Map a busy UVM page for CPU access.
 * UVMPAGER_MAPIN_WRITE is 0 and yields a read-only mapping
 * (host -> device).  UVMPAGER_MAPIN_READ adds VM_PROT_WRITE
 * (device -> host).  S_WRITE fills the page, so it must pass
 * MAPIN_READ.  Stock NetBSD zfs_map_page uses the same polarity.
 */
static caddr_t
zfs_mapped_map_page(struct vm_page *pp, int rw)
{
	vaddr_t va;

#ifdef __HAVE_MM_MD_DIRECT_MAPPED_PHYS
	if (mm_md_direct_mapped_phys(VM_PAGE_TO_PHYS(pp), &va))
		return ((caddr_t)va);
#endif
	va = uvm_pagermapin(&pp, 1, UVMPAGER_MAPIN_WAITOK |
	    (rw == S_READ ? UVMPAGER_MAPIN_WRITE : UVMPAGER_MAPIN_READ));
	return ((caddr_t)va);
}

static void
zfs_mapped_unmap_page(struct vm_page *pp, caddr_t addr)
{
#ifdef __HAVE_MM_MD_DIRECT_MAPPED_PHYS
	vaddr_t va;

	if (mm_md_direct_mapped_phys(VM_PAGE_TO_PHYS(pp), &va))
		return;
#endif
	(void) pp;
	uvm_pagermapout((vaddr_t)addr, 1);
}

/*
 * write(2) after mmap: copy the new DMU bytes into any resident
 * pages so a later fault / mappedread sees the write.  Only
 * existing pages (UFP_NOALLOC).  NetBSD 11 zfs_vnops.c.
 */
void
update_pages(znode_t *zp, int64_t start, int len, objset_t *os)
{
	vnode_t *vp = ZTOV(zp);
	struct uvm_object *uobj = &vp->v_uobj;
	krwlock_t *rw = uobj->vmobjlock;
	caddr_t va;
	int off;

	if (vp == NULL || vp->v_data == NULL)
		return;

	rw_enter(rw, RW_WRITER);
	off = start & PAGEOFFSET;
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		struct vm_page *pp = NULL;
		int nbytes = MIN(PAGESIZE - off, len);
		int npages = 1;
		int found;

		found = uvn_findpages(uobj, start, &npages, &pp, NULL,
		    UFP_NOALLOC);
		if (found) {
			if (nbytes == PAGESIZE) {
				switch (uvm_pagegetdirty(pp)) {
				case UVM_PAGE_STATUS_DIRTY:
					uvm_pagemarkdirty(pp,
					    UVM_PAGE_STATUS_UNKNOWN);
					break;
				case UVM_PAGE_STATUS_UNKNOWN:
					pmap_clear_modify(pp);
					break;
				case UVM_PAGE_STATUS_CLEAN:
					break;
				}
			}
			rw_exit(rw);
			va = zfs_mapped_map_page(pp, S_WRITE);
			(void) dmu_read(os, zp->z_id, start + off, nbytes,
			    va + off, DMU_READ_PREFETCH);
			zfs_mapped_unmap_page(pp, va);
			rw_enter(rw, RW_WRITER);
			uvm_page_unbusy(&pp, 1);
		}
		len -= nbytes;
		off = 0;
	}
	rw_exit(rw);
}

/*
 * NetBSD has no UIO_NOCOPY / sendfile pager fill.  Shared zfs_read
 * only calls this under #ifdef UIO_NOCOPY.
 */
int
mappedread_sf(znode_t *zp, int nbytes, zfs_uio_t *uio)
{
	return (dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl), uio,
	    nbytes, DMU_READ_PREFETCH));
}

/*
 * read(2) after mmap: prefer a resident page (may be dirty from a
 * store) over the ARC.  Bounce through a kmem buffer so uiomove
 * cannot fault while the page is mapped.  NetBSD 11 zfs_vnops.c.
 */
int
mappedread(znode_t *zp, int nbytes, zfs_uio_t *uio)
{
	vnode_t *vp = ZTOV(zp);
	struct uvm_object *uobj = &vp->v_uobj;
	krwlock_t *rw = uobj->vmobjlock;
	int64_t start;
	caddr_t va;
	size_t len = nbytes;
	int off;
	int error = 0;
	void *buf = NULL;

	start = zfs_uio_offset(uio);
	off = start & PAGEOFFSET;

	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		struct vm_page *pp;
		uint64_t bytes = MIN(PAGESIZE - off, len);
		int npages, found;

retry:
		pp = NULL;
		npages = 1;
		rw_enter(rw, RW_WRITER);
		found = uvn_findpages(uobj, start, &npages, &pp, NULL,
		    UFP_NOALLOC);
		rw_exit(rw);

		if (found) {
			if (buf != NULL) {
				va = zfs_mapped_map_page(pp, S_READ);
				memcpy(buf, va + off, bytes);
				zfs_mapped_unmap_page(pp, va);
			}
			rw_enter(rw, RW_WRITER);
			uvm_page_unbusy(&pp, 1);
			rw_exit(rw);
			if (buf == NULL) {
				buf = kmem_alloc(PAGESIZE, KM_SLEEP);
				goto retry;
			}
			error = zfs_uiomove(buf, bytes, UIO_READ, uio);
		} else {
			error = dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, bytes, DMU_READ_PREFETCH);
		}

		len -= bytes;
		off = 0;
		if (error)
			break;
	}
	if (buf != NULL)
		kmem_free(buf, PAGESIZE);
	return (error);
}

int
zfs_write_simple(znode_t *zp, const void *data, size_t len,
    loff_t pos, size_t *presid)
{
	struct uio auio;
	struct iovec iov;
	int error;
	vnode_t *vp = ZTOV(zp);

	/*
	 * Do not go through the Solaris-shaped vn_rdwr() wrapper:
	 * its argument order remaps kcred/NOCRED/curthread into
	 * ulimit/cr and then calls native vn_rdwr with NOCRED.
	 */
	memset(&auio, 0, sizeof (auio));
	iov.iov_base = __DECONST(void *, data);
	iov.iov_len = len;
	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = pos;
	auio.uio_resid = len;
	auio.uio_rw = UIO_WRITE;
	auio.uio_vmspace = vmspace_kernel();
	error = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0)
		return (SET_ERROR(error));
	error = VOP_WRITE(vp, &auio, IO_UNIT, kcred);
	VOP_UNLOCK(vp);
	if (error)
		return (SET_ERROR(error));
	if (presid == NULL) {
		if (auio.uio_resid != 0)
			error = SET_ERROR(EIO);
	} else {
		*presid = auio.uio_resid;
	}
	return (error);
}

void
zfs_zrele_async(znode_t *zp)
{
	vnode_t *vp = ZTOV(zp);
	objset_t *os = ITOZSB(vp)->z_os;

	VN_RELE_ASYNC(vp, dsl_pool_zrele_taskq(dmu_objset_pool(os)));
}

/*
 * Lookup an entry in a directory, or an extended attribute directory.
 * If it exists, return a held vnode reference for it.
 *
 *	IN:	dvp	- vnode of directory to search.
 *		nm	- name of entry to lookup.
 *		pnp	- full pathname to lookup [UNUSED].
 *		flags	- LOOKUP_XATTR set if looking for an attribute.
 *		rdir	- root directory vnode [UNUSED].
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	vpp	- vnode of located entry, NULL if not found.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	NA
 */
int
zfs_lookup(vnode_t *dvp, const char *nm, vnode_t **vpp,
    struct componentname *cnp, int nameiop, cred_t *cr, int flags,
    boolean_t cached)
{
	znode_t *zdp = VTOZ(dvp);
	znode_t *zp;
	zfsvfs_t *zfsvfs = zdp->z_zfsvfs;
	int	error = 0;

	/*
	 * Fast path lookup, however we must skip DNLC lookup
	 * for case folding or normalizing lookups because the
	 * DNLC code only stores the passed in name.  This means
	 * creating 'a' and removing 'A' on a case insensitive
	 * file system would work, but DNLC still thinks 'a'
	 * exists and won't let you create it again on the next
	 * pass through fast path.
	 */
	if (!(flags & LOOKUP_XATTR)) {
		if (dvp->v_type != VDIR) {
			return (SET_ERROR(ENOTDIR));
		} else if (zdp->z_sa_hdl == NULL) {
			return (SET_ERROR(EIO));
		}
	}

	DTRACE_PROBE2(zfs__fastpath__lookup__miss, vnode_t *, dvp,
	    const char *, nm);

	if ((error = zfs_enter_verify_zp(zfsvfs, zdp, FTAG)) != 0)
		return (error);

	*vpp = NULL;

	if (flags & LOOKUP_XATTR) {
		/*
		 * If the xattr property is off, refuse the lookup request.
		 */
		if (!(zfsvfs->z_flags & ZSB_XATTR)) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EOPNOTSUPP));
		}

		/*
		 * We don't allow recursive attributes..
		 * Maybe someday we will.
		 */
		if (zdp->z_pflags & ZFS_XATTR) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EINVAL));
		}

		if ((error = zfs_get_xattrdir(VTOZ(dvp), &zp, cr, flags))) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
		*vpp = ZTOV(zp);

		/*
		 * Do we have permission to get into attribute directory?
		 */
		if (flags & LOOKUP_NAMED_ATTR)
			error = zfs_zaccess(zp, ACE_EXECUTE, V_NAMEDATTR,
			    B_FALSE, cr);
		else
			error = zfs_zaccess(zp, ACE_EXECUTE, 0, B_FALSE, cr);
		if (error) {
			vrele(ZTOV(zp));
		}

		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Check accessibility of directory if we're not coming in via
	 * VOP_CACHEDLOOKUP.
	 */
	if (!cached) {
		if ((error = zfs_zaccess(zdp, ACE_EXECUTE, 0, B_FALSE, cr))) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

	if (zfsvfs->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	/*
	 * First handle the special cases.
	 */
	if (cnp != NULL && (cnp->cn_flags & ISDOTDOT) != 0) {
		/*
		 * If we are a snapshot mounted under .zfs, return
		 * the vp for the snapshot directory.
		 */
		if (zdp->z_id == zfsvfs->z_root && zfsvfs->z_parent != zfsvfs) {
			struct componentname cn;
			vnode_t *zfsctl_vp;
			int ltype;

			zfs_exit(zfsvfs, FTAG);
			ltype = VOP_ISLOCKED(dvp);
			VOP_UNLOCK(dvp);
			error = zfsctl_root(zfsvfs->z_parent, LK_SHARED,
			    &zfsctl_vp);
			if (error == 0) {
				cn.cn_nameptr = "snapshot";
				cn.cn_namelen = strlen(cn.cn_nameptr);
				cn.cn_nameiop = cnp->cn_nameiop;
				cn.cn_flags = cnp->cn_flags & ~ISDOTDOT;
				error = VOP_LOOKUP(zfsctl_vp, vpp, &cn);
				vput(zfsctl_vp);
			}
			vn_lock(dvp, ltype | LK_RETRY);
			return (error);
		}
	}
	if (zfs_has_ctldir(zdp) && strcmp(nm, ZFS_CTLDIR_NAME) == 0) {
		zfs_exit(zfsvfs, FTAG);
		if (zfsvfs->z_show_ctldir == ZFS_SNAPDIR_DISABLED)
			return (SET_ERROR(ENOENT));
		if (cnp != NULL && (cnp->cn_flags & ISLASTCN) != 0 &&
		    nameiop != LOOKUP)
			return (SET_ERROR(ENOTSUP));
		error = zfsctl_root(zfsvfs, 0, vpp);
		return (error);
	}

	/*
	 * NetBSD VOP_LOOKUP v2 returns *vpp referenced and
	 * unlocked (unless *vpp == dvp, which stays locked).
	 * namei will vn_lock the result.
	 *
	 * zfs_dirlook(".") returns dzp with no extra hold, so
	 * vref.  zfs_dirlook("..") always zfs_zget → vcache_get,
	 * which adds a hold even when parent == self (dataset
	 * root).  Do not vref that path: `if (*vpp == dvp) vref`
	 * over-held ".." at root.  The VOP wrapper special-cases
	 * ".." at root and vrefs there instead of calling here.
	 */
	error = zfs_dirlook(zdp, nm, &zp,
	    (flags & FIGNORECASE) ? ZCILOOK : 0);
	if (error == 0)
		*vpp = ZTOV(zp);

	zfs_exit(zfsvfs, FTAG);
	if (error == 0 && nm[0] == '.' && nm[1] == '\0') {
		ASSERT3P(*vpp, ==, dvp);
		vref(dvp);
	}
#ifdef ZFS_DEBUG
	else if (error == 0 && *vpp == dvp) {
		/*
		 * ".." at the dataset root (or any other alias of
		 * dvp).  zfs_zget already held; namei wants that
		 * one extra ref, not two.
		 */
		ASSERT3U(vrefcnt(dvp), >=, 2);
	}
#endif

	if (error != 0)
		*vpp = NULL;

	/* Translate errors and add SAVENAME when needed. */
	if (cnp != NULL && (cnp->cn_flags & ISLASTCN)) {
		switch (nameiop) {
		case CREATE:
		case RENAME:
			if (error == ENOENT) {
				error = EJUSTRETURN;
				break;
			}
			zfs_fallthrough;
		case DELETE:
			break;
		}
	}

	/* Insert name into cache (as non-existent) if appropriate. */
	if (cnp != NULL && zfsvfs->z_use_namecache && !zfsvfs->z_replay &&
	    error == ENOENT && (cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(dvp, NULL, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp->cn_flags);

	/* Insert name into cache if appropriate. */
	if (cnp != NULL && zfsvfs->z_use_namecache && !zfsvfs->z_replay &&
	    error == 0 && (cnp->cn_flags & MAKEENTRY)) {
		if (!(cnp->cn_flags & ISLASTCN) ||
		    (nameiop != DELETE && nameiop != RENAME)) {
			cache_enter(dvp, *vpp, cnp->cn_nameptr, cnp->cn_namelen,
			    cnp->cn_flags);
		}
	}

	return (error);
}

static inline bool
is_nametoolong(zfsvfs_t *zfsvfs, const char *name)
{
	size_t dlen = strlen(name);
	return ((!zfsvfs->z_longname && dlen >= ZAP_MAXNAMELEN) ||
	    dlen >= ZAP_MAXNAMELEN_NEW);
}

/*
 * POSIX: changing an EA updates the owning file's ctime.  The xattr
 * lives in a hidden directory, so dirent ops would otherwise only
 * bump that directory.  Hold the parent for the same DMU tx.
 */
static int
zfs_xattr_owner_hold(znode_t *dzp, znode_t **azpp)
{
	uint64_t parent;
	int error;

	*azpp = NULL;
	if ((dzp->z_pflags & ZFS_XATTR) == 0)
		return (0);
	error = sa_lookup(dzp->z_sa_hdl, SA_ZPL_PARENT(dzp->z_zfsvfs),
	    &parent, sizeof (parent));
	if (error != 0)
		return (error);
	return (zfs_zget(dzp->z_zfsvfs, parent, azpp));
}

static void
zfs_xattr_owner_ctime(znode_t *azp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs;
	sa_bulk_attr_t bulk[4];
	uint64_t mtime[2], ctime[2];
	int count = 0;

	if (azp == NULL)
		return;
	zfsvfs = azp->z_zfsvfs;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    &ctime, sizeof (ctime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &azp->z_pflags, 8);
	zfs_tstamp_update_setup(azp, STATE_CHANGED, mtime, ctime);
	ZFS_PERSIST_SEQ(azp, bulk, count);
	VERIFY0(sa_bulk_update(azp->z_sa_hdl, bulk, count, tx));
	zfs_znode_update_vfs(azp);
}

static void
zfs_xattr_owner_rele(znode_t *azp)
{
	if (azp != NULL)
		vrele(ZTOV(azp));
}

/*
 * Attempt to create a new entry in a directory.  If the entry
 * already exists, truncate the file if permissible, else return
 * an error.  Return the vp of the created or trunc'd file.
 *
 *	IN:	dvp	- vnode of directory to put new file entry in.
 *		name	- name of new file entry.
 *		vap	- attributes of new file.
 *		excl	- flag indicating exclusive or non-exclusive mode.
 *		mode	- mode to open file with.
 *		cr	- credentials of caller.
 *		flag	- large file flag [UNUSED].
 *		ct	- caller context
 *		vsecp	- ACL to be set
 *
 *	OUT:	vpp	- vnode of created or trunc'd entry.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated if new entry created
 *	 vp - ctime|mtime always, atime if new
 */
int
zfs_create(znode_t *dzp, const char *name, vattr_t *vap, int excl, int mode,
    znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp)
{
	(void) excl, (void) mode, (void) flag;
	znode_t		*zp;
	znode_t		*azp = NULL;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	objset_t	*os;
	dmu_tx_t	*tx;
	int		error;
	uid_t		uid = crgetuid(cr);
	gid_t		gid = crgetgid(cr);
	uint64_t	projid = ZFS_DEFAULT_PROJID;
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	uint64_t	txtype;

	if (is_nametoolong(zfsvfs, name))
		return (SET_ERROR(ENAMETOOLONG));

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */
	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (vsecp || (vap->va_mask & AT_XVATTR) ||
	    IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & AT_XVATTR) {
		if ((error = secpolicy_xvattr(ZTOV(dzp), (xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_type)) != 0) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

	*zpp = NULL;

	if ((vap->va_mode & S_ISVTX) && secpolicy_vnode_stky_modify(cr))
		vap->va_mode &= ~S_ISVTX;

	error = zfs_dirent_lookup(dzp, name, &zp, ZNEW);
	if (error) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	ASSERT0P(zp);

	/*
	 * Create a new file object and update the directory
	 * to reference it.
	 */
	if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		goto out;
	}

	/*
	 * We only support the creation of regular files in
	 * extended attribute directories.
	 */

	if ((dzp->z_pflags & ZFS_XATTR) &&
	    (vap->va_type != VREG)) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap,
	    cr, vsecp, &acl_ids)) != 0)
		goto out;

	projid = zfs_inherit_projid(dzp);
	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, projid)) {
		zfs_acl_ids_free(&acl_ids);
		error = SET_ERROR(EDQUOT);
		goto out;
	}

	if ((dzp->z_pflags & ZFS_XATTR) != 0 &&
	    strncmp(name, ".zxT.", 5) != 0)
		(void) zfs_xattr_owner_hold(dzp, &azp);

	tx = dmu_tx_create(os);

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
	if (azp != NULL)
		dmu_tx_hold_sa(tx, azp->z_sa_hdl, ZFS_SEQ_MAY_GROW(azp));
	if (!zfsvfs->z_use_sa &&
	    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
		    0, acl_ids.z_aclp->z_acl_bytes);
	}
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_xattr_owner_rele(azp);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	error = zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);
	if (error != 0) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
		goto out;
	}

	error = zfs_link_create(dzp, name, zp, tx, ZNEW);
	if (error != 0) {
		/*
		 * Since, we failed to add the directory entry for it,
		 * delete the newly created dnode.
		 */
		zfs_znode_delete(zp, tx);
		VOP_UNLOCK(ZTOV(zp));
		zrele(zp);
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
		goto out;
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	zfs_xattr_owner_ctime(azp, tx);

	txtype = zfs_log_create_txtype(Z_FILE, vsecp, vap);
	zfs_log_create(zilog, tx, txtype, dzp, zp, name,
	    vsecp, acl_ids.z_fuidp, vap);
	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

out:
	zfs_xattr_owner_rele(azp);
	if (error == 0) {
		*zpp = zp;
	}

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Remove an entry from a directory.
 *
 *	IN:	dvp	- vnode of directory to remove entry from.
 *		name	- name of entry to remove.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime
 *	 vp - ctime (if nlink > 0)
 */
int
zfs_remove_(vnode_t *dvp, vnode_t *vp, const char *name, cred_t *cr)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp;
	znode_t		*xzp;
	znode_t		*azp = NULL;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	xattr_obj;
	uint64_t	obj = 0;
	dmu_tx_t	*tx;
	boolean_t	unlinked;
	uint64_t	txtype;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zp = VTOZ(vp);
	if ((error = zfs_verify_zp(zp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	zilog = zfsvfs->z_log;

	xattr_obj = 0;
	xzp = NULL;

	if ((error = zfs_zaccess_delete(dzp, zp, cr))) {
		goto out;
	}

	/*
	 * Need to use rmdir for removing directories.
	 */
	if (vp->v_type == VDIR) {
		error = SET_ERROR(EPERM);
		goto out;
	}

	obj = zp->z_id;

	/* are there any extended attributes? */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error == 0 && xattr_obj) {
		error = zfs_zget(zfsvfs, xattr_obj, &xzp);
		if (error != 0)
			xzp = NULL;
	}

	if ((dzp->z_pflags & ZFS_XATTR) != 0 &&
	    strncmp(name, ".zxT.", 5) != 0)
		(void) zfs_xattr_owner_hold(dzp, &azp);

	/*
	 * We may delete the znode now, or we may put it in the unlinked set;
	 * it depends on whether we're the last link, and on whether there are
	 * other holds on the vnode.  So we dmu_tx_hold() the right things to
	 * allow for either case.
	 */
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, ZFS_SEQ_MAY_GROW(zp));
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);

	if (xzp) {
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
	}
	if (azp != NULL)
		dmu_tx_hold_sa(tx, azp->z_sa_hdl, ZFS_SEQ_MAY_GROW(azp));

	/* charge as an update -- would be nice not to charge at all */
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	/*
	 * Mark this transaction as typically resulting in a net free of space
	 */
	dmu_tx_mark_netfree(tx);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	/*
	 * Remove the directory entry.
	 */
	error = zfs_link_destroy(dzp, name, zp, tx, ZEXISTS, &unlinked);

	if (error) {
		dmu_tx_commit(tx);
		goto out;
	}

	if (unlinked) {
		zfs_unlinked_add(zp, tx);
		vp->v_vflag |= VV_NOSYNC;
	}
	zfs_xattr_owner_ctime(azp, tx);
	/* XXX check changes to linux vnops */
	txtype = TX_REMOVE;
	zfs_log_remove(zilog, tx, txtype, dzp, name, obj, unlinked);

	dmu_tx_commit(tx);
	if (zfsvfs->z_use_namecache)
		cache_purge(vp);
out:

	if (xzp)
		vrele(ZTOV(xzp));
	zfs_xattr_owner_rele(azp);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
zfs_lookup_internal(znode_t *dzp, const char *name, vnode_t **vpp,
    struct componentname *cnp, int nameiop)
{
	int error;

	cnp->cn_nameptr = __DECONST(char *, name);
	cnp->cn_namelen = strlen(name);
	cnp->cn_nameiop = nameiop;
	cnp->cn_flags = ISLASTCN;
	cnp->cn_cred = kcred;

	error = zfs_lookup(ZTOV(dzp), name, vpp, cnp, nameiop, kcred, 0,
	    B_FALSE);
#ifdef ZFS_DEBUG
	if (error) {
		printf("got error %d on name %s on op %d\n", error, name,
		    nameiop);
	}
#endif
	return (error);
}

int
zfs_remove(znode_t *dzp, const char *name, cred_t *cr, int flags)
{
	vnode_t *vp;
	int error;
	struct componentname cn;

	if ((error = zfs_lookup_internal(dzp, name, &vp, &cn, DELETE)))
		return (error);

	error = zfs_remove_(ZTOV(dzp), vp, name, cr);
	/*
	 * zfs_lookup() returns a referenced, unlocked vnode.
	 * vput() would genfs_unlock an unheld lock.
	 */
	vrele(vp);
	return (error);
}
/*
 * Create a new directory and insert it into dvp using the name
 * provided.  Return a pointer to the inserted directory.
 *
 *	IN:	dvp	- vnode of directory to add subdir to.
 *		dirname	- name of new directory.
 *		vap	- attributes of new directory.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *		vsecp	- ACL to be set
 *
 *	OUT:	vpp	- vnode of created directory.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 *	 vp - ctime|mtime|atime updated
 */
int
zfs_mkdir(znode_t *dzp, const char *dirname, vattr_t *vap, znode_t **zpp,
    cred_t *cr, int flags, vsecattr_t *vsecp)
{
	(void) flags, (void) vsecp;
	znode_t		*zp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	txtype;
	dmu_tx_t	*tx;
	int		error;
	uid_t		uid = crgetuid(cr);
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;

	ASSERT3U(vap->va_type, ==, VDIR);

	if (is_nametoolong(zfsvfs, dirname))
		return (SET_ERROR(ENAMETOOLONG));

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */
	if (zfsvfs->z_use_fuids == B_FALSE &&
	    ((vap->va_mask & AT_XVATTR) ||
	    IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	if (dzp->z_pflags & ZFS_XATTR) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (zfsvfs->z_utf8 && u8_validate(dirname,
	    strlen(dirname), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & AT_XVATTR) {
		if ((error = secpolicy_xvattr(ZTOV(dzp), (xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_type)) != 0) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr,
	    NULL, &acl_ids)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * First make sure the new directory doesn't exist.
	 *
	 * Existence is checked first to make sure we don't return
	 * EACCES instead of EEXIST which can cause some applications
	 * to fail.
	 */
	*zpp = NULL;

	if ((error = zfs_dirent_lookup(dzp, dirname, &zp, ZNEW))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	ASSERT0P(zp);

	if ((error = zfs_zaccess(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE, cr))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, zfs_inherit_projid(dzp))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EDQUOT));
	}

	/*
	 * Add a new entry to the directory.
	 */
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, dirname);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Create new node.
	 */
	error = zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);
	if (error != 0)
		goto out;

	/*
	 * Now put new name in parent dir.
	 */
	error = zfs_link_create(dzp, dirname, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
		VOP_UNLOCK(ZTOV(zp));
		zrele(zp);
		goto out;
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	*zpp = zp;

	txtype = zfs_log_create_txtype(Z_DIR, NULL, vap);
	zfs_log_create(zilog, tx, txtype, dzp, zp, dirname, NULL,
	    acl_ids.z_fuidp, vap);

out:
	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Remove a directory subdir entry.  If the current working
 * directory is the same as the subdir to be removed, the
 * remove will fail.
 *
 *	IN:	dvp	- vnode of directory to remove from.
 *		name	- name of directory to be removed.
 *		cwd	- vnode of current working directory.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
int
zfs_rmdir_(vnode_t *dvp, vnode_t *vp, const char *name, cred_t *cr)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	if ((error = zfs_verify_zp(zp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	zilog = zfsvfs->z_log;

	if ((error = zfs_zaccess_delete(dzp, zp, cr))) {
		goto out;
	}

	if (vp->v_type != VDIR) {
		error = SET_ERROR(ENOTDIR);
		goto out;
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, ZFS_SEQ_MAY_GROW(zp));
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	error = zfs_link_destroy(dzp, name, zp, tx, ZEXISTS, NULL);

	if (error == 0) {
		uint64_t txtype = TX_RMDIR;
		zfs_log_remove(zilog, tx, txtype, dzp, name,
		    ZFS_NO_OBJECT, B_FALSE);
	}

	dmu_tx_commit(tx);

	if (zfsvfs->z_use_namecache)
		cache_purge(vp);
out:
	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

int
zfs_rmdir(znode_t *dzp, const char *name, znode_t *cwd, cred_t *cr, int flags)
{
	struct componentname cn;
	vnode_t *vp;
	int error;

	if ((error = zfs_lookup_internal(dzp, name, &vp, &cn, DELETE)))
		return (error);

	error = zfs_rmdir_(ZTOV(dzp), vp, name, cr);
	/* Lookup returned unlocked; see zfs_remove(). */
	vrele(vp);
	return (error);
}

/*
 * Read as many directory entries as will fit into the provided
 * buffer from the given directory cursor position (specified in
 * the uio structure).
 *
 *	IN:	vp	- vnode of directory to read.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *		eofp	- set to true if end-of-file detected.
 *		ncookies- number of entries in cookies
 *		cookies	- offsets to directory entries
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - atime updated
 *
 * Note that the low 4 bits of the cookie returned by zap is always zero.
 * This allows us to use the low range for "special" directory entries:
 * We use 0 for '.', and 1 for '..'.  If this is the root of the filesystem,
 * we use the offset 2 for the '.zfs' directory.
 */
int
zfs_readdir(vnode_t *vp, zfs_uio_t *uio, cred_t *cr, int *eofp,
    int *ncookies, cookie_t **cookies)
{
	znode_t		*zp = VTOZ(vp);
	iovec_t		*iovp;
	dirent64_t	*odp;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	caddr_t		outbuf;
	size_t		bufsize;
	ssize_t		orig_resid;
	zap_cursor_t	zc;
	zap_attribute_t	*zap;
	uint_t		bytes_wanted;
	uint64_t	offset; /* must be unsigned; checks for < 1 */
	uint64_t	parent;
	int		local_eof;
	int		outcount;
	int		error;
	uint8_t		prefetch;
	uint8_t		type;
	int		ncooks = 0;
	size_t		cook_bytes = 0;
	cookie_t	*cooks = NULL;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent))) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * If we are not given an eof variable,
	 * use a local one.
	 */
	if (eofp == NULL)
		eofp = &local_eof;

	/*
	 * Check for valid iov_len.
	 */
	if (GET_UIO_STRUCT(uio)->uio_iov->iov_len <= 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Quit if directory has been removed (posix)
	 */
	if ((*eofp = (zp->z_unlinked != 0)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	error = 0;
	os = zfsvfs->z_os;
	offset = zfs_uio_offset(uio);
	orig_resid = zfs_uio_resid(uio);
	prefetch = zp->z_zn_prefetch;
	zap = zap_attribute_long_alloc();

	/*
	 * Initialize the iterator cursor.
	 */
	if (offset <= 3) {
		/*
		 * Start iteration from the beginning of the directory.
		 */
		zap_cursor_init(&zc, os, zp->z_id);
	} else {
		/*
		 * The offset is a serialized cursor.
		 */
		zap_cursor_init_serialized(&zc, os, zp->z_id, offset);
	}

	/*
	 * Get space to change directory entries into fs independent format.
	 */
	iovp = GET_UIO_STRUCT(uio)->uio_iov;
	bytes_wanted = iovp->iov_len;
	if (zfs_uio_segflg(uio) != UIO_SYSSPACE || zfs_uio_iovcnt(uio) != 1) {
		bufsize = bytes_wanted;
		outbuf = kmem_alloc(bufsize, KM_SLEEP);
		odp = (struct dirent64 *)outbuf;
	} else {
		bufsize = bytes_wanted;
		outbuf = NULL;
		odp = (struct dirent64 *)iovp->iov_base;
	}

	if (ncookies != NULL) {
		/*
		 * Minimum entry size is dirent size and 1 byte for a file name.
		 */
		ncooks = zfs_uio_resid(uio) / (sizeof (struct dirent) -
		    sizeof (((struct dirent *)NULL)->d_name) + 1);
		cook_bytes = ncooks * sizeof (*cooks);
		cooks = kmem_alloc(cook_bytes, KM_SLEEP);
		*cookies = cooks;
		*ncookies = ncooks;
	}

	/*
	 * Transform to file-system independent format
	 */
	outcount = 0;
	while (outcount < bytes_wanted) {
		ino64_t objnum;
		ushort_t reclen;

		/*
		 * Special case `.', `..', and `.zfs'.
		 */
		if (offset == 0) {
			(void) strlcpy(zap->za_name, ".", zap->za_name_len);
			zap->za_normalization_conflict = 0;
			objnum = zp->z_id;
			type = DT_DIR;
		} else if (offset == 1) {
			(void) strlcpy(zap->za_name, "..", zap->za_name_len);
			zap->za_normalization_conflict = 0;
			objnum = parent;
			type = DT_DIR;
		} else if (offset == 2 && zfs_show_ctldir(zp)) {
			(void) strlcpy(zap->za_name, ZFS_CTLDIR_NAME,
			    zap->za_name_len);
			zap->za_normalization_conflict = 0;
			objnum = ZFSCTL_INO_ROOT;
			type = DT_DIR;
		} else {
			/*
			 * Grab next entry.
			 */
			if ((error = zap_cursor_retrieve(&zc, zap))) {
				if ((*eofp = (error == ENOENT)) != 0)
					break;
				else
					goto update;
			}

			if (zap->za_integer_length != 8 ||
			    zap->za_num_integers != 1) {
				cmn_err(CE_WARN, "zap_readdir: bad directory "
				    "entry, obj = %lld, offset = %lld\n",
				    (u_longlong_t)zp->z_id,
				    (u_longlong_t)offset);
				error = SET_ERROR(ENXIO);
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap->za_first_integer);
			/*
			 * MacOS X can extract the object type here such as:
			 * uint8_t type = ZFS_DIRENT_TYPE(zap.za_first_integer);
			 */
			type = ZFS_DIRENT_TYPE(zap->za_first_integer);
		}

		reclen = DIRENT64_RECLEN(strlen(zap->za_name));

		/*
		 * Will this entry fit in the buffer?
		 */
		if (outcount + reclen > bufsize) {
			/*
			 * Did we manage to fit anything in the buffer?
			 */
			if (!outcount) {
				error = SET_ERROR(EINVAL);
				goto update;
			}
			break;
		}
		/*
		 * Add normal entry.
		 */
		odp->d_fileno = objnum;
		odp->d_reclen = reclen;
		odp->d_namlen = strlen(zap->za_name);
		/*
		 * Packed dirent: dest is this record, not sizeof(d_name).
		 */
		(void) strlcpy(odp->d_name, zap->za_name,
		    (size_t)odp->d_reclen - offsetof(dirent64_t, d_name));
		odp->d_type = type;
		/*
		 * strlcpy stops at the NUL.  The align pad out to
		 * d_reclen would otherwise be leftover heap.
		 */
		{
			size_t used = offsetof(dirent64_t, d_name) +
			    (size_t)odp->d_namlen + 1;

			if ((size_t)reclen > used)
				memset((char *)odp + used, 0,
				    (size_t)reclen - used);
		}
		odp = (dirent64_t *)((intptr_t)odp + reclen);

		outcount += reclen;

		ASSERT3S(outcount, <=, bufsize);

		if (prefetch)
			dmu_prefetch_dnode(os, objnum, ZIO_PRIORITY_SYNC_READ);

		/*
		 * Move to the next entry, fill in the previous offset.
		 */
		if (offset > 2 || (offset == 2 && !zfs_show_ctldir(zp))) {
			zap_cursor_advance(&zc);
			offset = zap_cursor_serialize(&zc);
		} else {
			offset += 1;
		}

		if (cooks != NULL) {
			*cooks++ = offset;
			ncooks--;
			KASSERT(ncooks >= 0, ("ncookies=%d", ncooks));
		}
	}
	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

	/* Subtract unused cookies */
	if (ncookies != NULL)
		*ncookies -= ncooks;

	if (zfs_uio_segflg(uio) == UIO_SYSSPACE && zfs_uio_iovcnt(uio) == 1) {
		iovp->iov_base += outcount;
		iovp->iov_len -= outcount;
		zfs_uio_resid(uio) -= outcount;
	} else if ((error =
	    zfs_uiomove(outbuf, (long)outcount, UIO_READ, uio))) {
		/*
		 * Reset the pointer.
		 */
		offset = zfs_uio_offset(uio);
	}

update:
	zap_cursor_fini(&zc);
	zap_attribute_free(zap);
	if (zfs_uio_segflg(uio) != UIO_SYSSPACE || zfs_uio_iovcnt(uio) != 1)
		kmem_free(outbuf, bufsize);

	if (error == ENOENT)
		error = orig_resid == zfs_uio_resid(uio) ? EINVAL : 0;

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	zfs_uio_setoffset(uio, offset);
	zfs_exit(zfsvfs, FTAG);
	if (error != 0 && cookies != NULL && ncookies != NULL) {
		kmem_free(*cookies, cook_bytes);
		*cookies = NULL;
		*ncookies = 0;
	}
	return (error);
}

/*
 * Get the requested file attributes and place them in the provided
 * vattr structure.
 *
 *	IN:	vp	- vnode of file.
 *		vap	- va_mask identifies requested attributes.
 *			  If AT_XVATTR set, then optional attrs are requested
 *		flags	- ATTR_NOACLCHECK (CIFS server context)
 *		cr	- credentials of caller.
 *
 *	OUT:	vap	- attribute values.
 *
 *	RETURN:	0 (always succeeds).
 */
int
zfs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int	error = 0;
	uint32_t blksize;
	u_longlong_t nblocks;
	uint64_t mtime[2], ctime[2], crtime[2], rdev;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t *xoap = NULL;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	sa_bulk_attr_t bulk[4];
	int count = 0;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	zfs_fuid_map_ids(zp, cr, &vap->va_uid, &vap->va_gid);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_RDEV(zfsvfs), NULL,
		    &rdev, 8);

	{
		int saerr = sa_bulk_lookup(zp->z_sa_hdl, bulk, count);
		if (saerr != 0) {
			/*
			 * Root znode SA can still be populating during
			 * create; ENOENT there must not fail stat/ls.
			 * Any other SA lookup error is real — match
			 * FreeBSD and fail the getattr.
			 */
			zfs_dbgmsg("zfs_getattr sa_bulk_lookup obj %llu "
			    "err %d root %d",
			    (u_longlong_t)zp->z_id, saerr,
			    zp->z_id == zfsvfs->z_root);
			if (saerr == ENOENT && zp->z_id == zfsvfs->z_root) {
				memset(mtime, 0, sizeof (mtime));
				memset(ctime, 0, sizeof (ctime));
				memset(crtime, 0, sizeof (crtime));
				rdev = 0;
			} else {
				zfs_exit(zfsvfs, FTAG);
				return (saerr);
			}
		}
	}

	/*
	 * If ACL is trivial don't bother looking for ACE_READ_ATTRIBUTES.
	 * Also, if we are the owner don't bother, since owner should
	 * always be allowed to read basic attributes of file.
	 */
	if (!(zp->z_pflags & ZFS_ACL_TRIVIAL) &&
	    (vap->va_uid != crgetuid(cr))) {
		if ((error = zfs_zaccess(zp, ACE_READ_ATTRIBUTES, 0,
		    skipaclchk, cr))) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

	/*
	 * Return all attributes.  It's cheaper to provide the answer
	 * than to determine whether we were asked the question.
	 */

	vap->va_type = (vp->v_type != VNON) ? vp->v_type : IFTOVT(zp->z_mode);
	if (vap->va_type == VNON)
		vap->va_type = VREG;
	vap->va_mode = zp->z_mode & ~S_IFMT;
	if (vp->v_mount != NULL) {
		vap->va_fsid =
		    (dev_t)vp->v_mount->mnt_stat.f_fsidx.__fsid_val[0];
		if (vap->va_fsid == 0)
			vap->va_fsid = (dev_t)vp->v_mount->mnt_stat.f_fsid;
	} else {
		vap->va_fsid = 0;
	}
	vap->va_nodeid = zp->z_id;
	vap->va_nlink = zp->z_links;
	if (((vp->v_vflag & VV_ROOT) != 0) && zfs_show_ctldir(zp) &&
	    zp->z_links < ZFS_LINK_MAX)
		vap->va_nlink++;
	vap->va_size = zp->z_size;
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		vap->va_rdev = zfs_cmpldev(rdev);
	else
		vap->va_rdev = NODEV;
	vap->va_gen = zp->z_gen;
	vap->va_flags = 0;	/* FreeBSD: Reset chflags(2) flags. */
	vap->va_filerev = atomic_load_64(&zp->z_seq);

	/*
	 * Add in any requested optional attributes and the create time.
	 * Also set the corresponding bits in the returned attribute bitmap.
	 */
	if ((xoap = xva_getxoptattr(xvap)) != NULL && zfsvfs->z_use_fuids) {
		if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE)) {
			xoap->xoa_archive =
			    ((zp->z_pflags & ZFS_ARCHIVE) != 0);
			XVA_SET_RTN(xvap, XAT_ARCHIVE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_READONLY)) {
			xoap->xoa_readonly =
			    ((zp->z_pflags & ZFS_READONLY) != 0);
			XVA_SET_RTN(xvap, XAT_READONLY);
		}

		if (XVA_ISSET_REQ(xvap, XAT_SYSTEM)) {
			xoap->xoa_system =
			    ((zp->z_pflags & ZFS_SYSTEM) != 0);
			XVA_SET_RTN(xvap, XAT_SYSTEM);
		}

		if (XVA_ISSET_REQ(xvap, XAT_HIDDEN)) {
			xoap->xoa_hidden =
			    ((zp->z_pflags & ZFS_HIDDEN) != 0);
			XVA_SET_RTN(xvap, XAT_HIDDEN);
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			xoap->xoa_nounlink =
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0);
			XVA_SET_RTN(xvap, XAT_NOUNLINK);
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			xoap->xoa_immutable =
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0);
			XVA_SET_RTN(xvap, XAT_IMMUTABLE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			xoap->xoa_appendonly =
			    ((zp->z_pflags & ZFS_APPENDONLY) != 0);
			XVA_SET_RTN(xvap, XAT_APPENDONLY);
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			xoap->xoa_nodump =
			    ((zp->z_pflags & ZFS_NODUMP) != 0);
			XVA_SET_RTN(xvap, XAT_NODUMP);
		}

		if (XVA_ISSET_REQ(xvap, XAT_OPAQUE)) {
			xoap->xoa_opaque =
			    ((zp->z_pflags & ZFS_OPAQUE) != 0);
			XVA_SET_RTN(xvap, XAT_OPAQUE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			xoap->xoa_av_quarantined =
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0);
			XVA_SET_RTN(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			xoap->xoa_av_modified =
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0);
			XVA_SET_RTN(xvap, XAT_AV_MODIFIED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) &&
		    vp->v_type == VREG) {
			zfs_sa_get_scanstamp(zp, xvap);
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			xoap->xoa_reparse = ((zp->z_pflags & ZFS_REPARSE) != 0);
			XVA_SET_RTN(xvap, XAT_REPARSE);
		}
		if (XVA_ISSET_REQ(xvap, XAT_GEN)) {
			xoap->xoa_generation = zp->z_gen;
			XVA_SET_RTN(xvap, XAT_GEN);
		}

		if (XVA_ISSET_REQ(xvap, XAT_OFFLINE)) {
			xoap->xoa_offline =
			    ((zp->z_pflags & ZFS_OFFLINE) != 0);
			XVA_SET_RTN(xvap, XAT_OFFLINE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_SPARSE)) {
			xoap->xoa_sparse =
			    ((zp->z_pflags & ZFS_SPARSE) != 0);
			XVA_SET_RTN(xvap, XAT_SPARSE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJINHERIT)) {
			xoap->xoa_projinherit =
			    ((zp->z_pflags & ZFS_PROJINHERIT) != 0);
			XVA_SET_RTN(xvap, XAT_PROJINHERIT);
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJID)) {
			xoap->xoa_projid = zp->z_projid;
			XVA_SET_RTN(xvap, XAT_PROJID);
		}
	}

	ZFS_TIME_DECODE(&vap->va_atime, zp->z_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, ctime);
	ZFS_TIME_DECODE(&vap->va_birthtime, crtime);

	sa_object_size(zp->z_sa_hdl, &blksize, &nblocks);
	vap->va_blksize = blksize;
	vap->va_bytes = nblocks << 9;	/* nblocks * 512 */

	if (zp->z_blksz == 0) {
		/*
		 * Block size hasn't been set; suggest maximal I/O transfers.
		 */
		vap->va_blksize = zfsvfs->z_max_blksz;
	}

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * For the operation of changing file's user/group/project, we need to
 * handle not only the main object that is assigned to the file directly,
 * but also the ones that are used by the file via hidden xattr directory.
 *
 * Because the xattr directory may contains many EA entries, as to it may
 * be impossible to change all of them via the transaction of changing the
 * main object's user/group/project attributes. Then we have to change them
 * via other multiple independent transactions one by one. It may be not good
 * solution, but we have no better idea yet.
 */
static int
zfs_setattr_dir(znode_t *dzp)
{
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	objset_t	*os = zfsvfs->z_os;
	zap_cursor_t	zc;
	zap_attribute_t	*zap;
	znode_t		*zp = NULL;
	dmu_tx_t	*tx = NULL;
	uint64_t	uid, gid;
	sa_bulk_attr_t	bulk[4];
	int		count;
	int		err;

	zap = zap_attribute_alloc();
	zap_cursor_init(&zc, os, dzp->z_id);
	while ((err = zap_cursor_retrieve(&zc, zap)) == 0) {
		count = 0;
		if (zap->za_integer_length != 8 || zap->za_num_integers != 1) {
			err = ENXIO;
			break;
		}

		err = zfs_dirent_lookup(dzp, zap->za_name, &zp, ZEXISTS);
		if (err == ENOENT)
			goto next;
		if (err)
			break;

		if (zp->z_uid == dzp->z_uid &&
		    zp->z_gid == dzp->z_gid &&
		    zp->z_projid == dzp->z_projid)
			goto next;

		tx = dmu_tx_create(os);
		if (!(zp->z_pflags & ZFS_PROJID))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);

		err = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (err)
			break;

		mutex_enter(&dzp->z_lock);

		if (zp->z_uid != dzp->z_uid) {
			uid = dzp->z_uid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &uid, sizeof (uid));
			zp->z_uid = uid;
		}

		if (zp->z_gid != dzp->z_gid) {
			gid = dzp->z_gid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
			    &gid, sizeof (gid));
			zp->z_gid = gid;
		}

		uint64_t projid = dzp->z_projid;
		if (zp->z_projid != projid) {
			if (!(zp->z_pflags & ZFS_PROJID)) {
				err = sa_add_projid(zp->z_sa_hdl, tx, projid);
				if (unlikely(err == EEXIST)) {
					err = 0;
				} else if (err != 0) {
					goto sa_add_projid_err;
				} else {
					projid = ZFS_INVALID_PROJID;
				}
			}

			if (projid != ZFS_INVALID_PROJID) {
				zp->z_projid = projid;
				SA_ADD_BULK_ATTR(bulk, count,
				    SA_ZPL_PROJID(zfsvfs), NULL, &zp->z_projid,
				    sizeof (zp->z_projid));
			}
		}

sa_add_projid_err:
		mutex_exit(&dzp->z_lock);

		if (likely(count > 0)) {
			err = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
			dmu_tx_commit(tx);
		} else if (projid == ZFS_INVALID_PROJID) {
			dmu_tx_commit(tx);
		} else {
			dmu_tx_abort(tx);
		}
		tx = NULL;
		if (err != 0 && err != ENOENT)
			break;

next:
		if (zp) {
			zrele(zp);
			zp = NULL;
		}
		zap_cursor_advance(&zc);
	}

	if (tx)
		dmu_tx_abort(tx);
	if (zp) {
		zrele(zp);
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(zap);

	return (err == ENOENT ? 0 : err);
}

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	zp	- znode of file to be modified.
 *		vap	- new attribute values.
 *			  If AT_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - ctime updated, mtime updated if size changed.
 */
int
zfs_setattr(znode_t *zp, vattr_t *vap, int flags, cred_t *cr)
{
	vnode_t		*vp = ZTOV(zp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		*oldva;
	xvattr_t	*tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask = 0;
	uint64_t	saved_mode;
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_uid, new_gid;
	uint64_t	xattr_obj;
	uint64_t	mtime[2], ctime[2];
	uint64_t	projid = ZFS_INVALID_PROJID;
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t	fuid_dirtied = B_FALSE;
	boolean_t	handle_eadir = B_FALSE;
	sa_bulk_attr_t	*bulk, *xattr_bulk;
	int		count = 0, xattr_count = 0;
	/*
	 * xvattr_t + vattr_t + sa_bulk_attr_t[15] is ~1.5KB on NetBSD
	 * (vattr is wide).  Heap it so the VOP frame stays under 512.
	 */
	struct zfs_setattr_scratch {
		vattr_t		oldva;
		xvattr_t	tmpxvattr;
		sa_bulk_attr_t	bulk[9];
		sa_bulk_attr_t	xattr_bulk[6];
	} *sc = NULL;

	if (mask == 0)
		return (0);

	if (mask & AT_NOSET)
		return (SET_ERROR(EINVAL));

	if ((err = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (err);

	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (((mask & AT_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & AT_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & AT_XVATTR))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (mask & AT_SIZE && vp->v_type == VDIR) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EISDIR));
	}

	if (mask & AT_SIZE && vp->v_type != VREG && vp->v_type != VFIFO) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If this is an xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) &&
	    ((mask & (AT_SIZE|AT_UID|AT_GID|AT_MTIME|AT_MODE)) ||
	    ((mask & AT_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	/*
	 * Note: ZFS_READONLY is handled in zfs_zaccess_common.
	 */

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (AT_ATIME | AT_MTIME)) {
		if (((mask & AT_ATIME) && TIMESPEC_OVERFLOW(&vap->va_atime)) ||
		    ((mask & AT_MTIME) && TIMESPEC_OVERFLOW(&vap->va_mtime))) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EOVERFLOW));
		}
	}
	if (xoap != NULL && (mask & AT_XVATTR)) {
		if (XVA_ISSET_REQ(xvap, XAT_CREATETIME) &&
		    TIMESPEC_OVERFLOW(&vap->va_birthtime)) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EOVERFLOW));
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJID)) {
			if (!dmu_objset_projectquota_enabled(os) ||
			    (!S_ISREG(zp->z_mode) && !S_ISDIR(zp->z_mode))) {
				zfs_exit(zfsvfs, FTAG);
				return (SET_ERROR(EOPNOTSUPP));
			}

			projid = xoap->xoa_projid;
			if (unlikely(projid == ZFS_INVALID_PROJID)) {
				zfs_exit(zfsvfs, FTAG);
				return (SET_ERROR(EINVAL));
			}

			if (projid == zp->z_projid && zp->z_pflags & ZFS_PROJID)
				projid = ZFS_INVALID_PROJID;
			else
				need_policy = TRUE;
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJINHERIT) &&
		    (xoap->xoa_projinherit !=
		    ((zp->z_pflags & ZFS_PROJINHERIT) != 0)) &&
		    (!dmu_objset_projectquota_enabled(os) ||
		    (!S_ISREG(zp->z_mode) && !S_ISDIR(zp->z_mode)))) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EOPNOTSUPP));
		}
	}

	attrzp = NULL;
	aclp = NULL;

	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EROFS));
	}

	/*
	 * First validate permissions
	 */

	if (mask & AT_SIZE) {
		/*
		 * sys_truncate does not apply RLIMIT_FSIZE.  vn_rdwr
		 * does, so `ulimit -f` stops writes and must stop a
		 * growing truncate too (large_files_002_pos).  Skip
		 * kernel-credential growth (vdev replay, xattr).
		 */
		if (vp->v_type == VREG && vap->va_size > zp->z_size &&
		    cr != (cred_t *)kcred && curlwp != NULL &&
		    curlwp->l_proc != NULL &&
		    (rlim_t)vap->va_size >
		    curlwp->l_proc->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
			mutex_enter(&proc_lock);
			psignal(curlwp->l_proc, SIGXFSZ);
			mutex_exit(&proc_lock);
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EFBIG));
		}
		/*
		 * XXX - Note, we are not providing any open
		 * mode flags here (like FNDELAY), so we may
		 * block if there are locks present... this
		 * should be addressed in openat().
		 */
		/* XXX - would it be OK to generate a log record here? */
		err = zfs_freesp(zp, vap->va_size, 0, 0, FALSE);
		if (err) {
			zfs_exit(zfsvfs, FTAG);
			return (err);
		}
	}

	if (mask & (AT_ATIME|AT_MTIME) ||
	    ((mask & AT_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_OFFLINE) ||
	    XVA_ISSET_REQ(xvap, XAT_SPARSE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM)))) {
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr);
	}

	if (mask & (AT_UID|AT_GID)) {
		int	idmask = (mask & (AT_UID|AT_GID));
		int	take_owner;
		int	take_group;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & AT_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		take_owner = (mask & AT_UID) && (vap->va_uid == crgetuid(cr));
		take_group = (mask & AT_GID) &&
		    zfs_groupmember(zfsvfs, vap->va_gid, cr);

		/*
		 * If both AT_UID and AT_GID are set then take_owner and
		 * take_group must both be set in order to allow taking
		 * ownership.
		 *
		 * Otherwise, send the check through secpolicy_vnode_setattr()
		 *
		 */

		if (((idmask == (AT_UID|AT_GID)) && take_owner && take_group) ||
		    ((idmask == AT_UID) && take_owner) ||
		    ((idmask == AT_GID) && take_group)) {
			if (zfs_zaccess(zp, ACE_WRITE_OWNER, 0,
			    skipaclchk, cr) == 0) {
				/*
				 * Remove setuid/setgid for non-privileged users
				 */
				secpolicy_setid_clear(vap, vp, cr);
				trim_mask = (mask & (AT_UID|AT_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	sc = kmem_zalloc(sizeof (*sc), KM_SLEEP);
	oldva = &sc->oldva;
	tmpxvattr = &sc->tmpxvattr;
	bulk = sc->bulk;
	xattr_bulk = sc->xattr_bulk;
	xva_init(tmpxvattr);

	oldva->va_mode = zp->z_mode;
	zfs_fuid_map_ids(zp, cr, &oldva->va_uid, &oldva->va_gid);
	if (mask & AT_XVATTR) {
		/*
		 * Update xvattr mask to include only those attributes
		 * that are actually changing.
		 *
		 * the bits will be restored prior to actually setting
		 * the attributes so the caller thinks they were set.
		 */
		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			if (xoap->xoa_appendonly !=
			    ((zp->z_pflags & ZFS_APPENDONLY) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_APPENDONLY);
				XVA_SET_REQ(tmpxvattr, XAT_APPENDONLY);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJINHERIT)) {
			if (xoap->xoa_projinherit !=
			    ((zp->z_pflags & ZFS_PROJINHERIT) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_PROJINHERIT);
				XVA_SET_REQ(tmpxvattr, XAT_PROJINHERIT);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			if (xoap->xoa_nounlink !=
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NOUNLINK);
				XVA_SET_REQ(tmpxvattr, XAT_NOUNLINK);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			if (xoap->xoa_immutable !=
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_IMMUTABLE);
				XVA_SET_REQ(tmpxvattr, XAT_IMMUTABLE);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			if (xoap->xoa_nodump !=
			    ((zp->z_pflags & ZFS_NODUMP) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NODUMP);
				XVA_SET_REQ(tmpxvattr, XAT_NODUMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			if (xoap->xoa_av_modified !=
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_MODIFIED);
				XVA_SET_REQ(tmpxvattr, XAT_AV_MODIFIED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			if ((vp->v_type != VREG &&
			    xoap->xoa_av_quarantined) ||
			    xoap->xoa_av_quarantined !=
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_QUARANTINED);
				XVA_SET_REQ(tmpxvattr, XAT_AV_QUARANTINED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			kmem_free(sc, sizeof (*sc));
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EPERM));
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}

	if (mask & AT_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, skipaclchk, cr) == 0) {
			err = secpolicy_setid_setsticky_clear(vp, vap,
			    oldva, cr);
			if (err) {
				kmem_free(sc, sizeof (*sc));
				zfs_exit(zfsvfs, FTAG);
				return (err);
			}
			trim_mask |= AT_MODE;
		} else {
			need_policy = TRUE;
		}
	}

	if (need_policy) {
		/*
		 * If trim_mask is set then take ownership
		 * has been granted or write_acl is present and user
		 * has the ability to modify mode.  In that case remove
		 * UID|GID and or MODE from mask so that
		 * secpolicy_vnode_setattr() doesn't revoke it.
		 */

		if (trim_mask) {
			saved_mask = vap->va_mask;
			vap->va_mask &= ~trim_mask;
			if (trim_mask & AT_MODE) {
				/*
				 * Save the mode, as secpolicy_vnode_setattr()
				 * will overwrite it with ova.va_mode.
				 */
				saved_mode = vap->va_mode;
			}
		}
		err = secpolicy_vnode_setattr(cr, vp, vap, oldva, flags,
		    (int (*)(void *, int, cred_t *))zfs_zaccess_unix, zp);
		if (err) {
			kmem_free(sc, sizeof (*sc));
			zfs_exit(zfsvfs, FTAG);
			return (err);
		}

		if (trim_mask) {
			vap->va_mask |= saved_mask;
			if (trim_mask & AT_MODE) {
				/*
				 * Recover the mode after
				 * secpolicy_vnode_setattr().
				 */
				vap->va_mode = saved_mode;
			}
		}
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	if ((mask & (AT_UID | AT_GID)) || projid != ZFS_INVALID_PROJID) {
		handle_eadir = B_TRUE;
		err = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj, sizeof (xattr_obj));

		if (err == 0 && xattr_obj) {
			err = zfs_zget(zp->z_zfsvfs, xattr_obj, &attrzp);
			if (err == 0) {
				err = vn_lock(ZTOV(attrzp), LK_EXCLUSIVE);
				if (err != 0)
					vrele(ZTOV(attrzp));
			}
			if (err)
				goto out2;
		}
		if (mask & AT_UID) {
			new_uid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_uid != zp->z_uid &&
			    zfs_id_overquota(zfsvfs, DMU_USERUSED_OBJECT,
			    new_uid)) {
				if (attrzp)
					vput(ZTOV(attrzp));
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (mask & AT_GID) {
			new_gid = zfs_fuid_create(zfsvfs, (uint64_t)vap->va_gid,
			    cr, ZFS_GROUP, &fuidp);
			if (new_gid != zp->z_gid &&
			    zfs_id_overquota(zfsvfs, DMU_GROUPUSED_OBJECT,
			    new_gid)) {
				if (attrzp)
					vput(ZTOV(attrzp));
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (projid != ZFS_INVALID_PROJID &&
		    zfs_id_overquota(zfsvfs, DMU_PROJECTUSED_OBJECT, projid)) {
			if (attrzp)
				vput(ZTOV(attrzp));
			err = SET_ERROR(EDQUOT);
			goto out2;
		}
	}
	tx = dmu_tx_create(os);

	if (mask & AT_MODE) {
		uint64_t pmode = zp->z_mode;
		uint64_t acl_obj;
		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		if (zp->z_zfsvfs->z_acl_mode == ZFS_ACL_RESTRICTED &&
		    !(zp->z_pflags & ZFS_ACL_TRIVIAL)) {
			err = SET_ERROR(EPERM);
			goto out;
		}

		if ((err = zfs_acl_chmod_setattr(zp, &aclp, new_mode)))
			goto out;

		if (!zp->z_is_sa && ((acl_obj = zfs_external_acl(zp)) != 0)) {
			/*
			 * Are we upgrading ACL from old V0 format
			 * to V1 format?
			 */
			if (zfsvfs->z_version >= ZPL_VERSION_FUID &&
			    zfs_znode_acl_version(zp) ==
			    ZFS_ACL_VERSION_INITIAL) {
				dmu_tx_hold_free(tx, acl_obj, 0,
				    DMU_OBJECT_END);
				dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
				    0, aclp->z_acl_bytes);
			} else {
				dmu_tx_hold_write(tx, acl_obj, 0,
				    aclp->z_acl_bytes);
			}
		} else if (!zp->z_is_sa && aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, aclp->z_acl_bytes);
		}
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	} else {
		if (((mask & AT_XVATTR) &&
		    XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) ||
		    (projid != ZFS_INVALID_PROJID &&
		    !(zp->z_pflags & ZFS_PROJID)) ||
		    !zp->z_has_seq)
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	}

	if (attrzp) {
		dmu_tx_hold_sa(tx, attrzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(attrzp));
	}

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);

	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err)
		goto out;

	count = 0;
	/*
	 * Set each attribute requested.
	 * We group settings according to the locks they need to acquire.
	 *
	 * Note: you cannot set ctime directly, although it will be
	 * updated as a side-effect of calling this function.
	 */

	if (projid != ZFS_INVALID_PROJID && !(zp->z_pflags & ZFS_PROJID)) {
		/*
		 * For the existed object that is upgraded from old system,
		 * its on-disk layout has no slot for the project ID attribute.
		 * But quota accounting logic needs to access related slots by
		 * offset directly. So we need to adjust old objects' layout
		 * to make the project ID to some unified and fixed offset.
		 */
		if (attrzp)
			err = sa_add_projid(attrzp->z_sa_hdl, tx, projid);
		if (err == 0)
			err = sa_add_projid(zp->z_sa_hdl, tx, projid);

		if (unlikely(err == EEXIST))
			err = 0;
		else if (err != 0)
			goto out;
		else
			projid = ZFS_INVALID_PROJID;
	}

	if (mask & (AT_UID|AT_GID|AT_MODE))
		mutex_enter(&zp->z_acl_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		if (mask & (AT_UID|AT_GID|AT_MODE))
			mutex_enter(&attrzp->z_acl_lock);
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_FLAGS(zfsvfs), NULL, &attrzp->z_pflags,
		    sizeof (attrzp->z_pflags));
		if (projid != ZFS_INVALID_PROJID) {
			attrzp->z_projid = projid;
			SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
			    SA_ZPL_PROJID(zfsvfs), NULL, &attrzp->z_projid,
			    sizeof (attrzp->z_projid));
		}
	}

	if (mask & (AT_UID|AT_GID)) {

		if (mask & AT_UID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &new_uid, sizeof (new_uid));
			zp->z_uid = new_uid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_UID(zfsvfs), NULL, &new_uid,
				    sizeof (new_uid));
				attrzp->z_uid = new_uid;
			}
		}

		if (mask & AT_GID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs),
			    NULL, &new_gid, sizeof (new_gid));
			zp->z_gid = new_gid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_GID(zfsvfs), NULL, &new_gid,
				    sizeof (new_gid));
				attrzp->z_gid = new_gid;
			}
		}
		if (!(mask & AT_MODE)) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs),
			    NULL, &new_mode, sizeof (new_mode));
			new_mode = zp->z_mode;
		}
		err = zfs_acl_chown_setattr(zp);
		ASSERT0(err);
		if (attrzp) {
			err = zfs_acl_chown_setattr(attrzp);
			ASSERT0(err);
		}
	}

	if (mask & AT_MODE) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
		    &new_mode, sizeof (new_mode));
		zp->z_mode = new_mode;
		ASSERT3P(aclp, !=, NULL);
		err = zfs_aclset_common(zp, aclp, cr, tx);
		ASSERT0(err);
		if (zp->z_acl_cached)
			zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = aclp;
		aclp = NULL;
	}

	if (mask & AT_ATIME) {
		ZFS_TIME_ENCODE(&vap->va_atime, zp->z_atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
		    &zp->z_atime, sizeof (zp->z_atime));
	}

	if (mask & AT_MTIME) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    mtime, sizeof (mtime));
	}

	if (projid != ZFS_INVALID_PROJID) {
		zp->z_projid = projid;
		SA_ADD_BULK_ATTR(bulk, count,
		    SA_ZPL_PROJID(zfsvfs), NULL, &zp->z_projid,
		    sizeof (zp->z_projid));
	}

	/* XXX - shouldn't this be done *before* the ATIME/MTIME checks? */
	if (mask & AT_SIZE && !(mask & AT_MTIME)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
		    NULL, mtime, sizeof (mtime));
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
		ZFS_PERSIST_SEQ(zp, bulk, count);
	} else if (mask != 0) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime, ctime);
		ZFS_PERSIST_SEQ(zp, bulk, count);
		if (attrzp) {
			SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
			    SA_ZPL_CTIME(zfsvfs), NULL,
			    &ctime, sizeof (ctime));
			zfs_tstamp_update_setup(attrzp, STATE_CHANGED,
			    mtime, ctime);
			ZFS_PERSIST_SEQ(attrzp, xattr_bulk, xattr_count);
		}
	}

	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */

	if (xoap && (mask & AT_XVATTR)) {

		if (XVA_ISSET_REQ(xvap, XAT_CREATETIME))
			xoap->xoa_createtime = vap->va_birthtime;
		/*
		 * restore trimmed off masks
		 * so that return masks can be set for caller.
		 */

		if (XVA_ISSET_REQ(tmpxvattr, XAT_APPENDONLY)) {
			XVA_SET_REQ(xvap, XAT_APPENDONLY);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_NOUNLINK)) {
			XVA_SET_REQ(xvap, XAT_NOUNLINK);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_IMMUTABLE)) {
			XVA_SET_REQ(xvap, XAT_IMMUTABLE);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_NODUMP)) {
			XVA_SET_REQ(xvap, XAT_NODUMP);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_AV_MODIFIED)) {
			XVA_SET_REQ(xvap, XAT_AV_MODIFIED);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_AV_QUARANTINED)) {
			XVA_SET_REQ(xvap, XAT_AV_QUARANTINED);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_PROJINHERIT)) {
			XVA_SET_REQ(xvap, XAT_PROJINHERIT);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			ASSERT3S(vp->v_type, ==, VREG);

		zfs_xvattr_set(zp, xvap, tx);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0)
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);

	if (mask & (AT_UID|AT_GID|AT_MODE))
		mutex_exit(&zp->z_acl_lock);

	if (attrzp) {
		if (mask & (AT_UID|AT_GID|AT_MODE))
			mutex_exit(&attrzp->z_acl_lock);
	}
	if (err == 0 && (mask & AT_UID))
		zfs_netbsd_posix_acl_purge(zp);
out:
	if (err == 0 && attrzp) {
		ASSERT3S(xattr_count, <=, 6);
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		if (err2 != 0 && err == 0)
			err = err2;
	}

	if (aclp)
		zfs_acl_free(aclp);

	if (fuidp) {
		zfs_fuid_info_free(fuidp);
		fuidp = NULL;
	}

	if (err) {
		dmu_tx_abort(tx);
		if (attrzp)
			vput(ZTOV(attrzp));
	} else {
		ASSERT3S(count, <=, 9);
		err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
		if (attrzp) {
			if (err2 == 0 && handle_eadir)
				err = zfs_setattr_dir(attrzp);
			vput(ZTOV(attrzp));
		}
	}

out2:
	if (sc != NULL)
		kmem_free(sc, sizeof (*sc));
	if (err == 0 && os->os_sync == ZFS_SYNC_ALWAYS)
		err = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (err);
}

/*
 * Look up the directory entries corresponding to the source and target
 * directory/name pairs.
 */
static int
zfs_rename_relock_lookup(znode_t *sdzp, const struct componentname *scnp,
    znode_t **szpp, znode_t *tdzp, const struct componentname *tcnp,
    znode_t **tzpp)
{
	zfsvfs_t *zfsvfs;
	znode_t *szp = NULL, *tzp = NULL;
	int error;

	*szpp = NULL;
	*tzpp = NULL;

	/*
	 * Before using sdzp and tdzp we must ensure that they are live.
	 * As a porting legacy from illumos we have two things to worry
	 * about.  One is typical for FreeBSD and it is that the vnode is
	 * not reclaimed (doomed).  The other is that the znode is live.
	 * The current code can invalidate the znode without acquiring the
	 * corresponding vnode lock if the object represented by the znode
	 * and vnode is no longer valid after a rollback or receive operation.
	 * z_teardown_lock hidden behind zfs_enter and zfs_exit is the lock
	 * that protects the znodes from the invalidation.
	 */
	zfsvfs = sdzp->z_zfsvfs;
	ASSERT3P(zfsvfs, ==, tdzp->z_zfsvfs);
	if ((error = zfs_enter_verify_zp(zfsvfs, sdzp, FTAG)) != 0)
		return (error);
	if ((error = zfs_verify_zp(tdzp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Re-resolve svp to be certain it still exists and fetch the
	 * correct vnode.
	 */
	error = zfs_dirent_lookup(sdzp, scnp->cn_nameptr, &szp, ZEXISTS);
	if (error != 0) {
		/* Source entry invalid or not there. */
		if ((scnp->cn_flags & ISDOTDOT) != 0 ||
		    (scnp->cn_namelen == 1 && scnp->cn_nameptr[0] == '.'))
			error = SET_ERROR(EINVAL);
		goto out;
	}
	*szpp = szp;

	/*
	 * Re-resolve tvp, if it disappeared we just carry on.
	 */
	error = zfs_dirent_lookup(tdzp, tcnp->cn_nameptr, &tzp, 0);
	if (error != 0) {
		vrele(ZTOV(szp));
		if ((tcnp->cn_flags & ISDOTDOT) != 0)
			error = SET_ERROR(EINVAL);
		goto out;
	}
	*tzpp = tzp;
out:
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * We acquire all but fdvp locks using non-blocking acquisitions.  If we
 * fail to acquire any lock in the path we will drop all held locks,
 * acquire the new lock in a blocking fashion, and then release it and
 * restart the rename.  This acquire/release step ensures that we do not
 * spin on a lock waiting for release.  On error release all vnode locks
 * and decrement references the way tmpfs_rename() would do.
 *
 * NetBSD vnode locks are not recursive.  Skip vn_lock when the vnode is
 * already one of the held rename vnodes (same-directory rename has
 * sdvp == tdvp; locking it twice livelocks or panics).
 */
static int
zfs_rename_relock(struct vnode *sdvp, struct vnode **svpp,
    struct vnode *tdvp, struct vnode **tvpp,
    const struct componentname *scnp, const struct componentname *tcnp)
{
	struct vnode	*nvp, *svp, *tvp;
	znode_t		*sdzp, *tdzp, *szp = NULL, *tzp = NULL;
	int		error;

	VOP_UNLOCK(tdvp);
	if (*tvpp != NULL && *tvpp != tdvp)
		VOP_UNLOCK(*tvpp);

relock:
	error = vn_lock(sdvp, LK_EXCLUSIVE);
	if (error)
		goto out;
	if (tdvp != sdvp) {
		error = vn_lock(tdvp, LK_EXCLUSIVE | LK_NOWAIT);
		if (error != 0) {
			VOP_UNLOCK(sdvp);
			if (error != EBUSY)
				goto out;
			error = vn_lock(tdvp, LK_EXCLUSIVE);
			if (error)
				goto out;
			VOP_UNLOCK(tdvp);
			goto relock;
		}
	}
	tdzp = VTOZ(tdvp);
	sdzp = VTOZ(sdvp);

	error = zfs_rename_relock_lookup(sdzp, scnp, &szp, tdzp, tcnp, &tzp);
	if (error != 0) {
		VOP_UNLOCK(sdvp);
		if (tdvp != sdvp)
			VOP_UNLOCK(tdvp);
		goto out;
	}
	svp = ZTOV(szp);
	tvp = tzp != NULL ? ZTOV(tzp) : NULL;

	/*
	 * Now try acquire locks on svp and tvp.
	 */
	nvp = svp;
	if (nvp != sdvp && nvp != tdvp) {
		error = vn_lock(nvp, LK_EXCLUSIVE | LK_NOWAIT);
		if (error != 0) {
			VOP_UNLOCK(sdvp);
			if (tdvp != sdvp)
				VOP_UNLOCK(tdvp);
			if (tvp != NULL)
				vrele(tvp);
			if (error != EBUSY) {
				vrele(nvp);
				goto out;
			}
			error = vn_lock(nvp, LK_EXCLUSIVE);
			if (error != 0) {
				vrele(nvp);
				goto out;
			}
			VOP_UNLOCK(nvp);
			/*
			 * Concurrent rename race.
			 * XXX ?
			 */
			if (nvp == tdvp) {
				vrele(nvp);
				error = SET_ERROR(EINVAL);
				goto out;
			}
			vrele(*svpp);
			*svpp = nvp;
			goto relock;
		}
	}
	vrele(*svpp);
	*svpp = nvp;

	if (*tvpp != NULL)
		vrele(*tvpp);
	*tvpp = NULL;
	if (tvp != NULL) {
		nvp = tvp;
		if (nvp != sdvp && nvp != tdvp && nvp != *svpp) {
			error = vn_lock(nvp, LK_EXCLUSIVE | LK_NOWAIT);
			if (error != 0) {
				VOP_UNLOCK(sdvp);
				if (tdvp != sdvp)
					VOP_UNLOCK(tdvp);
				if (*svpp != sdvp && *svpp != tdvp)
					VOP_UNLOCK(*svpp);
				if (error != EBUSY) {
					vrele(nvp);
					goto out;
				}
				error = vn_lock(nvp, LK_EXCLUSIVE);
				if (error != 0) {
					vrele(nvp);
					goto out;
				}
				vput(nvp);
				goto relock;
			}
		}
		*tvpp = nvp;
	}

	return (0);

out:
	return (error);
}

/*
 * Drop exclusive locks taken for rename.  Exact complement of the
 * predicates in zfs_rename_relock():
 *   sdvp always locked
 *   tdvp locked if tdvp != sdvp
 *   svp  locked if svp != sdvp && svp != tdvp
 *   tvp  locked if tvp != NULL && tvp != sdvp && tvp != tdvp && tvp != svp
 */
static void
zfs_rename_unlock(vnode_t *sdvp, vnode_t *svp, vnode_t *tdvp, vnode_t *tvp)
{
	if (tvp != NULL && tvp != sdvp && tvp != tdvp && tvp != svp)
		VOP_UNLOCK(tvp);
	if (svp != NULL && svp != sdvp && svp != tdvp)
		VOP_UNLOCK(svp);
	if (tdvp != sdvp)
		VOP_UNLOCK(tdvp);
	if (sdvp != NULL)
		VOP_UNLOCK(sdvp);
}

/*
 * Note that we must use VRELE_ASYNC in this function as it walks
 * up the directory tree and vrele may need to acquire an exclusive
 * lock if a last reference to a vnode is dropped.
 */
static int
zfs_rename_check(znode_t *szp, znode_t *sdzp, znode_t *tdzp)
{
	zfsvfs_t	*zfsvfs;
	znode_t		*zp, *zp1;
	uint64_t	parent;
	int		error;

	zfsvfs = tdzp->z_zfsvfs;
	if (tdzp == szp)
		return (SET_ERROR(EINVAL));
	if (tdzp == sdzp)
		return (0);
	if (tdzp->z_id == zfsvfs->z_root)
		return (0);
	zp = tdzp;
	for (;;) {
		ASSERT(!zp->z_unlinked);
		if ((error = sa_lookup(zp->z_sa_hdl,
		    SA_ZPL_PARENT(zfsvfs), &parent, sizeof (parent))) != 0)
			break;

		if (parent == szp->z_id) {
			error = SET_ERROR(EINVAL);
			break;
		}
		if (parent == zfsvfs->z_root)
			break;
		if (parent == sdzp->z_id)
			break;

		error = zfs_zget(zfsvfs, parent, &zp1);
		if (error != 0)
			break;

		if (zp != tdzp)
			VN_RELE_ASYNC(ZTOV(zp),
			    dsl_pool_zrele_taskq(
			    dmu_objset_pool(zfsvfs->z_os)));
		zp = zp1;
	}

	if (error == ENOTDIR)
		panic("checkpath: .. not a directory\n");
	if (zp != tdzp)
		VN_RELE_ASYNC(ZTOV(zp),
		    dsl_pool_zrele_taskq(dmu_objset_pool(zfsvfs->z_os)));
	return (error);
}

static int
zfs_do_rename_impl(vnode_t *sdvp, vnode_t **svpp, struct componentname *scnp,
    vnode_t *tdvp, vnode_t **tvpp, struct componentname *tcnp,
    cred_t *cr);

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdvp	- Source directory containing the "old entry".
 *		scnp	- Old entry name.
 *		tdvp	- Target directory to contain the "new entry".
 *		tcnp	- New entry name.
 *		cr	- credentials of caller.
 *	INOUT:	svpp	- Source file
 *		tvpp	- Target file, may point to NULL initially
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	sdvp,tdvp - ctime|mtime updated
 */
int
zfs_do_rename(vnode_t *sdvp, vnode_t **svpp, struct componentname *scnp,
    vnode_t *tdvp, vnode_t **tvpp, struct componentname *tcnp,
    cred_t *cr)
{
	int	error;

	ASSERT_VOP_ELOCKED(tdvp, __func__);
	if (*tvpp != NULL)
		ASSERT_VOP_ELOCKED(*tvpp, __func__);

	/* Reject renames across filesystems. */
	if ((*svpp)->v_mount != tdvp->v_mount ||
	    ((*tvpp) != NULL && (*svpp)->v_mount != (*tvpp)->v_mount)) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	if (zfsctl_is_node(tdvp)) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	/*
	 * Lock all four vnodes to ensure safety and semantics of renaming.
	 */
	error = zfs_rename_relock(sdvp, svpp, tdvp, tvpp, scnp, tcnp);
	if (error != 0) {
		/* no vnodes are locked in the case of error here */
		return (error);
	}

	error = zfs_do_rename_impl(sdvp, svpp, scnp, tdvp, tvpp, tcnp, cr);
	zfs_rename_unlock(sdvp, *svpp, tdvp, *tvpp);
	return (error);

out:
	if (*tvpp != NULL)
		VOP_UNLOCK(*tvpp);
	if (tdvp != *tvpp)
		VOP_UNLOCK(tdvp);

	return (error);
}

static int
zfs_do_rename_impl(vnode_t *sdvp, vnode_t **svpp, struct componentname *scnp,
    vnode_t *tdvp, vnode_t **tvpp, struct componentname *tcnp,
    cred_t *cr)
{
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs;
	zilog_t		*zilog;
	znode_t		*tdzp, *sdzp, *tzp, *szp;
	znode_t		*azp = NULL;
	const char	*snm = scnp->cn_nameptr;
	const char	*tnm = tcnp->cn_nameptr;
	int		error;

	tdzp = VTOZ(tdvp);
	sdzp = VTOZ(sdvp);
	zfsvfs = tdzp->z_zfsvfs;

	if ((error = zfs_enter_verify_zp(zfsvfs, tdzp, FTAG)) != 0)
		return (error);
	if ((error = zfs_verify_zp(sdzp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	/* If source and target are the same file, there is nothing to do. */
	if ((*svpp) == (*tvpp)) {
		error = 0;
		goto out;
	}

	if (((*svpp)->v_type == VDIR && (*svpp)->v_mountedhere != NULL) ||
	    ((*tvpp) != NULL && (*tvpp)->v_type == VDIR &&
	    (*tvpp)->v_mountedhere != NULL)) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	szp = VTOZ(*svpp);
	if ((error = zfs_verify_zp(szp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	tzp = *tvpp == NULL ? NULL : VTOZ(*tvpp);
	if (tzp != NULL) {
		if ((error = zfs_verify_zp(tzp)) != 0) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

	/*
	 * This is to prevent the creation of links into attribute space
	 * by renaming a linked file into/outof an attribute directory.
	 * See the comment in zfs_link() for why this is considered bad.
	 */
	if ((tdzp->z_pflags & ZFS_XATTR) != (sdzp->z_pflags & ZFS_XATTR)) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * If we are using project inheritance, means if the directory has
	 * ZFS_PROJINHERIT set, then its descendant directories will inherit
	 * not only the project ID, but also the ZFS_PROJINHERIT flag. Under
	 * such case, we only allow renames into our tree when the project
	 * IDs are the same.
	 *
	 * A rename within a single directory leaves the object exactly where
	 * it already is, so it cannot move it between projects and is always
	 * allowed.  Objects created before symlinks and other non-regular
	 * files began inheriting a project ID carry none of their own, and
	 * would otherwise not be renameable within the very directory that
	 * holds them -- which breaks "ln -sfn", implemented as
	 * create-under-a-temporary-name-then-rename.
	 */
	if (sdzp != tdzp && tdzp->z_pflags & ZFS_PROJINHERIT &&
	    tdzp->z_projid != szp->z_projid) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */
	if ((error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, cr)))
		goto out;

	if ((*svpp)->v_type == VDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((scnp->cn_namelen == 1 && scnp->cn_nameptr[0] == '.') ||
		    sdzp == szp ||
		    (scnp->cn_flags | tcnp->cn_flags) & ISDOTDOT) {
			error = EINVAL;
			goto out;
		}

		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if ((error = zfs_rename_check(szp, sdzp, tdzp)))
			goto out;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		/*
		 * Source and target must be the same type.
		 */
		if ((*svpp)->v_type == VDIR) {
			if ((*tvpp)->v_type != VDIR) {
				error = SET_ERROR(ENOTDIR);
				goto out;
			} else {
				cache_purge(tdvp);
				if (sdvp != tdvp)
					cache_purge(sdvp);
			}
		} else {
			if ((*tvpp)->v_type == VDIR) {
				error = SET_ERROR(EISDIR);
				goto out;
			}
		}
	}

	if ((tdzp->z_pflags & ZFS_XATTR) != 0)
		(void) zfs_xattr_owner_hold(tdzp, &azp);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, ZFS_SEQ_MAY_GROW(szp));
	dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(sdzp));
	dmu_tx_hold_zap(tx, sdzp->z_id, FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp) {
		dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(tdzp));
		zfs_sa_upgrade_txholds(tx, tdzp);
	}
	if (tzp) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(tzp));
		zfs_sa_upgrade_txholds(tx, tzp);
	}
	if (azp != NULL)
		dmu_tx_hold_sa(tx, azp->z_sa_hdl, ZFS_SEQ_MAY_GROW(azp));

	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	if (tzp)	/* Attempt to remove the existing target */
		error = zfs_link_destroy(tdzp, tnm, tzp, tx, 0, NULL);

	if (error == 0) {
		error = zfs_link_create(tdzp, tnm, szp, tx, ZRENAMING);
		if (error == 0) {
			szp->z_pflags |= ZFS_AV_MODIFIED;

			error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
			    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);

			error = zfs_link_destroy(sdzp, snm, szp, tx, ZRENAMING,
			    NULL);
			if (error == 0) {
				zfs_log_rename(zilog, tx, TX_RENAME, sdzp,
				    snm, tdzp, tnm, szp);
			} else {
				/*
				 * At this point, we have successfully created
				 * the target name, but have failed to remove
				 * the source name.  Since the create was done
				 * with the ZRENAMING flag, there are
				 * complications; for one, the link count is
				 * wrong.  The easiest way to deal with this
				 * is to remove the newly created target, and
				 * return the original error.  This must
				 * succeed; fortunately, it is very unlikely to
				 * fail, since we just created it.
				 */
				VERIFY0(zfs_link_destroy(tdzp, tnm, szp, tx,
				    ZRENAMING, NULL));
			}
		}
		if (error == 0 && zfsvfs->z_use_namecache) {
			cache_purge(*svpp);
			if (*tvpp != NULL)
				cache_purge(*tvpp);
			cache_enter(tdvp, *svpp, tcnp->cn_nameptr,
			    tcnp->cn_namelen, tcnp->cn_flags);
		}
	}

	if (error == 0)
		zfs_xattr_owner_ctime(azp, tx);

	dmu_tx_commit(tx);

out:
	zfs_xattr_owner_rele(azp);
	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);
	zfs_exit(zfsvfs, FTAG);

	return (error);
}

int
zfs_rename(znode_t *sdzp, const char *sname, znode_t *tdzp, const char *tname,
    cred_t *cr, int flags, uint64_t rflags, vattr_t *wo_vap)
{
	struct componentname scn, tcn;
	vnode_t *sdvp, *tdvp;
	vnode_t *svp, *tvp;
	int error;
	svp = tvp = NULL;

	if (is_nametoolong(tdzp->z_zfsvfs, tname))
		return (SET_ERROR(ENAMETOOLONG));

	if (rflags != 0 || wo_vap != NULL)
		return (SET_ERROR(EINVAL));

	sdvp = ZTOV(sdzp);
	tdvp = ZTOV(tdzp);
	error = zfs_lookup_internal(sdzp, sname, &svp, &scn, DELETE);
	if (error != 0)
		goto fail;
	/*
	 * zfs_lookup() returns a referenced, unlocked vnode (unless it
	 * aliases a directory this thread already holds).  VOP_RENAME
	 * does not come through here.  The only caller is ZIL replay,
	 * which does not hold sdvp.  VOP_UNLOCK(svp) on that unlocked
	 * vnode rw_exits a free vi_lock and the later vn_lock in
	 * zfs_rename_relock sleeps forever (slog_replay_fs_001).
	 */
	if (sdzp->z_zfsvfs->z_replay == B_FALSE && VOP_ISLOCKED(sdvp))
		VOP_UNLOCK(sdvp);

	vn_lock(tdvp, LK_EXCLUSIVE | LK_RETRY);
	error = zfs_lookup_internal(tdzp, tname, &tvp, &tcn, RENAME);
	if (error == EJUSTRETURN)
		tvp = NULL;
	else if (error != 0) {
		VOP_UNLOCK(tdvp);
		goto fail;
	}
	/*
	 * zfs_do_rename() requires tvp exclusive-locked.  NetBSD
	 * lookup does not lock it.
	 */
	if (tvp != NULL && !VOP_ISLOCKED(tvp)) {
		error = vn_lock(tvp, LK_EXCLUSIVE | LK_RETRY);
		if (error != 0) {
			VOP_UNLOCK(tdvp);
			goto fail;
		}
	}

	error = zfs_do_rename(sdvp, &svp, &scn, tdvp, &tvp, &tcn, cr);
fail:
	if (svp != NULL)
		vrele(svp);
	if (tvp != NULL)
		vrele(tvp);

	return (error);
}

/*
 * Insert the indicated symbolic reference entry into the directory.
 *
 *	IN:	dvp	- Directory to contain new symbolic link.
 *		link	- Name for new symlink entry.
 *		vap	- Attributes of new entry.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
int
zfs_symlink(znode_t *dzp, const char *name, vattr_t *vap,
    const char *link, znode_t **zpp, cred_t *cr, int flags)
{
	(void) flags;
	znode_t		*zp;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	len = strlen(link);
	int		error;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;
	uint64_t	txtype = TX_SYMLINK;

	ASSERT3S(vap->va_type, ==, VLNK);

	if (is_nametoolong(zfsvfs, name))
		return (SET_ERROR(ENAMETOOLONG));

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	if (len > MAXPATHLEN) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lookup(dzp, name, &zp, ZNEW);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, zfs_inherit_projid(dzp))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EDQUOT));
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datasets the symlink will be an SA attribute
	 */
	error = zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);
	if (error != 0) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    __DECONST(void *, link), len, tx);
	else
		zfs_sa_symlink(zp, __DECONST(char *, link), len, tx);

	zp->z_size = len;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);
	/*
	 * Insert the new object into the directory.
	 */
	error = zfs_link_create(dzp, name, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
		VOP_UNLOCK(ZTOV(zp));
		zrele(zp);
	} else {
		zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);
	}

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	if (error == 0) {
		*zpp = zp;

		if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
			error = zil_commit(zilog, 0);
	}

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Return, in the buffer contained in the provided uio structure,
 * the symbolic path referred to by vp.
 *
 *	IN:	vp	- vnode of symbolic link.
 *		uio	- structure to contain the link path.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- structure containing the link path.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - atime updated
 */
int
zfs_readlink(vnode_t *vp, zfs_uio_t *uio, cred_t *cr, caller_context_t *ct)
{
	(void) cr, (void) ct;
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), uio);
	else
		error = zfs_sa_readlink(zp, uio);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Insert a new entry into directory tdvp referencing svp.
 *
 *	IN:	tdvp	- Directory to contain new entry.
 *		svp	- vnode of new entry.
 *		name	- name of new entry.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	tdvp - ctime|mtime updated
 *	 svp - ctime updated
 */
int
zfs_link(znode_t *tdzp, znode_t *szp, const char *name, cred_t *cr,
    int flags)
{
	(void) flags;
	znode_t		*tzp;
	zfsvfs_t	*zfsvfs = tdzp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	int		error;
	uint64_t	parent;
	uid_t		owner;

	ASSERT3S(ZTOV(tdzp)->v_type, ==, VDIR);

	if (is_nametoolong(zfsvfs, name))
		return (SET_ERROR(ENAMETOOLONG));

	if ((error = zfs_enter_verify_zp(zfsvfs, tdzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (ZTOV(szp)->v_type == VDIR) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_verify_zp(szp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * If we are using project inheritance, means if the directory has
	 * ZFS_PROJINHERIT set, then its descendant directories will inherit
	 * not only the project ID, but also the ZFS_PROJINHERIT flag. Under
	 * such case, we only allow hard link creation in our tree when the
	 * project IDs are the same.
	 */
	if (tdzp->z_pflags & ZFS_PROJINHERIT &&
	    tdzp->z_projid != szp->z_projid) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EXDEV));
	}

	if (szp->z_pflags & (ZFS_APPENDONLY |
	    ZFS_IMMUTABLE | ZFS_READONLY)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	/* Prevent links to .zfs/shares files */

	if ((error = sa_lookup(szp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (uint64_t))) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	if (parent == zfsvfs->z_shares_dir) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if (zfsvfs->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_pflags & ZFS_XATTR) != (tdzp->z_pflags & ZFS_XATTR)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	owner = zfs_fuid_map_id(zfsvfs, szp->z_uid, cr, ZFS_OWNER);
	if (owner != crgetuid(cr) && secpolicy_basic_link(ZTOV(szp), cr) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_zaccess(tdzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lookup(tdzp, name, &tzp, ZNEW);
	if (error) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, ZFS_SEQ_MAY_GROW(szp));
	dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(tdzp));
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, name);
	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, tdzp);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	error = zfs_link_create(tdzp, name, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
		zfs_log_link(zilog, tx, txtype, tdzp, szp, name);
	}

	dmu_tx_commit(tx);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Free or allocate space in a file.  Currently, this function only
 * supports the `F_FREESP' command.  However, this command is somewhat
 * misnamed, as its functionality includes the ability to allocate as
 * well as free space.
 *
 *	IN:	ip	- inode of file to free data in.
 *		cmd	- action to take (only F_FREESP supported).
 *		bfp	- section of file to free/alloc.
 *		flag	- current file open mode flags.
 *		offset	- current file offset.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	ip - ctime|mtime updated
 */
int
zfs_space(znode_t *zp, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr)
{
	(void) offset;
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	uint64_t	off, len;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if (cmd != F_FREESP) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Callers might not be able to detect properly that we are read-only,
	 * so check it explicitly here.
	 */
	if (zfs_is_readonly(zfsvfs)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EROFS));
	}

	if (bfp->l_len < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Permissions aren't checked on Solaris because on this OS
	 * zfs_space() can only be called with an opened file handle.
	 * On Linux we can get here through truncate_range() which
	 * operates directly on inodes, so we need to check access rights.
	 */
	if ((error = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	off = bfp->l_start;
	len = bfp->l_len; /* 0 means from off to end of file */

	error = zfs_freesp(zp, off, len, flag, TRUE);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * VOP wrappers (zfs_netbsd_*) and vnodeopv vectors.  Same layout as
 * FreeBSD's zfs_vnops_os.c: ZPL helpers above, then OS VOP wrappers
 * (zfs_freebsd_* there) and the op tables.
 */

int (**zfs_vnodeop_p)(void *);
int (**zfs_specop_p)(void *);
int (**zfs_fifoop_p)(void *);

static int zfs_netbsd_xattr_vnode(vnode_t *, const char *, int, cred_t *,
    vnode_t **);
int zfs_netbsd_posix_access(vnode_t *, accmode_t, cred_t *);
void zfs_netbsd_posix_acl_purge(znode_t *);
static int zfs_netbsd_posix_inherit(vnode_t *, vnode_t *, cred_t *);

#define	ZFS_NETBSD_POSIX_ACL_MODE	((void *)(uintptr_t)1)

static caddr_t
zfs_pager_map(struct vm_page *pp, int rw)
{
	vaddr_t va;
	int flags;

#ifdef __HAVE_MM_MD_DIRECT_MAPPED_PHYS
	if (mm_md_direct_mapped_phys(VM_PAGE_TO_PHYS(pp), &va))
		return ((caddr_t)va);
#endif
#ifndef S_READ
#define	S_READ	1
#define	S_WRITE	2
#endif
	/* S_WRITE fills the page.  MAPIN_READ is the writable mapping. */
	flags = UVMPAGER_MAPIN_WAITOK |
	    (rw == S_READ ? UVMPAGER_MAPIN_WRITE : UVMPAGER_MAPIN_READ);
	va = uvm_pagermapin(&pp, 1, flags);
	return ((caddr_t)va);
}

static void
zfs_pager_unmap(struct vm_page *pp, caddr_t addr)
{
#ifdef __HAVE_MM_MD_DIRECT_MAPPED_PHYS
	vaddr_t va;

	if (mm_md_direct_mapped_phys(VM_PAGE_TO_PHYS(pp), &va))
		return;
#endif
	(void) pp;
	uvm_pagermapout((vaddr_t)addr, 1);
}

static boolean_t zfs_nb_rw_active(void);
static boolean_t zfs_nb_nofault_active(void);

static int
zfs_netbsd_getpages(void *v)
{
	struct vop_getpages_args *ap = v;
	vnode_t *vp = ap->a_vp;
	const int flags = ap->a_flags;
	const bool memwrite = (ap->a_access_type & VM_PROT_WRITE) != 0;
	struct uvm_object *uobj = &vp->v_uobj;
	krwlock_t *rw = uobj->vmobjlock;
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	vfs_t *mp;
	zfs_locked_range_t *rl = NULL;
	struct vm_page *pg;
	caddr_t va;
	int npages = *ap->a_count, err = 0, i;
	struct vm_page **pgs;

	if (vp->v_data == NULL) {
		if ((flags & PGO_LOCKED) == 0)
			rw_exit(rw);
		return (EOPNOTSUPP);
	}
	zp = VTOZ(vp);
	zfsvfs = zp->z_zfsvfs;

	if (flags & PGO_LOCKED) {
		/*
		 * Faults always pass a page array.  Readahead
		 * (ra_startio) uses !PGO_LOCKED and a_m == NULL.
		 */
		if (ap->a_m == NULL)
			return (EBUSY);
		uvn_findpages(uobj, ap->a_offset, &npages, ap->a_m, NULL,
		    UFP_NOWAIT | UFP_NOALLOC | UFP_NOBUSY |
		    (memwrite ? UFP_NORDONLY : 0));
		if (memwrite) {
			for (i = 0; i < npages; i++) {
				pg = ap->a_m[i];
				if (pg == NULL || pg == PGO_DONTCARE)
					continue;
				if (uvm_pagegetdirty(pg) ==
				    UVM_PAGE_STATUS_CLEAN)
					uvm_pagemarkdirty(pg,
					    UVM_PAGE_STATUS_UNKNOWN);
			}
		}
		return (ap->a_m[ap->a_centeridx] == NULL ? EBUSY : 0);
	}
	rw_exit(rw);

	/*
	 * zfs_nb_write_user() pass 1: this lwp is inside zfs_write()
	 * on a user buffer and may hold a range lock, a DB_FILL dbuf
	 * and an assigned tx.  Paging in a ZFS file here could wait on
	 * any of them, or on a writer that waits for this txg.  Fail
	 * the fault instead; copyin returns EFAULT, zfs_write() backs
	 * out, and the write is finished from a kernel buffer filled
	 * with no ZFS locks held.  Resident pages never get here: the
	 * PGO_LOCKED lookup above maps them.
	 */
	if (zfs_nb_nofault_active())
		return (EFAULT);

	/*
	 * GETPAGES is not fstrans-wrapped by UVM (pagedaemon).
	 * Taking fstrans_start + zfs_enter from the pagedaemon
	 * deadlocks umount: vfs_suspend waits for fstcnt while
	 * we wait for teardown.  putapage already refuses the
	 * pagedaemon; getpages uses nowait/tryenter instead.
	 */
	mp = vp->v_mount;
	if (curlwp == uvm.pagedaemon_lwp) {
		if (fstrans_start_nowait(mp) != 0)
			return (EAGAIN);
	} else {
		fstrans_start(mp);
	}
	if (vp->v_mount != mp) {
		fstrans_done(mp);
		return (ENOENT);
	}

	/*
	 * Faults use PGO_SYNCIO and expect returned pages to stay
	 * PG_BUSY.  Readahead (no page array) only prefetches, below.
	 */
	if (curlwp == uvm.pagedaemon_lwp)
		err = zfs_tryenter(zfsvfs, FTAG);
	else
		err = zfs_enter(zfsvfs, FTAG);
	if (err != 0) {
		fstrans_done(mp);
		return (err);
	}
	if ((err = zfs_verify_zp(zp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		fstrans_done(mp);
		return (err);
	}
	/*
	 * Readahead: ra_startio() is the only caller that passes no
	 * page array.  It is reached from posix_fadvise(WILLNEED),
	 * madvise(MADV_WILLNEED) and uvm_ra_request() on faults, one
	 * RA_IOCHUNK at a time.  The ARC is the cache, so start an
	 * async prefetch instead of filling pages synchronously.  The
	 * pages are not created: ra_startio() takes 0 as "window
	 * done", and a later fault or read is an ARC hit.  No
	 * rangelock: a prefetch only reads blocks, so it cannot see a
	 * torn page.  EINVAL past EOF ends the ra_startio() loop
	 * quietly, as genfs does.
	 */
	if (ap->a_m == NULL) {
		uint64_t off = (uint64_t)ap->a_offset;
		uint64_t size = zp->z_size;

		if (off >= size) {
			zfs_exit(zfsvfs, FTAG);
			fstrans_done(mp);
			return (EINVAL);
		}
		dmu_prefetch_user(zfsvfs->z_os, zp->z_id, 0, off,
		    MIN((uint64_t)npages << PAGE_SHIFT, size - off),
		    ZIO_PRIORITY_ASYNC_READ);
		zfs_exit(zfsvfs, FTAG);
		fstrans_done(mp);
		return (0);
	}
	/*
	 * Process context can block on the rangelock.  The
	 * pagedaemon cannot; UVM does not retry EBUSY, so if
	 * tryenter fails we return EAGAIN rather than filling
	 * a torn page against a concurrent write.
	 */
	if (curlwp == uvm.pagedaemon_lwp) {
		rl = zfs_rangelock_tryenter(&zp->z_rangelock, ap->a_offset,
		    (uint64_t)npages << PAGE_SHIFT, RL_READER);
		if (rl == NULL) {
			zfs_exit(zfsvfs, FTAG);
			fstrans_done(mp);
			return (EAGAIN);
		}
	} else if (zfs_rangelock_owner_covers(&zp->z_rangelock, ap->a_offset,
	    (uint64_t)npages << PAGE_SHIFT, B_FALSE)) {
		/*
		 * This LWP already holds a range that covers the
		 * fault (write of an mmap of the same file).  Taking
		 * it again deadlocks.  The lock we hold excludes
		 * every other writer.
		 */
		rl = NULL;
	} else if (zfs_nb_rw_active() &&
	    zfs_rangelock_owner_held(&zp->z_rangelock)) {
		/*
		 * Inside zfs_read/zfs_write and this thread holds a
		 * lock on this file that does not cover the fault.
		 * Blocking can wait on that lock: two writers of
		 * each other's mappings.  EAGAIN is not a short
		 * fault.  uvm_fault_lower_io sleeps and retries it,
		 * so neither thread leaves.  EFAULT fails the copy.
		 * A fault on a different file does not hold this
		 * lock and takes the blocking enter below, so
		 * ordinary contention on that file is not EFAULT.
		 */
		rl = zfs_rangelock_tryenter(&zp->z_rangelock, ap->a_offset,
		    (uint64_t)npages << PAGE_SHIFT, RL_READER);
		if (rl == NULL) {
			zfs_exit(zfsvfs, FTAG);
			fstrans_done(mp);
			return (EFAULT);
		}
	} else {
		/*
		 * Includes the range zfs_get_data() handed to an
		 * async zio.  owner_covers already handled a lock
		 * that contains this fault.  EAGAIN is retried
		 * forever by uvm_fault, so block for whoever holds
		 * this range.  The zio drops the lock it owns.
		 */
		rl = zfs_rangelock_enter(&zp->z_rangelock, ap->a_offset,
		    (uint64_t)npages << PAGE_SHIFT, RL_READER);
	}

	rw_enter(rw, RW_WRITER);
	{
		voff_t memeof = P2ROUNDUP_TYPED(vp->v_size, PAGE_SIZE, voff_t);

		/*
		 * PGO_PASTEOF (ubc_alloc UBC_FAULTBUSY) has already
		 * clipped to the intended EOF; extend memeof so the
		 * caller can walk the unclipped page array.
		 */
		if (flags & PGO_PASTEOF) {
			voff_t need = ap->a_offset +
			    ((voff_t)npages << PAGE_SHIFT);
			if (need > memeof)
				memeof = P2ROUNDUP_TYPED(need, PAGE_SIZE,
				    voff_t);
		}

		/*
		 * Clip to EOF rather than EINVAL the whole window.
		 * ra_startio issues RA_IOCHUNK (MAXPHYS) which often
		 * extends past the last page of a small file.
		 */
		if (ap->a_offset >= memeof) {
			rw_exit(rw);
			if (rl != NULL)
				zfs_rangelock_exit(rl);
			zfs_exit(zfsvfs, FTAG);
			fstrans_done(mp);
			return (EINVAL);
		}
		if (ap->a_offset + ((voff_t)npages << PAGE_SHIFT) > memeof)
			npages = (int)((memeof - ap->a_offset) >> PAGE_SHIFT);
	}
	pgs = ap->a_m;
	uvn_findpages(uobj, ap->a_offset, &npages, pgs, NULL, UFP_ALL);

	for (i = 0; i < npages; i++) {
		pg = pgs[i];
		if (pg == NULL || pg == PGO_DONTCARE)
			continue;
		if (pg->flags & PG_FAKE) {
			voff_t offset = pg->offset;
			uint64_t fsize = zp->z_size;
			size_t len;

			rw_exit(rw);
			va = zfs_pager_map(pg, S_WRITE);
			/*
			 * UVM requires a zero tail past logical EOF on
			 * the last page.  Do not read a full PAGE_SIZE
			 * from the DMU and hope the unused bytes are
			 * already zero.
			 */
			memset(va, 0, PAGE_SIZE);
			len = 0;
			if (offset < (voff_t)fsize)
				len = MIN((uint64_t)PAGE_SIZE,
				    fsize - (uint64_t)offset);
			err = 0;
			if (len != 0)
				err = dmu_read(zfsvfs->z_os, zp->z_id,
				    offset, len, va, DMU_READ_PREFETCH);
			zfs_pager_unmap(pg, va);
			if (err != 0) {
				uvm_aio_aiodone_pages(pgs, npages,
				    false, err);
				memset(pgs, 0, sizeof (pgs[0]) * npages);
				if (rl != NULL)
					zfs_rangelock_exit(rl);
				zfs_exit(zfsvfs, FTAG);
				fstrans_done(mp);
				return (err);
			}
			rw_enter(rw, RW_WRITER);
			pg->flags &= ~PG_FAKE;
		}
		if (memwrite && uvm_pagegetdirty(pg) ==
		    UVM_PAGE_STATUS_CLEAN)
			uvm_pagemarkdirty(pg, UVM_PAGE_STATUS_UNKNOWN);
	}
	rw_exit(rw);
	if (rl != NULL)
		zfs_rangelock_exit(rl);
	zfs_exit(zfsvfs, FTAG);
	fstrans_done(mp);
	return (err);
}

static specificdata_key_t zfs_putpage_key;
static int zfs_putpage_key_inited;

static specificdata_key_t zfs_nb_rw_key;
static int zfs_nb_rw_ready;

/* Set across pass 1 of zfs_nb_write_user(); read by getpages. */
static specificdata_key_t zfs_nb_nofault_key;
static int zfs_nb_nofault_ready;

/*
 * Set on the thread that is inside zfs_read/zfs_write.  The VOP
 * bounce runs those on a taskq thread, so the flag is taken there
 * rather than on the syscall thread that only copies to a kernel
 * buffer.  Nesting is a counter: the inline path and a nested
 * call share the thread.
 */
static void
zfs_nb_rw_enter(void)
{
	uintptr_t n;

	if (!zfs_nb_rw_ready)
		return;
	n = (uintptr_t)tsd_get(zfs_nb_rw_key);
	tsd_set(zfs_nb_rw_key, (void *)(n + 1));
}

static void
zfs_nb_rw_exit(void)
{
	uintptr_t n;

	if (!zfs_nb_rw_ready)
		return;
	n = (uintptr_t)tsd_get(zfs_nb_rw_key);
	if (n > 0)
		n--;
	tsd_set(zfs_nb_rw_key, (void *)n);
}

static boolean_t
zfs_nb_rw_active(void)
{
	if (!zfs_nb_rw_ready)
		return (B_FALSE);
	return ((uintptr_t)tsd_get(zfs_nb_rw_key) != 0);
}

static boolean_t
zfs_nb_nofault_active(void)
{
	if (!zfs_nb_nofault_ready)
		return (B_FALSE);
	return (tsd_get(zfs_nb_nofault_key) != NULL);
}

static int
zfs_nb_call_rw(znode_t *zp, zfs_uio_t *uio, int ioflag, cred_t *cr,
    boolean_t iswrite)
{
	int error;

	zfs_nb_rw_enter();
	if (iswrite)
		error = zfs_write(zp, uio, ioflag, cr);
	else
		error = zfs_read(zp, uio, ioflag, cr);
	zfs_nb_rw_exit();
	return (error);
}

void
zfs_netbsd_pager_init(void)
{
	if (!zfs_putpage_key_inited) {
		tsd_create(&zfs_putpage_key, NULL);
		zfs_putpage_key_inited = 1;
	}
	if (!zfs_nb_rw_ready) {
		tsd_create(&zfs_nb_rw_key, NULL);
		zfs_nb_rw_ready = 1;
	}
	if (!zfs_nb_nofault_ready) {
		tsd_create(&zfs_nb_nofault_key, NULL);
		zfs_nb_nofault_ready = 1;
	}
}

static int
zfs_putapage(vnode_t *vp, struct vm_page **pp, int count, int flags)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	dmu_tx_t *tx;
	voff_t off, len;
	int err, i;
	bool *cleanedp;

	(void) flags;
	if (zp->z_sa_hdl == NULL) {
		err = 0;
		goto out;
	}

	/*
	 * 11.0: pagedaemon must not write.  dmu_tx / dmu_write
	 * allocate and take locks; holding fstrans across that
	 * from the pagedaemon wedges vfs_suspend on fstcnt.
	 */
	if (curlwp == uvm.pagedaemon_lwp) {
		err = SET_ERROR(ENOMEM);
		goto out;
	}

	off = pp[0]->offset;
	len = (voff_t)count * PAGE_SIZE;
	if (off >= zp->z_size) {
		err = 0;
		goto out;
	}
	if (len > zp->z_size - off)
		len = zp->z_size - off;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, zp->z_id, off, len);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	/*
	 * GOP_WRITE is called with vmobjlock already dropped
	 * (genfs_do_putpages: rw_exit(slock) then GOP_WRITE).
	 * FreeBSD drops the vm_object lock and Linux drops the
	 * page lock before dmu_tx_assign(DMU_TX_WAIT) for the
	 * same reason: waiting for a txg while holding the pager
	 * lock deadlocks txg_sync.  Process context can DMU_TX_WAIT.
	 * The pagedaemon must not: txg_sync allocates pages.
	 */
	err = dmu_tx_assign(tx, curlwp == uvm.pagedaemon_lwp ?
	    DMU_TX_NOWAIT : DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		goto out;
	}
	for (i = 0; i < count && err == 0; i++) {
		voff_t poff = pp[i]->offset;
		uint64_t plen = PAGE_SIZE;
		caddr_t va;

		if (poff >= zp->z_size)
			break;
		if (plen > zp->z_size - poff)
			plen = zp->z_size - poff;
		if (plen == 0)
			break;
		va = zfs_pager_map(pp[i], S_READ);
		dmu_write(zfsvfs->z_os, zp->z_id, poff, plen, va, tx, 0);
		zfs_pager_unmap(pp[i], va);
	}
	cleanedp = tsd_get(zfs_putpage_key);
	if (cleanedp != NULL)
		*cleanedp = true;
	if (err == 0) {
		uint64_t mtime[2], ctime[2];
		sa_bulk_attr_t bulk[3];
		int bcount = 0;

		SA_ADD_BULK_ATTR(bulk, bcount, SA_ZPL_MTIME(zfsvfs), NULL,
		    &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, bcount, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, bcount, SA_ZPL_FLAGS(zfsvfs), NULL,
		    &zp->z_pflags, 8);
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
		(void) sa_bulk_update(zp->z_sa_hdl, bulk, bcount, tx);
		zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, off, len,
		    B_FALSE, B_FALSE, NULL, NULL);
	}
	dmu_tx_commit(tx);
out:
	uvm_aio_aiodone_pages(pp, count, true, err);
	return (err);
}

static void
zfs_netbsd_gop_putrange(struct vnode *vp, off_t off, off_t *lop, off_t *hip)
{
	znode_t *zp = VTOZ(vp);
	uint64_t blksz = zp->z_blksz != 0 ? zp->z_blksz : PAGE_SIZE;

	*lop = P2ALIGN_TYPED(off, blksz, off_t);
	*lop = P2ALIGN_TYPED(*lop, PAGE_SIZE, off_t);
	*hip = P2ROUNDUP_TYPED(*lop + blksz, PAGE_SIZE, off_t);
}

const struct genfs_ops zfs_genfsops = {
	.gop_write = zfs_putapage,
	.gop_putrange = zfs_netbsd_gop_putrange,
};

static int
zfs_netbsd_putpages(void *v)
{
	struct vop_putpages_args *ap = v;
	vnode_t *vp = ap->a_vp;
	int flags = ap->a_flags;
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	zfs_locked_range_t *rl = NULL;
	uint64_t len;
	int error;
	bool cleaned = false;
	bool cleaning = (flags & PGO_CLEANIT) != 0;
	boolean_t teardown_writer = B_FALSE;
	vfs_t *mp = vp->v_mount;

	if (vp->v_data == NULL)
		return (genfs_null_putpages(v));
	zp = VTOZ(vp);
	zfsvfs = zp->z_zfsvfs;

	if (cleaning) {
		/*
		 * This LWP already holds z_teardown_lock as writer
		 * across cache_purgevfs: zfsvfs_teardown (rollback /
		 * recv suspend) and zfs_acltype_apply_mnt from
		 * zfsvfs_setup (resume).  vinvalbuf reclaims a
		 * directory and gets here.  zfs_enter's rms_rlock
		 * then lockdebug-panics ("locking against myself").
		 * The writer lock is the exclusion zfs_enter wanted.
		 * Sampled once: we do not drop it in this function.
		 */
		teardown_writer = ZFS_TEARDOWN_WRITE_HELD(zfsvfs);
		ASSERT((ap->a_offlo & PAGE_MASK) == 0 &&
		    (ap->a_offhi & PAGE_MASK) == 0);
		if (ap->a_offhi == 0) {
			voff_t end = vp->v_writesize;

			if (end < (voff_t)ap->a_offlo)
				len = PAGE_SIZE;
			else
				len = P2ROUNDUP_TYPED(end, PAGE_SIZE, voff_t) -
				    (uint64_t)ap->a_offlo;
			if (len == 0)
				len = PAGE_SIZE;
		} else {
			len = ap->a_offhi - ap->a_offlo;
		}
		/*
		 * uvn_put: object locked on entry, VOP_PUTPAGES
		 * must unlock it.  PGO_LOCKED is a getpages flag.
		 * Drop here so zfs_enter / rangelock can sleep;
		 * if enter fails the contract is already satisfied
		 * (lock dropped).  Re-acquiring would deadlock
		 * uvm_vnp_setsize, which rw_enter's after uvn_put.
		 *
		 * PUTPAGES is not fstrans-wrapped.  Latch mp once  --
		 * fstrans_done on a re-read v_mount after reclaim
		 * is a silent no-op and pins fstcnt forever.
		 */
		rw_exit(vp->v_uobj.vmobjlock);
		if (curlwp == uvm.pagedaemon_lwp) {
			error = fstrans_start_nowait(mp);
			if (error != 0)
				return (error);
		} else {
			fstrans_start(mp);
			if (vp->v_mount != mp) {
				fstrans_done(mp);
				return (0);
			}
		}
		if (teardown_writer)
			error = 0;
		else if (curlwp == uvm.pagedaemon_lwp)
			error = zfs_tryenter(zfsvfs, FTAG);
		else
			error = zfs_enter(zfsvfs, FTAG);
		if (error != 0) {
			fstrans_done(mp);
			return (error);
		}
		if (curlwp == uvm.pagedaemon_lwp) {
			rl = zfs_rangelock_tryenter(&zp->z_rangelock,
			    ap->a_offlo, len, RL_WRITER);
			if (rl == NULL) {
				if (!teardown_writer)
					zfs_exit(zfsvfs, FTAG);
				fstrans_done(mp);
				return (SET_ERROR(ENOMEM));
			}
		} else if (zfs_rangelock_owner_covers(&zp->z_rangelock,
		    ap->a_offlo, len, B_TRUE)) {
			rl = NULL;
		} else {
			/*
			 * Not EAGAIN.  That is retried forever from
			 * getpages, and putpages callers have no use
			 * for it either.  A writer lock that already
			 * covers this range was skipped above.
			 * Blocking here waits on ourselves if this
			 * thread holds an overlapping lock.  Truncate
			 * locks to EOF, which covers.  No other caller
			 * is known; a debug build trips this if one
			 * shows up.
			 */
			ASSERT(!zfs_rangelock_owner_held(&zp->z_rangelock));
			rl = zfs_rangelock_enter(&zp->z_rangelock,
			    ap->a_offlo, len, RL_WRITER);
		}
		rw_enter(vp->v_uobj.vmobjlock, RW_WRITER);
		tsd_set(zfs_putpage_key, &cleaned);
	}
	error = genfs_putpages(v);
	if (cleaning) {
		tsd_set(zfs_putpage_key, NULL);
		if (rl != NULL)
			zfs_rangelock_exit(rl);
		/*
		 * posix_fadvise(DONTNEED) has no VOP.  vn_posix_fadvise()
		 * sends exactly CLEANIT|DEACTIVATE without FREE; nothing
		 * else in the kernel outside FFS/LFS does.  madvise
		 * (MADV_DONTNEED) and the sequential-fault behind pages
		 * send DEACTIVATE alone and keep the ARC, as on Linux.
		 * genfs_putpages has dropped the object lock and
		 * written dirty pages into dbufs; held dbufs are marked
		 * pending-evict and go on release.
		 */
		if (error == 0 && !teardown_writer &&
		    (flags & (PGO_CLEANIT | PGO_DEACTIVATE | PGO_FREE)) ==
		    (PGO_CLEANIT | PGO_DEACTIVATE))
			dmu_evict_range(zfsvfs->z_os, zp->z_id,
			    (uint64_t)ap->a_offlo, len);
		if (cleaned && (flags & PGO_RECLAIM) == 0) {
			if ((flags & PGO_SYNCIO) != 0 ||
			    zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS) {
				int zerr = zil_commit(zfsvfs->z_log, zp->z_id);

				if (zerr != 0 && error == 0)
					error = zerr;
			}
		}
		if (!teardown_writer)
			zfs_exit(zfsvfs, FTAG);
		fstrans_done(mp);
	}
	return (error);
}

static void
zfs_netbsd_update_mctime(vnode_t *vp)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	dmu_tx_t	*tx;
	sa_bulk_attr_t	bulk[2];
	uint64_t	mtime[2], ctime[2];
	int		count = 0, err;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return;
	}
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
	err = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	dmu_tx_commit(tx);
	if (err != 0) {
		printf("%s: sa_bulk_update failed with %d\n", __func__, err);
	}
}

static int
zfs_netbsd_open(void *v)
{
	struct vop_open_args *ap = v;
	vnode_t *vp = ap->a_vp;

	return (zfs_open(&vp, ap->a_mode, ap->a_cred));
}

static int
zfs_netbsd_close(void *v)
{
	struct vop_close_args *ap = v;

	return (zfs_close(ap->a_vp, ap->a_fflag, 1, 0, ap->a_cred));
}

/*
 * VOP_IOCTL for regular files.  vn_ioctl handles FIONREAD / FIONBIO
 * itself and only calls us for the rest.  11.0 zfs_ioctl implements
 * FIOSEEKDATA / FIOSEEKHOLE via dmu_offset_next; we call the common
 * zfs_holey() (same algorithm, plus mmap flush + rangelock).
 *
 * ZFS_IOC_FSGETXATTR / FSSETXATTR are the project-quota ioctls
 * zfs project(8) issues.  Same helpers as FreeBSD; zfs_setattr
 * takes its own locks, so do not vn_lock here.
 */
static int
zfs_ioctl_getxattr(vnode_t *vp, zfsxattr_t *fsx)
{
	znode_t *zp = VTOZ(vp);

	memset(fsx, 0, sizeof (*fsx));
	fsx->fsx_xflags = (zp->z_pflags & ZFS_PROJINHERIT) ?
	    FS_PROJINHERIT_FL : 0;
	fsx->fsx_projid = zp->z_projid;
	return (0);
}

static int
zfs_ioctl_setflags(vnode_t *vp, uint32_t ioctl_flags, xvattr_t *xva)
{
	uint64_t zfs_flags = VTOZ(vp)->z_pflags;
	xoptattr_t *xoap;

	if (ioctl_flags & ~(FS_PROJINHERIT_FL))
		return (SET_ERROR(EOPNOTSUPP));

	xva_init(xva);
	xoap = xva_getxoptattr(xva);

#define	FLAG_CHANGE(iflag, zflag, xflag, xfield)	do {		\
	if (((ioctl_flags & (iflag)) && !(zfs_flags & (zflag))) ||	\
	    ((zfs_flags & (zflag)) && !(ioctl_flags & (iflag)))) {	\
		XVA_SET_REQ(xva, (xflag));				\
		(xfield) = ((ioctl_flags & (iflag)) != 0);		\
	}								\
} while (0)

	FLAG_CHANGE(FS_PROJINHERIT_FL, ZFS_PROJINHERIT, XAT_PROJINHERIT,
	    xoap->xoa_projinherit);

#undef	FLAG_CHANGE
	return (0);
}

static int
zfs_ioctl_setxattr(vnode_t *vp, zfsxattr_t *fsx, cred_t *cr)
{
	znode_t *zp = VTOZ(vp);
	xvattr_t xva;
	xoptattr_t *xoap;
	int err;

	if (!zpl_is_valid_projid(fsx->fsx_projid))
		return (SET_ERROR(EINVAL));

	err = zfs_ioctl_setflags(vp, fsx->fsx_xflags, &xva);
	if (err)
		return (err);

	xoap = xva_getxoptattr(&xva);
	XVA_SET_REQ(&xva, XAT_PROJID);
	xoap->xoa_projid = fsx->fsx_projid;
	return (zfs_setattr(zp, (vattr_t *)&xva, 0, cr));
}

static int
zfs_netbsd_ioctl(void *v)
{
	struct vop_ioctl_args *ap = v;
	vnode_t *vp = ap->a_vp;
	ulong_t com = ap->a_command;
	znode_t *zp;
	loff_t off;
	int error;

	switch (com) {
	case FIOSEEKDATA:
	case FIOSEEKHOLE:
		if (vp->v_data == NULL)
			return (ENXIO);
		zp = VTOZ(vp);
		off = *(off_t *)ap->a_data;
		/*
		 * F_SEEK_* == FIOSEEK* (misc.h).  zfs_holey compares
		 * cmd against F_SEEK_HOLE and does zfs_enter_verify_zp.
		 * 11.0 does not vn_lock here; vn_ioctl is unlocked.
		 */
		error = zfs_holey(zp, com, &off);
		if (error)
			return (error);
		*(off_t *)ap->a_data = (off_t)off;
		return (0);
	case ZFS_IOC_REWRITE: {
		zfs_rewrite_args_t *args = ap->a_data;

		if ((ap->a_fflag & FWRITE) == 0)
			return (EBADF);
		if (vp->v_data == NULL)
			return (ENXIO);
		return (zfs_rewrite(VTOZ(vp), args->off, args->len,
		    args->flags, args->arg));
	}
	case ZFS_IOC_FSGETXATTR:
		if (vp->v_data == NULL)
			return (ENXIO);
		return (zfs_ioctl_getxattr(vp, ap->a_data));
	case ZFS_IOC_FSSETXATTR:
		if (vp->v_data == NULL)
			return (ENXIO);
		return (zfs_ioctl_setxattr(vp, ap->a_data, ap->a_cred));
	default:
		return (ENOTTY);
	}
}

/*
 * cache_012_pos (and any other user pread/pwrite of a miss) runs
 * zfs_read → dmu_buf_hold_array → arc_read → zio_create → kmem_alloc
 * on the syscall LWP.  577/578 bounced zio_execute; that is after
 * zio_create, so the last CALL was still kmem_intr_alloc on pid.pid
 * and the 12KB stack was already in the red hole.
 *
 * Hop userspace VOP_READ/WRITE onto z_vnops with a kernel bounce
 * buffer when zfs_nb_bounce is set.  The worker has a full kstack
 * for zio_create.  amd64 only has 12 KB of stack (UPAGES_STACK),
 * and cache_012_pos overflowed in zio_create with the hop off.
 * Default is 0 so a ZTS run shows whether the hop is still
 * required.  The common zio pipeline is not special-cased:
 * Linux has run 8 KB stacks for years with only the txg-sync and
 * pool-init redispatch, and that is what this port uses too.
 * UIO_SYSSPACE, O_DIRECT, and FSYNC/FRSYNC stay inline: the first
 * is already kernel (nested vdev, xattr), DIO holds user pages,
 * sync commit can wait for putpages of this vnode.  Faults during
 * an inline write are handled by zfs_nb_write_user(), not this hop.
 */
int zfs_nb_bounce = 0;

#define	ZFS_NB_VNOPS_CHUNK	(128 * 1024)

static taskq_t *zfs_nb_vnops_taskq;

struct zfs_nb_vnops_job {
	znode_t		*zp;
	zfs_uio_t	uio;
	int		ioflag;
	cred_t		*cr;
	boolean_t	iswrite;
	int		error;
	kmutex_t	lock;
	kcondvar_t	cv;
	int		done;
};

void
zfs_netbsd_vnops_taskq_init(void)
{
	int n;

	if (zfs_nb_vnops_taskq != NULL)
		return;
	n = MAX(boot_ncpus, 4);
	zfs_nb_vnops_taskq = taskq_create("z_vnops", n, minclsyspri,
	    n, INT_MAX, TASKQ_DYNAMIC);
}

void
zfs_netbsd_vnops_taskq_fini(void)
{
	if (zfs_nb_vnops_taskq != NULL) {
		taskq_destroy(zfs_nb_vnops_taskq);
		zfs_nb_vnops_taskq = NULL;
	}
}

static void
zfs_nb_vnops_task(void *arg)
{
	struct zfs_nb_vnops_job *j = arg;

	j->error = zfs_nb_call_rw(j->zp, &j->uio, j->ioflag, j->cr, j->iswrite);
	mutex_enter(&j->lock);
	j->done = 1;
	cv_signal(&j->cv);
	mutex_exit(&j->lock);
}

/*
 * Move a user uio through kernel buffers, ZFS_NB_VNOPS_CHUNK at a
 * time.  The user copy happens here, outside zfs_read/zfs_write, so
 * a fault holds no ZFS lock and no tx.  hop runs zfs_* on the vnops
 * taskq (the stack-depth bounce); otherwise it runs inline.
 */
static int
zfs_nb_kbuf_rw(znode_t *zp, zfs_uio_t *uio, int ioflag, cred_t *cr,
    boolean_t iswrite, boolean_t hop)
{
	struct zfs_nb_vnops_job job;
	struct uio auio;
	struct iovec iov;
	void *kbuf;
	size_t n, chunk;
	int error = 0;

	while (zfs_uio_resid(uio) > 0) {
		n = MIN((size_t)zfs_uio_resid(uio), ZFS_NB_VNOPS_CHUNK);
		kbuf = kmem_alloc(n, KM_SLEEP);
		if (iswrite) {
			size_t copied = 0;

			/*
			 * uiocopy does not advance the caller.  A short
			 * zfs_write (ENOSPC, EDQUOT, EFBIG) must not
			 * report the unread tail as written.
			 */
			error = zfs_uiocopy(kbuf, n, UIO_WRITE, uio, &copied);
			if (error != 0 || copied == 0) {
				kmem_free(kbuf, n);
				break;
			}
			chunk = copied;
		} else {
			chunk = n;
		}

		memset(&job, 0, sizeof (job));
		memset(&auio, 0, sizeof (auio));
		iov.iov_base = kbuf;
		iov.iov_len = chunk;
		auio.uio_iov = &iov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = zfs_uio_offset(uio);
		auio.uio_resid = chunk;
		auio.uio_rw = iswrite ? UIO_WRITE : UIO_READ;
		auio.uio_vmspace = vmspace_kernel();
		zfs_uio_init(&job.uio, &auio);
		job.zp = zp;
		job.ioflag = ioflag & ~(FSYNC | FRSYNC);
		job.cr = cr;
		job.iswrite = iswrite;

		if (!hop) {
			error = zfs_nb_call_rw(zp, &job.uio, ioflag, cr,
			    iswrite);
			goto done;
		}
		mutex_init(&job.lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&job.cv, NULL, CV_DEFAULT, NULL);
		if (taskq_dispatch(zfs_nb_vnops_taskq, zfs_nb_vnops_task,
		    &job, TQ_SLEEP) == TASKQID_INVALID) {
			mutex_destroy(&job.lock);
			cv_destroy(&job.cv);
			error = zfs_nb_call_rw(zp, &job.uio, ioflag, cr,
			    iswrite);
		} else {
			mutex_enter(&job.lock);
			while (job.done == 0)
				cv_wait(&job.cv, &job.lock);
			mutex_exit(&job.lock);
			error = job.error;
			mutex_destroy(&job.lock);
			cv_destroy(&job.cv);
		}
done:
		if (!iswrite && error == 0) {
			size_t got = chunk - auio.uio_resid;

			if (got > 0)
				error = zfs_uiomove(kbuf, got, UIO_READ, uio);
		}
		if (iswrite) {
			size_t got = chunk - (size_t)auio.uio_resid;

			/*
			 * Advance the caller only by bytes zfs_write
			 * committed.  O_APPEND does not land at the fd
			 * offset: zfs_write leaves the kernel uio at
			 * EOF + bytes, and vn_write stores that back.
			 */
			if (got > 0)
				zfs_uioskip(uio, got);
			if (error == 0 && (ioflag & FAPPEND) != 0)
				zfs_uio_setoffset(uio, auio.uio_offset);
		}
		kmem_free(kbuf, n);
		if (error != 0)
			break;
		if (auio.uio_resid != 0)
			break;
	}
	return (error);
}

/*
 * Pass 2, out of line so its locals are not on the stack under
 * zfs_write() in pass 1 (amd64 has 12 KB of kernel stack).
 */
static __noinline int
zfs_nb_write_finish(znode_t *zp, zfs_uio_t *uio, struct iovec *iov,
    ssize_t resid0, int ioflag, cred_t *cr)
{
	struct uio *su = GET_UIO_STRUCT(uio);
	struct uio cu;
	zfs_uio_t zcu;
	int error;

	/*
	 * Position the copy at the first unwritten byte.  The offset
	 * comes from the caller's uio: under FAPPEND zfs_write() moved
	 * it to EOF.
	 */
	cu = *su;
	cu.uio_iov = iov;
	cu.uio_resid = resid0;
	zfs_uio_init(&zcu, &cu);
	zfs_uioskip(&zcu, resid0 - su->uio_resid);
	cu.uio_offset = su->uio_offset;
	ASSERT3S(cu.uio_resid, ==, su->uio_resid);

	error = zfs_nb_kbuf_rw(zp, &zcu, ioflag, cr, B_TRUE, B_FALSE);
	su->uio_resid = cu.uio_resid;
	su->uio_offset = cu.uio_offset;
	return (error);
}

/*
 * Inline write from a user buffer: FreeBSD's vn_io_fault1() in
 * miniature.  NetBSD vn_write() holds the vnode lock exclusively
 * across VOP_WRITE, so the two passes are one write to other
 * writers.
 *
 * Pass 1 is the whole zfs_write() on the caller's uio with ZFS
 * page-ins refused (zfs_nb_nofault_active).  Resident sources, the
 * common case, finish here with one copy and no extra work.  A
 * fault into ZFS's pager fails with EFAULT instead of waiting on a
 * range lock, DB_FILL or the txg that zfs_write() holds; zfs_write()
 * commits what it copied and returns EFAULT, as on FreeBSD.
 *
 * Pass 2 finishes from kernel buffers: zfs_uiocopy() faults the
 * source in with no ZFS lock or tx held, so a page-in of any file,
 * including this one, is safe and the write always makes progress.
 *
 * Pass 1 may leave the iovecs ahead of uio_resid (a reverted dbuf
 * fill rewinds only resid and offset), so they are copied first and
 * pass 2 positions that copy from uio_resid, as vn_io_fault1() does
 * with cloneuio().  vn_write() reads back only resid and offset.
 *
 * Faults into other pagers (anon, swap, other filesystems) are not
 * refused in pass 1 and may sleep with the tx open, as on Linux.
 * Refusing those needs a NetBSD lwp no-fault flag.
 */
static int
zfs_nb_write_user(znode_t *zp, zfs_uio_t *uio, int ioflag, cred_t *cr)
{
	struct uio *su = GET_UIO_STRUCT(uio);
	struct iovec one, *iov;
	const size_t iovbytes = sizeof (struct iovec) * su->uio_iovcnt;
	const ssize_t resid0 = su->uio_resid;
	int error;

	iov = (su->uio_iovcnt <= 1) ? &one : kmem_alloc(iovbytes, KM_SLEEP);
	memcpy(iov, su->uio_iov, iovbytes);

	tsd_set(zfs_nb_nofault_key, (void *)1);
	error = zfs_nb_call_rw(zp, uio, ioflag, cr, B_TRUE);
	tsd_set(zfs_nb_nofault_key, NULL);
	if (error == EFAULT)
		error = zfs_nb_write_finish(zp, uio, iov, resid0, ioflag, cr);

	if (iov != &one)
		kmem_free(iov, iovbytes);
	return (error);
}

static int
zfs_nb_vnops_userio(znode_t *zp, zfs_uio_t *uio, int ioflag, cred_t *cr,
    boolean_t iswrite)
{
	if (zfs_uio_segflg(uio) != UIO_USERSPACE)
		return (zfs_nb_call_rw(zp, uio, ioflag, cr, iswrite));
	if (zfs_nb_bounce != 0 &&
	    zfs_nb_vnops_taskq != NULL &&
	    (ioflag & (FSYNC | FRSYNC | O_DIRECT)) == 0 &&
	    (uio->uio_extflg & UIO_DIRECT) == 0 &&
	    taskq_of_curthread() != zfs_nb_vnops_taskq)
		return (zfs_nb_kbuf_rw(zp, uio, ioflag, cr, iswrite, B_TRUE));
	if (iswrite)
		return (zfs_nb_write_user(zp, uio, ioflag, cr));
	return (zfs_nb_call_rw(zp, uio, ioflag, cr, B_FALSE));
}

static int
zfs_netbsd_read(void *v)
{
	struct vop_read_args *ap = v;
	vnode_t *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfs_uio_t uio;
	int ioflag = 0;

	switch (vp->v_type) {
	case VBLK:
	case VCHR:
		ZFS_ACCESSTIME_STAMP(zp->z_zfsvfs, zp);
		return (VOCALL(spec_vnodeop_p, VOFFSET(vop_read), ap));
	case VFIFO:
		ZFS_ACCESSTIME_STAMP(zp->z_zfsvfs, zp);
		return (VOCALL(fifo_vnodeop_p, VOFFSET(vop_read), ap));
	case VREG:
		break;
	case VDIR:
		/*
		 * Note: this is normal on NetBSD because it historically
		 * allows read() on a directory.
		 * We simply reject it here though because it doesn't make
		 * sense to allow read() unless we implement a conversion
		 * to the historical version of the UFS dirent structure,
		 * which i (yamt) don't think is worth the effort.
		 */
		return (EISDIR);
	default:
		return (EOPNOTSUPP);
	}

	if (ap->a_ioflag & IO_SYNC)
		ioflag |= FRSYNC;
	if (ap->a_ioflag & IO_APPEND)
		ioflag |= FAPPEND;
	if (ap->a_ioflag & IO_DIRECT)
		ioflag |= O_DIRECT;
	zfs_uio_init(&uio, ap->a_uio);
	return (zfs_nb_vnops_userio(zp, &uio, ioflag, ap->a_cred, B_FALSE));
}

static int
zfs_netbsd_write(void *v)
{
	struct vop_write_args *ap = v;
	vnode_t *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfs_uio_t uio;
	int ioflag = 0;

	switch (vp->v_type) {
	case VBLK:
	case VCHR:
		zfs_netbsd_update_mctime(vp);
		return (VOCALL(spec_vnodeop_p, VOFFSET(vop_write), ap));
	case VFIFO:
		zfs_netbsd_update_mctime(vp);
		return (VOCALL(fifo_vnodeop_p, VOFFSET(vop_write), ap));
	case VREG:
		break;
	case VDIR:
		/*
		 * Note: this shouldn't happen as NetBSD's vn_openchk
		 * rejects FWRITE on VDIR.
		 */
		return (EIO);
	default:
		return (EOPNOTSUPP);
	}

	if (ap->a_ioflag & IO_SYNC)
		ioflag |= FSYNC;
	if (ap->a_ioflag & IO_APPEND)
		ioflag |= FAPPEND;
	if (ap->a_ioflag & IO_DIRECT)
		ioflag |= O_DIRECT;
	zfs_uio_init(&uio, ap->a_uio);
	return (zfs_nb_vnops_userio(zp, &uio, ioflag, ap->a_cred, B_TRUE));
}

static int
zfs_netbsd_access(void *v)
{
	struct vop_access_args *ap = v;
	vnode_t *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	cred_t *cr = ap->a_cred;
	accmode_t accmode = ap->a_accmode &
	    (VREAD | VWRITE | VEXEC | VAPPEND);
	accmode_t rest = ap->a_accmode &
	    ~(VREAD | VWRITE | VEXEC | VAPPEND);
	int error = 0;

	if (cr == NULL || cr == NOCRED || cr == FSCRED)
		cr = kcred;

	if (accmode != 0) {
		if (zp->z_zfsvfs != NULL &&
		    zp->z_zfsvfs->z_acl_type == ZFS_ACLTYPE_POSIX) {
			uid_t owner;

			if ((accmode & (VWRITE | VAPPEND)) != 0) {
				if (zp->z_zfsvfs->z_vfs != NULL &&
				    (zp->z_zfsvfs->z_vfs->vfs_flag &
				    VFS_RDONLY))
					return (EROFS);
				if (zp->z_pflags & ZFS_IMMUTABLE)
					return (EPERM);
				if (vp->v_type != VDIR &&
				    (zp->z_pflags & ZFS_READONLY))
					return (EPERM);
			}
			if ((accmode & (VREAD | VEXEC)) != 0 &&
			    (zp->z_pflags & ZFS_AV_QUARANTINED))
				return (EACCES);
			error = zfs_netbsd_posix_access(vp, accmode, cr);
			if (error != 0) {
				owner = zfs_fuid_map_id(zp->z_zfsvfs,
				    zp->z_uid, cr, ZFS_OWNER);
				error = secpolicy_vnode_access2(cr, vp,
				    owner, 0, accmode);
			}
		} else {
			error = zfs_access(zp, accmode, 0, cr);
		}
	}
	if (error == 0 && rest != 0) {
		uid_t owner;

		/*
		 * VADMIN (chflags, owner ops) and any other leftover
		 * bit: owner or privilege.  Map the FUID the same way
		 * as the posix-access path a few lines above.
		 */
		owner = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_uid, cr,
		    ZFS_OWNER);
		error = secpolicy_vnode_setdac(vp, cr, owner);
	}
	return (error);
}

/*
 * cn_nameptr is a pathname slice, not always a C string.
 * VOP_PARSEPATH is genfs_parsepath, so cn_namelen is set on
 * the usual namei path.  Walk if it is 0 (empty "." from
 * namei).  cache_lookup / cache_enter must see this computed
 * name, not the raw cnp — an empty cn_nameptr rewritten to
 * "." in nm only must not be cached under length 0.
 */
static int
zfs_cn_name(struct componentname *cnp, char *buf, size_t buflen)
{
	const char *p;
	size_t n;

	p = cnp->cn_nameptr;
	if (p == NULL) {
		buf[0] = '\0';
		return (EINVAL);
	}
	n = cnp->cn_namelen;
	if (n == 0) {
		while (n + 1 < buflen && p[n] != '\0' && p[n] != '/')
			n++;
	}
	if (n >= buflen)
		return (ENAMETOOLONG);
	memcpy(buf, p, n);
	buf[n] = '\0';
	return (0);
}

/*
 * ZAP_MAXNAMELEN_NEW is 1023.  A VOP frame that size on a 12KB
 * NetBSD kstack (with namei already nested) is too much; heap it.
 */
#define	ZFS_CN_NMLEN	(ZAP_MAXNAMELEN_NEW + 1)

static char *
zfs_cn_name_kmem(struct componentname *cnp, int *errp)
{
	char *nm = kmem_alloc(ZFS_CN_NMLEN, KM_SLEEP);
	int error = zfs_cn_name(cnp, nm, ZFS_CN_NMLEN);

	if (error != 0) {
		kmem_free(nm, ZFS_CN_NMLEN);
		*errp = error;
		return (NULL);
	}
	*errp = 0;
	return (nm);
}

static void
zfs_cn_name_free(char *nm)
{
	if (nm != NULL)
		kmem_free(nm, ZFS_CN_NMLEN);
}

static void
zfs_cn_mark_last(struct componentname *cnp)
{
	const char *p;
	size_t n;

	if (cnp == NULL || cnp->cn_nameptr == NULL)
		return;
	if ((cnp->cn_flags & ISLASTCN) != 0)
		return;
	p = cnp->cn_nameptr;
	n = cnp->cn_namelen;
	if (n > 0 && n < MAXPATHLEN) {
		if (p[n] != '/')
			cnp->cn_flags |= ISLASTCN;
		return;
	}
	while (*p != '\0' && *p != '/')
		p++;
	if (*p != '/')
		cnp->cn_flags |= ISLASTCN;
}

static int
zfs_nb_cached_exec(znode_t *zp, cred_t *cr)
{
	vnode_t *vp;
	uid_t owner;
	gid_t group;
	mode_t mode;

	/*
	 * Orphan directories have no SA handle, so VOP_ACCESS cannot
	 * run.  Search permission is the cached mode, passed through
	 * kauth the way ufs_accessx does, so the secmodel decides
	 * about root.  The ACL itself is gone with the SA; a listener
	 * can still allow the vnode action.
	 */
	if (zp == NULL)
		return (EACCES);
	if (cr == NULL || cr == NOCRED || cr == FSCRED)
		return (0);
	vp = ZTOV(zp);
	if (vp == NULL)
		return (EACCES);
	mode = (mode_t)zp->z_mode & ALLPERMS;
	owner = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_uid, cr, ZFS_OWNER);
	group = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_gid, cr, ZFS_GROUP);
	return (kauth_authorize_vnode((kauth_cred_t)cr,
	    KAUTH_ACCESS_ACTION(VEXEC, vp->v_type, mode), vp, NULL,
	    genfs_can_access(vp, (kauth_cred_t)cr, owner, group, mode,
	    NULL, VEXEC)));
}

static int
zfs_netbsd_lookup(void *v)
{
	struct vop_lookup_v2_args *ap = v;
	struct vnode *dvp;
	struct vnode **vpp;
	struct componentname *cnp;
	char *nm = NULL;
	znode_t *dzp;
	const char *saved_ptr;
	size_t saved_len;
	int zfs_flags = 0;
	int error;
	int entered = 0;

	if (ap == NULL)
		return (EINVAL);
	dvp = ap->a_dvp;
	vpp = ap->a_vpp;
	cnp = ap->a_cnp;
	if (vpp != NULL)
		*vpp = NULL;
	if (dvp == NULL || vpp == NULL || cnp == NULL)
		return (EINVAL);

	zfs_cn_mark_last(cnp);
	nm = zfs_cn_name_kmem(cnp, &error);
	if (nm == NULL)
		return (error);
	/* namei may pass an empty component for "." */
	if (nm[0] == '\0') {
		nm[0] = '.';
		nm[1] = '\0';
	}

	/*
	 * Bind cnp to the computed name for the rest of this VOP:
	 * cache_lookup, cache_enter, and zfs_lookup's own cache_enter
	 * all read cn_nameptr/cn_namelen.  Restore before return;
	 * namei owns the original slice.
	 */
	saved_ptr = cnp->cn_nameptr;
	saved_len = cnp->cn_namelen;
	cnp->cn_nameptr = nm;
	cnp->cn_namelen = strlen(nm);

	dzp = VTOZ(dvp);
	if (dzp == NULL || dzp->z_zfsvfs == NULL) {
		error = EIO;
		goto out;
	}

	/*
	 * Take the teardown lock before treating a NULL SA handle as
	 * an orphan.  zfs_suspend_fs() resumes the mount while every
	 * znode still has z_sa_hdl == NULL; lookups must wait for
	 * zfs_resume_fs(), not return ENOENT.  zfs_lookup() enters
	 * again.  The rmlock reader is nested per LWP.
	 */
	error = zfs_enter(dzp->z_zfsvfs, FTAG);
	if (error != 0)
		goto out;
	entered = 1;

	/*
	 * A rollback or receive can delete a directory that still
	 * has a vnode.  Orphans have no SA handle, so VOP_ACCESS
	 * and zfs_lookup cannot serve them.  "." is this vnode.
	 * Anything else is ENOENT.  A child mount that covered this
	 * directory was unmounted from zfs_resume_fs(); unmount(2)
	 * cannot namei a path whose directory is gone.
	 * Only LOOKUP is allowed; the directory is not there to
	 * create or remove in.
	 */
	{
		int dot = (nm[0] == '.' && nm[1] == '\0');
		int orphan = (!dzp->z_is_ctldir &&
		    (dzp->z_nb_orphaned || dzp->z_sa_hdl == NULL));

		if (orphan) {
			error = zfs_nb_cached_exec(dzp, cnp->cn_cred);
			if (error != 0)
				goto out;

			if (dot) {
				if ((cnp->cn_flags & ISLASTCN) != 0 &&
				    cnp->cn_nameiop == RENAME) {
					error = EISDIR;
					goto out;
				}
				vref(dvp);
				*vpp = dvp;
				error = 0;
				goto cached;
			}
			error = ENOENT;
			goto out;
		}
	}

	error = VOP_ACCESS(dvp, VEXEC, cnp->cn_cred);
	if (error != 0)
		goto out;

	if (dzp->z_zfsvfs->z_use_namecache &&
	    cache_lookup(dvp, nm, cnp->cn_namelen,
	    cnp->cn_nameiop, cnp->cn_flags, NULL, vpp)) {
		error = (*vpp == NULL ? ENOENT : 0);
		goto cached;
	}

	if (nm[0] == '.' && nm[1] == '\0') {
		if ((cnp->cn_flags & ISLASTCN) != 0 &&
		    cnp->cn_nameiop == RENAME) {
			error = EISDIR;
			goto out;
		}
		vref(dvp);
		*vpp = dvp;
		error = 0;
		goto cached;
	}

	if ((nm[0] == '.' && nm[1] == '.' && nm[2] == '\0') ||
	    (cnp->cn_flags & ISDOTDOT) != 0) {
		if ((cnp->cn_flags & ISLASTCN) != 0 &&
		    cnp->cn_nameiop == RENAME) {
			error = EINVAL;
			goto out;
		}
		if (dzp->z_id == dzp->z_zfsvfs->z_root) {
			vref(dvp);
			*vpp = dvp;
			error = 0;
			goto cached;
		}
	}

	if (VTOZ(dvp)->z_zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		zfs_flags = FIGNORECASE;
	}

	error = zfs_lookup(dvp, nm, vpp, cnp, cnp->cn_nameiop,
	    cnp->cn_cred, zfs_flags, B_FALSE);

cached:
	if ((cnp->cn_flags & ISLASTCN) != 0 &&
	    (error == 0 || error == ENOENT)) {
		switch (cnp->cn_nameiop) {
		case CREATE:
		case RENAME:
			if (error == ENOENT) {
				error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred);
				if (error == 0)
					error = EJUSTRETURN;
			}
			break;
		case DELETE:
			if (error == 0) {
				error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred);
				if (error != 0) {
					vrele(*vpp);
					*vpp = NULL;
				}
			}
			break;
		}
	}
	if (dzp->z_zfsvfs->z_use_namecache &&
	    (error == 0 || error == ENOENT) &&
	    ((cnp->cn_flags & ISLASTCN) == 0 ||
	    (cnp->cn_nameiop != CREATE && cnp->cn_nameiop != DELETE &&
	    cnp->cn_nameiop != RENAME)))
		cache_enter(dvp, *vpp, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp->cn_flags);
out:
	if (entered)
		zfs_exit(dzp->z_zfsvfs, FTAG);
	cnp->cn_nameptr = __UNCONST(saved_ptr);
	cnp->cn_namelen = saved_len;
	zfs_cn_name_free(nm);
	return (error);
}

static int
zfs_netbsd_create(void *v)
{
	struct vop_create_v3_args *ap = v;
	char *nm;
	znode_t *zp = NULL;
	int error;

	vattr_init_mask(ap->a_vap);
	nm = zfs_cn_name_kmem(ap->a_cnp, &error);
	if (nm == NULL)
		return (error);
	error = zfs_create(VTOZ(ap->a_dvp), nm, ap->a_vap,
	    ap->a_vap->va_mode & VA_EXCLUSIVE, ap->a_vap->va_mode, &zp,
	    ap->a_cnp->cn_cred, 0, NULL);
	if (error == 0) {
		*ap->a_vpp = ZTOV(zp);
		error = zfs_netbsd_posix_inherit(ap->a_dvp, *ap->a_vpp,
		    ap->a_cnp->cn_cred);
		if (error != 0) {
			VOP_UNLOCK(*ap->a_vpp);
			vrele(*ap->a_vpp);
			(void) zfs_remove(VTOZ(ap->a_dvp), nm,
			    ap->a_cnp->cn_cred, 0);
			*ap->a_vpp = NULL;
		} else {
			VOP_UNLOCK(*ap->a_vpp);
		}
	} else {
		*ap->a_vpp = NULL;
	}
	zfs_cn_name_free(nm);
	return (error);
}

static int
zfs_netbsd_mknod(void *v)
{
	return (zfs_netbsd_create(v));
}

static int
zfs_netbsd_remove(void *v)
{
	struct vop_remove_v3_args *ap = v;
	char *nm;
	int error;

	/*
	 * NetBSD VOP_REMOVE: namei passes dvp+vp locked and held.
	 * After the VOP, vfs only vputs dvp (do_sys_unlinkat).  The
	 * VOP must consume vp's lock and hold, the same way UFS does.
	 * Do not go through zfs_remove(): that re-looks-up the same
	 * object, vrele's the extra hold, and leaves namei's hold
	 * (unmount leftover vnode).
	 */
	nm = zfs_cn_name_kmem(ap->a_cnp, &error);
	if (nm == NULL) {
		vput(ap->a_vp);
		return (error);
	}
	error = zfs_remove_(ap->a_dvp, ap->a_vp, nm, ap->a_cnp->cn_cred);
	zfs_cn_name_free(nm);
	vput(ap->a_vp);
	return (error);
}

static int
zfs_netbsd_mkdir(void *v)
{
	struct vop_mkdir_v3_args *ap = v;
	char *nm;
	znode_t *zp = NULL;
	int error;

	vattr_init_mask(ap->a_vap);
	nm = zfs_cn_name_kmem(ap->a_cnp, &error);
	if (nm == NULL)
		return (error);
	error = zfs_mkdir(VTOZ(ap->a_dvp), nm, ap->a_vap, &zp,
	    ap->a_cnp->cn_cred, 0, NULL);
	if (error == 0) {
		*ap->a_vpp = ZTOV(zp);
		error = zfs_netbsd_posix_inherit(ap->a_dvp, *ap->a_vpp,
		    ap->a_cnp->cn_cred);
		if (error != 0) {
			VOP_UNLOCK(*ap->a_vpp);
			vrele(*ap->a_vpp);
			(void) zfs_rmdir(VTOZ(ap->a_dvp), nm, NULL,
			    ap->a_cnp->cn_cred, 0);
			*ap->a_vpp = NULL;
		} else {
			VOP_UNLOCK(*ap->a_vpp);
		}
	} else {
		*ap->a_vpp = NULL;
	}
	zfs_cn_name_free(nm);
	return (error);
}

static int
zfs_netbsd_rmdir(void *v)
{
	struct vop_rmdir_v2_args *ap = v;
	char *nm;
	int error;

	/*
	 * NetBSD VOP_RMDIR: same hold contract as VOP_REMOVE.
	 * vfs only vputs dvp; consume vp here (ufs_rmdir).
	 */
	nm = zfs_cn_name_kmem(ap->a_cnp, &error);
	if (nm == NULL) {
		vput(ap->a_vp);
		return (error);
	}
	error = zfs_rmdir_(ap->a_dvp, ap->a_vp, nm, ap->a_cnp->cn_cred);
	zfs_cn_name_free(nm);
	vput(ap->a_vp);
	return (error);
}

static int
zfs_netbsd_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	zfs_uio_t uio;
	int eof = 0;
	int error;

	zfs_uio_init(&uio, ap->a_uio);
	error = zfs_readdir(ap->a_vp, &uio, ap->a_cred, &eof, NULL, NULL);
	if (ap->a_eofflag != NULL)
		*ap->a_eofflag = eof;
	return (error);
}

static int
zfs_netbsd_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	vnode_t *vp = ap->a_vp;
	int flags = ap->a_flags;
	int error;

	/*
	 * Native NetBSD osnet zfs_netbsd_fsync: push dirty pages
	 * first, then skip zil_commit on reclaim/lazy.  zil_commit
	 * during VOP_RECLAIM can deadlock in zfs_get_data/vcache_get
	 * on this vnode.  FSYNC_LAZY is the syncer trickle.
	 */
	if (vp->v_type == VREG && vp->v_data != NULL) {
		int pgo = PGO_CLEANIT;

		if ((flags & FSYNC_WAIT) != 0 &&
		    (flags & FSYNC_LAZY) == 0)
			pgo |= PGO_SYNCIO;
		rw_enter(vp->v_uobj.vmobjlock, RW_WRITER);
		error = VOP_PUTPAGES(vp,
		    P2ALIGN_TYPED(ap->a_offlo, PAGE_SIZE, voff_t),
		    P2ROUNDUP_TYPED(ap->a_offhi, PAGE_SIZE, voff_t), pgo);
		if (error != 0)
			return (error);
	}
	if ((flags & (FSYNC_RECLAIM | FSYNC_LAZY)) != 0)
		return (0);
	if (vp->v_data == NULL)
		return (0);
	return (zfs_fsync(VTOZ(vp), flags, ap->a_cred));
}

static int
zfs_netbsd_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	vattr_t *vap = ap->a_vap;
	xvattr_t xvap;
	ulong_t fflags = 0;
	int error;

	xva_init(&xvap);
	xvap.xva_vattr = *vap;
	/*
	 * va_mask is va_spare.  Native vn_stat leaves it unset.
	 * zfs_getattr fills every basic field regardless, but the
	 * xvattr path needs a known mask, not leftover stack.
	 */
	xvap.xva_vattr.va_mask = AT_ALL | AT_XVATTR;
	XVA_SET_REQ(&xvap, XAT_IMMUTABLE);
	XVA_SET_REQ(&xvap, XAT_APPENDONLY);
	XVA_SET_REQ(&xvap, XAT_NODUMP);
	XVA_SET_REQ(&xvap, XAT_ARCHIVE);
	XVA_SET_REQ(&xvap, XAT_OPAQUE);

	error = zfs_getattr(ap->a_vp, (vattr_t *)&xvap, 0, ap->a_cred);
	if (error != 0)
		return (error);

	*vap = xvap.xva_vattr;
#define	FLAG_RTN(fflag, xflag, xfield)	do {			\
	if (XVA_ISSET_RTN(&xvap, (xflag)) && (xfield) != 0)	\
		fflags |= (fflag);				\
} while (0)
	/*
	 * ZFS has one immutable pflag.  Report it as UF_IMMUTABLE
	 * so chflags nouchg works for the owner (ZTS).  setattr
	 * still accepts SF_IMMUTABLE|UF_IMMUTABLE as the same bit;
	 * schg then ls -lo looks like uchg.  There is no second
	 * pflag to round-trip schg separately.
	 */
	FLAG_RTN(UF_IMMUTABLE, XAT_IMMUTABLE,
	    xvap.xva_xoptattrs.xoa_immutable);
	FLAG_RTN(UF_APPEND, XAT_APPENDONLY,
	    xvap.xva_xoptattrs.xoa_appendonly);
	FLAG_RTN(UF_NODUMP, XAT_NODUMP, xvap.xva_xoptattrs.xoa_nodump);
	FLAG_RTN(SF_ARCHIVED, XAT_ARCHIVE, xvap.xva_xoptattrs.xoa_archive);
	FLAG_RTN(UF_OPAQUE, XAT_OPAQUE, xvap.xva_xoptattrs.xoa_opaque);
#undef	FLAG_RTN
	vap->va_flags = fflags;
	return (0);
}

static ulong_t
zfs_netbsd_chflags_mask(void)
{
	return (UF_NODUMP | UF_IMMUTABLE | UF_APPEND | UF_OPAQUE |
	    SF_IMMUTABLE | SF_APPEND | SF_ARCHIVED);
}

/*
 * Mirror getattr: ZFS has one immutable/append pflag, reported as
 * UF_* so owner nouchg works.  Used to compare SF_SETTABLE against
 * the incoming va_flags the same way UFS does.
 */
static ulong_t
zfs_netbsd_pflags_to_fflags(uint64_t zflags)
{
	ulong_t f = 0;

	if (zflags & ZFS_IMMUTABLE)
		f |= UF_IMMUTABLE;
	if (zflags & ZFS_APPENDONLY)
		f |= UF_APPEND;
	if (zflags & ZFS_NODUMP)
		f |= UF_NODUMP;
	if (zflags & ZFS_ARCHIVE)
		f |= SF_ARCHIVED;
	if (zflags & ZFS_OPAQUE)
		f |= UF_OPAQUE;
	return (f);
}

/*
 * UFS ufs_setattr raises WRITE_FLAGS, plus HAS_SYSFLAGS when
 * SF_IMMUTABLE|SF_APPEND are already set and WRITE_SYSFLAGS when
 * SF_SETTABLE is changing.  secmodel_securelevel denies the
 * combination at securelevel >= 1.  priv_check_cred(PRIV_VFS_SYSFLAGS)
 * is only ISSUSER, so without this kauth call root can always
 * chflags noschg / chflags 0 on ZFS.
 *
 * Clearing the collapsed pflag via nouchg does not change
 * SF_SETTABLE (getattr reports UF_*), so also raise WRITE_SYSFLAGS
 * when those pflags would actually drop — FreeBSD ZFS uses
 * securelevel_gt() for the same case.  changing_sysflags stays
 * tied to SF_SETTABLE so genfs_can_chflags still lets the owner
 * nouchg at securelevel 0.
 */
static int
zfs_netbsd_chflags_kauth(vnode_t *vp, cred_t *cr, znode_t *zp,
    ulong_t fflags, uint64_t zflags)
{
	kauth_action_t action;
	bool changing_sysflags;
	ulong_t curf, imm, apd;

	action = KAUTH_VNODE_WRITE_FLAGS;
	changing_sysflags = false;
	curf = zfs_netbsd_pflags_to_fflags(zflags);

	if (zflags & (ZFS_IMMUTABLE | ZFS_APPENDONLY | ZFS_NOUNLINK))
		action |= KAUTH_VNODE_HAS_SYSFLAGS;

	{
		ulong_t sf = SF_SETTABLE;
		/*
		 * ZFS_ARCHIVE is UF_ARCHIVE on FreeBSD: the owner
		 * may set and clear it.  NetBSD chflags(1) only has
		 * SF_ARCHIVED for "arch", so getattr still reports
		 * that bit, but kauth must not treat archive as a
		 * system flag.
		 */
		sf &= ~SF_ARCHIVED;
		if ((fflags & sf) != (curf & sf)) {
			action |= KAUTH_VNODE_WRITE_SYSFLAGS;
			changing_sysflags = true;
		}
	}
	imm = UF_IMMUTABLE | SF_IMMUTABLE;
	apd = UF_APPEND | SF_APPEND;
	if ((zflags & ZFS_IMMUTABLE) != 0 && (fflags & imm) == 0)
		action |= KAUTH_VNODE_WRITE_SYSFLAGS;
	if ((zflags & ZFS_APPENDONLY) != 0 && (fflags & apd) == 0)
		action |= KAUTH_VNODE_WRITE_SYSFLAGS;

	return (kauth_authorize_vnode((kauth_cred_t)cr, action, vp, NULL,
	    genfs_can_chflags(vp, (kauth_cred_t)cr, (uid_t)zp->z_uid,
	    changing_sysflags)));
}

static int
zfs_netbsd_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	vnode_t *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	cred_t *cr = ap->a_cred;
	znode_t *zp = VTOZ(vp);
	xvattr_t xvap;
	ulong_t fflags, zflags;
	int error;

	vattr_init_mask(vap);
	xva_init(&xvap);
	xvap.xva_vattr = *vap;

	if (vap->va_flags != VNOVAL) {
		if (zp->z_zfsvfs->z_use_fuids == B_FALSE)
			return (EOPNOTSUPP);
		fflags = vap->va_flags;
		if ((fflags & ~zfs_netbsd_chflags_mask()) != 0)
			return (EOPNOTSUPP);
		zflags = zp->z_pflags;
		error = zfs_netbsd_chflags_kauth(vp, cr, zp, fflags, zflags);
		if (error != 0)
			return (error);
		if (priv_check_cred(cr, PRIV_VFS_SYSFLAGS) != 0) {
			error = secpolicy_vnode_setdac(vp, cr, zp->z_uid);
			if (error != 0)
				return (error);
			if (fflags & (SF_IMMUTABLE | SF_APPEND))
				return (EPERM);
			/*
			 * ZFS_NOUNLINK is SF-class (FreeBSD also
			 * rejects owner changes while it is set).
			 * IMMUTABLE/APPENDONLY map from UF_* as well
			 * as SF_*; treating them as already-set system
			 * flags stopped the owner clearing uimmutable.
			 */
			if (zflags & ZFS_NOUNLINK)
				return (EPERM);
		}
		/*
		 * NetBSD vattr_t stores va_mask in va_spare. Copying *vap
		 * overwrites AT_XVATTR that xva_init() set. XVA_SET_REQ
		 * VERIFYs that bit, so restore it before FLAG_CHG.
		 */
		xvap.xva_vattr.va_mask |= AT_XVATTR;
#define	FLAG_CHG(fflag, zflag, xflag, xfield)	do {		\
	if (((fflags & (fflag)) && !(zflags & (zflag))) ||	\
	    ((zflags & (zflag)) && !(fflags & (fflag)))) {	\
		XVA_SET_REQ(&xvap, (xflag));			\
		(xfield) = ((fflags & (fflag)) != 0);		\
	}							\
} while (0)
		FLAG_CHG(SF_IMMUTABLE | UF_IMMUTABLE, ZFS_IMMUTABLE,
		    XAT_IMMUTABLE, xvap.xva_xoptattrs.xoa_immutable);
		FLAG_CHG(SF_APPEND | UF_APPEND, ZFS_APPENDONLY,
		    XAT_APPENDONLY, xvap.xva_xoptattrs.xoa_appendonly);
		FLAG_CHG(UF_NODUMP, ZFS_NODUMP, XAT_NODUMP,
		    xvap.xva_xoptattrs.xoa_nodump);
		FLAG_CHG(SF_ARCHIVED, ZFS_ARCHIVE, XAT_ARCHIVE,
		    xvap.xva_xoptattrs.xoa_archive);
		FLAG_CHG(UF_OPAQUE, ZFS_OPAQUE, XAT_OPAQUE,
		    xvap.xva_xoptattrs.xoa_opaque);
#undef	FLAG_CHG
		xvap.xva_vattr.va_mask |= AT_XVATTR;
	}
	error = zfs_setattr(zp, (vattr_t *)&xvap, 0, cr);
	if (error == 0)
		zfs_znode_cache_id(zp);
	return (error);
}

static int
zfs_netbsd_rename(void *v)
{
	struct vop_rename_args *ap = v;
	vnode_t *fdvp = ap->a_fdvp;
	vnode_t *fvp = ap->a_fvp;
	vnode_t *tdvp = ap->a_tdvp;
	vnode_t *tvp = ap->a_tvp;
	struct componentname scn, tcn;
	char *snm, *tnm;
	int error;

	/*
	 * NetBSD VOP_RENAME: tdvp (and tvp if present) are locked;
	 * fdvp/fvp are referenced.  VOP consumes all four refs.
	 * Call zfs_do_rename like FreeBSD -- do not go through
	 * zfs_rename(), which re-looks-up and re-locks (deadlock).
	 */
	scn = *ap->a_fcnp;
	tcn = *ap->a_tcnp;
	snm = zfs_cn_name_kmem(ap->a_fcnp, &error);
	if (snm == NULL)
		goto drop;
	tnm = zfs_cn_name_kmem(ap->a_tcnp, &error);
	if (tnm == NULL) {
		zfs_cn_name_free(snm);
		goto drop;
	}
	scn.cn_nameptr = snm;
	scn.cn_namelen = strlen(snm);
	tcn.cn_nameptr = tnm;
	tcn.cn_namelen = strlen(tnm);

	error = zfs_do_rename(fdvp, &fvp, &scn, tdvp, &tvp, &tcn,
	    ap->a_fcnp->cn_cred);

	zfs_cn_name_free(snm);
	zfs_cn_name_free(tnm);

	/*
	 * zfs_do_rename already dropped the incoming tdvp/tvp
	 * locks (relock + zfs_rename_unlock).  Only the four
	 * holds remain; vput would unlock twice.
	 */
	vrele(fdvp);
	vrele(fvp);
	vrele(tdvp);
	if (tvp != NULL)
		vrele(tvp);
	return (error);
drop:
	/*
	 * VOP_RENAME always consumes the four refs.  tdvp (and tvp)
	 * arrive exclusive-locked; fdvp/fvp are only referenced.
	 */
	vrele(fdvp);
	vrele(fvp);
	if (tvp != NULL)
		vput(tvp);
	vput(tdvp);
	return (error);
}

static int
zfs_netbsd_symlink(void *v)
{
	struct vop_symlink_v3_args *ap = v;
	char *nm;
	znode_t *zp = NULL;
	int error;

	vattr_init_mask(ap->a_vap);
	nm = zfs_cn_name_kmem(ap->a_cnp, &error);
	if (nm == NULL)
		return (error);
	error = zfs_symlink(VTOZ(ap->a_dvp), nm, ap->a_vap, ap->a_target,
	    &zp, ap->a_cnp->cn_cred, 0);
	zfs_cn_name_free(nm);
	if (error == 0) {
		*ap->a_vpp = ZTOV(zp);
		VOP_UNLOCK(*ap->a_vpp);
	} else {
		*ap->a_vpp = NULL;
	}
	return (error);
}

static int
zfs_netbsd_readlink(void *v)
{
	struct vop_readlink_args *ap = v;
	zfs_uio_t uio;

	zfs_uio_init(&uio, ap->a_uio);
	return (zfs_readlink(ap->a_vp, &uio, ap->a_cred, NULL));
}

static int
zfs_netbsd_link(void *v)
{
	struct vop_link_v2_args *ap = v;
	char *nm;
	int error;

	nm = zfs_cn_name_kmem(ap->a_cnp, &error);
	if (nm == NULL)
		return (error);
	/*
	 * UFS ufs_link raises ADD_LINK before creating the name.
	 * Stock secmodels defer (allow); a custom listener never
	 * saw ZFS hard links without this.
	 */
	error = kauth_authorize_vnode(ap->a_cnp->cn_cred,
	    KAUTH_VNODE_ADD_LINK, ap->a_vp, ap->a_dvp, 0);
	if (error != 0) {
		zfs_cn_name_free(nm);
		return (error);
	}
	error = zfs_link(VTOZ(ap->a_dvp), VTOZ(ap->a_vp), nm,
	    ap->a_cnp->cn_cred, 0);
	zfs_cn_name_free(nm);
	return (error);
}

static int
zfs_netbsd_inactive(void *v)
{
	struct vop_inactive_v2_args *ap = v;
	vnode_t *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	boolean_t recycle = B_FALSE;

	/*
	 * Linked files stay cached (recycle=false) so a later lookup
	 * reuses the znode.  That is wrong during unmount: teardown
	 * waits for z_all_znodes to empty, and reclaim is what takes
	 * a znode off that list.  Recycle unlinked files always, and
	 * every file once the mount is going away, so the last vrele
	 * (including VN_RELE_ASYNC on the zrele taskq) actually
	 * reclaims and the wait can finish.
	 *
	 * Flush a dirty atime here.  Reclaim's zfs_zinactive() only
	 * tears down the SA handle; it does not write atime.  Linux
	 * and FreeBSD both push z_atime_dirty from inactive.  Without
	 * that, a cached file's last access is lost when the vnode
	 * is later reclaimed.
	 */
	if (zp != NULL) {
		zfsvfs = zp->z_zfsvfs;
		if (zfsvfs != NULL && zp->z_sa_hdl != NULL &&
		    zp->z_atime_dirty && !zp->z_unlinked &&
		    !ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs)) {
			dmu_tx_t *tx;
			int error;

			ZFS_TEARDOWN_INACTIVE_ENTER_READ(zfsvfs);
			if (zp->z_sa_hdl != NULL && zp->z_atime_dirty &&
			    !zp->z_unlinked) {
				tx = dmu_tx_create(zfsvfs->z_os);
				dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
				zfs_sa_upgrade_txholds(tx, zp);
				/*
				 * VOP_INACTIVE holds the vnode.  WAIT
				 * here can sit in txg_wait while spa_sync
				 * wants this vnode (putpages / zget) —
				 * unkillable D-state, gtimeout cannot
				 * reap the ZTS case.  NOWAIT: keep the
				 * dirty bit if the tx is full.
				 */
				error = dmu_tx_assign(tx, DMU_TX_NOWAIT);
				if (error) {
					dmu_tx_abort(tx);
				} else {
					(void) sa_update(zp->z_sa_hdl,
					    SA_ZPL_ATIME(zfsvfs),
					    (void *)&zp->z_atime,
					    sizeof (zp->z_atime), tx);
					zp->z_atime_dirty = 0;
					dmu_tx_commit(tx);
				}
			}
			ZFS_TEARDOWN_INACTIVE_EXIT_READ(zfsvfs);
		}
		if (zp->z_nb_orphaned || zp->z_sa_hdl == NULL)
			recycle = B_TRUE;
		else if (zp->z_unlinked)
			recycle = B_TRUE;
		else if (zp->z_zfsvfs != NULL && zp->z_zfsvfs->z_unmounted)
			recycle = B_TRUE;
		else if (vp->v_mount != NULL &&
		    (vp->v_mount->mnt_iflag & IMNT_UNMOUNT) != 0)
			recycle = B_TRUE;
	}
	*ap->a_recycle = recycle;
	return (0);
}

static int
zfs_netbsd_reclaim(void *v)
{
	struct vop_reclaim_v2_args *ap = v;
	vnode_t *vp = ap->a_vp;
	znode_t *zp;
	zfsvfs_t *zfsvfs;

	/*
	 * VOP_RECLAIM v2 is entered locked and must unlock.  Native
	 * NetBSD (tmpfs, 11.0 osnet ZFS) unlocks first.  vcache_reclaim
	 * will not unlock for us (lktype = LK_NONE after return).
	 * Leaving the lock held leaks the VOP_LOCK fstrans hold
	 * (success post is FST_NO).  Process exit then leaves a
	 * trans_cnt>0 fli on the global list; the next dounmount of a
	 * recycled mount waits forever on fstcnt.
	 *
	 * Do not stay locked to "stop namei re-vget": vcache_reclaim
	 * has already set VS_RECLAIMING and purged the name cache.
	 *
	 * zfs_nb_reclaim is this function: unlock, cache_purge,
	 * genfs_node_destroy, then zfs_znode_free / zfs_zinactive
	 * with a NULL-safe sa_hdl.
	 */
	VOP_UNLOCK(vp);

	if (vp->v_data == NULL)
		return (0);
	zp = VTOZ(vp);

	/*
	 * Match FreeBSD: zfsvfs_teardown() may already have destroyed
	 * z_sa_hdl.  zinactive() then NULL-derefs in sa_handle_destroy().
	 * Save zfsvfs -- zfs_znode_free() invalidates zp->z_zfsvfs.
	 */
	cache_purge(vp);
	zfsvfs = zp->z_zfsvfs;
	/*
	 * Resume and suspend already own this lock as writer
	 * (zfsvfs_teardown through zfs_resume_fs).  A reader
	 * enter from cache_purgevfs → vcache_reclaim panics.
	 */
	if (zfsvfs != NULL && !ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs))
		ZFS_TEARDOWN_INACTIVE_ENTER_READ(zfsvfs);
	/*
	 * genfs_node_destroy walks VTOG(vp) == &zp->z_gnode.  Do that
	 * and clear v_data *before* zfs_znode_free, which returns zp
	 * to znode_cache (constructor may reincarnate it as a live
	 * znode on another CPU).
	 */
	genfs_node_destroy(vp);
	vp->v_data = NULL;
	if (zp->z_sa_hdl == NULL)
		zfs_znode_free(zp);
	else
		zfs_zinactive(zp);
	if (zfsvfs != NULL && !ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zfsvfs))
		ZFS_TEARDOWN_INACTIVE_EXIT_READ(zfsvfs);
	return (0);
}

static int
zfs_netbsd_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LONG_MAX;
		return (0);
	case _PC_NAME_MAX: {
		znode_t *nzp = VTOZ(ap->a_vp);
		zfsvfs_t *zfsvfs = (nzp != NULL) ? nzp->z_zfsvfs : NULL;

		*ap->a_retval = (zfsvfs != NULL && zfsvfs->z_longname) ?
		    (ZAP_MAXNAMELEN_NEW - 1) : (MAXNAMELEN - 1);
		return (0);
	}
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
#ifdef _PC_MIN_HOLE_SIZE
	case _PC_MIN_HOLE_SIZE:
		/* 11.0 zfs_pathconf: hole granularity is SPA_MINBLOCKSIZE. */
		*ap->a_retval = SPA_MINBLOCKSIZE;
		return (0);
#endif
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
#ifdef _PC_ACL_EXTENDED
	case _PC_ACL_EXTENDED: {
		znode_t *azp = VTOZ(ap->a_vp);
		zfsvfs_t *azfs = (azp != NULL) ? azp->z_zfsvfs : NULL;

		*ap->a_retval = (azfs != NULL &&
		    azfs->z_acl_type == ZFS_ACLTYPE_POSIX);
		return (0);
	}
#endif
#ifdef _PC_ACL_NFS4
	case _PC_ACL_NFS4: {
		znode_t *azp = VTOZ(ap->a_vp);
		zfsvfs_t *azfs = (azp != NULL) ? azp->z_zfsvfs : NULL;

		*ap->a_retval = (azfs != NULL &&
		    azfs->z_acl_type == ZFS_ACLTYPE_NFSV4);
		return (0);
	}
#endif
#ifdef _PC_ACL_PATH_MAX
	case _PC_ACL_PATH_MAX: {
		znode_t *azp = VTOZ(ap->a_vp);
		zfsvfs_t *azfs = (azp != NULL) ? azp->z_zfsvfs : NULL;

		if (azfs != NULL && (azfs->z_acl_type == ZFS_ACLTYPE_POSIX ||
		    azfs->z_acl_type == ZFS_ACLTYPE_NFSV4))
			*ap->a_retval = ACL_MAX_ENTRIES;
		else
			*ap->a_retval = 3;
		return (0);
	}
#endif
	default:
		switch (ap->a_vp->v_type) {
		case VBLK:
		case VCHR:
			return (VOCALL(spec_vnodeop_p,
			    VOFFSET(vop_pathconf), ap));
		case VFIFO:
			return (VOCALL(fifo_vnodeop_p,
			    VOFFSET(vop_pathconf), ap));
		case VREG:
		case VDIR:
			return (genfs_pathconf(v));
		default:
			return (EOPNOTSUPP);
		}

	}
}

static int
zfs_netbsd_print(void *v)
{
	struct vop_print_args *ap = v;
	vnode_t *vp = ap->a_vp;
	znode_t *zp = VTOZ(ap->a_vp);

	printf("zfs znode %p obj %llu\n", zp,
	    zp != NULL ? (unsigned long long)zp->z_id : 0ULL);

	switch (vp->v_type) {
	case VBLK:
	case VCHR:
		VOCALL(spec_vnodeop_p, VOFFSET(vop_print), ap);
		break;
	case VFIFO:
		VOCALL(fifo_vnodeop_p, VOFFSET(vop_print), ap);
		break;
	default:
		break;
	}

	return (0);
}

static int
zfs_netbsd_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	int error;

	if (ap->a_vp == NULL || ap->a_vp->v_data == NULL)
		return (EOPNOTSUPP);
	zp = VTOZ(ap->a_vp);
	zfsvfs = zp->z_zfsvfs;
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);
	if ((error = zfs_verify_zp(zp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	/*
	 * lf_advlock(F_SETLKW) can wait unbounded.  Holding
	 * z_teardown_lock across that wedges umount (writer).
	 * Snapshot size, drop the enter, then wait.  zp stays
	 * live via the vnode ref, same as tmpfs_advlock.
	 */
	{
		off_t size = zp->z_size;
		struct lockf **lockfp = &zp->z_lockf;

		zfs_exit(zfsvfs, FTAG);
		return (lf_advlock(ap, lockfp, size));
	}
}

/*
 * NetBSD 11 TRIM on a ZFS file: VOP_FDISCARD → zfs_space(F_FREESP).
 * vnode_if.src locks vp exclusive.  Same work as FreeBSD
 * vop_deallocate / Linux fallocate(PUNCH_HOLE).
 */
static int
zfs_netbsd_fdiscard(void *v)
{
	struct vop_fdiscard_args *ap = v;
	flock64_t fl;
	znode_t *zp;

	if (ap->a_vp->v_type != VREG)
		return (EOPNOTSUPP);
	zp = VTOZ(ap->a_vp);
	if (zp == NULL)
		return (EOPNOTSUPP);
	if (ap->a_pos < 0 || ap->a_len <= 0)
		return (EINVAL);
	memset(&fl, 0, sizeof (fl));
	fl.l_whence = SEEK_SET;
	fl.l_start = ap->a_pos;
	fl.l_len = ap->a_len;
	return (zfs_space(zp, F_FREESP, &fl, FWRITE, ap->a_pos, CRED()));
}

/*
 * VOP_FALLOCATE -- Linux zpl_fallocate mode 0: space check + extend.
 * ZFS cannot pin COW blocks; this matches Linux, not a reservation.
 */
static int
zfs_netbsd_fallocate(void *v)
{
	struct vop_fallocate_args *ap = v;
	znode_t *zp;
	zfsvfs_t *zfsvfs;
	uint64_t refd, avail, usedobj, availobj;
	int error;

	if (ap->a_vp->v_type != VREG)
		return (EOPNOTSUPP);
	if (ap->a_pos < 0 || ap->a_len <= 0)
		return (EINVAL);
	zp = VTOZ(ap->a_vp);
	if (zp == NULL)
		return (EOPNOTSUPP);
	zfsvfs = zp->z_zfsvfs;
	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	if (zfs_is_readonly(zfsvfs)) {
		zfs_exit(zfsvfs, FTAG);
		return (EROFS);
	}
	dmu_objset_space(zfsvfs->z_os, &refd, &avail, &usedobj, &availobj);
	if ((uint64_t)ap->a_len > avail) {
		zfs_exit(zfsvfs, FTAG);
		return (ENOSPC);
	}
	if ((uint64_t)ap->a_pos + (uint64_t)ap->a_len > zp->z_size)
		error = zfs_freesp(zp,
		    (uint64_t)ap->a_pos + (uint64_t)ap->a_len, 0, 0, TRUE);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
zfs_netbsd_xattr_name(int ns, const char *name, char *out, size_t outlen)
{
	int n;

	if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL)
		return (EINVAL);
	if (ns == EXTATTR_NAMESPACE_SYSTEM)
		n = snprintf(out, outlen, "system.%s", name);
	else if (ns == EXTATTR_NAMESPACE_USER)
		n = snprintf(out, outlen, "user.%s", name);
	else
		return (EOPNOTSUPP);
	if (n < 0 || (size_t)n >= outlen)
		return (ENAMETOOLONG);
	return (0);
}

#define	ZFS_XATTR_NMLEN	(NAME_MAX + 1)

static char *
zfs_xattr_name_kmem(int ns, const char *name, int *errp)
{
	char *nm = kmem_alloc(ZFS_XATTR_NMLEN, KM_SLEEP);
	int error = zfs_netbsd_xattr_name(ns, name, nm, ZFS_XATTR_NMLEN);

	if (error != 0) {
		kmem_free(nm, ZFS_XATTR_NMLEN);
		*errp = error;
		return (NULL);
	}
	*errp = 0;
	return (nm);
}

static void
zfs_xattr_name_free(char *nm)
{
	if (nm != NULL)
		kmem_free(nm, ZFS_XATTR_NMLEN);
}

/*
 * xattr=sa stores the nvlist in DXATTR.  Values that fit use that
 * path so the ZIL saxattr record matches the bytes.  A value that
 * would push the packed nvlist past the SA limit spills to the
 * directory, as on Linux and FreeBSD.  getextattr reads the SA
 * first, so a spill must drop the old SA name.
 */
static boolean_t
zfs_netbsd_use_sa_xattr(znode_t *zp)
{
	return (zp != NULL && zp->z_is_sa && zp->z_zfsvfs != NULL &&
	    zp->z_zfsvfs->z_xattr_sa);
}

static int
zfs_netbsd_sa_xattr_load(znode_t *zp)
{
	ASSERT(RW_WRITE_HELD(&zp->z_xattr_lock));
	if (zp->z_xattr_cached != NULL)
		return (0);
	return (zfs_sa_get_xattr(zp));
}

static int
zfs_netbsd_sa_xattr_get(znode_t *zp, const char *name, struct uio *uio,
    size_t *sizep)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uchar_t *val = NULL;
	uint_t vlen = 0;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	error = zfs_netbsd_sa_xattr_load(zp);
	if (error == 0) {
		error = nvlist_lookup_byte_array(zp->z_xattr_cached, name,
		    &val, &vlen);
		if (error == 0) {
			if (sizep != NULL)
				*sizep = vlen;
			if (uio != NULL && vlen != 0)
				error = uiomove(val, vlen, uio);
		} else {
			error = ENOATTR;
		}
	}
	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
zfs_netbsd_sa_xattr_set(znode_t *zp, const char *name, struct uio *uio)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uchar_t *buf;
	size_t entry_size;
	int error;

	if (uio == NULL)
		return (EINVAL);
	entry_size = uio->uio_resid;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	error = zfs_netbsd_sa_xattr_load(zp);
	if (error != 0)
		goto out;

	/*
	 * Decide before uiomove().  setextattr spills to the directory
	 * only while uio_resid is still the caller's value.
	 * zfs_sa_set_xattr() still rejects a packed list that does not
	 * fit.  A zero-length value is a NULL buffer: that is what
	 * kmem_alloc(0) returns, and nvlist_add_byte_array() accepts it.
	 */
	error = zfs_sa_xattr_fits(zp, name, entry_size);
	if (error != 0)
		goto out;

	buf = kmem_alloc(entry_size, KM_SLEEP);
	if (entry_size != 0)
		error = uiomove(buf, entry_size, uio);
	if (error == 0)
		error = nvlist_add_byte_array(zp->z_xattr_cached, name,
		    buf, entry_size);
	if (error == 0)
		error = zfs_sa_set_xattr(zp, name, buf, entry_size);
	kmem_free(buf, entry_size);
	if (error != 0 && zp->z_xattr_cached != NULL) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}
out:
	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
zfs_netbsd_sa_xattr_remove(znode_t *zp, const char *name)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	error = zfs_netbsd_sa_xattr_load(zp);
	if (error == 0) {
		error = nvlist_remove(zp->z_xattr_cached, name,
		    DATA_TYPE_BYTE_ARRAY);
		if (error == 0)
			error = zfs_sa_set_xattr(zp, name, NULL, 0);
		else
			error = ENOATTR;
		if (error != 0 && error != ENOATTR &&
		    zp->z_xattr_cached != NULL) {
			nvlist_free(zp->z_xattr_cached);
			zp->z_xattr_cached = NULL;
		}
	}
	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * xattr=sa updates go through zfs_sa_set_xattr, which never checks
 * the mount.  dmu_tx_assign then dsl_dataset_dirty() panics
 * "dirtying snapshot!" (zil.c has the same panic).  The panic sync
 * flushed a torn FFS buffer: login died with
 * "/etc/pwd.db: Inappropriate file type or format".
 * Snapshot cover-mounts are MNT_RDONLY; also refuse a snapshot
 * objset whose vfs flag was lost.
 */
static int
zfs_netbsd_xattr_ro(znode_t *zp)
{
	zfsvfs_t *zfsvfs;

	if (zp == NULL || (zfsvfs = zp->z_zfsvfs) == NULL)
		return (SET_ERROR(EIO));
	/*
	 * A snapshot cover vnode can lose z_vfs.  zfs_is_readonly()
	 * would NULL-deref.  Any read-only zfsvfs, including a
	 * snapshot objset, must fail the write with EROFS before
	 * zfs_sa_set_xattr dirties the dataset.
	 */
	if (zfsvfs->z_issnap)
		return (SET_ERROR(EROFS));
	if (zfsvfs->z_os != NULL && dmu_objset_is_snapshot(zfsvfs->z_os))
		return (SET_ERROR(EROFS));
	if (zfsvfs->z_vfs == NULL || zfs_is_readonly(zfsvfs))
		return (SET_ERROR(EROFS));
	return (0);
}

static const char *
zfs_netbsd_xattr_prefix(int ns, size_t *plen)
{
	if (ns == EXTATTR_NAMESPACE_SYSTEM) {
		*plen = 7;
		return ("system.");
	}
	if (ns == EXTATTR_NAMESPACE_USER) {
		*plen = 5;
		return ("user.");
	}
	*plen = 0;
	return (NULL);
}

static int
zfs_netbsd_xattr_vnode(vnode_t *vp, const char *storename, int create_dir,
    cred_t *cr, vnode_t **vpp)
{
	vnode_t *xvp = NULL, *avp = NULL;
	int error;

	*vpp = NULL;
	error = zfs_lookup(vp, NULL, &xvp, NULL, 0, cr, LOOKUP_XATTR |
	    (create_dir ? CREATE_XATTR_DIR : 0), B_FALSE);
	if (error != 0)
		return (error);
	error = zfs_lookup(xvp, (char *)storename, &avp, NULL, 0, cr, 0,
	    B_FALSE);
	vrele(xvp);
	if (error != 0)
		return (error);
	*vpp = avp;
	return (0);
}

static int
zfs_netbsd_getextattr(void *v)
{
	struct vop_getextattr_args *ap = v;
	vnode_t *avp = NULL;
	znode_t *zp = VTOZ(ap->a_vp);
	int error;
	char *nm;

	/*
	 * If the xattr property is off, refuse the request.  The
	 * directory lookup path checks ZSB_XATTR, but xattr=sa reads
	 * the SA before that lookup and would still return the value.
	 */
	if (!(zp->z_zfsvfs->z_flags & ZSB_XATTR))
		return (SET_ERROR(EOPNOTSUPP));

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace, ap->a_cred,
	    VREAD);
	if (error != 0)
		return (error);
	nm = zfs_xattr_name_kmem(ap->a_attrnamespace, ap->a_name, &error);
	if (nm == NULL)
		return (error);
	if (zfs_netbsd_use_sa_xattr(zp)) {
		error = zfs_netbsd_sa_xattr_get(zp, nm, ap->a_uio, ap->a_size);
		if (error == 0) {
			zfs_xattr_name_free(nm);
			return (0);
		}
		if (error != ENOATTR) {
			zfs_xattr_name_free(nm);
			return (error);
		}
		/* Fall back to a leftover dir xattr. */
	}
	error = zfs_netbsd_xattr_vnode(ap->a_vp, nm, 0, ap->a_cred, &avp);
	zfs_xattr_name_free(nm);
	if (error != 0)
		return (error);
	if (ap->a_uio != NULL) {
		error = vn_lock(avp, LK_SHARED);
		if (error == 0) {
			error = VOP_READ(avp, ap->a_uio, 0, ap->a_cred);
			VOP_UNLOCK(avp);
		}
	}
	if (ap->a_size != NULL && error == 0) {
		vattr_t *va = kmem_alloc(sizeof (*va), KM_SLEEP);

		/* VOP_GETATTR wrapper takes the lock. */
		error = VOP_GETATTR(avp, va, ap->a_cred);
		if (error == 0)
			*ap->a_size = (size_t)va->va_size;
		kmem_free(va, sizeof (*va));
	}
	vrele(avp);
	return (error);
}

static int
zfs_netbsd_xattr_remove(vnode_t *vp, int ns, const char *name, cred_t *cr)
{
	vnode_t *xvp = NULL;
	int error;
	char *nm;

	nm = zfs_xattr_name_kmem(ns, name, &error);
	if (nm == NULL)
		return (error);
	error = zfs_lookup(vp, NULL, &xvp, NULL, 0, cr, LOOKUP_XATTR, B_FALSE);
	if (error != 0) {
		zfs_xattr_name_free(nm);
		return (error);
	}
	error = zfs_remove(VTOZ(xvp), nm, cr, 0);
	zfs_xattr_name_free(nm);
	vrele(xvp);
	return (error);
}

static volatile uint64_t zfs_xattr_tmp_next = 1;

static int
zfs_netbsd_xattr_abort(vnode_t *vp, const char *tmp, cred_t *cr)
{
	vnode_t *xvp = NULL;
	int error;

	error = zfs_lookup(vp, NULL, &xvp, NULL, 0, cr, LOOKUP_XATTR,
	    B_FALSE);
	if (error != 0)
		return (error == ENOENT ? 0 : error);
	error = zfs_remove(VTOZ(xvp), __DECONST(char *, tmp), kcred, 0);
	vrele(xvp);
	return (error == ENOENT ? 0 : error);
}

/*
 * Write uio into a hidden temp object in vp's xattr directory.
 * listextattr only lists system./user. names, so .zxT.* stays
 * invisible.  The existing attribute is not truncated.
 */
static int
zfs_netbsd_xattr_stage(vnode_t *vp, char *tmp, size_t tmplen,
    struct uio *uio, cred_t *cr)
{
	vnode_t *xvp = NULL, *avp = NULL;
	vattr_t va;
	znode_t *zp = NULL;
	uint64_t id;
	int error;

	id = atomic_inc_64_nv(&zfs_xattr_tmp_next);
	if (snprintf(tmp, tmplen, ".zxT.%llx", (unsigned long long)id) >=
	    (int)tmplen)
		return (ENAMETOOLONG);

	error = zfs_lookup(vp, NULL, &xvp, NULL, 0, cr,
	    LOOKUP_XATTR | CREATE_XATTR_DIR, B_FALSE);
	if (error != 0)
		return (error);

	VATTR_NULL(&va);
	va.va_type = VREG;
	va.va_mode = S_IRUSR | S_IWUSR;
	va.va_mask = AT_TYPE | AT_MODE;
	error = zfs_create(VTOZ(xvp), tmp, &va, 0, va.va_mode, &zp, cr, 0,
	    NULL);
	if (error == EEXIST) {
		error = zfs_remove(VTOZ(xvp), tmp, kcred, 0);
		if (error == 0 || error == ENOENT)
			error = zfs_create(VTOZ(xvp), tmp, &va, 0,
			    va.va_mode, &zp, cr, 0, NULL);
	}
	if (error != 0) {
		vrele(xvp);
		return (error);
	}
	avp = ZTOV(zp);
	VOP_UNLOCK(avp);

	error = vn_lock(avp, LK_EXCLUSIVE);
	if (error != 0) {
		(void) zfs_remove(VTOZ(xvp), tmp, kcred, 0);
		vrele(avp);
		vrele(xvp);
		return (error);
	}
	error = 0;
	if (uio != NULL && uio->uio_resid != 0) {
		error = VOP_WRITE(avp, uio, IO_SYNC | IO_UNIT, cr);
		if (error == 0 && uio->uio_resid != 0)
			error = EIO;
	}
	VOP_UNLOCK(avp);
	vrele(avp);
	if (error != 0)
		(void) zfs_remove(VTOZ(xvp), tmp, kcred, 0);
	vrele(xvp);
	return (error);
}

/*
 * Atomically publish a staged temp over the live name.  Same-dir
 * rename is one DMU transaction: the old object is unlinked only
 * if the new name is installed.  kcred skips xattr-dir ACE
 * checks; the VOP already ran extattr_check_cred / setdac.
 */
static int
zfs_netbsd_xattr_commit(vnode_t *vp, const char *from, const char *to,
    cred_t *cr)
{
	vnode_t *xvp = NULL, *svp = NULL, *tvp = NULL;
	struct componentname scn, tcn;
	int error;

	error = zfs_lookup(vp, NULL, &xvp, NULL, 0, cr, LOOKUP_XATTR,
	    B_FALSE);
	if (error != 0)
		return (error);

	error = zfs_lookup(xvp, from, &svp, NULL, 0, kcred, 0, B_FALSE);
	if (error != 0) {
		vrele(xvp);
		return (error);
	}
	error = zfs_lookup(xvp, to, &tvp, NULL, 0, kcred, 0, B_FALSE);
	if (error == ENOENT) {
		tvp = NULL;
		error = 0;
	} else if (error != 0) {
		vrele(svp);
		vrele(xvp);
		return (error);
	}

	memset(&scn, 0, sizeof (scn));
	memset(&tcn, 0, sizeof (tcn));
	scn.cn_nameptr = __DECONST(char *, from);
	scn.cn_namelen = strlen(from);
	scn.cn_nameiop = DELETE;
	scn.cn_flags = ISLASTCN;
	scn.cn_cred = kcred;
	tcn.cn_nameptr = __DECONST(char *, to);
	tcn.cn_namelen = strlen(to);
	tcn.cn_nameiop = RENAME;
	tcn.cn_flags = ISLASTCN;
	tcn.cn_cred = kcred;

	error = vn_lock(xvp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0) {
		vrele(svp);
		if (tvp != NULL)
			vrele(tvp);
		vrele(xvp);
		return (error);
	}
	if (tvp != NULL) {
		error = vn_lock(tvp, LK_EXCLUSIVE | LK_RETRY);
		if (error != 0) {
			VOP_UNLOCK(xvp);
			vrele(svp);
			vrele(tvp);
			vrele(xvp);
			return (error);
		}
	}

	error = zfs_do_rename(xvp, &svp, &scn, xvp, &tvp, &tcn, kcred);
	vrele(svp);
	if (tvp != NULL)
		vrele(tvp);
	vrele(xvp);
	return (error);
}

static int
zfs_netbsd_deleteextattr(void *v)
{
	struct vop_deleteextattr_args *ap = v;
	znode_t *zp = VTOZ(ap->a_vp);
	char *nm;
	int error;

	if (!(zp->z_zfsvfs->z_flags & ZSB_XATTR))
		return (SET_ERROR(EOPNOTSUPP));

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace, ap->a_cred,
	    VWRITE);
	if (error != 0)
		return (error);
	/*
	 * Snapshot + xattr=sa: sa remove dirties DXATTR and panics
	 * in dsl_dataset_dirty.  See zfs_netbsd_xattr_ro.
	 */
	error = zfs_netbsd_xattr_ro(zp);
	if (error != 0)
		return (error);
	if (zfs_netbsd_use_sa_xattr(zp)) {
		nm = zfs_xattr_name_kmem(ap->a_attrnamespace, ap->a_name,
		    &error);
		if (nm == NULL)
			return (error);
		error = zfs_netbsd_sa_xattr_remove(zp, nm);
		zfs_xattr_name_free(nm);
		if (error == 0 || error != ENOATTR)
			return (error);
	}
	return (zfs_netbsd_xattr_remove(ap->a_vp, ap->a_attrnamespace,
	    ap->a_name, ap->a_cred));
}

static int
zfs_netbsd_setextattr(void *v)
{
	struct vop_setextattr_args *ap = v;
	znode_t *zp = VTOZ(ap->a_vp);
	char *nm;
	char *tmp;
	size_t resid;
	int error;

	if (!(zp->z_zfsvfs->z_flags & ZSB_XATTR))
		return (SET_ERROR(EOPNOTSUPP));

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace, ap->a_cred,
	    VWRITE);
	if (error != 0)
		return (error);
	/*
	 * Snapshot + xattr=sa: zfs_sa_set_xattr does not check the
	 * mount, so dsl_dataset_dirty panics "dirtying snapshot!".
	 * See zfs_netbsd_xattr_ro.
	 */
	error = zfs_netbsd_xattr_ro(zp);
	if (error != 0)
		return (error);
	if (ap->a_uio == NULL) {
		if (zfs_netbsd_use_sa_xattr(zp)) {
			nm = zfs_xattr_name_kmem(ap->a_attrnamespace,
			    ap->a_name, &error);
			if (nm == NULL)
				return (error);
			error = zfs_netbsd_sa_xattr_remove(zp, nm);
			zfs_xattr_name_free(nm);
			if (error == 0 || error != ENOATTR)
				return (error);
		}
		return (zfs_netbsd_xattr_remove(ap->a_vp, ap->a_attrnamespace,
		    ap->a_name, ap->a_cred));
	}
	nm = zfs_xattr_name_kmem(ap->a_attrnamespace, ap->a_name, &error);
	if (nm == NULL)
		return (error);
	if (zfs_netbsd_use_sa_xattr(zp)) {
		resid = ap->a_uio->uio_resid;
		error = zfs_netbsd_sa_xattr_set(zp, nm, ap->a_uio);
		if (error == 0) {
			/*
			 * SA holds the value getextattr returns.
			 * Drop an older directory copy.  None is fine.
			 */
			(void) zfs_netbsd_xattr_remove(ap->a_vp,
			    ap->a_attrnamespace, ap->a_name, ap->a_cred);
			zfs_xattr_name_free(nm);
			return (0);
		}
		/*
		 * EFBIG is before uiomove, so resid is unchanged.
		 * Spill to the directory.  Any other error, or a
		 * resid that moved, is returned as-is.
		 */
		if (error != EFBIG || ap->a_uio->uio_resid != resid) {
			zfs_xattr_name_free(nm);
			return (error);
		}
	}
	/*
	 * Stage into a temp object, then rename over the live name.
	 * Truncate-then-write destroyed the previous value if the
	 * new write failed (ENOSPC, EFAULT, short I/O).
	 */
	tmp = kmem_alloc(ZFS_XATTR_NMLEN, KM_SLEEP);
	error = zfs_netbsd_xattr_stage(ap->a_vp, tmp, ZFS_XATTR_NMLEN,
	    ap->a_uio, ap->a_cred);
	if (error == 0) {
		error = zfs_netbsd_xattr_commit(ap->a_vp, tmp, nm,
		    ap->a_cred);
		if (error != 0)
			(void) zfs_netbsd_xattr_abort(ap->a_vp, tmp,
			    ap->a_cred);
	}
	kmem_free(tmp, ZFS_XATTR_NMLEN);
	if (error == 0 && zfs_netbsd_use_sa_xattr(zp)) {
		int rerr;

		/*
		 * Directory holds the new bytes.  A leftover SA
		 * copy would hide them.  ENOATTR: nothing was there.
		 */
		rerr = zfs_netbsd_sa_xattr_remove(zp, nm);
		if (rerr != 0 && rerr != ENOATTR)
			error = rerr;
	}
	zfs_xattr_name_free(nm);
	return (error);
}

#ifndef EXTATTR_LIST_LENPREFIX
#define	EXTATTR_LIST_LENPREFIX	1
#endif

/*
 * extattr_list_file passes EXTATTR_LIST_LENPREFIX (one length byte, no
 * NUL).  llistxattr passes flag 0 and wants a trailing NUL, same as
 * ufs_extattr_list.  Emitting the length byte on the Linux path makes
 * rsync lgetxattr the name "\010fileattr" and get ENOATTR.
 */
static int
zfs_netbsd_xattr_emit(const char *name, size_t slen, int flag,
    struct uio *uio, size_t *sizep)
{
	int error = 0;

	if (slen == 0 || slen > 255)
		return (0);
	if (sizep != NULL)
		*sizep += slen + 1;
	if (uio == NULL)
		return (0);
	if ((flag & EXTATTR_LIST_LENPREFIX) != 0) {
		uint8_t nlen = (uint8_t)slen;

		error = uiomove(&nlen, 1, uio);
		if (error == 0)
			error = uiomove(__UNCONST(name), slen, uio);
	} else {
		char z = '\0';

		error = uiomove(__UNCONST(name), slen, uio);
		if (error == 0)
			error = uiomove(&z, 1, uio);
	}
	return (error);
}

/*
 * xattr=sa names live in the DXATTR nvlist, not the xattr directory.
 * listxattr has to return both or rsync -X / lsextattr miss them.
 */
static int
zfs_netbsd_listextattr_sa(znode_t *zp, const char *pfx, size_t plen,
    int flag, struct uio *uio, size_t *sizep)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	nvpair_t *nvp = NULL;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	error = zfs_netbsd_sa_xattr_load(zp);
	if (error == 0 && zp->z_xattr_cached != NULL) {
		while ((nvp = nvlist_next_nvpair(zp->z_xattr_cached,
		    nvp)) != NULL) {
			const char *name = nvpair_name(nvp);
			const char *shown;
			size_t slen;

			if (strncmp(name, pfx, plen) != 0)
				continue;
			shown = name + plen;
			slen = strlen(shown);
			error = zfs_netbsd_xattr_emit(shown, slen, flag,
			    uio, sizep);
			if (error != 0)
				break;
		}
	}
	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
zfs_netbsd_listextattr(void *v)
{
	struct vop_listextattr_args *ap = v;
	vnode_t *xvp = NULL;
	znode_t *dzp;
	zfsvfs_t *zfsvfs;
	zap_cursor_t zc;
	zap_attribute_t *za;
	const char *pfx;
	size_t plen;
	int error, zerr;

	if (!(VTOZ(ap->a_vp)->z_zfsvfs->z_flags & ZSB_XATTR))
		return (SET_ERROR(EOPNOTSUPP));

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace, ap->a_cred,
	    VREAD);
	if (error != 0)
		return (error);
	pfx = zfs_netbsd_xattr_prefix(ap->a_attrnamespace, &plen);
	if (pfx == NULL)
		return (EOPNOTSUPP);
	/*
	 * No xattr directory is an empty list, not an error.  zfs_get_xattrdir
	 * returns ENOATTR (not ENOENT) on that path.  rsync -X treats any
	 * listxattr error as fatal (slog_replay_fs_001/002: ENOATTR 93 on
	 * "/testpool/testfs/.").
	 */
	if (ap->a_size != NULL)
		*ap->a_size = 0;
	if (zfs_netbsd_use_sa_xattr(VTOZ(ap->a_vp))) {
		error = zfs_netbsd_listextattr_sa(VTOZ(ap->a_vp), pfx, plen,
		    ap->a_flag, ap->a_uio, ap->a_size);
		if (error != 0)
			return (error);
	}
	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred,
	    LOOKUP_XATTR, B_FALSE);
	if (error == ENOENT || error == ENOATTR)
		return (0);
	if (error != 0)
		return (error);
	dzp = VTOZ(xvp);
	zfsvfs = dzp->z_zfsvfs;
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0) {
		vrele(xvp);
		return (error);
	}
	error = vn_lock(xvp, LK_SHARED);
	if (error != 0) {
		zfs_exit(zfsvfs, FTAG);
		vrele(xvp);
		return (error);
	}
	za = zap_attribute_alloc();
	zap_cursor_init(&zc, zfsvfs->z_os, dzp->z_id);
	for (;;) {
		const char *shown;
		size_t slen;

		zerr = zap_cursor_retrieve(&zc, za);
		if (zerr == ENOENT)
			break;
		if (zerr != 0) {
			error = zerr;
			break;
		}
		if (strncmp(za->za_name, pfx, plen) == 0) {
			shown = za->za_name + plen;
			slen = strlen(shown);
			error = zfs_netbsd_xattr_emit(shown, slen, ap->a_flag,
			    ap->a_uio, ap->a_size);
			if (error != 0)
				break;
		}
		zap_cursor_advance(&zc);
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	VOP_UNLOCK(xvp);
	zfs_exit(zfsvfs, FTAG);
	vrele(xvp);
	return (error);
}

#ifndef ENOATTR
#define	ENOATTR	ENOENT
#endif

#define	POSIX_ACL_XATTR_VERSION	2

struct posix_acl_xattr_hdr {
	uint32_t	a_version;
} __packed;

struct posix_acl_xattr_ent {
	uint16_t	e_tag;
	uint16_t	e_perm;
	uint32_t	e_id;
} __packed;

static const char *
zfs_netbsd_acl_xattr(acl_type_t type)
{

	if (type == ACL_TYPE_ACCESS || type == ACL_TYPE_ACCESS_OLD)
		return ("system.posix_acl_access");
	if (type == ACL_TYPE_DEFAULT || type == ACL_TYPE_DEFAULT_OLD)
		return ("system.posix_acl_default");
	return (NULL);
}

static int
zfs_netbsd_acl_from_xattr(const void *buf, size_t len, struct acl *aclp)
{
	const struct posix_acl_xattr_hdr *hdr = buf;
	const struct posix_acl_xattr_ent *ent;
	size_t payload, n;
	unsigned i;

	if (len < sizeof (*hdr))
		return (EINVAL);
	if (le32toh(hdr->a_version) != POSIX_ACL_XATTR_VERSION)
		return (EINVAL);
	payload = len - sizeof (*hdr);
	if (payload % sizeof (*ent) != 0)
		return (EINVAL);
	n = payload / sizeof (*ent);
	if (n > ACL_MAX_ENTRIES)
		return (ENOSPC);
	ent = (const struct posix_acl_xattr_ent *)(hdr + 1);
	memset(aclp, 0, sizeof (*aclp));
	aclp->acl_maxcnt = ACL_MAX_ENTRIES;
	aclp->acl_cnt = (int)n;
	for (i = 0; i < n; i++) {
		aclp->acl_entry[i].ae_tag = le16toh(ent[i].e_tag);
		aclp->acl_entry[i].ae_perm = le16toh(ent[i].e_perm);
		aclp->acl_entry[i].ae_id = le32toh(ent[i].e_id);
	}
	return (acl_posix1e_check(aclp));
}

static int
zfs_netbsd_acl_to_xattr(const struct acl *aclp, void **bufp, size_t *lenp)
{
	struct posix_acl_xattr_hdr *hdr;
	struct posix_acl_xattr_ent *ent;
	size_t len;
	unsigned i;

	if (aclp->acl_cnt > ACL_MAX_ENTRIES)
		return (EINVAL);
	len = sizeof (*hdr) + aclp->acl_cnt * sizeof (*ent);
	hdr = kmem_zalloc(len, KM_SLEEP);
	hdr->a_version = htole32(POSIX_ACL_XATTR_VERSION);
	ent = (struct posix_acl_xattr_ent *)(hdr + 1);
	for (i = 0; i < aclp->acl_cnt; i++) {
		ent[i].e_tag = htole16((uint16_t)aclp->acl_entry[i].ae_tag);
		ent[i].e_perm = htole16((uint16_t)aclp->acl_entry[i].ae_perm);
		ent[i].e_id = htole32(aclp->acl_entry[i].ae_id);
	}
	*bufp = hdr;
	*lenp = len;
	return (0);
}

static int
zfs_netbsd_acl_read_xattr(vnode_t *vp, const char *name, cred_t *cr,
    void **bufp, size_t *lenp)
{
	vnode_t *avp = NULL;
	struct vattr va;
	struct uio auio;
	struct iovec iov;
	void *buf;
	size_t alloc_len;
	int error;

	*bufp = NULL;
	*lenp = 0;
	error = zfs_netbsd_xattr_vnode(vp, name, 0, cr, &avp);
	if (error != 0)
		return (error);
	error = vn_lock(avp, LK_SHARED);
	if (error != 0) {
		vrele(avp);
		return (error);
	}
	error = VOP_GETATTR(avp, &va, cr);
	if (error != 0 || va.va_size == 0) {
		VOP_UNLOCK(avp);
		vrele(avp);
		return (error != 0 ? error : ENOATTR);
	}
	if (va.va_size > sizeof (struct posix_acl_xattr_hdr) +
	    (size_t)ACL_MAX_ENTRIES * sizeof (struct posix_acl_xattr_ent)) {
		VOP_UNLOCK(avp);
		vrele(avp);
		return (ENOSPC);
	}
	alloc_len = (size_t)va.va_size;
	buf = kmem_zalloc(alloc_len, KM_SLEEP);
	memset(&auio, 0, sizeof (auio));
	iov.iov_base = buf;
	iov.iov_len = alloc_len;
	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = alloc_len;
	auio.uio_rw = UIO_READ;
	auio.uio_vmspace = vmspace_kernel();
	error = VOP_READ(avp, &auio, 0, cr);
	VOP_UNLOCK(avp);
	vrele(avp);
	if (error == 0 && auio.uio_resid != 0)
		error = EIO;
	if (error != 0) {
		kmem_free(buf, alloc_len);
		return (error);
	}
	*bufp = buf;
	*lenp = alloc_len;
	return (0);
}

static void
zfs_netbsd_acl_from_mode(mode_t mode, struct acl *aclp)
{

	memset(aclp, 0, sizeof (*aclp));
	aclp->acl_maxcnt = ACL_MAX_ENTRIES;
	aclp->acl_cnt = 3;
	aclp->acl_entry[0].ae_tag = ACL_USER_OBJ;
	aclp->acl_entry[0].ae_id = ACL_UNDEFINED_ID;
	aclp->acl_entry[0].ae_perm = ((mode & S_IRUSR) ? ACL_READ : 0) |
	    ((mode & S_IWUSR) ? ACL_WRITE : 0) |
	    ((mode & S_IXUSR) ? ACL_EXECUTE : 0);
	aclp->acl_entry[1].ae_tag = ACL_GROUP_OBJ;
	aclp->acl_entry[1].ae_id = ACL_UNDEFINED_ID;
	aclp->acl_entry[1].ae_perm = ((mode & S_IRGRP) ? ACL_READ : 0) |
	    ((mode & S_IWGRP) ? ACL_WRITE : 0) |
	    ((mode & S_IXGRP) ? ACL_EXECUTE : 0);
	aclp->acl_entry[2].ae_tag = ACL_OTHER;
	aclp->acl_entry[2].ae_id = ACL_UNDEFINED_ID;
	aclp->acl_entry[2].ae_perm = ((mode & S_IROTH) ? ACL_READ : 0) |
	    ((mode & S_IWOTH) ? ACL_WRITE : 0) |
	    ((mode & S_IXOTH) ? ACL_EXECUTE : 0);
}

static mode_t
zfs_netbsd_acl_to_mode(const struct acl *aclp)
{
	mode_t mode = 0;
	acl_perm_t mask = 0;
	int have_mask = 0;
	unsigned i;

	for (i = 0; i < aclp->acl_cnt; i++) {
		if (aclp->acl_entry[i].ae_tag == ACL_MASK) {
			mask = aclp->acl_entry[i].ae_perm;
			have_mask = 1;
			break;
		}
	}
	for (i = 0; i < aclp->acl_cnt; i++) {
		acl_perm_t p = aclp->acl_entry[i].ae_perm;

		switch (aclp->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			if (p & ACL_READ)
				mode |= S_IRUSR;
			if (p & ACL_WRITE)
				mode |= S_IWUSR;
			if (p & ACL_EXECUTE)
				mode |= S_IXUSR;
			break;
		case ACL_GROUP_OBJ:
			if (!have_mask) {
				if (p & ACL_READ)
					mode |= S_IRGRP;
				if (p & ACL_WRITE)
					mode |= S_IWGRP;
				if (p & ACL_EXECUTE)
					mode |= S_IXGRP;
			}
			break;
		case ACL_OTHER:
			if (p & ACL_READ)
				mode |= S_IROTH;
			if (p & ACL_WRITE)
				mode |= S_IWOTH;
			if (p & ACL_EXECUTE)
				mode |= S_IXOTH;
			break;
		default:
			break;
		}
	}
	if (have_mask) {
		if (mask & ACL_READ)
			mode |= S_IRGRP;
		if (mask & ACL_WRITE)
			mode |= S_IWGRP;
		if (mask & ACL_EXECUTE)
			mode |= S_IXGRP;
	}
	return (mode);
}

static int
zfs_netbsd_posix_acl_allows(const struct acl *aclp, uid_t owner, gid_t group,
    cred_t *cr, accmode_t accmode)
{
	acl_perm_t want = 0, allow = 0, mask = 0;
	int have_mask = 0, matched_group = 0;
	unsigned i;
	uid_t euid = crgetuid(cr);

	if (accmode & VREAD)
		want |= ACL_READ;
	if (accmode & (VWRITE | VAPPEND))
		want |= ACL_WRITE;
	if (accmode & VEXEC)
		want |= ACL_EXECUTE;
	if (want == 0)
		return (0);

	for (i = 0; i < aclp->acl_cnt; i++) {
		if (aclp->acl_entry[i].ae_tag == ACL_MASK) {
			mask = aclp->acl_entry[i].ae_perm;
			have_mask = 1;
			break;
		}
	}

	if (euid == owner) {
		for (i = 0; i < aclp->acl_cnt; i++) {
			if (aclp->acl_entry[i].ae_tag == ACL_USER_OBJ)
				return ((want & aclp->acl_entry[i].ae_perm) ==
				    want ? 0 : EACCES);
		}
		return (EACCES);
	}

	for (i = 0; i < aclp->acl_cnt; i++) {
		if (aclp->acl_entry[i].ae_tag == ACL_USER &&
		    aclp->acl_entry[i].ae_id == euid) {
			allow = aclp->acl_entry[i].ae_perm;
			if (have_mask)
				allow &= mask;
			return ((want & allow) == want ? 0 : EACCES);
		}
	}

	if (zfs_cr_groupmember(group, cr)) {
		for (i = 0; i < aclp->acl_cnt; i++) {
			if (aclp->acl_entry[i].ae_tag == ACL_GROUP_OBJ) {
				acl_perm_t p = aclp->acl_entry[i].ae_perm;

				if (have_mask)
					p &= mask;
				matched_group = 1;
				if ((want & p) == want)
					return (0);
			}
		}
	}
	for (i = 0; i < aclp->acl_cnt; i++) {
		if (aclp->acl_entry[i].ae_tag == ACL_GROUP &&
		    zfs_cr_groupmember(aclp->acl_entry[i].ae_id, cr)) {
			acl_perm_t p = aclp->acl_entry[i].ae_perm;

			if (have_mask)
				p &= mask;
			matched_group = 1;
			if ((want & p) == want)
				return (0);
		}
	}
	if (matched_group)
		return (EACCES);

	for (i = 0; i < aclp->acl_cnt; i++) {
		if (aclp->acl_entry[i].ae_tag == ACL_OTHER)
			return ((want & aclp->acl_entry[i].ae_perm) ==
			    want ? 0 : EACCES);
	}
	return (EACCES);
}

int
zfs_netbsd_posix_access(vnode_t *vp, accmode_t accmode, cred_t *cr)
{
	znode_t *zp;
	struct acl *acl;
	void *buf = NULL;
	void *cached;
	size_t len = 0;
	uid_t owner;
	gid_t group;
	int error;
	boolean_t from_mode = B_FALSE;
	uint64_t gen;

	if (vp == NULL)
		return (EACCES);
	zp = VTOZ(vp);
	if (zp == NULL)
		return (EACCES);
	owner = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_uid, cr, ZFS_OWNER);
	group = zfs_fuid_map_id(zp->z_zfsvfs, zp->z_gid, cr, ZFS_GROUP);
	acl = kmem_zalloc(sizeof (*acl), KM_SLEEP);

	mutex_enter(&zp->z_acl_lock);
	cached = zp->z_posix_acl;
	gen = zp->z_posix_acl_gen;
	if (cached == ZFS_NETBSD_POSIX_ACL_MODE) {
		mutex_exit(&zp->z_acl_lock);
		zfs_netbsd_acl_from_mode((mode_t)zp->z_mode, acl);
		error = zfs_netbsd_posix_acl_allows(acl, owner, group, cr,
		    accmode);
		kmem_free(acl, sizeof (*acl));
		return (error);
	}
	if (cached != NULL) {
		*acl = *(struct acl *)cached;
		mutex_exit(&zp->z_acl_lock);
		error = zfs_netbsd_posix_acl_allows(acl, owner, group, cr,
		    accmode);
		kmem_free(acl, sizeof (*acl));
		return (error);
	}
	mutex_exit(&zp->z_acl_lock);

	if (zp->z_zfsvfs != NULL &&
	    zp->z_zfsvfs->z_acl_type == ZFS_ACLTYPE_POSIX) {
		error = zfs_netbsd_acl_read_xattr(vp, "system.posix_acl_access",
		    cr, &buf, &len);
		if (error == 0) {
			error = zfs_netbsd_acl_from_xattr(buf, len, acl);
			kmem_free(buf, len);
			if (error != 0) {
				kmem_free(acl, sizeof (*acl));
				return (error);
			}
		} else if (error == ENOENT || error == ENOATTR) {
			zfs_netbsd_acl_from_mode((mode_t)zp->z_mode, acl);
			from_mode = B_TRUE;
		} else {
			kmem_free(acl, sizeof (*acl));
			return (error);
		}
	} else {
		zfs_netbsd_acl_from_mode((mode_t)zp->z_mode, acl);
		from_mode = B_TRUE;
	}

	/*
	 * Only cache if no purge ran during the unlocked xattr read.
	 * Otherwise a concurrent setfacl can have this thread restore
	 * the pre-update ACL.
	 */
	mutex_enter(&zp->z_acl_lock);
	if (zp->z_posix_acl_gen == gen && zp->z_posix_acl == NULL) {
		if (from_mode) {
			zp->z_posix_acl = ZFS_NETBSD_POSIX_ACL_MODE;
		} else {
			struct acl *copy;

			mutex_exit(&zp->z_acl_lock);
			copy = kmem_alloc(sizeof (*copy), KM_SLEEP);
			*copy = *acl;
			mutex_enter(&zp->z_acl_lock);
			if (zp->z_posix_acl_gen == gen &&
			    (zp->z_posix_acl == NULL ||
			    zp->z_posix_acl == ZFS_NETBSD_POSIX_ACL_MODE)) {
				zp->z_posix_acl = copy;
				copy = NULL;
			}
			mutex_exit(&zp->z_acl_lock);
			if (copy != NULL)
				kmem_free(copy, sizeof (*copy));
			goto allowed;
		}
	}
	mutex_exit(&zp->z_acl_lock);

allowed:
	error = zfs_netbsd_posix_acl_allows(acl, owner, group, cr, accmode);
	kmem_free(acl, sizeof (*acl));
	return (error);
}

void
zfs_netbsd_posix_acl_purge(znode_t *zp)
{
	void *p;

	if (zp == NULL)
		return;
	mutex_enter(&zp->z_acl_lock);
	p = zp->z_posix_acl;
	zp->z_posix_acl = NULL;
	zp->z_posix_acl_gen++;
	mutex_exit(&zp->z_acl_lock);
	if (p != NULL && p != ZFS_NETBSD_POSIX_ACL_MODE)
		kmem_free(p, sizeof (struct acl));
}

static int
zfs_netbsd_acl_check(vnode_t *vp, acl_type_t type, struct acl *aclp)
{

	if (type != ACL_TYPE_ACCESS && type != ACL_TYPE_DEFAULT &&
	    type != ACL_TYPE_ACCESS_OLD && type != ACL_TYPE_DEFAULT_OLD)
		return (EINVAL);
	if ((type == ACL_TYPE_DEFAULT || type == ACL_TYPE_DEFAULT_OLD) &&
	    vp->v_type != VDIR)
		return (EINVAL);
	if (aclp == NULL)
		return (EINVAL);
	return (acl_posix1e_check(aclp));
}

/*
 * NFSv4 ACLs live in the ZPL ace_t, not POSIX xattrs.  Same mapping
 * as FreeBSD zfs_freebsd_getacl/setacl: zfs_getsecattr / setsecattr
 * plus acl_from_aces / aces_from_acl.  acltype=off is EOPNOTSUPP on
 * the VOP (Linux-like) but zfs_zaccess still consults stored ACEs.
 */
static int
zfs_netbsd_getacl_nfsv4(struct vop_getacl_args *ap)
{
	vsecattr_t vsecattr;
	int error;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EINVAL);

	memset(&vsecattr, 0, sizeof (vsecattr));
	vsecattr.vsa_mask = VSA_ACE | VSA_ACECNT;
	error = zfs_getsecattr(VTOZ(ap->a_vp), &vsecattr, 0, ap->a_cred);
	if (error != 0)
		return (error);

	error = acl_from_aces(ap->a_aclp, vsecattr.vsa_aclentp,
	    vsecattr.vsa_aclcnt);
	if (vsecattr.vsa_aclentp != NULL)
		kmem_free(vsecattr.vsa_aclentp, vsecattr.vsa_aclentsz);
	return (error);
}

static int
zfs_netbsd_setacl_nfsv4(struct vop_setacl_args *ap)
{
	vsecattr_t vsecattr;
	size_t aclbsize;
	int error;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EINVAL);
	if (ap->a_aclp == NULL)
		return (EINVAL);
	if (ap->a_aclp->acl_cnt < 1 ||
	    ap->a_aclp->acl_cnt > ACL_MAX_ENTRIES)
		return (EINVAL);
	/*
	 * chmod(2) may split every entry and append the canonical
	 * six.  Refuse an ACL that would overflow (UFS/FreeBSD).
	 */
	if (ap->a_aclp->acl_cnt * 2 + 6 > ACL_MAX_ENTRIES)
		return (ENOSPC);

	error = acl_nfs4_check(ap->a_aclp, ap->a_vp->v_type == VDIR);
	if (error != 0)
		return (error);

	memset(&vsecattr, 0, sizeof (vsecattr));
	vsecattr.vsa_mask = VSA_ACE;
	aclbsize = (size_t)ap->a_aclp->acl_cnt * sizeof (ace_t);
	vsecattr.vsa_aclentp = kmem_alloc(aclbsize, KM_SLEEP);
	vsecattr.vsa_aclentsz = aclbsize;
	aces_from_acl(vsecattr.vsa_aclentp, &vsecattr.vsa_aclcnt, ap->a_aclp);
	error = zfs_setsecattr(VTOZ(ap->a_vp), &vsecattr, 0, ap->a_cred);
	kmem_free(vsecattr.vsa_aclentp, aclbsize);
	if (error == 0)
		zfs_znode_cache_id(VTOZ(ap->a_vp));
	return (error);
}

static int
zfs_netbsd_getacl(void *v)
{
	struct vop_getacl_args *ap = v;
	znode_t *zp = VTOZ(ap->a_vp);
	zfsvfs_t *zfsvfs;
	const char *name;
	void *buf = NULL;
	size_t len = 0;
	int error;

	if (zp == NULL)
		return (EOPNOTSUPP);
	zfsvfs = zp->z_zfsvfs;
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_NFSV4:
		return (zfs_netbsd_getacl_nfsv4(ap));
	case ZFS_ACLTYPE_POSIX:
		if (ap->a_type == ACL_TYPE_NFS4)
			return (EINVAL);
		break;
	default:
		/*
		 * acltype=off stores no POSIX ACL.  EOPNOTSUPP makes
		 * NetBSD rsync -A exit 23 (slog_replay_fs_001/002).
		 * A file with no ACL is the mode bits; an empty
		 * default ACL is "none".
		 */
		if (ap->a_type == ACL_TYPE_NFS4)
			return (EOPNOTSUPP);
		if (ap->a_type == ACL_TYPE_DEFAULT ||
		    ap->a_type == ACL_TYPE_DEFAULT_OLD) {
			if (ap->a_vp->v_type != VDIR)
				return (EINVAL);
			memset(ap->a_aclp, 0, sizeof (*ap->a_aclp));
			ap->a_aclp->acl_maxcnt = ACL_MAX_ENTRIES;
			ap->a_aclp->acl_cnt = 0;
			return (0);
		}
		if (ap->a_type != ACL_TYPE_ACCESS &&
		    ap->a_type != ACL_TYPE_ACCESS_OLD)
			return (EINVAL);
		zfs_netbsd_acl_from_mode((mode_t)zp->z_mode, ap->a_aclp);
		return (0);
	}
	name = zfs_netbsd_acl_xattr(ap->a_type);
	if (name == NULL)
		return (EINVAL);
	if ((ap->a_type == ACL_TYPE_DEFAULT ||
	    ap->a_type == ACL_TYPE_DEFAULT_OLD) &&
	    ap->a_vp->v_type != VDIR)
		return (EINVAL);
	error = zfs_netbsd_acl_read_xattr(ap->a_vp, name, ap->a_cred,
	    &buf, &len);
	if (error == ENOENT || error == ENOATTR) {
		if (ap->a_type == ACL_TYPE_DEFAULT ||
		    ap->a_type == ACL_TYPE_DEFAULT_OLD) {
			/*
			 * POSIX.1e: a missing default ACL is success
			 * with an empty ACL, not ENOATTR.  FFS
			 * ufs_getacl_posix1e does the same.  Linux
			 * getxattr returns ENODATA and GNU libacl
			 * turns that into empty; NetBSD tools take
			 * the VOP result literally.
			 */
			memset(ap->a_aclp, 0, sizeof (*ap->a_aclp));
			ap->a_aclp->acl_maxcnt = ACL_MAX_ENTRIES;
			ap->a_aclp->acl_cnt = 0;
			return (0);
		}
		/*
		 * Missing access ACL: POSIX.1e synthesizes a
		 * mode-derived 3-entry ACL.
		 */
		zfs_netbsd_acl_from_mode((mode_t)zp->z_mode, ap->a_aclp);
		return (0);
	}
	if (error != 0)
		return (error);
	error = zfs_netbsd_acl_from_xattr(buf, len, ap->a_aclp);
	kmem_free(buf, len);
	return (error);
}

static int
zfs_netbsd_setacl(void *v)
{
	struct vop_setacl_args *ap = v;
	znode_t *zp = VTOZ(ap->a_vp);
	zfsvfs_t *zfsvfs;
	const char *name;
	void *buf = NULL;
	size_t len = 0;
	vnode_t *xvp = NULL;
	struct uio auio;
	struct iovec iov;
	char *tmp;
	mode_t oldmode = 0;
	int error, access_acl;

	if (zp == NULL)
		return (EOPNOTSUPP);
	zfsvfs = zp->z_zfsvfs;
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_NFSV4:
		return (zfs_netbsd_setacl_nfsv4(ap));
	case ZFS_ACLTYPE_POSIX:
		if (ap->a_type == ACL_TYPE_NFS4)
			return (EINVAL);
		break;
	default:
		/*
		 * acltype=off: keep the mode in sync with an access
		 * ACL and accept an empty default ACL.  Extra default
		 * entries need acltype=posix.
		 */
		if (ap->a_type == ACL_TYPE_NFS4)
			return (EOPNOTSUPP);
		if (ap->a_aclp == NULL) {
			if (ap->a_type == ACL_TYPE_DEFAULT ||
			    ap->a_type == ACL_TYPE_DEFAULT_OLD) {
				if (ap->a_vp->v_type != VDIR)
					return (ENOTDIR);
				return (0);
			}
			return (EINVAL);
		}
		error = zfs_netbsd_acl_check(ap->a_vp, ap->a_type, ap->a_aclp);
		if (error != 0)
			return (error);
		if (ap->a_type == ACL_TYPE_DEFAULT ||
		    ap->a_type == ACL_TYPE_DEFAULT_OLD) {
			if (ap->a_aclp->acl_cnt == 0)
				return (0);
			return (EOPNOTSUPP);
		}
		{
			vattr_t mv;

			VATTR_NULL(&mv);
			mv.va_mode = zfs_netbsd_acl_to_mode(ap->a_aclp);
			mv.va_mask = AT_MODE;
			return (zfs_setattr(zp, &mv, 0, ap->a_cred));
		}
	}
	error = secpolicy_vnode_setdac(ap->a_vp, ap->a_cred,
	    zfs_fuid_map_id(zfsvfs, zp->z_uid, ap->a_cred, ZFS_OWNER));
	if (error != 0)
		return (error);
	name = zfs_netbsd_acl_xattr(ap->a_type);
	if (name == NULL)
		return (EINVAL);
	if (ap->a_aclp == NULL) {
		/*
		 * POSIX.1e allows deleting only the default ACL.
		 * FFS: EINVAL for ACCESS, ENOTDIR if not a directory.
		 * Missing default is success (TRIX).
		 */
		if (ap->a_type != ACL_TYPE_DEFAULT &&
		    ap->a_type != ACL_TYPE_DEFAULT_OLD)
			return (EINVAL);
		if (ap->a_vp->v_type != VDIR)
			return (ENOTDIR);
		error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred,
		    LOOKUP_XATTR, B_FALSE);
		if (error != 0)
			return (error == ENOENT ? 0 : error);
		error = zfs_remove(VTOZ(xvp), (char *)name, ap->a_cred, 0);
		vrele(xvp);
		return (error == ENOENT ? 0 : error);
	}
	error = zfs_netbsd_acl_check(ap->a_vp, ap->a_type, ap->a_aclp);
	if (error != 0)
		return (error);
	error = zfs_netbsd_acl_to_xattr(ap->a_aclp, &buf, &len);
	if (error != 0)
		return (error);
	memset(&auio, 0, sizeof (auio));
	iov.iov_base = buf;
	iov.iov_len = len;
	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_resid = len;
	auio.uio_rw = UIO_WRITE;
	auio.uio_vmspace = vmspace_kernel();
	/*
	 * Write the new ACL to a temp object first.  Update the
	 * mode while the live ACL is still the old one, then
	 * publish.  A failed setattr then leaves both mode and ACL
	 * unchanged; a failed publish rolls the mode back.
	 */
	tmp = kmem_alloc(ZFS_XATTR_NMLEN, KM_SLEEP);
	error = zfs_netbsd_xattr_stage(ap->a_vp, tmp, ZFS_XATTR_NMLEN,
	    &auio, ap->a_cred);
	if (error != 0) {
		kmem_free(tmp, ZFS_XATTR_NMLEN);
		kmem_free(buf, len);
		return (error);
	}
	access_acl = (ap->a_type == ACL_TYPE_ACCESS ||
	    ap->a_type == ACL_TYPE_ACCESS_OLD);
	if (access_acl) {
		vattr_t mv;

		oldmode = (mode_t)zp->z_mode;
		VATTR_NULL(&mv);
		mv.va_mode = zfs_netbsd_acl_to_mode(ap->a_aclp);
		mv.va_mask = AT_MODE;
		error = zfs_setattr(zp, &mv, 0, ap->a_cred);
		if (error != 0) {
			(void) zfs_netbsd_xattr_abort(ap->a_vp, tmp,
			    ap->a_cred);
			kmem_free(tmp, ZFS_XATTR_NMLEN);
			kmem_free(buf, len);
			return (error);
		}
	}
	error = zfs_netbsd_xattr_commit(ap->a_vp, tmp, name, ap->a_cred);
	if (error != 0) {
		if (access_acl) {
			vattr_t mv;
			int rerr;

			VATTR_NULL(&mv);
			mv.va_mode = oldmode;
			mv.va_mask = AT_MODE;
			rerr = zfs_setattr(zp, &mv, 0, ap->a_cred);
			if (rerr != 0) {
				cmn_err(CE_WARN,
				    "zfs_setacl: mode rollback failed "
				    "(%d) after ACL publish error %d",
				    rerr, error);
				error = rerr;
			}
		}
		(void) zfs_netbsd_xattr_abort(ap->a_vp, tmp, ap->a_cred);
	} else if (access_acl) {
		zfs_netbsd_posix_acl_purge(zp);
	}
	kmem_free(tmp, ZFS_XATTR_NMLEN);
	kmem_free(buf, len);
	if (error == 0)
		zfs_znode_cache_id(zp);
	return (error);
}

/*
 * POSIX.1e default-ACL inheritance (FFS ufs_do_posix1e_acl_inheritance_*).
 * Empty default ACL means none.  Access ACL is the default with
 * USER_OBJ/OTHER/(MASK|GROUP_OBJ) taken from the creation mode.
 * Directories also keep the parent's default ACL.
 */
static void
zfs_netbsd_acl_sync_from_mode(struct acl *acl, mode_t mode)
{
	struct acl_entry *mask = NULL, *group_obj = NULL;
	unsigned i;

	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			acl->acl_entry[i].ae_perm =
			    acl_posix1e_mode_to_perm(ACL_USER_OBJ, mode);
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;
		case ACL_GROUP_OBJ:
			group_obj = &acl->acl_entry[i];
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;
		case ACL_OTHER:
			acl->acl_entry[i].ae_perm =
			    acl_posix1e_mode_to_perm(ACL_OTHER, mode);
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;
		case ACL_MASK:
			mask = &acl->acl_entry[i];
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID;
			break;
		default:
			break;
		}
	}
	if (mask != NULL)
		mask->ae_perm = acl_posix1e_mode_to_perm(ACL_GROUP_OBJ, mode);
	else if (group_obj != NULL)
		group_obj->ae_perm =
		    acl_posix1e_mode_to_perm(ACL_GROUP_OBJ, mode);
}

static int
zfs_netbsd_posix_inherit(vnode_t *dvp, vnode_t *vp, cred_t *cr)
{
	znode_t *dzp = VTOZ(dvp);
	znode_t *zp = VTOZ(vp);
	struct acl *dacl, *acl;
	struct vop_getacl_args ga;
	struct vop_setacl_args sa;
	mode_t mode;
	int error;

	if (dzp == NULL || zp == NULL)
		return (0);
	if (dzp->z_zfsvfs == NULL ||
	    dzp->z_zfsvfs->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);
	if ((dzp->z_pflags & ZFS_XATTR) != 0)
		return (0);
	if (vp->v_type == VLNK)
		return (0);

	dacl = kmem_zalloc(sizeof (*dacl), KM_SLEEP);
	dacl->acl_maxcnt = ACL_MAX_ENTRIES;
	memset(&ga, 0, sizeof (ga));
	ga.a_vp = dvp;
	ga.a_type = ACL_TYPE_DEFAULT;
	ga.a_aclp = dacl;
	ga.a_cred = cr;
	error = zfs_netbsd_getacl(&ga);
	if (error != 0 || dacl->acl_cnt == 0) {
		kmem_free(dacl, sizeof (*dacl));
		return (error);
	}

	mode = acl_posix1e_newfilemode((mode_t)zp->z_mode, dacl);
	if (mode != (mode_t)zp->z_mode) {
		vattr_t va;

		VATTR_NULL(&va);
		va.va_mode = mode;
		va.va_mask = AT_MODE;
		error = zfs_setattr(zp, &va, 0, cr);
		if (error != 0) {
			kmem_free(dacl, sizeof (*dacl));
			return (error);
		}
	}

	acl = kmem_zalloc(sizeof (*acl), KM_SLEEP);
	*acl = *dacl;
	zfs_netbsd_acl_sync_from_mode(acl, mode);
	memset(&sa, 0, sizeof (sa));
	sa.a_vp = vp;
	sa.a_type = ACL_TYPE_ACCESS;
	sa.a_aclp = acl;
	sa.a_cred = cr;
	error = zfs_netbsd_setacl(&sa);
	if (error == 0 && vp->v_type == VDIR) {
		sa.a_type = ACL_TYPE_DEFAULT;
		sa.a_aclp = dacl;
		error = zfs_netbsd_setacl(&sa);
	}
	kmem_free(acl, sizeof (*acl));
	kmem_free(dacl, sizeof (*dacl));
	return (error);
}

static int
zfs_netbsd_aclcheck(void *v)
{
	struct vop_aclcheck_args *ap = v;
	znode_t *zp = VTOZ(ap->a_vp);
	zfsvfs_t *zfsvfs;

	if (zp == NULL)
		return (EOPNOTSUPP);
	zfsvfs = zp->z_zfsvfs;
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_OFF:
		if (ap->a_type == ACL_TYPE_NFS4)
			return (EOPNOTSUPP);
		if (ap->a_aclp == NULL)
			return (EINVAL);
		return (zfs_netbsd_acl_check(ap->a_vp, ap->a_type, ap->a_aclp));
	case ZFS_ACLTYPE_NFSV4:
		if (ap->a_type != ACL_TYPE_NFS4)
			return (EINVAL);
		if (ap->a_aclp == NULL)
			return (EINVAL);
		if (ap->a_aclp->acl_cnt > (ACL_MAX_ENTRIES - 6) / 2)
			return (ENOSPC);
		return (acl_nfs4_check(ap->a_aclp, ap->a_vp->v_type == VDIR));
	case ZFS_ACLTYPE_POSIX:
		if (ap->a_type == ACL_TYPE_NFS4)
			return (EINVAL);
		return (zfs_netbsd_acl_check(ap->a_vp, ap->a_type, ap->a_aclp));
	default:
		return (EOPNOTSUPP);
	}
}
const struct vnodeopv_entry_desc zfs_vnodeop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	{ &vop_bwrite_desc, genfs_eopnotsupp },
	{ &vop_parsepath_desc, genfs_parsepath },
	{ &vop_lookup_desc, zfs_netbsd_lookup },
	{ &vop_create_desc, zfs_netbsd_create },
	{ &vop_mknod_desc, zfs_netbsd_mknod },
	{ &vop_open_desc, zfs_netbsd_open },
	{ &vop_close_desc, zfs_netbsd_close },
	{ &vop_access_desc, zfs_netbsd_access },
	{ &vop_accessx_desc, genfs_accessx },
	{ &vop_getattr_desc, zfs_netbsd_getattr },
	{ &vop_setattr_desc, zfs_netbsd_setattr },
	{ &vop_read_desc, zfs_netbsd_read },
	{ &vop_write_desc, zfs_netbsd_write },
	{ &vop_fallocate_desc, zfs_netbsd_fallocate },
	{ &vop_fdiscard_desc, zfs_netbsd_fdiscard },
	{ &vop_ioctl_desc, zfs_netbsd_ioctl },
	{ &vop_fcntl_desc, genfs_fcntl },
	{ &vop_poll_desc, genfs_poll },
	{ &vop_kqfilter_desc, genfs_kqfilter },
	{ &vop_revoke_desc, genfs_revoke },
	{ &vop_mmap_desc, genfs_mmap },
	{ &vop_fsync_desc, zfs_netbsd_fsync },
	{ &vop_seek_desc, genfs_seek },
	{ &vop_remove_desc, zfs_netbsd_remove },
	{ &vop_link_desc, zfs_netbsd_link },
	{ &vop_rename_desc, zfs_netbsd_rename },
	{ &vop_mkdir_desc, zfs_netbsd_mkdir },
	{ &vop_rmdir_desc, zfs_netbsd_rmdir },
	{ &vop_symlink_desc, zfs_netbsd_symlink },
	{ &vop_readdir_desc, zfs_netbsd_readdir },
	{ &vop_readlink_desc, zfs_netbsd_readlink },
	{ &vop_abortop_desc, genfs_abortop },
	{ &vop_inactive_desc, zfs_netbsd_inactive },
	{ &vop_reclaim_desc, zfs_netbsd_reclaim },
	{ &vop_lock_desc, genfs_lock },
	{ &vop_unlock_desc, genfs_unlock },
	{ &vop_bmap_desc, genfs_eopnotsupp },
	{ &vop_strategy_desc, genfs_eopnotsupp },
	{ &vop_print_desc, zfs_netbsd_print },
	{ &vop_islocked_desc, genfs_islocked },
	{ &vop_pathconf_desc, zfs_netbsd_pathconf },
	{ &vop_advlock_desc, zfs_netbsd_advlock },
	{ &vop_whiteout_desc, genfs_eopnotsupp },
	{ &vop_getpages_desc, zfs_netbsd_getpages },
	{ &vop_putpages_desc, zfs_netbsd_putpages },
	{ &vop_getacl_desc, zfs_netbsd_getacl },
	{ &vop_setacl_desc, zfs_netbsd_setacl },
	{ &vop_closeextattr_desc, genfs_eopnotsupp },
	{ &vop_aclcheck_desc, zfs_netbsd_aclcheck },
	{ &vop_getextattr_desc, zfs_netbsd_getextattr },
	{ &vop_listextattr_desc, zfs_netbsd_listextattr },
	{ &vop_openextattr_desc, genfs_eopnotsupp },
	{ &vop_deleteextattr_desc, zfs_netbsd_deleteextattr },
	{ &vop_setextattr_desc, zfs_netbsd_setextattr },
	{ NULL, NULL }
};

const struct vnodeopv_desc zfs_vnodeop_opv_desc = {
	&zfs_vnodeop_p, zfs_vnodeop_entries
};

/*
 * Device and fifo vnodes must use the same ACL VOPs and
 * zfs_netbsd_pathconf as regular files.  spec_pathconf never
 * reports _PC_ACL_NFS4, so getfacl/setfacl would hit
 * vn_default_error.  Inherited ACEs still enforce through
 * zfs_netbsd_access.  FreeBSD's zfs_fifoops / specops wire
 * the same three VOPs.
 */
const struct vnodeopv_entry_desc zfs_specop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	GENFS_SPECOP_ENTRIES,
	{ &vop_access_desc, zfs_netbsd_access },
	{ &vop_accessx_desc, genfs_accessx },
	{ &vop_getattr_desc, zfs_netbsd_getattr },
	{ &vop_setattr_desc, zfs_netbsd_setattr },
	{ &vop_fcntl_desc, genfs_fcntl },
	{ &vop_inactive_desc, zfs_netbsd_inactive },
	{ &vop_reclaim_desc, zfs_netbsd_reclaim },
	{ &vop_lock_desc, genfs_lock },
	{ &vop_unlock_desc, genfs_unlock },
	{ &vop_print_desc, zfs_netbsd_print },
	{ &vop_islocked_desc, genfs_islocked },
	{ &vop_bwrite_desc, vn_bwrite },
	/* openextattr */
	/* closeextattr */
	{ &vop_getextattr_desc, zfs_netbsd_getextattr },
	{ &vop_setextattr_desc, zfs_netbsd_setextattr },
	{ &vop_listextattr_desc, zfs_netbsd_listextattr },
	{ &vop_deleteextattr_desc, zfs_netbsd_deleteextattr },
	{ &vop_getacl_desc, zfs_netbsd_getacl },
	{ &vop_setacl_desc, zfs_netbsd_setacl },
	{ &vop_aclcheck_desc, zfs_netbsd_aclcheck },
	{ &vop_close_desc, spec_close },
	{ &vop_read_desc, zfs_netbsd_read },
	{ &vop_write_desc, zfs_netbsd_write },
	{ &vop_fsync_desc, spec_fsync },
	{ &vop_pathconf_desc, zfs_netbsd_pathconf },
	{ NULL, NULL }
};

const struct vnodeopv_desc zfs_specop_opv_desc = {
	&zfs_specop_p, zfs_specop_entries
};

const struct vnodeopv_entry_desc zfs_fifoop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	GENFS_FIFOOP_ENTRIES,
	{ &vop_access_desc, zfs_netbsd_access },
	{ &vop_accessx_desc, genfs_accessx },
	{ &vop_getattr_desc, zfs_netbsd_getattr },
	{ &vop_setattr_desc, zfs_netbsd_setattr },
	{ &vop_fcntl_desc, genfs_fcntl },
	{ &vop_inactive_desc, zfs_netbsd_inactive },
	{ &vop_reclaim_desc, zfs_netbsd_reclaim },
	{ &vop_lock_desc, genfs_lock },
	{ &vop_unlock_desc, genfs_unlock },
	{ &vop_strategy_desc, vn_fifo_bypass },
	{ &vop_print_desc, zfs_netbsd_print },
	{ &vop_islocked_desc, genfs_islocked },
	{ &vop_bwrite_desc, vn_bwrite },
	/* openextattr */
	/* closeextattr */
	{ &vop_getextattr_desc, zfs_netbsd_getextattr },
	{ &vop_setextattr_desc, zfs_netbsd_setextattr },
	{ &vop_listextattr_desc, zfs_netbsd_listextattr },
	{ &vop_deleteextattr_desc, zfs_netbsd_deleteextattr },
	{ &vop_getacl_desc, zfs_netbsd_getacl },
	{ &vop_setacl_desc, zfs_netbsd_setacl },
	{ &vop_aclcheck_desc, zfs_netbsd_aclcheck },
	{ &vop_close_desc, vn_fifo_bypass },
	{ &vop_read_desc, zfs_netbsd_read },
	{ &vop_write_desc, zfs_netbsd_write },
	{ &vop_fsync_desc, vn_fifo_bypass },
	{ &vop_pathconf_desc, zfs_netbsd_pathconf },
	{ NULL, NULL }
};

const struct vnodeopv_desc zfs_fifoop_opv_desc = {
	&zfs_fifoop_p, zfs_fifoop_entries
};

int zfs_xattr_compat = 1;

ZFS_MODULE_PARAM(zfs, zfs_, xattr_compat, INT, ZMOD_RW,
	"Use legacy ZFS xattr naming for writing new user namespace xattrs");
