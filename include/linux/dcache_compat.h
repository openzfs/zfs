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

#ifndef _ZFS_DCACHE_H
#define _ZFS_DCACHE_H

#include <linux/dcache.h>

#define dname(dentry)	((char *)((dentry)->d_name.name))
#define dlen(dentry)	((int)((dentry)->d_name.len))

#ifndef HAVE_D_MAKE_ROOT
#define d_make_root(inode)	d_alloc_root(inode)
#endif /* HAVE_D_MAKE_ROOT */

#endif /* _ZFS_DCACHE_H */
