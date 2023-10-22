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
	return ((ftr >> 12) & 0x3);
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

#endif /* _MACOS_SIMD_AARCH64_H */
