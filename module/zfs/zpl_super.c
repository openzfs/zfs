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


static struct inode *
zpl_inode_alloc(struct super_block *sb)
{
	struct inode *ip;

	VERIFY3S(zfs_inode_alloc(sb, &ip), ==, 0);
	ip->i_version = 1;

	return (ip);
}

static void
zpl_inode_destroy(struct inode *ip)
{
        ASSERT(atomic_read(&ip->i_count) == 0);
	zfs_inode_destroy(ip);
}

static void
zpl_inode_delete(struct inode *ip)
{
	loff_t oldsize = i_size_read(ip);

	i_size_write(ip, 0);
	truncate_pagecache(ip, oldsize, 0);
	clear_inode(ip);
}

static void
zpl_evict_inode(struct inode *ip)
{
	zfs_inactive(ip);
}

static void
zpl_put_super(struct super_block *sb)
{
	int error;

	error = -zfs_umount(sb);
	ASSERT3S(error, <=, 0);
}

static int
zpl_statfs(struct dentry *dentry, struct kstatfs *statp)
{
	int error;

	error = -zfs_statvfs(dentry, statp);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_show_options(struct seq_file *seq, struct vfsmount *vfsp)
{
	struct super_block *sb = vfsp->mnt_sb;
	zfs_sb_t *zsb = sb->s_fs_info;

	/*
	 * The Linux VFS automatically handles the following flags:
	 * MNT_NOSUID, MNT_NODEV, MNT_NOEXEC, MNT_NOATIME, MNT_READONLY
	 */

	if (zsb->z_flags & ZSB_XATTR_USER)
		seq_printf(seq, ",%s", "xattr");

	return (0);
}

static int
zpl_fill_super(struct super_block *sb, void *data, int silent)
{
	int error;

	error = -zfs_domount(sb, data, silent);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_get_sb(struct file_system_type *fs_type, int flags,
    const char *osname, void *data, struct vfsmount *mnt)
{
	zpl_mount_data_t zmd = { osname, data, mnt };

	return get_sb_nodev(fs_type, flags, &zmd, zpl_fill_super, mnt);
}

static void
zpl_kill_sb(struct super_block *sb)
{
#ifdef HAVE_SNAPSHOT
	zfs_sb_t *zsb = sb->s_fs_info;

	if (zsb && dmu_objset_is_snapshot(zsb->z_os))
		zfs_snap_destroy(zsb);
#endif /* HAVE_SNAPSHOT */

	kill_anon_super(sb);
}

const struct super_operations zpl_super_operations = {
	.alloc_inode	= zpl_inode_alloc,
	.destroy_inode	= zpl_inode_destroy,
	.delete_inode	= zpl_inode_delete,
	.dirty_inode	= NULL,
	.write_inode	= NULL,
	.drop_inode	= NULL,
	.clear_inode	= zpl_evict_inode,
	.put_super	= zpl_put_super,
	.write_super	= NULL,
	.sync_fs	= NULL,
	.freeze_fs	= NULL,
	.unfreeze_fs	= NULL,
	.statfs		= zpl_statfs,
	.remount_fs	= NULL,
	.show_options	= zpl_show_options,
	.show_stats	= NULL,
};

#if 0
const struct export_operations zpl_export_operations = {
	.fh_to_dentry	= NULL,
	.fh_to_parent	= NULL,
	.get_parent	= NULL,
};
#endif

struct file_system_type zpl_fs_type = {
	.owner		= THIS_MODULE,
	.name		= ZFS_DRIVER,
	.get_sb		= zpl_get_sb,
	.kill_sb	= zpl_kill_sb,
};
