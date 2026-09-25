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
#include <stdio.h>

typedef struct {
    HANDLE h;		// pipe handle or INVALID_HANDLE_VALUE
    wchar_t name[128];	// pipe name
    DWORD  timeout_ms;	// per-call timeout
} zrpc_t;

static inline uint64_t
parse_u64_from_utf8(const char *s, int len)
{
	char tmp[40];
	int n = (len < 39 ? len : 39);
	memcpy(tmp, s, n);
	tmp[n] = 0;
	return (_strtoui64(tmp, NULL, 10));
}

BOOL  zrpc_init(zrpc_t *c, const wchar_t *pipename, DWORD timeout_ms);
void  zrpc_close(zrpc_t *c);

// Returns TRUE on success. Allocates *out on success; caller HeapFree’s it.
// On ERROR_BROKEN_PIPE / NOT_CONNECTED it will attempt one reconnect
// automatically.
BOOL  zrpc_call(zrpc_t *c, uint32_t op, const void *in, uint32_t in_len,
    uint32_t *status, uint8_t **out, uint32_t *out_len);
