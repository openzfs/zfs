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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */
#ifndef _DMU_REDACT_H_
#define	_DMU_REDACT_H_

#include <sys/spa.h>
#include <sys/dsl_bookmark.h>

#define	REDACT_BLOCK_MAX_COUNT (1ULL << 48)

static inline uint64_t
redact_block_get_size(redact_block_phys_t *rbp)
{
	return (BF64_GET_SB((rbp)->rbp_size_count, 48, 16, SPA_MINBLOCKSHIFT,
	    0));
}

static inline void
redact_block_set_size(redact_block_phys_t *rbp, uint64_t size)
{
	BF64_SET_SB((rbp)->rbp_size_count, 48, 16, SPA_MINBLOCKSHIFT, 0, size);
}

static inline uint64_t
redact_block_get_count(redact_block_phys_t *rbp)
{
	return (BF64_GET_SB((rbp)->rbp_size_count, 0, 48, 0, 1));
}

static inline void
redact_block_set_count(redact_block_phys_t *rbp, uint64_t count)
{
	BF64_SET_SB((rbp)->rbp_size_count, 0, 48, 0, 1, count);
}

int dmu_redact_snap(const char *, nvlist_t *, const char *);
#endif /* _DMU_REDACT_H_ */
