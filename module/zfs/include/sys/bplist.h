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

#ifndef	_SYS_BPLIST_H
#define	_SYS_BPLIST_H

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct bplist_phys {
	/*
	 * This is the bonus buffer for the dead lists.  The object's
	 * contents is an array of bpl_entries blkptr_t's, representing
	 * a total of bpl_bytes physical space.
	 */
	uint64_t	bpl_entries;
	uint64_t	bpl_bytes;
	uint64_t	bpl_comp;
	uint64_t	bpl_uncomp;
} bplist_phys_t;

#define	BPLIST_SIZE_V0	(2 * sizeof (uint64_t))

typedef struct bplist_q {
	blkptr_t	bpq_blk;
	void		*bpq_next;
} bplist_q_t;

typedef struct bplist {
	kmutex_t	bpl_lock;
	objset_t	*bpl_mos;
	uint64_t	bpl_object;
	uint8_t		bpl_blockshift;
	uint8_t		bpl_bpshift;
	uint8_t		bpl_havecomp;
	bplist_q_t	*bpl_queue;
	bplist_phys_t	*bpl_phys;
	dmu_buf_t	*bpl_dbuf;
	dmu_buf_t	*bpl_cached_dbuf;
} bplist_t;

extern uint64_t bplist_create(objset_t *mos, int blocksize, dmu_tx_t *tx);
extern void bplist_destroy(objset_t *mos, uint64_t object, dmu_tx_t *tx);
extern int bplist_open(bplist_t *bpl, objset_t *mos, uint64_t object);
extern void bplist_close(bplist_t *bpl);
extern boolean_t bplist_empty(bplist_t *bpl);
extern int bplist_iterate(bplist_t *bpl, uint64_t *itorp, blkptr_t *bp);
extern int bplist_enqueue(bplist_t *bpl, const blkptr_t *bp, dmu_tx_t *tx);
extern void bplist_enqueue_deferred(bplist_t *bpl, const blkptr_t *bp);
extern void bplist_sync(bplist_t *bpl, dmu_tx_t *tx);
extern void bplist_vacate(bplist_t *bpl, dmu_tx_t *tx);
extern int bplist_space(bplist_t *bpl,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
extern int bplist_space_birthrange(bplist_t *bpl,
    uint64_t mintxg, uint64_t maxtxg, uint64_t *dasizep);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BPLIST_H */
