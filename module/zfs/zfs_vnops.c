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
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
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
#include <sys/policy.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_quota.h>


static ulong_t zfs_fsync_sync_cnt = 4;

int
zfs_fsync(znode_t *zp, int syncflag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);

	(void) tsd_set(zfs_fsyncer_key, (void *)zfs_fsync_sync_cnt);

	if (zfsvfs->z_os->os_sync != ZFS_SYNC_DISABLED) {
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		zil_commit(zfsvfs->z_log, zp->z_id);
		ZFS_EXIT(zfsvfs);
	}
	tsd_set(zfs_fsyncer_key, NULL);

	return (0);
}

static unsigned long zfs_vnops_read_chunk_size = 1024 * 1024; /* Tunable */

/*
 * Read bytes from specified file into supplied buffer.
 *
 *	IN:	zp	- inode of file to be read from.
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
zfs_read(struct znode *zp, uio_t *uio, int ioflag, cred_t *cr)
{
	int error = 0;
	boolean_t frsync = B_FALSE;

	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EACCES));
	}

	/* We don't copy out anything useful for directories. */
	if (Z_ISDIR(ZTOTYPE(zp))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EISDIR));
	}

	/*
	 * Validate file offset
	 */
	if (uio->uio_loffset < (offset_t)0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio->uio_resid == 0) {
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
	    uio->uio_loffset, uio->uio_resid, RL_READER);

	/*
	 * If we are reading past end-of-file we can skip
	 * to the end; but we might still need to set atime.
	 */
	if (uio->uio_loffset >= zp->z_size) {
		error = 0;
		goto out;
	}

	ASSERT(uio->uio_loffset < zp->z_size);
	ssize_t n = MIN(uio->uio_resid, zp->z_size - uio->uio_loffset);
	ssize_t start_resid = n;

	while (n > 0) {
		ssize_t nbytes = MIN(n, zfs_vnops_read_chunk_size -
		    P2PHASE(uio->uio_loffset, zfs_vnops_read_chunk_size));
#ifdef UIO_NOCOPY
		if (uio->uio_segflg == UIO_NOCOPY)
			error = mappedread_sf(zp, nbytes, uio);
		else
#endif
		if (zn_has_cached_data(zp) && !(ioflag & O_DIRECT)) {
			error = mappedread(zp, nbytes, uio);
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

	int64_t nread = start_resid - n;
	dataset_kstats_update_read_kstats(&zfsvfs->z_kstat, nread);
	task_io_account_read(nread);
out:
	zfs_rangelock_exit(lr);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Write the bytes to a file.
 *
 *	IN:	zp	- znode of file to be written to.
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
zfs_write(znode_t *zp, uio_t *uio, int ioflag, cred_t *cr)
{
	int error = 0;
	ssize_t start_resid = uio->uio_resid;

	/*
	 * Fasttrack empty write
	 */
	ssize_t n = start_resid;
	if (n == 0)
		return (0);

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
	    (uio->uio_loffset < zp->z_size))) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EPERM));
	}

	/*
	 * Validate file offset
	 */
	offset_t woff = ioflag & O_APPEND ? zp->z_size : uio->uio_loffset;
	if (woff < 0) {
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EINVAL));
	}

	const uint64_t max_blksz = zfsvfs->z_max_blksz;

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
		uio->uio_loffset = woff;
	} else {
		/*
		 * Note that if the file block size will change as a result of
		 * this write, then this range lock will lock the entire file
		 * so that we can re-write the block safely.
		 */
		lr = zfs_rangelock_enter(&zp->z_rangelock, woff, n, RL_WRITER);
	}

	if (zn_rlimit_fsize(zp, uio, uio->uio_td)) {
		zfs_rangelock_exit(lr);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EFBIG));
	}

	const rlim64_t limit = MAXOFFSET_T;

	if (woff >= limit) {
		zfs_rangelock_exit(lr);
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(EFBIG));
	}

	if (n > limit - woff)
		n = limit - woff;

	uint64_t end_size = MAX(zp->z_size, woff + n);
	zilog_t *zilog = zfsvfs->z_log;

	const uint64_t uid = KUID_TO_SUID(ZTOUID(zp));
	const uint64_t gid = KGID_TO_SGID(ZTOGID(zp));
	const uint64_t projid = zp->z_projid;

	/*
	 * Write the file in reasonable size chunks.  Each chunk is written
	 * in a separate transaction; this keeps the intent log records small
	 * and allows us to do more fine-grained space accounting.
	 */
	while (n > 0) {
		woff = uio->uio_loffset;

		if (zfs_id_overblockquota(zfsvfs, DMU_USERUSED_OBJECT, uid) ||
		    zfs_id_overblockquota(zfsvfs, DMU_GROUPUSED_OBJECT, gid) ||
		    (projid != ZFS_DEFAULT_PROJID &&
		    zfs_id_overblockquota(zfsvfs, DMU_PROJECTUSED_OBJECT,
		    projid))) {
			error = SET_ERROR(EDQUOT);
			break;
		}

		arc_buf_t *abuf = NULL;
		if (n >= max_blksz && woff >= zp->z_size &&
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
			ASSERT3S(cbytes, ==, max_blksz);
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
		const ssize_t nbytes =
		    MIN(n, max_blksz - P2PHASE(woff, max_blksz));

		ssize_t tx_bytes;
		if (abuf == NULL) {
			tx_bytes = uio->uio_resid;
			uio_fault_disable(uio, B_TRUE);
			error = dmu_write_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, nbytes, tx);
			uio_fault_disable(uio, B_FALSE);
#ifdef __linux__
			if (error == EFAULT) {
				dmu_tx_commit(tx);
				/*
				 * Account for partial writes before
				 * continuing the loop.
				 * Update needs to occur before the next
				 * uio_prefaultpages, or prefaultpages may
				 * error, and we may break the loop early.
				 */
				if (tx_bytes != uio->uio_resid)
					n -= tx_bytes - uio->uio_resid;
				if (uio_prefaultpages(MIN(n, max_blksz), uio)) {
					break;
				}
				continue;
			}
#endif
			if (error != 0) {
				dmu_tx_commit(tx);
				break;
			}
			tx_bytes -= uio->uio_resid;
		} else {
			/* Implied by abuf != NULL: */
			ASSERT3S(n, >=, max_blksz);
			ASSERT0(P2PHASE(woff, max_blksz));
			/*
			 * We can simplify nbytes to MIN(n, max_blksz) since
			 * P2PHASE(woff, max_blksz) is 0, and knowing
			 * n >= max_blksz lets us simplify further:
			 */
			ASSERT3S(nbytes, ==, max_blksz);
			/*
			 * Thus, we're writing a full block at a block-aligned
			 * offset and extending the file past EOF.
			 *
			 * dmu_assign_arcbuf_by_dbuf() will directly assign the
			 * arc buffer to a dbuf.
			 */
			error = dmu_assign_arcbuf_by_dbuf(
			    sa_get_db(zp->z_sa_hdl), woff, abuf, tx);
			if (error != 0) {
				dmu_return_arcbuf(abuf);
				dmu_tx_commit(tx);
				break;
			}
			ASSERT3S(nbytes, <=, uio->uio_resid);
			uioskip(uio, nbytes);
			tx_bytes = nbytes;
		}
		if (tx_bytes && zn_has_cached_data(zp) &&
		    !(ioflag & O_DIRECT)) {
			update_pages(zp, woff, tx_bytes, zfsvfs->z_os);
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
		if ((zp->z_mode & (S_IXUSR | (S_IXUSR >> 3) |
		    (S_IXUSR >> 6))) != 0 &&
		    (zp->z_mode & (S_ISUID | S_ISGID)) != 0 &&
		    secpolicy_vnode_setid_retain(zp, cr,
		    ((zp->z_mode & S_ISUID) != 0 && uid == 0)) != 0) {
			uint64_t newmode;
			zp->z_mode &= ~(S_ISUID | S_ISGID);
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs),
			    (void *)&newmode, sizeof (uint64_t), tx);
		}
		mutex_exit(&zp->z_acl_lock);

		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);

		/*
		 * Update the file size (zp_size) if it has changed;
		 * account for possible concurrent updates.
		 */
		while ((end_size = zp->z_size) < uio->uio_loffset) {
			(void) atomic_cas_64(&zp->z_size, end_size,
			    uio->uio_loffset);
			ASSERT(error == 0);
		}
		/*
		 * If we are replaying and eof is non zero then force
		 * the file size to the specified eof. Note, there's no
		 * concurrency during replay.
		 */
		if (zfsvfs->z_replay && zfsvfs->z_replay_eof != 0)
			zp->z_size = zfsvfs->z_replay_eof;

		error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);

		zfs_log_write(zilog, tx, TX_WRITE, zp, woff, tx_bytes, ioflag,
		    NULL, NULL);
		dmu_tx_commit(tx);

		if (error != 0)
			break;
		ASSERT3S(tx_bytes, ==, nbytes);
		n -= nbytes;

		if (n > 0) {
			if (uio_prefaultpages(MIN(n, max_blksz), uio)) {
				error = SET_ERROR(EFAULT);
				break;
			}
		}
	}

	zfs_inode_update(zp);
	zfs_rangelock_exit(lr);

	/*
	 * If we're in replay mode, or we made no progress, or the
	 * uio data is inaccessible return an error.  Otherwise, it's
	 * at least a partial write, so it's successful.
	 */
	if (zfsvfs->z_replay || uio->uio_resid == start_resid ||
	    error == EFAULT) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (ioflag & (O_SYNC | O_DSYNC) ||
	    zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, zp->z_id);

	const int64_t nwritten = start_resid - uio->uio_resid;
	dataset_kstats_update_write_kstats(&zfsvfs->z_kstat, nwritten);
	task_io_account_write(nwritten);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*ARGSUSED*/
int
zfs_getsecattr(znode_t *zp, vsecattr_t *vsecp, int flag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	error = zfs_getacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*ARGSUSED*/
int
zfs_setsecattr(znode_t *zp, vsecattr_t *vsecp, int flag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	zilog_t	*zilog = zfsvfs->z_log;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	error = zfs_setacl(zp, vsecp, skipaclchk, cr);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

EXPORT_SYMBOL(zfs_fsync);
EXPORT_SYMBOL(zfs_read);
EXPORT_SYMBOL(zfs_write);
EXPORT_SYMBOL(zfs_getsecattr);
EXPORT_SYMBOL(zfs_setsecattr);

ZFS_MODULE_PARAM(zfs_vnops, zfs_vnops_, read_chunk_size, ULONG, ZMOD_RW,
	"Bytes to read per chunk");
