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
 * Copyright (C) 2015 Jörg Thalheim.
 */

#ifndef _ZFS_FS_H
#define	_ZFS_FS_H

#include <linux/fs.h>

#ifndef HAVE_FILE_INODE
static inline struct inode *file_inode(const struct file *f)
{
	return (f->f_dentry->d_inode);
}
#endif /* HAVE_FILE_INODE */

#endif /* _ZFS_FS_H */
