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

/*
 * Regardless of what happens inside ZFS a success code must never be returned
 * by mistake for critical operations like fsync() in case of forced exit.
 * Hence, zfsvfs' state must be re-checked on ZPL level as the final line of
 * defense.
 */
static inline int
zfsvfs_error(zfsvfs_t *zfsvfs)
{
	if (unlikely(zfsvfs == NULL ||
	    zfsvfs->z_os == NULL ||
	    zfsvfs->z_os->os_spa == NULL))
		return (SET_ERROR(EIO));

	if (SPA_EXITING(zfsvfs->z_os->os_spa))
		return (SET_ERROR(EIO));

	return (0);
}
#endif

extern void zfsvfs_update_fromname(const char *, const char *);

#endif /* _SYS_ZFS_VFSOPS_H */
