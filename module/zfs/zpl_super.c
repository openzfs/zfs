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
#include <sys/zfs_ctldir.h>
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
 * Called from __mark_inode_dirty() to reflect that something in the
 * inode has changed.  We use it to ensure the znode system attributes
 * are always strictly update to date with respect to the inode.
 */
#ifdef HAVE_DIRTY_INODE_WITH_FLAGS
static void
zpl_dirty_inode(struct inode *ip, int flags)
{
	zfs_dirty_inode(ip, flags);
}
#else
static void
zpl_dirty_inode(struct inode *ip)
{
	zfs_dirty_inode(ip, 0);
}
#endif /* HAVE_DIRTY_INODE_WITH_FLAGS */

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
 * be in a state where it can simply be freed.i
 *
 * The ->evict_inode() callback must minimally truncate the inode pages,
 * and call clear_inode().  For 2.6.35 and later kernels this will
 * simply update the inode state, with the sync occurring before the
 * truncate in evict().  For earlier kernels clear_inode() maps to
 * end_writeback() which is responsible for completing all outstanding
 * write back.  In either case, once this is done it is safe to cleanup
 * any remaining inode specific data via zfs_inactive().
 * remaining filesystem specific data.
 */
#ifdef HAVE_EVICT_INODE
static void
zpl_evict_inode(struct inode *ip)
{
	truncate_setsize(ip, 0);
	clear_inode(ip);
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

static void
zpl_umount_begin(struct super_block *sb)
{
	zfs_sb_t *zsb = sb->s_fs_info;
	int count;

	/*
	 * Best effort to unmount snapshots in .zfs/snapshot/.  Normally this
	 * isn't required because snapshots have the MNT_SHRINKABLE flag set.
	 */
	if (zsb->z_ctldir)
		(void) zfsctl_unmount_snapshots(zsb, MNT_FORCE, &count);
}

/*
 * ZFS specific features must be explicitly handled here, the VFS will
 * automatically handled the following generic functionality.
 *
 *   MNT_NOSUID,
 *   MNT_NODEV,
 *   MNT_NOEXEC,
 *   MNT_NOATIME,
 *   MNT_NODIRATIME,
 *   MNT_READONLY,
 *   MNT_STRICTATIME,
 *   MS_SYNCHRONOUS,
 *   MS_DIRSYNC,
 *   MS_MANDLOCK.
 */
static int
__zpl_show_options(struct seq_file *seq, zfs_sb_t *zsb)
{
	seq_printf(seq, ",%s", zsb->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

#ifdef CONFIG_FS_POSIX_ACL
	switch (zsb->z_acl_type) {
	case ZFS_ACLTYPE_POSIXACL:
		seq_puts(seq, ",posixacl");
		break;
	default:
		seq_puts(seq, ",noacl");
		break;
	}
#endif /* CONFIG_FS_POSIX_ACL */

	return (0);
}

#ifdef HAVE_SHOW_OPTIONS_WITH_DENTRY
static int
zpl_show_options(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_options(seq, root->d_sb->s_fs_info));
}
#else
static int
zpl_show_options(struct seq_file *seq, struct vfsmount *vfsp)
{
	return (__zpl_show_options(seq, vfsp->mnt_sb->s_fs_info));
}
#endif /* HAVE_SHOW_OPTIONS_WITH_DENTRY */

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

	return (mount_nodev(fs_type, flags, &zmd, zpl_fill_super));
}
#else
static int
zpl_get_sb(struct file_system_type *fs_type, int flags,
    const char *osname, void *data, struct vfsmount *mnt)
{
	zpl_mount_data_t zmd = { osname, data };

	return (get_sb_nodev(fs_type, flags, &zmd, zpl_fill_super, mnt));
}
#endif /* HAVE_MOUNT_NODEV */

static void
zpl_kill_sb(struct super_block *sb)
{
	zfs_preumount(sb);
	kill_anon_super(sb);

#ifdef HAVE_S_INSTANCES_LIST_HEAD
	sb->s_instances.next = &(zpl_fs_type.fs_supers);
#endif /* HAVE_S_INSTANCES_LIST_HEAD */
}

#if defined(HAVE_SHRINK) || defined(HAVE_SPLIT_SHRINKER_CALLBACK)
/*
 * Linux 3.1 - 3.x API
 *
 * The Linux 3.1 API introduced per-sb cache shrinkers to replace the
 * global ones.  This allows us a mechanism to cleanly target a specific
 * zfs file system when the dnode and inode caches grow too large.
 *
 * In addition, the 3.0 kernel added the iterate_supers_type() helper
 * function which is used to safely walk all of the zfs file systems.
 */
static void
zpl_prune_sb(struct super_block *sb, void *arg)
{
	int objects = 0;
	int error;

	error = -zfs_sb_prune(sb, *(unsigned long *)arg, &objects);
	ASSERT3S(error, <=, 0);
}
#endif /* defined(HAVE_SHRINK) || defined(HAVE_SPLIT_SHRINKER_CALLBACK) */

void
zpl_prune_sbs(int64_t bytes_to_scan, void *private)
{
#if defined(HAVE_SHRINK) || defined(HAVE_SPLIT_SHRINKER_CALLBACK)
	unsigned long nr_to_scan = (bytes_to_scan / sizeof (znode_t));

	iterate_supers_type(&zpl_fs_type, zpl_prune_sb, &nr_to_scan);
	kmem_reap();
#endif /* defined(HAVE_SHRINK) || defined(HAVE_SPLIT_SHRINKER_CALLBACK) */
}

#ifdef HAVE_NR_CACHED_OBJECTS
static int
zpl_nr_cached_objects(struct super_block *sb)
{
	zfs_sb_t *zsb = sb->s_fs_info;
	int nr;

	mutex_enter(&zsb->z_znodes_lock);
	nr = zsb->z_nr_znodes;
	mutex_exit(&zsb->z_znodes_lock);

	return (nr);
}
#endif /* HAVE_NR_CACHED_OBJECTS */

#ifdef HAVE_FREE_CACHED_OBJECTS
/*
 * Attempt to evict some meta data from the cache.  The ARC operates in
 * terms of bytes while the Linux VFS uses objects.  Now because this is
 * just a best effort eviction and the exact values aren't critical so we
 * extrapolate from an object count to a byte size using the znode_t size.
 */
static void
zpl_free_cached_objects(struct super_block *sb, int nr_to_scan)
{
	/* noop */
}
#endif /* HAVE_FREE_CACHED_OBJECTS */

const struct super_operations zpl_super_operations = {
	.alloc_inode		= zpl_inode_alloc,
	.destroy_inode		= zpl_inode_destroy,
	.dirty_inode		= zpl_dirty_inode,
	.write_inode		= NULL,
	.drop_inode		= NULL,
#ifdef HAVE_EVICT_INODE
	.evict_inode		= zpl_evict_inode,
#else
	.clear_inode		= zpl_clear_inode,
	.delete_inode		= zpl_inode_delete,
#endif /* HAVE_EVICT_INODE */
	.put_super		= zpl_put_super,
	.sync_fs		= zpl_sync_fs,
	.statfs			= zpl_statfs,
	.remount_fs		= zpl_remount_fs,
	.umount_begin		= zpl_umount_begin,
	.show_options		= zpl_show_options,
	.show_stats		= NULL,
#ifdef HAVE_NR_CACHED_OBJECTS
	.nr_cached_objects	= zpl_nr_cached_objects,
#endif /* HAVE_NR_CACHED_OBJECTS */
#ifdef HAVE_FREE_CACHED_OBJECTS
	.free_cached_objects	= zpl_free_cached_objects,
#endif /* HAVE_FREE_CACHED_OBJECTS */
};

struct file_system_type zpl_fs_type = {
	.owner			= THIS_MODULE,
	.name			= ZFS_DRIVER,
#ifdef HAVE_MOUNT_NODEV
	.mount			= zpl_mount,
#else
	.get_sb			= zpl_get_sb,
#endif /* HAVE_MOUNT_NODEV */
	.kill_sb		= zpl_kill_sb,
};
