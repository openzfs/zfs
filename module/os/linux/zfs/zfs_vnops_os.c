// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
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
#include <sys/zfs_acl_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_direct_os.h>
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
#include <sys/rrwlock.h>
#include <linux/mm_compat.h>

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
 *	This is done avoiding races using zfs_enter(zfsvfs).
 *      A zfs_exit(zfsvfs) is needed before all returns.  Any znodes
 *      must be checked with zfs_verify_zp(zp).  Both of these macros
 *      can return EIO from the calling function.
 *
 *  (2) zrele() should always be the last thing except for zil_commit() (if
 *	necessary) and zfs_exit(). This is for 3 reasons: First, if it's the
 *	last reference, the vnode/znode can be freed, so the zp may point to
 *	freed memory.  Second, the last reference will call zfs_zinactive(),
 *	which may induce a lot of work -- pushing cached pages (which acquires
 *	range locks) and syncing out cached atime changes.  Third,
 *	zfs_zinactive() may require a new tx, which could deadlock the system
 *	if you were already holding one. This deadlock occurs because the tx
 *	currently being operated on prevents a txg from syncing, which
 *	prevents the new tx from progressing, resulting in a deadlock.  If you
 *	must call zrele() within a tx, use zfs_zrele_async(). Note that iput()
 *	is a synonym for zrele().
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
 *	zfs_dirent_lock(&dl, ...)	// lock directory entry (may igrab())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx,
 *	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
 *		zfs_exit(zfsvfs);	// finished in zfs
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
 *	zfs_exit(zfsvfs);		// finished in zfs
 *	return (error);			// done, report error
 */
int
zfs_open(struct inode *ip, int mode, int flag, cred_t *cr)
{
	(void) cr;
	znode_t	*zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	/* Honor ZFS_APPENDONLY file attribute */
	if (blk_mode_is_open_write(mode) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((flag & O_APPEND) == 0)) {
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
zfs_close(struct inode *ip, int flag, cred_t *cr)
{
	(void) cr;
	znode_t	*zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	/* Decrement the synchronous opens in the znode */
	if (!zfsvfs->z_issnap && (flag & O_SYNC))
		atomic_dec_32(&zp->z_sync_cnt);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

#if defined(_KERNEL)

static int zfs_fillpage(struct inode *ip, struct page *pp);

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  Update all mapped
 * pages with the contents of the coresponding dmu buffer.
 */
void
update_pages(znode_t *zp, int64_t start, int len, objset_t *os)
{
	struct address_space *mp = ZTOI(zp)->i_mapping;
	int64_t off = start & (PAGE_SIZE - 1);

	for (start &= PAGE_MASK; len > 0; start += PAGE_SIZE) {
		uint64_t nbytes = MIN(PAGE_SIZE - off, len);

		struct page *pp = find_lock_page(mp, start >> PAGE_SHIFT);
		if (pp) {
			if (mapping_writably_mapped(mp))
				flush_dcache_page(pp);

			void *pb = kmap(pp);
			int error = dmu_read(os, zp->z_id, start + off,
			    nbytes, pb + off, DMU_READ_PREFETCH);
			kunmap(pp);

			if (error) {
				SetPageError(pp);
				ClearPageUptodate(pp);
			} else {
				ClearPageError(pp);
				SetPageUptodate(pp);

				if (mapping_writably_mapped(mp))
					flush_dcache_page(pp);

				mark_page_accessed(pp);
			}

			unlock_page(pp);
			put_page(pp);
		}

		len -= nbytes;
		off = 0;
	}
}

/*
 * When a file is memory mapped, we must keep the I/O data synchronized
 * between the DMU cache and the memory mapped pages.  Preferentially read
 * from memory mapped pages, otherwise fallback to reading through the dmu.
 *
 * A run of non-resident pages is read from the DMU in a single call rather
 * than one call per page.  A page may become resident between the lookup
 * and the read, but that is safe: zfs_read() holds the rangelock as reader,
 * so the DMU contents of the range are stable (writes, writeback and
 * truncation take the writer lock) and a concurrently faulted page is
 * filled by zfs_getpage() from those same contents.
 */
int
mappedread(znode_t *zp, int nbytes, zfs_uio_t *uio)
{
	struct inode *ip = ZTOI(zp);
	struct address_space *mp = ip->i_mapping;
	int64_t start = uio->uio_loffset;
	int64_t off = start & (PAGE_SIZE - 1);
	int len = nbytes;
	int error = 0;

	for (start &= PAGE_MASK; len > 0; start += PAGE_SIZE) {
		uint64_t bytes = MIN(PAGE_SIZE - off, len);

		struct page *pp = find_lock_page(mp, start >> PAGE_SHIFT);
		if (pp) {

			/*
			 * If filemap_fault() retries there exists a window
			 * where the page will be unlocked and not up to date.
			 * In this case we must try and fill the page.
			 */
			if (unlikely(!PageUptodate(pp))) {
				error = zfs_fillpage(ip, pp);
				if (error) {
					unlock_page(pp);
					put_page(pp);
					return (error);
				}
			}

			ASSERT(PageUptodate(pp) || PageDirty(pp));

			unlock_page(pp);

			void *pb = kmap(pp);
			error = zfs_uiomove(pb + off, bytes, UIO_READ, uio);
			kunmap(pp);

			if (mapping_writably_mapped(mp))
				flush_dcache_page(pp);

			mark_page_accessed(pp);
			put_page(pp);
		} else {
			/*
			 * Extend the read over any following non-resident
			 * pages so they are fetched in one DMU call.
			 */
			while (bytes < len) {
				struct page *tp = find_get_page(mp,
				    (start + PAGE_SIZE) >> PAGE_SHIFT);
				if (tp != NULL) {
					put_page(tp);
					break;
				}
				bytes += MIN(PAGE_SIZE, len - bytes);
				start += PAGE_SIZE;
			}
			error = dmu_read_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, bytes, DMU_READ_PREFETCH);
		}

		len -= bytes;
		off = 0;

		if (error)
			break;
	}

	return (error);
}
#endif /* _KERNEL */

static unsigned long zfs_delete_blocks = DMU_MAX_DELETEBLKCNT;

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
 *		positive error code if failure.  EIO is	returned
 *		for a short write when residp isn't provided.
 *
 * Timestamps:
 *	zp - ctime|mtime updated if byte count > 0
 */
int
zfs_write_simple(znode_t *zp, const void *data, size_t len,
    loff_t pos, size_t *residp)
{
	fstrans_cookie_t cookie;
	int error;

	struct iovec iov;
	iov.iov_base = (void *)data;
	iov.iov_len = len;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, pos, UIO_SYSSPACE, len, 0);

	cookie = spl_fstrans_mark();
	error = zfs_write(zp, &uio, 0, kcred);
	spl_fstrans_unmark(cookie);

	if (error == 0) {
		if (residp != NULL)
			*residp = zfs_uio_resid(&uio);
		else if (zfs_uio_resid(&uio) != 0)
			error = SET_ERROR(EIO);
	}

	return (error);
}

static void
zfs_rele_async_task(void *arg)
{
	iput(arg);
}

void
zfs_zrele_async(znode_t *zp)
{
	struct inode *ip = ZTOI(zp);
	objset_t *os = ITOZSB(ip)->z_os;

	ASSERT(atomic_read(&ip->i_count) > 0);
	ASSERT(os != NULL);

	/*
	 * If decrementing the count would put us at 0, we can't do it inline
	 * here, because that would be synchronous. Instead, dispatch an iput
	 * to run later.
	 *
	 * For more information on the dangers of a synchronous iput, see the
	 * header comment of this file.
	 */
	if (!atomic_add_unless(&ip->i_count, -1, 1)) {
		VERIFY(taskq_dispatch(dsl_pool_zrele_taskq(dmu_objset_pool(os)),
		    zfs_rele_async_task, ip, TQ_SLEEP) != TASKQID_INVALID);
	}
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
int
zfs_lookup(znode_t *zdp, char *nm, znode_t **zpp, int flags, cred_t *cr,
    int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zdp);
	int error = 0;

	/*
	 * Fast path lookup, however we must skip DNLC lookup
	 * for case folding or normalizing lookups because the
	 * DNLC code only stores the passed in name.  This means
	 * creating 'a' and removing 'A' on a case insensitive
	 * file system would work, but DNLC still thinks 'a'
	 * exists and won't let you create it again on the next
	 * pass through fast path.
	 */
	if (!(flags & (LOOKUP_XATTR | FIGNORECASE))) {

		if (!S_ISDIR(ZTOI(zdp)->i_mode)) {
			return (SET_ERROR(ENOTDIR));
		} else if (zdp->z_sa_hdl == NULL) {
			return (SET_ERROR(EIO));
		}

		if (nm[0] == 0 || (nm[0] == '.' && nm[1] == '\0')) {
			error = zfs_fastaccesschk_execute(zdp, cr);
			if (!error) {
				*zpp = zdp;
				zhold(*zpp);
				return (0);
			}
			return (error);
		}
	}

	if ((error = zfs_enter_verify_zp(zfsvfs, zdp, FTAG)) != 0)
		return (error);

	*zpp = NULL;

	if (flags & LOOKUP_XATTR) {
		/*
		 * We don't allow recursive attributes..
		 * Maybe someday we will.
		 */
		if (zdp->z_pflags & ZFS_XATTR) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(EINVAL));
		}

		if ((error = zfs_get_xattrdir(zdp, zpp, cr, flags))) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}

		/*
		 * Do we have permission to get into attribute directory?
		 */

		if ((error = zfs_zaccess(*zpp, ACE_EXECUTE, 0, B_TRUE, cr))) {
			zrele(*zpp);
			*zpp = NULL;
		}

		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (!S_ISDIR(ZTOI(zdp)->i_mode)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOTDIR));
	}

	/*
	 * Check accessibility of directory.
	 */

	if ((error = zfs_zaccess(zdp, ACE_EXECUTE, 0, B_FALSE, cr))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zfsvfs->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	error = zfs_dirlook(zdp, nm, zpp, flags, direntflags, realpnp);
	if ((error == 0) && (*zpp))
		zfs_znode_update_vfs(*zpp);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Perform a linear search in directory for the name of specific inode.
 * Note we don't pass in the buffer size of name because it's hardcoded to
 * NAME_MAX+1(256) in Linux.
 *
 *	IN:	dzp	- znode of directory to search.
 *		zp	- znode of the target
 *
 *	OUT:	name	- dentry name of the target
 *
 *	RETURN:	0 on success, error code on failure.
 */
int
zfs_get_name(znode_t *dzp, char *name, znode_t *zp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(dzp);
	int error = 0;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	if ((error = zfs_verify_zp(zp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/* ctldir should have got their name in zfs_vget */
	if (dzp->z_is_ctldir || zp->z_is_ctldir) {
		zfs_exit(zfsvfs, FTAG);
		return (ENOENT);
	}

	/* buffer len is hardcoded to 256 in Linux kernel */
	error = zap_value_search(zfsvfs->z_os, dzp->z_id, zp->z_id,
	    ZFS_DIRENT_OBJ(-1ULL), name, ZAP_MAXNAMELEN);

	zfs_exit(zfsvfs, FTAG);
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
 *		idmap	- idmap of the mount
 *
 *	OUT:	zpp	- znode of created or trunc'd entry.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dzp - ctime|mtime updated if new entry created
 *	 zp - ctime|mtime always, atime if new
 */
int
zfs_create_idmap(znode_t *dzp, char *name, vattr_t *vap, int excl,
    int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp,
    zidmap_t *idmap)
{
	znode_t		*zp;
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
	boolean_t	skip_acl = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

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

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			zfs_exit(zfsvfs, FTAG);
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
			zfs_exit(zfsvfs, FTAG);
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
		if ((error = zfs_zaccess_idmap(dzp, ACE_ADD_FILE, 0, skip_acl,
		    cr, idmap))) {
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
		    cr, vsecp, &acl_ids, idmap)) != 0)
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
		dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
		if (!zfsvfs->z_use_sa &&
		    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, acl_ids.z_aclp->z_acl_bytes);
		}

		error = dmu_tx_assign(tx,
		    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
			zfs_exit(zfsvfs, FTAG);
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
			remove_inode_hash(ZTOI(zp));
			zfs_acl_ids_free(&acl_ids);
			dmu_tx_commit(tx);
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
	} else {
		int aflags = (flag & O_APPEND) ? V_APPEND : 0;

		if (have_acl)
			zfs_acl_ids_free(&acl_ids);

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
		if (S_ISDIR(ZTOI(zp)->i_mode)) {
			error = SET_ERROR(EISDIR);
			goto out;
		}
		/*
		 * Verify requested access to file.
		 */
		if (mode && (error = zfs_zaccess_rwx_idmap(zp, mode, aflags,
		    cr, idmap))) {
			goto out;
		}

		atomic_inc_64(&dzp->z_seq);

		/*
		 * Truncate regular files if requested.
		 */
		if (S_ISREG(ZTOI(zp)->i_mode) &&
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
		zfs_znode_update_vfs(dzp);
		zfs_znode_update_vfs(zp);
		*zpp = zp;
	}

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}
int
zfs_create(znode_t *dzp, char *name, vattr_t *vap, int excl,
    int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp)
{
	return (zfs_create_idmap(dzp, name, vap, excl, mode, zpp, cr, flag,
	    vsecp, zfs_init_idmap));
}

int
zfs_tmpfile_idmap(struct inode *dip, vattr_t *vap, int excl,
    int mode, struct inode **ipp, cred_t *cr, int flag, vsecattr_t *vsecp,
    zidmap_t *idmap)
{
	(void) excl, (void) mode, (void) flag;
	znode_t		*zp = NULL, *dzp = ITOZ(dip);
	zfsvfs_t	*zfsvfs = ITOZSB(dip);
	objset_t	*os;
	dmu_tx_t	*tx;
	int		error;
	uid_t		uid;
	gid_t		gid;
	zfs_acl_ids_t   acl_ids;
	uint64_t	projid = ZFS_DEFAULT_PROJID;
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

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	os = zfsvfs->z_os;

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

top:
	*ipp = NULL;

	/*
	 * Create a new file object and update the directory
	 * to reference it.
	 */
	if ((error = zfs_zaccess_idmap(dzp, ACE_ADD_FILE, 0, B_FALSE,
	    cr, idmap))) {
		if (have_acl)
			zfs_acl_ids_free(&acl_ids);
		goto out;
	}

	if (!have_acl && (error = zfs_acl_ids_create(dzp, 0, vap,
	    cr, vsecp, &acl_ids, idmap)) != 0)
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
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	if (!zfsvfs->z_use_sa &&
	    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
		    0, acl_ids.z_aclp->z_acl_bytes);
	}
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
	if (error) {
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	zfs_mknode(dzp, vap, tx, cr, IS_TMPFILE, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	/* Add to unlinked set */
	zp->z_unlinked = B_TRUE;
	zfs_unlinked_add(zp, tx);
	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);
out:

	if (error) {
		if (zp)
			zrele(zp);
	} else {
		zfs_znode_update_vfs(dzp);
		zfs_znode_update_vfs(zp);
		*ipp = ZTOI(zp);
	}

	zfs_exit(zfsvfs, FTAG);
	return (error);
}
int
zfs_tmpfile(struct inode *dip, vattr_t *vap, int excl,
    int mode, struct inode **ipp, cred_t *cr, int flag, vsecattr_t *vsecp)
{
	return (zfs_tmpfile_idmap(dip, vap, excl, mode, ipp, cr, flag, vsecp,
	    zfs_init_idmap));
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

static uint64_t null_xattr = 0;

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
	pathname_t	*realnmp = NULL;
	pathname_t	realnm;
	int		error;
	int		zflg = ZEXISTS;
	boolean_t	waited = B_FALSE;

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	if (flags & FIGNORECASE) {
		zflg |= ZCILOOK;
		pn_alloc(&realnm);
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
			pn_free(realnmp);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess_delete(dzp, zp, cr, zfs_init_idmap))) {
		goto out;
	}

	/*
	 * Need to use rmdir for removing directories.
	 */
	if (S_ISDIR(ZTOI(zp)->i_mode)) {
		error = SET_ERROR(EPERM);
		goto out;
	}

	mutex_enter(&zp->z_lock);
	may_delete_now = atomic_read(&ZTOI(zp)->i_count) == 1 &&
	    !zn_has_cached_data(zp, 0, LLONG_MAX);
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
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, ZFS_SEQ_MAY_GROW(zp));
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
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
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(xzp));
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

	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
			pn_free(realnmp);
		dmu_tx_abort(tx);
		zrele(zp);
		if (xzp)
			zrele(xzp);
		zfs_exit(zfsvfs, FTAG);
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
		    atomic_read(&ZTOI(zp)->i_count) == 1 &&
		    !zn_has_cached_data(zp, 0, LLONG_MAX) &&
		    xattr_obj == xattr_obj_unlinked &&
		    zfs_external_acl(zp) == acl_obj;
		VERIFY_IMPLY(xattr_obj_unlinked, xzp);
	}

	if (delete_now) {
		if (xattr_obj_unlinked) {
			ASSERT3U(ZTOI(xzp)->i_nlink, ==, 2);
			mutex_enter(&xzp->z_lock);
			xzp->z_unlinked = B_TRUE;
			clear_nlink(ZTOI(xzp));
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
		pn_free(realnmp);

	zfs_dirent_unlock(dl);
	zfs_znode_update_vfs(dzp);
	zfs_znode_update_vfs(zp);

	if (delete_now)
		zrele(zp);
	else
		zfs_zrele_async(zp);

	if (xzp) {
		zfs_znode_update_vfs(xzp);
		zfs_zrele_async(xzp);
	}

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
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
 *		idmap	- idmap of the mount
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
int
zfs_mkdir_idmap(znode_t *dzp, char *dirname, vattr_t *vap, znode_t **zpp,
    cred_t *cr, int flags, vsecattr_t *vsecp, zidmap_t *idmap)
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
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

	if (vap->va_mask & ATTR_XVATTR) {
		if ((error = secpolicy_xvattr((xvattr_t *)vap,
		    crgetuid(cr), cr, vap->va_mode)) != 0) {
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr,
	    vsecp, &acl_ids, idmap)) != 0) {
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
top:
	*zpp = NULL;

	if ((error = zfs_dirent_lock(&dl, dzp, dirname, &zp, zf,
	    NULL, NULL))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess_idmap(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE,
	    cr, idmap))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, zfs_inherit_projid(dzp))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
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

	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
		zfs_exit(zfsvfs, FTAG);
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
		remove_inode_hash(ZTOI(zp));
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

	zfs_dirent_unlock(dl);

	if (error != 0) {
		zrele(zp);
	} else {
		zfs_znode_update_vfs(dzp);
		zfs_znode_update_vfs(zp);

		if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
			error = zil_commit(zilog, 0);

	}
	zfs_exit(zfsvfs, FTAG);
	return (error);
}
int
zfs_mkdir(znode_t *dzp, char *dirname, vattr_t *vap, znode_t **zpp,
    cred_t *cr, int flags, vsecattr_t *vsecp)
{
	return (zfs_mkdir_idmap(dzp, dirname, vap, zpp, cr, flags, vsecp,
	    zfs_init_idmap));
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

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
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
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess_delete(dzp, zp, cr, zfs_init_idmap))) {
		goto out;
	}

	if (!S_ISDIR(ZTOI(zp)->i_mode)) {
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
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, ZFS_SEQ_MAY_GROW(zp));
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(dzp));
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
		zfs_exit(zfsvfs, FTAG);
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

	zfs_znode_update_vfs(dzp);
	zfs_znode_update_vfs(zp);
	zrele(zp);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
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
int
zfs_readdir(struct inode *ip, struct dir_context *ctx, cred_t *cr)
{
	(void) cr;
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	objset_t	*os;
	zap_cursor_t	zc;
	zap_attribute_t	*zap;
	int		error;
	uint8_t		prefetch;
	uint8_t		type;
	int		done = 0;
	uint64_t	parent;
	uint64_t	offset; /* must be unsigned; checks for < 1 */

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent))) != 0)
		goto out;

	/*
	 * Quit if directory has been removed (posix)
	 */
	if (zp->z_unlinked)
		goto out;

	error = 0;
	os = zfsvfs->z_os;
	offset = ctx->pos;
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
	 * Transform to file-system independent format
	 */
	while (!done) {
		uint64_t objnum;
		/*
		 * Special case `.', `..', and `.zfs'.
		 */
		if (offset == 0) {
			(void) strcpy(zap->za_name, ".");
			zap->za_normalization_conflict = 0;
			objnum = zp->z_id;
			type = DT_DIR;
		} else if (offset == 1) {
			(void) strcpy(zap->za_name, "..");
			zap->za_normalization_conflict = 0;
			objnum = parent;
			type = DT_DIR;
		} else if (offset == 2 && zfs_show_ctldir(zp)) {
			(void) strcpy(zap->za_name, ZFS_CTLDIR_NAME);
			zap->za_normalization_conflict = 0;
			objnum = ZFSCTL_INO_ROOT;
			type = DT_DIR;
		} else {
			/*
			 * Grab next entry.
			 */
			if ((error = zap_cursor_retrieve(&zc, zap))) {
				if (error == ENOENT)
					break;
				else
					goto update;
			}

			/*
			 * Allow multiple entries provided the first entry is
			 * the object id.  Non-zpl consumers may safely make
			 * use of the additional space.
			 *
			 * XXX: This should be a feature flag for compatibility
			 */
			if (zap->za_integer_length != 8 ||
			    zap->za_num_integers == 0) {
				cmn_err(CE_WARN, "zap_readdir: bad directory "
				    "entry, obj = %lld, offset = %lld, "
				    "length = %d, num = %lld\n",
				    (u_longlong_t)zp->z_id,
				    (u_longlong_t)offset,
				    zap->za_integer_length,
				    (u_longlong_t)zap->za_num_integers);
				error = SET_ERROR(ENXIO);
				goto update;
			}

			objnum = ZFS_DIRENT_OBJ(zap->za_first_integer);
			type = ZFS_DIRENT_TYPE(zap->za_first_integer);
		}

		done = !dir_emit(ctx, zap->za_name, strlen(zap->za_name),
		    objnum, type);
		if (done)
			break;

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
		ctx->pos = offset;
	}
	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

update:
	zap_cursor_fini(&zc);
	zap_attribute_free(zap);
	if (error == ENOENT)
		error = 0;
out:
	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Get the basic file attributes and place them in the provided kstat
 * structure.  The inode is assumed to be the authoritative source
 * for most of the attributes.  However, the znode currently has the
 * authoritative atime, blksize, and block count.
 *
 *	IN:	ip	- inode of file.
 *
 *	OUT:	sp	- kstat values.
 *
 *	RETURN:	0 (always succeeds)
 */
int
zfs_getattr_fast(zidmap_t *idmap, u32 request_mask, struct inode *ip,
    struct kstat *sp)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	uint32_t blksize;
	u_longlong_t nblocks;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	mutex_enter(&zp->z_lock);

	zpl_generic_fillattr(idmap, request_mask, ip, sp);

	/*
	 * +1 link count for root inode with visible '.zfs' directory.
	 */
	if ((zp->z_id == zfsvfs->z_root) && zfs_show_ctldir(zp))
		if (sp->nlink < ZFS_LINK_MAX)
			sp->nlink++;

	sa_object_size(zp->z_sa_hdl, &blksize, &nblocks);
	sp->blksize = blksize;
	sp->blocks = nblocks;

	if (unlikely(zp->z_blksz == 0)) {
		/*
		 * Block size hasn't been set; suggest maximal I/O transfers.
		 */
		sp->blksize = zfsvfs->z_max_blksz;
	}

	mutex_exit(&zp->z_lock);

	/*
	 * Required to prevent NFS client from detecting different inode
	 * numbers of snapshot root dentry before and after snapshot mount.
	 */
	if (zfsvfs->z_issnap) {
		if (ip->i_sb->s_root->d_inode == ip)
			sp->ino = ZFSCTL_INO_SNAPDIRS -
			    dmu_objset_id(zfsvfs->z_os);
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
	struct inode	*dxip = ZTOI(dzp);
	struct inode	*xip = NULL;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	objset_t	*os = zfsvfs->z_os;
	zap_cursor_t	zc;
	zap_attribute_t	*zap;
	zfs_dirlock_t	*dl;
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

		err = zfs_dirent_lock(&dl, dzp, (char *)zap->za_name, &zp,
		    ZEXISTS, NULL, NULL);
		if (err == ENOENT)
			goto next;
		if (err)
			break;

		xip = ZTOI(zp);
		if (KUID_TO_SUID(xip->i_uid) == KUID_TO_SUID(dxip->i_uid) &&
		    KGID_TO_SGID(xip->i_gid) == KGID_TO_SGID(dxip->i_gid) &&
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

		if (KUID_TO_SUID(xip->i_uid) != KUID_TO_SUID(dxip->i_uid)) {
			xip->i_uid = dxip->i_uid;
			uid = zfs_uid_read(dxip);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &uid, sizeof (uid));
		}

		if (KGID_TO_SGID(xip->i_gid) != KGID_TO_SGID(dxip->i_gid)) {
			xip->i_gid = dxip->i_gid;
			gid = zfs_gid_read(dxip);
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
			    &gid, sizeof (gid));
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
	zap_attribute_free(zap);

	return (err == ENOENT ? 0 : err);
}

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
 *		idmap	- idmap of the mount
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime updated, mtime updated if size changed.
 */
int
zfs_setattr_idmap(znode_t *zp, vattr_t *vap, int flags, cred_t *cr,
    zidmap_t *idmap)
{
	struct inode	*ip;
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	objset_t	*os;
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
	uint64_t	mtime[2], ctime[2], atime[2];
	uint64_t	projid = ZFS_INVALID_PROJID;
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2 = 0;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t	fuid_dirtied = B_FALSE;
	boolean_t	handle_eadir = B_FALSE;
	sa_bulk_attr_t	*bulk, *xattr_bulk;
	int		count = 0, xattr_count = 0, bulks = 9;

	if (mask == 0)
		return (0);

	if ((err = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (err);
	ip = ZTOI(zp);
	os = zfsvfs->z_os;

	/*
	 * If this is a xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);
	if (xoap != NULL && (mask & ATTR_XVATTR)) {
		if (XVA_ISSET_REQ(xvap, XAT_PROJID)) {
			if (!dmu_objset_projectquota_enabled(os) ||
			    (!S_ISREG(ip->i_mode) && !S_ISDIR(ip->i_mode))) {
				zfs_exit(zfsvfs, FTAG);
				return (SET_ERROR(ENOTSUP));
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
		    (!S_ISREG(ip->i_mode) && !S_ISDIR(ip->i_mode)))) {
			zfs_exit(zfsvfs, FTAG);
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
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (mask & ATTR_SIZE && S_ISDIR(ip->i_mode)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EISDIR));
	}

	if (mask & ATTR_SIZE && !S_ISREG(ip->i_mode) && !S_ISFIFO(ip->i_mode)) {
		zfs_exit(zfsvfs, FTAG);
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

	/* ZFS_READONLY will be handled in zfs_zaccess() */

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
		err = zfs_zaccess_idmap(zp, ACE_WRITE_DATA, 0, skipaclchk,
		    cr, idmap);
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
		need_policy = zfs_zaccess_idmap(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr, idmap);
	}

	if (mask & (ATTR_UID|ATTR_GID)) {
		int	idmask = (mask & (ATTR_UID|ATTR_GID));
		int	take_owner;
		int	take_group;
		uid_t	uid;
		gid_t	gid;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & ATTR_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		uid = zfs_uid_to_vfsuid(idmap, zfs_i_user_ns(ip),
		    vap->va_uid);
		gid = zfs_gid_to_vfsgid(idmap, zfs_i_user_ns(ip),
		    vap->va_gid);
		take_owner = (mask & ATTR_UID) && (uid == crgetuid(cr));
		take_group = (mask & ATTR_GID) &&
		    zfs_groupmember(zfsvfs, gid, cr);

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
			if (zfs_zaccess_idmap(zp, ACE_WRITE_OWNER, 0,
			    skipaclchk, cr, idmap) == 0) {
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
			if ((!S_ISREG(ip->i_mode) &&
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
		if (zfs_zaccess_idmap(zp, ACE_WRITE_ACL, 0, skipaclchk, cr,
		    idmap) == 0) {
			err = secpolicy_setid_setsticky_clear(ip, vap,
			    &oldva, cr, idmap, zfs_i_user_ns(ip));
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
		err = secpolicy_vnode_setattr(cr, ip, vap, &oldva, flags,
		    zfs_zaccess_unix, zp);
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
			if (new_kuid != KUID_TO_SUID(ZTOI(zp)->i_uid) &&
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
			if (new_kgid != KGID_TO_SGID(ZTOI(zp)->i_gid) &&
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

		if (ZTOZSB(zp)->z_acl_mode == ZFS_ACL_RESTRICTED &&
		    !(zp->z_pflags & ZFS_ACL_TRIVIAL)) {
			err = EPERM;
			goto out;
		}

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
		    !(zp->z_pflags & ZFS_PROJID)) ||
		    !zp->z_has_seq)
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

	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_enter(&zp->z_acl_lock);
	mutex_enter(&zp->z_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		/*
		 * attrzp is zp's hidden xattr directory, so the second
		 * znode lock acquisition is nested rather than recursive.
		 */
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_enter_nested(&attrzp->z_acl_lock, NESTED_SINGLE);
		mutex_enter_nested(&attrzp->z_lock, NESTED_SINGLE);
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
			ZTOI(zp)->i_uid = SUID_TO_KUID(new_kuid);
			new_uid = zfs_uid_read(ZTOI(zp));
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &new_uid, sizeof (new_uid));
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_UID(zfsvfs), NULL, &new_uid,
				    sizeof (new_uid));
				ZTOI(attrzp)->i_uid = SUID_TO_KUID(new_uid);
			}
		}

		if (mask & ATTR_GID) {
			ZTOI(zp)->i_gid = SGID_TO_KGID(new_kgid);
			new_gid = zfs_gid_read(ZTOI(zp));
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs),
			    NULL, &new_gid, sizeof (new_gid));
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_GID(zfsvfs), NULL, &new_gid,
				    sizeof (new_gid));
				ZTOI(attrzp)->i_gid = SGID_TO_KGID(new_kgid);
			}
		}
		if (!(mask & ATTR_MODE)) {
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

	if (mask & ATTR_MODE) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
		    &new_mode, sizeof (new_mode));
		zp->z_mode = ZTOI(zp)->i_mode = new_mode;
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
		inode_timespec_t tmp_atime = zpl_inode_get_atime(ip);
		ZFS_TIME_ENCODE(&tmp_atime, atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
		    &atime, sizeof (atime));
	}

	if (mask & (ATTR_MTIME | ATTR_SIZE)) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		zpl_inode_set_mtime_to_ts(ZTOI(zp),
		    zpl_inode_timestamp_truncate(vap->va_mtime, ZTOI(zp)));

		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    mtime, sizeof (mtime));
	}

	if (mask & (ATTR_CTIME | ATTR_SIZE)) {
		ZFS_TIME_ENCODE(&vap->va_ctime, ctime);
		zpl_inode_set_ctime_to_ts(ZTOI(zp),
		    zpl_inode_timestamp_truncate(vap->va_ctime, ZTOI(zp)));
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    ctime, sizeof (ctime));
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
			ASSERT(S_ISREG(ip->i_mode));

		zfs_xvattr_set(zp, xvap, tx);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0) {
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);
		/*
		 * ATTR_MODE bumps via zfs_aclset_common -> tstamp_update_setup;
		 * ATTR_SIZE goes through zfs_freesp(log=FALSE) which does not.
		 */
		if (!(mask & ATTR_MODE))
			atomic_inc_64(&zp->z_seq);
		ZFS_PERSIST_SEQ(zp, bulk, count);
	}

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
		ASSERT3S(xattr_count, <=, bulks);
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		ASSERT0(err2);
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
		ASSERT3S(count, <=, bulks);
		if (count > 0)
			err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
		if (attrzp) {
			if (err2 == 0 && handle_eadir)
				err = zfs_setattr_dir(attrzp);
			zrele(attrzp);
		}
		zfs_znode_update_vfs(zp);
	}

out2:
	if (err == 0 && os->os_sync == ZFS_SYNC_ALWAYS)
		err = zil_commit(zilog, 0);

out3:
	kmem_free(xattr_bulk, sizeof (sa_bulk_attr_t) * bulks);
	kmem_free(bulk, sizeof (sa_bulk_attr_t) * bulks);
	kmem_free(tmpxvattr, sizeof (xvattr_t));
	zfs_exit(zfsvfs, FTAG);
	return (err);
}
int
zfs_setattr(znode_t *zp, vattr_t *vap, int flags, cred_t *cr)
{
	return (zfs_setattr_idmap(zp, vap, flags, cr, zfs_init_idmap));
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
 *		rflags  - RENAME_* flags
 *		wa_vap  - attributes for RENAME_WHITEOUT (must be a char 0:0).
 *		idmap	- idmap of the mount
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	sdzp,tdzp - ctime|mtime updated
 */
int
zfs_rename_idmap(znode_t *sdzp, char *snm, znode_t *tdzp, char *tnm,
    cred_t *cr, int flags, uint64_t rflags, vattr_t *wo_vap, zidmap_t *idmap)
{
	znode_t		*szp, *tzp;
	zfsvfs_t	*zfsvfs = ZTOZSB(sdzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*sdl, *tdl;
	dmu_tx_t	*tx;
	zfs_zlock_t	*zl;
	int		cmp, serr, terr;
	int		error = 0;
	int		zflg = 0;
	boolean_t	waited = B_FALSE;
	/* Needed for whiteout inode creation. */
	boolean_t	fuid_dirtied;
	zfs_acl_ids_t	acl_ids;
	boolean_t	have_acl = B_FALSE;
	znode_t		*wzp = NULL;


	if (snm == NULL || tnm == NULL)
		return (SET_ERROR(EINVAL));

	if (rflags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return (SET_ERROR(EINVAL));

	/* Already checked by Linux VFS, but just to make sure. */
	if (rflags & RENAME_EXCHANGE &&
	    (rflags & (RENAME_NOREPLACE | RENAME_WHITEOUT)))
		return (SET_ERROR(EINVAL));

	/*
	 * Make sure we only get wo_vap iff. RENAME_WHITEOUT and that it's the
	 * right kind of vattr_t for the whiteout file. These are set
	 * internally by ZFS so should never be incorrect.
	 */
	VERIFY_EQUIV(rflags & RENAME_WHITEOUT, wo_vap != NULL);
	VERIFY_IMPLY(wo_vap, wo_vap->va_mode == S_IFCHR);
	VERIFY_IMPLY(wo_vap, wo_vap->va_rdev == makedevice(0, 0));

	if ((error = zfs_enter_verify_zp(zfsvfs, sdzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	if ((error = zfs_verify_zp(tdzp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * We check i_sb because snapshots and the ctldir must have different
	 * super blocks.
	 */
	if (ZTOI(tdzp)->i_sb != ZTOI(sdzp)->i_sb ||
	    zfsctl_is_node(ZTOI(tdzp))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EXDEV));
	}

	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
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
		zfs_exit(zfsvfs, FTAG);
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
			zfs_exit(zfsvfs, FTAG);
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

		if (strcmp(snm, "..") == 0)
			serr = EINVAL;
		zfs_exit(zfsvfs, FTAG);
		return (serr);
	}
	if (terr) {
		zfs_dirent_unlock(sdl);
		zrele(szp);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(tnm, "..") == 0)
			terr = EINVAL;
		zfs_exit(zfsvfs, FTAG);
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
	if ((error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, cr, idmap)))
		goto out;

	if (S_ISDIR(ZTOI(szp)->i_mode)) {
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
		if (rflags & RENAME_NOREPLACE) {
			error = SET_ERROR(EEXIST);
			goto out;
		}
		/*
		 * Source and target must be the same type (unless exchanging).
		 */
		if (!(rflags & RENAME_EXCHANGE)) {
			boolean_t s_is_dir = S_ISDIR(ZTOI(szp)->i_mode) != 0;
			boolean_t t_is_dir = S_ISDIR(ZTOI(tzp)->i_mode) != 0;

			if (s_is_dir != t_is_dir) {
				error = SET_ERROR(s_is_dir ? ENOTDIR : EISDIR);
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
	} else if (rflags & RENAME_EXCHANGE) {
		/* Target must exist for RENAME_EXCHANGE. */
		error = SET_ERROR(ENOENT);
		goto out;
	}

	/* Set up inode creation for RENAME_WHITEOUT. */
	if (rflags & RENAME_WHITEOUT) {
		/*
		 * Whiteout files are not regular files or directories, so to
		 * match zfs_create() we do not inherit the project id.
		 */
		uint64_t wo_projid = ZFS_DEFAULT_PROJID;

		error = zfs_zaccess_idmap(sdzp, ACE_ADD_FILE, 0, B_FALSE,
		    cr, idmap);
		if (error)
			goto out;

		if (!have_acl) {
			error = zfs_acl_ids_create(sdzp, 0, wo_vap, cr, NULL,
			    &acl_ids, idmap);
			if (error)
				goto out;
			have_acl = B_TRUE;
		}

		if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, wo_projid)) {
			error = SET_ERROR(EDQUOT);
			goto out;
		}
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, ZFS_SEQ_MAY_GROW(szp));
	dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(sdzp));
	dmu_tx_hold_zap(tx, sdzp->z_id,
	    (rflags & RENAME_EXCHANGE) ? TRUE : FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp) {
		dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(tdzp));
		zfs_sa_upgrade_txholds(tx, tdzp);
	}
	if (tzp) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(tzp));
		zfs_sa_upgrade_txholds(tx, tzp);
	}
	if (rflags & RENAME_WHITEOUT) {
		dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
		    ZFS_SA_BASE_ATTR_SIZE);

		dmu_tx_hold_zap(tx, sdzp->z_id, TRUE, snm);
		dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(sdzp));
		if (!zfsvfs->z_use_sa &&
		    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, acl_ids.z_aclp->z_acl_bytes);
		}
	}
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Unlink the source.
	 */
	szp->z_pflags |= ZFS_AV_MODIFIED;
	if (tdzp->z_pflags & ZFS_PROJINHERIT)
		szp->z_pflags |= ZFS_PROJINHERIT;

	error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
	    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
	VERIFY0(error);

	error = zfs_link_destroy(sdl, szp, tx, ZRENAMING, NULL);
	if (error)
		goto commit;

	/*
	 * Unlink the target.
	 */
	if (tzp) {
		int tzflg = zflg;

		if (rflags & RENAME_EXCHANGE) {
			/* This inode will be re-linked soon. */
			tzflg |= ZRENAMING;

			tzp->z_pflags |= ZFS_AV_MODIFIED;
			if (sdzp->z_pflags & ZFS_PROJINHERIT)
				tzp->z_pflags |= ZFS_PROJINHERIT;

			error = sa_update(tzp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
			    (void *)&tzp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);
		}
		error = zfs_link_destroy(tdl, tzp, tx, tzflg, NULL);
		if (error)
			goto commit_link_szp;
	}

	/*
	 * Create the new target links:
	 *   * We always link the target.
	 *   * RENAME_EXCHANGE: Link the old target to the source.
	 *   * RENAME_WHITEOUT: Create a whiteout inode in-place of the source.
	 */
	error = zfs_link_create(tdl, szp, tx, ZRENAMING);
	if (error) {
		/*
		 * If we have removed the existing target, a subsequent call to
		 * zfs_link_create() to add back the same entry, but with a new
		 * dnode (szp), should not fail.
		 */
		ASSERT0P(tzp);
		goto commit_link_tzp;
	}

	switch (rflags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) {
	case RENAME_EXCHANGE:
		error = zfs_link_create(sdl, tzp, tx, ZRENAMING);
		/*
		 * The same argument as zfs_link_create() failing for
		 * szp applies here, since the source directory must
		 * have had an entry we are replacing.
		 */
		ASSERT0(error);
		if (error)
			goto commit_unlink_td_szp;
		break;
	case RENAME_WHITEOUT:
		zfs_mknode(sdzp, wo_vap, tx, cr, 0, &wzp, &acl_ids);
		error = zfs_link_create(sdl, wzp, tx, ZNEW);
		if (error) {
			zfs_znode_delete(wzp, tx);
			remove_inode_hash(ZTOI(wzp));
			goto commit_unlink_td_szp;
		}
		break;
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	switch (rflags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) {
	case RENAME_EXCHANGE:
		zfs_log_rename_exchange(zilog, tx,
		    (flags & FIGNORECASE ? TX_CI : 0), sdzp, sdl->dl_name,
		    tdzp, tdl->dl_name, szp);
		break;
	case RENAME_WHITEOUT:
		zfs_log_rename_whiteout(zilog, tx,
		    (flags & FIGNORECASE ? TX_CI : 0), sdzp, sdl->dl_name,
		    tdzp, tdl->dl_name, szp, wzp);
		break;
	default:
		ASSERT0(rflags & ~RENAME_NOREPLACE);
		zfs_log_rename(zilog, tx, (flags & FIGNORECASE ? TX_CI : 0),
		    sdzp, sdl->dl_name, tdzp, tdl->dl_name, szp);
		break;
	}

commit:
	dmu_tx_commit(tx);
out:
	if (have_acl)
		zfs_acl_ids_free(&acl_ids);

	zfs_znode_update_vfs(sdzp);
	if (sdzp == tdzp)
		rw_exit(&sdzp->z_name_lock);

	if (sdzp != tdzp)
		zfs_znode_update_vfs(tdzp);

	zfs_znode_update_vfs(szp);
	zrele(szp);
	if (wzp) {
		zfs_znode_update_vfs(wzp);
		zrele(wzp);
	}
	if (tzp) {
		zfs_znode_update_vfs(tzp);
		zrele(tzp);
	}

	if (zl != NULL)
		zfs_rename_unlock(&zl);

	zfs_dirent_unlock(sdl);
	zfs_dirent_unlock(tdl);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);

	/*
	 * Clean-up path for broken link state.
	 *
	 * At this point we are in a (very) bad state, so we need to do our
	 * best to correct the state. In particular, all of the nlinks are
	 * wrong because we were destroying and creating links with ZRENAMING.
	 *
	 * In some form, all of these operations have to resolve the state:
	 *
	 *  * link_destroy() *must* succeed. Fortunately, this is very likely
	 *    since we only just created it.
	 *
	 *  * link_create()s are allowed to fail (though they shouldn't because
	 *    we only just unlinked them and are putting the entries back
	 *    during clean-up). But if they fail, we can just forcefully drop
	 *    the nlink value to (at the very least) avoid broken nlink values
	 *    -- though in the case of non-empty directories we will have to
	 *    panic (otherwise we'd have a leaked directory with a broken ..).
	 */
commit_unlink_td_szp:
	VERIFY0(zfs_link_destroy(tdl, szp, tx, ZRENAMING, NULL));
commit_link_tzp:
	if (tzp) {
		if (zfs_link_create(tdl, tzp, tx, ZRENAMING))
			VERIFY0(zfs_drop_nlink(tzp, tx, NULL));
	}
commit_link_szp:
	if (zfs_link_create(sdl, szp, tx, ZRENAMING))
		VERIFY0(zfs_drop_nlink(szp, tx, NULL));
	goto commit;
}
int
zfs_rename(znode_t *sdzp, char *snm, znode_t *tdzp, char *tnm,
    cred_t *cr, int flags, uint64_t rflags, vattr_t *wo_vap)
{
	return (zfs_rename_idmap(sdzp, snm, tdzp, tnm, cr, flags, rflags,
	    wo_vap, zfs_init_idmap));
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
 *		idmap	- user namespace of the mount
 *
 *	OUT:	zpp	- Znode for new symbolic link.
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	dip - ctime|mtime updated
 */
int
zfs_symlink_idmap(znode_t *dzp, char *name, vattr_t *vap, char *link,
    znode_t **zpp, cred_t *cr, int flags, zidmap_t *idmap)
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

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

	if (len > MAXPATHLEN) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids, idmap)) != 0) {
		zfs_exit(zfsvfs, FTAG);
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
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess_idmap(dzp, ACE_ADD_FILE, 0, B_FALSE,
	    cr, idmap))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, ZFS_DEFAULT_PROJID)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
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
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
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
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datasets the symlink will be an SA attribute
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
		remove_inode_hash(ZTOI(zp));
	} else {
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);

		zfs_znode_update_vfs(dzp);
		zfs_znode_update_vfs(zp);
	}

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (error == 0) {
		*zpp = zp;

		if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
			error = zil_commit(zilog, 0);
	} else {
		zrele(zp);
	}

	zfs_exit(zfsvfs, FTAG);
	return (error);
}
int
zfs_symlink(znode_t *dzp, char *name, vattr_t *vap, char *link,
    znode_t **zpp, cred_t *cr, int flags)
{
	return (zfs_symlink_idmap(dzp, name, vap, link, zpp, cr, flags,
	    zfs_init_idmap));
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
int
zfs_readlink(struct inode *ip, zfs_uio_t *uio, cred_t *cr)
{
	(void) cr;
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), uio);
	else
		error = zfs_sa_readlink(zp, uio);
	mutex_exit(&zp->z_lock);

	zfs_exit(zfsvfs, FTAG);
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
int
zfs_link(znode_t *tdzp, znode_t *szp, char *name, cred_t *cr,
    int flags)
{
	struct inode *sip = ZTOI(szp);
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

	is_tmpfile = (sip->i_nlink == 0 &&
	    (inode_state_read_once(sip) & I_LINKABLE));

	ASSERT(S_ISDIR(ZTOI(tdzp)->i_mode));

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	if ((error = zfs_enter_verify_zp(zfsvfs, tdzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (S_ISDIR(sip->i_mode)) {
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

	/*
	 * We check i_sb because snapshots and the ctldir must have different
	 * super blocks.
	 */
	if (sip->i_sb != ZTOI(tdzp)->i_sb || zfsctl_is_node(sip)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EXDEV));
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
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

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

	owner = zfs_fuid_map_id(zfsvfs, KUID_TO_SUID(sip->i_uid),
	    cr, ZFS_OWNER);
	if (owner != crgetuid(cr) && secpolicy_basic_link(cr) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_zaccess(tdzp, ACE_ADD_FILE, 0, B_FALSE, cr))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

top:
	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, tdzp, name, &tzp, zf, NULL, NULL);
	if (error) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, ZFS_SEQ_MAY_GROW(szp));
	dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, ZFS_SEQ_MAY_GROW(tdzp));
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, name);
	if (is_tmpfile)
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, tdzp);
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	/* unmark z_unlinked so zfs_link_create will not reject */
	if (is_tmpfile)
		szp->z_unlinked = B_FALSE;
	error = zfs_link_create(dl, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
		/*
		 * tmpfile is created to be in z_unlinkedobj, so remove it.
		 * Also, we don't log in ZIL, because all previous file
		 * operation on the tmpfile are ignored by ZIL. Instead we
		 * always wait for txg to sync to make sure all previous
		 * operation are sync safe.
		 */
		if (is_tmpfile) {
			VERIFY0(zap_remove_int(zfsvfs->z_os,
			    zfsvfs->z_unlinkedobj, szp->z_id, tx));
		} else {
			if (flags & FIGNORECASE)
				txtype |= TX_CI;
			zfs_log_link(zilog, tx, txtype, tdzp, szp, name);
		}
	} else if (is_tmpfile) {
		/* restore z_unlinked since when linking failed */
		szp->z_unlinked = B_TRUE;
	}
	txg = dmu_tx_get_txg(tx);
	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (error == 0) {
		if (!is_tmpfile && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
			error = zil_commit(zilog, 0);

		if (is_tmpfile && zfsvfs->z_os->os_sync != ZFS_SYNC_DISABLED) {
			txg_wait_flag_t wait_flags =
			    spa_get_failmode(dmu_objset_spa(zfsvfs->z_os)) ==
			    ZIO_FAILURE_MODE_CONTINUE ? TXG_WAIT_SUSPEND : 0;
			error = txg_wait_synced_flags(
			    dmu_objset_pool(zfsvfs->z_os), txg, wait_flags);
			if (error != 0) {
				ASSERT3U(error, ==, ESHUTDOWN);
				error = SET_ERROR(EIO);
			}
		}
	}

	zfs_znode_update_vfs(tdzp);
	zfs_znode_update_vfs(szp);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/* Finish page writeback. */
static inline void
zfs_page_writeback_done(struct page *pp, int err)
{
	if (err != 0) {
		/*
		 * Writeback failed. Re-dirty the page. It was undirtied before
		 * the IO was issued (in zfs_putpage() or write_cache_pages()).
		 * The kernel only considers writeback for dirty pages; if we
		 * don't do this, it is eligible for eviction without being
		 * written out, which we definitely don't want.
		 */
#ifdef HAVE_VFS_FILEMAP_DIRTY_FOLIO
		filemap_dirty_folio(page_mapping(pp), page_folio(pp));
#else
		__set_page_dirty_nobuffers(pp);
#endif
	}

	ClearPageError(pp);
	end_page_writeback(pp);
}

/*
 * ZIL callback for page writeback. Passes to zfs_log_write() in zfs_putpage()
 * for syncing writes. Called when the ZIL itx has been written to the log or
 * the whole txg syncs, or if the ZIL crashes or the pool suspends. Any failure
 * is passed as `err`.
 */
static void
zfs_putpage_commit_cb(void *arg, int err)
{
	zfs_page_writeback_done(arg, err);
}

/*
 * Push a page out to disk, once the page is on stable storage the
 * registered commit callback will be run as notification of completion.
 *
 *	IN:	ip	 - page mapped for inode.
 *		pp	 - page to push (page is locked)
 *		wbc	 - writeback control data
 *		for_sync - does the caller intend to wait synchronously for the
 *			   page writeback to complete?
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime|mtime updated
 */
int
zfs_putpage(struct inode *ip, struct page *pp, struct writeback_control *wbc,
    boolean_t for_sync)
{
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	loff_t		offset;
	loff_t		pgoff;
	unsigned int	pglen;
	dmu_tx_t	*tx;
	caddr_t		va;
	int		err = 0;
	uint64_t	mtime[2], ctime[2];
	inode_timespec_t tmp_ts;
	sa_bulk_attr_t	bulk[4];
	int		cnt = 0;
	struct address_space *mapping;

	if ((err = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (err);

	ASSERT(PageLocked(pp));

	pgoff = page_offset(pp);	/* Page byte-offset in file */
	offset = i_size_read(ip);	/* File length in bytes */
	pglen = MIN(PAGE_SIZE,		/* Page length in bytes */
	    P2ROUNDUP(offset, PAGE_SIZE)-pgoff);

	/* Page is beyond end of file */
	if (pgoff >= offset) {
		unlock_page(pp);
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	/* Truncate page length to end of file */
	if (pgoff + pglen > offset)
		pglen = offset - pgoff;

#if 0
	/*
	 * FIXME: Allow mmap writes past its quota.  The correct fix
	 * is to register a page_mkwrite() handler to count the page
	 * against its quota when it is about to be dirtied.
	 */
	if (zfs_id_overblockquota(zfsvfs, DMU_USERUSED_OBJECT,
	    KUID_TO_SUID(ip->i_uid)) ||
	    zfs_id_overblockquota(zfsvfs, DMU_GROUPUSED_OBJECT,
	    KGID_TO_SGID(ip->i_gid)) ||
	    (zp->z_projid != ZFS_DEFAULT_PROJID &&
	    zfs_id_overblockquota(zfsvfs, DMU_PROJECTUSED_OBJECT,
	    zp->z_projid))) {
		err = EDQUOT;
	}
#endif

	/*
	 * The ordering here is critical and must adhere to the following
	 * rules in order to avoid deadlocking in either zfs_read() or
	 * zfs_free_range() due to a lock inversion.
	 *
	 * 1) The page must be unlocked prior to acquiring the range lock.
	 *    This is critical because zfs_read() calls find_lock_page()
	 *    which may block on the page lock while holding the range lock.
	 *
	 * 2) Before setting or clearing write back on a page the range lock
	 *    must be held in order to prevent a lock inversion with the
	 *    zfs_free_range() function.
	 *
	 * This presents a problem because upon entering this function the
	 * page lock is already held.  To safely acquire the range lock the
	 * page lock must be dropped.  This creates a window where another
	 * process could truncate, invalidate, dirty, or write out the page.
	 *
	 * Therefore, after successfully reacquiring the range and page locks
	 * the current page state is checked.  In the common case everything
	 * will be as is expected and it can be written out.  However, if
	 * the page state has changed it must be handled accordingly.
	 */
	mapping = pp->mapping;
	redirty_page_for_writepage(wbc, pp);
	unlock_page(pp);

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zp->z_rangelock,
	    pgoff, pglen, RL_WRITER);
	lock_page(pp);

	/* Page mapping changed or it was no longer dirty, we're done */
	if (unlikely((mapping != pp->mapping) || !PageDirty(pp))) {
		unlock_page(pp);
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	/* Another process started write block if required */
	if (PageWriteback(pp)) {
		unlock_page(pp);
		zfs_rangelock_exit(lr);

		if (wbc->sync_mode != WB_SYNC_NONE) {
			if (PageWriteback(pp))
#ifdef HAVE_PAGEMAP_FOLIO_WAIT_BIT
				folio_wait_bit(page_folio(pp), PG_writeback);
#else
				wait_on_page_bit(pp, PG_writeback);
#endif
		}

		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	/* Clear the dirty flag the required locks are held */
	if (!clear_page_dirty_for_io(pp)) {
		unlock_page(pp);
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	/*
	 * Counterpart for redirty_page_for_writepage() above.  This page
	 * was in fact not skipped and should not be counted as if it were.
	 */
	wbc->pages_skipped--;
	set_page_writeback(pp);
	unlock_page(pp);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, zp->z_id, pgoff, pglen);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, ZFS_SEQ_MAY_GROW(zp));
	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		zfs_page_writeback_done(pp, err);
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);

		/*
		 * Don't return error for an async writeback; we've re-dirtied
		 * the page so it will be tried again some other time.
		 */
		return (for_sync ? err : 0);
	}

	va = kmap(pp);
	ASSERT3U(pglen, <=, PAGE_SIZE);
	dmu_write(zfsvfs->z_os, zp->z_id, pgoff, pglen, va, tx,
	    DMU_READ_PREFETCH);
	kunmap(pp);

	/* Preserve the mtime and ctime provided by the inode */
	tmp_ts = zpl_inode_get_mtime(ip);
	ZFS_TIME_ENCODE(&tmp_ts, mtime);
	tmp_ts = zpl_inode_get_ctime(ip);
	ZFS_TIME_ENCODE(&tmp_ts, ctime);
	zp->z_atime_dirty = B_FALSE;
	atomic_inc_64(&zp->z_seq);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);
	ZFS_PERSIST_SEQ(zp, bulk, cnt);

	ASSERT3S(cnt, <=, ARRAY_SIZE(bulk));
	err = sa_bulk_update(zp->z_sa_hdl, bulk, cnt, tx);

	/*
	 * A note about for_sync vs wbc->sync_mode.
	 *
	 * for_sync indicates that this is a syncing writeback, that is, kernel
	 * caller expects the data to be durably stored before being notified.
	 * Often, but not always, the call was triggered by a userspace syncing
	 * op (eg fsync(), msync(MS_SYNC)). For our purposes, for_sync==TRUE
	 * means that that page should remain "locked" (in the writeback state)
	 * until it is definitely on disk (ie zil_commit() or spa_sync()).
	 * Otherwise, we can unlock and return as soon as it is on the
	 * in-memory ZIL.
	 *
	 * wbc->sync_mode has similar meaning. wbc is passed from the kernel to
	 * zpl_writepages()/zpl_writepage(); wbc->sync_mode==WB_SYNC_NONE
	 * indicates this a regular async writeback (eg a cache eviction) and
	 * so does not need a durability guarantee, while WB_SYNC_ALL indicates
	 * a syncing op that must be waited on (by convention, we test for
	 * !WB_SYNC_NONE rather than WB_SYNC_ALL, to prefer durability over
	 * performance should there ever be a new mode that we have not yet
	 * added support for).
	 *
	 * So, why a separate for_sync field? This is because zpl_writepages()
	 * calls zfs_putpage() multiple times for a single "logical" operation.
	 * It wants all the individual pages to be for_sync==TRUE ie only
	 * unlocked once durably stored, but it only wants one call to
	 * zil_commit() at the very end, once all the pages are synced. So,
	 * it repurposes sync_mode slightly to indicate who issue and wait for
	 * the IO: for NONE, the caller to zfs_putpage() will do it, while for
	 * ALL, zfs_putpage should do it.
	 *
	 * Summary:
	 *   for_sync:  0=unlock immediately; 1=unlock once on disk
	 *   sync_mode: NONE=caller will commit; ALL=we will commit
	 */
	boolean_t need_commit = (wbc->sync_mode != WB_SYNC_NONE);

	/*
	 * We use for_sync as the "commit" arg to zfs_log_write() (arg 7)
	 * because it is a policy flag that indicates "someone will call
	 * zil_commit() soon". for_sync=TRUE means exactly that; the only
	 * question is whether it will be us, or zpl_writepages().
	 */
	zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, pgoff, pglen, for_sync,
	    B_FALSE, for_sync ? zfs_putpage_commit_cb : NULL, pp);

	if (!for_sync) {
		/*
		 * Async writeback is logged and written to the DMU, so page
		 * can now be unlocked.
		 */
		zfs_page_writeback_done(pp, 0);
	}

	dmu_tx_commit(tx);

	zfs_rangelock_exit(lr);

	if (need_commit) {
		err = zil_commit_flags(zfsvfs->z_log, zp->z_id, ZIL_COMMIT_NOW);
		if (err != 0) {
			zfs_exit(zfsvfs, FTAG);
			return (err);
		}
	}

	dataset_kstats_update_write_kstats(&zfsvfs->z_kstat, pglen);

	zfs_exit(zfsvfs, FTAG);
	return (err);
}

/*
 * Update the system attributes when the inode has been dirtied.  For the
 * moment we only update the mode, atime, mtime, and ctime.
 */
int
zfs_dirty_inode(struct inode *ip, int flags)
{
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	dmu_tx_t	*tx;
	uint64_t	mode, atime[2], mtime[2], ctime[2];
	inode_timespec_t tmp_ts;
	sa_bulk_attr_t	bulk[5];
	int		error = 0;
	int		cnt = 0;

	if (zfs_is_readonly(zfsvfs) || dmu_objset_is_snapshot(zfsvfs->z_os))
		return (0);

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

#ifdef I_DIRTY_TIME
	/*
	 * This is the lazytime semantic introduced in Linux 4.0
	 * This flag will only be called from update_time when lazytime is set.
	 * (Note, I_DIRTY_SYNC will also set if not lazytime)
	 * Fortunately mtime and ctime are managed within ZFS itself, so we
	 * only need to dirty atime.
	 */
	if (flags == I_DIRTY_TIME) {
		zp->z_atime_dirty = B_TRUE;
		goto out;
	}
#endif

	tx = dmu_tx_create(zfsvfs->z_os);

	dmu_tx_hold_sa(tx, zp->z_sa_hdl, ZFS_SEQ_MAY_GROW(zp));
	zfs_sa_upgrade_txholds(tx, zp);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	mutex_enter(&zp->z_lock);
	zp->z_atime_dirty = B_FALSE;

	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_MODE(zfsvfs), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_ATIME(zfsvfs), NULL, &atime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, cnt, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);

	/* Preserve the mode, mtime and ctime provided by the inode */
	tmp_ts = zpl_inode_get_atime(ip);
	ZFS_TIME_ENCODE(&tmp_ts, atime);
	tmp_ts = zpl_inode_get_mtime(ip);
	ZFS_TIME_ENCODE(&tmp_ts, mtime);
	tmp_ts = zpl_inode_get_ctime(ip);
	ZFS_TIME_ENCODE(&tmp_ts, ctime);
	mode = ip->i_mode;

	zp->z_mode = mode;
	/* persist z_seq; callers bump it before zfs_mark_inode_dirty */
	ZFS_PERSIST_SEQ(zp, bulk, cnt);

	ASSERT3S(cnt, <=, ARRAY_SIZE(bulk));
	error = sa_bulk_update(zp->z_sa_hdl, bulk, cnt, tx);
	mutex_exit(&zp->z_lock);

	dmu_tx_commit(tx);
out:
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

void
zfs_inactive(struct inode *ip)
{
	znode_t	*zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	krwlock_t *zti_lock = &zfsvfs->z_teardown_inactive_lock;
	uint64_t atime[2];
	int error;
	int need_unlock = 0;
	boolean_t no_lockdep = B_FALSE;

	/* Only read lock if we haven't already write locked, e.g. rollback */
	if (!RW_WRITE_HELD(zti_lock)) {
		need_unlock = 1;
		/*
		 * kswapd reaches evict_inode() with fs_reclaim held.  Suppress
		 * lockdep only for this reclaim-thread acquire/release pair.
		 */
		no_lockdep = current_is_reclaim_thread();
		if (no_lockdep)
			rw_enter_nolockdep(zti_lock, RW_READER);
		else
			rw_enter(zti_lock, RW_READER);
	}
	if (zp->z_sa_hdl == NULL) {
		if (need_unlock) {
			if (no_lockdep)
				rw_exit_nolockdep(zti_lock);
			else
				rw_exit(zti_lock);
		}
		return;
	}

	if (zp->z_atime_dirty && zp->z_unlinked == B_FALSE) {
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);

		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			inode_timespec_t tmp_atime;
			tmp_atime = zpl_inode_get_atime(ip);
			ZFS_TIME_ENCODE(&tmp_atime, atime);
			mutex_enter(&zp->z_lock);
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_ATIME(zfsvfs),
			    (void *)&atime, sizeof (atime), tx);
			zp->z_atime_dirty = B_FALSE;
			mutex_exit(&zp->z_lock);
			dmu_tx_commit(tx);
		}
	}

	zfs_zinactive(zp);
	if (need_unlock) {
		if (no_lockdep)
			rw_exit_nolockdep(zti_lock);
		else
			rw_exit(zti_lock);
	}
}

/*
 * Fill pages with data from the disk.
 */
static int
zfs_fillpage(struct inode *ip, struct page *pp)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	loff_t i_size = i_size_read(ip);
	u_offset_t io_off = page_offset(pp);
	size_t io_len = PAGE_SIZE;

	/*
	 * The page may be faulted in after the file has been truncated.
	 * There is no data to read; just zero-fill the page.
	 */
	if (io_off >= i_size) {
		void *zva = kmap(pp);
		memset(zva, 0, PAGE_SIZE);
		kunmap(pp);
		ClearPageError(pp);
		SetPageUptodate(pp);
		return (0);
	}

	if (io_off + io_len > i_size)
		io_len = i_size - io_off;

	void *va = kmap(pp);
	int error = dmu_read(zfsvfs->z_os, zp->z_id, io_off,
	    io_len, va, DMU_READ_PREFETCH);
	if (io_len != PAGE_SIZE)
		memset((char *)va + io_len, 0, PAGE_SIZE - io_len);
	kunmap(pp);

	if (error) {
		/* convert checksum errors into IO errors */
		if (error == ECKSUM)
			error = SET_ERROR(EIO);

		SetPageError(pp);
		ClearPageUptodate(pp);
	} else {
		ClearPageError(pp);
		SetPageUptodate(pp);
	}

	return (error);
}

/*
 * Uses zfs_fillpage to read data from the file and fill the page.
 *
 *	IN:	ip	 - inode of file to get data from.
 *		pp	 - page to read
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	vp - atime updated
 */
int
zfs_getpage(struct inode *ip, struct page *pp)
{
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	znode_t *zp = ITOZ(ip);
	int error;
	loff_t i_size = i_size_read(ip);
	u_offset_t io_off = page_offset(pp);
	size_t io_len = PAGE_SIZE;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	/*
	 * If the page lies entirely at or beyond EOF (e.g. it raced a
	 * truncate) just lock the page and let zfs_fillpage() re-check
	 * i_size under the range lock and zero-fill it.
	 */
	if (io_off >= i_size)
		io_len = PAGE_SIZE;
	else if (io_off + io_len > i_size)
		io_len = i_size - io_off;

	/*
	 * It is important to hold the rangelock here because it is possible
	 * a Direct I/O write or block clone might be taking place at the same
	 * time that a page is being faulted in through filemap_fault(). With
	 * Direct I/O writes and block cloning db->db_data will be set to NULL
	 * with dbuf_clear_data() in dmu_buif_will_clone_or_dio(). If the
	 * rangelock is not held, then there is a race between faulting in a
	 * page and writing out a Direct I/O write or block cloning. Without
	 * the rangelock a NULL pointer dereference can occur in
	 * dmu_read_impl() for db->db_data during the mempcy operation when
	 * zfs_fillpage() calls dmu_read().
	 */
	zfs_locked_range_t *lr = zfs_rangelock_tryenter(&zp->z_rangelock,
	    io_off, io_len, RL_READER);
	if (lr == NULL) {
		/*
		 * It is important to drop the page lock before grabbing the
		 * rangelock to avoid another deadlock between here and
		 * zfs_write() -> update_pages(). update_pages() holds both the
		 * rangelock and the page lock.
		 */
		get_page(pp);
		unlock_page(pp);
		lr = zfs_rangelock_enter(&zp->z_rangelock, io_off,
		    io_len, RL_READER);
		lock_page(pp);
		put_page(pp);
	}
	error = zfs_fillpage(ip, pp);
	zfs_rangelock_exit(lr);

	if (error == 0)
		dataset_kstats_update_read_kstats(&zfsvfs->z_kstat, PAGE_SIZE);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Check ZFS specific permissions to memory map a section of a file.
 *
 *	IN:	ip	- inode of the file to mmap
 *		off	- file offset
 *		addrp	- start address in memory region
 *		len	- length of memory region
 *		vm_flags- address flags
 *
 *	RETURN:	0 if success
 *		error code if failure
 */
int
zfs_map(struct inode *ip, offset_t off, caddr_t *addrp, size_t len,
    unsigned long vm_flags)
{
	(void) addrp;
	znode_t  *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if ((vm_flags & VM_WRITE) && (vm_flags & VM_SHARED) &&
	    (zp->z_pflags & (ZFS_IMMUTABLE | ZFS_READONLY | ZFS_APPENDONLY))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if ((vm_flags & (VM_READ | VM_EXEC)) &&
	    (zp->z_pflags & ZFS_AV_QUARANTINED)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EACCES));
	}

	if (off < 0 || len > MAXOFFSET_T - off) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENXIO));
	}

	zfs_exit(zfsvfs, FTAG);
	return (0);
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

int
zfs_fid(struct inode *ip, fid_t *fidp)
{
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	uint32_t	gen;
	uint64_t	gen64;
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		size, i, error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (fidp->fid_len < SHORT_FID_LEN) {
		fidp->fid_len = SHORT_FID_LEN;
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOSPC));
	}

	if ((error = zfs_verify_zp(zp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs),
	    &gen64, sizeof (uint64_t))) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	gen = (uint32_t)gen64;

	size = SHORT_FID_LEN;

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = size;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* Must have a non-zero generation number to distinguish from .zfs */
	if (gen == 0)
		gen = 1;
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(zfs_open);
EXPORT_SYMBOL(zfs_close);
EXPORT_SYMBOL(zfs_lookup);
EXPORT_SYMBOL(zfs_create);
EXPORT_SYMBOL(zfs_tmpfile);
EXPORT_SYMBOL(zfs_remove);
EXPORT_SYMBOL(zfs_mkdir);
EXPORT_SYMBOL(zfs_rmdir);
EXPORT_SYMBOL(zfs_readdir);
EXPORT_SYMBOL(zfs_getattr_fast);
EXPORT_SYMBOL(zfs_setattr);
EXPORT_SYMBOL(zfs_rename);
EXPORT_SYMBOL(zfs_symlink);
EXPORT_SYMBOL(zfs_readlink);
EXPORT_SYMBOL(zfs_link);
EXPORT_SYMBOL(zfs_inactive);
EXPORT_SYMBOL(zfs_space);
EXPORT_SYMBOL(zfs_fid);
EXPORT_SYMBOL(zfs_getpage);
EXPORT_SYMBOL(zfs_putpage);
EXPORT_SYMBOL(zfs_dirty_inode);
EXPORT_SYMBOL(zfs_map);

module_param(zfs_delete_blocks, ulong, 0644);
MODULE_PARM_DESC(zfs_delete_blocks, "Delete files larger than N blocks async");
#endif

/*
 * =========================================================================
 * Async Direct I/O
 * =========================================================================
 *
 * Submits O_DIRECT reads and writes to the ZIO pipeline and returns
 * -EIOCBQUEUED to the Linux VFS so the submitting kworker can service
 * other io_uring / libaio requests.  Completion is signalled via
 * kiocb->ki_complete() from ZIO taskq context.
 *
 * Reads:  pin user pages into an ABD → dmu_read_abd_async().
 *         The ABD is freed in the callback after DMA completes.
 *
 * Writes: copy user data into a kernel-linear ABD (avoids FOLL_LONGTERM
 *         pinning issues on RHEL/mainline ≥ 6.0) → dmu_write_abd_async().
 *         On ZIO completion, metadata updates and transaction commit run
 *         on system_taskq in process context.
 */

/*
 * Dedicated caches for async read/write callback structs.
 * Eliminates per-I/O kmalloc fast-path overhead and improves
 * CPU cache locality by grouping same-type objects together.
 */
static kmem_cache_t *zfs_async_read_cb_cache = NULL;
static kmem_cache_t *zfs_async_write_cb_cache = NULL;

/*
 * Completion callback for zfs_read_async().  Invoked from ZIO taskq
 * context when all blocks have been read.  Cleans up the range lock,
 * DIO pages, atime, and signals completion via kiocb->ki_complete().
 */
struct zfs_async_read_cb
{
	struct kiocb	*kiocb;
	znode_t		*zp;
	zfsvfs_t	*zfsvfs;
	zfs_locked_range_t *lr;
	zfs_uio_t	uio;
	ssize_t		start_resid;
	abd_t		*data;	/* ABD wrapping user pages; freed in cb */
};

/*
 * Async DIO in-flight accounting.  The submitting thread holds the
 * teardown lock (zfs_enter) only across submission; completions run in
 * ZIO/system taskq threads, which must NOT release a teardown reader
 * lock acquired by another thread: once a teardown writer is waiting
 * (rr_writer_wanted -- umount, export, rollback, recv -F), rrwlock
 * tracks new readers on the submitting thread's rrn list, and a
 * cross-thread rrw_exit() misses that node and corrupts the anon/linked
 * reader counts (refcount VERIFY panic, or the teardown writer waits
 * forever).  Instead, each submitted async operation holds
 * z_async_dio_inflight until its completion has made its last touch of
 * the zfsvfs/znode, and zfsvfs_teardown() drains the counter right
 * after taking the teardown lock as writer -- at which point no new
 * async I/O can be submitted.
 */
static void
zfs_async_dio_hold(zfsvfs_t *zfsvfs)
{
	mutex_enter(&zfsvfs->z_async_dio_lock);
	zfsvfs->z_async_dio_inflight++;
	mutex_exit(&zfsvfs->z_async_dio_lock);
}

static void
zfs_async_dio_rele(zfsvfs_t *zfsvfs)
{
	mutex_enter(&zfsvfs->z_async_dio_lock);
	ASSERT3U(zfsvfs->z_async_dio_inflight, >, 0);
	if (--zfsvfs->z_async_dio_inflight == 0)
		cv_broadcast(&zfsvfs->z_async_dio_cv);
	mutex_exit(&zfsvfs->z_async_dio_lock);
}

static void
zfs_async_read_complete(void *arg, int error)
{
	struct zfs_async_read_cb *cb = arg;
	ssize_t read;

	/*
	 * For the DIO (ABD) path, dmu_read_abd_async() writes data directly
	 * to the user pages via DMA, so uio_resid is never decremented.
	 * On success, report the full requested size as the bytes read.
	 * On checksum error we report the error to the caller.
	 */
	if (error == 0)
		read = cb->start_resid;
	else
		read = 0;

	/*
	 * A Direct I/O read that fails checksum verification is suspect:
	 * a concurrent writer may have modified the buffer in flight. The
	 * synchronous path reissues the read through the ARC and, if that
	 * also fails, returns EIO (see zfs_read()). This completion runs in
	 * ZIO taskq context without the submitter's mm and cannot re-copy
	 * ARC data into the pinned user pages, so it cannot retry; convert
	 * ECKSUM to EIO so the internal error code never leaks to userspace
	 * and the result matches the sync path for genuinely corrupt data.
	 * (Transient races the sync path would recover via the ARC retry
	 * are not recovered here -- see harness tests/b4-arc-fallback.)
	 */
	if (error == ECKSUM)
		error = SET_ERROR(EIO);

	dataset_kstats_update_read_kstats(&cb->zfsvfs->z_kstat, read);
	zfs_rangelock_exit(cb->lr);

	/*
	 * Unpin DIO pages — DMA is complete (or failed).
	 */
	if (cb->uio.uio_extflg & UIO_DIRECT)
		zfs_uio_free_dio_pages(&cb->uio, UIO_READ);

	ZFS_ACCESSTIME_STAMP(cb->zfsvfs, cb->zp);

	/*
	 * Last touch of the zfsvfs/znode: drop the in-flight hold that
	 * has kept zfsvfs_teardown() at bay since submission (the
	 * teardown reader lock itself was released by the submitting
	 * thread; see zfs_async_dio_hold()).
	 */
	zfs_async_dio_rele(cb->zfsvfs);

	/*
	 * Free the ABD that wraps the user pages.  The data is now in the
	 * user pages, and the pages are unpinned by zfs_uio_free_dio_pages()
	 * above.  In the synchronous path, the caller frees the ABD after
	 * zio_wait() returns; here we must do it in the callback.
	 */
	if (cb->data != NULL)
		abd_free(cb->data);

#ifdef HAVE_2ARGS_KI_COMPLETE
	cb->kiocb->ki_complete(cb->kiocb, error ? -error : read);
#else
	cb->kiocb->ki_complete(cb->kiocb, error ? -error : read, 0);
#endif
	kmem_cache_free(zfs_async_read_cb_cache, cb);
}

/*
 * Async Direct I/O read.  Pins user pages (requires current->mm),
 * submits reads via dmu_read_abd_async(), and returns 0 on success.
 * The caller should then return -EIOCBQUEUED to the VFS.
 *
 * On I/O completion, zfs_async_read_complete() handles cleanup and
 * calls kiocb->ki_complete().
 *
 * Returns 0 on successful async submission, or positive errno if the
 * read must be done synchronously.
 */
int
zfs_read_async(znode_t *zp, zfs_uio_t *uio, int ioflag, cred_t *cr,
    struct kiocb *kiocb)
{
	(void) cr;
	int error;
	ssize_t n;

	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EACCES));
	}
	if (Z_ISDIR(ZTOTYPE(zp))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EISDIR));
	}

	/* Pin user pages for DIO — requires current->mm (we're in kworker) */
	error = zfs_setup_direct(zp, uio, UIO_READ, &ioflag);
	if (error || !(uio->uio_extflg & UIO_DIRECT)) {
		zfs_exit(zfsvfs, FTAG);
		return (error ? error : SET_ERROR(EOPNOTSUPP));
	}

	zfs_locked_range_t *lr = zfs_rangelock_enter(&zp->z_rangelock,
	    zfs_uio_offset(uio), zfs_uio_resid(uio), RL_READER);

	if (zfs_uio_offset(uio) >= zp->z_size) {
		/*
		 * File truncated between caller's i_size_read() guard
		 * and our range lock.  No data to read and no ZIO
		 * queued, so return an error — the caller falls
		 * through to sync rather than returning -EIOCBQUEUED.
		 */
		zfs_rangelock_exit(lr);
		zfs_uio_free_dio_pages(uio, UIO_READ);
		ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	n = MIN(zfs_uio_resid(uio), zp->z_size - zfs_uio_offset(uio));
	ssize_t aligned_n = P2ALIGN_TYPED(n, PAGE_SIZE, ssize_t);
	if (aligned_n == 0) {
		zfs_rangelock_exit(lr);
		zfs_uio_free_dio_pages(uio, UIO_READ);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	/* Build ABD from pinned pages */
	offset_t offset = zfs_uio_offset(uio);
	offset_t page_idx = (offset - zfs_uio_soffset(uio)) >> PAGESHIFT;
	ASSERT3U(page_idx, <, uio->uio_dio.npages);
	abd_t *data = abd_alloc_from_pages(&uio->uio_dio.pages[page_idx],
	    offset & (PAGESIZE - 1), aligned_n);

	dmu_flags_t dflags = DMU_READ_PREFETCH | DMU_DIRECTIO;
	if (ioflag & O_DIRECT)
		dflags |= DMU_UNCACHEDIO;

	struct zfs_async_read_cb *cb =
	    kmem_cache_alloc(zfs_async_read_cb_cache, KM_SLEEP);
	cb->kiocb = kiocb;
	cb->zp = zp;
	cb->zfsvfs = zfsvfs;
	cb->lr = lr;
	cb->uio = *uio;
	cb->uio.uio_resid = aligned_n;
	cb->start_resid = aligned_n;
	cb->data = data;

	/*
	 * Held until the completion's last touch of the zfsvfs; taken
	 * before submission because the completion may fire (and rele)
	 * before dmu_read_abd_async() returns.  A nonzero return means
	 * the callback will never run, so the error path must rele.
	 */
	zfs_async_dio_hold(zfsvfs);

	dmu_buf_t *db = sa_get_db(zp->z_sa_hdl);
	error = dmu_read_abd_async(DB_DNODE((dmu_buf_impl_t *)db),
	    offset, aligned_n, data, dflags, zfs_async_read_complete, cb);

	if (error) {
		zfs_async_dio_rele(zfsvfs);
		abd_free(data);
		zfs_rangelock_exit(lr);
		zfs_uio_free_dio_pages(uio, UIO_READ);
		zfs_exit(zfsvfs, FTAG);
		kmem_cache_free(zfs_async_read_cb_cache, cb);
		return (error);
	}

	/*
	 * The in-flight hold now stands in for the teardown reader lock,
	 * which must be released here, on the thread that acquired it.
	 */
	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * Callback state for zfs_write_async().  Carries everything needed to
 * complete the write once the ZIOs finish.
 */
struct zfs_async_write_cb {
	struct kiocb	*kiocb;
	znode_t		*zp;
	zfsvfs_t	*zfsvfs;
	zfs_locked_range_t *lr;
	zfs_uio_t	uio;
	ssize_t		start_resid;
	boolean_t	dio;
	abd_t		*data;
	dmu_tx_t	*tx;
	offset_t	woff;
	ssize_t		tx_bytes;
	sa_bulk_attr_t	bulk[4];
	int		bulk_count;
	uint64_t	mtime[2];
	uint64_t	ctime[2];
	uint64_t	clear_setid_bits_txg;
	cred_t		*cr;
	boolean_t	do_commit;
	int		error;
	ssize_t		wrote;
};

static void
zfs_async_write_task(void *arg)
{
	struct zfs_async_write_cb *cb = arg;
	zilog_t *zilog = cb->zfsvfs->z_log;

	if (cb->error == 0) {
		if (cb->cr != NULL) {
			zfs_clear_setid_bits_if_necessary(cb->zfsvfs, cb->zp,
			    cb->cr, &cb->clear_setid_bits_txg, cb->tx);
		}

		zfs_tstamp_update_setup(cb->zp, CONTENT_MODIFIED,
		    cb->mtime, cb->ctime);

		uint64_t end_size;
		while ((end_size = cb->zp->z_size) <
		    (uint64_t)(cb->woff + cb->wrote))
			(void) atomic_cas_64(&cb->zp->z_size, end_size,
			    (uint64_t)(cb->woff + cb->wrote));

		(void) sa_bulk_update(cb->zp->z_sa_hdl, cb->bulk,
		    cb->bulk_count, cb->tx);

		zfs_log_write(zilog, cb->tx, TX_WRITE, cb->zp, cb->woff,
		    cb->wrote, cb->do_commit, cb->dio, NULL, NULL);

		dmu_tx_commit(cb->tx);
	} else {
		/*
		 * The tx was assigned at submit time and must be committed
		 * even though the write failed: dmu_tx_abort() VERIFYs
		 * tx_txg == 0 and panics on an assigned tx.  The
		 * synchronous path likewise commits on a post-assign write
		 * error (see zfs_write()); committing with no changes is
		 * legal, and metadata updates and zfs_log_write are
		 * correctly skipped above.
		 */
		dmu_tx_commit(cb->tx);
		cb->wrote = 0;
	}

	dataset_kstats_update_write_kstats(&cb->zfsvfs->z_kstat, cb->wrote);
	zfs_znode_update_vfs(cb->zp);
	zfs_rangelock_exit(cb->lr);
	if (cb->uio.uio_extflg & UIO_DIRECT)
		zfs_uio_free_dio_pages(&cb->uio, UIO_WRITE);

	/* zil_commit before teardown lock release (matches zfs_write()) */
	if (cb->error == 0 && cb->do_commit) {
		int zil_err = zil_commit(zilog, cb->zp->z_id);
		if (zil_err != 0 && cb->error == 0)
			cb->error = zil_err;
	}

	/*
	 * Last touch of the zfsvfs/znode: drop the in-flight hold that
	 * has kept zfsvfs_teardown() at bay since submission (the
	 * teardown reader lock itself was released by the submitting
	 * thread; see zfs_async_dio_hold()).  Must come after the
	 * zil_commit above so teardown's zil_close() cannot race it.
	 */
	zfs_async_dio_rele(cb->zfsvfs);

	if (cb->data != NULL)
		abd_free(cb->data);
	if (cb->cr != NULL)
		crfree(cb->cr);

#ifdef HAVE_2ARGS_KI_COMPLETE
	cb->kiocb->ki_complete(cb->kiocb,
	    cb->error ? -cb->error : cb->wrote);
#else
	cb->kiocb->ki_complete(cb->kiocb,
	    cb->error ? -cb->error : cb->wrote, 0);
#endif
	kmem_cache_free(zfs_async_write_cb_cache, cb);
}

/*
 * Completion callback for dmu_write_abd_async().  Called from ZIO
 * taskq context when all writes finish.  Dispatches to system_taskq
 * for metadata operations, transaction commit, cleanup, and ki_complete
 * — all in process context, not ZIO taskq.
 */
static void
zfs_async_write_complete(void *arg, int error)
{
	struct zfs_async_write_cb *cb = arg;

	cb->wrote = cb->start_resid - cb->uio.uio_resid;
	cb->error = error;

	/*
	 * TQ_SLEEP dispatch sleeps for memory rather than failing
	 * (task_alloc() falls through to kmem_alloc(KM_SLEEP)); the only
	 * TASKQID_INVALID return is from a taskq being destroyed, which
	 * cannot happen to system_taskq while this module is loaded.
	 * Do not fall back to running the task inline: this is ZIO
	 * completion context, where zil_commit() can self-deadlock, and
	 * clobbering the error code of a successful write would report
	 * failure for data that is already on disk.
	 */
	VERIFY3U(taskq_dispatch(system_taskq, zfs_async_write_task,
	    cb, TQ_SLEEP), !=, TASKQID_INVALID);
}

/*
 * Async Direct I/O write.  Copies user data into a kernel ABD, acquires
 * locks, creates a transaction, and dispatches writes via
 * dmu_write_abd_async().  The caller returns -EIOCBQUEUED to the VFS.
 *
 * On I/O completion, zfs_async_write_complete() fires from ZIO taskq
 * and dispatches to system_taskq for metadata operations and cleanup
 * in process context.
 */
int
zfs_write_async(znode_t *zp, zfs_uio_t *uio, int ioflag, cred_t *cr,
    struct kiocb *kiocb)
{
	int error;
	ssize_t n;

	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EACCES));
	}
	if (Z_ISDIR(ZTOTYPE(zp))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EISDIR));
	}
	if (zfs_is_readonly(zfsvfs)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EROFS));
	}
	if ((zp->z_pflags & ZFS_IMMUTABLE) ||
	    ((zp->z_pflags & ZFS_APPENDONLY) && !(ioflag & O_APPEND) &&
	    (zfs_uio_offset(uio) < zp->z_size))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	offset_t woff = ioflag & O_APPEND ?
	    zp->z_size : zfs_uio_offset(uio);
	if (woff < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Async Direct I/O writes copy user data into a kernel ABD
	 * instead of pinning user pages via pin_user_pages_unlocked().
	 * On RHEL kernels (and mainline >= 6.0), all pin_user_pages*()
	 * variants implicitly add FOLL_LONGTERM which fails with ENOMEM
	 * under concurrent I/O (iodepth=64 → 2048 concurrently held
	 * pages).  Copying is safe on all kernel versions and the memcpy
	 * cost is negligible compared to disk I/O latency.
	 */
	n = zfs_uio_resid(uio);
	if (n == 0) {
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	n = P2ALIGN_TYPED(n, PAGE_SIZE, ssize_t);
	if (n == 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	/*
	 * Replicate DIO eligibility checks from zfs_setup_direct().
	 * Any condition that would skip DIO in the sync path returns
	 * EOPNOTSUPP so the caller falls back to zfs_write().  The
	 * sync path will then re-check everything including zfs_dio_strict.
	 */
	if (zfsvfs->z_os->os_direct == ZFS_DIRECT_ALWAYS)
		ioflag |= O_DIRECT;

	if (!zfs_dio_enabled ||
	    zfsvfs->z_os->os_direct == ZFS_DIRECT_DISABLED) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	if (!zfs_uio_page_aligned(uio) ||
	    !zfs_uio_aligned(uio, PAGE_SIZE)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	if (n < zp->z_blksz ||
	    zn_has_cached_data(zp, zfs_uio_offset(uio),
	    zfs_uio_offset(uio) + n - 1)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	zfs_locked_range_t *lr;
	if (ioflag & O_APPEND) {
		lr = zfs_rangelock_enter(&zp->z_rangelock, 0, n, RL_APPEND);
		woff = lr->lr_offset;
		if (lr->lr_length == UINT64_MAX)
			woff = zp->z_size;
		zfs_uio_setoffset(uio, woff);
	} else {
		lr = zfs_rangelock_enter(&zp->z_rangelock, woff, n, RL_WRITER);
	}

	if (zn_rlimit_fsize_uio(zp, uio)) {
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EFBIG));
	}

	if (lr->lr_length == UINT64_MAX) {
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	if (woff >= MAXOFFSET_T) {
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EFBIG));
	}
	if ((uint64_t)n > MAXOFFSET_T - woff)
		n = MAXOFFSET_T - woff;
	n = MIN(n, zfs_uio_resid(uio));
	n = P2ALIGN_TYPED(n, PAGE_SIZE, ssize_t);
	if (n == 0) {
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	/*
	 * Direct I/O writes are dispatched per-dbuf by
	 * dmu_write_abd_dispatch(), which slices the source ABD at
	 * (db_offset - offset) assuming every covered dbuf lies fully
	 * within [offset, offset+n).  That only holds for a write that
	 * is block-aligned in both offset and length.  The synchronous
	 * path enforces this in dmu_write_uio_dnode() via
	 * zfs_dio_aligned() and routes any unaligned span through the
	 * ARC.  Mirror that gate here: for a block-misaligned write,
	 * fall back to the sync path (EOPNOTSUPP) rather than dispatch
	 * a slice whose per-dbuf offset math underflows and trips the
	 * VERIFY in abd_get_offset_size().
	 */
	if (!zfs_dio_aligned(woff, n, zp->z_blksz)) {
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EOPNOTSUPP));
	}

	/*
	 * Allocate a kernel ABD to hold the user data.  The actual
	 * copy (zfs_uiomove) is deferred until after dmu_tx_assign
	 * succeeds: if the assign fails we must return to the caller
	 * with uio untouched so the sync fallback in zpl_iter_write()
	 * can retry the full write.
	 *
	 * We copy rather than pin user pages to avoid FOLL_LONGTERM
	 * issues on RHEL and mainline >= 6.0.
	 */
	offset_t offset = zfs_uio_offset(uio);
	abd_t *data = abd_alloc_linear(n, B_FALSE);

	boolean_t do_commit = (ioflag & (O_SYNC | O_DSYNC)) ||
	    (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS);
	dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)sa_get_db(zp->z_sa_hdl);
	DB_DNODE_ENTER(db);
	dmu_tx_hold_write_by_dnode(tx, DB_DNODE(db), woff, n);
	DB_DNODE_EXIT(db);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		abd_free(data);
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Copy user data into the kernel ABD.  Must be done AFTER
	 * dmu_tx_assign so uio stays intact on assign failure for
	 * the sync fallback.
	 *
	 * Save the full uio before the copy for restore if errors.
	 */
	zfs_uio_t saved_uio = *uio;

	error = zfs_uiomove(abd_to_buf(data), n, UIO_WRITE, uio);
	if (error) {
		/*
		 * The copy faulted partway.  zfs_uiomove() advanced the
		 * underlying iov_iter by the bytes consumed; rewind it
		 * before restoring the saved uio so the sync fallback in
		 * zpl_iter_write() retries from the original position (a
		 * bare struct restore leaves the shared iov_iter advanced).
		 * The tx is already assigned, so it must be committed, not
		 * aborted -- dmu_tx_abort() VERIFYs tx_txg == 0 and would
		 * panic; the synchronous path likewise commits on EFAULT.
		 */
		zfs_uio_iov_iter_revert(uio,
		    saved_uio.uio_resid - uio->uio_resid);
		*uio = saved_uio;
		dmu_tx_commit(tx);
		abd_free(data);
		zfs_rangelock_exit(lr);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	dmu_flags_t dflags = DMU_DIRECTIO;
	if (ioflag & O_DIRECT)
		dflags |= DMU_UNCACHEDIO;

	struct zfs_async_write_cb *cb =
	    kmem_cache_alloc(zfs_async_write_cb_cache, KM_SLEEP);
	cb->kiocb = kiocb;
	cb->zp = zp;
	cb->zfsvfs = zfsvfs;
	cb->lr = lr;
	cb->uio = *uio;
	cb->uio.uio_resid = 0;		/* all data already copied to ABD */
	cb->start_resid = n;
	cb->dio = B_FALSE;
	cb->data = data;
	cb->tx = tx;
	cb->woff = woff;
	cb->tx_bytes = n;
	cb->cr = cr;
	crhold(cr);

	cb->bulk_count = 0;
	SA_ADD_BULK_ATTR(cb->bulk, cb->bulk_count,
	    SA_ZPL_MTIME(zfsvfs), NULL, &cb->mtime, 16);
	SA_ADD_BULK_ATTR(cb->bulk, cb->bulk_count,
	    SA_ZPL_CTIME(zfsvfs), NULL, &cb->ctime, 16);
	SA_ADD_BULK_ATTR(cb->bulk, cb->bulk_count,
	    SA_ZPL_SIZE(zfsvfs), NULL, &zp->z_size, 8);
	SA_ADD_BULK_ATTR(cb->bulk, cb->bulk_count,
	    SA_ZPL_FLAGS(zfsvfs), NULL, &zp->z_pflags, 8);

	cb->clear_setid_bits_txg = 0;
	cb->do_commit = do_commit;
	cb->error = 0;
	cb->wrote = 0;

	/*
	 * Held until the completion's last touch of the zfsvfs; taken
	 * before submission because the completion may fire (and rele)
	 * before dmu_write_abd_async() returns.  A nonzero return means
	 * the callback will never run, so the error path must rele.
	 */
	zfs_async_dio_hold(zfsvfs);

	error = dmu_write_abd_async(DB_DNODE(db), offset, n, data, dflags,
	    tx, zfs_async_write_complete, cb);
	if (error) {
		/*
		 * Restore the uio so the sync fallback in zpl_iter_write()
		 * retries the full write.  zfs_uiomove() already consumed
		 * the user data into the ABD, so rewind the shared iov_iter
		 * before the struct restore; otherwise the fallback sees an
		 * exhausted iterator and fails a write whose data was fine.
		 * The tx is assigned -- commit, don't abort (abort VERIFYs
		 * tx_txg == 0).
		 */
		zfs_async_dio_rele(zfsvfs);
		zfs_uio_iov_iter_revert(uio,
		    saved_uio.uio_resid - uio->uio_resid);
		*uio = saved_uio;
		crfree(cr);
		abd_free(data);
		zfs_rangelock_exit(lr);
		dmu_tx_commit(tx);
		zfs_exit(zfsvfs, FTAG);
		kmem_cache_free(zfs_async_write_cb_cache, cb);
		return (error);
	}

	/*
	 * The in-flight hold now stands in for the teardown reader lock,
	 * which must be released here, on the thread that acquired it.
	 */
	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * Module init: create dedicated caches for async I/O callbacks.
 * Called once from zfs_kmod_init() — no lazy-init races.
 */
void
zfs_async_dio_init(void)
{
	zfs_async_read_cb_cache = kmem_cache_create("zfs_async_read_cb",
	    sizeof (struct zfs_async_read_cb), 0,
	    NULL, NULL, NULL, NULL, NULL, 0);
	zfs_async_write_cb_cache = kmem_cache_create("zfs_async_write_cb",
	    sizeof (struct zfs_async_write_cb), 0,
	    NULL, NULL, NULL, NULL, NULL, 0);
}

/*
 * Module fini: destroy caches created by zfs_async_dio_init().
 * Called once from zfs_kmod_fini().
 */
void
zfs_async_dio_fini(void)
{
	if (zfs_async_read_cb_cache != NULL) {
		kmem_cache_destroy(zfs_async_read_cb_cache);
		zfs_async_read_cb_cache = NULL;
	}
	if (zfs_async_write_cb_cache != NULL) {
		kmem_cache_destroy(zfs_async_write_cb_cache);
		zfs_async_write_cb_cache = NULL;
	}
}
