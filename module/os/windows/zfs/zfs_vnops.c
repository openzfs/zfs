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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright (c) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/dirent.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/uio.h>
#include <sys/vmsystm.h>
#include <sys/atomic.h>
#include <sys/pathname.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/policy.h>
#include <sys/sunddi.h>
#include <sys/sid.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/zfs_quota.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_rlock.h>
#include <sys/cred.h>
#include <sys/zpl.h>
#include <sys/zil.h>
#include <sys/sa_impl.h>
#include <sys/utfconv.h>

int zfs_vnop_force_formd_normalized_output = 0; /* disabled by default */

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
 *  (2)	zrele() should always be the last thing except for zil_commit()
 *	(if necessary) and ZFS_EXIT(). This is for 3 reasons:
 *	First, if it's the last reference, the vnode/znode
 *	can be freed, so the zp may point to freed memory.  Second, the last
 *	reference will call zfs_zinactive(), which may induce a lot of work --
 *	pushing cached pages (which acquires range locks) and syncing out
 *	cached atime changes.  Third, zfs_zinactive() may require a new tx,
 *	which could deadlock the system if you were already holding one.
 *	If you must call zrele() within a tx then use zfs_zrele_async().
 *
 *  (3)	All range locks must be grabbed before calling dmu_tx_assign(),
 *	as they can span dmu_tx_assign() calls.
 *
 *  (4) If ZPL locks are held, pass TXG_NOWAIT as the second argument to
 *      dmu_tx_assign().  This is critical because we don't want to block
 *      while holding locks.
 *
 *	If no ZPL locks are held (aside from ZFS_ENTER()), use TXG_WAIT.  This
 *	reduces lock contention and CPU usage when we must wait (note that if
 *	throughput is constrained by the storage, nearly every transaction
 *	must wait).
 *
 *      Note, in particular, that if a lock is sometimes acquired before
 *      the tx assigns, and sometimes after (e.g. z_lock), then failing
 *      to use a non-blocking assign can deadlock the system.  The scenario:
 *
 *	Thread A has grabbed a lock before calling dmu_tx_assign().
 *	Thread B is in an already-assigned tx, and blocks for this lock.
 *	Thread A calls dmu_tx_assign(TXG_WAIT) and blocks in txg_wait_open()
 *	forever, because the previous txg can't quiesce until B's tx commits.
 *
 *	If dmu_tx_assign() returns ERESTART and zfsvfs->z_assign is TXG_NOWAIT,
 *	then drop all locks, call dmu_tx_wait(), and try again.  On subsequent
 *	calls to dmu_tx_assign(), pass TXG_NOTHROTTLE in addition to TXG_NOWAIT,
 *	to indicate that this operation has already called dmu_tx_wait().
 *	This will ensure that we don't retry forever, waiting a short bit
 *	each time.
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
 *	ZFS_ENTER(zfsvfs);		// exit if unmounted
 * top:
 *	zfs_dirent_lock(&dl, ...)	// lock directory entry (may igrab())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
 *	if (error) {
 *		rw_exit(...);		// drop locks
 *		zfs_dirent_unlock(dl);	// unlock directory entry
 *		zrele(...);		// release held znodes
 *		if (error == ERESTART) {
 *			waited = B_TRUE;
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
 *	zrele(...);			// release held znodes
 *	zil_commit(zilog, foid);	// synchronous when necessary
 *	ZFS_EXIT(zfsvfs);		// finished in zfs
 *	return (error);			// done, report error
 */

/*
 * Virus scanning is unsupported.  It would be possible to add a hook
 * here to performance the required virus scan.  This could be done
 * entirely in the kernel or potentially as an update to invoke a
 * scanning utility.
 */
static int
zfs_vscan(struct vnode *vp, cred_t *cr, int async)
{
	return (0);
}

/* ARGSUSED */
int
zfs_open(struct vnode *vp, int mode, int flag, cred_t *cr)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ITOZSB(vp);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/* Honor ZFS_APPENDONLY file attribute */
	if ((mode & FWRITE) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((flag & O_APPEND) == 0)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	/* Virus scan eligible files on open */
	if (!zfs_has_ctldir(zp) && zfsvfs->z_vscan && S_ISREG(zp->z_mode) &&
	    !(zp->z_pflags & ZFS_AV_QUARANTINED) && zp->z_size > 0) {
		if (zfs_vscan(vp, cr, 0) != 0) {
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(EACCES));
		}
	}

	/* Keep a count of the synchronous opens in the znode */
	if (flag & (FSYNC | FDSYNC))
		atomic_inc_32(&zp->z_sync_cnt);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/* ARGSUSED */
int
zfs_close(struct vnode *vp, int flag, cred_t *cr)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ITOZSB(vp);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/* Decrement the synchronous opens in the znode */
	if (flag & (FSYNC | FDSYNC))
		atomic_dec_32(&zp->z_sync_cnt);

	if (!zfs_has_ctldir(zp) && zfsvfs->z_vscan && S_ISREG(zp->z_mode) &&
	    !(zp->z_pflags & ZFS_AV_QUARANTINED) && zp->z_size > 0)
		VERIFY(zfs_vscan(vp, cr, 1) == 0);

	ZFS_EXIT(zfsvfs);
	return (0);
}

#if defined(SEEK_HOLE) && defined(SEEK_DATA)
/*
 * Lseek support for finding holes (cmd == SEEK_HOLE) and
 * data (cmd == SEEK_DATA). "off" is an in/out parameter.
 */
static int
zfs_holey_common(struct vnode *vp, int cmd, loff_t *off)
{
	znode_t	*zp = VTOZ(vp);
	uint64_t noff = (uint64_t)*off; /* new offset */
	uint64_t file_sz;
	int error;
	boolean_t hole;

	file_sz = zp->z_size;
	if (noff >= file_sz)  {
		return (SET_ERROR(ENXIO));
	}

	if (cmd == SEEK_HOLE)
		hole = B_TRUE;
	else
		hole = B_FALSE;

	error = dmu_offset_next(ZTOZSB(zp)->z_os, zp->z_id, hole, &noff);

	if (error == ESRCH)
		return (SET_ERROR(ENXIO));

	/* file was dirty, so fall back to using generic logic */
	if (error == EBUSY) {
		if (hole)
			*off = file_sz;

		return (0);
	}

	/*
	 * We could find a hole that begins after the logical end-of-file,
	 * because dmu_offset_next() only works on whole blocks.  If the
	 * EOF falls mid-block, then indicate that the "virtual hole"
	 * at the end of the file begins at the logical EOF, rather than
	 * at the end of the last block.
	 */
	if (noff > file_sz) {
		ASSERT(hole);
		noff = file_sz;
	}

	if (noff < *off)
		return (error);
	*off = noff;
	return (error);
}

int
zfs_holey(struct vnode *vp, int cmd, loff_t *off)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	error = zfs_holey_common(vp, cmd, off);

	ZFS_EXIT(zfsvfs);
	return (error);
}
#endif /* SEEK_HOLE && SEEK_DATA */

#if defined(_KERNEL)
/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Write:	If we find a memory mapped page, we write to *both*
 *		the page and the dmu buffer.
 */
static void
update_pages(vnode_t *vp, int64_t start, int64_t len,
    objset_t *os, uint64_t oid)
{
#if 0 // WIN32 me
	znode_t *zp = VTOZ(vp);
	int error = 0;
	vm_offset_t vaddr = 0;
	upl_t upl;
	upl_page_info_t *pl = NULL;
	int upl_size;
	int upl_page;
	off_t off;

	off = start & (PAGE_SIZE - 1);
	start &= ~PAGE_MASK;

	upl_size = (off + len + (PAGE_SIZE - 1)) & ~PAGE_MASK;

	// dprintf("update_pages: start 0x%llx len 0x%llx: 1st off x%llx\n",
	//    start, len, off);
	/*
	 * Create a UPL for the current range and map its
	 * page list into the kernel virtual address space.
	 */
	error = ubc_create_upl(vp, start, upl_size, &upl, &pl,
	    UPL_FILE_IO | UPL_SET_LITE);
	if ((error != KERN_SUCCESS) || !upl) {
		printf("ZFS: update_pages failed to ubc_create_upl: %d\n",
		    error);
		return;
	}

	if (ubc_upl_map(upl, &vaddr) != KERN_SUCCESS) {
		printf("ZFS: update_pages failed to ubc_upl_map: %d\n",
		    error);
		(void) ubc_upl_abort(upl, UPL_ABORT_FREE_ON_EMPTY);
		return;
	}
	for (upl_page = 0; len > 0; ++upl_page) {
		uint64_t nbytes = MIN(PAGESIZE - off, len);
		/*
		 * We don't want a new page to "appear" in the middle of
		 * the file update (because it may not get the write
		 * update data), so we grab a lock to block
		 * zfs_getpage().
		 */
		rw_enter(&zp->z_map_lock, RW_WRITER);
		if (pl && upl_valid_page(pl, upl_page)) {
			rw_exit(&zp->z_map_lock);
			(void) dmu_read(os, oid, start+off, nbytes,
			    (void *)(vaddr+off), DMU_READ_PREFETCH);

		} else { // !upl_valid_page
			rw_exit(&zp->z_map_lock);
		}
		vaddr += PAGE_SIZE;
		start += PAGE_SIZE;
		len -= nbytes;
		off = 0;
	}

	/*
	 * Unmap the page list and free the UPL.
	 */
	(void) ubc_upl_unmap(upl);
	/*
	 * We want to abort here since due to dmu_write()
	 * we effectively didn't dirty any pages.
	 */
	(void) ubc_upl_abort(upl, UPL_ABORT_FREE_ON_EMPTY);
#endif
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Read:	We "read" preferentially from memory mapped pages,
 *		else we default from the dmu buffer.
 *
 * NOTE: We will always "break up" the IO into PAGESIZE uiomoves when
 *	 the file is memory mapped.
 */
static int
mappedread(struct vnode *vp, int nbytes, uio_t *uio)
{
	int error = ENOTSUP;
#if 0
	znode_t *zp = VTOZ(vp);
	objset_t *os = zp->z_zfsvfs->z_os;
	int len = nbytes;
	vm_offset_t vaddr = 0;
	upl_t upl;
	upl_page_info_t *pl = NULL;
	off_t upl_start;
	int upl_size;
	int upl_page;
	off_t off;

	upl_start = uio_offset(uio);
	off = upl_start & PAGE_MASK;
	upl_start &= ~PAGE_MASK;
	upl_size = (off + nbytes + (PAGE_SIZE - 1)) & ~PAGE_MASK;

	/*
	 * Create a UPL for the current range and map its
	 * page list into the kernel virtual address space.
	 */
	error = ubc_create_upl(vp, upl_start, upl_size, &upl, &pl,
	    UPL_FILE_IO | UPL_SET_LITE);
	if ((error != KERN_SUCCESS) || !upl) {
		return (EIO);
	}

	if (ubc_upl_map(upl, &vaddr) != KERN_SUCCESS) {
		(void) ubc_upl_abort(upl, UPL_ABORT_FREE_ON_EMPTY);
		return (ENOMEM);
	}

	for (upl_page = 0; len > 0; ++upl_page) {
		uint64_t bytes = MIN(PAGE_SIZE - off, len);
		if (pl && upl_valid_page(pl, upl_page)) {
			uio_setrw(uio, UIO_READ);
			error = uiomove((caddr_t)vaddr + off, bytes, UIO_READ,
			    uio);
		} else {
			error = dmu_read_uio(os, zp->z_id, uio, bytes);
		}

		vaddr += PAGE_SIZE;
		len -= bytes;
		off = 0;
		if (error)
			break;
	}

	/*
	 * Unmap the page list and free the UPL.
	 */
	(void) ubc_upl_unmap(upl);
	(void) ubc_upl_abort(upl, UPL_ABORT_FREE_ON_EMPTY);
#endif
	return (error);
}
#endif /* _KERNEL */

unsigned long zfs_read_chunk_size = 1024 * 1024; /* Tunable */
unsigned long zfs_delete_blocks = DMU_MAX_DELETEBLKCNT;

/*
 * Read bytes from specified file into supplied buffer.
 *
 *	IN:	ip	- inode of file to be read from.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		ioflag	- O_SYNC flags; used to provide FRSYNC semantics.
 *			  O_DIRECT flag; used to bypass page cache.
 *		cr	- credentials of caller.
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Side Effects:
 *	inode - atime updated if byte count > 0
 */
/* ARGSUSED */
int
zfs_read(struct vnode *vp, uio_t *uio, int ioflag, cred_t *cr)
{
	int error = 0;
	boolean_t frsync = B_FALSE;

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ITOZSB(vp);
	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EACCES));
	}

	/*
	 * Validate file offset
	 */
	if (uio_offset(uio) < (offset_t)0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio_resid(uio) == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

#ifdef FRSYNC
	/*
	 * If we're in FRSYNC mode, sync out this znode before reading it.
	 * Only do this for non-snapshots.
	 *
	 * Some platforms do not support FRSYNC and instead map it
	 * to O_SYNC, which results in unnecessary calls to zil_commit. We
	 * only honor FRSYNC requests on platforms which support it.
	 */
	frsync = !!(ioflag & FRSYNC);
#endif
	if (zfsvfs->z_log &&
	    (frsync || zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS))
		zil_commit(zfsvfs->z_log, zp->z_id);

	/*
	 * Lock the range against changes.
	 */
	zfs_locked_range_t *lr = zfs_rangelock_enter(&zp->z_rangelock,
	    uio_offset(uio), uio_resid(uio), RL_READER);

	/*
	 * If we are reading past end-of-file we can skip
	 * to the end; but we might still need to set atime.
	 */
	if (uio_offset(uio) >= zp->z_size) {
		error = 0;
		goto out;
	}

	ASSERT(uio_offset(uio) < zp->z_size);
	ssize_t n = MIN(uio_resid(uio), zp->z_size - uio_offset(uio));

	while (n > 0) {
		ssize_t nbytes = MIN(n, zfs_read_chunk_size -
		    P2PHASE(uio_offset(uio), zfs_read_chunk_size));

		if (zp->z_is_mapped && !(ioflag & O_DIRECT)) {
			error = mappedread(vp, nbytes, uio);
		} else {
			error = dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, nbytes);
		}

		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}

		n -= nbytes;
	}

out:
	zfs_rangelock_exit(lr);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Write the bytes to a file.
 *
 *	IN:	ip	- inode of file to be written to.
 *		uio	- structure supplying write location, range info,
 *			  and data buffer.
 *		ioflag	- O_APPEND flag set if in append mode.
 *			  O_DIRECT flag; used to bypass page cache.
 *		cr	- credentials of caller.
 *
 *	OUT:	uio	- updated offset and range.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime|mtime updated if byte count > 0
 */

/* ARGSUSED */
int
zfs_write(struct vnode *vp, uio_t *uio, int ioflag, cred_t *cr)
{
	int error = 0;
	ssize_t start_resid = uio_resid(uio);
	rlim64_t limit = MAXOFFSET_T;
	const iovec_t *aiov = NULL;
	arc_buf_t *abuf = NULL;
	int write_eof;

	/*
	 * Fasttrack empty write
	 */
	ssize_t n = start_resid;
	if (n == 0)
		return (0);

	if (limit == RLIM64_INFINITY || limit > MAXOFFSET_T)
		limit = MAXOFFSET_T;

	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	sa_bulk_attr_t bulk[4];
	int count = 0;
	uint64_t mtime[2], ctime[2];
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);

	/*
	 * Callers might not be able to detect properly that we are read-only,
	 * so check it explicitly here.
	 */
	if (zfs_is_readonly(zfsvfs)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EROFS));
	}

	/*
	 * If immutable or not appending then return EPERM
	 */
	if ((zp->z_pflags & (ZFS_IMMUTABLE | ZFS_READONLY)) ||
	    ((zp->z_pflags & ZFS_APPENDONLY) && !(ioflag & O_APPEND) &&
	    (uio_offset(uio) < zp->z_size))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	/*
	 * Validate file offset
	 */
	offset_t woff = ioflag & O_APPEND ? zp->z_size : uio_offset(uio);
	if (woff < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	int max_blksz = zfsvfs->z_max_blksz;
	xuio_t *xuio = NULL;

	/*
	 * Pre-fault the pages to ensure slow (eg NFS) pages
	 * don't hold up txg.
	 * Skip this if uio contains loaned arc_buf.
	 */
	if (uio_prefaultpages(MIN(n, max_blksz), uio)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EFAULT));
	}

	/*
	 * If in append mode, set the io offset pointer to eof.
	 */
	zfs_locked_range_t *lr;
	if (ioflag & O_APPEND) {
		/*
		 * Obtain an appending range lock to guarantee file append
		 * semantics.  We reset the write offset once we have the lock.
		 */
		lr = zfs_rangelock_enter(&zp->z_rangelock, 0, n, RL_APPEND);
		woff = lr->lr_offset;
		if (lr->lr_length == UINT64_MAX) {
			/*
			 * We overlocked the file because this write will cause
			 * the file block size to increase.
			 * Note that zp_size cannot change with this lock held.
			 */
			woff = zp->z_size;
		}
		uio_setoffset(uio, woff);
	} else {
		/*
		 * Note that if the file block size will change as a result of
		 * this write, then this range lock will lock the entire file
		 * so that we can re-write the block safely.
		 */
		lr = zfs_rangelock_enter(&zp->z_rangelock, woff, n, RL_WRITER);
	}

	if (woff >= limit) {
		zfs_rangelock_exit(lr);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EFBIG));
	}

	if ((woff + n) > limit || woff > (limit - n))
		n = limit - woff;

	/* Will this write extend the file length? */
	write_eof = (woff + n > zp->z_size);
	uint64_t end_size = MAX(zp->z_size, woff + n);
	zilog_t *zilog = zfsvfs->z_log;

	/*
	 * Write the file in reasonable size chunks.  Each chunk is written
	 * in a separate transaction; this keeps the intent log records small
	 * and allows us to do more fine-grained space accounting.
	 */
	while (n > 0) {
		woff = uio_offset(uio);

		if (zfs_id_overblockquota(zfsvfs, DMU_USERUSED_OBJECT,
		    zp->z_uid) ||
		    zfs_id_overblockquota(zfsvfs, DMU_GROUPUSED_OBJECT,
		    zp->z_gid) ||
		    (zp->z_projid != ZFS_DEFAULT_PROJID &&
		    zfs_id_overblockquota(zfsvfs, DMU_PROJECTUSED_OBJECT,
		    zp->z_projid))) {
			error = SET_ERROR(EDQUOT);
			break;
		}

		abuf = NULL;
		if (xuio) {

		} else if (n >= max_blksz && woff >= zp->z_size &&
		    P2PHASE(woff, max_blksz) == 0 &&
		    zp->z_blksz == max_blksz) {
			/*
			 * This write covers a full block.  "Borrow" a buffer
			 * from the dmu so that we can fill it before we enter
			 * a transaction.  This avoids the possibility of
			 * holding up the transaction if the data copy hangs
			 * up on a pagefault (e.g., from an NFS server mapping).
			 */
			size_t cbytes;

			abuf = dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
			    max_blksz);
			ASSERT(abuf != NULL);
			ASSERT(arc_buf_size(abuf) == max_blksz);
			if ((error = uiocopy(abuf->b_data, max_blksz,
			    UIO_WRITE, uio, &cbytes))) {
				dmu_return_arcbuf(abuf);
				break;
			}
			ASSERT(cbytes == max_blksz);
		}

		/*
		 * Start a transaction.
		 */
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)sa_get_db(zp->z_sa_hdl);
		DB_DNODE_ENTER(db);
		dmu_tx_hold_write_by_dnode(tx, DB_DNODE(db), woff,
		    MIN(n, max_blksz));
		DB_DNODE_EXIT(db);
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			break;
		}

		/*
		 * If rangelock_enter() over-locked we grow the blocksize
		 * and then reduce the lock range.  This will only happen
		 * on the first iteration since rangelock_reduce() will
		 * shrink down lr_length to the appropriate size.
		 */
		if (lr->lr_length == UINT64_MAX) {
			uint64_t new_blksz;

			if (zp->z_blksz > max_blksz) {
				/*
				 * File's blocksize is already larger than the
				 * "recordsize" property.  Only let it grow to
				 * the next power of 2.
				 */
				ASSERT(!ISP2(zp->z_blksz));
				new_blksz = MIN(end_size,
				    1 << highbit64(zp->z_blksz));
			} else {
				new_blksz = MIN(end_size, max_blksz);
			}
			zfs_grow_blocksize(zp, new_blksz, tx);
			zfs_rangelock_reduce(lr, woff, n);
		}

		/*
		 * XXX - should we really limit each write to z_max_blksz?
		 * Perhaps we should use SPA_MAXBLOCKSIZE chunks?
		 */
		ssize_t nbytes = MIN(n, max_blksz - P2PHASE(woff, max_blksz));

		ssize_t tx_bytes = 0;

		if (woff + nbytes > zp->z_size)
			vnode_pager_setsize(vp, woff + nbytes);

		/*
		 * This conditional is always true in OSX, it is kept so
		 * the sources look familiar to other platforms
		 */
		if (abuf == NULL) {
			tx_bytes = uio_resid(uio);
			error = dmu_write_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, nbytes, tx);
			tx_bytes -= uio_resid(uio);
		} else {
			tx_bytes = nbytes;
			/*
			 * If this is not a full block write, but we are
			 * extending the file past EOF and this data starts
			 * block-aligned, use assign_arcbuf().  Otherwise,
			 * write via dmu_write().
			 */
			if (tx_bytes < max_blksz && (!write_eof ||
			    aiov->iov_base != abuf->b_data)) {
				ASSERT(xuio);
				dmu_write(zfsvfs->z_os, zp->z_id, woff,
				    aiov->iov_len, aiov->iov_base, tx);
				dmu_return_arcbuf(abuf);
				xuio_stat_wbuf_copied();
			} else {
				ASSERT(xuio || tx_bytes == max_blksz);
				error = dmu_assign_arcbuf_by_dbuf(
				    sa_get_db(zp->z_sa_hdl), woff, abuf, tx);
				if (error != 0) {
					dmu_return_arcbuf(abuf);
					dmu_tx_commit(tx);
					break;
				}
			}
			ASSERT(tx_bytes <= uio_resid(uio));
			uioskip(uio, tx_bytes);
		}
		if (tx_bytes && zp->z_is_mapped && !(ioflag & O_DIRECT)) {
			update_pages(vp, woff, tx_bytes, zfsvfs->z_os,
			    zp->z_id);
		}

		/*
		 * If we made no progress, we're done.  If we made even
		 * partial progress, update the znode and ZIL accordingly.
		 */
		if (tx_bytes == 0) {
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
			    (void *)&zp->z_size, sizeof (uint64_t), tx);
			dmu_tx_commit(tx);
			ASSERT(error != 0);
			break;
		}

		/*
		 * Clear Set-UID/Set-GID bits on successful write if not
		 * privileged and at least one of the execute bits is set.
		 *
		 * It would be nice to do this after all writes have
		 * been done, but that would still expose the ISUID/ISGID
		 * to another app after the partial write is committed.
		 *
		 * Note: we don't call zfs_fuid_map_id() here because
		 * user 0 is not an ephemeral uid.
		 */
		mutex_enter(&zp->z_acl_lock);
		uint32_t uid = KUID_TO_SUID(zp->z_uid);
		if ((zp->z_mode & (S_IXUSR | (S_IXUSR >> 3) |
		    (S_IXUSR >> 6))) != 0 &&
		    (zp->z_mode & (S_ISUID | S_ISGID)) != 0 &&
		    secpolicy_vnode_setid_retain(cr,
		    ((zp->z_mode & S_ISUID) != 0 && uid == 0)) != 0) {
			uint64_t newmode;
			zp->z_mode &= ~(S_ISUID | S_ISGID);
			zp->z_mode = newmode = zp->z_mode;
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs),
			    (void *)&newmode, sizeof (uint64_t), tx);
		}
		mutex_exit(&zp->z_acl_lock);

		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);

		/*
		 * Update the file size (zp_size) if it has changed;
		 * account for possible concurrent updates.
		 */
		while ((end_size = zp->z_size) < uio_offset(uio)) {
			(void) atomic_cas_64(&zp->z_size, end_size,
			    uio_offset(uio));
			ASSERT(error == 0);
		}
		/*
		 * If we are replaying and eof is non zero then force
		 * the file size to the specified eof. Note, there's no
		 * concurrency during replay.
		 */
		if (zfsvfs->z_replay && zfsvfs->z_replay_eof != 0)
			zp->z_size = zfsvfs->z_replay_eof;

		if (error == 0)
			error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		else
			(void) sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);

		zfs_log_write(zilog, tx, TX_WRITE, zp, woff, tx_bytes, ioflag,
		    NULL, NULL);
		dmu_tx_commit(tx);

		if (error != 0)
			break;

		ASSERT(tx_bytes == nbytes);
		n -= nbytes;
	}

	zfs_rangelock_exit(lr);

	/*
	 * If we're in replay mode, or we made no progress, return error.
	 * Otherwise, it's at least a partial write, so it's successful.
	 */
	if (zfsvfs->z_replay || uio_resid(uio) == start_resid) {
		dprintf("%s: error resid %llu\n", __func__, uio_resid(uio));
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (ioflag & (O_SYNC | O_DSYNC) ||
	    zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, zp->z_id);

	ZFS_EXIT(zfsvfs);

	return (0);
}

/*
 * Write the bytes to a file.
 *
 *	IN:	zp	- znode of file to be written to
 *		data	- bytes to write
 *		len	- number of bytes to write
 *		pos	- offset to start writing at
 *
 *	OUT:	resid	- remaining bytes to write
 *
 *	RETURN:	0 if success
 *		positive error code if failure
 *
 * Timestamps:
 *	zp - ctime|mtime updated if byte count > 0
 */
int
zfs_write_simple(znode_t *zp, const void *data, size_t len,
    loff_t pos, size_t *presid)
{
	int error = 0;
	ssize_t resid;

	error = zfs_vn_rdwr(UIO_WRITE, ZTOV(zp), data, len,
	    pos, UIO_SYSSPACE, 0 /*IO_SYNC*/, RLIM64_INFINITY, NULL, &resid);

	if (error) {
		return (SET_ERROR(error));
	} else if (presid == NULL) {
		if (resid != 0) {
			error = SET_ERROR(EIO);
		}
	} else {
		*presid = resid;
	}
	return (error);
}

/*
 * Drop a reference on the passed inode asynchronously. This ensures
 * that the caller will never drop the last reference on an inode in
 * the current context. Doing so while holding open a tx could result
 * in a deadlock if iput_final() re-enters the filesystem code.
 */
void
zfs_zrele_async(znode_t *zp)
{
	struct vnode *vp = ZTOV(zp);
	objset_t *os = ITOZSB(vp)->z_os;

	ASSERT(os != NULL);

	if (vnode_iocount(vp) == 1)
		VERIFY(taskq_dispatch(dsl_pool_zrele_taskq(dmu_objset_pool(os)),
		    (task_func_t *)vnode_put, vp, TQ_SLEEP) != TASKQID_INVALID);
	else
		zrele(zp);
}

/* ARGSUSED */
void
zfs_get_done(zgd_t *zgd, int error)
{
	znode_t *zp = zgd->zgd_private;

	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_rangelock_exit(zgd->zgd_lr);

	/*
	 * Release the vnode asynchronously as we currently have the
	 * txg stopped from syncing.
	 */
	zfs_zrele_async(zp);

	kmem_free(zgd, sizeof (zgd_t));
}

#ifdef DEBUG
static int zil_fault_io = 0;
#endif

/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zfs_get_data(void *arg, lr_write_t *lr, char *buf, struct lwb *lwb, zio_t *zio)
{
	zfsvfs_t *zfsvfs = arg;
	objset_t *os = zfsvfs->z_os;
	znode_t *zp;
	uint64_t object = lr->lr_foid;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error = 0;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3P(zio, !=, NULL);
	ASSERT3U(size, !=, 0);

	/*
	 * Nothing to do if the file has been removed
	 */
	if (zfs_zget(zfsvfs, object, &zp) != 0)
		return (SET_ERROR(ENOENT));
	if (zp->z_unlinked) {
		/*
		 * Release the vnode asynchronously as we currently have the
		 * txg stopped from syncing.
		 */
		zfs_zrele_async(zp);
		return (SET_ERROR(ENOENT));
	}

	zgd = kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_lwb = lwb;
	zgd->zgd_private = zp;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_lr = zfs_rangelock_enter(&zp->z_rangelock,
		    offset, size, RL_READER);
		/* test for truncation needs to be done while range locked */
		if (offset >= zp->z_size) {
			error = SET_ERROR(ENOENT);
		} else {
			error = dmu_read(os, object, offset, size, buf,
			    DMU_READ_NO_PREFETCH);
		}
		ASSERT(error == 0 || error == ENOENT);
	} else { /* indirect write */
		/*
		 * Have to lock the whole block to ensure when it's
		 * written out and its checksum is being calculated
		 * that no one can change the data. We need to re-check
		 * blocksize after we get the lock in case it's changed!
		 */
		for (;;) {
			uint64_t blkoff;
			size = zp->z_blksz;
			blkoff = ISP2(size) ? P2PHASE(offset, size) : offset;
			offset -= blkoff;
			zgd->zgd_lr = zfs_rangelock_enter(&zp->z_rangelock,
			    offset, size, RL_READER);
			if (zp->z_blksz == size)
				break;
			offset += blkoff;
			zfs_rangelock_exit(zgd->zgd_lr);
		}
		/* test for truncation needs to be done while range locked */
		if (lr->lr_offset >= zp->z_size)
			error = SET_ERROR(ENOENT);
#ifdef DEBUG
		if (zil_fault_io) {
			error = SET_ERROR(EIO);
			zil_fault_io = 0;
		}
#endif
		if (error == 0)
			error = dmu_buf_hold(os, object, offset, zgd, &db,
			    DMU_READ_NO_PREFETCH);

		if (error == 0) {
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zfs_get_done, zgd);
			ASSERT(error || lr->lr_length <= size);

			/*
			 * On success, we need to wait for the write I/O
			 * initiated by dmu_sync() to complete before we can
			 * release this dbuf.  We will finish everything up
			 * in the zfs_get_done() callback.
			 */
			if (error == 0)
				return (0);

			if (error == EALREADY) {
				lr->lr_common.lrc_txtype = TX_WRITE2;
				/*
				 * TX_WRITE2 relies on the data previously
				 * written by the TX_WRITE that caused
				 * EALREADY.  We zero out the BP because
				 * it is the old, currently-on-disk BP.
				 */
				zgd->zgd_bp = NULL;
				BP_ZERO(bp);
				error = 0;
			}
		}
	}

	zfs_get_done(zgd, error);

	return (error);
}

/*ARGSUSED*/
int
zfs_access(struct vnode *vp, int mode, int flag, cred_t *cr)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ITOZSB(vp);
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
 * Lookup an entry in a directory, or an extended attribute directory.
 * If it exists, return a held inode reference for it.
 *
 *	IN:	zdp	- znode of directory to search.
 *		nm	- name of entry to lookup.
 *		flags	- LOOKUP_XATTR set if looking for an attribute.
 *		cr	- credentials of caller.
 *		direntflags - directory lookup flags
 *		realpnp - returned pathname.
 *
 *	OUT:	zpp	- znode of located entry, NULL if not found.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	NA
 */
/* ARGSUSED */
int
zfs_lookup(znode_t *zdp, char *nm, znode_t **zpp, int flags,
    cred_t *cr, int *direntflags, struct componentname *realpnp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zdp);
	int error = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zdp);

	*zpp = NULL;

	/*
	 * OsX has separate vnops for XATTR activity
	 */


	if (!S_ISDIR(zdp->z_mode)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(ENOTDIR));
	}

	/*
	 * Check accessibility of directory.
	 */

	if ((error = zfs_zaccess(zdp, ACE_EXECUTE, 0, B_FALSE, cr))) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfsvfs->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}

	error = zfs_dirlook(zdp, nm, zpp, flags, direntflags, realpnp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Attempt to create a new entry in a directory.  If the entry
 * already exists, truncate the file if permissible, else return
 * an error.  Return the ip of the created or trunc'd file.
 *
 *	IN:	dzp	- znode of directory to put new file entry in.
 *		name	- name of new file entry.
 *		vap	- attributes of new file.
 *		excl	- flag indicating exclusive or non-exclusive mode.
 *		mode	- mode to open file with.
 *		cr	- credentials of caller.
 *		flag	- file flag.
 *		vsecp	- ACL to be set
 *
 *	OUT:	zpp	- znode of created or trunc'd entry.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dzp - ctime|mtime updated if new entry created
 *	 zp - ctime|mtime always, atime if new
 */

/* ARGSUSED */
int
zfs_create(znode_t *dzp, char *name, vattr_t *vap, int excl,
    int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp)
{
	znode_t		*zp = NULL;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	objset_t	*os;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	uid_t		uid;
	gid_t		gid;
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	boolean_t	have_acl = B_FALSE;
	boolean_t	waited = B_FALSE;

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	gid = crgetgid(cr);
	uid = crgetuid(cr);

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (vsecp || IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr(vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

top:
	*zpp = NULL;
	if (*name == '\0') {
		/*
		 * Null component name refers to the directory itself.
		 */
		zhold(dzp);
		zp = dzp;
		dl = NULL;
		error = 0;
	} else {
		/* possible igrab(zp) */
		int zflg = 0;

		if (flag & FIGNORECASE)
			zflg |= ZCILOOK;

		error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
		    NULL, NULL);
		if (error) {
			if (have_acl)
				zfs_acl_ids_free(&acl_ids);
			if (strcmp(name, "..") == 0)
				error = SET_ERROR(EISDIR);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	if (zp == NULL) {
		uint64_t txtype;
		uint64_t projid = ZFS_DEFAULT_PROJID;

		/*
		 * Create a new file object and update the directory
		 * to reference it.
		 */
		if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
			if (have_acl)
				zfs_acl_ids_free(&acl_ids);
			goto out;
		}

		/*
		 * We only support the creation of regular files in
		 * extended attribute directories.
		 */

		if ((dzp->z_pflags & ZFS_XATTR) && !S_ISREG(vap->va_mode)) {
			if (have_acl)
				zfs_acl_ids_free(&acl_ids);
			error = SET_ERROR(EINVAL);
			goto out;
		}

		if (!have_acl && (error = zfs_acl_ids_create(dzp, 0, vap,
		    cr, vsecp, &acl_ids)) != 0)
			goto out;
		have_acl = B_TRUE;

		if (S_ISREG(vap->va_mode) || S_ISDIR(vap->va_mode))
			projid = zfs_inherit_projid(dzp);
		if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, projid)) {
			zfs_acl_ids_free(&acl_ids);
			error = SET_ERROR(EDQUOT);
			goto out;
		}

		tx = dmu_tx_create(os);

		dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
		    ZFS_SA_BASE_ATTR_SIZE);

		fuid_dirtied = zfsvfs->z_fuid_dirty;
		if (fuid_dirtied)
			zfs_fuid_txhold(zfsvfs, tx);
		dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
		dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
		if (!zfsvfs->z_use_sa &&
		    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, acl_ids.z_aclp->z_acl_bytes);
		}

		error = dmu_tx_assign(tx,
		    (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
		if (error) {
			zfs_dirent_unlock(dl);
			if (error == ERESTART) {
				waited = B_TRUE;
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				goto top;
			}
			zfs_acl_ids_free(&acl_ids);
			dmu_tx_abort(tx);
			ZFS_EXIT(zfsvfs);
			return (error);
		}

		zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

		error = zfs_link_create(dl, zp, tx, ZNEW);
		if (error != 0) {
			/*
			 * Since, we failed to add the directory entry for it,
			 * delete the newly created dnode.
			 */
			zfs_znode_delete(zp, tx);
			zfs_acl_ids_free(&acl_ids);
			dmu_tx_commit(tx);

			/*
			 * Failed, have zp but on OsX we don't have a vp, as it
			 * would have been attached below, and we've cleared out
			 * zp, signal then not to call zrele() on it.
			 */
			if (ZTOV(zp) == NULL) {
				zfs_znode_free(zp);
				zp = NULL;
			}

			goto out;
		}

		if (fuid_dirtied)
			zfs_fuid_sync(zfsvfs, tx);

		txtype = zfs_log_create_txtype(Z_FILE, vsecp, vap);
		if (flag & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_create(zilog, tx, txtype, dzp, zp, name,
		    vsecp, acl_ids.z_fuidp, vap);
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);

		/*
		 * OS X - attach the vnode _after_ committing the transaction
		 */
		zfs_znode_getvnode(zp, dzp, zfsvfs);

	} else {
		int aflags = (flag & O_APPEND) ? V_APPEND : 0;

		if (have_acl)
			zfs_acl_ids_free(&acl_ids);
		have_acl = B_FALSE;

		/*
		 * A directory entry already exists for this name.
		 */
		/*
		 * Can't truncate an existing file if in exclusive mode.
		 */
		if (excl) {
			error = SET_ERROR(EEXIST);
			goto out;
		}
		/*
		 * Can't open a directory for writing.
		 */
		if (S_ISDIR(zp->z_mode)) {
			error = SET_ERROR(EISDIR);
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
		if (S_ISREG(zp->z_mode) &&
		    (vap->va_mask & ATTR_SIZE) && (vap->va_size == 0)) {
			/* we can't hold any locks when calling zfs_freesp() */
			if (dl) {
				zfs_dirent_unlock(dl);
				dl = NULL;
			}
			error = zfs_freesp(zp, 0, 0, mode, TRUE);
		}
	}
out:

	if (dl)
		zfs_dirent_unlock(dl);

	if (error) {
		if (zp)
			zrele(zp);
	} else {
		*zpp = zp;
	}

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Remove an entry from a directory.
 *
 *	IN:	dzp	- znode of directory to remove entry from.
 *		name	- name of entry to remove.
 *		cr	- credentials of caller.
 *		flags	- case flags.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dzp - ctime|mtime
 *	 ip - ctime (if nlink > 0)
 */

uint64_t null_xattr = 0;

/*ARGSUSED*/
int
zfs_remove(znode_t *dzp, char *name, cred_t *cr, int flags)
{
	znode_t		*zp;
	znode_t		*xzp;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	uint64_t	acl_obj, xattr_obj;
	uint64_t	xattr_obj_unlinked = 0;
	uint64_t	obj = 0;
	uint64_t	links;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	boolean_t	may_delete_now, delete_now = FALSE;
	boolean_t	unlinked, toobig = FALSE;
	uint64_t	txtype;
	struct componentname	*realnmp = NULL;
	struct componentname	realnm = { 0 };
	int		error;
	int		zflg = ZEXISTS;
	boolean_t	waited = B_FALSE;

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (flags & FIGNORECASE) {
		zflg |= ZCILOOK;

		realnm.cn_nameptr = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
		realnm.cn_namelen = MAXPATHLEN;
		realnmp = &realnm;
	}

top:
	xattr_obj = 0;
	xzp = NULL;
	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if ((error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, realnmp))) {
		if (realnmp)
			kmem_free(realnm.cn_nameptr, realnm.cn_namelen);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if ((error = zfs_zaccess_delete(dzp, zp, cr))) {
		goto out;
	}

	/*
	 * Need to use rmdir for removing directories.
	 */
	if (S_ISDIR(zp->z_mode)) {
		error = SET_ERROR(EPERM);
		goto out;
	}

	mutex_enter(&zp->z_lock);
	may_delete_now = vnode_iocount(ZTOV(zp)) == 1 &&
	    !(zp->z_is_mapped);
	mutex_exit(&zp->z_lock);

	/*
	 * We may delete the znode now, or we may put it in the unlinked set;
	 * it depends on whether we're the last link, and on whether there are
	 * other holds on the inode.  So we dmu_tx_hold() the right things to
	 * allow for either case.
	 */
	obj = zp->z_id;
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	if (may_delete_now) {
		toobig = zp->z_size > zp->z_blksz * zfs_delete_blocks;
		/* if the file is too big, only hold_free a token amount */
		dmu_tx_hold_free(tx, zp->z_id, 0,
		    (toobig ? DMU_MAX_ACCESS : DMU_OBJECT_END));
	}

	/* are there any extended attributes? */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error == 0 && xattr_obj) {
		error = zfs_zget(zfsvfs, xattr_obj, &xzp);
		ASSERT0(error);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
	}

	mutex_enter(&zp->z_lock);
	if ((acl_obj = zfs_external_acl(zp)) != 0 && may_delete_now)
		dmu_tx_hold_free(tx, acl_obj, 0, DMU_OBJECT_END);
	mutex_exit(&zp->z_lock);

	/* charge as an update -- would be nice not to charge at all */
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	/*
	 * Mark this transaction as typically resulting in a net free of space
	 */
	dmu_tx_mark_netfree(tx);

	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			zrele(zp);
			if (xzp)
				zrele(xzp);
			goto top;
		}
		if (realnmp)
			kmem_free(realnm.cn_nameptr, realnm.cn_namelen);
		dmu_tx_abort(tx);
		zrele(zp);
		if (xzp)
			zrele(xzp);
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
		/*
		 * Hold z_lock so that we can make sure that the ACL obj
		 * hasn't changed.  Could have been deleted due to
		 * zfs_sa_upgrade().
		 */
		mutex_enter(&zp->z_lock);
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj_unlinked, sizeof (xattr_obj_unlinked));
		delete_now = may_delete_now && !toobig &&
		    vnode_iocount(ZTOV(zp)) == 1 &&
		    !(zp->z_is_mapped) && xattr_obj == xattr_obj_unlinked &&
		    zfs_external_acl(zp) == acl_obj;
	}

	if (delete_now) {
		if (xattr_obj_unlinked) {
			mutex_enter(&xzp->z_lock);
			xzp->z_unlinked = B_TRUE;
			links = 0;
			error = sa_update(xzp->z_sa_hdl, SA_ZPL_LINKS(zfsvfs),
			    &links, sizeof (links), tx);
			ASSERT3U(error,  ==,  0);
			mutex_exit(&xzp->z_lock);
			zfs_unlinked_add(xzp, tx);

			if (zp->z_is_sa)
				error = sa_remove(zp->z_sa_hdl,
				    SA_ZPL_XATTR(zfsvfs), tx);
			else
				error = sa_update(zp->z_sa_hdl,
				    SA_ZPL_XATTR(zfsvfs), &null_xattr,
				    sizeof (uint64_t), tx);
			ASSERT0(error);
		}
		/*
		 * Add to the unlinked set because a new reference could be
		 * taken concurrently resulting in a deferred destruction.
		 */
		zfs_unlinked_add(zp, tx);
		mutex_exit(&zp->z_lock);
	} else if (unlinked) {
		mutex_exit(&zp->z_lock);
		zfs_unlinked_add(zp, tx);
	}

	txtype = TX_REMOVE;
	if (flags & FIGNORECASE)
		txtype |= TX_CI;
	zfs_log_remove(zilog, tx, txtype, dzp, name, obj, unlinked);

	dmu_tx_commit(tx);
out:
	if (realnmp)
			kmem_free(realnm.cn_nameptr, realnm.cn_namelen);

	zfs_dirent_unlock(dl);

	if (delete_now)
		zrele(zp);
	else
		zfs_zrele_async(zp);

	if (xzp)
		zfs_zrele_async(xzp);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Create a new directory and insert it into dzp using the name
 * provided.  Return a pointer to the inserted directory.
 *
 *	IN:	dzp	- znode of directory to add subdir to.
 *		dirname	- name of new directory.
 *		vap	- attributes of new directory.
 *		cr	- credentials of caller.
 *		flags	- case flags.
 *		vsecp	- ACL to be set
 *
 *	OUT:	zpp	- znode of created directory.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dzp - ctime|mtime updated
 *	zpp - ctime|mtime|atime updated
 */
/*ARGSUSED*/
int
zfs_mkdir(znode_t *dzp, char *dirname, vattr_t *vap, znode_t **zpp,
    cred_t *cr, int flags, vsecattr_t *vsecp)
{
	znode_t		*zp;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	uint64_t	txtype;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	boolean_t	waited = B_FALSE;

	ASSERT(S_ISDIR(vap->va_mode));

	/*
	 * If we have an ephemeral id, ACL, or XVATTR then
	 * make sure file system is at proper version
	 */

	uid = crgetuid(cr);
	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (vsecp || IS_EPHEMERAL(uid) || IS_EPHEMERAL(gid)))
		return (SET_ERROR(EINVAL));

	if (dirname == NULL)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (dzp->z_pflags & ZFS_XATTR) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	if (zfsvfs->z_utf8 && u8_validate(dirname,
	    strlen(dirname), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr(vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr,
	    vsecp, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	/*
	 * First make sure the new directory doesn't exist.
	 *
	 * Existence is checked first to make sure we don't return
	 * EACCES instead of EEXIST which can cause some applications
	 * to fail.
	 */
top:
	*zpp = NULL;

	if ((error = zfs_dirent_lock(&dl, dzp, dirname, &zp, zf,
	    NULL, NULL))) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE, cr))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, zfs_inherit_projid(dzp))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EDQUOT));
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
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create new node.
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	/*
	 * Now put new name in parent dir.
	 */
	error = zfs_link_create(dl, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
		goto out;
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	*zpp = zp;

	txtype = zfs_log_create_txtype(Z_DIR, vsecp, vap);
	if (flags & FIGNORECASE)
		txtype |= TX_CI;
	zfs_log_create(zilog, tx, txtype, dzp, zp, dirname, vsecp,
	    acl_ids.z_fuidp, vap);

out:
	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);
	/*
	 * OS X - attach the vnode _after_ committing the transaction
	 */
	zfs_znode_getvnode(zp, dzp, zfsvfs);

	zfs_dirent_unlock(dl);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	if (error != 0) {
		zrele(zp);
	} else {
	}
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Remove a directory subdir entry.  If the current working
 * directory is the same as the subdir to be removed, the
 * remove will fail.
 *
 *	IN:	dzp	- znode of directory to remove from.
 *		name	- name of directory to be removed.
 *		cwd	- inode of current working directory.
 *		cr	- credentials of caller.
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dzp - ctime|mtime updated
 */
/*ARGSUSED*/
int
zfs_rmdir(znode_t *dzp, char *name, znode_t *cwd, cred_t *cr,
    int flags)
{
	znode_t		*zp;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zflg = ZEXISTS;
	boolean_t	waited = B_FALSE;

	if (name == NULL)
		return (SET_ERROR(EINVAL));

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
	if ((error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, NULL))) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if ((error = zfs_zaccess_delete(dzp, zp, cr))) {
		goto out;
	}

	if (ZTOTYPE(zp) != VDIR) {
		error = SET_ERROR(ENOTDIR);
		goto out;
	}

	if (zp == cwd) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * Grab a lock on the directory to make sure that no one is
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
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
	if (error) {
		rw_exit(&zp->z_parent_lock);
		rw_exit(&zp->z_name_lock);
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			zrele(zp);
			goto top;
		}
		dmu_tx_abort(tx);
		zrele(zp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_link_destroy(dl, zp, tx, zflg, NULL);

	if (error == 0) {
		uint64_t txtype = TX_RMDIR;
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_remove(zilog, tx, txtype, dzp, name, ZFS_NO_OBJECT,
		    B_FALSE);
	}

	dmu_tx_commit(tx);

	rw_exit(&zp->z_parent_lock);
	rw_exit(&zp->z_name_lock);
out:
	zfs_dirent_unlock(dl);

	zrele(zp);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Read directory entries from the given directory cursor position and emit
 * name and position for each entry.
 *
 *	IN:	ip	- inode of directory to read.
 *		ctx	- directory entry context.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - atime updated
 *
 * Note that the low 4 bits of the cookie returned by zap is always zero.
 * This allows us to use the low range for "special" directory entries:
 * We use 0 for '.', and 1 for '..'.  If this is the root of the filesystem,
 * we use the offset 2 for the '.zfs' directory.
 */
/* ARGSUSED */
int
zfs_readdir(vnode_t *vp, uio_t *uio, cred_t *cr, zfs_dirlist_t *zccb, int flags, int dirlisttype, int *a_numdirent)
{
	int		error = 0;

	znode_t		*zp = VTOZ(vp);
	znode_t		*tzp;
	iovec_t		*iovp;
	FILE_FULL_DIR_INFORMATION *eodp = NULL;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	caddr_t		outbuf;
	size_t		bufsize;
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	uint_t		bytes_wanted;
	uint64_t	offset; /* must be unsigned; checks for < 1 */
	uint64_t	parent;
	int		local_eof;
	int		outcount;
	uint8_t		prefetch;
	boolean_t	check_sysattrs;
	uint8_t		type;
	int		numdirent = 0;
	char		*bufptr;
	void *nameptr = NULL;
	ULONG namelenholder = 0;
	uint32_t *eofp = &zccb->dir_eof;
	int last_alignment = 0;
	int skip_this_entry;
	int structsize = 0;
	int flag_index_specified    = flags & SL_INDEX_SPECIFIED     ? 1 : 0;
	int flag_restart_scan       = flags & SL_RESTART_SCAN        ? 1 : 0;
	int flag_return_single_entry= flags & SL_RETURN_SINGLE_ENTRY ? 1 : 0;
	FILE_DIRECTORY_INFORMATION *fdi;

	dprintf("+zfs_readdir: Index %d, Restart %d, Single %d\n",
		flag_index_specified, flag_restart_scan, flag_return_single_entry);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent))) != 0) {
		ZFS_EXIT(zfsvfs);
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
	if (uio_curriovlen(uio) <= 0) {
		ZFS_EXIT(zfsvfs);
		return ((EINVAL));
	}

	/*
	 * Quit if directory has been removed (posix)
	 */
	if ((*eofp = zp->z_unlinked) != 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	// Make sure the dirlist type is a valid one
	switch (dirlisttype) {
	case FileFullDirectoryInformation:
	case FileIdBothDirectoryInformation:
	case FileBothDirectoryInformation:
	case FileDirectoryInformation:
	case FileNamesInformation:
	case FileIdFullDirectoryInformation:
		break;
	default:
		dprintf("%s: ** Directory type %d not handled!\n", __func__, dirlisttype);
		ZFS_EXIT(zfsvfs);
		return ((EINVAL));
	}

	error = 0;
	os = zfsvfs->z_os;
	offset = uio_offset(uio);
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
	bytes_wanted = uio_curriovlen(uio);
	bufsize = (size_t)bytes_wanted;
	outbuf = kmem_zalloc(bufsize, KM_SLEEP);   // ZERO?
	bufptr = (char *)outbuf;

	/*
	 * If this VFS supports the system attribute view interface; and
	 * we're looking at an extended attribute directory; and we care
	 * about normalization conflicts on this vfs; then we must check
	 * for normalization conflicts with the sysattr name space.
	 */
#ifdef TODO
	check_sysattrs = vfs_has_feature(vp->v_vfsp, VFSFT_SYSATTR_VIEWS) &&
	    (vp->v_flag & V_XATTRDIR) && zfsvfs->z_norm &&
	    (flags & V_RDDIR_ENTFLAGS);
#else
	check_sysattrs = 0;
#endif

	/*
	 * Transform to file-system independent format
	 */
	//zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;

	outcount = 0;
	while (outcount < bytes_wanted) {
		ino64_t objnum;
		ushort_t reclen, rawsize;
		uint64_t *next = NULL;
		size_t namelen;
		int force_formd_normalized_output;
		size_t  nfdlen;

		skip_this_entry = 0;


		/*
		 * Special case `.', `..', and `.zfs'.
		 */
		if (offset == 0) {
			(void)strlcpy(zap.za_name, ".", MAXNAMELEN);
			zap.za_normalization_conflict = 0;
			objnum = (zp->z_id == zfsvfs->z_root) ? 2 : zp->z_id;
			type = DT_DIR;
		} else if (offset == 1) {
			(void)strlcpy(zap.za_name, "..", MAXNAMELEN);
			zap.za_normalization_conflict = 0;
			objnum = (parent == zfsvfs->z_root) ? 2 : parent;
			objnum = (zp->z_id == zfsvfs->z_root) ? 1 : objnum;
			type = DT_DIR;
#if 1
		} else if (offset == 2 && zfs_show_ctldir(zp)) {
			(void)strlcpy(zap.za_name, ZFS_CTLDIR_NAME, MAXNAMELEN);
			zap.za_normalization_conflict = 0;
			objnum = ZFSCTL_INO_ROOT;
			type = DT_DIR;
#endif
		} else {

			/*
			 * Grab next entry.
			 */
			if ((error = zap_cursor_retrieve(&zc, &zap))) {
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
				error = SET_ERROR(ENXIO);
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap.za_first_integer);
			/*
			 * MacOS X can extract the object type here such as:
			 * uint8_t type = ZFS_DIRENT_TYPE(zap.za_first_integer);
			 */
			type = ZFS_DIRENT_TYPE(zap.za_first_integer);

			if (check_sysattrs && !zap.za_normalization_conflict) {
#ifdef TODO
				zap.za_normalization_conflict =
					xattr_sysattr_casechk(zap.za_name);
#else
				panic("%s:%u: TODO", __func__, __LINE__);
#endif
			}
		}

		/*
		 * Check if name will fit.
		 *
		 * Note: non-ascii names may expand (up to 3x) when converted to NFD
		 */
		namelen = strlen(zap.za_name);

		/* sysctl to force formD normalization of vnop output */
		if (zfs_vnop_force_formd_normalized_output &&
			!is_ascii_str(zap.za_name))
			force_formd_normalized_output = 1;
		else
			force_formd_normalized_output = 0;

		if (force_formd_normalized_output)
			namelen = MIN(MAXNAMLEN, namelen * 3);


		/*
		 * Do magic filename conversion for Windows here
		 */

		error = RtlUTF8ToUnicodeN(NULL, 0, &namelenholder, zap.za_name, namelen);

		// Did they provide a search pattern
		if (zccb->searchname.Buffer && zccb->searchname.Length) {
			UNICODE_STRING thisname;
			WCHAR tmpname[PATH_MAX];
			ULONG tmpnamelen;
			// We need to convert name to a tmp buffer here, as the output
			// buffer might not have enough room to hold the whole name, and
			// we need the whole name to do search match.
			error = RtlUTF8ToUnicodeN(tmpname, PATH_MAX, &tmpnamelen, zap.za_name, namelen);
			//dprintf("%s: '%.*S' -> '%s'\n", __func__,
			//	tmpnamelen / sizeof(WCHAR), tmpname, zap.za_name);


			thisname.Buffer = tmpname;
			thisname.Length = thisname.MaximumLength = tmpnamelen;
			// wildcard?
			if (zccb->ContainsWildCards) {
				if (!FsRtlIsNameInExpression(&zccb->searchname,
					&thisname,
					!(zfsvfs->z_case == ZFS_CASE_SENSITIVE), 
					NULL))
					skip_this_entry = 1;
			} else {
				if (!FsRtlAreNamesEqual(&thisname,
					&zccb->searchname,
					!(zfsvfs->z_case == ZFS_CASE_SENSITIVE),
					NULL))
					skip_this_entry = 1;
			}
#if 0
			dprintf("comparing names '%.*S' == '%.*S' skip %d\n",
				thisname.Length / sizeof(WCHAR), thisname.Buffer,
				zccb->searchname.Length / sizeof(WCHAR), zccb->searchname.Buffer,
				skip_this_entry);
#endif
			}


		if (!skip_this_entry) {
			// Windows combines vnop_readdir and vnop_getattr, so we need to lookup
			// a bunch of values, we try to do that as lightweight as possible.
			znode_t dummy = { 0 };  // For "." and ".."
			int get_zp = ENOENT;

			tzp = &dummy;

			// If "." use zp, if ".." use dzp, neither needs releasing. Otherwise, call zget.
			if (offset == 0 || offset == 1)
				tzp = zp;
			else
				get_zp = zfs_zget_ext(zfsvfs,
					offset == 1 ? parent : objnum, &tzp,  // objnum is adjusted above
#if 1
					ZGET_FLAG_UNLINKED);
#else
				ZGET_FLAG_UNLINKED | ZGET_FLAG_WITHOUT_VNODE );
#endif

			// If we failed to get the node (someone else might have deleted it), but we
			// need to return the name still, so it can be removed.
			if (get_zp != 0 && tzp == NULL)
				skip_this_entry = 1;

			// Is it worth warning about failing stat here?
			if (!skip_this_entry) {

				// We need to fill in more fields.
				sa_bulk_attr_t bulk[3];
				int count = 0;
				uint64_t mtime[2];
				uint64_t ctime[2];
				uint64_t crtime[2];
				SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
				SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
				SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
				sa_bulk_lookup(tzp->z_sa_hdl, bulk, count);
				// Is it worth warning about failed lookup here?		

				structsize = 0;

				switch (dirlisttype) {

				case FileFullDirectoryInformation:
					structsize = FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName[0]);
					if (outcount + structsize + namelenholder > bufsize) break;

					eodp = (FILE_FULL_DIR_INFORMATION *)bufptr;
					eodp->FileIndex = offset;
					eodp->AllocationSize.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : P2ROUNDUP(tzp->z_size, zfs_blksz(tzp)); // File size in block alignment
					eodp->EndOfFile.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : tzp->z_size; // File size in bytes
					TIME_UNIX_TO_WINDOWS(mtime, eodp->LastWriteTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(ctime, eodp->ChangeTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(crtime, eodp->CreationTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(tzp->z_atime, eodp->LastAccessTime.QuadPart);
					eodp->EaSize = tzp->z_pflags & ZFS_REPARSE ? 0xa0000003 : xattr_getsize(ZTOV(tzp)); // Magic code to change dir icon to link
					eodp->FileAttributes = zfs_getwinflags(tzp);
					nameptr = eodp->FileName;
					eodp->FileNameLength = namelenholder;

					break;

				case FileIdBothDirectoryInformation:
					structsize = FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName[0]);
					if (outcount + structsize + namelenholder > bufsize) break;

					eodp = (FILE_FULL_DIR_INFORMATION *)bufptr;
					FILE_ID_BOTH_DIR_INFORMATION *fibdi = (FILE_ID_BOTH_DIR_INFORMATION *)bufptr;
					fibdi->AllocationSize.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : P2ROUNDUP(tzp->z_size, zfs_blksz(tzp));
					fibdi->EndOfFile.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : tzp->z_size;
					TIME_UNIX_TO_WINDOWS(mtime, fibdi->LastWriteTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(ctime, fibdi->ChangeTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(crtime, fibdi->CreationTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(tzp->z_atime, fibdi->LastAccessTime.QuadPart);
					fibdi->EaSize = tzp->z_pflags & ZFS_REPARSE ? 0xa0000003 : xattr_getsize(ZTOV(tzp));
					fibdi->FileAttributes = zfs_getwinflags(tzp);
					fibdi->FileId.QuadPart = objnum;
					fibdi->FileIndex = offset;
					fibdi->ShortNameLength = 0;
					nameptr = fibdi->FileName;
					fibdi->FileNameLength = namelenholder;

					break;

				case FileBothDirectoryInformation:
					structsize = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName[0]);
					if (outcount + structsize + namelenholder > bufsize) break;

					eodp = (FILE_FULL_DIR_INFORMATION *)bufptr;
					FILE_BOTH_DIR_INFORMATION *fbdi = (FILE_BOTH_DIR_INFORMATION *)bufptr;
					fbdi->AllocationSize.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : P2ROUNDUP(tzp->z_size, zfs_blksz(tzp));
					fbdi->EndOfFile.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : tzp->z_size;
					TIME_UNIX_TO_WINDOWS(mtime, fbdi->LastWriteTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(ctime, fbdi->ChangeTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(crtime, fbdi->CreationTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(tzp->z_atime, fbdi->LastAccessTime.QuadPart);
					fbdi->EaSize = tzp->z_pflags & ZFS_REPARSE ? 0xa0000003 : xattr_getsize(ZTOV(tzp));
					fbdi->FileAttributes = zfs_getwinflags(tzp);
					fbdi->FileIndex = offset;
					fbdi->ShortNameLength = 0;
					nameptr = fbdi->FileName;
					fbdi->FileNameLength = namelenholder;

					break;

				case FileDirectoryInformation:
					structsize = FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName[0]);
					if (outcount + structsize + namelenholder > bufsize) break;
					eodp = (FILE_FULL_DIR_INFORMATION *)bufptr;
					//FILE_DIRECTORY_INFORMATION *fdi = (FILE_DIRECTORY_INFORMATION *)bufptr;
					fdi = (FILE_DIRECTORY_INFORMATION *)bufptr;
					fdi->AllocationSize.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : P2ROUNDUP(tzp->z_size, zfs_blksz(tzp));
					fdi->EndOfFile.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : tzp->z_size;
					TIME_UNIX_TO_WINDOWS(mtime, fdi->LastWriteTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(ctime, fdi->ChangeTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(crtime, fdi->CreationTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(tzp->z_atime, fdi->LastAccessTime.QuadPart);
					fdi->FileAttributes = zfs_getwinflags(tzp);
					//dtype == DT_DIR ? FILE_ATTRIBUTE_DIRECTORY :
					//	tzp->z_pflags&ZFS_REPARSEPOINT ? FILE_ATTRIBUTE_REPARSE_POINT : FILE_ATTRIBUTE_NORMAL;
					fdi->FileIndex = offset;
					nameptr = fdi->FileName;
					fdi->FileNameLength = namelenholder;

					break;

				case FileNamesInformation:
					structsize = FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName[0]);
					if (outcount + structsize + namelenholder > bufsize) break;
					eodp = (FILE_FULL_DIR_INFORMATION *)bufptr;
					FILE_NAMES_INFORMATION *fni = (FILE_NAMES_INFORMATION *)bufptr;
					fni = (FILE_NAMES_INFORMATION *)bufptr;
					fni->FileIndex = offset;
					nameptr = fni->FileName;
					fni->FileNameLength = namelenholder;
					break;

				case FileIdFullDirectoryInformation:
					structsize = FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName[0]);
					if (outcount + structsize + namelenholder > bufsize) break;

					eodp = (FILE_FULL_DIR_INFORMATION *)bufptr;
					FILE_ID_FULL_DIR_INFORMATION *fifdi = (FILE_ID_FULL_DIR_INFORMATION *)bufptr;
					fifdi->FileIndex = offset;
					fifdi->AllocationSize.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : P2ROUNDUP(tzp->z_size, zfs_blksz(tzp)); // File size in block alignment
					fifdi->EndOfFile.QuadPart = S_ISDIR(tzp->z_mode) ? 0 : tzp->z_size; // File size in bytes
					TIME_UNIX_TO_WINDOWS(mtime, fifdi->LastWriteTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(ctime, fifdi->ChangeTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(crtime, fifdi->CreationTime.QuadPart);
					TIME_UNIX_TO_WINDOWS(tzp->z_atime, fifdi->LastAccessTime.QuadPart);
					fifdi->EaSize = tzp->z_pflags & ZFS_REPARSE ? 0xa0000003 : xattr_getsize(ZTOV(tzp)); // Magic code to change dir icon to link
					fifdi->FileAttributes = zfs_getwinflags(tzp);
					fifdi->FileId.QuadPart = zp->z_id;
					nameptr = fifdi->FileName;
					fifdi->FileNameLength = namelenholder;
				}

				// Release the zp
#if 1
				if (get_zp == 0 && tzp != NULL) {
					VN_RELE(ZTOV(tzp));
				}
#else
				if (get_zp == 0) {
					if (ZTOV(tzp) == NULL) {
						zfs_zinactive(tzp);
					}
					else {
						VN_RELE(ZTOV(tzp));
					}
				}
#endif

				// If know we can't fit struct, just leave
				if (outcount + structsize + namelenholder > bufsize) break;

				rawsize = structsize + namelenholder;
				reclen = DIRENT_RECLEN(rawsize);

				/*
				 * Will this entry fit in the buffer? This time with alignment
				 */
				if (outcount + reclen > bufsize) {

					/*
					* Did we manage to fit anything in the buffer?
					*/
					if (!outcount) {
						error = (EINVAL);
						goto update;
					}
					break;
				}
				// If it is going to fit, compute alignment, in case
				// this dir entry is the last one, we don't align last one.
				last_alignment = reclen - rawsize;


				// Convert the filename over, or as much as we can fit
				ULONG namelenholder2 = 0;
				error = RtlUTF8ToUnicodeN(nameptr, namelenholder, &namelenholder2, zap.za_name, namelen);
				ASSERT(namelenholder == namelenholder2);
#if 0
				dprintf("%s: '%.*S' -> '%s' (namelen %d bytes: structsize %d)\n", __func__,
					namelenholder / sizeof(WCHAR), nameptr, zap.za_name, namelenholder, structsize);
#endif

				// If we aren't to skip, advance all pointers
				eodp->NextEntryOffset = reclen;

				outcount += reclen;
				bufptr += reclen;
				numdirent++;
			} // !skip_this_entry
		} // while

		ASSERT(outcount <= bufsize);

		/* Prefetch znode */
		if (prefetch)
			dmu_prefetch(os, objnum, 0, 0, 0, ZIO_PRIORITY_SYNC_READ);

		/*
		 * Move to the next entry, fill in the previous offset.
		 */
		if (offset > 2 || (offset == 2 && !zfs_show_ctldir(zp))) {
			zap_cursor_advance(&zc);
			offset = zap_cursor_serialize(&zc);
		} else {
			offset += 1;
		}

		if (!skip_this_entry && flag_return_single_entry) break;
	}

	// The last eodp should have Next offset of 0
	// This assumes NextEntryOffset is the FIRST entry in all structs
	if (eodp) eodp->NextEntryOffset = 0;

	// The outcout += reclen; above unfortunately adds the possibly
	// aligned (to 8 bytes) length. But the last entry should not
	// be rounded-up.
	if ((outcount > last_alignment) &&
		(last_alignment > 0)) {
		outcount -= last_alignment;
	}

	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

	if ((error = uiomove(outbuf, (long)outcount, UIO_READ, uio))) {
		/*
		 * Reset the pointer.
		 */
		offset = uio_offset(uio);
	}

update:
	zap_cursor_fini(&zc);
	if (outbuf) {
		kmem_free(outbuf, bufsize);
	}

	if (error == ENOENT)
		error = 0;

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	uio_setoffset(uio, offset);
	if (a_numdirent)
	*a_numdirent = numdirent;
	ZFS_EXIT(zfsvfs);

	dprintf("-zfs_readdir: num %d\n", numdirent);

	return (error);
}

ulong_t zfs_fsync_sync_cnt = 4;

int
zfs_fsync(znode_t *zp, int syncflag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	vnode_t *vp = ZTOV(zp);

	if (zp->z_is_mapped /* && !(syncflag & FNODSYNC) */ &&
	    vnode_isreg(vp) && !vnode_isswap(vp)) {
		// cluster_push(vp, /* waitdata ? IO_SYNC : */ 0);
	}

	(void) tsd_set(zfs_fsyncer_key, (void *)zfs_fsync_sync_cnt);

	if (zfsvfs->z_os->os_sync != ZFS_SYNC_DISABLED &&
	    !vnode_isrecycled(ZTOV(zp))) {
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		zil_commit(zfsvfs->z_log, zp->z_id);
		ZFS_EXIT(zfsvfs);
	}
	tsd_set(zfs_fsyncer_key, NULL);

	return (0);
}

/*
 * Get the requested file attributes and place them in the provided
 * vattr structure.
 *
 *      IN:     vp      - vnode of file.
 *              vap     - va_mask identifies requested attributes.
 *                        If ATTR_XVATTR set, then optional attrs are requested
 *              flags   - ATTR_NOACLCHECK (CIFS server context)
 *              cr      - credentials of caller.
 *              ct      - caller context
 *
 *      OUT:    vap     - attribute values.
 *
 *      RETURN: 0 (always succeeds)
 */
int
zfs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error = 0;
	uint64_t links;
	uint64_t mtime[2], ctime[2], crtime[2], rdev;
	xvattr_t *xvap = (xvattr_t *)vap; /* vap may be an xvattr_t * */
	xoptattr_t *xoap = NULL;
	boolean_t skipaclchk = /* (flags&ATTR_NOACLCHECK)?B_TRUE: */ B_FALSE;
	sa_bulk_attr_t bulk[4];
	int count = 0;

	VERIFY3P(zp->z_zfsvfs, ==, vfs_fsprivate(vnode_mount(vp)));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	zfs_fuid_map_ids(zp, cr, &vap->va_uid, &vap->va_gid);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
	if (vnode_isblk(vp) || vnode_ischr(vp))
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_RDEV(zfsvfs), NULL,
		    &rdev, 8);

	if ((error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
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
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	/*
	 * Return all attributes.  It's cheaper to provide the answer
	 * than to determine whether we were asked the question.
	 */

	mutex_enter(&zp->z_lock);
	vap->va_type = IFTOVT(zp->z_mode);
	vap->va_mode = zp->z_mode & ~S_IFMT;
	vap->va_nodeid = zp->z_id;
	if (vnode_isvroot((vp)) && zfs_show_ctldir(zp))
		links = zp->z_links + 1;
	else
		links = zp->z_links;
	vap->va_nlink = MIN(links, LINK_MAX);   /* nlink_t limit! */
	vap->va_size = zp->z_size;
	if (vnode_isblk(vp) || vnode_ischr(vp))
		vap->va_rdev = zfs_cmpldev(rdev);

	vap->va_flags = 0; /* FreeBSD: Reset chflags(2) flags. */

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
		    vnode_isreg(vp)) {
			zfs_sa_get_scanstamp(zp, xvap);
		}
		if (XVA_ISSET_REQ(xvap, XAT_CREATETIME)) {
			uint64_t times[2];

			(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_CRTIME(zfsvfs),
			    times, sizeof (times));
			ZFS_TIME_DECODE(&xoap->xoa_createtime, times);
			XVA_SET_RTN(xvap, XAT_CREATETIME);
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
	}

	ZFS_TIME_DECODE(&vap->va_atime, zp->z_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, ctime);
	ZFS_TIME_DECODE(&vap->va_crtime, crtime);

	mutex_exit(&zp->z_lock);

	ZFS_EXIT(zfsvfs);
	return (0);
}

void kx_qsort(void* array, size_t nm, size_t member_size, int (*)(const void *, const void *));

#ifdef NOTSUREYET
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
	struct vnode	*dxip = ZTOI(dzp);
	struct vnode	*xip = NULL;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	objset_t	*os = zfsvfs->z_os;
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	zfs_dirlock_t	*dl;
	znode_t		*zp;
	dmu_tx_t	*tx = NULL;
	uint64_t	uid, gid;
	sa_bulk_attr_t	bulk[4];
	int		count;
	int		err;

	zap_cursor_init(&zc, os, dzp->z_id);
	while ((err = zap_cursor_retrieve(&zc, &zap)) == 0) {
		count = 0;
		if (zap.za_integer_length != 8 || zap.za_num_integers != 1) {
			err = ENXIO;
			break;
		}

		err = zfs_dirent_lock(&dl, dzp, (char *)zap.za_name, &zp,
		    ZEXISTS, NULL, NULL);
		if (err == ENOENT)
			goto next;
		if (err)
			break;

		xip = ZTOI(zp);
		if (zp->z_uid == dzp->z_uid &&
		    zp->z_gid == dzp->z_gid &&
		    zp->z_projid == dzp->z_projid)
			goto next;

		tx = dmu_tx_create(os);
		if (!(zp->z_pflags & ZFS_PROJID))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);

		err = dmu_tx_assign(tx, TXG_WAIT);
		if (err)
			break;

		mutex_enter(&dzp->z_lock);

		if (zp->z_uid != dxzp->z_uid) {
			zp->z_uid = dzp->z_uid;
			uid = zfs_uid_read(dzp);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &uid, sizeof (uid));
		}

		if (KGID_TO_SGID(zp->z_gid) != KGID_TO_SGID(dxzp->z_gid)) {
			zp->z_gid = dzp->z_gid;
			gid = zfs_gid_read(dzp);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
			    &gid, sizeof (gid));
		}

		if (zp->z_projid != dzp->z_projid) {
			if (!(zp->z_pflags & ZFS_PROJID)) {
				zp->z_pflags |= ZFS_PROJID;
				SA_ADD_BULK_ATTR(bulk, count,
				    SA_ZPL_FLAGS(zfsvfs), NULL, &zp->z_pflags,
				    sizeof (zp->z_pflags));
			}

			zp->z_projid = dzp->z_projid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PROJID(zfsvfs),
			    NULL, &zp->z_projid, sizeof (zp->z_projid));
		}

		mutex_exit(&dzp->z_lock);

		if (likely(count > 0)) {
			err = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
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
			zfs_dirent_unlock(dl);
		}
		zap_cursor_advance(&zc);
	}

	if (tx)
		dmu_tx_abort(tx);
	if (zp) {
		zrele(zp);
		zfs_dirent_unlock(dl);
	}
	zap_cursor_fini(&zc);

	return (err == ENOENT ? 0 : err);
}
#endif

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	zp	- znode of file to be modified.
 *		vap	- new attribute values.
 *			  If ATTR_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime updated, mtime updated if size changed.
 */
/* ARGSUSED */
int
zfs_setattr(znode_t *zp, vattr_t *vap, int flags, cred_t *cr)
{
	struct vnode	*vp;
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	objset_t	*os = zfsvfs->z_os;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		oldva;
	xvattr_t	*tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask = 0;
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_kuid = 0, new_kgid = 0, new_uid, new_gid;
	uint64_t	xattr_obj;
	uint64_t	mtime[2], ctime[2], atime[2], crtime[2];
	uint64_t	projid = ZFS_INVALID_PROJID;
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2 = 0;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t	fuid_dirtied = B_FALSE;
	boolean_t	handle_eadir = B_FALSE;
	sa_bulk_attr_t	*bulk, *xattr_bulk;
	int		count = 0, xattr_count = 0, bulks = 9;

	if (mask == 0)
		return (0);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	vp = ZTOV(zp);

	/*
	 * If this is a xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);
	if (xoap != NULL && (mask & ATTR_XVATTR)) {
		if (XVA_ISSET_REQ(xvap, XAT_PROJID)) {
			if (!dmu_objset_projectquota_enabled(os) ||
			    (!S_ISREG(zp->z_mode) && !S_ISDIR(zp->z_mode))) {
				ZFS_EXIT(zfsvfs);
				return (SET_ERROR(ENOTSUP));
			}

			projid = xoap->xoa_projid;
			if (unlikely(projid == ZFS_INVALID_PROJID)) {
				ZFS_EXIT(zfsvfs);
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
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(ENOTSUP));
		}
	}

	zilog = zfsvfs->z_log;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (((mask & ATTR_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & ATTR_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & ATTR_XVATTR))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	if (mask & ATTR_SIZE && S_ISDIR(zp->z_mode)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EISDIR));
	}

	if (mask & ATTR_SIZE && !S_ISREG(zp->z_mode) && !S_ISFIFO(zp->z_mode)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	tmpxvattr = kmem_alloc(sizeof (xvattr_t), KM_SLEEP);
	xva_init(tmpxvattr);

	bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * bulks, KM_SLEEP);
	xattr_bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * bulks, KM_SLEEP);

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) &&
	    ((mask & (ATTR_SIZE|ATTR_UID|ATTR_GID|ATTR_MTIME|ATTR_MODE)) ||
	    ((mask & ATTR_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		err = SET_ERROR(EPERM);
		goto out3;
	}

	if ((mask & ATTR_SIZE) && (zp->z_pflags & ZFS_READONLY)) {
		err = SET_ERROR(EPERM);
		goto out3;
	}

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (ATTR_ATIME | ATTR_MTIME)) {
		if (((mask & ATTR_ATIME) &&
		    TIMESPEC_OVERFLOW(&vap->va_atime)) ||
		    ((mask & ATTR_MTIME) &&
		    TIMESPEC_OVERFLOW(&vap->va_mtime))) {
			err = SET_ERROR(EOVERFLOW);
			goto out3;
		}
	}

top:
	attrzp = NULL;
	aclp = NULL;

	/* Can this be moved to before the top label? */
	if (zfs_is_readonly(zfsvfs)) {
		err = SET_ERROR(EROFS);
		goto out3;
	}

	/*
	 * First validate permissions
	 */

	if (mask & ATTR_SIZE) {
		err = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr);
		if (err)
			goto out3;

		/*
		 * XXX - Note, we are not providing any open
		 * mode flags here (like FNDELAY), so we may
		 * block if there are locks present... this
		 * should be addressed in openat().
		 */
		/* XXX - would it be OK to generate a log record here? */
		err = zfs_freesp(zp, vap->va_size, 0, 0, FALSE);
		if (err)
			goto out3;
	}

	if (mask & (ATTR_ATIME|ATTR_MTIME) ||
	    ((mask & ATTR_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_OFFLINE) ||
	    XVA_ISSET_REQ(xvap, XAT_SPARSE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM)))) {
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    B_FALSE, cr);
	}

	if (mask & (ATTR_UID|ATTR_GID)) {
		int	idmask = (mask & (ATTR_UID|ATTR_GID));
		int	take_owner;
		int	take_group;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & ATTR_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		take_owner = (mask & ATTR_UID) && (vap->va_uid == crgetuid(cr));
		take_group = (mask & ATTR_GID) &&
		    zfs_groupmember(zfsvfs, vap->va_gid, cr);

		/*
		 * If both ATTR_UID and ATTR_GID are set then take_owner and
		 * take_group must both be set in order to allow taking
		 * ownership.
		 *
		 * Otherwise, send the check through secpolicy_vnode_setattr()
		 *
		 */

		if (((idmask == (ATTR_UID|ATTR_GID)) &&
		    take_owner && take_group) ||
		    ((idmask == ATTR_UID) && take_owner) ||
		    ((idmask == ATTR_GID) && take_group)) {
			if (zfs_zaccess(zp, ACE_WRITE_OWNER, 0,
			    B_FALSE, cr) == 0) {
				/*
				 * Remove setuid/setgid for non-privileged users
				 */
				(void) secpolicy_setid_clear(vap, cr);
				trim_mask = (mask & (ATTR_UID|ATTR_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	mutex_enter(&zp->z_lock);
	oldva.va_mode = zp->z_mode;
	zfs_fuid_map_ids(zp, cr, &oldva.va_uid, &oldva.va_gid);
	if (mask & ATTR_XVATTR) {
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
			if ((!S_ISREG(zp->z_mode) &&
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
			mutex_exit(&zp->z_lock);
			err = SET_ERROR(EPERM);
			goto out3;
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}

	mutex_exit(&zp->z_lock);

	if (mask & ATTR_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, B_FALSE, cr) == 0) {
			err = secpolicy_setid_setsticky_clear(vp, vap,
			    &oldva, cr);
			if (err)
				goto out3;

			trim_mask |= ATTR_MODE;
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
		if (err)
			goto out3;

		if (trim_mask)
			vap->va_mask |= saved_mask;
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	if ((mask & (ATTR_UID | ATTR_GID)) || projid != ZFS_INVALID_PROJID) {
		handle_eadir = B_TRUE;
		err = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj, sizeof (xattr_obj));

		if (err == 0 && xattr_obj) {
			err = zfs_zget(ZTOZSB(zp), xattr_obj, &attrzp);
			if (err)
				goto out2;
		}
		if (mask & ATTR_UID) {
			new_kuid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_kuid != zp->z_uid &&
			    zfs_id_overquota(zfsvfs, DMU_USERUSED_OBJECT,
			    new_kuid)) {
				if (attrzp)
					zrele(attrzp);
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (mask & ATTR_GID) {
			new_kgid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_gid, cr, ZFS_GROUP, &fuidp);
			if (new_kgid != zp->z_gid &&
			    zfs_id_overquota(zfsvfs, DMU_GROUPUSED_OBJECT,
			    new_kgid)) {
				if (attrzp)
					zrele(attrzp);
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (projid != ZFS_INVALID_PROJID &&
		    zfs_id_overquota(zfsvfs, DMU_PROJECTUSED_OBJECT, projid)) {
			if (attrzp)
				zrele(attrzp);
			err = EDQUOT;
			goto out2;
		}
	}
	tx = dmu_tx_create(os);

	if (mask & ATTR_MODE) {
		uint64_t pmode = zp->z_mode;
		uint64_t acl_obj;
		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		if ((err = zfs_acl_chmod_setattr(zp, &aclp, new_mode)))
			goto out;

		mutex_enter(&zp->z_lock);
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
		mutex_exit(&zp->z_lock);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	} else {
		if (((mask & ATTR_XVATTR) &&
		    XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) ||
		    (projid != ZFS_INVALID_PROJID &&
		    !(zp->z_pflags & ZFS_PROJID)))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	}

	if (attrzp) {
		dmu_tx_hold_sa(tx, attrzp->z_sa_hdl, B_FALSE);
	}

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);

	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, TXG_WAIT);
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

	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_enter(&zp->z_acl_lock);
	mutex_enter(&zp->z_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_enter(&attrzp->z_acl_lock);
		mutex_enter(&attrzp->z_lock);
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

	if (mask & (ATTR_UID|ATTR_GID)) {

		if (mask & ATTR_UID) {
			new_uid = new_kuid;
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

		if (mask & ATTR_GID) {
			new_gid = new_kgid;
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
		if (!(mask & ATTR_MODE)) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs),
			    NULL, &new_mode, sizeof (new_mode));
			new_mode = zp->z_mode;
		}
		err = zfs_acl_chown_setattr(zp);
		ASSERT(err == 0);
		if (attrzp) {
			err = zfs_acl_chown_setattr(attrzp);
			ASSERT(err == 0);
		}
	}

	if (mask & ATTR_MODE) {
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

	if ((mask & ATTR_ATIME) || zp->z_atime_dirty) {
		zp->z_atime_dirty = B_FALSE;
		ZFS_TIME_ENCODE(&vap->va_atime, zp->z_atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
		    &atime, sizeof (atime));
	}

	if (mask & (ATTR_MTIME | ATTR_SIZE)) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    mtime, sizeof (mtime));
	}

	if (mask & (ATTR_CTIME | ATTR_SIZE)) {
		ZFS_TIME_ENCODE(&vap->va_ctime, ctime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    ctime, sizeof (ctime));
	}

	if (mask & ATTR_CRTIME) {
		ZFS_TIME_ENCODE(&vap->va_crtime, crtime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL,
		    crtime, sizeof (crtime));
	}

	if (projid != ZFS_INVALID_PROJID) {
		zp->z_projid = projid;
		SA_ADD_BULK_ATTR(bulk, count,
		    SA_ZPL_PROJID(zfsvfs), NULL, &zp->z_projid,
		    sizeof (zp->z_projid));
	}

	if (attrzp && mask) {
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_CTIME(zfsvfs), NULL, &ctime,
		    sizeof (ctime));
	}

	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */

	if (xoap && (mask & ATTR_XVATTR)) {

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
			ASSERT(S_ISREG(zp->z_mode));

		zfs_xvattr_set(zp, xvap, tx);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0)
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);

	mutex_exit(&zp->z_lock);
	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_exit(&zp->z_acl_lock);

	if (attrzp) {
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_exit(&attrzp->z_acl_lock);
		mutex_exit(&attrzp->z_lock);
	}
out:
	if (err == 0 && xattr_count > 0) {
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		ASSERT(err2 == 0);
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
			zrele(attrzp);
		if (err == ERESTART)
			goto top;
	} else {
		if (count > 0)
			err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
		if (attrzp) {
			zrele(attrzp);
		}
	}

out2:
	if (os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

out3:
	kmem_free(xattr_bulk, sizeof (sa_bulk_attr_t) * bulks);
	kmem_free(bulk, sizeof (sa_bulk_attr_t) * bulks);
	kmem_free(tmpxvattr, sizeof (xvattr_t));
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
			zfs_zrele_async(zl->zl_znode);
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
	uint64_t	rootid = ZTOZSB(zp)->z_root;
	uint64_t	oidp = zp->z_id;
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
				oidp = zp->z_id;
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

		if (oidp == szp->z_id)		/* We're a descendant of szp */
			return (SET_ERROR(EINVAL));

		if (oidp == rootid)		/* We've hit the top */
			return (0);

		if (rw == RW_READER) {		/* i.e. not the first pass */
			int error = zfs_zget(ZTOZSB(zp), oidp, &zp);
			if (error)
				return (error);
			zl->zl_znode = zp;
		}
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(ZTOZSB(zp)),
		    &oidp, sizeof (oidp));
		rwlp = &zp->z_parent_lock;
		rw = RW_READER;

	} while (zp->z_id != sdzp->z_id);

	return (0);
}

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdzp	- Source directory containing the "old entry".
 *		snm	- Old entry name.
 *		tdzp	- Target directory to contain the "new entry".
 *		tnm	- New entry name.
 *		cr	- credentials of caller.
 *		flags	- case flags
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	sdzp,tdzp - ctime|mtime updated
 */
/*ARGSUSED*/
int
zfs_rename(znode_t *sdzp, char *snm, znode_t *tdzp, char *tnm,
    cred_t *cr, int flags)
{
	znode_t		*szp, *tzp;
	zfsvfs_t	*zfsvfs = ZTOZSB(sdzp);
	zilog_t		*zilog;
	uint64_t addtime[2];
	zfs_dirlock_t	*sdl, *tdl;
	dmu_tx_t	*tx;
	zfs_zlock_t	*zl;
	int		cmp, serr, terr;
	int		error = 0;
	int		zflg = 0;
	boolean_t	waited = B_FALSE;

	if (snm == NULL || tnm == NULL)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(sdzp);
	zilog = zfsvfs->z_log;

	ZFS_VERIFY_ZP(tdzp);

	/*
	 * We check i_sb because snapshots and the ctldir must have different
	 * super blocks.
	 */
	// Can't we use zp->z_zfsvfs in place of zp->vp->v_vfs ?
	if (VTOM(ZTOV(tdzp)) != VTOM(ZTOV(sdzp)) ||
	    zfsctl_is_node(ZTOV(tdzp))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EXDEV));
	}

	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
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
	if ((tdzp->z_pflags & ZFS_XATTR) != (sdzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
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

	/*
	 * If the source and destination directories are the same, we should
	 * grab the z_name_lock of that directory only once.
	 */
	if (sdzp == tdzp) {
		zflg |= ZHAVELOCK;
		rw_enter(&sdzp->z_name_lock, RW_READER);
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
				zrele(tzp);
		}

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(snm, ".") == 0 || strcmp(snm, "..") == 0)
			serr = EINVAL;
		ZFS_EXIT(zfsvfs);
		return (serr);
	}
	if (terr) {
		zfs_dirent_unlock(sdl);
		zrele(szp);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(tnm, "..") == 0)
			terr = EINVAL;
		ZFS_EXIT(zfsvfs);
		return (terr);
	}

	/*
	 * If we are using project inheritance, means if the directory has
	 * ZFS_PROJINHERIT set, then its descendant directories will inherit
	 * not only the project ID, but also the ZFS_PROJINHERIT flag. Under
	 * such case, we only allow renames into our tree when the project
	 * IDs are the same.
	 */
	if (tdzp->z_pflags & ZFS_PROJINHERIT &&
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

	if (S_ISDIR(szp->z_mode)) {
		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if ((error = zfs_rename_lock(szp, tdzp, sdzp, &zl)))
			goto out;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		/*
		 * Source and target must be the same type.
		 */
		if (S_ISDIR(szp->z_mode)) {
			if (!S_ISDIR(tzp->z_mode)) {
				error = SET_ERROR(ENOTDIR);
				goto out;
			}
		} else {
			if (S_ISDIR(tzp->z_mode)) {
				error = SET_ERROR(EISDIR);
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

#if defined(MAC_OS_X_VERSION_10_12) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
		/* If renamex(VFS_RENAME_EXCL) is used, error out */
		if (flags & VFS_RENAME_EXCL) {
			error = EEXIST;
			goto out;
		}
#endif

	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, sdzp->z_id, FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp) {
		dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tdzp);
	}
	if (tzp) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tzp);
	}

	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
	if (error) {
		if (zl != NULL)
			zfs_rename_unlock(&zl);
		zfs_dirent_unlock(sdl);
		zfs_dirent_unlock(tdl);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			zrele(szp);
			if (tzp)
				zrele(tzp);
			goto top;
		}
		dmu_tx_abort(tx);
		zrele(szp);
		if (tzp)
			zrele(tzp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (tzp)	/* Attempt to remove the existing target */
		error = zfs_link_destroy(tdl, tzp, tx, zflg, NULL);

	if (error == 0) {
		error = zfs_link_create(tdl, szp, tx, ZRENAMING);
		if (error == 0) {
			szp->z_pflags |= ZFS_AV_MODIFIED;
			if (tdzp->z_pflags & ZFS_PROJINHERIT)
				szp->z_pflags |= ZFS_PROJINHERIT;

			error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
			    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);

			error = zfs_link_destroy(sdl, szp, tx, ZRENAMING, NULL);
			if (error == 0) {
				zfs_log_rename(zilog, tx, TX_RENAME |
				    (flags & FIGNORECASE ? TX_CI : 0), sdzp,
				    sdl->dl_name, tdzp, tdl->dl_name, szp);

				/*
				 * Update cached name - for vget, and access
				 * without calling vnop_lookup first - it is
				 * easier to clear it out and let getattr
				 * look it up if needed.
				 */
				if (tzp) {
					mutex_enter(&tzp->z_lock);
					tzp->z_name_cache[0] = 0;
					mutex_exit(&tzp->z_lock);
				}
				if (szp) {
					mutex_enter(&szp->z_lock);
					szp->z_name_cache[0] = 0;
					mutex_exit(&szp->z_lock);
				}

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
				VERIFY3U(zfs_link_destroy(tdl, szp, tx,
				    ZRENAMING, NULL), ==, 0);
			}
		} else {
			/*
			 * If we had removed the existing target, subsequent
			 * call to zfs_link_create() to add back the same entry
			 * but, the new dnode (szp) should not fail.
			 */
			ASSERT(tzp == NULL);
		}
	}

	dmu_tx_commit(tx);
out:
	if (zl != NULL)
		zfs_rename_unlock(&zl);

	zfs_dirent_unlock(sdl);
	zfs_dirent_unlock(tdl);

	if (sdzp == tdzp)
		rw_exit(&sdzp->z_name_lock);

	zrele(szp);
	if (tzp) {
		zrele(tzp);
	}

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Insert the indicated symbolic reference entry into the directory.
 *
 *	IN:	dzp	- Directory to contain new symbolic link.
 *		name	- Name of directory entry in dip.
 *		vap	- Attributes of new entry.
 *		link	- Name for new symlink entry.
 *		cr	- credentials of caller.
 *		flags	- case flags
 *
 *	OUT:	zpp	- Znode for new symbolic link.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dip - ctime|mtime updated
 */
/*ARGSUSED*/
int
zfs_symlink(znode_t *dzp, char *name, vattr_t *vap, char *link,
    znode_t **zpp, cred_t *cr, int flags)
{
	znode_t		*zp;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	uint64_t	len = strlen(link);
	int		error;
	int		zflg = ZNEW;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;
	uint64_t	txtype = TX_SYMLINK;
	boolean_t	waited = B_FALSE;

	ASSERT(S_ISLNK(vap->va_mode));

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

	if (len > MAXPATHLEN) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
top:
	*zpp = NULL;

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg, NULL, NULL);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, ZFS_DEFAULT_PROJID)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EDQUOT));
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datsets the symlink will be an SA attribute
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    link, len, tx);
	else
		zfs_sa_symlink(zp, link, len, tx);
	mutex_exit(&zp->z_lock);

	zp->z_size = len;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);
	/*
	 * Insert the new object into the directory.
	 */
	error = zfs_link_create(dl, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
	} else {
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);
	}

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	/*
	 * OS X - attach the vnode _after_ committing the transaction
	 */
	zfs_znode_getvnode(zp, dzp, zfsvfs);

	if (error == 0) {
		*zpp = zp;

		if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
			zil_commit(zilog, 0);
	} else {
		zrele(zp);
	}

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Return, in the buffer contained in the provided uio structure,
 * the symbolic path referred to by ip.
 *
 *	IN:	ip	- inode of symbolic link
 *		uio	- structure to contain the link path.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - atime updated
 */
/* ARGSUSED */
int
zfs_readlink(struct vnode *vp, uio_t *uio, cred_t *cr)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = ITOZSB(vp);
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), uio);
	else
		error = zfs_sa_readlink(zp, uio);
	mutex_exit(&zp->z_lock);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Insert a new entry into directory tdzp referencing szp.
 *
 *	IN:	tdzp	- Directory to contain new entry.
 *		szp	- znode of new entry.
 *		name	- name of new entry.
 *		cr	- credentials of caller.
 *		flags	- case flags.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	tdzp - ctime|mtime updated
 *	 szp - ctime updated
 */
/* ARGSUSED */
int
zfs_link(znode_t *tdzp, znode_t *szp, char *name, cred_t *cr,
    int flags)
{
	struct vnode *svp = ZTOV(szp);
	znode_t		*tzp;
	zfsvfs_t	*zfsvfs = ZTOZSB(tdzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	uint64_t	parent;
	uid_t		owner;
	boolean_t	waited = B_FALSE;
	boolean_t	is_tmpfile = 0;
	uint64_t	txg;

	ASSERT(S_ISDIR(tdzp->z_mode));

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(tdzp);
	zilog = zfsvfs->z_log;

#ifdef __APPLE__
	if (VTOM(svp) != VTOM(ZTOV(tdzp))) {
		ZFS_EXIT(zfsvfs);
		return (EXDEV);
	}
#endif

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (vnode_isdir(svp)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	ZFS_VERIFY_ZP(szp);

	/*
	 * If we are using project inheritance, means if the directory has
	 * ZFS_PROJINHERIT set, then its descendant directories will inherit
	 * not only the project ID, but also the ZFS_PROJINHERIT flag. Under
	 * such case, we only allow hard link creation in our tree when the
	 * project IDs are the same.
	 */
	if (tdzp->z_pflags & ZFS_PROJINHERIT &&
	    tdzp->z_projid != szp->z_projid) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EXDEV));
	}

	/* Prevent links to .zfs/shares files */

	if ((error = sa_lookup(szp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	if (parent == zfsvfs->z_shares_dir) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	if (zfsvfs->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_pflags & ZFS_XATTR) != (tdzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	owner = zfs_fuid_map_id(zfsvfs, KUID_TO_SUID(szp->z_uid),
	    cr, ZFS_OWNER);
	if (owner != crgetuid(cr) && secpolicy_basic_link(cr) != 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_zaccess(tdzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

top:
	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, tdzp, name, &tzp, zf, NULL, NULL);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, name);
	if (is_tmpfile)
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, tdzp);
	error = dmu_tx_assign(tx, (waited ? TXG_NOTHROTTLE : 0) | TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
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
		zfs_log_link(zilog, tx, txtype, tdzp, szp, name);
	} else if (is_tmpfile) {
		/* restore z_unlinked since when linking failed */
		szp->z_unlinked = B_TRUE;
	}
	txg = dmu_tx_get_txg(tx);
	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*ARGSUSED*/
void
zfs_inactive(struct vnode *vp)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ITOZSB(vp);
	int error;

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_sa_hdl == NULL) {
		/*
		 * The fs has been unmounted, or we did a
		 * suspend/resume and this file no longer exists.
		 */
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		vnode_recycle(vp);
		return;
	}

	if (zp->z_unlinked) {
		/*
		 * Fast path to recycle a vnode of a removed file.
		 */
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		vnode_recycle(vp);
		return;
	}

	if (zp->z_atime_dirty && zp->z_unlinked == 0) {
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);

		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_ATIME(zfsvfs),
			    (void *)&zp->z_atime, sizeof (zp->z_atime), tx);
			zp->z_atime_dirty = 0;
			dmu_tx_commit(tx);
		}
	}
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
}

static int
zfs_getsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	// boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t skipaclchk = B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	error = zfs_getacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zfsvfs);

	return (error);
}

int
zfs_setsecattr(znode_t *zp, vsecattr_t *vsecp, int flag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	// boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t skipaclchk = B_FALSE;
	zilog_t *zilog = zfsvfs->z_log;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	error = zfs_setacl(zp, vsecp, skipaclchk, cr);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}


/*
 * Free or allocate space in a file.  Currently, this function only
 * supports the `F_FREESP' command.  However, this command is somewhat
 * misnamed, as its functionality includes the ability to allocate as
 * well as free space.
 *
 *	IN:	zp	- znode of file to free data in.
 *		cmd	- action to take (only F_FREESP supported).
 *		bfp	- section of file to free/alloc.
 *		flag	- current file open mode flags.
 *		offset	- current file offset.
 *		cr	- credentials of caller.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	zp - ctime|mtime updated
 */
/* ARGSUSED */
int
zfs_space(znode_t *zp, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr)
{
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	uint64_t	off, len;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (cmd != F_FREESP) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Callers might not be able to detect properly that we are read-only,
	 * so check it explicitly here.
	 */
	if (zfs_is_readonly(zfsvfs)) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EROFS));
	}

	if (bfp->l_len < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Permissions aren't checked on Solaris because on this OS
	 * zfs_space() can only be called with an opened file handle.
	 * On Linux we can get here through truncate_range() which
	 * operates directly on inodes, so we need to check access rights.
	 */
	if ((error = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr))) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	off = bfp->l_start;
	len = bfp->l_len; /* 0 means from off to end of file */

	error = zfs_freesp(zp, off, len, flag, TRUE);

	ZFS_EXIT(zfsvfs);
	return (error);
}
