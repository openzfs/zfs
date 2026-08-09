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
	/* cppcheck-suppress syntaxError */
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
	/* cppcheck-suppress syntaxError */
	BF64_SET_SB((rbp)->rbp_size_count, 0, 48, 0, 1, count);
}

int dmu_redact_snap(const char *, nvlist_t *, const char *);
#endif /* _DMU_REDACT_H_ */
