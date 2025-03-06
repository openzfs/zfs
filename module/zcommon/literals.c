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
 * Copyright (c) 2025, Klara, Inc.
 */

#include "literals.h"

static const char *vdev_state_strings[] = {
	"UNKNOWN",
	"CLOSED",
	"OFFLINE",
	"REMOVED",
	"CANT_OPEN",
	"FAULTED",
	"DEGRADED",
	"ONLINE"
};

const char *
vdev_state_string(uint64_t state)
{
	if (state <= VDEV_STATE_HEALTHY)
		return (vdev_state_strings[state]);
	return ("UNDEFINED");
}

static const char *vdev_aux_strings[] = {
	"NONE",
	"OPEN_FAILED",
	"CORRUPT_DATA",
	"NO_REPLICAS",
	"BAD_GUID_SUM",
	"TOO_SMALL",
	"BAD_LABEL",
	"VERSION_NEWER",
	"VERSION_OLDER",
	"UNSUP_FEAT",
	"SPARED",
	"ERR_EXCEEDED",
	"IO_FAILURE",
	"BAD_LOG",
	"EXTERNAL",
	"SPLIT_POOL",
	"BAD_ASHIFT",
	"EXTERNAL_PERSIST",
	"ACTIVE",
	"CHILDREN_OFFLINE",
	"ASHIFT_TOO_BIG"
};

const char *
vdev_aux_string(uint64_t aux)
{
	if (aux <= VDEV_AUX_ASHIFT_TOO_BIG)
		return (vdev_aux_strings[aux]);
	return ("UNDEFINED");
}
