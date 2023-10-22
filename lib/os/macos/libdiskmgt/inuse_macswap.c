/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
