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
/*
 * Copyright (c) 2014, 2016 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef KSTAT_OSX_INCLUDED
#define	KSTAT_OSX_INCLUDED

typedef struct osx_kstat {
	kstat_named_t spa_version;
	kstat_named_t zpl_version;

	kstat_named_t darwin_active_vnodes;
	kstat_named_t darwin_debug;
	kstat_named_t darwin_reclaim_nodes;
	kstat_named_t darwin_ignore_negatives;
	kstat_named_t darwin_ignore_positives;
	kstat_named_t darwin_create_negatives;
	kstat_named_t darwin_force_formd_normalized;
	kstat_named_t darwin_skip_unlinked_drain;
	kstat_named_t darwin_use_system_sync;

	kstat_named_t arc_zfs_arc_max;
	kstat_named_t arc_zfs_arc_min;
	kstat_named_t arc_zfs_arc_meta_limit;
	kstat_named_t arc_zfs_arc_meta_min;
	kstat_named_t arc_zfs_arc_grow_retry;
	kstat_named_t arc_zfs_arc_shrink_shift;
	kstat_named_t arc_zfs_arc_p_min_shift;
	kstat_named_t arc_zfs_arc_average_blocksize;

	kstat_named_t l2arc_write_max;
	kstat_named_t l2arc_write_boost;
	kstat_named_t l2arc_headroom;
	kstat_named_t l2arc_headroom_boost;
	kstat_named_t l2arc_feed_secs;
	kstat_named_t l2arc_feed_min_ms;

	kstat_named_t zfs_vdev_max_active;
	kstat_named_t zfs_vdev_sync_read_min_active;
	kstat_named_t zfs_vdev_sync_read_max_active;
	kstat_named_t zfs_vdev_sync_write_min_active;
	kstat_named_t zfs_vdev_sync_write_max_active;
	kstat_named_t zfs_vdev_async_read_min_active;
	kstat_named_t zfs_vdev_async_read_max_active;
	kstat_named_t zfs_vdev_async_write_min_active;
	kstat_named_t zfs_vdev_async_write_max_active;
	kstat_named_t zfs_vdev_scrub_min_active;
	kstat_named_t zfs_vdev_scrub_max_active;
	kstat_named_t zfs_vdev_async_write_active_min_dirty_percent;
	kstat_named_t zfs_vdev_async_write_active_max_dirty_percent;
	kstat_named_t zfs_vdev_aggregation_limit;
	kstat_named_t zfs_vdev_read_gap_limit;
	kstat_named_t zfs_vdev_write_gap_limit;

	kstat_named_t arc_lotsfree_percent;
	kstat_named_t zfs_dirty_data_max;
	kstat_named_t zfs_delay_max_ns;
	kstat_named_t zfs_delay_min_dirty_percent;
	kstat_named_t zfs_delay_scale;
	kstat_named_t spa_asize_inflation;
	kstat_named_t zfs_prefetch_disable;
	kstat_named_t zfetch_max_streams;
	kstat_named_t zfetch_min_sec_reap;
	kstat_named_t zfetch_array_rd_sz;
	kstat_named_t zfs_default_bs;
	kstat_named_t zfs_default_ibs;
	kstat_named_t metaslab_aliquot;
	kstat_named_t spa_max_replication_override;
	kstat_named_t spa_mode_global;
	kstat_named_t zfs_flags;
	kstat_named_t zfs_txg_timeout;
	kstat_named_t zfs_vdev_cache_max;
	kstat_named_t zfs_vdev_cache_size;
	kstat_named_t zfs_vdev_cache_bshift;
	kstat_named_t vdev_mirror_shift;
	kstat_named_t zfs_scrub_limit;
	kstat_named_t zfs_no_scrub_io;
	kstat_named_t zfs_no_scrub_prefetch;
	kstat_named_t fzap_default_block_shift;
	kstat_named_t zfs_immediate_write_sz;
//	kstat_named_t zfs_read_chunk_size;
	kstat_named_t zfs_nocacheflush;
	kstat_named_t zil_replay_disable;
	kstat_named_t metaslab_df_alloc_threshold;
	kstat_named_t metaslab_df_free_pct;
	kstat_named_t zio_injection_enabled;
	kstat_named_t zvol_immediate_write_sz;

	kstat_named_t l2arc_noprefetch;
	kstat_named_t l2arc_feed_again;
	kstat_named_t l2arc_norw;

	kstat_named_t zfs_recover;

	kstat_named_t zfs_free_bpobj_enabled;

	kstat_named_t zfs_send_corrupt_data;
	kstat_named_t zfs_send_queue_length;
	kstat_named_t zfs_recv_queue_length;

	kstat_named_t zvol_inhibit_dev;
	kstat_named_t zfs_send_set_freerecords_bit;

	kstat_named_t zfs_write_implies_delete_child;
	kstat_named_t zfs_send_holes_without_birth_time;

	kstat_named_t dbuf_cache_max_bytes;

	kstat_named_t zfs_vdev_queue_depth_pct;
	kstat_named_t zio_dva_throttle_enabled;

	kstat_named_t zfs_lua_max_instrlimit;
	kstat_named_t zfs_lua_max_memlimit;

	kstat_named_t zfs_trim_extent_bytes_max;
	kstat_named_t zfs_trim_extent_bytes_min;
	kstat_named_t zfs_trim_metaslab_skip;
	kstat_named_t zfs_trim_txg_batch;
	kstat_named_t zfs_trim_queue_limit;

	kstat_named_t zfs_send_unmodified_spill_blocks;
	kstat_named_t zfs_special_class_metadata_reserve_pct;

	kstat_named_t zfs_vdev_raidz_impl;
	kstat_named_t icp_gcm_impl;
	kstat_named_t icp_aes_impl;
	kstat_named_t zfs_fletcher_4_impl;

	kstat_named_t zfs_expire_snapshot;
	kstat_named_t zfs_admin_snapshot;
	kstat_named_t zfs_auto_snapshot;

	kstat_named_t zfs_spa_discard_memory_limit;
	kstat_named_t zfs_async_block_max_blocks;
	kstat_named_t zfs_initialize_chunk_size;
	kstat_named_t zfs_scan_suspend_progress;
	kstat_named_t zfs_removal_suspend_progress;
	kstat_named_t zfs_livelist_max_entries;

	kstat_named_t zfs_allow_redacted_dataset_mount;
	kstat_named_t zfs_checksum_events_per_second;
	kstat_named_t zfs_commit_timeout_pct;
	kstat_named_t zfs_compressed_arc_enabled;
	kstat_named_t zfs_condense_indirect_commit_entry_delay_ms;
	kstat_named_t zfs_condense_min_mapping_bytes;
	kstat_named_t zfs_deadman_checktime_ms;
	kstat_named_t zfs_deadman_failmode;
	kstat_named_t zfs_deadman_synctime_ms;
	kstat_named_t zfs_deadman_ziotime_ms;
	kstat_named_t zfs_disable_ivset_guid_check;
	kstat_named_t zfs_initialize_value;
	kstat_named_t zfs_keep_log_spacemaps_at_export;
	kstat_named_t l2arc_rebuild_blocks_min_l2size;
	kstat_named_t l2arc_rebuild_enabled;
	kstat_named_t l2arc_trim_ahead;
	kstat_named_t zfs_livelist_condense_new_alloc;
	kstat_named_t zfs_livelist_condense_sync_cancel;
	kstat_named_t zfs_livelist_condense_sync_pause;
	kstat_named_t zfs_livelist_condense_zthr_cancel;
	kstat_named_t zfs_livelist_condense_zthr_pause;
	kstat_named_t zfs_livelist_min_percent_shared;
	kstat_named_t zfs_max_dataset_nesting;
	kstat_named_t zfs_max_missing_tvds;
	kstat_named_t metaslab_debug_load;
	kstat_named_t metaslab_force_ganging;
	kstat_named_t zfs_multihost_fail_intervals;
	kstat_named_t zfs_multihost_import_intervals;
	kstat_named_t zfs_multihost_interval;
	kstat_named_t zfs_override_estimate_recordsize;
	kstat_named_t zfs_remove_max_segment;
	kstat_named_t zfs_resilver_min_time_ms;
	kstat_named_t zfs_scan_legacy;
	kstat_named_t zfs_scan_vdev_limit;
	kstat_named_t zfs_slow_io_events_per_second;
	kstat_named_t spa_load_verify_data;
	kstat_named_t spa_load_verify_metadata;
	kstat_named_t zfs_unlink_suspend_progress;
	kstat_named_t zfs_vdev_min_ms_count;
	kstat_named_t vdev_validate_skip;
	kstat_named_t zfs_zevent_len_max;
	kstat_named_t zio_slow_io_ms;
} osx_kstat_t;

extern unsigned int zfs_vnop_ignore_negatives;
extern unsigned int zfs_vnop_ignore_positives;
extern unsigned int zfs_vnop_create_negatives;
extern unsigned int zfs_vnop_skip_unlinked_drain;
extern uint64_t zfs_vfs_sync_paranoia;
extern uint64_t vnop_num_vnodes;
extern uint64_t vnop_num_reclaims;

extern unsigned long zfs_arc_max;
extern unsigned long zfs_arc_min;
extern unsigned long zfs_arc_meta_limit;
extern uint64_t zfs_arc_meta_min;
extern int zfs_arc_grow_retry;
extern int zfs_arc_shrink_shift;
extern int zfs_arc_p_min_shift;
extern int zfs_arc_average_blocksize;

extern uint64_t l2arc_write_max;
extern uint64_t l2arc_write_boost;
extern uint64_t l2arc_headroom;
extern uint64_t l2arc_headroom_boost;
extern uint64_t l2arc_feed_secs;
extern uint64_t l2arc_feed_min_ms;

extern uint32_t zfs_vdev_max_active;
extern uint32_t zfs_vdev_sync_read_min_active;
extern uint32_t zfs_vdev_sync_read_max_active;
extern uint32_t zfs_vdev_sync_write_min_active;
extern uint32_t zfs_vdev_sync_write_max_active;
extern uint32_t zfs_vdev_async_read_min_active;
extern uint32_t zfs_vdev_async_read_max_active;
extern uint32_t zfs_vdev_async_write_min_active;
extern uint32_t zfs_vdev_async_write_max_active;
extern uint32_t zfs_vdev_scrub_min_active;
extern uint32_t zfs_vdev_scrub_max_active;
extern int zfs_vdev_async_write_active_min_dirty_percent;
extern int zfs_vdev_async_write_active_max_dirty_percent;
extern int zfs_vdev_aggregation_limit;
extern int zfs_vdev_read_gap_limit;
extern int zfs_vdev_write_gap_limit;

extern uint_t arc_reduce_dnlc_percent;
extern int arc_lotsfree_percent;
extern hrtime_t zfs_delay_max_ns;
extern int spa_asize_inflation;
extern unsigned int	zfetch_max_streams;
extern unsigned int	zfetch_min_sec_reap;
extern int zfs_default_bs;
extern int zfs_default_ibs;
extern uint64_t metaslab_aliquot;
extern int zfs_vdev_cache_max;
extern int spa_max_replication_override;
extern int zfs_no_scrub_io;
extern int zfs_no_scrub_prefetch;
extern ssize_t zfs_immediate_write_sz;
extern offset_t zfs_read_chunk_size;
extern uint64_t metaslab_df_alloc_threshold;
extern int metaslab_df_free_pct;
extern ssize_t zvol_immediate_write_sz;

extern boolean_t l2arc_noprefetch;
extern boolean_t l2arc_feed_again;
extern boolean_t l2arc_norw;

extern int zfs_top_maxinflight;
extern int zfs_resilver_delay;
extern int zfs_scrub_delay;
extern int zfs_scan_idle;

extern int64_t zfs_free_bpobj_enabled;

extern int zfs_send_corrupt_data;
extern int zfs_send_queue_length;
extern int zfs_recv_queue_length;

extern uint64_t zvol_inhibit_dev;
extern int zfs_send_set_freerecords_bit;

extern uint64_t zfs_write_implies_delete_child;
extern uint32_t send_holes_without_birth_time;
extern uint64_t zfs_send_holes_without_birth_time;

extern uint64_t dbuf_cache_max_bytes;

extern int zfs_vdev_queue_depth_pct;
extern boolean_t zio_dva_throttle_enabled;

extern uint64_t zfs_lua_max_instrlimit;
extern uint64_t zfs_lua_max_memlimit;


extern uint64_t  zfs_trim_extent_bytes_max;
extern uint64_t  zfs_trim_extent_bytes_min;
extern unsigned int zfs_trim_metaslab_skip;
extern uint64_t  zfs_trim_txg_batch;
extern uint64_t  zfs_trim_queue_limit;

extern int zfs_send_unmodified_spill_blocks;
extern int zfs_special_class_metadata_reserve_pct;

extern int zfs_vnop_force_formd_normalized_output;

extern int zfs_arc_min_prefetch_ms;
extern int zfs_arc_min_prescient_prefetch_ms;

extern int zfs_expire_snapshot;
extern int zfs_admin_snapshot;
extern int zfs_auto_snapshot;

extern unsigned long zfs_spa_discard_memory_limit;
extern unsigned long zfs_async_block_max_blocks;
extern unsigned long zfs_initialize_chunk_size;
extern int zfs_scan_suspend_progress;
extern int zfs_removal_suspend_progress;
extern unsigned long zfs_livelist_max_entries;

extern int zfs_allow_redacted_dataset_mount;
extern unsigned int zfs_checksum_events_per_second;
extern int zfs_commit_timeout_pct;
extern int zfs_compressed_arc_enabled;
extern int zfs_condense_indirect_commit_entry_delay_ms;
extern unsigned long zfs_condense_min_mapping_bytes;
extern unsigned long zfs_deadman_checktime_ms;
extern char *zfs_deadman_failmode;
extern unsigned long zfs_deadman_synctime_ms;
extern unsigned long zfs_deadman_ziotime_ms;
extern int zfs_disable_ivset_guid_check;
extern unsigned long zfs_initialize_value;
extern int zfs_keep_log_spacemaps_at_export;
extern unsigned long l2arc_rebuild_blocks_min_l2size;
extern int l2arc_rebuild_enabled;
extern unsigned long l2arc_trim_ahead;
extern int zfs_livelist_condense_new_alloc;
extern int zfs_livelist_condense_sync_cancel;
extern int zfs_livelist_condense_sync_pause;
extern int zfs_livelist_condense_zthr_cancel;
extern int zfs_livelist_condense_zthr_pause;
extern int zfs_livelist_min_percent_shared;
extern int zfs_max_dataset_nesting;
extern unsigned long zfs_max_missing_tvds;
extern int metaslab_debug_load;
extern unsigned long metaslab_force_ganging;
extern unsigned int zfs_multihost_fail_intervals;
extern unsigned int zfs_multihost_import_intervals;
extern unsigned long zfs_multihost_interval;
extern int zfs_override_estimate_recordsize;
extern int zfs_remove_max_segment;
extern int zfs_resilver_min_time_ms;
extern int zfs_scan_legacy;
extern unsigned long zfs_scan_vdev_limit;
extern unsigned int zfs_slow_io_events_per_second;
extern int spa_load_verify_data;
extern int spa_load_verify_metadata;
extern int zfs_unlink_suspend_progress;
extern int zfs_vdev_min_ms_count;
extern int vdev_validate_skip;
extern int zfs_zevent_len_max;
extern int zio_slow_io_ms;

int kstat_osx_init(void);
void kstat_osx_fini(void);

int arc_kstat_update(kstat_t *ksp, int rw);
int arc_kstat_update_osx(kstat_t *ksp, int rw);

#endif
