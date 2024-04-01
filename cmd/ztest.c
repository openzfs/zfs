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
 * Copyright (c) 2011, 2024 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Joyent, Inc.
 * Copyright (c) 2017, Intel Corporation.
 */

/*
 * The objective of this program is to provide a DMU/ZAP/SPA stress test
 * that runs entirely in userland, is easy to use, and easy to extend.
 *
 * The overall design of the ztest program is as follows:
 *
 * (1) For each major functional area (e.g. adding vdevs to a pool,
 *     creating and destroying datasets, reading and writing objects, etc)
 *     we have a simple routine to test that functionality.  These
 *     individual routines do not have to do anything "stressful".
 *
 * (2) We turn these simple functionality tests into a stress test by
 *     running them all in parallel, with as many threads as desired,
 *     and spread across as many datasets, objects, and vdevs as desired.
 *
 * (3) While all this is happening, we inject faults into the pool to
 *     verify that self-healing data really works.
 *
 * (4) Every time we open a dataset, we change its checksum and compression
 *     functions.  Thus even individual objects vary from block to block
 *     in which checksum they use and whether they're compressed.
 *
 * (5) To verify that we never lose on-disk consistency after a crash,
 *     we run the entire test in a child of the main process.
 *     At random times, the child self-immolates with a SIGKILL.
 *     This is the software equivalent of pulling the power cord.
 *     The parent then runs the test again, using the existing
 *     storage pool, as many times as desired. If backwards compatibility
 *     testing is enabled ztest will sometimes run the "older" version
 *     of ztest after a SIGKILL.
 *
 * (6) To verify that we don't have future leaks or temporal incursions,
 *     many of the functional tests record the transaction group number
 *     as part of their data.  When reading old data, they verify that
 *     the transaction group number is less than the current, open txg.
 *     If you add a new test, please do this if applicable.
 *
 * (7) Threads are created with a reduced stack size, for sanity checking.
 *     Therefore, it's important not to allocate huge buffers on the stack.
 *
 * When run with no arguments, ztest runs for about five minutes and
 * produces no output if successful.  To get a little bit of information,
 * specify -V.  To get more information, specify -VV, and so on.
 *
 * To turn this into an overnight stress test, use -T to specify run time.
 *
 * You can ask more vdevs [-v], datasets [-d], or threads [-t]
 * to increase the pool capacity, fanout, and overall stress level.
 *
 * Use the -k option to set the desired frequency of kills.
 *
 * When ztest invokes itself it passes all relevant information through a
 * temporary file which is mmap-ed in the child process. This allows shared
 * memory to survive the exec syscall. The ztest_shared_hdr_t struct is always
 * stored at offset 0 of this file and contains information on the size and
 * number of shared structures in the file. The information stored in this file
 * must remain backwards compatible with older versions of ztest so that
 * ztest can invoke them during backwards compatibility testing (-B).
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/vdev_draid.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_initialize.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_trim.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_scan.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_refcount.h>
#include <sys/zfeature.h>
#include <sys/dsl_userhold.h>
#include <sys/abd.h>
#include <sys/blake3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <umem.h>
#include <ctype.h>
#include <math.h>
#include <sys/fs/zfs.h>
#include <zfs_fletcher.h>
#include <libnvpair.h>
#include <libzutil.h>
#include <sys/crypto/icp.h>
#include <sys/zfs_impl.h>
#if (__GLIBC__ && !__UCLIBC__)
#include <execinfo.h> /* for backtrace() */
#endif

static int ztest_fd_data = -1;
static int ztest_fd_rand = -1;

typedef struct ztest_shared_hdr {
	uint64_t	zh_hdr_size;
	uint64_t	zh_opts_size;
	uint64_t	zh_size;
	uint64_t	zh_stats_size;
	uint64_t	zh_stats_count;
	uint64_t	zh_ds_size;
	uint64_t	zh_ds_count;
	uint64_t	zh_scratch_state_size;
} ztest_shared_hdr_t;

static ztest_shared_hdr_t *ztest_shared_hdr;

enum ztest_class_state {
	ZTEST_VDEV_CLASS_OFF,
	ZTEST_VDEV_CLASS_ON,
	ZTEST_VDEV_CLASS_RND
};

/* Dedicated RAIDZ Expansion test states */
typedef enum {
	RAIDZ_EXPAND_NONE,		/* Default is none, must opt-in	*/
	RAIDZ_EXPAND_REQUESTED,		/* The '-X' option was used	*/
	RAIDZ_EXPAND_STARTED,		/* Testing has commenced	*/
	RAIDZ_EXPAND_KILLED,		/* Reached the proccess kill	*/
	RAIDZ_EXPAND_CHECKED,		/* Pool scrub verification done	*/
} raidz_expand_test_state_t;


#define	ZO_GVARS_MAX_ARGLEN	((size_t)64)
#define	ZO_GVARS_MAX_COUNT	((size_t)10)

typedef struct ztest_shared_opts {
	char zo_pool[ZFS_MAX_DATASET_NAME_LEN];
	char zo_dir[ZFS_MAX_DATASET_NAME_LEN];
	char zo_alt_ztest[MAXNAMELEN];
	char zo_alt_libpath[MAXNAMELEN];
	uint64_t zo_vdevs;
	uint64_t zo_vdevtime;
	size_t zo_vdev_size;
	int zo_ashift;
	int zo_mirrors;
	int zo_raid_do_expand;
	int zo_raid_children;
	int zo_raid_parity;
	char zo_raid_type[8];
	int zo_draid_data;
	int zo_draid_spares;
	int zo_datasets;
	int zo_threads;
	uint64_t zo_passtime;
	uint64_t zo_killrate;
	int zo_verbose;
	int zo_init;
	uint64_t zo_time;
	uint64_t zo_maxloops;
	uint64_t zo_metaslab_force_ganging;
	raidz_expand_test_state_t zo_raidz_expand_test;
	int zo_mmp_test;
	int zo_special_vdevs;
	int zo_dump_dbgmsg;
	int zo_gvars_count;
	char zo_gvars[ZO_GVARS_MAX_COUNT][ZO_GVARS_MAX_ARGLEN];
} ztest_shared_opts_t;

/* Default values for command line options. */
#define	DEFAULT_POOL "ztest"
#define	DEFAULT_VDEV_DIR "/tmp"
#define	DEFAULT_VDEV_COUNT 5
#define	DEFAULT_VDEV_SIZE (SPA_MINDEVSIZE * 4)	/* 256m default size */
#define	DEFAULT_VDEV_SIZE_STR "256M"
#define	DEFAULT_ASHIFT SPA_MINBLOCKSHIFT
#define	DEFAULT_MIRRORS 2
#define	DEFAULT_RAID_CHILDREN 4
#define	DEFAULT_RAID_PARITY 1
#define	DEFAULT_DRAID_DATA 4
#define	DEFAULT_DRAID_SPARES 1
#define	DEFAULT_DATASETS_COUNT 7
#define	DEFAULT_THREADS 23
#define	DEFAULT_RUN_TIME 300 /* 300 seconds */
#define	DEFAULT_RUN_TIME_STR "300 sec"
#define	DEFAULT_PASS_TIME 60 /* 60 seconds */
#define	DEFAULT_PASS_TIME_STR "60 sec"
#define	DEFAULT_KILL_RATE 70 /* 70% kill rate */
#define	DEFAULT_KILLRATE_STR "70%"
#define	DEFAULT_INITS 1
#define	DEFAULT_MAX_LOOPS 50 /* 5 minutes */
#define	DEFAULT_FORCE_GANGING (64 << 10)
#define	DEFAULT_FORCE_GANGING_STR "64K"

/* Simplifying assumption: -1 is not a valid default. */
#define	NO_DEFAULT -1

static const ztest_shared_opts_t ztest_opts_defaults = {
	.zo_pool = DEFAULT_POOL,
	.zo_dir = DEFAULT_VDEV_DIR,
	.zo_alt_ztest = { '\0' },
	.zo_alt_libpath = { '\0' },
	.zo_vdevs = DEFAULT_VDEV_COUNT,
	.zo_ashift = DEFAULT_ASHIFT,
	.zo_mirrors = DEFAULT_MIRRORS,
	.zo_raid_children = DEFAULT_RAID_CHILDREN,
	.zo_raid_parity = DEFAULT_RAID_PARITY,
	.zo_raid_type = VDEV_TYPE_RAIDZ,
	.zo_vdev_size = DEFAULT_VDEV_SIZE,
	.zo_draid_data = DEFAULT_DRAID_DATA,	/* data drives */
	.zo_draid_spares = DEFAULT_DRAID_SPARES, /* distributed spares */
	.zo_datasets = DEFAULT_DATASETS_COUNT,
	.zo_threads = DEFAULT_THREADS,
	.zo_passtime = DEFAULT_PASS_TIME,
	.zo_killrate = DEFAULT_KILL_RATE,
	.zo_verbose = 0,
	.zo_mmp_test = 0,
	.zo_init = DEFAULT_INITS,
	.zo_time = DEFAULT_RUN_TIME,
	.zo_maxloops = DEFAULT_MAX_LOOPS, /* max loops during spa_freeze() */
	.zo_metaslab_force_ganging = DEFAULT_FORCE_GANGING,
	.zo_special_vdevs = ZTEST_VDEV_CLASS_RND,
	.zo_gvars_count = 0,
	.zo_raidz_expand_test = RAIDZ_EXPAND_NONE,
};

extern uint64_t metaslab_force_ganging;
extern uint64_t metaslab_df_alloc_threshold;
extern uint64_t zfs_deadman_synctime_ms;
extern uint_t metaslab_preload_limit;
extern int zfs_compressed_arc_enabled;
extern int zfs_abd_scatter_enabled;
extern uint_t dmu_object_alloc_chunk_shift;
extern boolean_t zfs_force_some_double_word_sm_entries;
extern unsigned long zio_decompress_fail_fraction;
extern unsigned long zfs_reconstruct_indirect_damage_fraction;
extern uint64_t raidz_expand_max_reflow_bytes;
extern uint_t raidz_expand_pause_point;


static ztest_shared_opts_t *ztest_shared_opts;
static ztest_shared_opts_t ztest_opts;
static const char *const ztest_wkeydata = "abcdefghijklmnopqrstuvwxyz012345";

typedef struct ztest_shared_ds {
	uint64_t	zd_seq;
} ztest_shared_ds_t;

static ztest_shared_ds_t *ztest_shared_ds;
#define	ZTEST_GET_SHARED_DS(d) (&ztest_shared_ds[d])

typedef struct ztest_scratch_state {
	uint64_t	zs_raidz_scratch_verify_pause;
} ztest_shared_scratch_state_t;

static ztest_shared_scratch_state_t *ztest_scratch_state;

#define	BT_MAGIC	0x123456789abcdefULL
#define	MAXFAULTS(zs) \
	(MAX((zs)->zs_mirrors, 1) * (ztest_opts.zo_raid_parity + 1) - 1)

enum ztest_io_type {
	ZTEST_IO_WRITE_TAG,
	ZTEST_IO_WRITE_PATTERN,
	ZTEST_IO_WRITE_ZEROES,
	ZTEST_IO_TRUNCATE,
	ZTEST_IO_SETATTR,
	ZTEST_IO_REWRITE,
	ZTEST_IO_TYPES
};

typedef struct ztest_block_tag {
	uint64_t	bt_magic;
	uint64_t	bt_objset;
	uint64_t	bt_object;
	uint64_t	bt_dnodesize;
	uint64_t	bt_offset;
	uint64_t	bt_gen;
	uint64_t	bt_txg;
	uint64_t	bt_crtxg;
} ztest_block_tag_t;

typedef struct bufwad {
	uint64_t	bw_index;
	uint64_t	bw_txg;
	uint64_t	bw_data;
} bufwad_t;

/*
 * It would be better to use a rangelock_t per object.  Unfortunately
 * the rangelock_t is not a drop-in replacement for rl_t, because we
 * still need to map from object ID to rangelock_t.
 */
typedef enum {
	ZTRL_READER,
	ZTRL_WRITER,
	ZTRL_APPEND
} rl_type_t;

typedef struct rll {
	void		*rll_writer;
	int		rll_readers;
	kmutex_t	rll_lock;
	kcondvar_t	rll_cv;
} rll_t;

typedef struct rl {
	uint64_t	rl_object;
	uint64_t	rl_offset;
	uint64_t	rl_size;
	rll_t		*rl_lock;
} rl_t;

#define	ZTEST_RANGE_LOCKS	64
#define	ZTEST_OBJECT_LOCKS	64

/*
 * Object descriptor.  Used as a template for object lookup/create/remove.
 */
typedef struct ztest_od {
	uint64_t	od_dir;
	uint64_t	od_object;
	dmu_object_type_t od_type;
	dmu_object_type_t od_crtype;
	uint64_t	od_blocksize;
	uint64_t	od_crblocksize;
	uint64_t	od_crdnodesize;
	uint64_t	od_gen;
	uint64_t	od_crgen;
	char		od_name[ZFS_MAX_DATASET_NAME_LEN];
} ztest_od_t;

/*
 * Per-dataset state.
 */
typedef struct ztest_ds {
	ztest_shared_ds_t *zd_shared;
	objset_t	*zd_os;
	pthread_rwlock_t zd_zilog_lock;
	zilog_t		*zd_zilog;
	ztest_od_t	*zd_od;		/* debugging aid */
	char		zd_name[ZFS_MAX_DATASET_NAME_LEN];
	kmutex_t	zd_dirobj_lock;
	rll_t		zd_object_lock[ZTEST_OBJECT_LOCKS];
	rll_t		zd_range_lock[ZTEST_RANGE_LOCKS];
} ztest_ds_t;

/*
 * Per-iteration state.
 */
typedef void ztest_func_t(ztest_ds_t *zd, uint64_t id);

typedef struct ztest_info {
	ztest_func_t	*zi_func;	/* test function */
	uint64_t	zi_iters;	/* iterations per execution */
	uint64_t	*zi_interval;	/* execute every <interval> seconds */
	const char	*zi_funcname;	/* name of test function */
} ztest_info_t;

typedef struct ztest_shared_callstate {
	uint64_t	zc_count;	/* per-pass count */
	uint64_t	zc_time;	/* per-pass time */
	uint64_t	zc_next;	/* next time to call this function */
} ztest_shared_callstate_t;

static ztest_shared_callstate_t *ztest_shared_callstate;
#define	ZTEST_GET_SHARED_CALLSTATE(c) (&ztest_shared_callstate[c])

ztest_func_t ztest_dmu_read_write;
ztest_func_t ztest_dmu_write_parallel;
ztest_func_t ztest_dmu_object_alloc_free;
ztest_func_t ztest_dmu_object_next_chunk;
ztest_func_t ztest_dmu_commit_callbacks;
ztest_func_t ztest_zap;
ztest_func_t ztest_zap_parallel;
ztest_func_t ztest_zil_commit;
ztest_func_t ztest_zil_remount;
ztest_func_t ztest_dmu_read_write_zcopy;
ztest_func_t ztest_dmu_objset_create_destroy;
ztest_func_t ztest_dmu_prealloc;
ztest_func_t ztest_fzap;
ztest_func_t ztest_dmu_snapshot_create_destroy;
ztest_func_t ztest_dsl_prop_get_set;
ztest_func_t ztest_spa_prop_get_set;
ztest_func_t ztest_spa_create_destroy;
ztest_func_t ztest_fault_inject;
ztest_func_t ztest_dmu_snapshot_hold;
ztest_func_t ztest_mmp_enable_disable;
ztest_func_t ztest_scrub;
ztest_func_t ztest_dsl_dataset_promote_busy;
ztest_func_t ztest_vdev_attach_detach;
ztest_func_t ztest_vdev_raidz_attach;
ztest_func_t ztest_vdev_LUN_growth;
ztest_func_t ztest_vdev_add_remove;
ztest_func_t ztest_vdev_class_add;
ztest_func_t ztest_vdev_aux_add_remove;
ztest_func_t ztest_split_pool;
ztest_func_t ztest_reguid;
ztest_func_t ztest_spa_upgrade;
ztest_func_t ztest_device_removal;
ztest_func_t ztest_spa_checkpoint_create_discard;
ztest_func_t ztest_initialize;
ztest_func_t ztest_trim;
ztest_func_t ztest_blake3;
ztest_func_t ztest_fletcher;
ztest_func_t ztest_fletcher_incr;
ztest_func_t ztest_verify_dnode_bt;

static uint64_t zopt_always = 0ULL * NANOSEC;		/* all the time */
static uint64_t zopt_incessant = 1ULL * NANOSEC / 10;	/* every 1/10 second */
static uint64_t zopt_often = 1ULL * NANOSEC;		/* every second */
static uint64_t zopt_sometimes = 10ULL * NANOSEC;	/* every 10 seconds */
static uint64_t zopt_rarely = 60ULL * NANOSEC;		/* every 60 seconds */

#define	ZTI_INIT(func, iters, interval) \
	{   .zi_func = (func), \
	    .zi_iters = (iters), \
	    .zi_interval = (interval), \
	    .zi_funcname = # func }

static ztest_info_t ztest_info[] = {
	ZTI_INIT(ztest_dmu_read_write, 1, &zopt_always),
	ZTI_INIT(ztest_dmu_write_parallel, 10, &zopt_always),
	ZTI_INIT(ztest_dmu_object_alloc_free, 1, &zopt_always),
	ZTI_INIT(ztest_dmu_object_next_chunk, 1, &zopt_sometimes),
	ZTI_INIT(ztest_dmu_commit_callbacks, 1, &zopt_always),
	ZTI_INIT(ztest_zap, 30, &zopt_always),
	ZTI_INIT(ztest_zap_parallel, 100, &zopt_always),
	ZTI_INIT(ztest_split_pool, 1, &zopt_sometimes),
	ZTI_INIT(ztest_zil_commit, 1, &zopt_incessant),
	ZTI_INIT(ztest_zil_remount, 1, &zopt_sometimes),
	ZTI_INIT(ztest_dmu_read_write_zcopy, 1, &zopt_often),
	ZTI_INIT(ztest_dmu_objset_create_destroy, 1, &zopt_often),
	ZTI_INIT(ztest_dsl_prop_get_set, 1, &zopt_often),
	ZTI_INIT(ztest_spa_prop_get_set, 1, &zopt_sometimes),
#if 0
	ZTI_INIT(ztest_dmu_prealloc, 1, &zopt_sometimes),
#endif
	ZTI_INIT(ztest_fzap, 1, &zopt_sometimes),
	ZTI_INIT(ztest_dmu_snapshot_create_destroy, 1, &zopt_sometimes),
	ZTI_INIT(ztest_spa_create_destroy, 1, &zopt_sometimes),
	ZTI_INIT(ztest_fault_inject, 1, &zopt_sometimes),
	ZTI_INIT(ztest_dmu_snapshot_hold, 1, &zopt_sometimes),
	ZTI_INIT(ztest_mmp_enable_disable, 1, &zopt_sometimes),
	ZTI_INIT(ztest_reguid, 1, &zopt_rarely),
	ZTI_INIT(ztest_scrub, 1, &zopt_rarely),
	ZTI_INIT(ztest_spa_upgrade, 1, &zopt_rarely),
	ZTI_INIT(ztest_dsl_dataset_promote_busy, 1, &zopt_rarely),
	ZTI_INIT(ztest_vdev_attach_detach, 1, &zopt_sometimes),
	ZTI_INIT(ztest_vdev_raidz_attach, 1, &zopt_sometimes),
	ZTI_INIT(ztest_vdev_LUN_growth, 1, &zopt_rarely),
	ZTI_INIT(ztest_vdev_add_remove, 1, &ztest_opts.zo_vdevtime),
	ZTI_INIT(ztest_vdev_class_add, 1, &ztest_opts.zo_vdevtime),
	ZTI_INIT(ztest_vdev_aux_add_remove, 1, &ztest_opts.zo_vdevtime),
	ZTI_INIT(ztest_device_removal, 1, &zopt_sometimes),
	ZTI_INIT(ztest_spa_checkpoint_create_discard, 1, &zopt_rarely),
	ZTI_INIT(ztest_initialize, 1, &zopt_sometimes),
	ZTI_INIT(ztest_trim, 1, &zopt_sometimes),
	ZTI_INIT(ztest_blake3, 1, &zopt_rarely),
	ZTI_INIT(ztest_fletcher, 1, &zopt_rarely),
	ZTI_INIT(ztest_fletcher_incr, 1, &zopt_rarely),
	ZTI_INIT(ztest_verify_dnode_bt, 1, &zopt_sometimes),
};

#define	ZTEST_FUNCS	(sizeof (ztest_info) / sizeof (ztest_info_t))

/*
 * The following struct is used to hold a list of uncalled commit callbacks.
 * The callbacks are ordered by txg number.
 */
typedef struct ztest_cb_list {
	kmutex_t	zcl_callbacks_lock;
	list_t		zcl_callbacks;
} ztest_cb_list_t;

/*
 * Stuff we need to share writably between parent and child.
 */
typedef struct ztest_shared {
	boolean_t	zs_do_init;
	hrtime_t	zs_proc_start;
	hrtime_t	zs_proc_stop;
	hrtime_t	zs_thread_start;
	hrtime_t	zs_thread_stop;
	hrtime_t	zs_thread_kill;
	uint64_t	zs_enospc_count;
	uint64_t	zs_vdev_next_leaf;
	uint64_t	zs_vdev_aux;
	uint64_t	zs_alloc;
	uint64_t	zs_space;
	uint64_t	zs_splits;
	uint64_t	zs_mirrors;
	uint64_t	zs_metaslab_sz;
	uint64_t	zs_metaslab_df_alloc_threshold;
	uint64_t	zs_guid;
} ztest_shared_t;

#define	ID_PARALLEL	-1ULL

static char ztest_dev_template[] = "%s/%s.%llua";
static char ztest_aux_template[] = "%s/%s.%s.%llu";
static ztest_shared_t *ztest_shared;

static spa_t *ztest_spa = NULL;
static ztest_ds_t *ztest_ds;

static kmutex_t ztest_vdev_lock;
static boolean_t ztest_device_removal_active = B_FALSE;
static boolean_t ztest_pool_scrubbed = B_FALSE;
static kmutex_t ztest_checkpoint_lock;

/*
 * The ztest_name_lock protects the pool and dataset namespace used by
 * the individual tests. To modify the namespace, consumers must grab
 * this lock as writer. Grabbing the lock as reader will ensure that the
 * namespace does not change while the lock is held.
 */
static pthread_rwlock_t ztest_name_lock;

static boolean_t ztest_dump_core = B_TRUE;
static boolean_t ztest_exiting;

/* Global commit callback list */
static ztest_cb_list_t zcl;
/* Commit cb delay */
static uint64_t zc_min_txg_delay = UINT64_MAX;
static int zc_cb_counter = 0;

/*
 * Minimum number of commit callbacks that need to be registered for us to check
 * whether the minimum txg delay is acceptable.
 */
#define	ZTEST_COMMIT_CB_MIN_REG	100

/*
 * If a number of txgs equal to this threshold have been created after a commit
 * callback has been registered but not called, then we assume there is an
 * implementation bug.
 */
#define	ZTEST_COMMIT_CB_THRESH	(TXG_CONCURRENT_STATES + 1000)

enum ztest_object {
	ZTEST_META_DNODE = 0,
	ZTEST_DIROBJ,
	ZTEST_OBJECTS
};

static __attribute__((noreturn)) void usage(boolean_t requested);
static int ztest_scrub_impl(spa_t *spa);

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}

static void
dump_debug_buffer(void)
{
	ssize_t ret __attribute__((unused));

	if (!ztest_opts.zo_dump_dbgmsg)
		return;

	/*
	 * We use write() instead of printf() so that this function
	 * is safe to call from a signal handler.
	 */
	ret = write(STDOUT_FILENO, "\n", 1);
	zfs_dbgmsg_print("ztest");
}

#define	BACKTRACE_SZ	100

static void sig_handler(int signo)
{
	struct sigaction action;
#if (__GLIBC__ && !__UCLIBC__) /* backtrace() is a GNU extension */
	int nptrs;
	void *buffer[BACKTRACE_SZ];

	nptrs = backtrace(buffer, BACKTRACE_SZ);
	backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
#endif
	dump_debug_buffer();

	/*
	 * Restore default action and re-raise signal so SIGSEGV and
	 * SIGABRT can trigger a core dump.
	 */
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	(void) sigaction(signo, &action, NULL);
	raise(signo);
}

#define	FATAL_MSG_SZ	1024

static const char *fatal_msg;

static __attribute__((format(printf, 2, 3))) __attribute__((noreturn)) void
fatal(int do_perror, const char *message, ...)
{
	va_list args;
	int save_errno = errno;
	char *buf;

	(void) fflush(stdout);
	buf = umem_alloc(FATAL_MSG_SZ, UMEM_NOFAIL);
	if (buf == NULL)
		goto out;

	va_start(args, message);
	(void) sprintf(buf, "ztest: ");
	/* LINTED */
	(void) vsprintf(buf + strlen(buf), message, args);
	va_end(args);
	if (do_perror) {
		(void) snprintf(buf + strlen(buf), FATAL_MSG_SZ - strlen(buf),
		    ": %s", strerror(save_errno));
	}
	(void) fprintf(stderr, "%s\n", buf);
	fatal_msg = buf;			/* to ease debugging */

out:
	if (ztest_dump_core)
		abort();
	else
		dump_debug_buffer();

	exit(3);
}

static int
str2shift(const char *buf)
{
	const char *ends = "BKMGTPEZ";
	int i;

	if (buf[0] == '\0')
		return (0);
	for (i = 0; i < strlen(ends); i++) {
		if (toupper(buf[0]) == ends[i])
			break;
	}
	if (i == strlen(ends)) {
		(void) fprintf(stderr, "ztest: invalid bytes suffix: %s\n",
		    buf);
		usage(B_FALSE);
	}
	if (buf[1] == '\0' || (toupper(buf[1]) == 'B' && buf[2] == '\0')) {
		return (10*i);
	}
	(void) fprintf(stderr, "ztest: invalid bytes suffix: %s\n", buf);
	usage(B_FALSE);
}

static uint64_t
nicenumtoull(const char *buf)
{
	char *end;
	uint64_t val;

	val = strtoull(buf, &end, 0);
	if (end == buf) {
		(void) fprintf(stderr, "ztest: bad numeric value: %s\n", buf);
		usage(B_FALSE);
	} else if (end[0] == '.') {
		double fval = strtod(buf, &end);
		fval *= pow(2, str2shift(end));
		/*
		 * UINT64_MAX is not exactly representable as a double.
		 * The closest representation is UINT64_MAX + 1, so we
		 * use a >= comparison instead of > for the bounds check.
		 */
		if (fval >= (double)UINT64_MAX) {
			(void) fprintf(stderr, "ztest: value too large: %s\n",
			    buf);
			usage(B_FALSE);
		}
		val = (uint64_t)fval;
	} else {
		int shift = str2shift(end);
		if (shift >= 64 || (val << shift) >> shift != val) {
			(void) fprintf(stderr, "ztest: value too large: %s\n",
			    buf);
			usage(B_FALSE);
		}
		val <<= shift;
	}
	return (val);
}

typedef struct ztest_option {
	const char	short_opt;
	const char	*long_opt;
	const char	*long_opt_param;
	const char	*comment;
	unsigned int	default_int;
	const char	*default_str;
} ztest_option_t;

/*
 * The following option_table is used for generating the usage info as well as
 * the long and short option information for calling getopt_long().
 */
static ztest_option_t option_table[] = {
	{ 'v',	"vdevs", "INTEGER", "Number of vdevs", DEFAULT_VDEV_COUNT,
	    NULL},
	{ 's',	"vdev-size", "INTEGER", "Size of each vdev",
	    NO_DEFAULT, DEFAULT_VDEV_SIZE_STR},
	{ 'a',	"alignment-shift", "INTEGER",
	    "Alignment shift; use 0 for random", DEFAULT_ASHIFT, NULL},
	{ 'm',	"mirror-copies", "INTEGER", "Number of mirror copies",
	    DEFAULT_MIRRORS, NULL},
	{ 'r',	"raid-disks", "INTEGER", "Number of raidz/draid disks",
	    DEFAULT_RAID_CHILDREN, NULL},
	{ 'R',	"raid-parity", "INTEGER", "Raid parity",
	    DEFAULT_RAID_PARITY, NULL},
	{ 'K',  "raid-kind", "raidz|eraidz|draid|random", "Raid kind",
	    NO_DEFAULT, "random"},
	{ 'D',	"draid-data", "INTEGER", "Number of draid data drives",
	    DEFAULT_DRAID_DATA, NULL},
	{ 'S',	"draid-spares", "INTEGER", "Number of draid spares",
	    DEFAULT_DRAID_SPARES, NULL},
	{ 'd',	"datasets", "INTEGER", "Number of datasets",
	    DEFAULT_DATASETS_COUNT, NULL},
	{ 't',	"threads", "INTEGER", "Number of ztest threads",
	    DEFAULT_THREADS, NULL},
	{ 'g',	"gang-block-threshold", "INTEGER",
	    "Metaslab gang block threshold",
	    NO_DEFAULT, DEFAULT_FORCE_GANGING_STR},
	{ 'i',	"init-count", "INTEGER", "Number of times to initialize pool",
	    DEFAULT_INITS, NULL},
	{ 'k',	"kill-percentage", "INTEGER", "Kill percentage",
	    NO_DEFAULT, DEFAULT_KILLRATE_STR},
	{ 'p',	"pool-name", "STRING", "Pool name",
	    NO_DEFAULT, DEFAULT_POOL},
	{ 'f',	"vdev-file-directory", "PATH", "File directory for vdev files",
	    NO_DEFAULT, DEFAULT_VDEV_DIR},
	{ 'M',	"multi-host", NULL,
	    "Multi-host; simulate pool imported on remote host",
	    NO_DEFAULT, NULL},
	{ 'E',	"use-existing-pool", NULL,
	    "Use existing pool instead of creating new one", NO_DEFAULT, NULL},
	{ 'T',	"run-time", "INTEGER", "Total run time",
	    NO_DEFAULT, DEFAULT_RUN_TIME_STR},
	{ 'P',	"pass-time", "INTEGER", "Time per pass",
	    NO_DEFAULT, DEFAULT_PASS_TIME_STR},
	{ 'F',	"freeze-loops", "INTEGER", "Max loops in spa_freeze()",
	    DEFAULT_MAX_LOOPS, NULL},
	{ 'B',	"alt-ztest", "PATH", "Alternate ztest path",
	    NO_DEFAULT, NULL},
	{ 'C',	"vdev-class-state", "on|off|random", "vdev class state",
	    NO_DEFAULT, "random"},
	{ 'X', "raidz-expansion", NULL,
	    "Perform a dedicated raidz expansion test",
	    NO_DEFAULT, NULL},
	{ 'o',	"option", "\"OPTION=INTEGER\"",
	    "Set global variable to an unsigned 32-bit integer value",
	    NO_DEFAULT, NULL},
	{ 'G',	"dump-debug-msg", NULL,
	    "Dump zfs_dbgmsg buffer before exiting due to an error",
	    NO_DEFAULT, NULL},
	{ 'V',	"verbose", NULL,
	    "Verbose (use multiple times for ever more verbosity)",
	    NO_DEFAULT, NULL},
	{ 'h',	"help",	NULL, "Show this help",
	    NO_DEFAULT, NULL},
	{0, 0, 0, 0, 0, 0}
};

static struct option *long_opts = NULL;
static char *short_opts = NULL;

static void
init_options(void)
{
	ASSERT3P(long_opts, ==, NULL);
	ASSERT3P(short_opts, ==, NULL);

	int count = sizeof (option_table) / sizeof (option_table[0]);
	long_opts = umem_alloc(sizeof (struct option) * count, UMEM_NOFAIL);

	short_opts = umem_alloc(sizeof (char) * 2 * count, UMEM_NOFAIL);
	int short_opt_index = 0;

	for (int i = 0; i < count; i++) {
		long_opts[i].val = option_table[i].short_opt;
		long_opts[i].name = option_table[i].long_opt;
		long_opts[i].has_arg = option_table[i].long_opt_param != NULL
		    ? required_argument : no_argument;
		long_opts[i].flag = NULL;
		short_opts[short_opt_index++] = option_table[i].short_opt;
		if (option_table[i].long_opt_param != NULL) {
			short_opts[short_opt_index++] = ':';
		}
	}
}

static void
fini_options(void)
{
	int count = sizeof (option_table) / sizeof (option_table[0]);

	umem_free(long_opts, sizeof (struct option) * count);
	umem_free(short_opts, sizeof (char) * 2 * count);

	long_opts = NULL;
	short_opts = NULL;
}

static __attribute__((noreturn)) void
usage(boolean_t requested)
{
	char option[80];
	FILE *fp = requested ? stdout : stderr;

	(void) fprintf(fp, "Usage: %s [OPTIONS...]\n", DEFAULT_POOL);
	for (int i = 0; option_table[i].short_opt != 0; i++) {
		if (option_table[i].long_opt_param != NULL) {
			(void) sprintf(option, "  -%c --%s=%s",
			    option_table[i].short_opt,
			    option_table[i].long_opt,
			    option_table[i].long_opt_param);
		} else {
			(void) sprintf(option, "  -%c --%s",
			    option_table[i].short_opt,
			    option_table[i].long_opt);
		}
		(void) fprintf(fp, "  %-43s%s", option,
		    option_table[i].comment);

		if (option_table[i].long_opt_param != NULL) {
			if (option_table[i].default_str != NULL) {
				(void) fprintf(fp, " (default: %s)",
				    option_table[i].default_str);
			} else if (option_table[i].default_int != NO_DEFAULT) {
				(void) fprintf(fp, " (default: %u)",
				    option_table[i].default_int);
			}
		}
		(void) fprintf(fp, "\n");
	}
	exit(requested ? 0 : 1);
}

static uint64_t
ztest_random(uint64_t range)
{
	uint64_t r;

	ASSERT3S(ztest_fd_rand, >=, 0);

	if (range == 0)
		return (0);

	if (read(ztest_fd_rand, &r, sizeof (r)) != sizeof (r))
		fatal(B_TRUE, "short read from /dev/urandom");

	return (r % range);
}

static void
ztest_parse_name_value(const char *input, ztest_shared_opts_t *zo)
{
	char name[32];
	char *value;
	int state = ZTEST_VDEV_CLASS_RND;

	(void) strlcpy(name, input, sizeof (name));

	value = strchr(name, '=');
	if (value == NULL) {
		(void) fprintf(stderr, "missing value in property=value "
		    "'-C' argument (%s)\n", input);
		usage(B_FALSE);
	}
	*(value) = '\0';
	value++;

	if (strcmp(value, "on") == 0) {
		state = ZTEST_VDEV_CLASS_ON;
	} else if (strcmp(value, "off") == 0) {
		state = ZTEST_VDEV_CLASS_OFF;
	} else if (strcmp(value, "random") == 0) {
		state = ZTEST_VDEV_CLASS_RND;
	} else {
		(void) fprintf(stderr, "invalid property value '%s'\n", value);
		usage(B_FALSE);
	}

	if (strcmp(name, "special") == 0) {
		zo->zo_special_vdevs = state;
	} else {
		(void) fprintf(stderr, "invalid property name '%s'\n", name);
		usage(B_FALSE);
	}
	if (zo->zo_verbose >= 3)
		(void) printf("%s vdev state is '%s'\n", name, value);
}

static void
process_options(int argc, char **argv)
{
	char *path;
	ztest_shared_opts_t *zo = &ztest_opts;

	int opt;
	uint64_t value;
	const char *raid_kind = "random";

	memcpy(zo, &ztest_opts_defaults, sizeof (*zo));

	init_options();

	while ((opt = getopt_long(argc, argv, short_opts, long_opts,
	    NULL)) != EOF) {
		value = 0;
		switch (opt) {
		case 'v':
		case 's':
		case 'a':
		case 'm':
		case 'r':
		case 'R':
		case 'D':
		case 'S':
		case 'd':
		case 't':
		case 'g':
		case 'i':
		case 'k':
		case 'T':
		case 'P':
		case 'F':
			value = nicenumtoull(optarg);
		}
		switch (opt) {
		case 'v':
			zo->zo_vdevs = value;
			break;
		case 's':
			zo->zo_vdev_size = MAX(SPA_MINDEVSIZE, value);
			break;
		case 'a':
			zo->zo_ashift = value;
			break;
		case 'm':
			zo->zo_mirrors = value;
			break;
		case 'r':
			zo->zo_raid_children = MAX(1, value);
			break;
		case 'R':
			zo->zo_raid_parity = MIN(MAX(value, 1), 3);
			break;
		case 'K':
			raid_kind = optarg;
			break;
		case 'D':
			zo->zo_draid_data = MAX(1, value);
			break;
		case 'S':
			zo->zo_draid_spares = MAX(1, value);
			break;
		case 'd':
			zo->zo_datasets = MAX(1, value);
			break;
		case 't':
			zo->zo_threads = MAX(1, value);
			break;
		case 'g':
			zo->zo_metaslab_force_ganging =
			    MAX(SPA_MINBLOCKSIZE << 1, value);
			break;
		case 'i':
			zo->zo_init = value;
			break;
		case 'k':
			zo->zo_killrate = value;
			break;
		case 'p':
			(void) strlcpy(zo->zo_pool, optarg,
			    sizeof (zo->zo_pool));
			break;
		case 'f':
			path = realpath(optarg, NULL);
			if (path == NULL) {
				(void) fprintf(stderr, "error: %s: %s\n",
				    optarg, strerror(errno));
				usage(B_FALSE);
			} else {
				(void) strlcpy(zo->zo_dir, path,
				    sizeof (zo->zo_dir));
				free(path);
			}
			break;
		case 'M':
			zo->zo_mmp_test = 1;
			break;
		case 'V':
			zo->zo_verbose++;
			break;
		case 'X':
			zo->zo_raidz_expand_test = RAIDZ_EXPAND_REQUESTED;
			break;
		case 'E':
			zo->zo_init = 0;
			break;
		case 'T':
			zo->zo_time = value;
			break;
		case 'P':
			zo->zo_passtime = MAX(1, value);
			break;
		case 'F':
			zo->zo_maxloops = MAX(1, value);
			break;
		case 'B':
			(void) strlcpy(zo->zo_alt_ztest, optarg,
			    sizeof (zo->zo_alt_ztest));
			break;
		case 'C':
			ztest_parse_name_value(optarg, zo);
			break;
		case 'o':
			if (zo->zo_gvars_count >= ZO_GVARS_MAX_COUNT) {
				(void) fprintf(stderr,
				    "max global var count (%zu) exceeded\n",
				    ZO_GVARS_MAX_COUNT);
				usage(B_FALSE);
			}
			char *v = zo->zo_gvars[zo->zo_gvars_count];
			if (strlcpy(v, optarg, ZO_GVARS_MAX_ARGLEN) >=
			    ZO_GVARS_MAX_ARGLEN) {
				(void) fprintf(stderr,
				    "global var option '%s' is too long\n",
				    optarg);
				usage(B_FALSE);
			}
			zo->zo_gvars_count++;
			break;
		case 'G':
			zo->zo_dump_dbgmsg = 1;
			break;
		case 'h':
			usage(B_TRUE);
			break;
		case '?':
		default:
			usage(B_FALSE);
			break;
		}
	}

	fini_options();

	/* Force compatible options for raidz expansion run */
	if (zo->zo_raidz_expand_test == RAIDZ_EXPAND_REQUESTED) {
		zo->zo_mmp_test = 0;
		zo->zo_mirrors = 0;
		zo->zo_vdevs = 1;
		zo->zo_vdev_size = DEFAULT_VDEV_SIZE * 2;
		zo->zo_raid_do_expand = B_FALSE;
		raid_kind = "raidz";
	}

	if (strcmp(raid_kind, "random") == 0) {
		switch (ztest_random(3)) {
		case 0:
			raid_kind = "raidz";
			break;
		case 1:
			raid_kind = "eraidz";
			break;
		case 2:
			raid_kind = "draid";
			break;
		}

		if (ztest_opts.zo_verbose >= 3)
			(void) printf("choosing RAID type '%s'\n", raid_kind);
	}

	if (strcmp(raid_kind, "draid") == 0) {
		uint64_t min_devsize;

		/* With fewer disk use 256M, otherwise 128M is OK */
		min_devsize = (ztest_opts.zo_raid_children < 16) ?
		    (256ULL << 20) : (128ULL << 20);

		/* No top-level mirrors with dRAID for now */
		zo->zo_mirrors = 0;

		/* Use more appropriate defaults for dRAID */
		if (zo->zo_vdevs == ztest_opts_defaults.zo_vdevs)
			zo->zo_vdevs = 1;
		if (zo->zo_raid_children ==
		    ztest_opts_defaults.zo_raid_children)
			zo->zo_raid_children = 16;
		if (zo->zo_ashift < 12)
			zo->zo_ashift = 12;
		if (zo->zo_vdev_size < min_devsize)
			zo->zo_vdev_size = min_devsize;

		if (zo->zo_draid_data + zo->zo_raid_parity >
		    zo->zo_raid_children - zo->zo_draid_spares) {
			(void) fprintf(stderr, "error: too few draid "
			    "children (%d) for stripe width (%d)\n",
			    zo->zo_raid_children,
			    zo->zo_draid_data + zo->zo_raid_parity);
			usage(B_FALSE);
		}

		(void) strlcpy(zo->zo_raid_type, VDEV_TYPE_DRAID,
		    sizeof (zo->zo_raid_type));

	} else if (strcmp(raid_kind, "eraidz") == 0) {
		/* using eraidz (expandable raidz) */
		zo->zo_raid_do_expand = B_TRUE;

		/* tests expect top-level to be raidz */
		zo->zo_mirrors = 0;
		zo->zo_vdevs = 1;

		/* Make sure parity is less than data columns */
		zo->zo_raid_parity = MIN(zo->zo_raid_parity,
		    zo->zo_raid_children - 1);

	} else /* using raidz */ {
		ASSERT0(strcmp(raid_kind, "raidz"));

		zo->zo_raid_parity = MIN(zo->zo_raid_parity,
		    zo->zo_raid_children - 1);
	}

	zo->zo_vdevtime =
	    (zo->zo_vdevs > 0 ? zo->zo_time * NANOSEC / zo->zo_vdevs :
	    UINT64_MAX >> 2);

	if (*zo->zo_alt_ztest) {
		const char *invalid_what = "ztest";
		char *val = zo->zo_alt_ztest;
		if (0 != access(val, X_OK) ||
		    (strrchr(val, '/') == NULL && (errno == EINVAL)))
			goto invalid;

		int dirlen = strrchr(val, '/') - val;
		strlcpy(zo->zo_alt_libpath, val,
		    MIN(sizeof (zo->zo_alt_libpath), dirlen + 1));
		invalid_what = "library path", val = zo->zo_alt_libpath;
		if (strrchr(val, '/') == NULL && (errno == EINVAL))
			goto invalid;
		*strrchr(val, '/') = '\0';
		strlcat(val, "/lib", sizeof (zo->zo_alt_libpath));

		if (0 != access(zo->zo_alt_libpath, X_OK))
			goto invalid;
		return;

invalid:
		ztest_dump_core = B_FALSE;
		fatal(B_TRUE, "invalid alternate %s %s", invalid_what, val);
	}
}

static void
ztest_kill(ztest_shared_t *zs)
{
	zs->zs_alloc = metaslab_class_get_alloc(spa_normal_class(ztest_spa));
	zs->zs_space = metaslab_class_get_space(spa_normal_class(ztest_spa));

	/*
	 * Before we kill ourselves, make sure that the config is updated.
	 * See comment above spa_write_cachefile().
	 */
	if (raidz_expand_pause_point != RAIDZ_EXPAND_PAUSE_NONE) {
		if (mutex_tryenter(&spa_namespace_lock)) {
			spa_write_cachefile(ztest_spa, B_FALSE, B_FALSE,
			    B_FALSE);
			mutex_exit(&spa_namespace_lock);

			ztest_scratch_state->zs_raidz_scratch_verify_pause =
			    raidz_expand_pause_point;
		} else {
			/*
			 * Do not verify scratch object in case if
			 * spa_namespace_lock cannot be acquired,
			 * it can cause deadlock in spa_config_update().
			 */
			raidz_expand_pause_point = RAIDZ_EXPAND_PAUSE_NONE;

			return;
		}
	} else {
		mutex_enter(&spa_namespace_lock);
		spa_write_cachefile(ztest_spa, B_FALSE, B_FALSE, B_FALSE);
		mutex_exit(&spa_namespace_lock);
	}

	(void) raise(SIGKILL);
}

static void
ztest_record_enospc(const char *s)
{
	(void) s;
	ztest_shared->zs_enospc_count++;
}

static uint64_t
ztest_get_ashift(void)
{
	if (ztest_opts.zo_ashift == 0)
		return (SPA_MINBLOCKSHIFT + ztest_random(5));
	return (ztest_opts.zo_ashift);
}

static boolean_t
ztest_is_draid_spare(const char *name)
{
	uint64_t spare_id = 0, parity = 0, vdev_id = 0;

	if (sscanf(name, VDEV_TYPE_DRAID "%"PRIu64"-%"PRIu64"-%"PRIu64"",
	    &parity, &vdev_id, &spare_id) == 3) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

static nvlist_t *
make_vdev_file(const char *path, const char *aux, const char *pool,
    size_t size, uint64_t ashift)
{
	char *pathbuf = NULL;
	uint64_t vdev;
	nvlist_t *file;
	boolean_t draid_spare = B_FALSE;


	if (ashift == 0)
		ashift = ztest_get_ashift();

	if (path == NULL) {
		pathbuf = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
		path = pathbuf;

		if (aux != NULL) {
			vdev = ztest_shared->zs_vdev_aux;
			(void) snprintf(pathbuf, MAXPATHLEN,
			    ztest_aux_template, ztest_opts.zo_dir,
			    pool == NULL ? ztest_opts.zo_pool : pool,
			    aux, vdev);
		} else {
			vdev = ztest_shared->zs_vdev_next_leaf++;
			(void) snprintf(pathbuf, MAXPATHLEN,
			    ztest_dev_template, ztest_opts.zo_dir,
			    pool == NULL ? ztest_opts.zo_pool : pool, vdev);
		}
	} else {
		draid_spare = ztest_is_draid_spare(path);
	}

	if (size != 0 && !draid_spare) {
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
		if (fd == -1)
			fatal(B_TRUE, "can't open %s", path);
		if (ftruncate(fd, size) != 0)
			fatal(B_TRUE, "can't ftruncate %s", path);
		(void) close(fd);
	}

	file = fnvlist_alloc();
	fnvlist_add_string(file, ZPOOL_CONFIG_TYPE,
	    draid_spare ? VDEV_TYPE_DRAID_SPARE : VDEV_TYPE_FILE);
	fnvlist_add_string(file, ZPOOL_CONFIG_PATH, path);
	fnvlist_add_uint64(file, ZPOOL_CONFIG_ASHIFT, ashift);
	umem_free(pathbuf, MAXPATHLEN);

	return (file);
}

static nvlist_t *
make_vdev_raid(const char *path, const char *aux, const char *pool, size_t size,
    uint64_t ashift, int r)
{
	nvlist_t *raid, **child;
	int c;

	if (r < 2)
		return (make_vdev_file(path, aux, pool, size, ashift));
	child = umem_alloc(r * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < r; c++)
		child[c] = make_vdev_file(path, aux, pool, size, ashift);

	raid = fnvlist_alloc();
	fnvlist_add_string(raid, ZPOOL_CONFIG_TYPE,
	    ztest_opts.zo_raid_type);
	fnvlist_add_uint64(raid, ZPOOL_CONFIG_NPARITY,
	    ztest_opts.zo_raid_parity);
	fnvlist_add_nvlist_array(raid, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)child, r);

	if (strcmp(ztest_opts.zo_raid_type, VDEV_TYPE_DRAID) == 0) {
		uint64_t ndata = ztest_opts.zo_draid_data;
		uint64_t nparity = ztest_opts.zo_raid_parity;
		uint64_t nspares = ztest_opts.zo_draid_spares;
		uint64_t children = ztest_opts.zo_raid_children;
		uint64_t ngroups = 1;

		/*
		 * Calculate the minimum number of groups required to fill a
		 * slice. This is the LCM of the stripe width (data + parity)
		 * and the number of data drives (children - spares).
		 */
		while (ngroups * (ndata + nparity) % (children - nspares) != 0)
			ngroups++;

		/* Store the basic dRAID configuration. */
		fnvlist_add_uint64(raid, ZPOOL_CONFIG_DRAID_NDATA, ndata);
		fnvlist_add_uint64(raid, ZPOOL_CONFIG_DRAID_NSPARES, nspares);
		fnvlist_add_uint64(raid, ZPOOL_CONFIG_DRAID_NGROUPS, ngroups);
	}

	for (c = 0; c < r; c++)
		fnvlist_free(child[c]);

	umem_free(child, r * sizeof (nvlist_t *));

	return (raid);
}

static nvlist_t *
make_vdev_mirror(const char *path, const char *aux, const char *pool,
    size_t size, uint64_t ashift, int r, int m)
{
	nvlist_t *mirror, **child;
	int c;

	if (m < 1)
		return (make_vdev_raid(path, aux, pool, size, ashift, r));

	child = umem_alloc(m * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < m; c++)
		child[c] = make_vdev_raid(path, aux, pool, size, ashift, r);

	mirror = fnvlist_alloc();
	fnvlist_add_string(mirror, ZPOOL_CONFIG_TYPE, VDEV_TYPE_MIRROR);
	fnvlist_add_nvlist_array(mirror, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)child, m);

	for (c = 0; c < m; c++)
		fnvlist_free(child[c]);

	umem_free(child, m * sizeof (nvlist_t *));

	return (mirror);
}

static nvlist_t *
make_vdev_root(const char *path, const char *aux, const char *pool, size_t size,
    uint64_t ashift, const char *class, int r, int m, int t)
{
	nvlist_t *root, **child;
	int c;
	boolean_t log;

	ASSERT3S(t, >, 0);

	log = (class != NULL && strcmp(class, "log") == 0);

	child = umem_alloc(t * sizeof (nvlist_t *), UMEM_NOFAIL);

	for (c = 0; c < t; c++) {
		child[c] = make_vdev_mirror(path, aux, pool, size, ashift,
		    r, m);
		fnvlist_add_uint64(child[c], ZPOOL_CONFIG_IS_LOG, log);

		if (class != NULL && class[0] != '\0') {
			ASSERT(m > 1 || log);   /* expecting a mirror */
			fnvlist_add_string(child[c],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, class);
		}
	}

	root = fnvlist_alloc();
	fnvlist_add_string(root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(root, aux ? aux : ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)child, t);

	for (c = 0; c < t; c++)
		fnvlist_free(child[c]);

	umem_free(child, t * sizeof (nvlist_t *));

	return (root);
}

/*
 * Find a random spa version. Returns back a random spa version in the
 * range [initial_version, SPA_VERSION_FEATURES].
 */
static uint64_t
ztest_random_spa_version(uint64_t initial_version)
{
	uint64_t version = initial_version;

	if (version <= SPA_VERSION_BEFORE_FEATURES) {
		version = version +
		    ztest_random(SPA_VERSION_BEFORE_FEATURES - version + 1);
	}

	if (version > SPA_VERSION_BEFORE_FEATURES)
		version = SPA_VERSION_FEATURES;

	ASSERT(SPA_VERSION_IS_SUPPORTED(version));
	return (version);
}

static int
ztest_random_blocksize(void)
{
	ASSERT3U(ztest_spa->spa_max_ashift, !=, 0);

	/*
	 * Choose a block size >= the ashift.
	 * If the SPA supports new MAXBLOCKSIZE, test up to 1MB blocks.
	 */
	int maxbs = SPA_OLD_MAXBLOCKSHIFT;
	if (spa_maxblocksize(ztest_spa) == SPA_MAXBLOCKSIZE)
		maxbs = 20;
	uint64_t block_shift =
	    ztest_random(maxbs - ztest_spa->spa_max_ashift + 1);
	return (1 << (SPA_MINBLOCKSHIFT + block_shift));
}

static int
ztest_random_dnodesize(void)
{
	int slots;
	int max_slots = spa_maxdnodesize(ztest_spa) >> DNODE_SHIFT;

	if (max_slots == DNODE_MIN_SLOTS)
		return (DNODE_MIN_SIZE);

	/*
	 * Weight the random distribution more heavily toward smaller
	 * dnode sizes since that is more likely to reflect real-world
	 * usage.
	 */
	ASSERT3U(max_slots, >, 4);
	switch (ztest_random(10)) {
	case 0:
		slots = 5 + ztest_random(max_slots - 4);
		break;
	case 1 ... 4:
		slots = 2 + ztest_random(3);
		break;
	default:
		slots = 1;
		break;
	}

	return (slots << DNODE_SHIFT);
}

static int
ztest_random_ibshift(void)
{
	return (DN_MIN_INDBLKSHIFT +
	    ztest_random(DN_MAX_INDBLKSHIFT - DN_MIN_INDBLKSHIFT + 1));
}

static uint64_t
ztest_random_vdev_top(spa_t *spa, boolean_t log_ok)
{
	uint64_t top;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *tvd;

	ASSERT3U(spa_config_held(spa, SCL_ALL, RW_READER), !=, 0);

	do {
		top = ztest_random(rvd->vdev_children);
		tvd = rvd->vdev_child[top];
	} while (!vdev_is_concrete(tvd) || (tvd->vdev_islog && !log_ok) ||
	    tvd->vdev_mg == NULL || tvd->vdev_mg->mg_class == NULL);

	return (top);
}

static uint64_t
ztest_random_dsl_prop(zfs_prop_t prop)
{
	uint64_t value;

	do {
		value = zfs_prop_random_value(prop, ztest_random(-1ULL));
	} while (prop == ZFS_PROP_CHECKSUM && value == ZIO_CHECKSUM_OFF);

	return (value);
}

static int
ztest_dsl_prop_set_uint64(char *osname, zfs_prop_t prop, uint64_t value,
    boolean_t inherit)
{
	const char *propname = zfs_prop_to_name(prop);
	const char *valname;
	char *setpoint;
	uint64_t curval;
	int error;

	error = dsl_prop_set_int(osname, propname,
	    (inherit ? ZPROP_SRC_NONE : ZPROP_SRC_LOCAL), value);

	if (error == ENOSPC) {
		ztest_record_enospc(FTAG);
		return (error);
	}
	ASSERT0(error);

	setpoint = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
	VERIFY0(dsl_prop_get_integer(osname, propname, &curval, setpoint));

	if (ztest_opts.zo_verbose >= 6) {
		int err;

		err = zfs_prop_index_to_string(prop, curval, &valname);
		if (err)
			(void) printf("%s %s = %llu at '%s'\n", osname,
			    propname, (unsigned long long)curval, setpoint);
		else
			(void) printf("%s %s = %s at '%s'\n",
			    osname, propname, valname, setpoint);
	}
	umem_free(setpoint, MAXPATHLEN);

	return (error);
}

static int
ztest_spa_prop_set_uint64(zpool_prop_t prop, uint64_t value)
{
	spa_t *spa = ztest_spa;
	nvlist_t *props = NULL;
	int error;

	props = fnvlist_alloc();
	fnvlist_add_uint64(props, zpool_prop_to_name(prop), value);

	error = spa_prop_set(spa, props);

	fnvlist_free(props);

	if (error == ENOSPC) {
		ztest_record_enospc(FTAG);
		return (error);
	}
	ASSERT0(error);

	return (error);
}

static int
ztest_dmu_objset_own(const char *name, dmu_objset_type_t type,
    boolean_t readonly, boolean_t decrypt, const void *tag, objset_t **osp)
{
	int err;
	char *cp = NULL;
	char ddname[ZFS_MAX_DATASET_NAME_LEN];

	strlcpy(ddname, name, sizeof (ddname));
	cp = strchr(ddname, '@');
	if (cp != NULL)
		*cp = '\0';

	err = dmu_objset_own(name, type, readonly, decrypt, tag, osp);
	while (decrypt && err == EACCES) {
		dsl_crypto_params_t *dcp;
		nvlist_t *crypto_args = fnvlist_alloc();

		fnvlist_add_uint8_array(crypto_args, "wkeydata",
		    (uint8_t *)ztest_wkeydata, WRAPPING_KEY_LEN);
		VERIFY0(dsl_crypto_params_create_nvlist(DCP_CMD_NONE, NULL,
		    crypto_args, &dcp));
		err = spa_keystore_load_wkey(ddname, dcp, B_FALSE);
		/*
		 * Note: if there was an error loading, the wkey was not
		 * consumed, and needs to be freed.
		 */
		dsl_crypto_params_free(dcp, (err != 0));
		fnvlist_free(crypto_args);

		if (err == EINVAL) {
			/*
			 * We couldn't load a key for this dataset so try
			 * the parent. This loop will eventually hit the
			 * encryption root since ztest only makes clones
			 * as children of their origin datasets.
			 */
			cp = strrchr(ddname, '/');
			if (cp == NULL)
				return (err);

			*cp = '\0';
			err = EACCES;
			continue;
		} else if (err != 0) {
			break;
		}

		err = dmu_objset_own(name, type, readonly, decrypt, tag, osp);
		break;
	}

	return (err);
}

static void
ztest_rll_init(rll_t *rll)
{
	rll->rll_writer = NULL;
	rll->rll_readers = 0;
	mutex_init(&rll->rll_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&rll->rll_cv, NULL, CV_DEFAULT, NULL);
}

static void
ztest_rll_destroy(rll_t *rll)
{
	ASSERT3P(rll->rll_writer, ==, NULL);
	ASSERT0(rll->rll_readers);
	mutex_destroy(&rll->rll_lock);
	cv_destroy(&rll->rll_cv);
}

static void
ztest_rll_lock(rll_t *rll, rl_type_t type)
{
	mutex_enter(&rll->rll_lock);

	if (type == ZTRL_READER) {
		while (rll->rll_writer != NULL)
			(void) cv_wait(&rll->rll_cv, &rll->rll_lock);
		rll->rll_readers++;
	} else {
		while (rll->rll_writer != NULL || rll->rll_readers)
			(void) cv_wait(&rll->rll_cv, &rll->rll_lock);
		rll->rll_writer = curthread;
	}

	mutex_exit(&rll->rll_lock);
}

static void
ztest_rll_unlock(rll_t *rll)
{
	mutex_enter(&rll->rll_lock);

	if (rll->rll_writer) {
		ASSERT0(rll->rll_readers);
		rll->rll_writer = NULL;
	} else {
		ASSERT3S(rll->rll_readers, >, 0);
		ASSERT3P(rll->rll_writer, ==, NULL);
		rll->rll_readers--;
	}

	if (rll->rll_writer == NULL && rll->rll_readers == 0)
		cv_broadcast(&rll->rll_cv);

	mutex_exit(&rll->rll_lock);
}

static void
ztest_object_lock(ztest_ds_t *zd, uint64_t object, rl_type_t type)
{
	rll_t *rll = &zd->zd_object_lock[object & (ZTEST_OBJECT_LOCKS - 1)];

	ztest_rll_lock(rll, type);
}

static void
ztest_object_unlock(ztest_ds_t *zd, uint64_t object)
{
	rll_t *rll = &zd->zd_object_lock[object & (ZTEST_OBJECT_LOCKS - 1)];

	ztest_rll_unlock(rll);
}

static rl_t *
ztest_range_lock(ztest_ds_t *zd, uint64_t object, uint64_t offset,
    uint64_t size, rl_type_t type)
{
	uint64_t hash = object ^ (offset % (ZTEST_RANGE_LOCKS + 1));
	rll_t *rll = &zd->zd_range_lock[hash & (ZTEST_RANGE_LOCKS - 1)];
	rl_t *rl;

	rl = umem_alloc(sizeof (*rl), UMEM_NOFAIL);
	rl->rl_object = object;
	rl->rl_offset = offset;
	rl->rl_size = size;
	rl->rl_lock = rll;

	ztest_rll_lock(rll, type);

	return (rl);
}

static void
ztest_range_unlock(rl_t *rl)
{
	rll_t *rll = rl->rl_lock;

	ztest_rll_unlock(rll);

	umem_free(rl, sizeof (*rl));
}

static void
ztest_zd_init(ztest_ds_t *zd, ztest_shared_ds_t *szd, objset_t *os)
{
	zd->zd_os = os;
	zd->zd_zilog = dmu_objset_zil(os);
	zd->zd_shared = szd;
	dmu_objset_name(os, zd->zd_name);
	int l;

	if (zd->zd_shared != NULL)
		zd->zd_shared->zd_seq = 0;

	VERIFY0(pthread_rwlock_init(&zd->zd_zilog_lock, NULL));
	mutex_init(&zd->zd_dirobj_lock, NULL, MUTEX_DEFAULT, NULL);

	for (l = 0; l < ZTEST_OBJECT_LOCKS; l++)
		ztest_rll_init(&zd->zd_object_lock[l]);

	for (l = 0; l < ZTEST_RANGE_LOCKS; l++)
		ztest_rll_init(&zd->zd_range_lock[l]);
}

static void
ztest_zd_fini(ztest_ds_t *zd)
{
	int l;

	mutex_destroy(&zd->zd_dirobj_lock);
	(void) pthread_rwlock_destroy(&zd->zd_zilog_lock);

	for (l = 0; l < ZTEST_OBJECT_LOCKS; l++)
		ztest_rll_destroy(&zd->zd_object_lock[l]);

	for (l = 0; l < ZTEST_RANGE_LOCKS; l++)
		ztest_rll_destroy(&zd->zd_range_lock[l]);
}

#define	TXG_MIGHTWAIT	(ztest_random(10) == 0 ? TXG_NOWAIT : TXG_WAIT)

static uint64_t
ztest_tx_assign(dmu_tx_t *tx, uint64_t txg_how, const char *tag)
{
	uint64_t txg;
	int error;

	/*
	 * Attempt to assign tx to some transaction group.
	 */
	error = dmu_tx_assign(tx, txg_how);
	if (error) {
		if (error == ERESTART) {
			ASSERT3U(txg_how, ==, TXG_NOWAIT);
			dmu_tx_wait(tx);
		} else {
			ASSERT3U(error, ==, ENOSPC);
			ztest_record_enospc(tag);
		}
		dmu_tx_abort(tx);
		return (0);
	}
	txg = dmu_tx_get_txg(tx);
	ASSERT3U(txg, !=, 0);
	return (txg);
}

static void
ztest_bt_generate(ztest_block_tag_t *bt, objset_t *os, uint64_t object,
    uint64_t dnodesize, uint64_t offset, uint64_t gen, uint64_t txg,
    uint64_t crtxg)
{
	bt->bt_magic = BT_MAGIC;
	bt->bt_objset = dmu_objset_id(os);
	bt->bt_object = object;
	bt->bt_dnodesize = dnodesize;
	bt->bt_offset = offset;
	bt->bt_gen = gen;
	bt->bt_txg = txg;
	bt->bt_crtxg = crtxg;
}

static void
ztest_bt_verify(ztest_block_tag_t *bt, objset_t *os, uint64_t object,
    uint64_t dnodesize, uint64_t offset, uint64_t gen, uint64_t txg,
    uint64_t crtxg)
{
	ASSERT3U(bt->bt_magic, ==, BT_MAGIC);
	ASSERT3U(bt->bt_objset, ==, dmu_objset_id(os));
	ASSERT3U(bt->bt_object, ==, object);
	ASSERT3U(bt->bt_dnodesize, ==, dnodesize);
	ASSERT3U(bt->bt_offset, ==, offset);
	ASSERT3U(bt->bt_gen, <=, gen);
	ASSERT3U(bt->bt_txg, <=, txg);
	ASSERT3U(bt->bt_crtxg, ==, crtxg);
}

static ztest_block_tag_t *
ztest_bt_bonus(dmu_buf_t *db)
{
	dmu_object_info_t doi;
	ztest_block_tag_t *bt;

	dmu_object_info_from_db(db, &doi);
	ASSERT3U(doi.doi_bonus_size, <=, db->db_size);
	ASSERT3U(doi.doi_bonus_size, >=, sizeof (*bt));
	bt = (void *)((char *)db->db_data + doi.doi_bonus_size - sizeof (*bt));

	return (bt);
}

/*
 * Generate a token to fill up unused bonus buffer space.  Try to make
 * it unique to the object, generation, and offset to verify that data
 * is not getting overwritten by data from other dnodes.
 */
#define	ZTEST_BONUS_FILL_TOKEN(obj, ds, gen, offset) \
	(((ds) << 48) | ((gen) << 32) | ((obj) << 8) | (offset))

/*
 * Fill up the unused bonus buffer region before the block tag with a
 * verifiable pattern. Filling the whole bonus area with non-zero data
 * helps ensure that all dnode traversal code properly skips the
 * interior regions of large dnodes.
 */
static void
ztest_fill_unused_bonus(dmu_buf_t *db, void *end, uint64_t obj,
    objset_t *os, uint64_t gen)
{
	uint64_t *bonusp;

	ASSERT(IS_P2ALIGNED((char *)end - (char *)db->db_data, 8));

	for (bonusp = db->db_data; bonusp < (uint64_t *)end; bonusp++) {
		uint64_t token = ZTEST_BONUS_FILL_TOKEN(obj, dmu_objset_id(os),
		    gen, bonusp - (uint64_t *)db->db_data);
		*bonusp = token;
	}
}

/*
 * Verify that the unused area of a bonus buffer is filled with the
 * expected tokens.
 */
static void
ztest_verify_unused_bonus(dmu_buf_t *db, void *end, uint64_t obj,
    objset_t *os, uint64_t gen)
{
	uint64_t *bonusp;

	for (bonusp = db->db_data; bonusp < (uint64_t *)end; bonusp++) {
		uint64_t token = ZTEST_BONUS_FILL_TOKEN(obj, dmu_objset_id(os),
		    gen, bonusp - (uint64_t *)db->db_data);
		VERIFY3U(*bonusp, ==, token);
	}
}

/*
 * ZIL logging ops
 */

#define	lrz_type	lr_mode
#define	lrz_blocksize	lr_uid
#define	lrz_ibshift	lr_gid
#define	lrz_bonustype	lr_rdev
#define	lrz_dnodesize	lr_crtime[1]

static void
ztest_log_create(ztest_ds_t *zd, dmu_tx_t *tx, lr_create_t *lr)
{
	char *name = (void *)(lr + 1);		/* name follows lr */
	size_t namesize = strlen(name) + 1;
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_CREATE, sizeof (*lr) + namesize);
	memcpy(&itx->itx_lr + 1, &lr->lr_common + 1,
	    sizeof (*lr) + namesize - sizeof (lr_t));

	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_remove(ztest_ds_t *zd, dmu_tx_t *tx, lr_remove_t *lr, uint64_t object)
{
	char *name = (void *)(lr + 1);		/* name follows lr */
	size_t namesize = strlen(name) + 1;
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_REMOVE, sizeof (*lr) + namesize);
	memcpy(&itx->itx_lr + 1, &lr->lr_common + 1,
	    sizeof (*lr) + namesize - sizeof (lr_t));

	itx->itx_oid = object;
	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_write(ztest_ds_t *zd, dmu_tx_t *tx, lr_write_t *lr)
{
	itx_t *itx;
	itx_wr_state_t write_state = ztest_random(WR_NUM_STATES);

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	if (lr->lr_length > zil_max_log_data(zd->zd_zilog, sizeof (lr_write_t)))
		write_state = WR_INDIRECT;

	itx = zil_itx_create(TX_WRITE,
	    sizeof (*lr) + (write_state == WR_COPIED ? lr->lr_length : 0));

	if (write_state == WR_COPIED &&
	    dmu_read(zd->zd_os, lr->lr_foid, lr->lr_offset, lr->lr_length,
	    ((lr_write_t *)&itx->itx_lr) + 1, DMU_READ_NO_PREFETCH) != 0) {
		zil_itx_destroy(itx);
		itx = zil_itx_create(TX_WRITE, sizeof (*lr));
		write_state = WR_NEED_COPY;
	}
	itx->itx_private = zd;
	itx->itx_wr_state = write_state;
	itx->itx_sync = (ztest_random(8) == 0);

	memcpy(&itx->itx_lr + 1, &lr->lr_common + 1,
	    sizeof (*lr) - sizeof (lr_t));

	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_truncate(ztest_ds_t *zd, dmu_tx_t *tx, lr_truncate_t *lr)
{
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_TRUNCATE, sizeof (*lr));
	memcpy(&itx->itx_lr + 1, &lr->lr_common + 1,
	    sizeof (*lr) - sizeof (lr_t));

	itx->itx_sync = B_FALSE;
	zil_itx_assign(zd->zd_zilog, itx, tx);
}

static void
ztest_log_setattr(ztest_ds_t *zd, dmu_tx_t *tx, lr_setattr_t *lr)
{
	itx_t *itx;

	if (zil_replaying(zd->zd_zilog, tx))
		return;

	itx = zil_itx_create(TX_SETATTR, sizeof (*lr));
	memcpy(&itx->itx_lr + 1, &lr->lr_common + 1,
	    sizeof (*lr) - sizeof (lr_t));

	itx->itx_sync = B_FALSE;
	zil_itx_assign(zd->zd_zilog, itx, tx);
}

/*
 * ZIL replay ops
 */
static int
ztest_replay_create(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_create_t *lr = arg2;
	char *name = (void *)(lr + 1);		/* name follows lr */
	objset_t *os = zd->zd_os;
	ztest_block_tag_t *bbt;
	dmu_buf_t *db;
	dmu_tx_t *tx;
	uint64_t txg;
	int error = 0;
	int bonuslen;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ASSERT3U(lr->lr_doid, ==, ZTEST_DIROBJ);
	ASSERT3S(name[0], !=, '\0');

	tx = dmu_tx_create(os);

	dmu_tx_hold_zap(tx, lr->lr_doid, B_TRUE, name);

	if (lr->lrz_type == DMU_OT_ZAP_OTHER) {
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	} else {
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
	}

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0)
		return (ENOSPC);

	ASSERT3U(dmu_objset_zil(os)->zl_replay, ==, !!lr->lr_foid);
	bonuslen = DN_BONUS_SIZE(lr->lrz_dnodesize);

	if (lr->lrz_type == DMU_OT_ZAP_OTHER) {
		if (lr->lr_foid == 0) {
			lr->lr_foid = zap_create_dnsize(os,
			    lr->lrz_type, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		} else {
			error = zap_create_claim_dnsize(os, lr->lr_foid,
			    lr->lrz_type, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		}
	} else {
		if (lr->lr_foid == 0) {
			lr->lr_foid = dmu_object_alloc_dnsize(os,
			    lr->lrz_type, 0, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		} else {
			error = dmu_object_claim_dnsize(os, lr->lr_foid,
			    lr->lrz_type, 0, lr->lrz_bonustype,
			    bonuslen, lr->lrz_dnodesize, tx);
		}
	}

	if (error) {
		ASSERT3U(error, ==, EEXIST);
		ASSERT(zd->zd_zilog->zl_replay);
		dmu_tx_commit(tx);
		return (error);
	}

	ASSERT3U(lr->lr_foid, !=, 0);

	if (lr->lrz_type != DMU_OT_ZAP_OTHER)
		VERIFY0(dmu_object_set_blocksize(os, lr->lr_foid,
		    lr->lrz_blocksize, lr->lrz_ibshift, tx));

	VERIFY0(dmu_bonus_hold(os, lr->lr_foid, FTAG, &db));
	bbt = ztest_bt_bonus(db);
	dmu_buf_will_dirty(db, tx);
	ztest_bt_generate(bbt, os, lr->lr_foid, lr->lrz_dnodesize, -1ULL,
	    lr->lr_gen, txg, txg);
	ztest_fill_unused_bonus(db, bbt, lr->lr_foid, os, lr->lr_gen);
	dmu_buf_rele(db, FTAG);

	VERIFY0(zap_add(os, lr->lr_doid, name, sizeof (uint64_t), 1,
	    &lr->lr_foid, tx));

	(void) ztest_log_create(zd, tx, lr);

	dmu_tx_commit(tx);

	return (0);
}

static int
ztest_replay_remove(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_remove_t *lr = arg2;
	char *name = (void *)(lr + 1);		/* name follows lr */
	objset_t *os = zd->zd_os;
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	uint64_t object, txg;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ASSERT3U(lr->lr_doid, ==, ZTEST_DIROBJ);
	ASSERT3S(name[0], !=, '\0');

	VERIFY0(
	    zap_lookup(os, lr->lr_doid, name, sizeof (object), 1, &object));
	ASSERT3U(object, !=, 0);

	ztest_object_lock(zd, object, ZTRL_WRITER);

	VERIFY0(dmu_object_info(os, object, &doi));

	tx = dmu_tx_create(os);

	dmu_tx_hold_zap(tx, lr->lr_doid, B_FALSE, name);
	dmu_tx_hold_free(tx, object, 0, DMU_OBJECT_END);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		ztest_object_unlock(zd, object);
		return (ENOSPC);
	}

	if (doi.doi_type == DMU_OT_ZAP_OTHER) {
		VERIFY0(zap_destroy(os, object, tx));
	} else {
		VERIFY0(dmu_object_free(os, object, tx));
	}

	VERIFY0(zap_remove(os, lr->lr_doid, name, tx));

	(void) ztest_log_remove(zd, tx, lr, object);

	dmu_tx_commit(tx);

	ztest_object_unlock(zd, object);

	return (0);
}

static int
ztest_replay_write(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_write_t *lr = arg2;
	objset_t *os = zd->zd_os;
	void *data = lr + 1;			/* data follows lr */
	uint64_t offset, length;
	ztest_block_tag_t *bt = data;
	ztest_block_tag_t *bbt;
	uint64_t gen, txg, lrtxg, crtxg;
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	dmu_buf_t *db;
	arc_buf_t *abuf = NULL;
	rl_t *rl;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	/* If it's a dmu_sync() block, write the whole block */
	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		uint64_t blocksize = BP_GET_LSIZE(&lr->lr_blkptr);
		if (length < blocksize) {
			offset -= offset % blocksize;
			length = blocksize;
		}
	}

	if (bt->bt_magic == BSWAP_64(BT_MAGIC))
		byteswap_uint64_array(bt, sizeof (*bt));

	if (bt->bt_magic != BT_MAGIC)
		bt = NULL;

	ztest_object_lock(zd, lr->lr_foid, ZTRL_READER);
	rl = ztest_range_lock(zd, lr->lr_foid, offset, length, ZTRL_WRITER);

	VERIFY0(dmu_bonus_hold(os, lr->lr_foid, FTAG, &db));

	dmu_object_info_from_db(db, &doi);

	bbt = ztest_bt_bonus(db);
	ASSERT3U(bbt->bt_magic, ==, BT_MAGIC);
	gen = bbt->bt_gen;
	crtxg = bbt->bt_crtxg;
	lrtxg = lr->lr_common.lrc_txg;

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, lr->lr_foid, offset, length);

	if (ztest_random(8) == 0 && length == doi.doi_data_block_size &&
	    P2PHASE(offset, length) == 0)
		abuf = dmu_request_arcbuf(db, length);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		if (abuf != NULL)
			dmu_return_arcbuf(abuf);
		dmu_buf_rele(db, FTAG);
		ztest_range_unlock(rl);
		ztest_object_unlock(zd, lr->lr_foid);
		return (ENOSPC);
	}

	if (bt != NULL) {
		/*
		 * Usually, verify the old data before writing new data --
		 * but not always, because we also want to verify correct
		 * behavior when the data was not recently read into cache.
		 */
		ASSERT(doi.doi_data_block_size);
		ASSERT0(offset % doi.doi_data_block_size);
		if (ztest_random(4) != 0) {
			int prefetch = ztest_random(2) ?
			    DMU_READ_PREFETCH : DMU_READ_NO_PREFETCH;
			ztest_block_tag_t rbt;

			VERIFY(dmu_read(os, lr->lr_foid, offset,
			    sizeof (rbt), &rbt, prefetch) == 0);
			if (rbt.bt_magic == BT_MAGIC) {
				ztest_bt_verify(&rbt, os, lr->lr_foid, 0,
				    offset, gen, txg, crtxg);
			}
		}

		/*
		 * Writes can appear to be newer than the bonus buffer because
		 * the ztest_get_data() callback does a dmu_read() of the
		 * open-context data, which may be different than the data
		 * as it was when the write was generated.
		 */
		if (zd->zd_zilog->zl_replay) {
			ztest_bt_verify(bt, os, lr->lr_foid, 0, offset,
			    MAX(gen, bt->bt_gen), MAX(txg, lrtxg),
			    bt->bt_crtxg);
		}

		/*
		 * Set the bt's gen/txg to the bonus buffer's gen/txg
		 * so that all of the usual ASSERTs will work.
		 */
		ztest_bt_generate(bt, os, lr->lr_foid, 0, offset, gen, txg,
		    crtxg);
	}

	if (abuf == NULL) {
		dmu_write(os, lr->lr_foid, offset, length, data, tx);
	} else {
		memcpy(abuf->b_data, data, length);
		VERIFY0(dmu_assign_arcbuf_by_dbuf(db, offset, abuf, tx));
	}

	(void) ztest_log_write(zd, tx, lr);

	dmu_buf_rele(db, FTAG);

	dmu_tx_commit(tx);

	ztest_range_unlock(rl);
	ztest_object_unlock(zd, lr->lr_foid);

	return (0);
}

static int
ztest_replay_truncate(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_truncate_t *lr = arg2;
	objset_t *os = zd->zd_os;
	dmu_tx_t *tx;
	uint64_t txg;
	rl_t *rl;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ztest_object_lock(zd, lr->lr_foid, ZTRL_READER);
	rl = ztest_range_lock(zd, lr->lr_foid, lr->lr_offset, lr->lr_length,
	    ZTRL_WRITER);

	tx = dmu_tx_create(os);

	dmu_tx_hold_free(tx, lr->lr_foid, lr->lr_offset, lr->lr_length);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		ztest_range_unlock(rl);
		ztest_object_unlock(zd, lr->lr_foid);
		return (ENOSPC);
	}

	VERIFY0(dmu_free_range(os, lr->lr_foid, lr->lr_offset,
	    lr->lr_length, tx));

	(void) ztest_log_truncate(zd, tx, lr);

	dmu_tx_commit(tx);

	ztest_range_unlock(rl);
	ztest_object_unlock(zd, lr->lr_foid);

	return (0);
}

static int
ztest_replay_setattr(void *arg1, void *arg2, boolean_t byteswap)
{
	ztest_ds_t *zd = arg1;
	lr_setattr_t *lr = arg2;
	objset_t *os = zd->zd_os;
	dmu_tx_t *tx;
	dmu_buf_t *db;
	ztest_block_tag_t *bbt;
	uint64_t txg, lrtxg, crtxg, dnodesize;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	ztest_object_lock(zd, lr->lr_foid, ZTRL_WRITER);

	VERIFY0(dmu_bonus_hold(os, lr->lr_foid, FTAG, &db));

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, lr->lr_foid);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);
	if (txg == 0) {
		dmu_buf_rele(db, FTAG);
		ztest_object_unlock(zd, lr->lr_foid);
		return (ENOSPC);
	}

	bbt = ztest_bt_bonus(db);
	ASSERT3U(bbt->bt_magic, ==, BT_MAGIC);
	crtxg = bbt->bt_crtxg;
	lrtxg = lr->lr_common.lrc_txg;
	dnodesize = bbt->bt_dnodesize;

	if (zd->zd_zilog->zl_replay) {
		ASSERT3U(lr->lr_size, !=, 0);
		ASSERT3U(lr->lr_mode, !=, 0);
		ASSERT3U(lrtxg, !=, 0);
	} else {
		/*
		 * Randomly change the size and increment the generation.
		 */
		lr->lr_size = (ztest_random(db->db_size / sizeof (*bbt)) + 1) *
		    sizeof (*bbt);
		lr->lr_mode = bbt->bt_gen + 1;
		ASSERT0(lrtxg);
	}

	/*
	 * Verify that the current bonus buffer is not newer than our txg.
	 */
	ztest_bt_verify(bbt, os, lr->lr_foid, dnodesize, -1ULL, lr->lr_mode,
	    MAX(txg, lrtxg), crtxg);

	dmu_buf_will_dirty(db, tx);

	ASSERT3U(lr->lr_size, >=, sizeof (*bbt));
	ASSERT3U(lr->lr_size, <=, db->db_size);
	VERIFY0(dmu_set_bonus(db, lr->lr_size, tx));
	bbt = ztest_bt_bonus(db);

	ztest_bt_generate(bbt, os, lr->lr_foid, dnodesize, -1ULL, lr->lr_mode,
	    txg, crtxg);
	ztest_fill_unused_bonus(db, bbt, lr->lr_foid, os, bbt->bt_gen);
	dmu_buf_rele(db, FTAG);

	(void) ztest_log_setattr(zd, tx, lr);

	dmu_tx_commit(tx);

	ztest_object_unlock(zd, lr->lr_foid);

	return (0);
}

static zil_replay_func_t *ztest_replay_vector[TX_MAX_TYPE] = {
	NULL,			/* 0 no such transaction type */
	ztest_replay_create,	/* TX_CREATE */
	NULL,			/* TX_MKDIR */
	NULL,			/* TX_MKXATTR */
	NULL,			/* TX_SYMLINK */
	ztest_replay_remove,	/* TX_REMOVE */
	NULL,			/* TX_RMDIR */
	NULL,			/* TX_LINK */
	NULL,			/* TX_RENAME */
	ztest_replay_write,	/* TX_WRITE */
	ztest_replay_truncate,	/* TX_TRUNCATE */
	ztest_replay_setattr,	/* TX_SETATTR */
	NULL,			/* TX_ACL */
	NULL,			/* TX_CREATE_ACL */
	NULL,			/* TX_CREATE_ATTR */
	NULL,			/* TX_CREATE_ACL_ATTR */
	NULL,			/* TX_MKDIR_ACL */
	NULL,			/* TX_MKDIR_ATTR */
	NULL,			/* TX_MKDIR_ACL_ATTR */
	NULL,			/* TX_WRITE2 */
	NULL,			/* TX_SETSAXATTR */
	NULL,			/* TX_RENAME_EXCHANGE */
	NULL,			/* TX_RENAME_WHITEOUT */
};

/*
 * ZIL get_data callbacks
 */

static void
ztest_get_done(zgd_t *zgd, int error)
{
	(void) error;
	ztest_ds_t *zd = zgd->zgd_private;
	uint64_t object = ((rl_t *)zgd->zgd_lr)->rl_object;

	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	ztest_range_unlock((rl_t *)zgd->zgd_lr);
	ztest_object_unlock(zd, object);

	umem_free(zgd, sizeof (*zgd));
}

static int
ztest_get_data(void *arg, uint64_t arg2, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio)
{
	(void) arg2;
	ztest_ds_t *zd = arg;
	objset_t *os = zd->zd_os;
	uint64_t object = lr->lr_foid;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	uint64_t txg = lr->lr_common.lrc_txg;
	uint64_t crtxg;
	dmu_object_info_t doi;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3U(size, !=, 0);

	ztest_object_lock(zd, object, ZTRL_READER);
	error = dmu_bonus_hold(os, object, FTAG, &db);
	if (error) {
		ztest_object_unlock(zd, object);
		return (error);
	}

	crtxg = ztest_bt_bonus(db)->bt_crtxg;

	if (crtxg == 0 || crtxg > txg) {
		dmu_buf_rele(db, FTAG);
		ztest_object_unlock(zd, object);
		return (ENOENT);
	}

	dmu_object_info_from_db(db, &doi);
	dmu_buf_rele(db, FTAG);
	db = NULL;

	zgd = umem_zalloc(sizeof (*zgd), UMEM_NOFAIL);
	zgd->zgd_lwb = lwb;
	zgd->zgd_private = zd;

	if (buf != NULL) {	/* immediate write */
		zgd->zgd_lr = (struct zfs_locked_range *)ztest_range_lock(zd,
		    object, offset, size, ZTRL_READER);

		error = dmu_read(os, object, offset, size, buf,
		    DMU_READ_NO_PREFETCH);
		ASSERT0(error);
	} else {
		ASSERT3P(zio, !=, NULL);
		size = doi.doi_data_block_size;
		if (ISP2(size)) {
			offset = P2ALIGN(offset, size);
		} else {
			ASSERT3U(offset, <, size);
			offset = 0;
		}

		zgd->zgd_lr = (struct zfs_locked_range *)ztest_range_lock(zd,
		    object, offset, size, ZTRL_READER);

		error = dmu_buf_hold_noread(os, object, offset, zgd, &db);

		if (error == 0) {
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT3U(db->db_offset, ==, offset);
			ASSERT3U(db->db_size, ==, size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    ztest_get_done, zgd);

			if (error == 0)
				return (0);
		}
	}

	ztest_get_done(zgd, error);

	return (error);
}

static void *
ztest_lr_alloc(size_t lrsize, char *name)
{
	char *lr;
	size_t namesize = name ? strlen(name) + 1 : 0;

	lr = umem_zalloc(lrsize + namesize, UMEM_NOFAIL);

	if (name)
		memcpy(lr + lrsize, name, namesize);

	return (lr);
}

static void
ztest_lr_free(void *lr, size_t lrsize, char *name)
{
	size_t namesize = name ? strlen(name) + 1 : 0;

	umem_free(lr, lrsize + namesize);
}

/*
 * Lookup a bunch of objects.  Returns the number of objects not found.
 */
static int
ztest_lookup(ztest_ds_t *zd, ztest_od_t *od, int count)
{
	int missing = 0;
	int error;
	int i;

	ASSERT(MUTEX_HELD(&zd->zd_dirobj_lock));

	for (i = 0; i < count; i++, od++) {
		od->od_object = 0;
		error = zap_lookup(zd->zd_os, od->od_dir, od->od_name,
		    sizeof (uint64_t), 1, &od->od_object);
		if (error) {
			ASSERT3S(error, ==, ENOENT);
			ASSERT0(od->od_object);
			missing++;
		} else {
			dmu_buf_t *db;
			ztest_block_tag_t *bbt;
			dmu_object_info_t doi;

			ASSERT3U(od->od_object, !=, 0);
			ASSERT0(missing);	/* there should be no gaps */

			ztest_object_lock(zd, od->od_object, ZTRL_READER);
			VERIFY0(dmu_bonus_hold(zd->zd_os, od->od_object,
			    FTAG, &db));
			dmu_object_info_from_db(db, &doi);
			bbt = ztest_bt_bonus(db);
			ASSERT3U(bbt->bt_magic, ==, BT_MAGIC);
			od->od_type = doi.doi_type;
			od->od_blocksize = doi.doi_data_block_size;
			od->od_gen = bbt->bt_gen;
			dmu_buf_rele(db, FTAG);
			ztest_object_unlock(zd, od->od_object);
		}
	}

	return (missing);
}

static int
ztest_create(ztest_ds_t *zd, ztest_od_t *od, int count)
{
	int missing = 0;
	int i;

	ASSERT(MUTEX_HELD(&zd->zd_dirobj_lock));

	for (i = 0; i < count; i++, od++) {
		if (missing) {
			od->od_object = 0;
			missing++;
			continue;
		}

		lr_create_t *lr = ztest_lr_alloc(sizeof (*lr), od->od_name);

		lr->lr_doid = od->od_dir;
		lr->lr_foid = 0;	/* 0 to allocate, > 0 to claim */
		lr->lrz_type = od->od_crtype;
		lr->lrz_blocksize = od->od_crblocksize;
		lr->lrz_ibshift = ztest_random_ibshift();
		lr->lrz_bonustype = DMU_OT_UINT64_OTHER;
		lr->lrz_dnodesize = od->od_crdnodesize;
		lr->lr_gen = od->od_crgen;
		lr->lr_crtime[0] = time(NULL);

		if (ztest_replay_create(zd, lr, B_FALSE) != 0) {
			ASSERT0(missing);
			od->od_object = 0;
			missing++;
		} else {
			od->od_object = lr->lr_foid;
			od->od_type = od->od_crtype;
			od->od_blocksize = od->od_crblocksize;
			od->od_gen = od->od_crgen;
			ASSERT3U(od->od_object, !=, 0);
		}

		ztest_lr_free(lr, sizeof (*lr), od->od_name);
	}

	return (missing);
}

static int
ztest_remove(ztest_ds_t *zd, ztest_od_t *od, int count)
{
	int missing = 0;
	int error;
	int i;

	ASSERT(MUTEX_HELD(&zd->zd_dirobj_lock));

	od += count - 1;

	for (i = count - 1; i >= 0; i--, od--) {
		if (missing) {
			missing++;
			continue;
		}

		/*
		 * No object was found.
		 */
		if (od->od_object == 0)
			continue;

		lr_remove_t *lr = ztest_lr_alloc(sizeof (*lr), od->od_name);

		lr->lr_doid = od->od_dir;

		if ((error = ztest_replay_remove(zd, lr, B_FALSE)) != 0) {
			ASSERT3U(error, ==, ENOSPC);
			missing++;
		} else {
			od->od_object = 0;
		}
		ztest_lr_free(lr, sizeof (*lr), od->od_name);
	}

	return (missing);
}

static int
ztest_write(ztest_ds_t *zd, uint64_t object, uint64_t offset, uint64_t size,
    const void *data)
{
	lr_write_t *lr;
	int error;

	lr = ztest_lr_alloc(sizeof (*lr) + size, NULL);

	lr->lr_foid = object;
	lr->lr_offset = offset;
	lr->lr_length = size;
	lr->lr_blkoff = 0;
	BP_ZERO(&lr->lr_blkptr);

	memcpy(lr + 1, data, size);

	error = ztest_replay_write(zd, lr, B_FALSE);

	ztest_lr_free(lr, sizeof (*lr) + size, NULL);

	return (error);
}

static int
ztest_truncate(ztest_ds_t *zd, uint64_t object, uint64_t offset, uint64_t size)
{
	lr_truncate_t *lr;
	int error;

	lr = ztest_lr_alloc(sizeof (*lr), NULL);

	lr->lr_foid = object;
	lr->lr_offset = offset;
	lr->lr_length = size;

	error = ztest_replay_truncate(zd, lr, B_FALSE);

	ztest_lr_free(lr, sizeof (*lr), NULL);

	return (error);
}

static int
ztest_setattr(ztest_ds_t *zd, uint64_t object)
{
	lr_setattr_t *lr;
	int error;

	lr = ztest_lr_alloc(sizeof (*lr), NULL);

	lr->lr_foid = object;
	lr->lr_size = 0;
	lr->lr_mode = 0;

	error = ztest_replay_setattr(zd, lr, B_FALSE);

	ztest_lr_free(lr, sizeof (*lr), NULL);

	return (error);
}

static void
ztest_prealloc(ztest_ds_t *zd, uint64_t object, uint64_t offset, uint64_t size)
{
	objset_t *os = zd->zd_os;
	dmu_tx_t *tx;
	uint64_t txg;
	rl_t *rl;

	txg_wait_synced(dmu_objset_pool(os), 0);

	ztest_object_lock(zd, object, ZTRL_READER);
	rl = ztest_range_lock(zd, object, offset, size, ZTRL_WRITER);

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, object, offset, size);

	txg = ztest_tx_assign(tx, TXG_WAIT, FTAG);

	if (txg != 0) {
		dmu_prealloc(os, object, offset, size, tx);
		dmu_tx_commit(tx);
		txg_wait_synced(dmu_objset_pool(os), txg);
	} else {
		(void) dmu_free_long_range(os, object, offset, size);
	}

	ztest_range_unlock(rl);
	ztest_object_unlock(zd, object);
}

static void
ztest_io(ztest_ds_t *zd, uint64_t object, uint64_t offset)
{
	int err;
	ztest_block_tag_t wbt;
	dmu_object_info_t doi;
	enum ztest_io_type io_type;
	uint64_t blocksize;
	void *data;

	VERIFY0(dmu_object_info(zd->zd_os, object, &doi));
	blocksize = doi.doi_data_block_size;
	data = umem_alloc(blocksize, UMEM_NOFAIL);

	/*
	 * Pick an i/o type at random, biased toward writing block tags.
	 */
	io_type = ztest_random(ZTEST_IO_TYPES);
	if (ztest_random(2) == 0)
		io_type = ZTEST_IO_WRITE_TAG;

	(void) pthread_rwlock_rdlock(&zd->zd_zilog_lock);

	switch (io_type) {

	case ZTEST_IO_WRITE_TAG:
		ztest_bt_generate(&wbt, zd->zd_os, object, doi.doi_dnodesize,
		    offset, 0, 0, 0);
		(void) ztest_write(zd, object, offset, sizeof (wbt), &wbt);
		break;

	case ZTEST_IO_WRITE_PATTERN:
		(void) memset(data, 'a' + (object + offset) % 5, blocksize);
		if (ztest_random(2) == 0) {
			/*
			 * Induce fletcher2 collisions to ensure that
			 * zio_ddt_collision() detects and resolves them
			 * when using fletcher2-verify for deduplication.
			 */
			((uint64_t *)data)[0] ^= 1ULL << 63;
			((uint64_t *)data)[4] ^= 1ULL << 63;
		}
		(void) ztest_write(zd, object, offset, blocksize, data);
		break;

	case ZTEST_IO_WRITE_ZEROES:
		memset(data, 0, blocksize);
		(void) ztest_write(zd, object, offset, blocksize, data);
		break;

	case ZTEST_IO_TRUNCATE:
		(void) ztest_truncate(zd, object, offset, blocksize);
		break;

	case ZTEST_IO_SETATTR:
		(void) ztest_setattr(zd, object);
		break;
	default:
		break;

	case ZTEST_IO_REWRITE:
		(void) pthread_rwlock_rdlock(&ztest_name_lock);
		err = ztest_dsl_prop_set_uint64(zd->zd_name,
		    ZFS_PROP_CHECKSUM, spa_dedup_checksum(ztest_spa),
		    B_FALSE);
		ASSERT(err == 0 || err == ENOSPC);
		err = ztest_dsl_prop_set_uint64(zd->zd_name,
		    ZFS_PROP_COMPRESSION,
		    ztest_random_dsl_prop(ZFS_PROP_COMPRESSION),
		    B_FALSE);
		ASSERT(err == 0 || err == ENOSPC);
		(void) pthread_rwlock_unlock(&ztest_name_lock);

		VERIFY0(dmu_read(zd->zd_os, object, offset, blocksize, data,
		    DMU_READ_NO_PREFETCH));

		(void) ztest_write(zd, object, offset, blocksize, data);
		break;
	}

	(void) pthread_rwlock_unlock(&zd->zd_zilog_lock);

	umem_free(data, blocksize);
}

/*
 * Initialize an object description template.
 */
static void
ztest_od_init(ztest_od_t *od, uint64_t id, const char *tag, uint64_t index,
    dmu_object_type_t type, uint64_t blocksize, uint64_t dnodesize,
    uint64_t gen)
{
	od->od_dir = ZTEST_DIROBJ;
	od->od_object = 0;

	od->od_crtype = type;
	od->od_crblocksize = blocksize ? blocksize : ztest_random_blocksize();
	od->od_crdnodesize = dnodesize ? dnodesize : ztest_random_dnodesize();
	od->od_crgen = gen;

	od->od_type = DMU_OT_NONE;
	od->od_blocksize = 0;
	od->od_gen = 0;

	(void) snprintf(od->od_name, sizeof (od->od_name),
	    "%s(%"PRId64")[%"PRIu64"]",
	    tag, id, index);
}

/*
 * Lookup or create the objects for a test using the od template.
 * If the objects do not all exist, or if 'remove' is specified,
 * remove any existing objects and create new ones.  Otherwise,
 * use the existing objects.
 */
static int
ztest_object_init(ztest_ds_t *zd, ztest_od_t *od, size_t size, boolean_t remove)
{
	int count = size / sizeof (*od);
	int rv = 0;

	mutex_enter(&zd->zd_dirobj_lock);
	if ((ztest_lookup(zd, od, count) != 0 || remove) &&
	    (ztest_remove(zd, od, count) != 0 ||
	    ztest_create(zd, od, count) != 0))
		rv = -1;
	zd->zd_od = od;
	mutex_exit(&zd->zd_dirobj_lock);

	return (rv);
}

void
ztest_zil_commit(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	zilog_t *zilog = zd->zd_zilog;

	(void) pthread_rwlock_rdlock(&zd->zd_zilog_lock);

	zil_commit(zilog, ztest_random(ZTEST_OBJECTS));

	/*
	 * Remember the committed values in zd, which is in parent/child
	 * shared memory.  If we die, the next iteration of ztest_run()
	 * will verify that the log really does contain this record.
	 */
	mutex_enter(&zilog->zl_lock);
	ASSERT3P(zd->zd_shared, !=, NULL);
	ASSERT3U(zd->zd_shared->zd_seq, <=, zilog->zl_commit_lr_seq);
	zd->zd_shared->zd_seq = zilog->zl_commit_lr_seq;
	mutex_exit(&zilog->zl_lock);

	(void) pthread_rwlock_unlock(&zd->zd_zilog_lock);
}

/*
 * This function is designed to simulate the operations that occur during a
 * mount/unmount operation.  We hold the dataset across these operations in an
 * attempt to expose any implicit assumptions about ZIL management.
 */
void
ztest_zil_remount(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	objset_t *os = zd->zd_os;

	/*
	 * We hold the ztest_vdev_lock so we don't cause problems with
	 * other threads that wish to remove a log device, such as
	 * ztest_device_removal().
	 */
	mutex_enter(&ztest_vdev_lock);

	/*
	 * We grab the zd_dirobj_lock to ensure that no other thread is
	 * updating the zil (i.e. adding in-memory log records) and the
	 * zd_zilog_lock to block any I/O.
	 */
	mutex_enter(&zd->zd_dirobj_lock);
	(void) pthread_rwlock_wrlock(&zd->zd_zilog_lock);

	/* zfsvfs_teardown() */
	zil_close(zd->zd_zilog);

	/* zfsvfs_setup() */
	VERIFY3P(zil_open(os, ztest_get_data, NULL), ==, zd->zd_zilog);
	zil_replay(os, zd, ztest_replay_vector);

	(void) pthread_rwlock_unlock(&zd->zd_zilog_lock);
	mutex_exit(&zd->zd_dirobj_lock);
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Verify that we can't destroy an active pool, create an existing pool,
 * or create a pool with a bad vdev spec.
 */
void
ztest_spa_create_destroy(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_opts_t *zo = &ztest_opts;
	spa_t *spa;
	nvlist_t *nvroot;

	if (zo->zo_mmp_test)
		return;

	/*
	 * Attempt to create using a bad file.
	 */
	nvroot = make_vdev_root("/dev/bogus", NULL, NULL, 0, 0, NULL, 0, 0, 1);
	VERIFY3U(ENOENT, ==,
	    spa_create("ztest_bad_file", nvroot, NULL, NULL, NULL));
	fnvlist_free(nvroot);

	/*
	 * Attempt to create using a bad mirror.
	 */
	nvroot = make_vdev_root("/dev/bogus", NULL, NULL, 0, 0, NULL, 0, 2, 1);
	VERIFY3U(ENOENT, ==,
	    spa_create("ztest_bad_mirror", nvroot, NULL, NULL, NULL));
	fnvlist_free(nvroot);

	/*
	 * Attempt to create an existing pool.  It shouldn't matter
	 * what's in the nvroot; we should fail with EEXIST.
	 */
	(void) pthread_rwlock_rdlock(&ztest_name_lock);
	nvroot = make_vdev_root("/dev/bogus", NULL, NULL, 0, 0, NULL, 0, 0, 1);
	VERIFY3U(EEXIST, ==,
	    spa_create(zo->zo_pool, nvroot, NULL, NULL, NULL));
	fnvlist_free(nvroot);

	/*
	 * We open a reference to the spa and then we try to export it
	 * expecting one of the following errors:
	 *
	 * EBUSY
	 *	Because of the reference we just opened.
	 *
	 * ZFS_ERR_EXPORT_IN_PROGRESS
	 *	For the case that there is another ztest thread doing
	 *	an export concurrently.
	 */
	VERIFY0(spa_open(zo->zo_pool, &spa, FTAG));
	int error = spa_destroy(zo->zo_pool);
	if (error != EBUSY && error != ZFS_ERR_EXPORT_IN_PROGRESS) {
		fatal(B_FALSE, "spa_destroy(%s) returned unexpected value %d",
		    spa->spa_name, error);
	}
	spa_close(spa, FTAG);

	(void) pthread_rwlock_unlock(&ztest_name_lock);
}

/*
 * Start and then stop the MMP threads to ensure the startup and shutdown code
 * works properly.  Actual protection and property-related code tested via ZTS.
 */
void
ztest_mmp_enable_disable(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_opts_t *zo = &ztest_opts;
	spa_t *spa = ztest_spa;

	if (zo->zo_mmp_test)
		return;

	/*
	 * Since enabling MMP involves setting a property, it could not be done
	 * while the pool is suspended.
	 */
	if (spa_suspended(spa))
		return;

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	mutex_enter(&spa->spa_props_lock);

	zfs_multihost_fail_intervals = 0;

	if (!spa_multihost(spa)) {
		spa->spa_multihost = B_TRUE;
		mmp_thread_start(spa);
	}

	mutex_exit(&spa->spa_props_lock);
	spa_config_exit(spa, SCL_CONFIG, FTAG);

	txg_wait_synced(spa_get_dsl(spa), 0);
	mmp_signal_all_threads();
	txg_wait_synced(spa_get_dsl(spa), 0);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	mutex_enter(&spa->spa_props_lock);

	if (spa_multihost(spa)) {
		mmp_thread_stop(spa);
		spa->spa_multihost = B_FALSE;
	}

	mutex_exit(&spa->spa_props_lock);
	spa_config_exit(spa, SCL_CONFIG, FTAG);
}

static int
ztest_get_raidz_children(spa_t *spa)
{
	(void) spa;
	vdev_t *raidvd;

	ASSERT(MUTEX_HELD(&ztest_vdev_lock));

	if (ztest_opts.zo_raid_do_expand) {
		raidvd = ztest_spa->spa_root_vdev->vdev_child[0];

		ASSERT(raidvd->vdev_ops == &vdev_raidz_ops);

		return (raidvd->vdev_children);
	}

	return (ztest_opts.zo_raid_children);
}

void
ztest_spa_upgrade(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa;
	uint64_t initial_version = SPA_VERSION_INITIAL;
	uint64_t raidz_children, version, newversion;
	nvlist_t *nvroot, *props;
	char *name;

	if (ztest_opts.zo_mmp_test)
		return;

	/* dRAID added after feature flags, skip upgrade test. */
	if (strcmp(ztest_opts.zo_raid_type, VDEV_TYPE_DRAID) == 0)
		return;

	mutex_enter(&ztest_vdev_lock);
	name = kmem_asprintf("%s_upgrade", ztest_opts.zo_pool);

	/*
	 * Clean up from previous runs.
	 */
	(void) spa_destroy(name);

	raidz_children = ztest_get_raidz_children(ztest_spa);

	nvroot = make_vdev_root(NULL, NULL, name, ztest_opts.zo_vdev_size, 0,
	    NULL, raidz_children, ztest_opts.zo_mirrors, 1);

	/*
	 * If we're configuring a RAIDZ device then make sure that the
	 * initial version is capable of supporting that feature.
	 */
	switch (ztest_opts.zo_raid_parity) {
	case 0:
	case 1:
		initial_version = SPA_VERSION_INITIAL;
		break;
	case 2:
		initial_version = SPA_VERSION_RAIDZ2;
		break;
	case 3:
		initial_version = SPA_VERSION_RAIDZ3;
		break;
	}

	/*
	 * Create a pool with a spa version that can be upgraded. Pick
	 * a value between initial_version and SPA_VERSION_BEFORE_FEATURES.
	 */
	do {
		version = ztest_random_spa_version(initial_version);
	} while (version > SPA_VERSION_BEFORE_FEATURES);

	props = fnvlist_alloc();
	fnvlist_add_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_VERSION), version);
	VERIFY0(spa_create(name, nvroot, props, NULL, NULL));
	fnvlist_free(nvroot);
	fnvlist_free(props);

	VERIFY0(spa_open(name, &spa, FTAG));
	VERIFY3U(spa_version(spa), ==, version);
	newversion = ztest_random_spa_version(version + 1);

	if (ztest_opts.zo_verbose >= 4) {
		(void) printf("upgrading spa version from "
		    "%"PRIu64" to %"PRIu64"\n",
		    version, newversion);
	}

	spa_upgrade(spa, newversion);
	VERIFY3U(spa_version(spa), >, version);
	VERIFY3U(spa_version(spa), ==, fnvlist_lookup_uint64(spa->spa_config,
	    zpool_prop_to_name(ZPOOL_PROP_VERSION)));
	spa_close(spa, FTAG);

	kmem_strfree(name);
	mutex_exit(&ztest_vdev_lock);
}

static void
ztest_spa_checkpoint(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&ztest_checkpoint_lock));

	int error = spa_checkpoint(spa->spa_name);

	switch (error) {
	case 0:
	case ZFS_ERR_DEVRM_IN_PROGRESS:
	case ZFS_ERR_DISCARDING_CHECKPOINT:
	case ZFS_ERR_CHECKPOINT_EXISTS:
	case ZFS_ERR_RAIDZ_EXPAND_IN_PROGRESS:
		break;
	case ENOSPC:
		ztest_record_enospc(FTAG);
		break;
	default:
		fatal(B_FALSE, "spa_checkpoint(%s) = %d", spa->spa_name, error);
	}
}

static void
ztest_spa_discard_checkpoint(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&ztest_checkpoint_lock));

	int error = spa_checkpoint_discard(spa->spa_name);

	switch (error) {
	case 0:
	case ZFS_ERR_DISCARDING_CHECKPOINT:
	case ZFS_ERR_NO_CHECKPOINT:
		break;
	default:
		fatal(B_FALSE, "spa_discard_checkpoint(%s) = %d",
		    spa->spa_name, error);
	}

}

void
ztest_spa_checkpoint_create_discard(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;

	mutex_enter(&ztest_checkpoint_lock);
	if (ztest_random(2) == 0) {
		ztest_spa_checkpoint(spa);
	} else {
		ztest_spa_discard_checkpoint(spa);
	}
	mutex_exit(&ztest_checkpoint_lock);
}


static vdev_t *
vdev_lookup_by_path(vdev_t *vd, const char *path)
{
	vdev_t *mvd;
	int c;

	if (vd->vdev_path != NULL && strcmp(path, vd->vdev_path) == 0)
		return (vd);

	for (c = 0; c < vd->vdev_children; c++)
		if ((mvd = vdev_lookup_by_path(vd->vdev_child[c], path)) !=
		    NULL)
			return (mvd);

	return (NULL);
}

static int
spa_num_top_vdevs(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	ASSERT3U(spa_config_held(spa, SCL_VDEV, RW_READER), ==, SCL_VDEV);
	return (rvd->vdev_children);
}

/*
 * Verify that vdev_add() works as expected.
 */
void
ztest_vdev_add_remove(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	uint64_t leaves;
	uint64_t guid;
	uint64_t raidz_children;

	nvlist_t *nvroot;
	int error;

	if (ztest_opts.zo_mmp_test)
		return;

	mutex_enter(&ztest_vdev_lock);
	raidz_children = ztest_get_raidz_children(spa);
	leaves = MAX(zs->zs_mirrors + zs->zs_splits, 1) * raidz_children;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	ztest_shared->zs_vdev_next_leaf = spa_num_top_vdevs(spa) * leaves;

	/*
	 * If we have slogs then remove them 1/4 of the time.
	 */
	if (spa_has_slogs(spa) && ztest_random(4) == 0) {
		metaslab_group_t *mg;

		/*
		 * find the first real slog in log allocation class
		 */
		mg =  spa_log_class(spa)->mc_allocator[0].mca_rotor;
		while (!mg->mg_vd->vdev_islog)
			mg = mg->mg_next;

		guid = mg->mg_vd->vdev_guid;

		spa_config_exit(spa, SCL_VDEV, FTAG);

		/*
		 * We have to grab the zs_name_lock as writer to
		 * prevent a race between removing a slog (dmu_objset_find)
		 * and destroying a dataset. Removing the slog will
		 * grab a reference on the dataset which may cause
		 * dsl_destroy_head() to fail with EBUSY thus
		 * leaving the dataset in an inconsistent state.
		 */
		pthread_rwlock_wrlock(&ztest_name_lock);
		error = spa_vdev_remove(spa, guid, B_FALSE);
		pthread_rwlock_unlock(&ztest_name_lock);

		switch (error) {
		case 0:
		case EEXIST:	/* Generic zil_reset() error */
		case EBUSY:	/* Replay required */
		case EACCES:	/* Crypto key not loaded */
		case ZFS_ERR_CHECKPOINT_EXISTS:
		case ZFS_ERR_DISCARDING_CHECKPOINT:
			break;
		default:
			fatal(B_FALSE, "spa_vdev_remove() = %d", error);
		}
	} else {
		spa_config_exit(spa, SCL_VDEV, FTAG);

		/*
		 * Make 1/4 of the devices be log devices
		 */
		nvroot = make_vdev_root(NULL, NULL, NULL,
		    ztest_opts.zo_vdev_size, 0, (ztest_random(4) == 0) ?
		    "log" : NULL, raidz_children, zs->zs_mirrors,
		    1);

		error = spa_vdev_add(spa, nvroot, B_FALSE);
		fnvlist_free(nvroot);

		switch (error) {
		case 0:
			break;
		case ENOSPC:
			ztest_record_enospc("spa_vdev_add");
			break;
		default:
			fatal(B_FALSE, "spa_vdev_add() = %d", error);
		}
	}

	mutex_exit(&ztest_vdev_lock);
}

void
ztest_vdev_class_add(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	uint64_t leaves;
	nvlist_t *nvroot;
	uint64_t raidz_children;
	const char *class = (ztest_random(2) == 0) ?
	    VDEV_ALLOC_BIAS_SPECIAL : VDEV_ALLOC_BIAS_DEDUP;
	int error;

	/*
	 * By default add a special vdev 50% of the time
	 */
	if ((ztest_opts.zo_special_vdevs == ZTEST_VDEV_CLASS_OFF) ||
	    (ztest_opts.zo_special_vdevs == ZTEST_VDEV_CLASS_RND &&
	    ztest_random(2) == 0)) {
		return;
	}

	mutex_enter(&ztest_vdev_lock);

	/* Only test with mirrors */
	if (zs->zs_mirrors < 2) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/* requires feature@allocation_classes */
	if (!spa_feature_is_enabled(spa, SPA_FEATURE_ALLOCATION_CLASSES)) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	raidz_children = ztest_get_raidz_children(spa);
	leaves = MAX(zs->zs_mirrors + zs->zs_splits, 1) * raidz_children;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	ztest_shared->zs_vdev_next_leaf = spa_num_top_vdevs(spa) * leaves;
	spa_config_exit(spa, SCL_VDEV, FTAG);

	nvroot = make_vdev_root(NULL, NULL, NULL, ztest_opts.zo_vdev_size, 0,
	    class, raidz_children, zs->zs_mirrors, 1);

	error = spa_vdev_add(spa, nvroot, B_FALSE);
	fnvlist_free(nvroot);

	if (error == ENOSPC)
		ztest_record_enospc("spa_vdev_add");
	else if (error != 0)
		fatal(B_FALSE, "spa_vdev_add() = %d", error);

	/*
	 * 50% of the time allow small blocks in the special class
	 */
	if (error == 0 &&
	    spa_special_class(spa)->mc_groups == 1 && ztest_random(2) == 0) {
		if (ztest_opts.zo_verbose >= 3)
			(void) printf("Enabling special VDEV small blocks\n");
		error = ztest_dsl_prop_set_uint64(zd->zd_name,
		    ZFS_PROP_SPECIAL_SMALL_BLOCKS, 32768, B_FALSE);
		ASSERT(error == 0 || error == ENOSPC);
	}

	mutex_exit(&ztest_vdev_lock);

	if (ztest_opts.zo_verbose >= 3) {
		metaslab_class_t *mc;

		if (strcmp(class, VDEV_ALLOC_BIAS_SPECIAL) == 0)
			mc = spa_special_class(spa);
		else
			mc = spa_dedup_class(spa);
		(void) printf("Added a %s mirrored vdev (of %d)\n",
		    class, (int)mc->mc_groups);
	}
}

/*
 * Verify that adding/removing aux devices (l2arc, hot spare) works as expected.
 */
void
ztest_vdev_aux_add_remove(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	spa_aux_vdev_t *sav;
	const char *aux;
	char *path;
	uint64_t guid = 0;
	int error, ignore_err = 0;

	if (ztest_opts.zo_mmp_test)
		return;

	path = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);

	if (ztest_random(2) == 0) {
		sav = &spa->spa_spares;
		aux = ZPOOL_CONFIG_SPARES;
	} else {
		sav = &spa->spa_l2cache;
		aux = ZPOOL_CONFIG_L2CACHE;
	}

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	if (sav->sav_count != 0 && ztest_random(4) == 0) {
		/*
		 * Pick a random device to remove.
		 */
		vdev_t *svd = sav->sav_vdevs[ztest_random(sav->sav_count)];

		/* dRAID spares cannot be removed; try anyways to see ENOTSUP */
		if (strstr(svd->vdev_path, VDEV_TYPE_DRAID) != NULL)
			ignore_err = ENOTSUP;

		guid = svd->vdev_guid;
	} else {
		/*
		 * Find an unused device we can add.
		 */
		zs->zs_vdev_aux = 0;
		for (;;) {
			int c;
			(void) snprintf(path, MAXPATHLEN, ztest_aux_template,
			    ztest_opts.zo_dir, ztest_opts.zo_pool, aux,
			    zs->zs_vdev_aux);
			for (c = 0; c < sav->sav_count; c++)
				if (strcmp(sav->sav_vdevs[c]->vdev_path,
				    path) == 0)
					break;
			if (c == sav->sav_count &&
			    vdev_lookup_by_path(rvd, path) == NULL)
				break;
			zs->zs_vdev_aux++;
		}
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);

	if (guid == 0) {
		/*
		 * Add a new device.
		 */
		nvlist_t *nvroot = make_vdev_root(NULL, aux, NULL,
		    (ztest_opts.zo_vdev_size * 5) / 4, 0, NULL, 0, 0, 1);
		error = spa_vdev_add(spa, nvroot, B_FALSE);

		switch (error) {
		case 0:
			break;
		default:
			fatal(B_FALSE, "spa_vdev_add(%p) = %d", nvroot, error);
		}
		fnvlist_free(nvroot);
	} else {
		/*
		 * Remove an existing device.  Sometimes, dirty its
		 * vdev state first to make sure we handle removal
		 * of devices that have pending state changes.
		 */
		if (ztest_random(2) == 0)
			(void) vdev_online(spa, guid, 0, NULL);

		error = spa_vdev_remove(spa, guid, B_FALSE);

		switch (error) {
		case 0:
		case EBUSY:
		case ZFS_ERR_CHECKPOINT_EXISTS:
		case ZFS_ERR_DISCARDING_CHECKPOINT:
			break;
		default:
			if (error != ignore_err)
				fatal(B_FALSE,
				    "spa_vdev_remove(%"PRIu64") = %d",
				    guid, error);
		}
	}

	mutex_exit(&ztest_vdev_lock);

	umem_free(path, MAXPATHLEN);
}

/*
 * split a pool if it has mirror tlvdevs
 */
void
ztest_split_pool(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	nvlist_t *tree, **child, *config, *split, **schild;
	uint_t c, children, schildren = 0, lastlogid = 0;
	int error = 0;

	if (ztest_opts.zo_mmp_test)
		return;

	mutex_enter(&ztest_vdev_lock);

	/* ensure we have a usable config; mirrors of raidz aren't supported */
	if (zs->zs_mirrors < 3 || ztest_opts.zo_raid_children > 1) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/* clean up the old pool, if any */
	(void) spa_destroy("splitp");

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	/* generate a config from the existing config */
	mutex_enter(&spa->spa_props_lock);
	tree = fnvlist_lookup_nvlist(spa->spa_config, ZPOOL_CONFIG_VDEV_TREE);
	mutex_exit(&spa->spa_props_lock);

	VERIFY0(nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN,
	    &child, &children));

	schild = umem_alloc(rvd->vdev_children * sizeof (nvlist_t *),
	    UMEM_NOFAIL);
	for (c = 0; c < children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		nvlist_t **mchild;
		uint_t mchildren;

		if (tvd->vdev_islog || tvd->vdev_ops == &vdev_hole_ops) {
			schild[schildren] = fnvlist_alloc();
			fnvlist_add_string(schild[schildren],
			    ZPOOL_CONFIG_TYPE, VDEV_TYPE_HOLE);
			fnvlist_add_uint64(schild[schildren],
			    ZPOOL_CONFIG_IS_HOLE, 1);
			if (lastlogid == 0)
				lastlogid = schildren;
			++schildren;
			continue;
		}
		lastlogid = 0;
		VERIFY0(nvlist_lookup_nvlist_array(child[c],
		    ZPOOL_CONFIG_CHILDREN, &mchild, &mchildren));
		schild[schildren++] = fnvlist_dup(mchild[0]);
	}

	/* OK, create a config that can be used to split */
	split = fnvlist_alloc();
	fnvlist_add_string(split, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_nvlist_array(split, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)schild, lastlogid != 0 ? lastlogid : schildren);

	config = fnvlist_alloc();
	fnvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, split);

	for (c = 0; c < schildren; c++)
		fnvlist_free(schild[c]);
	umem_free(schild, rvd->vdev_children * sizeof (nvlist_t *));
	fnvlist_free(split);

	spa_config_exit(spa, SCL_VDEV, FTAG);

	(void) pthread_rwlock_wrlock(&ztest_name_lock);
	error = spa_vdev_split_mirror(spa, "splitp", config, NULL, B_FALSE);
	(void) pthread_rwlock_unlock(&ztest_name_lock);

	fnvlist_free(config);

	if (error == 0) {
		(void) printf("successful split - results:\n");
		mutex_enter(&spa_namespace_lock);
		show_pool_stats(spa);
		show_pool_stats(spa_lookup("splitp"));
		mutex_exit(&spa_namespace_lock);
		++zs->zs_splits;
		--zs->zs_mirrors;
	}
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Verify that we can attach and detach devices.
 */
void
ztest_vdev_attach_detach(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	spa_aux_vdev_t *sav = &spa->spa_spares;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *oldvd, *newvd, *pvd;
	nvlist_t *root;
	uint64_t leaves;
	uint64_t leaf, top;
	uint64_t ashift = ztest_get_ashift();
	uint64_t oldguid, pguid;
	uint64_t oldsize, newsize;
	uint64_t raidz_children;
	char *oldpath, *newpath;
	int replacing;
	int oldvd_has_siblings = B_FALSE;
	int newvd_is_spare = B_FALSE;
	int newvd_is_dspare = B_FALSE;
	int oldvd_is_log;
	int oldvd_is_special;
	int error, expected_error;

	if (ztest_opts.zo_mmp_test)
		return;

	oldpath = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
	newpath = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);

	mutex_enter(&ztest_vdev_lock);
	raidz_children = ztest_get_raidz_children(spa);
	leaves = MAX(zs->zs_mirrors, 1) * raidz_children;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * If a vdev is in the process of being removed, its removal may
	 * finish while we are in progress, leading to an unexpected error
	 * value.  Don't bother trying to attach while we are in the middle
	 * of removal.
	 */
	if (ztest_device_removal_active) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		goto out;
	}

	/*
	 * RAIDZ leaf VDEV mirrors are not currently supported while a
	 * RAIDZ expansion is in progress.
	 */
	if (ztest_opts.zo_raid_do_expand) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		goto out;
	}

	/*
	 * Decide whether to do an attach or a replace.
	 */
	replacing = ztest_random(2);

	/*
	 * Pick a random top-level vdev.
	 */
	top = ztest_random_vdev_top(spa, B_TRUE);

	/*
	 * Pick a random leaf within it.
	 */
	leaf = ztest_random(leaves);

	/*
	 * Locate this vdev.
	 */
	oldvd = rvd->vdev_child[top];

	/* pick a child from the mirror */
	if (zs->zs_mirrors >= 1) {
		ASSERT3P(oldvd->vdev_ops, ==, &vdev_mirror_ops);
		ASSERT3U(oldvd->vdev_children, >=, zs->zs_mirrors);
		oldvd = oldvd->vdev_child[leaf / raidz_children];
	}

	/* pick a child out of the raidz group */
	if (ztest_opts.zo_raid_children > 1) {
		if (strcmp(oldvd->vdev_ops->vdev_op_type, "raidz") == 0)
			ASSERT3P(oldvd->vdev_ops, ==, &vdev_raidz_ops);
		else
			ASSERT3P(oldvd->vdev_ops, ==, &vdev_draid_ops);
		oldvd = oldvd->vdev_child[leaf % raidz_children];
	}

	/*
	 * If we're already doing an attach or replace, oldvd may be a
	 * mirror vdev -- in which case, pick a random child.
	 */
	while (oldvd->vdev_children != 0) {
		oldvd_has_siblings = B_TRUE;
		ASSERT3U(oldvd->vdev_children, >=, 2);
		oldvd = oldvd->vdev_child[ztest_random(oldvd->vdev_children)];
	}

	oldguid = oldvd->vdev_guid;
	oldsize = vdev_get_min_asize(oldvd);
	oldvd_is_log = oldvd->vdev_top->vdev_islog;
	oldvd_is_special =
	    oldvd->vdev_top->vdev_alloc_bias == VDEV_BIAS_SPECIAL ||
	    oldvd->vdev_top->vdev_alloc_bias == VDEV_BIAS_DEDUP;
	(void) strlcpy(oldpath, oldvd->vdev_path, MAXPATHLEN);
	pvd = oldvd->vdev_parent;
	pguid = pvd->vdev_guid;

	/*
	 * If oldvd has siblings, then half of the time, detach it.  Prior
	 * to the detach the pool is scrubbed in order to prevent creating
	 * unrepairable blocks as a result of the data corruption injection.
	 */
	if (oldvd_has_siblings && ztest_random(2) == 0) {
		spa_config_exit(spa, SCL_ALL, FTAG);

		error = ztest_scrub_impl(spa);
		if (error)
			goto out;

		error = spa_vdev_detach(spa, oldguid, pguid, B_FALSE);
		if (error != 0 && error != ENODEV && error != EBUSY &&
		    error != ENOTSUP && error != ZFS_ERR_CHECKPOINT_EXISTS &&
		    error != ZFS_ERR_DISCARDING_CHECKPOINT)
			fatal(B_FALSE, "detach (%s) returned %d",
			    oldpath, error);
		goto out;
	}

	/*
	 * For the new vdev, choose with equal probability between the two
	 * standard paths (ending in either 'a' or 'b') or a random hot spare.
	 */
	if (sav->sav_count != 0 && ztest_random(3) == 0) {
		newvd = sav->sav_vdevs[ztest_random(sav->sav_count)];
		newvd_is_spare = B_TRUE;

		if (newvd->vdev_ops == &vdev_draid_spare_ops)
			newvd_is_dspare = B_TRUE;

		(void) strlcpy(newpath, newvd->vdev_path, MAXPATHLEN);
	} else {
		(void) snprintf(newpath, MAXPATHLEN, ztest_dev_template,
		    ztest_opts.zo_dir, ztest_opts.zo_pool,
		    top * leaves + leaf);
		if (ztest_random(2) == 0)
			newpath[strlen(newpath) - 1] = 'b';
		newvd = vdev_lookup_by_path(rvd, newpath);
	}

	if (newvd) {
		/*
		 * Reopen to ensure the vdev's asize field isn't stale.
		 */
		vdev_reopen(newvd);
		newsize = vdev_get_min_asize(newvd);
	} else {
		/*
		 * Make newsize a little bigger or smaller than oldsize.
		 * If it's smaller, the attach should fail.
		 * If it's larger, and we're doing a replace,
		 * we should get dynamic LUN growth when we're done.
		 */
		newsize = 10 * oldsize / (9 + ztest_random(3));
	}

	/*
	 * If pvd is not a mirror or root, the attach should fail with ENOTSUP,
	 * unless it's a replace; in that case any non-replacing parent is OK.
	 *
	 * If newvd is already part of the pool, it should fail with EBUSY.
	 *
	 * If newvd is too small, it should fail with EOVERFLOW.
	 *
	 * If newvd is a distributed spare and it's being attached to a
	 * dRAID which is not its parent it should fail with EINVAL.
	 */
	if (pvd->vdev_ops != &vdev_mirror_ops &&
	    pvd->vdev_ops != &vdev_root_ops && (!replacing ||
	    pvd->vdev_ops == &vdev_replacing_ops ||
	    pvd->vdev_ops == &vdev_spare_ops))
		expected_error = ENOTSUP;
	else if (newvd_is_spare &&
	    (!replacing || oldvd_is_log || oldvd_is_special))
		expected_error = ENOTSUP;
	else if (newvd == oldvd)
		expected_error = replacing ? 0 : EBUSY;
	else if (vdev_lookup_by_path(rvd, newpath) != NULL)
		expected_error = EBUSY;
	else if (!newvd_is_dspare && newsize < oldsize)
		expected_error = EOVERFLOW;
	else if (ashift > oldvd->vdev_top->vdev_ashift)
		expected_error = EDOM;
	else if (newvd_is_dspare && pvd != vdev_draid_spare_get_parent(newvd))
		expected_error = EINVAL;
	else
		expected_error = 0;

	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * Build the nvlist describing newpath.
	 */
	root = make_vdev_root(newpath, NULL, NULL, newvd == NULL ? newsize : 0,
	    ashift, NULL, 0, 0, 1);

	/*
	 * When supported select either a healing or sequential resilver.
	 */
	boolean_t rebuilding = B_FALSE;
	if (pvd->vdev_ops == &vdev_mirror_ops ||
	    pvd->vdev_ops ==  &vdev_root_ops) {
		rebuilding = !!ztest_random(2);
	}

	error = spa_vdev_attach(spa, oldguid, root, replacing, rebuilding);

	fnvlist_free(root);

	/*
	 * If our parent was the replacing vdev, but the replace completed,
	 * then instead of failing with ENOTSUP we may either succeed,
	 * fail with ENODEV, or fail with EOVERFLOW.
	 */
	if (expected_error == ENOTSUP &&
	    (error == 0 || error == ENODEV || error == EOVERFLOW))
		expected_error = error;

	/*
	 * If someone grew the LUN, the replacement may be too small.
	 */
	if (error == EOVERFLOW || error == EBUSY)
		expected_error = error;

	if (error == ZFS_ERR_CHECKPOINT_EXISTS ||
	    error == ZFS_ERR_DISCARDING_CHECKPOINT ||
	    error == ZFS_ERR_RESILVER_IN_PROGRESS ||
	    error == ZFS_ERR_REBUILD_IN_PROGRESS)
		expected_error = error;

	if (error != expected_error && expected_error != EBUSY) {
		fatal(B_FALSE, "attach (%s %"PRIu64", %s %"PRIu64", %d) "
		    "returned %d, expected %d",
		    oldpath, oldsize, newpath,
		    newsize, replacing, error, expected_error);
	}
out:
	mutex_exit(&ztest_vdev_lock);

	umem_free(oldpath, MAXPATHLEN);
	umem_free(newpath, MAXPATHLEN);
}

static void
raidz_scratch_verify(void)
{
	spa_t *spa;
	uint64_t write_size, logical_size, offset;
	raidz_reflow_scratch_state_t state;
	vdev_raidz_expand_t *vre;
	vdev_t *raidvd;

	ASSERT(raidz_expand_pause_point == RAIDZ_EXPAND_PAUSE_NONE);

	if (ztest_scratch_state->zs_raidz_scratch_verify_pause == 0)
		return;

	kernel_init(SPA_MODE_READ);

	mutex_enter(&spa_namespace_lock);
	spa = spa_lookup(ztest_opts.zo_pool);
	ASSERT(spa);
	spa->spa_import_flags |= ZFS_IMPORT_SKIP_MMP;
	mutex_exit(&spa_namespace_lock);

	VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));

	ASSERT3U(RRSS_GET_OFFSET(&spa->spa_uberblock), !=, UINT64_MAX);

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_READER);

	vre = spa->spa_raidz_expand;
	if (vre == NULL)
		goto out;

	raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);
	offset = RRSS_GET_OFFSET(&spa->spa_uberblock);
	state = RRSS_GET_STATE(&spa->spa_uberblock);
	write_size = P2ALIGN(VDEV_BOOT_SIZE, 1 << raidvd->vdev_ashift);
	logical_size = write_size * raidvd->vdev_children;

	switch (state) {
		/*
		 * Initial state of reflow process.  RAIDZ expansion was
		 * requested by user, but scratch object was not created.
		 */
		case RRSS_SCRATCH_NOT_IN_USE:
			ASSERT3U(offset, ==, 0);
			break;

		/*
		 * Scratch object was synced and stored in boot area.
		 */
		case RRSS_SCRATCH_VALID:

		/*
		 * Scratch object was synced back to raidz start offset,
		 * raidz is ready for sector by sector reflow process.
		 */
		case RRSS_SCRATCH_INVALID_SYNCED:

		/*
		 * Scratch object was synced back to raidz start offset
		 * on zpool importing, raidz is ready for sector by sector
		 * reflow process.
		 */
		case RRSS_SCRATCH_INVALID_SYNCED_ON_IMPORT:
			ASSERT3U(offset, ==, logical_size);
			break;

		/*
		 * Sector by sector reflow process started.
		 */
		case RRSS_SCRATCH_INVALID_SYNCED_REFLOW:
			ASSERT3U(offset, >=, logical_size);
			break;
	}

out:
	spa_config_exit(spa, SCL_ALL, FTAG);

	mutex_exit(&ztest_vdev_lock);

	ztest_scratch_state->zs_raidz_scratch_verify_pause = 0;

	spa_close(spa, FTAG);
	kernel_fini();
}

static void
ztest_scratch_thread(void *arg)
{
	(void) arg;

	/* wait up to 10 seconds */
	for (int t = 100; t > 0; t -= 1) {
		if (raidz_expand_pause_point == RAIDZ_EXPAND_PAUSE_NONE)
			thread_exit();

		(void) poll(NULL, 0, 100);
	}

	/* killed when the scratch area progress reached a certain point */
	ztest_kill(ztest_shared);
}

/*
 * Verify that we can attach raidz device.
 */
void
ztest_vdev_raidz_attach(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	uint64_t leaves, raidz_children, newsize, ashift = ztest_get_ashift();
	kthread_t *scratch_thread = NULL;
	vdev_t *newvd, *pvd;
	nvlist_t *root;
	char *newpath = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
	int error, expected_error = 0;

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_READER);

	/* Only allow attach when raid-kind = 'eraidz' */
	if (!ztest_opts.zo_raid_do_expand) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		goto out;
	}

	if (ztest_opts.zo_mmp_test) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		goto out;
	}

	if (ztest_device_removal_active) {
		spa_config_exit(spa, SCL_ALL, FTAG);
		goto out;
	}

	pvd = vdev_lookup_top(spa, 0);

	ASSERT(pvd->vdev_ops == &vdev_raidz_ops);

	/*
	 * Get size of a child of the raidz group,
	 * make sure device is a bit bigger
	 */
	newvd = pvd->vdev_child[ztest_random(pvd->vdev_children)];
	newsize = 10 * vdev_get_min_asize(newvd) / (9 + ztest_random(2));

	/*
	 * Get next attached leaf id
	 */
	raidz_children = ztest_get_raidz_children(spa);
	leaves = MAX(zs->zs_mirrors + zs->zs_splits, 1) * raidz_children;
	zs->zs_vdev_next_leaf = spa_num_top_vdevs(spa) * leaves;

	if (spa->spa_raidz_expand)
		expected_error = ZFS_ERR_RAIDZ_EXPAND_IN_PROGRESS;

	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * Path to vdev to be attached
	 */
	(void) snprintf(newpath, MAXPATHLEN, ztest_dev_template,
	    ztest_opts.zo_dir, ztest_opts.zo_pool, zs->zs_vdev_next_leaf);

	/*
	 * Build the nvlist describing newpath.
	 */
	root = make_vdev_root(newpath, NULL, NULL, newsize, ashift, NULL,
	    0, 0, 1);

	/*
	 * 50% of the time, set raidz_expand_pause_point to cause
	 * raidz_reflow_scratch_sync() to pause at a certain point and
	 * then kill the test after 10 seconds so raidz_scratch_verify()
	 * can confirm consistency when the pool is imported.
	 */
	if (ztest_random(2) == 0 && expected_error == 0) {
		raidz_expand_pause_point =
		    ztest_random(RAIDZ_EXPAND_PAUSE_SCRATCH_POST_REFLOW_2) + 1;
		scratch_thread = thread_create(NULL, 0, ztest_scratch_thread,
		    ztest_shared, 0, NULL, TS_RUN | TS_JOINABLE, defclsyspri);
	}

	error = spa_vdev_attach(spa, pvd->vdev_guid, root, B_FALSE, B_FALSE);

	nvlist_free(root);

	if (error == EOVERFLOW || error == ENXIO ||
	    error == ZFS_ERR_CHECKPOINT_EXISTS ||
	    error == ZFS_ERR_DISCARDING_CHECKPOINT)
		expected_error = error;

	if (error != 0 && error != expected_error) {
		fatal(0, "raidz attach (%s %"PRIu64") returned %d, expected %d",
		    newpath, newsize, error, expected_error);
	}

	if (raidz_expand_pause_point) {
		if (error != 0) {
			/*
			 * Do not verify scratch object in case of error
			 * returned by vdev attaching.
			 */
			raidz_expand_pause_point = RAIDZ_EXPAND_PAUSE_NONE;
		}

		VERIFY0(thread_join(scratch_thread));
	}
out:
	mutex_exit(&ztest_vdev_lock);

	umem_free(newpath, MAXPATHLEN);
}

void
ztest_device_removal(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;
	vdev_t *vd;
	uint64_t guid;
	int error;

	mutex_enter(&ztest_vdev_lock);

	if (ztest_device_removal_active) {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * Remove a random top-level vdev and wait for removal to finish.
	 */
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	vd = vdev_lookup_top(spa, ztest_random_vdev_top(spa, B_FALSE));
	guid = vd->vdev_guid;
	spa_config_exit(spa, SCL_VDEV, FTAG);

	error = spa_vdev_remove(spa, guid, B_FALSE);
	if (error == 0) {
		ztest_device_removal_active = B_TRUE;
		mutex_exit(&ztest_vdev_lock);

		/*
		 * spa->spa_vdev_removal is created in a sync task that
		 * is initiated via dsl_sync_task_nowait(). Since the
		 * task may not run before spa_vdev_remove() returns, we
		 * must wait at least 1 txg to ensure that the removal
		 * struct has been created.
		 */
		txg_wait_synced(spa_get_dsl(spa), 0);

		while (spa->spa_removing_phys.sr_state == DSS_SCANNING)
			txg_wait_synced(spa_get_dsl(spa), 0);
	} else {
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * The pool needs to be scrubbed after completing device removal.
	 * Failure to do so may result in checksum errors due to the
	 * strategy employed by ztest_fault_inject() when selecting which
	 * offset are redundant and can be damaged.
	 */
	error = spa_scan(spa, POOL_SCAN_SCRUB);
	if (error == 0) {
		while (dsl_scan_scrubbing(spa_get_dsl(spa)))
			txg_wait_synced(spa_get_dsl(spa), 0);
	}

	mutex_enter(&ztest_vdev_lock);
	ztest_device_removal_active = B_FALSE;
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Callback function which expands the physical size of the vdev.
 */
static vdev_t *
grow_vdev(vdev_t *vd, void *arg)
{
	spa_t *spa __maybe_unused = vd->vdev_spa;
	size_t *newsize = arg;
	size_t fsize;
	int fd;

	ASSERT3S(spa_config_held(spa, SCL_STATE, RW_READER), ==, SCL_STATE);
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	if ((fd = open(vd->vdev_path, O_RDWR)) == -1)
		return (vd);

	fsize = lseek(fd, 0, SEEK_END);
	VERIFY0(ftruncate(fd, *newsize));

	if (ztest_opts.zo_verbose >= 6) {
		(void) printf("%s grew from %lu to %lu bytes\n",
		    vd->vdev_path, (ulong_t)fsize, (ulong_t)*newsize);
	}
	(void) close(fd);
	return (NULL);
}

/*
 * Callback function which expands a given vdev by calling vdev_online().
 */
static vdev_t *
online_vdev(vdev_t *vd, void *arg)
{
	(void) arg;
	spa_t *spa = vd->vdev_spa;
	vdev_t *tvd = vd->vdev_top;
	uint64_t guid = vd->vdev_guid;
	uint64_t generation = spa->spa_config_generation + 1;
	vdev_state_t newstate = VDEV_STATE_UNKNOWN;
	int error;

	ASSERT3S(spa_config_held(spa, SCL_STATE, RW_READER), ==, SCL_STATE);
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	/* Calling vdev_online will initialize the new metaslabs */
	spa_config_exit(spa, SCL_STATE, spa);
	error = vdev_online(spa, guid, ZFS_ONLINE_EXPAND, &newstate);
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	/*
	 * If vdev_online returned an error or the underlying vdev_open
	 * failed then we abort the expand. The only way to know that
	 * vdev_open fails is by checking the returned newstate.
	 */
	if (error || newstate != VDEV_STATE_HEALTHY) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("Unable to expand vdev, state %u, "
			    "error %d\n", newstate, error);
		}
		return (vd);
	}
	ASSERT3U(newstate, ==, VDEV_STATE_HEALTHY);

	/*
	 * Since we dropped the lock we need to ensure that we're
	 * still talking to the original vdev. It's possible this
	 * vdev may have been detached/replaced while we were
	 * trying to online it.
	 */
	if (generation != spa->spa_config_generation) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("vdev configuration has changed, "
			    "guid %"PRIu64", state %"PRIu64", "
			    "expected gen %"PRIu64", got gen %"PRIu64"\n",
			    guid,
			    tvd->vdev_state,
			    generation,
			    spa->spa_config_generation);
		}
		return (vd);
	}
	return (NULL);
}

/*
 * Traverse the vdev tree calling the supplied function.
 * We continue to walk the tree until we either have walked all
 * children or we receive a non-NULL return from the callback.
 * If a NULL callback is passed, then we just return back the first
 * leaf vdev we encounter.
 */
static vdev_t *
vdev_walk_tree(vdev_t *vd, vdev_t *(*func)(vdev_t *, void *), void *arg)
{
	uint_t c;

	if (vd->vdev_ops->vdev_op_leaf) {
		if (func == NULL)
			return (vd);
		else
			return (func(vd, arg));
	}

	for (c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		if ((cvd = vdev_walk_tree(cvd, func, arg)) != NULL)
			return (cvd);
	}
	return (NULL);
}

/*
 * Verify that dynamic LUN growth works as expected.
 */
void
ztest_vdev_LUN_growth(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;
	vdev_t *vd, *tvd;
	metaslab_class_t *mc;
	metaslab_group_t *mg;
	size_t psize, newsize;
	uint64_t top;
	uint64_t old_class_space, new_class_space, old_ms_count, new_ms_count;

	mutex_enter(&ztest_checkpoint_lock);
	mutex_enter(&ztest_vdev_lock);
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	/*
	 * If there is a vdev removal in progress, it could complete while
	 * we are running, in which case we would not be able to verify
	 * that the metaslab_class space increased (because it decreases
	 * when the device removal completes).
	 */
	if (ztest_device_removal_active) {
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	/*
	 * If we are under raidz expansion, the test can failed because the
	 * metaslabs count will not increase immediately after the vdev is
	 * expanded. It will happen only after raidz expansion completion.
	 */
	if (spa->spa_raidz_expand) {
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	top = ztest_random_vdev_top(spa, B_TRUE);

	tvd = spa->spa_root_vdev->vdev_child[top];
	mg = tvd->vdev_mg;
	mc = mg->mg_class;
	old_ms_count = tvd->vdev_ms_count;
	old_class_space = metaslab_class_get_space(mc);

	/*
	 * Determine the size of the first leaf vdev associated with
	 * our top-level device.
	 */
	vd = vdev_walk_tree(tvd, NULL, NULL);
	ASSERT3P(vd, !=, NULL);
	ASSERT(vd->vdev_ops->vdev_op_leaf);

	psize = vd->vdev_psize;

	/*
	 * We only try to expand the vdev if it's healthy, less than 4x its
	 * original size, and it has a valid psize.
	 */
	if (tvd->vdev_state != VDEV_STATE_HEALTHY ||
	    psize == 0 || psize >= 4 * ztest_opts.zo_vdev_size) {
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}
	ASSERT3U(psize, >, 0);
	newsize = psize + MAX(psize / 8, SPA_MAXBLOCKSIZE);
	ASSERT3U(newsize, >, psize);

	if (ztest_opts.zo_verbose >= 6) {
		(void) printf("Expanding LUN %s from %lu to %lu\n",
		    vd->vdev_path, (ulong_t)psize, (ulong_t)newsize);
	}

	/*
	 * Growing the vdev is a two step process:
	 *	1). expand the physical size (i.e. relabel)
	 *	2). online the vdev to create the new metaslabs
	 */
	if (vdev_walk_tree(tvd, grow_vdev, &newsize) != NULL ||
	    vdev_walk_tree(tvd, online_vdev, NULL) != NULL ||
	    tvd->vdev_state != VDEV_STATE_HEALTHY) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("Could not expand LUN because "
			    "the vdev configuration changed.\n");
		}
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	spa_config_exit(spa, SCL_STATE, spa);

	/*
	 * Expanding the LUN will update the config asynchronously,
	 * thus we must wait for the async thread to complete any
	 * pending tasks before proceeding.
	 */
	for (;;) {
		boolean_t done;
		mutex_enter(&spa->spa_async_lock);
		done = (spa->spa_async_thread == NULL && !spa->spa_async_tasks);
		mutex_exit(&spa->spa_async_lock);
		if (done)
			break;
		txg_wait_synced(spa_get_dsl(spa), 0);
		(void) poll(NULL, 0, 100);
	}

	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	tvd = spa->spa_root_vdev->vdev_child[top];
	new_ms_count = tvd->vdev_ms_count;
	new_class_space = metaslab_class_get_space(mc);

	if (tvd->vdev_mg != mg || mg->mg_class != mc) {
		if (ztest_opts.zo_verbose >= 5) {
			(void) printf("Could not verify LUN expansion due to "
			    "intervening vdev offline or remove.\n");
		}
		spa_config_exit(spa, SCL_STATE, spa);
		mutex_exit(&ztest_vdev_lock);
		mutex_exit(&ztest_checkpoint_lock);
		return;
	}

	/*
	 * Make sure we were able to grow the vdev.
	 */
	if (new_ms_count <= old_ms_count) {
		fatal(B_FALSE,
		    "LUN expansion failed: ms_count %"PRIu64" < %"PRIu64"\n",
		    old_ms_count, new_ms_count);
	}

	/*
	 * Make sure we were able to grow the pool.
	 */
	if (new_class_space <= old_class_space) {
		fatal(B_FALSE,
		    "LUN expansion failed: class_space %"PRIu64" < %"PRIu64"\n",
		    old_class_space, new_class_space);
	}

	if (ztest_opts.zo_verbose >= 5) {
		char oldnumbuf[NN_NUMBUF_SZ], newnumbuf[NN_NUMBUF_SZ];

		nicenum(old_class_space, oldnumbuf, sizeof (oldnumbuf));
		nicenum(new_class_space, newnumbuf, sizeof (newnumbuf));
		(void) printf("%s grew from %s to %s\n",
		    spa->spa_name, oldnumbuf, newnumbuf);
	}

	spa_config_exit(spa, SCL_STATE, spa);
	mutex_exit(&ztest_vdev_lock);
	mutex_exit(&ztest_checkpoint_lock);
}

/*
 * Verify that dmu_objset_{create,destroy,open,close} work as expected.
 */
static void
ztest_objset_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	(void) arg, (void) cr;

	/*
	 * Create the objects common to all ztest datasets.
	 */
	VERIFY0(zap_create_claim(os, ZTEST_DIROBJ,
	    DMU_OT_ZAP_OTHER, DMU_OT_NONE, 0, tx));
}

static int
ztest_dataset_create(char *dsname)
{
	int err;
	uint64_t rand;
	dsl_crypto_params_t *dcp = NULL;

	/*
	 * 50% of the time, we create encrypted datasets
	 * using a random cipher suite and a hard-coded
	 * wrapping key.
	 */
	rand = ztest_random(2);
	if (rand != 0) {
		nvlist_t *crypto_args = fnvlist_alloc();
		nvlist_t *props = fnvlist_alloc();

		/* slight bias towards the default cipher suite */
		rand = ztest_random(ZIO_CRYPT_FUNCTIONS);
		if (rand < ZIO_CRYPT_AES_128_CCM)
			rand = ZIO_CRYPT_ON;

		fnvlist_add_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), rand);
		fnvlist_add_uint8_array(crypto_args, "wkeydata",
		    (uint8_t *)ztest_wkeydata, WRAPPING_KEY_LEN);

		/*
		 * These parameters aren't really used by the kernel. They
		 * are simply stored so that userspace knows how to load
		 * the wrapping key.
		 */
		fnvlist_add_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), ZFS_KEYFORMAT_RAW);
		fnvlist_add_string(props,
		    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), "prompt");
		fnvlist_add_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_SALT), 0ULL);
		fnvlist_add_uint64(props,
		    zfs_prop_to_name(ZFS_PROP_PBKDF2_ITERS), 0ULL);

		VERIFY0(dsl_crypto_params_create_nvlist(DCP_CMD_NONE, props,
		    crypto_args, &dcp));

		/*
		 * Cycle through all available encryption implementations
		 * to verify interoperability.
		 */
		VERIFY0(gcm_impl_set("cycle"));
		VERIFY0(aes_impl_set("cycle"));

		fnvlist_free(crypto_args);
		fnvlist_free(props);
	}

	err = dmu_objset_create(dsname, DMU_OST_OTHER, 0, dcp,
	    ztest_objset_create_cb, NULL);
	dsl_crypto_params_free(dcp, !!err);

	rand = ztest_random(100);
	if (err || rand < 80)
		return (err);

	if (ztest_opts.zo_verbose >= 5)
		(void) printf("Setting dataset %s to sync always\n", dsname);
	return (ztest_dsl_prop_set_uint64(dsname, ZFS_PROP_SYNC,
	    ZFS_SYNC_ALWAYS, B_FALSE));
}

static int
ztest_objset_destroy_cb(const char *name, void *arg)
{
	(void) arg;
	objset_t *os;
	dmu_object_info_t doi;
	int error;

	/*
	 * Verify that the dataset contains a directory object.
	 */
	VERIFY0(ztest_dmu_objset_own(name, DMU_OST_OTHER, B_TRUE,
	    B_TRUE, FTAG, &os));
	error = dmu_object_info(os, ZTEST_DIROBJ, &doi);
	if (error != ENOENT) {
		/* We could have crashed in the middle of destroying it */
		ASSERT0(error);
		ASSERT3U(doi.doi_type, ==, DMU_OT_ZAP_OTHER);
		ASSERT3S(doi.doi_physical_blocks_512, >=, 0);
	}
	dmu_objset_disown(os, B_TRUE, FTAG);

	/*
	 * Destroy the dataset.
	 */
	if (strchr(name, '@') != NULL) {
		error = dsl_destroy_snapshot(name, B_TRUE);
		if (error != ECHRNG) {
			/*
			 * The program was executed, but encountered a runtime
			 * error, such as insufficient slop, or a hold on the
			 * dataset.
			 */
			ASSERT0(error);
		}
	} else {
		error = dsl_destroy_head(name);
		if (error == ENOSPC) {
			/* There could be checkpoint or insufficient slop */
			ztest_record_enospc(FTAG);
		} else if (error != EBUSY) {
			/* There could be a hold on this dataset */
			ASSERT0(error);
		}
	}
	return (0);
}

static boolean_t
ztest_snapshot_create(char *osname, uint64_t id)
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	(void) snprintf(snapname, sizeof (snapname), "%"PRIu64"", id);

	error = dmu_objset_snapshot_one(osname, snapname);
	if (error == ENOSPC) {
		ztest_record_enospc(FTAG);
		return (B_FALSE);
	}
	if (error != 0 && error != EEXIST && error != ECHRNG) {
		fatal(B_FALSE, "ztest_snapshot_create(%s@%s) = %d", osname,
		    snapname, error);
	}
	return (B_TRUE);
}

static boolean_t
ztest_snapshot_destroy(char *osname, uint64_t id)
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	(void) snprintf(snapname, sizeof (snapname), "%s@%"PRIu64"",
	    osname, id);

	error = dsl_destroy_snapshot(snapname, B_FALSE);
	if (error != 0 && error != ENOENT && error != ECHRNG)
		fatal(B_FALSE, "ztest_snapshot_destroy(%s) = %d",
		    snapname, error);
	return (B_TRUE);
}

void
ztest_dmu_objset_create_destroy(ztest_ds_t *zd, uint64_t id)
{
	(void) zd;
	ztest_ds_t *zdtmp;
	int iters;
	int error;
	objset_t *os, *os2;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	zilog_t *zilog;
	int i;

	zdtmp = umem_alloc(sizeof (ztest_ds_t), UMEM_NOFAIL);

	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	(void) snprintf(name, sizeof (name), "%s/temp_%"PRIu64"",
	    ztest_opts.zo_pool, id);

	/*
	 * If this dataset exists from a previous run, process its replay log
	 * half of the time.  If we don't replay it, then dsl_destroy_head()
	 * (invoked from ztest_objset_destroy_cb()) should just throw it away.
	 */
	if (ztest_random(2) == 0 &&
	    ztest_dmu_objset_own(name, DMU_OST_OTHER, B_FALSE,
	    B_TRUE, FTAG, &os) == 0) {
		ztest_zd_init(zdtmp, NULL, os);
		zil_replay(os, zdtmp, ztest_replay_vector);
		ztest_zd_fini(zdtmp);
		dmu_objset_disown(os, B_TRUE, FTAG);
	}

	/*
	 * There may be an old instance of the dataset we're about to
	 * create lying around from a previous run.  If so, destroy it
	 * and all of its snapshots.
	 */
	(void) dmu_objset_find(name, ztest_objset_destroy_cb, NULL,
	    DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);

	/*
	 * Verify that the destroyed dataset is no longer in the namespace.
	 * It may still be present if the destroy above fails with ENOSPC.
	 */
	error = ztest_dmu_objset_own(name, DMU_OST_OTHER, B_TRUE, B_TRUE,
	    FTAG, &os);
	if (error == 0) {
		dmu_objset_disown(os, B_TRUE, FTAG);
		ztest_record_enospc(FTAG);
		goto out;
	}
	VERIFY3U(ENOENT, ==, error);

	/*
	 * Verify that we can create a new dataset.
	 */
	error = ztest_dataset_create(name);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(B_FALSE, "dmu_objset_create(%s) = %d", name, error);
	}

	VERIFY0(ztest_dmu_objset_own(name, DMU_OST_OTHER, B_FALSE, B_TRUE,
	    FTAG, &os));

	ztest_zd_init(zdtmp, NULL, os);

	/*
	 * Open the intent log for it.
	 */
	zilog = zil_open(os, ztest_get_data, NULL);

	/*
	 * Put some objects in there, do a little I/O to them,
	 * and randomly take a couple of snapshots along the way.
	 */
	iters = ztest_random(5);
	for (i = 0; i < iters; i++) {
		ztest_dmu_object_alloc_free(zdtmp, id);
		if (ztest_random(iters) == 0)
			(void) ztest_snapshot_create(name, i);
	}

	/*
	 * Verify that we cannot create an existing dataset.
	 */
	VERIFY3U(EEXIST, ==,
	    dmu_objset_create(name, DMU_OST_OTHER, 0, NULL, NULL, NULL));

	/*
	 * Verify that we can hold an objset that is also owned.
	 */
	VERIFY0(dmu_objset_hold(name, FTAG, &os2));
	dmu_objset_rele(os2, FTAG);

	/*
	 * Verify that we cannot own an objset that is already owned.
	 */
	VERIFY3U(EBUSY, ==, ztest_dmu_objset_own(name, DMU_OST_OTHER,
	    B_FALSE, B_TRUE, FTAG, &os2));

	zil_close(zilog);
	dmu_objset_disown(os, B_TRUE, FTAG);
	ztest_zd_fini(zdtmp);
out:
	(void) pthread_rwlock_unlock(&ztest_name_lock);

	umem_free(zdtmp, sizeof (ztest_ds_t));
}

/*
 * Verify that dmu_snapshot_{create,destroy,open,close} work as expected.
 */
void
ztest_dmu_snapshot_create_destroy(ztest_ds_t *zd, uint64_t id)
{
	(void) pthread_rwlock_rdlock(&ztest_name_lock);
	(void) ztest_snapshot_destroy(zd->zd_name, id);
	(void) ztest_snapshot_create(zd->zd_name, id);
	(void) pthread_rwlock_unlock(&ztest_name_lock);
}

/*
 * Cleanup non-standard snapshots and clones.
 */
static void
ztest_dsl_dataset_cleanup(char *osname, uint64_t id)
{
	char *snap1name;
	char *clone1name;
	char *snap2name;
	char *clone2name;
	char *snap3name;
	int error;

	snap1name  = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	clone1name = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	snap2name  = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	clone2name = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	snap3name  = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);

	(void) snprintf(snap1name, ZFS_MAX_DATASET_NAME_LEN, "%s@s1_%"PRIu64"",
	    osname, id);
	(void) snprintf(clone1name, ZFS_MAX_DATASET_NAME_LEN, "%s/c1_%"PRIu64"",
	    osname, id);
	(void) snprintf(snap2name, ZFS_MAX_DATASET_NAME_LEN, "%s@s2_%"PRIu64"",
	    clone1name, id);
	(void) snprintf(clone2name, ZFS_MAX_DATASET_NAME_LEN, "%s/c2_%"PRIu64"",
	    osname, id);
	(void) snprintf(snap3name, ZFS_MAX_DATASET_NAME_LEN, "%s@s3_%"PRIu64"",
	    clone1name, id);

	error = dsl_destroy_head(clone2name);
	if (error && error != ENOENT)
		fatal(B_FALSE, "dsl_destroy_head(%s) = %d", clone2name, error);
	error = dsl_destroy_snapshot(snap3name, B_FALSE);
	if (error && error != ENOENT)
		fatal(B_FALSE, "dsl_destroy_snapshot(%s) = %d",
		    snap3name, error);
	error = dsl_destroy_snapshot(snap2name, B_FALSE);
	if (error && error != ENOENT)
		fatal(B_FALSE, "dsl_destroy_snapshot(%s) = %d",
		    snap2name, error);
	error = dsl_destroy_head(clone1name);
	if (error && error != ENOENT)
		fatal(B_FALSE, "dsl_destroy_head(%s) = %d", clone1name, error);
	error = dsl_destroy_snapshot(snap1name, B_FALSE);
	if (error && error != ENOENT)
		fatal(B_FALSE, "dsl_destroy_snapshot(%s) = %d",
		    snap1name, error);

	umem_free(snap1name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(clone1name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(snap2name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(clone2name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(snap3name, ZFS_MAX_DATASET_NAME_LEN);
}

/*
 * Verify dsl_dataset_promote handles EBUSY
 */
void
ztest_dsl_dataset_promote_busy(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os;
	char *snap1name;
	char *clone1name;
	char *snap2name;
	char *clone2name;
	char *snap3name;
	char *osname = zd->zd_name;
	int error;

	snap1name  = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	clone1name = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	snap2name  = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	clone2name = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);
	snap3name  = umem_alloc(ZFS_MAX_DATASET_NAME_LEN, UMEM_NOFAIL);

	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	ztest_dsl_dataset_cleanup(osname, id);

	(void) snprintf(snap1name, ZFS_MAX_DATASET_NAME_LEN, "%s@s1_%"PRIu64"",
	    osname, id);
	(void) snprintf(clone1name, ZFS_MAX_DATASET_NAME_LEN, "%s/c1_%"PRIu64"",
	    osname, id);
	(void) snprintf(snap2name, ZFS_MAX_DATASET_NAME_LEN, "%s@s2_%"PRIu64"",
	    clone1name, id);
	(void) snprintf(clone2name, ZFS_MAX_DATASET_NAME_LEN, "%s/c2_%"PRIu64"",
	    osname, id);
	(void) snprintf(snap3name, ZFS_MAX_DATASET_NAME_LEN, "%s@s3_%"PRIu64"",
	    clone1name, id);

	error = dmu_objset_snapshot_one(osname, strchr(snap1name, '@') + 1);
	if (error && error != EEXIST) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(B_FALSE, "dmu_take_snapshot(%s) = %d", snap1name, error);
	}

	error = dmu_objset_clone(clone1name, snap1name);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(B_FALSE, "dmu_objset_create(%s) = %d", clone1name, error);
	}

	error = dmu_objset_snapshot_one(clone1name, strchr(snap2name, '@') + 1);
	if (error && error != EEXIST) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(B_FALSE, "dmu_open_snapshot(%s) = %d", snap2name, error);
	}

	error = dmu_objset_snapshot_one(clone1name, strchr(snap3name, '@') + 1);
	if (error && error != EEXIST) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(B_FALSE, "dmu_open_snapshot(%s) = %d", snap3name, error);
	}

	error = dmu_objset_clone(clone2name, snap3name);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc(FTAG);
			goto out;
		}
		fatal(B_FALSE, "dmu_objset_create(%s) = %d", clone2name, error);
	}

	error = ztest_dmu_objset_own(snap2name, DMU_OST_ANY, B_TRUE, B_TRUE,
	    FTAG, &os);
	if (error)
		fatal(B_FALSE, "dmu_objset_own(%s) = %d", snap2name, error);
	error = dsl_dataset_promote(clone2name, NULL);
	if (error == ENOSPC) {
		dmu_objset_disown(os, B_TRUE, FTAG);
		ztest_record_enospc(FTAG);
		goto out;
	}
	if (error != EBUSY)
		fatal(B_FALSE, "dsl_dataset_promote(%s), %d, not EBUSY",
		    clone2name, error);
	dmu_objset_disown(os, B_TRUE, FTAG);

out:
	ztest_dsl_dataset_cleanup(osname, id);

	(void) pthread_rwlock_unlock(&ztest_name_lock);

	umem_free(snap1name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(clone1name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(snap2name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(clone2name, ZFS_MAX_DATASET_NAME_LEN);
	umem_free(snap3name, ZFS_MAX_DATASET_NAME_LEN);
}

#undef OD_ARRAY_SIZE
#define	OD_ARRAY_SIZE	4

/*
 * Verify that dmu_object_{alloc,free} work as expected.
 */
void
ztest_dmu_object_alloc_free(ztest_ds_t *zd, uint64_t id)
{
	ztest_od_t *od;
	int batchsize;
	int size;
	int b;

	size = sizeof (ztest_od_t) * OD_ARRAY_SIZE;
	od = umem_alloc(size, UMEM_NOFAIL);
	batchsize = OD_ARRAY_SIZE;

	for (b = 0; b < batchsize; b++)
		ztest_od_init(od + b, id, FTAG, b, DMU_OT_UINT64_OTHER,
		    0, 0, 0);

	/*
	 * Destroy the previous batch of objects, create a new batch,
	 * and do some I/O on the new objects.
	 */
	if (ztest_object_init(zd, od, size, B_TRUE) != 0) {
		zd->zd_od = NULL;
		umem_free(od, size);
		return;
	}

	while (ztest_random(4 * batchsize) != 0)
		ztest_io(zd, od[ztest_random(batchsize)].od_object,
		    ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);

	umem_free(od, size);
}

/*
 * Rewind the global allocator to verify object allocation backfilling.
 */
void
ztest_dmu_object_next_chunk(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	objset_t *os = zd->zd_os;
	uint_t dnodes_per_chunk = 1 << dmu_object_alloc_chunk_shift;
	uint64_t object;

	/*
	 * Rewind the global allocator randomly back to a lower object number
	 * to force backfilling and reclamation of recently freed dnodes.
	 */
	mutex_enter(&os->os_obj_lock);
	object = ztest_random(os->os_obj_next_chunk);
	os->os_obj_next_chunk = P2ALIGN(object, dnodes_per_chunk);
	mutex_exit(&os->os_obj_lock);
}

#undef OD_ARRAY_SIZE
#define	OD_ARRAY_SIZE	2

/*
 * Verify that dmu_{read,write} work as expected.
 */
void
ztest_dmu_read_write(ztest_ds_t *zd, uint64_t id)
{
	int size;
	ztest_od_t *od;

	objset_t *os = zd->zd_os;
	size = sizeof (ztest_od_t) * OD_ARRAY_SIZE;
	od = umem_alloc(size, UMEM_NOFAIL);
	dmu_tx_t *tx;
	int freeit, error;
	uint64_t i, n, s, txg;
	bufwad_t *packbuf, *bigbuf, *pack, *bigH, *bigT;
	uint64_t packobj, packoff, packsize, bigobj, bigoff, bigsize;
	uint64_t chunksize = (1000 + ztest_random(1000)) * sizeof (uint64_t);
	uint64_t regions = 997;
	uint64_t stride = 123456789ULL;
	uint64_t width = 40;
	int free_percent = 5;

	/*
	 * This test uses two objects, packobj and bigobj, that are always
	 * updated together (i.e. in the same tx) so that their contents are
	 * in sync and can be compared.  Their contents relate to each other
	 * in a simple way: packobj is a dense array of 'bufwad' structures,
	 * while bigobj is a sparse array of the same bufwads.  Specifically,
	 * for any index n, there are three bufwads that should be identical:
	 *
	 *	packobj, at offset n * sizeof (bufwad_t)
	 *	bigobj, at the head of the nth chunk
	 *	bigobj, at the tail of the nth chunk
	 *
	 * The chunk size is arbitrary. It doesn't have to be a power of two,
	 * and it doesn't have any relation to the object blocksize.
	 * The only requirement is that it can hold at least two bufwads.
	 *
	 * Normally, we write the bufwad to each of these locations.
	 * However, free_percent of the time we instead write zeroes to
	 * packobj and perform a dmu_free_range() on bigobj.  By comparing
	 * bigobj to packobj, we can verify that the DMU is correctly
	 * tracking which parts of an object are allocated and free,
	 * and that the contents of the allocated blocks are correct.
	 */

	/*
	 * Read the directory info.  If it's the first time, set things up.
	 */
	ztest_od_init(od, id, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, chunksize);
	ztest_od_init(od + 1, id, FTAG, 1, DMU_OT_UINT64_OTHER, 0, 0,
	    chunksize);

	if (ztest_object_init(zd, od, size, B_FALSE) != 0) {
		umem_free(od, size);
		return;
	}

	bigobj = od[0].od_object;
	packobj = od[1].od_object;
	chunksize = od[0].od_gen;
	ASSERT3U(chunksize, ==, od[1].od_gen);

	/*
	 * Prefetch a random chunk of the big object.
	 * Our aim here is to get some async reads in flight
	 * for blocks that we may free below; the DMU should
	 * handle this race correctly.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(2 * width - 1);
	dmu_prefetch(os, bigobj, 0, n * chunksize, s * chunksize,
	    ZIO_PRIORITY_SYNC_READ);

	/*
	 * Pick a random index and compute the offsets into packobj and bigobj.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(width - 1);

	packoff = n * sizeof (bufwad_t);
	packsize = s * sizeof (bufwad_t);

	bigoff = n * chunksize;
	bigsize = s * chunksize;

	packbuf = umem_alloc(packsize, UMEM_NOFAIL);
	bigbuf = umem_alloc(bigsize, UMEM_NOFAIL);

	/*
	 * free_percent of the time, free a range of bigobj rather than
	 * overwriting it.
	 */
	freeit = (ztest_random(100) < free_percent);

	/*
	 * Read the current contents of our objects.
	 */
	error = dmu_read(os, packobj, packoff, packsize, packbuf,
	    DMU_READ_PREFETCH);
	ASSERT0(error);
	error = dmu_read(os, bigobj, bigoff, bigsize, bigbuf,
	    DMU_READ_PREFETCH);
	ASSERT0(error);

	/*
	 * Get a tx for the mods to both packobj and bigobj.
	 */
	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, packobj, packoff, packsize);

	if (freeit)
		dmu_tx_hold_free(tx, bigobj, bigoff, bigsize);
	else
		dmu_tx_hold_write(tx, bigobj, bigoff, bigsize);

	/* This accounts for setting the checksum/compression. */
	dmu_tx_hold_bonus(tx, bigobj);

	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0) {
		umem_free(packbuf, packsize);
		umem_free(bigbuf, bigsize);
		umem_free(od, size);
		return;
	}

	enum zio_checksum cksum;
	do {
		cksum = (enum zio_checksum)
		    ztest_random_dsl_prop(ZFS_PROP_CHECKSUM);
	} while (cksum >= ZIO_CHECKSUM_LEGACY_FUNCTIONS);
	dmu_object_set_checksum(os, bigobj, cksum, tx);

	enum zio_compress comp;
	do {
		comp = (enum zio_compress)
		    ztest_random_dsl_prop(ZFS_PROP_COMPRESSION);
	} while (comp >= ZIO_COMPRESS_LEGACY_FUNCTIONS);
	dmu_object_set_compress(os, bigobj, comp, tx);

	/*
	 * For each index from n to n + s, verify that the existing bufwad
	 * in packobj matches the bufwads at the head and tail of the
	 * corresponding chunk in bigobj.  Then update all three bufwads
	 * with the new values we want to write out.
	 */
	for (i = 0; i < s; i++) {
		/* LINTED */
		pack = (bufwad_t *)((char *)packbuf + i * sizeof (bufwad_t));
		/* LINTED */
		bigH = (bufwad_t *)((char *)bigbuf + i * chunksize);
		/* LINTED */
		bigT = (bufwad_t *)((char *)bigH + chunksize) - 1;

		ASSERT3U((uintptr_t)bigH - (uintptr_t)bigbuf, <, bigsize);
		ASSERT3U((uintptr_t)bigT - (uintptr_t)bigbuf, <, bigsize);

		if (pack->bw_txg > txg)
			fatal(B_FALSE,
			    "future leak: got %"PRIx64", open txg is %"PRIx64"",
			    pack->bw_txg, txg);

		if (pack->bw_data != 0 && pack->bw_index != n + i)
			fatal(B_FALSE, "wrong index: "
			    "got %"PRIx64", wanted %"PRIx64"+%"PRIx64"",
			    pack->bw_index, n, i);

		if (memcmp(pack, bigH, sizeof (bufwad_t)) != 0)
			fatal(B_FALSE, "pack/bigH mismatch in %p/%p",
			    pack, bigH);

		if (memcmp(pack, bigT, sizeof (bufwad_t)) != 0)
			fatal(B_FALSE, "pack/bigT mismatch in %p/%p",
			    pack, bigT);

		if (freeit) {
			memset(pack, 0, sizeof (bufwad_t));
		} else {
			pack->bw_index = n + i;
			pack->bw_txg = txg;
			pack->bw_data = 1 + ztest_random(-2ULL);
		}
		*bigH = *pack;
		*bigT = *pack;
	}

	/*
	 * We've verified all the old bufwads, and made new ones.
	 * Now write them out.
	 */
	dmu_write(os, packobj, packoff, packsize, packbuf, tx);

	if (freeit) {
		if (ztest_opts.zo_verbose >= 7) {
			(void) printf("freeing offset %"PRIx64" size %"PRIx64""
			    " txg %"PRIx64"\n",
			    bigoff, bigsize, txg);
		}
		VERIFY0(dmu_free_range(os, bigobj, bigoff, bigsize, tx));
	} else {
		if (ztest_opts.zo_verbose >= 7) {
			(void) printf("writing offset %"PRIx64" size %"PRIx64""
			    " txg %"PRIx64"\n",
			    bigoff, bigsize, txg);
		}
		dmu_write(os, bigobj, bigoff, bigsize, bigbuf, tx);
	}

	dmu_tx_commit(tx);

	/*
	 * Sanity check the stuff we just wrote.
	 */
	{
		void *packcheck = umem_alloc(packsize, UMEM_NOFAIL);
		void *bigcheck = umem_alloc(bigsize, UMEM_NOFAIL);

		VERIFY0(dmu_read(os, packobj, packoff,
		    packsize, packcheck, DMU_READ_PREFETCH));
		VERIFY0(dmu_read(os, bigobj, bigoff,
		    bigsize, bigcheck, DMU_READ_PREFETCH));

		ASSERT0(memcmp(packbuf, packcheck, packsize));
		ASSERT0(memcmp(bigbuf, bigcheck, bigsize));

		umem_free(packcheck, packsize);
		umem_free(bigcheck, bigsize);
	}

	umem_free(packbuf, packsize);
	umem_free(bigbuf, bigsize);
	umem_free(od, size);
}

static void
compare_and_update_pbbufs(uint64_t s, bufwad_t *packbuf, bufwad_t *bigbuf,
    uint64_t bigsize, uint64_t n, uint64_t chunksize, uint64_t txg)
{
	uint64_t i;
	bufwad_t *pack;
	bufwad_t *bigH;
	bufwad_t *bigT;

	/*
	 * For each index from n to n + s, verify that the existing bufwad
	 * in packobj matches the bufwads at the head and tail of the
	 * corresponding chunk in bigobj.  Then update all three bufwads
	 * with the new values we want to write out.
	 */
	for (i = 0; i < s; i++) {
		/* LINTED */
		pack = (bufwad_t *)((char *)packbuf + i * sizeof (bufwad_t));
		/* LINTED */
		bigH = (bufwad_t *)((char *)bigbuf + i * chunksize);
		/* LINTED */
		bigT = (bufwad_t *)((char *)bigH + chunksize) - 1;

		ASSERT3U((uintptr_t)bigH - (uintptr_t)bigbuf, <, bigsize);
		ASSERT3U((uintptr_t)bigT - (uintptr_t)bigbuf, <, bigsize);

		if (pack->bw_txg > txg)
			fatal(B_FALSE,
			    "future leak: got %"PRIx64", open txg is %"PRIx64"",
			    pack->bw_txg, txg);

		if (pack->bw_data != 0 && pack->bw_index != n + i)
			fatal(B_FALSE, "wrong index: "
			    "got %"PRIx64", wanted %"PRIx64"+%"PRIx64"",
			    pack->bw_index, n, i);

		if (memcmp(pack, bigH, sizeof (bufwad_t)) != 0)
			fatal(B_FALSE, "pack/bigH mismatch in %p/%p",
			    pack, bigH);

		if (memcmp(pack, bigT, sizeof (bufwad_t)) != 0)
			fatal(B_FALSE, "pack/bigT mismatch in %p/%p",
			    pack, bigT);

		pack->bw_index = n + i;
		pack->bw_txg = txg;
		pack->bw_data = 1 + ztest_random(-2ULL);

		*bigH = *pack;
		*bigT = *pack;
	}
}

#undef OD_ARRAY_SIZE
#define	OD_ARRAY_SIZE	2

void
ztest_dmu_read_write_zcopy(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t *od;
	dmu_tx_t *tx;
	uint64_t i;
	int error;
	int size;
	uint64_t n, s, txg;
	bufwad_t *packbuf, *bigbuf;
	uint64_t packobj, packoff, packsize, bigobj, bigoff, bigsize;
	uint64_t blocksize = ztest_random_blocksize();
	uint64_t chunksize = blocksize;
	uint64_t regions = 997;
	uint64_t stride = 123456789ULL;
	uint64_t width = 9;
	dmu_buf_t *bonus_db;
	arc_buf_t **bigbuf_arcbufs;
	dmu_object_info_t doi;

	size = sizeof (ztest_od_t) * OD_ARRAY_SIZE;
	od = umem_alloc(size, UMEM_NOFAIL);

	/*
	 * This test uses two objects, packobj and bigobj, that are always
	 * updated together (i.e. in the same tx) so that their contents are
	 * in sync and can be compared.  Their contents relate to each other
	 * in a simple way: packobj is a dense array of 'bufwad' structures,
	 * while bigobj is a sparse array of the same bufwads.  Specifically,
	 * for any index n, there are three bufwads that should be identical:
	 *
	 *	packobj, at offset n * sizeof (bufwad_t)
	 *	bigobj, at the head of the nth chunk
	 *	bigobj, at the tail of the nth chunk
	 *
	 * The chunk size is set equal to bigobj block size so that
	 * dmu_assign_arcbuf_by_dbuf() can be tested for object updates.
	 */

	/*
	 * Read the directory info.  If it's the first time, set things up.
	 */
	ztest_od_init(od, id, FTAG, 0, DMU_OT_UINT64_OTHER, blocksize, 0, 0);
	ztest_od_init(od + 1, id, FTAG, 1, DMU_OT_UINT64_OTHER, 0, 0,
	    chunksize);


	if (ztest_object_init(zd, od, size, B_FALSE) != 0) {
		umem_free(od, size);
		return;
	}

	bigobj = od[0].od_object;
	packobj = od[1].od_object;
	blocksize = od[0].od_blocksize;
	chunksize = blocksize;
	ASSERT3U(chunksize, ==, od[1].od_gen);

	VERIFY0(dmu_object_info(os, bigobj, &doi));
	VERIFY(ISP2(doi.doi_data_block_size));
	VERIFY3U(chunksize, ==, doi.doi_data_block_size);
	VERIFY3U(chunksize, >=, 2 * sizeof (bufwad_t));

	/*
	 * Pick a random index and compute the offsets into packobj and bigobj.
	 */
	n = ztest_random(regions) * stride + ztest_random(width);
	s = 1 + ztest_random(width - 1);

	packoff = n * sizeof (bufwad_t);
	packsize = s * sizeof (bufwad_t);

	bigoff = n * chunksize;
	bigsize = s * chunksize;

	packbuf = umem_zalloc(packsize, UMEM_NOFAIL);
	bigbuf = umem_zalloc(bigsize, UMEM_NOFAIL);

	VERIFY0(dmu_bonus_hold(os, bigobj, FTAG, &bonus_db));

	bigbuf_arcbufs = umem_zalloc(2 * s * sizeof (arc_buf_t *), UMEM_NOFAIL);

	/*
	 * Iteration 0 test zcopy for DB_UNCACHED dbufs.
	 * Iteration 1 test zcopy to already referenced dbufs.
	 * Iteration 2 test zcopy to dirty dbuf in the same txg.
	 * Iteration 3 test zcopy to dbuf dirty in previous txg.
	 * Iteration 4 test zcopy when dbuf is no longer dirty.
	 * Iteration 5 test zcopy when it can't be done.
	 * Iteration 6 one more zcopy write.
	 */
	for (i = 0; i < 7; i++) {
		uint64_t j;
		uint64_t off;

		/*
		 * In iteration 5 (i == 5) use arcbufs
		 * that don't match bigobj blksz to test
		 * dmu_assign_arcbuf_by_dbuf() when it can't directly
		 * assign an arcbuf to a dbuf.
		 */
		for (j = 0; j < s; j++) {
			if (i != 5 || chunksize < (SPA_MINBLOCKSIZE * 2)) {
				bigbuf_arcbufs[j] =
				    dmu_request_arcbuf(bonus_db, chunksize);
			} else {
				bigbuf_arcbufs[2 * j] =
				    dmu_request_arcbuf(bonus_db, chunksize / 2);
				bigbuf_arcbufs[2 * j + 1] =
				    dmu_request_arcbuf(bonus_db, chunksize / 2);
			}
		}

		/*
		 * Get a tx for the mods to both packobj and bigobj.
		 */
		tx = dmu_tx_create(os);

		dmu_tx_hold_write(tx, packobj, packoff, packsize);
		dmu_tx_hold_write(tx, bigobj, bigoff, bigsize);

		txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
		if (txg == 0) {
			umem_free(packbuf, packsize);
			umem_free(bigbuf, bigsize);
			for (j = 0; j < s; j++) {
				if (i != 5 ||
				    chunksize < (SPA_MINBLOCKSIZE * 2)) {
					dmu_return_arcbuf(bigbuf_arcbufs[j]);
				} else {
					dmu_return_arcbuf(
					    bigbuf_arcbufs[2 * j]);
					dmu_return_arcbuf(
					    bigbuf_arcbufs[2 * j + 1]);
				}
			}
			umem_free(bigbuf_arcbufs, 2 * s * sizeof (arc_buf_t *));
			umem_free(od, size);
			dmu_buf_rele(bonus_db, FTAG);
			return;
		}

		/*
		 * 50% of the time don't read objects in the 1st iteration to
		 * test dmu_assign_arcbuf_by_dbuf() for the case when there are
		 * no existing dbufs for the specified offsets.
		 */
		if (i != 0 || ztest_random(2) != 0) {
			error = dmu_read(os, packobj, packoff,
			    packsize, packbuf, DMU_READ_PREFETCH);
			ASSERT0(error);
			error = dmu_read(os, bigobj, bigoff, bigsize,
			    bigbuf, DMU_READ_PREFETCH);
			ASSERT0(error);
		}
		compare_and_update_pbbufs(s, packbuf, bigbuf, bigsize,
		    n, chunksize, txg);

		/*
		 * We've verified all the old bufwads, and made new ones.
		 * Now write them out.
		 */
		dmu_write(os, packobj, packoff, packsize, packbuf, tx);
		if (ztest_opts.zo_verbose >= 7) {
			(void) printf("writing offset %"PRIx64" size %"PRIx64""
			    " txg %"PRIx64"\n",
			    bigoff, bigsize, txg);
		}
		for (off = bigoff, j = 0; j < s; j++, off += chunksize) {
			dmu_buf_t *dbt;
			if (i != 5 || chunksize < (SPA_MINBLOCKSIZE * 2)) {
				memcpy(bigbuf_arcbufs[j]->b_data,
				    (caddr_t)bigbuf + (off - bigoff),
				    chunksize);
			} else {
				memcpy(bigbuf_arcbufs[2 * j]->b_data,
				    (caddr_t)bigbuf + (off - bigoff),
				    chunksize / 2);
				memcpy(bigbuf_arcbufs[2 * j + 1]->b_data,
				    (caddr_t)bigbuf + (off - bigoff) +
				    chunksize / 2,
				    chunksize / 2);
			}

			if (i == 1) {
				VERIFY(dmu_buf_hold(os, bigobj, off,
				    FTAG, &dbt, DMU_READ_NO_PREFETCH) == 0);
			}
			if (i != 5 || chunksize < (SPA_MINBLOCKSIZE * 2)) {
				VERIFY0(dmu_assign_arcbuf_by_dbuf(bonus_db,
				    off, bigbuf_arcbufs[j], tx));
			} else {
				VERIFY0(dmu_assign_arcbuf_by_dbuf(bonus_db,
				    off, bigbuf_arcbufs[2 * j], tx));
				VERIFY0(dmu_assign_arcbuf_by_dbuf(bonus_db,
				    off + chunksize / 2,
				    bigbuf_arcbufs[2 * j + 1], tx));
			}
			if (i == 1) {
				dmu_buf_rele(dbt, FTAG);
			}
		}
		dmu_tx_commit(tx);

		/*
		 * Sanity check the stuff we just wrote.
		 */
		{
			void *packcheck = umem_alloc(packsize, UMEM_NOFAIL);
			void *bigcheck = umem_alloc(bigsize, UMEM_NOFAIL);

			VERIFY0(dmu_read(os, packobj, packoff,
			    packsize, packcheck, DMU_READ_PREFETCH));
			VERIFY0(dmu_read(os, bigobj, bigoff,
			    bigsize, bigcheck, DMU_READ_PREFETCH));

			ASSERT0(memcmp(packbuf, packcheck, packsize));
			ASSERT0(memcmp(bigbuf, bigcheck, bigsize));

			umem_free(packcheck, packsize);
			umem_free(bigcheck, bigsize);
		}
		if (i == 2) {
			txg_wait_open(dmu_objset_pool(os), 0, B_TRUE);
		} else if (i == 3) {
			txg_wait_synced(dmu_objset_pool(os), 0);
		}
	}

	dmu_buf_rele(bonus_db, FTAG);
	umem_free(packbuf, packsize);
	umem_free(bigbuf, bigsize);
	umem_free(bigbuf_arcbufs, 2 * s * sizeof (arc_buf_t *));
	umem_free(od, size);
}

void
ztest_dmu_write_parallel(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	ztest_od_t *od;

	od = umem_alloc(sizeof (ztest_od_t), UMEM_NOFAIL);
	uint64_t offset = (1ULL << (ztest_random(20) + 43)) +
	    (ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);

	/*
	 * Have multiple threads write to large offsets in an object
	 * to verify that parallel writes to an object -- even to the
	 * same blocks within the object -- doesn't cause any trouble.
	 */
	ztest_od_init(od, ID_PARALLEL, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (ztest_od_t), B_FALSE) != 0)
		return;

	while (ztest_random(10) != 0)
		ztest_io(zd, od->od_object, offset);

	umem_free(od, sizeof (ztest_od_t));
}

void
ztest_dmu_prealloc(ztest_ds_t *zd, uint64_t id)
{
	ztest_od_t *od;
	uint64_t offset = (1ULL << (ztest_random(4) + SPA_MAXBLOCKSHIFT)) +
	    (ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);
	uint64_t count = ztest_random(20) + 1;
	uint64_t blocksize = ztest_random_blocksize();
	void *data;

	od = umem_alloc(sizeof (ztest_od_t), UMEM_NOFAIL);

	ztest_od_init(od, id, FTAG, 0, DMU_OT_UINT64_OTHER, blocksize, 0, 0);

	if (ztest_object_init(zd, od, sizeof (ztest_od_t),
	    !ztest_random(2)) != 0) {
		umem_free(od, sizeof (ztest_od_t));
		return;
	}

	if (ztest_truncate(zd, od->od_object, offset, count * blocksize) != 0) {
		umem_free(od, sizeof (ztest_od_t));
		return;
	}

	ztest_prealloc(zd, od->od_object, offset, count * blocksize);

	data = umem_zalloc(blocksize, UMEM_NOFAIL);

	while (ztest_random(count) != 0) {
		uint64_t randoff = offset + (ztest_random(count) * blocksize);
		if (ztest_write(zd, od->od_object, randoff, blocksize,
		    data) != 0)
			break;
		while (ztest_random(4) != 0)
			ztest_io(zd, od->od_object, randoff);
	}

	umem_free(data, blocksize);
	umem_free(od, sizeof (ztest_od_t));
}

/*
 * Verify that zap_{create,destroy,add,remove,update} work as expected.
 */
#define	ZTEST_ZAP_MIN_INTS	1
#define	ZTEST_ZAP_MAX_INTS	4
#define	ZTEST_ZAP_MAX_PROPS	1000

void
ztest_zap(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t *od;
	uint64_t object;
	uint64_t txg, last_txg;
	uint64_t value[ZTEST_ZAP_MAX_INTS];
	uint64_t zl_ints, zl_intsize, prop;
	int i, ints;
	dmu_tx_t *tx;
	char propname[100], txgname[100];
	int error;
	const char *const hc[2] = { "s.acl.h", ".s.open.h.hyLZlg" };

	od = umem_alloc(sizeof (ztest_od_t), UMEM_NOFAIL);
	ztest_od_init(od, id, FTAG, 0, DMU_OT_ZAP_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (ztest_od_t),
	    !ztest_random(2)) != 0)
		goto out;

	object = od->od_object;

	/*
	 * Generate a known hash collision, and verify that
	 * we can lookup and remove both entries.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0)
		goto out;
	for (i = 0; i < 2; i++) {
		value[i] = i;
		VERIFY0(zap_add(os, object, hc[i], sizeof (uint64_t),
		    1, &value[i], tx));
	}
	for (i = 0; i < 2; i++) {
		VERIFY3U(EEXIST, ==, zap_add(os, object, hc[i],
		    sizeof (uint64_t), 1, &value[i], tx));
		VERIFY0(
		    zap_length(os, object, hc[i], &zl_intsize, &zl_ints));
		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, 1);
	}
	for (i = 0; i < 2; i++) {
		VERIFY0(zap_remove(os, object, hc[i], tx));
	}
	dmu_tx_commit(tx);

	/*
	 * Generate a bunch of random entries.
	 */
	ints = MAX(ZTEST_ZAP_MIN_INTS, object % ZTEST_ZAP_MAX_INTS);

	prop = ztest_random(ZTEST_ZAP_MAX_PROPS);
	(void) sprintf(propname, "prop_%"PRIu64"", prop);
	(void) sprintf(txgname, "txg_%"PRIu64"", prop);
	memset(value, 0, sizeof (value));
	last_txg = 0;

	/*
	 * If these zap entries already exist, validate their contents.
	 */
	error = zap_length(os, object, txgname, &zl_intsize, &zl_ints);
	if (error == 0) {
		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, 1);

		VERIFY0(zap_lookup(os, object, txgname, zl_intsize,
		    zl_ints, &last_txg));

		VERIFY0(zap_length(os, object, propname, &zl_intsize,
		    &zl_ints));

		ASSERT3U(zl_intsize, ==, sizeof (uint64_t));
		ASSERT3U(zl_ints, ==, ints);

		VERIFY0(zap_lookup(os, object, propname, zl_intsize,
		    zl_ints, value));

		for (i = 0; i < ints; i++) {
			ASSERT3U(value[i], ==, last_txg + object + i);
		}
	} else {
		ASSERT3U(error, ==, ENOENT);
	}

	/*
	 * Atomically update two entries in our zap object.
	 * The first is named txg_%llu, and contains the txg
	 * in which the property was last updated.  The second
	 * is named prop_%llu, and the nth element of its value
	 * should be txg + object + n.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0)
		goto out;

	if (last_txg > txg)
		fatal(B_FALSE, "zap future leak: old %"PRIu64" new %"PRIu64"",
		    last_txg, txg);

	for (i = 0; i < ints; i++)
		value[i] = txg + object + i;

	VERIFY0(zap_update(os, object, txgname, sizeof (uint64_t),
	    1, &txg, tx));
	VERIFY0(zap_update(os, object, propname, sizeof (uint64_t),
	    ints, value, tx));

	dmu_tx_commit(tx);

	/*
	 * Remove a random pair of entries.
	 */
	prop = ztest_random(ZTEST_ZAP_MAX_PROPS);
	(void) sprintf(propname, "prop_%"PRIu64"", prop);
	(void) sprintf(txgname, "txg_%"PRIu64"", prop);

	error = zap_length(os, object, txgname, &zl_intsize, &zl_ints);

	if (error == ENOENT)
		goto out;

	ASSERT0(error);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
	txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
	if (txg == 0)
		goto out;
	VERIFY0(zap_remove(os, object, txgname, tx));
	VERIFY0(zap_remove(os, object, propname, tx));
	dmu_tx_commit(tx);
out:
	umem_free(od, sizeof (ztest_od_t));
}

/*
 * Test case to test the upgrading of a microzap to fatzap.
 */
void
ztest_fzap(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t *od;
	uint64_t object, txg, value;

	od = umem_alloc(sizeof (ztest_od_t), UMEM_NOFAIL);
	ztest_od_init(od, id, FTAG, 0, DMU_OT_ZAP_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (ztest_od_t),
	    !ztest_random(2)) != 0)
		goto out;
	object = od->od_object;

	/*
	 * Add entries to this ZAP and make sure it spills over
	 * and gets upgraded to a fatzap. Also, since we are adding
	 * 2050 entries we should see ptrtbl growth and leaf-block split.
	 */
	for (value = 0; value < 2050; value++) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		dmu_tx_t *tx;
		int error;

		(void) snprintf(name, sizeof (name), "fzap-%"PRIu64"-%"PRIu64"",
		    id, value);

		tx = dmu_tx_create(os);
		dmu_tx_hold_zap(tx, object, B_TRUE, name);
		txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
		if (txg == 0)
			goto out;
		error = zap_add(os, object, name, sizeof (uint64_t), 1,
		    &value, tx);
		ASSERT(error == 0 || error == EEXIST);
		dmu_tx_commit(tx);
	}
out:
	umem_free(od, sizeof (ztest_od_t));
}

void
ztest_zap_parallel(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	objset_t *os = zd->zd_os;
	ztest_od_t *od;
	uint64_t txg, object, count, wsize, wc, zl_wsize, zl_wc;
	dmu_tx_t *tx;
	int i, namelen, error;
	int micro = ztest_random(2);
	char name[20], string_value[20];
	void *data;

	od = umem_alloc(sizeof (ztest_od_t), UMEM_NOFAIL);
	ztest_od_init(od, ID_PARALLEL, FTAG, micro, DMU_OT_ZAP_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (ztest_od_t), B_FALSE) != 0) {
		umem_free(od, sizeof (ztest_od_t));
		return;
	}

	object = od->od_object;

	/*
	 * Generate a random name of the form 'xxx.....' where each
	 * x is a random printable character and the dots are dots.
	 * There are 94 such characters, and the name length goes from
	 * 6 to 20, so there are 94^3 * 15 = 12,458,760 possible names.
	 */
	namelen = ztest_random(sizeof (name) - 5) + 5 + 1;

	for (i = 0; i < 3; i++)
		name[i] = '!' + ztest_random('~' - '!' + 1);
	for (; i < namelen - 1; i++)
		name[i] = '.';
	name[i] = '\0';

	if ((namelen & 1) || micro) {
		wsize = sizeof (txg);
		wc = 1;
		data = &txg;
	} else {
		wsize = 1;
		wc = namelen;
		data = string_value;
	}

	count = -1ULL;
	VERIFY0(zap_count(os, object, &count));
	ASSERT3S(count, !=, -1ULL);

	/*
	 * Select an operation: length, lookup, add, update, remove.
	 */
	i = ztest_random(5);

	if (i >= 2) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_zap(tx, object, B_TRUE, NULL);
		txg = ztest_tx_assign(tx, TXG_MIGHTWAIT, FTAG);
		if (txg == 0) {
			umem_free(od, sizeof (ztest_od_t));
			return;
		}
		memcpy(string_value, name, namelen);
	} else {
		tx = NULL;
		txg = 0;
		memset(string_value, 0, namelen);
	}

	switch (i) {

	case 0:
		error = zap_length(os, object, name, &zl_wsize, &zl_wc);
		if (error == 0) {
			ASSERT3U(wsize, ==, zl_wsize);
			ASSERT3U(wc, ==, zl_wc);
		} else {
			ASSERT3U(error, ==, ENOENT);
		}
		break;

	case 1:
		error = zap_lookup(os, object, name, wsize, wc, data);
		if (error == 0) {
			if (data == string_value &&
			    memcmp(name, data, namelen) != 0)
				fatal(B_FALSE, "name '%s' != val '%s' len %d",
				    name, (char *)data, namelen);
		} else {
			ASSERT3U(error, ==, ENOENT);
		}
		break;

	case 2:
		error = zap_add(os, object, name, wsize, wc, data, tx);
		ASSERT(error == 0 || error == EEXIST);
		break;

	case 3:
		VERIFY0(zap_update(os, object, name, wsize, wc, data, tx));
		break;

	case 4:
		error = zap_remove(os, object, name, tx);
		ASSERT(error == 0 || error == ENOENT);
		break;
	}

	if (tx != NULL)
		dmu_tx_commit(tx);

	umem_free(od, sizeof (ztest_od_t));
}

/*
 * Commit callback data.
 */
typedef struct ztest_cb_data {
	list_node_t		zcd_node;
	uint64_t		zcd_txg;
	int			zcd_expected_err;
	boolean_t		zcd_added;
	boolean_t		zcd_called;
	spa_t			*zcd_spa;
} ztest_cb_data_t;

/* This is the actual commit callback function */
static void
ztest_commit_callback(void *arg, int error)
{
	ztest_cb_data_t *data = arg;
	uint64_t synced_txg;

	VERIFY3P(data, !=, NULL);
	VERIFY3S(data->zcd_expected_err, ==, error);
	VERIFY(!data->zcd_called);

	synced_txg = spa_last_synced_txg(data->zcd_spa);
	if (data->zcd_txg > synced_txg)
		fatal(B_FALSE,
		    "commit callback of txg %"PRIu64" called prematurely, "
		    "last synced txg = %"PRIu64"\n",
		    data->zcd_txg, synced_txg);

	data->zcd_called = B_TRUE;

	if (error == ECANCELED) {
		ASSERT0(data->zcd_txg);
		ASSERT(!data->zcd_added);

		/*
		 * The private callback data should be destroyed here, but
		 * since we are going to check the zcd_called field after
		 * dmu_tx_abort(), we will destroy it there.
		 */
		return;
	}

	ASSERT(data->zcd_added);
	ASSERT3U(data->zcd_txg, !=, 0);

	(void) mutex_enter(&zcl.zcl_callbacks_lock);

	/* See if this cb was called more quickly */
	if ((synced_txg - data->zcd_txg) < zc_min_txg_delay)
		zc_min_txg_delay = synced_txg - data->zcd_txg;

	/* Remove our callback from the list */
	list_remove(&zcl.zcl_callbacks, data);

	(void) mutex_exit(&zcl.zcl_callbacks_lock);

	umem_free(data, sizeof (ztest_cb_data_t));
}

/* Allocate and initialize callback data structure */
static ztest_cb_data_t *
ztest_create_cb_data(objset_t *os, uint64_t txg)
{
	ztest_cb_data_t *cb_data;

	cb_data = umem_zalloc(sizeof (ztest_cb_data_t), UMEM_NOFAIL);

	cb_data->zcd_txg = txg;
	cb_data->zcd_spa = dmu_objset_spa(os);
	list_link_init(&cb_data->zcd_node);

	return (cb_data);
}

/*
 * Commit callback test.
 */
void
ztest_dmu_commit_callbacks(ztest_ds_t *zd, uint64_t id)
{
	objset_t *os = zd->zd_os;
	ztest_od_t *od;
	dmu_tx_t *tx;
	ztest_cb_data_t *cb_data[3], *tmp_cb;
	uint64_t old_txg, txg;
	int i, error = 0;

	od = umem_alloc(sizeof (ztest_od_t), UMEM_NOFAIL);
	ztest_od_init(od, id, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, 0);

	if (ztest_object_init(zd, od, sizeof (ztest_od_t), B_FALSE) != 0) {
		umem_free(od, sizeof (ztest_od_t));
		return;
	}

	tx = dmu_tx_create(os);

	cb_data[0] = ztest_create_cb_data(os, 0);
	dmu_tx_callback_register(tx, ztest_commit_callback, cb_data[0]);

	dmu_tx_hold_write(tx, od->od_object, 0, sizeof (uint64_t));

	/* Every once in a while, abort the transaction on purpose */
	if (ztest_random(100) == 0)
		error = -1;

	if (!error)
		error = dmu_tx_assign(tx, TXG_NOWAIT);

	txg = error ? 0 : dmu_tx_get_txg(tx);

	cb_data[0]->zcd_txg = txg;
	cb_data[1] = ztest_create_cb_data(os, txg);
	dmu_tx_callback_register(tx, ztest_commit_callback, cb_data[1]);

	if (error) {
		/*
		 * It's not a strict requirement to call the registered
		 * callbacks from inside dmu_tx_abort(), but that's what
		 * it's supposed to happen in the current implementation
		 * so we will check for that.
		 */
		for (i = 0; i < 2; i++) {
			cb_data[i]->zcd_expected_err = ECANCELED;
			VERIFY(!cb_data[i]->zcd_called);
		}

		dmu_tx_abort(tx);

		for (i = 0; i < 2; i++) {
			VERIFY(cb_data[i]->zcd_called);
			umem_free(cb_data[i], sizeof (ztest_cb_data_t));
		}

		umem_free(od, sizeof (ztest_od_t));
		return;
	}

	cb_data[2] = ztest_create_cb_data(os, txg);
	dmu_tx_callback_register(tx, ztest_commit_callback, cb_data[2]);

	/*
	 * Read existing data to make sure there isn't a future leak.
	 */
	VERIFY0(dmu_read(os, od->od_object, 0, sizeof (uint64_t),
	    &old_txg, DMU_READ_PREFETCH));

	if (old_txg > txg)
		fatal(B_FALSE,
		    "future leak: got %"PRIu64", open txg is %"PRIu64"",
		    old_txg, txg);

	dmu_write(os, od->od_object, 0, sizeof (uint64_t), &txg, tx);

	(void) mutex_enter(&zcl.zcl_callbacks_lock);

	/*
	 * Since commit callbacks don't have any ordering requirement and since
	 * it is theoretically possible for a commit callback to be called
	 * after an arbitrary amount of time has elapsed since its txg has been
	 * synced, it is difficult to reliably determine whether a commit
	 * callback hasn't been called due to high load or due to a flawed
	 * implementation.
	 *
	 * In practice, we will assume that if after a certain number of txgs a
	 * commit callback hasn't been called, then most likely there's an
	 * implementation bug..
	 */
	tmp_cb = list_head(&zcl.zcl_callbacks);
	if (tmp_cb != NULL &&
	    tmp_cb->zcd_txg + ZTEST_COMMIT_CB_THRESH < txg) {
		fatal(B_FALSE,
		    "Commit callback threshold exceeded, "
		    "oldest txg: %"PRIu64", open txg: %"PRIu64"\n",
		    tmp_cb->zcd_txg, txg);
	}

	/*
	 * Let's find the place to insert our callbacks.
	 *
	 * Even though the list is ordered by txg, it is possible for the
	 * insertion point to not be the end because our txg may already be
	 * quiescing at this point and other callbacks in the open txg
	 * (from other objsets) may have sneaked in.
	 */
	tmp_cb = list_tail(&zcl.zcl_callbacks);
	while (tmp_cb != NULL && tmp_cb->zcd_txg > txg)
		tmp_cb = list_prev(&zcl.zcl_callbacks, tmp_cb);

	/* Add the 3 callbacks to the list */
	for (i = 0; i < 3; i++) {
		if (tmp_cb == NULL)
			list_insert_head(&zcl.zcl_callbacks, cb_data[i]);
		else
			list_insert_after(&zcl.zcl_callbacks, tmp_cb,
			    cb_data[i]);

		cb_data[i]->zcd_added = B_TRUE;
		VERIFY(!cb_data[i]->zcd_called);

		tmp_cb = cb_data[i];
	}

	zc_cb_counter += 3;

	(void) mutex_exit(&zcl.zcl_callbacks_lock);

	dmu_tx_commit(tx);

	umem_free(od, sizeof (ztest_od_t));
}

/*
 * Visit each object in the dataset. Verify that its properties
 * are consistent what was stored in the block tag when it was created,
 * and that its unused bonus buffer space has not been overwritten.
 */
void
ztest_verify_dnode_bt(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	objset_t *os = zd->zd_os;
	uint64_t obj;
	int err = 0;

	for (obj = 0; err == 0; err = dmu_object_next(os, &obj, FALSE, 0)) {
		ztest_block_tag_t *bt = NULL;
		dmu_object_info_t doi;
		dmu_buf_t *db;

		ztest_object_lock(zd, obj, ZTRL_READER);
		if (dmu_bonus_hold(os, obj, FTAG, &db) != 0) {
			ztest_object_unlock(zd, obj);
			continue;
		}

		dmu_object_info_from_db(db, &doi);
		if (doi.doi_bonus_size >= sizeof (*bt))
			bt = ztest_bt_bonus(db);

		if (bt && bt->bt_magic == BT_MAGIC) {
			ztest_bt_verify(bt, os, obj, doi.doi_dnodesize,
			    bt->bt_offset, bt->bt_gen, bt->bt_txg,
			    bt->bt_crtxg);
			ztest_verify_unused_bonus(db, bt, obj, os, bt->bt_gen);
		}

		dmu_buf_rele(db, FTAG);
		ztest_object_unlock(zd, obj);
	}
}

void
ztest_dsl_prop_get_set(ztest_ds_t *zd, uint64_t id)
{
	(void) id;
	zfs_prop_t proplist[] = {
		ZFS_PROP_CHECKSUM,
		ZFS_PROP_COMPRESSION,
		ZFS_PROP_COPIES,
		ZFS_PROP_DEDUP
	};

	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	for (int p = 0; p < sizeof (proplist) / sizeof (proplist[0]); p++) {
		int error = ztest_dsl_prop_set_uint64(zd->zd_name, proplist[p],
		    ztest_random_dsl_prop(proplist[p]), (int)ztest_random(2));
		ASSERT(error == 0 || error == ENOSPC);
	}

	int error = ztest_dsl_prop_set_uint64(zd->zd_name, ZFS_PROP_RECORDSIZE,
	    ztest_random_blocksize(), (int)ztest_random(2));
	ASSERT(error == 0 || error == ENOSPC);

	(void) pthread_rwlock_unlock(&ztest_name_lock);
}

void
ztest_spa_prop_get_set(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	nvlist_t *props = NULL;

	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	(void) ztest_spa_prop_set_uint64(ZPOOL_PROP_AUTOTRIM, ztest_random(2));

	VERIFY0(spa_prop_get(ztest_spa, &props));

	if (ztest_opts.zo_verbose >= 6)
		dump_nvlist(props, 4);

	fnvlist_free(props);

	(void) pthread_rwlock_unlock(&ztest_name_lock);
}

static int
user_release_one(const char *snapname, const char *holdname)
{
	nvlist_t *snaps, *holds;
	int error;

	snaps = fnvlist_alloc();
	holds = fnvlist_alloc();
	fnvlist_add_boolean(holds, holdname);
	fnvlist_add_nvlist(snaps, snapname, holds);
	fnvlist_free(holds);
	error = dsl_dataset_user_release(snaps, NULL);
	fnvlist_free(snaps);
	return (error);
}

/*
 * Test snapshot hold/release and deferred destroy.
 */
void
ztest_dmu_snapshot_hold(ztest_ds_t *zd, uint64_t id)
{
	int error;
	objset_t *os = zd->zd_os;
	objset_t *origin;
	char snapname[100];
	char fullname[100];
	char clonename[100];
	char tag[100];
	char osname[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *holds;

	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	dmu_objset_name(os, osname);

	(void) snprintf(snapname, sizeof (snapname), "sh1_%"PRIu64"", id);
	(void) snprintf(fullname, sizeof (fullname), "%s@%s", osname, snapname);
	(void) snprintf(clonename, sizeof (clonename), "%s/ch1_%"PRIu64"",
	    osname, id);
	(void) snprintf(tag, sizeof (tag), "tag_%"PRIu64"", id);

	/*
	 * Clean up from any previous run.
	 */
	error = dsl_destroy_head(clonename);
	if (error != ENOENT)
		ASSERT0(error);
	error = user_release_one(fullname, tag);
	if (error != ESRCH && error != ENOENT)
		ASSERT0(error);
	error = dsl_destroy_snapshot(fullname, B_FALSE);
	if (error != ENOENT)
		ASSERT0(error);

	/*
	 * Create snapshot, clone it, mark snap for deferred destroy,
	 * destroy clone, verify snap was also destroyed.
	 */
	error = dmu_objset_snapshot_one(osname, snapname);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_snapshot");
			goto out;
		}
		fatal(B_FALSE, "dmu_objset_snapshot(%s) = %d", fullname, error);
	}

	error = dmu_objset_clone(clonename, fullname);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_clone");
			goto out;
		}
		fatal(B_FALSE, "dmu_objset_clone(%s) = %d", clonename, error);
	}

	error = dsl_destroy_snapshot(fullname, B_TRUE);
	if (error) {
		fatal(B_FALSE, "dsl_destroy_snapshot(%s, B_TRUE) = %d",
		    fullname, error);
	}

	error = dsl_destroy_head(clonename);
	if (error)
		fatal(B_FALSE, "dsl_destroy_head(%s) = %d", clonename, error);

	error = dmu_objset_hold(fullname, FTAG, &origin);
	if (error != ENOENT)
		fatal(B_FALSE, "dmu_objset_hold(%s) = %d", fullname, error);

	/*
	 * Create snapshot, add temporary hold, verify that we can't
	 * destroy a held snapshot, mark for deferred destroy,
	 * release hold, verify snapshot was destroyed.
	 */
	error = dmu_objset_snapshot_one(osname, snapname);
	if (error) {
		if (error == ENOSPC) {
			ztest_record_enospc("dmu_objset_snapshot");
			goto out;
		}
		fatal(B_FALSE, "dmu_objset_snapshot(%s) = %d", fullname, error);
	}

	holds = fnvlist_alloc();
	fnvlist_add_string(holds, fullname, tag);
	error = dsl_dataset_user_hold(holds, 0, NULL);
	fnvlist_free(holds);

	if (error == ENOSPC) {
		ztest_record_enospc("dsl_dataset_user_hold");
		goto out;
	} else if (error) {
		fatal(B_FALSE, "dsl_dataset_user_hold(%s, %s) = %u",
		    fullname, tag, error);
	}

	error = dsl_destroy_snapshot(fullname, B_FALSE);
	if (error != EBUSY) {
		fatal(B_FALSE, "dsl_destroy_snapshot(%s, B_FALSE) = %d",
		    fullname, error);
	}

	error = dsl_destroy_snapshot(fullname, B_TRUE);
	if (error) {
		fatal(B_FALSE, "dsl_destroy_snapshot(%s, B_TRUE) = %d",
		    fullname, error);
	}

	error = user_release_one(fullname, tag);
	if (error)
		fatal(B_FALSE, "user_release_one(%s, %s) = %d",
		    fullname, tag, error);

	VERIFY3U(dmu_objset_hold(fullname, FTAG, &origin), ==, ENOENT);

out:
	(void) pthread_rwlock_unlock(&ztest_name_lock);
}

/*
 * Inject random faults into the on-disk data.
 */
void
ztest_fault_inject(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	ztest_shared_t *zs = ztest_shared;
	spa_t *spa = ztest_spa;
	int fd;
	uint64_t offset;
	uint64_t leaves;
	uint64_t bad = 0x1990c0ffeedecadeull;
	uint64_t top, leaf;
	uint64_t raidz_children;
	char *path0;
	char *pathrand;
	size_t fsize;
	int bshift = SPA_MAXBLOCKSHIFT + 2;
	int iters = 1000;
	int maxfaults;
	int mirror_save;
	vdev_t *vd0 = NULL;
	uint64_t guid0 = 0;
	boolean_t islog = B_FALSE;
	boolean_t injected = B_FALSE;

	path0 = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
	pathrand = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);

	mutex_enter(&ztest_vdev_lock);

	/*
	 * Device removal is in progress, fault injection must be disabled
	 * until it completes and the pool is scrubbed.  The fault injection
	 * strategy for damaging blocks does not take in to account evacuated
	 * blocks which may have already been damaged.
	 */
	if (ztest_device_removal_active)
		goto out;

	/*
	 * The fault injection strategy for damaging blocks cannot be used
	 * if raidz expansion is in progress. The leaves value
	 * (attached raidz children) is variable and strategy for damaging
	 * blocks will corrupt same data blocks on different child vdevs
	 * because of the reflow process.
	 */
	if (spa->spa_raidz_expand != NULL)
		goto out;

	maxfaults = MAXFAULTS(zs);
	raidz_children = ztest_get_raidz_children(spa);
	leaves = MAX(zs->zs_mirrors, 1) * raidz_children;
	mirror_save = zs->zs_mirrors;

	ASSERT3U(leaves, >=, 1);

	/*
	 * While ztest is running the number of leaves will not change.  This
	 * is critical for the fault injection logic as it determines where
	 * errors can be safely injected such that they are always repairable.
	 *
	 * When restarting ztest a different number of leaves may be requested
	 * which will shift the regions to be damaged.  This is fine as long
	 * as the pool has been scrubbed prior to using the new mapping.
	 * Failure to do can result in non-repairable damage being injected.
	 */
	if (ztest_pool_scrubbed == B_FALSE)
		goto out;

	/*
	 * Grab the name lock as reader. There are some operations
	 * which don't like to have their vdevs changed while
	 * they are in progress (i.e. spa_change_guid). Those
	 * operations will have grabbed the name lock as writer.
	 */
	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	/*
	 * We need SCL_STATE here because we're going to look at vd0->vdev_tsd.
	 */
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	if (ztest_random(2) == 0) {
		/*
		 * Inject errors on a normal data device or slog device.
		 */
		top = ztest_random_vdev_top(spa, B_TRUE);
		leaf = ztest_random(leaves) + zs->zs_splits;

		/*
		 * Generate paths to the first leaf in this top-level vdev,
		 * and to the random leaf we selected.  We'll induce transient
		 * write failures and random online/offline activity on leaf 0,
		 * and we'll write random garbage to the randomly chosen leaf.
		 */
		(void) snprintf(path0, MAXPATHLEN, ztest_dev_template,
		    ztest_opts.zo_dir, ztest_opts.zo_pool,
		    top * leaves + zs->zs_splits);
		(void) snprintf(pathrand, MAXPATHLEN, ztest_dev_template,
		    ztest_opts.zo_dir, ztest_opts.zo_pool,
		    top * leaves + leaf);

		vd0 = vdev_lookup_by_path(spa->spa_root_vdev, path0);
		if (vd0 != NULL && vd0->vdev_top->vdev_islog)
			islog = B_TRUE;

		/*
		 * If the top-level vdev needs to be resilvered
		 * then we only allow faults on the device that is
		 * resilvering.
		 */
		if (vd0 != NULL && maxfaults != 1 &&
		    (!vdev_resilver_needed(vd0->vdev_top, NULL, NULL) ||
		    vd0->vdev_resilver_txg != 0)) {
			/*
			 * Make vd0 explicitly claim to be unreadable,
			 * or unwritable, or reach behind its back
			 * and close the underlying fd.  We can do this if
			 * maxfaults == 0 because we'll fail and reexecute,
			 * and we can do it if maxfaults >= 2 because we'll
			 * have enough redundancy.  If maxfaults == 1, the
			 * combination of this with injection of random data
			 * corruption below exceeds the pool's fault tolerance.
			 */
			vdev_file_t *vf = vd0->vdev_tsd;

			zfs_dbgmsg("injecting fault to vdev %llu; maxfaults=%d",
			    (long long)vd0->vdev_id, (int)maxfaults);

			if (vf != NULL && ztest_random(3) == 0) {
				(void) close(vf->vf_file->f_fd);
				vf->vf_file->f_fd = -1;
			} else if (ztest_random(2) == 0) {
				vd0->vdev_cant_read = B_TRUE;
			} else {
				vd0->vdev_cant_write = B_TRUE;
			}
			guid0 = vd0->vdev_guid;
		}
	} else {
		/*
		 * Inject errors on an l2cache device.
		 */
		spa_aux_vdev_t *sav = &spa->spa_l2cache;

		if (sav->sav_count == 0) {
			spa_config_exit(spa, SCL_STATE, FTAG);
			(void) pthread_rwlock_unlock(&ztest_name_lock);
			goto out;
		}
		vd0 = sav->sav_vdevs[ztest_random(sav->sav_count)];
		guid0 = vd0->vdev_guid;
		(void) strlcpy(path0, vd0->vdev_path, MAXPATHLEN);
		(void) strlcpy(pathrand, vd0->vdev_path, MAXPATHLEN);

		leaf = 0;
		leaves = 1;
		maxfaults = INT_MAX;	/* no limit on cache devices */
	}

	spa_config_exit(spa, SCL_STATE, FTAG);
	(void) pthread_rwlock_unlock(&ztest_name_lock);

	/*
	 * If we can tolerate two or more faults, or we're dealing
	 * with a slog, randomly online/offline vd0.
	 */
	if ((maxfaults >= 2 || islog) && guid0 != 0) {
		if (ztest_random(10) < 6) {
			int flags = (ztest_random(2) == 0 ?
			    ZFS_OFFLINE_TEMPORARY : 0);

			/*
			 * We have to grab the zs_name_lock as writer to
			 * prevent a race between offlining a slog and
			 * destroying a dataset. Offlining the slog will
			 * grab a reference on the dataset which may cause
			 * dsl_destroy_head() to fail with EBUSY thus
			 * leaving the dataset in an inconsistent state.
			 */
			if (islog)
				(void) pthread_rwlock_wrlock(&ztest_name_lock);

			VERIFY3U(vdev_offline(spa, guid0, flags), !=, EBUSY);

			if (islog)
				(void) pthread_rwlock_unlock(&ztest_name_lock);
		} else {
			/*
			 * Ideally we would like to be able to randomly
			 * call vdev_[on|off]line without holding locks
			 * to force unpredictable failures but the side
			 * effects of vdev_[on|off]line prevent us from
			 * doing so.
			 */
			(void) vdev_online(spa, guid0, 0, NULL);
		}
	}

	if (maxfaults == 0)
		goto out;

	/*
	 * We have at least single-fault tolerance, so inject data corruption.
	 */
	fd = open(pathrand, O_RDWR);

	if (fd == -1) /* we hit a gap in the device namespace */
		goto out;

	fsize = lseek(fd, 0, SEEK_END);

	while (--iters != 0) {
		/*
		 * The offset must be chosen carefully to ensure that
		 * we do not inject a given logical block with errors
		 * on two different leaf devices, because ZFS can not
		 * tolerate that (if maxfaults==1).
		 *
		 * To achieve this we divide each leaf device into
		 * chunks of size (# leaves * SPA_MAXBLOCKSIZE * 4).
		 * Each chunk is further divided into error-injection
		 * ranges (can accept errors) and clear ranges (we do
		 * not inject errors in those). Each error-injection
		 * range can accept errors only for a single leaf vdev.
		 * Error-injection ranges are separated by clear ranges.
		 *
		 * For example, with 3 leaves, each chunk looks like:
		 *    0 to  32M: injection range for leaf 0
		 *  32M to  64M: clear range - no injection allowed
		 *  64M to  96M: injection range for leaf 1
		 *  96M to 128M: clear range - no injection allowed
		 * 128M to 160M: injection range for leaf 2
		 * 160M to 192M: clear range - no injection allowed
		 *
		 * Each clear range must be large enough such that a
		 * single block cannot straddle it. This way a block
		 * can't be a target in two different injection ranges
		 * (on different leaf vdevs).
		 */
		offset = ztest_random(fsize / (leaves << bshift)) *
		    (leaves << bshift) + (leaf << bshift) +
		    (ztest_random(1ULL << (bshift - 1)) & -8ULL);

		/*
		 * Only allow damage to the labels at one end of the vdev.
		 *
		 * If all labels are damaged, the device will be totally
		 * inaccessible, which will result in loss of data,
		 * because we also damage (parts of) the other side of
		 * the mirror/raidz.
		 *
		 * Additionally, we will always have both an even and an
		 * odd label, so that we can handle crashes in the
		 * middle of vdev_config_sync().
		 */
		if ((leaf & 1) == 0 && offset < VDEV_LABEL_START_SIZE)
			continue;

		/*
		 * The two end labels are stored at the "end" of the disk, but
		 * the end of the disk (vdev_psize) is aligned to
		 * sizeof (vdev_label_t).
		 */
		uint64_t psize = P2ALIGN(fsize, sizeof (vdev_label_t));
		if ((leaf & 1) == 1 &&
		    offset + sizeof (bad) > psize - VDEV_LABEL_END_SIZE)
			continue;

		if (mirror_save != zs->zs_mirrors) {
			(void) close(fd);
			goto out;
		}

		if (pwrite(fd, &bad, sizeof (bad), offset) != sizeof (bad))
			fatal(B_TRUE,
			    "can't inject bad word at 0x%"PRIx64" in %s",
			    offset, pathrand);

		if (ztest_opts.zo_verbose >= 7)
			(void) printf("injected bad word into %s,"
			    " offset 0x%"PRIx64"\n", pathrand, offset);

		injected = B_TRUE;
	}

	(void) close(fd);
out:
	mutex_exit(&ztest_vdev_lock);

	if (injected && ztest_opts.zo_raid_do_expand) {
		int error = spa_scan(spa, POOL_SCAN_SCRUB);
		if (error == 0) {
			while (dsl_scan_scrubbing(spa_get_dsl(spa)))
				txg_wait_synced(spa_get_dsl(spa), 0);
		}
	}

	umem_free(path0, MAXPATHLEN);
	umem_free(pathrand, MAXPATHLEN);
}

/*
 * By design ztest will never inject uncorrectable damage in to the pool.
 * Issue a scrub, wait for it to complete, and verify there is never any
 * persistent damage.
 *
 * Only after a full scrub has been completed is it safe to start injecting
 * data corruption.  See the comment in zfs_fault_inject().
 */
static int
ztest_scrub_impl(spa_t *spa)
{
	int error = spa_scan(spa, POOL_SCAN_SCRUB);
	if (error)
		return (error);

	while (dsl_scan_scrubbing(spa_get_dsl(spa)))
		txg_wait_synced(spa_get_dsl(spa), 0);

	if (spa_approx_errlog_size(spa) > 0)
		return (ECKSUM);

	ztest_pool_scrubbed = B_TRUE;

	return (0);
}

/*
 * Scrub the pool.
 */
void
ztest_scrub(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;
	int error;

	/*
	 * Scrub in progress by device removal.
	 */
	if (ztest_device_removal_active)
		return;

	/*
	 * Start a scrub, wait a moment, then force a restart.
	 */
	(void) spa_scan(spa, POOL_SCAN_SCRUB);
	(void) poll(NULL, 0, 100);

	error = ztest_scrub_impl(spa);
	if (error == EBUSY)
		error = 0;
	ASSERT0(error);
}

/*
 * Change the guid for the pool.
 */
void
ztest_reguid(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;
	uint64_t orig, load;
	int error;
	ztest_shared_t *zs = ztest_shared;

	if (ztest_opts.zo_mmp_test)
		return;

	orig = spa_guid(spa);
	load = spa_load_guid(spa);

	(void) pthread_rwlock_wrlock(&ztest_name_lock);
	error = spa_change_guid(spa);
	zs->zs_guid = spa_guid(spa);
	(void) pthread_rwlock_unlock(&ztest_name_lock);

	if (error != 0)
		return;

	if (ztest_opts.zo_verbose >= 4) {
		(void) printf("Changed guid old %"PRIu64" -> %"PRIu64"\n",
		    orig, spa_guid(spa));
	}

	VERIFY3U(orig, !=, spa_guid(spa));
	VERIFY3U(load, ==, spa_load_guid(spa));
}

void
ztest_blake3(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	hrtime_t end = gethrtime() + NANOSEC;
	zio_cksum_salt_t salt;
	void *salt_ptr = &salt.zcs_bytes;
	struct abd *abd_data, *abd_meta;
	void *buf, *templ;
	int i, *ptr;
	uint32_t size;
	BLAKE3_CTX ctx;
	const zfs_impl_t *blake3 = zfs_impl_get_ops("blake3");

	size = ztest_random_blocksize();
	buf = umem_alloc(size, UMEM_NOFAIL);
	abd_data = abd_alloc(size, B_FALSE);
	abd_meta = abd_alloc(size, B_TRUE);

	for (i = 0, ptr = buf; i < size / sizeof (*ptr); i++, ptr++)
		*ptr = ztest_random(UINT_MAX);
	memset(salt_ptr, 'A', 32);

	abd_copy_from_buf_off(abd_data, buf, 0, size);
	abd_copy_from_buf_off(abd_meta, buf, 0, size);

	while (gethrtime() <= end) {
		int run_count = 100;
		zio_cksum_t zc_ref1, zc_ref2;
		zio_cksum_t zc_res1, zc_res2;

		void *ref1 = &zc_ref1;
		void *ref2 = &zc_ref2;
		void *res1 = &zc_res1;
		void *res2 = &zc_res2;

		/* BLAKE3_KEY_LEN = 32 */
		VERIFY0(blake3->setname("generic"));
		templ = abd_checksum_blake3_tmpl_init(&salt);
		Blake3_InitKeyed(&ctx, salt_ptr);
		Blake3_Update(&ctx, buf, size);
		Blake3_Final(&ctx, ref1);
		zc_ref2 = zc_ref1;
		ZIO_CHECKSUM_BSWAP(&zc_ref2);
		abd_checksum_blake3_tmpl_free(templ);

		VERIFY0(blake3->setname("cycle"));
		while (run_count-- > 0) {

			/* Test current implementation */
			Blake3_InitKeyed(&ctx, salt_ptr);
			Blake3_Update(&ctx, buf, size);
			Blake3_Final(&ctx, res1);
			zc_res2 = zc_res1;
			ZIO_CHECKSUM_BSWAP(&zc_res2);

			VERIFY0(memcmp(ref1, res1, 32));
			VERIFY0(memcmp(ref2, res2, 32));

			/* Test ABD - data */
			templ = abd_checksum_blake3_tmpl_init(&salt);
			abd_checksum_blake3_native(abd_data, size,
			    templ, &zc_res1);
			abd_checksum_blake3_byteswap(abd_data, size,
			    templ, &zc_res2);

			VERIFY0(memcmp(ref1, res1, 32));
			VERIFY0(memcmp(ref2, res2, 32));

			/* Test ABD - metadata */
			abd_checksum_blake3_native(abd_meta, size,
			    templ, &zc_res1);
			abd_checksum_blake3_byteswap(abd_meta, size,
			    templ, &zc_res2);
			abd_checksum_blake3_tmpl_free(templ);

			VERIFY0(memcmp(ref1, res1, 32));
			VERIFY0(memcmp(ref2, res2, 32));

		}
	}

	abd_free(abd_data);
	abd_free(abd_meta);
	umem_free(buf, size);
}

void
ztest_fletcher(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	hrtime_t end = gethrtime() + NANOSEC;

	while (gethrtime() <= end) {
		int run_count = 100;
		void *buf;
		struct abd *abd_data, *abd_meta;
		uint32_t size;
		int *ptr;
		int i;
		zio_cksum_t zc_ref;
		zio_cksum_t zc_ref_byteswap;

		size = ztest_random_blocksize();

		buf = umem_alloc(size, UMEM_NOFAIL);
		abd_data = abd_alloc(size, B_FALSE);
		abd_meta = abd_alloc(size, B_TRUE);

		for (i = 0, ptr = buf; i < size / sizeof (*ptr); i++, ptr++)
			*ptr = ztest_random(UINT_MAX);

		abd_copy_from_buf_off(abd_data, buf, 0, size);
		abd_copy_from_buf_off(abd_meta, buf, 0, size);

		VERIFY0(fletcher_4_impl_set("scalar"));
		fletcher_4_native(buf, size, NULL, &zc_ref);
		fletcher_4_byteswap(buf, size, NULL, &zc_ref_byteswap);

		VERIFY0(fletcher_4_impl_set("cycle"));
		while (run_count-- > 0) {
			zio_cksum_t zc;
			zio_cksum_t zc_byteswap;

			fletcher_4_byteswap(buf, size, NULL, &zc_byteswap);
			fletcher_4_native(buf, size, NULL, &zc);

			VERIFY0(memcmp(&zc, &zc_ref, sizeof (zc)));
			VERIFY0(memcmp(&zc_byteswap, &zc_ref_byteswap,
			    sizeof (zc_byteswap)));

			/* Test ABD - data */
			abd_fletcher_4_byteswap(abd_data, size, NULL,
			    &zc_byteswap);
			abd_fletcher_4_native(abd_data, size, NULL, &zc);

			VERIFY0(memcmp(&zc, &zc_ref, sizeof (zc)));
			VERIFY0(memcmp(&zc_byteswap, &zc_ref_byteswap,
			    sizeof (zc_byteswap)));

			/* Test ABD - metadata */
			abd_fletcher_4_byteswap(abd_meta, size, NULL,
			    &zc_byteswap);
			abd_fletcher_4_native(abd_meta, size, NULL, &zc);

			VERIFY0(memcmp(&zc, &zc_ref, sizeof (zc)));
			VERIFY0(memcmp(&zc_byteswap, &zc_ref_byteswap,
			    sizeof (zc_byteswap)));

		}

		umem_free(buf, size);
		abd_free(abd_data);
		abd_free(abd_meta);
	}
}

void
ztest_fletcher_incr(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	void *buf;
	size_t size;
	int *ptr;
	int i;
	zio_cksum_t zc_ref;
	zio_cksum_t zc_ref_bswap;

	hrtime_t end = gethrtime() + NANOSEC;

	while (gethrtime() <= end) {
		int run_count = 100;

		size = ztest_random_blocksize();
		buf = umem_alloc(size, UMEM_NOFAIL);

		for (i = 0, ptr = buf; i < size / sizeof (*ptr); i++, ptr++)
			*ptr = ztest_random(UINT_MAX);

		VERIFY0(fletcher_4_impl_set("scalar"));
		fletcher_4_native(buf, size, NULL, &zc_ref);
		fletcher_4_byteswap(buf, size, NULL, &zc_ref_bswap);

		VERIFY0(fletcher_4_impl_set("cycle"));

		while (run_count-- > 0) {
			zio_cksum_t zc;
			zio_cksum_t zc_bswap;
			size_t pos = 0;

			ZIO_SET_CHECKSUM(&zc, 0, 0, 0, 0);
			ZIO_SET_CHECKSUM(&zc_bswap, 0, 0, 0, 0);

			while (pos < size) {
				size_t inc = 64 * ztest_random(size / 67);
				/* sometimes add few bytes to test non-simd */
				if (ztest_random(100) < 10)
					inc += P2ALIGN(ztest_random(64),
					    sizeof (uint32_t));

				if (inc > (size - pos))
					inc = size - pos;

				fletcher_4_incremental_native(buf + pos, inc,
				    &zc);
				fletcher_4_incremental_byteswap(buf + pos, inc,
				    &zc_bswap);

				pos += inc;
			}

			VERIFY3U(pos, ==, size);

			VERIFY(ZIO_CHECKSUM_EQUAL(zc, zc_ref));
			VERIFY(ZIO_CHECKSUM_EQUAL(zc_bswap, zc_ref_bswap));

			/*
			 * verify if incremental on the whole buffer is
			 * equivalent to non-incremental version
			 */
			ZIO_SET_CHECKSUM(&zc, 0, 0, 0, 0);
			ZIO_SET_CHECKSUM(&zc_bswap, 0, 0, 0, 0);

			fletcher_4_incremental_native(buf, size, &zc);
			fletcher_4_incremental_byteswap(buf, size, &zc_bswap);

			VERIFY(ZIO_CHECKSUM_EQUAL(zc, zc_ref));
			VERIFY(ZIO_CHECKSUM_EQUAL(zc_bswap, zc_ref_bswap));
		}

		umem_free(buf, size);
	}
}

static int
ztest_set_global_vars(void)
{
	for (size_t i = 0; i < ztest_opts.zo_gvars_count; i++) {
		char *kv = ztest_opts.zo_gvars[i];
		VERIFY3U(strlen(kv), <=, ZO_GVARS_MAX_ARGLEN);
		VERIFY3U(strlen(kv), >, 0);
		int err = set_global_var(kv);
		if (ztest_opts.zo_verbose > 0) {
			(void) printf("setting global var %s ... %s\n", kv,
			    err ? "failed" : "ok");
		}
		if (err != 0) {
			(void) fprintf(stderr,
			    "failed to set global var '%s'\n", kv);
			return (err);
		}
	}
	return (0);
}

static char **
ztest_global_vars_to_zdb_args(void)
{
	char **args = calloc(2*ztest_opts.zo_gvars_count + 1, sizeof (char *));
	char **cur = args;
	if (args == NULL)
		return (NULL);
	for (size_t i = 0; i < ztest_opts.zo_gvars_count; i++) {
		*cur++ = (char *)"-o";
		*cur++ = ztest_opts.zo_gvars[i];
	}
	ASSERT3P(cur, ==, &args[2*ztest_opts.zo_gvars_count]);
	*cur = NULL;
	return (args);
}

/* The end of strings is indicated by a NULL element */
static char *
join_strings(char **strings, const char *sep)
{
	size_t totallen = 0;
	for (char **sp = strings; *sp != NULL; sp++) {
		totallen += strlen(*sp);
		totallen += strlen(sep);
	}
	if (totallen > 0) {
		ASSERT(totallen >= strlen(sep));
		totallen -= strlen(sep);
	}

	size_t buflen = totallen + 1;
	char *o = umem_alloc(buflen, UMEM_NOFAIL); /* trailing 0 byte */
	o[0] = '\0';
	for (char **sp = strings; *sp != NULL; sp++) {
		size_t would;
		would = strlcat(o, *sp, buflen);
		VERIFY3U(would, <, buflen);
		if (*(sp+1) == NULL) {
			break;
		}
		would = strlcat(o, sep, buflen);
		VERIFY3U(would, <, buflen);
	}
	ASSERT3S(strlen(o), ==, totallen);
	return (o);
}

static int
ztest_check_path(char *path)
{
	struct stat s;
	/* return true on success */
	return (!stat(path, &s));
}

static void
ztest_get_zdb_bin(char *bin, int len)
{
	char *zdb_path;
	/*
	 * Try to use $ZDB and in-tree zdb path. If not successful, just
	 * let popen to search through PATH.
	 */
	if ((zdb_path = getenv("ZDB"))) {
		strlcpy(bin, zdb_path, len); /* In env */
		if (!ztest_check_path(bin)) {
			ztest_dump_core = 0;
			fatal(B_TRUE, "invalid ZDB '%s'", bin);
		}
		return;
	}

	VERIFY3P(realpath(getexecname(), bin), !=, NULL);
	if (strstr(bin, ".libs/ztest")) {
		strstr(bin, ".libs/ztest")[0] = '\0'; /* In-tree */
		strcat(bin, "zdb");
		if (ztest_check_path(bin))
			return;
	}
	strcpy(bin, "zdb");
}

static vdev_t *
ztest_random_concrete_vdev_leaf(vdev_t *vd)
{
	if (vd == NULL)
		return (NULL);

	if (vd->vdev_children == 0)
		return (vd);

	vdev_t *eligible[vd->vdev_children];
	int eligible_idx = 0, i;
	for (i = 0; i < vd->vdev_children; i++) {
		vdev_t *cvd = vd->vdev_child[i];
		if (cvd->vdev_top->vdev_removing)
			continue;
		if (cvd->vdev_children > 0 ||
		    (vdev_is_concrete(cvd) && !cvd->vdev_detached)) {
			eligible[eligible_idx++] = cvd;
		}
	}
	VERIFY3S(eligible_idx, >, 0);

	uint64_t child_no = ztest_random(eligible_idx);
	return (ztest_random_concrete_vdev_leaf(eligible[child_no]));
}

void
ztest_initialize(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;
	int error = 0;

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	/* Random leaf vdev */
	vdev_t *rand_vd = ztest_random_concrete_vdev_leaf(spa->spa_root_vdev);
	if (rand_vd == NULL) {
		spa_config_exit(spa, SCL_VDEV, FTAG);
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * The random vdev we've selected may change as soon as we
	 * drop the spa_config_lock. We create local copies of things
	 * we're interested in.
	 */
	uint64_t guid = rand_vd->vdev_guid;
	char *path = strdup(rand_vd->vdev_path);
	boolean_t active = rand_vd->vdev_initialize_thread != NULL;

	zfs_dbgmsg("vd %px, guid %llu", rand_vd, (u_longlong_t)guid);
	spa_config_exit(spa, SCL_VDEV, FTAG);

	uint64_t cmd = ztest_random(POOL_INITIALIZE_FUNCS);

	nvlist_t *vdev_guids = fnvlist_alloc();
	nvlist_t *vdev_errlist = fnvlist_alloc();
	fnvlist_add_uint64(vdev_guids, path, guid);
	error = spa_vdev_initialize(spa, vdev_guids, cmd, vdev_errlist);
	fnvlist_free(vdev_guids);
	fnvlist_free(vdev_errlist);

	switch (cmd) {
	case POOL_INITIALIZE_CANCEL:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Cancel initialize %s", path);
			if (!active)
				(void) printf(" failed (no initialize active)");
			(void) printf("\n");
		}
		break;
	case POOL_INITIALIZE_START:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Start initialize %s", path);
			if (active && error == 0)
				(void) printf(" failed (already active)");
			else if (error != 0)
				(void) printf(" failed (error %d)", error);
			(void) printf("\n");
		}
		break;
	case POOL_INITIALIZE_SUSPEND:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Suspend initialize %s", path);
			if (!active)
				(void) printf(" failed (no initialize active)");
			(void) printf("\n");
		}
		break;
	}
	free(path);
	mutex_exit(&ztest_vdev_lock);
}

void
ztest_trim(ztest_ds_t *zd, uint64_t id)
{
	(void) zd, (void) id;
	spa_t *spa = ztest_spa;
	int error = 0;

	mutex_enter(&ztest_vdev_lock);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	/* Random leaf vdev */
	vdev_t *rand_vd = ztest_random_concrete_vdev_leaf(spa->spa_root_vdev);
	if (rand_vd == NULL) {
		spa_config_exit(spa, SCL_VDEV, FTAG);
		mutex_exit(&ztest_vdev_lock);
		return;
	}

	/*
	 * The random vdev we've selected may change as soon as we
	 * drop the spa_config_lock. We create local copies of things
	 * we're interested in.
	 */
	uint64_t guid = rand_vd->vdev_guid;
	char *path = strdup(rand_vd->vdev_path);
	boolean_t active = rand_vd->vdev_trim_thread != NULL;

	zfs_dbgmsg("vd %p, guid %llu", rand_vd, (u_longlong_t)guid);
	spa_config_exit(spa, SCL_VDEV, FTAG);

	uint64_t cmd = ztest_random(POOL_TRIM_FUNCS);
	uint64_t rate = 1 << ztest_random(30);
	boolean_t partial = (ztest_random(5) > 0);
	boolean_t secure = (ztest_random(5) > 0);

	nvlist_t *vdev_guids = fnvlist_alloc();
	nvlist_t *vdev_errlist = fnvlist_alloc();
	fnvlist_add_uint64(vdev_guids, path, guid);
	error = spa_vdev_trim(spa, vdev_guids, cmd, rate, partial,
	    secure, vdev_errlist);
	fnvlist_free(vdev_guids);
	fnvlist_free(vdev_errlist);

	switch (cmd) {
	case POOL_TRIM_CANCEL:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Cancel TRIM %s", path);
			if (!active)
				(void) printf(" failed (no TRIM active)");
			(void) printf("\n");
		}
		break;
	case POOL_TRIM_START:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Start TRIM %s", path);
			if (active && error == 0)
				(void) printf(" failed (already active)");
			else if (error != 0)
				(void) printf(" failed (error %d)", error);
			(void) printf("\n");
		}
		break;
	case POOL_TRIM_SUSPEND:
		if (ztest_opts.zo_verbose >= 4) {
			(void) printf("Suspend TRIM %s", path);
			if (!active)
				(void) printf(" failed (no TRIM active)");
			(void) printf("\n");
		}
		break;
	}
	free(path);
	mutex_exit(&ztest_vdev_lock);
}

/*
 * Verify pool integrity by running zdb.
 */
static void
ztest_run_zdb(uint64_t guid)
{
	int status;
	char *bin;
	char *zdb;
	char *zbuf;
	const int len = MAXPATHLEN + MAXNAMELEN + 20;
	FILE *fp;

	bin = umem_alloc(len, UMEM_NOFAIL);
	zdb = umem_alloc(len, UMEM_NOFAIL);
	zbuf = umem_alloc(1024, UMEM_NOFAIL);

	ztest_get_zdb_bin(bin, len);

	char **set_gvars_args = ztest_global_vars_to_zdb_args();
	if (set_gvars_args == NULL) {
		fatal(B_FALSE, "Failed to allocate memory in "
		    "ztest_global_vars_to_zdb_args(). Cannot run zdb.\n");
	}
	char *set_gvars_args_joined = join_strings(set_gvars_args, " ");
	free(set_gvars_args);

	size_t would = snprintf(zdb, len,
	    "%s -bcc%s%s -G -d -Y -e -y %s -p %s %"PRIu64,
	    bin,
	    ztest_opts.zo_verbose >= 3 ? "s" : "",
	    ztest_opts.zo_verbose >= 4 ? "v" : "",
	    set_gvars_args_joined,
	    ztest_opts.zo_dir,
	    guid);
	ASSERT3U(would, <, len);

	umem_free(set_gvars_args_joined, strlen(set_gvars_args_joined) + 1);

	if (ztest_opts.zo_verbose >= 5)
		(void) printf("Executing %s\n", zdb);

	fp = popen(zdb, "r");

	while (fgets(zbuf, 1024, fp) != NULL)
		if (ztest_opts.zo_verbose >= 3)
			(void) printf("%s", zbuf);

	status = pclose(fp);

	if (status == 0)
		goto out;

	ztest_dump_core = 0;
	if (WIFEXITED(status))
		fatal(B_FALSE, "'%s' exit code %d", zdb, WEXITSTATUS(status));
	else
		fatal(B_FALSE, "'%s' died with signal %d",
		    zdb, WTERMSIG(status));
out:
	umem_free(bin, len);
	umem_free(zdb, len);
	umem_free(zbuf, 1024);
}

static void
ztest_walk_pool_directory(const char *header)
{
	spa_t *spa = NULL;

	if (ztest_opts.zo_verbose >= 6)
		(void) puts(header);

	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL)
		if (ztest_opts.zo_verbose >= 6)
			(void) printf("\t%s\n", spa_name(spa));
	mutex_exit(&spa_namespace_lock);
}

static void
ztest_spa_import_export(char *oldname, char *newname)
{
	nvlist_t *config, *newconfig;
	uint64_t pool_guid;
	spa_t *spa;
	int error;

	if (ztest_opts.zo_verbose >= 4) {
		(void) printf("import/export: old = %s, new = %s\n",
		    oldname, newname);
	}

	/*
	 * Clean up from previous runs.
	 */
	(void) spa_destroy(newname);

	/*
	 * Get the pool's configuration and guid.
	 */
	VERIFY0(spa_open(oldname, &spa, FTAG));

	/*
	 * Kick off a scrub to tickle scrub/export races.
	 */
	if (ztest_random(2) == 0)
		(void) spa_scan(spa, POOL_SCAN_SCRUB);

	pool_guid = spa_guid(spa);
	spa_close(spa, FTAG);

	ztest_walk_pool_directory("pools before export");

	/*
	 * Export it.
	 */
	VERIFY0(spa_export(oldname, &config, B_FALSE, B_FALSE));

	ztest_walk_pool_directory("pools after export");

	/*
	 * Try to import it.
	 */
	newconfig = spa_tryimport(config);
	ASSERT3P(newconfig, !=, NULL);
	fnvlist_free(newconfig);

	/*
	 * Import it under the new name.
	 */
	error = spa_import(newname, config, NULL, 0);
	if (error != 0) {
		dump_nvlist(config, 0);
		fatal(B_FALSE, "couldn't import pool %s as %s: error %u",
		    oldname, newname, error);
	}

	ztest_walk_pool_directory("pools after import");

	/*
	 * Try to import it again -- should fail with EEXIST.
	 */
	VERIFY3U(EEXIST, ==, spa_import(newname, config, NULL, 0));

	/*
	 * Try to import it under a different name -- should fail with EEXIST.
	 */
	VERIFY3U(EEXIST, ==, spa_import(oldname, config, NULL, 0));

	/*
	 * Verify that the pool is no longer visible under the old name.
	 */
	VERIFY3U(ENOENT, ==, spa_open(oldname, &spa, FTAG));

	/*
	 * Verify that we can open and close the pool using the new name.
	 */
	VERIFY0(spa_open(newname, &spa, FTAG));
	ASSERT3U(pool_guid, ==, spa_guid(spa));
	spa_close(spa, FTAG);

	fnvlist_free(config);
}

static void
ztest_resume(spa_t *spa)
{
	if (spa_suspended(spa) && ztest_opts.zo_verbose >= 6)
		(void) printf("resuming from suspended state\n");
	spa_vdev_state_enter(spa, SCL_NONE);
	vdev_clear(spa, NULL);
	(void) spa_vdev_state_exit(spa, NULL, 0);
	(void) zio_resume(spa);
}

static __attribute__((noreturn)) void
ztest_resume_thread(void *arg)
{
	spa_t *spa = arg;

	while (!ztest_exiting) {
		if (spa_suspended(spa))
			ztest_resume(spa);
		(void) poll(NULL, 0, 100);

		/*
		 * Periodically change the zfs_compressed_arc_enabled setting.
		 */
		if (ztest_random(10) == 0)
			zfs_compressed_arc_enabled = ztest_random(2);

		/*
		 * Periodically change the zfs_abd_scatter_enabled setting.
		 */
		if (ztest_random(10) == 0)
			zfs_abd_scatter_enabled = ztest_random(2);
	}

	thread_exit();
}

static __attribute__((noreturn)) void
ztest_deadman_thread(void *arg)
{
	ztest_shared_t *zs = arg;
	spa_t *spa = ztest_spa;
	hrtime_t delay, overdue, last_run = gethrtime();

	delay = (zs->zs_thread_stop - zs->zs_thread_start) +
	    MSEC2NSEC(zfs_deadman_synctime_ms);

	while (!ztest_exiting) {
		/*
		 * Wait for the delay timer while checking occasionally
		 * if we should stop.
		 */
		if (gethrtime() < last_run + delay) {
			(void) poll(NULL, 0, 1000);
			continue;
		}

		/*
		 * If the pool is suspended then fail immediately. Otherwise,
		 * check to see if the pool is making any progress. If
		 * vdev_deadman() discovers that there hasn't been any recent
		 * I/Os then it will end up aborting the tests.
		 */
		if (spa_suspended(spa) || spa->spa_root_vdev == NULL) {
			fatal(B_FALSE,
			    "aborting test after %llu seconds because "
			    "pool has transitioned to a suspended state.",
			    (u_longlong_t)zfs_deadman_synctime_ms / 1000);
		}
		vdev_deadman(spa->spa_root_vdev, FTAG);

		/*
		 * If the process doesn't complete within a grace period of
		 * zfs_deadman_synctime_ms over the expected finish time,
		 * then it may be hung and is terminated.
		 */
		overdue = zs->zs_proc_stop + MSEC2NSEC(zfs_deadman_synctime_ms);
		if (gethrtime() > overdue) {
			fatal(B_FALSE,
			    "aborting test after %llu seconds because "
			    "the process is overdue for termination.",
			    (gethrtime() - zs->zs_proc_start) / NANOSEC);
		}

		(void) printf("ztest has been running for %lld seconds\n",
		    (gethrtime() - zs->zs_proc_start) / NANOSEC);

		last_run = gethrtime();
		delay = MSEC2NSEC(zfs_deadman_checktime_ms);
	}

	thread_exit();
}

static void
ztest_execute(int test, ztest_info_t *zi, uint64_t id)
{
	ztest_ds_t *zd = &ztest_ds[id % ztest_opts.zo_datasets];
	ztest_shared_callstate_t *zc = ZTEST_GET_SHARED_CALLSTATE(test);
	hrtime_t functime = gethrtime();
	int i;

	for (i = 0; i < zi->zi_iters; i++)
		zi->zi_func(zd, id);

	functime = gethrtime() - functime;

	atomic_add_64(&zc->zc_count, 1);
	atomic_add_64(&zc->zc_time, functime);

	if (ztest_opts.zo_verbose >= 4)
		(void) printf("%6.2f sec in %s\n",
		    (double)functime / NANOSEC, zi->zi_funcname);
}

typedef struct ztest_raidz_expand_io {
	uint64_t	rzx_id;
	uint64_t	rzx_amount;
	uint64_t	rzx_bufsize;
	const void	*rzx_buffer;
	uint64_t	rzx_alloc_max;
	spa_t		*rzx_spa;
} ztest_expand_io_t;

#undef OD_ARRAY_SIZE
#define	OD_ARRAY_SIZE	10

/*
 * Write a request amount of data to some dataset objects.
 * There will be ztest_opts.zo_threads count of these running in parallel.
 */
static __attribute__((noreturn)) void
ztest_rzx_thread(void *arg)
{
	ztest_expand_io_t *info = (ztest_expand_io_t *)arg;
	ztest_od_t *od;
	int batchsize;
	int od_size;
	ztest_ds_t *zd = &ztest_ds[info->rzx_id % ztest_opts.zo_datasets];
	spa_t *spa = info->rzx_spa;

	od_size = sizeof (ztest_od_t) * OD_ARRAY_SIZE;
	od = umem_alloc(od_size, UMEM_NOFAIL);
	batchsize = OD_ARRAY_SIZE;

	/* Create objects to write to */
	for (int b = 0; b < batchsize; b++) {
		ztest_od_init(od + b, info->rzx_id, FTAG, b,
		    DMU_OT_UINT64_OTHER, 0, 0, 0);
	}
	if (ztest_object_init(zd, od, od_size, B_FALSE) != 0) {
		umem_free(od, od_size);
		thread_exit();
	}

	for (uint64_t offset = 0, written = 0; written < info->rzx_amount;
	    offset += info->rzx_bufsize) {
		/* write to 10 objects */
		for (int i = 0; i < batchsize && written < info->rzx_amount;
		    i++) {
			(void) pthread_rwlock_rdlock(&zd->zd_zilog_lock);
			ztest_write(zd, od[i].od_object, offset,
			    info->rzx_bufsize, info->rzx_buffer);
			(void) pthread_rwlock_unlock(&zd->zd_zilog_lock);
			written += info->rzx_bufsize;
		}
		txg_wait_synced(spa_get_dsl(spa), 0);
		/* due to inflation, we'll typically bail here */
		if (metaslab_class_get_alloc(spa_normal_class(spa)) >
		    info->rzx_alloc_max) {
			break;
		}
	}

	/* Remove a few objects to leave some holes in allocation space */
	mutex_enter(&zd->zd_dirobj_lock);
	(void) ztest_remove(zd, od, 2);
	mutex_exit(&zd->zd_dirobj_lock);

	umem_free(od, od_size);

	thread_exit();
}

static __attribute__((noreturn)) void
ztest_thread(void *arg)
{
	int rand;
	uint64_t id = (uintptr_t)arg;
	ztest_shared_t *zs = ztest_shared;
	uint64_t call_next;
	hrtime_t now;
	ztest_info_t *zi;
	ztest_shared_callstate_t *zc;

	while ((now = gethrtime()) < zs->zs_thread_stop) {
		/*
		 * See if it's time to force a crash.
		 */
		if (now > zs->zs_thread_kill &&
		    raidz_expand_pause_point == RAIDZ_EXPAND_PAUSE_NONE) {
			ztest_kill(zs);
		}

		/*
		 * If we're getting ENOSPC with some regularity, stop.
		 */
		if (zs->zs_enospc_count > 10)
			break;

		/*
		 * Pick a random function to execute.
		 */
		rand = ztest_random(ZTEST_FUNCS);
		zi = &ztest_info[rand];
		zc = ZTEST_GET_SHARED_CALLSTATE(rand);
		call_next = zc->zc_next;

		if (now >= call_next &&
		    atomic_cas_64(&zc->zc_next, call_next, call_next +
		    ztest_random(2 * zi->zi_interval[0] + 1)) == call_next) {
			ztest_execute(rand, zi, id);
		}
	}

	thread_exit();
}

static void
ztest_dataset_name(char *dsname, const char *pool, int d)
{
	(void) snprintf(dsname, ZFS_MAX_DATASET_NAME_LEN, "%s/ds_%d", pool, d);
}

static void
ztest_dataset_destroy(int d)
{
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int t;

	ztest_dataset_name(name, ztest_opts.zo_pool, d);

	if (ztest_opts.zo_verbose >= 3)
		(void) printf("Destroying %s to free up space\n", name);

	/*
	 * Cleanup any non-standard clones and snapshots.  In general,
	 * ztest thread t operates on dataset (t % zopt_datasets),
	 * so there may be more than one thing to clean up.
	 */
	for (t = d; t < ztest_opts.zo_threads;
	    t += ztest_opts.zo_datasets)
		ztest_dsl_dataset_cleanup(name, t);

	(void) dmu_objset_find(name, ztest_objset_destroy_cb, NULL,
	    DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);
}

static void
ztest_dataset_dirobj_verify(ztest_ds_t *zd)
{
	uint64_t usedobjs, dirobjs, scratch;

	/*
	 * ZTEST_DIROBJ is the object directory for the entire dataset.
	 * Therefore, the number of objects in use should equal the
	 * number of ZTEST_DIROBJ entries, +1 for ZTEST_DIROBJ itself.
	 * If not, we have an object leak.
	 *
	 * Note that we can only check this in ztest_dataset_open(),
	 * when the open-context and syncing-context values agree.
	 * That's because zap_count() returns the open-context value,
	 * while dmu_objset_space() returns the rootbp fill count.
	 */
	VERIFY0(zap_count(zd->zd_os, ZTEST_DIROBJ, &dirobjs));
	dmu_objset_space(zd->zd_os, &scratch, &scratch, &usedobjs, &scratch);
	ASSERT3U(dirobjs + 1, ==, usedobjs);
}

static int
ztest_dataset_open(int d)
{
	ztest_ds_t *zd = &ztest_ds[d];
	uint64_t committed_seq = ZTEST_GET_SHARED_DS(d)->zd_seq;
	objset_t *os;
	zilog_t *zilog;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	ztest_dataset_name(name, ztest_opts.zo_pool, d);

	(void) pthread_rwlock_rdlock(&ztest_name_lock);

	error = ztest_dataset_create(name);
	if (error == ENOSPC) {
		(void) pthread_rwlock_unlock(&ztest_name_lock);
		ztest_record_enospc(FTAG);
		return (error);
	}
	ASSERT(error == 0 || error == EEXIST);

	VERIFY0(ztest_dmu_objset_own(name, DMU_OST_OTHER, B_FALSE,
	    B_TRUE, zd, &os));
	(void) pthread_rwlock_unlock(&ztest_name_lock);

	ztest_zd_init(zd, ZTEST_GET_SHARED_DS(d), os);

	zilog = zd->zd_zilog;

	if (zilog->zl_header->zh_claim_lr_seq != 0 &&
	    zilog->zl_header->zh_claim_lr_seq < committed_seq)
		fatal(B_FALSE, "missing log records: "
		    "claimed %"PRIu64" < committed %"PRIu64"",
		    zilog->zl_header->zh_claim_lr_seq, committed_seq);

	ztest_dataset_dirobj_verify(zd);

	zil_replay(os, zd, ztest_replay_vector);

	ztest_dataset_dirobj_verify(zd);

	if (ztest_opts.zo_verbose >= 6)
		(void) printf("%s replay %"PRIu64" blocks, "
		    "%"PRIu64" records, seq %"PRIu64"\n",
		    zd->zd_name,
		    zilog->zl_parse_blk_count,
		    zilog->zl_parse_lr_count,
		    zilog->zl_replaying_seq);

	zilog = zil_open(os, ztest_get_data, NULL);

	if (zilog->zl_replaying_seq != 0 &&
	    zilog->zl_replaying_seq < committed_seq)
		fatal(B_FALSE, "missing log records: "
		    "replayed %"PRIu64" < committed %"PRIu64"",
		    zilog->zl_replaying_seq, committed_seq);

	return (0);
}

static void
ztest_dataset_close(int d)
{
	ztest_ds_t *zd = &ztest_ds[d];

	zil_close(zd->zd_zilog);
	dmu_objset_disown(zd->zd_os, B_TRUE, zd);

	ztest_zd_fini(zd);
}

static int
ztest_replay_zil_cb(const char *name, void *arg)
{
	(void) arg;
	objset_t *os;
	ztest_ds_t *zdtmp;

	VERIFY0(ztest_dmu_objset_own(name, DMU_OST_ANY, B_TRUE,
	    B_TRUE, FTAG, &os));

	zdtmp = umem_alloc(sizeof (ztest_ds_t), UMEM_NOFAIL);

	ztest_zd_init(zdtmp, NULL, os);
	zil_replay(os, zdtmp, ztest_replay_vector);
	ztest_zd_fini(zdtmp);

	if (dmu_objset_zil(os)->zl_parse_lr_count != 0 &&
	    ztest_opts.zo_verbose >= 6) {
		zilog_t *zilog = dmu_objset_zil(os);

		(void) printf("%s replay %"PRIu64" blocks, "
		    "%"PRIu64" records, seq %"PRIu64"\n",
		    name,
		    zilog->zl_parse_blk_count,
		    zilog->zl_parse_lr_count,
		    zilog->zl_replaying_seq);
	}

	umem_free(zdtmp, sizeof (ztest_ds_t));

	dmu_objset_disown(os, B_TRUE, FTAG);
	return (0);
}

static void
ztest_freeze(void)
{
	ztest_ds_t *zd = &ztest_ds[0];
	spa_t *spa;
	int numloops = 0;

	/* freeze not supported during RAIDZ expansion */
	if (ztest_opts.zo_raid_do_expand)
		return;

	if (ztest_opts.zo_verbose >= 3)
		(void) printf("testing spa_freeze()...\n");

	raidz_scratch_verify();
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));
	VERIFY0(ztest_dataset_open(0));
	ztest_spa = spa;

	/*
	 * Force the first log block to be transactionally allocated.
	 * We have to do this before we freeze the pool -- otherwise
	 * the log chain won't be anchored.
	 */
	while (BP_IS_HOLE(&zd->zd_zilog->zl_header->zh_log)) {
		ztest_dmu_object_alloc_free(zd, 0);
		zil_commit(zd->zd_zilog, 0);
	}

	txg_wait_synced(spa_get_dsl(spa), 0);

	/*
	 * Freeze the pool.  This stops spa_sync() from doing anything,
	 * so that the only way to record changes from now on is the ZIL.
	 */
	spa_freeze(spa);

	/*
	 * Because it is hard to predict how much space a write will actually
	 * require beforehand, we leave ourselves some fudge space to write over
	 * capacity.
	 */
	uint64_t capacity = metaslab_class_get_space(spa_normal_class(spa)) / 2;

	/*
	 * Run tests that generate log records but don't alter the pool config
	 * or depend on DSL sync tasks (snapshots, objset create/destroy, etc).
	 * We do a txg_wait_synced() after each iteration to force the txg
	 * to increase well beyond the last synced value in the uberblock.
	 * The ZIL should be OK with that.
	 *
	 * Run a random number of times less than zo_maxloops and ensure we do
	 * not run out of space on the pool.
	 */
	while (ztest_random(10) != 0 &&
	    numloops++ < ztest_opts.zo_maxloops &&
	    metaslab_class_get_alloc(spa_normal_class(spa)) < capacity) {
		ztest_od_t od;
		ztest_od_init(&od, 0, FTAG, 0, DMU_OT_UINT64_OTHER, 0, 0, 0);
		VERIFY0(ztest_object_init(zd, &od, sizeof (od), B_FALSE));
		ztest_io(zd, od.od_object,
		    ztest_random(ZTEST_RANGE_LOCKS) << SPA_MAXBLOCKSHIFT);
		txg_wait_synced(spa_get_dsl(spa), 0);
	}

	/*
	 * Commit all of the changes we just generated.
	 */
	zil_commit(zd->zd_zilog, 0);
	txg_wait_synced(spa_get_dsl(spa), 0);

	/*
	 * Close our dataset and close the pool.
	 */
	ztest_dataset_close(0);
	spa_close(spa, FTAG);
	kernel_fini();

	/*
	 * Open and close the pool and dataset to induce log replay.
	 */
	raidz_scratch_verify();
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));
	ASSERT3U(spa_freeze_txg(spa), ==, UINT64_MAX);
	VERIFY0(ztest_dataset_open(0));
	ztest_spa = spa;
	txg_wait_synced(spa_get_dsl(spa), 0);
	ztest_dataset_close(0);
	ztest_reguid(NULL, 0);

	spa_close(spa, FTAG);
	kernel_fini();
}

static void
ztest_import_impl(void)
{
	importargs_t args = { 0 };
	nvlist_t *cfg = NULL;
	int nsearch = 1;
	char *searchdirs[nsearch];
	int flags = ZFS_IMPORT_MISSING_LOG;

	searchdirs[0] = ztest_opts.zo_dir;
	args.paths = nsearch;
	args.path = searchdirs;
	args.can_be_active = B_FALSE;

	libpc_handle_t lpch = {
		.lpc_lib_handle = NULL,
		.lpc_ops = &libzpool_config_ops,
		.lpc_printerr = B_TRUE
	};
	VERIFY0(zpool_find_config(&lpch, ztest_opts.zo_pool, &cfg, &args));
	VERIFY0(spa_import(ztest_opts.zo_pool, cfg, NULL, flags));
	fnvlist_free(cfg);
}

/*
 * Import a storage pool with the given name.
 */
static void
ztest_import(ztest_shared_t *zs)
{
	spa_t *spa;

	mutex_init(&ztest_vdev_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ztest_checkpoint_lock, NULL, MUTEX_DEFAULT, NULL);
	VERIFY0(pthread_rwlock_init(&ztest_name_lock, NULL));

	raidz_scratch_verify();
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	ztest_import_impl();

	VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));
	zs->zs_metaslab_sz =
	    1ULL << spa->spa_root_vdev->vdev_child[0]->vdev_ms_shift;
	zs->zs_guid = spa_guid(spa);
	spa_close(spa, FTAG);

	kernel_fini();

	if (!ztest_opts.zo_mmp_test) {
		ztest_run_zdb(zs->zs_guid);
		ztest_freeze();
		ztest_run_zdb(zs->zs_guid);
	}

	(void) pthread_rwlock_destroy(&ztest_name_lock);
	mutex_destroy(&ztest_vdev_lock);
	mutex_destroy(&ztest_checkpoint_lock);
}

/*
 * After the expansion was killed, check that the pool is healthy
 */
static void
ztest_raidz_expand_check(spa_t *spa)
{
	ASSERT3U(ztest_opts.zo_raidz_expand_test, ==, RAIDZ_EXPAND_KILLED);
	/*
	 * Set pool check done flag, main program will run a zdb check
	 * of the pool when we exit.
	 */
	ztest_shared_opts->zo_raidz_expand_test = RAIDZ_EXPAND_CHECKED;

	/* Wait for reflow to finish */
	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("\nwaiting for reflow to finish ...\n");
	}
	pool_raidz_expand_stat_t rzx_stats;
	pool_raidz_expand_stat_t *pres = &rzx_stats;
	do {
		txg_wait_synced(spa_get_dsl(spa), 0);
		(void) poll(NULL, 0, 500); /* wait 1/2 second */

		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		(void) spa_raidz_expand_get_stats(spa, pres);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	} while (pres->pres_state != DSS_FINISHED &&
	    pres->pres_reflowed < pres->pres_to_reflow);

	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("verifying an interrupted raidz "
		    "expansion using a pool scrub ...\n");
	}
	/* Will fail here if there is non-recoverable corruption detected */
	VERIFY0(ztest_scrub_impl(spa));
	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("raidz expansion scrub check complete\n");
	}
}

/*
 * Start a raidz expansion test.  We run some I/O on the pool for a while
 * to get some data in the pool.  Then we grow the raidz and
 * kill the test at the requested offset into the reflow, verifying that
 * doing such does not lead to pool corruption.
 */
static void
ztest_raidz_expand_run(ztest_shared_t *zs, spa_t *spa)
{
	nvlist_t *root;
	pool_raidz_expand_stat_t rzx_stats;
	pool_raidz_expand_stat_t *pres = &rzx_stats;
	kthread_t **run_threads;
	vdev_t *cvd, *rzvd = spa->spa_root_vdev->vdev_child[0];
	int total_disks = rzvd->vdev_children;
	int data_disks = total_disks - vdev_get_nparity(rzvd);
	uint64_t alloc_goal;
	uint64_t csize;
	int error, t;
	int threads = ztest_opts.zo_threads;
	ztest_expand_io_t *thread_args;

	ASSERT3U(ztest_opts.zo_raidz_expand_test, !=, RAIDZ_EXPAND_NONE);
	ASSERT3U(rzvd->vdev_ops, ==, &vdev_raidz_ops);
	ztest_opts.zo_raidz_expand_test = RAIDZ_EXPAND_STARTED;

	/* Setup a 1 MiB buffer of random data */
	uint64_t bufsize = 1024 * 1024;
	void *buffer = umem_alloc(bufsize, UMEM_NOFAIL);

	if (read(ztest_fd_rand, buffer, bufsize) != bufsize) {
		fatal(B_TRUE, "short read from /dev/urandom");
	}
	/*
	 * Put some data in the pool and then attach a vdev to initiate
	 * reflow.
	 */
	run_threads = umem_zalloc(threads * sizeof (kthread_t *), UMEM_NOFAIL);
	thread_args = umem_zalloc(threads * sizeof (ztest_expand_io_t),
	    UMEM_NOFAIL);
	/* Aim for roughly 25% of allocatable space up to 1GB */
	alloc_goal = (vdev_get_min_asize(rzvd) * data_disks) / total_disks;
	alloc_goal = MIN(alloc_goal >> 2, 1024*1024*1024);
	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("adding data to pool '%s', goal %llu bytes\n",
		    ztest_opts.zo_pool, (u_longlong_t)alloc_goal);
	}

	/*
	 * Kick off all the I/O generators that run in parallel.
	 */
	for (t = 0; t < threads; t++) {
		if (t < ztest_opts.zo_datasets && ztest_dataset_open(t) != 0) {
			umem_free(run_threads, threads * sizeof (kthread_t *));
			umem_free(buffer, bufsize);
			return;
		}
		thread_args[t].rzx_id = t;
		thread_args[t].rzx_amount = alloc_goal / threads;
		thread_args[t].rzx_bufsize = bufsize;
		thread_args[t].rzx_buffer = buffer;
		thread_args[t].rzx_alloc_max = alloc_goal;
		thread_args[t].rzx_spa = spa;
		run_threads[t] = thread_create(NULL, 0, ztest_rzx_thread,
		    &thread_args[t], 0, NULL, TS_RUN | TS_JOINABLE,
		    defclsyspri);
	}

	/*
	 * Wait for all of the writers to complete.
	 */
	for (t = 0; t < threads; t++)
		VERIFY0(thread_join(run_threads[t]));

	/*
	 * Close all datasets. This must be done after all the threads
	 * are joined so we can be sure none of the datasets are in-use
	 * by any of the threads.
	 */
	for (t = 0; t < ztest_opts.zo_threads; t++) {
		if (t < ztest_opts.zo_datasets)
			ztest_dataset_close(t);
	}

	txg_wait_synced(spa_get_dsl(spa), 0);

	zs->zs_alloc = metaslab_class_get_alloc(spa_normal_class(spa));
	zs->zs_space = metaslab_class_get_space(spa_normal_class(spa));

	umem_free(buffer, bufsize);
	umem_free(run_threads, threads * sizeof (kthread_t *));
	umem_free(thread_args, threads * sizeof (ztest_expand_io_t));

	/* Set our reflow target to 25%, 50% or 75% of allocated size */
	uint_t multiple = ztest_random(3) + 1;
	uint64_t reflow_max = (rzvd->vdev_stat.vs_alloc * multiple) / 4;
	raidz_expand_max_reflow_bytes = reflow_max;

	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("running raidz expansion test, killing when "
		    "reflow reaches %llu bytes (%u/4 of allocated space)\n",
		    (u_longlong_t)reflow_max, multiple);
	}

	/* XXX - do we want some I/O load during the reflow? */

	/*
	 * Use a disk size that is larger than existing ones
	 */
	cvd = rzvd->vdev_child[0];
	csize = vdev_get_min_asize(cvd);
	csize += csize / 10;
	/*
	 * Path to vdev to be attached
	 */
	char *newpath = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
	(void) snprintf(newpath, MAXPATHLEN, ztest_dev_template,
	    ztest_opts.zo_dir, ztest_opts.zo_pool, rzvd->vdev_children);
	/*
	 * Build the nvlist describing newpath.
	 */
	root = make_vdev_root(newpath, NULL, NULL, csize, ztest_get_ashift(),
	    NULL, 0, 0, 1);
	/*
	 * Expand the raidz vdev by attaching the new disk
	 */
	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("expanding raidz: %d wide to %d wide with '%s'\n",
		    (int)rzvd->vdev_children, (int)rzvd->vdev_children + 1,
		    newpath);
	}
	error = spa_vdev_attach(spa, rzvd->vdev_guid, root, B_FALSE, B_FALSE);
	nvlist_free(root);
	if (error != 0) {
		fatal(0, "raidz expand: attach (%s %llu) returned %d",
		    newpath, (long long)csize, error);
	}

	/*
	 * Wait for reflow to begin
	 */
	while (spa->spa_raidz_expand == NULL) {
		txg_wait_synced(spa_get_dsl(spa), 0);
		(void) poll(NULL, 0, 100); /* wait 1/10 second */
	}
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	(void) spa_raidz_expand_get_stats(spa, pres);
	spa_config_exit(spa, SCL_CONFIG, FTAG);
	while (pres->pres_state != DSS_SCANNING) {
		txg_wait_synced(spa_get_dsl(spa), 0);
		(void) poll(NULL, 0, 100); /* wait 1/10 second */
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		(void) spa_raidz_expand_get_stats(spa, pres);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	ASSERT3U(pres->pres_state, ==, DSS_SCANNING);
	ASSERT3U(pres->pres_to_reflow, !=, 0);
	/*
	 * Set so when we are killed we go to raidz checking rather than
	 * restarting test.
	 */
	ztest_shared_opts->zo_raidz_expand_test = RAIDZ_EXPAND_KILLED;
	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("raidz expansion reflow started, waiting for "
		    "%llu bytes to be copied\n", (u_longlong_t)reflow_max);
	}

	/*
	 * Wait for reflow maximum to be reached and then kill the test
	 */
	while (pres->pres_reflowed < reflow_max) {
		txg_wait_synced(spa_get_dsl(spa), 0);
		(void) poll(NULL, 0, 100); /* wait 1/10 second */
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		(void) spa_raidz_expand_get_stats(spa, pres);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	/* Reset the reflow pause before killing */
	raidz_expand_max_reflow_bytes = 0;

	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("killing raidz expansion test after reflow "
		    "reached %llu bytes\n", (u_longlong_t)pres->pres_reflowed);
	}

	/*
	 * Kill ourself to simulate a panic during a reflow.  Our parent will
	 * restart the test and the changed flag value will drive the test
	 * through the scrub/check code to verify the pool is not corrupted.
	 */
	ztest_kill(zs);
}

static void
ztest_generic_run(ztest_shared_t *zs, spa_t *spa)
{
	kthread_t **run_threads;
	int t;

	run_threads = umem_zalloc(ztest_opts.zo_threads * sizeof (kthread_t *),
	    UMEM_NOFAIL);

	/*
	 * Kick off all the tests that run in parallel.
	 */
	for (t = 0; t < ztest_opts.zo_threads; t++) {
		if (t < ztest_opts.zo_datasets && ztest_dataset_open(t) != 0) {
			umem_free(run_threads, ztest_opts.zo_threads *
			    sizeof (kthread_t *));
			return;
		}

		run_threads[t] = thread_create(NULL, 0, ztest_thread,
		    (void *)(uintptr_t)t, 0, NULL, TS_RUN | TS_JOINABLE,
		    defclsyspri);
	}

	/*
	 * Wait for all of the tests to complete.
	 */
	for (t = 0; t < ztest_opts.zo_threads; t++)
		VERIFY0(thread_join(run_threads[t]));

	/*
	 * Close all datasets. This must be done after all the threads
	 * are joined so we can be sure none of the datasets are in-use
	 * by any of the threads.
	 */
	for (t = 0; t < ztest_opts.zo_threads; t++) {
		if (t < ztest_opts.zo_datasets)
			ztest_dataset_close(t);
	}

	txg_wait_synced(spa_get_dsl(spa), 0);

	zs->zs_alloc = metaslab_class_get_alloc(spa_normal_class(spa));
	zs->zs_space = metaslab_class_get_space(spa_normal_class(spa));

	umem_free(run_threads, ztest_opts.zo_threads * sizeof (kthread_t *));
}

/*
 * Setup our test context and kick off threads to run tests on all datasets
 * in parallel.
 */
static void
ztest_run(ztest_shared_t *zs)
{
	spa_t *spa;
	objset_t *os;
	kthread_t *resume_thread, *deadman_thread;
	uint64_t object;
	int error;
	int t, d;

	ztest_exiting = B_FALSE;

	/*
	 * Initialize parent/child shared state.
	 */
	mutex_init(&ztest_vdev_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ztest_checkpoint_lock, NULL, MUTEX_DEFAULT, NULL);
	VERIFY0(pthread_rwlock_init(&ztest_name_lock, NULL));

	zs->zs_thread_start = gethrtime();
	zs->zs_thread_stop =
	    zs->zs_thread_start + ztest_opts.zo_passtime * NANOSEC;
	zs->zs_thread_stop = MIN(zs->zs_thread_stop, zs->zs_proc_stop);
	zs->zs_thread_kill = zs->zs_thread_stop;
	if (ztest_random(100) < ztest_opts.zo_killrate) {
		zs->zs_thread_kill -=
		    ztest_random(ztest_opts.zo_passtime * NANOSEC);
	}

	mutex_init(&zcl.zcl_callbacks_lock, NULL, MUTEX_DEFAULT, NULL);

	list_create(&zcl.zcl_callbacks, sizeof (ztest_cb_data_t),
	    offsetof(ztest_cb_data_t, zcd_node));

	/*
	 * Open our pool.  It may need to be imported first depending on
	 * what tests were running when the previous pass was terminated.
	 */
	raidz_scratch_verify();
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);
	error = spa_open(ztest_opts.zo_pool, &spa, FTAG);
	if (error) {
		VERIFY3S(error, ==, ENOENT);
		ztest_import_impl();
		VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));
		zs->zs_metaslab_sz =
		    1ULL << spa->spa_root_vdev->vdev_child[0]->vdev_ms_shift;
	}

	metaslab_preload_limit = ztest_random(20) + 1;
	ztest_spa = spa;

	/*
	 * XXX - BUGBUG raidz expansion do not run this for generic for now
	 */
	if (ztest_opts.zo_raidz_expand_test != RAIDZ_EXPAND_NONE)
		VERIFY0(vdev_raidz_impl_set("cycle"));

	dmu_objset_stats_t dds;
	VERIFY0(ztest_dmu_objset_own(ztest_opts.zo_pool,
	    DMU_OST_ANY, B_TRUE, B_TRUE, FTAG, &os));
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	dmu_objset_fast_stat(os, &dds);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
	dmu_objset_disown(os, B_TRUE, FTAG);

	/* Give the dedicated raidz expansion test more grace time */
	if (ztest_opts.zo_raidz_expand_test != RAIDZ_EXPAND_NONE)
		zfs_deadman_synctime_ms *= 2;

	/*
	 * Create a thread to periodically resume suspended I/O.
	 */
	resume_thread = thread_create(NULL, 0, ztest_resume_thread,
	    spa, 0, NULL, TS_RUN | TS_JOINABLE, defclsyspri);

	/*
	 * Create a deadman thread and set to panic if we hang.
	 */
	deadman_thread = thread_create(NULL, 0, ztest_deadman_thread,
	    zs, 0, NULL, TS_RUN | TS_JOINABLE, defclsyspri);

	spa->spa_deadman_failmode = ZIO_FAILURE_MODE_PANIC;

	/*
	 * Verify that we can safely inquire about any object,
	 * whether it's allocated or not.  To make it interesting,
	 * we probe a 5-wide window around each power of two.
	 * This hits all edge cases, including zero and the max.
	 */
	for (t = 0; t < 64; t++) {
		for (d = -5; d <= 5; d++) {
			error = dmu_object_info(spa->spa_meta_objset,
			    (1ULL << t) + d, NULL);
			ASSERT(error == 0 || error == ENOENT ||
			    error == EINVAL);
		}
	}

	/*
	 * If we got any ENOSPC errors on the previous run, destroy something.
	 */
	if (zs->zs_enospc_count != 0) {
		/* Not expecting ENOSPC errors during raidz expansion tests */
		ASSERT3U(ztest_opts.zo_raidz_expand_test, ==,
		    RAIDZ_EXPAND_NONE);

		int d = ztest_random(ztest_opts.zo_datasets);
		ztest_dataset_destroy(d);
	}
	zs->zs_enospc_count = 0;

	/*
	 * If we were in the middle of ztest_device_removal() and were killed
	 * we need to ensure the removal and scrub complete before running
	 * any tests that check ztest_device_removal_active. The removal will
	 * be restarted automatically when the spa is opened, but we need to
	 * initiate the scrub manually if it is not already in progress. Note
	 * that we always run the scrub whenever an indirect vdev exists
	 * because we have no way of knowing for sure if ztest_device_removal()
	 * fully completed its scrub before the pool was reimported.
	 *
	 * Does not apply for the RAIDZ expansion specific test runs
	 */
	if (ztest_opts.zo_raidz_expand_test == RAIDZ_EXPAND_NONE &&
	    (spa->spa_removing_phys.sr_state == DSS_SCANNING ||
	    spa->spa_removing_phys.sr_prev_indirect_vdev != -1)) {
		while (spa->spa_removing_phys.sr_state == DSS_SCANNING)
			txg_wait_synced(spa_get_dsl(spa), 0);

		error = ztest_scrub_impl(spa);
		if (error == EBUSY)
			error = 0;
		ASSERT0(error);
	}

	if (ztest_opts.zo_verbose >= 4)
		(void) printf("starting main threads...\n");

	/*
	 * Replay all logs of all datasets in the pool. This is primarily for
	 * temporary datasets which wouldn't otherwise get replayed, which
	 * can trigger failures when attempting to offline a SLOG in
	 * ztest_fault_inject().
	 */
	(void) dmu_objset_find(ztest_opts.zo_pool, ztest_replay_zil_cb,
	    NULL, DS_FIND_CHILDREN);

	if (ztest_opts.zo_raidz_expand_test == RAIDZ_EXPAND_REQUESTED)
		ztest_raidz_expand_run(zs, spa);
	else if (ztest_opts.zo_raidz_expand_test == RAIDZ_EXPAND_KILLED)
		ztest_raidz_expand_check(spa);
	else
		ztest_generic_run(zs, spa);

	/* Kill the resume and deadman threads */
	ztest_exiting = B_TRUE;
	VERIFY0(thread_join(resume_thread));
	VERIFY0(thread_join(deadman_thread));
	ztest_resume(spa);

	/*
	 * Right before closing the pool, kick off a bunch of async I/O;
	 * spa_close() should wait for it to complete.
	 */
	for (object = 1; object < 50; object++) {
		dmu_prefetch(spa->spa_meta_objset, object, 0, 0, 1ULL << 20,
		    ZIO_PRIORITY_SYNC_READ);
	}

	/* Verify that at least one commit cb was called in a timely fashion */
	if (zc_cb_counter >= ZTEST_COMMIT_CB_MIN_REG)
		VERIFY0(zc_min_txg_delay);

	spa_close(spa, FTAG);

	/*
	 * Verify that we can loop over all pools.
	 */
	mutex_enter(&spa_namespace_lock);
	for (spa = spa_next(NULL); spa != NULL; spa = spa_next(spa))
		if (ztest_opts.zo_verbose > 3)
			(void) printf("spa_next: found %s\n", spa_name(spa));
	mutex_exit(&spa_namespace_lock);

	/*
	 * Verify that we can export the pool and reimport it under a
	 * different name.
	 */
	if ((ztest_random(2) == 0) && !ztest_opts.zo_mmp_test) {
		char name[ZFS_MAX_DATASET_NAME_LEN];
		(void) snprintf(name, sizeof (name), "%s_import",
		    ztest_opts.zo_pool);
		ztest_spa_import_export(ztest_opts.zo_pool, name);
		ztest_spa_import_export(name, ztest_opts.zo_pool);
	}

	kernel_fini();

	list_destroy(&zcl.zcl_callbacks);
	mutex_destroy(&zcl.zcl_callbacks_lock);
	(void) pthread_rwlock_destroy(&ztest_name_lock);
	mutex_destroy(&ztest_vdev_lock);
	mutex_destroy(&ztest_checkpoint_lock);
}

static void
print_time(hrtime_t t, char *timebuf)
{
	hrtime_t s = t / NANOSEC;
	hrtime_t m = s / 60;
	hrtime_t h = m / 60;
	hrtime_t d = h / 24;

	s -= m * 60;
	m -= h * 60;
	h -= d * 24;

	timebuf[0] = '\0';

	if (d)
		(void) sprintf(timebuf,
		    "%llud%02lluh%02llum%02llus", d, h, m, s);
	else if (h)
		(void) sprintf(timebuf, "%lluh%02llum%02llus", h, m, s);
	else if (m)
		(void) sprintf(timebuf, "%llum%02llus", m, s);
	else
		(void) sprintf(timebuf, "%llus", s);
}

static nvlist_t *
make_random_props(void)
{
	nvlist_t *props;

	props = fnvlist_alloc();

	if (ztest_random(2) == 0)
		return (props);

	fnvlist_add_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_AUTOREPLACE), 1);

	return (props);
}

/*
 * Create a storage pool with the given name and initial vdev size.
 * Then test spa_freeze() functionality.
 */
static void
ztest_init(ztest_shared_t *zs)
{
	spa_t *spa;
	nvlist_t *nvroot, *props;
	int i;

	mutex_init(&ztest_vdev_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ztest_checkpoint_lock, NULL, MUTEX_DEFAULT, NULL);
	VERIFY0(pthread_rwlock_init(&ztest_name_lock, NULL));

	raidz_scratch_verify();
	kernel_init(SPA_MODE_READ | SPA_MODE_WRITE);

	/*
	 * Create the storage pool.
	 */
	(void) spa_destroy(ztest_opts.zo_pool);
	ztest_shared->zs_vdev_next_leaf = 0;
	zs->zs_splits = 0;
	zs->zs_mirrors = ztest_opts.zo_mirrors;
	nvroot = make_vdev_root(NULL, NULL, NULL, ztest_opts.zo_vdev_size, 0,
	    NULL, ztest_opts.zo_raid_children, zs->zs_mirrors, 1);
	props = make_random_props();

	/*
	 * We don't expect the pool to suspend unless maxfaults == 0,
	 * in which case ztest_fault_inject() temporarily takes away
	 * the only valid replica.
	 */
	fnvlist_add_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_FAILUREMODE),
	    MAXFAULTS(zs) ? ZIO_FAILURE_MODE_PANIC : ZIO_FAILURE_MODE_WAIT);

	for (i = 0; i < SPA_FEATURES; i++) {
		char *buf;

		if (!spa_feature_table[i].fi_zfs_mod_supported)
			continue;

		/*
		 * 75% chance of using the log space map feature. We want ztest
		 * to exercise both the code paths that use the log space map
		 * feature and the ones that don't.
		 */
		if (i == SPA_FEATURE_LOG_SPACEMAP && ztest_random(4) == 0)
			continue;

		VERIFY3S(-1, !=, asprintf(&buf, "feature@%s",
		    spa_feature_table[i].fi_uname));
		fnvlist_add_uint64(props, buf, 0);
		free(buf);
	}

	VERIFY0(spa_create(ztest_opts.zo_pool, nvroot, props, NULL, NULL));
	fnvlist_free(nvroot);
	fnvlist_free(props);

	VERIFY0(spa_open(ztest_opts.zo_pool, &spa, FTAG));
	zs->zs_metaslab_sz =
	    1ULL << spa->spa_root_vdev->vdev_child[0]->vdev_ms_shift;
	zs->zs_guid = spa_guid(spa);
	spa_close(spa, FTAG);

	kernel_fini();

	if (!ztest_opts.zo_mmp_test) {
		ztest_run_zdb(zs->zs_guid);
		ztest_freeze();
		ztest_run_zdb(zs->zs_guid);
	}

	(void) pthread_rwlock_destroy(&ztest_name_lock);
	mutex_destroy(&ztest_vdev_lock);
	mutex_destroy(&ztest_checkpoint_lock);
}

static void
setup_data_fd(void)
{
	static char ztest_name_data[] = "/tmp/ztest.data.XXXXXX";

	ztest_fd_data = mkstemp(ztest_name_data);
	ASSERT3S(ztest_fd_data, >=, 0);
	(void) unlink(ztest_name_data);
}

static int
shared_data_size(ztest_shared_hdr_t *hdr)
{
	int size;

	size = hdr->zh_hdr_size;
	size += hdr->zh_opts_size;
	size += hdr->zh_size;
	size += hdr->zh_stats_size * hdr->zh_stats_count;
	size += hdr->zh_ds_size * hdr->zh_ds_count;
	size += hdr->zh_scratch_state_size;

	return (size);
}

static void
setup_hdr(void)
{
	int size;
	ztest_shared_hdr_t *hdr;

	hdr = (void *)mmap(0, P2ROUNDUP(sizeof (*hdr), getpagesize()),
	    PROT_READ | PROT_WRITE, MAP_SHARED, ztest_fd_data, 0);
	ASSERT3P(hdr, !=, MAP_FAILED);

	VERIFY0(ftruncate(ztest_fd_data, sizeof (ztest_shared_hdr_t)));

	hdr->zh_hdr_size = sizeof (ztest_shared_hdr_t);
	hdr->zh_opts_size = sizeof (ztest_shared_opts_t);
	hdr->zh_size = sizeof (ztest_shared_t);
	hdr->zh_stats_size = sizeof (ztest_shared_callstate_t);
	hdr->zh_stats_count = ZTEST_FUNCS;
	hdr->zh_ds_size = sizeof (ztest_shared_ds_t);
	hdr->zh_ds_count = ztest_opts.zo_datasets;
	hdr->zh_scratch_state_size = sizeof (ztest_shared_scratch_state_t);

	size = shared_data_size(hdr);
	VERIFY0(ftruncate(ztest_fd_data, size));

	(void) munmap((caddr_t)hdr, P2ROUNDUP(sizeof (*hdr), getpagesize()));
}

static void
setup_data(void)
{
	int size, offset;
	ztest_shared_hdr_t *hdr;
	uint8_t *buf;

	hdr = (void *)mmap(0, P2ROUNDUP(sizeof (*hdr), getpagesize()),
	    PROT_READ, MAP_SHARED, ztest_fd_data, 0);
	ASSERT3P(hdr, !=, MAP_FAILED);

	size = shared_data_size(hdr);

	(void) munmap((caddr_t)hdr, P2ROUNDUP(sizeof (*hdr), getpagesize()));
	hdr = ztest_shared_hdr = (void *)mmap(0, P2ROUNDUP(size, getpagesize()),
	    PROT_READ | PROT_WRITE, MAP_SHARED, ztest_fd_data, 0);
	ASSERT3P(hdr, !=, MAP_FAILED);
	buf = (uint8_t *)hdr;

	offset = hdr->zh_hdr_size;
	ztest_shared_opts = (void *)&buf[offset];
	offset += hdr->zh_opts_size;
	ztest_shared = (void *)&buf[offset];
	offset += hdr->zh_size;
	ztest_shared_callstate = (void *)&buf[offset];
	offset += hdr->zh_stats_size * hdr->zh_stats_count;
	ztest_shared_ds = (void *)&buf[offset];
	offset += hdr->zh_ds_size * hdr->zh_ds_count;
	ztest_scratch_state = (void *)&buf[offset];
}

static boolean_t
exec_child(char *cmd, char *libpath, boolean_t ignorekill, int *statusp)
{
	pid_t pid;
	int status;
	char *cmdbuf = NULL;

	pid = fork();

	if (cmd == NULL) {
		cmdbuf = umem_alloc(MAXPATHLEN, UMEM_NOFAIL);
		(void) strlcpy(cmdbuf, getexecname(), MAXPATHLEN);
		cmd = cmdbuf;
	}

	if (pid == -1)
		fatal(B_TRUE, "fork failed");

	if (pid == 0) {	/* child */
		char fd_data_str[12];

		VERIFY3S(11, >=,
		    snprintf(fd_data_str, 12, "%d", ztest_fd_data));
		VERIFY0(setenv("ZTEST_FD_DATA", fd_data_str, 1));

		if (libpath != NULL) {
			const char *curlp = getenv("LD_LIBRARY_PATH");
			if (curlp == NULL)
				VERIFY0(setenv("LD_LIBRARY_PATH", libpath, 1));
			else {
				char *newlp = NULL;
				VERIFY3S(-1, !=,
				    asprintf(&newlp, "%s:%s", libpath, curlp));
				VERIFY0(setenv("LD_LIBRARY_PATH", newlp, 1));
				free(newlp);
			}
		}
		(void) execl(cmd, cmd, (char *)NULL);
		ztest_dump_core = B_FALSE;
		fatal(B_TRUE, "exec failed: %s", cmd);
	}

	if (cmdbuf != NULL) {
		umem_free(cmdbuf, MAXPATHLEN);
		cmd = NULL;
	}

	while (waitpid(pid, &status, 0) != pid)
		continue;
	if (statusp != NULL)
		*statusp = status;

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			(void) fprintf(stderr, "child exited with code %d\n",
			    WEXITSTATUS(status));
			exit(2);
		}
		return (B_FALSE);
	} else if (WIFSIGNALED(status)) {
		if (!ignorekill || WTERMSIG(status) != SIGKILL) {
			(void) fprintf(stderr, "child died with signal %d\n",
			    WTERMSIG(status));
			exit(3);
		}
		return (B_TRUE);
	} else {
		(void) fprintf(stderr, "something strange happened to child\n");
		exit(4);
	}
}

static void
ztest_run_init(void)
{
	int i;

	ztest_shared_t *zs = ztest_shared;

	/*
	 * Blow away any existing copy of zpool.cache
	 */
	(void) remove(spa_config_path);

	if (ztest_opts.zo_init == 0) {
		if (ztest_opts.zo_verbose >= 1)
			(void) printf("Importing pool %s\n",
			    ztest_opts.zo_pool);
		ztest_import(zs);
		return;
	}

	/*
	 * Create and initialize our storage pool.
	 */
	for (i = 1; i <= ztest_opts.zo_init; i++) {
		memset(zs, 0, sizeof (*zs));
		if (ztest_opts.zo_verbose >= 3 &&
		    ztest_opts.zo_init != 1) {
			(void) printf("ztest_init(), pass %d\n", i);
		}
		ztest_init(zs);
	}
}

int
main(int argc, char **argv)
{
	int kills = 0;
	int iters = 0;
	int older = 0;
	int newer = 0;
	ztest_shared_t *zs;
	ztest_info_t *zi;
	ztest_shared_callstate_t *zc;
	char timebuf[100];
	char numbuf[NN_NUMBUF_SZ];
	char *cmd;
	boolean_t hasalt;
	int f, err;
	char *fd_data_str = getenv("ZTEST_FD_DATA");
	struct sigaction action;

	(void) setvbuf(stdout, NULL, _IOLBF, 0);

	dprintf_setup(&argc, argv);
	zfs_deadman_synctime_ms = 300000;
	zfs_deadman_checktime_ms = 30000;
	/*
	 * As two-word space map entries may not come up often (especially
	 * if pool and vdev sizes are small) we want to force at least some
	 * of them so the feature get tested.
	 */
	zfs_force_some_double_word_sm_entries = B_TRUE;

	/*
	 * Verify that even extensively damaged split blocks with many
	 * segments can be reconstructed in a reasonable amount of time
	 * when reconstruction is known to be possible.
	 *
	 * Note: the lower this value is, the more damage we inflict, and
	 * the more time ztest spends in recovering that damage. We chose
	 * to induce damage 1/100th of the time so recovery is tested but
	 * not so frequently that ztest doesn't get to test other code paths.
	 */
	zfs_reconstruct_indirect_damage_fraction = 100;

	action.sa_handler = sig_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	if (sigaction(SIGSEGV, &action, NULL) < 0) {
		(void) fprintf(stderr, "ztest: cannot catch SIGSEGV: %s.\n",
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (sigaction(SIGABRT, &action, NULL) < 0) {
		(void) fprintf(stderr, "ztest: cannot catch SIGABRT: %s.\n",
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*
	 * Force random_get_bytes() to use /dev/urandom in order to prevent
	 * ztest from needlessly depleting the system entropy pool.
	 */
	random_path = "/dev/urandom";
	ztest_fd_rand = open(random_path, O_RDONLY | O_CLOEXEC);
	ASSERT3S(ztest_fd_rand, >=, 0);

	if (!fd_data_str) {
		process_options(argc, argv);

		setup_data_fd();
		setup_hdr();
		setup_data();
		memcpy(ztest_shared_opts, &ztest_opts,
		    sizeof (*ztest_shared_opts));
	} else {
		ztest_fd_data = atoi(fd_data_str);
		setup_data();
		memcpy(&ztest_opts, ztest_shared_opts, sizeof (ztest_opts));
	}
	ASSERT3U(ztest_opts.zo_datasets, ==, ztest_shared_hdr->zh_ds_count);

	err = ztest_set_global_vars();
	if (err != 0 && !fd_data_str) {
		/* error message done by ztest_set_global_vars */
		exit(EXIT_FAILURE);
	} else {
		/* children should not be spawned if setting gvars fails */
		VERIFY3S(err, ==, 0);
	}

	/* Override location of zpool.cache */
	VERIFY3S(asprintf((char **)&spa_config_path, "%s/zpool.cache",
	    ztest_opts.zo_dir), !=, -1);

	ztest_ds = umem_alloc(ztest_opts.zo_datasets * sizeof (ztest_ds_t),
	    UMEM_NOFAIL);
	zs = ztest_shared;

	if (fd_data_str) {
		metaslab_force_ganging = ztest_opts.zo_metaslab_force_ganging;
		metaslab_df_alloc_threshold =
		    zs->zs_metaslab_df_alloc_threshold;

		if (zs->zs_do_init)
			ztest_run_init();
		else
			ztest_run(zs);
		exit(0);
	}

	hasalt = (strlen(ztest_opts.zo_alt_ztest) != 0);

	if (ztest_opts.zo_verbose >= 1) {
		(void) printf("%"PRIu64" vdevs, %d datasets, %d threads, "
		    "%d %s disks, parity %d, %"PRIu64" seconds...\n\n",
		    ztest_opts.zo_vdevs,
		    ztest_opts.zo_datasets,
		    ztest_opts.zo_threads,
		    ztest_opts.zo_raid_children,
		    ztest_opts.zo_raid_type,
		    ztest_opts.zo_raid_parity,
		    ztest_opts.zo_time);
	}

	cmd = umem_alloc(MAXNAMELEN, UMEM_NOFAIL);
	(void) strlcpy(cmd, getexecname(), MAXNAMELEN);

	zs->zs_do_init = B_TRUE;
	if (strlen(ztest_opts.zo_alt_ztest) != 0) {
		if (ztest_opts.zo_verbose >= 1) {
			(void) printf("Executing older ztest for "
			    "initialization: %s\n", ztest_opts.zo_alt_ztest);
		}
		VERIFY(!exec_child(ztest_opts.zo_alt_ztest,
		    ztest_opts.zo_alt_libpath, B_FALSE, NULL));
	} else {
		VERIFY(!exec_child(NULL, NULL, B_FALSE, NULL));
	}
	zs->zs_do_init = B_FALSE;

	zs->zs_proc_start = gethrtime();
	zs->zs_proc_stop = zs->zs_proc_start + ztest_opts.zo_time * NANOSEC;

	for (f = 0; f < ZTEST_FUNCS; f++) {
		zi = &ztest_info[f];
		zc = ZTEST_GET_SHARED_CALLSTATE(f);
		if (zs->zs_proc_start + zi->zi_interval[0] > zs->zs_proc_stop)
			zc->zc_next = UINT64_MAX;
		else
			zc->zc_next = zs->zs_proc_start +
			    ztest_random(2 * zi->zi_interval[0] + 1);
	}

	/*
	 * Run the tests in a loop.  These tests include fault injection
	 * to verify that self-healing data works, and forced crashes
	 * to verify that we never lose on-disk consistency.
	 */
	while (gethrtime() < zs->zs_proc_stop) {
		int status;
		boolean_t killed;

		/*
		 * Initialize the workload counters for each function.
		 */
		for (f = 0; f < ZTEST_FUNCS; f++) {
			zc = ZTEST_GET_SHARED_CALLSTATE(f);
			zc->zc_count = 0;
			zc->zc_time = 0;
		}

		/* Set the allocation switch size */
		zs->zs_metaslab_df_alloc_threshold =
		    ztest_random(zs->zs_metaslab_sz / 4) + 1;

		if (!hasalt || ztest_random(2) == 0) {
			if (hasalt && ztest_opts.zo_verbose >= 1) {
				(void) printf("Executing newer ztest: %s\n",
				    cmd);
			}
			newer++;
			killed = exec_child(cmd, NULL, B_TRUE, &status);
		} else {
			if (hasalt && ztest_opts.zo_verbose >= 1) {
				(void) printf("Executing older ztest: %s\n",
				    ztest_opts.zo_alt_ztest);
			}
			older++;
			killed = exec_child(ztest_opts.zo_alt_ztest,
			    ztest_opts.zo_alt_libpath, B_TRUE, &status);
		}

		if (killed)
			kills++;
		iters++;

		if (ztest_opts.zo_verbose >= 1) {
			hrtime_t now = gethrtime();

			now = MIN(now, zs->zs_proc_stop);
			print_time(zs->zs_proc_stop - now, timebuf);
			nicenum(zs->zs_space, numbuf, sizeof (numbuf));

			(void) printf("Pass %3d, %8s, %3"PRIu64" ENOSPC, "
			    "%4.1f%% of %5s used, %3.0f%% done, %8s to go\n",
			    iters,
			    WIFEXITED(status) ? "Complete" : "SIGKILL",
			    zs->zs_enospc_count,
			    100.0 * zs->zs_alloc / zs->zs_space,
			    numbuf,
			    100.0 * (now - zs->zs_proc_start) /
			    (ztest_opts.zo_time * NANOSEC), timebuf);
		}

		if (ztest_opts.zo_verbose >= 2) {
			(void) printf("\nWorkload summary:\n\n");
			(void) printf("%7s %9s   %s\n",
			    "Calls", "Time", "Function");
			(void) printf("%7s %9s   %s\n",
			    "-----", "----", "--------");
			for (f = 0; f < ZTEST_FUNCS; f++) {
				zi = &ztest_info[f];
				zc = ZTEST_GET_SHARED_CALLSTATE(f);
				print_time(zc->zc_time, timebuf);
				(void) printf("%7"PRIu64" %9s   %s\n",
				    zc->zc_count, timebuf,
				    zi->zi_funcname);
			}
			(void) printf("\n");
		}

		if (!ztest_opts.zo_mmp_test)
			ztest_run_zdb(zs->zs_guid);
		if (ztest_shared_opts->zo_raidz_expand_test ==
		    RAIDZ_EXPAND_CHECKED)
			break; /* raidz expand test complete */
	}

	if (ztest_opts.zo_verbose >= 1) {
		if (hasalt) {
			(void) printf("%d runs of older ztest: %s\n", older,
			    ztest_opts.zo_alt_ztest);
			(void) printf("%d runs of newer ztest: %s\n", newer,
			    cmd);
		}
		(void) printf("%d killed, %d completed, %.0f%% kill rate\n",
		    kills, iters - kills, (100.0 * kills) / MAX(1, iters));
	}

	umem_free(cmd, MAXNAMELEN);

	return (0);
}
