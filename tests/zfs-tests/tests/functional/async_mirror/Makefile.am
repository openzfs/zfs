# SPDX-License-Identifier: CDDL-1.0
#
# This test group has its own Makefile.  With it in
# tests/zfs-tests/tests/Makefile.am, the file list of that Makefile is
# longer than one shell command may be (MAX_ARG_STRLEN), and
# "make dist" fails.
#
# Regenerate the lists in this folder with:
#   ls *.cfg *.kshlib | sort | sed 's/^/\t/;$!s/$/ \\/'
#   ls *.ksh | sort | sed 's/^/\t/;$!s/$/ \\/'

async_mirrordir = $(datadir)/$(PACKAGE)/zfs-tests/tests/functional/async_mirror

dist_async_mirror_DATA = \
	async_mirror.cfg \
	async_mirror.kshlib

dist_async_mirror_SCRIPTS = \
	async_mirror_backlog_no_extra_txgs_pos.ksh \
	async_mirror_block_types_pos.ksh \
	async_mirror_catch_up_pos.ksh \
	async_mirror_checkpoint_reguid_neg.ksh \
	async_mirror_child_changes_neg.ksh \
	async_mirror_claimed_slog_pos.ksh \
	async_mirror_claimed_zil_copied_pos.ksh \
	async_mirror_claimed_zil_pos.ksh \
	async_mirror_commit_read_error_pos.ksh \
	async_mirror_commit_reread_alone_pos.ksh \
	async_mirror_config_mark_pos.ksh \
	async_mirror_damaged_data_pos.ksh \
	async_mirror_damaged_metadata_pos.ksh \
	async_mirror_dedup_copies_pos.ksh \
	async_mirror_deferred_block_pos.ksh \
	async_mirror_deferred_limit_pos.ksh \
	async_mirror_deferred_many_txgs_pos.ksh \
	async_mirror_detach_pos.ksh \
	async_mirror_disabled_plain_mirror_pos.ksh \
	async_mirror_dtl_label_flush_pos.ksh \
	async_mirror_dtl_replace_missing_pos.ksh \
	async_mirror_enable_sync_writes_pos.ksh \
	async_mirror_erase_crash_pos.ksh \
	async_mirror_events_pos.ksh \
	async_mirror_export_import_resume_pos.ksh \
	async_mirror_failed_copy_dtl_pos.ksh \
	async_mirror_failed_copy_resilver_pos.ksh \
	async_mirror_failed_detach_sync_pos.ksh \
	async_mirror_feature_active_pos.ksh \
	async_mirror_feature_active_until_match_pos.ksh \
	async_mirror_feature_enabled_after_off_pos.ksh \
	async_mirror_flush_failure_pos.ksh \
	async_mirror_flush_fence_pos.ksh \
	async_mirror_fork_recorded_neg.ksh \
	async_mirror_freeze_persists_pos.ksh \
	async_mirror_frozen_no_release_pos.ksh \
	async_mirror_frozen_pool_txg_pos.ksh \
	async_mirror_giveup_small_pool_pos.ksh \
	async_mirror_giveup_space_healthy_pos.ksh \
	async_mirror_giveup_space_pos.ksh \
	async_mirror_giveup_stall_pos.ksh \
	async_mirror_held_burst_pos.ksh \
	async_mirror_held_limit_giveup_pos.ksh \
	async_mirror_held_limit_healthy_pos.ksh \
	async_mirror_held_limit_stalled_pos.ksh \
	async_mirror_held_slow_child_pos.ksh \
	async_mirror_held_space_enospc_pos.ksh \
	async_mirror_held_throttle_pos.ksh \
	async_mirror_idle_after_write_pos.ksh \
	async_mirror_import_damaged_record_neg.ksh \
	async_mirror_import_ends_freeze_pos.ksh \
	async_mirror_import_keeps_lagging_blocks_pos.ksh \
	async_mirror_import_keeps_live_blocks_neg.ksh \
	async_mirror_import_load_limit_pos.ksh \
	async_mirror_import_no_cksum_errors_pos.ksh \
	async_mirror_keep_lagging_copy_pos.ksh \
	async_mirror_keep_one_copy_pos.ksh \
	async_mirror_keep_option_neg.ksh \
	async_mirror_keep_refused_no_repair_neg.ksh \
	async_mirror_keep_unloadable_pos.ksh \
	async_mirror_label_refresh_error_pos.ksh \
	async_mirror_lagging_alone_label_rewind_pos.ksh \
	async_mirror_lagging_alone_pos.ksh \
	async_mirror_lagging_alone_rewind_pos.ksh \
	async_mirror_lagging_alone_rewound_pos.ksh \
	async_mirror_lagging_alone_slog_pos.ksh \
	async_mirror_lagging_alone_txg_pos.ksh \
	async_mirror_lagging_alone_zil_pos.ksh \
	async_mirror_lagging_older_block_pos.ksh \
	async_mirror_lagging_rejoins_pos.ksh \
	async_mirror_last_normal_neg.ksh \
	async_mirror_last_normal_topology_neg.ksh \
	async_mirror_leader_loss_pos.ksh \
	async_mirror_leader_write_error_pos.ksh \
	async_mirror_legacy_label_mark_neg.ksh \
	async_mirror_lost_metadata_freeze_pos.ksh \
	async_mirror_media_error_pos.ksh \
	async_mirror_missing_at_import_online_pos.ksh \
	async_mirror_multihost_neg.ksh \
	async_mirror_off_resilvers_zil_pos.ksh \
	async_mirror_old_software_pos.ksh \
	async_mirror_one_top_vdev_neg.ksh \
	async_mirror_online_clean_copy_pos.ksh \
	async_mirror_online_diverged_neg.ksh \
	async_mirror_plain_mirror_no_mark_pos.ksh \
	async_mirror_prop_ends_freeze_pos.ksh \
	async_mirror_prop_off_pos.ksh \
	async_mirror_prop_off_wait_pos.ksh \
	async_mirror_prop_on_pos.ksh \
	async_mirror_prop_persists_pos.ksh \
	async_mirror_prop_refused_neg.ksh \
	async_mirror_prop_switch_export_pos.ksh \
	async_mirror_readonly_import_fence_pos.ksh \
	async_mirror_readonly_lagging_no_fork_pos.ksh \
	async_mirror_readonly_rewind_fence_pos.ksh \
	async_mirror_read_retry_pos.ksh \
	async_mirror_read_write_modes_pos.ksh \
	async_mirror_rebuild_fallback_source_pos.ksh \
	async_mirror_refused_freeze_pos.ksh \
	async_mirror_refused_later_txg_pos.ksh \
	async_mirror_release_futile_pos.ksh \
	async_mirror_release_idle_pos.ksh \
	async_mirror_release_time_limit_pos.ksh \
	async_mirror_release_urgent_pos.ksh \
	async_mirror_removed_vdev_free_pos.ksh \
	async_mirror_remove_vdev_neg.ksh \
	async_mirror_replace_failed_lagging_pos.ksh \
	async_mirror_replace_failed_rebuild_neg.ksh \
	async_mirror_replace_frozen_lagging_pos.ksh \
	async_mirror_replace_healthy_lagging_neg.ksh \
	async_mirror_resilvering_child_neg.ksh \
	async_mirror_resilvering_leader_pos.ksh \
	async_mirror_resilver_lagging_pos.ksh \
	async_mirror_resilver_leader_pos.ksh \
	async_mirror_rest_copy_pos.ksh \
	async_mirror_rewind_child_ahead_pos.ksh \
	async_mirror_rewind_erase_uberblocks_pos.ksh \
	async_mirror_rewind_fence_pos.ksh \
	async_mirror_rewind_import_pos.ksh \
	async_mirror_rewind_resume_pos.ksh \
	async_mirror_rewound_child_history_pos.ksh \
	async_mirror_scrub_lagging_pos.ksh \
	async_mirror_second_lagging_neg.ksh \
	async_mirror_send_receive_pos.ksh \
	async_mirror_sort_passes_pos.ksh \
	async_mirror_spare_failed_lagging_neg.ksh \
	async_mirror_split_lagging_ahead_neg.ksh \
	async_mirror_split_leader_ahead_neg.ksh \
	async_mirror_status_data_errors_pos.ksh \
	async_mirror_switch_txg_lineage_pos.ksh \
	async_mirror_sync_writes_pos.ksh \
	async_mirror_throttle_pos.ksh \
	async_mirror_transient_failure_pos.ksh \
	async_mirror_trim_initialize_neg.ksh \
	async_mirror_write_only_read_pos.ksh \
	async_mirror_zdb_child_entry_pos.ksh \
	async_mirror_zdb_no_leak_pos.ksh \
	async_mirror_zdb_open_pos.ksh \
	async_mirror_zil_free_pos.ksh \
	async_mirror_zil_next_block_lagging_pos.ksh \
	async_mirror_zil_next_block_leader_pos.ksh \
	async_mirror_zil_switch_on_free_pos.ksh \
	async_mirror_zpool_status_json_pos.ksh \
	async_mirror_zpool_status_pos.ksh \
	async_mirror_zpool_sync_pos.ksh \
	async_mirror_zpool_wait_pos.ksh \
	cleanup.ksh \
	setup.ksh
