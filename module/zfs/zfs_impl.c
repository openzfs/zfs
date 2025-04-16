// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <sys/zfs_impl.h>

#include <sys/blake3.h>
#include <sys/sha2.h>

/*
 * impl_ops - backend for implementations of algorithms
 */
const zfs_impl_t *impl_ops[] = {
	&zfs_blake3_ops,
	&zfs_sha256_ops,
	&zfs_sha512_ops,
	NULL
};

/*
 * zfs_impl_get_ops - Get the API functions for an impl backend
 */
const zfs_impl_t *
zfs_impl_get_ops(const char *algo)
{
	const zfs_impl_t **ops = impl_ops;

	if (!algo || !*algo)
		return (*ops);

	for (; *ops; ops++) {
		if (strcmp(algo, (*ops)->name) == 0)
			break;
	}

	ASSERT3P(ops, !=, NULL);
	return (*ops);
}
