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

#if !defined(_KERNEL) || !defined(HAVE_DECLARE_EVENT_CLASS)

#define trace_zfs_arc_arc_hit(a)       ((void)0)
#define trace_zfs_arc_arc_evict(a)     ((void)0)
#define trace_zfs_arc_arc_delete(a)    ((void)0)
#define trace_zfs_arc_new_state_mru(a) ((void)0)
#define trace_zfs_arc_new_state_mfu(a) ((void)0)
#define trace_zfs_arc_l2arc_hit(a)     ((void)0)
#define trace_zfs_arc_l2arc_miss(a)    ((void)0)

#else

#undef TRACE_SYSTEM
#define TRACE_SYSTEM zfs

#if !defined(_TRACE_ZFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ZFS_H

#include <linux/tracepoint.h>

typedef struct arc_buf_hdr arc_buf_hdr_t;

DECLARE_EVENT_CLASS(zfs_arc_buf_hdr_class,
	TP_PROTO(arc_buf_hdr_t *ab),
	TP_ARGS(ab),
	TP_STRUCT__entry(
		__array(uint64_t, dva_word, 2)
		__field(uint64_t, birth)
		__field(uint64_t, cksum0)
		__field(uint32_t, flags)
		__field(uint32_t, datacnt)
		__field(uint64_t, type)
		__field(uint64_t, size)
		__field(uint64_t, spa)
		__field(uint64_t, state_type)
		__field(clock_t,  access)
		__field(uint32_t, mru_hits)
		__field(uint32_t, mru_ghost_hits)
		__field(uint32_t, mfu_hits)
		__field(uint32_t, mfu_ghost_hits)
		__field(uint32_t, l2_hits)
		__field(int32_t,  refcount)
	),
	TP_fast_assign(
		__entry->dva_word[0]    = ab->b_dva.dva_word[0];
		__entry->dva_word[1]    = ab->b_dva.dva_word[1];
		__entry->birth          = ab->b_birth;
		__entry->cksum0         = ab->b_cksum0;
		__entry->flags          = ab->b_flags;
		__entry->datacnt        = ab->b_datacnt;
		__entry->type           = ab->b_type;
		__entry->size           = ab->b_size;
		__entry->spa            = ab->b_spa;
		__entry->state_type     = ab->b_state->arcs_state;
		__entry->access         = ab->b_arc_access;
		__entry->mru_hits       = ab->b_mru_hits;
		__entry->mru_ghost_hits = ab->b_mru_ghost_hits;
		__entry->mfu_hits       = ab->b_mfu_hits;
		__entry->mfu_ghost_hits = ab->b_mfu_ghost_hits;
		__entry->l2_hits        = ab->b_l2_hits;
		__entry->refcount       = ab->b_refcnt.rc_count;
	),
	TP_printk("dva %llu:%llu birth %llu cksum0 0x%llx flags 0x%x "
		  "datacnt %u type %llu size %llu spa %llu state_type %llu "
		  "access %lu mru_hits %u mru_ghost_hits %u mfu_hits %u "
		  "mfu_ghost_hits %u l2_hits %u refcount %i",
		  __entry->dva_word[0], __entry->dva_word[1],
		  __entry->birth, __entry->cksum0, __entry->flags,
		  __entry->datacnt, __entry->type, __entry->size,
		  __entry->spa, __entry->state_type, __entry->access,
		  __entry->mru_hits, __entry->mru_ghost_hits,
		  __entry->mfu_hits, __entry->mfu_ghost_hits,
		  __entry->l2_hits, __entry->refcount)
);

#define DEFINE_ARC_BUF_HDR_EVENT(name) \
DEFINE_EVENT(zfs_arc_buf_hdr_class, name, \
	TP_PROTO(arc_buf_hdr_t *ab), \
	TP_ARGS(ab))
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_arc_hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_arc_evict);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_arc_delete);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_new_state_mru);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_new_state_mfu);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_l2arc_hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc_l2arc_miss);

#endif /* _TRACE_ZFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

#endif /* _KERNEL */
