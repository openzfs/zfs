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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _LIBSPL_SYS_DEBUG_H
#define	_LIBSPL_SYS_DEBUG_H

#include <assert.h>

#ifndef	__printflike
#define	__printflike(x, y) __attribute__((__format__(__printf__, x, y)))
#endif

#ifndef __maybe_unused
#define	__maybe_unused __attribute__((unused))
#endif

#ifndef __must_check
#define	__must_check __attribute__((warn_unused_result))
#endif

#ifndef noinline
#define	noinline	__attribute__((noinline))
#endif

#ifndef likely
#define	likely(x)	__builtin_expect((x), 1)
#endif

#ifndef unlikely
#define	unlikely(x)	__builtin_expect((x), 0)
#endif

/*
 * Kernel modules
 */
#define		__init
#define		__exit

#endif
