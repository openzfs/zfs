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
 * Copyright (C) 2015 Jörg Thalheim.
 */

#ifndef _ZFS_VFS_H
#define	_ZFS_VFS_H

#include <sys/taskq.h>
#include <sys/cred.h>
#include <linux/backing-dev.h>
#include <linux/compat.h>

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

#if defined(SEEK_HOLE) && defined(SEEK_DATA)
static inline loff_t
lseek_execute(
	struct file *filp,
	struct inode *inode,
	loff_t offset,
	loff_t maxsize)
{
#ifdef FMODE_UNSIGNED_OFFSET
	if (offset < 0 && !(filp->f_mode & FMODE_UNSIGNED_OFFSET))
#else
	if (offset < 0 && !(filp->f_op->fop_flags & FOP_UNSIGNED_OFFSET))
#endif
		return (-EINVAL);

	if (offset > maxsize)
		return (-EINVAL);

	if (offset != filp->f_pos) {
		spin_lock(&filp->f_lock);
		filp->f_pos = offset;
#ifdef HAVE_FILE_F_VERSION
		filp->f_version = 0;
#endif
		spin_unlock(&filp->f_lock);
	}

	return (offset);
}
#endif /* SEEK_HOLE && SEEK_DATA */

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

void zpl_posix_acl_release_impl(struct posix_acl *);

static inline void
zpl_posix_acl_release(struct posix_acl *acl)
{
	if ((acl == NULL) || (acl == ACL_NOT_CACHED))
		return;
	if (refcount_dec_and_test(&acl->a_refcount))
		zpl_posix_acl_release_impl(acl);
}
#endif /* CONFIG_FS_POSIX_ACL */

static inline uid_t zfs_uid_read_impl(struct inode *ip)
{
	return (from_kuid(kcred->user_ns, ip->i_uid));
}

static inline uid_t zfs_uid_read(struct inode *ip)
{
	return (zfs_uid_read_impl(ip));
}

static inline gid_t zfs_gid_read_impl(struct inode *ip)
{
	return (from_kgid(kcred->user_ns, ip->i_gid));
}

static inline gid_t zfs_gid_read(struct inode *ip)
{
	return (zfs_gid_read_impl(ip));
}

static inline void zfs_uid_write(struct inode *ip, uid_t uid)
{
	ip->i_uid = make_kuid(kcred->user_ns, uid);
}

static inline void zfs_gid_write(struct inode *ip, gid_t gid)
{
	ip->i_gid = make_kgid(kcred->user_ns, gid);
}

/*
 * 3.15 API change
 */
#ifndef RENAME_NOREPLACE
#define	RENAME_NOREPLACE	(1 << 0) /* Don't overwrite target */
#endif
#ifndef RENAME_EXCHANGE
#define	RENAME_EXCHANGE		(1 << 1) /* Exchange source and dest */
#endif
#ifndef RENAME_WHITEOUT
#define	RENAME_WHITEOUT		(1 << 2) /* Whiteout source */
#endif

/*
 * 4.9 API change
 */
#if !(defined(HAVE_SETATTR_PREPARE_NO_USERNS) || \
    defined(HAVE_SETATTR_PREPARE_USERNS) || \
    defined(HAVE_SETATTR_PREPARE_IDMAP))
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

#if defined(HAVE_PATH_IOPS_GETATTR)
#define	ZPL_GETATTR_WRAPPER(func)					\
static int								\
func(const struct path *path, struct kstat *stat, u32 request_mask,	\
    unsigned int query_flags)						\
{									\
	return (func##_impl(path, stat, request_mask, query_flags));	\
}
#elif defined(HAVE_USERNS_IOPS_GETATTR)
#define	ZPL_GETATTR_WRAPPER(func)					\
static int								\
func(struct user_namespace *user_ns, const struct path *path,	\
    struct kstat *stat, u32 request_mask, unsigned int query_flags)	\
{									\
	return (func##_impl(user_ns, path, stat, request_mask, \
	    query_flags));	\
}
#elif defined(HAVE_IDMAP_IOPS_GETATTR)
#define	ZPL_GETATTR_WRAPPER(func)					\
static int								\
func(struct mnt_idmap *user_ns, const struct path *path,	\
    struct kstat *stat, u32 request_mask, unsigned int query_flags)	\
{									\
	return (func##_impl(user_ns, path, stat, request_mask,	\
	    query_flags));	\
}
#else
#error
#endif

/*
 * Returns true when called in the context of a 32-bit system call.
 */
static inline int
zpl_is_32bit_api(void)
{
#ifdef CONFIG_COMPAT
	return (in_compat_syscall());
#else
	return (BITS_PER_LONG == 32);
#endif
}

/*
 * 5.12 API change
 * To support id-mapped mounts, generic_fillattr() was modified to
 * accept a new struct user_namespace* as its first arg.
 *
 * 6.3 API change
 * generic_fillattr() first arg is changed to struct mnt_idmap *
 *
 * 6.6 API change
 * generic_fillattr() gets new second arg request_mask, a u32 type
 *
 */
#ifdef HAVE_GENERIC_FILLATTR_IDMAP
#define	zpl_generic_fillattr(idmap, ip, sp)	\
    generic_fillattr(idmap, ip, sp)
#elif defined(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK)
#define	zpl_generic_fillattr(idmap, rqm, ip, sp)	\
    generic_fillattr(idmap, rqm, ip, sp)
#elif defined(HAVE_GENERIC_FILLATTR_USERNS)
#define	zpl_generic_fillattr(user_ns, ip, sp)	\
    generic_fillattr(user_ns, ip, sp)
#else
#define	zpl_generic_fillattr(user_ns, ip, sp)	generic_fillattr(ip, sp)
#endif

#endif /* _ZFS_VFS_H */
