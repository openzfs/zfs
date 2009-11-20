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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* Portions Copyright 2007 Jeremy Teo */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/uio.h>
#include <sys/vmsystm.h>
#include <sys/atomic.h>
#include <sys/vm.h>
#include <vm/seg_vn.h>
#include <vm/pvn.h>
#include <vm/as.h>
#include <vm/kpm.h>
#include <vm/seg_kpm.h>
#include <sys/mman.h>
#include <sys/pathname.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/dirent.h>
#include <sys/policy.h>
#include <sys/sunddi.h>
#include <sys/filio.h>
#include <sys/sid.h>
#include "fs/fs_subr.h"
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/dnlc.h>
#include <sys/zfs_rlock.h>
#include <sys/extdirent.h>
#include <sys/kidmap.h>
#include <sys/cred_impl.h>
#include <sys/attr.h>

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
 *  (1) A check must be made in each zfs thread for a mounted file system.
 *	This is done avoiding races using ZFS_ENTER(zfsvfs).
 *      A ZFS_EXIT(zfsvfs) is needed before all returns.  Any znodes
 *      must be checked with ZFS_VERIFY_ZP(zp).  Both of these macros
 *      can return EIO from the calling function.
 *
 *  (2)	VN_RELE() should always be the last thing except for zil_commit()
 *	(if necessary) and ZFS_EXIT(). This is for 3 reasons:
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
 *  (4)	Always pass TXG_NOWAIT as the second argument to dmu_tx_assign().
 *	This is critical because we don't want to block while holding locks.
 *	Note, in particular, that if a lock is sometimes acquired before
 *	the tx assigns, and sometimes after (e.g. z_lock), then failing to
 *	use a non-blocking assign can deadlock the system.  The scenario:
 *
 *	Thread A has grabbed a lock before calling dmu_tx_assign().
 *	Thread B is in an already-assigned tx, and blocks for this lock.
 *	Thread A calls dmu_tx_assign(TXG_WAIT) and blocks in txg_wait_open()
 *	forever, because the previous txg can't quiesce until B's tx commits.
 *
 *	If dmu_tx_assign() returns ERESTART and zfsvfs->z_assign is TXG_NOWAIT,
 *	then drop all locks, call dmu_tx_wait(), and try again.
 *
 *  (5)	If the operation succeeded, generate the intent log entry for it
 *	before dropping locks.  This ensures that the ordering of events
 *	in the intent log matches the order in which they actually occurred.
 *      During ZIL replay the zfs_log_* functions will update the sequence
 *	number to indicate the zil transaction has replayed.
 *
 *  (6)	At the end of each vnode op, the DMU tx must always commit,
 *	regardless of whether there were any errors.
 *
 *  (7)	After dropping all locks, invoke zil_commit(zilog, seq, foid)
 *	to ensure that synchronous semantics are provided when necessary.
 *
 * In general, this is how things should be ordered in each vnode op:
 *
 *	ZFS_ENTER(zfsvfs);		// exit if unmounted
 * top:
 *	zfs_dirent_lock(&dl, ...)	// lock directory entry (may VN_HOLD())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx, TXG_NOWAIT);	// try to assign
 *	if (error) {
 *		rw_exit(...);		// drop locks
 *		zfs_dirent_unlock(dl);	// unlock directory entry
 *		VN_RELE(...);		// release held vnodes
 *		if (error == ERESTART) {
 *			dmu_tx_wait(tx);
 *			dmu_tx_abort(tx);
 *			goto top;
 *		}
 *		dmu_tx_abort(tx);	// abort DMU tx
 *		ZFS_EXIT(zfsvfs);	// finished in zfs
 *		return (error);		// really out of space
 *	}
 *	error = do_real_work();		// do whatever this VOP does
 *	if (error == 0)
 *		zfs_log_*(...);		// on success, make ZIL entry
 *	dmu_tx_commit(tx);		// commit DMU tx -- error or not
 *	rw_exit(...);			// drop locks
 *	zfs_dirent_unlock(dl);		// unlock directory entry
 *	VN_RELE(...);			// release held vnodes
 *	zil_commit(zilog, seq, foid);	// synchronous when necessary
 *	ZFS_EXIT(zfsvfs);		// finished in zfs
 *	return (error);			// done, report error
 */

/* ARGSUSED */
static int
zfs_open(vnode_t **vpp, int flag, cred_t *cr, caller_context_t *ct)
{
	znode_t	*zp = VTOZ(*vpp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((flag & FWRITE) && (zp->z_phys->zp_flags & ZFS_APPENDONLY) &&
	    ((flag & FAPPEND) == 0)) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if (!zfs_has_ctldir(zp) && zp->z_zfsvfs->z_vscan &&
	    ZTOV(zp)->v_type == VREG &&
	    !(zp->z_phys->zp_flags & ZFS_AV_QUARANTINED) &&
	    zp->z_phys->zp_size > 0) {
		if (fs_vscan(*vpp, cr, 0) != 0) {
			ZFS_EXIT(zfsvfs);
			return (EACCES);
		}
	}

	/* Keep a count of the synchronous opens in the znode */
	if (flag & (FSYNC | FDSYNC))
		atomic_inc_32(&zp->z_sync_cnt);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/* ARGSUSED */
static int
zfs_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr,
    caller_context_t *ct)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	/*
	 * Clean up any locks held by this process on the vp.
	 */
	cleanlocks(vp, ddi_get_pid(), 0);
	cleanshares(vp, ddi_get_pid());

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/* Decrement the synchronous opens in the znode */
	if ((flag & (FSYNC | FDSYNC)) && (count == 1))
		atomic_dec_32(&zp->z_sync_cnt);

	if (!zfs_has_ctldir(zp) && zp->z_zfsvfs->z_vscan &&
	    ZTOV(zp)->v_type == VREG &&
	    !(zp->z_phys->zp_flags & ZFS_AV_QUARANTINED) &&
	    zp->z_phys->zp_size > 0)
		VERIFY(fs_vscan(vp, cr, 1) == 0);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Lseek support for finding holes (cmd == _FIO_SEEK_HOLE) and
 * data (cmd == _FIO_SEEK_DATA). "off" is an in/out parameter.
 */
static int
zfs_holey(vnode_t *vp, int cmd, offset_t *off)
{
	znode_t	*zp = VTOZ(vp);
	uint64_t noff = (uint64_t)*off; /* new offset */
	uint64_t file_sz;
	int error;
	boolean_t hole;

	file_sz = zp->z_phys->zp_size;
	if (noff >= file_sz)  {
		return (ENXIO);
	}

	if (cmd == _FIO_SEEK_HOLE)
		hole = B_TRUE;
	else
		hole = B_FALSE;

	error = dmu_offset_next(zp->z_zfsvfs->z_os, zp->z_id, hole, &noff);

	/* end of file? */
	if ((error == ESRCH) || (noff > file_sz)) {
		/*
		 * Handle the virtual hole at the end of file.
		 */
		if (hole) {
			*off = file_sz;
			return (0);
		}
		return (ENXIO);
	}

	if (noff < *off)
		return (error);
	*off = noff;
	return (error);
}

/* ARGSUSED */
static int
zfs_ioctl(vnode_t *vp, int com, intptr_t data, int flag, cred_t *cred,
    int *rvalp, caller_context_t *ct)
{
	offset_t off;
	int error;
	zfsvfs_t *zfsvfs;
	znode_t *zp;

	switch (com) {
	case _FIOFFS:
		return (zfs_sync(vp->v_vfsp, 0, cred));

		/*
		 * The following two ioctls are used by bfu.  Faking out,
		 * necessary to avoid bfu errors.
		 */
	case _FIOGDIO:
	case _FIOSDIO:
		return (0);

	case _FIO_SEEK_DATA:
	case _FIO_SEEK_HOLE:
		if (ddi_copyin((void *)data, &off, sizeof (off), flag))
			return (EFAULT);

		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);

		/* offset parameter is in/out */
		error = zfs_holey(vp, com, &off);
		ZFS_EXIT(zfsvfs);
		if (error)
			return (error);
		if (ddi_copyout(&off, (void *)data, sizeof (off), flag))
			return (EFAULT);
		return (0);
	}
	return (ENOTTY);
}

/*
 * Utility functions to map and unmap a single physical page.  These
 * are used to manage the mappable copies of ZFS file data, and therefore
 * do not update ref/mod bits.
 */
caddr_t
zfs_map_page(page_t *pp, enum seg_rw rw)
{
	if (kpm_enable)
		return (hat_kpm_mapin(pp, 0));
	ASSERT(rw == S_READ || rw == S_WRITE);
	return (ppmapin(pp, PROT_READ | ((rw == S_WRITE) ? PROT_WRITE : 0),
	    (caddr_t)-1));
}

void
zfs_unmap_page(page_t *pp, caddr_t addr)
{
	if (kpm_enable) {
		hat_kpm_mapout(pp, 0, addr);
	} else {
		ppmapout(addr);
	}
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Write:	If we find a memory mapped page, we write to *both*
 *		the page and the dmu buffer.
 */
static void
update_pages(vnode_t *vp, int64_t start, int len, objset_t *os, uint64_t oid)
{
	int64_t	off;

	off = start & PAGEOFFSET;
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		page_t *pp;
		uint64_t nbytes = MIN(PAGESIZE - off, len);

		if (pp = page_lookup(vp, start, SE_SHARED)) {
			caddr_t va;

			va = zfs_map_page(pp, S_WRITE);
			(void) dmu_read(os, oid, start+off, nbytes, va+off,
			    DMU_READ_PREFETCH);
			zfs_unmap_page(pp, va);
			page_unlock(pp);
		}
		len -= nbytes;
		off = 0;
	}
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Read:	We "read" preferentially from memory mapped pages,
 *		else we default from the dmu buffer.
 *
 * NOTE: We will always "break up" the IO into PAGESIZE uiomoves when
 *	the file is memory mapped.
 */
static int
mappedread(vnode_t *vp, int nbytes, uio_t *uio)
{
	znode_t *zp = VTOZ(vp);
	objset_t *os = zp->z_zfsvfs->z_os;
	int64_t	start, off;
	int len = nbytes;
	int error = 0;

	start = uio->uio_loffset;
	off = start & PAGEOFFSET;
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		page_t *pp;
		uint64_t bytes = MIN(PAGESIZE - off, len);

		if (pp = page_lookup(vp, start, SE_SHARED)) {
			caddr_t va;

			va = zfs_map_page(pp, S_READ);
			error = uiomove(va + off, bytes, UIO_READ, uio);
			zfs_unmap_page(pp, va);
			page_unlock(pp);
		} else {
			error = dmu_read_uio(os, zp->z_id, uio, bytes);
		}
		len -= bytes;
		off = 0;
		if (error)
			break;
	}
	return (error);
}

offset_t zfs_read_chunk_size = 1024 * 1024; /* Tunable */

/*
 * Read bytes from specified file into supplied buffer.
 *
 *	IN:	vp	- vnode of file to be read from.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		ioflag	- SYNC flags; used to provide FRSYNC semantics.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Side Effects:
 *	vp - atime updated if byte count > 0
 */
/* ARGSUSED */
static int
zfs_read(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	ssize_t		n, nbytes;
	int		error;
	rl_t		*rl;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	os = zfsvfs->z_os;

	if (zp->z_phys->zp_flags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zfsvfs);
		return (EACCES);
	}

	/*
	 * Validate file offset
	 */
	if (uio->uio_loffset < (offset_t)0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio->uio_resid == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	/*
	 * Check for mandatory locks
	 */
	if (MANDMODE((mode_t)zp->z_phys->zp_mode)) {
		if (error = chklock(vp, FREAD,
		    uio->uio_loffset, uio->uio_resid, uio->uio_fmode, ct)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	/*
	 * If we're in FRSYNC mode, sync out this znode before reading it.
	 */
	if (ioflag & FRSYNC)
		zil_commit(zfsvfs->z_log, zp->z_last_itx, zp->z_id);

	/*
	 * Lock the range against changes.
	 */
	rl = zfs_range_lock(zp, uio->uio_loffset, uio->uio_resid, RL_READER);

	/*
	 * If we are reading past end-of-file we can skip
	 * to the end; but we might still need to set atime.
	 */
	if (uio->uio_loffset >= zp->z_phys->zp_size) {
		error = 0;
		goto out;
	}

	ASSERT(uio->uio_loffset < zp->z_phys->zp_size);
	n = MIN(uio->uio_resid, zp->z_phys->zp_size - uio->uio_loffset);

	while (n > 0) {
		nbytes = MIN(n, zfs_read_chunk_size -
		    P2PHASE(uio->uio_loffset, zfs_read_chunk_size));

		if (vn_has_cached_data(vp))
			error = mappedread(vp, nbytes, uio);
		else
			error = dmu_read_uio(os, zp->z_id, uio, nbytes);
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = EIO;
			break;
		}

		n -= nbytes;
	}

out:
	zfs_range_unlock(rl);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Write the bytes to a file.
 *
 *	IN:	vp	- vnode of file to be written to.
 *		uio	- structure supplying write location, range info,
 *			  and data buffer.
 *		ioflag	- FAPPEND flag set if in append mode.
 *		cr	- credentials of caller.
 *		ct	- caller context (NFS/CIFS fem monitor only)
 *
 *	OUT:	uio	- updated offset and range.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime|mtime updated if byte count > 0
 */
/* ARGSUSED */
static int
zfs_write(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	rlim64_t	limit = uio->uio_llimit;
	ssize_t		start_resid = uio->uio_resid;
	ssize_t		tx_bytes;
	uint64_t	end_size;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	offset_t	woff;
	ssize_t		n, nbytes;
	rl_t		*rl;
	int		max_blksz = zfsvfs->z_max_blksz;
	uint64_t	pflags;
	int		error;
	arc_buf_t	*abuf;

	/*
	 * Fasttrack empty write
	 */
	n = start_resid;
	if (n == 0)
		return (0);

	if (limit == RLIM64_INFINITY || limit > MAXOFFSET_T)
		limit = MAXOFFSET_T;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * If immutable or not appending then return EPERM
	 */
	pflags = zp->z_phys->zp_flags;
	if ((pflags & (ZFS_IMMUTABLE | ZFS_READONLY)) ||
	    ((pflags & ZFS_APPENDONLY) && !(ioflag & FAPPEND) &&
	    (uio->uio_loffset < zp->z_phys->zp_size))) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	zilog = zfsvfs->z_log;

	/*
	 * Pre-fault the pages to ensure slow (eg NFS) pages
	 * don't hold up txg.
	 */
	uio_prefaultpages(n, uio);

	/*
	 * If in append mode, set the io offset pointer to eof.
	 */
	if (ioflag & FAPPEND) {
		/*
		 * Range lock for a file append:
		 * The value for the start of range will be determined by
		 * zfs_range_lock() (to guarantee append semantics).
		 * If this write will cause the block size to increase,
		 * zfs_range_lock() will lock the entire file, so we must
		 * later reduce the range after we grow the block size.
		 */
		rl = zfs_range_lock(zp, 0, n, RL_APPEND);
		if (rl->r_len == UINT64_MAX) {
			/* overlocked, zp_size can't change */
			woff = uio->uio_loffset = zp->z_phys->zp_size;
		} else {
			woff = uio->uio_loffset = rl->r_off;
		}
	} else {
		woff = uio->uio_loffset;
		/*
		 * Validate file offset
		 */
		if (woff < 0) {
			ZFS_EXIT(zfsvfs);
			return (EINVAL);
		}

		/*
		 * If we need to grow the block size then zfs_range_lock()
		 * will lock a wider range than we request here.
		 * Later after growing the block size we reduce the range.
		 */
		rl = zfs_range_lock(zp, woff, n, RL_WRITER);
	}

	if (woff >= limit) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (EFBIG);
	}

	if ((woff + n) > limit || woff > (limit - n))
		n = limit - woff;

	/*
	 * Check for mandatory locks
	 */
	if (MANDMODE((mode_t)zp->z_phys->zp_mode) &&
	    (error = chklock(vp, FWRITE, woff, n, uio->uio_fmode, ct)) != 0) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	end_size = MAX(zp->z_phys->zp_size, woff + n);

	/*
	 * Write the file in reasonable size chunks.  Each chunk is written
	 * in a separate transaction; this keeps the intent log records small
	 * and allows us to do more fine-grained space accounting.
	 */
	while (n > 0) {
		abuf = NULL;
		woff = uio->uio_loffset;

again:
		if (zfs_usergroup_overquota(zfsvfs,
		    B_FALSE, zp->z_phys->zp_uid) ||
		    zfs_usergroup_overquota(zfsvfs,
		    B_TRUE, zp->z_phys->zp_gid)) {
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			error = EDQUOT;
			break;
		}

		/*
		 * If dmu_assign_arcbuf() is expected to execute with minimum
		 * overhead loan an arc buffer and copy user data to it before
		 * we enter a txg.  This avoids holding a txg forever while we
		 * pagefault on a hanging NFS server mapping.
		 */
		if (abuf == NULL && n >= max_blksz &&
		    woff >= zp->z_phys->zp_size &&
		    P2PHASE(woff, max_blksz) == 0 &&
		    zp->z_blksz == max_blksz) {
			size_t cbytes;

			abuf = dmu_request_arcbuf(zp->z_dbuf, max_blksz);
			ASSERT(abuf != NULL);
			ASSERT(arc_buf_size(abuf) == max_blksz);
			if (error = uiocopy(abuf->b_data, max_blksz,
			    UIO_WRITE, uio, &cbytes)) {
				dmu_return_arcbuf(abuf);
				break;
			}
			ASSERT(cbytes == max_blksz);
		}

		/*
		 * Start a transaction.
		 */
		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_bonus(tx, zp->z_id);
		dmu_tx_hold_write(tx, zp->z_id, woff, MIN(n, max_blksz));
		error = dmu_tx_assign(tx, TXG_NOWAIT);
		if (error) {
			if (error == ERESTART) {
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				goto again;
			}
			dmu_tx_abort(tx);
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			break;
		}

		/*
		 * If zfs_range_lock() over-locked we grow the blocksize
		 * and then reduce the lock range.  This will only happen
		 * on the first iteration since zfs_range_reduce() will
		 * shrink down r_len to the appropriate size.
		 */
		if (rl->r_len == UINT64_MAX) {
			uint64_t new_blksz;

			if (zp->z_blksz > max_blksz) {
				ASSERT(!ISP2(zp->z_blksz));
				new_blksz = MIN(end_size, SPA_MAXBLOCKSIZE);
			} else {
				new_blksz = MIN(end_size, max_blksz);
			}
			zfs_grow_blocksize(zp, new_blksz, tx);
			zfs_range_reduce(rl, woff, n);
		}

		/*
		 * XXX - should we really limit each write to z_max_blksz?
		 * Perhaps we should use SPA_MAXBLOCKSIZE chunks?
		 */
		nbytes = MIN(n, max_blksz - P2PHASE(woff, max_blksz));

		if (abuf == NULL) {
			tx_bytes = uio->uio_resid;
			error = dmu_write_uio(zfsvfs->z_os, zp->z_id, uio,
			    nbytes, tx);
			tx_bytes -= uio->uio_resid;
		} else {
			tx_bytes = nbytes;
			ASSERT(tx_bytes == max_blksz);
			dmu_assign_arcbuf(zp->z_dbuf, woff, abuf, tx);
			ASSERT(tx_bytes <= uio->uio_resid);
			uioskip(uio, tx_bytes);
		}
		if (tx_bytes && vn_has_cached_data(vp)) {
			update_pages(vp, woff,
			    tx_bytes, zfsvfs->z_os, zp->z_id);
		}

		/*
		 * If we made no progress, we're done.  If we made even
		 * partial progress, update the znode and ZIL accordingly.
		 */
		if (tx_bytes == 0) {
			dmu_tx_commit(tx);
			ASSERT(error != 0);
			break;
		}

		/*
		 * Clear Set-UID/Set-GID bits on successful write if not
		 * privileged and at least one of the excute bits is set.
		 *
		 * It would be nice to to this after all writes have
		 * been done, but that would still expose the ISUID/ISGID
		 * to another app after the partial write is committed.
		 *
		 * Note: we don't call zfs_fuid_map_id() here because
		 * user 0 is not an ephemeral uid.
		 */
		mutex_enter(&zp->z_acl_lock);
		if ((zp->z_phys->zp_mode & (S_IXUSR | (S_IXUSR >> 3) |
		    (S_IXUSR >> 6))) != 0 &&
		    (zp->z_phys->zp_mode & (S_ISUID | S_ISGID)) != 0 &&
		    secpolicy_vnode_setid_retain(cr,
		    (zp->z_phys->zp_mode & S_ISUID) != 0 &&
		    zp->z_phys->zp_uid == 0) != 0) {
			zp->z_phys->zp_mode &= ~(S_ISUID | S_ISGID);
		}
		mutex_exit(&zp->z_acl_lock);

		/*
		 * Update time stamp.  NOTE: This marks the bonus buffer as
		 * dirty, so we don't have to do it again for zp_size.
		 */
		zfs_time_stamper(zp, CONTENT_MODIFIED, tx);

		/*
		 * Update the file size (zp_size) if it has changed;
		 * account for possible concurrent updates.
		 */
		while ((end_size = zp->z_phys->zp_size) < uio->uio_loffset)
			(void) atomic_cas_64(&zp->z_phys->zp_size, end_size,
			    uio->uio_loffset);
		zfs_log_write(zilog, tx, TX_WRITE, zp, woff, tx_bytes, ioflag);
		dmu_tx_commit(tx);

		if (error != 0)
			break;
		ASSERT(tx_bytes == nbytes);
		n -= nbytes;
	}

	zfs_range_unlock(rl);

	/*
	 * If we're in replay mode, or we made no progress, return error.
	 * Otherwise, it's at least a partial write, so it's successful.
	 */
	if (zfsvfs->z_replay || uio->uio_resid == start_resid) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (ioflag & (FSYNC | FDSYNC))
		zil_commit(zilog, zp->z_last_itx, zp->z_id);

	ZFS_EXIT(zfsvfs);
	return (0);
}

void
zfs_get_done(dmu_buf_t *db, void *vzgd)
{
	zgd_t *zgd = (zgd_t *)vzgd;
	rl_t *rl = zgd->zgd_rl;
	vnode_t *vp = ZTOV(rl->r_zp);
	objset_t *os = rl->r_zp->z_zfsvfs->z_os;

	dmu_buf_rele(db, vzgd);
	zfs_range_unlock(rl);
	/*
	 * Release the vnode asynchronously as we currently have the
	 * txg stopped from syncing.
	 */
	VN_RELE_ASYNC(vp, dsl_pool_vnrele_taskq(dmu_objset_pool(os)));
	zil_add_block(zgd->zgd_zilog, zgd->zgd_bp);
	kmem_free(zgd, sizeof (zgd_t));
}

#ifdef DEBUG
static int zil_fault_io = 0;
#endif

/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zfs_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio)
{
	zfsvfs_t *zfsvfs = arg;
	objset_t *os = zfsvfs->z_os;
	znode_t *zp;
	uint64_t off = lr->lr_offset;
	dmu_buf_t *db;
	rl_t *rl;
	zgd_t *zgd;
	int dlen = lr->lr_length;		/* length of user data */
	int error = 0;

	ASSERT(zio);
	ASSERT(dlen != 0);

	/*
	 * Nothing to do if the file has been removed
	 */
	if (zfs_zget(zfsvfs, lr->lr_foid, &zp) != 0)
		return (ENOENT);
	if (zp->z_unlinked) {
		/*
		 * Release the vnode asynchronously as we currently have the
		 * txg stopped from syncing.
		 */
		VN_RELE_ASYNC(ZTOV(zp),
		    dsl_pool_vnrele_taskq(dmu_objset_pool(os)));
		return (ENOENT);
	}

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		rl = zfs_range_lock(zp, off, dlen, RL_READER);
		/* test for truncation needs to be done while range locked */
		if (off >= zp->z_phys->zp_size) {
			error = ENOENT;
			goto out;
		}
		VERIFY(0 == dmu_read(os, lr->lr_foid, off, dlen, buf,
		    DMU_READ_NO_PREFETCH));
	} else { /* indirect write */
		uint64_t boff; /* block starting offset */

		/*
		 * Have to lock the whole block to ensure when it's
		 * written out and it's checksum is being calculated
		 * that no one can change the data. We need to re-check
		 * blocksize after we get the lock in case it's changed!
		 */
		for (;;) {
			if (ISP2(zp->z_blksz)) {
				boff = P2ALIGN_TYPED(off, zp->z_blksz,
				    uint64_t);
			} else {
				boff = 0;
			}
			dlen = zp->z_blksz;
			rl = zfs_range_lock(zp, boff, dlen, RL_READER);
			if (zp->z_blksz == dlen)
				break;
			zfs_range_unlock(rl);
		}
		/* test for truncation needs to be done while range locked */
		if (off >= zp->z_phys->zp_size) {
			error = ENOENT;
			goto out;
		}
		zgd = (zgd_t *)kmem_alloc(sizeof (zgd_t), KM_SLEEP);
		zgd->zgd_rl = rl;
		zgd->zgd_zilog = zfsvfs->z_log;
		zgd->zgd_bp = &lr->lr_blkptr;
#ifdef DEBUG
		if (zil_fault_io) {
			error = EIO;
			zil_fault_io = 0;
		} else {
			error = dmu_buf_hold(os, lr->lr_foid, boff, zgd, &db);
		}
#else
		error = dmu_buf_hold(os, lr->lr_foid, boff, zgd, &db);
#endif
		if (error != 0) {
			kmem_free(zgd, sizeof (zgd_t));
			goto out;
		}

		ASSERT(boff == db->db_offset);
		lr->lr_blkoff = off - boff;
		error = dmu_sync(zio, db, &lr->lr_blkptr,
		    lr->lr_common.lrc_txg, zfs_get_done, zgd);
		ASSERT((error && error != EINPROGRESS) ||
		    lr->lr_length <= zp->z_blksz);
		if (error == 0)
			zil_add_block(zfsvfs->z_log, &lr->lr_blkptr);
		/*
		 * If we get EINPROGRESS, then we need to wait for a
		 * write IO initiated by dmu_sync() to complete before
		 * we can release this dbuf.  We will finish everything
		 * up in the zfs_get_done() callback.
		 */
		if (error == EINPROGRESS)
			return (0);
		dmu_buf_rele(db, zgd);
		kmem_free(zgd, sizeof (zgd_t));
	}
out:
	zfs_range_unlock(rl);
	/*
	 * Release the vnode asynchronously as we currently have the
	 * txg stopped from syncing.
	 */
	VN_RELE_ASYNC(ZTOV(zp), dsl_pool_vnrele_taskq(dmu_objset_pool(os)));
	return (error);
}

/*ARGSUSED*/
static int
zfs_access(vnode_t *vp, int mode, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (flag & V_ACE_MASK)
		error = zfs_zaccess(zp, mode, flag, B_FALSE, cr);
	else
		error = zfs_zaccess_rwx(zp, mode, flag, cr);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * If vnode is for a device return a specfs vnode instead.
 */
static int
specvp_check(vnode_t **vpp, cred_t *cr)
{
	int error = 0;

	if (IS_DEVVP(*vpp)) {
		struct vnode *svp;

		svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp);
		if (svp == NULL)
			error = ENOSYS;
		*vpp = svp;
	}
	return (error);
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
 *		direntflags - directory lookup flags
 *		realpnp - returned pathname.
 *
 *	OUT:	vpp	- vnode of located entry, NULL if not found.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	NA
 */
/* ARGSUSED */
static int
zfs_lookup(vnode_t *dvp, char *nm, vnode_t **vpp, struct pathname *pnp,
    int flags, vnode_t *rdir, cred_t *cr,  caller_context_t *ct,
    int *direntflags, pathname_t *realpnp)
{
	znode_t *zdp = VTOZ(dvp);
	zfsvfs_t *zfsvfs = zdp->z_zfsvfs;
	int	error = 0;

	/* fast path */
	if (!(flags & (LOOKUP_XATTR | FIGNORECASE))) {

		if (dvp->v_type != VDIR) {
			return (ENOTDIR);
		} else if (zdp->z_dbuf == NULL) {
			return (EIO);
		}

		if (nm[0] == 0 || (nm[0] == '.' && nm[1] == '\0')) {
			error = zfs_fastaccesschk_execute(zdp, cr);
			if (!error) {
				*vpp = dvp;
				VN_HOLD(*vpp);
				return (0);
			}
			return (error);
		} else {
			vnode_t *tvp = dnlc_lookup(dvp, nm);

			if (tvp) {
				error = zfs_fastaccesschk_execute(zdp, cr);
				if (error) {
					VN_RELE(tvp);
					return (error);
				}
				if (tvp == DNLC_NO_VNODE) {
					VN_RELE(tvp);
					return (ENOENT);
				} else {
					*vpp = tvp;
					return (specvp_check(vpp, cr));
				}
			}
		}
	}

	DTRACE_PROBE2(zfs__fastpath__lookup__miss, vnode_t *, dvp, char *, nm);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zdp);

	*vpp = NULL;

	if (flags & LOOKUP_XATTR) {
		/*
		 * If the xattr property is off, refuse the lookup request.
		 */
		if (!(zfsvfs->z_vfs->vfs_flag & VFS_XATTR)) {
			ZFS_EXIT(zfsvfs);
			return (EINVAL);
		}

		/*
		 * We don't allow recursive attributes..
		 * Maybe someday we will.
		 */
		if (zdp->z_phys->zp_flags & ZFS_XATTR) {
			ZFS_EXIT(zfsvfs);
			return (EINVAL);
		}

		if (error = zfs_get_xattrdir(VTOZ(dvp), vpp, cr, flags)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}

		/*
		 * Do we have permission to get into attribute directory?
		 */

		if (error = zfs_zaccess(VTOZ(*vpp), ACE_EXECUTE, 0,
		    B_FALSE, cr)) {
			VN_RELE(*vpp);
			*vpp = NULL;
		}

		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (dvp->v_type != VDIR) {
		ZFS_EXIT(zfsvfs);
		return (ENOTDIR);
	}

	/*
	 * Check accessibility of directory.
	 */

	if (error = zfs_zaccess(zdp, ACE_EXECUTE, 0, B_FALSE, cr)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfsvfs->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	error = zfs_dirlook(zdp, nm, vpp, flags, direntflags, realpnp);
	if (error == 0)
		error = specvp_check(vpp, cr);

	ZFS_EXIT(zfsvfs);
	return (error);
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
 *		vsecp 	- ACL to be set
 *
 *	OUT:	vpp	- vnode of created or trunc'd entry.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated if new entry created
 *	 vp - ctime|mtime always, atime if new
 */

/* ARGSUSED */
static int
zfs_create(vnode_t *dvp, char *name, vattr_t *vap, vcexcl_t excl,
    int mode, vnode_t **vpp, cred_t *cr, int flag, caller_context_t *ct,
    vsecattr_t *vsecp)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	objset_t	*os;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	ksid_t		*ksid;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	ksid = crgetsid(cr, KSID_OWNER);
	if (ksid)
		uid = ksid_getid(ksid);
	else
		uid = crgetuid(cr);

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (vsecp || (vap->va_mask & AT_XVATTR) ||
	    IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (EINVAL);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	if (vap->va_mask & AT_XVATTR) {
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_type)) != 0) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}
top:
	*vpp = NULL;

	if ((vap->va_mode & VSVTX) && secpolicy_vnode_stky_modify(cr))
		vap->va_mode &= ~VSVTX;

	if (*name == '\0') {
		/*
		 * Null component name refers to the directory itself.
		 */
		VN_HOLD(dvp);
		zp = dzp;
		dl = NULL;
		error = 0;
	} else {
		/* possible VN_HOLD(zp) */
		int zflg = 0;

		if (flag & FIGNORECASE)
			zflg |= ZCILOOK;

		error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
		    NULL, NULL);
		if (error) {
			if (strcmp(name, "..") == 0)
				error = EISDIR;
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}
	if (zp == NULL) {
		uint64_t txtype;

		/*
		 * Create a new file object and update the directory
		 * to reference it.
		 */
		if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
			goto out;
		}

		/*
		 * We only support the creation of regular files in
		 * extended attribute directories.
		 */
		if ((dzp->z_phys->zp_flags & ZFS_XATTR) &&
		    (vap->va_type != VREG)) {
			error = EINVAL;
			goto out;
		}

		if ((error = zfs_acl_ids_create(dzp, 0, vap, cr, vsecp,
		    &acl_ids)) != 0)
			goto out;
		if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
			zfs_acl_ids_free(&acl_ids);
			error = EDQUOT;
			goto out;
		}

		tx = dmu_tx_create(os);
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		fuid_dirtied = zfsvfs->z_fuid_dirty;
		if (fuid_dirtied)
			zfs_fuid_txhold(zfsvfs, tx);
		dmu_tx_hold_bonus(tx, dzp->z_id);
		dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
		if (acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, SPA_MAXBLOCKSIZE);
		}
		error = dmu_tx_assign(tx, TXG_NOWAIT);
		if (error) {
			zfs_acl_ids_free(&acl_ids);
			zfs_dirent_unlock(dl);
			if (error == ERESTART) {
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				goto top;
			}
			dmu_tx_abort(tx);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
		zfs_mknode(dzp, vap, tx, cr, 0, &zp, 0, &acl_ids);

		if (fuid_dirtied)
			zfs_fuid_sync(zfsvfs, tx);

		(void) zfs_link_create(dl, zp, tx, ZNEW);

		txtype = zfs_log_create_txtype(Z_FILE, vsecp, vap);
		if (flag & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_create(zilog, tx, txtype, dzp, zp, name,
		    vsecp, acl_ids.z_fuidp, vap);
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
	} else {
		int aflags = (flag & FAPPEND) ? V_APPEND : 0;

		/*
		 * A directory entry already exists for this name.
		 */
		/*
		 * Can't truncate an existing file if in exclusive mode.
		 */
		if (excl == EXCL) {
			error = EEXIST;
			goto out;
		}
		/*
		 * Can't open a directory for writing.
		 */
		if ((ZTOV(zp)->v_type == VDIR) && (mode & S_IWRITE)) {
			error = EISDIR;
			goto out;
		}
		/*
		 * Verify requested access to file.
		 */
		if (mode && (error = zfs_zaccess_rwx(zp, mode, aflags, cr))) {
			goto out;
		}

		mutex_enter(&dzp->z_lock);
		dzp->z_seq++;
		mutex_exit(&dzp->z_lock);

		/*
		 * Truncate regular files if requested.
		 */
		if ((ZTOV(zp)->v_type == VREG) &&
		    (vap->va_mask & AT_SIZE) && (vap->va_size == 0)) {
			/* we can't hold any locks when calling zfs_freesp() */
			zfs_dirent_unlock(dl);
			dl = NULL;
			error = zfs_freesp(zp, 0, 0, mode, TRUE);
			if (error == 0) {
				vnevent_create(ZTOV(zp), ct);
			}
		}
	}
out:

	if (dl)
		zfs_dirent_unlock(dl);

	if (error) {
		if (zp)
			VN_RELE(ZTOV(zp));
	} else {
		*vpp = ZTOV(zp);
		error = specvp_check(vpp, cr);
	}

	ZFS_EXIT(zfsvfs);
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
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime
 *	 vp - ctime (if nlink > 0)
 */
/*ARGSUSED*/
static int
zfs_remove(vnode_t *dvp, char *name, cred_t *cr, caller_context_t *ct,
    int flags)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	znode_t		*xzp = NULL;
	vnode_t		*vp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	acl_obj, xattr_obj;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	boolean_t	may_delete_now, delete_now = FALSE;
	boolean_t	unlinked, toobig = FALSE;
	uint64_t	txtype;
	pathname_t	*realnmp = NULL;
	pathname_t	realnm;
	int		error;
	int		zflg = ZEXISTS;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (flags & FIGNORECASE) {
		zflg |= ZCILOOK;
		pn_alloc(&realnm);
		realnmp = &realnm;
	}

top:
	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if (error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, realnmp)) {
		if (realnmp)
			pn_free(realnmp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	vp = ZTOV(zp);

	if (error = zfs_zaccess_delete(dzp, zp, cr)) {
		goto out;
	}

	/*
	 * Need to use rmdir for removing directories.
	 */
	if (vp->v_type == VDIR) {
		error = EPERM;
		goto out;
	}

	vnevent_remove(vp, dvp, name, ct);

	if (realnmp)
		dnlc_remove(dvp, realnmp->pn_buf);
	else
		dnlc_remove(dvp, name);

	mutex_enter(&vp->v_lock);
	may_delete_now = vp->v_count == 1 && !vn_has_cached_data(vp);
	mutex_exit(&vp->v_lock);

	/*
	 * We may delete the znode now, or we may put it in the unlinked set;
	 * it depends on whether we're the last link, and on whether there are
	 * other holds on the vnode.  So we dmu_tx_hold() the right things to
	 * allow for either case.
	 */
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_bonus(tx, zp->z_id);
	if (may_delete_now) {
		toobig =
		    zp->z_phys->zp_size > zp->z_blksz * DMU_MAX_DELETEBLKCNT;
		/* if the file is too big, only hold_free a token amount */
		dmu_tx_hold_free(tx, zp->z_id, 0,
		    (toobig ? DMU_MAX_ACCESS : DMU_OBJECT_END));
	}

	/* are there any extended attributes? */
	if ((xattr_obj = zp->z_phys->zp_xattr) != 0) {
		/* XXX - do we need this if we are deleting? */
		dmu_tx_hold_bonus(tx, xattr_obj);
	}

	/* are there any additional acls */
	if ((acl_obj = zp->z_phys->zp_acl.z_acl_extern_obj) != 0 &&
	    may_delete_now)
		dmu_tx_hold_free(tx, acl_obj, 0, DMU_OBJECT_END);

	/* charge as an update -- would be nice not to charge at all */
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		VN_RELE(vp);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		if (realnmp)
			pn_free(realnmp);
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Remove the directory entry.
	 */
	error = zfs_link_destroy(dl, zp, tx, zflg, &unlinked);

	if (error) {
		dmu_tx_commit(tx);
		goto out;
	}

	if (unlinked) {
		mutex_enter(&vp->v_lock);
		delete_now = may_delete_now && !toobig &&
		    vp->v_count == 1 && !vn_has_cached_data(vp) &&
		    zp->z_phys->zp_xattr == xattr_obj &&
		    zp->z_phys->zp_acl.z_acl_extern_obj == acl_obj;
		mutex_exit(&vp->v_lock);
	}

	if (delete_now) {
		if (zp->z_phys->zp_xattr) {
			error = zfs_zget(zfsvfs, zp->z_phys->zp_xattr, &xzp);
			ASSERT3U(error, ==, 0);
			ASSERT3U(xzp->z_phys->zp_links, ==, 2);
			dmu_buf_will_dirty(xzp->z_dbuf, tx);
			mutex_enter(&xzp->z_lock);
			xzp->z_unlinked = 1;
			xzp->z_phys->zp_links = 0;
			mutex_exit(&xzp->z_lock);
			zfs_unlinked_add(xzp, tx);
			zp->z_phys->zp_xattr = 0; /* probably unnecessary */
		}
		mutex_enter(&zp->z_lock);
		mutex_enter(&vp->v_lock);
		vp->v_count--;
		ASSERT3U(vp->v_count, ==, 0);
		mutex_exit(&vp->v_lock);
		mutex_exit(&zp->z_lock);
		zfs_znode_delete(zp, tx);
	} else if (unlinked) {
		zfs_unlinked_add(zp, tx);
	}

	txtype = TX_REMOVE;
	if (flags & FIGNORECASE)
		txtype |= TX_CI;
	zfs_log_remove(zilog, tx, txtype, dzp, name);

	dmu_tx_commit(tx);
out:
	if (realnmp)
		pn_free(realnmp);

	zfs_dirent_unlock(dl);

	if (!delete_now) {
		VN_RELE(vp);
	} else if (xzp) {
		/* this rele is delayed to prevent nesting transactions */
		VN_RELE(ZTOV(xzp));
	}

	ZFS_EXIT(zfsvfs);
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
 *		vsecp	- ACL to be set
 *
 *	OUT:	vpp	- vnode of created directory.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 *	 vp - ctime|mtime|atime updated
 */
/*ARGSUSED*/
static int
zfs_mkdir(vnode_t *dvp, char *dirname, vattr_t *vap, vnode_t **vpp, cred_t *cr,
    caller_context_t *ct, int flags, vsecattr_t *vsecp)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	uint64_t	txtype;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	ksid_t		*ksid;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;

	ASSERT(vap->va_type == VDIR);

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	ksid = crgetsid(cr, KSID_OWNER);
	if (ksid)
		uid = ksid_getid(ksid);
	else
		uid = crgetuid(cr);
	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (vsecp || (vap->va_mask & AT_XVATTR) ||
	    IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (EINVAL);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (dzp->z_phys->zp_flags & ZFS_XATTR) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (zfsvfs->z_utf8 && u8_validate(dirname,
	    strlen(dirname), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

	if (vap->va_mask & AT_XVATTR)
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_type)) != 0) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}

	/*
	 * First make sure the new directory doesn't exist.
	 */
top:
	*vpp = NULL;

	if (error = zfs_dirent_lock(&dl, dzp, dirname, &zp, zf,
	    NULL, NULL)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (error = zfs_zaccess(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE, cr)) {
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr, vsecp,
	    &acl_ids)) != 0) {
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (EDQUOT);
	}

	/*
	 * Add a new entry to the directory.
	 */
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, dirname);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	if (acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE)
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
		    0, SPA_MAXBLOCKSIZE);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create new node.
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, 0, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	/*
	 * Now put new name in parent dir.
	 */
	(void) zfs_link_create(dl, zp, tx, ZNEW);

	*vpp = ZTOV(zp);

	txtype = zfs_log_create_txtype(Z_DIR, vsecp, vap);
	if (flags & FIGNORECASE)
		txtype |= TX_CI;
	zfs_log_create(zilog, tx, txtype, dzp, zp, dirname, vsecp,
	    acl_ids.z_fuidp, vap);

	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	ZFS_EXIT(zfsvfs);
	return (0);
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
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_rmdir(vnode_t *dvp, char *name, vnode_t *cwd, cred_t *cr,
    caller_context_t *ct, int flags)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp;
	vnode_t		*vp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zflg = ZEXISTS;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;
top:
	zp = NULL;

	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if (error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, NULL)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	vp = ZTOV(zp);

	if (error = zfs_zaccess_delete(dzp, zp, cr)) {
		goto out;
	}

	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	if (vp == cwd) {
		error = EINVAL;
		goto out;
	}

	vnevent_rmdir(vp, dvp, name, ct);

	/*
	 * Grab a lock on the directory to make sure that noone is
	 * trying to add (or lookup) entries while we are removing it.
	 */
	rw_enter(&zp->z_name_lock, RW_WRITER);

	/*
	 * Grab a lock on the parent pointer to make sure we play well
	 * with the treewalk and directory rename code.
	 */
	rw_enter(&zp->z_parent_lock, RW_WRITER);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_bonus(tx, zp->z_id);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		rw_exit(&zp->z_parent_lock);
		rw_exit(&zp->z_name_lock);
		zfs_dirent_unlock(dl);
		VN_RELE(vp);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_link_destroy(dl, zp, tx, zflg, NULL);

	if (error == 0) {
		uint64_t txtype = TX_RMDIR;
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_remove(zilog, tx, txtype, dzp, name);
	}

	dmu_tx_commit(tx);

	rw_exit(&zp->z_parent_lock);
	rw_exit(&zp->z_name_lock);
out:
	zfs_dirent_unlock(dl);

	VN_RELE(vp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Read as many directory entries as will fit into the provided
 * buffer from the given directory cursor position (specified in
 * the uio structure.
 *
 *	IN:	vp	- vnode of directory to read.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *		eofp	- set to true if end-of-file detected.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - atime updated
 *
 * Note that the low 4 bits of the cookie returned by zap is always zero.
 * This allows us to use the low range for "special" directory entries:
 * We use 0 for '.', and 1 for '..'.  If this is the root of the filesystem,
 * we use the offset 2 for the '.zfs' directory.
 */
/* ARGSUSED */
static int
zfs_readdir(vnode_t *vp, uio_t *uio, cred_t *cr, int *eofp,
    caller_context_t *ct, int flags)
{
	znode_t		*zp = VTOZ(vp);
	iovec_t		*iovp;
	edirent_t	*eodp;
	dirent64_t	*odp;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	caddr_t		outbuf;
	size_t		bufsize;
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	uint_t		bytes_wanted;
	uint64_t	offset; /* must be unsigned; checks for < 1 */
	int		local_eof;
	int		outcount;
	int		error;
	uint8_t		prefetch;
	boolean_t	check_sysattrs;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * If we are not given an eof variable,
	 * use a local one.
	 */
	if (eofp == NULL)
		eofp = &local_eof;

	/*
	 * Check for valid iov_len.
	 */
	if (uio->uio_iov->iov_len <= 0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * Quit if directory has been removed (posix)
	 */
	if ((*eofp = zp->z_unlinked) != 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	error = 0;
	os = zfsvfs->z_os;
	offset = uio->uio_loffset;
	prefetch = zp->z_zn_prefetch;

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
	iovp = uio->uio_iov;
	bytes_wanted = iovp->iov_len;
	if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1) {
		bufsize = bytes_wanted;
		outbuf = kmem_alloc(bufsize, KM_SLEEP);
		odp = (struct dirent64 *)outbuf;
	} else {
		bufsize = bytes_wanted;
		odp = (struct dirent64 *)iovp->iov_base;
	}
	eodp = (struct edirent *)odp;

	/*
	 * If this VFS supports the system attribute view interface; and
	 * we're looking at an extended attribute directory; and we care
	 * about normalization conflicts on this vfs; then we must check
	 * for normalization conflicts with the sysattr name space.
	 */
	check_sysattrs = vfs_has_feature(vp->v_vfsp, VFSFT_SYSATTR_VIEWS) &&
	    (vp->v_flag & V_XATTRDIR) && zfsvfs->z_norm &&
	    (flags & V_RDDIR_ENTFLAGS);

	/*
	 * Transform to file-system independent format
	 */
	outcount = 0;
	while (outcount < bytes_wanted) {
		ino64_t objnum;
		ushort_t reclen;
		off64_t *next;

		/*
		 * Special case `.', `..', and `.zfs'.
		 */
		if (offset == 0) {
			(void) strcpy(zap.za_name, ".");
			zap.za_normalization_conflict = 0;
			objnum = zp->z_id;
		} else if (offset == 1) {
			(void) strcpy(zap.za_name, "..");
			zap.za_normalization_conflict = 0;
			objnum = zp->z_phys->zp_parent;
		} else if (offset == 2 && zfs_show_ctldir(zp)) {
			(void) strcpy(zap.za_name, ZFS_CTLDIR_NAME);
			zap.za_normalization_conflict = 0;
			objnum = ZFSCTL_INO_ROOT;
		} else {
			/*
			 * Grab next entry.
			 */
			if (error = zap_cursor_retrieve(&zc, &zap)) {
				if ((*eofp = (error == ENOENT)) != 0)
					break;
				else
					goto update;
			}

			if (zap.za_integer_length != 8 ||
			    zap.za_num_integers != 1) {
				cmn_err(CE_WARN, "zap_readdir: bad directory "
				    "entry, obj = %lld, offset = %lld\n",
				    (u_longlong_t)zp->z_id,
				    (u_longlong_t)offset);
				error = ENXIO;
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap.za_first_integer);
			/*
			 * MacOS X can extract the object type here such as:
			 * uint8_t type = ZFS_DIRENT_TYPE(zap.za_first_integer);
			 */

			if (check_sysattrs && !zap.za_normalization_conflict) {
				zap.za_normalization_conflict =
				    xattr_sysattr_casechk(zap.za_name);
			}
		}

		if (flags & V_RDDIR_ACCFILTER) {
			/*
			 * If we have no access at all, don't include
			 * this entry in the returned information
			 */
			znode_t	*ezp;
			if (zfs_zget(zp->z_zfsvfs, objnum, &ezp) != 0)
				goto skip_entry;
			if (!zfs_has_access(ezp, cr)) {
				VN_RELE(ZTOV(ezp));
				goto skip_entry;
			}
			VN_RELE(ZTOV(ezp));
		}

		if (flags & V_RDDIR_ENTFLAGS)
			reclen = EDIRENT_RECLEN(strlen(zap.za_name));
		else
			reclen = DIRENT64_RECLEN(strlen(zap.za_name));

		/*
		 * Will this entry fit in the buffer?
		 */
		if (outcount + reclen > bufsize) {
			/*
			 * Did we manage to fit anything in the buffer?
			 */
			if (!outcount) {
				error = EINVAL;
				goto update;
			}
			break;
		}
		if (flags & V_RDDIR_ENTFLAGS) {
			/*
			 * Add extended flag entry:
			 */
			eodp->ed_ino = objnum;
			eodp->ed_reclen = reclen;
			/* NOTE: ed_off is the offset for the *next* entry */
			next = &(eodp->ed_off);
			eodp->ed_eflags = zap.za_normalization_conflict ?
			    ED_CASE_CONFLICT : 0;
			(void) strncpy(eodp->ed_name, zap.za_name,
			    EDIRENT_NAMELEN(reclen));
			eodp = (edirent_t *)((intptr_t)eodp + reclen);
		} else {
			/*
			 * Add normal entry:
			 */
			odp->d_ino = objnum;
			odp->d_reclen = reclen;
			/* NOTE: d_off is the offset for the *next* entry */
			next = &(odp->d_off);
			(void) strncpy(odp->d_name, zap.za_name,
			    DIRENT64_NAMELEN(reclen));
			odp = (dirent64_t *)((intptr_t)odp + reclen);
		}
		outcount += reclen;

		ASSERT(outcount <= bufsize);

		/* Prefetch znode */
		if (prefetch)
			dmu_prefetch(os, objnum, 0, 0);

	skip_entry:
		/*
		 * Move to the next entry, fill in the previous offset.
		 */
		if (offset > 2 || (offset == 2 && !zfs_show_ctldir(zp))) {
			zap_cursor_advance(&zc);
			offset = zap_cursor_serialize(&zc);
		} else {
			offset += 1;
		}
		*next = offset;
	}
	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

	if (uio->uio_segflg == UIO_SYSSPACE && uio->uio_iovcnt == 1) {
		iovp->iov_base += outcount;
		iovp->iov_len -= outcount;
		uio->uio_resid -= outcount;
	} else if (error = uiomove(outbuf, (long)outcount, UIO_READ, uio)) {
		/*
		 * Reset the pointer.
		 */
		offset = uio->uio_loffset;
	}

update:
	zap_cursor_fini(&zc);
	if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
		kmem_free(outbuf, bufsize);

	if (error == ENOENT)
		error = 0;

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	uio->uio_loffset = offset;
	ZFS_EXIT(zfsvfs);
	return (error);
}

ulong_t zfs_fsync_sync_cnt = 4;

static int
zfs_fsync(vnode_t *vp, int syncflag, cred_t *cr, caller_context_t *ct)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	/*
	 * Regardless of whether this is required for standards conformance,
	 * this is the logical behavior when fsync() is called on a file with
	 * dirty pages.  We use B_ASYNC since the ZIL transactions are already
	 * going to be pushed out as part of the zil_commit().
	 */
	if (vn_has_cached_data(vp) && !(syncflag & FNODSYNC) &&
	    (vp->v_type == VREG) && !(IS_SWAPVP(vp)))
		(void) VOP_PUTPAGE(vp, (offset_t)0, (size_t)0, B_ASYNC, cr, ct);

	(void) tsd_set(zfs_fsyncer_key, (void *)zfs_fsync_sync_cnt);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	zil_commit(zfsvfs->z_log, zp->z_last_itx, zp->z_id);
	ZFS_EXIT(zfsvfs);
	return (0);
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
 *		ct	- caller context
 *
 *	OUT:	vap	- attribute values.
 *
 *	RETURN:	0 (always succeeds)
 */
/* ARGSUSED */
static int
zfs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	znode_phys_t *pzp;
	int	error = 0;
	uint64_t links;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t *xoap = NULL;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	pzp = zp->z_phys;

	/*
	 * If ACL is trivial don't bother looking for ACE_READ_ATTRIBUTES.
	 * Also, if we are the owner don't bother, since owner should
	 * always be allowed to read basic attributes of file.
	 */
	if (!(pzp->zp_flags & ZFS_ACL_TRIVIAL) &&
	    (pzp->zp_uid != crgetuid(cr))) {
		if (error = zfs_zaccess(zp, ACE_READ_ATTRIBUTES, 0,
		    skipaclchk, cr)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	/*
	 * Return all attributes.  It's cheaper to provide the answer
	 * than to determine whether we were asked the question.
	 */

	mutex_enter(&zp->z_lock);
	vap->va_type = vp->v_type;
	vap->va_mode = pzp->zp_mode & MODEMASK;
	zfs_fuid_map_ids(zp, cr, &vap->va_uid, &vap->va_gid);
	vap->va_fsid = zp->z_zfsvfs->z_vfs->vfs_dev;
	vap->va_nodeid = zp->z_id;
	if ((vp->v_flag & VROOT) && zfs_show_ctldir(zp))
		links = pzp->zp_links + 1;
	else
		links = pzp->zp_links;
	vap->va_nlink = MIN(links, UINT32_MAX);	/* nlink_t limit! */
	vap->va_size = pzp->zp_size;
	vap->va_rdev = vp->v_rdev;
	vap->va_seq = zp->z_seq;

	/*
	 * Add in any requested optional attributes and the create time.
	 * Also set the corresponding bits in the returned attribute bitmap.
	 */
	if ((xoap = xva_getxoptattr(xvap)) != NULL && zfsvfs->z_use_fuids) {
		if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE)) {
			xoap->xoa_archive =
			    ((pzp->zp_flags & ZFS_ARCHIVE) != 0);
			XVA_SET_RTN(xvap, XAT_ARCHIVE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_READONLY)) {
			xoap->xoa_readonly =
			    ((pzp->zp_flags & ZFS_READONLY) != 0);
			XVA_SET_RTN(xvap, XAT_READONLY);
		}

		if (XVA_ISSET_REQ(xvap, XAT_SYSTEM)) {
			xoap->xoa_system =
			    ((pzp->zp_flags & ZFS_SYSTEM) != 0);
			XVA_SET_RTN(xvap, XAT_SYSTEM);
		}

		if (XVA_ISSET_REQ(xvap, XAT_HIDDEN)) {
			xoap->xoa_hidden =
			    ((pzp->zp_flags & ZFS_HIDDEN) != 0);
			XVA_SET_RTN(xvap, XAT_HIDDEN);
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			xoap->xoa_nounlink =
			    ((pzp->zp_flags & ZFS_NOUNLINK) != 0);
			XVA_SET_RTN(xvap, XAT_NOUNLINK);
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			xoap->xoa_immutable =
			    ((pzp->zp_flags & ZFS_IMMUTABLE) != 0);
			XVA_SET_RTN(xvap, XAT_IMMUTABLE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			xoap->xoa_appendonly =
			    ((pzp->zp_flags & ZFS_APPENDONLY) != 0);
			XVA_SET_RTN(xvap, XAT_APPENDONLY);
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			xoap->xoa_nodump =
			    ((pzp->zp_flags & ZFS_NODUMP) != 0);
			XVA_SET_RTN(xvap, XAT_NODUMP);
		}

		if (XVA_ISSET_REQ(xvap, XAT_OPAQUE)) {
			xoap->xoa_opaque =
			    ((pzp->zp_flags & ZFS_OPAQUE) != 0);
			XVA_SET_RTN(xvap, XAT_OPAQUE);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			xoap->xoa_av_quarantined =
			    ((pzp->zp_flags & ZFS_AV_QUARANTINED) != 0);
			XVA_SET_RTN(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			xoap->xoa_av_modified =
			    ((pzp->zp_flags & ZFS_AV_MODIFIED) != 0);
			XVA_SET_RTN(xvap, XAT_AV_MODIFIED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) &&
		    vp->v_type == VREG &&
		    (pzp->zp_flags & ZFS_BONUS_SCANSTAMP)) {
			size_t len;
			dmu_object_info_t doi;

			/*
			 * Only VREG files have anti-virus scanstamps, so we
			 * won't conflict with symlinks in the bonus buffer.
			 */
			dmu_object_info_from_db(zp->z_dbuf, &doi);
			len = sizeof (xoap->xoa_av_scanstamp) +
			    sizeof (znode_phys_t);
			if (len <= doi.doi_bonus_size) {
				/*
				 * pzp points to the start of the
				 * znode_phys_t. pzp + 1 points to the
				 * first byte after the znode_phys_t.
				 */
				(void) memcpy(xoap->xoa_av_scanstamp,
				    pzp + 1,
				    sizeof (xoap->xoa_av_scanstamp));
				XVA_SET_RTN(xvap, XAT_AV_SCANSTAMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_CREATETIME)) {
			ZFS_TIME_DECODE(&xoap->xoa_createtime, pzp->zp_crtime);
			XVA_SET_RTN(xvap, XAT_CREATETIME);
		}
	}

	ZFS_TIME_DECODE(&vap->va_atime, pzp->zp_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, pzp->zp_mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, pzp->zp_ctime);

	mutex_exit(&zp->z_lock);

	dmu_object_size_from_db(zp->z_dbuf, &vap->va_blksize, &vap->va_nblocks);

	if (zp->z_blksz == 0) {
		/*
		 * Block size hasn't been set; suggest maximal I/O transfers.
		 */
		vap->va_blksize = zfsvfs->z_max_blksz;
	}

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	vp	- vnode of file to be modified.
 *		vap	- new attribute values.
 *			  If AT_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime updated, mtime updated if size changed.
 */
/* ARGSUSED */
static int
zfs_setattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
	caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	znode_phys_t	*pzp;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		oldva;
	xvattr_t	tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask;
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_uid, new_gid;
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp = NULL;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t fuid_dirtied = B_FALSE;

	if (mask == 0)
		return (0);

	if (mask & AT_NOSET)
		return (EINVAL);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	pzp = zp->z_phys;
	zilog = zfsvfs->z_log;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (((mask & AT_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & AT_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & AT_XVATTR))) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (mask & AT_SIZE && vp->v_type == VDIR) {
		ZFS_EXIT(zfsvfs);
		return (EISDIR);
	}

	if (mask & AT_SIZE && vp->v_type != VREG && vp->v_type != VFIFO) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * If this is an xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);

	xva_init(&tmpxvattr);

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((pzp->zp_flags & ZFS_IMMUTABLE) &&
	    ((mask & (AT_SIZE|AT_UID|AT_GID|AT_MTIME|AT_MODE)) ||
	    ((mask & AT_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if ((mask & AT_SIZE) && (pzp->zp_flags & ZFS_READONLY)) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (AT_ATIME | AT_MTIME)) {
		if (((mask & AT_ATIME) && TIMESPEC_OVERFLOW(&vap->va_atime)) ||
		    ((mask & AT_MTIME) && TIMESPEC_OVERFLOW(&vap->va_mtime))) {
			ZFS_EXIT(zfsvfs);
			return (EOVERFLOW);
		}
	}

top:
	attrzp = NULL;

	/* Can this be moved to before the top label? */
	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		ZFS_EXIT(zfsvfs);
		return (EROFS);
	}

	/*
	 * First validate permissions
	 */

	if (mask & AT_SIZE) {
		err = zfs_zaccess(zp, ACE_WRITE_DATA, 0, skipaclchk, cr);
		if (err) {
			ZFS_EXIT(zfsvfs);
			return (err);
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
			ZFS_EXIT(zfsvfs);
			return (err);
		}
	}

	if (mask & (AT_ATIME|AT_MTIME) ||
	    ((mask & AT_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM))))
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr);

	if (mask & (AT_UID|AT_GID)) {
		int	idmask = (mask & (AT_UID|AT_GID));
		int	take_owner;
		int	take_group;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & AT_MODE))
			vap->va_mode = pzp->zp_mode;

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
				secpolicy_setid_clear(vap, cr);
				trim_mask = (mask & (AT_UID|AT_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	mutex_enter(&zp->z_lock);
	oldva.va_mode = pzp->zp_mode;
	zfs_fuid_map_ids(zp, cr, &oldva.va_uid, &oldva.va_gid);
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
			    ((pzp->zp_flags & ZFS_APPENDONLY) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_APPENDONLY);
				XVA_SET_REQ(&tmpxvattr, XAT_APPENDONLY);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			if (xoap->xoa_nounlink !=
			    ((pzp->zp_flags & ZFS_NOUNLINK) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NOUNLINK);
				XVA_SET_REQ(&tmpxvattr, XAT_NOUNLINK);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			if (xoap->xoa_immutable !=
			    ((pzp->zp_flags & ZFS_IMMUTABLE) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_IMMUTABLE);
				XVA_SET_REQ(&tmpxvattr, XAT_IMMUTABLE);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			if (xoap->xoa_nodump !=
			    ((pzp->zp_flags & ZFS_NODUMP) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NODUMP);
				XVA_SET_REQ(&tmpxvattr, XAT_NODUMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			if (xoap->xoa_av_modified !=
			    ((pzp->zp_flags & ZFS_AV_MODIFIED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_MODIFIED);
				XVA_SET_REQ(&tmpxvattr, XAT_AV_MODIFIED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			if ((vp->v_type != VREG &&
			    xoap->xoa_av_quarantined) ||
			    xoap->xoa_av_quarantined !=
			    ((pzp->zp_flags & ZFS_AV_QUARANTINED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_QUARANTINED);
				XVA_SET_REQ(&tmpxvattr, XAT_AV_QUARANTINED);
			}
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}

	mutex_exit(&zp->z_lock);

	if (mask & AT_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, skipaclchk, cr) == 0) {
			err = secpolicy_setid_setsticky_clear(vp, vap,
			    &oldva, cr);
			if (err) {
				ZFS_EXIT(zfsvfs);
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
		}
		err = secpolicy_vnode_setattr(cr, vp, vap, &oldva, flags,
		    (int (*)(void *, int, cred_t *))zfs_zaccess_unix, zp);
		if (err) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}

		if (trim_mask)
			vap->va_mask |= saved_mask;
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_bonus(tx, zp->z_id);

	if (mask & AT_MODE) {
		uint64_t pmode = pzp->zp_mode;

		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		if (err = zfs_acl_chmod_setattr(zp, &aclp, new_mode))
			goto out;
		if (pzp->zp_acl.z_acl_extern_obj) {
			/* Are we upgrading ACL from old V0 format to new V1 */
			if (zfsvfs->z_version <= ZPL_VERSION_FUID &&
			    pzp->zp_acl.z_acl_version ==
			    ZFS_ACL_VERSION_INITIAL) {
				dmu_tx_hold_free(tx,
				    pzp->zp_acl.z_acl_extern_obj, 0,
				    DMU_OBJECT_END);
				dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
				    0, aclp->z_acl_bytes);
			} else {
				dmu_tx_hold_write(tx,
				    pzp->zp_acl.z_acl_extern_obj, 0,
				    aclp->z_acl_bytes);
			}
		} else if (aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, aclp->z_acl_bytes);
		}
	}

	if (mask & (AT_UID | AT_GID)) {
		if (pzp->zp_xattr) {
			err = zfs_zget(zp->z_zfsvfs, pzp->zp_xattr, &attrzp);
			if (err)
				goto out;
			dmu_tx_hold_bonus(tx, attrzp->z_id);
		}
		if (mask & AT_UID) {
			new_uid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_uid != pzp->zp_uid &&
			    zfs_usergroup_overquota(zfsvfs, B_FALSE, new_uid)) {
				err = EDQUOT;
				goto out;
			}
		}

		if (mask & AT_GID) {
			new_gid = zfs_fuid_create(zfsvfs, (uint64_t)vap->va_gid,
			    cr, ZFS_GROUP, &fuidp);
			if (new_gid != pzp->zp_gid &&
			    zfs_usergroup_overquota(zfsvfs, B_TRUE, new_gid)) {
				err = EDQUOT;
				goto out;
			}
		}
		fuid_dirtied = zfsvfs->z_fuid_dirty;
		if (fuid_dirtied) {
			if (zfsvfs->z_fuid_obj == 0) {
				dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
				dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
				    FUID_SIZE_ESTIMATE(zfsvfs));
				dmu_tx_hold_zap(tx, MASTER_NODE_OBJ,
				    FALSE, NULL);
			} else {
				dmu_tx_hold_bonus(tx, zfsvfs->z_fuid_obj);
				dmu_tx_hold_write(tx, zfsvfs->z_fuid_obj, 0,
				    FUID_SIZE_ESTIMATE(zfsvfs));
			}
		}
	}

	err = dmu_tx_assign(tx, TXG_NOWAIT);
	if (err) {
		if (err == ERESTART)
			dmu_tx_wait(tx);
		goto out;
	}

	dmu_buf_will_dirty(zp->z_dbuf, tx);

	/*
	 * Set each attribute requested.
	 * We group settings according to the locks they need to acquire.
	 *
	 * Note: you cannot set ctime directly, although it will be
	 * updated as a side-effect of calling this function.
	 */

	mutex_enter(&zp->z_lock);

	if (mask & AT_MODE) {
		mutex_enter(&zp->z_acl_lock);
		zp->z_phys->zp_mode = new_mode;
		err = zfs_aclset_common(zp, aclp, cr, tx);
		ASSERT3U(err, ==, 0);
		zp->z_acl_cached = aclp;
		aclp = NULL;
		mutex_exit(&zp->z_acl_lock);
	}

	if (attrzp)
		mutex_enter(&attrzp->z_lock);

	if (mask & AT_UID) {
		pzp->zp_uid = new_uid;
		if (attrzp)
			attrzp->z_phys->zp_uid = new_uid;
	}

	if (mask & AT_GID) {
		pzp->zp_gid = new_gid;
		if (attrzp)
			attrzp->z_phys->zp_gid = new_gid;
	}

	if (attrzp)
		mutex_exit(&attrzp->z_lock);

	if (mask & AT_ATIME)
		ZFS_TIME_ENCODE(&vap->va_atime, pzp->zp_atime);

	if (mask & AT_MTIME)
		ZFS_TIME_ENCODE(&vap->va_mtime, pzp->zp_mtime);

	/* XXX - shouldn't this be done *before* the ATIME/MTIME checks? */
	if (mask & AT_SIZE)
		zfs_time_stamper_locked(zp, CONTENT_MODIFIED, tx);
	else if (mask != 0)
		zfs_time_stamper_locked(zp, STATE_CHANGED, tx);
	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */

	if (xoap && (mask & AT_XVATTR)) {

		/*
		 * restore trimmed off masks
		 * so that return masks can be set for caller.
		 */

		if (XVA_ISSET_REQ(&tmpxvattr, XAT_APPENDONLY)) {
			XVA_SET_REQ(xvap, XAT_APPENDONLY);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_NOUNLINK)) {
			XVA_SET_REQ(xvap, XAT_NOUNLINK);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_IMMUTABLE)) {
			XVA_SET_REQ(xvap, XAT_IMMUTABLE);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_NODUMP)) {
			XVA_SET_REQ(xvap, XAT_NODUMP);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_AV_MODIFIED)) {
			XVA_SET_REQ(xvap, XAT_AV_MODIFIED);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_AV_QUARANTINED)) {
			XVA_SET_REQ(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) {
			size_t len;
			dmu_object_info_t doi;

			ASSERT(vp->v_type == VREG);

			/* Grow the bonus buffer if necessary. */
			dmu_object_info_from_db(zp->z_dbuf, &doi);
			len = sizeof (xoap->xoa_av_scanstamp) +
			    sizeof (znode_phys_t);
			if (len > doi.doi_bonus_size)
				VERIFY(dmu_set_bonus(zp->z_dbuf, len, tx) == 0);
		}
		zfs_xvattr_set(zp, xvap);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0)
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);

	mutex_exit(&zp->z_lock);

out:
	if (attrzp)
		VN_RELE(ZTOV(attrzp));

	if (aclp)
		zfs_acl_free(aclp);

	if (fuidp) {
		zfs_fuid_info_free(fuidp);
		fuidp = NULL;
	}

	if (err)
		dmu_tx_abort(tx);
	else
		dmu_tx_commit(tx);

	if (err == ERESTART)
		goto top;

	ZFS_EXIT(zfsvfs);
	return (err);
}

typedef struct zfs_zlock {
	krwlock_t	*zl_rwlock;	/* lock we acquired */
	znode_t		*zl_znode;	/* znode we held */
	struct zfs_zlock *zl_next;	/* next in list */
} zfs_zlock_t;

/*
 * Drop locks and release vnodes that were held by zfs_rename_lock().
 */
static void
zfs_rename_unlock(zfs_zlock_t **zlpp)
{
	zfs_zlock_t *zl;

	while ((zl = *zlpp) != NULL) {
		if (zl->zl_znode != NULL)
			VN_RELE(ZTOV(zl->zl_znode));
		rw_exit(zl->zl_rwlock);
		*zlpp = zl->zl_next;
		kmem_free(zl, sizeof (*zl));
	}
}

/*
 * Search back through the directory tree, using the ".." entries.
 * Lock each directory in the chain to prevent concurrent renames.
 * Fail any attempt to move a directory into one of its own descendants.
 * XXX - z_parent_lock can overlap with map or grow locks
 */
static int
zfs_rename_lock(znode_t *szp, znode_t *tdzp, znode_t *sdzp, zfs_zlock_t **zlpp)
{
	zfs_zlock_t	*zl;
	znode_t		*zp = tdzp;
	uint64_t	rootid = zp->z_zfsvfs->z_root;
	uint64_t	*oidp = &zp->z_id;
	krwlock_t	*rwlp = &szp->z_parent_lock;
	krw_t		rw = RW_WRITER;

	/*
	 * First pass write-locks szp and compares to zp->z_id.
	 * Later passes read-lock zp and compare to zp->z_parent.
	 */
	do {
		if (!rw_tryenter(rwlp, rw)) {
			/*
			 * Another thread is renaming in this path.
			 * Note that if we are a WRITER, we don't have any
			 * parent_locks held yet.
			 */
			if (rw == RW_READER && zp->z_id > szp->z_id) {
				/*
				 * Drop our locks and restart
				 */
				zfs_rename_unlock(&zl);
				*zlpp = NULL;
				zp = tdzp;
				oidp = &zp->z_id;
				rwlp = &szp->z_parent_lock;
				rw = RW_WRITER;
				continue;
			} else {
				/*
				 * Wait for other thread to drop its locks
				 */
				rw_enter(rwlp, rw);
			}
		}

		zl = kmem_alloc(sizeof (*zl), KM_SLEEP);
		zl->zl_rwlock = rwlp;
		zl->zl_znode = NULL;
		zl->zl_next = *zlpp;
		*zlpp = zl;

		if (*oidp == szp->z_id)		/* We're a descendant of szp */
			return (EINVAL);

		if (*oidp == rootid)		/* We've hit the top */
			return (0);

		if (rw == RW_READER) {		/* i.e. not the first pass */
			int error = zfs_zget(zp->z_zfsvfs, *oidp, &zp);
			if (error)
				return (error);
			zl->zl_znode = zp;
		}
		oidp = &zp->z_phys->zp_parent;
		rwlp = &zp->z_parent_lock;
		rw = RW_READER;

	} while (zp->z_id != sdzp->z_id);

	return (0);
}

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdvp	- Source directory containing the "old entry".
 *		snm	- Old entry name.
 *		tdvp	- Target directory to contain the "new entry".
 *		tnm	- New entry name.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	sdvp,tdvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_rename(vnode_t *sdvp, char *snm, vnode_t *tdvp, char *tnm, cred_t *cr,
    caller_context_t *ct, int flags)
{
	znode_t		*tdzp, *szp, *tzp;
	znode_t		*sdzp = VTOZ(sdvp);
	zfsvfs_t	*zfsvfs = sdzp->z_zfsvfs;
	zilog_t		*zilog;
	vnode_t		*realvp;
	zfs_dirlock_t	*sdl, *tdl;
	dmu_tx_t	*tx;
	zfs_zlock_t	*zl;
	int		cmp, serr, terr;
	int		error = 0;
	int		zflg = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(sdzp);
	zilog = zfsvfs->z_log;

	/*
	 * Make sure we have the real vp for the target directory.
	 */
	if (VOP_REALVP(tdvp, &realvp, ct) == 0)
		tdvp = realvp;

	if (tdvp->v_vfsp != sdvp->v_vfsp) {
		ZFS_EXIT(zfsvfs);
		return (EXDEV);
	}

	tdzp = VTOZ(tdvp);
	ZFS_VERIFY_ZP(tdzp);
	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

top:
	szp = NULL;
	tzp = NULL;
	zl = NULL;

	/*
	 * This is to prevent the creation of links into attribute space
	 * by renaming a linked file into/outof an attribute directory.
	 * See the comment in zfs_link() for why this is considered bad.
	 */
	if ((tdzp->z_phys->zp_flags & ZFS_XATTR) !=
	    (sdzp->z_phys->zp_flags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * Lock source and target directory entries.  To prevent deadlock,
	 * a lock ordering must be defined.  We lock the directory with
	 * the smallest object id first, or if it's a tie, the one with
	 * the lexically first name.
	 */
	if (sdzp->z_id < tdzp->z_id) {
		cmp = -1;
	} else if (sdzp->z_id > tdzp->z_id) {
		cmp = 1;
	} else {
		/*
		 * First compare the two name arguments without
		 * considering any case folding.
		 */
		int nofold = (zfsvfs->z_norm & ~U8_TEXTPREP_TOUPPER);

		cmp = u8_strcmp(snm, tnm, 0, nofold, U8_UNICODE_LATEST, &error);
		ASSERT(error == 0 || !zfsvfs->z_utf8);
		if (cmp == 0) {
			/*
			 * POSIX: "If the old argument and the new argument
			 * both refer to links to the same existing file,
			 * the rename() function shall return successfully
			 * and perform no other action."
			 */
			ZFS_EXIT(zfsvfs);
			return (0);
		}
		/*
		 * If the file system is case-folding, then we may
		 * have some more checking to do.  A case-folding file
		 * system is either supporting mixed case sensitivity
		 * access or is completely case-insensitive.  Note
		 * that the file system is always case preserving.
		 *
		 * In mixed sensitivity mode case sensitive behavior
		 * is the default.  FIGNORECASE must be used to
		 * explicitly request case insensitive behavior.
		 *
		 * If the source and target names provided differ only
		 * by case (e.g., a request to rename 'tim' to 'Tim'),
		 * we will treat this as a special case in the
		 * case-insensitive mode: as long as the source name
		 * is an exact match, we will allow this to proceed as
		 * a name-change request.
		 */
		if ((zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
		    (zfsvfs->z_case == ZFS_CASE_MIXED &&
		    flags & FIGNORECASE)) &&
		    u8_strcmp(snm, tnm, 0, zfsvfs->z_norm, U8_UNICODE_LATEST,
		    &error) == 0) {
			/*
			 * case preserving rename request, require exact
			 * name matches
			 */
			zflg |= ZCIEXACT;
			zflg &= ~ZCILOOK;
		}
	}

	if (cmp < 0) {
		serr = zfs_dirent_lock(&sdl, sdzp, snm, &szp,
		    ZEXISTS | zflg, NULL, NULL);
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, ZRENAMING | zflg, NULL, NULL);
	} else {
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, zflg, NULL, NULL);
		serr = zfs_dirent_lock(&sdl,
		    sdzp, snm, &szp, ZEXISTS | ZRENAMING | zflg,
		    NULL, NULL);
	}

	if (serr) {
		/*
		 * Source entry invalid or not there.
		 */
		if (!terr) {
			zfs_dirent_unlock(tdl);
			if (tzp)
				VN_RELE(ZTOV(tzp));
		}
		if (strcmp(snm, "..") == 0)
			serr = EINVAL;
		ZFS_EXIT(zfsvfs);
		return (serr);
	}
	if (terr) {
		zfs_dirent_unlock(sdl);
		VN_RELE(ZTOV(szp));
		if (strcmp(tnm, "..") == 0)
			terr = EINVAL;
		ZFS_EXIT(zfsvfs);
		return (terr);
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */

	if (error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, cr))
		goto out;

	if (ZTOV(szp)->v_type == VDIR) {
		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if (error = zfs_rename_lock(szp, tdzp, sdzp, &zl))
			goto out;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		/*
		 * Source and target must be the same type.
		 */
		if (ZTOV(szp)->v_type == VDIR) {
			if (ZTOV(tzp)->v_type != VDIR) {
				error = ENOTDIR;
				goto out;
			}
		} else {
			if (ZTOV(tzp)->v_type == VDIR) {
				error = EISDIR;
				goto out;
			}
		}
		/*
		 * POSIX dictates that when the source and target
		 * entries refer to the same file object, rename
		 * must do nothing and exit without error.
		 */
		if (szp->z_id == tzp->z_id) {
			error = 0;
			goto out;
		}
	}

	vnevent_rename_src(ZTOV(szp), sdvp, snm, ct);
	if (tzp)
		vnevent_rename_dest(ZTOV(tzp), tdvp, tnm, ct);

	/*
	 * notify the target directory if it is not the same
	 * as source directory.
	 */
	if (tdvp != sdvp) {
		vnevent_rename_dest_dir(tdvp, ct);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_bonus(tx, szp->z_id);	/* nlink changes */
	dmu_tx_hold_bonus(tx, sdzp->z_id);	/* nlink changes */
	dmu_tx_hold_zap(tx, sdzp->z_id, FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp)
		dmu_tx_hold_bonus(tx, tdzp->z_id);	/* nlink changes */
	if (tzp)
		dmu_tx_hold_bonus(tx, tzp->z_id);	/* parent changes */
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		if (zl != NULL)
			zfs_rename_unlock(&zl);
		zfs_dirent_unlock(sdl);
		zfs_dirent_unlock(tdl);
		VN_RELE(ZTOV(szp));
		if (tzp)
			VN_RELE(ZTOV(tzp));
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (tzp)	/* Attempt to remove the existing target */
		error = zfs_link_destroy(tdl, tzp, tx, zflg, NULL);

	if (error == 0) {
		error = zfs_link_create(tdl, szp, tx, ZRENAMING);
		if (error == 0) {
			szp->z_phys->zp_flags |= ZFS_AV_MODIFIED;

			error = zfs_link_destroy(sdl, szp, tx, ZRENAMING, NULL);
			ASSERT(error == 0);

			zfs_log_rename(zilog, tx,
			    TX_RENAME | (flags & FIGNORECASE ? TX_CI : 0),
			    sdzp, sdl->dl_name, tdzp, tdl->dl_name, szp);

			/* Update path information for the target vnode */
			vn_renamepath(tdvp, ZTOV(szp), tnm, strlen(tnm));
		}
	}

	dmu_tx_commit(tx);
out:
	if (zl != NULL)
		zfs_rename_unlock(&zl);

	zfs_dirent_unlock(sdl);
	zfs_dirent_unlock(tdl);

	VN_RELE(ZTOV(szp));
	if (tzp)
		VN_RELE(ZTOV(tzp));

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Insert the indicated symbolic reference entry into the directory.
 *
 *	IN:	dvp	- Directory to contain new symbolic link.
 *		link	- Name for new symlink entry.
 *		vap	- Attributes of new entry.
 *		target	- Target path of new symlink.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_symlink(vnode_t *dvp, char *name, vattr_t *vap, char *link, cred_t *cr,
    caller_context_t *ct, int flags)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	int		len = strlen(link);
	int		error;
	int		zflg = ZNEW;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;

	ASSERT(vap->va_type == VLNK);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}
	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;
top:
	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (len > MAXPATHLEN) {
		ZFS_EXIT(zfsvfs);
		return (ENAMETOOLONG);
	}

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg, NULL, NULL);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	VERIFY(0 == zfs_acl_ids_create(dzp, 0, vap, cr, NULL, &acl_ids));
	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (EDQUOT);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_bonus(tx, dzp->z_id);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	if (acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE)
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, SPA_MAXBLOCKSIZE);
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	dmu_buf_will_dirty(dzp->z_dbuf, tx);

	/*
	 * Create a new object for the symlink.
	 * Put the link content into bonus buffer if it will fit;
	 * otherwise, store it just like any other file data.
	 */
	if (sizeof (znode_phys_t) + len <= dmu_bonus_max()) {
		zfs_mknode(dzp, vap, tx, cr, 0, &zp, len, &acl_ids);
		if (len != 0)
			bcopy(link, zp->z_phys + 1, len);
	} else {
		dmu_buf_t *dbp;

		zfs_mknode(dzp, vap, tx, cr, 0, &zp, 0, &acl_ids);

		if (fuid_dirtied)
			zfs_fuid_sync(zfsvfs, tx);
		/*
		 * Nothing can access the znode yet so no locking needed
		 * for growing the znode's blocksize.
		 */
		zfs_grow_blocksize(zp, len, tx);

		VERIFY(0 == dmu_buf_hold(zfsvfs->z_os,
		    zp->z_id, 0, FTAG, &dbp));
		dmu_buf_will_dirty(dbp, tx);

		ASSERT3U(len, <=, dbp->db_size);
		bcopy(link, dbp->db_data, len);
		dmu_buf_rele(dbp, FTAG);
	}
	zp->z_phys->zp_size = len;

	/*
	 * Insert the new object into the directory.
	 */
	(void) zfs_link_create(dl, zp, tx, ZNEW);
	if (error == 0) {
		uint64_t txtype = TX_SYMLINK;
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);
	}

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	VN_RELE(ZTOV(zp));

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Return, in the buffer contained in the provided uio structure,
 * the symbolic path referred to by vp.
 *
 *	IN:	vp	- vnode of symbolic link.
 *		uoip	- structure to contain the link path.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- structure to contain the link path.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - atime updated
 */
/* ARGSUSED */
static int
zfs_readlink(vnode_t *vp, uio_t *uio, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	size_t		bufsz;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	bufsz = (size_t)zp->z_phys->zp_size;
	if (bufsz + sizeof (znode_phys_t) <= zp->z_dbuf->db_size) {
		error = uiomove(zp->z_phys + 1,
		    MIN((size_t)bufsz, uio->uio_resid), UIO_READ, uio);
	} else {
		dmu_buf_t *dbp;
		error = dmu_buf_hold(zfsvfs->z_os, zp->z_id, 0, FTAG, &dbp);
		if (error) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
		error = uiomove(dbp->db_data,
		    MIN((size_t)bufsz, uio->uio_resid), UIO_READ, uio);
		dmu_buf_rele(dbp, FTAG);
	}

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Insert a new entry into directory tdvp referencing svp.
 *
 *	IN:	tdvp	- Directory to contain new entry.
 *		svp	- vnode of new entry.
 *		name	- name of new entry.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	tdvp - ctime|mtime updated
 *	 svp - ctime updated
 */
/* ARGSUSED */
static int
zfs_link(vnode_t *tdvp, vnode_t *svp, char *name, cred_t *cr,
    caller_context_t *ct, int flags)
{
	znode_t		*dzp = VTOZ(tdvp);
	znode_t		*tzp, *szp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	vnode_t		*realvp;
	int		error;
	int		zf = ZNEW;
	uid_t		owner;

	ASSERT(tdvp->v_type == VDIR);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (VOP_REALVP(svp, &realvp, ct) == 0)
		svp = realvp;

	if (svp->v_vfsp != tdvp->v_vfsp) {
		ZFS_EXIT(zfsvfs);
		return (EXDEV);
	}
	szp = VTOZ(svp);
	ZFS_VERIFY_ZP(szp);

	if (zfsvfs->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

top:
	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_phys->zp_flags & ZFS_XATTR) !=
	    (dzp->z_phys->zp_flags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (svp->v_type == VDIR) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	owner = zfs_fuid_map_id(zfsvfs, szp->z_phys->zp_uid, cr, ZFS_OWNER);
	if (owner != crgetuid(cr) &&
	    secpolicy_basic_link(cr) != 0) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &tzp, zf, NULL, NULL);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_bonus(tx, szp->z_id);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_link_create(dl, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_link(zilog, tx, txtype, dzp, szp, name);
	}

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (error == 0) {
		vnevent_link(svp, ct);
	}

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * zfs_null_putapage() is used when the file system has been force
 * unmounted. It just drops the pages.
 */
/* ARGSUSED */
static int
zfs_null_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp,
		size_t *lenp, int flags, cred_t *cr)
{
	pvn_write_done(pp, B_INVAL|B_FORCE|B_ERROR);
	return (0);
}

/*
 * Push a page out to disk, klustering if possible.
 *
 *	IN:	vp	- file to push page to.
 *		pp	- page to push.
 *		flags	- additional flags.
 *		cr	- credentials of caller.
 *
 *	OUT:	offp	- start of range pushed.
 *		lenp	- len of range pushed.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * NOTE: callers must have locked the page to be pushed.  On
 * exit, the page (and all other pages in the kluster) must be
 * unlocked.
 */
/* ARGSUSED */
static int
zfs_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp,
		size_t *lenp, int flags, cred_t *cr)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	dmu_tx_t	*tx;
	u_offset_t	off, koff;
	size_t		len, klen;
	uint64_t	filesz;
	int		err;

	filesz = zp->z_phys->zp_size;
	off = pp->p_offset;
	len = PAGESIZE;
	/*
	 * If our blocksize is bigger than the page size, try to kluster
	 * multiple pages so that we write a full block (thus avoiding
	 * a read-modify-write).
	 */
	if (off < filesz && zp->z_blksz > PAGESIZE) {
		klen = P2ROUNDUP((ulong_t)zp->z_blksz, PAGESIZE);
		koff = ISP2(klen) ? P2ALIGN(off, (u_offset_t)klen) : 0;
		ASSERT(koff <= filesz);
		if (koff + klen > filesz)
			klen = P2ROUNDUP(filesz - koff, (uint64_t)PAGESIZE);
		pp = pvn_write_kluster(vp, pp, &off, &len, koff, klen, flags);
	}
	ASSERT3U(btop(len), ==, btopr(len));

	/*
	 * Can't push pages past end-of-file.
	 */
	if (off >= filesz) {
		/* ignore all pages */
		err = 0;
		goto out;
	} else if (off + len > filesz) {
		int npages = btopr(filesz - off);
		page_t *trunc;

		page_list_break(&pp, &trunc, npages);
		/* ignore pages past end of file */
		if (trunc)
			pvn_write_done(trunc, flags);
		len = filesz - off;
	}

	if (zfs_usergroup_overquota(zfsvfs, B_FALSE, zp->z_phys->zp_uid) ||
	    zfs_usergroup_overquota(zfsvfs, B_TRUE, zp->z_phys->zp_gid)) {
		err = EDQUOT;
		goto out;
	}
top:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, zp->z_id, off, len);
	dmu_tx_hold_bonus(tx, zp->z_id);
	err = dmu_tx_assign(tx, TXG_NOWAIT);
	if (err != 0) {
		if (err == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		goto out;
	}

	if (zp->z_blksz <= PAGESIZE) {
		caddr_t va = zfs_map_page(pp, S_READ);
		ASSERT3U(len, <=, PAGESIZE);
		dmu_write(zfsvfs->z_os, zp->z_id, off, len, va, tx);
		zfs_unmap_page(pp, va);
	} else {
		err = dmu_write_pages(zfsvfs->z_os, zp->z_id, off, len, pp, tx);
	}

	if (err == 0) {
		zfs_time_stamper(zp, CONTENT_MODIFIED, tx);
		zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, off, len, 0);
	}
	dmu_tx_commit(tx);

out:
	pvn_write_done(pp, (err ? B_ERROR : 0) | flags);
	if (offp)
		*offp = off;
	if (lenp)
		*lenp = len;

	return (err);
}

/*
 * Copy the portion of the file indicated from pages into the file.
 * The pages are stored in a page list attached to the files vnode.
 *
 *	IN:	vp	- vnode of file to push page data to.
 *		off	- position in file to put data.
 *		len	- amount of data to write.
 *		flags	- flags to control the operation.
 *		cr	- credentials of caller.
 *		ct	- caller context.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_putpage(vnode_t *vp, offset_t off, size_t len, int flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	page_t		*pp;
	size_t		io_len;
	u_offset_t	io_off;
	uint_t		blksz;
	rl_t		*rl;
	int		error = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * Align this request to the file block size in case we kluster.
	 * XXX - this can result in pretty aggresive locking, which can
	 * impact simultanious read/write access.  One option might be
	 * to break up long requests (len == 0) into block-by-block
	 * operations to get narrower locking.
	 */
	blksz = zp->z_blksz;
	if (ISP2(blksz))
		io_off = P2ALIGN_TYPED(off, blksz, u_offset_t);
	else
		io_off = 0;
	if (len > 0 && ISP2(blksz))
		io_len = P2ROUNDUP_TYPED(len + (off - io_off), blksz, size_t);
	else
		io_len = 0;

	if (io_len == 0) {
		/*
		 * Search the entire vp list for pages >= io_off.
		 */
		rl = zfs_range_lock(zp, io_off, UINT64_MAX, RL_WRITER);
		error = pvn_vplist_dirty(vp, io_off, zfs_putapage, flags, cr);
		goto out;
	}
	rl = zfs_range_lock(zp, io_off, io_len, RL_WRITER);

	if (off > zp->z_phys->zp_size) {
		/* past end of file */
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	len = MIN(io_len, P2ROUNDUP(zp->z_phys->zp_size, PAGESIZE) - io_off);

	for (off = io_off; io_off < off + len; io_off += io_len) {
		if ((flags & B_INVAL) || ((flags & B_ASYNC) == 0)) {
			pp = page_lookup(vp, io_off,
			    (flags & (B_INVAL | B_FREE)) ? SE_EXCL : SE_SHARED);
		} else {
			pp = page_lookup_nowait(vp, io_off,
			    (flags & B_FREE) ? SE_EXCL : SE_SHARED);
		}

		if (pp != NULL && pvn_getdirty(pp, flags)) {
			int err;

			/*
			 * Found a dirty page to push
			 */
			err = zfs_putapage(vp, pp, &io_off, &io_len, flags, cr);
			if (err)
				error = err;
		} else {
			io_len = PAGESIZE;
		}
	}
out:
	zfs_range_unlock(rl);
	if ((flags & B_ASYNC) == 0)
		zil_commit(zfsvfs->z_log, UINT64_MAX, zp->z_id);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*ARGSUSED*/
void
zfs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_dbuf == NULL) {
		/*
		 * The fs has been unmounted, or we did a
		 * suspend/resume and this file no longer exists.
		 */
		if (vn_has_cached_data(vp)) {
			(void) pvn_vplist_dirty(vp, 0, zfs_null_putapage,
			    B_INVAL, cr);
		}

		mutex_enter(&zp->z_lock);
		vp->v_count = 0; /* count arrives as 1 */
		mutex_exit(&zp->z_lock);
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		zfs_znode_free(zp);
		return;
	}

	/*
	 * Attempt to push any data in the page cache.  If this fails
	 * we will get kicked out later in zfs_zinactive().
	 */
	if (vn_has_cached_data(vp)) {
		(void) pvn_vplist_dirty(vp, 0, zfs_putapage, B_INVAL|B_ASYNC,
		    cr);
	}

	if (zp->z_atime_dirty && zp->z_unlinked == 0) {
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);

		dmu_tx_hold_bonus(tx, zp->z_id);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			dmu_buf_will_dirty(zp->z_dbuf, tx);
			mutex_enter(&zp->z_lock);
			zp->z_atime_dirty = 0;
			mutex_exit(&zp->z_lock);
			dmu_tx_commit(tx);
		}
	}

	zfs_zinactive(zp);
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
}

/*
 * Bounds-check the seek operation.
 *
 *	IN:	vp	- vnode seeking within
 *		ooff	- old file offset
 *		noffp	- pointer to new file offset
 *		ct	- caller context
 *
 *	RETURN:	0 if success
 *		EINVAL if new offset invalid
 */
/* ARGSUSED */
static int
zfs_seek(vnode_t *vp, offset_t ooff, offset_t *noffp,
    caller_context_t *ct)
{
	if (vp->v_type == VDIR)
		return (0);
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}

/*
 * Pre-filter the generic locking function to trap attempts to place
 * a mandatory lock on a memory mapped file.
 */
static int
zfs_frlock(vnode_t *vp, int cmd, flock64_t *bfp, int flag, offset_t offset,
    flk_callback_t *flk_cbp, cred_t *cr, caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * We are following the UFS semantics with respect to mapcnt
	 * here: If we see that the file is mapped already, then we will
	 * return an error, but we don't worry about races between this
	 * function and zfs_map().
	 */
	if (zp->z_mapcnt > 0 && MANDMODE((mode_t)zp->z_phys->zp_mode)) {
		ZFS_EXIT(zfsvfs);
		return (EAGAIN);
	}
	error = fs_frlock(vp, cmd, bfp, flag, offset, flk_cbp, cr, ct);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * If we can't find a page in the cache, we will create a new page
 * and fill it with file data.  For efficiency, we may try to fill
 * multiple pages at once (klustering) to fill up the supplied page
 * list.  Note that the pages to be filled are held with an exclusive
 * lock to prevent access by other threads while they are being filled.
 */
static int
zfs_fillpage(vnode_t *vp, u_offset_t off, struct seg *seg,
    caddr_t addr, page_t *pl[], size_t plsz, enum seg_rw rw)
{
	znode_t *zp = VTOZ(vp);
	page_t *pp, *cur_pp;
	objset_t *os = zp->z_zfsvfs->z_os;
	u_offset_t io_off, total;
	size_t io_len;
	int err;

	if (plsz == PAGESIZE || zp->z_blksz <= PAGESIZE) {
		/*
		 * We only have a single page, don't bother klustering
		 */
		io_off = off;
		io_len = PAGESIZE;
		pp = page_create_va(vp, io_off, io_len,
		    PG_EXCL | PG_WAIT, seg, addr);
	} else {
		/*
		 * Try to find enough pages to fill the page list
		 */
		pp = pvn_read_kluster(vp, off, seg, addr, &io_off,
		    &io_len, off, plsz, 0);
	}
	if (pp == NULL) {
		/*
		 * The page already exists, nothing to do here.
		 */
		*pl = NULL;
		return (0);
	}

	/*
	 * Fill the pages in the kluster.
	 */
	cur_pp = pp;
	for (total = io_off + io_len; io_off < total; io_off += PAGESIZE) {
		caddr_t va;

		ASSERT3U(io_off, ==, cur_pp->p_offset);
		va = zfs_map_page(cur_pp, S_WRITE);
		err = dmu_read(os, zp->z_id, io_off, PAGESIZE, va,
		    DMU_READ_PREFETCH);
		zfs_unmap_page(cur_pp, va);
		if (err) {
			/* On error, toss the entire kluster */
			pvn_read_done(pp, B_ERROR);
			/* convert checksum errors into IO errors */
			if (err == ECKSUM)
				err = EIO;
			return (err);
		}
		cur_pp = cur_pp->p_next;
	}

	/*
	 * Fill in the page list array from the kluster starting
	 * from the desired offset `off'.
	 * NOTE: the page list will always be null terminated.
	 */
	pvn_plist_init(pp, pl, plsz, off, io_len, rw);
	ASSERT(pl == NULL || (*pl)->p_offset == off);

	return (0);
}

/*
 * Return pointers to the pages for the file region [off, off + len]
 * in the pl array.  If plsz is greater than len, this function may
 * also return page pointers from after the specified region
 * (i.e. the region [off, off + plsz]).  These additional pages are
 * only returned if they are already in the cache, or were created as
 * part of a klustered read.
 *
 *	IN:	vp	- vnode of file to get data from.
 *		off	- position in file to get data from.
 *		len	- amount of data to retrieve.
 *		plsz	- length of provided page list.
 *		seg	- segment to obtain pages for.
 *		addr	- virtual address of fault.
 *		rw	- mode of created pages.
 *		cr	- credentials of caller.
 *		ct	- caller context.
 *
 *	OUT:	protp	- protection mode of created pages.
 *		pl	- list of pages created.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - atime updated
 */
/* ARGSUSED */
static int
zfs_getpage(vnode_t *vp, offset_t off, size_t len, uint_t *protp,
	page_t *pl[], size_t plsz, struct seg *seg, caddr_t addr,
	enum seg_rw rw, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	page_t		**pl0 = pl;
	int		err = 0;

	/* we do our own caching, faultahead is unnecessary */
	if (pl == NULL)
		return (0);
	else if (len > plsz)
		len = plsz;
	else
		len = P2ROUNDUP(len, PAGESIZE);
	ASSERT(plsz >= len);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (protp)
		*protp = PROT_ALL;

	/*
	 * Loop through the requested range [off, off + len) looking
	 * for pages.  If we don't find a page, we will need to create
	 * a new page and fill it with data from the file.
	 */
	while (len > 0) {
		if (*pl = page_lookup(vp, off, SE_SHARED))
			*(pl+1) = NULL;
		else if (err = zfs_fillpage(vp, off, seg, addr, pl, plsz, rw))
			goto out;
		while (*pl) {
			ASSERT3U((*pl)->p_offset, ==, off);
			off += PAGESIZE;
			addr += PAGESIZE;
			if (len > 0) {
				ASSERT3U(len, >=, PAGESIZE);
				len -= PAGESIZE;
			}
			ASSERT3U(plsz, >=, PAGESIZE);
			plsz -= PAGESIZE;
			pl++;
		}
	}

	/*
	 * Fill out the page array with any pages already in the cache.
	 */
	while (plsz > 0 &&
	    (*pl++ = page_lookup_nowait(vp, off, SE_SHARED))) {
			off += PAGESIZE;
			plsz -= PAGESIZE;
	}
out:
	if (err) {
		/*
		 * Release any pages we have previously locked.
		 */
		while (pl > pl0)
			page_unlock(*--pl);
	} else {
		ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	}

	*pl = NULL;

	ZFS_EXIT(zfsvfs);
	return (err);
}

/*
 * Request a memory map for a section of a file.  This code interacts
 * with common code and the VM system as follows:
 *
 *	common code calls mmap(), which ends up in smmap_common()
 *
 *	this calls VOP_MAP(), which takes you into (say) zfs
 *
 *	zfs_map() calls as_map(), passing segvn_create() as the callback
 *
 *	segvn_create() creates the new segment and calls VOP_ADDMAP()
 *
 *	zfs_addmap() updates z_mapcnt
 */
/*ARGSUSED*/
static int
zfs_map(vnode_t *vp, offset_t off, struct as *as, caddr_t *addrp,
    size_t len, uchar_t prot, uchar_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	segvn_crargs_t	vn_a;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((prot & PROT_WRITE) &&
	    (zp->z_phys->zp_flags & (ZFS_IMMUTABLE | ZFS_READONLY |
	    ZFS_APPENDONLY))) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if ((prot & (PROT_READ | PROT_EXEC)) &&
	    (zp->z_phys->zp_flags & ZFS_AV_QUARANTINED)) {
		ZFS_EXIT(zfsvfs);
		return (EACCES);
	}

	if (vp->v_flag & VNOMAP) {
		ZFS_EXIT(zfsvfs);
		return (ENOSYS);
	}

	if (off < 0 || len > MAXOFFSET_T - off) {
		ZFS_EXIT(zfsvfs);
		return (ENXIO);
	}

	if (vp->v_type != VREG) {
		ZFS_EXIT(zfsvfs);
		return (ENODEV);
	}

	/*
	 * If file is locked, disallow mapping.
	 */
	if (MANDMODE((mode_t)zp->z_phys->zp_mode) && vn_has_flocks(vp)) {
		ZFS_EXIT(zfsvfs);
		return (EAGAIN);
	}

	as_rangelock(as);
	error = choose_addr(as, addrp, len, off, ADDR_VACALIGN, flags);
	if (error != 0) {
		as_rangeunlock(as);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	vn_a.vp = vp;
	vn_a.offset = (u_offset_t)off;
	vn_a.type = flags & MAP_TYPE;
	vn_a.prot = prot;
	vn_a.maxprot = maxprot;
	vn_a.cred = cr;
	vn_a.amp = NULL;
	vn_a.flags = flags & ~MAP_TYPE;
	vn_a.szc = 0;
	vn_a.lgrp_mem_policy_flags = 0;

	error = as_map(as, *addrp, len, segvn_create, &vn_a);

	as_rangeunlock(as);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/* ARGSUSED */
static int
zfs_addmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
    size_t len, uchar_t prot, uchar_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uint64_t pages = btopr(len);

	atomic_add_64(&VTOZ(vp)->z_mapcnt, pages);
	return (0);
}

/*
 * The reason we push dirty pages as part of zfs_delmap() is so that we get a
 * more accurate mtime for the associated file.  Since we don't have a way of
 * detecting when the data was actually modified, we have to resort to
 * heuristics.  If an explicit msync() is done, then we mark the mtime when the
 * last page is pushed.  The problem occurs when the msync() call is omitted,
 * which by far the most common case:
 *
 * 	open()
 * 	mmap()
 * 	<modify memory>
 * 	munmap()
 * 	close()
 * 	<time lapse>
 * 	putpage() via fsflush
 *
 * If we wait until fsflush to come along, we can have a modification time that
 * is some arbitrary point in the future.  In order to prevent this in the
 * common case, we flush pages whenever a (MAP_SHARED, PROT_WRITE) mapping is
 * torn down.
 */
/* ARGSUSED */
static int
zfs_delmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
    size_t len, uint_t prot, uint_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uint64_t pages = btopr(len);

	ASSERT3U(VTOZ(vp)->z_mapcnt, >=, pages);
	atomic_add_64(&VTOZ(vp)->z_mapcnt, -pages);

	if ((flags & MAP_SHARED) && (prot & PROT_WRITE) &&
	    vn_has_cached_data(vp))
		(void) VOP_PUTPAGE(vp, off, len, B_ASYNC, cr, ct);

	return (0);
}

/*
 * Free or allocate space in a file.  Currently, this function only
 * supports the `F_FREESP' command.  However, this command is somewhat
 * misnamed, as its functionality includes the ability to allocate as
 * well as free space.
 *
 *	IN:	vp	- vnode of file to free data in.
 *		cmd	- action to take (only F_FREESP supported).
 *		bfp	- section of file to free/alloc.
 *		flag	- current file open mode flags.
 *		offset	- current file offset.
 *		cr	- credentials of caller [UNUSED].
 *		ct	- caller context.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime|mtime updated
 */
/* ARGSUSED */
static int
zfs_space(vnode_t *vp, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	uint64_t	off, len;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (cmd != F_FREESP) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (error = convoff(vp, bfp, 0, offset)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (bfp->l_len < 0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	off = bfp->l_start;
	len = bfp->l_len; /* 0 means from off to end of file */

	error = zfs_freesp(zp, off, len, flag, TRUE);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*ARGSUSED*/
static int
zfs_fid(vnode_t *vp, fid_t *fidp, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	uint32_t	gen;
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		size, i;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	gen = (uint32_t)zp->z_gen;

	size = (zfsvfs->z_parent != zfsvfs) ? LONG_FID_LEN : SHORT_FID_LEN;
	if (fidp->fid_len < size) {
		fidp->fid_len = size;
		ZFS_EXIT(zfsvfs);
		return (ENOSPC);
	}

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = size;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* Must have a non-zero generation number to distinguish from .zfs */
	if (gen == 0)
		gen = 1;
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	if (size == LONG_FID_LEN) {
		uint64_t	objsetid = dmu_objset_id(zfsvfs->z_os);
		zfid_long_t	*zlfid;

		zlfid = (zfid_long_t *)fidp;

		for (i = 0; i < sizeof (zlfid->zf_setid); i++)
			zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

		/* XXX - this should be the generation number for the objset */
		for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
			zlfid->zf_setgen[i] = 0;
	}

	ZFS_EXIT(zfsvfs);
	return (0);
}

static int
zfs_pathconf(vnode_t *vp, int cmd, ulong_t *valp, cred_t *cr,
    caller_context_t *ct)
{
	znode_t		*zp, *xzp;
	zfsvfs_t	*zfsvfs;
	zfs_dirlock_t	*dl;
	int		error;

	switch (cmd) {
	case _PC_LINK_MAX:
		*valp = ULONG_MAX;
		return (0);

	case _PC_FILESIZEBITS:
		*valp = 64;
		return (0);

	case _PC_XATTR_EXISTS:
		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		*valp = 0;
		error = zfs_dirent_lock(&dl, zp, "", &xzp,
		    ZXATTR | ZEXISTS | ZSHARED, NULL, NULL);
		if (error == 0) {
			zfs_dirent_unlock(dl);
			if (!zfs_dirempty(xzp))
				*valp = 1;
			VN_RELE(ZTOV(xzp));
		} else if (error == ENOENT) {
			/*
			 * If there aren't extended attributes, it's the
			 * same as having zero of them.
			 */
			error = 0;
		}
		ZFS_EXIT(zfsvfs);
		return (error);

	case _PC_SATTR_ENABLED:
	case _PC_SATTR_EXISTS:
		*valp = vfs_has_feature(vp->v_vfsp, VFSFT_SYSATTR_VIEWS) &&
		    (vp->v_type == VREG || vp->v_type == VDIR);
		return (0);

	case _PC_ACCESS_FILTERING:
		*valp = vfs_has_feature(vp->v_vfsp, VFSFT_ACCESS_FILTER) &&
		    vp->v_type == VDIR;
		return (0);

	case _PC_ACL_ENABLED:
		*valp = _ACL_ACE_ENABLED;
		return (0);

	case _PC_MIN_HOLE_SIZE:
		*valp = (ulong_t)SPA_MINBLOCKSIZE;
		return (0);

	default:
		return (fs_pathconf(vp, cmd, valp, cr, ct));
	}
}

/*ARGSUSED*/
static int
zfs_getsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	error = zfs_getacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*ARGSUSED*/
static int
zfs_setsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	error = zfs_setacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Predeclare these here so that the compiler assumes that
 * this is an "old style" function declaration that does
 * not include arguments => we won't get type mismatch errors
 * in the initializations that follow.
 */
static int zfs_inval();
static int zfs_isdir();

static int
zfs_inval()
{
	return (EINVAL);
}

static int
zfs_isdir()
{
	return (EISDIR);
}
/*
 * Directory vnode operations template
 */
vnodeops_t *zfs_dvnodeops;
const fs_operation_def_t zfs_dvnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = zfs_open },
	VOPNAME_CLOSE,		{ .vop_close = zfs_close },
	VOPNAME_READ,		{ .error = zfs_isdir },
	VOPNAME_WRITE,		{ .error = zfs_isdir },
	VOPNAME_IOCTL,		{ .vop_ioctl = zfs_ioctl },
	VOPNAME_GETATTR,	{ .vop_getattr = zfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = zfs_setattr },
	VOPNAME_ACCESS,		{ .vop_access = zfs_access },
	VOPNAME_LOOKUP,		{ .vop_lookup = zfs_lookup },
	VOPNAME_CREATE,		{ .vop_create = zfs_create },
	VOPNAME_REMOVE,		{ .vop_remove = zfs_remove },
	VOPNAME_LINK,		{ .vop_link = zfs_link },
	VOPNAME_RENAME,		{ .vop_rename = zfs_rename },
	VOPNAME_MKDIR,		{ .vop_mkdir = zfs_mkdir },
	VOPNAME_RMDIR,		{ .vop_rmdir = zfs_rmdir },
	VOPNAME_READDIR,	{ .vop_readdir = zfs_readdir },
	VOPNAME_SYMLINK,	{ .vop_symlink = zfs_symlink },
	VOPNAME_FSYNC,		{ .vop_fsync = zfs_fsync },
	VOPNAME_INACTIVE,	{ .vop_inactive = zfs_inactive },
	VOPNAME_FID,		{ .vop_fid = zfs_fid },
	VOPNAME_SEEK,		{ .vop_seek = zfs_seek },
	VOPNAME_PATHCONF,	{ .vop_pathconf = zfs_pathconf },
	VOPNAME_GETSECATTR,	{ .vop_getsecattr = zfs_getsecattr },
	VOPNAME_SETSECATTR,	{ .vop_setsecattr = zfs_setsecattr },
	VOPNAME_VNEVENT, 	{ .vop_vnevent = fs_vnevent_support },
	NULL,			NULL
};

/*
 * Regular file vnode operations template
 */
vnodeops_t *zfs_fvnodeops;
const fs_operation_def_t zfs_fvnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = zfs_open },
	VOPNAME_CLOSE,		{ .vop_close = zfs_close },
	VOPNAME_READ,		{ .vop_read = zfs_read },
	VOPNAME_WRITE,		{ .vop_write = zfs_write },
	VOPNAME_IOCTL,		{ .vop_ioctl = zfs_ioctl },
	VOPNAME_GETATTR,	{ .vop_getattr = zfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = zfs_setattr },
	VOPNAME_ACCESS,		{ .vop_access = zfs_access },
	VOPNAME_LOOKUP,		{ .vop_lookup = zfs_lookup },
	VOPNAME_RENAME,		{ .vop_rename = zfs_rename },
	VOPNAME_FSYNC,		{ .vop_fsync = zfs_fsync },
	VOPNAME_INACTIVE,	{ .vop_inactive = zfs_inactive },
	VOPNAME_FID,		{ .vop_fid = zfs_fid },
	VOPNAME_SEEK,		{ .vop_seek = zfs_seek },
	VOPNAME_FRLOCK,		{ .vop_frlock = zfs_frlock },
	VOPNAME_SPACE,		{ .vop_space = zfs_space },
	VOPNAME_GETPAGE,	{ .vop_getpage = zfs_getpage },
	VOPNAME_PUTPAGE,	{ .vop_putpage = zfs_putpage },
	VOPNAME_MAP,		{ .vop_map = zfs_map },
	VOPNAME_ADDMAP,		{ .vop_addmap = zfs_addmap },
	VOPNAME_DELMAP,		{ .vop_delmap = zfs_delmap },
	VOPNAME_PATHCONF,	{ .vop_pathconf = zfs_pathconf },
	VOPNAME_GETSECATTR,	{ .vop_getsecattr = zfs_getsecattr },
	VOPNAME_SETSECATTR,	{ .vop_setsecattr = zfs_setsecattr },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,			NULL
};

/*
 * Symbolic link vnode operations template
 */
vnodeops_t *zfs_symvnodeops;
const fs_operation_def_t zfs_symvnodeops_template[] = {
	VOPNAME_GETATTR,	{ .vop_getattr = zfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = zfs_setattr },
	VOPNAME_ACCESS,		{ .vop_access = zfs_access },
	VOPNAME_RENAME,		{ .vop_rename = zfs_rename },
	VOPNAME_READLINK,	{ .vop_readlink = zfs_readlink },
	VOPNAME_INACTIVE,	{ .vop_inactive = zfs_inactive },
	VOPNAME_FID,		{ .vop_fid = zfs_fid },
	VOPNAME_PATHCONF,	{ .vop_pathconf = zfs_pathconf },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,			NULL
};

/*
 * special share hidden files vnode operations template
 */
vnodeops_t *zfs_sharevnodeops;
const fs_operation_def_t zfs_sharevnodeops_template[] = {
	VOPNAME_GETATTR,	{ .vop_getattr = zfs_getattr },
	VOPNAME_ACCESS,		{ .vop_access = zfs_access },
	VOPNAME_INACTIVE,	{ .vop_inactive = zfs_inactive },
	VOPNAME_FID,		{ .vop_fid = zfs_fid },
	VOPNAME_PATHCONF,	{ .vop_pathconf = zfs_pathconf },
	VOPNAME_GETSECATTR,	{ .vop_getsecattr = zfs_getsecattr },
	VOPNAME_SETSECATTR,	{ .vop_setsecattr = zfs_setsecattr },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,			NULL
};

/*
 * Extended attribute directory vnode operations template
 *	This template is identical to the directory vnodes
 *	operation template except for restricted operations:
 *		VOP_MKDIR()
 *		VOP_SYMLINK()
 * Note that there are other restrictions embedded in:
 *	zfs_create()	- restrict type to VREG
 *	zfs_link()	- no links into/out of attribute space
 *	zfs_rename()	- no moves into/out of attribute space
 */
vnodeops_t *zfs_xdvnodeops;
const fs_operation_def_t zfs_xdvnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = zfs_open },
	VOPNAME_CLOSE,		{ .vop_close = zfs_close },
	VOPNAME_IOCTL,		{ .vop_ioctl = zfs_ioctl },
	VOPNAME_GETATTR,	{ .vop_getattr = zfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = zfs_setattr },
	VOPNAME_ACCESS,		{ .vop_access = zfs_access },
	VOPNAME_LOOKUP,		{ .vop_lookup = zfs_lookup },
	VOPNAME_CREATE,		{ .vop_create = zfs_create },
	VOPNAME_REMOVE,		{ .vop_remove = zfs_remove },
	VOPNAME_LINK,		{ .vop_link = zfs_link },
	VOPNAME_RENAME,		{ .vop_rename = zfs_rename },
	VOPNAME_MKDIR,		{ .error = zfs_inval },
	VOPNAME_RMDIR,		{ .vop_rmdir = zfs_rmdir },
	VOPNAME_READDIR,	{ .vop_readdir = zfs_readdir },
	VOPNAME_SYMLINK,	{ .error = zfs_inval },
	VOPNAME_FSYNC,		{ .vop_fsync = zfs_fsync },
	VOPNAME_INACTIVE,	{ .vop_inactive = zfs_inactive },
	VOPNAME_FID,		{ .vop_fid = zfs_fid },
	VOPNAME_SEEK,		{ .vop_seek = zfs_seek },
	VOPNAME_PATHCONF,	{ .vop_pathconf = zfs_pathconf },
	VOPNAME_GETSECATTR,	{ .vop_getsecattr = zfs_getsecattr },
	VOPNAME_SETSECATTR,	{ .vop_setsecattr = zfs_setsecattr },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,			NULL
};

/*
 * Error vnode operations template
 */
vnodeops_t *zfs_evnodeops;
const fs_operation_def_t zfs_evnodeops_template[] = {
	VOPNAME_INACTIVE,	{ .vop_inactive = zfs_inactive },
	VOPNAME_PATHCONF,	{ .vop_pathconf = zfs_pathconf },
	NULL,			NULL
};
