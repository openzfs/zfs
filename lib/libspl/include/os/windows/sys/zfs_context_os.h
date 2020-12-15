/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

#ifndef ZFS_CONTEXT_OS_H_
#define	ZFS_CONTEXT_OS_H_

#define	ZFS_EXPORTS_PATH	"/etc/exports"
#define	MNTTYPE_ZFS_SUBTYPE ('Z'<<24|'F'<<16|'S'<<8)

struct spa_iokit;
typedef struct spa_iokit spa_iokit_t;

typedef off_t loff_t;

struct zfs_handle;

#define	noinline		__attribute__((noinline))

extern void zfs_rollback_os(struct zfs_handle *zhp);
extern void libzfs_macos_wrapfd(int *srcfd, boolean_t send);

#endif
