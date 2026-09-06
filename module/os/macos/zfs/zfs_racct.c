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

#include <sys/zfs_racct.h>

void
zfs_racct_read(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags)
{
	spa_iostats_read_add(spa, size, iops, flags);
}

void
zfs_racct_write(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags)
{
	spa_iostats_write_add(spa, size, iops, flags);
}
