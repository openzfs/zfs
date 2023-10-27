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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2019 by Delphix. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 * Copyright (c) 2017, Intel Corporation.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/fm/util.h>
#include <sys/dsl_scan.h>
#include <sys/fs/zfs.h>
#include <sys/kstat.h>
#include <sys/ZFSPool.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_boot.h>
#include <libkern/OSKextLib.h>

#include "zfs_prop.h"

const char *
spa_history_zone(void)
{
	return ("macos");
}

void
spa_import_os(spa_t *spa)
{
	int haslock = 0;
	int error;

	haslock = mutex_owned(&spa_namespace_lock);

	/* Increase open refcount */
	spa_open_ref(spa, FTAG);

	if (haslock) {
		mutex_exit(&spa_namespace_lock);
	}

	/* Create IOKit pool proxy */
	if ((error = spa_iokit_pool_proxy_create(spa)) != 0) {
		printf("%s spa_iokit_pool_proxy_create error %d\n",
		    __func__, error);
		/* spa_create succeeded, ignore proxy error */
	}

	/* Cache vdev info, needs open ref above, and pool proxy */

	if (error == 0 && (error = zfs_boot_update_bootinfo(spa)) != 0) {
		printf("%s update_bootinfo error %d\n", __func__, error);
		/* create succeeded, ignore error from bootinfo */
	}

	/* Drop open refcount */
	if (haslock) {
		mutex_enter(&spa_namespace_lock);
	}

	spa_close(spa, FTAG);
}

void
spa_export_os(spa_t *spa)
{
	/* Remove IOKit pool proxy */
	spa_iokit_pool_proxy_destroy(spa);
}

void
spa_activate_os(spa_t *spa)
{
	/* spa_t *spa = (spa_t *)arg; */
	/* Lock kext in kernel while mounted */
	OSKextRetainKextWithLoadTag(OSKextGetCurrentLoadTag());
}

void
spa_deactivate_os(spa_t *spa)
{
	/* spa_t *spa = (spa_t *)arg; */
	OSKextReleaseKextWithLoadTag(OSKextGetCurrentLoadTag());
}
