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
 */

#ifndef _ZFS_VFS_H
#define	_ZFS_VFS_H

#include <sys/taskq.h>

/*
 * 2.6.28 API change,
 * Added insert_inode_locked() helper function, prior to this most callers
 * used insert_inode_hash().  The older method doesn't check for collisions
 * in the inode_hashtable but it still acceptible for use.
 */
#ifndef HAVE_INSERT_INODE_LOCKED
static inline int
insert_inode_locked(struct inode *ip)
{
	insert_inode_hash(ip);
	return (0);
}
#endif /* HAVE_INSERT_INODE_LOCKED */

/*
 * 2.6.35 API change,
 * Add truncate_setsize() if it is not exported by the Linux kernel.
 *
 * Truncate the inode and pages associated with the inode. The pages are
 * unmapped and removed from cache.
 */
#ifndef HAVE_TRUNCATE_SETSIZE
static inline void
truncate_setsize(struct inode *ip, loff_t new)
{
	struct address_space *mapping = ip->i_mapping;

	i_size_write(ip, new);

	unmap_mapping_range(mapping, new + PAGE_SIZE - 1, 0, 1);
	truncate_inode_pages(mapping, new);
	unmap_mapping_range(mapping, new + PAGE_SIZE - 1, 0, 1);
}
#endif /* HAVE_TRUNCATE_SETSIZE */

#if defined(HAVE_BDI) && !defined(HAVE_BDI_SETUP_AND_REGISTER)
/*
 * 2.6.34 API change,
 * Add bdi_setup_and_register() function if not yet provided by kernel.
 * It is used to quickly initialize and register a BDI for the filesystem.
 */
extern atomic_long_t zfs_bdi_seq;

static inline int
bdi_setup_and_register(
	struct backing_dev_info *bdi,
	char *name,
	unsigned int cap)
{
	char tmp[32];
	int error;

	bdi->name = name;
	bdi->capabilities = cap;
	error = bdi_init(bdi);
	if (error)
		return (error);

	sprintf(tmp, "%.28s%s", name, "-%d");
	error = bdi_register(bdi, NULL, tmp,
	    atomic_long_inc_return(&zfs_bdi_seq));
	if (error) {
		bdi_destroy(bdi);
		return (error);
	}

	return (error);
}
#endif /* HAVE_BDI && !HAVE_BDI_SETUP_AND_REGISTER */

/*
 * 2.6.38 API change,
 * LOOKUP_RCU flag introduced to distinguish rcu-walk from ref-walk cases.
 */
#ifndef LOOKUP_RCU
#define	LOOKUP_RCU	0x0
#endif /* LOOKUP_RCU */

/*
 * 3.2-rc1 API change,
 * Add set_nlink() if it is not exported by the Linux kernel.
 *
 * i_nlink is read-only in Linux 3.2, but it can be set directly in
 * earlier kernels.
 */
#ifndef HAVE_SET_NLINK
static inline void
set_nlink(struct inode *inode, unsigned int nlink)
{
	inode->i_nlink = nlink;
}
#endif /* HAVE_SET_NLINK */

/*
 * 3.3 API change,
 * The VFS .create, .mkdir and .mknod callbacks were updated to take a
 * umode_t type rather than an int.  To cleanly handle both definitions
 * the zpl_umode_t type is introduced and set accordingly.
 */
#ifdef HAVE_MKDIR_UMODE_T
typedef	umode_t		zpl_umode_t;
#else
typedef	int		zpl_umode_t;
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

/*
 * 3.6 API change,
 * The sget() helper function now takes the mount flags as an argument.
 */
#ifdef HAVE_5ARG_SGET
#define	zpl_sget(type, cmp, set, fl, mtd)	sget(type, cmp, set, fl, mtd)
#else
#define	zpl_sget(type, cmp, set, fl, mtd)	sget(type, cmp, set, mtd)
#endif /* HAVE_5ARG_SGET */

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
#ifndef HAVE_POSIX_ACL_CACHING
#define	ACL_NOT_CACHED ((void *)(-1))
#endif /* HAVE_POSIX_ACL_CACHING */

#if defined(HAVE_POSIX_ACL_RELEASE) && !defined(HAVE_POSIX_ACL_RELEASE_GPL_ONLY)

#define	zpl_posix_acl_release(arg)		posix_acl_release(arg)
#define	zpl_set_cached_acl(ip, ty, n)		set_cached_acl(ip, ty, n)
#define	zpl_forget_cached_acl(ip, ty)		forget_cached_acl(ip, ty)

#else

static inline void
zpl_posix_acl_free(void *arg) {
	kfree(arg);
}

static inline void
zpl_posix_acl_release(struct posix_acl *acl)
{
	if ((acl == NULL) || (acl == ACL_NOT_CACHED))
		return;

	if (atomic_dec_and_test(&acl->a_refcount)) {
		taskq_dispatch_delay(system_taskq, zpl_posix_acl_free, acl,
		    TQ_SLEEP, ddi_get_lbolt() + 60*HZ);
	}
}

static inline void
zpl_set_cached_acl(struct inode *ip, int type, struct posix_acl *newer) {
#ifdef HAVE_POSIX_ACL_CACHING
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
#endif /* HAVE_POSIX_ACL_CACHING */
}

static inline void
zpl_forget_cached_acl(struct inode *ip, int type) {
	zpl_set_cached_acl(ip, type, (struct posix_acl *)ACL_NOT_CACHED);
}
#endif /* HAVE_POSIX_ACL_RELEASE */

#ifndef HAVE___POSIX_ACL_CHMOD
#ifdef HAVE_POSIX_ACL_CHMOD
#define	__posix_acl_chmod(acl, gfp, mode)	posix_acl_chmod(acl, gfp, mode)
#define	__posix_acl_create(acl, gfp, mode)	posix_acl_create(acl, gfp, mode)
#else
static inline int
__posix_acl_chmod(struct posix_acl **acl, int flags, umode_t umode) {
	struct posix_acl *oldacl = *acl;
	mode_t mode = umode;
	int error;

	*acl = posix_acl_clone(*acl, flags);
	zpl_posix_acl_release(oldacl);

	if (!(*acl))
		return (-ENOMEM);

	error = posix_acl_chmod_masq(*acl, mode);
	if (error) {
		zpl_posix_acl_release(*acl);
		*acl = NULL;
	}

	return (error);
}

static inline int
__posix_acl_create(struct posix_acl **acl, int flags, umode_t *umodep) {
	struct posix_acl *oldacl = *acl;
	mode_t mode = *umodep;
	int error;

	*acl = posix_acl_clone(*acl, flags);
	zpl_posix_acl_release(oldacl);

	if (!(*acl))
		return (-ENOMEM);

	error = posix_acl_create_masq(*acl, &mode);
	*umodep = mode;

	if (error < 0) {
		zpl_posix_acl_release(*acl);
		*acl = NULL;
	}

	return (error);
}
#endif /* HAVE_POSIX_ACL_CHMOD */
#endif /* HAVE___POSIX_ACL_CHMOD */

#ifdef HAVE_POSIX_ACL_EQUIV_MODE_UMODE_T
typedef umode_t zpl_equivmode_t;
#else
typedef mode_t zpl_equivmode_t;
#endif /* HAVE_POSIX_ACL_EQUIV_MODE_UMODE_T */
#endif /* CONFIG_FS_POSIX_ACL */

#ifndef HAVE_CURRENT_UMASK
static inline int
current_umask(void)
{
	return (current->fs->umask);
}
#endif /* HAVE_CURRENT_UMASK */

/*
 * 2.6.38 API change,
 * The is_owner_or_cap() function was renamed to inode_owner_or_capable().
 */
#ifdef HAVE_INODE_OWNER_OR_CAPABLE
#define	zpl_inode_owner_or_capable(ip)		inode_owner_or_capable(ip)
#else
#define	zpl_inode_owner_or_capable(ip)		is_owner_or_cap(ip)
#endif /* HAVE_INODE_OWNER_OR_CAPABLE */

#endif /* _ZFS_VFS_H */
