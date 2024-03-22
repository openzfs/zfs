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
 */

#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_quota.h>
#include <sys/zpl.h>
#include <sys/dmu.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>

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
zpl_root_iterate(struct file *filp, zpl_dir_context_t *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!zpl_dir_emit_dots(filp, ctx)) {
		error = SET_ERROR(-EIO);
		goto out;
	}

	if (ctx->pos == 2) {
		if (!zpl_dir_emit(ctx, ZFS_SNAPDIR_NAME,
		    strlen(ZFS_SNAPDIR_NAME), ZFSCTL_INO_SNAPDIR, DT_DIR)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 3) {
		if (!zpl_dir_emit(ctx, ZFS_SHAREDIR_NAME,
		    strlen(ZFS_SHAREDIR_NAME), ZFSCTL_INO_SHARES, DT_DIR)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 4 && zfs_ctldir_spacefiles) {
		if (!zpl_dir_emit(ctx, ZFS_SPACEDIR_NAME,
		    strlen(ZFS_SPACEDIR_NAME),
		    ZFSCTL_INO_SPACEDIR, DT_DIR))  {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 5 && zfs_ctldir_spacefiles) {
		if (!zpl_dir_emit(ctx, ZFS_QUOTADIR_NAME,
		    strlen(ZFS_QUOTADIR_NAME),
		    ZFSCTL_INO_QUOTADIR, DT_DIR)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

out:
	zpl_exit(zfsvfs, FTAG);
	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_root_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

/*
 * Get root directory attributes.
 */
static int
#ifdef HAVE_IDMAP_IOPS_GETATTR
zpl_root_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_USERNS_IOPS_GETATTR)
zpl_root_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_root_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;

#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
	(void) user_ns;
#endif
#else
	generic_fillattr(ip, stat);
#endif
	stat->atime = current_time(ip);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_root_getattr);

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
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_root_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_root_iterate,
#else
	.readdir	= zpl_root_readdir,
#endif
};

const struct inode_operations zpl_ops_root = {
	.lookup		= zpl_root_lookup,
	.getattr	= zpl_root_getattr,
};

static struct vfsmount *
zpl_snapdir_automount(struct path *path)
{
	int error;

	error = -zfsctl_snapshot_mount(path, 0);
	if (error)
		return (ERR_PTR(error));

	/*
	 * Rather than returning the new vfsmount for the snapshot we must
	 * return NULL to indicate a mount collision.  This is done because
	 * the user space mount calls do_add_mount() which adds the vfsmount
	 * to the name space.  If we returned the new mount here it would be
	 * added again to the vfsmount list resulting in list corruption.
	 */
	return (NULL);
}

/*
 * Negative dentries must always be revalidated so newly created snapshots
 * can be detected and automounted.  Normal dentries should be kept because
 * as of the 3.18 kernel revaliding the mountpoint dentry will result in
 * the snapshot being immediately unmounted.
 */
static int
#ifdef HAVE_D_REVALIDATE_NAMEIDATA
zpl_snapdir_revalidate(struct dentry *dentry, struct nameidata *i)
#else
zpl_snapdir_revalidate(struct dentry *dentry, unsigned int flags)
#endif
{
	return (!!dentry->d_inode);
}

static dentry_operations_t zpl_dops_snapdirs = {
/*
 * Auto mounting of snapshots is only supported for 2.6.37 and
 * newer kernels.  Prior to this kernel the ops->follow_link()
 * callback was used as a hack to trigger the mount.  The
 * resulting vfsmount was then explicitly grafted in to the
 * name space.  While it might be possible to add compatibility
 * code to accomplish this it would require considerable care.
 */
	.d_automount	= zpl_snapdir_automount,
	.d_revalidate	= zpl_snapdir_revalidate,
};

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
	d_clear_d_op(dentry);
	d_set_d_op(dentry, &zpl_dops_snapdirs);
	dentry->d_flags |= DCACHE_NEED_AUTOMOUNT;

	return (d_splice_alias(ip, dentry));
}

static int
zpl_snapdir_iterate(struct file *filp, zpl_dir_context_t *ctx)
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

	if (!zpl_dir_emit_dots(filp, ctx))
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

		if (!zpl_dir_emit(ctx, snapname, strlen(snapname),
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

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_snapdir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_snapdir_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

static int
#ifdef HAVE_IOPS_RENAME_USERNS
zpl_snapdir_rename2(struct user_namespace *user_ns, struct inode *sdip,
    struct dentry *sdentry, struct inode *tdip, struct dentry *tdentry,
    unsigned int flags)
#elif defined(HAVE_IOPS_RENAME_IDMAP)
zpl_snapdir_rename2(struct mnt_idmap *user_ns, struct inode *sdip,
    struct dentry *sdentry, struct inode *tdip, struct dentry *tdentry,
    unsigned int flags)
#else
zpl_snapdir_rename2(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry, unsigned int flags)
#endif
{
	cred_t *cr = CRED();
	int error;

	/* We probably don't want to support renameat2(2) in ctldir */
	if (flags)
		return (-EINVAL);

	crhold(cr);
	error = -zfsctl_snapdir_rename(sdip, dname(sdentry),
	    tdip, dname(tdentry), cr, 0);
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

#if (!defined(HAVE_RENAME_WANTS_FLAGS) && \
	!defined(HAVE_IOPS_RENAME_USERNS) && \
	!defined(HAVE_IOPS_RENAME_IDMAP))
static int
zpl_snapdir_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry)
{
	return (zpl_snapdir_rename2(sdip, sdentry, tdip, tdentry, 0));
}
#endif

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
#ifdef HAVE_IOPS_MKDIR_USERNS
zpl_snapdir_mkdir(struct user_namespace *user_ns, struct inode *dip,
    struct dentry *dentry, umode_t mode)
#elif defined(HAVE_IOPS_MKDIR_IDMAP)
zpl_snapdir_mkdir(struct mnt_idmap *user_ns, struct inode *dip,
    struct dentry *dentry, umode_t mode)
#else
zpl_snapdir_mkdir(struct inode *dip, struct dentry *dentry, umode_t mode)
#endif
{
	cred_t *cr = CRED();
	vattr_t *vap;
	struct inode *ip;
	int error;

	crhold(cr);
	vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
#if (defined(HAVE_IOPS_MKDIR_USERNS) || defined(HAVE_IOPS_MKDIR_IDMAP))
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr, user_ns);
#else
	zpl_vap_init(vap, dip, mode | S_IFDIR, cr, zfs_init_idmap);
#endif

	error = -zfsctl_snapdir_mkdir(dip, dname(dentry), vap, &ip, cr, 0);
	if (error == 0) {
		d_clear_d_op(dentry);
		d_set_d_op(dentry, &zpl_dops_snapdirs);
		d_instantiate(dentry, ip);
	}

	kmem_free(vap, sizeof (vattr_t));
	ASSERT3S(error, <=, 0);
	crfree(cr);

	return (error);
}

/*
 * Get snapshot directory attributes.
 */
static int
#ifdef HAVE_IDMAP_IOPS_GETATTR
zpl_snapdir_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_USERNS_IOPS_GETATTR)
zpl_snapdir_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_snapdir_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	int error;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);
#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
	(void) user_ns;
#endif
#else
	generic_fillattr(ip, stat);
#endif

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
ZPL_GETATTR_WRAPPER(zpl_snapdir_getattr);

/*
 * The '.zfs/snapshot' directory file operations.  These mainly control
 * generating the list of available snapshots when doing an 'ls' in the
 * directory.  See zpl_snapdir_readdir().
 */
const struct file_operations zpl_fops_snapdir = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_snapdir_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_snapdir_iterate,
#else
	.readdir	= zpl_snapdir_readdir,
#endif

};

/*
 * The '.zfs/snapshot' directory inode operations.  These mainly control
 * creating an inode for a snapshot directory and initializing the needed
 * infrastructure to automount the snapshot.  See zpl_snapdir_lookup().
 */
const struct inode_operations zpl_ops_snapdir = {
	.lookup		= zpl_snapdir_lookup,
	.getattr	= zpl_snapdir_getattr,
#if (defined(HAVE_RENAME_WANTS_FLAGS) || \
	defined(HAVE_IOPS_RENAME_USERNS) || \
	defined(HAVE_IOPS_RENAME_IDMAP))
	.rename		= zpl_snapdir_rename2,
#else
	.rename		= zpl_snapdir_rename,
#endif
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
zpl_shares_iterate(struct file *filp, zpl_dir_context_t *ctx)
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
		zpl_dir_emit_dots(filp, ctx);
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

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_shares_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_shares_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

static int
#ifdef HAVE_USERNS_IOPS_GETATTR
zpl_shares_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_IDMAP_IOPS_GETATTR)
zpl_shares_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_shares_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;
	zfsvfs_t *zfsvfs = ITOZSB(ip);
	znode_t *dzp;
	int error;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_shares_dir == 0) {
#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
		generic_fillattr(user_ns, path->dentry->d_inode, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
		generic_fillattr(user_ns, path->dentry->d_inode, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
		(void) user_ns;
#endif
#else
		generic_fillattr(path->dentry->d_inode, stat);
#endif
		stat->nlink = stat->size = 2;
		stat->atime = current_time(ip);
		zpl_exit(zfsvfs, FTAG);
		return (0);
	}

	error = -zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp);
	if (error == 0) {
#ifdef HAVE_GENERIC_FILLATTR_IDMAP_REQMASK
		error = -zfs_getattr_fast(user_ns, request_mask, ZTOI(dzp),
		    stat);
#elif (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
		error = -zfs_getattr_fast(user_ns, ZTOI(dzp), stat);
#else
		error = -zfs_getattr_fast(kcred->user_ns, ZTOI(dzp), stat);
#endif
		iput(ZTOI(dzp));
	}

	zpl_exit(zfsvfs, FTAG);
	ASSERT3S(error, <=, 0);

	return (error);
}
ZPL_GETATTR_WRAPPER(zpl_shares_getattr);

/*
 * The '.zfs/shares' directory file operations.
 */
const struct file_operations zpl_fops_shares = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_shares_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_shares_iterate,
#else
	.readdir	= zpl_shares_readdir,
#endif

};

/*
 * The '.zfs/shares' directory inode operations.
 */
const struct inode_operations zpl_ops_shares = {
	.lookup		= zpl_shares_lookup,
	.getattr	= zpl_shares_getattr,
};

static int
zpl_spacedir_iterate(struct file *filp, zpl_dir_context_t *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!zpl_dir_emit_dots(filp, ctx)) {
		error = SET_ERROR(-EIO);
		goto out;
	}

	if (ctx->pos == 2) {
		if (!zpl_dir_emit(ctx, ZFS_USERFILE_NAME,
		    strlen(ZFS_USERFILE_NAME), ZFSCTL_INO_SPACE_USER,
		    DT_REG)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 3) {
		if (!zpl_dir_emit(ctx, ZFS_GROUPFILE_NAME,
		    strlen(ZFS_GROUPFILE_NAME), ZFSCTL_INO_SPACE_GROUP,
		    DT_REG)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 4) {
		if (!zpl_dir_emit(ctx, ZFS_PROJECTFILE_NAME,
		    strlen(ZFS_PROJECTFILE_NAME), ZFSCTL_INO_SPACE_PROJ,
		    DT_REG)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

out:
	zpl_exit(zfsvfs, FTAG);
	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_spacedir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_spacedir_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

static struct dentry *
zpl_spacedir_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;

	crhold(cr);
	error = -zfsctl_spacedir_lookup(dip, dname(dentry), &ip, 0, cr,
	    NULL, NULL);
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

static int
#ifdef HAVE_USERNS_IOPS_GETATTR
zpl_spacedir_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_IDMAP_IOPS_GETATTR)
zpl_spacedir_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_spacedir_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;

#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
	(void) user_ns;
#endif
#else
	generic_fillattr(ip, stat);
#endif
	stat->atime = current_time(ip);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_spacedir_getattr);

/*
 * The '.zfs/space' directory file operations.
 */
const struct file_operations zpl_fops_spacedir = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_spacedir_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_spacedir_iterate,
#else
	.readdir	= zpl_spacedir_readdir,
#endif

};

/*
 * The '.zfs/space' directory inode operations.
 */
const struct inode_operations zpl_ops_spacedir = {
	.lookup		= zpl_spacedir_lookup,
	.getattr	= zpl_spacedir_getattr,
};

static int
zpl_quotadir_iterate(struct file *filp, zpl_dir_context_t *ctx)
{
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(filp));
	int error = 0;

	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (!zpl_dir_emit_dots(filp, ctx)) {
		error = SET_ERROR(-EIO);
		goto out;
	}

	if (ctx->pos == 2) {
		if (!zpl_dir_emit(ctx, ZFS_USERFILE_NAME,
		    strlen(ZFS_USERFILE_NAME), ZFSCTL_INO_QUOTA_USER,
		    DT_REG)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 3) {
		if (!zpl_dir_emit(ctx, ZFS_GROUPFILE_NAME,
		    strlen(ZFS_GROUPFILE_NAME), ZFSCTL_INO_QUOTA_GROUP,
		    DT_REG)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

	if (ctx->pos == 4) {
		if (!zpl_dir_emit(ctx, ZFS_PROJECTFILE_NAME,
		    strlen(ZFS_PROJECTFILE_NAME), ZFSCTL_INO_QUOTA_PROJ,
		    DT_REG)) {
			error = SET_ERROR(-EIO);
			goto out;
		}

		ctx->pos++;
	}

out:
	zpl_exit(zfsvfs, FTAG);
	return (error);
}

#if !defined(HAVE_VFS_ITERATE) && !defined(HAVE_VFS_ITERATE_SHARED)
static int
zpl_quotadir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	zpl_dir_context_t ctx =
	    ZPL_DIR_CONTEXT_INIT(dirent, filldir, filp->f_pos);
	int error;

	error = zpl_quotadir_iterate(filp, &ctx);
	filp->f_pos = ctx.pos;

	return (error);
}
#endif /* !HAVE_VFS_ITERATE && !HAVE_VFS_ITERATE_SHARED */

static struct dentry *
zpl_quotadir_lookup(struct inode *dip, struct dentry *dentry,
    unsigned int flags)
{
	cred_t *cr = CRED();
	struct inode *ip;
	int error;

	crhold(cr);
	error = -zfsctl_quotadir_lookup(dip, dname(dentry), &ip, 0, cr,
	    NULL, NULL);
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

static int
#ifdef HAVE_USERNS_IOPS_GETATTR
zpl_quotadir_getattr_impl(struct user_namespace *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#elif defined(HAVE_IDMAP_IOPS_GETATTR)
zpl_quotadir_getattr_impl(struct mnt_idmap *user_ns,
    const struct path *path, struct kstat *stat, u32 request_mask,
    unsigned int query_flags)
#else
zpl_quotadir_getattr_impl(const struct path *path, struct kstat *stat,
    u32 request_mask, unsigned int query_flags)
#endif
{
	(void) request_mask, (void) query_flags;
	struct inode *ip = path->dentry->d_inode;

#if (defined(HAVE_USERNS_IOPS_GETATTR) || defined(HAVE_IDMAP_IOPS_GETATTR))
#ifdef HAVE_GENERIC_FILLATTR_USERNS
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP)
	generic_fillattr(user_ns, ip, stat);
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
	generic_fillattr(user_ns, request_mask, ip, stat);
#else
	(void) user_ns;
#endif
#else
	generic_fillattr(ip, stat);
#endif
	stat->atime = current_time(ip);

	return (0);
}
ZPL_GETATTR_WRAPPER(zpl_quotadir_getattr);

/*
 * The '.zfs/quota' directory file operations.
 */
const struct file_operations zpl_fops_quotadir = {
	.open		= zpl_common_open,
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
#ifdef HAVE_VFS_ITERATE_SHARED
	.iterate_shared	= zpl_quotadir_iterate,
#elif defined(HAVE_VFS_ITERATE)
	.iterate	= zpl_quotadir_iterate,
#else
	.readdir	= zpl_quotadir_readdir,
#endif

};

/*
 * The '.zfs/quota' directory inode operations.
 */
const struct inode_operations zpl_ops_quotadir = {
	.lookup		= zpl_quotadir_lookup,
	.getattr	= zpl_quotadir_getattr,
};

/*
 * Helpers for:
 *  .zfs/(space|quota)user
 *  .zfs/(space|quota)group
 *  .zfs/(space|quota)project
 */
static int foreach_zfs_useracct(zfsvfs_t *zfsvfs,
	zfs_userquota_prop_t type, uint64_t cookie,
	int (*fn)(zfs_useracct_t *zua, zfs_userquota_prop_t type, void *v),
	void *fn_arg)
{
	uint64_t cbufsize, bufsize = 16 * sizeof (zfs_useracct_t);
	zfs_useracct_t *buf = (zfs_useracct_t *)kmem_alloc(bufsize, KM_SLEEP);
	int err = 0;
	for (;;) {
		cbufsize = bufsize;
		if (zfs_userspace_many(zfsvfs, type, &cookie,
		    buf, &cbufsize)) {
			err = 1;
			break;
		};
		if (cbufsize == 0) {
			break;
		}
		zfs_useracct_t *zua = buf;
		while (cbufsize > 0) {
			if (fn(zua, type, fn_arg)) {
				err = 1;
			}
			zua++;
			cbufsize -= sizeof (zfs_useracct_t);
		}
	}
	kmem_free(buf, bufsize);
	return (err);
}

static int zua_nvlist_add(zfs_useracct_t *zua,
	zfs_userquota_prop_t type, void *v)
{
	nvlist_t *ids = (nvlist_t *)v;
	char name[MAXNAMELEN];
	(void) snprintf(name, sizeof (name), "%u", zua->zu_rid);
	nvlist_t *spacelist;
	if (nvlist_lookup_nvlist(ids, name, &spacelist) ||
	    spacelist == NULL) {
		VERIFY0(nvlist_alloc(&spacelist, NV_UNIQUE_NAME, KM_NOSLEEP));
		VERIFY0(nvlist_add_nvlist(ids, name, spacelist));
		/* lookup again because nvlist_add_nvlist does a deep-copy */
		VERIFY0(nvlist_lookup_nvlist(ids, name, &spacelist));
	}
	VERIFY0(nvlist_add_uint64(spacelist,
	    zfs_userquota_prop_prefixes[type], zua->zu_space));
	return (0);
}

static void seq_print_spaceval(struct seq_file *seq, nvlist_t *spacelist,
	zfs_userquota_prop_t type)
{
	uint64_t spaceval;
	seq_printf(seq, ",");
	if (!nvlist_lookup_uint64(spacelist,
	    zfs_userquota_prop_prefixes[type],
	    &spaceval)) {
		seq_printf(seq, "%llu", spaceval);
	}
}

static int zpl_quotaspace_show(struct seq_file *seq)
{
	zfs_userquota_prop_t *props = (zfs_userquota_prop_t *)seq->private;
	zfsvfs_t *zfsvfs = ITOZSB(file_inode(seq->file));
	nvlist_t *ids;
	unsigned int i;
	VERIFY0(nvlist_alloc(&ids, NV_UNIQUE_NAME, 0));
	for (i = 0; i < 2; ++i)
		VERIFY0(foreach_zfs_useracct(zfsvfs, props[i], 0,
		    zua_nvlist_add, ids));
	for (nvpair_t *idpair = nvlist_next_nvpair(ids, NULL);
	    idpair != NULL; idpair = nvlist_next_nvpair(ids, idpair)) {
		const char *id = nvpair_name(idpair);
		seq_printf(seq, "%s", id);
		nvlist_t *spacelist;
		VERIFY0(nvpair_value_nvlist(idpair, &spacelist));
		for (i = 0; i < 2; ++i)
			seq_print_spaceval(seq, spacelist, props[i]);
		seq_putc(seq, '\n');
	}
	nvlist_free(ids);
	return (0);
}

static int zpl_quota_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "id,quota,objquota\n");
	return (zpl_quotaspace_show(seq));
}

static int zpl_space_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "id,used,objused\n");
	return (zpl_quotaspace_show(seq));
}

static zfs_userquota_prop_t userspace_props[2] = {
	ZFS_PROP_USERUSED,
	ZFS_PROP_USEROBJUSED
};

static zfs_userquota_prop_t groupspace_props[2] = {
	ZFS_PROP_GROUPUSED,
	ZFS_PROP_GROUPOBJUSED
};

static zfs_userquota_prop_t projectspace_props[2] = {
	ZFS_PROP_PROJECTUSED,
	ZFS_PROP_PROJECTOBJUSED
};

static zfs_userquota_prop_t userquota_props[2] = {
	ZFS_PROP_USERQUOTA,
	ZFS_PROP_USEROBJQUOTA
};

static zfs_userquota_prop_t groupquota_props[2] = {
	ZFS_PROP_GROUPQUOTA,
	ZFS_PROP_GROUPOBJQUOTA
};

static zfs_userquota_prop_t projectquota_props[2] = {
	ZFS_PROP_PROJECTQUOTA,
	ZFS_PROP_PROJECTOBJQUOTA
};

static int zpl_fops_userspace_open(struct inode *inode, struct file *file)
{
	return single_open(file, zpl_space_show,
	    (void *)userspace_props);
}

static int zpl_fops_groupspace_open(struct inode *inode, struct file *file)
{
	return single_open(file, zpl_space_show,
	    (void *)groupspace_props);
}

static int zpl_fops_projectspace_open(struct inode *inode, struct file *file)
{
	return single_open(file, zpl_space_show,
	    (void *)projectspace_props);
}

static int zpl_fops_userquota_open(struct inode *inode, struct file *file)
{
	return single_open(file, zpl_quota_show,
	    (void *)userquota_props);
}

static int zpl_fops_groupquota_open(struct inode *inode, struct file *file)
{
	return single_open(file, zpl_quota_show,
	    (void *)groupquota_props);
}

static int zpl_fops_projectquota_open(struct inode *inode, struct file *file)
{
	return single_open(file, zpl_quota_show,
	    (void *)projectquota_props);
}

/* .zfs/space/user */
const struct file_operations zpl_fops_userspace_file = {
	.open		= zpl_fops_userspace_open,
#ifdef HAVE_SEQ_READ_ITER
	.read_iter	= seq_read_iter,
#endif
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* .zfs/space/group */
const struct file_operations zpl_fops_groupspace_file = {
	.open		= zpl_fops_groupspace_open,
	.read		= seq_read,
#ifdef HAVE_SEQ_READ_ITER
	.read_iter	= seq_read_iter,
#endif
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* .zfs/space/project */
const struct file_operations zpl_fops_projectspace_file = {
	.open		= zpl_fops_projectspace_open,
	.read		= seq_read,
#ifdef HAVE_SEQ_READ_ITER
	.read_iter	= seq_read_iter,
#endif
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* .zfs/quota/user */
const struct file_operations zpl_fops_userquota_file = {
	.open		= zpl_fops_userquota_open,
#ifdef HAVE_SEQ_READ_ITER
	.read_iter	= seq_read_iter,
#endif
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* .zfs/quota/group */
const struct file_operations zpl_fops_groupquota_file = {
	.open		= zpl_fops_groupquota_open,
	.read		= seq_read,
#ifdef HAVE_SEQ_READ_ITER
	.read_iter	= seq_read_iter,
#endif
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* .zfs/quota/project */
const struct file_operations zpl_fops_projectquota_file = {
	.open		= zpl_fops_projectquota_open,
	.read		= seq_read,
#ifdef HAVE_SEQ_READ_ITER
	.read_iter	= seq_read_iter,
#endif
	.llseek		= seq_lseek,
	.release	= single_release,
};


const struct inode_operations zpl_ops_userspace_file = {};
const struct inode_operations zpl_ops_groupspace_file = {};
const struct inode_operations zpl_ops_projectspace_file = {};
const struct inode_operations zpl_ops_userquota_file = {};
const struct inode_operations zpl_ops_groupquota_file = {};
const struct inode_operations zpl_ops_projectquota_file = {};
