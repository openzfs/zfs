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

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#if !defined(_TRACE_ZFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZFS_H

#include <linux/tracepoint.h>
#include <sys/types.h>
#include <sys/list.h>

/*
 * Redefine the DTRACE_PROBE* functions to use Linux tracepoints
 */
#undef DTRACE_PROBE1
#define	DTRACE_PROBE1(name, t1, arg1) \
	trace_zfs_##name((arg1))

#undef DTRACE_PROBE2
#define	DTRACE_PROBE2(name, t1, arg1, t2, arg2) \
	trace_zfs_##name((arg1), (arg2))

#undef DTRACE_PROBE3
#define	DTRACE_PROBE3(name, t1, arg1, t2, arg2, t3, arg3) \
	trace_zfs_##name((arg1), (arg2), (arg3))

#undef DTRACE_PROBE4
#define	DTRACE_PROBE4(name, t1, arg1, t2, arg2, t3, arg3, t4, arg4) \
	trace_zfs_##name((arg1), (arg2), (arg3), (arg4))

typedef struct arc_buf_hdr arc_buf_hdr_t;
typedef struct zio zio_t;
typedef struct vdev vdev_t;
typedef struct l2arc_write_callback l2arc_write_callback_t;
typedef struct blkptr blkptr_t;
typedef struct zbookmark_phys zbookmark_phys_t;
typedef struct l2arc_dev l2arc_dev_t;
typedef struct dmu_buf_impl dmu_buf_impl_t;
typedef struct dmu_tx dmu_tx_t;
typedef struct dnode dnode_t;
typedef struct dsl_pool dsl_pool_t;
typedef struct znode znode_t;
typedef struct zfs_ace_hdr zfs_ace_hdr_t;
typedef struct zilog zilog_t;
typedef struct zrlock zrlock_t;

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     arc_buf_hdr_t *, ...);
 */
DECLARE_EVENT_CLASS(zfs_arc_buf_hdr_class,
	TP_PROTO(arc_buf_hdr_t *ab),
	TP_ARGS(ab),
	TP_STRUCT__entry(
	    __array(uint64_t,		hdr_dva_word, 2)
	    __field(uint64_t,		hdr_birth)
	    __field(uint64_t,		hdr_cksum0)
	    __field(uint32_t,		hdr_flags)
	    __field(uint32_t,		hdr_datacnt)
	    __field(arc_buf_contents_t,	hdr_type)
	    __field(uint64_t,		hdr_size)
	    __field(uint64_t,		hdr_spa)
	    __field(arc_state_type_t,	hdr_state_type)
	    __field(clock_t,		hdr_access)
	    __field(uint32_t,		hdr_mru_hits)
	    __field(uint32_t,		hdr_mru_ghost_hits)
	    __field(uint32_t,		hdr_mfu_hits)
	    __field(uint32_t,		hdr_mfu_ghost_hits)
	    __field(uint32_t,		hdr_l2_hits)
	    __field(int64_t,		hdr_refcount)
	),
	TP_fast_assign(
	    __entry->hdr_dva_word[0]	= ab->b_dva.dva_word[0];
	    __entry->hdr_dva_word[1]	= ab->b_dva.dva_word[1];
	    __entry->hdr_birth		= ab->b_birth;
	    __entry->hdr_cksum0		= ab->b_cksum0;
	    __entry->hdr_flags		= ab->b_flags;
	    __entry->hdr_datacnt	= ab->b_datacnt;
	    __entry->hdr_type		= ab->b_type;
	    __entry->hdr_size		= ab->b_size;
	    __entry->hdr_spa		= ab->b_spa;
	    __entry->hdr_state_type	= ab->b_state->arcs_state;
	    __entry->hdr_access		= ab->b_arc_access;
	    __entry->hdr_mru_hits	= ab->b_mru_hits;
	    __entry->hdr_mru_ghost_hits	= ab->b_mru_ghost_hits;
	    __entry->hdr_mfu_hits	= ab->b_mfu_hits;
	    __entry->hdr_mfu_ghost_hits	= ab->b_mfu_ghost_hits;
	    __entry->hdr_l2_hits	= ab->b_l2_hits;
	    __entry->hdr_refcount	= ab->b_refcnt.rc_count;
	),
	TP_printk("hdr { dva 0x%llx:0x%llx birth %llu cksum0 0x%llx "
	    "flags 0x%x datacnt %u type %u size %llu spa %llu "
	    "state_type %u access %lu mru_hits %u mru_ghost_hits %u "
	    "mfu_hits %u mfu_ghost_hits %u l2_hits %u refcount %lli }",
	    __entry->hdr_dva_word[0], __entry->hdr_dva_word[1],
	    __entry->hdr_birth, __entry->hdr_cksum0, __entry->hdr_flags,
	    __entry->hdr_datacnt, __entry->hdr_type, __entry->hdr_size,
	    __entry->hdr_spa, __entry->hdr_state_type,
	    __entry->hdr_access, __entry->hdr_mru_hits,
	    __entry->hdr_mru_ghost_hits, __entry->hdr_mfu_hits,
	    __entry->hdr_mfu_ghost_hits, __entry->hdr_l2_hits,
	    __entry->hdr_refcount)
);

#define	DEFINE_ARC_BUF_HDR_EVENT(name) \
DEFINE_EVENT(zfs_arc_buf_hdr_class, name, \
	TP_PROTO(arc_buf_hdr_t *ab), \
	TP_ARGS(ab))
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__evict);
DEFINE_ARC_BUF_HDR_EVENT(zfs_arc__delete);
DEFINE_ARC_BUF_HDR_EVENT(zfs_new_state__mru);
DEFINE_ARC_BUF_HDR_EVENT(zfs_new_state__mfu);
DEFINE_ARC_BUF_HDR_EVENT(zfs_l2arc__hit);
DEFINE_ARC_BUF_HDR_EVENT(zfs_l2arc__miss);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     vdev_t *, ...,
 *     zio_t *, ...);
 */
#define	ZIO_TP_STRUCT_ENTRY						\
		__field(zio_type_t,		zio_type)		\
		__field(int,			zio_cmd)		\
		__field(zio_priority_t,		zio_priority)		\
		__field(uint64_t,		zio_size)		\
		__field(uint64_t,		zio_orig_size)		\
		__field(uint64_t,		zio_offset)		\
		__field(hrtime_t,		zio_timestamp)		\
		__field(hrtime_t,		zio_delta)		\
		__field(uint64_t,		zio_delay)		\
		__field(enum zio_flag,		zio_flags)		\
		__field(enum zio_stage,		zio_stage)		\
		__field(enum zio_stage,		zio_pipeline)		\
		__field(enum zio_flag,		zio_orig_flags)		\
		__field(enum zio_stage,		zio_orig_stage)		\
		__field(enum zio_stage,		zio_orig_pipeline)	\
		__field(uint8_t,		zio_reexecute)		\
		__field(uint64_t,		zio_txg)		\
		__field(int,			zio_error)		\
		__field(uint64_t,		zio_ena)		\
									\
		__field(enum zio_checksum,	zp_checksum)		\
		__field(enum zio_compress,	zp_compress)		\
		__field(dmu_object_type_t,	zp_type)		\
		__field(uint8_t,		zp_level)		\
		__field(uint8_t,		zp_copies)		\
		__field(boolean_t,		zp_dedup)		\
		__field(boolean_t,		zp_dedup_verify)	\
		__field(boolean_t,		zp_nopwrite)

#define	ZIO_TP_FAST_ASSIGN						    \
		__entry->zio_type		= zio->io_type;		    \
		__entry->zio_cmd		= zio->io_cmd;		    \
		__entry->zio_priority		= zio->io_priority;	    \
		__entry->zio_size		= zio->io_size;		    \
		__entry->zio_orig_size		= zio->io_orig_size;	    \
		__entry->zio_offset		= zio->io_offset;	    \
		__entry->zio_timestamp		= zio->io_timestamp;	    \
		__entry->zio_delta		= zio->io_delta;	    \
		__entry->zio_delay		= zio->io_delay;	    \
		__entry->zio_flags		= zio->io_flags;	    \
		__entry->zio_stage		= zio->io_stage;	    \
		__entry->zio_pipeline		= zio->io_pipeline;	    \
		__entry->zio_orig_flags		= zio->io_orig_flags;	    \
		__entry->zio_orig_stage		= zio->io_orig_stage;	    \
		__entry->zio_orig_pipeline	= zio->io_orig_pipeline;    \
		__entry->zio_reexecute		= zio->io_reexecute;	    \
		__entry->zio_txg		= zio->io_txg;		    \
		__entry->zio_error		= zio->io_error;	    \
		__entry->zio_ena		= zio->io_ena;		    \
									    \
		__entry->zp_checksum		= zio->io_prop.zp_checksum; \
		__entry->zp_compress		= zio->io_prop.zp_compress; \
		__entry->zp_type		= zio->io_prop.zp_type;	    \
		__entry->zp_level		= zio->io_prop.zp_level;    \
		__entry->zp_copies		= zio->io_prop.zp_copies;   \
		__entry->zp_dedup		= zio->io_prop.zp_dedup;    \
		__entry->zp_nopwrite		= zio->io_prop.zp_nopwrite; \
		__entry->zp_dedup_verify	= zio->io_prop.zp_dedup_verify;

#define	ZIO_TP_PRINTK_FMT						\
	"zio { type %u cmd %i prio %u size %llu orig_size %llu "	\
	"offset %llu timestamp %llu delta %llu delay %llu "		\
	"flags 0x%x stage 0x%x pipeline 0x%x orig_flags 0x%x "		\
	"orig_stage 0x%x orig_pipeline 0x%x reexecute %u "		\
	"txg %llu error %d ena %llu prop { checksum %u compress %u "	\
	"type %u level %u copies %u dedup %u dedup_verify %u nopwrite %u } }"

#define	ZIO_TP_PRINTK_ARGS						\
	__entry->zio_type, __entry->zio_cmd, __entry->zio_priority,	\
	__entry->zio_size, __entry->zio_orig_size, __entry->zio_offset,	\
	__entry->zio_timestamp, __entry->zio_delta, __entry->zio_delay,	\
	__entry->zio_flags, __entry->zio_stage, __entry->zio_pipeline,	\
	__entry->zio_orig_flags, __entry->zio_orig_stage,		\
	__entry->zio_orig_pipeline, __entry->zio_reexecute,		\
	__entry->zio_txg, __entry->zio_error, __entry->zio_ena,		\
	__entry->zp_checksum, __entry->zp_compress, __entry->zp_type,	\
	__entry->zp_level, __entry->zp_copies, __entry->zp_dedup,	\
	__entry->zp_dedup_verify, __entry->zp_nopwrite


DECLARE_EVENT_CLASS(zfs_l2arc_rw_class,
	TP_PROTO(vdev_t *vd, zio_t *zio),
	TP_ARGS(vd, zio),
	TP_STRUCT__entry(
	    __field(uint64_t,	vdev_id)
	    __field(uint64_t,	vdev_guid)
	    __field(uint64_t,	vdev_state)
	    ZIO_TP_STRUCT_ENTRY
	),
	TP_fast_assign(
	    __entry->vdev_id	= vd->vdev_id;
	    __entry->vdev_guid	= vd->vdev_guid;
	    __entry->vdev_state	= vd->vdev_state;
	    ZIO_TP_FAST_ASSIGN
	),
	TP_printk("vdev { id %llu guid %llu state %llu } "
	    ZIO_TP_PRINTK_FMT, __entry->vdev_id, __entry->vdev_guid,
	    __entry->vdev_state, ZIO_TP_PRINTK_ARGS)
);

#define	DEFINE_L2ARC_RW_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_rw_class, name, \
	TP_PROTO(vdev_t *vd, zio_t *zio), \
	TP_ARGS(vd, zio))
DEFINE_L2ARC_RW_EVENT(zfs_l2arc__read);
DEFINE_L2ARC_RW_EVENT(zfs_l2arc__write);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zio_t *, ...,
 *     l2arc_write_callback_t *, ...);
 */
DECLARE_EVENT_CLASS(zfs_l2arc_iodone_class,
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb),
	TP_ARGS(zio, cb),
	TP_STRUCT__entry(ZIO_TP_STRUCT_ENTRY),
	TP_fast_assign(ZIO_TP_FAST_ASSIGN),
	TP_printk(ZIO_TP_PRINTK_FMT, ZIO_TP_PRINTK_ARGS)
);

#define	DEFINE_L2ARC_IODONE_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_iodone_class, name, \
	TP_PROTO(zio_t *zio, l2arc_write_callback_t *cb), \
	TP_ARGS(zio, cb))
DEFINE_L2ARC_IODONE_EVENT(zfs_l2arc__iodone);

/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     arc_buf_hdr_t *, ...,
 *     const blkptr_t *,
 *     uint64_t,
 *     const zbookmark_phys_t *);
 */
DECLARE_EVENT_CLASS(zfs_arc_miss_class,
	TP_PROTO(arc_buf_hdr_t *hdr,
	    const blkptr_t *bp, uint64_t size, const zbookmark_phys_t *zb),
	TP_ARGS(hdr, bp, size, zb),
	TP_STRUCT__entry(
	    __array(uint64_t,		hdr_dva_word, 2)
	    __field(uint64_t,		hdr_birth)
	    __field(uint64_t,		hdr_cksum0)
	    __field(uint32_t,		hdr_flags)
	    __field(uint32_t,		hdr_datacnt)
	    __field(arc_buf_contents_t,	hdr_type)
	    __field(uint64_t,		hdr_size)
	    __field(uint64_t,		hdr_spa)
	    __field(arc_state_type_t,	hdr_state_type)
	    __field(clock_t,		hdr_access)
	    __field(uint32_t,		hdr_mru_hits)
	    __field(uint32_t,		hdr_mru_ghost_hits)
	    __field(uint32_t,		hdr_mfu_hits)
	    __field(uint32_t,		hdr_mfu_ghost_hits)
	    __field(uint32_t,		hdr_l2_hits)
	    __field(int64_t,		hdr_refcount)

	    __array(uint64_t,		bp_dva0, 2)
	    __array(uint64_t,		bp_dva1, 2)
	    __array(uint64_t,		bp_dva2, 2)
	    __array(uint64_t,		bp_cksum, 4)

	    __field(uint64_t,		bp_lsize)

	    __field(uint64_t,		zb_objset)
	    __field(uint64_t,		zb_object)
	    __field(int64_t,		zb_level)
	    __field(uint64_t,		zb_blkid)
	),
	TP_fast_assign(
	    __entry->hdr_dva_word[0]	= hdr->b_dva.dva_word[0];
	    __entry->hdr_dva_word[1]	= hdr->b_dva.dva_word[1];
	    __entry->hdr_birth		= hdr->b_birth;
	    __entry->hdr_cksum0		= hdr->b_cksum0;
	    __entry->hdr_flags		= hdr->b_flags;
	    __entry->hdr_datacnt	= hdr->b_datacnt;
	    __entry->hdr_type		= hdr->b_type;
	    __entry->hdr_size		= hdr->b_size;
	    __entry->hdr_spa		= hdr->b_spa;
	    __entry->hdr_state_type	= hdr->b_state->arcs_state;
	    __entry->hdr_access		= hdr->b_arc_access;
	    __entry->hdr_mru_hits	= hdr->b_mru_hits;
	    __entry->hdr_mru_ghost_hits	= hdr->b_mru_ghost_hits;
	    __entry->hdr_mfu_hits	= hdr->b_mfu_hits;
	    __entry->hdr_mfu_ghost_hits	= hdr->b_mfu_ghost_hits;
	    __entry->hdr_l2_hits	= hdr->b_l2_hits;
	    __entry->hdr_refcount	= hdr->b_refcnt.rc_count;

	    __entry->bp_dva0[0]		= bp->blk_dva[0].dva_word[0];
	    __entry->bp_dva0[1]		= bp->blk_dva[0].dva_word[1];
	    __entry->bp_dva1[0]		= bp->blk_dva[1].dva_word[0];
	    __entry->bp_dva1[1]		= bp->blk_dva[1].dva_word[1];
	    __entry->bp_dva2[0]		= bp->blk_dva[2].dva_word[0];
	    __entry->bp_dva2[1]		= bp->blk_dva[2].dva_word[1];
	    __entry->bp_cksum[0]	= bp->blk_cksum.zc_word[0];
	    __entry->bp_cksum[1]	= bp->blk_cksum.zc_word[1];
	    __entry->bp_cksum[2]	= bp->blk_cksum.zc_word[2];
	    __entry->bp_cksum[3]	= bp->blk_cksum.zc_word[3];

	    __entry->bp_lsize		= size;

	    __entry->zb_objset		= zb->zb_objset;
	    __entry->zb_object		= zb->zb_object;
	    __entry->zb_level		= zb->zb_level;
	    __entry->zb_blkid		= zb->zb_blkid;
	),
	TP_printk("hdr { dva 0x%llx:0x%llx birth %llu cksum0 0x%llx "
	    "flags 0x%x datacnt %u type %u size %llu spa %llu state_type %u "
	    "access %lu mru_hits %u mru_ghost_hits %u mfu_hits %u "
	    "mfu_ghost_hits %u l2_hits %u refcount %lli } "
	    "bp { dva0 0x%llx:0x%llx dva1 0x%llx:0x%llx dva2 "
	    "0x%llx:0x%llx cksum 0x%llx:0x%llx:0x%llx:0x%llx "
	    "lsize %llu } zb { objset %llu object %llu level %lli "
	    "blkid %llu }",
	    __entry->hdr_dva_word[0], __entry->hdr_dva_word[1],
	    __entry->hdr_birth, __entry->hdr_cksum0, __entry->hdr_flags,
	    __entry->hdr_datacnt, __entry->hdr_type, __entry->hdr_size,
	    __entry->hdr_spa, __entry->hdr_state_type, __entry->hdr_access,
	    __entry->hdr_mru_hits, __entry->hdr_mru_ghost_hits,
	    __entry->hdr_mfu_hits, __entry->hdr_mfu_ghost_hits,
	    __entry->hdr_l2_hits, __entry->hdr_refcount,
	    __entry->bp_dva0[0], __entry->bp_dva0[1],
	    __entry->bp_dva1[0], __entry->bp_dva1[1],
	    __entry->bp_dva2[0], __entry->bp_dva2[1],
	    __entry->bp_cksum[0], __entry->bp_cksum[1],
	    __entry->bp_cksum[2], __entry->bp_cksum[3],
	    __entry->bp_lsize, __entry->zb_objset, __entry->zb_object,
	    __entry->zb_level, __entry->zb_blkid)
);

#define	DEFINE_ARC_MISS_EVENT(name) \
DEFINE_EVENT(zfs_arc_miss_class, name, \
	TP_PROTO(arc_buf_hdr_t *hdr, \
	    const blkptr_t *bp, uint64_t size, const zbookmark_phys_t *zb), \
	TP_ARGS(hdr, bp, size, zb))
DEFINE_ARC_MISS_EVENT(zfs_arc__miss);

/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     l2arc_dev_t *, ...,
 *     list_t *, ...,
 *     uint64_t, ...,
 *     boolean_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_l2arc_evict_class,
	TP_PROTO(l2arc_dev_t *dev,
	    list_t *buflist, uint64_t taddr, boolean_t all),
	TP_ARGS(dev, buflist, taddr, all),
	TP_STRUCT__entry(
	    __field(uint64_t,		vdev_id)
	    __field(uint64_t,		vdev_guid)
	    __field(uint64_t,		vdev_state)

	    __field(uint64_t,		l2ad_hand)
	    __field(uint64_t,		l2ad_start)
	    __field(uint64_t,		l2ad_end)
	    __field(uint64_t,		l2ad_evict)
	    __field(boolean_t,		l2ad_first)
	    __field(boolean_t,		l2ad_writing)

	    __field(uint64_t,		taddr)
	    __field(boolean_t,		all)
	),
	TP_fast_assign(
	    __entry->vdev_id		= dev->l2ad_vdev->vdev_id;
	    __entry->vdev_guid		= dev->l2ad_vdev->vdev_guid;
	    __entry->vdev_state		= dev->l2ad_vdev->vdev_state;

	    __entry->l2ad_hand		= dev->l2ad_hand;
	    __entry->l2ad_start		= dev->l2ad_start;
	    __entry->l2ad_end		= dev->l2ad_end;
	    __entry->l2ad_evict		= dev->l2ad_evict;
	    __entry->l2ad_first		= dev->l2ad_first;
	    __entry->l2ad_writing	= dev->l2ad_writing;

	    __entry->taddr		= taddr;
	    __entry->all		= all;
	),
	TP_printk("l2ad { vdev { id %llu guid %llu state %llu } "
	    "hand %llu start %llu end %llu evict %llu "
	    "first %d writing %d } taddr %llu all %d",
	    __entry->vdev_id, __entry->vdev_guid, __entry->vdev_state,
	    __entry->l2ad_hand, __entry->l2ad_start,
	    __entry->l2ad_end, __entry->l2ad_evict,
	    __entry->l2ad_first, __entry->l2ad_writing,
	    __entry->taddr, __entry->all)
);

#define	DEFINE_L2ARC_EVICT_EVENT(name) \
DEFINE_EVENT(zfs_l2arc_evict_class, name, \
	TP_PROTO(l2arc_dev_t *dev, \
	    list_t *buflist, uint64_t taddr, boolean_t all), \
	TP_ARGS(dev, buflist, taddr, all))
DEFINE_L2ARC_EVICT_EVENT(zfs_l2arc__evict);

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     dmu_tx_t *, ...,
 *     uint64_t, ...,
 *     uint64_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_delay_mintime_class,
	TP_PROTO(dmu_tx_t *tx, uint64_t dirty, uint64_t min_tx_time),
	TP_ARGS(tx, dirty, min_tx_time),
	TP_STRUCT__entry(
	    __field(uint64_t,			tx_txg)
	    __field(uint64_t,			tx_lastsnap_txg)
	    __field(uint64_t,			tx_lasttried_txg)
	    __field(boolean_t,			tx_anyobj)
	    __field(boolean_t,			tx_waited)
	    __field(hrtime_t,			tx_start)
	    __field(boolean_t,			tx_wait_dirty)
	    __field(int,			tx_err)
#ifdef DEBUG_DMU_TX
	    __field(uint64_t,			tx_space_towrite)
	    __field(uint64_t,			tx_space_tofree)
	    __field(uint64_t,			tx_space_tooverwrite)
	    __field(uint64_t,			tx_space_tounref)
	    __field(int64_t,			tx_space_written)
	    __field(int64_t,			tx_space_freed)
#endif
	    __field(uint64_t,			min_tx_time)
	    __field(uint64_t,			dirty)
	),
	TP_fast_assign(
	    __entry->tx_txg			= tx->tx_txg;
	    __entry->tx_lastsnap_txg		= tx->tx_lastsnap_txg;
	    __entry->tx_lasttried_txg		= tx->tx_lasttried_txg;
	    __entry->tx_anyobj			= tx->tx_anyobj;
	    __entry->tx_waited			= tx->tx_waited;
	    __entry->tx_start			= tx->tx_start;
	    __entry->tx_wait_dirty		= tx->tx_wait_dirty;
	    __entry->tx_err			= tx->tx_err;
#ifdef DEBUG_DMU_TX
	    __entry->tx_space_towrite		= tx->tx_space_towrite;
	    __entry->tx_space_tofree		= tx->tx_space_tofree;
	    __entry->tx_space_tooverwrite	= tx->tx_space_tooverwrite;
	    __entry->tx_space_tounref		= tx->tx_space_tounref;
	    __entry->tx_space_written		= tx->tx_space_written.rc_count;
	    __entry->tx_space_freed		= tx->tx_space_freed.rc_count;
#endif
	    __entry->dirty			= dirty;
	    __entry->min_tx_time		= min_tx_time;
	),
	TP_printk("tx { txg %llu lastsnap_txg %llu tx_lasttried_txg %llu "
	    "anyobj %d waited %d start %llu wait_dirty %d err %i "
#ifdef DEBUG_DMU_TX
	    "space_towrite %llu space_tofree %llu space_tooverwrite %llu "
	    "space_tounref %llu space_written %lli space_freed %lli "
#endif
	    "} dirty %llu min_tx_time %llu",
	    __entry->tx_txg, __entry->tx_lastsnap_txg,
	    __entry->tx_lasttried_txg, __entry->tx_anyobj, __entry->tx_waited,
	    __entry->tx_start, __entry->tx_wait_dirty, __entry->tx_err,
#ifdef DEBUG_DMU_TX
	    __entry->tx_space_towrite, __entry->tx_space_tofree,
	    __entry->tx_space_tooverwrite, __entry->tx_space_tounref,
	    __entry->tx_space_written, __entry->tx_space_freed,
#endif
	    __entry->dirty, __entry->min_tx_time)
);

#define	DEFINE_DELAY_MINTIME_EVENT(name) \
DEFINE_EVENT(zfs_delay_mintime_class, name, \
	TP_PROTO(dmu_tx_t *tx, uint64_t dirty, uint64_t min_tx_time), \
	TP_ARGS(tx, dirty, min_tx_time))
DEFINE_DELAY_MINTIME_EVENT(zfs_delay__mintime);

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     dnode_t *, ...,
 *     int64_t, ...,
 *     uint32_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_dnode_move_class,
	TP_PROTO(dnode_t *dn, int64_t refcount, uint32_t dbufs),
	TP_ARGS(dn, refcount, dbufs),
	TP_STRUCT__entry(
	    __field(uint64_t,		dn_object)
	    __field(dmu_object_type_t,	dn_type)
	    __field(uint16_t,		dn_bonuslen)
	    __field(uint8_t,		dn_bonustype)
	    __field(uint8_t,		dn_nblkptr)
	    __field(uint8_t,		dn_checksum)
	    __field(uint8_t,		dn_compress)
	    __field(uint8_t,		dn_nlevels)
	    __field(uint8_t,		dn_indblkshift)
	    __field(uint8_t,		dn_datablkshift)
	    __field(uint8_t,		dn_moved)
	    __field(uint16_t,		dn_datablkszsec)
	    __field(uint32_t,		dn_datablksz)
	    __field(uint64_t,		dn_maxblkid)
	    __field(int64_t,		dn_tx_holds)
	    __field(int64_t,		dn_holds)
	    __field(boolean_t,		dn_have_spill)

	    __field(int64_t,		refcount)
	    __field(uint32_t,		dbufs)
	),
	TP_fast_assign(
	    __entry->dn_object		= dn->dn_object;
	    __entry->dn_type		= dn->dn_type;
	    __entry->dn_bonuslen	= dn->dn_bonuslen;
	    __entry->dn_bonustype	= dn->dn_bonustype;
	    __entry->dn_nblkptr		= dn->dn_nblkptr;
	    __entry->dn_checksum	= dn->dn_checksum;
	    __entry->dn_compress	= dn->dn_compress;
	    __entry->dn_nlevels		= dn->dn_nlevels;
	    __entry->dn_indblkshift	= dn->dn_indblkshift;
	    __entry->dn_datablkshift	= dn->dn_datablkshift;
	    __entry->dn_moved		= dn->dn_moved;
	    __entry->dn_datablkszsec	= dn->dn_datablkszsec;
	    __entry->dn_datablksz	= dn->dn_datablksz;
	    __entry->dn_maxblkid	= dn->dn_maxblkid;
	    __entry->dn_tx_holds	= dn->dn_tx_holds.rc_count;
	    __entry->dn_holds		= dn->dn_holds.rc_count;
	    __entry->dn_have_spill	= dn->dn_have_spill;

	    __entry->refcount		= refcount;
	    __entry->dbufs		= dbufs;
	),
	TP_printk("dn { object %llu type %d bonuslen %u bonustype %u "
	    "nblkptr %u checksum %u compress %u nlevels %u indblkshift %u "
	    "datablkshift %u moved %u datablkszsec %u datablksz %u "
	    "maxblkid %llu tx_holds %lli holds %lli have_spill %d } "
	    "refcount %lli dbufs %u",
	    __entry->dn_object, __entry->dn_type, __entry->dn_bonuslen,
	    __entry->dn_bonustype, __entry->dn_nblkptr, __entry->dn_checksum,
	    __entry->dn_compress, __entry->dn_nlevels, __entry->dn_indblkshift,
	    __entry->dn_datablkshift, __entry->dn_moved,
	    __entry->dn_datablkszsec, __entry->dn_datablksz,
	    __entry->dn_maxblkid, __entry->dn_tx_holds, __entry->dn_holds,
	    __entry->dn_have_spill, __entry->refcount, __entry->dbufs)
);

#define	DEFINE_DNODE_MOVE_EVENT(name) \
DEFINE_EVENT(zfs_dnode_move_class, name, \
	TP_PROTO(dnode_t *dn, int64_t refcount, uint32_t dbufs), \
	TP_ARGS(dn, refcount, dbufs))
DEFINE_DNODE_MOVE_EVENT(zfs_dnode__move);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     dsl_pool_t *, ...,
 *     uint64_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_txg_class,
	TP_PROTO(dsl_pool_t *dp, uint64_t txg),
	TP_ARGS(dp, txg),
	TP_STRUCT__entry(
	    __field(uint64_t, txg)
	),
	TP_fast_assign(
	    __entry->txg = txg;
	),
	TP_printk("txg %llu", __entry->txg)
);

#define	DEFINE_TXG_EVENT(name) \
DEFINE_EVENT(zfs_txg_class, name, \
	TP_PROTO(dsl_pool_t *dp, uint64_t txg), \
	TP_ARGS(dp, txg))
DEFINE_TXG_EVENT(zfs_dsl_pool_sync__done);
DEFINE_TXG_EVENT(zfs_txg__quiescing);
DEFINE_TXG_EVENT(zfs_txg__opened);
DEFINE_TXG_EVENT(zfs_txg__syncing);
DEFINE_TXG_EVENT(zfs_txg__synced);
DEFINE_TXG_EVENT(zfs_txg__quiesced);

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     znode_t *, ...,
 *     zfs_ace_hdr_t *, ...,
 *     uint32_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_ace_class,
	TP_PROTO(znode_t *zn, zfs_ace_hdr_t *ace, uint32_t mask_matched),
	TP_ARGS(zn, ace, mask_matched),
	TP_STRUCT__entry(
	    __field(uint64_t,		z_id)
	    __field(uint8_t,		z_unlinked)
	    __field(uint8_t,		z_atime_dirty)
	    __field(uint8_t,		z_zn_prefetch)
	    __field(uint8_t,		z_moved)
	    __field(uint_t,		z_blksz)
	    __field(uint_t,		z_seq)
	    __field(uint64_t,		z_mapcnt)
	    __field(uint64_t,		z_gen)
	    __field(uint64_t,		z_size)
	    __array(uint64_t,		z_atime, 2)
	    __field(uint64_t,		z_links)
	    __field(uint64_t,		z_pflags)
	    __field(uint64_t,		z_uid)
	    __field(uint64_t,		z_gid)
	    __field(uint32_t,		z_sync_cnt)
	    __field(mode_t,		z_mode)
	    __field(boolean_t,		z_is_sa)
	    __field(boolean_t,		z_is_zvol)
	    __field(boolean_t,		z_is_mapped)
	    __field(boolean_t,		z_is_ctldir)
	    __field(boolean_t,		z_is_stale)

	    __field(unsigned long,	i_ino)
	    __field(unsigned int,	i_nlink)
	    __field(u64,		i_version)
	    __field(loff_t,		i_size)
	    __field(unsigned int,	i_blkbits)
	    __field(unsigned short,	i_bytes)
	    __field(umode_t,		i_mode)
	    __field(__u32,		i_generation)

	    __field(uint16_t,		z_type)
	    __field(uint16_t,		z_flags)
	    __field(uint32_t,		z_access_mask)

	    __field(uint32_t,		mask_matched)
	),
	TP_fast_assign(
	    __entry->z_id		= zn->z_id;
	    __entry->z_unlinked		= zn->z_unlinked;
	    __entry->z_atime_dirty	= zn->z_atime_dirty;
	    __entry->z_zn_prefetch	= zn->z_zn_prefetch;
	    __entry->z_moved		= zn->z_moved;
	    __entry->z_blksz		= zn->z_blksz;
	    __entry->z_seq		= zn->z_seq;
	    __entry->z_mapcnt		= zn->z_mapcnt;
	    __entry->z_gen		= zn->z_gen;
	    __entry->z_size		= zn->z_size;
	    __entry->z_atime[0]		= zn->z_atime[0];
	    __entry->z_atime[1]		= zn->z_atime[1];
	    __entry->z_links		= zn->z_links;
	    __entry->z_pflags		= zn->z_pflags;
	    __entry->z_uid		= zn->z_uid;
	    __entry->z_gid		= zn->z_gid;
	    __entry->z_sync_cnt		= zn->z_sync_cnt;
	    __entry->z_mode		= zn->z_mode;
	    __entry->z_is_sa		= zn->z_is_sa;
	    __entry->z_is_zvol		= zn->z_is_zvol;
	    __entry->z_is_mapped	= zn->z_is_mapped;
	    __entry->z_is_ctldir	= zn->z_is_ctldir;
	    __entry->z_is_stale		= zn->z_is_stale;

	    __entry->i_ino		= zn->z_inode.i_ino;
	    __entry->i_nlink		= zn->z_inode.i_nlink;
	    __entry->i_version		= zn->z_inode.i_version;
	    __entry->i_size		= zn->z_inode.i_size;
	    __entry->i_blkbits		= zn->z_inode.i_blkbits;
	    __entry->i_bytes		= zn->z_inode.i_bytes;
	    __entry->i_mode		= zn->z_inode.i_mode;
	    __entry->i_generation	= zn->z_inode.i_generation;

	    __entry->z_type		= ace->z_type;
	    __entry->z_flags		= ace->z_flags;
	    __entry->z_access_mask	= ace->z_access_mask;

	    __entry->mask_matched	= mask_matched;
	),
	TP_printk("zn { id %llu unlinked %u atime_dirty %u "
	    "zn_prefetch %u moved %u blksz %u seq %u "
	    "mapcnt %llu gen %llu size %llu atime 0x%llx:0x%llx "
	    "links %llu pflags %llu uid %llu gid %llu "
	    "sync_cnt %u mode 0x%x is_sa %d is_zvol %d "
	    "is_mapped %d is_ctldir %d is_stale %d inode { "
	    "ino %lu nlink %u version %llu size %lli blkbits %u "
	    "bytes %u mode 0x%x generation %x } } ace { type %u "
	    "flags %u access_mask %u } mask_matched %u",
	    __entry->z_id, __entry->z_unlinked, __entry->z_atime_dirty,
	    __entry->z_zn_prefetch, __entry->z_moved, __entry->z_blksz,
	    __entry->z_seq, __entry->z_mapcnt, __entry->z_gen,
	    __entry->z_size, __entry->z_atime[0], __entry->z_atime[1],
	    __entry->z_links, __entry->z_pflags, __entry->z_uid,
	    __entry->z_gid, __entry->z_sync_cnt, __entry->z_mode,
	    __entry->z_is_sa, __entry->z_is_zvol, __entry->z_is_mapped,
	    __entry->z_is_ctldir, __entry->z_is_stale, __entry->i_ino,
	    __entry->i_nlink, __entry->i_version, __entry->i_size,
	    __entry->i_blkbits, __entry->i_bytes, __entry->i_mode,
	    __entry->i_generation, __entry->z_type, __entry->z_flags,
	    __entry->z_access_mask, __entry->mask_matched)
);

#define	DEFINE_ACE_EVENT(name) \
DEFINE_EVENT(zfs_ace_class, name, \
	TP_PROTO(znode_t *zn, zfs_ace_hdr_t *ace, uint32_t mask_matched), \
	TP_ARGS(zn, ace, mask_matched))
DEFINE_ACE_EVENT(zfs_zfs__ace__denies);
DEFINE_ACE_EVENT(zfs_zfs__ace__allows);

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     zilog_t *, ...);
 */
DECLARE_EVENT_CLASS(zfs_zil_class,
	TP_PROTO(zilog_t *zilog),
	TP_ARGS(zilog),
	TP_STRUCT__entry(
	    __field(uint64_t,	zl_lr_seq)
	    __field(uint64_t,	zl_commit_lr_seq)
	    __field(uint64_t,	zl_destroy_txg)
	    __field(uint64_t,	zl_replaying_seq)
	    __field(uint32_t,	zl_suspend)
	    __field(uint8_t,	zl_suspending)
	    __field(uint8_t,	zl_keep_first)
	    __field(uint8_t,	zl_replay)
	    __field(uint8_t,	zl_stop_sync)
	    __field(uint8_t,	zl_writer)
	    __field(uint8_t,	zl_logbias)
	    __field(uint8_t,	zl_sync)
	    __field(int,	zl_parse_error)
	    __field(uint64_t,	zl_parse_blk_seq)
	    __field(uint64_t,	zl_parse_lr_seq)
	    __field(uint64_t,	zl_parse_blk_count)
	    __field(uint64_t,	zl_parse_lr_count)
	    __field(uint64_t,	zl_next_batch)
	    __field(uint64_t,	zl_com_batch)
	    __field(uint64_t,	zl_itx_list_sz)
	    __field(uint64_t,	zl_cur_used)
	    __field(clock_t,	zl_replay_time)
	    __field(uint64_t,	zl_replay_blks)
	),
	TP_fast_assign(
	    __entry->zl_lr_seq		= zilog->zl_lr_seq;
	    __entry->zl_commit_lr_seq	= zilog->zl_commit_lr_seq;
	    __entry->zl_destroy_txg	= zilog->zl_destroy_txg;
	    __entry->zl_replaying_seq	= zilog->zl_replaying_seq;
	    __entry->zl_suspend		= zilog->zl_suspend;
	    __entry->zl_suspending	= zilog->zl_suspending;
	    __entry->zl_keep_first	= zilog->zl_keep_first;
	    __entry->zl_replay		= zilog->zl_replay;
	    __entry->zl_stop_sync	= zilog->zl_stop_sync;
	    __entry->zl_writer		= zilog->zl_writer;
	    __entry->zl_logbias		= zilog->zl_logbias;
	    __entry->zl_sync		= zilog->zl_sync;
	    __entry->zl_parse_error	= zilog->zl_parse_error;
	    __entry->zl_parse_blk_seq	= zilog->zl_parse_blk_seq;
	    __entry->zl_parse_lr_seq	= zilog->zl_parse_lr_seq;
	    __entry->zl_parse_blk_count	= zilog->zl_parse_blk_count;
	    __entry->zl_parse_lr_count	= zilog->zl_parse_lr_count;
	    __entry->zl_next_batch	= zilog->zl_next_batch;
	    __entry->zl_com_batch	= zilog->zl_com_batch;
	    __entry->zl_itx_list_sz	= zilog->zl_itx_list_sz;
	    __entry->zl_cur_used	= zilog->zl_cur_used;
	    __entry->zl_replay_time	= zilog->zl_replay_time;
	    __entry->zl_replay_blks	= zilog->zl_replay_blks;
	),
	TP_printk("zl { lr_seq %llu commit_lr_seq %llu destroy_txg %llu "
	    "replaying_seq %llu suspend %u suspending %u keep_first %u "
	    "replay %u stop_sync %u writer %u logbias %u sync %u "
	    "parse_error %u parse_blk_seq %llu parse_lr_seq %llu "
	    "parse_blk_count %llu parse_lr_count %llu next_batch %llu "
	    "com_batch %llu itx_list_sz %llu cur_used %llu replay_time %lu "
	    "replay_blks %llu }",
	    __entry->zl_lr_seq, __entry->zl_commit_lr_seq,
	    __entry->zl_destroy_txg, __entry->zl_replaying_seq,
	    __entry->zl_suspend, __entry->zl_suspending, __entry->zl_keep_first,
	    __entry->zl_replay, __entry->zl_stop_sync, __entry->zl_writer,
	    __entry->zl_logbias, __entry->zl_sync, __entry->zl_parse_error,
	    __entry->zl_parse_blk_seq, __entry->zl_parse_lr_seq,
	    __entry->zl_parse_blk_count, __entry->zl_parse_lr_count,
	    __entry->zl_next_batch, __entry->zl_com_batch,
	    __entry->zl_itx_list_sz, __entry->zl_cur_used,
	    __entry->zl_replay_time, __entry->zl_replay_blks)
);

#define	DEFINE_ZIL_EVENT(name) \
DEFINE_EVENT(zfs_zil_class, name, \
	TP_PROTO(zilog_t *zilog), \
	TP_ARGS(zilog))
DEFINE_ZIL_EVENT(zfs_zil__cw1);
DEFINE_ZIL_EVENT(zfs_zil__cw2);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     dmu_buf_impl_t *, ...,
 *     zio_t *, ...);
 */
#define	DBUF_TP_STRUCT_ENTRY					\
	__field(const char *,	os_spa)				\
	__field(uint64_t,	ds_object)			\
	__field(uint64_t,	db_object)			\
	__field(uint64_t,	db_level)			\
	__field(uint64_t,	db_blkid)			\
	__field(uint64_t,	db_offset)			\
	__field(uint64_t,	db_size)			\
	__field(uint64_t,	db_state)			\
	__field(int64_t,	db_holds)			\

#define	DBUF_TP_FAST_ASSIGN					\
	__entry->os_spa =					\
	    spa_name(DB_DNODE(db)->dn_objset->os_spa);		\
								\
	__entry->ds_object = db->db_objset->os_dsl_dataset ?	\
	    db->db_objset->os_dsl_dataset->ds_object : 0;	\
								\
	__entry->db_object = db->db.db_object;			\
	__entry->db_level  = db->db_level;			\
	__entry->db_blkid  = db->db_blkid;			\
	__entry->db_offset = db->db.db_offset;			\
	__entry->db_size   = db->db.db_size;			\
	__entry->db_state  = db->db_state;			\
	__entry->db_holds  = refcount_count(&db->db_holds);

#define	DBUF_TP_PRINTK_FMT						\
	"dbuf { spa \"%s\" objset %llu object %llu level %llu "		\
	"blkid %llu offset %llu size %llu state %llu holds %lld }"

#define	DBUF_TP_PRINTK_ARGS					\
	__entry->os_spa, __entry->ds_object,			\
	__entry->db_object, __entry->db_level,			\
	__entry->db_blkid, __entry->db_offset,			\
	__entry->db_size, __entry->db_state, __entry->db_holds

DECLARE_EVENT_CLASS(zfs_dbuf_class,
	TP_PROTO(dmu_buf_impl_t *db, zio_t *zio),
	TP_ARGS(db, zio),
	TP_STRUCT__entry(DBUF_TP_STRUCT_ENTRY),
	TP_fast_assign(DBUF_TP_FAST_ASSIGN),
	TP_printk(DBUF_TP_PRINTK_FMT, DBUF_TP_PRINTK_ARGS)
);

#define	DEFINE_DBUF_EVENT(name) \
DEFINE_EVENT(zfs_dbuf_class, name, \
	TP_PROTO(dmu_buf_impl_t *db, zio_t *zio), \
	TP_ARGS(db, zio))
DEFINE_DBUF_EVENT(zfs_blocked__read);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zrlock_t *, ...,
 *     uint32_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_zrlock_class,
	TP_PROTO(zrlock_t *zrl, uint32_t n),
	TP_ARGS(zrl, n),
	TP_STRUCT__entry(
	    __field(int32_t,		zr_refcount)
#ifdef	ZFS_DEBUG
	    __field(pid_t,		zr_owner_pid)
	    __field(const char *,	zr_caller)
#endif
	    __field(uint32_t,		n)
	),
	TP_fast_assign(
	    __entry->zr_refcount	= zrl->zr_refcount;
#ifdef	ZFS_DEBUG
	    __entry->zr_owner_pid	= zrl->zr_owner->pid;
	    __entry->zr_caller		= zrl->zr_caller;
#endif
	    __entry->n			= n;
	),
#ifdef	ZFS_DEBUG
	TP_printk("zrl { refcount %d owner_pid %d caller %s } n %u",
	    __entry->zr_refcount, __entry->zr_owner_pid, __entry->zr_caller,
	    __entry->n)
#else
	TP_printk("zrl { refcount %d } n %u",
	    __entry->zr_refcount, __entry->n)
#endif
);

#define	DEFINE_ZRLOCK_EVENT(name) \
DEFINE_EVENT(zfs_zrlock_class, name, \
	TP_PROTO(zrlock_t *zrl, uint32_t n), \
	TP_ARGS(zrl, n))
DEFINE_ZRLOCK_EVENT(zfs_zrlock__reentry);

/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     const char *, ...,
 *     const char *, ...,
 *     int, ...,
 *     uintptr_t, ...);
 */
DECLARE_EVENT_CLASS(zfs_set_error_class,
	TP_PROTO(const char *file, const char *function, int line,
	    uintptr_t error),
	TP_ARGS(file, function, line, error),
	TP_STRUCT__entry(
	    __field(const char *,	file)
	    __field(const char *,	function)
	    __field(int,		line)
	    __field(uintptr_t,		error)
	),
	TP_fast_assign(
	    __entry->file = strchr(file, '/') ? strrchr(file, '/') + 1 : file;
	    __entry->function		= function;
	    __entry->line		= line;
	    __entry->error		= error;
	),
	TP_printk("%s:%d:%s(): error 0x%lx", __entry->file, __entry->line,
	    __entry->function, __entry->error)
);

#define	DEFINE_SET_ERROR_EVENT(name) \
DEFINE_EVENT(zfs_set_error_class, name, \
	TP_PROTO(const char *file, const char *function, int line, \
	    uintptr_t error), \
	TP_ARGS(file, function, line, error))
DEFINE_SET_ERROR_EVENT(zfs_set__error);

/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     const char *, ...,
 *     const char *, ...,
 *     int, ...,
 *     const char *, ...);
 */
DECLARE_EVENT_CLASS(zfs_dprintf_class,
	TP_PROTO(const char *file, const char *function, int line,
	    const char *msg),
	TP_ARGS(file, function, line, msg),
	TP_STRUCT__entry(
	    __field(const char *,	file)
	    __field(const char *,	function)
	    __field(int,		line)
	    __string(msg, msg)
	),
	TP_fast_assign(
	    __entry->file		= file;
	    __entry->function		= function;
	    __entry->line		= line;
	    __assign_str(msg, msg);
	),
	TP_printk("%s:%d:%s(): %s", __entry->file, __entry->line,
	    __entry->function, __get_str(msg))
);

#define	DEFINE_DPRINTF_EVENT(name) \
DEFINE_EVENT(zfs_dprintf_class, name, \
	TP_PROTO(const char *file, const char *function, int line, \
	    const char *msg), \
	TP_ARGS(file, function, line, msg))
DEFINE_DPRINTF_EVENT(zfs_zfs__dprintf);

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     const char *, ...);
 */
DECLARE_EVENT_CLASS(zfs_dbgmsg_class,
	TP_PROTO(const char *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
	    __string(msg, msg)
	),
	TP_fast_assign(
	    __assign_str(msg, msg);
	),
	TP_printk("%s", __get_str(msg))
);

#define	DEFINE_DBGMSG_EVENT(name) \
DEFINE_EVENT(zfs_dbgmsg_class, name, \
	TP_PROTO(const char *msg), \
	TP_ARGS(msg))
DEFINE_DBGMSG_EVENT(zfs_zfs__dbgmsg);

#endif /* _TRACE_ZFS_H */

#undef TRACE_INCLUDE_PATH
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
