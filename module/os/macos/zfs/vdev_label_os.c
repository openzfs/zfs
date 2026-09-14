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
 * Copyright (c) 2023 by iXsystems, Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>

/*
 * Check if the reserved boot area is in-use.
 *
 * This function always returns 0, as there are no known external uses
 * of the reserved area on Linux.
 */
int
vdev_check_boot_reserve(spa_t *spa, vdev_t *childvd)
{
	(void) spa;
	(void) childvd;

	return (0);
}
