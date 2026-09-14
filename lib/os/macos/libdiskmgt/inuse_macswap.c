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
 * Copyright (c) 2016, Brendon Humphrey (brendon.humphrey@mac.com).
 */

#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include "disks_private.h"

static const char *SWAP_SYSCTL_NAME = "vm.swapfileprefix";

/* Return index of last slash or strlen if none */
static ssize_t
zfs_dirnamelen(const char *path)
{
	const char *end = strrchr(path, '/');
	return (end ? end - path : strlen(path));
}

int
inuse_macswap(const char *dev_name)
{
	size_t oldlen = 0;
	char *tmp;
	char *swap_filename;
	char real_swap_path[MAXPATHLEN];
	char real_dev_path[MAXPATHLEN];
	int idx;

	/* Obtain the swap file prefix (path + prototype basename) */
	if (sysctlbyname(SWAP_SYSCTL_NAME, NULL, &oldlen, NULL, 0) != 0)
		return (0);

	swap_filename = (char *)malloc(oldlen);
	if (sysctlbyname(SWAP_SYSCTL_NAME, swap_filename, &oldlen, NULL,
	    0) != 0)
		return (0);

	/*
	 * Get the directory portion of the vm.swapfileprefix sysctl
	 * once links etc have been resolved.
	 */
	tmp = realpath(swap_filename, NULL);
	idx = zfs_dirnamelen(swap_filename);

	(void) strlcpy(real_swap_path, swap_filename, idx);
	free(swap_filename);
	free(tmp);

	/* Get the (resolved) directory portion of dev_name */
	tmp = realpath(dev_name, NULL);
	idx = zfs_dirnamelen(tmp);
	(void) strlcpy(real_dev_path, tmp, idx);
	free(tmp);

	/* If the strings are equal, the file is in the swap dir */
	return (strcmp(real_dev_path, real_swap_path) == 0);
}
