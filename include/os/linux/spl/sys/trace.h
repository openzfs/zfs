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

/*
 * Calls to DTRACE_PROBE* are mapped to standard Linux kernel trace points
 * when they are available(when HAVE_DECLARE_EVENT_CLASS is defined).  The
 * tracepoint event class definitions are found in the general tracing
 * header file: include/sys/trace_*.h.  See include/sys/trace_vdev.h for
 * a good example.
 *
 * If tracepoints are not available, stub functions are generated which can
 * be traced using kprobes.  In this case, the DEFINE_DTRACE_PROBE* macros
 * are used to provide the stub functions and also the prototypes for
 * those functions.  The mechanism to do this relies on DEFINE_DTRACE_PROBE
 * macros defined in the general tracing headers(see trace_vdev.h) and
 * CREATE_TRACE_POINTS being defined only in module/zfs/trace.c.  When ZFS
 * source files include the general tracing headers, e.g.
 * module/zfs/vdev_removal.c including trace_vdev.h, DTRACE_PROBE calls
 * are mapped to stub functions calls and prototypes for those calls are
 * declared via DEFINE_DTRACE_PROBE*.  Only module/zfs/trace.c defines
 * CREATE_TRACE_POINTS.  That is followed by includes of all the general
 * tracing headers thereby defining all stub functions in one place via
 * the DEFINE_DTRACE_PROBE macros.
 *
 * When adding new DTRACE_PROBEs to zfs source, both a tracepoint event
 * class definition and a DEFINE_DTRACE_PROBE definition are needed to
 * avoid undefined function errors.
 */

#if defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#if !defined(_TRACE_ZFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZFS_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * DTRACE_PROBE with 0 arguments is not currently available with
 *  tracepoint events
 */
#define	DTRACE_PROBE(name) \
	((void)0)

#define	DTRACE_PROBE1(name, t1, arg1) \
	trace_zfs_##name((arg1))

#define	DTRACE_PROBE2(name, t1, arg1, t2, arg2) \
	trace_zfs_##name((arg1), (arg2))

#define	DTRACE_PROBE3(name, t1, arg1, t2, arg2, t3, arg3) \
	trace_zfs_##name((arg1), (arg2), (arg3))

#define	DTRACE_PROBE4(name, t1, arg1, t2, arg2, t3, arg3, t4, arg4) \
	trace_zfs_##name((arg1), (arg2), (arg3), (arg4))

#endif /* _TRACE_ZFS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>

#else /* HAVE_DECLARE_EVENT_CLASS */

#define	DTRACE_PROBE(name) \
	trace_zfs_##name()

#define	DTRACE_PROBE1(name, t1, arg1) \
	trace_zfs_##name((uintptr_t)(arg1))

#define	DTRACE_PROBE2(name, t1, arg1, t2, arg2) \
	trace_zfs_##name((uintptr_t)(arg1), (uintptr_t)(arg2))

#define	DTRACE_PROBE3(name, t1, arg1, t2, arg2, t3, arg3) \
	trace_zfs_##name((uintptr_t)(arg1), (uintptr_t)(arg2), \
	(uintptr_t)(arg3))

#define	DTRACE_PROBE4(name, t1, arg1, t2, arg2, t3, arg3, t4, arg4) \
	trace_zfs_##name((uintptr_t)(arg1), (uintptr_t)(arg2), \
	(uintptr_t)(arg3), (uintptr_t)(arg4))

#define	PROTO_DTRACE_PROBE(name)				\
	noinline void trace_zfs_##name(void)
#define	PROTO_DTRACE_PROBE1(name)				\
	noinline void trace_zfs_##name(uintptr_t)
#define	PROTO_DTRACE_PROBE2(name)				\
	noinline void trace_zfs_##name(uintptr_t, uintptr_t)
#define	PROTO_DTRACE_PROBE3(name)				\
	noinline void trace_zfs_##name(uintptr_t, uintptr_t,	\
	uintptr_t)
#define	PROTO_DTRACE_PROBE4(name)				\
	noinline void trace_zfs_##name(uintptr_t, uintptr_t,	\
	uintptr_t, uintptr_t)

#if defined(CREATE_TRACE_POINTS)

#define	FUNC_DTRACE_PROBE(name)					\
PROTO_DTRACE_PROBE(name);					\
noinline void trace_zfs_##name(void) { }			\
EXPORT_SYMBOL(trace_zfs_##name)

#define	FUNC_DTRACE_PROBE1(name)				\
PROTO_DTRACE_PROBE1(name);					\
noinline void trace_zfs_##name(uintptr_t arg1) { }		\
EXPORT_SYMBOL(trace_zfs_##name)

#define	FUNC_DTRACE_PROBE2(name)				\
PROTO_DTRACE_PROBE2(name);					\
noinline void trace_zfs_##name(uintptr_t arg1,			\
    uintptr_t arg2) { }						\
EXPORT_SYMBOL(trace_zfs_##name)

#define	FUNC_DTRACE_PROBE3(name)				\
PROTO_DTRACE_PROBE3(name);					\
noinline void trace_zfs_##name(uintptr_t arg1,			\
    uintptr_t arg2, uintptr_t arg3) { }				\
EXPORT_SYMBOL(trace_zfs_##name)

#define	FUNC_DTRACE_PROBE4(name)				\
PROTO_DTRACE_PROBE4(name);					\
noinline void trace_zfs_##name(uintptr_t arg1,			\
    uintptr_t arg2, uintptr_t arg3, uintptr_t arg4) { }		\
EXPORT_SYMBOL(trace_zfs_##name)

#undef	DEFINE_DTRACE_PROBE
#define	DEFINE_DTRACE_PROBE(name)	FUNC_DTRACE_PROBE(name)

#undef	DEFINE_DTRACE_PROBE1
#define	DEFINE_DTRACE_PROBE1(name)	FUNC_DTRACE_PROBE1(name)

#undef	DEFINE_DTRACE_PROBE2
#define	DEFINE_DTRACE_PROBE2(name)	FUNC_DTRACE_PROBE2(name)

#undef	DEFINE_DTRACE_PROBE3
#define	DEFINE_DTRACE_PROBE3(name)	FUNC_DTRACE_PROBE3(name)

#undef	DEFINE_DTRACE_PROBE4
#define	DEFINE_DTRACE_PROBE4(name)	FUNC_DTRACE_PROBE4(name)

#else /* CREATE_TRACE_POINTS */

#define	DEFINE_DTRACE_PROBE(name)	PROTO_DTRACE_PROBE(name)
#define	DEFINE_DTRACE_PROBE1(name)	PROTO_DTRACE_PROBE1(name)
#define	DEFINE_DTRACE_PROBE2(name)	PROTO_DTRACE_PROBE2(name)
#define	DEFINE_DTRACE_PROBE3(name)	PROTO_DTRACE_PROBE3(name)
#define	DEFINE_DTRACE_PROBE4(name)	PROTO_DTRACE_PROBE4(name)

#endif /* CREATE_TRACE_POINTS */
#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
