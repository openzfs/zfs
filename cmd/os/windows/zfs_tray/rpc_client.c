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

#include "rpc_client.h"

// Include after libzfs for dprintf
#include "pipe_rpc.h"

static HANDLE
zrpc_connect_once(const wchar_t *pipename, DWORD timeout_ms)
{
	// Try open directly first
	HANDLE h = CreateFileW(pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE)
		return (h);

	DWORD err = GetLastError();
	if (err != ERROR_PIPE_BUSY && err != ERROR_FILE_NOT_FOUND)
		return (INVALID_HANDLE_VALUE);

	// Wait for an instance to become available
	if (!WaitNamedPipeW(pipename, timeout_ms))
		return (INVALID_HANDLE_VALUE);

	// Try again
	return (CreateFileW(pipename, GENERIC_READ | GENERIC_WRITE, 0, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
}

static BOOL
zrpc_connect_with_retry(const wchar_t *pipename, HANDLE *ph)
{
	const int max_tries = 8;
	const DWORD step_ms = 200; // total up to ~1.6s
	for (int i = 0; i < max_tries; i++) {
		HANDLE h = zrpc_connect_once(pipename, step_ms);
		if (h != INVALID_HANDLE_VALUE) {
			*ph = h;
			return (TRUE);
		}
	}
	return (FALSE);
}


static BOOL
zrpc_connect(zrpc_t *c)
{
	HANDLE h;
	if (c->h != INVALID_HANDLE_VALUE)
		return (TRUE);

	if (zrpc_connect_with_retry(c->name, &h)) {
		c->h = h;
		dprintf("%s: connected\n", __func__);
		return (TRUE);
	}
	return (FALSE);
}

BOOL
zrpc_init(zrpc_t *c, const wchar_t *pipename, DWORD timeout_ms)
{
	ZeroMemory(c, sizeof (*c));
	c->h = INVALID_HANDLE_VALUE;
	lstrcpynW(c->name, pipename, _countof(c->name));
	c->timeout_ms = timeout_ms ? timeout_ms : 5000;
	return (zrpc_connect(c));
}

void
zrpc_close(zrpc_t *c)
{
	if (c->h != INVALID_HANDLE_VALUE) {
		dprintf("%s: closing handle\n", __func__);
		CloseHandle(c->h);
		c->h = INVALID_HANDLE_VALUE;
	}
}

static BOOL
read_all(HANDLE h, void *buf, uint32_t len, DWORD timeout)
{
	uint8_t *p = (uint8_t *)buf;
	DWORD got = 0, tot = 0;
	while (tot < len) {
		if (!ReadFile(h, p+tot, len-tot, &got, NULL))
			return (FALSE);
		tot += got;
	}
	return (TRUE);
}

static BOOL
write_all(HANDLE h, const void *buf, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	DWORD sent = 0, tot = 0;
	while (tot < len) {
		if (!WriteFile(h, p+tot, len-tot, &sent, NULL))
			return (FALSE);
		tot += sent;
	}
	return (TRUE);
}

static BOOL
do_call_once(zrpc_t *c, uint32_t op, const void *in, uint32_t in_len,
    uint32_t *status, uint8_t **out, uint32_t *out_len)
{
	req_hdr_t rq = { op, in_len };

	dprintf("%s: sending op %d\n", __func__, op);


	if (!write_all(c->h, &rq, sizeof (rq)))
		return (FALSE);
	if (in_len && !write_all(c->h, in, in_len))
		return (FALSE);

	rsp_hdr_t rs;
	if (!read_all(c->h, &rs, sizeof (rs), c->timeout_ms))
		return (FALSE);

	uint8_t *buf = NULL;
	if (rs.len) {
		buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, rs.len);
		if (!buf)
			return (FALSE);
		if (!read_all(c->h, buf, rs.len, c->timeout_ms)) {
			HeapFree(GetProcessHeap(), 0, buf);
			dprintf("%s: read_all payload failed\n", __func__);
			return (FALSE);
		}
	}
	if (status) *status = rs.status;
	if (out) *out = buf;
	else if (buf)
		HeapFree(GetProcessHeap(), 0, buf);
	if (out_len) *out_len = rs.len;
	dprintf("recv hdr: err=%u size=%u\n", rs.status, rs.len);

	// One thing per connection, one day fix?
	zrpc_close(c);

	return (TRUE);
}

BOOL
zrpc_call(zrpc_t *c, uint32_t op, const void *in, uint32_t in_len,
    uint32_t *status, uint8_t **out, uint32_t *out_len)
{
	dprintf("%s: call op %d \n", __func__, op);

	if (!zrpc_connect(c)) {
		Sleep(500);

		if (!zrpc_connect(c))
			return (FALSE);
	}
	dprintf("%s: connected %d \n", __func__, op);

	if (do_call_once(c, op, in, in_len, status, out, out_len))
		return (TRUE);

	DWORD e = GetLastError();
	dprintf("%s: GetLastError %x \n", __func__, e);
	if (e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED) {
		zrpc_close(c);
		Sleep(500);
		if (!zrpc_connect(c))
			return (FALSE);
		return (do_call_once(c, op, in, in_len, status, out, out_len));
	}
	return (FALSE);
}
