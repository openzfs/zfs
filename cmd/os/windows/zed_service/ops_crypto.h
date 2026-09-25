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
#include <stdint.h>
#include <sys/nvpair.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build an nvlist result for mount preflight.
 * Pool-scope result shape:
 * {"ok":true, "scope":"pool", "pool":"tank", "locked":[{"name":"tank/
 *  secure", "keyformat":"passphrase", "keylocation":"prompt"}]}
 *
 * Dataset-scope result shape:
 * {"ok":true, "scope":"dataset", "dataset":"tank/fs", "ds_type":"filesystem",
 *  "encroot":"tank/secure", "locked":[ ... ]}
 * If dataset is not a filesystem: ds_type is "volume"/"snapshot"/"other"
 * and "locked" is empty.
 */
nvlist_t *zed_mount_preflight_pool_nvl(libzfs_handle_t *lzh,
    const char *pool_name);
nvlist_t *zed_mount_preflight_dataset_nvl(libzfs_handle_t *lzh,
    const char *dataset);

/*
 * Load a key for a single encryption root / dataset.
 * Returns nvlist: {"ok":true,"dataset":"..."} or
 * {"ok":false,"dataset":"...","err":"..."}.
 * `pass` may be NULL/0 (if your tree accepts non-interactive key sources).
 */
nvlist_t *zed_load_key_one_nvl(libzfs_handle_t *lzh,
    const char *dataset_utf8,
    const uint8_t *pass,
    uint32_t passlen);

#ifdef __cplusplus
}
#endif
