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
#include <linux/exportfs.h>
#include <linux/writeback.h>

/* zpl_inode.c */
extern const struct inode_operations zpl_inode_operations;
extern const struct inode_operations zpl_dir_inode_operations;
extern const struct inode_operations zpl_symlink_inode_operations;
extern const struct inode_operations zpl_special_inode_operations;

/* zpl_file.c */
extern ssize_t zpl_read_common(struct inode *ip, const char *buf,
    size_t len, loff_t pos, uio_seg_t segment, int flags, cred_t *cr);
extern ssize_t zpl_write_common(struct inode *ip, const char *buf,
    size_t len, loff_t pos, uio_seg_t segment, int flags, cred_t *cr);

extern const struct address_space_operations zpl_address_space_operations;
extern const struct file_operations zpl_file_operations;
extern const struct file_operations zpl_dir_file_operations;

/* zpl_super.c */
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

/* zpl_snap.c */
#define ZFS_CTLDIR_NAME     ".zfs"
#define ZFS_SNAPDIR_NAME    "snapshot"
#define ZFS_SHAREDIR_NAME   "shares"

void zpl_snap_create(zfs_sb_t *zsb);
void zpl_snap_destroy(zfs_sb_t *zsb);

/* Functions related to dir entries, required for snapshot automounting */
extern const struct dentry_operations zfs_dentry_ops;

#endif	/* _SYS_ZPL_H */
