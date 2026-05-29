// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _MOCK_DMU_H
#define	_MOCK_DMU_H

/*
 * In-memory mock of the core DMU types for unit testing.
 *
 * Provides mock_dnode_t carrying a flat array of fixed-size blocks.
 */

#include <sys/types.h>

typedef struct mock_dnode mock_dnode_t;
typedef struct mock_dmu_tx mock_dmu_tx_t;

/* Create a mock dnode with the given block size and object type. */
mock_dnode_t *mock_dnode_create(size_t blksize, dmu_object_type_t type);

/* Free a mock dnode and all its blocks. */
void mock_dnode_destroy(mock_dnode_t *mdn);

/* Returns the current number of blocks underlying this dnode. */
size_t mock_dnode_block_count(mock_dnode_t *mdn);

/* Returns a pointer to the data under the given block id. */
const void *mock_dnode_block_data(mock_dnode_t *mdn, uint64_t blkid);

/* Returns the current dnode ref (hold) count. */
uint64_t mock_dnode_refcount(mock_dnode_t *mdn);

/* Create/destroy a mock transaction handle. */
mock_dmu_tx_t *mock_tx_create(void);
void mock_tx_destroy(mock_dmu_tx_t *tx);

#endif /* _MOCK_DMU_H */
