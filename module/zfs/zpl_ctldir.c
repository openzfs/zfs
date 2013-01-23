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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 */

#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>

/*
 * Common open routine.  Disallow any write access.
 */
/* ARGSUSED */
static int
zpl_common_open(struct inode *ip, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		return (-EACCES);

	return generic_file_open(ip, filp);
}

static int
zpl_common_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *ip = dentry->d_inode;
	int error = 0;

	switch (filp->f_pos) {
	case 0:
		error = filldir(dirent, ".", 1, 0, ip->i_ino, DT_DIR);
		if (error)
			break;

		filp->f_pos++;
		/* fall-thru */
	case 1:
		error = filldir(dirent, "..", 2, 1, parent_ino(dentry), DT_DIR);
		if (error)
			break;

		filp->f_pos++;
		/* fall-thru */
	default:
		break;
	}

	return (error);
}

/*
 * Get root directory contents.
 */
static int
zpl_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *ip = dentry->d_inode;
	zfs_sb_t *zsb = ITOZSB(ip);
	int error = 0;

	ZFS_ENTER(zsb);

	switch (filp->f_pos) {
	case 0:
		error = filldir(dirent, ".", 1, 0, ip->i_ino, DT_DIR);
		if (error)
			goto out;

		filp->f_pos++;
		/* fall-thru */
	case 1:
		error = filldir(dirent, "..", 2, 1, parent_ino(dentry), DT_DIR);
		if (error)
			goto out;

		filp->f_pos++;
		/* fall-thru */
	case 2:
		error = filldir(dirent, ZFS_SNAPDIR_NAME,
		    strlen(ZFS_SNAPDIR_NAME), 2, ZFSCTL_INO_SNAPDIR, DT_DIR);
		if (error)
			goto out;

		filp->f_pos++;
		/* fall-thru */
	case 3:
		error = filldir(dirent, ZFS_SHAREDIR_NAME,
		    strlen(ZFS_SHAREDIR_NAME), 3, ZFSCTL_INO_SHARES, DT_DIR);
		if (error)
			goto out;

		filp->f_pos++;
		/* fall-thru */
	}
out:
	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Get root directory attributes.
 */
/* ARGSUSED */
static int
zpl_root_getattr(struct vfsmount *mnt, struct dentry *dentry,
    struct kstat *stat)
{
	int error;

	error = simple_getattr(mnt, dentry, stat);
	stat->atime = CURRENT_TIME;

	return (error);
}

static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_root_lookup(struct inode *dip, struct dentry *dentry, struct nameidata *nd)
#else
zpl_root_lookup(struct inode *dip, struct dentry *dentry, unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;

	crhold(cr);
	error = -zfsctl_root_lookup(dip, dname(dentry), &ip, 0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return d_splice_alias(NULL, dentry);
		else
			return ERR_PTR(error);
	}

        return d_splice_alias(ip, dentry);
}

/*
 * The '.zfs' control directory file and inode operations.
 */
const struct file_operations zpl_fops_root = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= zpl_root_readdir,
};

const struct inode_operations zpl_ops_root = {
	.lookup		= zpl_root_lookup,
	.getattr	= zpl_root_getattr,
};

#ifdef HAVE_AUTOMOUNT
static struct vfsmount *
zpl_snapdir_automount(struct path *path)
{
	struct dentry *dentry = path->dentry;
	int error;

	/*
	 * We must briefly disable automounts for this dentry because the
	 * user space mount utility will trigger another lookup on this
	 * directory.  That will result in zpl_snapdir_automount() being
	 * called repeatedly.  The DCACHE_NEED_AUTOMOUNT flag can be
	 * safely reset once the mount completes.
	 */
	dentry->d_flags &= ~DCACHE_NEED_AUTOMOUNT;
	error = -zfsctl_mount_snapshot(path, 0);
	dentry->d_flags |= DCACHE_NEED_AUTOMOUNT;
	if (error)
		return ERR_PTR(error);

	/*
	 * Rather than returning the new vfsmount for the snapshot we must
	 * return NULL to indicate a mount collision.  This is done because
	 * the user space mount calls do_add_mount() which adds the vfsmount
	 * to the name space.  If we returned the new mount here it would be
	 * added again to the vfsmount list resulting in list corruption.
	 */
	return (NULL);
}
#endif /* HAVE_AUTOMOUNT */

/*
 * Revalidate any dentry in the snapshot directory on lookup, since a snapshot
 * having the same name have been created or destroyed since it was cached.
 */
static int
#ifdef HAVE_D_REVALIDATE_NAMEIDATA
zpl_snapdir_revalidate(struct dentry *dentry, struct nameidata *i)
#else
zpl_snapdir_revalidate(struct dentry *dentry, unsigned int flags)
#endif
{
	return 0;
}

dentry_operations_t zpl_dops_snapdirs = {
/*
 * Auto mounting of snapshots is only supported for 2.6.37 and
 * newer kernels.  Prior to this kernel the ops->follow_link()
 * callback was used as a hack to trigger the mount.  The
 * resulting vfsmount was then explicitly grafted in to the
 * name space.  While it might be possible to add compatibility
 * code to accomplish this it would require considerable care.
 */
#ifdef HAVE_AUTOMOUNT
	.d_automount	= zpl_snapdir_automount,
#endif /* HAVE_AUTOMOUNT */
	.d_revalidate	= zpl_snapdir_revalidate,
};

static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_snapdir_lookup(struct inode *dip, struct dentry *dentry,
    struct nameidata *nd)
#else
zpl_snapdir_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
#endif

{
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	error = -zfsctl_snapdir_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	if (error && error != -ENOENT)
		return ERR_PTR(error);

	ASSERT(error == 0 || ip == NULL);
	d_clear_d_op(dentry);
	d_set_d_op(dentry, &zpl_dops_snapdirs);

	return d_splice_alias(ip, dentry);
}

/* ARGSUSED */
static int
zpl_snapdir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *dip = dentry->d_inode;
	zfs_sb_t *zsb = ITOZSB(dip);
	char snapname[MAXNAMELEN];
	uint64_t id, cookie;
	boolean_t case_conflict;
	int error = 0;

	ZFS_ENTER(zsb);

	cookie = filp->f_pos;
	switch (filp->f_pos) {
	case 0:
		error = filldir(dirent, ".", 1, 0, dip->i_ino, DT_DIR);
		if (error)
			goto out;

		filp->f_pos++;
		/* fall-thru */
	case 1:
		error = filldir(dirent, "..", 2, 1, parent_ino(dentry), DT_DIR);
		if (error)
			goto out;

		filp->f_pos++;
		/* fall-thru */
	default:
		while (error == 0) {
			error = -dmu_snapshot_list_next(zsb->z_os, MAXNAMELEN,
			    snapname, &id, &cookie, &case_conflict);
			if (error)
				goto out;

			error = filldir(dirent, snapname, strlen(snapname),
			    filp->f_pos, ZFSCTL_INO_SHARES - id, DT_DIR);
			if (error)
				goto out;

			filp->f_pos = cookie;
		}
	}
out:
	ZFS_EXIT(zsb);

	if (error == -ENOENT)
		return (0);

	return (error);
}

int
zpl_snapdir_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfsctl_snapdir_rename(sdip, dname(sdentry),
	    tdip, dname(tdentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

static int
zpl_snapdir_rmdir(struct inode *dip, struct dentry *dentry)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfsctl_snapdir_remove(dip, dname(dentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

static int
zpl_snapdir_mkdir(struct inode *dip, struct dentry *dentry, zpl_umode_t mode)
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr);

	error = -zfsctl_snapdir_mkdir(dip, dname(dentry), vap, &ip, cr, 0);
	if (error == 0) {
		d_clear_d_op(dentry);
		d_set_d_op(dentry, &zpl_dops_snapdirs);
		d_instantiate(dentry, ip);
	}

	kmem_free(vap, sizeof(vattr_t));
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

/*
 * Get snapshot directory attributes.
 */
/* ARGSUSED */
static int
zpl_snapdir_getattr(struct vfsmount *mnt, struct dentry *dentry,
    struct kstat *stat)
{
	zfs_sb_t *zsb = ITOZSB(dentry->d_inode);
	int error;

	ZFS_ENTER(zsb);
	error = simple_getattr(mnt, dentry, stat);
	stat->nlink = stat->size = avl_numnodes(&zsb->z_ctldir_snaps) + 2;
	stat->ctime = stat->mtime = dmu_objset_snap_cmtime(zsb->z_os);
	stat->atime = CURRENT_TIME;
	ZFS_EXIT(zsb);

	return (error);
}

/*
 * The '.zfs/snapshot' directory file operations.  These mainly control
 * generating the list of available snapshots when doing an 'ls' in the
 * directory.  See zpl_snapdir_readdir().
 */
const struct file_operations zpl_fops_snapdir = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= zpl_snapdir_readdir,
};

/*
 * The '.zfs/snapshot' directory inode operations.  These mainly control
 * creating an inode for a snapshot directory and initializing the needed
 * infrastructure to automount the snapshot.  See zpl_snapdir_lookup().
 */
const struct inode_operations zpl_ops_snapdir = {
	.lookup		= zpl_snapdir_lookup,
	.getattr	= zpl_snapdir_getattr,
	.rename		= zpl_snapdir_rename,
	.rmdir		= zpl_snapdir_rmdir,
	.mkdir		= zpl_snapdir_mkdir,
};

static struct dentry *
#ifdef HAVE_LOOKUP_NAMEIDATA
zpl_shares_lookup(struct inode *dip, struct dentry *dentry,
    struct nameidata *nd)
#else
zpl_shares_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	error = -zfsctl_shares_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return d_splice_alias(NULL, dentry);
		else
			return ERR_PTR(error);
	}

	return d_splice_alias(ip, dentry);
}

/* ARGSUSED */
static int
zpl_shares_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	cred_t *cr = CRED();
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *ip = dentry->d_inode;
	zfs_sb_t *zsb = ITOZSB(ip);
	znode_t *dzp;
	int error;

	ZFS_ENTER(zsb);

	if (zsb->z_shares_dir == 0) {
		error = zpl_common_readdir(filp, dirent, filldir);
		ZFS_EXIT(zsb);
		return (error);
	}

	error = -zfs_zget(zsb, zsb->z_shares_dir, &dzp);
	if (error) {
		ZFS_EXIT(zsb);
		return (error);
	}

	crhold(cr);
	error = -zfs_readdir(ZTOI(dzp), dirent, filldir, &filp->f_pos, cr);
	crfree(cr);

	iput(ZTOI(dzp));
	ZFS_EXIT(zsb);
	ASSERT3S(error, <=, 0);

	return (error);
}

/* ARGSUSED */
static int
zpl_shares_getattr(struct vfsmount *mnt, struct dentry *dentry,
    struct kstat *stat)
{
	struct inode *ip = dentry->d_inode;
	zfs_sb_t *zsb = ITOZSB(ip);
	znode_t *dzp;
	int error;

	ZFS_ENTER(zsb);

	if (zsb->z_shares_dir == 0) {
		error = simple_getattr(mnt, dentry, stat);
		stat->nlink = stat->size = 2;
		stat->atime = CURRENT_TIME;
		ZFS_EXIT(zsb);
		return (error);
	}

	error = -zfs_zget(zsb, zsb->z_shares_dir, &dzp);
	if (error == 0)
		error = -zfs_getattr_fast(dentry->d_inode, stat);

	iput(ZTOI(dzp));
	ZFS_EXIT(zsb);
	ASSERT3S(error, <=, 0);

	return (error);
}

/*
 * The '.zfs/shares' directory file operations.
 */
const struct file_operations zpl_fops_shares = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= zpl_shares_readdir,
};

/*
 * The '.zfs/shares' directory inode operations.
 */
const struct inode_operations zpl_ops_shares = {
	.lookup		= zpl_shares_lookup,
	.getattr	= zpl_shares_getattr,
};
