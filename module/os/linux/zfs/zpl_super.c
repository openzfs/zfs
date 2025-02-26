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
 */


#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>
#include <linux/iversion.h>


static struct inode *
zpl_inode_alloc(struct super_block *sb)
{
	struct inode *ip;

	VERIFY3S(zfs_inode_alloc(sb, &ip), ==, 0);
	inode_set_iversion(ip, 1);

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
static void
zpl_dirty_inode(struct inode *ip, int flags)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_dirty_inode(ip, flags);
	spl_fstrans_unmark(cookie);
}

/*
 * When ->drop_inode() is called its return value indicates if the
 * inode should be evicted from the inode cache.  If the inode is
 * unhashed and has no links the default policy is to evict it
 * immediately.
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

static int
zpl_sync_fs(struct super_block *sb, int wait)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_sync(sb, wait, cr);
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
	 * deactivate_locked_super calls shrinker_free and only then
	 * sops->kill_sb cb, resulting in UAF on umount when trying to reach
	 * for the shrinker functions in zpl_prune_sb of in-umount dataset.
	 * Increment if s_active is not zero, but don't prune if it is -
	 * umount could be underway.
	 */
	if (atomic_inc_not_zero(&sb->s_active)) {
		(void) -zfs_prune(sb, nr_to_scan, &objects);
		atomic_dec(&sb->s_active);
	}

}

const struct super_operations zpl_super_operations = {
	.alloc_inode		= zpl_inode_alloc,
	.destroy_inode		= zpl_inode_destroy,
	.dirty_inode		= zpl_dirty_inode,
	.write_inode		= NULL,
	.evict_inode		= zpl_evict_inode,
	.put_super		= zpl_put_super,
	.sync_fs		= zpl_sync_fs,
	.statfs			= zpl_statfs,
	.remount_fs		= zpl_remount_fs,
	.show_devname		= zpl_show_devname,
	.show_options		= zpl_show_options,
	.show_stats		= NULL,
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
