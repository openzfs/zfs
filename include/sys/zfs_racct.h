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
 * Portions Copyright 2021 iXsystems, Inc.
 */

#ifndef _SYS_ZFS_RACCT_H
#define	_SYS_ZFS_RACCT_H

#include <sys/types.h>
#include <sys/spa.h>

/*
 * Platform-dependent resource accounting hooks
 */
void zfs_racct_read(spa_t *spa, uint64_t size, uint64_t iops,
    dmu_flags_t flags);
void zfs_racct_write(spa_t *spa, uint64_t size, uint64_t iops,
    dmu_flags_t flags);

#endif /* _SYS_ZFS_RACCT_H */
