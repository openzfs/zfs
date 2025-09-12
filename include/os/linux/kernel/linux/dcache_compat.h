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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_DCACHE_H
#define	_ZFS_DCACHE_H

#include <linux/dcache.h>

#define	dname(dentry)	((char *)((dentry)->d_name.name))
#define	dlen(dentry)	((int)((dentry)->d_name.len))

#define	d_alias			d_u.d_alias

/*
 * Starting from Linux 5.13, flush_dcache_page() becomes an inline function
 * and under some configurations, may indirectly referencing GPL-only
 * symbols, e.g., cpu_feature_keys on powerpc and PageHuge on riscv.
 * Override this function when it is detected being GPL-only.
 */
#if defined __powerpc__ && defined HAVE_FLUSH_DCACHE_PAGE_GPL_ONLY
#include <linux/simd_powerpc.h>
#define	flush_dcache_page(page)	do {					\
		if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE) &&	\
		    test_bit(PG_dcache_clean, &(page)->flags))		\
			clear_bit(PG_dcache_clean, &(page)->flags);	\
	} while (0)
#endif
/*
 * For riscv implementation, the use of PageHuge can be safely removed.
 * Because it handles pages allocated by HugeTLB, while flush_dcache_page
 * in zfs module is only called on kernel pages.
 */
#if defined __riscv && defined HAVE_FLUSH_DCACHE_PAGE_GPL_ONLY
#define	flush_dcache_page(page)	do {					\
		if (test_bit(PG_dcache_clean, &(page)->flags))		\
			clear_bit(PG_dcache_clean, &(page)->flags);	\
	} while (0)
#endif

/*
 * Walk and invalidate all dentry aliases of an inode
 * unless it's a mountpoint
 */
static inline void
zpl_d_drop_aliases(struct inode *inode)
{
	struct dentry *dentry;
	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_alias) {
		if (!IS_ROOT(dentry) && !d_mountpoint(dentry) &&
		    (dentry->d_inode == inode)) {
			d_drop(dentry);
		}
	}
	spin_unlock(&inode->i_lock);
}
#endif /* _ZFS_DCACHE_H */
