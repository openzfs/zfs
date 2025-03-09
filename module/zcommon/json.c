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

#include "json.h"

void
json_add_output_version(nvlist_t *nvl, const char *cmd, int maj_v, int min_v)
{
	nvlist_t *ov = fnvlist_alloc();
	fnvlist_add_string(ov, "command", cmd);
	fnvlist_add_uint32(ov, "vers_major", maj_v);
	fnvlist_add_uint32(ov, "vers_minor", min_v);
	fnvlist_add_nvlist(nvl, "output_version", ov);
	fnvlist_free(ov);
}
