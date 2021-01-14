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
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */


#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <sys/file.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_project.h>

/*
 * When using fallocate(2) to preallocate space, inflate the requested
 * capacity check by 10% to account for the required metadata blocks.
 */
unsigned int zfs_fallocate_reserve_percent = 110;

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
zpl_iterate(struct file *filp, zpl_dir_context_t *ctx)
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

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

#if defined(HAVE_FSYNC_WITHOUT_DENTRY)
/*
 * Linux 2.6.35 - 3.0 API,
 * As of 2.6.35 the dentry argument to the fops->fsync() hook was deemed
 * redundant.  The dentry is still accessible via filp->f_path.dentry,
 * and we are guaranteed that filp will never be NULL.
 */
static int
zpl_fsync(struct file *filp, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(ITOZ(inode), datasync, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_FILE_AIO_FSYNC
static int
zpl_aio_fsync(struct kiocb *kiocb, int datasync)
{
	return (zpl_fsync(kiocb->ki_filp, datasync));
}
#endif

#elif defined(HAVE_FSYNC_RANGE)
/*
 * Linux 3.1 - 3.x API,
 * As of 3.1 the responsibility to call filemap_write_and_wait_range() has
 * been pushed down in to the .fsync() vfs hook.  Additionally, the i_mutex
 * lock is no longer held by the caller, for zfs we don't require the lock
 * to be held so we don't acquire it.
 */
static int
zpl_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	cred_t *cr = CRED();
	int error;
	fstrans_cookie_t cookie;

	error = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (error)
		return (error);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(ITOZ(inode), datasync, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_FILE_AIO_FSYNC
static int
zpl_aio_fsync(struct kiocb *kiocb, int datasync)
{
	return (zpl_fsync(kiocb->ki_filp, kiocb->ki_pos, -1, datasync));
}
#endif

#else
#error "Unsupported fops->fsync() implementation"
#endif

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

#if defined(HAVE_VFS_RW_ITERATE)

/*
 * When HAVE_VFS_IOV_ITER is defined the iov_iter structure supports
 * iovecs, kvevs, bvecs and pipes, plus all the required interfaces to
 * manipulate the iov_iter are available.  In which case the full iov_iter
 * can be attached to the uio and correctly handled in the lower layers.
 * Otherwise, for older kernels extract the iovec and pass it instead.
 */
static void
zpl_uio_init(uio_t *uio, struct kiocb *kiocb, struct iov_iter *to,
    loff_t pos, ssize_t count, size_t skip)
{
#if defined(HAVE_VFS_IOV_ITER)
	uio_iov_iter_init(uio, to, pos, count, skip);
#else
	uio_iovec_init(uio, to->iov, to->nr_segs, pos,
	    to->type & ITER_KVEC ? UIO_SYSSPACE : UIO_USERSPACE,
	    count, skip);
#endif
}

static ssize_t
zpl_iter_read(struct kiocb *kiocb, struct iov_iter *to)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct file *filp = kiocb->ki_filp;
	ssize_t count = iov_iter_count(to);
	uio_t uio;

	zpl_uio_init(&uio, kiocb, to, kiocb->ki_pos, count, 0);

	crhold(cr);
	cookie = spl_fstrans_mark();

	int error = -zfs_read(ITOZ(filp->f_mapping->host), &uio,
	    filp->f_flags | zfs_io_flags(kiocb), cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error < 0)
		return (error);

	ssize_t read = count - uio.uio_resid;
	kiocb->ki_pos += read;

	zpl_file_accessed(filp);

	return (read);
}

static inline ssize_t
zpl_generic_write_checks(struct kiocb *kiocb, struct iov_iter *from,
    size_t *countp)
{
#ifdef HAVE_GENERIC_WRITE_CHECKS_KIOCB
	ssize_t ret = generic_write_checks(kiocb, from);
	if (ret <= 0)
		return (ret);

	*countp = ret;
#else
	struct file *file = kiocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *ip = mapping->host;
	int isblk = S_ISBLK(ip->i_mode);

	*countp = iov_iter_count(from);
	ssize_t ret = generic_write_checks(file, &kiocb->ki_pos, countp, isblk);
	if (ret)
		return (ret);
#endif

	return (0);
}

static ssize_t
zpl_iter_write(struct kiocb *kiocb, struct iov_iter *from)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct file *filp = kiocb->ki_filp;
	struct inode *ip = filp->f_mapping->host;
	uio_t uio;
	size_t count = 0;
	ssize_t ret;

	ret = zpl_generic_write_checks(kiocb, from, &count);
	if (ret)
		return (ret);

	zpl_uio_init(&uio, kiocb, from, kiocb->ki_pos, count, from->iov_offset);

	crhold(cr);
	cookie = spl_fstrans_mark();

	int error = -zfs_write(ITOZ(ip), &uio,
	    filp->f_flags | zfs_io_flags(kiocb), cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error < 0)
		return (error);

	ssize_t wrote = count - uio.uio_resid;
	kiocb->ki_pos += wrote;

	if (wrote > 0)
		iov_iter_advance(from, wrote);

	return (wrote);
}

#else /* !HAVE_VFS_RW_ITERATE */

static ssize_t
zpl_aio_read(struct kiocb *kiocb, const struct iovec *iov,
    unsigned long nr_segs, loff_t pos)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct file *filp = kiocb->ki_filp;
	size_t count;
	ssize_t ret;

	ret = generic_segment_checks(iov, &nr_segs, &count, VERIFY_WRITE);
	if (ret)
		return (ret);

	uio_t uio;
	uio_iovec_init(&uio, iov, nr_segs, kiocb->ki_pos, UIO_USERSPACE,
	    count, 0);

	crhold(cr);
	cookie = spl_fstrans_mark();

	int error = -zfs_read(ITOZ(filp->f_mapping->host), &uio,
	    filp->f_flags | zfs_io_flags(kiocb), cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error < 0)
		return (error);

	ssize_t read = count - uio.uio_resid;
	kiocb->ki_pos += read;

	zpl_file_accessed(filp);

	return (read);
}

static ssize_t
zpl_aio_write(struct kiocb *kiocb, const struct iovec *iov,
    unsigned long nr_segs, loff_t pos)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct file *filp = kiocb->ki_filp;
	struct inode *ip = filp->f_mapping->host;
	size_t count;
	ssize_t ret;

	ret = generic_segment_checks(iov, &nr_segs, &count, VERIFY_READ);
	if (ret)
		return (ret);

	ret = generic_write_checks(filp, &pos, &count, S_ISBLK(ip->i_mode));
	if (ret)
		return (ret);

	uio_t uio;
	uio_iovec_init(&uio, iov, nr_segs, kiocb->ki_pos, UIO_USERSPACE,
	    count, 0);

	crhold(cr);
	cookie = spl_fstrans_mark();

	int error = -zfs_write(ITOZ(ip), &uio,
	    filp->f_flags | zfs_io_flags(kiocb), cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error < 0)
		return (error);

	ssize_t wrote = count - uio.uio_resid;
	kiocb->ki_pos += wrote;

	return (wrote);
}
#endif /* HAVE_VFS_RW_ITERATE */

#if defined(HAVE_VFS_RW_ITERATE)
static ssize_t
zpl_direct_IO_impl(int rw, struct kiocb *kiocb, struct iov_iter *iter)
{
	if (rw == WRITE)
		return (zpl_iter_write(kiocb, iter));
	else
		return (zpl_iter_read(kiocb, iter));
}
#if defined(HAVE_VFS_DIRECT_IO_ITER)
static ssize_t
zpl_direct_IO(struct kiocb *kiocb, struct iov_iter *iter)
{
	return (zpl_direct_IO_impl(iov_iter_rw(iter), kiocb, iter));
}
#elif defined(HAVE_VFS_DIRECT_IO_ITER_OFFSET)
static ssize_t
zpl_direct_IO(struct kiocb *kiocb, struct iov_iter *iter, loff_t pos)
{
	ASSERT3S(pos, ==, kiocb->ki_pos);
	return (zpl_direct_IO_impl(iov_iter_rw(iter), kiocb, iter));
}
#elif defined(HAVE_VFS_DIRECT_IO_ITER_RW_OFFSET)
static ssize_t
zpl_direct_IO(int rw, struct kiocb *kiocb, struct iov_iter *iter, loff_t pos)
{
	ASSERT3S(pos, ==, kiocb->ki_pos);
	return (zpl_direct_IO_impl(rw, kiocb, iter));
}
#else
#error "Unknown direct IO interface"
#endif

#else /* HAVE_VFS_RW_ITERATE */

#if defined(HAVE_VFS_DIRECT_IO_IOVEC)
static ssize_t
zpl_direct_IO(int rw, struct kiocb *kiocb, const struct iovec *iov,
    loff_t pos, unsigned long nr_segs)
{
	if (rw == WRITE)
		return (zpl_aio_write(kiocb, iov, nr_segs, pos));
	else
		return (zpl_aio_read(kiocb, iov, nr_segs, pos));
}
#elif defined(HAVE_VFS_DIRECT_IO_ITER_RW_OFFSET)
static ssize_t
zpl_direct_IO(int rw, struct kiocb *kiocb, struct iov_iter *iter, loff_t pos)
{
	const struct iovec *iovp = iov_iter_iovec(iter);
	unsigned long nr_segs = iter->nr_segs;

	ASSERT3S(pos, ==, kiocb->ki_pos);
	if (rw == WRITE)
		return (zpl_aio_write(kiocb, iovp, nr_segs, pos));
	else
		return (zpl_aio_read(kiocb, iovp, nr_segs, pos));
}
#else
#error "Unknown direct IO interface"
#endif

#endif /* HAVE_VFS_RW_ITERATE */

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
	znode_t *zp = ITOZ(ip);
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

	mutex_enter(&zp->z_lock);
	zp->z_is_mapped = B_TRUE;
	mutex_exit(&zp->z_lock);

	return (error);
}

/*
 * Populate a page with data for the Linux page cache.  This function is
 * only used to support mmap(2).  There will be an identical copy of the
 * data in the ARC which is kept up to date via .write() and .writepage().
 */
static int
zpl_readpage(struct file *filp, struct page *pp)
{
	struct inode *ip;
	struct page *pl[1];
	int error = 0;
	fstrans_cookie_t cookie;

	ASSERT(PageLocked(pp));
	ip = pp->mapping->host;
	pl[0] = pp;

	cookie = spl_fstrans_mark();
	error = -zfs_getpage(ip, pl, 1);
	spl_fstrans_unmark(cookie);

	if (error) {
		SetPageError(pp);
		ClearPageUptodate(pp);
	} else {
		ClearPageError(pp);
		SetPageUptodate(pp);
		flush_dcache_page(pp);
	}

	unlock_page(pp);
	return (error);
}

/*
 * Populate a set of pages with data for the Linux page cache.  This
 * function will only be called for read ahead and never for demand
 * paging.  For simplicity, the code relies on read_cache_pages() to
 * correctly lock each page for IO and call zpl_readpage().
 */
static int
zpl_readpages(struct file *filp, struct address_space *mapping,
    struct list_head *pages, unsigned nr_pages)
{
	return (read_cache_pages(mapping, pages,
	    (filler_t *)zpl_readpage, filp));
}

static int
zpl_putpage(struct page *pp, struct writeback_control *wbc, void *data)
{
	struct address_space *mapping = data;
	fstrans_cookie_t cookie;

	ASSERT(PageLocked(pp));
	ASSERT(!PageWriteback(pp));

	cookie = spl_fstrans_mark();
	(void) zfs_putpage(mapping->host, pp, wbc);
	spl_fstrans_unmark(cookie);

	return (0);
}

static int
zpl_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	znode_t		*zp = ITOZ(mapping->host);
	zfsvfs_t	*zfsvfs = ITOZSB(mapping->host);
	enum writeback_sync_modes sync_mode;
	int result;

	ZPL_ENTER(zfsvfs);
	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		wbc->sync_mode = WB_SYNC_ALL;
	ZPL_EXIT(zfsvfs);
	sync_mode = wbc->sync_mode;

	/*
	 * We don't want to run write_cache_pages() in SYNC mode here, because
	 * that would make putpage() wait for a single page to be committed to
	 * disk every single time, resulting in atrocious performance. Instead
	 * we run it once in non-SYNC mode so that the ZIL gets all the data,
	 * and then we commit it all in one go.
	 */
	wbc->sync_mode = WB_SYNC_NONE;
	result = write_cache_pages(mapping, wbc, zpl_putpage, mapping);
	if (sync_mode != wbc->sync_mode) {
		ZPL_ENTER(zfsvfs);
		ZPL_VERIFY_ZP(zp);
		if (zfsvfs->z_log != NULL)
			zil_commit(zfsvfs->z_log, zp->z_id);
		ZPL_EXIT(zfsvfs);

		/*
		 * We need to call write_cache_pages() again (we can't just
		 * return after the commit) because the previous call in
		 * non-SYNC mode does not guarantee that we got all the dirty
		 * pages (see the implementation of write_cache_pages() for
		 * details). That being said, this is a no-op in most cases.
		 */
		wbc->sync_mode = sync_mode;
		result = write_cache_pages(mapping, wbc, zpl_putpage, mapping);
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

	return (zpl_putpage(pp, wbc, pp->mapping));
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

	if ((mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE)) != 0)
		return (-EOPNOTSUPP);

	if (offset < 0 || len <= 0)
		return (-EINVAL);

	spl_inode_lock(ip);
	olen = i_size_read(ip);

	crhold(cr);
	cookie = spl_fstrans_mark();
	if (mode & FALLOC_FL_PUNCH_HOLE) {
		flock64_t bf;

		if (offset > olen)
			goto out_unmark;

		if (offset + len > olen)
			len = olen - offset;
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
		return (-EACCES);

	if (!inode_owner_or_capable(ip))
		return (-EACCES);

	xva_init(xva);
	xoap = xva_getxoptattr(xva);

	XVA_SET_REQ(xva, XAT_IMMUTABLE);
	if (ioctl_flags & FS_IMMUTABLE_FL)
		xoap->xoa_immutable = B_TRUE;

	XVA_SET_REQ(xva, XAT_APPENDONLY);
	if (ioctl_flags & FS_APPEND_FL)
		xoap->xoa_appendonly = B_TRUE;

	XVA_SET_REQ(xva, XAT_NODUMP);
	if (ioctl_flags & FS_NODUMP_FL)
		xoap->xoa_nodump = B_TRUE;

	XVA_SET_REQ(xva, XAT_PROJINHERIT);
	if (ioctl_flags & ZFS_PROJINHERIT_FL)
		xoap->xoa_projinherit = B_TRUE;

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
	err = -zfs_setattr(ITOZ(ip), (vattr_t *)&xva, 0, cr);
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
	err = -zfs_setattr(ITOZ(ip), (vattr_t *)&xva, 0, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (err);
}

static long
zpl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return (zpl_ioctl_getflags(filp, (void *)arg));
	case FS_IOC_SETFLAGS:
		return (zpl_ioctl_setflags(filp, (void *)arg));
	case ZFS_IOC_FSGETXATTR:
		return (zpl_ioctl_getxattr(filp, (void *)arg));
	case ZFS_IOC_FSSETXATTR:
		return (zpl_ioctl_setxattr(filp, (void *)arg));
	default:
		return (-ENOTTY);
	}
}

#ifdef CONFIG_COMPAT
static long
zpl_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
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
	.readpages	= zpl_readpages,
	.readpage	= zpl_readpage,
	.writepage	= zpl_writepage,
	.writepages	= zpl_writepages,
	.direct_IO	= zpl_direct_IO,
};

const struct file_operations zpl_file_operations = {
	.open		= zpl_open,
	.release	= zpl_release,
	.llseek		= zpl_llseek,
#ifdef HAVE_VFS_RW_ITERATE
#ifdef HAVE_NEW_SYNC_READ
	.read		= new_sync_read,
	.write		= new_sync_write,
#endif
	.read_iter	= zpl_iter_read,
	.write_iter	= zpl_iter_write,
#ifdef HAVE_VFS_IOV_ITER
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
#endif
#else
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= zpl_aio_read,
	.aio_write	= zpl_aio_write,
#endif
	.mmap		= zpl_mmap,
	.fsync		= zpl_fsync,
#ifdef HAVE_FILE_AIO_FSYNC
	.aio_fsync	= zpl_aio_fsync,
#endif
	.fallocate	= zpl_fallocate,
	.unlocked_ioctl	= zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= zpl_compat_ioctl,
#endif
};

const struct file_operations zpl_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#if defined(HAVE_VFS_ITERATE_SHARED)
	.iterate_shared	= zpl_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_iterate,
#else
	.readdir	= zpl_readdir,
#endif
	.fsync		= zpl_fsync,
	.unlocked_ioctl = zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = zpl_compat_ioctl,
#endif
};

/* BEGIN CSTYLED */
module_param(zfs_fallocate_reserve_percent, uint, 0644);
MODULE_PARM_DESC(zfs_fallocate_reserve_percent,
    "Percentage of length to use for the available capacity check");
/* END CSTYLED */
