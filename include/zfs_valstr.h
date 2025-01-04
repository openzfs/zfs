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
 * Copyright (c) 2024, Klara Inc.
 */

#ifndef	_ZFS_VALSTR_H
#define	_ZFS_VALSTR_H extern __attribute__((visibility("default")))

#include <sys/fs/zfs.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * These macros create function prototypes for pretty-printing or stringifying
 * certain kinds of numeric types.
 *
 * _ZFS_VALSTR_DECLARE_BITFIELD(name) creates:
 *
 *   size_t zfs_valstr_<name>_bits(uint64_t bits, char *out, size_t outlen);
 *     expands single char for each set bit, and space for each clear bit
 *
 *   size_t zfs_valstr_<name>_pairs(uint64_t bits, char *out, size_t outlen);
 *     expands two-char mnemonic for each bit set in `bits`, separated by `|`
 *
 *   size_t zfs_valstr_<name>(uint64_t bits, char *out, size_t outlen);
 *     expands full name of each bit set in `bits`, separated by spaces
 *
 * _ZFS_VALSTR_DECLARE_ENUM(name) creates:
 *
 *   size_t zfs_valstr_<name>(int v, char *out, size_t outlen);
 *     expands full name of enum value
 *
 * Each _ZFS_VALSTR_DECLARE_xxx needs a corresponding _VALSTR_xxx_IMPL string
 * table in vfs_valstr.c.
 */

#define	_ZFS_VALSTR_DECLARE_BITFIELD(name)			\
	_ZFS_VALSTR_H size_t zfs_valstr_ ## name ## _bits(	\
	    uint64_t bits, char *out, size_t outlen);		\
	_ZFS_VALSTR_H size_t zfs_valstr_ ## name ## _pairs(	\
	    uint64_t bits, char *out, size_t outlen);		\
	_ZFS_VALSTR_H size_t zfs_valstr_ ## name(		\
	    uint64_t bits, char *out, size_t outlen);		\

#define	_ZFS_VALSTR_DECLARE_ENUM(name)				\
	_ZFS_VALSTR_H size_t zfs_valstr_ ## name(		\
	    int v, char *out, size_t outlen);			\

_ZFS_VALSTR_DECLARE_BITFIELD(zio_flag)
_ZFS_VALSTR_DECLARE_BITFIELD(zio_stage)

_ZFS_VALSTR_DECLARE_ENUM(zio_priority)

#undef _ZFS_VALSTR_DECLARE_BITFIELD
#undef _ZFS_VALSTR_DECLARE_ENUM

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_VALSTR_H */
