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
 * Portions Copyright 2020 iXsystems, Inc.
 */

#ifndef _SYS_ZFS_VFSOPS_H
#define	_SYS_ZFS_VFSOPS_H

#ifdef _KERNEL
#include <sys/zfs_vfsops_os.h>
#endif

extern void zfsvfs_update_fromname(const char *, const char *);

#endif /* _SYS_ZFS_VFSOPS_H */
