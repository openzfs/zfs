// SPDX-License-Identifier: CDDL-1.0
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
#define	TRACE_SYSTEM_VAR zfs_zrlock

#if !defined(_TRACE_ZRLOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZRLOCK_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zrlock_t *, ...,
 *     uint32_t, ...);
 */
/* BEGIN CSTYLED */
DECLARE_EVENT_CLASS(zfs_zrlock_class,
	TP_PROTO(zrlock_t *zrl, kthread_t *owner, uint32_t n),
	TP_ARGS(zrl, owner, n),
	TP_STRUCT__entry(
	    __field(int32_t,		refcount)
#ifdef	ZFS_DEBUG
	    __field(pid_t,		owner_pid)
	    __field(const char *,	caller)
#endif
	    __field(uint32_t,		n)
	),
	TP_fast_assign(
	    __entry->refcount	= zrl->zr_refcount;
#ifdef	ZFS_DEBUG
	    __entry->owner_pid	= owner ? owner->pid : 0;
	    __entry->caller = zrl->zr_caller ? zrl->zr_caller : "(null)";
#endif
	    __entry->n		= n;
	),
#ifdef	ZFS_DEBUG
	TP_printk("zrl { refcount %d owner_pid %d caller %s } n %u",
	    __entry->refcount, __entry->owner_pid, __entry->caller,
	    __entry->n)
#else
	TP_printk("zrl { refcount %d } n %u",
	    __entry->refcount, __entry->n)
#endif
);
/* END CSTYLED */

#define	DEFINE_ZRLOCK_EVENT(name) \
DEFINE_EVENT(zfs_zrlock_class, name, \
    TP_PROTO(zrlock_t *zrl, kthread_t *owner, uint32_t n), \
    TP_ARGS(zrl, owner, n))
DEFINE_ZRLOCK_EVENT(zfs_zrlock__reentry);

#endif /* _TRACE_ZRLOCK_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zrlock
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE3(zrlock__reentry);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
