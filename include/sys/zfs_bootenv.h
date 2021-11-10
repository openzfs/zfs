/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2020 Toomas Soome <tsoome@me.com>
 */

#ifndef _ZFS_BOOTENV_H
#define	_ZFS_BOOTENV_H

/*
 * Define macros for label bootenv nvlist pair keys.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	BOOTENV_VERSION		"version"

#define	BE_ILLUMOS_VENDOR	"illumos"
#define	BE_FREEBSD_VENDOR	"freebsd"
#define	BE_GRUB_VENDOR		"grub"
#define	BE_LINUX_VENDOR		"linux"

#include <sys/zfs_bootenv_os.h>

#define	GRUB_ENVMAP		BE_GRUB_VENDOR ":" "envmap"

#define	FREEBSD_BOOTONCE	BE_FREEBSD_VENDOR ":" "bootonce"
#define	FREEBSD_BOOTONCE_USED	BE_FREEBSD_VENDOR ":" "bootonce-used"
#define	FREEBSD_NVSTORE		BE_FREEBSD_VENDOR ":" "nvstore"
#define	ILLUMOS_BOOTONCE	BE_ILLUMOS_VENDOR ":" "bootonce"
#define	ILLUMOS_BOOTONCE_USED	BE_ILLUMOS_VENDOR ":" "bootonce-used"
#define	ILLUMOS_NVSTORE		BE_ILLUMOS_VENDOR ":" "nvstore"

#define	OS_BOOTONCE		BOOTENV_OS ":" "bootonce"
#define	OS_BOOTONCE_USED	BOOTENV_OS ":" "bootonce-used"
#define	OS_NVSTORE		BOOTENV_OS ":" "nvstore"

#ifdef __cplusplus
}
#endif

#endif /* _ZFS_BOOTENV_H */
