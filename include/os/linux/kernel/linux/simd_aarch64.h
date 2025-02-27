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
 * Copyright (C) 2016 Romain Dolbeau <romain@dolbeau.org>.
 * Copyright (C) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 * Copyright (C) 2022 Sebastian Gottschall <s.gottschall@dd-wrt.com>
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
 *   zfs_neon_available()
 *   zfs_sha256_available()
 *   zfs_sha512_available()
 */

#ifndef _LINUX_SIMD_AARCH64_H
#define	_LINUX_SIMD_AARCH64_H

#include <sys/types.h>
#include <asm/neon.h>
#include <asm/elf.h>
#include <asm/hwcap.h>
#include <linux/version.h>
#include <asm/sysreg.h>

#define	ID_AA64PFR0_EL1		sys_reg(3, 0, 0, 1, 0)
#define	ID_AA64ISAR0_EL1	sys_reg(3, 0, 0, 6, 0)

#if (defined(HAVE_KERNEL_NEON) && defined(CONFIG_KERNEL_MODE_NEON))
#define	kfpu_allowed()		1
#define	kfpu_begin()		kernel_neon_begin()
#define	kfpu_end()		kernel_neon_end()
#else
#define	kfpu_allowed()		0
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#endif
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)

#define	get_ftr(id) {				\
	unsigned long __val;			\
	asm("mrs %0, "#id : "=r" (__val));	\
	__val;					\
}

/*
 * Check if NEON is available
 */
static inline boolean_t
zfs_neon_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64PFR0_EL1)) >> 16) & 0xf;
	return (ftr == 0 || ftr == 1);
}

/*
 * Check if SHA256 is available
 */
static inline boolean_t
zfs_sha256_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64ISAR0_EL1)) >> 12) & 0x3;
	return (ftr & 0x1);
}

/*
 * Check if SHA512 is available
 */
static inline boolean_t
zfs_sha512_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64ISAR0_EL1)) >> 12) & 0x3;
	return (ftr & 0x2);
}

#endif /* _LINUX_SIMD_AARCH64_H */
