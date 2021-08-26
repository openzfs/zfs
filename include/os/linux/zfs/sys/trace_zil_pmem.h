#if defined(_KERNEL)
#if defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#undef TRACE_SYSTEM_VAR
#define	TRACE_SYSTEM_VAR zfs_zil_pmem

#if !defined(_TRACE_ZIL_PMEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ZIL_PMEM_H

#include <linux/tracepoint.h>
#include <sys/types.h>

#include <sys/zil_pmem.h>
#include <sys/zil_pmem_prb.h>

TRACE_EVENT(zfs_zil_pmem_prb_write_entry__done,
	TP_PROTO(zilpmem_prb_t *prb, prb_write_stats_t *stats),
	TP_ARGS(prb, stats),
	TP_STRUCT__entry(
		__field(uint64_t 		, get_committer_slot_nanos)
		__field(uint64_t 		, put_committer_slot_nanos)
		__field(uint64_t 		, dt_sl_aquisition_nanos)
		__field(uint64_t 		, dt_sl_held_nanos)
		__field(uint64_t 		, pmem_nanos)
		__field(size_t 			, get_chunk_calls)
		__field(size_t 			, get_chunk_calls_sleeps)
		__field(size_t 			, obsolete)
		__field(size_t 			, beginning_new_gen)
		__field(size_t 			, committer_slot)
		// __field(prb_chunk_t 		, *entry_chunk)
		// __field(uint8_t 		, *entry_pmem_base)
	),
	TP_fast_assign(
	__entry->get_committer_slot_nanos = stats->get_committer_slot_nanos;
	__entry->put_committer_slot_nanos = stats->put_committer_slot_nanos;
	__entry->dt_sl_aquisition_nanos = stats->dt_sl_aquisition_nanos;
	__entry->dt_sl_held_nanos = stats->dt_sl_held_nanos;
	__entry->pmem_nanos = stats->pmem_nanos;
	__entry->get_chunk_calls = stats->get_chunk_calls;
	__entry->get_chunk_calls_sleeps = stats->get_chunk_calls_sleeps;
	__entry->obsolete = stats->obsolete;
	__entry->beginning_new_gen = stats->beginning_new_gen;
	__entry->committer_slot = stats->committer_slot;
	// __entry->*entry_chunk = stats->*entry_chunk;
	// __entry->*entry_pmem_base = stats->*entry_pmem_base;
	),
	TP_printk("prb_write_stats_t { %s }", "TODO")
);

#endif /* _TRACE_ZIL_PMEM_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_zil_pmem
#include <trace/define_trace.h>

#else

DEFINE_DTRACE_PROBE2(zil_pmem_prb_write_entry__done);

#endif /* HAVE_DECLARE_EVENT_CLASS */
#endif /* _KERNEL */
