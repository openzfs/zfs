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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzutil.h>
#include <limits.h>
#include <sys/spa.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "zpool_util.h"
#include <sys/zfs_context.h>

#include <sys/efi_partition.h>
#include <sys/stat.h>
#include <sys/vtoc.h>
#include <sys/mntent.h>
#include <uuid/uuid.h>

boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	return (B_FALSE);
}

int
check_device(const char *name, boolean_t force,
    boolean_t isspare, boolean_t iswholedisk)
{
	char path[MAXPATHLEN];

//	if (strncmp(name, _PATH_DEV, sizeof (_PATH_DEV) - 1) != 0)
//		snprintf(path, sizeof (path), "%s%s", _PATH_DEV, name);
//	else
//		strlcpy(path, name, sizeof (path));

	return (check_file(path, force, isspare));
}
