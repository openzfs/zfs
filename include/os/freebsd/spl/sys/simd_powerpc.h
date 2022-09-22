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
 * Copyright (C) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

/*
 * USER API:
 *
 * Kernel fpu methods:
 *	kfpu_allowed()
 *	kfpu_begin()
 *	kfpu_end()
 *	kfpu_init()
 *	kfpu_fini()
 *
 * SIMD support:
 *
 * Following functions should be called to determine whether CPU feature
 * is supported. All functions are usable in kernel and user space.
 * If a SIMD algorithm is using more than one instruction set
 * all relevant feature test functions should be called.
 *
 * Supported features:
 *   zfs_altivec_available()
 *   zfs_vsx_available()
 *   zfs_isa207_available()
 */

#ifndef _FREEBSD_SIMD_POWERPC_H
#define	_FREEBSD_SIMD_POWERPC_H

#include <sys/types.h>
#include <sys/cdefs.h>

#include <machine/pcb.h>
#include <machine/cpu.h>

#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)

/*
 * Check if Altivec is available
 */
static inline boolean_t
zfs_altivec_available(void)
{
	return ((cpu_features & PPC_FEATURE_HAS_ALTIVEC) != 0);
}

/*
 * Check if VSX is available
 */
static inline boolean_t
zfs_vsx_available(void)
{
	return ((cpu_features & PPC_FEATURE_HAS_VSX) != 0);
}

/*
 * Check if POWER ISA 2.07 is available (SHA2)
 */
static inline boolean_t
zfs_isa207_available(void)
{
	return ((cpu_features2 & PPC_FEATURE2_ARCH_2_07) != 0);
}

#endif
