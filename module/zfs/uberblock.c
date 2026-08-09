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
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/uberblock_impl.h>
#include <sys/vdev_impl.h>
#include <sys/mmp.h>

int
uberblock_verify(uberblock_t *ub)
{
	if (ub->ub_magic == BSWAP_64((uint64_t)UBERBLOCK_MAGIC))
		byteswap_uint64_array(ub, sizeof (uberblock_t));

	if (ub->ub_magic != UBERBLOCK_MAGIC)
		return (SET_ERROR(EINVAL));

	return (0);
}

/*
 * Update the uberblock and return TRUE if anything changed in this
 * transaction group.
 */
boolean_t
uberblock_update(uberblock_t *ub, vdev_t *rvd, uint64_t txg, uint64_t mmp_delay)
{
	ASSERT(ub->ub_txg < txg);

	/*
	 * We explicitly do not set ub_version here, so that older versions
	 * continue to be written with the previous uberblock version.
	 */
	ub->ub_magic = UBERBLOCK_MAGIC;
	ub->ub_txg = txg;
	ub->ub_guid_sum = rvd->vdev_guid_sum;
	ub->ub_timestamp = gethrestime_sec();
	ub->ub_software_version = SPA_VERSION;
	ub->ub_mmp_magic = MMP_MAGIC;
	if (spa_multihost(rvd->vdev_spa)) {
		ub->ub_mmp_delay = mmp_delay;
		ub->ub_mmp_config = MMP_SEQ_SET(0) |
		    MMP_INTERVAL_SET(zfs_multihost_interval) |
		    MMP_FAIL_INT_SET(zfs_multihost_fail_intervals);
	} else {
		ub->ub_mmp_delay = 0;
		ub->ub_mmp_config = 0;
	}
	ub->ub_checkpoint_txg = 0;

	return (BP_GET_LOGICAL_BIRTH(&ub->ub_rootbp) == txg);
}
