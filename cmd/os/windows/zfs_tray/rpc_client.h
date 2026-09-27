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
