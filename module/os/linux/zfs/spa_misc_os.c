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
#include <sys/dmu.h>
#include <sys/fs/zfs.h>
#include <sys/kstat.h>
#include <sys/zone.h>
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

int
param_set_active_allocator(const char *val, zfs_kernel_param_t *kp)
{
	int error;

	error = -param_set_active_allocator_common(val);
	if (error == 0)
		error = param_set_charp(val, kp);

	return (error);
}

const char *
spa_history_zone(void)
{
	return ("linux");
}

static int
spa_restore_zoned_uid_cb(const char *dsname, void *arg)
{
	(void) arg;
	char setpoint[ZFS_MAX_DATASET_NAME_LEN] = { 0 };
	uint64_t zoned_uid = 0;
	int err;

	err = dsl_prop_get(dsname, "zoned_uid", 8, 1, &zoned_uid,
	    setpoint);
	if (err != 0)
		return (err);
	if (!dsl_prop_setpoint_is_direct(dsname, setpoint))
		return (0);

	zone_dataset_set_uid(dsname, zoned_uid);
	return (0);
}

void
spa_import_os(spa_t *spa)
{
	int error;

	zone_dataset_remove_uid(spa_name(spa));
	error = dmu_objset_find(spa_name(spa),
	    spa_restore_zoned_uid_cb, NULL, DS_FIND_CHILDREN);
	if (error != 0) {
		zone_dataset_remove_uid(spa_name(spa));
		cmn_err(CE_WARN, "failed to restore zoned_uid for pool "
		    "'%s': %d", spa_name(spa), error);
	}
}

void
spa_export_os(spa_t *spa)
{
	zone_dataset_remove_uid(spa_name(spa));
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
