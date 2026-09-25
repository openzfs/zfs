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
