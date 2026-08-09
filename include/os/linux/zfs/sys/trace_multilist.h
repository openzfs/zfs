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
#define	TRACE_SYSTEM_VAR zfs_multilist

#if !defined(_TRACE_MULTILIST_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_MULTILIST_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     multilist_t *, ...,
 *     unsigned int, ...,
 *     void *, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_multilist_insert_remove_class,
	TP_PROTO(multilist_t *ml, unsigned sublist_idx, void *obj),
	TP_ARGS(ml, sublist_idx, obj),
	TP_STRUCT__entry(
	    __field(size_t,		ml_offset)
	    __field(uint64_t,		ml_num_sublists)

	    __field(unsigned int,	sublist_idx)
	),
	TP_fast_assign(
	    __entry->ml_offset		= ml->ml_offset;
	    __entry->ml_num_sublists	= ml->ml_num_sublists;

	    __entry->sublist_idx	= sublist_idx;
	),
	TP_printk("ml { offset %ld numsublists %llu sublistidx %u } ",
	    __entry->ml_offset, __entry->ml_num_sublists, __entry->sublist_idx)
);
/* END CSTYLED */

#define	DEFINE_MULTILIST_INSERT_REMOVE_EVENT(name) \
DEFINE_EVENT(zfs_multilist_insert_remove_class, name, \
    TP_PROTO(multilist_t *ml, unsigned int sublist_idx, void *obj), \
    TP_ARGS(ml, sublist_idx, obj))
DEFINE_MULTILIST_INSERT_REMOVE_EVENT(zfs_multilist__insert);
DEFINE_MULTILIST_INSERT_REMOVE_EVENT(zfs_multilist__remove);

#endif /* _TRACE_MULTILIST_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_multilist
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE3(multilist__insert);
DEFINE_DTRACE_PROBE3(multilist__remove);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
