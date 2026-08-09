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

_ZFS_VALSTR_DECLARE_ENUM(zio_type)
_ZFS_VALSTR_DECLARE_ENUM(zio_priority)

#undef _ZFS_VALSTR_DECLARE_BITFIELD
#undef _ZFS_VALSTR_DECLARE_ENUM

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_VALSTR_H */
