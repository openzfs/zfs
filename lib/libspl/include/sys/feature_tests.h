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

#ifndef _SYS_FEATURE_TESTS_H
#define	_SYS_FEATURE_TESTS_H

#define	____cacheline_aligned

#if !defined(zfs_fallthrough) && !defined(_LIBCPP_VERSION)
#if defined(HAVE_IMPLICIT_FALLTHROUGH)
#define	zfs_fallthrough		__attribute__((__fallthrough__))
#else
#define	zfs_fallthrough		((void)0)
#endif
#endif

#endif
