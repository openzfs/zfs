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
 */

#ifndef	_SYS_ZPL_H
#define	_SYS_ZPL_H

#include <sys/mntent.h>
#include <sys/vfs.h>
#include <linux/aio.h>
#include <linux/dcache_compat.h>
#include <linux/exportfs.h>
#include <linux/falloc.h>
#include <linux/parser.h>
#include <linux/vfs_compat.h>
#include <linux/writeback.h>
#include <linux/xattr_compat.h>

/* zpl_inode.c */
extern void zpl_vap_init(vattr_t *vap, struct inode *dir,
    umode_t mode, cred_t *cr, zidmap_t *mnt_ns);

extern const struct inode_operations zpl_inode_operations;
extern const struct inode_operations zpl_dir_inode_operations;
extern const struct inode_operations zpl_symlink_inode_operations;
extern const struct inode_operations zpl_special_inode_operations;

/* zpl_file.c */
extern const struct address_space_operations zpl_address_space_operations;
extern const struct file_operations zpl_file_operations;
extern const struct file_operations zpl_dir_file_operations;

/* zpl_super.c */
extern void zpl_prune_sb(uint64_t nr_to_scan, void *arg);

extern const struct super_operations zpl_super_operations;
extern const struct export_operations zpl_export_operations;
extern struct file_system_type zpl_fs_type;

/* zpl_xattr.c */
extern ssize_t zpl_xattr_list(struct dentry *dentry, char *buf, size_t size);
extern int zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr);

#if defined(CONFIG_FS_POSIX_ACL)

#if defined(HAVE_SET_ACL_IDMAP_DENTRY)
extern int zpl_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
    struct posix_acl *acl, int type);
#elif defined(HAVE_SET_ACL_USERNS)
extern int zpl_set_acl(struct user_namespace *userns, struct inode *ip,
    struct posix_acl *acl, int type);
#elif defined(HAVE_SET_ACL_USERNS_DENTRY_ARG2)
extern int zpl_set_acl(struct user_namespace *userns, struct dentry *dentry,
    struct posix_acl *acl, int type);
#else
extern int zpl_set_acl(struct inode *ip, struct posix_acl *acl, int type);
#endif /* HAVE_SET_ACL_USERNS */

#if defined(HAVE_GET_ACL_RCU) || defined(HAVE_GET_INODE_ACL)
extern struct posix_acl *zpl_get_acl(struct inode *ip, int type, bool rcu);
#elif defined(HAVE_GET_ACL)
extern struct posix_acl *zpl_get_acl(struct inode *ip, int type);
#endif
extern int zpl_init_acl(struct inode *ip, struct inode *dir);
extern int zpl_chmod_acl(struct inode *ip);
#else
static inline int
zpl_init_acl(struct inode *ip, struct inode *dir)
{
	return (0);
}

static inline int
zpl_chmod_acl(struct inode *ip)
{
	return (0);
}
#endif /* CONFIG_FS_POSIX_ACL */

extern xattr_handler_t *zpl_xattr_handlers[];

/* zpl_ctldir.c */
extern const struct file_operations zpl_fops_root;
extern const struct inode_operations zpl_ops_root;

extern const struct file_operations zpl_fops_snapdir;
extern const struct inode_operations zpl_ops_snapdir;

extern const struct file_operations zpl_fops_shares;
extern const struct inode_operations zpl_ops_shares;

/* zpl_file_range.c */

/* handlers for file_operations of the same name */
extern ssize_t zpl_copy_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, size_t len, unsigned int flags);
extern loff_t zpl_remap_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, loff_t len, unsigned int flags);
extern int zpl_clone_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, uint64_t len);
extern int zpl_dedupe_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, uint64_t len);

/* compat for FICLONE/FICLONERANGE/FIDEDUPERANGE ioctls */
typedef struct {
	int64_t		fcr_src_fd;
	uint64_t	fcr_src_offset;
	uint64_t	fcr_src_length;
	uint64_t	fcr_dest_offset;
} zfs_ioc_compat_file_clone_range_t;

typedef struct {
	int64_t		fdri_dest_fd;
	uint64_t	fdri_dest_offset;
	uint64_t	fdri_bytes_deduped;
	int32_t		fdri_status;
	uint32_t	fdri_reserved;
} zfs_ioc_compat_dedupe_range_info_t;

typedef struct {
	uint64_t	fdr_src_offset;
	uint64_t	fdr_src_length;
	uint16_t	fdr_dest_count;
	uint16_t	fdr_reserved1;
	uint32_t	fdr_reserved2;
	zfs_ioc_compat_dedupe_range_info_t	fdr_info[];
} zfs_ioc_compat_dedupe_range_t;

#define	ZFS_IOC_COMPAT_FICLONE		_IOW(0x94, 9, int)
#define	ZFS_IOC_COMPAT_FICLONERANGE \
    _IOW(0x94, 13, zfs_ioc_compat_file_clone_range_t)
#define	ZFS_IOC_COMPAT_FIDEDUPERANGE \
    _IOWR(0x94, 54, zfs_ioc_compat_dedupe_range_t)

extern long zpl_ioctl_ficlone(struct file *filp, void *arg);
extern long zpl_ioctl_ficlonerange(struct file *filp, void *arg);
extern long zpl_ioctl_fideduperange(struct file *filp, void *arg);


#if defined(HAVE_INODE_TIMESTAMP_TRUNCATE)
#define	zpl_inode_timestamp_truncate(ts, ip)	timestamp_truncate(ts, ip)
#else
#define	zpl_inode_timestamp_truncate(ts, ip)	\
	timespec64_trunc(ts, (ip)->i_sb->s_time_gran)
#endif

#if defined(HAVE_INODE_OWNER_OR_CAPABLE)
#define	zpl_inode_owner_or_capable(ns, ip)	inode_owner_or_capable(ip)
#elif defined(HAVE_INODE_OWNER_OR_CAPABLE_USERNS)
#define	zpl_inode_owner_or_capable(ns, ip)	inode_owner_or_capable(ns, ip)
#elif defined(HAVE_INODE_OWNER_OR_CAPABLE_IDMAP)
#define	zpl_inode_owner_or_capable(idmap, ip) inode_owner_or_capable(idmap, ip)
#else
#error "Unsupported kernel"
#endif

#if defined(HAVE_SETATTR_PREPARE_USERNS) || defined(HAVE_SETATTR_PREPARE_IDMAP)
#define	zpl_setattr_prepare(ns, dentry, ia)	setattr_prepare(ns, dentry, ia)
#else
/*
 * Use kernel-provided version, or our own from
 * linux/vfs_compat.h
 */
#define	zpl_setattr_prepare(ns, dentry, ia)	setattr_prepare(dentry, ia)
#endif

#ifdef HAVE_INODE_GET_CTIME
#define	zpl_inode_get_ctime(ip)	inode_get_ctime(ip)
#else
#define	zpl_inode_get_ctime(ip)	(ip->i_ctime)
#endif
#ifdef HAVE_INODE_SET_CTIME_TO_TS
#define	zpl_inode_set_ctime_to_ts(ip, ts)	inode_set_ctime_to_ts(ip, ts)
#else
#define	zpl_inode_set_ctime_to_ts(ip, ts)	(ip->i_ctime = ts)
#endif
#ifdef HAVE_INODE_GET_ATIME
#define	zpl_inode_get_atime(ip)	inode_get_atime(ip)
#else
#define	zpl_inode_get_atime(ip)	(ip->i_atime)
#endif
#ifdef HAVE_INODE_SET_ATIME_TO_TS
#define	zpl_inode_set_atime_to_ts(ip, ts)	inode_set_atime_to_ts(ip, ts)
#else
#define	zpl_inode_set_atime_to_ts(ip, ts)	(ip->i_atime = ts)
#endif
#ifdef HAVE_INODE_GET_MTIME
#define	zpl_inode_get_mtime(ip)	inode_get_mtime(ip)
#else
#define	zpl_inode_get_mtime(ip)	(ip->i_mtime)
#endif
#ifdef HAVE_INODE_SET_MTIME_TO_TS
#define	zpl_inode_set_mtime_to_ts(ip, ts)	inode_set_mtime_to_ts(ip, ts)
#else
#define	zpl_inode_set_mtime_to_ts(ip, ts)	(ip->i_mtime = ts)
#endif

#endif	/* _SYS_ZPL_H */
