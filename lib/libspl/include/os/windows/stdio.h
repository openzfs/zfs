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

#ifndef _LIBSPL_WINDOWS_STDIO_H
#define	_LIBSPL_WINDOWS_STDIO_H

#include_next <stdio.h>

/*
 * This header is mostly here to include sysmacros, as so many places
 * appear to get MAX() and ISP() from somewhere else on other platforms.
 */
#include <sys/sysmacros.h>

#endif
