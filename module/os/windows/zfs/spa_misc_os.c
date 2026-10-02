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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2019 by Delphix. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 * Copyright (c) 2017, Intel Corporation.
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
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
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vss.h>

#include "zfs_prop.h"

// Windows might have something built-in to busy a driver?
uint64_t zfs_module_busy = 0;

const char *
spa_history_zone(void)
{
	return ("windows");
}

void
spa_import_os(spa_t *spa)
{
	zfs_vss_pool_add(spa);
}

void
spa_export_os(spa_t *spa)
{
	zfs_vss_pool_remove(spa);
}

void
spa_activate_os(spa_t *arg)
{
	atomic_inc_64(&zfs_module_busy);
}

void
spa_deactivate_os(spa_t *arg)
{
	atomic_dec_64(&zfs_module_busy);
}
