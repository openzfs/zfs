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

// JSON (UTF-8) builders. Return HeapAlloc'ed string and set *out_len.
// Caller must HeapFree() the returned buffer.

// { "candidates":[ {name,guid,state?,hostid?}, ... ] }
char *zed_import_scan_json(size_t *out_len);

// { "ok":true, "name":"...", "renamed":"..."} or { "ok":false, "err":"..."}
char *zed_import_one_json(uint32_t flags, uint64_t guid,
    const char *new_name_utf8, const char *altroot_utf8,
    size_t *out_len);

// { "imported":[...], "errors":[{"name":"...","err":"..."}] }
char *zed_import_all_json(uint32_t flags, const char *altroot_utf8,
    size_t *out_len);
