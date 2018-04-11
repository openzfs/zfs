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
 * Copyright (c) 2011, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc. All rights reserved.
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
	uint64_t (*msop_alloc)(metaslab_t *, uint64_t);
} metaslab_ops_t;


extern metaslab_ops_t *zfs_metaslab_ops;

int metaslab_init(metaslab_group_t *, uint64_t, uint64_t, uint64_t,
    metaslab_t **);
void metaslab_fini(metaslab_t *);

void metaslab_load_wait(metaslab_t *);
int metaslab_load(metaslab_t *);
void metaslab_unload(metaslab_t *);

void metaslab_sync(metaslab_t *, uint64_t);
void metaslab_sync_done(metaslab_t *, uint64_t);
void metaslab_sync_reassess(metaslab_group_t *);
uint64_t metaslab_block_maxsize(metaslab_t *);
void metaslab_auto_trim(metaslab_t *, uint64_t, boolean_t);
uint64_t metaslab_trim_mem_used(metaslab_t *);

#define	METASLAB_HINTBP_FAVOR		0x0
#define	METASLAB_HINTBP_AVOID		0x1
#define	METASLAB_GANG_HEADER		0x2
#define	METASLAB_GANG_CHILD		0x4
#define	METASLAB_ASYNC_ALLOC		0x8
#define	METASLAB_DONT_THROTTLE		0x10
#define	METASLAB_FASTWRITE		0x20

int metaslab_alloc(spa_t *, metaslab_class_t *, uint64_t,
    blkptr_t *, int, uint64_t, blkptr_t *, int, zio_alloc_list_t *, zio_t *);
void metaslab_free(spa_t *, const blkptr_t *, uint64_t, boolean_t);
int metaslab_claim(spa_t *, const blkptr_t *, uint64_t);
void metaslab_check_free(spa_t *, const blkptr_t *);
zio_t *metaslab_trim_all(metaslab_t *, uint64_t *, uint64_t *, boolean_t *);
void metaslab_fastwrite_mark(spa_t *, const blkptr_t *);
void metaslab_fastwrite_unmark(spa_t *, const blkptr_t *);

void metaslab_alloc_trace_init(void);
void metaslab_alloc_trace_fini(void);
void metaslab_trace_init(zio_alloc_list_t *);
void metaslab_trace_fini(zio_alloc_list_t *);

metaslab_class_t *metaslab_class_create(spa_t *, metaslab_ops_t *);
void metaslab_class_destroy(metaslab_class_t *);
int metaslab_class_validate(metaslab_class_t *);
void metaslab_class_histogram_verify(metaslab_class_t *);
uint64_t metaslab_class_fragmentation(metaslab_class_t *);
uint64_t metaslab_class_expandable_space(metaslab_class_t *);
boolean_t metaslab_class_throttle_reserve(metaslab_class_t *, int,
    zio_t *, int);
void metaslab_class_throttle_unreserve(metaslab_class_t *, int, zio_t *);

void metaslab_class_space_update(metaslab_class_t *, int64_t, int64_t,
    int64_t, int64_t);
uint64_t metaslab_class_get_alloc(metaslab_class_t *);
uint64_t metaslab_class_get_space(metaslab_class_t *);
uint64_t metaslab_class_get_dspace(metaslab_class_t *);
uint64_t metaslab_class_get_deferred(metaslab_class_t *);

metaslab_group_t *metaslab_group_create(metaslab_class_t *, vdev_t *);
void metaslab_group_destroy(metaslab_group_t *);
void metaslab_group_activate(metaslab_group_t *);
void metaslab_group_passivate(metaslab_group_t *);
boolean_t metaslab_group_initialized(metaslab_group_t *);
uint64_t metaslab_group_get_space(metaslab_group_t *);
void metaslab_group_histogram_verify(metaslab_group_t *);
uint64_t metaslab_group_fragmentation(metaslab_group_t *);
void metaslab_group_histogram_remove(metaslab_group_t *, metaslab_t *);
void metaslab_group_alloc_decrement(spa_t *, uint64_t, void *, int);
void metaslab_group_alloc_verify(spa_t *, const blkptr_t *, void *);

void metaslab_trimstats_create(spa_t *spa);
void metaslab_trimstats_destroy(spa_t *spa);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_METASLAB_H */
