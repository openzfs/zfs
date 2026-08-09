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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DMU_TRAVERSE_H
#define	_SYS_DMU_TRAVERSE_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dnode_phys;
struct dsl_dataset;
struct zilog;
struct arc_buf;

typedef int (blkptr_cb_t)(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const struct dnode_phys *dnp, void *arg);

#define	TRAVERSE_PRE			(1<<0)
#define	TRAVERSE_POST			(1<<1)
#define	TRAVERSE_PREFETCH_METADATA	(1<<2)
#define	TRAVERSE_PREFETCH_DATA		(1<<3)
#define	TRAVERSE_PREFETCH (TRAVERSE_PREFETCH_METADATA | TRAVERSE_PREFETCH_DATA)
#define	TRAVERSE_HARD			(1<<4)

/*
 * Encrypted dnode blocks have encrypted bonus buffers while the rest
 * of the dnode is left unencrypted. Callers can specify the
 * TRAVERSE_NO_DECRYPT flag to indicate to the traversal code that
 * they wish to receive the raw encrypted dnodes instead of attempting
 * to read the logical data.
 */
#define	TRAVERSE_NO_DECRYPT		(1<<5)

/*
 * Always use logical birth time for birth time comparisons.  This is useful
 * for operations that care about user data changes rather than physical
 * block rewrites (e.g., incremental replication).
 */
#define	TRAVERSE_LOGICAL		(1<<6)

/* Special traverse error return value to indicate skipping of children */
#define	TRAVERSE_VISIT_NO_CHILDREN	-1

int traverse_dataset(struct dsl_dataset *ds,
    uint64_t txg_start, int flags, blkptr_cb_t func, void *arg);
int traverse_dataset_resume(struct dsl_dataset *ds, uint64_t txg_start,
    zbookmark_phys_t *resume, int flags, blkptr_cb_t func, void *arg);
int traverse_dataset_destroyed(spa_t *spa, blkptr_t *blkptr,
    uint64_t txg_start, zbookmark_phys_t *resume, int flags,
    blkptr_cb_t func, void *arg);
int traverse_pool(spa_t *spa,
    uint64_t txg_start, int flags, blkptr_cb_t func, void *arg);

/*
 * Note that this calculation cannot overflow with the current maximum indirect
 * block size (128k).  If that maximum is increased to 1M, however, this
 * calculation can overflow, and handling would need to be added to ensure
 * continued correctness.
 */
static inline uint64_t
bp_span_in_blocks(uint8_t indblkshift, uint64_t level)
{
	unsigned int shift = level * (indblkshift - SPA_BLKPTRSHIFT);
	ASSERT3U(shift, <, 64);
	return (1ULL << shift);
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DMU_TRAVERSE_H */
