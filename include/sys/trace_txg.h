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
#ifndef _SYS_TRACE_TXG_INDIRECT
#error "trace_txg.h included directly"
#endif

/*
 * Generic support for two argument tracepoints of the form:
 *
 * DTRACE_PROBE2(...,
 *     dsl_pool_t *, ...,
 *     uint64_t, ...);
 */

#if defined(_SYS_DSL_POOL_H_END)
#if !defined(_ZFS_TXG_CLASS) || defined(TRACE_HEADER_MULTI_READ)
#define	_ZFS_TXG_CLASS

DECLARE_EVENT_CLASS(zfs_txg_class,
	TP_PROTO(dsl_pool_t *dp, uint64_t txg),
	TP_ARGS(dp, txg),
	TP_STRUCT__entry(
	    __field(uint64_t, txg)
	),
	TP_fast_assign(
	    __entry->txg = txg;
	),
	TP_printk("txg %llu", __entry->txg)
);

#define	DEFINE_TXG_EVENT(name) \
DEFINE_EVENT(zfs_txg_class, name, \
	TP_PROTO(dsl_pool_t *dp, uint64_t txg), \
	TP_ARGS(dp, txg))
DEFINE_TXG_EVENT(zfs_dsl_pool_sync__done);
DEFINE_TXG_EVENT(zfs_txg__quiescing);
DEFINE_TXG_EVENT(zfs_txg__opened);
DEFINE_TXG_EVENT(zfs_txg__syncing);
DEFINE_TXG_EVENT(zfs_txg__synced);
DEFINE_TXG_EVENT(zfs_txg__quiesced);

#endif /* _ZFS_TXG_CLASS */
#endif /* _SYS_DSL_POOL_H_END */
