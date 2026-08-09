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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_STRING_H
#define	_LIBSPL_STRING_H

#include_next <string.h>

#ifndef HAVE_STRLCAT
extern size_t strlcat(char *dst, const char *src, size_t dstsize);
#endif

#ifndef HAVE_STRLCPY
extern size_t strlcpy(char *dst, const char *src, size_t len);
#endif

#endif
