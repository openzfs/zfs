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
#include <sys/zfs_vfsops.h>

#include "zfs_prop.h"

// Windows might have something built-in to busy a driver?
uint64_t zfs_module_busy = 0;

const char *
spa_history_zone(void)
{
	return ("windows");
}

void
spa_create_os(void *arg)
{
}

void
spa_export_os(void *arg)
{
}

void
spa_activate_os(void *arg)
{
	 atomic_inc_64(&zfs_module_busy);
}

void
spa_deactivate_os(void *arg)
{
	atomic_dec_64(&zfs_module_busy);
}
