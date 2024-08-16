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
#ifdef HAVE_VFS_SPLICE_COPY_FILE_RANGE
#include <linux/splice.h>
#endif
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
zpl_clone_file_range_impl(struct file *src_file, loff_t src_off,
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

	if (!zfs_bclone_enabled)
		return (-EOPNOTSUPP);

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

	/* Flags is reserved for future extensions and must be zero. */
	if (flags != 0)
		return (-EINVAL);

	/* Try to do it via zfs_clone_range() and allow shortening. */
	ret = zpl_clone_file_range_impl(src_file, src_off,
	    dst_file, dst_off, len);

#if defined(HAVE_VFS_GENERIC_COPY_FILE_RANGE)
	/*
	 * Since Linux 5.3 the filesystem driver is responsible for executing
	 * an appropriate fallback, and a generic fallback function is provided.
	 */
	if (ret == -EOPNOTSUPP || ret == -EINVAL || ret == -EXDEV ||
	    ret == -EAGAIN)
		ret = generic_copy_file_range(src_file, src_off, dst_file,
		    dst_off, len, flags);
#elif defined(HAVE_VFS_SPLICE_COPY_FILE_RANGE)
	/*
	 * Since 6.8 the fallback function is called splice_copy_file_range
	 * and has a slightly different signature.
	 */
	if (ret == -EOPNOTSUPP || ret == -EINVAL || ret == -EXDEV ||
	    ret == -EAGAIN)
		ret = splice_copy_file_range(src_file, src_off, dst_file,
		    dst_off, len);
#else
	/*
	 * Before Linux 5.3 the filesystem has to return -EOPNOTSUPP to signal
	 * to the kernel that it should fallback to a content copy.
	 */
	if (ret == -EINVAL || ret == -EXDEV || ret == -EAGAIN)
		ret = -EOPNOTSUPP;
#endif /* HAVE_VFS_GENERIC_COPY_FILE_RANGE || HAVE_VFS_SPLICE_COPY_FILE_RANGE */

	return (ret);
}

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
 *
 * REMAP_FILE_CAN_SHORTEN lets us know we can clone less than the given range
 * if we want. It's designed for filesystems that may need to shorten the
 * length for alignment, EOF, or any other requirement. ZFS may shorten the
 * request when there is outstanding dirty data which hasn't been written.
 */
loff_t
zpl_remap_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, loff_t len, unsigned int flags)
{
	if (flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_CAN_SHORTEN))
		return (-EINVAL);

	/* No support for dedup yet */
	if (flags & REMAP_FILE_DEDUP)
		return (-EOPNOTSUPP);

	/* Zero length means to clone everything to the end of the file */
	if (len == 0)
		len = i_size_read(file_inode(src_file)) - src_off;

	ssize_t ret = zpl_clone_file_range_impl(src_file, src_off,
	    dst_file, dst_off, len);

	if (!(flags & REMAP_FILE_CAN_SHORTEN) && ret >= 0 && ret != len)
		ret = -EINVAL;

	return (ret);
}
#endif /* HAVE_VFS_REMAP_FILE_RANGE */

#if defined(HAVE_VFS_CLONE_FILE_RANGE)
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

	/* The entire length must be cloned or this is an error. */
	ssize_t ret = zpl_clone_file_range_impl(src_file, src_off,
	    dst_file, dst_off, len);

	if (ret >= 0 && ret != len)
		ret = -EINVAL;

	return (ret);
}
#endif /* HAVE_VFS_CLONE_FILE_RANGE */

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

	ssize_t ret = zpl_clone_file_range_impl(src_file, 0, dst_file, 0, len);

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

	ssize_t ret = zpl_clone_file_range_impl(src_file, fcr.fcr_src_offset,
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
zpl_ioctl_fideduperange(struct file *filp, void *arg)
{
	(void) arg;

	/* No support for dedup yet */
	return (-ENOTTY);
}
