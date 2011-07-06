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

/*
 * When ->drop_inode() is called its return value indicates if the
 * inode should be evicted from the inode cache.  If the inode is
 * unhashed and has no links the default policy is to evict it
 * immediately.
 *
 * Prior to 2.6.36 this eviction was accomplished by the vfs calling
 * ->delete_inode().  It was ->delete_inode()'s responsibility to
 * truncate the inode pages and call clear_inode().  The call to
 * clear_inode() synchronously invalidates all the buffers and
 * calls ->clear_inode().  It was ->clear_inode()'s responsibility
 * to cleanup and filesystem specific data before freeing the inode.
 *
 * This elaborate mechanism was replaced by ->evict_inode() which
 * does the job of both ->delete_inode() and ->clear_inode().  It
 * will be called exactly once, and when it returns the inode must
 * be in a state where it can simply be freed.  The ->evict_inode()
 * callback must minimally truncate the inode pages, and call
 * end_writeback() to complete all outstanding writeback for the
 * inode.  After this is complete evict inode can cleanup any
 * remaining filesystem specific data.
 */
#ifdef HAVE_EVICT_INODE
static void
zpl_evict_inode(struct inode *ip)
{
	truncate_setsize(ip, 0);
	end_writeback(ip);
	zfs_inactive(ip);
}

#else

static void
zpl_clear_inode(struct inode *ip)
{
	zfs_inactive(ip);
}

static void
zpl_inode_delete(struct inode *ip)
{
	truncate_setsize(ip, 0);
	clear_inode(ip);
}

#endif /* HAVE_EVICT_INODE */

static void
zpl_put_super(struct super_block *sb)
{
	int error;

	error = -zfs_umount(sb);
	ASSERT3S(error, <=, 0);
}

static int
zpl_sync_fs(struct super_block *sb, int wait)
{
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	error = -zfs_sync(sb, wait, cr);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
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
zpl_remount_fs(struct super_block *sb, int *flags, char *data)
{
	int error;
	error = -zfs_remount(sb, flags, data);
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

	seq_printf(seq, ",%s", zsb->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

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

#ifdef HAVE_MOUNT_NODEV
static struct dentry *
zpl_mount(struct file_system_type *fs_type, int flags,
    const char *osname, void *data)
{
	zpl_mount_data_t zmd = { osname, data };

	return mount_nodev(fs_type, flags, &zmd, zpl_fill_super);
}
#else
static int
zpl_get_sb(struct file_system_type *fs_type, int flags,
    const char *osname, void *data, struct vfsmount *mnt)
{
	zpl_mount_data_t zmd = { osname, data };

	return get_sb_nodev(fs_type, flags, &zmd, zpl_fill_super, mnt);
}
#endif /* HAVE_MOUNT_NODEV */

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
	.dirty_inode	= NULL,
	.write_inode	= NULL,
	.drop_inode	= NULL,
#ifdef HAVE_EVICT_INODE
	.evict_inode	= zpl_evict_inode,
#else
	.clear_inode	= zpl_clear_inode,
	.delete_inode	= zpl_inode_delete,
#endif /* HAVE_EVICT_INODE */
	.put_super	= zpl_put_super,
	.write_super	= NULL,
	.sync_fs	= zpl_sync_fs,
	.statfs		= zpl_statfs,
	.remount_fs	= zpl_remount_fs,
	.show_options	= zpl_show_options,
	.show_stats	= NULL,
};

struct file_system_type zpl_fs_type = {
	.owner		= THIS_MODULE,
	.name		= ZFS_DRIVER,
#ifdef HAVE_MOUNT_NODEV
	.mount		= zpl_mount,
#else
	.get_sb		= zpl_get_sb,
#endif /* HAVE_MOUNT_NODEV */
	.kill_sb	= zpl_kill_sb,
};
