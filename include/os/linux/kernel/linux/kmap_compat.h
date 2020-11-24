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

/* 2.6.37 API change */
#define	zfs_kmap_atomic(page)	kmap_atomic(page)
#define	zfs_kunmap_atomic(addr)	kunmap_atomic(addr)
#define	zfs_kmap(page)		kmap(page)
#define	zfs_kunmap(page)	kunmap(page)

/* 5.0 API change - no more 'type' argument for access_ok() */
#ifdef HAVE_ACCESS_OK_TYPE
#define	zfs_access_ok(type, addr, size)	access_ok(type, addr, size)
#else
#define	zfs_access_ok(type, addr, size)	access_ok(addr, size)
#endif

/*
 * read returning FOLL_WRITE is due to the fact that we are stating
 * that the kernel will have write access to the user pages. So, when
 * a Direct I/O read request is issued, the kernel must write to the user
 * pages.
 *
 * get_user_pages_unlocked was not available to 4.0, so we also check
 * for get_user_pages on older kernels.
 */
/* 4.9 API change - for and read flag is passed as gup flags */
#if defined(HAVE_GET_USER_PAGES_UNLOCKED_GUP_FLAGS)
#define	zfs_get_user_pages(addr, numpages, read, pages) \
	get_user_pages_unlocked(addr, numpages, pages, read ? FOLL_WRITE : 0)

/* 4.8 API change - no longer takes struct task_struct as arguement */
#elif defined(HAVE_GET_USER_PAGES_UNLOCKED_WRITE_FLAG)
#define	zfs_get_user_pages(addr, numpages, read, pages) \
	get_user_pages_unlocked(addr, numpages, read, 0, pages)

/* 4.0-4.3, 4.5-4.7 API */
#elif defined(HAVE_GET_USER_PAGES_UNLOCKED_TASK_STRUCT)
#define	zfs_get_user_pages(addr, numpages, read, pages) \
	get_user_pages_unlocked(current, current->mm, addr, numpages, read, 0, \
	    pages)

/* 4.4 API */
#elif defined(HAVE_GET_USER_PAGES_UNLOCKED_TASK_STRUCT_GUP_FLAGS)
#define	zfs_get_user_pages(addr, numpages, read, pages) \
	get_user_pages_unlocked(current, current->mm, addr, numpages, pages, \
	    read ? FOLL_WRITE : 0)

/* Using get_user_pages if kernel is < 4.0 */
#elif	defined(HAVE_GET_USER_PAGES_TASK_STRUCT)
#define	zfs_get_user_pages(addr, numpages, read, pages) \
	get_user_pages(current, current->mm, addr, numpages, read, 0, pages,  \
	    NULL)
#else
/*
 * This case is unreachable. We must be able to use either
 * get_user_pages_unlocked() or get_user_pages() to map user pages into
 * the kernel.
 */
#error	"Unknown Direct I/O interface"
#endif

#endif	/* _ZFS_KMAP_H */
