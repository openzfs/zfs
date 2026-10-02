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
#include <windows.h>
#include <stdint.h>

int ParsePoolNames(const char *json, int json_len,
    wchar_t names[][64], int maxnames);

typedef struct {
    wchar_t name[64];
    uint64_t guid;
    wchar_t health[32];
    wchar_t capacity_pct[16];
    wchar_t alloc[64];
    wchar_t freeb[64];
} PoolSummary;

BOOL GetPoolSummaryFromStatusJSON(const char *json, int json_len,
    PoolSummary *out);
