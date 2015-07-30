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

#ifndef _ZFS_FILE_COMPAT_H
#define	_ZFS_FILE_COMPAT_H

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define	file_open(name, fl, mode)	filp_open(name, fl, mode)
#define	file_close(f)			filp_close(f, NULL)
#define	file_pos(f)			((f)->f_pos)
#define	file_dentry(f)			((f)->f_path.dentry)

static inline ssize_t
file_read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	mm_segment_t saved_fs = get_fs();
	ssize_t ret;

	set_fs(get_ds());
	ret = vfs_read(fp, buf, count, pos);
	set_fs(saved_fs);

	return (ret);
}

static inline ssize_t
file_write(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	mm_segment_t saved_fs = get_fs();
	ssize_t ret;

	set_fs(get_ds());
	ret = vfs_write(fp, buf, count, pos);
	set_fs(saved_fs);

	return (ret);
}

#ifdef HAVE_2ARGS_VFS_UNLINK
#define	file_unlink(ip, dp)		vfs_unlink(ip, dp)
#else
#define	file_unlink(ip, dp)		vfs_unlink(ip, dp, NULL)
#endif /* HAVE_2ARGS_VFS_UNLINK */

#ifdef HAVE_2ARGS_VFS_GETATTR
#define	file_stat(fp, st)		vfs_getattr(&(fp)->f_path, st)
#else
#define	file_stat(fp, st)		vfs_getattr((fp)->f_path.mnt, \
					    (fp)->f_dentry, st)
#endif /* HAVE_2ARGS_VFS_GETATTR */

#ifdef HAVE_2ARGS_VFS_FSYNC
#define	file_fsync(fp, sync)		vfs_fsync(fp, sync)
#else
#define	file_fsync(fp, sync)		vfs_fsync(fp, (fp)->f_dentry, sync)
#endif /* HAVE_2ARGS_VFS_FSYNC */

#endif /* ZFS_FILE_COMPAT_H */
