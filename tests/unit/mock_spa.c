// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, Hewlett Packard Enterprise Development LP.
 */

#include <sys/types.h>
#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/zfeature.h>

/*
 * Mock implementations of spa/dsl functions for unit tests.
 */
char *
spa_name(spa_t *spa)
{
	(void) spa;
	static char name[] = "mockpool";
	return (name);
}

void
spa_feature_incr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	(void) spa, (void) fid, (void) tx;
}

void
spa_feature_decr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	(void) spa, (void) fid, (void) tx;
}

boolean_t
spa_feature_is_active(spa_t *spa, spa_feature_t fid)
{
	(void) spa, (void) fid;
	return (B_FALSE);
}
