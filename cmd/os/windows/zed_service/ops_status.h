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
