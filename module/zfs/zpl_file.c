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


#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zpl.h>


static int
zpl_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_readdir(dentry->d_inode, dirent, filldir,
	    &filp->f_pos, cr);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_fsync(filp->f_path.dentry->d_inode, datasync, cr);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

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
	cred_t *cr;
	ssize_t read;

	cr = (cred_t *)get_current_cred();
	read = zpl_read_common(filp->f_mapping->host, buf, len, *ppos,
	    UIO_USERSPACE, filp->f_flags, cr);
	put_cred(cr);

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
	cred_t *cr;
	ssize_t wrote;

	cr = (cred_t *)get_current_cred();
	wrote = zpl_write_common(filp->f_mapping->host, buf, len, *ppos,
	    UIO_USERSPACE, filp->f_flags, cr);
	put_cred(cr);

	if (wrote < 0)
		return (wrote);

	*ppos += wrote;
	return (wrote);
}

const struct address_space_operations zpl_address_space_operations = {
#if 0
	.readpage	= zpl_readpage,
	.writepage	= zpl_writepage,
	.direct_IO	= zpl_direct_IO,
#endif
};

const struct file_operations zpl_file_operations = {
	.open		= generic_file_open,
	.llseek		= generic_file_llseek,
	.read		= zpl_read,	/* do_sync_read */
	.write		= zpl_write,	/* do_sync_write */
	.readdir	= zpl_readdir,
	.mmap		= generic_file_mmap,
	.fsync		= zpl_fsync,
	.aio_read	= NULL,		/* generic_file_aio_read */
	.aio_write	= NULL,		/* generic_file_aio_write */
};

const struct file_operations zpl_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= zpl_readdir,
	.fsync		= zpl_fsync,
};
