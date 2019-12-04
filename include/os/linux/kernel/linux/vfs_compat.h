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
 * Copyright (C) 2015 Jörg Thalheim.
 */

#ifndef _ZFS_VFS_H
#define	_ZFS_VFS_H

#include <sys/taskq.h>
#include <sys/cred.h>
#include <linux/backing-dev.h>
#include <linux/compat.h>

/*
 * 2.6.34 - 3.19, bdi_setup_and_register() takes 3 arguments.
 * 4.0 - 4.11, bdi_setup_and_register() takes 2 arguments.
 * 4.12 - x.y, super_setup_bdi_name() new interface.
 */
#if defined(HAVE_SUPER_SETUP_BDI_NAME)
extern atomic_long_t zfs_bdi_seq;

static inline int
zpl_bdi_setup(struct super_block *sb, char *name)
{
	return super_setup_bdi_name(sb, "%.28s-%ld", name,
	    atomic_long_inc_return(&zfs_bdi_seq));
}
static inline void
zpl_bdi_destroy(struct super_block *sb)
{
}
#elif defined(HAVE_2ARGS_BDI_SETUP_AND_REGISTER)
static inline int
zpl_bdi_setup(struct super_block *sb, char *name)
{
	struct backing_dev_info *bdi;
	int error;

	bdi = kmem_zalloc(sizeof (struct backing_dev_info), KM_SLEEP);
	error = bdi_setup_and_register(bdi, name);
	if (error) {
		kmem_free(bdi, sizeof (struct backing_dev_info));
		return (error);
	}

	sb->s_bdi = bdi;

	return (0);
}
static inline void
zpl_bdi_destroy(struct super_block *sb)
{
	struct backing_dev_info *bdi = sb->s_bdi;

	bdi_destroy(bdi);
	kmem_free(bdi, sizeof (struct backing_dev_info));
	sb->s_bdi = NULL;
}
#elif defined(HAVE_3ARGS_BDI_SETUP_AND_REGISTER)
static inline int
zpl_bdi_setup(struct super_block *sb, char *name)
{
	struct backing_dev_info *bdi;
	int error;

	bdi = kmem_zalloc(sizeof (struct backing_dev_info), KM_SLEEP);
	error = bdi_setup_and_register(bdi, name, BDI_CAP_MAP_COPY);
	if (error) {
		kmem_free(sb->s_bdi, sizeof (struct backing_dev_info));
		return (error);
	}

	sb->s_bdi = bdi;

	return (0);
}
static inline void
zpl_bdi_destroy(struct super_block *sb)
{
	struct backing_dev_info *bdi = sb->s_bdi;

	bdi_destroy(bdi);
	kmem_free(bdi, sizeof (struct backing_dev_info));
	sb->s_bdi = NULL;
}
#else
#error "Unsupported kernel"
#endif

/*
 * 4.14 adds SB_* flag definitions, define them to MS_* equivalents
 * if not set.
 */
#ifndef	SB_RDONLY
#define	SB_RDONLY	MS_RDONLY
#endif

#ifndef	SB_SILENT
#define	SB_SILENT	MS_SILENT
#endif

#ifndef	SB_ACTIVE
#define	SB_ACTIVE	MS_ACTIVE
#endif

#ifndef	SB_POSIXACL
#define	SB_POSIXACL	MS_POSIXACL
#endif

#ifndef	SB_MANDLOCK
#define	SB_MANDLOCK	MS_MANDLOCK
#endif

#ifndef	SB_NOATIME
#define	SB_NOATIME	MS_NOATIME
#endif

/*
 * 3.5 API change,
 * The clear_inode() function replaces end_writeback() and introduces an
 * ordering change regarding when the inode_sync_wait() occurs.  See the
 * configure check in config/kernel-clear-inode.m4 for full details.
 */
#if defined(HAVE_EVICT_INODE) && !defined(HAVE_CLEAR_INODE)
#define	clear_inode(ip)		end_writeback(ip)
#endif /* HAVE_EVICT_INODE && !HAVE_CLEAR_INODE */

#if defined(SEEK_HOLE) && defined(SEEK_DATA) && !defined(HAVE_LSEEK_EXECUTE)
static inline loff_t
lseek_execute(
	struct file *filp,
	struct inode *inode,
	loff_t offset,
	loff_t maxsize)
{
	if (offset < 0 && !(filp->f_mode & FMODE_UNSIGNED_OFFSET))
		return (-EINVAL);

	if (offset > maxsize)
		return (-EINVAL);

	if (offset != filp->f_pos) {
		spin_lock(&filp->f_lock);
		filp->f_pos = offset;
		filp->f_version = 0;
		spin_unlock(&filp->f_lock);
	}

	return (offset);
}
#endif /* SEEK_HOLE && SEEK_DATA && !HAVE_LSEEK_EXECUTE */

#if defined(CONFIG_FS_POSIX_ACL)
/*
 * These functions safely approximates the behavior of posix_acl_release()
 * which cannot be used because it calls the GPL-only symbol kfree_rcu().
 * The in-kernel version, which can access the RCU, frees the ACLs after
 * the grace period expires.  Because we're unsure how long that grace
 * period may be this implementation conservatively delays for 60 seconds.
 * This is several orders of magnitude larger than expected grace period.
 * At 60 seconds the kernel will also begin issuing RCU stall warnings.
 */

#include <linux/posix_acl.h>

#if defined(HAVE_POSIX_ACL_RELEASE) && !defined(HAVE_POSIX_ACL_RELEASE_GPL_ONLY)
#define	zpl_posix_acl_release(arg)		posix_acl_release(arg)
#else
void zpl_posix_acl_release_impl(struct posix_acl *);

static inline void
zpl_posix_acl_release(struct posix_acl *acl)
{
	if ((acl == NULL) || (acl == ACL_NOT_CACHED))
		return;
#ifdef HAVE_ACL_REFCOUNT
	if (refcount_dec_and_test(&acl->a_refcount))
		zpl_posix_acl_release_impl(acl);
#else
	if (atomic_dec_and_test(&acl->a_refcount))
		zpl_posix_acl_release_impl(acl);
#endif
}
#endif /* HAVE_POSIX_ACL_RELEASE */

#ifdef HAVE_SET_CACHED_ACL_USABLE
#define	zpl_set_cached_acl(ip, ty, n)		set_cached_acl(ip, ty, n)
#define	zpl_forget_cached_acl(ip, ty)		forget_cached_acl(ip, ty)
#else
static inline void
zpl_set_cached_acl(struct inode *ip, int type, struct posix_acl *newer)
{
	struct posix_acl *older = NULL;

	spin_lock(&ip->i_lock);

	if ((newer != ACL_NOT_CACHED) && (newer != NULL))
		posix_acl_dup(newer);

	switch (type) {
	case ACL_TYPE_ACCESS:
		older = ip->i_acl;
		rcu_assign_pointer(ip->i_acl, newer);
		break;
	case ACL_TYPE_DEFAULT:
		older = ip->i_default_acl;
		rcu_assign_pointer(ip->i_default_acl, newer);
		break;
	}

	spin_unlock(&ip->i_lock);

	zpl_posix_acl_release(older);
}

static inline void
zpl_forget_cached_acl(struct inode *ip, int type)
{
	zpl_set_cached_acl(ip, type, (struct posix_acl *)ACL_NOT_CACHED);
}
#endif /* HAVE_SET_CACHED_ACL_USABLE */

/*
 * 3.1 API change,
 * posix_acl_chmod() was added as the preferred interface.
 *
 * 3.14 API change,
 * posix_acl_chmod() was changed to __posix_acl_chmod()
 */
#ifndef HAVE___POSIX_ACL_CHMOD
#ifdef HAVE_POSIX_ACL_CHMOD
#define	__posix_acl_chmod(acl, gfp, mode)	posix_acl_chmod(acl, gfp, mode)
#define	__posix_acl_create(acl, gfp, mode)	posix_acl_create(acl, gfp, mode)
#else
#error "Unsupported kernel"
#endif /* HAVE_POSIX_ACL_CHMOD */
#endif /* HAVE___POSIX_ACL_CHMOD */

/*
 * 4.8 API change,
 * posix_acl_valid() now must be passed a namespace, the namespace from
 * from super block associated with the given inode is used for this purpose.
 */
#ifdef HAVE_POSIX_ACL_VALID_WITH_NS
#define	zpl_posix_acl_valid(ip, acl)  posix_acl_valid(ip->i_sb->s_user_ns, acl)
#else
#define	zpl_posix_acl_valid(ip, acl)  posix_acl_valid(acl)
#endif

#endif /* CONFIG_FS_POSIX_ACL */

/*
 * 3.19 API change
 * struct access f->f_dentry->d_inode was replaced by accessor function
 * file_inode(f)
 */
#ifndef HAVE_FILE_INODE
static inline struct inode *file_inode(const struct file *f)
{
	return (f->f_dentry->d_inode);
}
#endif /* HAVE_FILE_INODE */

/*
 * 4.1 API change
 * struct access file->f_path.dentry was replaced by accessor function
 * file_dentry(f)
 */
#ifndef HAVE_FILE_DENTRY
static inline struct dentry *file_dentry(const struct file *f)
{
	return (f->f_path.dentry);
}
#endif /* HAVE_FILE_DENTRY */

static inline uid_t zfs_uid_read_impl(struct inode *ip)
{
#ifdef HAVE_SUPER_USER_NS
	return (from_kuid(ip->i_sb->s_user_ns, ip->i_uid));
#else
	return (from_kuid(kcred->user_ns, ip->i_uid));
#endif
}

static inline uid_t zfs_uid_read(struct inode *ip)
{
	return (zfs_uid_read_impl(ip));
}

static inline gid_t zfs_gid_read_impl(struct inode *ip)
{
#ifdef HAVE_SUPER_USER_NS
	return (from_kgid(ip->i_sb->s_user_ns, ip->i_gid));
#else
	return (from_kgid(kcred->user_ns, ip->i_gid));
#endif
}

static inline gid_t zfs_gid_read(struct inode *ip)
{
	return (zfs_gid_read_impl(ip));
}

static inline void zfs_uid_write(struct inode *ip, uid_t uid)
{
#ifdef HAVE_SUPER_USER_NS
	ip->i_uid = make_kuid(ip->i_sb->s_user_ns, uid);
#else
	ip->i_uid = make_kuid(kcred->user_ns, uid);
#endif
}

static inline void zfs_gid_write(struct inode *ip, gid_t gid)
{
#ifdef HAVE_SUPER_USER_NS
	ip->i_gid = make_kgid(ip->i_sb->s_user_ns, gid);
#else
	ip->i_gid = make_kgid(kcred->user_ns, gid);
#endif
}

/*
 * 4.9 API change
 */
#ifndef HAVE_SETATTR_PREPARE
static inline int
setattr_prepare(struct dentry *dentry, struct iattr *ia)
{
	return (inode_change_ok(dentry->d_inode, ia));
}
#endif

/*
 * 4.11 API change
 * These macros are defined by kernel 4.11.  We define them so that the same
 * code builds under kernels < 4.11 and >= 4.11.  The macros are set to 0 so
 * that it will create obvious failures if they are accidentally used when built
 * against a kernel >= 4.11.
 */

#ifndef STATX_BASIC_STATS
#define	STATX_BASIC_STATS	0
#endif

#ifndef AT_STATX_SYNC_AS_STAT
#define	AT_STATX_SYNC_AS_STAT	0
#endif

/*
 * 4.11 API change
 * 4.11 takes struct path *, < 4.11 takes vfsmount *
 */

#ifdef HAVE_VFSMOUNT_IOPS_GETATTR
#define	ZPL_GETATTR_WRAPPER(func)					\
static int								\
func(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)	\
{									\
	struct path path = { .mnt = mnt, .dentry = dentry };		\
	return func##_impl(&path, stat, STATX_BASIC_STATS,		\
	    AT_STATX_SYNC_AS_STAT);					\
}
#elif defined(HAVE_PATH_IOPS_GETATTR)
#define	ZPL_GETATTR_WRAPPER(func)					\
static int								\
func(const struct path *path, struct kstat *stat, u32 request_mask,	\
    unsigned int query_flags)						\
{									\
	return (func##_impl(path, stat, request_mask, query_flags));	\
}
#else
#error
#endif

/*
 * 4.9 API change
 * Preferred interface to get the current FS time.
 */
#if !defined(HAVE_CURRENT_TIME)
static inline struct timespec
current_time(struct inode *ip)
{
	return (timespec_trunc(current_kernel_time(), ip->i_sb->s_time_gran));
}
#endif

/*
 * 4.16 API change
 * Added iversion interface for managing inode version field.
 */
#ifdef HAVE_INODE_SET_IVERSION
#include <linux/iversion.h>
#else
static inline void
inode_set_iversion(struct inode *ip, u64 val)
{
	ip->i_version = val;
}
#endif

/*
 * Returns true when called in the context of a 32-bit system call.
 */
static inline int
zpl_is_32bit_api(void)
{
#ifdef CONFIG_COMPAT
#ifdef HAVE_IN_COMPAT_SYSCALL
	return (in_compat_syscall());
#else
	return (is_compat_task());
#endif
#else
	return (BITS_PER_LONG == 32);
#endif
}

#endif /* _ZFS_VFS_H */
