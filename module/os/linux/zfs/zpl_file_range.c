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
 * Copyright (c) 2023, Klara Inc.
 */

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfeature.h>

/*
 * Clone part of a file via block cloning.
 *
 * Note that we are not required to update file offsets; the kernel will take
 * care of that depending on how it was called.
 */
static ssize_t
__zpl_clone_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, size_t len)
{
	struct inode *src_i = file_inode(src_file);
	struct inode *dst_i = file_inode(dst_file);
	uint64_t src_off_o = (uint64_t)src_off;
	uint64_t dst_off_o = (uint64_t)dst_off;
	uint64_t len_o = (uint64_t)len;
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int err;

	if (!spa_feature_is_enabled(
	    dmu_objset_spa(ITOZSB(dst_i)->z_os), SPA_FEATURE_BLOCK_CLONING))
		return (-EOPNOTSUPP);

	if (src_i != dst_i)
		spl_inode_lock_shared(src_i);
	spl_inode_lock(dst_i);

	crhold(cr);
	cookie = spl_fstrans_mark();

	err = -zfs_clone_range(ITOZ(src_i), &src_off_o, ITOZ(dst_i),
	    &dst_off_o, &len_o, cr);

	spl_fstrans_unmark(cookie);
	crfree(cr);

	spl_inode_unlock(dst_i);
	if (src_i != dst_i)
		spl_inode_unlock_shared(src_i);

	if (err < 0)
		return (err);

	return ((ssize_t)len_o);
}

#if defined(HAVE_VFS_COPY_FILE_RANGE) || \
    defined(HAVE_VFS_FILE_OPERATIONS_EXTEND)
/*
 * Entry point for copy_file_range(). Copy len bytes from src_off in src_file
 * to dst_off in dst_file. We are permitted to do this however we like, so we
 * try to just clone the blocks, and if we can't support it, fall back to the
 * kernel's generic byte copy function.
 */
ssize_t
zpl_copy_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, size_t len, unsigned int flags)
{
	ssize_t ret;

	if (flags != 0)
		return (-EINVAL);

	/* Try to do it via zfs_clone_range() */
	ret = __zpl_clone_file_range(src_file, src_off,
	    dst_file, dst_off, len);

#ifdef HAVE_VFS_GENERIC_COPY_FILE_RANGE
	/*
	 * Since Linux 5.3 the filesystem driver is responsible for executing
	 * an appropriate fallback, and a generic fallback function is provided.
	 */
	if (ret == -EOPNOTSUPP || ret == -EINVAL || ret == -EXDEV ||
	    ret == -EAGAIN)
		ret = generic_copy_file_range(src_file, src_off, dst_file,
		    dst_off, len, flags);
#else
	/*
	 * Before Linux 5.3 the filesystem has to return -EOPNOTSUPP to signal
	 * to the kernel that it should fallback to a content copy.
	 */
	if (ret == -EINVAL || ret == -EXDEV || ret == -EAGAIN)
		ret = -EOPNOTSUPP;
#endif /* HAVE_VFS_GENERIC_COPY_FILE_RANGE */

	return (ret);
}
#endif /* HAVE_VFS_COPY_FILE_RANGE || HAVE_VFS_FILE_OPERATIONS_EXTEND */

#ifdef HAVE_VFS_REMAP_FILE_RANGE
/*
 * Entry point for FICLONE/FICLONERANGE/FIDEDUPERANGE.
 *
 * FICLONE and FICLONERANGE are basically the same as copy_file_range(), except
 * that they must clone - they cannot fall back to copying. FICLONE is exactly
 * FICLONERANGE, for the entire file. We don't need to try to tell them apart;
 * the kernel will sort that out for us.
 *
 * FIDEDUPERANGE is for turning a non-clone into a clone, that is, compare the
 * range in both files and if they're the same, arrange for them to be backed
 * by the same storage.
 */
loff_t
zpl_remap_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, loff_t len, unsigned int flags)
{
	if (flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_CAN_SHORTEN))
		return (-EINVAL);

	/*
	 * REMAP_FILE_CAN_SHORTEN lets us know we can clone less than the given
	 * range if we want. Its designed for filesystems that make data past
	 * EOF available, and don't want it to be visible in both files. ZFS
	 * doesn't do that, so we just turn the flag off.
	 */
	flags &= ~REMAP_FILE_CAN_SHORTEN;
	if (flags & REMAP_FILE_DEDUP) {
		/* Zero length means to clone everything to the end of the file
		 */
		if (len == 0)
			len = i_size_read(file_inode(src_file)) - src_off;
		// Both nodes must be range locked
		zfs_locked_range_t *src_zlr = zfs_rangelock_enter(
		    &ITOZ(file_inode(src_file))->z_rangelock, src_off, len,
		    RL_READER);
		zfs_locked_range_t *dst_zlr = zfs_rangelock_enter(
		    &ITOZ(file_inode(dst_file))->z_rangelock, dst_off, len,
		    RL_WRITER);
		bool same = false;
		int ret = zpl_dedupe_file_compare(src_file, src_off, dst_file,
						  dst_off, len, &same);
		if (ret)
			return ret;
		if (!same)
			return -EBADE;
		/* TODO(locked version) */
		ret = __zpl_clone_file_range(src_file, src_off, dst_file,
						dst_off, len);
		zfs_rangelock_exit(src_zlr);
		zfs_rangelock_exit(dst_zlr);
		return ret;
	}

	return (__zpl_clone_file_range(src_file, src_off, dst_file,
		dst_off, len));
	}
#endif /* HAVE_VFS_REMAP_FILE_RANGE */

#if defined(HAVE_VFS_CLONE_FILE_RANGE) || \
    defined(HAVE_VFS_FILE_OPERATIONS_EXTEND)
/*
 * Entry point for FICLONE and FICLONERANGE, before Linux 4.20.
 */
int
zpl_clone_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, uint64_t len)
{
	/* Zero length means to clone everything to the end of the file */
	if (len == 0)
		len = i_size_read(file_inode(src_file)) - src_off;

	return (__zpl_clone_file_range(src_file, src_off,
	    dst_file, dst_off, len));
}
#endif /* HAVE_VFS_CLONE_FILE_RANGE || HAVE_VFS_FILE_OPERATIONS_EXTEND */

#ifdef HAVE_VFS_DEDUPE_FILE_RANGE
/*
 * Entry point for FIDEDUPERANGE, before Linux 4.20.
 */
int
zpl_dedupe_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, uint64_t len)
{
	/* No support for dedup yet */
	return (-EOPNOTSUPP);
}
#endif /* HAVE_VFS_DEDUPE_FILE_RANGE */

/* Entry point for FICLONE, before Linux 4.5. */
long
zpl_ioctl_ficlone(struct file *dst_file, void *arg)
{
	unsigned long sfd = (unsigned long)arg;

	struct file *src_file = fget(sfd);
	if (src_file == NULL)
		return (-EBADF);

	if (dst_file->f_op != src_file->f_op) {
		fput(src_file);
		return (-EXDEV);
	}
	size_t len = i_size_read(file_inode(src_file));

	ssize_t ret =
	    __zpl_clone_file_range(src_file, 0, dst_file, 0, len);

	fput(src_file);

	if (ret < 0) {
		if (ret == -EOPNOTSUPP)
			return (-ENOTTY);
		return (ret);
	}

	if (ret != len)
		return (-EINVAL);

	return (0);
}

/* Entry point for FICLONERANGE, before Linux 4.5. */
long
zpl_ioctl_ficlonerange(struct file *dst_file, void __user *arg)
{
	zfs_ioc_compat_file_clone_range_t fcr;

	if (copy_from_user(&fcr, arg, sizeof (fcr)))
		return (-EFAULT);

	struct file *src_file = fget(fcr.fcr_src_fd);
	if (src_file == NULL)
		return (-EBADF);

	if (dst_file->f_op != src_file->f_op) {
		fput(src_file);
		return (-EXDEV);
	}

	size_t len = fcr.fcr_src_length;
	if (len == 0)
		len = i_size_read(file_inode(src_file)) - fcr.fcr_src_offset;

	ssize_t ret = __zpl_clone_file_range(src_file, fcr.fcr_src_offset,
	    dst_file, fcr.fcr_dest_offset, len);

	fput(src_file);

	if (ret < 0) {
		if (ret == -EOPNOTSUPP)
			return (-ENOTTY);
		return (ret);
	}

	if (ret != len)
		return (-EINVAL);

	return (0);
}

/* Entry point for FIDEDUPERANGE, before Linux 4.5. */
long
zpl_ioctl_fideduperange(struct file *src_file, void *arg) {
	zfs_ioc_compat_dedupe_range_t dup;
	int i;

	if (copy_from_user(&dup, arg, sizeof(dup)))
		return (-EFAULT);

	u16 count = dup.fdr_dest_count;
	struct inode *src_inode = file_inode(src_file);

	/* Nothing to duplicate to */
	if (count == 0)
		return -EINVAL;

	/* Check the src file */
	if (!(src_file->f_mode & FMODE_READ))
		return -EINVAL;

	if (S_ISDIR(src_inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(src_inode->i_mode))
		return -EINVAL;

	if (dup.fdr_src_offset + dup.fdr_src_length > i_size_read(src_inode))
		return -EINVAL;

	/* Check the dup structure */
	if (dup.fdr_reserved1 || dup.fdr_reserved2)
		return -EINVAL;

	/* Set output values to safe results */
	for (i = 0; i < count; i++) {
		dup.fdr_info[i].fdri_bytes_deduped = 0ULL;
		dup.fdr_info[i].fdri_status = FILE_DEDUPE_RANGE_SAME;
	}

	for (i = 0; i < count; i++) {
		struct fd dst_fd = fdget(dup.fdr_info[i].fdri_dest_fd);
		struct file *dst_file = dst_fd.file;

		if (!dst_file) {
			dup.fdr_info[i].fdri_status = -EBADF;
			continue;
		}
		if (dup.fdr_info[i].fdri_reserved) {
			dup.fdr_info[i].fdri_status = -EINVAL;
			goto do_fdput;
		}
		loff_t deduped =
		    zpl_remap_file_range(src_file, dup.fdr_src_offset, dst_file,
					 dup.fdr_info[i].fdri_dest_offset,
					 dup.fdr_src_length, REMAP_FILE_DEDUP);
		if (deduped == -EBADE) {
			dup.fdr_info[i].fdri_status = FILE_DEDUPE_RANGE_DIFFERS;
		} else if (deduped < 0) {
			dup.fdr_info[i].fdri_status = deduped;
		} else {
			dup.fdr_info[i].fdri_bytes_deduped = dup.fdr_src_length;
		}
	do_fdput:
		fdput(dst_fd);
	}
	return 0;
}

int
zpl_dedupe_file_compare(struct file *src_file, loff_t src_off,
			struct file *dst_file, loff_t dst_off, uint64_t len,
			bool *is_same) {
	bool same = true;
	int err = 0;
	znode_t *src_znode = ITOZ(file_inode(src_file));
	znode_t *dst_znode = ITOZ(file_inode(dst_file));
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	void *src_buf = kmem_zalloc(PAGE_SIZE, KM_SLEEP);
	void *dst_buf = kmem_zalloc(PAGE_SIZE, KM_SLEEP);


	while (len) {
		zfs_uio_t uio;

		uint64_t cmp_len = min(((uint64_t)PAGE_SIZE), len);

		if (cmp_len == 0)
			break;
		struct iovec iov;
		iov.iov_base = src_buf;
		iov.iov_len = cmp_len;

		zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE, cmp_len, 0);
		crhold(cr);
		cookie = spl_fstrans_mark();
		err = -zfs_read(src_znode, &uio, src_file->f_flags, cr);
		spl_fstrans_unmark(cookie);
		crfree(cr);

		if (err)
			goto done;
		iov.iov_base = dst_buf;
		iov.iov_len = cmp_len;
		crhold(cr);
		cookie = spl_fstrans_mark();
		err = -zfs_read(dst_znode, &uio, dst_file->f_flags, cr);
		spl_fstrans_unmark(cookie);
		crfree(cr);

		if (err)
			goto done;

		if (memcmp(src_buf, dst_buf, cmp_len))
			same = false;

		if (!same)
			break;

		src_off += cmp_len;
		dst_off += cmp_len;
		len -= cmp_len;
	}

	*is_same = same;

done:
	kmem_free(src_buf, PAGE_SIZE);
	kmem_free(dst_buf, PAGE_SIZE);
	return err;
}
