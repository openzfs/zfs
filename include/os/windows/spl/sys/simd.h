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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@compeng.uni-frankfurt.de>.
 */

/*
 * USER API:
 *
 * Kernel fpu methods:
 * 	kfpu_begin()
 * 	kfpu_end()
 *
 * SIMD support:
 *
 * Following functions should be called to determine whether CPU feature
 * is supported. All functions are usable in kernel and user space.
 * If a SIMD algorithm is using more than one instruction set
 * all relevant feature test functions should be called.
 *
 * Supported features:
 * 	zfs_sse_available()
 * 	zfs_sse2_available()
 * 	zfs_sse3_available()
 * 	zfs_ssse3_available()
 * 	zfs_sse4_1_available()
 * 	zfs_sse4_2_available()
 *
 * 	zfs_avx_available()
 * 	zfs_avx2_available()
 *
 * 	zfs_bmi1_available()
 * 	zfs_bmi2_available()
 *
 * 	zfs_avx512f_available()
 * 	zfs_avx512cd_available()
 * 	zfs_avx512er_available()
 * 	zfs_avx512pf_available()
 * 	zfs_avx512bw_available()
 * 	zfs_avx512dq_available()
 * 	zfs_avx512vl_available()
 * 	zfs_avx512ifma_available()
 * 	zfs_avx512vbmi_available()
 *
 * NOTE(AVX-512VL):	If using AVX-512 instructions with 128Bit registers
 * 			also add zfs_avx512vl_available() to feature check.
 */

#ifndef _WINDOWS_SYS_SIMD_H
#define	_WINDOWS_SYS_SIMD_H

#include <sys/isa_defs.h>
#include <sys/processor.h>

#if defined(__amd64__) || defined(__i386__)
#include <sys/simd_x86.h>

#elif defined(__aarch64__)
#include <sys/simd_aarch64.h>

#else
#define	kfpu_allowed()		0
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#endif

#ifndef simd_stat_init
#define	simd_stat_init()	do {} while (0)
#endif

#ifndef simd_stat_fini
#define	simd_stat_fini()	do {} while (0)
#endif

#endif /* _WINDOWS_SYS_SIMD_H */
