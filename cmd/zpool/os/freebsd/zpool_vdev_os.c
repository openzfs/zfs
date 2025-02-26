// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016, 2017 Intel Corporation.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 */

/*
 * Functions to convert between a list of vdevs and an nvlist representing the
 * configuration.  Each entry in the list can be one of:
 *
 * 	Device vdevs
 * 		disk=(path=..., devid=...)
 * 		file=(path=...)
 *
 * 	Group vdevs
 * 		raidz[1|2]=(...)
 * 		mirror=(...)
 *
 * 	Hot spares
 *
 * While the underlying implementation supports it, group vdevs cannot contain
 * other group vdevs.  All userland verification of devices is contained within
 * this file.  If successful, the nvlist returned can be passed directly to the
 * kernel; we've done as much verification as possible in userland.
 *
 * Hot spares are a special case, and passed down as an array of disk vdevs, at
 * the same level as the root of the vdev tree.
 *
 * The only function exported by this file is 'make_root_vdev'.  The
 * function performs several passes:
 *
 * 	1. Construct the vdev specification.  Performs syntax validation and
 *         makes sure each device is valid.
 * 	2. Check for devices in use.  Using libdiskmgt, makes sure that no
 *         devices are also in use.  Some can be overridden using the 'force'
 *         flag, others cannot.
 * 	3. Check for replication errors if the 'force' flag is not specified.
 *         validates that the replication level is consistent across the
 *         entire pool.
 * 	4. Call libzfs to label any whole disks with an EFI label.
 */

#include <assert.h>
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
#include <paths.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/mntent.h>
#include <libgeom.h>

#include "zpool_util.h"
#include <sys/zfs_context.h>

int
check_device(const char *name, boolean_t force, boolean_t isspare,
    boolean_t iswholedisk)
{
	(void) iswholedisk;
	char path[MAXPATHLEN];

	if (strncmp(name, _PATH_DEV, sizeof (_PATH_DEV) - 1) != 0)
		snprintf(path, sizeof (path), "%s%s", _PATH_DEV, name);
	else
		strlcpy(path, name, sizeof (path));

	return (check_file(path, force, isspare));
}

boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	(void) path, (void) sector_size;
	return (0);
}

void
after_zpool_upgrade(zpool_handle_t *zhp)
{
	char bootfs[ZPOOL_MAXPROPLEN];

	if (zpool_get_prop(zhp, ZPOOL_PROP_BOOTFS, bootfs,
	    sizeof (bootfs), NULL, B_FALSE) == 0 &&
	    strcmp(bootfs, "-") != 0) {
		(void) printf(gettext("Pool '%s' has the bootfs "
		    "property set, you might need to update\nthe boot "
		    "code. See gptzfsboot(8) and loader.efi(8) for "
		    "details.\n"), zpool_get_name(zhp));
	}
}

int
check_file(const char *file, boolean_t force, boolean_t isspare)
{
	return (check_file_generic(file, force, isspare));
}

int
zpool_power_current_state(zpool_handle_t *zhp, char *vdev)
{

	(void) zhp;
	(void) vdev;
	/* Enclosure slot power not supported on FreeBSD yet */
	return (-1);
}

int
zpool_power(zpool_handle_t *zhp, char *vdev, boolean_t turn_on)
{

	(void) zhp;
	(void) vdev;
	(void) turn_on;
	/* Enclosure slot power not supported on FreeBSD yet */
	return (ENOTSUP);
}
