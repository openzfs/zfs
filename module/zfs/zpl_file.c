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
 */


#include <sys/dmu_objset.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zpl.h>


static int
zpl_open(struct inode *ip, struct file *filp)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_open(ip, filp->f_mode, filp->f_flags, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	if (error)
		return (error);

	return generic_file_open(ip, filp);
}

static int
zpl_release(struct inode *ip, struct file *filp)
{
	cred_t *cr = CRED();
	int error;

	if (ITOZ(ip)->z_atime_dirty)
		mark_inode_dirty(ip);

	crhold(cr);
	error = -zfs_close(ip, filp->f_flags, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_iterate(struct file *filp, struct dir_context *ctx)
{
	struct dentry *dentry = filp->f_path.dentry;
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_readdir(dentry->d_inode, ctx, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#if !defined(HAVE_VFS_ITERATE)
static int
zpl_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dir_context ctx = DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* HAVE_VFS_ITERATE */

#if defined(HAVE_FSYNC_WITH_DENTRY)
/*
 * Linux 2.6.x - 2.6.34 API,
 * Through 2.6.34 the nfsd kernel server would pass a NULL 'file struct *'
 * to the fops->fsync() hook.  For this reason, we must be careful not to
 * use filp unconditionally.
 */
static int
zpl_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_fsync(dentry->d_inode, datasync, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#elif defined(HAVE_FSYNC_WITHOUT_DENTRY)
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

	crhold(cr);
	error = -zfs_fsync(inode, datasync, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

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

	error = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (error)
		return (error);

	crhold(cr);
	error = -zfs_fsync(inode, datasync, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}
#else
#error "Unsupported fops->fsync() implementation"
#endif

ssize_t
zpl_read_common(struct inode *ip, const char *buf, size_t len, loff_t pos,
     uio_seg_t segment, int flags, cred_t *cr)
{
	int error;
	struct iovec iov;
	uio_t uio;

	iov.iov_base = (void *)buf;
	iov.iov_len = len;

	uio.uio_iov = &iov;
	uio.uio_resid = len;
	uio.uio_iovcnt = 1;
	uio.uio_loffset = pos;
	uio.uio_limit = MAXOFFSET_T;
	uio.uio_segflg = segment;

	error = -zfs_read(ip, &uio, flags, cr);
	if (error < 0)
		return (error);

	return (len - uio.uio_resid);
}

static ssize_t
zpl_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	cred_t *cr = CRED();
	ssize_t read;

	crhold(cr);
	read = zpl_read_common(filp->f_mapping->host, buf, len, *ppos,
	    UIO_USERSPACE, filp->f_flags, cr);
	crfree(cr);

	if (read < 0)
		return (read);

	*ppos += read;
	return (read);
}

ssize_t
zpl_write_common(struct inode *ip, const char *buf, size_t len, loff_t pos,
    uio_seg_t segment, int flags, cred_t *cr)
{
	int error;
	struct iovec iov;
	uio_t uio;

	iov.iov_base = (void *)buf;
	iov.iov_len = len;

	uio.uio_iov = &iov;
	uio.uio_resid = len,
	uio.uio_iovcnt = 1;
	uio.uio_loffset = pos;
	uio.uio_limit = MAXOFFSET_T;
	uio.uio_segflg = segment;

	error = -zfs_write(ip, &uio, flags, cr);
	if (error < 0)
		return (error);

	return (len - uio.uio_resid);
}

static ssize_t
zpl_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	cred_t *cr = CRED();
	ssize_t wrote;

	crhold(cr);
	wrote = zpl_write_common(filp->f_mapping->host, buf, len, *ppos,
	    UIO_USERSPACE, filp->f_flags, cr);
	crfree(cr);

	if (wrote < 0)
		return (wrote);

	*ppos += wrote;
	return (wrote);
}

static loff_t
zpl_llseek(struct file *filp, loff_t offset, int whence)
{
#if defined(SEEK_HOLE) && defined(SEEK_DATA)
	if (whence == SEEK_DATA || whence == SEEK_HOLE) {
		struct inode *ip = filp->f_mapping->host;
		loff_t maxbytes = ip->i_sb->s_maxbytes;
		loff_t error;

		spl_inode_lock(ip);
		error = -zfs_holey(ip, whence, &offset);
		if (error == 0)
			error = lseek_execute(filp, ip, offset, maxbytes);
		spl_inode_unlock(ip);

		return (error);
	}
#endif /* SEEK_HOLE && SEEK_DATA */

	return generic_file_llseek(filp, offset, whence);
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

	error = -zfs_map(ip, vma->vm_pgoff, (caddr_t *)vma->vm_start,
	    (size_t)(vma->vm_end - vma->vm_start), vma->vm_flags);
	if (error)
		return (error);

	error = generic_file_mmap(filp, vma);
	if (error)
		return (error);

	mutex_enter(&zp->z_lock);
	zp->z_is_mapped = 1;
	mutex_exit(&zp->z_lock);

	return (error);
}

/*
 * Populate a page with data for the Linux page cache.  This function is
 * only used to support mmap(2).  There will be an identical copy of the
 * data in the ARC which is kept up to date via .write() and .writepage().
 *
 * Current this function relies on zpl_read_common() and the O_DIRECT
 * flag to read in a page.  This works but the more correct way is to
 * update zfs_fillpage() to be Linux friendly and use that interface.
 */
static int
zpl_readpage(struct file *filp, struct page *pp)
{
	struct inode *ip;
	struct page *pl[1];
	int error = 0;

	ASSERT(PageLocked(pp));
	ip = pp->mapping->host;
	pl[0] = pp;

	error = -zfs_getpage(ip, pl, 1);

	if (error) {
		SetPageError(pp);
		ClearPageUptodate(pp);
	} else {
		ClearPageError(pp);
		SetPageUptodate(pp);
		flush_dcache_page(pp);
	}

	unlock_page(pp);
	return error;
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

int
zpl_putpage(struct page *pp, struct writeback_control *wbc, void *data)
{
	struct address_space *mapping = data;

	ASSERT(PageLocked(pp));
	ASSERT(!PageWriteback(pp));
	ASSERT(!(current->flags & PF_NOFS));

	/*
	 * Annotate this call path with a flag that indicates that it is
	 * unsafe to use KM_SLEEP during memory allocations due to the
	 * potential for a deadlock.  KM_PUSHPAGE should be used instead.
	 */
	current->flags |= PF_NOFS;
	(void) zfs_putpage(mapping->host, pp, wbc);
	current->flags &= ~PF_NOFS;

	return (0);
}

static int
zpl_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	znode_t		*zp = ITOZ(mapping->host);
	zfs_sb_t	*zsb = ITOZSB(mapping->host);
	enum writeback_sync_modes sync_mode;
	int result;

	ZFS_ENTER(zsb);
	if (zsb->z_os->os_sync == ZFS_SYNC_ALWAYS)
		wbc->sync_mode = WB_SYNC_ALL;
	ZFS_EXIT(zsb);
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
		ZFS_ENTER(zsb);
		ZFS_VERIFY_ZP(zp);
		zil_commit(zsb->z_log, zp->z_id);
		ZFS_EXIT(zsb);

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
	return result;
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
	return zpl_putpage(pp, wbc, pp->mapping);
}

/*
 * The only flag combination which matches the behavior of zfs_space()
 * is FALLOC_FL_PUNCH_HOLE.  This flag was introduced in the 2.6.38 kernel.
 */
long
zpl_fallocate_common(struct inode *ip, int mode, loff_t offset, loff_t len)
{
	cred_t *cr = CRED();
	int error = -EOPNOTSUPP;

	if (mode & FALLOC_FL_KEEP_SIZE)
		return (-EOPNOTSUPP);

	crhold(cr);

#ifdef FALLOC_FL_PUNCH_HOLE
	if (mode & FALLOC_FL_PUNCH_HOLE) {
		flock64_t bf;

		bf.l_type = F_WRLCK;
		bf.l_whence = 0;
		bf.l_start = offset;
		bf.l_len = len;
		bf.l_pid = 0;

		error = -zfs_space(ip, F_FREESP, &bf, FWRITE, offset, cr);
	}
#endif /* FALLOC_FL_PUNCH_HOLE */

	crfree(cr);

	ASSERT3S(error, <=, 0);
	return (error);
}

#ifdef HAVE_FILE_FALLOCATE
static long
zpl_fallocate(struct file *filp, int mode, loff_t offset, loff_t len)
{
	return zpl_fallocate_common(filp->f_path.dentry->d_inode,
	    mode, offset, len);
}
#endif /* HAVE_FILE_FALLOCATE */

static long
zpl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ZFS_IOC_GETFLAGS:
	case ZFS_IOC_SETFLAGS:
		return (-EOPNOTSUPP);
	default:
		return (-ENOTTY);
	}
}

#ifdef CONFIG_COMPAT
static long
zpl_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return zpl_ioctl(filp, cmd, arg);
}
#endif /* CONFIG_COMPAT */


const struct address_space_operations zpl_address_space_operations = {
	.readpages	= zpl_readpages,
	.readpage	= zpl_readpage,
	.writepage	= zpl_writepage,
	.writepages     = zpl_writepages,
};

const struct file_operations zpl_file_operations = {
	.open		= zpl_open,
	.release	= zpl_release,
	.llseek		= zpl_llseek,
	.read		= zpl_read,
	.write		= zpl_write,
	.mmap		= zpl_mmap,
	.fsync		= zpl_fsync,
#ifdef HAVE_FILE_FALLOCATE
	.fallocate      = zpl_fallocate,
#endif /* HAVE_FILE_FALLOCATE */
	.unlocked_ioctl = zpl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = zpl_compat_ioctl,
#endif
};

const struct file_operations zpl_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE
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
