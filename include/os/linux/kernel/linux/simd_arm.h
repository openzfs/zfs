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
 *   zfs_neon_available()
 *   zfs_sha256_available()
 */

#ifndef _LINUX_SIMD_ARM_H
#define	_LINUX_SIMD_ARM_H

#include <sys/types.h>
#include <asm/neon.h>
#include <asm/elf.h>
#include <asm/hwcap.h>

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

/*
 * Check if NEON is available
 */
static inline boolean_t
zfs_neon_available(void)
{
	return (elf_hwcap & HWCAP_NEON);
}

/*
 * Check if SHA256 is available
 */
static inline boolean_t
zfs_sha256_available(void)
{
	return (elf_hwcap2 & HWCAP2_SHA2);
}

#endif /* _LINUX_SIMD_ARM_H */
