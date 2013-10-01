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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_METASLAB_H
#define	_SYS_METASLAB_H

#include <sys/spa.h>
#include <sys/space_map.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct metaslab_ops {
	uint64_t (*msop_alloc)(metaslab_t *msp, uint64_t size);
	boolean_t (*msop_fragmented)(metaslab_t *msp);
} metaslab_ops_t;

extern metaslab_ops_t *zfs_metaslab_ops;

metaslab_t *metaslab_init(metaslab_group_t *mg, uint64_t id,
    uint64_t object, uint64_t txg);
void metaslab_fini(metaslab_t *msp);

void metaslab_load_wait(metaslab_t *msp);
int metaslab_load(metaslab_t *msp);
void metaslab_unload(metaslab_t *msp);

void metaslab_sync(metaslab_t *msp, uint64_t txg);
void metaslab_sync_done(metaslab_t *msp, uint64_t txg);
void metaslab_sync_reassess(metaslab_group_t *mg);
uint64_t metaslab_block_maxsize(metaslab_t *msp);

#define	METASLAB_HINTBP_FAVOR	0x0
#define	METASLAB_HINTBP_AVOID	0x1
#define	METASLAB_GANG_HEADER	0x2
#define	METASLAB_GANG_CHILD	0x4
#define	METASLAB_GANG_AVOID	0x8
#define	METASLAB_FASTWRITE	0x10

int metaslab_alloc(spa_t *spa, metaslab_class_t *mc, uint64_t psize,
    blkptr_t *bp, int ncopies, uint64_t txg, blkptr_t *hintbp, int flags);
void metaslab_free(spa_t *spa, const blkptr_t *bp, uint64_t txg, boolean_t now);
int metaslab_claim(spa_t *spa, const blkptr_t *bp, uint64_t txg);
void metaslab_check_free(spa_t *spa, const blkptr_t *bp);
void metaslab_fastwrite_mark(spa_t *spa, const blkptr_t *bp);
void metaslab_fastwrite_unmark(spa_t *spa, const blkptr_t *bp);

metaslab_class_t *metaslab_class_create(spa_t *spa, metaslab_ops_t *ops);
void metaslab_class_destroy(metaslab_class_t *mc);
int metaslab_class_validate(metaslab_class_t *mc);

void metaslab_class_space_update(metaslab_class_t *mc,
    int64_t alloc_delta, int64_t defer_delta,
    int64_t space_delta, int64_t dspace_delta);
uint64_t metaslab_class_get_alloc(metaslab_class_t *mc);
uint64_t metaslab_class_get_space(metaslab_class_t *mc);
uint64_t metaslab_class_get_dspace(metaslab_class_t *mc);
uint64_t metaslab_class_get_deferred(metaslab_class_t *mc);

metaslab_group_t *metaslab_group_create(metaslab_class_t *mc, vdev_t *vd);
void metaslab_group_destroy(metaslab_group_t *mg);
void metaslab_group_activate(metaslab_group_t *mg);
void metaslab_group_passivate(metaslab_group_t *mg);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_METASLAB_H */
