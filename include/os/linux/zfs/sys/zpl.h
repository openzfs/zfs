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
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_SYS_ZPL_H
#define	_SYS_ZPL_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/mntent.h>
#include <sys/vfs.h>
#include <linux/aio.h>
#include <linux/dcache_compat.h>
#include <linux/exportfs.h>
#include <linux/falloc.h>
#include <linux/parser.h>
#include <linux/vfs_compat.h>
#include <linux/writeback.h>
#include <linux/idmap_compat.h>
#include <linux/xattr_compat.h>

/* zpl_inode.c */
extern void zpl_vap_init(vattr_t *vap, struct inode *dir,
    umode_t mode, cred_t *cr, zidmap_t *idmap);

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
extern const struct dentry_operations zpl_dentry_operations;
extern const struct export_operations zpl_export_operations;
extern struct file_system_type zpl_fs_type;

/* zpl_xattr.c */
extern ssize_t zpl_xattr_list(struct dentry *dentry, char *buf, size_t size);
extern int zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr);

#if defined(CONFIG_FS_POSIX_ACL)
extern int zpl_set_posix_acl(struct inode *ip, struct posix_acl *acl, int type);
extern struct posix_acl *zpl_get_posix_acl(struct inode *ip, int type);
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

/*
 * Snapentry. Held on the snapdir dentry, coordinates mount, unmount and
 * access through the snapdir mountpoint.
 */
typedef struct zfs_snapentry {
	/* The snapdir dentry itself, that owns this snapentry (via d_fsdata) */
	struct dentry	*se_dentry;

	/*
	 * State flags, see below. Early in struct to be in first cacheline,
	 * for unlocked RCU-walk check in zpl_snapdir_manage().
	 */
	unsigned long	se_flags;

	/* Time of last transit through this snapdir. */
	uint64_t	se_atime;

	/* se_mtx protects se_flags, se_taskqid, se_mount_task and se_cv */
	kmutex_t	se_mtx;

	/* ID of expiry timer. */
	taskqid_t	se_taskqid;

	/* Mount task, see zpl_snapdir_manage(). */
	struct task_struct *se_mount_task;

	/* Pool & snapshot objset that we mounted */
	spa_t		*se_spa;
	uint64_t	se_objsetid;

	/* Signal state transition completed, SE_BUSY clear. */
	kcondvar_t	se_cv;
} zfs_snapentry_t;

/*
 * Snapentry flags.
 *
 * Bit 0 indicates that something currently adding a mount or invalidating the
 * dentry.
 *
 * Bit 1 indicates an in-flight VFS op wants to traverse into the mount if
 * it exists; see zpl_snapdir_manage().
 */
enum {
	SE_BUSY,	/* mounting or unmounting, should wait */
	SE_WANT_MOUNT,	/* next VFS op should attempt automount */
};

/*
 * We use atomic bitops for se_flags because we need to test SE_BUSY without
 * holding se_mtx in zpl_snapdir_manage(). Beyond that, they are always
 * protected by se_mtx.
 */
#define	SE_TEST(se, fl)		test_bit((fl), &(se)->se_flags)
#define	SE_SET(se, fl)		set_bit((fl), &(se)->se_flags)
#define	SE_CLEAR(se, fl)	clear_bit((fl), &(se)->se_flags)

extern void zfsctl_snapshot_timer_clear(zfs_snapentry_t *se);

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


#if defined(HAVE_INODE_TIMESTAMP_TRUNCATE)
#define	zpl_inode_timestamp_truncate(ts, ip)	timestamp_truncate(ts, ip)
#else
#define	zpl_inode_timestamp_truncate(ts, ip)	\
	timespec64_trunc(ts, (ip)->i_sb->s_time_gran)
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
