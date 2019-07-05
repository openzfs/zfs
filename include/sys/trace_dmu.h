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

#if defined(_KERNEL)
#if defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_dmu

#if !defined(_TRACE_DMU_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_DMU_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     dmu_tx_t *, ...,
 *     uint64_t, ...,
 *     uint64_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_delay_mintime_class,
	TP_PROTO(dmu_tx_t *tx, uint64_t dirty, uint64_t min_tx_time),
	TP_ARGS(tx, dirty, min_tx_time),
	TP_STRUCT__entry(
	    __field(uint64_t,			tx_txg)
	    __field(uint64_t,			tx_lastsnap_txg)
	    __field(uint64_t,			tx_lasttried_txg)
	    __field(boolean_t,			tx_anyobj)
	    __field(boolean_t,			tx_dirty_delayed)
	    __field(hrtime_t,			tx_start)
	    __field(boolean_t,			tx_wait_dirty)
	    __field(int,			tx_err)
	    __field(uint64_t,			min_tx_time)
	    __field(uint64_t,			dirty)
	),
	TP_fast_assign(
	    __entry->tx_txg			= tx->tx_txg;
	    __entry->tx_lastsnap_txg		= tx->tx_lastsnap_txg;
	    __entry->tx_lasttried_txg		= tx->tx_lasttried_txg;
	    __entry->tx_anyobj			= tx->tx_anyobj;
	    __entry->tx_dirty_delayed		= tx->tx_dirty_delayed;
	    __entry->tx_start			= tx->tx_start;
	    __entry->tx_wait_dirty		= tx->tx_wait_dirty;
	    __entry->tx_err			= tx->tx_err;
	    __entry->dirty			= dirty;
	    __entry->min_tx_time		= min_tx_time;
	),
	TP_printk("tx { txg %llu lastsnap_txg %llu tx_lasttried_txg %llu "
	    "anyobj %d dirty_delayed %d start %llu wait_dirty %d err %i "
	    "} dirty %llu min_tx_time %llu",
	    __entry->tx_txg, __entry->tx_lastsnap_txg,
	    __entry->tx_lasttried_txg, __entry->tx_anyobj,
	    __entry->tx_dirty_delayed, __entry->tx_start,
	    __entry->tx_wait_dirty, __entry->tx_err,
	    __entry->dirty, __entry->min_tx_time)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_DELAY_MINTIME_EVENT(name) \
DEFINE_EVENT(zfs_delay_mintime_class, name, \
	TP_PROTO(dmu_tx_t *tx, uint64_t dirty, uint64_t min_tx_time), \
	TP_ARGS(tx, dirty, min_tx_time))
/* END CSTYLED */
DEFINE_DELAY_MINTIME_EVENT(zfs_delay__mintime);

/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_free_long_range_class,
	TP_PROTO(uint64_t long_free_dirty_all_txgs, uint64_t chunk_len, \
	    uint64_t txg),
	TP_ARGS(long_free_dirty_all_txgs, chunk_len, txg),
	TP_STRUCT__entry(
	    __field(uint64_t,			long_free_dirty_all_txgs)
	    __field(uint64_t,			chunk_len)
	    __field(uint64_t,			txg)
	),
	TP_fast_assign(
	    __entry->long_free_dirty_all_txgs	= long_free_dirty_all_txgs;
	    __entry->chunk_len					= chunk_len;
	    __entry->txg						= txg;
	),
	TP_printk("long_free_dirty_all_txgs %llu chunk_len %llu txg %llu",
	   __entry->long_free_dirty_all_txgs,
	   __entry->chunk_len, __entry->txg)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_FREE_LONG_RANGE_EVENT(name) \
DEFINE_EVENT(zfs_free_long_range_class, name, \
	TP_PROTO(uint64_t long_free_dirty_all_txgs, \
	    uint64_t chunk_len, uint64_t txg), \
	TP_ARGS(long_free_dirty_all_txgs, chunk_len, txg))
/* END CSTYLED */
DEFINE_FREE_LONG_RANGE_EVENT(zfs_free__long__range);

#endif /* _TRACE_DMU_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_dmu
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE3(delay__mintime);
DEFINE_DTRACE_PROBE3(free__long__range);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
