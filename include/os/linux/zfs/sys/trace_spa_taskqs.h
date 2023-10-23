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
#define	TRACE_SYSTEM_VAR zfs_spa_taskqs

#if !defined(_TRACE_SPA_TASKQS_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_SPA_TASKQS_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     spa_taskqs_t *stqs, ...,
 *     taskq_ent_t *ent, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_spa_taskqs_ent_class,
	TP_PROTO(spa_taskqs_t *stqs, taskq_ent_t *ent),
	TP_ARGS(stqs, ent),
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_SPA_TASKQS_ENT_EVENT(name) \
DEFINE_EVENT(zfs_spa_taskqs_ent_class, name, \
	TP_PROTO(spa_taskqs_t *stqs, taskq_ent_t *ent), \
	TP_ARGS(stqs, ent))
/* END CSTYLED */
DEFINE_SPA_TASKQS_ENT_EVENT(zfs_spa_taskqs_ent__dispatch);
DEFINE_SPA_TASKQS_ENT_EVENT(zfs_spa_taskqs_ent__dispatched);

#endif /* _TRACE_SPA_TASKQS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_spa_taskqs
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE2(spa_taskqs_ent__dispatch);
DEFINE_DTRACE_PROBE2(spa_taskqs_ent__dispatched);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
