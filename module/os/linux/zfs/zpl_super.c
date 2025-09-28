// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2023, Datto Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */


#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>
#include <linux/iversion.h>
#include <linux/version.h>
#include <linux/vfs_compat.h>

/*
 * What to do when the last reference to an inode is released. If 0, the kernel
 * will cache it on the superblock. If 1, the inode will be freed immediately.
 * See zpl_drop_inode().
 */
int zfs_delete_inode = 0;

/*
 * What to do when the last reference to a dentry is released. If 0, the kernel
 * will cache it until the entry (file) is destroyed. If 1, the dentry will be
 * marked for cleanup, at which time its inode reference will be released. See
 * zpl_dentry_delete().
 */
int zfs_delete_dentry = 0;

static struct inode *
zpl_inode_alloc(struct super_block *sb)
{
	struct inode *ip;

	VERIFY3S(zfs_inode_alloc(sb, &ip), ==, 0);
	inode_set_iversion(ip, 1);

	return (ip);
}

#ifdef HAVE_SOPS_FREE_INODE
static void
zpl_inode_free(struct inode *ip)
{
	ASSERT0(atomic_read(&ip->i_count));
	zfs_inode_free(ip);
}
#endif

static void
zpl_inode_destroy(struct inode *ip)
{
	ASSERT0(atomic_read(&ip->i_count));
	zfs_inode_destroy(ip);
}

/*
 * Called from __mark_inode_dirty() to reflect that something in the
 * inode has changed.  We use it to ensure the znode system attributes
 * are always strictly update to date with respect to the inode.
 */
static void
zpl_dirty_inode(struct inode *ip, int flags)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_dirty_inode(ip, flags);
	spl_fstrans_unmark(cookie);
}

/*
 * ->drop_inode() is called when the last reference to an inode is released.
 * Its return value indicates if the inode should be destroyed immediately, or
 * cached on the superblock structure.
 *
 * By default (zfs_delete_inode=0), we call generic_drop_inode(), which returns
 * "destroy immediately" if the inode is unhashed and has no links (roughly: no
 * longer exists on disk). On datasets with millions of rarely-accessed files,
 * this can cause a large amount of memory to be "pinned" by cached inodes,
 * which in turn pin their associated dnodes and dbufs, until the kernel starts
 * reporting memory pressure and requests OpenZFS release some memory (see
 * zfs_prune()).
 *
 * When set to 1, we call generic_delete_inode(), which always returns "destroy
 * immediately", resulting in inodes being destroyed immediately, releasing
 * their associated dnodes and dbufs to the dbuf cached and the ARC to be
 * evicted as normal.
 *
 * Note that the "last reference" doesn't always mean the last _userspace_
 * reference; the dentry cache also holds a reference, so "busy" inodes will
 * still be kept alive that way (subject to dcache tuning).
 */
static int
zpl_drop_inode(struct inode *ip)
{
	if (zfs_delete_inode)
		return (generic_delete_inode(ip));
	return (generic_drop_inode(ip));
}

/*
 * The ->evict_inode() callback must minimally truncate the inode pages,
 * and call clear_inode().  For 2.6.35 and later kernels this will
 * simply update the inode state, with the sync occurring before the
 * truncate in evict().  For earlier kernels clear_inode() maps to
 * end_writeback() which is responsible for completing all outstanding
 * write back.  In either case, once this is done it is safe to cleanup
 * any remaining inode specific data via zfs_inactive().
 * remaining filesystem specific data.
 */
static void
zpl_evict_inode(struct inode *ip)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	truncate_setsize(ip, 0);
	clear_inode(ip);
	zfs_inactive(ip);
	spl_fstrans_unmark(cookie);
}

static void
zpl_put_super(struct super_block *sb)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_umount(sb);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);
}

/*
 * zfs_sync() is the underlying implementation for the sync(2) and syncfs(2)
 * syscalls, via sb->s_op->sync_fs().
 *
 * Before kernel 5.17 (torvalds/linux@5679897eb104), syncfs() ->
 * sync_filesystem() would ignore the return from sync_fs(), instead only
 * considing the error from syncing the underlying block device (sb->s_dev).
 * Since OpenZFS doesn't _have_ an underlying block device, there's no way for
 * us to report a sync directly.
 *
 * However, in 5.8 (torvalds/linux@735e4ae5ba28) the superblock gained an extra
 * error store `s_wb_err`, to carry errors seen on page writeback since the
 * last call to syncfs(). If sync_filesystem() does not return an error, any
 * existing writeback error on the superblock will be used instead (and cleared
 * either way). We don't use this (page writeback is a different thing for us),
 * so for 5.8-5.17 we can use that instead to get syncfs() to return the error.
 *
 * Before 5.8, we have no other good options - no matter what happens, the
 * userspace program will be told the call has succeeded, and so we must make
 * it so, Therefore, when we are asked to wait for sync to complete (wait ==
 * 1), if zfs_sync() has returned an error we have no choice but to block,
 * regardless of the reason.
 *
 * The 5.17 change was backported to the 5.10, 5.15 and 5.16 series, and likely
 * to some vendor kernels. Meanwhile, s_wb_err is still in use in 6.15 (the
 * mainline Linux series at time of writing), and has likely been backported to
 * vendor kernels before 5.8. We don't really want to use a workaround when we
 * don't have to, but we can't really detect whether or not sync_filesystem()
 * will return our errors (without a difficult runtime test anyway). So, we use
 * a static version check: any kernel reporting its version as 5.17+ will use a
 * direct error return, otherwise, we'll either use s_wb_err if it was detected
 * at configure (5.8-5.16 + vendor backports). If it's unavailable, we will
 * block to ensure the correct semantics.
 *
 * See https://github.com/openzfs/zfs/issues/17416 for further discussion.
 */
static int
zpl_sync_fs(struct super_block *sb, int wait)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_sync(sb, wait, cr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
#ifdef HAVE_SUPER_BLOCK_S_WB_ERR
	if (error && wait)
		errseq_set(&sb->s_wb_err, error);
#else
	if (error && wait) {
		zfsvfs_t *zfsvfs = sb->s_fs_info;
		ASSERT3P(zfsvfs, !=, NULL);
		if (zfs_enter(zfsvfs, FTAG) == 0) {
			txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
			zfs_exit(zfsvfs, FTAG);
			error = 0;
		}
	}
#endif
#endif /* < 5.17.0 */

	spl_fstrans_unmark(cookie);
	crfree(cr);

	ASSERT3S(error, <=, 0);
	return (error);
}

static int
zpl_statfs(struct dentry *dentry, struct kstatfs *statp)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_statvfs(dentry->d_inode, statp);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	/*
	 * If required by a 32-bit system call, dynamically scale the
	 * block size up to 16MiB and decrease the block counts.  This
	 * allows for a maximum size of 64EiB to be reported.  The file
	 * counts must be artificially capped at 2^32-1.
	 */
	if (unlikely(zpl_is_32bit_api())) {
		while (statp->f_blocks > UINT32_MAX &&
		    statp->f_bsize < SPA_MAXBLOCKSIZE) {
			statp->f_frsize <<= 1;
			statp->f_bsize <<= 1;

			statp->f_blocks >>= 1;
			statp->f_bfree >>= 1;
			statp->f_bavail >>= 1;
		}

		uint64_t usedobjs = statp->f_files - statp->f_ffree;
		statp->f_ffree = MIN(statp->f_ffree, UINT32_MAX - usedobjs);
		statp->f_files = statp->f_ffree + usedobjs;
	}

	return (error);
}

static int
zpl_remount_fs(struct super_block *sb, int *flags, char *data)
{
	zfs_mnt_t zm = { .mnt_osname = NULL, .mnt_data = data };
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_remount(sb, flags, &zm);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_show_devname(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	int error;
	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	char *fsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dmu_objset_name(zfsvfs->z_os, fsname);

	for (int i = 0; fsname[i] != 0; i++) {
		/*
		 * Spaces in the dataset name must be converted to their
		 * octal escape sequence for getmntent(3) to correctly
		 * parse then fsname portion of /proc/self/mounts.
		 */
		if (fsname[i] == ' ') {
			seq_puts(seq, "\\040");
		} else {
			seq_putc(seq, fsname[i]);
		}
	}

	kmem_free(fsname, ZFS_MAX_DATASET_NAME_LEN);

	zpl_exit(zfsvfs, FTAG);

	return (0);
}

static int
zpl_show_devname(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_devname(seq, root->d_sb->s_fs_info));
}

static int
__zpl_show_options(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	seq_printf(seq, ",%s",
	    zfsvfs->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

#ifdef CONFIG_FS_POSIX_ACL
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_POSIX:
		seq_puts(seq, ",posixacl");
		break;
	default:
		seq_puts(seq, ",noacl");
		break;
	}
#endif /* CONFIG_FS_POSIX_ACL */

	switch (zfsvfs->z_case) {
	case ZFS_CASE_SENSITIVE:
		seq_puts(seq, ",casesensitive");
		break;
	case ZFS_CASE_INSENSITIVE:
		seq_puts(seq, ",caseinsensitive");
		break;
	default:
		seq_puts(seq, ",casemixed");
		break;
	}

	return (0);
}

static int
zpl_show_options(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_options(seq, root->d_sb->s_fs_info));
}

static int
zpl_fill_super(struct super_block *sb, void *data, int silent)
{
	zfs_mnt_t *zm = (zfs_mnt_t *)data;
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_domount(sb, zm, silent);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_test_super(struct super_block *s, void *data)
{
	zfsvfs_t *zfsvfs = s->s_fs_info;
	objset_t *os = data;
	/*
	 * If the os doesn't match the z_os in the super_block, assume it is
	 * not a match. Matching would imply a multimount of a dataset. It is
	 * possible that during a multimount, there is a simultaneous operation
	 * that changes the z_os, e.g., rollback, where the match will be
	 * missed, but in that case the user will get an EBUSY.
	 */
	return (zfsvfs != NULL && os == zfsvfs->z_os);
}

static struct super_block *
zpl_mount_impl(struct file_system_type *fs_type, int flags, zfs_mnt_t *zm)
{
	struct super_block *s;
	objset_t *os;
	boolean_t issnap = B_FALSE;
	int err;

	err = dmu_objset_hold(zm->mnt_osname, FTAG, &os);
	if (err)
		return (ERR_PTR(-err));

	/*
	 * The dsl pool lock must be released prior to calling sget().
	 * It is possible sget() may block on the lock in grab_super()
	 * while deactivate_super() holds that same lock and waits for
	 * a txg sync.  If the dsl_pool lock is held over sget()
	 * this can prevent the pool sync and cause a deadlock.
	 */
	dsl_dataset_long_hold(dmu_objset_ds(os), FTAG);
	dsl_pool_rele(dmu_objset_pool(os), FTAG);

	s = sget(fs_type, zpl_test_super, set_anon_super, flags, os);

	/*
	 * Recheck with the lock held to prevent mounting the wrong dataset
	 * since z_os can be stale when the teardown lock is held.
	 *
	 * We can't do this in zpl_test_super in since it's under spinlock and
	 * also s_umount lock is not held there so it would race with
	 * zfs_umount and zfsvfs can be freed.
	 */
	if (!IS_ERR(s) && s->s_fs_info != NULL) {
		zfsvfs_t *zfsvfs = s->s_fs_info;
		if (zpl_enter(zfsvfs, FTAG) == 0) {
			if (os != zfsvfs->z_os)
				err = -SET_ERROR(EBUSY);
			issnap = zfsvfs->z_issnap;
			zpl_exit(zfsvfs, FTAG);
		} else {
			err = -SET_ERROR(EBUSY);
		}
	}
	dsl_dataset_long_rele(dmu_objset_ds(os), FTAG);
	dsl_dataset_rele(dmu_objset_ds(os), FTAG);

	if (IS_ERR(s))
		return (ERR_CAST(s));

	if (err) {
		deactivate_locked_super(s);
		return (ERR_PTR(err));
	}

	if (s->s_root == NULL) {
		err = zpl_fill_super(s, zm, flags & SB_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(s);
			return (ERR_PTR(err));
		}
		s->s_flags |= SB_ACTIVE;
	} else if (!issnap && ((flags ^ s->s_flags) & SB_RDONLY)) {
		/*
		 * Skip ro check for snap since snap is always ro regardless
		 * ro flag is passed by mount or not.
		 */
		deactivate_locked_super(s);
		return (ERR_PTR(-EBUSY));
	}

	return (s);
}

static struct dentry *
zpl_mount(struct file_system_type *fs_type, int flags,
    const char *osname, void *data)
{
	zfs_mnt_t zm = { .mnt_osname = osname, .mnt_data = data };

	struct super_block *sb = zpl_mount_impl(fs_type, flags, &zm);
	if (IS_ERR(sb))
		return (ERR_CAST(sb));

	return (dget(sb->s_root));
}

static void
zpl_kill_sb(struct super_block *sb)
{
	zfs_preumount(sb);
	kill_anon_super(sb);
}

void
zpl_prune_sb(uint64_t nr_to_scan, void *arg)
{
	struct super_block *sb = (struct super_block *)arg;
	int objects = 0;

	/*
	 * Ensure the superblock is not in the process of being torn down.
	 */
#ifdef HAVE_SB_DYING
	if (down_read_trylock(&sb->s_umount)) {
		if (!(sb->s_flags & SB_DYING) && sb->s_root &&
		    (sb->s_flags & SB_BORN)) {
			(void) zfs_prune(sb, nr_to_scan, &objects);
		}
		up_read(&sb->s_umount);
	}
#else
	if (down_read_trylock(&sb->s_umount)) {
		if (!hlist_unhashed(&sb->s_instances) &&
		    sb->s_root && (sb->s_flags & SB_BORN)) {
			(void) zfs_prune(sb, nr_to_scan, &objects);
		}
		up_read(&sb->s_umount);
	}
#endif
}

const struct super_operations zpl_super_operations = {
	.alloc_inode		= zpl_inode_alloc,
#ifdef HAVE_SOPS_FREE_INODE
	.free_inode		= zpl_inode_free,
#endif
	.destroy_inode		= zpl_inode_destroy,
	.dirty_inode		= zpl_dirty_inode,
	.write_inode		= NULL,
	.drop_inode		= zpl_drop_inode,
	.evict_inode		= zpl_evict_inode,
	.put_super		= zpl_put_super,
	.sync_fs		= zpl_sync_fs,
	.statfs			= zpl_statfs,
	.remount_fs		= zpl_remount_fs,
	.show_devname		= zpl_show_devname,
	.show_options		= zpl_show_options,
	.show_stats		= NULL,
};

/*
 * ->d_delete() is called when the last reference to a dentry is released. Its
 *  return value indicates if the dentry should be destroyed immediately, or
 *  retained in the dentry cache.
 *
 * By default (zfs_delete_dentry=0) the kernel will always cache unused
 * entries.  Each dentry holds an inode reference, so cached dentries can hold
 * the final inode reference indefinitely, leading to the inode and its related
 * data being pinned (see zpl_drop_inode()).
 *
 * When set to 1, we signal that the dentry should be destroyed immediately and
 * never cached. This reduces memory usage, at the cost of higher overheads to
 * lookup a file, as the inode and its underlying data (dnode/dbuf) need to be
 * reloaded and reinflated.
 *
 * Note that userspace does not have direct control over dentry references and
 * reclaim; rather, this is part of the kernel's caching and reclaim subsystems
 * (eg vm.vfs_cache_pressure).
 */
static int
zpl_dentry_delete(const struct dentry *dentry)
{
	return (zfs_delete_dentry ? 1 : 0);
}

const struct dentry_operations zpl_dentry_operations = {
	.d_delete = zpl_dentry_delete,
};

struct file_system_type zpl_fs_type = {
	.owner			= THIS_MODULE,
	.name			= ZFS_DRIVER,
#if defined(HAVE_IDMAP_MNT_API)
	.fs_flags		= FS_USERNS_MOUNT | FS_ALLOW_IDMAP,
#else
	.fs_flags		= FS_USERNS_MOUNT,
#endif
	.mount			= zpl_mount,
	.kill_sb		= zpl_kill_sb,
};

ZFS_MODULE_PARAM(zfs, zfs_, delete_inode, INT, ZMOD_RW,
	"Delete inodes as soon as the last reference is released.");

ZFS_MODULE_PARAM(zfs, zfs_, delete_dentry, INT, ZMOD_RW,
	"Delete dentries from dentry cache as soon as the last reference is "
	"released.");
