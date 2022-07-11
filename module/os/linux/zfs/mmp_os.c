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
 * Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
 */

#include <sys/zfs_context.h>
#include <sys/mmp.h>

int
param_set_multihost_interval(const char *val, zfs_kernel_param_t *kp)
{
	int ret;

	ret = param_set_ulong(val, kp);
	if (ret < 0)
		return (ret);

	if (spa_mode_global != SPA_MODE_UNINIT)
		mmp_signal_all_threads();

	return (ret);
}
