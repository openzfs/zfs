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

#ifndef _SYS_SPACE_MAP_H
#define	_SYS_SPACE_MAP_H

#include <sys/avl.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct space_map_ops space_map_ops_t;

typedef struct space_map {
	avl_tree_t	sm_root;	/* AVL tree of map segments */
	uint64_t	sm_space;	/* sum of all segments in the map */
	uint64_t	sm_start;	/* start of map */
	uint64_t	sm_size;	/* size of map */
	uint8_t		sm_shift;	/* unit shift */
	uint8_t		sm_pad[3];	/* unused */
	uint8_t		sm_loaded;	/* map loaded? */
	uint8_t		sm_loading;	/* map loading? */
	kcondvar_t	sm_load_cv;	/* map load completion */
	space_map_ops_t	*sm_ops;	/* space map block picker ops vector */
	void		*sm_ppd;	/* picker-private data */
	kmutex_t	*sm_lock;	/* pointer to lock that protects map */
} space_map_t;

typedef struct space_seg {
	avl_node_t	ss_node;	/* AVL node */
	uint64_t	ss_start;	/* starting offset of this segment */
	uint64_t	ss_end;		/* ending offset (non-inclusive) */
} space_seg_t;

typedef struct space_ref {
	avl_node_t	sr_node;	/* AVL node */
	uint64_t	sr_offset;	/* offset (start or end) */
	int64_t		sr_refcnt;	/* associated reference count */
} space_ref_t;

typedef struct space_map_obj {
	uint64_t	smo_object;	/* on-disk space map object */
	uint64_t	smo_objsize;	/* size of the object */
	uint64_t	smo_alloc;	/* space allocated from the map */
} space_map_obj_t;

struct space_map_ops {
	void	(*smop_load)(space_map_t *sm);
	void	(*smop_unload)(space_map_t *sm);
	uint64_t (*smop_alloc)(space_map_t *sm, uint64_t size);
	void	(*smop_claim)(space_map_t *sm, uint64_t start, uint64_t size);
	void	(*smop_free)(space_map_t *sm, uint64_t start, uint64_t size);
};

/*
 * debug entry
 *
 *    1      3         10                     50
 *  ,---+--------+------------+---------------------------------.
 *  | 1 | action |  syncpass  |        txg (lower bits)         |
 *  `---+--------+------------+---------------------------------'
 *   63  62    60 59        50 49                               0
 *
 *
 *
 * non-debug entry
 *
 *    1               47                   1           15
 *  ,-----------------------------------------------------------.
 *  | 0 |   offset (sm_shift units)    | type |       run       |
 *  `-----------------------------------------------------------'
 *   63  62                          17   16   15               0
 */

/* All this stuff takes and returns bytes */
#define	SM_RUN_DECODE(x)	(BF64_DECODE(x, 0, 15) + 1)
#define	SM_RUN_ENCODE(x)	BF64_ENCODE((x) - 1, 0, 15)
#define	SM_TYPE_DECODE(x)	BF64_DECODE(x, 15, 1)
#define	SM_TYPE_ENCODE(x)	BF64_ENCODE(x, 15, 1)
#define	SM_OFFSET_DECODE(x)	BF64_DECODE(x, 16, 47)
#define	SM_OFFSET_ENCODE(x)	BF64_ENCODE(x, 16, 47)
#define	SM_DEBUG_DECODE(x)	BF64_DECODE(x, 63, 1)
#define	SM_DEBUG_ENCODE(x)	BF64_ENCODE(x, 63, 1)

#define	SM_DEBUG_ACTION_DECODE(x)	BF64_DECODE(x, 60, 3)
#define	SM_DEBUG_ACTION_ENCODE(x)	BF64_ENCODE(x, 60, 3)

#define	SM_DEBUG_SYNCPASS_DECODE(x)	BF64_DECODE(x, 50, 10)
#define	SM_DEBUG_SYNCPASS_ENCODE(x)	BF64_ENCODE(x, 50, 10)

#define	SM_DEBUG_TXG_DECODE(x)		BF64_DECODE(x, 0, 50)
#define	SM_DEBUG_TXG_ENCODE(x)		BF64_ENCODE(x, 0, 50)

#define	SM_RUN_MAX			SM_RUN_DECODE(~0ULL)

#define	SM_ALLOC	0x0
#define	SM_FREE		0x1

/*
 * The data for a given space map can be kept on blocks of any size.
 * Larger blocks entail fewer i/o operations, but they also cause the
 * DMU to keep more data in-core, and also to waste more i/o bandwidth
 * when only a few blocks have changed since the last transaction group.
 * This could use a lot more research, but for now, set the freelist
 * block size to 4k (2^12).
 */
#define	SPACE_MAP_BLOCKSHIFT	12

typedef void space_map_func_t(space_map_t *sm, uint64_t start, uint64_t size);

extern void space_map_create(space_map_t *sm, uint64_t start, uint64_t size,
    uint8_t shift, kmutex_t *lp);
extern void space_map_destroy(space_map_t *sm);
extern void space_map_add(space_map_t *sm, uint64_t start, uint64_t size);
extern void space_map_remove(space_map_t *sm, uint64_t start, uint64_t size);
extern boolean_t space_map_contains(space_map_t *sm,
    uint64_t start, uint64_t size);
extern void space_map_vacate(space_map_t *sm,
    space_map_func_t *func, space_map_t *mdest);
extern void space_map_walk(space_map_t *sm,
    space_map_func_t *func, space_map_t *mdest);

extern void space_map_load_wait(space_map_t *sm);
extern int space_map_load(space_map_t *sm, space_map_ops_t *ops,
    uint8_t maptype, space_map_obj_t *smo, objset_t *os);
extern void space_map_unload(space_map_t *sm);

extern uint64_t space_map_alloc(space_map_t *sm, uint64_t size);
extern void space_map_claim(space_map_t *sm, uint64_t start, uint64_t size);
extern void space_map_free(space_map_t *sm, uint64_t start, uint64_t size);

extern void space_map_sync(space_map_t *sm, uint8_t maptype,
    space_map_obj_t *smo, objset_t *os, dmu_tx_t *tx);
extern void space_map_truncate(space_map_obj_t *smo,
    objset_t *os, dmu_tx_t *tx);

extern void space_map_ref_create(avl_tree_t *t);
extern void space_map_ref_destroy(avl_tree_t *t);
extern void space_map_ref_add_seg(avl_tree_t *t,
    uint64_t start, uint64_t end, int64_t refcnt);
extern void space_map_ref_add_map(avl_tree_t *t,
    space_map_t *sm, int64_t refcnt);
extern void space_map_ref_generate_map(avl_tree_t *t,
    space_map_t *sm, int64_t minref);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPACE_MAP_H */
