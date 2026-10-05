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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _LIBSPL_SYS_XATTR_H
#define	_LIBSPL_SYS_XATTR_H

#include_next <sys/xattr.h>

/* macOS has one more argument */
#define	setxattr(A, B, C, D, E) setxattr(A, B, C, D, E, 0)
#define	getxattr(A, B, C, D, E) getxattr(A, B, C, D, E, 0)

#endif
