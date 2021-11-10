/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/dmu.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/nvpair.h>
#include <sys/mount.h>
#include <sys/taskqueue.h>
#include <sys/sdt.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/dsl_scan.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_userhold.h>
#include <sys/zfeature.h>
#include <sys/zcp.h>
#include <sys/zio_checksum.h>
#include <sys/vdev_removal.h>
#include <sys/dsl_crypt.h>

#include <sys/zfs_ioctl_compat.h>
#include <sys/zfs_context.h>

#include <sys/arc_impl.h>
#include <sys/dsl_pool.h>


/* BEGIN CSTYLED */
SYSCTL_DECL(_vfs_zfs);
SYSCTL_NODE(_vfs_zfs, OID_AUTO, arc, CTLFLAG_RW, 0, "ZFS adaptive replacement cache");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, condense, CTLFLAG_RW, 0, "ZFS condense");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, dbuf, CTLFLAG_RW, 0, "ZFS disk buf cache");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, dbuf_cache, CTLFLAG_RW, 0, "ZFS disk buf cache");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, deadman, CTLFLAG_RW, 0, "ZFS deadman");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, dedup, CTLFLAG_RW, 0, "ZFS dedup");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, l2arc, CTLFLAG_RW, 0, "ZFS l2arc");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, livelist, CTLFLAG_RW, 0, "ZFS livelist");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, lua, CTLFLAG_RW, 0, "ZFS lua");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, metaslab, CTLFLAG_RW, 0, "ZFS metaslab");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, mg, CTLFLAG_RW, 0, "ZFS metaslab group");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, multihost, CTLFLAG_RW, 0, "ZFS multihost protection");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, prefetch, CTLFLAG_RW, 0, "ZFS prefetch");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, reconstruct, CTLFLAG_RW, 0, "ZFS reconstruct");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, recv, CTLFLAG_RW, 0, "ZFS receive");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, send, CTLFLAG_RW, 0, "ZFS send");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, spa, CTLFLAG_RW, 0, "ZFS space allocation");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, trim, CTLFLAG_RW, 0, "ZFS TRIM");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, txg, CTLFLAG_RW, 0, "ZFS transaction group");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, vdev, CTLFLAG_RW, 0, "ZFS VDEV");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, vnops, CTLFLAG_RW, 0, "ZFS VNOPS");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, zevent, CTLFLAG_RW, 0, "ZFS event");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, zil, CTLFLAG_RW, 0, "ZFS ZIL");
SYSCTL_NODE(_vfs_zfs, OID_AUTO, zio, CTLFLAG_RW, 0, "ZFS ZIO");

SYSCTL_NODE(_vfs_zfs_livelist, OID_AUTO, condense, CTLFLAG_RW, 0,
    "ZFS livelist condense");
SYSCTL_NODE(_vfs_zfs_vdev, OID_AUTO, cache, CTLFLAG_RW, 0, "ZFS VDEV Cache");
SYSCTL_NODE(_vfs_zfs_vdev, OID_AUTO, file, CTLFLAG_RW, 0, "ZFS VDEV file");
SYSCTL_NODE(_vfs_zfs_vdev, OID_AUTO, mirror, CTLFLAG_RD, 0,
    "ZFS VDEV mirror");

SYSCTL_DECL(_vfs_zfs_version);
SYSCTL_CONST_STRING(_vfs_zfs_version, OID_AUTO, module, CTLFLAG_RD,
    (ZFS_META_VERSION "-" ZFS_META_RELEASE), "OpenZFS module version");

extern arc_state_t ARC_anon;
extern arc_state_t ARC_mru;
extern arc_state_t ARC_mru_ghost;
extern arc_state_t ARC_mfu;
extern arc_state_t ARC_mfu_ghost;
extern arc_state_t ARC_l2c_only;

/*
 * minimum lifespan of a prefetch block in clock ticks
 * (initialized in arc_init())
 */

/* arc.c */

int
param_set_arc_max(SYSCTL_HANDLER_ARGS)
{
	uint64_t val;
	int err;

	val = zfs_arc_max;
	err = sysctl_handle_long(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (SET_ERROR(err));

	if (val != 0 && (val < MIN_ARC_MAX || val <= arc_c_min ||
	    val >= arc_all_memory()))
		return (SET_ERROR(EINVAL));

	zfs_arc_max = val;
	arc_tuning_update(B_TRUE);

	/* Update the sysctl to the tuned value */
	if (val != 0)
		zfs_arc_max = arc_c_max;

	return (0);
}

int
param_set_arc_min(SYSCTL_HANDLER_ARGS)
{
	uint64_t val;
	int err;

	val = zfs_arc_min;
	err = sysctl_handle_64(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (SET_ERROR(err));

	if (val != 0 && (val < 2ULL << SPA_MAXBLOCKSHIFT || val > arc_c_max))
		return (SET_ERROR(EINVAL));

	zfs_arc_min = val;
	arc_tuning_update(B_TRUE);

	/* Update the sysctl to the tuned value */
	if (val != 0)
		zfs_arc_min = arc_c_min;

	return (0);
}

/* legacy compat */
extern uint64_t l2arc_write_max;	/* def max write size */
extern uint64_t l2arc_write_boost;	/* extra warmup write */
extern uint64_t l2arc_headroom;		/* # of dev writes */
extern uint64_t l2arc_headroom_boost;
extern uint64_t l2arc_feed_secs;	/* interval seconds */
extern uint64_t l2arc_feed_min_ms;	/* min interval msecs */
extern int l2arc_noprefetch;			/* don't cache prefetch bufs */
extern int l2arc_feed_again;			/* turbo warmup */
extern int l2arc_norw;			/* no reads during writes */

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, l2arc_write_max, CTLFLAG_RW,
    &l2arc_write_max, 0, "max write size (LEGACY)");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, l2arc_write_boost, CTLFLAG_RW,
    &l2arc_write_boost, 0, "extra write during warmup (LEGACY)");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, l2arc_headroom, CTLFLAG_RW,
    &l2arc_headroom, 0, "number of dev writes (LEGACY)");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, l2arc_feed_secs, CTLFLAG_RW,
    &l2arc_feed_secs, 0, "interval seconds (LEGACY)");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, l2arc_feed_min_ms, CTLFLAG_RW,
    &l2arc_feed_min_ms, 0, "min interval milliseconds (LEGACY)");

SYSCTL_INT(_vfs_zfs, OID_AUTO, l2arc_noprefetch, CTLFLAG_RW,
    &l2arc_noprefetch, 0, "don't cache prefetch bufs (LEGACY)");
SYSCTL_INT(_vfs_zfs, OID_AUTO, l2arc_feed_again, CTLFLAG_RW,
    &l2arc_feed_again, 0, "turbo warmup (LEGACY)");
SYSCTL_INT(_vfs_zfs, OID_AUTO, l2arc_norw, CTLFLAG_RW,
    &l2arc_norw, 0, "no reads during writes (LEGACY)");
#if 0
extern int zfs_compressed_arc_enabled;
SYSCTL_INT(_vfs_zfs, OID_AUTO, compressed_arc_enabled, CTLFLAG_RW,
    &zfs_compressed_arc_enabled, 1, "compressed arc buffers (LEGACY)");
#endif

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, anon_size, CTLFLAG_RD,
    &ARC_anon.arcs_size.rc_count, 0, "size of anonymous state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, anon_metadata_esize, CTLFLAG_RD,
    &ARC_anon.arcs_esize[ARC_BUFC_METADATA].rc_count, 0,
    "size of anonymous state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, anon_data_esize, CTLFLAG_RD,
    &ARC_anon.arcs_esize[ARC_BUFC_DATA].rc_count, 0,
    "size of anonymous state");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mru_size, CTLFLAG_RD,
    &ARC_mru.arcs_size.rc_count, 0, "size of mru state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mru_metadata_esize, CTLFLAG_RD,
    &ARC_mru.arcs_esize[ARC_BUFC_METADATA].rc_count, 0,
    "size of metadata in mru state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mru_data_esize, CTLFLAG_RD,
    &ARC_mru.arcs_esize[ARC_BUFC_DATA].rc_count, 0,
    "size of data in mru state");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mru_ghost_size, CTLFLAG_RD,
    &ARC_mru_ghost.arcs_size.rc_count, 0, "size of mru ghost state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mru_ghost_metadata_esize, CTLFLAG_RD,
    &ARC_mru_ghost.arcs_esize[ARC_BUFC_METADATA].rc_count, 0,
    "size of metadata in mru ghost state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mru_ghost_data_esize, CTLFLAG_RD,
    &ARC_mru_ghost.arcs_esize[ARC_BUFC_DATA].rc_count, 0,
    "size of data in mru ghost state");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mfu_size, CTLFLAG_RD,
    &ARC_mfu.arcs_size.rc_count, 0, "size of mfu state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mfu_metadata_esize, CTLFLAG_RD,
    &ARC_mfu.arcs_esize[ARC_BUFC_METADATA].rc_count, 0,
    "size of metadata in mfu state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mfu_data_esize, CTLFLAG_RD,
    &ARC_mfu.arcs_esize[ARC_BUFC_DATA].rc_count, 0,
    "size of data in mfu state");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mfu_ghost_size, CTLFLAG_RD,
    &ARC_mfu_ghost.arcs_size.rc_count, 0, "size of mfu ghost state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mfu_ghost_metadata_esize, CTLFLAG_RD,
    &ARC_mfu_ghost.arcs_esize[ARC_BUFC_METADATA].rc_count, 0,
    "size of metadata in mfu ghost state");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, mfu_ghost_data_esize, CTLFLAG_RD,
    &ARC_mfu_ghost.arcs_esize[ARC_BUFC_DATA].rc_count, 0,
    "size of data in mfu ghost state");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, l2c_only_size, CTLFLAG_RD,
    &ARC_l2c_only.arcs_size.rc_count, 0, "size of mru state");

static int
sysctl_vfs_zfs_arc_no_grow_shift(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = arc_no_grow_shift;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

        if (val < 0 || val >= arc_shrink_shift)
		return (EINVAL);

	arc_no_grow_shift = val;
	return (0);
}

SYSCTL_PROC(_vfs_zfs, OID_AUTO, arc_no_grow_shift,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE, NULL, sizeof (int),
    sysctl_vfs_zfs_arc_no_grow_shift, "I",
    "log2(fraction of ARC which must be free to allow growing)");

int
param_set_arc_long(SYSCTL_HANDLER_ARGS)
{
	int err;

	err = sysctl_handle_long(oidp, arg1, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	arc_tuning_update(B_TRUE);

	return (0);
}

int
param_set_arc_int(SYSCTL_HANDLER_ARGS)
{
	int err;

	err = sysctl_handle_int(oidp, arg1, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	arc_tuning_update(B_TRUE);

	return (0);
}

SYSCTL_PROC(_vfs_zfs, OID_AUTO, arc_min,
    CTLTYPE_ULONG | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    &zfs_arc_min, sizeof (zfs_arc_min), param_set_arc_min, "LU",
    "min arc size (LEGACY)");
SYSCTL_PROC(_vfs_zfs, OID_AUTO, arc_max,
    CTLTYPE_ULONG | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    &zfs_arc_max, sizeof (zfs_arc_max), param_set_arc_max, "LU",
    "max arc size (LEGACY)");

/* dbuf.c */


/* dmu.c */

/* dmu_zfetch.c */
SYSCTL_NODE(_vfs_zfs, OID_AUTO, zfetch, CTLFLAG_RW, 0, "ZFS ZFETCH (LEGACY)");

/* max bytes to prefetch per stream (default 8MB) */
extern uint32_t	zfetch_max_distance;
SYSCTL_UINT(_vfs_zfs_zfetch, OID_AUTO, max_distance, CTLFLAG_RWTUN,
    &zfetch_max_distance, 0, "Max bytes to prefetch per stream (LEGACY)");

/* max bytes to prefetch indirects for per stream (default 64MB) */
extern uint32_t	zfetch_max_idistance;
SYSCTL_UINT(_vfs_zfs_zfetch, OID_AUTO, max_idistance, CTLFLAG_RWTUN,
    &zfetch_max_idistance, 0,
    "Max bytes to prefetch indirects for per stream (LEGACY)");

/* dsl_pool.c */

/* dnode.c */
extern int zfs_default_bs;
SYSCTL_INT(_vfs_zfs, OID_AUTO, default_bs, CTLFLAG_RWTUN,
    &zfs_default_bs, 0, "Default dnode block shift");

extern int zfs_default_ibs;
SYSCTL_INT(_vfs_zfs, OID_AUTO, default_ibs, CTLFLAG_RWTUN,
    &zfs_default_ibs, 0, "Default dnode indirect block shift");


/* dsl_scan.c */

/* metaslab.c */

/*
 * In pools where the log space map feature is not enabled we touch
 * multiple metaslabs (and their respective space maps) with each
 * transaction group. Thus, we benefit from having a small space map
 * block size since it allows us to issue more I/O operations scattered
 * around the disk. So a sane default for the space map block size
 * is 8~16K.
 */
extern int zfs_metaslab_sm_blksz_no_log;
SYSCTL_INT(_vfs_zfs_metaslab, OID_AUTO, sm_blksz_no_log, CTLFLAG_RDTUN,
    &zfs_metaslab_sm_blksz_no_log, 0,
    "Block size for space map in pools with log space map disabled.  "
    "Power of 2 and greater than 4096.");

/*
 * When the log space map feature is enabled, we accumulate a lot of
 * changes per metaslab that are flushed once in a while so we benefit
 * from a bigger block size like 128K for the metaslab space maps.
 */
extern int zfs_metaslab_sm_blksz_with_log;
SYSCTL_INT(_vfs_zfs_metaslab, OID_AUTO, sm_blksz_with_log, CTLFLAG_RDTUN,
    &zfs_metaslab_sm_blksz_with_log, 0,
    "Block size for space map in pools with log space map enabled.  "
    "Power of 2 and greater than 4096.");

/*
 * The in-core space map representation is more compact than its on-disk form.
 * The zfs_condense_pct determines how much more compact the in-core
 * space map representation must be before we compact it on-disk.
 * Values should be greater than or equal to 100.
 */
extern int zfs_condense_pct;
SYSCTL_INT(_vfs_zfs, OID_AUTO, condense_pct, CTLFLAG_RWTUN,
    &zfs_condense_pct, 0,
    "Condense on-disk spacemap when it is more than this many percents"
    " of in-memory counterpart");

extern int zfs_remove_max_segment;
SYSCTL_INT(_vfs_zfs, OID_AUTO, remove_max_segment, CTLFLAG_RWTUN,
    &zfs_remove_max_segment, 0, "Largest contiguous segment ZFS will attempt to"
    " allocate when removing a device");

extern int zfs_removal_suspend_progress;
SYSCTL_INT(_vfs_zfs, OID_AUTO, removal_suspend_progress, CTLFLAG_RWTUN,
    &zfs_removal_suspend_progress, 0, "Ensures certain actions can happen while"
    " in the middle of a removal");


/*
 * Minimum size which forces the dynamic allocator to change
 * it's allocation strategy.  Once the space map cannot satisfy
 * an allocation of this size then it switches to using more
 * aggressive strategy (i.e search by size rather than offset).
 */
extern uint64_t metaslab_df_alloc_threshold;
SYSCTL_QUAD(_vfs_zfs_metaslab, OID_AUTO, df_alloc_threshold, CTLFLAG_RWTUN,
    &metaslab_df_alloc_threshold, 0,
    "Minimum size which forces the dynamic allocator to change it's allocation strategy");

/*
 * The minimum free space, in percent, which must be available
 * in a space map to continue allocations in a first-fit fashion.
 * Once the space map's free space drops below this level we dynamically
 * switch to using best-fit allocations.
 */
extern int metaslab_df_free_pct;
SYSCTL_INT(_vfs_zfs_metaslab, OID_AUTO, df_free_pct, CTLFLAG_RWTUN,
    &metaslab_df_free_pct, 0,
    "The minimum free space, in percent, which must be available in a "
    "space map to continue allocations in a first-fit fashion");

/*
 * Percentage of all cpus that can be used by the metaslab taskq.
 */
extern int metaslab_load_pct;
SYSCTL_INT(_vfs_zfs_metaslab, OID_AUTO, load_pct, CTLFLAG_RWTUN,
    &metaslab_load_pct, 0,
    "Percentage of cpus that can be used by the metaslab taskq");

/*
 * Max number of metaslabs per group to preload.
 */
extern int metaslab_preload_limit;
SYSCTL_INT(_vfs_zfs_metaslab, OID_AUTO, preload_limit, CTLFLAG_RWTUN,
    &metaslab_preload_limit, 0,
    "Max number of metaslabs per group to preload");

/* spa.c */
extern int zfs_ccw_retry_interval;
SYSCTL_INT(_vfs_zfs, OID_AUTO, ccw_retry_interval, CTLFLAG_RWTUN,
    &zfs_ccw_retry_interval, 0,
    "Configuration cache file write, retry after failure, interval (seconds)");

extern uint64_t zfs_max_missing_tvds_cachefile;
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, max_missing_tvds_cachefile, CTLFLAG_RWTUN,
    &zfs_max_missing_tvds_cachefile, 0,
    "allow importing pools with missing top-level vdevs in cache file");

extern uint64_t zfs_max_missing_tvds_scan;
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, max_missing_tvds_scan, CTLFLAG_RWTUN,
    &zfs_max_missing_tvds_scan, 0,
    "allow importing pools with missing top-level vdevs during scan");

/* spa_misc.c */
extern int zfs_flags;
static int
sysctl_vfs_zfs_debug_flags(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = zfs_flags;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	/*
	 * ZFS_DEBUG_MODIFY must be enabled prior to boot so all
	 * arc buffers in the system have the necessary additional
	 * checksum data.  However, it is safe to disable at any
	 * time.
	 */
	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		val &= ~ZFS_DEBUG_MODIFY;
	zfs_flags = val;

	return (0);
}

SYSCTL_PROC(_vfs_zfs, OID_AUTO, debugflags,
    CTLTYPE_UINT | CTLFLAG_MPSAFE | CTLFLAG_RWTUN, NULL, 0,
    sysctl_vfs_zfs_debug_flags, "IU", "Debug flags for ZFS testing.");

int
param_set_deadman_synctime(SYSCTL_HANDLER_ARGS)
{
	unsigned long val;
	int err;

	val = zfs_deadman_synctime_ms;
	err = sysctl_handle_long(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);
	zfs_deadman_synctime_ms = val;

	spa_set_deadman_synctime(MSEC2NSEC(zfs_deadman_synctime_ms));

	return (0);
}

int
param_set_deadman_ziotime(SYSCTL_HANDLER_ARGS)
{
	unsigned long val;
	int err;

	val = zfs_deadman_ziotime_ms;
	err = sysctl_handle_long(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);
	zfs_deadman_ziotime_ms = val;

	spa_set_deadman_ziotime(MSEC2NSEC(zfs_deadman_synctime_ms));

	return (0);
}

int
param_set_deadman_failmode(SYSCTL_HANDLER_ARGS)
{
	char buf[16];
	int rc;

	if (req->newptr == NULL)
		strlcpy(buf, zfs_deadman_failmode, sizeof (buf));

	rc = sysctl_handle_string(oidp, buf, sizeof (buf), req);
	if (rc || req->newptr == NULL)
		return (rc);
	if (strcmp(buf, zfs_deadman_failmode) == 0)
		return (0);
	if (!strcmp(buf,  "wait"))
		zfs_deadman_failmode = "wait";
	if (!strcmp(buf,  "continue"))
		zfs_deadman_failmode = "continue";
	if (!strcmp(buf,  "panic"))
		zfs_deadman_failmode = "panic";

	return (-param_set_deadman_failmode_common(buf));
}


/* spacemap.c */
extern int space_map_ibs;
SYSCTL_INT(_vfs_zfs, OID_AUTO, space_map_ibs, CTLFLAG_RWTUN,
    &space_map_ibs, 0, "Space map indirect block shift");


/* vdev.c */
int
param_set_min_auto_ashift(SYSCTL_HANDLER_ARGS)
{
	uint64_t val;
	int err;

	val = zfs_vdev_min_auto_ashift;
	err = sysctl_handle_64(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (SET_ERROR(err));

	if (val < ASHIFT_MIN || val > zfs_vdev_max_auto_ashift)
		return (SET_ERROR(EINVAL));

	zfs_vdev_min_auto_ashift = val;

	return (0);
}

int
param_set_max_auto_ashift(SYSCTL_HANDLER_ARGS)
{
	uint64_t val;
	int err;

	val = zfs_vdev_max_auto_ashift;
	err = sysctl_handle_64(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (SET_ERROR(err));

	if (val > ASHIFT_MAX || val < zfs_vdev_min_auto_ashift)
		return (SET_ERROR(EINVAL));

	zfs_vdev_max_auto_ashift = val;

	return (0);
}

SYSCTL_PROC(_vfs_zfs, OID_AUTO, min_auto_ashift,
    CTLTYPE_U64 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    &zfs_vdev_min_auto_ashift, sizeof (zfs_vdev_min_auto_ashift),
    param_set_min_auto_ashift, "QU",
    "Min ashift used when creating new top-level vdev. (LEGACY)");
SYSCTL_PROC(_vfs_zfs, OID_AUTO, max_auto_ashift,
    CTLTYPE_U64 | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    &zfs_vdev_max_auto_ashift, sizeof (zfs_vdev_max_auto_ashift),
    param_set_max_auto_ashift, "QU",
    "Max ashift used when optimizing for logical -> physical sector size on "
    "new top-level vdevs. (LEGACY)");

/*
 * Since the DTL space map of a vdev is not expected to have a lot of
 * entries, we default its block size to 4K.
 */
extern int zfs_vdev_dtl_sm_blksz;
SYSCTL_INT(_vfs_zfs, OID_AUTO, dtl_sm_blksz, CTLFLAG_RDTUN,
    &zfs_vdev_dtl_sm_blksz, 0,
    "Block size for DTL space map.  Power of 2 and greater than 4096.");

/*
 * vdev-wide space maps that have lots of entries written to them at
 * the end of each transaction can benefit from a higher I/O bandwidth
 * (e.g. vdev_obsolete_sm), thus we default their block size to 128K.
 */
extern int zfs_vdev_standard_sm_blksz;
SYSCTL_INT(_vfs_zfs, OID_AUTO, standard_sm_blksz, CTLFLAG_RDTUN,
    &zfs_vdev_standard_sm_blksz, 0,
    "Block size for standard space map.  Power of 2 and greater than 4096.");

extern int vdev_validate_skip;
SYSCTL_INT(_vfs_zfs, OID_AUTO, validate_skip, CTLFLAG_RDTUN,
    &vdev_validate_skip, 0,
    "Enable to bypass vdev_validate().");


/* vdev_cache.c */

/* vdev_mirror.c */
/*
 * The load configuration settings below are tuned by default for
 * the case where all devices are of the same rotational type.
 *
 * If there is a mixture of rotating and non-rotating media, setting
 * non_rotating_seek_inc to 0 may well provide better results as it
 * will direct more reads to the non-rotating vdevs which are more
 * likely to have a higher performance.
 */


/* vdev_queue.c */
#define	ZFS_VDEV_QUEUE_KNOB_MIN(name)					\
extern uint32_t zfs_vdev_ ## name ## _min_active;				\
SYSCTL_UINT(_vfs_zfs_vdev, OID_AUTO, name ## _min_active, CTLFLAG_RWTUN,\
    &zfs_vdev_ ## name ## _min_active, 0,				\
    "Initial number of I/O requests of type " #name			\
    " active for each device");

#define	ZFS_VDEV_QUEUE_KNOB_MAX(name)					\
extern uint32_t zfs_vdev_ ## name ## _max_active;				\
SYSCTL_UINT(_vfs_zfs_vdev, OID_AUTO, name ## _max_active, CTLFLAG_RWTUN, \
    &zfs_vdev_ ## name ## _max_active, 0,				\
    "Maximum number of I/O requests of type " #name			\
    " active for each device");


#undef ZFS_VDEV_QUEUE_KNOB

extern uint32_t zfs_vdev_max_active;
SYSCTL_UINT(_vfs_zfs, OID_AUTO, top_maxinflight, CTLFLAG_RWTUN,
    &zfs_vdev_max_active, 0,
    "The maximum number of I/Os of all types active for each device. (LEGACY)");

extern int zfs_vdev_def_queue_depth;
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, def_queue_depth, CTLFLAG_RWTUN,
    &zfs_vdev_def_queue_depth, 0,
    "Default queue depth for each allocator");

/*extern uint64_t zfs_multihost_history;
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, multihost_history, CTLFLAG_RWTUN,
    &zfs_multihost_history, 0,
    "Historical staticists for the last N multihost updates");*/

#ifdef notyet
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, trim_on_init, CTLFLAG_RW,
    &vdev_trim_on_init, 0, "Enable/disable full vdev trim on initialisation");
#endif


/* zio.c */
#if defined(__LP64__)
int zio_use_uma = 1;
#else
int zio_use_uma = 0;
#endif

SYSCTL_INT(_vfs_zfs_zio, OID_AUTO, use_uma, CTLFLAG_RDTUN, &zio_use_uma, 0,
    "Use uma(9) for ZIO allocations");
SYSCTL_INT(_vfs_zfs_zio, OID_AUTO, exclude_metadata, CTLFLAG_RDTUN, &zio_exclude_metadata, 0,
    "Exclude metadata buffers from dumps as well");

int
param_set_slop_shift(SYSCTL_HANDLER_ARGS)
{
	int val;
	int err;

	val = *(int *)arg1;

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	if (val < 1 || val > 31)
		return (EINVAL);

	*(int *)arg1 = val;

	return (0);
}

int
param_set_multihost_interval(SYSCTL_HANDLER_ARGS)
{
	int err;

	err = sysctl_handle_long(oidp, arg1, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	if (spa_mode_global != SPA_MODE_UNINIT)
		mmp_signal_all_threads();

	return (0);
}
