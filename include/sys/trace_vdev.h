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

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_vdev

#if !defined(_TRACE_VDEV_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_VDEV_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *      vdev_t *, ...,
 *      metaslab_group_t *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_vdev_mg_class,
	TP_PROTO(vdev_t *vd, metaslab_group_t *mg),
	TP_ARGS(vd, mg),
	TP_STRUCT__entry(
	    __field(uint64_t,	vdev_id)
	    __field(uint64_t,	vdev_guid)
	    __field(boolean_t,	mg_allocatable)
	    __field(uint64_t,	mg_free_capacity)
	),
	TP_fast_assign(
	    __entry->vdev_id		= vd->vdev_id;
	    __entry->vdev_guid		= vd->vdev_guid;
	    __entry->mg_allocatable	= mg->mg_allocatable;
	    __entry->mg_free_capacity	= mg->mg_free_capacity;
	),
	TP_printk("vd { vdev_id %llu vdev_guid %llu }"
	    "mg { mg_allocatable %d mg_free_capacity %llu }",
	    __entry->vdev_id, __entry->vdev_guid,
	    __entry->mg_allocatable, __entry->mg_free_capacity)
);

/* BEGIN CSTYLED */
#define	DEFINE_VDEV_MG_EVENT(name) \
DEFINE_EVENT(zfs_vdev_mg_class, name, \
	TP_PROTO(vdev_t *vd, metaslab_group_t *mg), \
	TP_ARGS(vd, mg))
/* END CSTYLED */
DEFINE_VDEV_MG_EVENT(zfs_vdev_trim_all_restart);

#endif /* _TRACE_VDEV_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_vdev
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
