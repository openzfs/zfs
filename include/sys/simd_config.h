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
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _SYS_SIMD_CONFIG_H
#define	_SYS_SIMD_CONFIG_H

/*
 * Goal: a HAVE_SIMD(x) macro that expands to either 1 or 0 depending on
 * the availabilty of that extension on the wanted toolchain.
 *
 * We need to allow the following:
 * - #define HAVE_TOOLCHAIN_AVX 1   (zfs_config.h, detected by configure)
 * - #undef HAVE_TOOLCHAIN_AVX      (zfs_config.h, probed but not detected)
 * - -DHAVE_TOOLCHAIN_AVX           (Makefile.bsd)
 *
 * For completeness, we also allow disabling by defined HAVE_TOOLCHAIN_AVX=0.
 *
 * The "obvious" implementation of this would be a macro that includes
 * defined(...) in its expansion, but unfortunately that is not portable, and
 * can produce compiler warnings (see -Wexpansion-to-defined). So we need to
 * build our own version.
 */

/*
 * 1. Expand incoming token to its defined value, if anything:
 *    eg HAVE_SIMD(AVX)
 *	-> _zfs_deftrue(HAVE_TOOLCHAIN_AVX)
 *	or -> __zfs_deftrue()			    (HAVE_TOOLCHAIN_AVX defined)
 *	or -> __zfs_deftrue(1)			    (HAVE_TOOLCHAIN_AVX = 1)
 *	   -> __zfs_deftrue(0)			    (HAVE_TOOLCHAIN_AVX = 0)
 *	or -> __zfs_deftrue(HAVE_TOOLCHAIN_AVX)	    (HAVE_TOOLCHAIN_AVX undef)
 */
#define	_zfs_deftrue(x)		__zfs_deftrue(x)

/*
 * 2. Replace know values with a token that we control:
 *    __zfs_deftrue()
 *	-> ___zfs_deftrue(___zfs_deftrue_arg_)
 *    __zfs_deftrue(1)
 *	-> ___zfs_deftrue(___zfs_deftrue_arg_1)
 *    __zfs_deftrue(0)
 *	-> ___zfs_deftrue(___zfs_deftrue_arg_0)
 *    __zfs_deftrue(HAVE_TOOLCHAIN_AVX)
 *	-> ___zfs_deftrue(___zfs_deftrue_arg_HAVE_TOOLCHAIN_AVX)
 */
#define	__zfs_deftrue(v)	___zfs_deftrue(___zfs_deftrue_arg_##v)

/*
 * 3. Expand the incoming token into positional parameters for the next call:
 *    ___zfs_deftrue(___zfs_deftrue_arg_0)
 *	-> ____zfs_deftrue(0, 0, 0)
 *    ___zfs_deftrue(___zfs_deftrue_arg_1)
 *	-> ____zfs_deftrue(0, 1, 0)
 *    ___zfs_deftrue(___zfs_deftrue_arg_HAVE_TOOLCHAIN_AVX)
 *	-> ____zfs_deftrue(___zfs_deftrue_arg_HAVE_TOOLCHAIN_AVX, 0)
 */
#define	___zfs_deftrue_arg_		0, 1
#define	___zfs_deftrue_arg_1		0, 1
#define	___zfs_deftrue_arg_0		0, 0
#define	___zfs_deftrue(t, ...)		____zfs_deftrue(t, 0)

/*
 * 4. Emit the second argument, either the original value or the default 0.
 *    ____zfs_deftrue(0, 0, 0)					-> 0
 *    ____zfs_deftrue(0, 1, 0)					-> 1
 *    ____zfs_deftrue(___zfs_deftrue_arg_HAVE_TOOLCHAIN_AVX, 0)	-> 0
 */
#define	____zfs_deftrue(_n, v, ...)	v

/*
 * The Linux kernel requires a specific toolchain. Everything else uses the
 * regular compiler toolchain.
 */
#if defined(_KERNEL) && defined(__linux__)
#define	HAVE_SIMD(ext)	_zfs_deftrue(HAVE_KERNEL_##ext)
#else
#define	HAVE_SIMD(ext)	_zfs_deftrue(HAVE_TOOLCHAIN_##ext)
#endif

#endif
