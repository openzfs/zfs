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

/* Mount flags */
#define	ZMNT_READONLY 0x0001 /* dataset-level: try mount -o ro */

/* Unmount flags */
#define	ZUMNT_FORCE 0x0001 /* force unmount */

/*
 * JSON builders: return HeapAlloc'ed UTF-8 JSON; caller HeapFree()s.
 * Return shape examples:
 *   {"ok":true,"pool":"tank"}                      // pool ops
 *   {"ok":false,"pool":"tank","err":"<msg>"}       // pool ops err
 *   {"ok":true,"dataset":"tank/fs"}                // dataset ops
 *   {"ok":false,"dataset":"tank/fs","err":"<msg>"} // dataset err
 */

/* Pool-wide */
nvlist_t *
    zed_mount_pool_nvl(uint64_t pool_guid, const char *pool_name,
	const char *mntopts, uint32_t flags);

nvlist_t *
    zed_unmount_pool_nvl(uint64_t pool_guid, const char *pool_name,
	uint32_t flags);

nvlist_t *
    zed_mount_dataset_nvl(const char *dataset,
	const char *mntpoint_opt, /* NULL = default */
	uint32_t flags);

nvlist_t *
    zed_unmount_dataset_nvl(const char *dataset,
	uint32_t flags);


#ifdef __cplusplus
}
#endif
