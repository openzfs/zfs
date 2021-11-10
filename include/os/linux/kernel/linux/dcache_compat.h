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
#define	_ZFS_DCACHE_H

#include <linux/dcache.h>

#define	dname(dentry)	((char *)((dentry)->d_name.name))
#define	dlen(dentry)	((int)((dentry)->d_name.len))

#ifndef HAVE_D_MAKE_ROOT
#define	d_make_root(inode)	d_alloc_root(inode)
#endif /* HAVE_D_MAKE_ROOT */

/*
 * 2.6.30 API change,
 * The const keyword was added to the 'struct dentry_operations' in
 * the dentry structure.  To handle this we define an appropriate
 * dentry_operations_t typedef which can be used.
 */
typedef const struct dentry_operations	dentry_operations_t;

/*
 * 2.6.38 API addition,
 * Added d_clear_d_op() helper function which clears some flags and the
 * registered dentry->d_op table.  This is required because d_set_d_op()
 * issues a warning when the dentry operations table is already set.
 * For the .zfs control directory to work properly we must be able to
 * override the default operations table and register custom .d_automount
 * and .d_revalidate callbacks.
 */
static inline void
d_clear_d_op(struct dentry *dentry)
{
	dentry->d_op = NULL;
	dentry->d_flags &= ~(
	    DCACHE_OP_HASH | DCACHE_OP_COMPARE |
	    DCACHE_OP_REVALIDATE | DCACHE_OP_DELETE);
}

#endif /* _ZFS_DCACHE_H */
