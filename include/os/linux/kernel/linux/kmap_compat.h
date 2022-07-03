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

/* 5.0 API change - no more 'type' argument for access_ok() */
#ifdef HAVE_ACCESS_OK_TYPE
#define	zfs_access_ok(type, addr, size)	access_ok(type, addr, size)
#else
#define	zfs_access_ok(type, addr, size)	access_ok(addr, size)
#endif

#endif	/* _ZFS_KMAP_H */
