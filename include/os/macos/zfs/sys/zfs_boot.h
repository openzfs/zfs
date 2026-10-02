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
 * Copyright (c) 2016, Evan Susarret.  All rights reserved.
 */

#ifndef	ZFS_BOOT_H_INCLUDED
#define	ZFS_BOOT_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* Link data vdevs to virtual devices */
int zfs_boot_update_bootinfo(spa_t *spa);

int zfs_attach_devicedisk(zfsvfs_t *zfsvfs);
int zfs_detach_devicedisk(zfsvfs_t *zfsvfs);
int zfs_devdisk_get_path(void *, char *, int);


#ifdef __cplusplus
} /* extern "C" */
#endif	/* __cplusplus */



#ifdef __cplusplus
#include <IOKit/IOService.h>
bool zfs_boot_init(IOService *);
void zfs_boot_fini();
#endif	/* __cplusplus */


#endif /* ZFS_BOOT_H_INCLUDED */
