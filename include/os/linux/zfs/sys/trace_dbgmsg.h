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
#define	TRACE_SYSTEM_VAR zfs_dbgmsg

#if !defined(_TRACE_DBGMSG_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_DBGMSG_H

#include <linux/tracepoint.h>

/*
 * This file defines tracepoint events for use by the dbgmsg(),
 * dprintf(), and SET_ERROR() interfaces. These are grouped here because
 * they all provide a way to store simple messages in the debug log (as
 * opposed to events used by the DTRACE_PROBE interfaces which typically
 * dump structured data).
 *
 * This header is included inside the trace.h multiple inclusion guard,
 * and it is guarded above against direct inclusion, so it and need not
 * be guarded separately.
 */

/*
 * Generic support for one argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     const char *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_dprintf_class,
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
/* END CSTYLED */

#define	DEFINE_DPRINTF_EVENT(name) \
DEFINE_EVENT(zfs_dprintf_class, name, \
    TP_PROTO(const char *msg), \
    TP_ARGS(msg))
DEFINE_DPRINTF_EVENT(zfs_zfs__dprintf);

#endif /* _TRACE_DBGMSG_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_dbgmsg
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE1(zfs__dprintf);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
