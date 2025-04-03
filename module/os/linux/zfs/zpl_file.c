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
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */


#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/fs.h>
#include <linux/migrate.h>
#include <sys/file.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_project.h>
#if defined(HAVE_VFS_SET_PAGE_DIRTY_NOBUFFERS) || \
    defined(HAVE_VFS_FILEMAP_DIRTY_FOLIO)
#include <linux/pagemap.h>
#endif
#include <linux/fadvise.h>
#ifdef HAVE_VFS_FILEMAP_DIRTY_FOLIO
#include <linux/writeback.h>
#endif

/*
 * When using fallocate(2) to preallocate space, inflate the requested
 * capacity check by 10% to account for the required metadata blocks.
 */
static unsigned int zfs_fallocate_reserve_percent = 110;

static int
zpl_open(struct inode *ip, struct file *filp)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	error = generic_file_open(ip, filp);
	if (error)
		return (error);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_open(ip, filp->f_mode, filp->f_flags, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_release(struct inode *ip, struct file *filp)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	if (ITOZ(ip)->z_atime_dirty)
		zfs_mark_inode_dirty(ip);

	crhold(cr);
	error = -zfs_close(ip, filp->f_flags, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_iterate(struct file *filp, struct dir_context *ctx)
{
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_readdir(file_inode(filp), ctx, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	znode_t *zp = ITOZ(inode);
	zfsvfs_t *zfsvfs = ITOZSB(inode);
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	/*
	 * The variables z_sync_writes_cnt and z_async_writes_cnt work in
	 * tandem so that sync writes can detect if there are any non-sync
	 * writes going on and vice-versa. The "vice-versa" part to this logic
	 * is located in zfs_putpage() where non-sync writes check if there are
	 * any ongoing sync writes. If any sync and non-sync writes overlap,
	 * we do a commit to complete the non-sync writes since the latter can
	 * potentially take several seconds to complete and thus block sync
	 * writes in the upcoming call to filemap_write_and_wait_range().
	 */
	atomic_inc_32(&zp->z_sync_writes_cnt);
	/*
	 * If the following check does not detect an overlapping non-sync write
	 * (say because it's just about to start), then it is guaranteed that
	 * the non-sync write will detect this sync write. This is because we
	 * always increment z_sync_writes_cnt / z_async_writes_cnt before doing
	 * the check on z_async_writes_cnt / z_sync_writes_cnt here and in
	 * zfs_putpage() respectively.
	 */
	if (atomic_load_32(&zp->z_async_writes_cnt) > 0) {
		if ((error = zpl_enter(zfsvfs, FTAG)) != 0) {
			atomic_dec_32(&zp->z_sync_writes_cnt);
			return (error);
		}
		zil_commit(zfsvfs->z_log, zp->z_id);
		zpl_exit(zfsvfs, FTAG);
	}

	error = filemap_write_and_wait_range(inode->i_mapping, start, end);

	/*
	 * The sync write is not complete yet but we decrement
	 * z_sync_writes_cnt since zfs_fsync() increments and decrements
	 * it internally. If a non-sync write starts just after the decrement
	 * operation but before we call zfs_fsync(), it may not detect this
	 * overlapping sync write but it does not matter since we have already
	 * gone past filemap_write_and_wait_range() and we won't block due to
	 * the non-sync write.
	 */
	atomic_dec_32(&zp->z_sync_writes_cnt);

	if (error)
		return (error);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(zp, datasync, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static inline int
zfs_io_flags(struct kiocb *kiocb)
{
	int flags = 0;

#if defined(IOCB_DSYNC)
	if (kiocb->ki_flags & IOCB_DSYNC)
		flags |= O_DSYNC;
#endif
#if defined(IOCB_SYNC)
	if (kiocb->ki_flags & IOCB_SYNC)
		flags |= O_SYNC;
#endif
#if defined(IOCB_APPEND)
	if (kiocb->ki_flags & IOCB_APPEND)
		flags |= O_APPEND;
#endif
#if defined(IOCB_DIRECT)
	if (kiocb->ki_flags & IOCB_DIRECT)
		flags |= O_DIRECT;
#endif
	return (flags);
}

/*
 * If relatime is enabled, call file_accessed() if zfs_relatime_need_update()
 * is true.  This is needed since datasets with inherited "relatime" property
 * aren't necessarily mounted with the MNT_RELATIME flag (e.g. after
 * `zfs set relatime=...`), which is what relatime test in VFS by
 * relatime_need_update() is based on.
 */
static inline void
zpl_file_accessed(struct file *filp)
{
	struct inode *ip = filp->f_mapping->host;

	if (!IS_NOATIME(ip) && ITOZSB(ip)->z_relatime) {
		if (zfs_relatime_need_update(ip))
			file_accessed(filp);
	} else {
		file_accessed(filp);
	}
}

static ssize_t
zpl_iter_read(struct kiocb *kiocb, struct iov_iter *to)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct file *filp = kiocb->ki_filp;
	ssize_t count = iov_iter_count(to);
	zfs_uio_t uio;

	zfs_uio_iov_iter_init(&uio, to, kiocb->ki_pos, count, 0);

	crhold(cr);
	cookie = spl_fstrans_mark();

	ssize_t ret = -zfs_read(ITOZ(filp->f_mapping->host), &uio,
	    filp->f_flags | zfs_io_flags(kiocb), cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (ret < 0)
		return (ret);

	ssize_t read = count - uio.uio_resid;
	kiocb->ki_pos += read;

	zpl_file_accessed(filp);

	return (read);
}

static inline ssize_t
zpl_generic_write_checks(struct kiocb *kiocb, struct iov_iter *from,
    size_t *countp)
{
	ssize_t ret = generic_write_checks(kiocb, from);
	if (ret <= 0)
		return (ret);

	*countp = ret;

	return (0);
}

static ssize_t
zpl_iter_write(struct kiocb *kiocb, struct iov_iter *from)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct file *filp = kiocb->ki_filp;
	struct inode *ip = filp->f_mapping->host;
	zfs_uio_t uio;
	size_t count = 0;
	ssize_t ret;

	ret = zpl_generic_write_checks(kiocb, from, &count);
	if (ret)
		return (ret);

	zfs_uio_iov_iter_init(&uio, from, kiocb->ki_pos, count,
	    from->iov_offset);

	crhold(cr);
	cookie = spl_fstrans_mark();

	ret = -zfs_write(ITOZ(ip), &uio,
	    filp->f_flags | zfs_io_flags(kiocb), cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (ret < 0)
		return (ret);

	ssize_t wrote = count - uio.uio_resid;
	kiocb->ki_pos += wrote;

	return (wrote);
}

static ssize_t
zpl_direct_IO(struct kiocb *kiocb, struct iov_iter *iter)
{
	/*
	 * All O_DIRECT requests should be handled by
	 * zpl_iter_write/read}(). There is no way kernel generic code should
	 * call the direct_IO address_space_operations function. We set this
	 * code path to be fatal if it is executed.
	 */
	PANIC(0);
	return (0);
}

static loff_t
zpl_llseek(struct file *filp, loff_t offset, int whence)
{
#if defined(SEEK_HOLE) && defined(SEEK_DATA)
	fstrans_cookie_t cookie;

	if (whence == SEEK_DATA || whence == SEEK_HOLE) {
		struct inode *ip = filp->f_mapping->host;
		loff_t maxbytes = ip->i_sb->s_maxbytes;
		loff_t error;

		spl_inode_lock_shared(ip);
		cookie = spl_fstrans_mark();
		error = -zfs_holey(ITOZ(ip), whence, &offset);
		spl_fstrans_unmark(cookie);
		if (error == 0)
			error = lseek_execute(filp, ip, offset, maxbytes);
		spl_inode_unlock_shared(ip);

		return (error);
	}
#endif /* SEEK_HOLE && SEEK_DATA */

	return (generic_file_llseek(filp, offset, whence));
}

/*
 * It's worth taking a moment to describe how mmap is implemented
 * for zfs because it differs considerably from other Linux filesystems.
 * However, this issue is handled the same way under OpenSolaris.
 *
 * The issue is that by design zfs bypasses the Linux page cache and
 * leaves all caching up to the ARC.  This has been shown to work
 * well for the common read(2)/write(2) case.  However, mmap(2)
 * is problem because it relies on being tightly integrated with the
 * page cache.  To handle this we cache mmap'ed files twice, once in
 * the ARC and a second time in the page cache.  The code is careful
 * to keep both copies synchronized.
 *
 * When a file with an mmap'ed region is written to using write(2)
 * both the data in the ARC and existing pages in the page cache
 * are updated.  For a read(2) data will be read first from the page
 * cache then the ARC if needed.  Neither a write(2) or read(2) will
 * will ever result in new pages being added to the page cache.
 *
 * New pages are added to the page cache only via .readpage() which
 * is called when the vfs needs to read a page off disk to back the
 * virtual memory region.  These pages may be modified without
 * notifying the ARC and will be written out periodically via
 * .writepage().  This will occur due to either a sync or the usual
 * page aging behavior.  Note because a read(2) of a mmap'ed file
 * will always check the page cache first even when the ARC is out
 * of date correct data will still be returned.
 *
 * While this implementation ensures correct behavior it does have
 * have some drawbacks.  The most obvious of which is that it
 * increases the required memory footprint when access mmap'ed
 * files.  It also adds additional complexity to the code keeping
 * both caches synchronized.
 *
 * Longer term it may be possible to cleanly resolve this wart by
 * mapping page cache pages directly on to the ARC buffers.  The
 * Linux address space operations are flexible enough to allow
 * selection of which pages back a particular index.  The trick
 * would be working out the details of which subsystem is in
 * charge, the ARC, the page cache, or both.  It may also prove
 * helpful to move the ARC buffers to a scatter-gather lists
 * rather than a vmalloc'ed region.
 */
static int
zpl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct inode *ip = filp->f_mapping->host;
	int error;
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	error = -zfs_map(ip, vma->vm_pgoff, (caddr_t *)vma->vm_start,
	    (size_t)(vma->vm_end - vma->vm_start), vma->vm_flags);
	spl_fstrans_unmark(cookie);

	if (error)
		return (error);

	error = generic_file_mmap(filp, vma);
	if (error)
		return (error);

	return (error);
}

/*
 * Populate a page with data for the Linux page cache.  This function is
 * only used to support mmap(2).  There will be an identical copy of the
 * data in the ARC which is kept up to date via .write() and .writepage().
 */
static inline int
zpl_readpage_common(struct page *pp)
{
	fstrans_cookie_t cookie;

	ASSERT(PageLocked(pp));

	cookie = spl_fstrans_mark();
	int error = -zfs_getpage(pp->mapping->host, pp);
	spl_fstrans_unmark(cookie);

	unlock_page(pp);

	return (error);
}

#ifdef HAVE_VFS_READ_FOLIO
static int
zpl_read_folio(struct file *filp, struct folio *folio)
{
	return (zpl_readpage_common(&folio->page));
}
#else
static int
zpl_readpage(struct file *filp, struct page *pp)
{
	return (zpl_readpage_common(pp));
}
#endif

static int
zpl_readpage_filler(void *data, struct page *pp)
{
	return (zpl_readpage_common(pp));
}

/*
 * Populate a set of pages with data for the Linux page cache.  This
 * function will only be called for read ahead and never for demand
 * paging.  For simplicity, the code relies on read_cache_pages() to
 * correctly lock each page for IO and call zpl_readpage().
 */
#ifdef HAVE_VFS_READPAGES
static int
zpl_readpages(struct file *filp, struct address_space *mapping,
    struct list_head *pages, unsigned nr_pages)
{
	return (read_cache_pages(mapping, pages, zpl_readpage_filler, NULL));
}
#else
static void
zpl_readahead(struct readahead_control *ractl)
{
	struct page *page;

	while ((page = readahead_page(ractl)) != NULL) {
		int ret;

		ret = zpl_readpage_filler(NULL, page);
		put_page(page);
		if (ret)
			break;
	}
}
#endif

static int
zpl_putpage(struct page *pp, struct writeback_control *wbc, void *data)
{
	boolean_t *for_sync = data;
	fstrans_cookie_t cookie;
	int ret;

	ASSERT(PageLocked(pp));
	ASSERT(!PageWriteback(pp));

	cookie = spl_fstrans_mark();
	ret = zfs_putpage(pp->mapping->host, pp, wbc, *for_sync);
	spl_fstrans_unmark(cookie);

	return (ret);
}

#ifdef HAVE_WRITEPAGE_T_FOLIO
static int
zpl_putfolio(struct folio *pp, struct writeback_control *wbc, void *data)
{
	return (zpl_putpage(&pp->page, wbc, data));
}
#endif

static inline int
zpl_write_cache_pages(struct address_space *mapping,
    struct writeback_control *wbc, void *data)
{
	int result;

#ifdef HAVE_WRITEPAGE_T_FOLIO
	result = write_cache_pages(mapping, wbc, zpl_putfolio, data);
#else
	result = write_cache_pages(mapping, wbc, zpl_putpage, data);
#endif
	return (result);
}

static int
zpl_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	znode_t		*zp = ITOZ(mapping->host);
	zfsvfs_t	*zfsvfs = ITOZSB(mapping->host);
	enum writeback_sync_modes sync_mode;
	int result;

	if ((result = zpl_enter(zfsvfs, FTAG)) != 0)
		return (result);
	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		wbc->sync_mode = WB_SYNC_ALL;
	zpl_exit(zfsvfs, FTAG);
	sync_mode = wbc->sync_mode;

	/*
	 * We don't want to run write_cache_pages() in SYNC mode here, because
	 * that would make putpage() wait for a single page to be committed to
	 * disk every single time, resulting in atrocious performance. Instead
	 * we run it once in non-SYNC mode so that the ZIL gets all the data,
	 * and then we commit it all in one go.
	 */
	boolean_t for_sync = (sync_mode == WB_SYNC_ALL);
	wbc->sync_mode = WB_SYNC_NONE;
	result = zpl_write_cache_pages(mapping, wbc, &for_sync);
	if (sync_mode != wbc->sync_mode) {
		if ((result = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
			return (result);
		if (zfsvfs->z_log != NULL)
			zil_commit(zfsvfs->z_log, zp->z_id);
		zpl_exit(zfsvfs, FTAG);

		/*
		 * We need to call write_cache_pages() again (we can't just
		 * return after the commit) because the previous call in
		 * non-SYNC mode does not guarantee that we got all the dirty
		 * pages (see the implementation of write_cache_pages() for
		 * details). That being said, this is a no-op in most cases.
		 */
		wbc->sync_mode = sync_mode;
		result = zpl_write_cache_pages(mapping, wbc, &for_sync);
	}
	return (result);
}

/*
 * Write out dirty pages to the ARC, this function is only required to
 * support mmap(2).  Mapped pages may be dirtied by memory operations
 * which never call .write().  These dirty pages are kept in sync with
 * the ARC buffers via this hook.
 */
static int
zpl_writepage(struct page *pp, struct writeback_control *wbc)
{
	if (ITOZSB(pp->mapping->host)->z_os->os_sync == ZFS_SYNC_ALWAYS)
		wbc->sync_mode = WB_SYNC_ALL;

	boolean_t for_sync = (wbc->sync_mode == WB_SYNC_ALL);

	return (zpl_putpage(pp, wbc, &for_sync));
}

/*
 * The flag combination which matches the behavior of zfs_space() is
 * FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE.  The FALLOC_FL_PUNCH_HOLE
 * flag was introduced in the 2.6.38 kernel.
 *
 * The original mode=0 (allocate space) behavior can be reasonably emulated
 * by checking if enough space exists and creating a sparse file, as real
 * persistent space reservation is not possible due to COW, snapshots, etc.
 */
static long
zpl_fallocate_common(struct inode *ip, int mode, loff_t offset, loff_t len)
{
	cred_t *cr = CRED();
	loff_t olen;
	fstrans_cookie_t cookie;
	int error = 0;

	int test_mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE;

	if ((mode & ~(FALLOC_FL_KEEP_SIZE | test_mode)) != 0)
		return (-EOPNOTSUPP);

	if (offset < 0 || len <= 0)
		return (-EINVAL);

	spl_inode_lock(ip);
	olen = i_size_read(ip);

	crhold(cr);
	cookie = spl_fstrans_mark();
	if (mode & (test_mode)) {
		flock64_t bf;

		if (mode & FALLOC_FL_KEEP_SIZE) {
			if (offset > olen)
				goto out_unmark;

			if (offset + len > olen)
				len = olen - offset;
		}
		bf.l_type = F_WRLCK;
		bf.l_whence = SEEK_SET;
		bf.l_start = offset;
		bf.l_len = len;
		bf.l_pid = 0;

		error = -zfs_space(ITOZ(ip), F_FREESP, &bf, O_RDWR, offset, cr);
	} else if ((mode & ~FALLOC_FL_KEEP_SIZE) == 0) {
		unsigned int percent = zfs_fallocate_reserve_percent;
		struct kstatfs statfs;

		/* Legacy mode, disable fallocate compatibility. */
		if (percent == 0) {
			error = -EOPNOTSUPP;
			goto out_unmark;
		}

		/*
		 * Use zfs_statvfs() instead of dmu_objset_space() since it
		 * also checks project quota limits, which are relevant here.
		 */
		error = zfs_statvfs(ip, &statfs);
		if (error)
			goto out_unmark;

		/*
		 * Shrink available space a bit to account for overhead/races.
		 * We know the product previously fit into availbytes from
		 * dmu_objset_space(), so the smaller product will also fit.
		 */
		if (len > statfs.f_bavail * (statfs.f_bsize * 100 / percent)) {
			error = -ENOSPC;
			goto out_unmark;
		}
		if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > olen)
			error = zfs_freesp(ITOZ(ip), offset + len, 0, 0, FALSE);
	}
out_unmark:
	spl_fstrans_unmark(cookie);
	spl_inode_unlock(ip);

	crfree(cr);

	return (error);
}

static long
zpl_fallocate(struct file *filp, int mode, loff_t offset, loff_t len)
{
	return zpl_fallocate_common(file_inode(filp),
	    mode, offset, len);
}

static int
zpl_ioctl_getversion(struct file *filp, void __user *arg)
{
	uint32_t generation = file_inode(filp)->i_generation;

	return (copy_to_user(arg, &generation, sizeof (generation)));
}

static int
zpl_fadvise(struct file *filp, loff_t offset, loff_t len, int advice)
{
	struct inode *ip = file_inode(filp);
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	objset_t *os = zfsvfs->z_os;
	int error = 0;

	if (S_ISFIFO(ip->i_mode))
		return (-ESPIPE);

	if (offset < 0 || len < 0)
		return (-EINVAL);

	if ((error = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	switch (advice) {
	case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_WILLNEED:
#ifdef HAVE_GENERIC_FADVISE
		if (zn_has_cached_data(zp, offset, offset + len - 1))
			error = generic_fadvise(filp, offset, len, advice);
#endif
		/*
		 * Pass on the caller's size directly, but note that
		 * dmu_prefetch_max will effectively cap it.  If there
		 * really is a larger sequential access pattern, perhaps
		 * dmu_zfetch will detect it.
		 */
		if (len == 0)
			len = i_size_read(ip) - offset;

		dmu_prefetch(os, zp->z_id, 0, offset, len,
		    ZIO_PRIORITY_ASYNC_READ);
		break;
	case POSIX_FADV_NORMAL:
	case POSIX_FADV_RANDOM:
	case POSIX_FADV_DONTNEED:
	case POSIX_FADV_NOREUSE:
		/* ignored for now */
		break;
	default:
		error = -EINVAL;
		break;
	}

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

#define	ZFS_FL_USER_VISIBLE	(FS_FL_USER_VISIBLE | ZFS_PROJINHERIT_FL)
#define	ZFS_FL_USER_MODIFIABLE	(FS_FL_USER_MODIFIABLE | ZFS_PROJINHERIT_FL)

static uint32_t
__zpl_ioctl_getflags(struct inode *ip)
{
	uint64_t zfs_flags = ITOZ(ip)->z_pflags;
	uint32_t ioctl_flags = 0;

	if (zfs_flags & ZFS_IMMUTABLE)
		ioctl_flags |= FS_IMMUTABLE_FL;

	if (zfs_flags & ZFS_APPENDONLY)
		ioctl_flags |= FS_APPEND_FL;

	if (zfs_flags & ZFS_NODUMP)
		ioctl_flags |= FS_NODUMP_FL;

	if (zfs_flags & ZFS_PROJINHERIT)
		ioctl_flags |= ZFS_PROJINHERIT_FL;

	return (ioctl_flags & ZFS_FL_USER_VISIBLE);
}

/*
 * Map zfs file z_pflags (xvattr_t) to linux file attributes. Only file
 * attributes common to both Linux and Solaris are mapped.
 */
static int
zpl_ioctl_getflags(struct file *filp, void __user *arg)
{
	uint32_t flags;
	int err;

	flags = __zpl_ioctl_getflags(file_inode(filp));
	err = copy_to_user(arg, &flags, sizeof (flags));

	return (err);
}

/*
 * fchange() is a helper macro to detect if we have been asked to change a
 * flag. This is ugly, but the requirement that we do this is a consequence of
 * how the Linux file attribute interface was designed. Another consequence is
 * that concurrent modification of files suffers from a TOCTOU race. Neither
 * are things we can fix without modifying the kernel-userland interface, which
 * is outside of our jurisdiction.
 */

#define	fchange(f0, f1, b0, b1) (!((f0) & (b0)) != !((f1) & (b1)))

static int
__zpl_ioctl_setflags(struct inode *ip, uint32_t ioctl_flags, xvattr_t *xva)
{
	uint64_t zfs_flags = ITOZ(ip)->z_pflags;
	xoptattr_t *xoap;

	if (ioctl_flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL | FS_NODUMP_FL |
	    ZFS_PROJINHERIT_FL))
		return (-EOPNOTSUPP);

	if (ioctl_flags & ~ZFS_FL_USER_MODIFIABLE)
		return (-EACCES);

	if ((fchange(ioctl_flags, zfs_flags, FS_IMMUTABLE_FL, ZFS_IMMUTABLE) ||
	    fchange(ioctl_flags, zfs_flags, FS_APPEND_FL, ZFS_APPENDONLY)) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return (-EPERM);

	if (!zpl_inode_owner_or_capable(zfs_init_idmap, ip))
		return (-EACCES);

	xva_init(xva);
	xoap = xva_getxoptattr(xva);

#define	FLAG_CHANGE(iflag, zflag, xflag, xfield)	do {	\
	if (((ioctl_flags & (iflag)) && !(zfs_flags & (zflag))) ||	\
	    ((zfs_flags & (zflag)) && !(ioctl_flags & (iflag)))) {	\
		XVA_SET_REQ(xva, (xflag));	\
		(xfield) = ((ioctl_flags & (iflag)) != 0);	\
	}	\
} while (0)

	FLAG_CHANGE(FS_IMMUTABLE_FL, ZFS_IMMUTABLE, XAT_IMMUTABLE,
	    xoap->xoa_immutable);
	FLAG_CHANGE(FS_APPEND_FL, ZFS_APPENDONLY, XAT_APPENDONLY,
	    xoap->xoa_appendonly);
	FLAG_CHANGE(FS_NODUMP_FL, ZFS_NODUMP, XAT_NODUMP,
	    xoap->xoa_nodump);
	FLAG_CHANGE(ZFS_PROJINHERIT_FL, ZFS_PROJINHERIT, XAT_PROJINHERIT,
	    xoap->xoa_projinherit);

#undef	FLAG_CHANGE

	return (0);
}

static int
zpl_ioctl_setflags(struct file *filp, void __user *arg)
{
	struct inode *ip = file_inode(filp);
	uint32_t flags;
	cred_t *cr = CRED();
	xvattr_t xva;
	int err;
	fstrans_cookie_t cookie;

	if (copy_from_user(&flags, arg, sizeof (flags)))
		return (-EFAULT);

	err = __zpl_ioctl_setflags(ip, flags, &xva);
	if (err)
		return (err);

	crhold(cr);
	cookie = spl_fstrans_mark();
	err = -zfs_setattr(ITOZ(ip), (vattr_t *)&xva, 0, cr, zfs_init_idmap);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (err);
}

static int
zpl_ioctl_getxattr(struct file *filp, void __user *arg)
{
	zfsxattr_t fsx = { 0 };
	struct inode *ip = file_inode(filp);
	int err;

	fsx.fsx_xflags = __zpl_ioctl_getflags(ip);
	fsx.fsx_projid = ITOZ(ip)->z_projid;
	err = copy_to_user(arg, &fsx, sizeof (fsx));

	return (err);
}

static int
zpl_ioctl_setxattr(struct file *filp, void __user *arg)
{
	struct inode *ip = file_inode(filp);
	zfsxattr_t fsx;
	cred_t *cr = CRED();
	xvattr_t xva;
	xoptattr_t *xoap;
	int err;
	fstrans_cookie_t cookie;

	if (copy_from_user(&fsx, arg, sizeof (fsx)))
		return (-EFAULT);

	if (!zpl_is_valid_projid(fsx.fsx_projid))
		return (-EINVAL);

	err = __zpl_ioctl_setflags(ip, fsx.fsx_xflags, &xva);
	if (err)
		return (err);

	xoap = xva_getxoptattr(&xva);
	XVA_SET_REQ(&xva, XAT_PROJID);
	xoap->xoa_projid = fsx.fsx_projid;

	crhold(cr);
	cookie = spl_fstrans_mark();
	err = -zfs_setattr(ITOZ(ip), (vattr_t *)&xva, 0, cr, zfs_init_idmap);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (err);
}

/*
 * Expose Additional File Level Attributes of ZFS.
 */
static int
zpl_ioctl_getdosflags(struct file *filp, void __user *arg)
{
	struct inode *ip = file_inode(filp);
	uint64_t dosflags = ITOZ(ip)->z_pflags;
	dosflags &= ZFS_DOS_FL_USER_VISIBLE;
	int err = copy_to_user(arg, &dosflags, sizeof (dosflags));

	return (err);
}

static int
__zpl_ioctl_setdosflags(struct inode *ip, uint64_t ioctl_flags, xvattr_t *xva)
{
	uint64_t zfs_flags = ITOZ(ip)->z_pflags;
	xoptattr_t *xoap;

	if (ioctl_flags & (~ZFS_DOS_FL_USER_VISIBLE))
		return (-EOPNOTSUPP);

	if ((fchange(ioctl_flags, zfs_flags, ZFS_IMMUTABLE, ZFS_IMMUTABLE) ||
	    fchange(ioctl_flags, zfs_flags, ZFS_APPENDONLY, ZFS_APPENDONLY)) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return (-EPERM);

	if (!zpl_inode_owner_or_capable(zfs_init_idmap, ip))
		return (-EACCES);

	xva_init(xva);
	xoap = xva_getxoptattr(xva);

#define	FLAG_CHANGE(iflag, xflag, xfield)	do {	\
	if (((ioctl_flags & (iflag)) && !(zfs_flags & (iflag))) ||	\
	    ((zfs_flags & (iflag)) && !(ioctl_flags & (iflag)))) {	\
		XVA_SET_REQ(xva, (xflag));	\
		(xfield) = ((ioctl_flags & (iflag)) != 0);	\
	}	\
} while (0)

	FLAG_CHANGE(ZFS_IMMUTABLE, XAT_IMMUTABLE, xoap->xoa_immutable);
	FLAG_CHANGE(ZFS_APPENDONLY, XAT_APPENDONLY, xoap->xoa_appendonly);
	FLAG_CHANGE(ZFS_NODUMP, XAT_NODUMP, xoap->xoa_nodump);
	FLAG_CHANGE(ZFS_READONLY, XAT_READONLY, xoap->xoa_readonly);
	FLAG_CHANGE(ZFS_HIDDEN, XAT_HIDDEN, xoap->xoa_hidden);
	FLAG_CHANGE(ZFS_SYSTEM, XAT_SYSTEM, xoap->xoa_system);
	FLAG_CHANGE(ZFS_ARCHIVE, XAT_ARCHIVE, xoap->xoa_archive);
	FLAG_CHANGE(ZFS_NOUNLINK, XAT_NOUNLINK, xoap->xoa_nounlink);
	FLAG_CHANGE(ZFS_REPARSE, XAT_REPARSE, xoap->xoa_reparse);
	FLAG_CHANGE(ZFS_OFFLINE, XAT_OFFLINE, xoap->xoa_offline);
	FLAG_CHANGE(ZFS_SPARSE, XAT_SPARSE, xoap->xoa_sparse);

#undef	FLAG_CHANGE

	return (0);
}

/*
 * Set Additional File Level Attributes of ZFS.
 */
static int
zpl_ioctl_setdosflags(struct file *filp, void __user *arg)
{
	struct inode *ip = file_inode(filp);
	uint64_t dosflags;
	cred_t *cr = CRED();
	xvattr_t xva;
	int err;
	fstrans_cookie_t cookie;

	if (copy_from_user(&dosflags, arg, sizeof (dosflags)))
		return (-EFAULT);

	err = __zpl_ioctl_setdosflags(ip, dosflags, &xva);
	if (err)
		return (err);

	crhold(cr);
	cookie = spl_fstrans_mark();
	err = -zfs_setattr(ITOZ(ip), (vattr_t *)&xva, 0, cr, zfs_init_idmap);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (err);
}

static long
zpl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC_GETVERSION:
		return (zpl_ioctl_getversion(filp, (void *)arg));
	case FS_IOC_GETFLAGS:
		return (zpl_ioctl_getflags(filp, (void *)arg));
	case FS_IOC_SETFLAGS:
		return (zpl_ioctl_setflags(filp, (void *)arg));
	case ZFS_IOC_FSGETXATTR:
		return (zpl_ioctl_getxattr(filp, (void *)arg));
	case ZFS_IOC_FSSETXATTR:
		return (zpl_ioctl_setxattr(filp, (void *)arg));
	case ZFS_IOC_GETDOSFLAGS:
		return (zpl_ioctl_getdosflags(filp, (void *)arg));
	case ZFS_IOC_SETDOSFLAGS:
		return (zpl_ioctl_setdosflags(filp, (void *)arg));
	case ZFS_IOC_COMPAT_FICLONE:
		return (zpl_ioctl_ficlone(filp, (void *)arg));
	case ZFS_IOC_COMPAT_FICLONERANGE:
		return (zpl_ioctl_ficlonerange(filp, (void *)arg));
	case ZFS_IOC_COMPAT_FIDEDUPERANGE:
		return (zpl_ioctl_fideduperange(filp, (void *)arg));
	default:
		return (-ENOTTY);
	}
}

#ifdef CONFIG_COMPAT
static long
zpl_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	default:
		return (-ENOTTY);
	}
	return (zpl_ioctl(filp, cmd, (unsigned long)compat_ptr(arg)));
}
#endif /* CONFIG_COMPAT */

const struct address_space_operations zpl_address_space_operations = {
#ifdef HAVE_VFS_READPAGES
	.readpages	= zpl_readpages,
#else
	.readahead	= zpl_readahead,
#endif
#ifdef HAVE_VFS_READ_FOLIO
	.read_folio	= zpl_read_folio,
#else
	.readpage	= zpl_readpage,
#endif
	.writepage	= zpl_writepage,
	.writepages	= zpl_writepages,
	.direct_IO	= zpl_direct_IO,
#ifdef HAVE_VFS_SET_PAGE_DIRTY_NOBUFFERS
	.set_page_dirty = __set_page_dirty_nobuffers,
#endif
#ifdef HAVE_VFS_FILEMAP_DIRTY_FOLIO
	.dirty_folio	= filemap_dirty_folio,
#endif
#ifdef HAVE_VFS_MIGRATE_FOLIO
	.migrate_folio	= migrate_folio,
#else
	.migratepage	= migrate_page,
#endif
};

const struct file_operations zpl_file_operations = {
	.open		= zpl_open,
	.release	= zpl_release,
	.llseek		= zpl_llseek,
	.read_iter	= zpl_iter_read,
	.write_iter	= zpl_iter_write,
#ifdef HAVE_COPY_SPLICE_READ
	.splice_read	= copy_splice_read,
#else
	.splice_read	= generic_file_splice_read,
#endif
	.splice_write	= iter_file_splice_write,
	.mmap		= zpl_mmap,
	.fsync		= zpl_fsync,
	.fallocate	= zpl_fallocate,
	.copy_file_range	= zpl_copy_file_range,
#ifdef HAVE_VFS_CLONE_FILE_RANGE
	.clone_file_range	= zpl_clone_file_range,
#endif
#ifdef HAVE_VFS_REMAP_FILE_RANGE
	.remap_file_range	= zpl_remap_file_range,
#endif
#ifdef HAVE_VFS_DEDUPE_FILE_RANGE
	.dedupe_file_range	= zpl_dedupe_file_range,
#endif
	.fadvise	= zpl_fadvise,
	.unlocked_ioctl	= zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= zpl_compat_ioctl,
#endif
};

const struct file_operations zpl_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= zpl_iterate,
	.fsync		= zpl_fsync,
	.unlocked_ioctl = zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = zpl_compat_ioctl,
#endif
};

module_param(zfs_fallocate_reserve_percent, uint, 0644);
MODULE_PARM_DESC(zfs_fallocate_reserve_percent,
	"Percentage of length to use for the available capacity check");
