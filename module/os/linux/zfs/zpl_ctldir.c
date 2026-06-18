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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>
#include <sys/dmu.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <linux/version.h>

/*
 * Common open routine.  Disallow any write access.
 */
static int
zpl_common_open(struct inode *ip, struct file *filp)
{
	if (blk_mode_is_open_write(filp->f_mode))
		return (-EACCES);

	return (generic_file_open(ip, filp));
}

/*
 * Get root directory contents.
 */
static int
zpl_root_iterate(struct file *filp, struct dir_context *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	int error = 0;

	if (zfsvfs->z_show_ctldir == ZFS_SNAPDIR_DISABLED) {
		return (SET_ERROR(ENOENT));
	}

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!dir_emit_dots(filp, ctx))
		goto out;

	if (ctx->pos == 2) {
		if (!dir_emit(ctx, ZFS_SNAPDIR_NAME,
		    strlen(ZFS_SNAPDIR_NAME), ZFSCTL_INO_SNAPDIR, DT_DIR))
			goto out;

		ctx->pos++;
	}

	if (ctx->pos == 3) {
		if (!dir_emit(ctx, ZFS_SHAREDIR_NAME,
		    strlen(ZFS_SHAREDIR_NAME), ZFSCTL_INO_SHARES, DT_DIR))
			goto out;

		ctx->pos++;
	}
out:
	zpl_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Get root directory attributes.
 */
ZPL_IDMAP_IOP_DEFINE(int, zpl_root_getattr, 4,
    const struct path *, path, struct kstat *, stat, u32, request_mask,
    unsigned int, query_flags)
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zpl_generic_fillattr(idmap, request_mask, ip, stat);
	stat->atime = current_time(ip);

	return (0);
}

static struct dentry *
zpl_root_lookup(struct inode *dip, struct dentry *dentry, unsigned int flags)
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
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	return (d_splice_alias(ip, dentry));
}

/*
 * The '.zfs' control directory file and inode operations.
 */
const struct file_operations zpl_fops_root = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= zpl_root_iterate,
};

const struct inode_operations zpl_ops_root = {
	.lookup		= zpl_root_lookup,
	.getattr	= zpl_root_getattr,
};

/*
 * Snapdir control nodes. The snapdir system is described in full at the top of
 * zfs_ctldir.c.
 *
 * This file has the dentry operations that manage the kernel's path traversal
 * into the snapshot mounts, and coordinate between the d_manage and
 * d_automount handlers to get a snapshot online safely.
 */

/*
 * Lookups (zpl_snapdir_lookup() or zpl_snapdir_revalidate()) with flags
 * matching this criteria will result in an automount being triggered when
 * walking a snapshot control dentry. They should roughly match the criteria
 * laid out in the kernel's follow_automount() function. Read the grand theory
 * for more on why this is necessary.
 */
static inline bool
lookup_want_automount(unsigned int flags)
{
	if (!(flags & (LOOKUP_PARENT | LOOKUP_DIRECTORY | LOOKUP_OPEN |
	    LOOKUP_CREATE | LOOKUP_AUTOMOUNT)))
		return (false);

#ifdef LOOKUP_NO_XDEV
	/* LOOKUP_NO_XDEV added in 5.6 to support openat2(RESOLVE_NO_XDEV). */
	if (flags & LOOKUP_NO_XDEV)
		return (false);
#endif

	return (true);
}

static int
zpl_snapdir_manage(const struct path *path, bool rcu_walk)
{
	struct dentry *dentry = path->dentry;
	zfs_snapentry_t *se = dentry->d_fsdata;

	if (rcu_walk) {
		/*
		 * In RCU-walk mode, we are under the RCU lock and must not
		 * block, and should not slow down if we can help it (eg take a
		 * spinlock).
		 *
		 * Note that this is the only place it's safe to access flag
		 * bits without se_mtx held, and that's only because of this
		 * unique context. SE_BUSY will only ever change under lock,
		 * as the last thing before the lock is released.
		 *
		 * If we catch SE_BUSY just before it's cleared, then we fall
		 * back to REF-walk when we didn't need to, but that is always
		 * safe.
		 *
		 * If we catch it just before it's set, then we will proceed,
		 * however RCU-walk cannot trigger automount. If there's no
		 * mount there, we will simply return as such; if there is
		 * a mount, we will walk into it. In the worst cases, we miss
		 * the the mount happening and can't walk into it, or we enter
		 * a mount and prevent it being unmounted. Both these are
		 * safe and defensible behaviour for a walk racing a mount or
		 * unmount.
		 */
		if (SE_TEST(se, SE_BUSY))
			return (-ECHILD);

		/* dentry is stable, walk may proceed. */
		return (0);
	}

	/*
	 * REF-walk. Unlocked, and may block. Valid returns are:
	 *   0        proceed, enter existing mount or start automount
	 *   EISDIR   don't automount, caller is not entering
	 *   ESTALE   restart the path walk (dentry invalidated, raced)
	 */

	mutex_enter(&se->se_mtx);
	if (se->se_mount_task == current) {
		/*
		 * Caller is our own task, reentering through follow_down.
		 * Allow it to proceed.
		 */
		mutex_exit(&se->se_mtx);
		return (0);
	}

	/*
	 * Localise the mount intent before the busy wait. At this point we
	 * know we definitely want to mount, but we might sleep on BUSY below,
	 * and then another mount completing under a different parent will
	 * clear the bit while we wait.
	 *
	 * Note that there is a tiny gap here: if this thread is preempted
	 * between d_revalidate() (which set the flag) and this read then
	 * another walk could clear it. However, that's a few non-sleeping
	 * instructions in the pathwalk fast-path and so vanishingly unlikely.
	 */
	bool want_mount = SE_TEST(se, SE_WANT_MOUNT);

	/*
	 * Wait for any mount/unmount to complete. This includes the mount
	 * task that we just let through.
	 */
	while (SE_TEST(se, SE_BUSY))
		cv_wait(&se->se_cv, &se->se_mtx);

	/*
	 * There can't be a mount task any longer, because otherwise we'd
	 * be busy above.
	 */
	ASSERT0P(se->se_mount_task);

	if (d_unhashed(dentry)) {
		/*
		 * dentry was invalidated while we were sleeping. That might be
		 * expiry, but could also be the underlying dataset being
		 * unmounted. Force pathwalk to start again from the beginning.
		 */
		mutex_exit(&se->se_mtx);
		return (-ESTALE);
	}

	if (!dentry->d_inode) {
		/*
		 * dentry has no inode. Can happen when the snapshot is
		 * destroyed by rmdir'ing the snapdir, but the dentry hasn't
		 * been unhashed or invalidated yet. Returning EISDIR
		 * disables automount for this walk.
		 */
		mutex_exit(&se->se_mtx);
		return (-EISDIR);
	}

	/*
	 * Check for an existing mount. We have to use follow_down_one() (ie
	 * lookup_mnt()) here because mounts are keyed on path (parent mount +
	 * dentry), so eg d_mountpoint() would find _any_ mount, not
	 * necessarily _this_ mount.
	 */
	struct path check_path = *path;
	path_get(&check_path);
	bool mounted = follow_down_one(&check_path);
	path_put(&check_path);

	if (mounted) {
		/*
		 * Something mounted here, let the VFS have it and hope its
		 * the right thing!
		 */
		SE_CLEAR(se, SE_WANT_MOUNT);
		mutex_exit(&se->se_mtx);
		return (0);
	}

	/* Nothing mounted under this parent, continue. */

	if (!want_mount) {
		/*
		 * Whatever triggered this walk is not looking for anything
		 * "inside" the mount, so tell the VFS not to attempt
		 * automount.
		 */
		mutex_exit(&se->se_mtx);
		return (-EISDIR);
	}

	/* We are the mount task now. */
	se->se_mount_task = current;
	SE_SET(se, SE_BUSY);
	mutex_exit(&se->se_mtx);

	struct path am_path = *path;
	path_get(&am_path);
	int err = zpl_follow_down(&am_path, LOOKUP_AUTOMOUNT);
	if (err) {
		/* Mount failed or some other internal error. */
		path_put(&am_path);
		err = -ESTALE;
		goto out;
	}

	if (am_path.dentry != am_path.mnt->mnt_root ||
	    am_path.mnt->mnt_sb->s_type != &zpl_fs_type) {
		/*
		 * follow_down() succeeded but ended up somewhere not the root
		 * of a ZFS filesystem, nothing more we can do.
		 */
		path_put(&am_path);
		err = -ESTALE;
		goto out;
	}

	zfsvfs_t *zfsvfs = am_path.mnt->mnt_sb->s_fs_info;
	if (dmu_objset_spa(zfsvfs->z_os) != se->se_spa ||
	    dmu_objset_id(zfsvfs->z_os) != se->se_objsetid) {
		/*
		 * follow_down() ended up in a ZFS filesystem, but not the
		 * snapshot we expected. Again, nothing more we can do.
		 */
		path_put(&am_path);
		err = -ESTALE;
		goto out;
	}

	/* Finalise and publish the mount. */
	zfsctl_snapshot_finish_mount(se, am_path.mnt);

	path_put(&am_path);

out:
	mutex_enter(&se->se_mtx);
	ASSERT3P(se->se_mount_task, ==, current);
	ASSERT(SE_TEST(se, SE_BUSY));

	if (err == 0)
		/* Something mounted, hopefully the thing we wanted! */
		SE_CLEAR(se, SE_WANT_MOUNT);

	se->se_mount_task = NULL;
	SE_CLEAR(se, SE_BUSY);
	cv_broadcast(&se->se_cv);
	mutex_exit(&se->se_mtx);

	return (err);
}

static struct vfsmount *
zpl_snapdir_automount(struct path *path)
{
	struct dentry *dentry = path->dentry;
	zfs_snapentry_t *se = dentry->d_fsdata;

	/*
	 * Only the mounting task from zpl_snapdir_manage() can make it here.
	 * However, before 5.5 it would enter twice, as the mount loop (in
	 * follow_managed(), which is now __traverse_mounts()) would not reload
	 * the dentry flags, so would not "see" the automount. This was ok
	 * because nothing does the d_manage() double-entry thing we're doing,
	 * and the filesystem itself would just return the mount it had already
	 * created.
	 *
	 * So if anything arrives here when se_mount_task is NULL, we simply
	 * return NULL here. On those older kernels, that should cause the path
	 * to be reevaluated and enter the mount.
	 */
	if (se->se_mount_task == NULL)
		return (NULL);

	/*
	 * This has better be the mounting task, or something very strange
	 * has happened.
	 */
	ASSERT3P(se->se_mount_task, ==, current);
	ASSERT(SE_TEST(se, SE_BUSY));
	ASSERT3P(dentry->d_inode, !=, NULL);

	struct vfsmount *mnt = NULL;
	int error = -zfsctl_snapshot_mount(path, &mnt);

	if (error)
		return (ERR_PTR(error));

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
	/*
	 * Before torvalds/linux@006ff7498fe89 (6.16), the kernel's internal
	 * expiry machinery could expire any mount that had only a single
	 * reference, reasoning that that reference must be the mountpoint
	 * itself, and thus there are no users.
	 *
	 * To work around this, filesystems implementing d_automount were
	 * expected to return a mount with two refs, the extra to cause expiry
	 * to assume the mount is in use and skip it. finish_automount()
	 * enforces this by calling BUG() if the count is <2, and will release
	 * the extra once the graft is completed.
	 *
	 * 6.16 changes this to simply have the expiry task ignore mounts that
	 * aren't mounted yet, making life much easier for filesystems.
	 *
	 * There's no test that can really detect this, so we go for a simple
	 * version check, and take an extra reference on older kernels.
	 *
	 * This should be safe for RHEL too; at time of writing, EL8.10 (4.18+)
	 * and EL9.8 (5.14) kernels have not backported this change.
	 */
	mntget(mnt);
#endif

	return (mnt);
}

/*
 * Kernel will revalidate when the dentry may have changed state during
 * RCU-walk (eg when the dentry is reused on splice, see zpl_snapdir_lookup()).
 * If we have an inode, then update the flags and declare it good.
 *
 * For negative dentries, no revalidation necessary - they're either brand-new
 * and not yet live (eg between lookup->mkdir) or they've been invalidated
 * because the mount is dead and we want a new one on next lookup.
 */
#ifdef HAVE_D_REVALIDATE_4ARGS
static int
zpl_snapdir_revalidate(struct inode *dir, const struct qstr *name,
    struct dentry *dentry, unsigned int flags)
#else
static int
zpl_snapdir_revalidate(struct dentry *dentry, unsigned int flags)
#endif
{
	zfs_snapentry_t *se = dentry->d_fsdata;

	if (dentry->d_inode) {
		if (lookup_want_automount(flags))
			SE_SET(se, SE_WANT_MOUNT);
		atomic_store_64(&se->se_atime, jiffies);
		return (1);
	}

	return (0);
}

/* Kernel is done with the dentry, tear down the snapentry too. */
static void
zpl_snapdir_release(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	zfs_snapentry_t *se = dentry->d_fsdata;
	dentry->d_fsdata = NULL;
	spin_unlock(&dentry->d_lock);

	/*
	 * Release can be called more than once if part of the release was
	 * deferred, so we might have already cleaned up. Do nothing if so.
	 */
	if (se == NULL)
		return;

	zfsctl_snapshot_timer_clear(se);

	mutex_destroy(&se->se_mtx);
	cv_destroy(&se->se_cv);

	kmem_free(se, sizeof (zfs_snapentry_t));
}

static const struct dentry_operations zpl_dops_snapdirs = {
	.d_manage	= zpl_snapdir_manage,
	.d_automount	= zpl_snapdir_automount,
	.d_revalidate	= zpl_snapdir_revalidate,
	.d_release	= zpl_snapdir_release,
};

/*
 * Snapdir control dentries need a zfs_snapentry_t to track a possible mount
 * and custom dentry operations to coordinate access against management of
 * the mount. This sets all that up on the given dentry.
 */
static void
zpl_snapdir_init_snapentry(struct dentry *dentry)
{
	/*
	 * The snapentry starts off as an empty stub. It will be filled in
	 * later if/when the mount actually happens.
	 */
	zfs_snapentry_t *se = kmem_zalloc(sizeof (zfs_snapentry_t), KM_SLEEP);

	se->se_taskqid = TASKQID_INVALID;
	mutex_init(&se->se_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&se->se_cv, NULL, CV_DEFAULT, NULL);
	se->se_flags = 0;

	/*
	 * We have to take the lock here as the dentry might be about to be
	 * reused and so on a cleanup list or similar.
	 */
	spin_lock(&dentry->d_lock);
	ASSERT0P(dentry->d_fsdata);

	se->se_dentry = dentry;
	dentry->d_fsdata = se;

	/*
	 * Full set of "op" flags. The dentry may have other flags tracking
	 * its state, so we want to mask these off instead of setting it to
	 * NULL.
	 */
	static const unsigned int op_flags =
	    DCACHE_OP_HASH | DCACHE_OP_COMPARE |
	    DCACHE_OP_REVALIDATE | DCACHE_OP_DELETE |
	    DCACHE_OP_PRUNE | DCACHE_OP_WEAK_REVALIDATE | DCACHE_OP_REAL;

#ifdef HAVE_D_SET_D_OP
	/*
	 * d_set_d_op() will set the DCACHE_OP_ flags according to what it
	 * finds in the passed dentry_operations, so we don't have to.
	 *
	 * We clear the flags and the old op table before calling d_set_d_op()
	 * because issues a warning when the dentry operations table is already
	 * set.
	 */
	dentry->d_op = NULL;
	dentry->d_flags &= ~op_flags;
	d_set_d_op(dentry, &zpl_dops_snapdirs);
	dentry->d_flags |= DCACHE_MANAGE_TRANSIT | DCACHE_NEED_AUTOMOUNT;
#else
	/*
	 * Since 6.17 there's no exported way to modify dentry ops, so we have
	 * to reach in and do it ourselves. We have the lock, so this should
	 * be safe.
	 *
	 * Note that the DCACHE_OP_ flags must match the associated ops in
	 * zpl_dops_snapdirs.
	 */
	dentry->d_op = &zpl_dops_snapdirs;
	dentry->d_flags &= ~op_flags;
	dentry->d_flags |= DCACHE_OP_REVALIDATE |
	    DCACHE_MANAGE_TRANSIT | DCACHE_NEED_AUTOMOUNT;
#endif

	se->se_dentry = dentry;

	spin_unlock(&dentry->d_lock);
}

static struct dentry *
zpl_snapdir_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfsctl_snapdir_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error && error != -ENOENT)
		return (ERR_PTR(error));

	ASSERT(error == 0 || ip == NULL);

	zpl_snapdir_init_snapentry(dentry);
	zfs_snapentry_t *se = dentry->d_fsdata;

	if (lookup_want_automount(flags))
		SE_SET(se, SE_WANT_MOUNT);

	struct dentry *old = d_splice_alias(ip, dentry);
	if (old == NULL || IS_ERR(old))
		return (old);

	/*
	 * Previous dentry was invalidated and waiting to be cleaned up, so
	 * d_splice_alias() has re-lifed it and thrown our new one away. So we
	 * have to get it back into shape.
	 *
	 * The dentry is already published and an RCU-walk lookup may already
	 * be in progress, however it is also on a wait list until this call
	 * returns, and REF-walk can't happen until that list is cleared. So
	 * we're safe to make adjustments here provided the RCU-walk won't
	 * see them. d_revalidate()->zpl_snapdir_revalidate() will be called
	 * on our dentries on that list before the RCU-walk, which will
	 * correctly set SE_WANT_MOUNT.
	 *
	 * However, there may not be a walk in progress, and so the VFS will
	 * trust the dentry returned here. So we also need to correctly set
	 * SE_WANT_MOUNT here too.
	 *
	 * Any other state from the previous version will be cleaned up
	 * before actually repopulating it fully in
	 * zpl_snapdir_automount()->zfsctl_snapshot_mount().
	 */
	se = old->d_fsdata;
	if (lookup_want_automount(flags))
		SE_SET(se, SE_WANT_MOUNT);

	return (old);
}

static int
zpl_snapdir_iterate(struct file *filp, struct dir_context *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	fstrans_cookie_t cookie;
	char snapname[MAXNAMELEN];
	boolean_t case_conflict;
	uint64_t id, pos;
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);
	cookie = spl_fstrans_mark();

	if (!dir_emit_dots(filp, ctx))
		goto out;

	/* Start the position at 0 if it already emitted . and .. */
	pos = (ctx->pos == 2 ? 0 : ctx->pos);
	while (error == 0) {
		dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
		error = -dmu_snapshot_list_next(zfsvfs->z_os, MAXNAMELEN,
		    snapname, &id, &pos, &case_conflict);
		dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
		if (error)
			goto out;

		if (!dir_emit(ctx, snapname, strlen(snapname),
		    ZFSCTL_INO_SHARES - id, DT_DIR))
			goto out;

		ctx->pos = pos;
	}
out:
	spl_fstrans_unmark(cookie);
	zpl_exit(zfsvfs, FTAG);

	if (error == -ENOENT)
		return (0);

	return (error);
}

ZPL_IDMAP_IOP_DEFINE(int, zpl_snapdir_rename, 5,
    struct inode *, sdip, struct dentry *, sdentry,
    struct inode *, tdip, struct dentry *, tdentry, unsigned int, flags)
{
	cred_t *cr = CRED();
	int error;

	/* We probably don't want to support renameat2(2) in ctldir */
	if (flags)
		return (-EINVAL);

	crhold(cr);
	error = -zfsctl_snapdir_rename(sdip, sdentry, tdip, tdentry, cr);
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
	error = -zfsctl_snapdir_remove(dip, dentry, cr);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

#if defined(HAVE_MKDIR_DENTRY_RETURN)
ZPL_IDMAP_IOP_DEFINE(struct dentry *, zpl_snapdir_mkdir, 3,
    struct inode *, dip, struct dentry *, dentry, umode_t, mode)
#else
ZPL_IDMAP_IOP_DEFINE(int, zpl_snapdir_mkdir, 3,
    struct inode *, dip, struct dentry *, dentry, umode_t, mode)
#endif
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr, idmap);

	error = -zfsctl_snapdir_mkdir(dip, dname(dentry), vap, &ip, cr, 0);
	if (error == 0)
		d_instantiate(dentry, ip);

	kmem_free(vap, sizeof (vattr_t));
	ASSERT3S(error, <=, 0);
	crfree(cr);

#if defined(HAVE_MKDIR_DENTRY_RETURN)
	return (ERR_PTR(error));
#else
	return (error);
#endif
}

/*
 * Get snapshot directory attributes.
 */
ZPL_IDMAP_IOP_DEFINE(int, zpl_snapdir_getattr, 4,
    const struct path *, path, struct kstat *, stat, u32, request_mask,
    unsigned int, query_flags)
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	int error;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	zpl_generic_fillattr(idmap, request_mask, ip, stat);
	stat->nlink = stat->size = 2;

	dsl_dataset_t *ds = dmu_objset_ds(zfsvfs->z_os);
	if (dsl_dataset_phys(ds)->ds_snapnames_zapobj != 0) {
		uint64_t snap_count;
		int err = zap_count(
		    dmu_objset_pool(ds->ds_objset)->dp_meta_objset,
		    dsl_dataset_phys(ds)->ds_snapnames_zapobj, &snap_count);
		if (err != 0) {
			zpl_exit(zfsvfs, FTAG);
			return (-err);
		}
		stat->nlink += snap_count;
	}

	stat->ctime = stat->mtime = dmu_objset_snap_cmtime(zfsvfs->z_os);
	stat->atime = current_time(ip);
	zpl_exit(zfsvfs, FTAG);

	return (0);
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
	.iterate_shared	= zpl_snapdir_iterate,

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
zpl_shares_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	struct inode *ip = NULL;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfsctl_shares_lookup(dip, dname(dentry), &ip,
	    0, cr, NULL, NULL);
	ASSERT3S(error, <=, 0);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	if (error) {
		if (error == -ENOENT)
			return (d_splice_alias(NULL, dentry));
		else
			return (ERR_PTR(error));
	}

	return (d_splice_alias(ip, dentry));
}

static int
zpl_shares_iterate(struct file *filp, struct dir_context *ctx)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	znode_t *dzp;
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);
	cookie = spl_fstrans_mark();

	if (zfsvfs->z_shares_dir == 0) {
		dir_emit_dots(filp, ctx);
		goto out;
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error)
		goto out;

	crhold(cr);
	error = -zfs_readdir(ZTOI(dzp), ctx, cr);
	crfree(cr);

	iput(ZTOI(dzp));
out:
	spl_fstrans_unmark(cookie);
	zpl_exit(zfsvfs, FTAG);
	ASSERT3S(error, <=, 0);

	return (error);
}

ZPL_IDMAP_IOP_DEFINE(int, zpl_shares_getattr, 4,
    const struct path *, path, struct kstat *, stat, u32, request_mask,
    unsigned int, query_flags)
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	znode_t *dzp;
	int error;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_shares_dir == 0) {
		zpl_generic_fillattr(idmap, request_mask, ip, stat);
		stat->nlink = stat->size = 2;
		stat->atime = current_time(ip);
		zpl_exit(zfsvfs, FTAG);
		return (0);
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error == 0) {
		error = -zfs_getattr_fast(idmap, request_mask, ZTOI(dzp),
		    stat);
		iput(ZTOI(dzp));
	}

	zpl_exit(zfsvfs, FTAG);
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
	.iterate_shared	= zpl_shares_iterate,
};

/*
 * The '.zfs/shares' directory inode operations.
 */
const struct inode_operations zpl_ops_shares = {
	.lookup		= zpl_shares_lookup,
	.getattr	= zpl_shares_getattr,
};
