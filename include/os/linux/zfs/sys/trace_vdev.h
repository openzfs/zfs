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

/*
 * If tracepoints are available define dtrace_probe events for vdev
 * related probes.  Definitions in include/os/linux/spl/sys/trace.h
 * will map DTRACE_PROBE* calls to tracepoints.
 */

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_vdev

#if !defined(_TRACE_VDEV_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_VDEV_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     spa_t *, ...,
 *     uint64_t, ...,
 *     uint64_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_removing_class_3,
	TP_PROTO(spa_t *spa, uint64_t offset, uint64_t size),
	TP_ARGS(spa, offset, size),
	TP_STRUCT__entry(
	    __field(spa_t *,	vdev_spa)
	    __field(uint64_t,	vdev_offset)
	    __field(uint64_t,	vdev_size)
	),
	TP_fast_assign(
	    __entry->vdev_spa	= spa;
	    __entry->vdev_offset = offset;
	    __entry->vdev_size	= size;
	),
	TP_printk("spa %p offset %llu size %llu",
	    __entry->vdev_spa, __entry->vdev_offset,
	    __entry->vdev_size)
);
/* END CSTYLED */

#define	DEFINE_REMOVE_FREE_EVENT(name) \
DEFINE_EVENT(zfs_removing_class_3, name, \
    TP_PROTO(spa_t *spa, uint64_t offset, uint64_t size), \
    TP_ARGS(spa, offset, size))
DEFINE_REMOVE_FREE_EVENT(zfs_remove__free__synced);
DEFINE_REMOVE_FREE_EVENT(zfs_remove__free__unvisited);

/*
 * Generic support for four argument tracepoints of the form:
 *
 * DTRACE_PROBE4(...,
 *     spa_t *, ...,
 *     uint64_t, ...,
 *     uint64_t, ...,
 *     uint64_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_removing_class_4,
	TP_PROTO(spa_t *spa, uint64_t offset, uint64_t size, uint64_t txg),
	TP_ARGS(spa, offset, size, txg),
	TP_STRUCT__entry(
	    __field(spa_t *,	vdev_spa)
	    __field(uint64_t,	vdev_offset)
	    __field(uint64_t,	vdev_size)
	    __field(uint64_t,	vdev_txg)
	),
	TP_fast_assign(
	    __entry->vdev_spa	= spa;
	    __entry->vdev_offset = offset;
	    __entry->vdev_size	= size;
	    __entry->vdev_txg	= txg;
	),
	TP_printk("spa %p offset %llu size %llu txg %llu",
	    __entry->vdev_spa, __entry->vdev_offset,
	    __entry->vdev_size, __entry->vdev_txg)
);

#define DEFINE_REMOVE_FREE_EVENT_TXG(name) \
DEFINE_EVENT(zfs_removing_class_4, name, \
    TP_PROTO(spa_t *spa, uint64_t offset, uint64_t size,uint64_t txg), \
    TP_ARGS(spa, offset, size, txg))
DEFINE_REMOVE_FREE_EVENT_TXG(zfs_remove__free__inflight);

#endif /* _TRACE_VDEV_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_vdev
#include <trace/define_trace.h>

#else

/*
 * When tracepoints are not available, a DEFINE_DTRACE_PROBE* macro is
 * needed for each DTRACE_PROBE.  These will be used to generate stub
 * tracing functions and prototypes for those functions.  See
 * include/os/linux/spl/sys/trace.h.
 */

DEFINE_DTRACE_PROBE3(remove__free__synced);
DEFINE_DTRACE_PROBE3(remove__free__unvisited);
DEFINE_DTRACE_PROBE4(remove__free__inflight);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
