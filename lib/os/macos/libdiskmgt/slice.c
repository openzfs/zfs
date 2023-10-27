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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <dirent.h>

#include "libdiskmgt.h"
#include "disks_private.h"
#include <libnvpair.h>

#ifndef VT_ENOTSUP
#define	VT_ENOTSUP	(-5)
#endif

#define	FMT_UNKNOWN	0
#define	FMT_VTOC	1
#define	FMT_EFI		2

typedef int (*detectorp)(char *, nvlist_t *, int *);

static detectorp detectors[] = {
	inuse_mnt,
	inuse_corestorage,
	inuse_partition,
	inuse_active_zpool,
	inuse_exported_zpool,
	inuse_fs,  /* fs should always be last */
	NULL
};

static int	add_inuse(char *name, nvlist_t *attrs);

nvlist_t *
slice_get_stats(char *slice, int stat_type, int *errp)
{
	nvlist_t	*stats;

	if (stat_type != DM_SLICE_STAT_USE) {
		*errp = EINVAL;
		return (NULL);
	}

	*errp = 0;

	if (nvlist_alloc(&stats, NVATTRS_STAT, 0) != 0) {
		*errp = ENOMEM;
		return (NULL);
	}

	if ((*errp = add_inuse(slice, stats)) != 0) {
		return (NULL);
	}

	return (stats);
}

/*
 * Check if/how the slice is used.
 */
static int
add_inuse(char *name, nvlist_t *attrs)
{
	int	i = 0;
	int	error = 0;

	for (i = 0; detectors[i] != NULL; i ++) {
		if (detectors[i](name, attrs, &error) || error != 0) {
			if (error != 0) {
				return (error);
			}
			break;
		}
	}

	return (0);
}
