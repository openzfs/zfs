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
#define _ZFS_VFS_H

/*
 * 2.6.35 API change,
 * The dentry argument to the .fsync() vfs hook was deemed unused by
 * all filesystem consumers and dropped.  Add a compatibility prototype
 * to ensure correct usage when defining this callback.
 */
#ifdef HAVE_2ARGS_FSYNC
#define ZPL_FSYNC_PROTO(fn, x, y, z)	static int fn(struct file *x, int z)
#else
#define ZPL_FSYNC_PROTO(fn, x, y, z)	static int fn(struct file *x, \
						      struct dentry *y, int z)
#endif

#endif /* _ZFS_VFS_H */
