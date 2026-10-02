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
