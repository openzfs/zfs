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

#ifndef _MACOS_SIMD_AARCH64_H
#define	_MACOS_SIMD_AARCH64_H

#include <sys/types.h>

#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)


#define	get_ftr(id, __val) {	\
		asm("mrs %0, "#id : "=r" (__val)); \
	}

/*
 * Check if NEON is available
 */
static inline boolean_t
zfs_neon_available(void)
{
	/* All armv8 has neon, macOS only runs on armv8 */
	return (B_TRUE);
}

/*
 * Check if SHA256 is available
 */
static inline boolean_t
zfs_sha256_available(void)
{
	uint64_t ftr;
	get_ftr(ID_AA64ISAR0_EL1, ftr);
	return ((ftr >> 12) & 0x3);
}

/*
 * Check if SHA512 is available
 */
static inline boolean_t
zfs_sha512_available(void)
{
	uint64_t ftr;
	get_ftr(ID_AA64ISAR0_EL1, ftr);
	return (((ftr >> 12) & 0x3) >= 2);
}

/*
 * Check if AESV8 is available
 */
static inline boolean_t
zfs_aesv8_available(void)
{
	uint64_t ftr;
	get_ftr(ID_AA64ISAR0_EL1, ftr);
	return ((ftr >> 4) & 0xf);
}

static inline boolean_t
zfs_pmull_available(void)
{
	uint64_t ftr;
	get_ftr(ID_AA64ISAR0_EL1, ftr);
	return ((ftr >> 8) & 0xf);
}

#endif /* _MACOS_SIMD_AARCH64_H */
