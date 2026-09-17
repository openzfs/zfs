// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
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
