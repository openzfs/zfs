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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 */

#ifndef	_SYS_ARC_H
#define	_SYS_ARC_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/refcount.h>

typedef struct arc_buf_hdr arc_buf_hdr_t;
typedef struct arc_buf arc_buf_t;
typedef struct arc_prune arc_prune_t;
typedef void arc_done_func_t(zio_t *zio, arc_buf_t *buf, void *private);
typedef void arc_prune_func_t(int64_t bytes, void *private);
typedef int arc_evict_func_t(void *private);

/* generic arc_done_func_t's which you can use */
arc_done_func_t arc_bcopy_func;
arc_done_func_t arc_getbuf_func;

/* generic arc_prune_func_t wrapper for callbacks */
struct arc_prune {
	arc_prune_func_t	*p_pfunc;
	void			*p_private;
	list_node_t		p_node;
	refcount_t		p_refcnt;
};

struct arc_buf {
	arc_buf_hdr_t		*b_hdr;
	arc_buf_t		*b_next;
	kmutex_t		b_evict_lock;
	void			*b_data;
	arc_evict_func_t	*b_efunc;
	void			*b_private;
};

typedef enum arc_buf_contents {
	ARC_BUFC_DATA,				/* buffer contains data */
	ARC_BUFC_METADATA,			/* buffer contains metadata */
	ARC_BUFC_NUMTYPES
} arc_buf_contents_t;
/*
 * These are the flags we pass into calls to the arc
 */
#define	ARC_WAIT	(1 << 1)	/* perform I/O synchronously */
#define	ARC_NOWAIT	(1 << 2)	/* perform I/O asynchronously */
#define	ARC_PREFETCH	(1 << 3)	/* I/O is a prefetch */
#define	ARC_CACHED	(1 << 4)	/* I/O was already in cache */
#define	ARC_L2CACHE	(1 << 5)	/* cache in L2ARC */
#define	ARC_L2COMPRESS	(1 << 6)	/* compress in L2ARC */

/*
 * The following breakdows of arc_size exist for kstat only.
 */
typedef enum arc_space_type {
	ARC_SPACE_DATA,
	ARC_SPACE_META,
	ARC_SPACE_HDRS,
	ARC_SPACE_L2HDRS,
	ARC_SPACE_OTHER,
	ARC_SPACE_NUMTYPES
} arc_space_type_t;

typedef enum arc_state_type {
	ARC_STATE_ANON,
	ARC_STATE_MRU,
	ARC_STATE_MRU_GHOST,
	ARC_STATE_MFU,
	ARC_STATE_MFU_GHOST,
	ARC_STATE_L2C_ONLY,
	ARC_STATE_NUMTYPES
} arc_state_type_t;

typedef struct arc_buf_info {
	arc_state_type_t	abi_state_type;
	arc_buf_contents_t	abi_state_contents;
	uint64_t		abi_state_index;
	uint32_t		abi_flags;
	uint32_t		abi_datacnt;
	uint64_t		abi_size;
	uint64_t		abi_spa;
	uint64_t		abi_access;
	uint32_t		abi_mru_hits;
	uint32_t		abi_mru_ghost_hits;
	uint32_t		abi_mfu_hits;
	uint32_t		abi_mfu_ghost_hits;
	uint32_t		abi_l2arc_hits;
	uint32_t		abi_holds;
	uint64_t		abi_l2arc_dattr;
	uint64_t		abi_l2arc_asize;
	enum zio_compress	abi_l2arc_compress;
} arc_buf_info_t;

void arc_space_consume(uint64_t space, arc_space_type_t type);
void arc_space_return(uint64_t space, arc_space_type_t type);
arc_buf_t *arc_buf_alloc(spa_t *spa, uint64_t size, void *tag,
    arc_buf_contents_t type);
arc_buf_t *arc_loan_buf(spa_t *spa, uint64_t size);
void arc_return_buf(arc_buf_t *buf, void *tag);
void arc_loan_inuse_buf(arc_buf_t *buf, void *tag);
void arc_buf_add_ref(arc_buf_t *buf, void *tag);
boolean_t arc_buf_remove_ref(arc_buf_t *buf, void *tag);
void arc_buf_info(arc_buf_t *buf, arc_buf_info_t *abi, int state_index);
uint64_t arc_buf_size(arc_buf_t *buf);
void arc_release(arc_buf_t *buf, void *tag);
int arc_released(arc_buf_t *buf);
void arc_buf_sigsegv(int sig, siginfo_t *si, void *unused);
void arc_buf_freeze(arc_buf_t *buf);
void arc_buf_thaw(arc_buf_t *buf);
boolean_t arc_buf_eviction_needed(arc_buf_t *buf);
#ifdef ZFS_DEBUG
int arc_referenced(arc_buf_t *buf);
#endif

int arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    arc_done_func_t *done, void *private, zio_priority_t priority, int flags,
    uint32_t *arc_flags, const zbookmark_phys_t *zb);
zio_t *arc_write(zio_t *pio, spa_t *spa, uint64_t txg,
    blkptr_t *bp, arc_buf_t *buf, boolean_t l2arc, boolean_t l2arc_compress,
    const zio_prop_t *zp, arc_done_func_t *ready, arc_done_func_t *physdone,
    arc_done_func_t *done, void *private, zio_priority_t priority,
    int zio_flags, const zbookmark_phys_t *zb);

arc_prune_t *arc_add_prune_callback(arc_prune_func_t *func, void *private);
void arc_remove_prune_callback(arc_prune_t *p);
void arc_freed(spa_t *spa, const blkptr_t *bp);

void arc_set_callback(arc_buf_t *buf, arc_evict_func_t *func, void *private);
boolean_t arc_clear_callback(arc_buf_t *buf);

void arc_flush(spa_t *spa);
void arc_tempreserve_clear(uint64_t reserve);
int arc_tempreserve_space(uint64_t reserve, uint64_t txg);

void arc_init(void);
void arc_fini(void);

/*
 * Level 2 ARC
 */

void l2arc_add_vdev(spa_t *spa, vdev_t *vd);
void l2arc_remove_vdev(vdev_t *vd);
boolean_t l2arc_vdev_present(vdev_t *vd);
void l2arc_init(void);
void l2arc_fini(void);
void l2arc_start(void);
void l2arc_stop(void);

#ifndef _KERNEL
extern boolean_t arc_watch;
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ARC_H */
