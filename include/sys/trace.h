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

#if !defined(_TRACE_ZFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZFS_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * The event classes in trace_dbgmsg.h have no ZFS header dependencies
 * so it is included within the _TRACE_ZFS_H guard.
 */
#define	_SYS_TRACE_DBGMSG_INDIRECT
#include <sys/trace_dbgmsg.h>
#undef _SYS_TRACE_DBGMSG_INDIRECT

/*
 * Redefine the DTRACE_PROBE* functions to use Linux tracepoints
 */
#undef DTRACE_PROBE1
#define	DTRACE_PROBE1(name, t1, arg1) \
	trace_zfs_##name((arg1))

#undef DTRACE_PROBE2
#define	DTRACE_PROBE2(name, t1, arg1, t2, arg2) \
	trace_zfs_##name((arg1), (arg2))

#undef DTRACE_PROBE3
#define	DTRACE_PROBE3(name, t1, arg1, t2, arg2, t3, arg3) \
	trace_zfs_##name((arg1), (arg2), (arg3))

#undef DTRACE_PROBE4
#define	DTRACE_PROBE4(name, t1, arg1, t2, arg2, t3, arg3, t4, arg4) \
	trace_zfs_##name((arg1), (arg2), (arg3), (arg4))

#endif /* _TRACE_ZFS_H */

/*
 * The following header files are intentionally included outside of the
 * _TRACE_ZFS_H multiple inclusion guard. This allows trace.h to be
 * included from zfs_context.h so that dprintf, dbgmsg, and SET_ERROR
 * are made readily available. But zfs_context.h is often included
 * before the other headers needed by the events declared below.
 * Therefore the headers below allow multiple inclusion, but with guards
 * against missing dependencies and multiple declaration around each
 * event class. Files using the events declared below may need to
 * explicitly include trace.h after including the required headers.
 */
#define	_SYS_TRACE_ACL_INDIRECT
#include <sys/trace_acl.h>
#undef _SYS_TRACE_ACL_INDIRECT

#define	_SYS_TRACE_ARC_INDIRECT
#include <sys/trace_arc.h>
#undef _SYS_TRACE_ARC_INDIRECT

#define	_SYS_TRACE_DBUF_INDIRECT
#include <sys/trace_dbuf.h>
#undef _SYS_TRACE_DBUF_INDIRECT

#define	_SYS_TRACE_DMU_INDIRECT
#include <sys/trace_dmu.h>
#undef _SYS_TRACE_DMU_INDIRECT

#define	_SYS_TRACE_DNODE_INDIRECT
#include <sys/trace_dnode.h>
#undef _SYS_TRACE_DNODE_INDIRECT

#define	_SYS_TRACE_TXG_INDIRECT
#include <sys/trace_txg.h>
#undef _SYS_TRACE_TXG_INDIRECT

#define	_SYS_TRACE_ZIL_INDIRECT
#include <sys/trace_zil.h>
#undef _SYS_TRACE_ZIL_INDIRECT

#define	_SYS_TRACE_ZRLOCK_INDIRECT
#include <sys/trace_zrlock.h>
#undef _SYS_TRACE_ZRLOCK_INDIRECT

#undef TRACE_INCLUDE_PATH
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
