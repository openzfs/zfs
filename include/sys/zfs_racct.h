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
 * Portions Copyright 2021 iXsystems, Inc.
 */

#ifndef _SYS_ZFS_RACCT_H
#define	_SYS_ZFS_RACCT_H

#include <sys/types.h>
#include <sys/spa.h>

/*
 * Platform-dependent resource accounting hooks
 */
void zfs_racct_read(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags);
void zfs_racct_write(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags);

#endif /* _SYS_ZFS_RACCT_H */
