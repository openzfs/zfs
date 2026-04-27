// SPDX-License-Identifier: CDDL-1.0
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
