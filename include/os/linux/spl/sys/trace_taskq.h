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
#define	TRACE_SYSTEM_VAR zfs_taskq

#if !defined(_TRACE_TASKQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_TASKQ_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for single argument tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *     taskq_ent_t *, ...);
 */
DECLARE_EVENT_CLASS(zfs_taskq_ent_class,
    TP_PROTO(taskq_ent_t *taskq_ent),
    TP_ARGS(taskq_ent),
    TP_STRUCT__entry(__field(taskq_ent_t *, taskq_ent)),
    TP_fast_assign(
	__entry->taskq_ent = taskq_ent;
),
    TP_printk("taskq_ent %p", __entry->taskq_ent)
);

#define	DEFINE_TASKQ_EVENT(name) \
DEFINE_EVENT(zfs_taskq_ent_class, name, \
    TP_PROTO(taskq_ent_t *taskq_ent), \
    TP_ARGS(taskq_ent))
DEFINE_TASKQ_EVENT(zfs_taskq_ent__birth);
DEFINE_TASKQ_EVENT(zfs_taskq_ent__start);
DEFINE_TASKQ_EVENT(zfs_taskq_ent__finish);

#endif /* _TRACE_TASKQ_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_taskq
#include <trace/define_trace.h>

#else

/*
 * When tracepoints are not available, a DEFINE_DTRACE_PROBE* macro is
 * needed for each DTRACE_PROBE.  These will be used to generate stub
 * tracing functions and prototypes for those functions.  See
 * include/os/linux/spl/sys/trace.h.
 */

DEFINE_DTRACE_PROBE1(taskq_ent__birth);
DEFINE_DTRACE_PROBE1(taskq_ent__start);
DEFINE_DTRACE_PROBE1(taskq_ent__finish);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
