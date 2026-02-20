// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#include <sys/types.h>
#include <string.h>
#include <libzfs.h>
#include <libzfsbootenv.h>
#include <sys/fs/zfs.h>

/*
 * Set a "default" loader name for the target platform. This is the traditional
 * behaviour of libzfsbootenv for these platforms that have a dedicated loader
 * and integrated tooling.
 *
 * For anything else, just use "unknown". The application should be setting
 * a name to match the loader it is setting up environment for, and if not, at
 * least those won't trample anything.
 */
#if defined(__FreeBSD__)
#define	LOADER_DEFAULT	ZFS_BE_LOADER_FREEBSD
#elif defined(__illumos__)
#define	LOADER_DEFAULT	ZFS_BE_LOADER_ILLUMOS
#else
#define	LOADER_DEFAULT	"unknown"
#endif

#define	LOADER_MAXLEN	(32)
static char lzbe_loader[LOADER_MAXLEN+1] = LOADER_DEFAULT;

const char *
lzbe_loader_get(void)
{
	return (lzbe_loader);
}

int
lzbe_loader_set(const char *loader, size_t len)
{
	if (len > LOADER_MAXLEN)
		return (ENAMETOOLONG);
	stpncpy(lzbe_loader, loader, LOADER_MAXLEN);
	return (0);
}
