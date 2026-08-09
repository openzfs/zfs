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
 * Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
 */

#include <sys/zfs_context.h>
#include <sys/mmp.h>

int
param_set_multihost_interval(const char *val, zfs_kernel_param_t *kp)
{
	int ret;

	ret = spl_param_set_u64(val, kp);
	if (ret < 0)
		return (ret);

	if (spa_mode_global != SPA_MODE_UNINIT)
		mmp_signal_all_threads();

	return (ret);
}
