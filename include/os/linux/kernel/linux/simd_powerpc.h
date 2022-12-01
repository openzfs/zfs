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

/* only for __powerpc__ */
#if defined(__powerpc__)

#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <asm/switch_to.h>
#include <sys/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
#include <asm/cpufeature.h>
#else
#include <asm/cputable.h>
#endif

#define	kfpu_allowed()			1

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
#ifdef CONFIG_SPE
#define	kfpu_begin()				\
	{					\
		preempt_disable();		\
		enable_kernel_altivec();	\
		enable_kernel_vsx();		\
		enable_kernel_spe();		\
	}
#define	kfpu_end()				\
	{					\
		disable_kernel_spe();		\
		disable_kernel_vsx();		\
		disable_kernel_altivec();	\
		preempt_enable();		\
	}
#else /* CONFIG_SPE */
#define	kfpu_begin()				\
	{					\
		preempt_disable();		\
		enable_kernel_altivec();	\
		enable_kernel_vsx();		\
	}
#define	kfpu_end()				\
	{					\
		disable_kernel_vsx();		\
		disable_kernel_altivec();	\
		preempt_enable();		\
	}
#endif
#else
/* seems that before 4.5 no-one bothered */
#define	kfpu_begin()
#define	kfpu_end()		preempt_enable()
#endif

#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

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

#endif /* defined(__powerpc) */

#endif /* _LINUX_SIMD_POWERPC_H */
