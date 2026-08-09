// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#if defined(_KERNEL)
#if defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_txg

#if !defined(_TRACE_TXG_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_TXG_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     dsl_pool_t *, ...,
 *     uint64_t, ...);
 */
/* BEGIN CSTYLED */
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
/* END CSTYLED */

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

#endif /* _TRACE_TXG_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_txg
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE2(dsl_pool_sync__done);
DEFINE_DTRACE_PROBE2(txg__quiescing);
DEFINE_DTRACE_PROBE2(txg__opened);
DEFINE_DTRACE_PROBE2(txg__syncing);
DEFINE_DTRACE_PROBE2(txg__synced);
DEFINE_DTRACE_PROBE2(txg__quiesced);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
