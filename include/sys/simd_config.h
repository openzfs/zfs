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
