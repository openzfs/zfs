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
 * Copyright (c) 2014 by Prakash Surya. All rights reserved.
 */

#include <sys/spa.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>

/*
 * XXX: The following has all been temporarily copied here due to how
 * they are defined in *.c files. We need to delve into the internals of
 * certain structures, so we need the full definition of the structure.
 * To do this properly, we need to refactor the code such that we can
 * simply '#include' the appropriate files.
 */

typedef struct arc_callback arc_callback_t;
typedef struct arc_buf_hdr arc_buf_hdr_t;
typedef struct l2arc_buf_hdr l2arc_buf_hdr_t;
typedef struct l2arc_dev l2arc_dev_t;

struct l2arc_buf_hdr {
	/* protected by arc_buf_hdr  mutex */
	l2arc_dev_t		*b_dev;		/* L2ARC device */
	uint64_t		b_daddr;	/* disk address, offset byte */
	/* compression applied to buffer data */
	enum zio_compress	b_compress;
	/* real alloc'd buffer size depending on b_compress applied */
	uint32_t		b_hits;
	uint64_t		b_asize;
	/* temporary buffer holder for in-flight compressed data */
	void			*b_tmp_cdata;
};

typedef struct arc_state {
	list_t	arcs_list[ARC_BUFC_NUMTYPES];	/* list of evictable buffers */
	uint64_t arcs_lsize[ARC_BUFC_NUMTYPES];	/* amount of evictable data */
	uint64_t arcs_size;	/* total amount of data in this state */
	kmutex_t arcs_mtx;
	arc_state_type_t arcs_state;
} arc_state_t;

struct arc_callback {
	void			*acb_private;
	arc_done_func_t		*acb_done;
	arc_buf_t		*acb_buf;
	zio_t			*acb_zio_dummy;
	arc_callback_t		*acb_next;
};

struct arc_buf_hdr {
	/* protected by hash lock */
	dva_t			b_dva;
	uint64_t		b_birth;
	uint64_t		b_cksum0;

	kmutex_t		b_freeze_lock;
	zio_cksum_t		*b_freeze_cksum;

	arc_buf_hdr_t		*b_hash_next;
	arc_buf_t		*b_buf;
	uint32_t		b_flags;
	uint32_t		b_datacnt;

	arc_callback_t		*b_acb;
	kcondvar_t		b_cv;

	/* immutable */
	arc_buf_contents_t	b_type;
	uint64_t		b_size;
	uint64_t		b_spa;

	/* protected by arc state mutex */
	arc_state_t		*b_state;
	list_node_t		b_arc_node;

	/* updated atomically */
	clock_t			b_arc_access;
	uint32_t		b_mru_hits;
	uint32_t		b_mru_ghost_hits;
	uint32_t		b_mfu_hits;
	uint32_t		b_mfu_ghost_hits;
	uint32_t		b_l2_hits;

	/* self protecting */
	refcount_t		b_refcnt;

	l2arc_buf_hdr_t		*b_l2hdr;
	list_node_t		b_l2node;
};

#define CREATE_TRACE_POINTS
#include "trace.h"
