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

#ifndef ZFS_CONTEXT_OS_H
#define	ZFS_CONTEXT_OS_H

#include <linux/dcache_compat.h>
#include <linux/utsname_compat.h>
#include <linux/compiler_compat.h>
#include <linux/module.h>

#if THREAD_SIZE >= 16384
#define	HAVE_LARGE_STACKS	1
#endif

#if defined(CONFIG_UML)
#undef setjmp
#undef longjmp
#endif

#endif
