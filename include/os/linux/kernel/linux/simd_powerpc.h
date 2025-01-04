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
 * Copyright (C) 2019 Romain Dolbeau
 *           <romain.dolbeau@european-processor-initiative.eu>
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

#ifndef _LINUX_SIMD_POWERPC_H
#define	_LINUX_SIMD_POWERPC_H

#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <asm/switch_to.h>
#include <sys/types.h>
#include <linux/version.h>
#include <asm/cpufeature.h>

#define	kfpu_allowed()			1

#ifdef	CONFIG_ALTIVEC
#define	ENABLE_KERNEL_ALTIVEC	enable_kernel_altivec();
#define	DISABLE_KERNEL_ALTIVEC	disable_kernel_altivec();
#else
#define	ENABLE_KERNEL_ALTIVEC
#define	DISABLE_KERNEL_ALTIVEC
#endif
#ifdef	CONFIG_VSX
#define	ENABLE_KERNEL_VSX	enable_kernel_vsx();
#define	DISABLE_KERNEL_VSX	disable_kernel_vsx();
#else
#define	ENABLE_KERNEL_VSX
#define	DISABLE_KERNEL_VSX
#endif
#ifdef	CONFIG_SPE
#define	ENABLE_KERNEL_SPE	enable_kernel_spe();
#define	DISABLE_KERNEL_SPE	disable_kernel_spe();
#else
#define	ENABLE_KERNEL_SPE
#define	DISABLE_KERNEL_SPE
#endif
#define	kfpu_begin()				\
	{					\
		preempt_disable();		\
		ENABLE_KERNEL_ALTIVEC		\
		ENABLE_KERNEL_VSX		\
		ENABLE_KERNEL_SPE		\
	}
#define	kfpu_end()				\
	{					\
		DISABLE_KERNEL_SPE		\
		DISABLE_KERNEL_VSX		\
		DISABLE_KERNEL_ALTIVEC		\
		preempt_enable();		\
	}

#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

/*
 * Linux 4.7 makes cpu_has_feature to use jump labels on powerpc if
 * CONFIG_JUMP_LABEL_FEATURE_CHECKS is enabled, in this case however it
 * references GPL-only symbol cpu_feature_keys. Therefore we overrides this
 * interface when it is detected being GPL-only.
 */
#if defined(CONFIG_JUMP_LABEL_FEATURE_CHECKS) && \
    defined(HAVE_CPU_HAS_FEATURE_GPL_ONLY)
#define	cpu_has_feature(feature)	early_cpu_has_feature(feature)
#endif

/*
 * Check if AltiVec instruction set is available
 */
static inline boolean_t
zfs_altivec_available(void)
{
	return (cpu_has_feature(CPU_FTR_ALTIVEC));
}

/*
 * Check if VSX is available
 */
static inline boolean_t
zfs_vsx_available(void)
{
	return (cpu_has_feature(CPU_FTR_VSX));
}

/*
 * Check if POWER ISA 2.07 is available (SHA2)
 */
static inline boolean_t
zfs_isa207_available(void)
{
	return (cpu_has_feature(CPU_FTR_ARCH_207S));
}

#endif /* _LINUX_SIMD_POWERPC_H */
