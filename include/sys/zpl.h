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

#ifndef	_SYS_ZPL_H
#define	_SYS_ZPL_H

#include <sys/vfs.h>
#include <linux/vfs_compat.h>
#include <linux/xattr_compat.h>
#include <linux/dcache_compat.h>
#include <linux/exportfs.h>
#include <linux/writeback.h>
#include <linux/falloc.h>

/* zpl_inode.c */
extern void zpl_vap_init(vattr_t *vap, struct inode *dir,
    zpl_umode_t mode, cred_t *cr);

extern const struct inode_operations zpl_inode_operations;
extern const struct inode_operations zpl_dir_inode_operations;
extern const struct inode_operations zpl_symlink_inode_operations;
extern const struct inode_operations zpl_special_inode_operations;
extern dentry_operations_t zpl_dentry_operations;

/* zpl_file.c */
extern ssize_t zpl_read_common(struct inode *ip, const char *buf,
    size_t len, loff_t pos, uio_seg_t segment, int flags, cred_t *cr);
extern ssize_t zpl_write_common(struct inode *ip, const char *buf,
    size_t len, loff_t pos, uio_seg_t segment, int flags, cred_t *cr);
extern long zpl_fallocate_common(struct inode *ip, int mode,
    loff_t offset, loff_t len);

extern const struct address_space_operations zpl_address_space_operations;
extern const struct file_operations zpl_file_operations;
extern const struct file_operations zpl_dir_file_operations;

/* zpl_super.c */
extern void zpl_prune_sbs(int64_t bytes_to_scan, void *private);

typedef struct zpl_mount_data {
	const char *z_osname;	/* Dataset name */
	void *z_data;		/* Mount options string */
} zpl_mount_data_t;

extern const struct super_operations zpl_super_operations;
extern const struct export_operations zpl_export_operations;
extern struct file_system_type zpl_fs_type;

/* zpl_xattr.c */
extern ssize_t zpl_xattr_list(struct dentry *dentry, char *buf, size_t size);
extern int zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr);

extern xattr_handler_t *zpl_xattr_handlers[];

/* zpl_ctldir.c */
extern const struct file_operations zpl_fops_root;
extern const struct inode_operations zpl_ops_root;

extern const struct file_operations zpl_fops_snapdir;
extern const struct inode_operations zpl_ops_snapdir;
#ifdef HAVE_AUTOMOUNT
extern const struct dentry_operations zpl_dops_snapdirs;
#else
extern const struct inode_operations zpl_ops_snapdirs;
#endif /* HAVE_AUTOMOUNT */

extern const struct file_operations zpl_fops_shares;
extern const struct inode_operations zpl_ops_shares;

#endif	/* _SYS_ZPL_H */
