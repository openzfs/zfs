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
 * Copyright (c) 2022 by Triad National Security, LLC.
 */

#include <sys/vdev_impl.h>

#ifdef _KERNEL

int
param_set_direct_write_verify_pct(const char *buf, zfs_kernel_param_t *kp)
{
	uint_t val;
	int error;

	error = kstrtouint(buf, 0, &val);
	if (error < 0)
		return (SET_ERROR(error));

	if (val > 100)
		return (SET_ERROR(-EINVAL));

	error = param_set_uint(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	return (0);
}

#endif /* _KERNEL */
