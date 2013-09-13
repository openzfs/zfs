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
#include <sys/vfs.h>
#include <sys/zpl.h>


static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
#else
zpl_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;

	if (dlen(dentry) > ZFS_MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	crhold(cr);
	error = -zfs_lookup(dir, dname(dentry), &ip, 0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	spin_lock(&dentry->d_lock);
	dentry->d_time = jiffies;
#ifndef HAVE_S_D_OP
	d_set_d_op(dentry, &zpl_dentry_operations);
#endif /* HAVE_S_D_OP */
	spin_unlock(&dentry->d_lock);

	if (error) {
		if (error == -ENOENT)
			return d_splice_alias(NULL, dentry);
		else
			return ERR_PTR(error);
	}

	return d_splice_alias(ip, dentry);
}

void
zpl_vap_init(vattr_t *vap, struct inode *dir, zpl_umode_t mode, cred_t *cr)
{
	vap->va_mask = ATTR_MODE;
	vap->va_mode = mode;
	vap->va_uid = crgetfsuid(cr);

	if (dir && dir->i_mode & S_ISGID) {
		vap->va_gid = KGID_TO_SGID(dir->i_gid);
		if (S_ISDIR(mode))
			vap->va_mode |= S_ISGID;
	} else {
		vap->va_gid = crgetfsgid(cr);
	}
}

static int
#ifdef HAVE_CREATE_NAMEIDATA
zpl_create(struct inode *dir, struct dentry *dentry, zpl_umode_t mode,
    struct nameidata *nd)
#else
zpl_create(struct inode *dir, struct dentry *dentry, zpl_umode_t mode,
    bool flag)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip;
	vattr_t *vap;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, mode, cr);

	error = -zfs_create(dir, dname(dentry), vap, 0, mode, &ip, cr, 0, NULL);
	if (error == 0) {
		error = zpl_xattr_security_init(ip, dir, &dentry->d_name);
		VERIFY3S(error, ==, 0);
		d_instantiate(dentry, ip);
	}

	kmem_free(vap, sizeof(vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_mknod(struct inode *dir, struct dentry *dentry, zpl_umode_t mode,
    dev_t rdev)
{
	cred_t *cr = CRED();
	struct inode *ip;
	vattr_t *vap;
	int error;

	/*
	 * We currently expect Linux to supply rdev=0 for all sockets
	 * and fifos, but we want to know if this behavior ever changes.
	 */
	if (S_ISSOCK(mode) || S_ISFIFO(mode))
		ASSERT(rdev == 0);

	crhold(cr);
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, mode, cr);
	vap->va_rdev = rdev;

	error = -zfs_create(dir, dname(dentry), vap, 0, mode, &ip, cr, 0, NULL);
	if (error == 0)
		d_instantiate(dentry, ip);

	kmem_free(vap, sizeof(vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_unlink(struct inode *dir, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_remove(dir, dname(dentry), cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_mkdir(struct inode *dir, struct dentry *dentry, zpl_umode_t mode)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, mode | S_IFDIR, cr);

	error = -zfs_mkdir(dir, dname(dentry), vap, &ip, cr, 0, NULL);
	if (error == 0)
		d_instantiate(dentry, ip);

	kmem_free(vap, sizeof(vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_rmdir(struct inode * dir, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_rmdir(dir, dname(dentry), NULL, cr, 0);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	boolean_t issnap = ITOZSB(dentry->d_inode)->z_issnap;
	int error;

	/*
	 * Ensure MNT_SHRINKABLE is set on snapshots to ensure they are
	 * unmounted automatically with the parent file system.  This
	 * is done on the first getattr because it's not easy to get the
	 * vfsmount structure at mount time.  This call path is explicitly
	 * marked unlikely to avoid any performance impact.  FWIW, ext4
	 * resorts to a similar trick for sysadmin convenience.
	 */
	if (unlikely(issnap && !(mnt->mnt_flags & MNT_SHRINKABLE)))
		mnt->mnt_flags |= MNT_SHRINKABLE;

	error = -zfs_getattr_fast(dentry->d_inode, stat);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_setattr(struct dentry *dentry, struct iattr *ia)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	int error;

	error = inode_change_ok(dentry->d_inode, ia);
	if (error)
		return (error);

	crhold(cr);
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	vap->va_mask = ia->ia_valid & ATTR_IATTR_MASK;
	vap->va_mode = ia->ia_mode;
	vap->va_uid = KUID_TO_SUID(ia->ia_uid);
	vap->va_gid = KGID_TO_SGID(ia->ia_gid);
	vap->va_size = ia->ia_size;
	vap->va_atime = ia->ia_atime;
	vap->va_mtime = ia->ia_mtime;
	vap->va_ctime = ia->ia_ctime;

	error = -zfs_setattr(dentry->d_inode, vap, 0, cr);

	kmem_free(vap, sizeof(vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_rename(sdip, dname(sdentry), tdip, dname(tdentry), cr, 0);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dir, S_IFLNK | S_IRWXUGO, cr);

	error = -zfs_symlink(dir, dname(dentry), vap, (char *)name, &ip, cr, 0);
	if (error == 0)
		d_instantiate(dentry, ip);

	kmem_free(vap, sizeof(vattr_t));
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static void *
zpl_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	cred_t *cr = CRED();
	struct inode *ip = dentry->d_inode;
	struct iovec iov;
	uio_t uio;
	char *link;
	int error;

	crhold(cr);

	iov.iov_len = MAXPATHLEN;
	iov.iov_base = link = kmem_zalloc(MAXPATHLEN, KM_SLEEP);

	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = (MAXPATHLEN - 1);
	uio.uio_segflg = UIO_SYSSPACE;

	error = -zfs_readlink(ip, &uio, cr);
	if (error) {
		kmem_free(link, MAXPATHLEN);
		nd_set_link(nd, ERR_PTR(error));
	} else {
		nd_set_link(nd, link);
	}

	crfree(cr);
	return (NULL);
}

static void
zpl_put_link(struct dentry *dentry, struct nameidata *nd, void *ptr)
{
	const char *link = nd_get_link(nd);

	if (!IS_ERR(link))
		kmem_free(link, MAXPATHLEN);
}

static int
zpl_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	cred_t *cr = CRED();
	struct inode *ip = old_dentry->d_inode;
	int error;

	if (ip->i_nlink >= ZFS_LINK_MAX)
		return -EMLINK;

	crhold(cr);
	ip->i_ctime = CURRENT_TIME_SEC;
	igrab(ip); /* Use ihold() if available */

	error = -zfs_link(dir, ip, dname(dentry), cr);
	if (error) {
		iput(ip);
		goto out;
	}

	d_instantiate(dentry, ip);
out:
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifdef HAVE_INODE_TRUNCATE_RANGE
static void
zpl_truncate_range(struct inode* ip, loff_t start, loff_t end)
{
	cred_t *cr = CRED();
	flock64_t bf;

	ASSERT3S(start, <=, end);

	/*
	 * zfs_freesp() will interpret (len == 0) as meaning "truncate until
	 * the end of the file". We don't want that.
	 */
	if (start == end)
		return;

	crhold(cr);

	bf.l_type = F_WRLCK;
	bf.l_whence = 0;
	bf.l_start = start;
	bf.l_len = end - start;
	bf.l_pid = 0;
	zfs_space(ip, F_FREESP, &bf, FWRITE, start, cr);

	crfree(cr);
}
#endif /* HAVE_INODE_TRUNCATE_RANGE */

#ifdef HAVE_INODE_FALLOCATE
static long
zpl_fallocate(struct inode *ip, int mode, loff_t offset, loff_t len)
{
	return zpl_fallocate_common(ip, mode, offset, len);
}
#endif /* HAVE_INODE_FALLOCATE */

static int
#ifdef HAVE_D_REVALIDATE_NAMEIDATA
zpl_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	unsigned int flags = (nd ? nd->flags : 0);
#else
zpl_revalidate(struct dentry *dentry, unsigned int flags)
{
#endif /* HAVE_D_REVALIDATE_NAMEIDATA */
	zfs_sb_t *zsb = dentry->d_sb->s_fs_info;
	int error;

	if (flags & LOOKUP_RCU)
		return (-ECHILD);

	/*
	 * After a rollback negative dentries created before the rollback
	 * time must be invalidated.  Otherwise they can obscure files which
	 * are only present in the rolled back dataset.
	 */
	if (dentry->d_inode == NULL) {
		spin_lock(&dentry->d_lock);
		error = time_before(dentry->d_time, zsb->z_rollback_time);
		spin_unlock(&dentry->d_lock);

		if (error)
			return (0);
	}

	/*
	 * The dentry may reference a stale inode if a mounted file system
	 * was rolled back to a point in time where the object didn't exist.
	 */
	if (dentry->d_inode && ITOZ(dentry->d_inode)->z_is_stale)
		return (0);

	return (1);
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
#ifdef HAVE_INODE_TRUNCATE_RANGE
	.truncate_range = zpl_truncate_range,
#endif /* HAVE_INODE_TRUNCATE_RANGE */
#ifdef HAVE_INODE_FALLOCATE
	.fallocate	= zpl_fallocate,
#endif /* HAVE_INODE_FALLOCATE */
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
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
	.listxattr	= zpl_xattr_list,
};

const struct inode_operations zpl_special_inode_operations = {
	.setattr	= zpl_setattr,
	.getattr	= zpl_getattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.removexattr	= generic_removexattr,
	.listxattr	= zpl_xattr_list,
};

dentry_operations_t zpl_dentry_operations = {
	.d_revalidate	= zpl_revalidate,
};
