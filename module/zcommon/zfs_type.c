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
 * Copyright (c) 2016 by ClusterHQ Inc. All rights reserved.
 */

#include <zfs_type.h>

#if defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif

static const char *zfs_types[] = { "filesystem", "snapshot", "volume", "pool",
	"bookmark" };

#define	ZFS_TYPE_COUNT	(sizeof (zfs_types) / sizeof (&zfs_types[0]))

const char *
zfs_type_name(zfs_type_t type)
{
	return ((type < ZFS_TYPE_COUNT) ? zfs_types[type] : NULL);
}

int
zfs_nvl_to_type(nvlist_t *nvl, const char *key, zfs_type_t *type)
{
	int i;
	char *type_in;

	if (nvlist_lookup_string(nvl, key, &type_in) != 0)
		return (ENOENT);

	for (i = 0; i < ZFS_TYPE_COUNT; i++) {
		if (strcmp(zfs_types[i], type_in) == 0) {
			*type = i;
			return (0);
		}
	}

	return (EINVAL);
}

nvlist_t *
zfs_type_to_nvl(zfs_type_t type)
{
	nvlist_t *nvl = fnvlist_alloc();
	int i;

	for (i = 0; i < ZFS_TYPE_COUNT; i++)
		if (type & (1 << i))
			fnvlist_add_boolean(nvl, zfs_types[i]);

	return (nvl);
}
