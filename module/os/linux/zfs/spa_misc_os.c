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
#include "zfs_prop.h"


int
param_set_deadman_failmode(const char *val, zfs_kernel_param_t *kp)
{
	int error;

	error = -param_set_deadman_failmode_common(val);
	if (error == 0)
		error = param_set_charp(val, kp);

	return (error);
}

int
param_set_deadman_ziotime(const char *val, zfs_kernel_param_t *kp)
{
	int error;

	error = spl_param_set_u64(val, kp);
	if (error < 0)
		return (SET_ERROR(error));

	spa_set_deadman_ziotime(MSEC2NSEC(zfs_deadman_ziotime_ms));

	return (0);
}

int
param_set_deadman_synctime(const char *val, zfs_kernel_param_t *kp)
{
	int error;

	error = spl_param_set_u64(val, kp);
	if (error < 0)
		return (SET_ERROR(error));

	spa_set_deadman_synctime(MSEC2NSEC(zfs_deadman_synctime_ms));

	return (0);
}

int
param_set_slop_shift(const char *buf, zfs_kernel_param_t *kp)
{
	unsigned long val;
	int error;

	error = kstrtoul(buf, 0, &val);
	if (error)
		return (SET_ERROR(error));

	if (val < 1 || val > 31)
		return (SET_ERROR(-EINVAL));

	error = param_set_int(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

const char *
spa_history_zone(void)
{
	return ("linux");
}

void
spa_import_os(spa_t *spa)
{
	(void) spa;
}

void
spa_export_os(spa_t *spa)
{
	(void) spa;
}

void
spa_activate_os(spa_t *spa)
{
	(void) spa;
}

void
spa_deactivate_os(spa_t *spa)
{
	(void) spa;
}
