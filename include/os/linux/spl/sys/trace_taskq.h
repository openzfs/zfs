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
