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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_STAT_H
#define	_SYS_FS_ZFS_STAT_H

#ifdef _KERNEL
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/dmu.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A limited number of zpl level stats are retrievable
 * with an ioctl.  zfs diff is the current consumer.
 */
typedef struct zfs_stat {
	uint64_t	zs_gen;
	uint64_t	zs_mode;
	uint64_t	zs_links;
	uint64_t	zs_ctime[2];
} zfs_stat_t;

extern int zfs_obj_to_stats(objset_t *osp, uint64_t obj, zfs_stat_t *sb,
    char *buf, int len);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_STAT_H */
