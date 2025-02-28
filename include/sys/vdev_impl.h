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
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2023, Klara Inc.
 */

#ifndef _SYS_VDEV_IMPL_H
#define	_SYS_VDEV_IMPL_H

#include <sys/avl.h>
#include <sys/bpobj.h>
#include <sys/dmu.h>
#include <sys/metaslab.h>
#include <sys/nvpair.h>
#include <sys/space_map.h>
#include <sys/vdev.h>
#include <sys/uberblock_impl.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/vdev_indirect_births.h>
#include <sys/vdev_rebuild.h>
#include <sys/vdev_removal.h>
#include <sys/zfs_ratelimit.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Virtual device descriptors.
 *
 * All storage pool operations go through the virtual device framework,
 * which provides data replication and I/O scheduling.
 */

/*
 * Forward declarations that lots of things need.
 */
typedef struct vdev_queue vdev_queue_t;
struct abd;

extern uint_t zfs_vdev_queue_depth_pct;
extern uint_t zfs_vdev_def_queue_depth;
extern uint_t zfs_vdev_async_write_max_active;

/*
 * Virtual device operations
 */
typedef int	vdev_init_func_t(spa_t *spa, nvlist_t *nv, void **tsd);
typedef void	vdev_kobj_post_evt_func_t(vdev_t *vd);
typedef void	vdev_fini_func_t(vdev_t *vd);
typedef int	vdev_open_func_t(vdev_t *vd, uint64_t *size, uint64_t *max_size,
    uint64_t *ashift, uint64_t *pshift);
typedef void	vdev_close_func_t(vdev_t *vd);
typedef uint64_t vdev_asize_func_t(vdev_t *vd, uint64_t psize, uint64_t txg);
typedef uint64_t vdev_min_asize_func_t(vdev_t *vd);
typedef uint64_t vdev_min_alloc_func_t(vdev_t *vd);
typedef void	vdev_io_start_func_t(zio_t *zio);
typedef void	vdev_io_done_func_t(zio_t *zio);
typedef void	vdev_state_change_func_t(vdev_t *vd, int, int);
typedef boolean_t vdev_need_resilver_func_t(vdev_t *vd, const dva_t *dva,
    size_t psize, uint64_t phys_birth);
typedef void	vdev_hold_func_t(vdev_t *vd);
typedef void	vdev_rele_func_t(vdev_t *vd);

typedef void	vdev_remap_cb_t(uint64_t inner_offset, vdev_t *vd,
    uint64_t offset, uint64_t size, void *arg);
typedef void	vdev_remap_func_t(vdev_t *vd, uint64_t offset, uint64_t size,
    vdev_remap_cb_t callback, void *arg);
/*
 * Given a target vdev, translates the logical range "in" to the physical
 * range "res"
 */
typedef void vdev_xlation_func_t(vdev_t *cvd, const zfs_range_seg64_t *logical,
    zfs_range_seg64_t *physical, zfs_range_seg64_t *remain);
typedef uint64_t vdev_rebuild_asize_func_t(vdev_t *vd, uint64_t start,
    uint64_t size, uint64_t max_segment);
typedef void vdev_metaslab_init_func_t(vdev_t *vd, uint64_t *startp,
    uint64_t *sizep);
typedef void vdev_config_generate_func_t(vdev_t *vd, nvlist_t *nv);
typedef uint64_t vdev_nparity_func_t(vdev_t *vd);
typedef uint64_t vdev_ndisks_func_t(vdev_t *vd);

typedef const struct vdev_ops {
	vdev_init_func_t		*vdev_op_init;
	vdev_fini_func_t		*vdev_op_fini;
	vdev_open_func_t		*vdev_op_open;
	vdev_close_func_t		*vdev_op_close;
	vdev_asize_func_t		*vdev_op_asize;
	vdev_min_asize_func_t		*vdev_op_min_asize;
	vdev_min_alloc_func_t		*vdev_op_min_alloc;
	vdev_io_start_func_t		*vdev_op_io_start;
	vdev_io_done_func_t		*vdev_op_io_done;
	vdev_state_change_func_t	*vdev_op_state_change;
	vdev_need_resilver_func_t	*vdev_op_need_resilver;
	vdev_hold_func_t		*vdev_op_hold;
	vdev_rele_func_t		*vdev_op_rele;
	vdev_remap_func_t		*vdev_op_remap;
	vdev_xlation_func_t		*vdev_op_xlate;
	vdev_rebuild_asize_func_t	*vdev_op_rebuild_asize;
	vdev_metaslab_init_func_t	*vdev_op_metaslab_init;
	vdev_config_generate_func_t	*vdev_op_config_generate;
	vdev_nparity_func_t		*vdev_op_nparity;
	vdev_ndisks_func_t		*vdev_op_ndisks;
	vdev_kobj_post_evt_func_t	*vdev_op_kobj_evt_post;
	char				vdev_op_type[16];
	boolean_t			vdev_op_leaf;
} vdev_ops_t;

/*
 * Virtual device properties
 */
typedef union vdev_queue_class {
	struct {
		ulong_t 	vqc_list_numnodes;
		list_t		vqc_list;
	};
	avl_tree_t	vqc_tree;
} vdev_queue_class_t;

struct vdev_queue {
	vdev_t		*vq_vdev;
	vdev_queue_class_t vq_class[ZIO_PRIORITY_NUM_QUEUEABLE];
	avl_tree_t	vq_read_offset_tree;
	avl_tree_t	vq_write_offset_tree;
	uint64_t	vq_last_offset;
	zio_priority_t	vq_last_prio;	/* Last sent I/O priority. */
	uint32_t	vq_cqueued;	/* Classes with queued I/Os. */
	uint32_t	vq_cactive[ZIO_PRIORITY_NUM_QUEUEABLE];
	uint32_t	vq_active;	/* Number of active I/Os. */
	uint32_t	vq_ia_active;	/* Active interactive I/Os. */
	uint32_t	vq_nia_credit;	/* Non-interactive I/Os credit. */
	list_t		vq_active_list;	/* List of active I/Os. */
	hrtime_t	vq_io_complete_ts; /* time last i/o completed */
	hrtime_t	vq_io_delta_ts;
	zio_t		vq_io_search; /* used as local for stack reduction */
	kmutex_t	vq_lock;
};

typedef enum vdev_alloc_bias {
	VDEV_BIAS_NONE,
	VDEV_BIAS_LOG,		/* dedicated to ZIL data (SLOG) */
	VDEV_BIAS_SPECIAL,	/* dedicated to ddt, metadata, and small blks */
	VDEV_BIAS_DEDUP		/* dedicated to dedup metadata */
} vdev_alloc_bias_t;


/*
 * On-disk indirect vdev state.
 *
 * An indirect vdev is described exclusively in the MOS config of a pool.
 * The config for an indirect vdev includes several fields, which are
 * accessed in memory by a vdev_indirect_config_t.
 */
typedef struct vdev_indirect_config {
	/*
	 * Object (in MOS) which contains the indirect mapping. This object
	 * contains an array of vdev_indirect_mapping_entry_phys_t ordered by
	 * vimep_src. The bonus buffer for this object is a
	 * vdev_indirect_mapping_phys_t. This object is allocated when a vdev
	 * removal is initiated.
	 *
	 * Note that this object can be empty if none of the data on the vdev
	 * has been copied yet.
	 */
	uint64_t	vic_mapping_object;

	/*
	 * Object (in MOS) which contains the birth times for the mapping
	 * entries. This object contains an array of
	 * vdev_indirect_birth_entry_phys_t sorted by vibe_offset. The bonus
	 * buffer for this object is a vdev_indirect_birth_phys_t. This object
	 * is allocated when a vdev removal is initiated.
	 *
	 * Note that this object can be empty if none of the vdev has yet been
	 * copied.
	 */
	uint64_t	vic_births_object;

	/*
	 * This is the vdev ID which was removed previous to this vdev, or
	 * UINT64_MAX if there are no previously removed vdevs.
	 */
	uint64_t	vic_prev_indirect_vdev;
} vdev_indirect_config_t;

/*
 * Virtual device descriptor
 */
struct vdev {
	/*
	 * Common to all vdev types.
	 */
	uint64_t	vdev_id;	/* child number in vdev parent	*/
	uint64_t	vdev_guid;	/* unique ID for this vdev	*/
	uint64_t	vdev_guid_sum;	/* self guid + all child guids	*/
	uint64_t	vdev_orig_guid;	/* orig. guid prior to remove	*/
	uint64_t	vdev_asize;	/* allocatable device capacity	*/
	uint64_t	vdev_min_asize;	/* min acceptable asize		*/
	uint64_t	vdev_max_asize;	/* max acceptable asize		*/
	uint64_t	vdev_ashift;	/* block alignment shift	*/

	/*
	 * Logical block alignment shift
	 *
	 * The smallest sized/aligned I/O supported by the device.
	 */
	uint64_t	vdev_logical_ashift;
	/*
	 * Physical block alignment shift
	 *
	 * The device supports logical I/Os with vdev_logical_ashift
	 * size/alignment, but optimum performance will be achieved by
	 * aligning/sizing requests to vdev_physical_ashift.  Smaller
	 * requests may be inflated or incur device level read-modify-write
	 * operations.
	 *
	 * May be 0 to indicate no preference (i.e. use vdev_logical_ashift).
	 */
	uint64_t	vdev_physical_ashift;
	uint64_t	vdev_state;	/* see VDEV_STATE_* #defines	*/
	uint64_t	vdev_prevstate;	/* used when reopening a vdev	*/
	vdev_ops_t	*vdev_ops;	/* vdev operations		*/
	spa_t		*vdev_spa;	/* spa for this vdev		*/
	void		*vdev_tsd;	/* type-specific data		*/
	vdev_t		*vdev_top;	/* top-level vdev		*/
	vdev_t		*vdev_parent;	/* parent vdev			*/
	vdev_t		**vdev_child;	/* array of children		*/
	uint64_t	vdev_children;	/* number of children		*/
	vdev_stat_t	vdev_stat;	/* virtual device statistics	*/
	vdev_stat_ex_t	vdev_stat_ex;	/* extended statistics		*/
	boolean_t	vdev_expanding;	/* expand the vdev?		*/
	boolean_t	vdev_reopening;	/* reopen in progress?		*/
	boolean_t	vdev_nonrot;	/* true if solid state		*/
	int		vdev_load_error; /* error on last load		*/
	int		vdev_open_error; /* error on last open		*/
	int		vdev_validate_error; /* error on last validate	*/
	kthread_t	*vdev_open_thread; /* thread opening children	*/
	kthread_t	*vdev_validate_thread; /* thread validating children */
	uint64_t	vdev_crtxg;	/* txg when top-level was added */
	uint64_t	vdev_root_zap;

	/*
	 * Top-level vdev state.
	 */
	uint64_t	vdev_ms_array;	/* metaslab array object	*/
	uint64_t	vdev_ms_shift;	/* metaslab size shift		*/
	uint64_t	vdev_ms_count;	/* number of metaslabs		*/
	metaslab_group_t *vdev_mg;	/* metaslab group		*/
	metaslab_group_t *vdev_log_mg;	/* embedded slog metaslab group	*/
	metaslab_t	**vdev_ms;	/* metaslab array		*/
	txg_list_t	vdev_ms_list;	/* per-txg dirty metaslab lists	*/
	txg_list_t	vdev_dtl_list;	/* per-txg dirty DTL lists	*/
	txg_node_t	vdev_txg_node;	/* per-txg dirty vdev linkage	*/
	boolean_t	vdev_remove_wanted; /* async remove wanted?	*/
	boolean_t	vdev_fault_wanted; /* async faulted wanted?	*/
	list_node_t	vdev_config_dirty_node; /* config dirty list	*/
	list_node_t	vdev_state_dirty_node; /* state dirty list	*/
	uint64_t	vdev_deflate_ratio; /* deflation ratio (x512)	*/
	uint64_t	vdev_islog;	/* is an intent log device	*/
	uint64_t	vdev_noalloc;	/* device is passivated?	*/
	uint64_t	vdev_removing;	/* device is being removed?	*/
	uint64_t	vdev_failfast;	/* device failfast setting	*/
	boolean_t	vdev_rz_expanding; /* raidz is being expanded?	*/
	boolean_t	vdev_ishole;	/* is a hole in the namespace	*/
	uint64_t	vdev_top_zap;
	vdev_alloc_bias_t vdev_alloc_bias; /* metaslab allocation bias	*/

	/* pool checkpoint related */
	space_map_t	*vdev_checkpoint_sm;	/* contains reserved blocks */

	/* Initialize related */
	boolean_t	vdev_initialize_exit_wanted;
	vdev_initializing_state_t	vdev_initialize_state;
	list_node_t	vdev_initialize_node;
	kthread_t	*vdev_initialize_thread;
	/* Protects vdev_initialize_thread and vdev_initialize_state. */
	kmutex_t	vdev_initialize_lock;
	kcondvar_t	vdev_initialize_cv;
	uint64_t	vdev_initialize_offset[TXG_SIZE];
	uint64_t	vdev_initialize_last_offset;
	/* valid while initializing */
	zfs_range_tree_t	*vdev_initialize_tree;
	uint64_t	vdev_initialize_bytes_est;
	uint64_t	vdev_initialize_bytes_done;
	uint64_t	vdev_initialize_action_time;	/* start and end time */

	/* TRIM related */
	boolean_t	vdev_trim_exit_wanted;
	boolean_t	vdev_autotrim_exit_wanted;
	vdev_trim_state_t	vdev_trim_state;
	list_node_t	vdev_trim_node;
	kmutex_t	vdev_autotrim_lock;
	kcondvar_t	vdev_autotrim_cv;
	kcondvar_t	vdev_autotrim_kick_cv;
	kthread_t	*vdev_autotrim_thread;
	/* Protects vdev_trim_thread and vdev_trim_state. */
	kmutex_t	vdev_trim_lock;
	kcondvar_t	vdev_trim_cv;
	kthread_t	*vdev_trim_thread;
	uint64_t	vdev_trim_offset[TXG_SIZE];
	uint64_t	vdev_trim_last_offset;
	uint64_t	vdev_trim_bytes_est;
	uint64_t	vdev_trim_bytes_done;
	uint64_t	vdev_trim_rate;		/* requested rate (bytes/sec) */
	uint64_t	vdev_trim_partial;	/* requested partial TRIM */
	uint64_t	vdev_trim_secure;	/* requested secure TRIM */
	uint64_t	vdev_trim_action_time;	/* start and end time */

	/* Rebuild related */
	boolean_t	vdev_rebuilding;
	boolean_t	vdev_rebuild_exit_wanted;
	boolean_t	vdev_rebuild_cancel_wanted;
	boolean_t	vdev_rebuild_reset_wanted;
	kmutex_t	vdev_rebuild_lock;
	kcondvar_t	vdev_rebuild_cv;
	kthread_t	*vdev_rebuild_thread;
	vdev_rebuild_t	vdev_rebuild_config;

	/* For limiting outstanding I/Os (initialize, TRIM) */
	kmutex_t	vdev_initialize_io_lock;
	kcondvar_t	vdev_initialize_io_cv;
	uint64_t	vdev_initialize_inflight;
	kmutex_t	vdev_trim_io_lock;
	kcondvar_t	vdev_trim_io_cv;
	uint64_t	vdev_trim_inflight[3];

	/*
	 * Values stored in the config for an indirect or removing vdev.
	 */
	vdev_indirect_config_t	vdev_indirect_config;

	/*
	 * The vdev_indirect_rwlock protects the vdev_indirect_mapping
	 * pointer from changing on indirect vdevs (when it is condensed).
	 * Note that removing (not yet indirect) vdevs have different
	 * access patterns (the mapping is not accessed from open context,
	 * e.g. from zio_read) and locking strategy (e.g. svr_lock).
	 */
	krwlock_t vdev_indirect_rwlock;
	vdev_indirect_mapping_t *vdev_indirect_mapping;
	vdev_indirect_births_t *vdev_indirect_births;

	/*
	 * In memory data structures used to manage the obsolete sm, for
	 * indirect or removing vdevs.
	 *
	 * The vdev_obsolete_segments is the in-core record of the segments
	 * that are no longer referenced anywhere in the pool (due to
	 * being freed or remapped and not referenced by any snapshots).
	 * During a sync, segments are added to vdev_obsolete_segments
	 * via vdev_indirect_mark_obsolete(); at the end of each sync
	 * pass, this is appended to vdev_obsolete_sm via
	 * vdev_indirect_sync_obsolete().  The vdev_obsolete_lock
	 * protects against concurrent modifications of vdev_obsolete_segments
	 * from multiple zio threads.
	 */
	kmutex_t	vdev_obsolete_lock;
	zfs_range_tree_t	*vdev_obsolete_segments;
	space_map_t	*vdev_obsolete_sm;

	/*
	 * Protects the vdev_scan_io_queue field itself as well as the
	 * structure's contents (when present).
	 */
	kmutex_t			vdev_scan_io_queue_lock;
	struct dsl_scan_io_queue	*vdev_scan_io_queue;

	/*
	 * Leaf vdev state.
	 */
	zfs_range_tree_t	*vdev_dtl[DTL_TYPES]; /* dirty time logs */
	space_map_t	*vdev_dtl_sm;	/* dirty time log space map	*/
	txg_node_t	vdev_dtl_node;	/* per-txg dirty DTL linkage	*/
	uint64_t	vdev_dtl_object; /* DTL object			*/
	uint64_t	vdev_psize;	/* physical device capacity	*/
	uint64_t	vdev_wholedisk;	/* true if this is a whole disk */
	uint64_t	vdev_offline;	/* persistent offline state	*/
	uint64_t	vdev_faulted;	/* persistent faulted state	*/
	uint64_t	vdev_degraded;	/* persistent degraded state	*/
	uint64_t	vdev_removed;	/* persistent removed state	*/
	uint64_t	vdev_resilver_txg; /* persistent resilvering state */
	uint64_t	vdev_rebuild_txg; /* persistent rebuilding state */
	char		*vdev_path;	/* vdev path (if any)		*/
	char		*vdev_devid;	/* vdev devid (if any)		*/
	char		*vdev_physpath;	/* vdev device path (if any)	*/
	char		*vdev_enc_sysfs_path;	/* enclosure sysfs path */
	char		*vdev_fru;	/* physical FRU location	*/
	uint64_t	vdev_not_present; /* not present during import	*/
	uint64_t	vdev_unspare;	/* unspare when resilvering done */
	boolean_t	vdev_nowritecache; /* true if flushwritecache failed */
	boolean_t	vdev_has_trim;	/* TRIM is supported		*/
	boolean_t	vdev_has_securetrim; /* secure TRIM is supported */
	boolean_t	vdev_checkremove; /* temporary online test	*/
	boolean_t	vdev_forcefault; /* force online fault		*/
	boolean_t	vdev_splitting;	/* split or repair in progress  */
	boolean_t	vdev_delayed_close; /* delayed device close?	*/
	boolean_t	vdev_tmpoffline; /* device taken offline temporarily? */
	boolean_t	vdev_detached;	/* device detached?		*/
	boolean_t	vdev_cant_read;	/* vdev is failing all reads	*/
	boolean_t	vdev_cant_write; /* vdev is failing all writes	*/
	boolean_t	vdev_isspare;	/* was a hot spare		*/
	boolean_t	vdev_isl2cache;	/* was a l2cache device		*/
	boolean_t	vdev_copy_uberblocks;  /* post expand copy uberblocks */
	boolean_t	vdev_resilver_deferred;  /* resilver deferred */
	boolean_t	vdev_kobj_flag; /* kobj event record */
	boolean_t	vdev_attaching; /* vdev attach ashift handling */
	vdev_queue_t	vdev_queue;	/* I/O deadline schedule queue	*/
	spa_aux_vdev_t	*vdev_aux;	/* for l2cache and spares vdevs	*/
	zio_t		*vdev_probe_zio; /* root of current probe	*/
	vdev_aux_t	vdev_label_aux;	/* on-disk aux state		*/
	uint64_t	vdev_leaf_zap;
	hrtime_t	vdev_mmp_pending; /* 0 if write finished	*/
	uint64_t	vdev_mmp_kstat_id;	/* to find kstat entry */
	uint64_t	vdev_expansion_time;	/* vdev's last expansion time */
	list_node_t	vdev_leaf_node;		/* leaf vdev list */

	/*
	 * For DTrace to work in userland (libzpool) context, these fields must
	 * remain at the end of the structure.  DTrace will use the kernel's
	 * CTF definition for 'struct vdev', and since the size of a kmutex_t is
	 * larger in userland, the offsets for the rest of the fields would be
	 * incorrect.
	 */
	kmutex_t	vdev_dtl_lock;	/* vdev_dtl_{map,resilver}	*/
	kmutex_t	vdev_stat_lock;	/* vdev_stat			*/
	kmutex_t	vdev_probe_lock; /* protects vdev_probe_zio	*/

	/*
	 * We rate limit ZIO delay, deadman, and checksum events, since they
	 * can flood ZED with tons of events when a drive is acting up.
	 *
	 * We also rate limit Direct I/O write verify errors, since a user might
	 * be continually manipulating a buffer that can flood ZED with tons of
	 * events.
	 */
	zfs_ratelimit_t vdev_delay_rl;
	zfs_ratelimit_t vdev_deadman_rl;
	zfs_ratelimit_t vdev_dio_verify_rl;
	zfs_ratelimit_t vdev_checksum_rl;

	/*
	 * Vdev properties for tuning ZED or zfsd
	 */
	uint64_t	vdev_checksum_n;
	uint64_t	vdev_checksum_t;
	uint64_t	vdev_io_n;
	uint64_t	vdev_io_t;
	uint64_t	vdev_slow_io_n;
	uint64_t	vdev_slow_io_t;
};

#define	VDEV_PAD_SIZE		(8 << 10)
/* 2 padding areas (vl_pad1 and vl_be) to skip */
#define	VDEV_SKIP_SIZE		VDEV_PAD_SIZE * 2
#define	VDEV_PHYS_SIZE		(112 << 10)
#define	VDEV_UBERBLOCK_RING	(128 << 10)

/*
 * MMP blocks occupy the last MMP_BLOCKS_PER_LABEL slots in the uberblock
 * ring when MMP is enabled.
 */
#define	MMP_BLOCKS_PER_LABEL	1

/* The largest uberblock we support is 8k. */
#define	MAX_UBERBLOCK_SHIFT (13)
#define	VDEV_UBERBLOCK_SHIFT(vd)	\
	MIN(MAX((vd)->vdev_top->vdev_ashift, UBERBLOCK_SHIFT), \
	    MAX_UBERBLOCK_SHIFT)
#define	VDEV_UBERBLOCK_COUNT(vd)	\
	(VDEV_UBERBLOCK_RING >> VDEV_UBERBLOCK_SHIFT(vd))
#define	VDEV_UBERBLOCK_OFFSET(vd, n)	\
	offsetof(vdev_label_t, vl_uberblock[(n) << VDEV_UBERBLOCK_SHIFT(vd)])
#define	VDEV_UBERBLOCK_SIZE(vd)		(1ULL << VDEV_UBERBLOCK_SHIFT(vd))

typedef struct vdev_phys {
	char		vp_nvlist[VDEV_PHYS_SIZE - sizeof (zio_eck_t)];
	zio_eck_t	vp_zbt;
} vdev_phys_t;

typedef enum vbe_vers {
	/*
	 * The bootenv file is stored as ascii text in the envblock.
	 * It is used by the GRUB bootloader used on Linux to store the
	 * contents of the grubenv file. The file is stored as raw ASCII,
	 * and is protected by an embedded checksum. By default, GRUB will
	 * check if the boot filesystem supports storing the environment data
	 * in a special location, and if so, will invoke filesystem specific
	 * logic to retrieve it. This can be overridden by a variable, should
	 * the user so desire.
	 */
	VB_RAW = 0,

	/*
	 * The bootenv file is converted to an nvlist and then packed into the
	 * envblock.
	 */
	VB_NVLIST = 1
} vbe_vers_t;

typedef struct vdev_boot_envblock {
	uint64_t	vbe_version;
	char		vbe_bootenv[VDEV_PAD_SIZE - sizeof (uint64_t) -
			sizeof (zio_eck_t)];
	zio_eck_t	vbe_zbt;
} vdev_boot_envblock_t;
_Static_assert(sizeof (vdev_boot_envblock_t) == VDEV_PAD_SIZE,
	"vdev_boot_envblock_t wrong size");

typedef struct vdev_label {
	char		vl_pad1[VDEV_PAD_SIZE];			/*  8K */
	vdev_boot_envblock_t	vl_be;				/*  8K */
	vdev_phys_t	vl_vdev_phys;				/* 112K	*/
	char		vl_uberblock[VDEV_UBERBLOCK_RING];	/* 128K	*/
} vdev_label_t;						/* 256K total */

/*
 * vdev_dirty() flags
 */
#define	VDD_METASLAB	0x01
#define	VDD_DTL		0x02

/* Offset of embedded boot loader region on each label */
#define	VDEV_BOOT_OFFSET	(2 * sizeof (vdev_label_t))
/*
 * Size of embedded boot loader region on each label.
 * The total size of the first two labels plus the boot area is 4MB.
 * On RAIDZ, this space is overwritten during RAIDZ expansion.
 */
#define	VDEV_BOOT_SIZE		(7ULL << 19)			/* 3.5M */

/*
 * Size of label regions at the start and end of each leaf device.
 */
#define	VDEV_LABEL_START_SIZE	(2 * sizeof (vdev_label_t) + VDEV_BOOT_SIZE)
#define	VDEV_LABEL_END_SIZE	(2 * sizeof (vdev_label_t))
#define	VDEV_LABELS		4
#define	VDEV_BEST_LABEL		VDEV_LABELS
#define	VDEV_OFFSET_IS_LABEL(vd, off)                           \
	(((off) < VDEV_LABEL_START_SIZE) ||                     \
	((off) >= ((vd)->vdev_psize - VDEV_LABEL_END_SIZE)))

#define	VDEV_ALLOC_LOAD		0
#define	VDEV_ALLOC_ADD		1
#define	VDEV_ALLOC_SPARE	2
#define	VDEV_ALLOC_L2CACHE	3
#define	VDEV_ALLOC_ROOTPOOL	4
#define	VDEV_ALLOC_SPLIT	5
#define	VDEV_ALLOC_ATTACH	6

/*
 * Allocate or free a vdev
 */
extern vdev_t *vdev_alloc_common(spa_t *spa, uint_t id, uint64_t guid,
    vdev_ops_t *ops);
extern int vdev_alloc(spa_t *spa, vdev_t **vdp, nvlist_t *config,
    vdev_t *parent, uint_t id, int alloctype);
extern void vdev_free(vdev_t *vd);

/*
 * Add or remove children and parents
 */
extern void vdev_add_child(vdev_t *pvd, vdev_t *cvd);
extern void vdev_remove_child(vdev_t *pvd, vdev_t *cvd);
extern void vdev_compact_children(vdev_t *pvd);
extern vdev_t *vdev_add_parent(vdev_t *cvd, vdev_ops_t *ops);
extern void vdev_remove_parent(vdev_t *cvd);

/*
 * vdev sync load and sync
 */
extern boolean_t vdev_log_state_valid(vdev_t *vd);
extern int vdev_load(vdev_t *vd);
extern int vdev_dtl_load(vdev_t *vd);
extern void vdev_sync(vdev_t *vd, uint64_t txg);
extern void vdev_sync_done(vdev_t *vd, uint64_t txg);
extern void vdev_dirty(vdev_t *vd, int flags, void *arg, uint64_t txg);
extern void vdev_dirty_leaves(vdev_t *vd, int flags, uint64_t txg);

/*
 * Available vdev types.
 */
extern vdev_ops_t vdev_root_ops;
extern vdev_ops_t vdev_mirror_ops;
extern vdev_ops_t vdev_replacing_ops;
extern vdev_ops_t vdev_raidz_ops;
extern vdev_ops_t vdev_draid_ops;
extern vdev_ops_t vdev_draid_spare_ops;
extern vdev_ops_t vdev_disk_ops;
extern vdev_ops_t vdev_file_ops;
extern vdev_ops_t vdev_missing_ops;
extern vdev_ops_t vdev_hole_ops;
extern vdev_ops_t vdev_spare_ops;
extern vdev_ops_t vdev_indirect_ops;

/*
 * Common size functions
 */
extern void vdev_default_xlate(vdev_t *vd, const zfs_range_seg64_t *logical_rs,
    zfs_range_seg64_t *physical_rs, zfs_range_seg64_t *remain_rs);
extern uint64_t vdev_default_asize(vdev_t *vd, uint64_t psize, uint64_t txg);
extern uint64_t vdev_default_min_asize(vdev_t *vd);
extern uint64_t vdev_get_min_asize(vdev_t *vd);
extern void vdev_set_min_asize(vdev_t *vd);
extern uint64_t vdev_get_min_alloc(vdev_t *vd);
extern uint64_t vdev_get_nparity(vdev_t *vd);
extern uint64_t vdev_get_ndisks(vdev_t *vd);

/*
 * Global variables
 */
extern int zfs_vdev_standard_sm_blksz;

/*
 * Functions from vdev_indirect.c
 */
extern void vdev_indirect_sync_obsolete(vdev_t *vd, dmu_tx_t *tx);
extern boolean_t vdev_indirect_should_condense(vdev_t *vd);
extern void spa_condense_indirect_start_sync(vdev_t *vd, dmu_tx_t *tx);
extern int vdev_obsolete_sm_object(vdev_t *vd, uint64_t *sm_obj);
extern int vdev_obsolete_counts_are_precise(vdev_t *vd, boolean_t *are_precise);

/*
 * Other miscellaneous functions
 */
int vdev_checkpoint_sm_object(vdev_t *vd, uint64_t *sm_obj);
void vdev_metaslab_group_create(vdev_t *vd);
uint64_t vdev_best_ashift(uint64_t logical, uint64_t a, uint64_t b);
#if defined(__linux__)
int param_get_raidz_impl(char *buf, zfs_kernel_param_t *kp);
#endif
int param_set_raidz_impl(ZFS_MODULE_PARAM_ARGS);

/*
 * Vdev ashift optimization tunables
 */
extern uint_t zfs_vdev_min_auto_ashift;
extern uint_t zfs_vdev_max_auto_ashift;
int param_set_min_auto_ashift(ZFS_MODULE_PARAM_ARGS);
int param_set_max_auto_ashift(ZFS_MODULE_PARAM_ARGS);

/*
 * VDEV checksum verification for Direct I/O writes
 */
extern uint_t zfs_vdev_direct_write_verify;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_IMPL_H */
