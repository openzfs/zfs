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
