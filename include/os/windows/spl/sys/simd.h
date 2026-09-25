/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
