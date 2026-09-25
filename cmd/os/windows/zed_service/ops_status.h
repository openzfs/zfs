/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#pragma once
#include <stddef.h>

#include "pipe_rpc.h"

// Returns HeapAlloc'd JSON for { "pools": [ ... ] }.
// Caller HeapFree()s the returned buffer. On error returns NULL.
char *zed_status_json_build(size_t *out_len);
char *zed_list_json_build(size_t *out_len);
char *zed_status_json_build_by_guid(uint64_t guid,
    zfs_status_verbosity_t verb, size_t *out_len);
