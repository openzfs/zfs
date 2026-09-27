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

#ifndef _SPL_PARAM_H
#define	_SPL_PARAM_H

/* Pages to bytes and back */
#define	ptob(pages)			(pages << PAGE_SHIFT)
#define	btop(bytes)			(bytes >> PAGE_SHIFT)
#ifndef howmany
#define	howmany(x, y)   ((((x) % (y)) == 0) ? ((x) / (y)) : (((x) / (y)) + 1))
#endif

#define	MAXUID				UINT32_MAX

#define	PAGESHIFT	PAGE_SHIFT

#endif /* SPL_PARAM_H */
