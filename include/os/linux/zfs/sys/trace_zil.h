/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#if defined(_KERNEL)
#if defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_zil

#if !defined(_TRACE_ZIL_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZIL_H

#include <linux/tracepoint.h>
#include <sys/types.h>

#define	ZILOG_TP_STRUCT_ENTRY						    \
		__field(uint64_t,	zl_lr_seq)			    \
		__field(uint64_t,	zl_commit_lr_seq)		    \
		__field(uint64_t,	zl_destroy_txg)			    \
		__field(uint64_t,	zl_replaying_seq)		    \
		__field(uint32_t,	zl_suspend)			    \
		__field(uint8_t,	zl_suspending)			    \
		__field(uint8_t,	zl_keep_first)			    \
		__field(uint8_t,	zl_replay)			    \
		__field(uint8_t,	zl_stop_sync)			    \
		__field(uint8_t,	zl_logbias)			    \
		__field(uint8_t,	zl_sync)			    \
		__field(int,		zl_parse_error)			    \
		__field(uint64_t,	zl_parse_blk_seq)		    \
		__field(uint64_t,	zl_parse_lr_seq)		    \
		__field(uint64_t,	zl_parse_blk_count)		    \
		__field(uint64_t,	zl_parse_lr_count)		    \
		__field(uint64_t,	zl_cur_used)			    \
		__field(clock_t,	zl_replay_time)			    \
		__field(uint64_t,	zl_replay_blks)

#define	ZILOG_TP_FAST_ASSIGN						    \
		__entry->zl_lr_seq		= zilog->zl_lr_seq;	    \
		__entry->zl_commit_lr_seq	= zilog->zl_commit_lr_seq;  \
		__entry->zl_destroy_txg	= zilog->zl_destroy_txg;	    \
		__entry->zl_replaying_seq	= zilog->zl_replaying_seq;  \
		__entry->zl_suspend		= zilog->zl_suspend;	    \
		__entry->zl_suspending	= zilog->zl_suspending;		    \
		__entry->zl_keep_first	= zilog->zl_keep_first;		    \
		__entry->zl_replay		= zilog->zl_replay;	    \
		__entry->zl_stop_sync	= zilog->zl_stop_sync;		    \
		__entry->zl_logbias		= zilog->zl_logbias;	    \
		__entry->zl_sync		= zilog->zl_sync;	    \
		__entry->zl_parse_error	= zilog->zl_parse_error;	    \
		__entry->zl_parse_blk_seq	= zilog->zl_parse_blk_seq;  \
		__entry->zl_parse_lr_seq	= zilog->zl_parse_lr_seq;   \
		__entry->zl_parse_blk_count	= zilog->zl_parse_blk_count;\
		__entry->zl_parse_lr_count	= zilog->zl_parse_lr_count; \
		__entry->zl_cur_used	= zilog->zl_cur_used;		    \
		__entry->zl_replay_time	= zilog->zl_replay_time;	    \
		__entry->zl_replay_blks	= zilog->zl_replay_blks;

#define	ZILOG_TP_PRINTK_FMT						    \
	"zl { lr_seq %llu commit_lr_seq %llu destroy_txg %llu "		    \
	"replaying_seq %llu suspend %u suspending %u keep_first %u "	    \
	"replay %u stop_sync %u logbias %u sync %u "			    \
	"parse_error %u parse_blk_seq %llu parse_lr_seq %llu "		    \
	"parse_blk_count %llu parse_lr_count %llu "			    \
	"cur_used %llu replay_time %lu replay_blks %llu }"

#define	ZILOG_TP_PRINTK_ARGS						    \
	    __entry->zl_lr_seq, __entry->zl_commit_lr_seq,		    \
	    __entry->zl_destroy_txg, __entry->zl_replaying_seq,		    \
	    __entry->zl_suspend, __entry->zl_suspending,		    \
	    __entry->zl_keep_first, __entry->zl_replay,			    \
	    __entry->zl_stop_sync, __entry->zl_logbias, __entry->zl_sync,   \
	    __entry->zl_parse_error, __entry->zl_parse_blk_seq,		    \
	    __entry->zl_parse_lr_seq, __entry->zl_parse_blk_count,	    \
	    __entry->zl_parse_lr_count, __entry->zl_cur_used,		    \
	    __entry->zl_replay_time, __entry->zl_replay_blks

#define	ITX_TP_STRUCT_ENTRY						    \
		__field(itx_wr_state_t,	itx_wr_state)			    \
		__field(uint8_t,	itx_sync)			    \
		__field(zil_callback_t,	itx_callback)			    \
		__field(void *,		itx_callback_data)		    \
		__field(uint64_t,	itx_oid)			    \
									    \
		__field(uint64_t,	lrc_txtype)			    \
		__field(uint64_t,	lrc_reclen)			    \
		__field(uint64_t,	lrc_txg)			    \
		__field(uint64_t,	lrc_seq)

#define	ITX_TP_FAST_ASSIGN						    \
		__entry->itx_wr_state		= itx->itx_wr_state;	    \
		__entry->itx_sync		= itx->itx_sync;	    \
		__entry->itx_callback		= itx->itx_callback;	    \
		__entry->itx_callback_data	= itx->itx_callback_data;   \
		__entry->itx_oid		= itx->itx_oid;		    \
									    \
		__entry->lrc_txtype		= itx->itx_lr.lrc_txtype;   \
		__entry->lrc_reclen		= itx->itx_lr.lrc_reclen;   \
		__entry->lrc_txg		= itx->itx_lr.lrc_txg;	    \
		__entry->lrc_seq		= itx->itx_lr.lrc_seq;

#define	ITX_TP_PRINTK_FMT						    \
	"itx { wr_state %u sync %u callback %p callback_data %p oid %llu"   \
	" { txtype %llu reclen %llu txg %llu seq %llu } }"

#define	ITX_TP_PRINTK_ARGS						    \
	    __entry->itx_wr_state, __entry->itx_sync, __entry->itx_callback,\
	    __entry->itx_callback_data, __entry->itx_oid,		    \
	    __entry->lrc_txtype, __entry->lrc_reclen, __entry->lrc_txg,	    \
	    __entry->lrc_seq

#define	ZCW_TP_STRUCT_ENTRY						    \
		__field(lwb_t *,	zcw_lwb)			    \
		__field(boolean_t,	zcw_done)			    \
		__field(int,		zcw_zio_error)			    \

#define	ZCW_TP_FAST_ASSIGN						    \
		__entry->zcw_lwb		= zcw->zcw_lwb;		    \
		__entry->zcw_done		= zcw->zcw_done;	    \
		__entry->zcw_zio_error		= zcw->zcw_zio_error;

#define	ZCW_TP_PRINTK_FMT						    \
	"zcw { lwb %p done %u error %u }"

#define	ZCW_TP_PRINTK_ARGS						    \
	    __entry->zcw_lwb, __entry->zcw_done, __entry->zcw_zio_error

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zilog_t *, ...,
 *     itx_t *, ...);
 */

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wordered-compare-function-pointers"
#endif
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_zil_process_itx_class,
	TP_PROTO(zilog_t *zilog, itx_t *itx),
	TP_ARGS(zilog, itx),
	TP_STRUCT__entry(
	    ZILOG_TP_STRUCT_ENTRY
	    ITX_TP_STRUCT_ENTRY
	),
	TP_fast_assign(
	    ZILOG_TP_FAST_ASSIGN
	    ITX_TP_FAST_ASSIGN
	),
	TP_printk(
	    ZILOG_TP_PRINTK_FMT " " ITX_TP_PRINTK_FMT,
	    ZILOG_TP_PRINTK_ARGS, ITX_TP_PRINTK_ARGS)
);
/* END CSTYLED */
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#define	DEFINE_ZIL_PROCESS_ITX_EVENT(name) \
DEFINE_EVENT(zfs_zil_process_itx_class, name, \
    TP_PROTO(zilog_t *zilog, itx_t *itx), \
    TP_ARGS(zilog, itx))
DEFINE_ZIL_PROCESS_ITX_EVENT(zfs_zil__process__commit__itx);
DEFINE_ZIL_PROCESS_ITX_EVENT(zfs_zil__process__normal__itx);

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zilog_t *, ...,
 *     zil_commit_waiter_t *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_zil_commit_io_error_class,
	TP_PROTO(zilog_t *zilog, zil_commit_waiter_t *zcw),
	TP_ARGS(zilog, zcw),
	TP_STRUCT__entry(
	    ZILOG_TP_STRUCT_ENTRY
	    ZCW_TP_STRUCT_ENTRY
	),
	TP_fast_assign(
	    ZILOG_TP_FAST_ASSIGN
	    ZCW_TP_FAST_ASSIGN
	),
	TP_printk(
	    ZILOG_TP_PRINTK_FMT " " ZCW_TP_PRINTK_FMT,
	    ZILOG_TP_PRINTK_ARGS, ZCW_TP_PRINTK_ARGS)
);

#define	DEFINE_ZIL_COMMIT_IO_ERROR_EVENT(name) \
DEFINE_EVENT(zfs_zil_commit_io_error_class, name, \
    TP_PROTO(zilog_t *zilog, zil_commit_waiter_t *zcw), \
    TP_ARGS(zilog, zcw))
DEFINE_ZIL_COMMIT_IO_ERROR_EVENT(zfs_zil__commit__io__error);

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     zilog_t *, ...,
 *     uint64_t, ...,
 *     uint64_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_zil_block_size_class,
	TP_PROTO(zilog_t *zilog, uint64_t res, uint64_t s1),
	TP_ARGS(zilog, res, s1),
	TP_STRUCT__entry(
	    ZILOG_TP_STRUCT_ENTRY
	    __field(uint64_t, res)
	    __field(uint64_t, s1)
	),
	TP_fast_assign(
	    ZILOG_TP_FAST_ASSIGN
	    __entry->res = res;
	    __entry->s1 = s1;
	),
	TP_printk(
	    ZILOG_TP_PRINTK_FMT " res %llu s1 %llu",
	    ZILOG_TP_PRINTK_ARGS, __entry->res, __entry->s1)
);

#define	DEFINE_ZIL_BLOCK_SIZE_EVENT(name) \
DEFINE_EVENT(zfs_zil_block_size_class, name, \
    TP_PROTO(zilog_t *zilog, uint64_t res, uint64_t s1), \
    TP_ARGS(zilog, res, s1))
DEFINE_ZIL_BLOCK_SIZE_EVENT(zfs_zil__block__size);

#endif /* _TRACE_ZIL_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zil
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE2(zil__process__commit__itx);
DEFINE_DTRACE_PROBE2(zil__process__normal__itx);
DEFINE_DTRACE_PROBE2(zil__commit__io__error);
DEFINE_DTRACE_PROBE3(zil__block__size);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
