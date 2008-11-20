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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_DMU_TRAVERSE_H
#define	_SYS_DMU_TRAVERSE_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/dnode.h>
#include <sys/arc.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ADVANCE_POST	0		/* post-order traversal */
#define	ADVANCE_PRE	0x01		/* pre-order traversal */
#define	ADVANCE_PRUNE	0x02		/* prune by prev snapshot birth time */
#define	ADVANCE_DATA	0x04		/* read user data blocks */
#define	ADVANCE_HOLES	0x08		/* visit holes */
#define	ADVANCE_ZIL	0x10		/* visit intent log blocks */
#define	ADVANCE_NOLOCK	0x20		/* Don't grab SPA sync lock */

#define	ZB_NO_LEVEL	-2
#define	ZB_MAXLEVEL	32		/* Next power of 2 >= DN_MAX_LEVELS */
#define	ZB_MAXBLKID	(1ULL << 62)
#define	ZB_MAXOBJSET	(1ULL << 62)
#define	ZB_MAXOBJECT	(1ULL << 62)

#define	ZB_MOS_CACHE	0
#define	ZB_MDN_CACHE	1
#define	ZB_DN_CACHE	2
#define	ZB_DEPTH	3

typedef struct zseg {
	uint64_t	seg_mintxg;
	uint64_t	seg_maxtxg;
	zbookmark_t	seg_start;
	zbookmark_t	seg_end;
	list_node_t	seg_node;
} zseg_t;

typedef struct traverse_blk_cache {
	zbookmark_t	bc_bookmark;
	blkptr_t	bc_blkptr;
	void		*bc_data;
	dnode_phys_t	*bc_dnode;
	int		bc_errno;
	int		bc_pad1;
	uint64_t	bc_pad2;
} traverse_blk_cache_t;

typedef int (blkptr_cb_t)(traverse_blk_cache_t *bc, spa_t *spa, void *arg);

struct traverse_handle {
	spa_t		*th_spa;
	blkptr_cb_t	*th_func;
	void		*th_arg;
	uint16_t	th_advance;
	uint16_t	th_locked;
	int		th_zio_flags;
	list_t		th_seglist;
	traverse_blk_cache_t th_cache[ZB_DEPTH][ZB_MAXLEVEL];
	traverse_blk_cache_t th_zil_cache;
	uint64_t	th_hits;
	uint64_t	th_arc_hits;
	uint64_t	th_reads;
	uint64_t	th_callbacks;
	uint64_t	th_syncs;
	uint64_t	th_restarts;
	zbookmark_t	th_noread;
	zbookmark_t	th_lastcb;
};

int traverse_dsl_dataset(struct dsl_dataset *ds, uint64_t txg_start,
    int advance, blkptr_cb_t func, void *arg);
int traverse_zvol(objset_t *os, int advance, blkptr_cb_t func, void *arg);

traverse_handle_t *traverse_init(spa_t *spa, blkptr_cb_t *func, void *arg,
    int advance, int zio_flags);
void traverse_fini(traverse_handle_t *th);

void traverse_add_dnode(traverse_handle_t *th,
    uint64_t mintxg, uint64_t maxtxg, uint64_t objset, uint64_t object);
void traverse_add_objset(traverse_handle_t *th,
    uint64_t mintxg, uint64_t maxtxg, uint64_t objset);
void traverse_add_pool(traverse_handle_t *th, uint64_t mintxg, uint64_t maxtxg);

int traverse_more(traverse_handle_t *th);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DMU_TRAVERSE_H */
