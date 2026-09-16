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
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

#ifndef _ZFS_KMAP_H
#define	_ZFS_KMAP_H

#include <linux/highmem.h>
#include <linux/uaccess.h>

#ifdef HAVE_KMAP_LOCAL_PAGE
/* 5.11 API change */
#define	zfs_kmap_local(page)   kmap_local_page(page)
#define	zfs_kunmap_local(addr) kunmap_local(addr)
#else
/* 2.6.37 API change */
#define	zfs_kmap_local(page)   kmap_atomic(page)
#define	zfs_kunmap_local(addr) kunmap_atomic(addr)
#endif
#define	zfs_kmap(page)		kmap(page)
#define	zfs_kunmap(page)	kunmap(page)

/*
 * Does zfs_kmap_local() give access to only the one page it was passed?
 * If not, a run of physically contiguous pages can be mapped once and
 * accessed as a whole.
 *
 * Only CONFIG_HIGHMEM kernels ever set up a single page temporary
 * mapping; everywhere else zfs_kmap_local() is just page_address().  We
 * deliberately do not narrow this to PageHighMem(), because a HIGHMEM
 * kernel built with CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP maps lowmem pages
 * one at a time as well.  Compare folio_test_partial_kmap() upstream.
 */
static inline int
zfs_kmap_partial(void)
{
	return (IS_ENABLED(CONFIG_HIGHMEM));
}

/* 5.0 API change - no more 'type' argument for access_ok() */
#ifdef HAVE_ACCESS_OK_TYPE
#define	zfs_access_ok(type, addr, size)	access_ok(type, addr, size)
#else
#define	zfs_access_ok(type, addr, size)	access_ok(addr, size)
#endif

#endif	/* _ZFS_KMAP_H */
