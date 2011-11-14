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
#include <sys/dmu.h>
#include <linux/version.h>
#include <linux/snapshots_automount.h>

struct inode *
zpl_snap_linux_iget(struct super_block *sb, unsigned long ino);

static struct vfsmount *
zpl_do_automount(struct dentry *mntpt)
{
	struct vfsmount *mnt = ERR_PTR(-ENOENT);
	char *snapname = NULL;
	char *zfs_fs_name = NULL;
	zfs_sb_t *zsb = ITOZSB(mntpt->d_inode);

	ASSERT(mntpt->d_parent);
	zfs_fs_name = kzalloc(MAXNAMELEN, KM_SLEEP);
	dmu_objset_name(zsb->z_os, zfs_fs_name);
	snapname = kmem_asprintf("%s@%s", zfs_fs_name, mntpt->d_name.name);
	mnt = linux_kern_mount(&zpl_fs_type, 0, snapname, NULL);
	kfree(zfs_fs_name);
	kfree(snapname);
	return mnt;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)

struct vfsmount *zpl_d_automount(struct path *path)
{
	struct vfsmount *newmnt;

	newmnt = zpl_do_automount(path->dentry);
	if (IS_ERR(newmnt)) {
		return newmnt;
	}
	mntget(newmnt); /* prevent immediate expiration */
	return newmnt;
}

const struct dentry_operations zpl_dentry_ops = {
	.d_automount = zpl_d_automount,
};

#else

static void*
zpl_snapshots_dir_mountpoint_follow_link(struct dentry *dentry,
					struct nameidata *nd)
{
	struct vfsmount *mnt = ERR_PTR(-ENOENT);
	mnt = zpl_do_automount(dentry);
	mntget(mnt);
	rc = PTR_ERR(mnt);
	if (IS_ERR(mnt)) {
		goto out_err;
	}
	mnt->mnt_mountpoint = dentry;
	ASSERT(nd);
	rc = linux_add_mount(mnt, nd,
			nd->path.mnt->mnt_flags | MNT_READONLY, NULL);
	switch (rc) {
	case 0:
		path_put(&nd->path);
		nd->path.mnt = mnt;
		nd->path.dentry = dget(mnt->mnt_root);
		break;
	case -EBUSY:
		    nd->path.dentry = dget(mnt->mnt_root);

	/* someone else made a mount here whilst we were busy */

#ifdef HAVE_2ARGS_FOLLOW_DOWN           /* for kernel version < 2.6.31 */
		    while (d_mountpoint(nd->path.dentry) &&
			follow_down(&mnt, &nd->path.dentry)) {
			;
		    }
#else
		    while (d_mountpoint(nd->path.dentry) &&
			follow_down(&nd->path)) {
			;
		    }
#endif
		rc = 0;
	default:
		mntput(mnt);
		break;
	}
	goto out;
out_err:
	path_put(&nd->path);

out:
	return ERR_PTR(rc);
}

const struct inode_operations zpl_snapshots_dir_inode_operations = {
	.follow_link    = zpl_snapshots_dir_mountpoint_follow_link,
};

#endif

static int
zpl_snap_dir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *dir = filp->f_path.dentry->d_inode;
	char snapname[MAXNAMELEN];
	uint64_t id, cookie;
	boolean_t case_conflict;
	int error, rc;
	zfs_sb_t *zsb = ITOZSB(dir);

	cookie = filp->f_pos;
	rc = error = 0;
	if (!filp->f_pos) {
		rc = filldir(dirent, ".", 1, filp->f_pos, dir->i_ino, DT_DIR);
		if(rc)
			goto done;
		filp->f_pos++;
	}
	if (filp->f_pos == 1) {
		rc = filldir(dirent, "..", 2, filp->f_pos,
			parent_ino(filp->f_path.dentry), DT_DIR);
		if(rc)
			goto done;
		filp->f_pos++;
	}

	while (!(error = dmu_snapshot_list_next(zsb->z_os,MAXNAMELEN, snapname,
						&id, &cookie,
						&case_conflict))) {
	ASSERT(id > 0);
	rc = filldir(dirent, snapname, strlen(snapname), filp->f_pos,
			ZFSCTL_INO_SHARES - id, DT_DIR);
	filp->f_pos = cookie; // next position ptr
	}
	if (error) {
		if (error == ENOENT) {
			return 0;
	}
		return -error;
	}
done:
	return 0;
}

static struct dentry *
zpl_snap_dir_lookup(struct inode *dir,struct dentry *dentry,
			struct nameidata *nd)
{
	struct inode *ip = NULL;
	uint64_t id;
	struct dentry *dentry_to_return = NULL;
	zfs_sb_t *zsb = ITOZSB(dir);

	if (dentry->d_name.len >= MAXNAMELEN) {
		return ERR_PTR(-ENAMETOOLONG);
	}

	 if (!(id = dmu_snapname_to_id(zsb->z_os, dentry->d_name.name))) {
		d_add(dentry, NULL);
		return NULL;
	}
	ip = zpl_snap_linux_iget(zsb->z_sb,
					ZFSCTL_INO_SHARES - id);
	if(unlikely(IS_ERR(ip))) {
		return ERR_CAST(ip);
	}
	dentry_to_return = d_splice_alias(ip, dentry);
	d_set_d_op(dentry, &zpl_dentry_ops);
	return dentry_to_return;
}

/*
 * .zfs/snapshot dir, file operations
 */

const struct file_operations zpl_snap_dir_file_operations = {
	.read           = generic_read_dir,
	.readdir        = zpl_snap_dir_readdir,
};

/*
 * .zfs/snapshot dir, inode operations
 */
struct inode_operations zpl_snap_dir_inode_operations = {
	.lookup = zpl_snap_dir_lookup,
};


/*
 * readdir for .zfs dir
 */

static int
zpl_zfsctl_dir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	u64 ino;
	int i = filp->f_pos;

	switch (i) {
	case 0:
		ino = dentry->d_inode->i_ino;
		if(filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
			break;
		filp->f_pos++;
		i++;
	case 1:
		ino = parent_ino(dentry);
		if(filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
			break;
		filp->f_pos++;
		i++;
	case 2:
		if(filldir(dirent, ZFS_SNAPDIR_NAME, strlen(ZFS_SNAPDIR_NAME),
				i, ZFSCTL_INO_SNAPDIR, DT_DIR) < 0)
			break;
		filp->f_pos++;
	}
	return 0;
}

/*
 * lookup for .zfs dir (contains only one dir viz. snapshot)
 */

static struct dentry *
zpl_zfsctl_dir_lookup(struct inode *dir,struct dentry *dentry,
                       struct nameidata *nd)
{
	struct inode *inode = NULL;

	if(dentry->d_name.len >= MAXNAMELEN) {
		return ERR_PTR(-ENAMETOOLONG);
	}
	if(strcmp(dentry->d_name.name, ZFS_SNAPDIR_NAME) == 0) {
		inode = ilookup(dir->i_sb, ZFSCTL_INO_SNAPDIR);
		if(!inode) {
			return NULL;
		}
		return d_splice_alias(inode, dentry);
	} else {
		return d_splice_alias(NULL, dentry);
	}
}

/*
 * .zfs dir file operations
 */

const struct file_operations zpl_zfsctl_dir_file_operations = {
	.read           = generic_read_dir,
	.readdir        = zpl_zfsctl_dir_readdir,
};

/*
 * .zfs dir inode operations
 */

const struct inode_operations zpl_zfsctl_dir_inode_operations = {
	.lookup         = zpl_zfsctl_dir_lookup,
};

struct inode *
zpl_snap_linux_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	inode->i_mode = (S_IFDIR | S_IRWXU);
	inode->i_uid = crgetuid(current->cred);
	inode->i_gid = crgetgid(current->cred);
	inode->i_sb = sb;
	inode->i_private = inode;

	if(inode->i_ino == ZFSCTL_INO_ROOT) {
		inode->i_op = &zpl_zfsctl_dir_inode_operations;
		inode->i_fop = &zpl_zfsctl_dir_file_operations;
	}
	else if(inode->i_ino == ZFSCTL_INO_SNAPDIR) {
		inode->i_op = &zpl_snap_dir_inode_operations;
		inode->i_fop = &zpl_snap_dir_file_operations;
	}
	else {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
		inode->i_op = &zpl_snapshots_dir_inode_operations;
#else
		inode->i_flags |= S_AUTOMOUNT;
#endif
		inode->i_fop = &simple_dir_operations;
	}
	unlock_new_inode(inode);
	return inode;
}

void
zpl_snap_create(zfs_sb_t *zsb)
{
	struct inode *ip_ctl_dir = NULL;
	struct inode *ip_snap_dir = NULL;
	struct dentry *dentry_ctl_dir = NULL;
	struct dentry *dentry_snap_dir = NULL;

	ip_ctl_dir = zpl_snap_linux_iget(zsb->z_sb, ZFSCTL_INO_ROOT);

	ASSERT(!IS_ERR(ip_ctl_dir));

	/* for .zfs dir, root dentry is the parent one */
	ASSERT(zsb->z_sb->s_root);
	dentry_ctl_dir = d_alloc_name(zsb->z_sb->s_root, ZFS_CTLDIR_NAME);

	/* If failed, indicates ENOMEM error */
	ASSERT(dentry_ctl_dir);
	d_add(dentry_ctl_dir, ip_ctl_dir);

	zsb->z_snap_linux.zsl_ctldir_dentry = dentry_ctl_dir;
	zsb->z_snap_linux.zsl_ctldir_ip = ip_ctl_dir;

	ip_snap_dir = zpl_snap_linux_iget(zsb->z_sb, ZFSCTL_INO_SNAPDIR);

	/* If failed, indicates ENOMEM error */
	ASSERT(!IS_ERR(ip_ctl_dir));
	zsb->z_snap_linux.zsl_snapdir_ip = ip_snap_dir;

	/* for .zfs/snapshot dir, .zfs dentry is the parent one */
	dentry_snap_dir = d_alloc_name(dentry_ctl_dir, ZFS_CTLDIR_NAME);

	/* If failed, indicates ENOMEM error */
	ASSERT(dentry_snap_dir);
	d_add(dentry_snap_dir, ip_snap_dir);

	zsb->z_snap_linux.zsl_snapdir_dentry = dentry_snap_dir;
}
void
zpl_snap_destroy(zfs_sb_t *zsb)
{
	ASSERT(zsb->z_snap_linux.zsl_snapdir_ip);
	drop_nlink(zsb->z_snap_linux.zsl_snapdir_ip);
	ASSERT(zsb->z_snap_linux.zsl_snapdir_dentry);
	dput(zsb->z_snap_linux.zsl_snapdir_dentry);
	ASSERT(zsb->z_snap_linux.zsl_ctldir_ip);
	drop_nlink(zsb->z_snap_linux.zsl_ctldir_ip);
	ASSERT(zsb->z_snap_linux.zsl_ctldir_dentry);
	dput(zsb->z_snap_linux.zsl_ctldir_dentry);
}

