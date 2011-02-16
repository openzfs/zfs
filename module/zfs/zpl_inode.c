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
#include <sys/vfs.h>
#include <sys/zpl.h>


static struct dentry *
zpl_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct inode *ip;
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_lookup(dir, dname(dentry), &ip, 0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	put_cred(cr);

	if (error) {
		if (error == -ENOENT)
			return d_splice_alias(NULL, dentry);
		else
			return ERR_PTR(error);
	}

	return d_splice_alias(ip, dentry);
}

static int
zpl_create(struct inode *dir, struct dentry *dentry, int mode,
    struct nameidata *nd)
{
	const struct cred *cred;
	struct inode *ip;
	vattr_t *vap;
	int error;

	cred = get_current_cred();
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	vap->va_mode = mode;
	vap->va_mask = ATTR_MODE;
	vap->va_uid = current_fsuid();
	vap->va_gid = current_fsgid();

	error = -zfs_create(dir, (char *)dentry->d_name.name,
	    vap, 0, mode, &ip, (struct cred *)cred, 0, NULL);
	if (error)
		goto out;

	d_instantiate(dentry, ip);
out:
	kmem_free(vap, sizeof(vattr_t));
	put_cred(cred);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
        const struct cred *cred;
	struct inode *ip;
	vattr_t *vap;
	int error;

	cred = get_current_cred();
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	vap->va_mode = mode;
	vap->va_mask = ATTR_MODE;
	vap->va_rdev = rdev;
	vap->va_uid = current_fsuid();
	vap->va_gid = current_fsgid();

	error = -zfs_create(dir, (char *)dentry->d_name.name,
	    vap, 0, mode, &ip, (struct cred *)cred, 0, NULL);
	if (error)
		goto out;

	d_instantiate(dentry, ip);
out:
	kmem_free(vap, sizeof(vattr_t));
	put_cred(cred);
	ASSERT3S(error, <=, 0);

	return (-error);
}

static int
zpl_unlink(struct inode *dir, struct dentry *dentry)
{
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_remove(dir, dname(dentry), cr);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	cred_t *cr;
	vattr_t *vap;
	struct inode *ip;
	int error;

	cr = (cred_t *)get_current_cred();
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	vap->va_mode = S_IFDIR | mode;
	vap->va_mask = ATTR_MODE;
	vap->va_uid = current_fsuid();
	vap->va_gid = current_fsgid();

	error = -zfs_mkdir(dir, dname(dentry), vap, &ip, cr, 0, NULL);
	if (error)
		goto out;

	d_instantiate(dentry, ip);
out:
	kmem_free(vap, sizeof(vattr_t));
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_rmdir(struct inode * dir, struct dentry *dentry)
{
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_rmdir(dir, dname(dentry), NULL, cr, 0);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_getattr(dentry->d_inode, stat, 0, cr);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_setattr(struct dentry *dentry, struct iattr *attr)
{
	cred_t *cr;
	int error;

	error = inode_change_ok(dentry->d_inode, attr);
	if (error)
		return (error);

	cr = (cred_t *)get_current_cred();
	error = -zfs_setattr(dentry->d_inode, attr, 0, cr);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (-error);
}

static int
zpl_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	cred_t *cr;
	int error;

	cr = (cred_t *)get_current_cred();
	error = -zfs_rename(sdip, dname(sdentry), tdip, dname(tdentry), cr, 0);
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	cred_t *cr;
	vattr_t *vap;
	struct inode *ip;
	int error;

	cr = (cred_t *)get_current_cred();
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	vap->va_mode = S_IFLNK | S_IRWXUGO;
	vap->va_mask = ATTR_MODE;
	vap->va_uid = current_fsuid();
	vap->va_gid = current_fsgid();

	error = -zfs_symlink(dir, dname(dentry), vap, (char *)name, &ip, cr, 0);
	if (error)
		goto out;

	d_instantiate(dentry, ip);
out:
	kmem_free(vap, sizeof(vattr_t));
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static void *
zpl_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	(void) zfs_follow_link(dentry, nd);
	return NULL;
}

static void
zpl_put_link(struct dentry *dentry, struct nameidata *nd, void *ptr)
{
	char *link;

	link = nd_get_link(nd);
	if (!IS_ERR(link))
		kmem_free(link, MAXPATHLEN + 1);
}

static int
zpl_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *ip = old_dentry->d_inode;
	cred_t *cr;
	int error;

	if (ip->i_nlink >= ZFS_LINK_MAX)
		return -EMLINK;

	cr = (cred_t *)get_current_cred();
	ip->i_ctime = CURRENT_TIME_SEC;
	igrab(ip); /* Use ihold() if available */

	error = -zfs_link(dir, ip, dname(dentry), cr);
	if (error) {
		iput(ip);
		goto out;
	}

	d_instantiate(dentry, ip);
out:
	put_cred(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

const struct inode_operations zpl_inode_operations = {
	.create		= zpl_create,
	.link		= zpl_link,
	.unlink		= zpl_unlink,
	.symlink	= zpl_symlink,
	.mkdir		= zpl_mkdir,
	.rmdir		= zpl_rmdir,
	.mknod		= zpl_mknod,
	.rename		= zpl_rename,
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
	.listxattr	= zpl_xattr_list,
};

const struct inode_operations zpl_dir_inode_operations = {
	.create		= zpl_create,
	.lookup		= zpl_lookup,
	.link		= zpl_link,
	.unlink		= zpl_unlink,
	.symlink	= zpl_symlink,
	.mkdir		= zpl_mkdir,
	.rmdir		= zpl_rmdir,
	.mknod		= zpl_mknod,
	.rename		= zpl_rename,
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
	.listxattr	= zpl_xattr_list,
};

const struct inode_operations zpl_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= zpl_follow_link,
	.put_link	= zpl_put_link,
};

const struct inode_operations zpl_special_inode_operations = {
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
	.listxattr	= zpl_xattr_list,
};
