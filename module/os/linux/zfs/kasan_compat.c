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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef _ZFS_LINUX_KASAN_ENABLED_H
#define	_ZFS_LINUX_KASAN_ENABLED_H

#ifdef HAVE_KASAN_ENABLED_GPL_ONLY
/*
 * The kernel supports a runtime setting to enable/disable KASAN. The control
 * flag kasan_flag_enabled is a GPL-only symbol, which prevents us from
 * accessing it. Unfortunately, this is called by the header function
 * kasan_enabled(), which in turn is used to call or skip instrumentation
 * functions in various header-based kernel facilities. If we inadvertently
 * call one, the build breaks.
 *
 * To work around this, we define our own `kasan_flag_enabled` set to "false",
 * disabling use of KASAN inside our code. The linker will resolve this symbol
 * at build time, and so never need to reach out to the off-limits kernel
 * symbol.
 */
#include <linux/static_key.h>
struct static_key_false kasan_flag_enabled = STATIC_KEY_FALSE_INIT;
#endif

#endif
