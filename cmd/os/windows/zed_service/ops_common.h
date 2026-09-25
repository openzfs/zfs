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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	RESP_OK_NVL(client, nvl) do { \
		size_t _len = 0; \
		char *_json = zed_json_from_nvlist((nvl), &_len); \
		if (_json) RESP_OK_JSON((client), _len, _json); \
		else RESP_ERR((client), ERROR_OUTOFMEMORY); \
	} while (0)

extern char *zed_json_from_nvlist(nvlist_t *nvl, size_t *out_len);
extern DWORD WriteAll(HANDLE h, const void *buf, DWORD len);
extern DWORD ReadAll(HANDLE h, void *buf, DWORD need);

extern void RESP_LIBZFS_ERR(HANDLE client, const char *fallback);


#ifdef __cplusplus
}
#endif
