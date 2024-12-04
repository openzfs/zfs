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
 * Copyright (c) 2023, 2024, Klara Inc.
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 */

#ifndef _ZFS_MM_COMPAT_H
#define	_ZFS_MM_COMPAT_H

#include <linux/mm.h>
#include <linux/pagemap.h>

/* 5.4 introduced page_size(). Older kernels can use a trivial macro instead */
#ifndef HAVE_MM_PAGE_SIZE
#define	page_size(p) ((unsigned long)(PAGE_SIZE << compound_order(p)))
#endif

/* 6.11 removed page_mapping(). A simple wrapper around folio_mapping() works */
#ifndef HAVE_MM_PAGE_MAPPING
#define	page_mapping(p) folio_mapping(page_folio(p))
#endif

/*
 * 6.12 removed PG_error, SetPageError and ClearPageError, with no direct
 * replacement, because page writeback errors are recorded elsewhere. Since we
 * only use the page cache to assist with mmap(), never directly backing it
 * with IO, it shouldn't be possible for this condition to occur on our pages
 * anyway, even if this is the right way to report it. So it should be safe
 * to remove, but for avoidance of doubt, we make it a no-op on 6.12 and leave
 * it for everything else.
 */
#ifndef HAVE_MM_PAGE_FLAG_ERROR
#define	SetPageError(p)		do {} while (0)
#define	ClearPageError(p)	do {} while (0)
#endif

#endif /* _ZFS_MM_COMPAT_H */
