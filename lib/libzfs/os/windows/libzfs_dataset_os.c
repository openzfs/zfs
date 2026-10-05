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
 * Copyright (c) 2026 Jorgen Lundman <lundman@lundman.net>
 */

#include <ctype.h>
#include <string.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <libzfs.h>

#include "libzfs_impl.h"

/*
 * Read the stored driveletter property first, then dynamically check the
 * mount table: if the dataset is currently mounted on a drive letter that
 * differs from the stored value (or the stored value is the auto-assign
 * default "-"), report the actual live letter with source=temporary. This
 * way "zfs get driveletter" is always useful without permanently changing
 * the property.
 *
 * On a live-letter override, propbuf/src are updated in place and B_TRUE is
 * returned; the caller should treat that as "done, return (0)" immediately.
 * Otherwise B_FALSE is returned and the caller should fall through to its
 * normal (stored-value) handling.
 */
boolean_t
zfs_prop_get_driveletter_os(zfs_handle_t *zhp, char *propbuf, size_t proplen,
    zprop_source_t *src)
{
	struct mnttab mntent;

	(void) proplen;

	if (libzfs_mnttab_find(zhp->zfs_hdl, zhp->zfs_name, &mntent) != 0 ||
	    mntent.mnt_mountp[0] == '\0' || mntent.mnt_mountp[1] != ':')
		return (B_FALSE);

	char actual = (char)tolower((unsigned char)mntent.mnt_mountp[0]);
	char stored = (char)tolower((unsigned char)propbuf[0]);

	if (stored != '-' && actual == stored)
		return (B_FALSE);

	propbuf[0] = actual;
	propbuf[1] = ':';
	propbuf[2] = '\0';
	if (src)
		*src = ZPROP_SRC_TEMPORARY;

	return (B_TRUE);
}
