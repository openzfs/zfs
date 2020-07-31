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
 * Copyright 2017 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/refcount.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_pool.h>
#ifdef _KERNEL
#include <sys/vmsystm.h>
#include <vm/anon.h>
#include <sys/fs/swapnode.h>
#include <sys/dnlc.h>
#endif
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/kstat_windows.h>
#include <sys/zfs_ioctl.h>
#include <sys/spa.h>
#include <sys/zap_impl.h>
#include <sys/zil.h>

/*
 * In Solaris the tunable are set via /etc/system. Until we have a load
 * time configuration, we add them to writable kstat tunables.
 *
 * This table is more or less populated from IllumOS mdb zfs_params sources
 * https://github.com/illumos/illumos-gate/blob/master/usr/src/cmd/mdb/common/modules/zfs/zfs.c#L336-L392
 *
 */



osx_kstat_t osx_kstat = {
	{ "spa_version",				KSTAT_DATA_UINT64 },
	{ "zpl_version",				KSTAT_DATA_UINT64 },

	{ "active_vnodes",				KSTAT_DATA_UINT64 },
	{ "vnop_debug",					KSTAT_DATA_UINT64 },
	{ "reclaim_nodes",				KSTAT_DATA_UINT64 },
	{ "ignore_negatives",			KSTAT_DATA_UINT64 },
	{ "ignore_positives",			KSTAT_DATA_UINT64 },
	{ "create_negatives",			KSTAT_DATA_UINT64 },
	{ "force_formd_normalized",		KSTAT_DATA_UINT64 },
	{ "skip_unlinked_drain",		KSTAT_DATA_UINT64 },
	{ "use_system_sync",			KSTAT_DATA_UINT64 },

	{ "zfs_arc_max",				KSTAT_DATA_UINT64 },
	{ "zfs_arc_min",				KSTAT_DATA_UINT64 },
	{ "zfs_arc_meta_limit",			KSTAT_DATA_UINT64 },
	{ "zfs_arc_meta_min",			KSTAT_DATA_UINT64 },
	{ "zfs_arc_grow_retry",			KSTAT_DATA_UINT64 },
	{ "zfs_arc_shrink_shift",		KSTAT_DATA_UINT64 },
	{ "zfs_arc_p_min_shift",		KSTAT_DATA_UINT64 },
	{ "zfs_arc_average_blocksize",	KSTAT_DATA_UINT64 },

	{ "l2arc_write_max",			KSTAT_DATA_UINT64 },
	{ "l2arc_write_boost",			KSTAT_DATA_UINT64 },
	{ "l2arc_headroom",				KSTAT_DATA_UINT64 },
	{ "l2arc_headroom_boost",		KSTAT_DATA_UINT64 },
	{ "l2arc_max_block_size",		KSTAT_DATA_UINT64 },
	{ "l2arc_feed_secs",			KSTAT_DATA_UINT64 },
	{ "l2arc_feed_min_ms",			KSTAT_DATA_UINT64 },

	{ "max_active",					KSTAT_DATA_UINT64 },
	{ "sync_read_min_active",		KSTAT_DATA_UINT64 },
	{ "sync_read_max_active",		KSTAT_DATA_UINT64 },
	{ "sync_write_min_active",		KSTAT_DATA_UINT64 },
	{ "sync_write_max_active",		KSTAT_DATA_UINT64 },
	{ "async_read_min_active",		KSTAT_DATA_UINT64 },
	{ "async_read_max_active",		KSTAT_DATA_UINT64 },
	{ "async_write_min_active",		KSTAT_DATA_UINT64 },
	{ "async_write_max_active",		KSTAT_DATA_UINT64 },
	{ "scrub_min_active",			KSTAT_DATA_UINT64 },
	{ "scrub_max_active",			KSTAT_DATA_UINT64 },
	{ "async_write_min_dirty_pct",	KSTAT_DATA_INT64  },
	{ "async_write_max_dirty_pct",	KSTAT_DATA_INT64  },
	{ "aggregation_limit",			KSTAT_DATA_INT64  },
	{ "read_gap_limit",				KSTAT_DATA_INT64  },
	{ "write_gap_limit",			KSTAT_DATA_INT64  },

	{"arc_reduce_dnlc_percent",		KSTAT_DATA_INT64  },
	{"arc_lotsfree_percent",		KSTAT_DATA_INT64  },
	{"zfs_dirty_data_max",			KSTAT_DATA_INT64  },
	{"zfs_dirty_data_sync",			KSTAT_DATA_INT64  },
	{"zfs_delay_max_ns",			KSTAT_DATA_INT64  },
	{"zfs_delay_min_dirty_percent",	KSTAT_DATA_INT64  },
	{"zfs_delay_scale",				KSTAT_DATA_INT64  },
	{"spa_asize_inflation",			KSTAT_DATA_INT64  },
	{"zfs_mdcomp_disable",			KSTAT_DATA_INT64  },
	{"zfs_prefetch_disable",		KSTAT_DATA_INT64  },
	{"zfetch_max_streams",			KSTAT_DATA_INT64  },
	{"zfetch_min_sec_reap",			KSTAT_DATA_INT64  },
	{"zfetch_array_rd_sz",			KSTAT_DATA_INT64  },
	{"zfs_default_bs",				KSTAT_DATA_INT64  },
	{"zfs_default_ibs",				KSTAT_DATA_INT64  },
	{"metaslab_aliquot",			KSTAT_DATA_INT64  },
	{"spa_max_replication_override",KSTAT_DATA_INT64  },
	{"spa_mode_global",				KSTAT_DATA_INT64  },
	{"zfs_flags",					KSTAT_DATA_INT64  },
	{"zfs_txg_timeout",				KSTAT_DATA_INT64  },
	{"zfs_vdev_cache_max",			KSTAT_DATA_INT64  },
	{"zfs_vdev_cache_size",			KSTAT_DATA_INT64  },
	{"zfs_vdev_cache_bshift",		KSTAT_DATA_INT64  },
	{"vdev_mirror_shift",			KSTAT_DATA_INT64  },
	{"zfs_scrub_limit",				KSTAT_DATA_INT64  },
	{"zfs_no_scrub_io",				KSTAT_DATA_INT64  },
	{"zfs_no_scrub_prefetch",		KSTAT_DATA_INT64  },
	{"fzap_default_block_shift",	KSTAT_DATA_INT64  },
	{"zfs_immediate_write_sz",		KSTAT_DATA_INT64  },
	{"zfs_read_chunk_size",			KSTAT_DATA_INT64  },
	{"zfs_nocacheflush",			KSTAT_DATA_INT64  },
	{"zil_replay_disable",			KSTAT_DATA_INT64  },
	{"metaslab_df_alloc_threshold",	KSTAT_DATA_INT64  },
	{"metaslab_df_free_pct",		KSTAT_DATA_INT64  },
	{"zio_injection_enabled",		KSTAT_DATA_INT64  },
	{"zvol_immediate_write_sz",		KSTAT_DATA_INT64  },

	{ "l2arc_noprefetch",			KSTAT_DATA_INT64  },
	{ "l2arc_feed_again",			KSTAT_DATA_INT64  },
	{ "l2arc_norw",					KSTAT_DATA_INT64  },

	{"zfs_recover",					KSTAT_DATA_INT64  },

	{"zfs_free_bpobj_enabled",		KSTAT_DATA_INT64  },

	{"zfs_send_corrupt_data",		KSTAT_DATA_UINT64  },
	{"zfs_send_queue_length",		KSTAT_DATA_UINT64  },
	{"zfs_recv_queue_length",		KSTAT_DATA_UINT64  },

	{"zvol_inhibit_dev",			KSTAT_DATA_UINT64  },
	{"zfs_send_set_freerecords_bit",KSTAT_DATA_UINT64  },

	{"zfs_write_implies_delete_child",KSTAT_DATA_UINT64  },
	{"zfs_send_holes_without_brth_tme",KSTAT_DATA_UINT64  },

	{"dbuf_cache_max_bytes",		KSTAT_DATA_UINT64  },

	{"zfs_vdev_queue_depth_pct",	KSTAT_DATA_UINT64  },
	{"zio_dva_throttle_enabled",	KSTAT_DATA_UINT64  },

	{"zfs_vdev_file_size_mismatch_cnt",KSTAT_DATA_UINT64  },

	{"zfs_lua_max_instrlimit",		KSTAT_DATA_UINT64  },
	{"zfs_lua_max_memlimit",		KSTAT_DATA_UINT64  },

	{"zfs_trim_extent_bytes_max",	KSTAT_DATA_UINT64  },
	{"zfs_trim_extent_bytes_min",	KSTAT_DATA_UINT64  },
	{"zfs_trim_metaslab_skip",		KSTAT_DATA_UINT64  },
	{"zfs_trim_txg_batch",			KSTAT_DATA_UINT64  },
	{"zfs_trim_queue_limit",		KSTAT_DATA_UINT64  },

	{"hostid",					KSTAT_DATA_UINT32 },
	{"send_unmodified_spill_blocks",		KSTAT_DATA_UINT64  },
	{"special_class_metadata_rsrv_pct",		KSTAT_DATA_UINT64  },

	{ "zfs_disable_wincache",		KSTAT_DATA_UINT64 },
	{ "zfs_disable_removablemedia",		KSTAT_DATA_UINT64 },
	{ "zfs_vdev_initialize_value",		KSTAT_DATA_UINT64 },
};




static kstat_t		*osx_kstat_ksp;

#if !defined (__OPTIMIZE__)
#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

static int osx_kstat_update(kstat_t *ksp, int rw)
{
	osx_kstat_t *ks = ksp->ks_data;

	if (rw == KSTAT_WRITE) {

		/* win32 */

		debug_vnop_osx_printf = ks->win32_debug.value.ui64;
		extern void saveBuffer(void);
		if (ks->win32_debug.value.ui64 == 1337)
			saveBuffer();
		if (ks->win32_debug.value.ui64 == 9119)
			panic("ZFS: User requested panic\n");
		zfs_vnop_ignore_negatives = ks->win32_ignore_negatives.value.ui64;
		zfs_vnop_ignore_positives = ks->win32_ignore_positives.value.ui64;
		zfs_vnop_create_negatives = ks->win32_create_negatives.value.ui64;
		zfs_vnop_force_formd_normalized_output = ks->win32_force_formd_normalized.value.ui64;
		zfs_vnop_skip_unlinked_drain = ks->win32_skip_unlinked_drain.value.ui64;
		zfs_vfs_sync_paranoia = ks->win32_use_system_sync.value.ui64;

		/* ARC */
		arc_kstat_update(ksp, rw);
		arc_kstat_update_osx(ksp, rw);

		/* L2ARC */
		l2arc_write_max = ks->l2arc_write_max.value.ui64;
		l2arc_write_boost = ks->l2arc_write_boost.value.ui64;
		l2arc_headroom = ks->l2arc_headroom.value.ui64;
		l2arc_headroom_boost = ks->l2arc_headroom_boost.value.ui64;
		l2arc_max_block_size = ks->l2arc_max_block_size.value.ui64;
		l2arc_feed_secs = ks->l2arc_feed_secs.value.ui64;
		l2arc_feed_min_ms = ks->l2arc_feed_min_ms.value.ui64;

		l2arc_noprefetch = ks->l2arc_noprefetch.value.i64;
		l2arc_feed_again = ks->l2arc_feed_again.value.i64;
		l2arc_norw = ks->l2arc_norw.value.i64;

		/* vdev_queue */

		zfs_vdev_max_active =
			ks->zfs_vdev_max_active.value.ui64;
		zfs_vdev_sync_read_min_active =
			ks->zfs_vdev_sync_read_min_active.value.ui64;
		zfs_vdev_sync_read_max_active =
			ks->zfs_vdev_sync_read_max_active.value.ui64;
		zfs_vdev_sync_write_min_active =
			ks->zfs_vdev_sync_write_min_active.value.ui64;
		zfs_vdev_sync_write_max_active =
			ks->zfs_vdev_sync_write_max_active.value.ui64;
		zfs_vdev_async_read_min_active =
			ks->zfs_vdev_async_read_min_active.value.ui64;
		zfs_vdev_async_read_max_active =
			ks->zfs_vdev_async_read_max_active.value.ui64;
		zfs_vdev_async_write_min_active =
			ks->zfs_vdev_async_write_min_active.value.ui64;
		zfs_vdev_async_write_max_active =
			ks->zfs_vdev_async_write_max_active.value.ui64;
		zfs_vdev_scrub_min_active =
			ks->zfs_vdev_scrub_min_active.value.ui64;
		zfs_vdev_scrub_max_active =
			ks->zfs_vdev_scrub_max_active.value.ui64;
		zfs_vdev_async_write_active_min_dirty_percent =
			ks->zfs_vdev_async_write_active_min_dirty_percent.value.i64;
		zfs_vdev_async_write_active_max_dirty_percent =
			ks->zfs_vdev_async_write_active_max_dirty_percent.value.i64;
		zfs_vdev_aggregation_limit =
			ks->zfs_vdev_aggregation_limit.value.i64;
		zfs_vdev_read_gap_limit =
			ks->zfs_vdev_read_gap_limit.value.i64;
		zfs_vdev_write_gap_limit =
			ks->zfs_vdev_write_gap_limit.value.i64;

		arc_reduce_dnlc_percent =
			ks->arc_reduce_dnlc_percent.value.i64;
		arc_lotsfree_percent =
			ks->arc_lotsfree_percent.value.i64;
		zfs_dirty_data_max =
			ks->zfs_dirty_data_max.value.i64;
		zfs_dirty_data_sync =
			ks->zfs_dirty_data_sync.value.i64;
		zfs_delay_max_ns =
			ks->zfs_delay_max_ns.value.i64;
		zfs_delay_min_dirty_percent =
			ks->zfs_delay_min_dirty_percent.value.i64;
		zfs_delay_scale =
			ks->zfs_delay_scale.value.i64;
		spa_asize_inflation =
			ks->spa_asize_inflation.value.i64;
		zfs_mdcomp_disable =
			ks->zfs_mdcomp_disable.value.i64;
		zfs_prefetch_disable =
			ks->zfs_prefetch_disable.value.i64;
		zfetch_max_streams =
			ks->zfetch_max_streams.value.i64;
		zfetch_min_sec_reap =
			ks->zfetch_min_sec_reap.value.i64;
		zfetch_array_rd_sz =
			ks->zfetch_array_rd_sz.value.i64;
		zfs_default_bs =
			ks->zfs_default_bs.value.i64;
		zfs_default_ibs =
			ks->zfs_default_ibs.value.i64;
		metaslab_aliquot =
			ks->metaslab_aliquot.value.i64;
		spa_max_replication_override =
			ks->spa_max_replication_override.value.i64;
		spa_mode_global =
			ks->spa_mode_global.value.i64;
		zfs_flags =
			ks->zfs_flags.value.i64;
		zfs_txg_timeout =
			ks->zfs_txg_timeout.value.i64;
		zfs_vdev_cache_max =
			ks->zfs_vdev_cache_max.value.i64;
		zfs_vdev_cache_size =
			ks->zfs_vdev_cache_size.value.i64;
		zfs_no_scrub_io =
			ks->zfs_no_scrub_io.value.i64;
		zfs_no_scrub_prefetch =
			ks->zfs_no_scrub_prefetch.value.i64;
		fzap_default_block_shift =
			ks->fzap_default_block_shift.value.i64;
		zfs_immediate_write_sz =
			ks->zfs_immediate_write_sz.value.i64;
		zfs_read_chunk_size =
			ks->zfs_read_chunk_size.value.i64;
		zfs_nocacheflush =
			ks->zfs_nocacheflush.value.i64;
		zil_replay_disable =
			ks->zil_replay_disable.value.i64;
		metaslab_df_alloc_threshold =
			ks->metaslab_df_alloc_threshold.value.i64;
		metaslab_df_free_pct =
			ks->metaslab_df_free_pct.value.i64;
		zio_injection_enabled =
			ks->zio_injection_enabled.value.i64;
		zvol_immediate_write_sz =
			ks->zvol_immediate_write_sz.value.i64;

		zfs_recover =
			ks->zfs_recover.value.i64;

		zfs_free_bpobj_enabled	 =
			ks->zfs_free_bpobj_enabled.value.i64;

		zfs_send_corrupt_data =
			ks->zfs_send_corrupt_data.value.ui64;
		zfs_send_queue_length =
			ks->zfs_send_queue_length.value.ui64;
		zfs_recv_queue_length =
			ks->zfs_recv_queue_length.value.ui64;

		zvol_inhibit_dev =
			ks->zvol_inhibit_dev.value.ui64;
		zfs_send_set_freerecords_bit =
			ks->zfs_send_set_freerecords_bit.value.ui64;

		zfs_write_implies_delete_child =
			ks->zfs_write_implies_delete_child.value.ui64;
		send_holes_without_birth_time =
			ks->zfs_send_holes_without_birth_time.value.ui64;

		dbuf_cache_max_bytes =
		    ks->dbuf_cache_max_bytes.value.ui64;

		zfs_vdev_queue_depth_pct =
		    ks->zfs_vdev_queue_depth_pct.value.ui64;

		zio_dva_throttle_enabled =
		    (boolean_t) ks->zio_dva_throttle_enabled.value.ui64;

		zfs_lua_max_instrlimit =
		    ks->zfs_lua_max_instrlimit.value.ui64;
		zfs_lua_max_memlimit =
		    ks->zfs_lua_max_memlimit.value.ui64;

		zfs_trim_extent_bytes_max =
			ks->zfs_trim_extent_bytes_max.value.ui64;
		zfs_trim_extent_bytes_min =
			ks->zfs_trim_extent_bytes_min.value.ui64;
		zfs_trim_metaslab_skip =
			ks->zfs_trim_metaslab_skip.value.ui64;
		zfs_trim_txg_batch =
			ks->zfs_trim_txg_batch.value.ui64;
		zfs_trim_queue_limit =
			ks->zfs_trim_queue_limit.value.ui64;

		spl_hostid = ks->win32_hw_hostid.value.ui32;
		zfs_send_unmodified_spill_blocks =
			ks->zfs_send_unmodified_spill_blocks.value.ui64;
		zfs_special_class_metadata_reserve_pct =
			ks->zfs_special_class_metadata_reserve_pct.value.ui64;

		zfs_disable_wincache =
			ks->zfs_disable_wincache.value.ui64;
		zfs_disable_removablemedia =
			ks->zfs_disable_removablemedia.value.ui64;
		zfs_initialize_value =
			ks->zfs_vdev_initialize_value.value.ui64;
	} else {

		/* kstat READ */
		ks->spa_version.value.ui64                   = SPA_VERSION;
		ks->zpl_version.value.ui64                   = ZPL_VERSION;

		/* win32 */
		ks->win32_active_vnodes.value.ui64          = vnop_num_vnodes;
		ks->win32_reclaim_nodes.value.ui64          = vnop_num_reclaims;
		ks->win32_debug.value.ui64                  = debug_vnop_osx_printf;
		ks->win32_ignore_negatives.value.ui64       = zfs_vnop_ignore_negatives;
		ks->win32_ignore_positives.value.ui64       = zfs_vnop_ignore_positives;
		ks->win32_create_negatives.value.ui64       = zfs_vnop_create_negatives;
		ks->win32_force_formd_normalized.value.ui64 = zfs_vnop_force_formd_normalized_output;
		ks->win32_skip_unlinked_drain.value.ui64    = zfs_vnop_skip_unlinked_drain;
		ks->win32_use_system_sync.value.ui64 = zfs_vfs_sync_paranoia;

		/* ARC */
		arc_kstat_update(ksp, rw);
		arc_kstat_update_osx(ksp, rw);

		/* L2ARC */
		ks->l2arc_write_max.value.ui64               = l2arc_write_max;
		ks->l2arc_write_boost.value.ui64             = l2arc_write_boost;
		ks->l2arc_headroom.value.ui64                = l2arc_headroom;
		ks->l2arc_headroom_boost.value.ui64          = l2arc_headroom_boost;
		ks->l2arc_max_block_size.value.ui64          = l2arc_max_block_size;
		ks->l2arc_feed_secs.value.ui64               = l2arc_feed_secs;
		ks->l2arc_feed_min_ms.value.ui64             = l2arc_feed_min_ms;

		ks->l2arc_noprefetch.value.i64               = l2arc_noprefetch;
		ks->l2arc_feed_again.value.i64               = l2arc_feed_again;
		ks->l2arc_norw.value.i64                     = l2arc_norw;

		/* vdev_queue */
		ks->zfs_vdev_max_active.value.ui64 =
			zfs_vdev_max_active ;
		ks->zfs_vdev_sync_read_min_active.value.ui64 =
			zfs_vdev_sync_read_min_active ;
		ks->zfs_vdev_sync_read_max_active.value.ui64 =
			zfs_vdev_sync_read_max_active ;
		ks->zfs_vdev_sync_write_min_active.value.ui64 =
			zfs_vdev_sync_write_min_active ;
		ks->zfs_vdev_sync_write_max_active.value.ui64 =
			zfs_vdev_sync_write_max_active ;
		ks->zfs_vdev_async_read_min_active.value.ui64 =
			zfs_vdev_async_read_min_active ;
		ks->zfs_vdev_async_read_max_active.value.ui64 =
			zfs_vdev_async_read_max_active ;
		ks->zfs_vdev_async_write_min_active.value.ui64 =
			zfs_vdev_async_write_min_active ;
		ks->zfs_vdev_async_write_max_active.value.ui64 =
			zfs_vdev_async_write_max_active ;
		ks->zfs_vdev_scrub_min_active.value.ui64 =
			zfs_vdev_scrub_min_active ;
		ks->zfs_vdev_scrub_max_active.value.ui64 =
			zfs_vdev_scrub_max_active ;
		ks->zfs_vdev_async_write_active_min_dirty_percent.value.i64 =
			zfs_vdev_async_write_active_min_dirty_percent ;
		ks->zfs_vdev_async_write_active_max_dirty_percent.value.i64 =
			zfs_vdev_async_write_active_max_dirty_percent ;
		ks->zfs_vdev_aggregation_limit.value.i64 =
			zfs_vdev_aggregation_limit ;
		ks->zfs_vdev_read_gap_limit.value.i64 =
			zfs_vdev_read_gap_limit ;
		ks->zfs_vdev_write_gap_limit.value.i64 =
			zfs_vdev_write_gap_limit;

		ks->arc_reduce_dnlc_percent.value.i64 =
			arc_reduce_dnlc_percent;
		ks->arc_lotsfree_percent.value.i64 =
			arc_lotsfree_percent;
		ks->zfs_dirty_data_max.value.i64 =
			zfs_dirty_data_max;
		ks->zfs_dirty_data_sync.value.i64 =
			zfs_dirty_data_sync;
		ks->zfs_delay_max_ns.value.i64 =
			zfs_delay_max_ns;
		ks->zfs_delay_min_dirty_percent.value.i64 =
			zfs_delay_min_dirty_percent;
		ks->zfs_delay_scale.value.i64 =
			zfs_delay_scale;
		ks->spa_asize_inflation.value.i64 =
			spa_asize_inflation;
		ks->zfs_mdcomp_disable.value.i64 =
			zfs_mdcomp_disable;
		ks->zfs_prefetch_disable.value.i64 =
			zfs_prefetch_disable;
		ks->zfetch_max_streams.value.i64 =
			zfetch_max_streams;
		ks->zfetch_min_sec_reap.value.i64 =
			zfetch_min_sec_reap;
		ks->zfetch_array_rd_sz.value.i64 =
			zfetch_array_rd_sz;
		ks->zfs_default_bs.value.i64 =
			zfs_default_bs;
		ks->zfs_default_ibs.value.i64 =
			zfs_default_ibs;
		ks->metaslab_aliquot.value.i64 =
			metaslab_aliquot;
		ks->spa_max_replication_override.value.i64 =
			spa_max_replication_override;
		ks->spa_mode_global.value.i64 =
			spa_mode_global;
		ks->zfs_flags.value.i64 =
			zfs_flags;
		ks->zfs_txg_timeout.value.i64 =
			zfs_txg_timeout;
		ks->zfs_vdev_cache_max.value.i64 =
			zfs_vdev_cache_max;
		ks->zfs_vdev_cache_size.value.i64 =
			zfs_vdev_cache_size;
		ks->zfs_no_scrub_io.value.i64 =
			zfs_no_scrub_io;
		ks->zfs_no_scrub_prefetch.value.i64 =
			zfs_no_scrub_prefetch;
		ks->fzap_default_block_shift.value.i64 =
			fzap_default_block_shift;
		ks->zfs_immediate_write_sz.value.i64 =
			zfs_immediate_write_sz;
		ks->zfs_read_chunk_size.value.i64 =
			zfs_read_chunk_size;
		ks->zfs_nocacheflush.value.i64 =
			zfs_nocacheflush;
		ks->zil_replay_disable.value.i64 =
			zil_replay_disable;
		ks->metaslab_df_alloc_threshold.value.i64 =
			metaslab_df_alloc_threshold;
		ks->metaslab_df_free_pct.value.i64 =
			metaslab_df_free_pct;
		ks->zio_injection_enabled.value.i64 =
			zio_injection_enabled;
		ks->zvol_immediate_write_sz.value.i64 =
			zvol_immediate_write_sz;

		ks->zfs_recover.value.i64 =
			zfs_recover;

		ks->zfs_free_bpobj_enabled.value.i64 =
			zfs_free_bpobj_enabled;

		ks->zfs_send_corrupt_data.value.ui64 =
			zfs_send_corrupt_data;
		ks->zfs_send_queue_length.value.ui64 =
			zfs_send_queue_length;
		ks->zfs_recv_queue_length.value.ui64 =
			zfs_recv_queue_length;

		ks->zvol_inhibit_dev.value.ui64 =
			zvol_inhibit_dev;
		ks->zfs_send_set_freerecords_bit.value.ui64 =
			zfs_send_set_freerecords_bit;

		ks->zfs_write_implies_delete_child.value.ui64 =
			zfs_write_implies_delete_child;
		ks->zfs_send_holes_without_birth_time.value.ui64 =
			send_holes_without_birth_time;

		ks->dbuf_cache_max_bytes.value.ui64 = dbuf_cache_max_bytes;

		ks->zfs_vdev_queue_depth_pct.value.ui64 = zfs_vdev_queue_depth_pct;
		ks->zio_dva_throttle_enabled.value.ui64 = (uint64_t) zio_dva_throttle_enabled;

		ks->zfs_vdev_file_size_mismatch_cnt.value.ui64 = zfs_vdev_file_size_mismatch_cnt;

		ks->zfs_lua_max_instrlimit.value.ui64 = zfs_lua_max_instrlimit;
		ks->zfs_lua_max_memlimit.value.ui64 = zfs_lua_max_memlimit;

		ks->zfs_trim_extent_bytes_max.value.ui64 =
			zfs_trim_extent_bytes_max;
		ks->zfs_trim_extent_bytes_min.value.ui64 =
			zfs_trim_extent_bytes_min;
		ks->zfs_trim_metaslab_skip.value.ui64 =
			zfs_trim_metaslab_skip;
		ks->zfs_trim_txg_batch.value.ui64 =
			zfs_trim_txg_batch;
		ks->zfs_trim_queue_limit.value.ui64 =
			zfs_trim_queue_limit;

		ks->win32_hw_hostid.value.ui32 = spl_hostid;
		ks->zfs_send_unmodified_spill_blocks.value.ui64 =
			zfs_send_unmodified_spill_blocks;
		ks->zfs_special_class_metadata_reserve_pct.value.ui64 =
			zfs_special_class_metadata_reserve_pct;

		ks->zfs_disable_wincache.value.ui64 =
			zfs_disable_wincache;
		ks->zfs_disable_removablemedia.value.ui64 =
			zfs_disable_removablemedia;
		ks->zfs_vdev_initialize_value.value.ui64 =
			zfs_initialize_value;
	}

	return 0;
}

int kstat_osx_init(PUNICODE_STRING RegistryPath)
{
	int error = 0;

	osx_kstat_ksp = kstat_create("zfs", 0, "tunable", "win32",
	    KSTAT_TYPE_NAMED, sizeof (osx_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL|KSTAT_FLAG_WRITABLE);

	if (osx_kstat_ksp != NULL) {
		osx_kstat_ksp->ks_data = &osx_kstat;
        osx_kstat_ksp->ks_update = osx_kstat_update;
		kstat_install(osx_kstat_ksp);

		// We don't hold the ksp here, only call at init, so there are
		// no other threads.
		KSTAT_ENTER(osx_kstat_ksp);
		error = KSTAT_UPDATE(osx_kstat_ksp, KSTAT_READ);
		if (error != 0) goto out;

		// Returns number of changed, zero means nothing to do.
		error = spl_kstat_registry(RegistryPath, osx_kstat_ksp);
		if (error == 0) goto out;

		error = KSTAT_UPDATE(osx_kstat_ksp, KSTAT_WRITE);

	out:
		KSTAT_EXIT(osx_kstat_ksp);
	}

	return 0;
}

void kstat_osx_fini(void)
{
    if (osx_kstat_ksp != NULL) {
        kstat_delete(osx_kstat_ksp);
        osx_kstat_ksp = NULL;
    }
}
