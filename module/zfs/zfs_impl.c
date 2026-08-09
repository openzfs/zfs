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
