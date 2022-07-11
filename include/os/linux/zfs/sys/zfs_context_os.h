/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
