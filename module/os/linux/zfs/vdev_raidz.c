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
/* Copyright (C) 2025 ConnectWise */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_raidz.h>

int
param_get_raidz_impl(char *buf, zfs_kernel_param_t *kp)
{
	return (vdev_raidz_impl_get(buf, PAGE_SIZE));
}

int
param_set_raidz_impl(const char *val, zfs_kernel_param_t *kp)
{
	int error;

	error = vdev_raidz_impl_set(val);
	return (error);
}
