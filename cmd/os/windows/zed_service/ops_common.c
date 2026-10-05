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

#define	_CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libzfs.h>
#include <sys/nvpair.h>

#include "pipe_rpc.h"
#include "memfile.h"
#include "ops_common.h"

/* Provided by your service main */
extern libzfs_handle_t *g_lzh;

char *
zed_json_from_nvlist(nvlist_t *nvl, size_t *out_len)
{
	memfile_t mf;
	FILE *fp = memfile_open(&mf);
	nvlist_print_json(fp, nvl);
	fclose(fp);
	return (memfile_take(&mf, out_len));
}

DWORD
ReadAll(HANDLE h, void *buf, DWORD need)
{
	DWORD got = 0, total = 0;
	BYTE *p = (BYTE *)buf;
	while (total < need) {
		if (!ReadFile(h, p + total, need - total, &got, NULL))
			return (GetLastError());
		if (got == 0)
			return (ERROR_BROKEN_PIPE);
		total += got;
	}
	return (0);
}

DWORD
WriteAll(HANDLE h, const void *buf, DWORD len)
{
	DWORD put = 0, total = 0;
	const BYTE *p = (const BYTE *)buf;
	while (total < len) {
		if (!WriteFile(h, p + total, len - total, &put, NULL))
			return (GetLastError());
		total += put;
	}
	return (0);
}

void
RESP_LIBZFS_ERR(HANDLE client, const char *fallback)
{
	const char *d = libzfs_error_description(g_lzh);
	nvlist_t *e = fnvlist_alloc();
	fnvlist_add_boolean_value(e, "ok", B_FALSE);
	fnvlist_add_string(e, "err", d ? d : fallback);
	RESP_OK_NVL(client, e);
}
