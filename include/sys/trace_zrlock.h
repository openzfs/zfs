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

/* Do not include this file directly. Please use <sys/trace.h> instead. */
#ifndef _SYS_TRACE_ZRLOCK_INDIRECT
#error "trace_zrlock.h included directly"
#endif

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     zrlock_t *, ...,
 *     uint32_t, ...);
 */

#ifdef _SYS_ZRLOCK_H_END
#if !defined(_ZFS_ZRLOCK_CLASS) || defined(TRACE_HEADER_MULTI_READ)
#define	_ZFS_ZRLOCK_CLASS

DECLARE_EVENT_CLASS(zfs_zrlock_class,
	TP_PROTO(zrlock_t *zrl, uint32_t n),
	TP_ARGS(zrl, n),
	TP_STRUCT__entry(
	    __field(int32_t,		zr_refcount)
#ifdef	ZFS_DEBUG
	    __field(pid_t,		zr_owner_pid)
	    __field(const char *,	zr_caller)
#endif
	    __field(uint32_t,		n)
	),
	TP_fast_assign(
	    __entry->zr_refcount	= zrl->zr_refcount;
#ifdef	ZFS_DEBUG
	    __entry->zr_owner_pid	= zrl->zr_owner->pid;
	    __entry->zr_caller		= zrl->zr_caller;
#endif
	    __entry->n			= n;
	),
#ifdef	ZFS_DEBUG
	TP_printk("zrl { refcount %d owner_pid %d caller %s } n %u",
	    __entry->zr_refcount, __entry->zr_owner_pid, __entry->zr_caller,
	    __entry->n)
#else
	TP_printk("zrl { refcount %d } n %u",
	    __entry->zr_refcount, __entry->n)
#endif
);

#define	DEFINE_ZRLOCK_EVENT(name) \
DEFINE_EVENT(zfs_zrlock_class, name, \
	TP_PROTO(zrlock_t *zrl, uint32_t n), \
	TP_ARGS(zrl, n))
DEFINE_ZRLOCK_EVENT(zfs_zrlock__reentry);

#endif /* _ZFS_ZRLOCK_CLASS */
#endif /* _SYS_ZRLOCK_H_END */
