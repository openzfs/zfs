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

#ifndef	_ZFS_PRETTY_H
#define	_ZFS_PRETTY_H extern __attribute__((visibility("default")))

#include <sys/fs/zfs.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	_ZFS_PRETTY_DECLARE(name)	\
	_ZFS_PRETTY_H size_t zfs_pretty_ ## name ## _bits( \
	    uint64_t bits, char *out, size_t outlen); \
	_ZFS_PRETTY_H size_t zfs_pretty_ ## name ## _pairs( \
	    uint64_t bits, char *out, size_t outlen); \
	_ZFS_PRETTY_H size_t zfs_pretty_ ## name ## _str( \
	    uint64_t bits, char *out, size_t outlen); \

_ZFS_PRETTY_DECLARE(zio_flag)
_ZFS_PRETTY_DECLARE(abd_flag)
_ZFS_PRETTY_DECLARE(arc_flag)

#undef _ZFS_PRETTY_DECLARE

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_PRETTY_H */
