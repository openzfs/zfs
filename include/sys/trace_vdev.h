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
 * Support for tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *      vdev_t *vd, ...,
 *	uint64_t mused, ...,
 *	uint64_t mlim, ...,
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_vdev_mused_mlim_class,
	TP_PROTO(vdev_t *vd, uint64_t mused, uint64_t mlim),
	TP_ARGS(vd, mused, mlim),
	TP_STRUCT__entry(
	    __field(uint64_t,	vdev_id)
	    __field(uint64_t,	vdev_guid)
	    __field(uint64_t,	mused)
	    __field(uint64_t,	mlim)
	),
	TP_fast_assign(
	    __entry->vdev_id		= vd->vdev_id;
	    __entry->vdev_guid		= vd->vdev_guid;
	    __entry->mused		= mused;
	    __entry->mlim		= mlim;
	),
	TP_printk("vd { vdev_id %llu vdev_guid %llu }"
	    " mused = %llu mlim = %llu",
	    __entry->vdev_id, __entry->vdev_guid,
	    __entry->mused, __entry->mlim)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define DEFINE_VDEV_MUSED_MLIM_EVENT(name) \
DEFINE_EVENT(zfs_vdev_mused_mlim_class, name, \
	TP_PROTO(vdev_t *vd, uint64_t mused, uint64_t mlim), \
	TP_ARGS(vd, mused, mlim))
/* END CSTYLED */
DEFINE_VDEV_MUSED_MLIM_EVENT(zfs_autotrim__mem__lim);

/*
 * Generic support for tracepoints of the form:
 *
 * DTRACE_PROBE1(...,
 *      metaslab_t *, ...,
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_msp_class,
	TP_PROTO(metaslab_t *msp),
	TP_ARGS(msp),
	TP_STRUCT__entry(
	    __field(uint64_t,	ms_id)
	    __field(uint64_t,	ms_start)
	    __field(uint64_t,	ms_size)
	    __field(uint64_t,	ms_fragmentation)
	),
	TP_fast_assign(
	    __entry->ms_id		= msp->ms_id;
	    __entry->ms_start		= msp->ms_start;
	    __entry->ms_size		= msp->ms_size;
	    __entry->ms_fragmentation	= msp->ms_fragmentation;
	),
	TP_printk("msp { ms_id %llu ms_start %llu ms_size %llu "
	    "ms_fragmentation %llu }",
	    __entry->ms_id, __entry->ms_start,
	    __entry->ms_size, __entry->ms_fragmentation)
);
/* END CSTYLED */

/* BEGIN CSTYLED */
#define	DEFINE_MSP_EVENT(name) \
DEFINE_EVENT(zfs_msp_class, name, \
	TP_PROTO(metaslab_t *msp), \
	TP_ARGS(msp))
/* END CSTYLED */
DEFINE_MSP_EVENT(zfs_preserve__spilled);
DEFINE_MSP_EVENT(zfs_drop__spilled);

#endif /* _TRACE_VDEV_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_vdev
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
