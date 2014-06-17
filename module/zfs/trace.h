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

#define trace_zfs_arc_arc_hit(a)        ((void)0)
#define trace_zfs_arc_arc_evict(a)      ((void)0)
#define trace_zfs_arc_arc_delete(a)     ((void)0)
#define trace_zfs_arc_new_state_mru(a)  ((void)0)
#define trace_zfs_arc_new_state_mfu(a)  ((void)0)
#define trace_zfs_arc_l2arc_hit(a)      ((void)0)
#define trace_zfs_arc_l2arc_miss(a)     ((void)0)
#define trace_zfs_arc_l2arc_read(a,b)   ((void)0)
#define trace_zfs_arc_l2arc_write(a,b)  ((void)0)
#define trace_zfs_arc_l2arc_iodone(a,b) ((void)0)

#else

#undef TRACE_SYSTEM
#define TRACE_SYSTEM zfs

#if !defined(_TRACE_ZFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ZFS_H

#include <linux/tracepoint.h>

typedef struct arc_buf_hdr arc_buf_hdr_t;
typedef struct zio zio_t;
typedef struct vdev vdev_t;
typedef struct l2arc_write_callback l2arc_write_callback_t;

DECLARE_EVENT_CLASS(zfs_arc_buf_hdr_class,
	TP_PROTO(arc_buf_hdr_t *ab),
	TP_ARGS(ab),
	TP_STRUCT__entry(
		__array(uint64_t,           dva_word, 2)
		__field(uint64_t,           birth)
		__field(uint64_t,           cksum0)
		__field(uint32_t,           flags)
		__field(uint32_t,           datacnt)
		__field(arc_buf_contents_t, type)
		__field(uint64_t,           size)
		__field(uint64_t,           spa)
		__field(arc_state_type_t,   state_type)
		__field(clock_t,            access)
		__field(uint32_t,           mru_hits)
		__field(uint32_t,           mru_ghost_hits)
		__field(uint32_t,           mfu_hits)
		__field(uint32_t,           mfu_ghost_hits)
		__field(uint32_t,           l2_hits)
		__field(int32_t,            refcount)
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
	TP_printk("hdr { dva 0x%llx:0x%llx birth %llu cksum0 0x%llx "
		  "flags 0x%x datacnt %u type %u size %llu spa %llu "
		  "state_type %u access %lu mru_hits %u mru_ghost_hits %u "
		  "mfu_hits %u mfu_ghost_hits %u l2_hits %u refcount %i }",
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

DECLARE_EVENT_CLASS(zfs_l2arc_rw_class,
	TP_PROTO(vdev_t *vd, zio_t *zio),
	TP_ARGS(vd, zio),
	TP_STRUCT__entry(
		__field(uint64_t,       vdev_id)
		__field(uint64_t,       vdev_guid)
		__field(uint64_t,       vdev_state)
		__field(zio_type_t,     io_type)
		__field(int,            io_cmd)
		__field(zio_priority_t, io_priority)
		__field(uint64_t,       io_size)
		__field(uint64_t,       io_orig_size)
		__field(uint64_t,       io_offset)
		__field(hrtime_t,       io_timestamp)
		__field(hrtime_t,       io_delta)
		__field(uint64_t,       io_delay)
		__field(enum zio_flag,  io_flags)
		__field(enum zio_stage, io_stage)
		__field(enum zio_stage, io_pipeline)
		__field(enum zio_flag,  io_orig_flags)
		__field(enum zio_stage, io_orig_stage)
		__field(enum zio_stage, io_orig_pipeline)
	),
	TP_fast_assign(
		__entry->vdev_id          = vd->vdev_id;
		__entry->vdev_guid        = vd->vdev_guid;
		__entry->vdev_state       = vd->vdev_state;
		__entry->io_type          = zio->io_type;
		__entry->io_cmd           = zio->io_cmd;
		__entry->io_priority      = zio->io_priority;
		__entry->io_size          = zio->io_size;
		__entry->io_orig_size     = zio->io_orig_size;
		__entry->io_offset        = zio->io_offset;
		__entry->io_timestamp     = zio->io_timestamp;
		__entry->io_delta         = zio->io_delta;
		__entry->io_delay         = zio->io_delay;
		__entry->io_flags         = zio->io_flags;
		__entry->io_stage         = zio->io_stage;
		__entry->io_pipeline      = zio->io_pipeline;
		__entry->io_orig_flags    = zio->io_orig_flags;
		__entry->io_orig_stage    = zio->io_orig_stage;
		__entry->io_orig_pipeline = zio->io_orig_pipeline;
	),
	TP_printk("vdev { id %llu guid %llu state %llu } zio { type %u "
		  "cmd %i prio %u size %llu orig_size %llu "
		  "offset %llu timestamp %llu delta %llu delay %llu "
		  "flags 0x%x stage 0x%x pipeline 0x%x orig_flags 0x%x "
		  "orig_stage 0x%x orig_pipeline 0x%x }",
		  __entry->vdev_id, __entry->vdev_guid, __entry->vdev_state,
		  __entry->io_type, __entry->io_cmd, __entry->io_priority,
		  __entry->io_size, __entry->io_orig_size, __entry->io_offset,
		  __entry->io_timestamp, __entry->io_delta, __entry->io_delay,
		  __entry->io_flags, __entry->io_stage, __entry->io_pipeline,
		  __entry->io_orig_flags, __entry->io_orig_stage,
		  __entry->io_orig_pipeline)
);

#define DEFINE_L2ARC_RW_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_rw_class, name, \
	TP_PROTO(vdev_t *vd, zio_t *zio), \
	TP_ARGS(vd, zio))
DEFINE_L2ARC_RW_EVENT(zfs_arc_l2arc_read);
DEFINE_L2ARC_RW_EVENT(zfs_arc_l2arc_write);

DECLARE_EVENT_CLASS(zfs_l2arc_iodone_class,
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb),
	TP_ARGS(zio, cb),
	TP_STRUCT__entry(
		__field(zio_type_t,     io_type)
		__field(int,            io_cmd)
		__field(zio_priority_t, io_priority)
		__field(uint64_t,       io_size)
		__field(uint64_t,       io_orig_size)
		__field(uint64_t,       io_offset)
		__field(hrtime_t,       io_timestamp)
		__field(hrtime_t,       io_delta)
		__field(uint64_t,       io_delay)
		__field(enum zio_flag,  io_flags)
		__field(enum zio_stage, io_stage)
		__field(enum zio_stage, io_pipeline)
		__field(enum zio_flag,  io_orig_flags)
		__field(enum zio_stage, io_orig_stage)
		__field(enum zio_stage, io_orig_pipeline)
	),
	TP_fast_assign(
		__entry->io_type          = zio->io_type;
		__entry->io_cmd           = zio->io_cmd;
		__entry->io_priority      = zio->io_priority;
		__entry->io_size          = zio->io_size;
		__entry->io_orig_size     = zio->io_orig_size;
		__entry->io_offset        = zio->io_offset;
		__entry->io_timestamp     = zio->io_timestamp;
		__entry->io_delta         = zio->io_delta;
		__entry->io_delay         = zio->io_delay;
		__entry->io_flags         = zio->io_flags;
		__entry->io_stage         = zio->io_stage;
		__entry->io_pipeline      = zio->io_pipeline;
		__entry->io_orig_flags    = zio->io_orig_flags;
		__entry->io_orig_stage    = zio->io_orig_stage;
		__entry->io_orig_pipeline = zio->io_orig_pipeline;
	),
	TP_printk("zio { type %u cmd %i prio %u size %llu orig_size %llu "
		  "offset %llu timestamp %llu delta %llu delay %llu "
		  "flags 0x%x stage 0x%x pipeline 0x%x orig_flags 0x%x "
		  "orig_stage 0x%x orig_pipeline 0x%x }",
		  __entry->io_type, __entry->io_cmd, __entry->io_priority,
		  __entry->io_size, __entry->io_orig_size, __entry->io_offset,
		  __entry->io_timestamp, __entry->io_delta, __entry->io_delay,
		  __entry->io_flags, __entry->io_stage, __entry->io_pipeline,
		  __entry->io_orig_flags, __entry->io_orig_stage,
		  __entry->io_orig_pipeline)
);

#define DEFINE_L2ARC_IODONE_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_iodone_class, name, \
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb), \
	TP_ARGS(zio, cb))
DEFINE_L2ARC_IODONE_EVENT(zfs_arc_l2arc_iodone);

#endif /* _TRACE_ZFS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

#endif /* _KERNEL */
