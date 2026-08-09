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

#include <sys/list.h>

#if defined(_KERNEL)
#if defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_zio

#if !defined(_TRACE_ZIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZIO_H

#include <linux/tracepoint.h>
#include <sys/types.h>
#include <sys/trace_common.h> /* For ZIO macros */

/* BEGIN CSTYLED */
TRACE_EVENT(zfs_zio__delay__miss,
	TP_PROTO(zio_t *zio, hrtime_t now),
	TP_ARGS(zio, now),
	TP_STRUCT__entry(
	    ZIO_TP_STRUCT_ENTRY
	    __field(hrtime_t, now)
	),
	TP_fast_assign(
	    ZIO_TP_FAST_ASSIGN
	    __entry->now = now;
	),
	TP_printk("now %llu " ZIO_TP_PRINTK_FMT, __entry->now,
	    ZIO_TP_PRINTK_ARGS)
);

TRACE_EVENT(zfs_zio__delay__hit,
	TP_PROTO(zio_t *zio, hrtime_t now, hrtime_t diff),
	TP_ARGS(zio, now, diff),
	TP_STRUCT__entry(
	    ZIO_TP_STRUCT_ENTRY
	    __field(hrtime_t, now)
	    __field(hrtime_t, diff)
	),
	TP_fast_assign(
	    ZIO_TP_FAST_ASSIGN
	    __entry->now = now;
	    __entry->diff = diff;
	),
	TP_printk("now %llu diff %llu " ZIO_TP_PRINTK_FMT, __entry->now,
	    __entry->diff, ZIO_TP_PRINTK_ARGS)
);

TRACE_EVENT(zfs_zio__delay__skip,
	TP_PROTO(zio_t *zio),
	TP_ARGS(zio),
	TP_STRUCT__entry(ZIO_TP_STRUCT_ENTRY),
	TP_fast_assign(ZIO_TP_FAST_ASSIGN),
	TP_printk(ZIO_TP_PRINTK_FMT, ZIO_TP_PRINTK_ARGS)
);
/* END CSTYLED */

#endif /* _TRACE_ZIO_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zio
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE2(zio__delay__miss);
DEFINE_DTRACE_PROBE3(zio__delay__hit);
DEFINE_DTRACE_PROBE1(zio__delay__skip);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
