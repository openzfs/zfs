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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, Joyent, Inc.
 * Copyright (c) 2011, 2020, Delphix. All rights reserved.
 * Copyright (c) 2014, Saso Kiselkov. All rights reserved.
 * Copyright (c) 2017, Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright (c) 2020, George Amanakis. All rights reserved.
 * Copyright (c) 2019, Klara Inc.
 * Copyright (c) 2019, Allan Jude
 * Copyright (c) 2020, The FreeBSD Foundation [1]
 *
 * [1] Portions of this software were developed by Allan Jude
 *     under sponsorship from the FreeBSD Foundation.
 */

/*
 * DVA-based Adjustable Replacement Cache
 *
 * While much of the theory of operation used here is
 * based on the self-tuning, low overhead replacement cache
 * presented by Megiddo and Modha at FAST 2003, there are some
 * significant differences:
 *
 * 1. The Megiddo and Modha model assumes any page is evictable.
 * Pages in its cache cannot be "locked" into memory.  This makes
 * the eviction algorithm simple: evict the last page in the list.
 * This also make the performance characteristics easy to reason
 * about.  Our cache is not so simple.  At any given moment, some
 * subset of the blocks in the cache are un-evictable because we
 * have handed out a reference to them.  Blocks are only evictable
 * when there are no external references active.  This makes
 * eviction far more problematic:  we choose to evict the evictable
 * blocks that are the "lowest" in the list.
 *
 * There are times when it is not possible to evict the requested
 * space.  In these circumstances we are unable to adjust the cache
 * size.  To prevent the cache growing unbounded at these times we
 * implement a "cache throttle" that slows the flow of new data
 * into the cache until we can make space available.
 *
 * 2. The Megiddo and Modha model assumes a fixed cache size.
 * Pages are evicted when the cache is full and there is a cache
 * miss.  Our model has a variable sized cache.  It grows with
 * high use, but also tries to react to memory pressure from the
 * operating system: decreasing its size when system memory is
 * tight.
 *
 * 3. The Megiddo and Modha model assumes a fixed page size. All
 * elements of the cache are therefore exactly the same size.  So
 * when adjusting the cache size following a cache miss, its simply
 * a matter of choosing a single page to evict.  In our model, we
 * have variable sized cache blocks (ranging from 512 bytes to
 * 128K bytes).  We therefore choose a set of blocks to evict to make
 * space for a cache miss that approximates as closely as possible
 * the space used by the new block.
 *
 * See also:  "ARC: A Self-Tuning, Low Overhead Replacement Cache"
 * by N. Megiddo & D. Modha, FAST 2003
 */

/*
 * The locking model:
 *
 * A new reference to a cache buffer can be obtained in two
 * ways: 1) via a hash table lookup using the DVA as a key,
 * or 2) via one of the ARC lists.  The arc_read() interface
 * uses method 1, while the internal ARC algorithms for
 * adjusting the cache use method 2.  We therefore provide two
 * types of locks: 1) the hash table lock array, and 2) the
 * ARC list locks.
 *
 * Buffers do not have their own mutexes, rather they rely on the
 * hash table mutexes for the bulk of their protection (i.e. most
 * fields in the arc_buf_hdr_t are protected by these mutexes).
 *
 * buf_hash_find() returns the appropriate mutex (held) when it
 * locates the requested buffer in the hash table.  It returns
 * NULL for the mutex if the buffer was not in the table.
 *
 * buf_hash_remove() expects the appropriate hash mutex to be
 * already held before it is invoked.
 *
 * Each ARC state also has a mutex which is used to protect the
 * buffer list associated with the state.  When attempting to
 * obtain a hash table lock while holding an ARC list lock you
 * must use: mutex_tryenter() to avoid deadlock.  Also note that
 * the active state mutex must be held before the ghost state mutex.
 *
 * It as also possible to register a callback which is run when the
 * arc_meta_limit is reached and no buffers can be safely evicted.  In
 * this case the arc user should drop a reference on some arc buffers so
 * they can be reclaimed and the arc_meta_limit honored.  For example,
 * when using the ZPL each dentry holds a references on a znode.  These
 * dentries must be pruned before the arc buffer holding the znode can
 * be safely evicted.
 *
 * Note that the majority of the performance stats are manipulated
 * with atomic operations.
 *
 * The L2ARC uses the l2ad_mtx on each vdev for the following:
 *
 *	- L2ARC buflist creation
 *	- L2ARC buflist eviction
 *	- L2ARC write completion, which walks L2ARC buflists
 *	- ARC header destruction, as it removes from L2ARC buflists
 *	- ARC header release, as it removes from L2ARC buflists
 */

/*
 * ARC operation:
 *
 * Every block that is in the ARC is tracked by an arc_buf_hdr_t structure.
 * This structure can point either to a block that is still in the cache or to
 * one that is only accessible in an L2 ARC device, or it can provide
 * information about a block that was recently evicted. If a block is
 * only accessible in the L2ARC, then the arc_buf_hdr_t only has enough
 * information to retrieve it from the L2ARC device. This information is
 * stored in the l2arc_buf_hdr_t sub-structure of the arc_buf_hdr_t. A block
 * that is in this state cannot access the data directly.
 *
 * Blocks that are actively being referenced or have not been evicted
 * are cached in the L1ARC. The L1ARC (l1arc_buf_hdr_t) is a structure within
 * the arc_buf_hdr_t that will point to the data block in memory. A block can
 * only be read by a consumer if it has an l1arc_buf_hdr_t. The L1ARC
 * caches data in two ways -- in a list of ARC buffers (arc_buf_t) and
 * also in the arc_buf_hdr_t's private physical data block pointer (b_pabd).
 *
 * The L1ARC's data pointer may or may not be uncompressed. The ARC has the
 * ability to store the physical data (b_pabd) associated with the DVA of the
 * arc_buf_hdr_t. Since the b_pabd is a copy of the on-disk physical block,
 * it will match its on-disk compression characteristics. This behavior can be
 * disabled by setting 'zfs_compressed_arc_enabled' to B_FALSE. When the
 * compressed ARC functionality is disabled, the b_pabd will point to an
 * uncompressed version of the on-disk data.
 *
 * Data in the L1ARC is not accessed by consumers of the ARC directly. Each
 * arc_buf_hdr_t can have multiple ARC buffers (arc_buf_t) which reference it.
 * Each ARC buffer (arc_buf_t) is being actively accessed by a specific ARC
 * consumer. The ARC will provide references to this data and will keep it
 * cached until it is no longer in use. The ARC caches only the L1ARC's physical
 * data block and will evict any arc_buf_t that is no longer referenced. The
 * amount of memory consumed by the arc_buf_ts' data buffers can be seen via the
 * "overhead_size" kstat.
 *
 * Depending on the consumer, an arc_buf_t can be requested in uncompressed or
 * compressed form. The typical case is that consumers will want uncompressed
 * data, and when that happens a new data buffer is allocated where the data is
 * decompressed for them to use. Currently the only consumer who wants
 * compressed arc_buf_t's is "zfs send", when it streams data exactly as it
 * exists on disk. When this happens, the arc_buf_t's data buffer is shared
 * with the arc_buf_hdr_t.
 *
 * Here is a diagram showing an arc_buf_hdr_t referenced by two arc_buf_t's. The
 * first one is owned by a compressed send consumer (and therefore references
 * the same compressed data buffer as the arc_buf_hdr_t) and the second could be
 * used by any other consumer (and has its own uncompressed copy of the data
 * buffer).
 *
 *   arc_buf_hdr_t
 *   +-----------+
 *   | fields    |
 *   | common to |
 *   | L1- and   |
 *   | L2ARC     |
 *   +-----------+
 *   | l2arc_buf_hdr_t
 *   |           |
 *   +-----------+
 *   | l1arc_buf_hdr_t
 *   |           |              arc_buf_t
 *   | b_buf     +------------>+-----------+      arc_buf_t
 *   | b_pabd    +-+           |b_next     +---->+-----------+
 *   +-----------+ |           |-----------|     |b_next     +-->NULL
 *                 |           |b_comp = T |     +-----------+
 *                 |           |b_data     +-+   |b_comp = F |
 *                 |           +-----------+ |   |b_data     +-+
 *                 +->+------+               |   +-----------+ |
 *        compressed  |      |               |                 |
 *           data     |      |<--------------+                 | uncompressed
 *                    +------+          compressed,            |     data
 *                                        shared               +-->+------+
 *                                         data                    |      |
 *                                                                 |      |
 *                                                                 +------+
 *
 * When a consumer reads a block, the ARC must first look to see if the
 * arc_buf_hdr_t is cached. If the hdr is cached then the ARC allocates a new
 * arc_buf_t and either copies uncompressed data into a new data buffer from an
 * existing uncompressed arc_buf_t, decompresses the hdr's b_pabd buffer into a
 * new data buffer, or shares the hdr's b_pabd buffer, depending on whether the
 * hdr is compressed and the desired compression characteristics of the
 * arc_buf_t consumer. If the arc_buf_t ends up sharing data with the
 * arc_buf_hdr_t and both of them are uncompressed then the arc_buf_t must be
 * the last buffer in the hdr's b_buf list, however a shared compressed buf can
 * be anywhere in the hdr's list.
 *
 * The diagram below shows an example of an uncompressed ARC hdr that is
 * sharing its data with an arc_buf_t (note that the shared uncompressed buf is
 * the last element in the buf list):
 *
 *                arc_buf_hdr_t
 *                +-----------+
 *                |           |
 *                |           |
 *                |           |
 *                +-----------+
 * l2arc_buf_hdr_t|           |
 *                |           |
 *                +-----------+
 * l1arc_buf_hdr_t|           |
 *                |           |                 arc_buf_t    (shared)
 *                |    b_buf  +------------>+---------+      arc_buf_t
 *                |           |             |b_next   +---->+---------+
 *                |  b_pabd   +-+           |---------|     |b_next   +-->NULL
 *                +-----------+ |           |         |     +---------+
 *                              |           |b_data   +-+   |         |
 *                              |           +---------+ |   |b_data   +-+
 *                              +->+------+             |   +---------+ |
 *                                 |      |             |               |
 *                   uncompressed  |      |             |               |
 *                        data     +------+             |               |
 *                                    ^                 +->+------+     |
 *                                    |       uncompressed |      |     |
 *                                    |           data     |      |     |
 *                                    |                    +------+     |
 *                                    +---------------------------------+
 *
 * Writing to the ARC requires that the ARC first discard the hdr's b_pabd
 * since the physical block is about to be rewritten. The new data contents
 * will be contained in the arc_buf_t. As the I/O pipeline performs the write,
 * it may compress the data before writing it to disk. The ARC will be called
 * with the transformed data and will bcopy the transformed on-disk block into
 * a newly allocated b_pabd. Writes are always done into buffers which have
 * either been loaned (and hence are new and don't have other readers) or
 * buffers which have been released (and hence have their own hdr, if there
 * were originally other readers of the buf's original hdr). This ensures that
 * the ARC only needs to update a single buf and its hdr after a write occurs.
 *
 * When the L2ARC is in use, it will also take advantage of the b_pabd. The
 * L2ARC will always write the contents of b_pabd to the L2ARC. This means
 * that when compressed ARC is enabled that the L2ARC blocks are identical
 * to the on-disk block in the main data pool. This provides a significant
 * advantage since the ARC can leverage the bp's checksum when reading from the
 * L2ARC to determine if the contents are valid. However, if the compressed
 * ARC is disabled, then the L2ARC's block must be transformed to look
 * like the physical block in the main data pool before comparing the
 * checksum and determining its validity.
 *
 * The L1ARC has a slightly different system for storing encrypted data.
 * Raw (encrypted + possibly compressed) data has a few subtle differences from
 * data that is just compressed. The biggest difference is that it is not
 * possible to decrypt encrypted data (or vice-versa) if the keys aren't loaded.
 * The other difference is that encryption cannot be treated as a suggestion.
 * If a caller would prefer compressed data, but they actually wind up with
 * uncompressed data the worst thing that could happen is there might be a
 * performance hit. If the caller requests encrypted data, however, we must be
 * sure they actually get it or else secret information could be leaked. Raw
 * data is stored in hdr->b_crypt_hdr.b_rabd. An encrypted header, therefore,
 * may have both an encrypted version and a decrypted version of its data at
 * once. When a caller needs a raw arc_buf_t, it is allocated and the data is
 * copied out of this header. To avoid complications with b_pabd, raw buffers
 * cannot be shared.
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/spa_impl.h>
#include <sys/zio_compress.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/zfs_refcount.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_pool.h>
#include <sys/zio_checksum.h>
#include <sys/multilist.h>
#include <sys/abd.h>
#include <sys/zil.h>
#include <sys/fm/fs/zfs.h>
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/zthr.h>
#include <zfs_fletcher.h>
#include <sys/arc_impl.h>
#include <sys/trace_zfs.h>
#include <sys/aggsum.h>
#include <cityhash.h>
#include <sys/vdev_trim.h>
#include <sys/zstd/zstd.h>

#ifndef _KERNEL
/* set with ZFS_DEBUG=watch, to enable watchpoints on frozen buffers */
boolean_t arc_watch = B_FALSE;
#endif

/*
 * This thread's job is to keep enough free memory in the system, by
 * calling arc_kmem_reap_soon() plus arc_reduce_target_size(), which improves
 * arc_available_memory().
 */
static zthr_t *arc_reap_zthr;

/*
 * This thread's job is to keep arc_size under arc_c, by calling
 * arc_evict(), which improves arc_is_overflowing().
 */
static zthr_t *arc_evict_zthr;

static kmutex_t arc_evict_lock;
static boolean_t arc_evict_needed = B_FALSE;

/*
 * Count of bytes evicted since boot.
 */
static uint64_t arc_evict_count;

/*
 * List of arc_evict_waiter_t's, representing threads waiting for the
 * arc_evict_count to reach specific values.
 */
static list_t arc_evict_waiters;

/*
 * When arc_is_overflowing(), arc_get_data_impl() waits for this percent of
 * the requested amount of data to be evicted.  For example, by default for
 * every 2KB that's evicted, 1KB of it may be "reused" by a new allocation.
 * Since this is above 100%, it ensures that progress is made towards getting
 * arc_size under arc_c.  Since this is finite, it ensures that allocations
 * can still happen, even during the potentially long time that arc_size is
 * more than arc_c.
 */
int zfs_arc_eviction_pct = 200;

/*
 * The number of headers to evict in arc_evict_state_impl() before
 * dropping the sublist lock and evicting from another sublist. A lower
 * value means we're more likely to evict the "correct" header (i.e. the
 * oldest header in the arc state), but comes with higher overhead
 * (i.e. more invocations of arc_evict_state_impl()).
 */
int zfs_arc_evict_batch_limit = 10;

/* number of seconds before growing cache again */
int arc_grow_retry = 5;

/*
 * Minimum time between calls to arc_kmem_reap_soon().
 */
int arc_kmem_cache_reap_retry_ms = 1000;

/* shift of arc_c for calculating overflow limit in arc_get_data_impl */
int zfs_arc_overflow_shift = 8;

/* shift of arc_c for calculating both min and max arc_p */
int arc_p_min_shift = 4;

/* log2(fraction of arc to reclaim) */
int arc_shrink_shift = 7;

/* percent of pagecache to reclaim arc to */
#ifdef _KERNEL
uint_t zfs_arc_pc_percent = 0;
#endif

/*
 * log2(fraction of ARC which must be free to allow growing).
 * I.e. If there is less than arc_c >> arc_no_grow_shift free memory,
 * when reading a new block into the ARC, we will evict an equal-sized block
 * from the ARC.
 *
 * This must be less than arc_shrink_shift, so that when we shrink the ARC,
 * we will still not allow it to grow.
 */
int			arc_no_grow_shift = 5;


/*
 * minimum lifespan of a prefetch block in clock ticks
 * (initialized in arc_init())
 */
static int		arc_min_prefetch_ms;
static int		arc_min_prescient_prefetch_ms;

/*
 * If this percent of memory is free, don't throttle.
 */
int arc_lotsfree_percent = 10;

/*
 * The arc has filled available memory and has now warmed up.
 */
boolean_t arc_warm;

/*
 * These tunables are for performance analysis.
 */
unsigned long zfs_arc_max = 0;
unsigned long zfs_arc_min = 0;
unsigned long zfs_arc_meta_limit = 0;
unsigned long zfs_arc_meta_min = 0;
unsigned long zfs_arc_dnode_limit = 0;
unsigned long zfs_arc_dnode_reduce_percent = 10;
int zfs_arc_grow_retry = 0;
int zfs_arc_shrink_shift = 0;
int zfs_arc_p_min_shift = 0;
int zfs_arc_average_blocksize = 8 * 1024; /* 8KB */

/*
 * ARC dirty data constraints for arc_tempreserve_space() throttle.
 */
unsigned long zfs_arc_dirty_limit_percent = 50;	/* total dirty data limit */
unsigned long zfs_arc_anon_limit_percent = 25;	/* anon block dirty limit */
unsigned long zfs_arc_pool_dirty_percent = 20;	/* each pool's anon allowance */

/*
 * Enable or disable compressed arc buffers.
 */
int zfs_compressed_arc_enabled = B_TRUE;

/*
 * ARC will evict meta buffers that exceed arc_meta_limit. This
 * tunable make arc_meta_limit adjustable for different workloads.
 */
unsigned long zfs_arc_meta_limit_percent = 75;

/*
 * Percentage that can be consumed by dnodes of ARC meta buffers.
 */
unsigned long zfs_arc_dnode_limit_percent = 10;

/*
 * These tunables are Linux specific
 */
unsigned long zfs_arc_sys_free = 0;
int zfs_arc_min_prefetch_ms = 0;
int zfs_arc_min_prescient_prefetch_ms = 0;
int zfs_arc_p_dampener_disable = 1;
int zfs_arc_meta_prune = 10000;
int zfs_arc_meta_strategy = ARC_STRATEGY_META_BALANCED;
int zfs_arc_meta_adjust_restarts = 4096;
int zfs_arc_lotsfree_percent = 10;

/* The 6 states: */
arc_state_t ARC_anon;
arc_state_t ARC_mru;
arc_state_t ARC_mru_ghost;
arc_state_t ARC_mfu;
arc_state_t ARC_mfu_ghost;
arc_state_t ARC_l2c_only;

arc_stats_t arc_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "demand_data_hits",		KSTAT_DATA_UINT64 },
	{ "demand_data_misses",		KSTAT_DATA_UINT64 },
	{ "demand_metadata_hits",	KSTAT_DATA_UINT64 },
	{ "demand_metadata_misses",	KSTAT_DATA_UINT64 },
	{ "prefetch_data_hits",		KSTAT_DATA_UINT64 },
	{ "prefetch_data_misses",	KSTAT_DATA_UINT64 },
	{ "prefetch_metadata_hits",	KSTAT_DATA_UINT64 },
	{ "prefetch_metadata_misses",	KSTAT_DATA_UINT64 },
	{ "mru_hits",			KSTAT_DATA_UINT64 },
	{ "mru_ghost_hits",		KSTAT_DATA_UINT64 },
	{ "mfu_hits",			KSTAT_DATA_UINT64 },
	{ "mfu_ghost_hits",		KSTAT_DATA_UINT64 },
	{ "deleted",			KSTAT_DATA_UINT64 },
	{ "mutex_miss",			KSTAT_DATA_UINT64 },
	{ "access_skip",		KSTAT_DATA_UINT64 },
	{ "evict_skip",			KSTAT_DATA_UINT64 },
	{ "evict_not_enough",		KSTAT_DATA_UINT64 },
	{ "evict_l2_cached",		KSTAT_DATA_UINT64 },
	{ "evict_l2_eligible",		KSTAT_DATA_UINT64 },
	{ "evict_l2_eligible_mfu",	KSTAT_DATA_UINT64 },
	{ "evict_l2_eligible_mru",	KSTAT_DATA_UINT64 },
	{ "evict_l2_ineligible",	KSTAT_DATA_UINT64 },
	{ "evict_l2_skip",		KSTAT_DATA_UINT64 },
	{ "hash_elements",		KSTAT_DATA_UINT64 },
	{ "hash_elements_max",		KSTAT_DATA_UINT64 },
	{ "hash_collisions",		KSTAT_DATA_UINT64 },
	{ "hash_chains",		KSTAT_DATA_UINT64 },
	{ "hash_chain_max",		KSTAT_DATA_UINT64 },
	{ "p",				KSTAT_DATA_UINT64 },
	{ "c",				KSTAT_DATA_UINT64 },
	{ "c_min",			KSTAT_DATA_UINT64 },
	{ "c_max",			KSTAT_DATA_UINT64 },
	{ "size",			KSTAT_DATA_UINT64 },
	{ "compressed_size",		KSTAT_DATA_UINT64 },
	{ "uncompressed_size",		KSTAT_DATA_UINT64 },
	{ "overhead_size",		KSTAT_DATA_UINT64 },
	{ "hdr_size",			KSTAT_DATA_UINT64 },
	{ "data_size",			KSTAT_DATA_UINT64 },
	{ "metadata_size",		KSTAT_DATA_UINT64 },
	{ "dbuf_size",			KSTAT_DATA_UINT64 },
	{ "dnode_size",			KSTAT_DATA_UINT64 },
	{ "bonus_size",			KSTAT_DATA_UINT64 },
#if defined(COMPAT_FREEBSD11)
	{ "other_size",			KSTAT_DATA_UINT64 },
#endif
	{ "anon_size",			KSTAT_DATA_UINT64 },
	{ "anon_evictable_data",	KSTAT_DATA_UINT64 },
	{ "anon_evictable_metadata",	KSTAT_DATA_UINT64 },
	{ "mru_size",			KSTAT_DATA_UINT64 },
	{ "mru_evictable_data",		KSTAT_DATA_UINT64 },
	{ "mru_evictable_metadata",	KSTAT_DATA_UINT64 },
	{ "mru_ghost_size",		KSTAT_DATA_UINT64 },
	{ "mru_ghost_evictable_data",	KSTAT_DATA_UINT64 },
	{ "mru_ghost_evictable_metadata", KSTAT_DATA_UINT64 },
	{ "mfu_size",			KSTAT_DATA_UINT64 },
	{ "mfu_evictable_data",		KSTAT_DATA_UINT64 },
	{ "mfu_evictable_metadata",	KSTAT_DATA_UINT64 },
	{ "mfu_ghost_size",		KSTAT_DATA_UINT64 },
	{ "mfu_ghost_evictable_data",	KSTAT_DATA_UINT64 },
	{ "mfu_ghost_evictable_metadata", KSTAT_DATA_UINT64 },
	{ "l2_hits",			KSTAT_DATA_UINT64 },
	{ "l2_misses",			KSTAT_DATA_UINT64 },
	{ "l2_prefetch_asize",		KSTAT_DATA_UINT64 },
	{ "l2_mru_asize",		KSTAT_DATA_UINT64 },
	{ "l2_mfu_asize",		KSTAT_DATA_UINT64 },
	{ "l2_bufc_data_asize",		KSTAT_DATA_UINT64 },
	{ "l2_bufc_metadata_asize",	KSTAT_DATA_UINT64 },
	{ "l2_feeds",			KSTAT_DATA_UINT64 },
	{ "l2_rw_clash",		KSTAT_DATA_UINT64 },
	{ "l2_read_bytes",		KSTAT_DATA_UINT64 },
	{ "l2_write_bytes",		KSTAT_DATA_UINT64 },
	{ "l2_writes_sent",		KSTAT_DATA_UINT64 },
	{ "l2_writes_done",		KSTAT_DATA_UINT64 },
	{ "l2_writes_error",		KSTAT_DATA_UINT64 },
	{ "l2_writes_lock_retry",	KSTAT_DATA_UINT64 },
	{ "l2_evict_lock_retry",	KSTAT_DATA_UINT64 },
	{ "l2_evict_reading",		KSTAT_DATA_UINT64 },
	{ "l2_evict_l1cached",		KSTAT_DATA_UINT64 },
	{ "l2_free_on_write",		KSTAT_DATA_UINT64 },
	{ "l2_abort_lowmem",		KSTAT_DATA_UINT64 },
	{ "l2_cksum_bad",		KSTAT_DATA_UINT64 },
	{ "l2_io_error",		KSTAT_DATA_UINT64 },
	{ "l2_size",			KSTAT_DATA_UINT64 },
	{ "l2_asize",			KSTAT_DATA_UINT64 },
	{ "l2_hdr_size",		KSTAT_DATA_UINT64 },
	{ "l2_log_blk_writes",		KSTAT_DATA_UINT64 },
	{ "l2_log_blk_avg_asize",	KSTAT_DATA_UINT64 },
	{ "l2_log_blk_asize",		KSTAT_DATA_UINT64 },
	{ "l2_log_blk_count",		KSTAT_DATA_UINT64 },
	{ "l2_data_to_meta_ratio",	KSTAT_DATA_UINT64 },
	{ "l2_rebuild_success",		KSTAT_DATA_UINT64 },
	{ "l2_rebuild_unsupported",	KSTAT_DATA_UINT64 },
	{ "l2_rebuild_io_errors",	KSTAT_DATA_UINT64 },
	{ "l2_rebuild_dh_errors",	KSTAT_DATA_UINT64 },
	{ "l2_rebuild_cksum_lb_errors",	KSTAT_DATA_UINT64 },
	{ "l2_rebuild_lowmem",		KSTAT_DATA_UINT64 },
	{ "l2_rebuild_size",		KSTAT_DATA_UINT64 },
	{ "l2_rebuild_asize",		KSTAT_DATA_UINT64 },
	{ "l2_rebuild_bufs",		KSTAT_DATA_UINT64 },
	{ "l2_rebuild_bufs_precached",	KSTAT_DATA_UINT64 },
	{ "l2_rebuild_log_blks",	KSTAT_DATA_UINT64 },
	{ "memory_throttle_count",	KSTAT_DATA_UINT64 },
	{ "memory_direct_count",	KSTAT_DATA_UINT64 },
	{ "memory_indirect_count",	KSTAT_DATA_UINT64 },
	{ "memory_all_bytes",		KSTAT_DATA_UINT64 },
	{ "memory_free_bytes",		KSTAT_DATA_UINT64 },
	{ "memory_available_bytes",	KSTAT_DATA_INT64 },
	{ "arc_no_grow",		KSTAT_DATA_UINT64 },
	{ "arc_tempreserve",		KSTAT_DATA_UINT64 },
	{ "arc_loaned_bytes",		KSTAT_DATA_UINT64 },
	{ "arc_prune",			KSTAT_DATA_UINT64 },
	{ "arc_meta_used",		KSTAT_DATA_UINT64 },
	{ "arc_meta_limit",		KSTAT_DATA_UINT64 },
	{ "arc_dnode_limit",		KSTAT_DATA_UINT64 },
	{ "arc_meta_max",		KSTAT_DATA_UINT64 },
	{ "arc_meta_min",		KSTAT_DATA_UINT64 },
	{ "async_upgrade_sync",		KSTAT_DATA_UINT64 },
	{ "demand_hit_predictive_prefetch", KSTAT_DATA_UINT64 },
	{ "demand_hit_prescient_prefetch", KSTAT_DATA_UINT64 },
	{ "arc_need_free",		KSTAT_DATA_UINT64 },
	{ "arc_sys_free",		KSTAT_DATA_UINT64 },
	{ "arc_raw_size",		KSTAT_DATA_UINT64 },
	{ "cached_only_in_progress",	KSTAT_DATA_UINT64 },
	{ "abd_chunk_waste_size",	KSTAT_DATA_UINT64 },
};

#define	ARCSTAT_MAX(stat, val) {					\
	uint64_t m;							\
	while ((val) > (m = arc_stats.stat.value.ui64) &&		\
	    (m != atomic_cas_64(&arc_stats.stat.value.ui64, m, (val))))	\
		continue;						\
}

#define	ARCSTAT_MAXSTAT(stat) \
	ARCSTAT_MAX(stat##_max, arc_stats.stat.value.ui64)

/*
 * We define a macro to allow ARC hits/misses to be easily broken down by
 * two separate conditions, giving a total of four different subtypes for
 * each of hits and misses (so eight statistics total).
 */
#define	ARCSTAT_CONDSTAT(cond1, stat1, notstat1, cond2, stat2, notstat2, stat) \
	if (cond1) {							\
		if (cond2) {						\
			ARCSTAT_BUMP(arcstat_##stat1##_##stat2##_##stat); \
		} else {						\
			ARCSTAT_BUMP(arcstat_##stat1##_##notstat2##_##stat); \
		}							\
	} else {							\
		if (cond2) {						\
			ARCSTAT_BUMP(arcstat_##notstat1##_##stat2##_##stat); \
		} else {						\
			ARCSTAT_BUMP(arcstat_##notstat1##_##notstat2##_##stat);\
		}							\
	}

/*
 * This macro allows us to use kstats as floating averages. Each time we
 * update this kstat, we first factor it and the update value by
 * ARCSTAT_AVG_FACTOR to shrink the new value's contribution to the overall
 * average. This macro assumes that integer loads and stores are atomic, but
 * is not safe for multiple writers updating the kstat in parallel (only the
 * last writer's update will remain).
 */
#define	ARCSTAT_F_AVG_FACTOR	3
#define	ARCSTAT_F_AVG(stat, value) \
	do { \
		uint64_t x = ARCSTAT(stat); \
		x = x - x / ARCSTAT_F_AVG_FACTOR + \
		    (value) / ARCSTAT_F_AVG_FACTOR; \
		ARCSTAT(stat) = x; \
		_NOTE(CONSTCOND) \
	} while (0)

kstat_t			*arc_ksp;
static arc_state_t	*arc_anon;
static arc_state_t	*arc_mru_ghost;
static arc_state_t	*arc_mfu_ghost;
static arc_state_t	*arc_l2c_only;

arc_state_t	*arc_mru;
arc_state_t	*arc_mfu;

/*
 * There are several ARC variables that are critical to export as kstats --
 * but we don't want to have to grovel around in the kstat whenever we wish to
 * manipulate them.  For these variables, we therefore define them to be in
 * terms of the statistic variable.  This assures that we are not introducing
 * the possibility of inconsistency by having shadow copies of the variables,
 * while still allowing the code to be readable.
 */
#define	arc_tempreserve	ARCSTAT(arcstat_tempreserve)
#define	arc_loaned_bytes	ARCSTAT(arcstat_loaned_bytes)
#define	arc_meta_limit	ARCSTAT(arcstat_meta_limit) /* max size for metadata */
/* max size for dnodes */
#define	arc_dnode_size_limit	ARCSTAT(arcstat_dnode_limit)
#define	arc_meta_min	ARCSTAT(arcstat_meta_min) /* min size for metadata */
#define	arc_meta_max	ARCSTAT(arcstat_meta_max) /* max size of metadata */
#define	arc_need_free	ARCSTAT(arcstat_need_free) /* waiting to be evicted */

/* size of all b_rabd's in entire arc */
#define	arc_raw_size	ARCSTAT(arcstat_raw_size)
/* compressed size of entire arc */
#define	arc_compressed_size	ARCSTAT(arcstat_compressed_size)
/* uncompressed size of entire arc */
#define	arc_uncompressed_size	ARCSTAT(arcstat_uncompressed_size)
/* number of bytes in the arc from arc_buf_t's */
#define	arc_overhead_size	ARCSTAT(arcstat_overhead_size)

/*
 * There are also some ARC variables that we want to export, but that are
 * updated so often that having the canonical representation be the statistic
 * variable causes a performance bottleneck. We want to use aggsum_t's for these
 * instead, but still be able to export the kstat in the same way as before.
 * The solution is to always use the aggsum version, except in the kstat update
 * callback.
 */
aggsum_t arc_size;
aggsum_t arc_meta_used;
aggsum_t astat_data_size;
aggsum_t astat_metadata_size;
aggsum_t astat_dbuf_size;
aggsum_t astat_dnode_size;
aggsum_t astat_bonus_size;
aggsum_t astat_hdr_size;
aggsum_t astat_l2_hdr_size;
aggsum_t astat_abd_chunk_waste_size;

hrtime_t arc_growtime;
list_t arc_prune_list;
kmutex_t arc_prune_mtx;
taskq_t *arc_prune_taskq;

#define	GHOST_STATE(state)	\
	((state) == arc_mru_ghost || (state) == arc_mfu_ghost ||	\
	(state) == arc_l2c_only)

#define	HDR_IN_HASH_TABLE(hdr)	((hdr)->b_flags & ARC_FLAG_IN_HASH_TABLE)
#define	HDR_IO_IN_PROGRESS(hdr)	((hdr)->b_flags & ARC_FLAG_IO_IN_PROGRESS)
#define	HDR_IO_ERROR(hdr)	((hdr)->b_flags & ARC_FLAG_IO_ERROR)
#define	HDR_PREFETCH(hdr)	((hdr)->b_flags & ARC_FLAG_PREFETCH)
#define	HDR_PRESCIENT_PREFETCH(hdr)	\
	((hdr)->b_flags & ARC_FLAG_PRESCIENT_PREFETCH)
#define	HDR_COMPRESSION_ENABLED(hdr)	\
	((hdr)->b_flags & ARC_FLAG_COMPRESSED_ARC)

#define	HDR_L2CACHE(hdr)	((hdr)->b_flags & ARC_FLAG_L2CACHE)
#define	HDR_L2_READING(hdr)	\
	(((hdr)->b_flags & ARC_FLAG_IO_IN_PROGRESS) &&	\
	((hdr)->b_flags & ARC_FLAG_HAS_L2HDR))
#define	HDR_L2_WRITING(hdr)	((hdr)->b_flags & ARC_FLAG_L2_WRITING)
#define	HDR_L2_EVICTED(hdr)	((hdr)->b_flags & ARC_FLAG_L2_EVICTED)
#define	HDR_L2_WRITE_HEAD(hdr)	((hdr)->b_flags & ARC_FLAG_L2_WRITE_HEAD)
#define	HDR_PROTECTED(hdr)	((hdr)->b_flags & ARC_FLAG_PROTECTED)
#define	HDR_NOAUTH(hdr)		((hdr)->b_flags & ARC_FLAG_NOAUTH)
#define	HDR_SHARED_DATA(hdr)	((hdr)->b_flags & ARC_FLAG_SHARED_DATA)

#define	HDR_ISTYPE_METADATA(hdr)	\
	((hdr)->b_flags & ARC_FLAG_BUFC_METADATA)
#define	HDR_ISTYPE_DATA(hdr)	(!HDR_ISTYPE_METADATA(hdr))

#define	HDR_HAS_L1HDR(hdr)	((hdr)->b_flags & ARC_FLAG_HAS_L1HDR)
#define	HDR_HAS_L2HDR(hdr)	((hdr)->b_flags & ARC_FLAG_HAS_L2HDR)
#define	HDR_HAS_RABD(hdr)	\
	(HDR_HAS_L1HDR(hdr) && HDR_PROTECTED(hdr) &&	\
	(hdr)->b_crypt_hdr.b_rabd != NULL)
#define	HDR_ENCRYPTED(hdr)	\
	(HDR_PROTECTED(hdr) && DMU_OT_IS_ENCRYPTED((hdr)->b_crypt_hdr.b_ot))
#define	HDR_AUTHENTICATED(hdr)	\
	(HDR_PROTECTED(hdr) && !DMU_OT_IS_ENCRYPTED((hdr)->b_crypt_hdr.b_ot))

/* For storing compression mode in b_flags */
#define	HDR_COMPRESS_OFFSET	(highbit64(ARC_FLAG_COMPRESS_0) - 1)

#define	HDR_GET_COMPRESS(hdr)	((enum zio_compress)BF32_GET((hdr)->b_flags, \
	HDR_COMPRESS_OFFSET, SPA_COMPRESSBITS))
#define	HDR_SET_COMPRESS(hdr, cmp) BF32_SET((hdr)->b_flags, \
	HDR_COMPRESS_OFFSET, SPA_COMPRESSBITS, (cmp));

#define	ARC_BUF_LAST(buf)	((buf)->b_next == NULL)
#define	ARC_BUF_SHARED(buf)	((buf)->b_flags & ARC_BUF_FLAG_SHARED)
#define	ARC_BUF_COMPRESSED(buf)	((buf)->b_flags & ARC_BUF_FLAG_COMPRESSED)
#define	ARC_BUF_ENCRYPTED(buf)	((buf)->b_flags & ARC_BUF_FLAG_ENCRYPTED)

/*
 * Other sizes
 */

#define	HDR_FULL_CRYPT_SIZE ((int64_t)sizeof (arc_buf_hdr_t))
#define	HDR_FULL_SIZE ((int64_t)offsetof(arc_buf_hdr_t, b_crypt_hdr))
#define	HDR_L2ONLY_SIZE ((int64_t)offsetof(arc_buf_hdr_t, b_l1hdr))

/*
 * Hash table routines
 */

#define	HT_LOCK_ALIGN	64
#define	HT_LOCK_PAD	(P2NPHASE(sizeof (kmutex_t), (HT_LOCK_ALIGN)))

struct ht_lock {
	kmutex_t	ht_lock;
#ifdef _KERNEL
	unsigned char	pad[HT_LOCK_PAD];
#endif
};

#define	BUF_LOCKS 8192
typedef struct buf_hash_table {
	uint64_t ht_mask;
	arc_buf_hdr_t **ht_table;
	struct ht_lock ht_locks[BUF_LOCKS];
} buf_hash_table_t;

static buf_hash_table_t buf_hash_table;

#define	BUF_HASH_INDEX(spa, dva, birth) \
	(buf_hash(spa, dva, birth) & buf_hash_table.ht_mask)
#define	BUF_HASH_LOCK_NTRY(idx) (buf_hash_table.ht_locks[idx & (BUF_LOCKS-1)])
#define	BUF_HASH_LOCK(idx)	(&(BUF_HASH_LOCK_NTRY(idx).ht_lock))
#define	HDR_LOCK(hdr) \
	(BUF_HASH_LOCK(BUF_HASH_INDEX(hdr->b_spa, &hdr->b_dva, hdr->b_birth)))

uint64_t zfs_crc64_table[256];

/*
 * Level 2 ARC
 */

#define	L2ARC_WRITE_SIZE	(8 * 1024 * 1024)	/* initial write max */
#define	L2ARC_HEADROOM		2			/* num of writes */

/*
 * If we discover during ARC scan any buffers to be compressed, we boost
 * our headroom for the next scanning cycle by this percentage multiple.
 */
#define	L2ARC_HEADROOM_BOOST	200
#define	L2ARC_FEED_SECS		1		/* caching interval secs */
#define	L2ARC_FEED_MIN_MS	200		/* min caching interval ms */

/*
 * We can feed L2ARC from two states of ARC buffers, mru and mfu,
 * and each of the state has two types: data and metadata.
 */
#define	L2ARC_FEED_TYPES	4

#define	l2arc_writes_sent	ARCSTAT(arcstat_l2_writes_sent)
#define	l2arc_writes_done	ARCSTAT(arcstat_l2_writes_done)

/* L2ARC Performance Tunables */
unsigned long l2arc_write_max = L2ARC_WRITE_SIZE;	/* def max write size */
unsigned long l2arc_write_boost = L2ARC_WRITE_SIZE;	/* extra warmup write */
unsigned long l2arc_headroom = L2ARC_HEADROOM;		/* # of dev writes */
unsigned long l2arc_headroom_boost = L2ARC_HEADROOM_BOOST;
unsigned long l2arc_feed_secs = L2ARC_FEED_SECS;	/* interval seconds */
unsigned long l2arc_feed_min_ms = L2ARC_FEED_MIN_MS;	/* min interval msecs */
int l2arc_noprefetch = B_TRUE;			/* don't cache prefetch bufs */
int l2arc_feed_again = B_TRUE;			/* turbo warmup */
int l2arc_norw = B_FALSE;			/* no reads during writes */
int l2arc_meta_percent = 33;			/* limit on headers size */

/*
 * L2ARC Internals
 */
static list_t L2ARC_dev_list;			/* device list */
static list_t *l2arc_dev_list;			/* device list pointer */
static kmutex_t l2arc_dev_mtx;			/* device list mutex */
static l2arc_dev_t *l2arc_dev_last;		/* last device used */
static list_t L2ARC_free_on_write;		/* free after write buf list */
static list_t *l2arc_free_on_write;		/* free after write list ptr */
static kmutex_t l2arc_free_on_write_mtx;	/* mutex for list */
static uint64_t l2arc_ndev;			/* number of devices */

typedef struct l2arc_read_callback {
	arc_buf_hdr_t		*l2rcb_hdr;		/* read header */
	blkptr_t		l2rcb_bp;		/* original blkptr */
	zbookmark_phys_t	l2rcb_zb;		/* original bookmark */
	int			l2rcb_flags;		/* original flags */
	abd_t			*l2rcb_abd;		/* temporary buffer */
} l2arc_read_callback_t;

typedef struct l2arc_data_free {
	/* protected by l2arc_free_on_write_mtx */
	abd_t		*l2df_abd;
	size_t		l2df_size;
	arc_buf_contents_t l2df_type;
	list_node_t	l2df_list_node;
} l2arc_data_free_t;

typedef enum arc_fill_flags {
	ARC_FILL_LOCKED		= 1 << 0, /* hdr lock is held */
	ARC_FILL_COMPRESSED	= 1 << 1, /* fill with compressed data */
	ARC_FILL_ENCRYPTED	= 1 << 2, /* fill with encrypted data */
	ARC_FILL_NOAUTH		= 1 << 3, /* don't attempt to authenticate */
	ARC_FILL_IN_PLACE	= 1 << 4  /* fill in place (special case) */
} arc_fill_flags_t;

static kmutex_t l2arc_feed_thr_lock;
static kcondvar_t l2arc_feed_thr_cv;
static uint8_t l2arc_thread_exit;

static kmutex_t l2arc_rebuild_thr_lock;
static kcondvar_t l2arc_rebuild_thr_cv;

enum arc_hdr_alloc_flags {
	ARC_HDR_ALLOC_RDATA = 0x1,
	ARC_HDR_DO_ADAPT = 0x2,
};


static abd_t *arc_get_data_abd(arc_buf_hdr_t *, uint64_t, void *, boolean_t);
static void *arc_get_data_buf(arc_buf_hdr_t *, uint64_t, void *);
static void arc_get_data_impl(arc_buf_hdr_t *, uint64_t, void *, boolean_t);
static void arc_free_data_abd(arc_buf_hdr_t *, abd_t *, uint64_t, void *);
static void arc_free_data_buf(arc_buf_hdr_t *, void *, uint64_t, void *);
static void arc_free_data_impl(arc_buf_hdr_t *hdr, uint64_t size, void *tag);
static void arc_hdr_free_abd(arc_buf_hdr_t *, boolean_t);
static void arc_hdr_alloc_abd(arc_buf_hdr_t *, int);
static void arc_access(arc_buf_hdr_t *, kmutex_t *);
static void arc_buf_watch(arc_buf_t *);

static arc_buf_contents_t arc_buf_type(arc_buf_hdr_t *);
static uint32_t arc_bufc_to_flags(arc_buf_contents_t);
static inline void arc_hdr_set_flags(arc_buf_hdr_t *hdr, arc_flags_t flags);
static inline void arc_hdr_clear_flags(arc_buf_hdr_t *hdr, arc_flags_t flags);

static boolean_t l2arc_write_eligible(uint64_t, arc_buf_hdr_t *);
static void l2arc_read_done(zio_t *);
static void l2arc_do_free_on_write(void);
static void l2arc_hdr_arcstats_update(arc_buf_hdr_t *hdr, boolean_t incr,
    boolean_t state_only);

#define	l2arc_hdr_arcstats_increment(hdr) \
	l2arc_hdr_arcstats_update((hdr), B_TRUE, B_FALSE)
#define	l2arc_hdr_arcstats_decrement(hdr) \
	l2arc_hdr_arcstats_update((hdr), B_FALSE, B_FALSE)
#define	l2arc_hdr_arcstats_increment_state(hdr) \
	l2arc_hdr_arcstats_update((hdr), B_TRUE, B_TRUE)
#define	l2arc_hdr_arcstats_decrement_state(hdr) \
	l2arc_hdr_arcstats_update((hdr), B_FALSE, B_TRUE)

/*
 * l2arc_mfuonly : A ZFS module parameter that controls whether only MFU
 * 		metadata and data are cached from ARC into L2ARC.
 */
int l2arc_mfuonly = 0;

/*
 * L2ARC TRIM
 * l2arc_trim_ahead : A ZFS module parameter that controls how much ahead of
 * 		the current write size (l2arc_write_max) we should TRIM if we
 * 		have filled the device. It is defined as a percentage of the
 * 		write size. If set to 100 we trim twice the space required to
 * 		accommodate upcoming writes. A minimum of 64MB will be trimmed.
 * 		It also enables TRIM of the whole L2ARC device upon creation or
 * 		addition to an existing pool or if the header of the device is
 * 		invalid upon importing a pool or onlining a cache device. The
 * 		default is 0, which disables TRIM on L2ARC altogether as it can
 * 		put significant stress on the underlying storage devices. This
 * 		will vary depending of how well the specific device handles
 * 		these commands.
 */
unsigned long l2arc_trim_ahead = 0;

/*
 * Performance tuning of L2ARC persistence:
 *
 * l2arc_rebuild_enabled : A ZFS module parameter that controls whether adding
 * 		an L2ARC device (either at pool import or later) will attempt
 * 		to rebuild L2ARC buffer contents.
 * l2arc_rebuild_blocks_min_l2size : A ZFS module parameter that controls
 * 		whether log blocks are written to the L2ARC device. If the L2ARC
 * 		device is less than 1GB, the amount of data l2arc_evict()
 * 		evicts is significant compared to the amount of restored L2ARC
 * 		data. In this case do not write log blocks in L2ARC in order
 * 		not to waste space.
 */
int l2arc_rebuild_enabled = B_TRUE;
unsigned long l2arc_rebuild_blocks_min_l2size = 1024 * 1024 * 1024;

/* L2ARC persistence rebuild control routines. */
void l2arc_rebuild_vdev(vdev_t *vd, boolean_t reopen);
static void l2arc_dev_rebuild_thread(void *arg);
static int l2arc_rebuild(l2arc_dev_t *dev);

/* L2ARC persistence read I/O routines. */
static int l2arc_dev_hdr_read(l2arc_dev_t *dev);
static int l2arc_log_blk_read(l2arc_dev_t *dev,
    const l2arc_log_blkptr_t *this_lp, const l2arc_log_blkptr_t *next_lp,
    l2arc_log_blk_phys_t *this_lb, l2arc_log_blk_phys_t *next_lb,
    zio_t *this_io, zio_t **next_io);
static zio_t *l2arc_log_blk_fetch(vdev_t *vd,
    const l2arc_log_blkptr_t *lp, l2arc_log_blk_phys_t *lb);
static void l2arc_log_blk_fetch_abort(zio_t *zio);

/* L2ARC persistence block restoration routines. */
static void l2arc_log_blk_restore(l2arc_dev_t *dev,
    const l2arc_log_blk_phys_t *lb, uint64_t lb_asize);
static void l2arc_hdr_restore(const l2arc_log_ent_phys_t *le,
    l2arc_dev_t *dev);

/* L2ARC persistence write I/O routines. */
static void l2arc_log_blk_commit(l2arc_dev_t *dev, zio_t *pio,
    l2arc_write_callback_t *cb);

/* L2ARC persistence auxiliary routines. */
boolean_t l2arc_log_blkptr_valid(l2arc_dev_t *dev,
    const l2arc_log_blkptr_t *lbp);
static boolean_t l2arc_log_blk_insert(l2arc_dev_t *dev,
    const arc_buf_hdr_t *ab);
boolean_t l2arc_range_check_overlap(uint64_t bottom,
    uint64_t top, uint64_t check);
static void l2arc_blk_fetch_done(zio_t *zio);
static inline uint64_t
    l2arc_log_blk_overhead(uint64_t write_sz, l2arc_dev_t *dev);

/*
 * We use Cityhash for this. It's fast, and has good hash properties without
 * requiring any large static buffers.
 */
static uint64_t
buf_hash(uint64_t spa, const dva_t *dva, uint64_t birth)
{
	return (cityhash4(spa, dva->dva_word[0], dva->dva_word[1], birth));
}

#define	HDR_EMPTY(hdr)						\
	((hdr)->b_dva.dva_word[0] == 0 &&			\
	(hdr)->b_dva.dva_word[1] == 0)

#define	HDR_EMPTY_OR_LOCKED(hdr)				\
	(HDR_EMPTY(hdr) || MUTEX_HELD(HDR_LOCK(hdr)))

#define	HDR_EQUAL(spa, dva, birth, hdr)				\
	((hdr)->b_dva.dva_word[0] == (dva)->dva_word[0]) &&	\
	((hdr)->b_dva.dva_word[1] == (dva)->dva_word[1]) &&	\
	((hdr)->b_birth == birth) && ((hdr)->b_spa == spa)

static void
buf_discard_identity(arc_buf_hdr_t *hdr)
{
	hdr->b_dva.dva_word[0] = 0;
	hdr->b_dva.dva_word[1] = 0;
	hdr->b_birth = 0;
}

static arc_buf_hdr_t *
buf_hash_find(uint64_t spa, const blkptr_t *bp, kmutex_t **lockp)
{
	const dva_t *dva = BP_IDENTITY(bp);
	uint64_t birth = BP_PHYSICAL_BIRTH(bp);
	uint64_t idx = BUF_HASH_INDEX(spa, dva, birth);
	kmutex_t *hash_lock = BUF_HASH_LOCK(idx);
	arc_buf_hdr_t *hdr;

	mutex_enter(hash_lock);
	for (hdr = buf_hash_table.ht_table[idx]; hdr != NULL;
	    hdr = hdr->b_hash_next) {
		if (HDR_EQUAL(spa, dva, birth, hdr)) {
			*lockp = hash_lock;
			return (hdr);
		}
	}
	mutex_exit(hash_lock);
	*lockp = NULL;
	return (NULL);
}

/*
 * Insert an entry into the hash table.  If there is already an element
 * equal to elem in the hash table, then the already existing element
 * will be returned and the new element will not be inserted.
 * Otherwise returns NULL.
 * If lockp == NULL, the caller is assumed to already hold the hash lock.
 */
static arc_buf_hdr_t *
buf_hash_insert(arc_buf_hdr_t *hdr, kmutex_t **lockp)
{
	uint64_t idx = BUF_HASH_INDEX(hdr->b_spa, &hdr->b_dva, hdr->b_birth);
	kmutex_t *hash_lock = BUF_HASH_LOCK(idx);
	arc_buf_hdr_t *fhdr;
	uint32_t i;

	ASSERT(!DVA_IS_EMPTY(&hdr->b_dva));
	ASSERT(hdr->b_birth != 0);
	ASSERT(!HDR_IN_HASH_TABLE(hdr));

	if (lockp != NULL) {
		*lockp = hash_lock;
		mutex_enter(hash_lock);
	} else {
		ASSERT(MUTEX_HELD(hash_lock));
	}

	for (fhdr = buf_hash_table.ht_table[idx], i = 0; fhdr != NULL;
	    fhdr = fhdr->b_hash_next, i++) {
		if (HDR_EQUAL(hdr->b_spa, &hdr->b_dva, hdr->b_birth, fhdr))
			return (fhdr);
	}

	hdr->b_hash_next = buf_hash_table.ht_table[idx];
	buf_hash_table.ht_table[idx] = hdr;
	arc_hdr_set_flags(hdr, ARC_FLAG_IN_HASH_TABLE);

	/* collect some hash table performance data */
	if (i > 0) {
		ARCSTAT_BUMP(arcstat_hash_collisions);
		if (i == 1)
			ARCSTAT_BUMP(arcstat_hash_chains);

		ARCSTAT_MAX(arcstat_hash_chain_max, i);
	}

	ARCSTAT_BUMP(arcstat_hash_elements);
	ARCSTAT_MAXSTAT(arcstat_hash_elements);

	return (NULL);
}

static void
buf_hash_remove(arc_buf_hdr_t *hdr)
{
	arc_buf_hdr_t *fhdr, **hdrp;
	uint64_t idx = BUF_HASH_INDEX(hdr->b_spa, &hdr->b_dva, hdr->b_birth);

	ASSERT(MUTEX_HELD(BUF_HASH_LOCK(idx)));
	ASSERT(HDR_IN_HASH_TABLE(hdr));

	hdrp = &buf_hash_table.ht_table[idx];
	while ((fhdr = *hdrp) != hdr) {
		ASSERT3P(fhdr, !=, NULL);
		hdrp = &fhdr->b_hash_next;
	}
	*hdrp = hdr->b_hash_next;
	hdr->b_hash_next = NULL;
	arc_hdr_clear_flags(hdr, ARC_FLAG_IN_HASH_TABLE);

	/* collect some hash table performance data */
	ARCSTAT_BUMPDOWN(arcstat_hash_elements);

	if (buf_hash_table.ht_table[idx] &&
	    buf_hash_table.ht_table[idx]->b_hash_next == NULL)
		ARCSTAT_BUMPDOWN(arcstat_hash_chains);
}

/*
 * Global data structures and functions for the buf kmem cache.
 */

static kmem_cache_t *hdr_full_cache;
static kmem_cache_t *hdr_full_crypt_cache;
static kmem_cache_t *hdr_l2only_cache;
static kmem_cache_t *buf_cache;

static void
buf_fini(void)
{
	int i;

#if defined(_KERNEL)
	/*
	 * Large allocations which do not require contiguous pages
	 * should be using vmem_free() in the linux kernel\
	 */
	vmem_free(buf_hash_table.ht_table,
	    (buf_hash_table.ht_mask + 1) * sizeof (void *));
#else
	kmem_free(buf_hash_table.ht_table,
	    (buf_hash_table.ht_mask + 1) * sizeof (void *));
#endif
	for (i = 0; i < BUF_LOCKS; i++)
		mutex_destroy(&buf_hash_table.ht_locks[i].ht_lock);
	kmem_cache_destroy(hdr_full_cache);
	kmem_cache_destroy(hdr_full_crypt_cache);
	kmem_cache_destroy(hdr_l2only_cache);
	kmem_cache_destroy(buf_cache);
}

/*
 * Constructor callback - called when the cache is empty
 * and a new buf is requested.
 */
/* ARGSUSED */
static int
hdr_full_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_hdr_t *hdr = vbuf;

	bzero(hdr, HDR_FULL_SIZE);
	hdr->b_l1hdr.b_byteswap = DMU_BSWAP_NUMFUNCS;
	cv_init(&hdr->b_l1hdr.b_cv, NULL, CV_DEFAULT, NULL);
	zfs_refcount_create(&hdr->b_l1hdr.b_refcnt);
	mutex_init(&hdr->b_l1hdr.b_freeze_lock, NULL, MUTEX_DEFAULT, NULL);
	list_link_init(&hdr->b_l1hdr.b_arc_node);
	list_link_init(&hdr->b_l2hdr.b_l2node);
	multilist_link_init(&hdr->b_l1hdr.b_arc_node);
	arc_space_consume(HDR_FULL_SIZE, ARC_SPACE_HDRS);

	return (0);
}

/* ARGSUSED */
static int
hdr_full_crypt_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_hdr_t *hdr = vbuf;

	hdr_full_cons(vbuf, unused, kmflag);
	bzero(&hdr->b_crypt_hdr, sizeof (hdr->b_crypt_hdr));
	arc_space_consume(sizeof (hdr->b_crypt_hdr), ARC_SPACE_HDRS);

	return (0);
}

/* ARGSUSED */
static int
hdr_l2only_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_hdr_t *hdr = vbuf;

	bzero(hdr, HDR_L2ONLY_SIZE);
	arc_space_consume(HDR_L2ONLY_SIZE, ARC_SPACE_L2HDRS);

	return (0);
}

/* ARGSUSED */
static int
buf_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_t *buf = vbuf;

	bzero(buf, sizeof (arc_buf_t));
	mutex_init(&buf->b_evict_lock, NULL, MUTEX_DEFAULT, NULL);
	arc_space_consume(sizeof (arc_buf_t), ARC_SPACE_HDRS);

	return (0);
}

/*
 * Destructor callback - called when a cached buf is
 * no longer required.
 */
/* ARGSUSED */
static void
hdr_full_dest(void *vbuf, void *unused)
{
	arc_buf_hdr_t *hdr = vbuf;

	ASSERT(HDR_EMPTY(hdr));
	cv_destroy(&hdr->b_l1hdr.b_cv);
	zfs_refcount_destroy(&hdr->b_l1hdr.b_refcnt);
	mutex_destroy(&hdr->b_l1hdr.b_freeze_lock);
	ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
	arc_space_return(HDR_FULL_SIZE, ARC_SPACE_HDRS);
}

/* ARGSUSED */
static void
hdr_full_crypt_dest(void *vbuf, void *unused)
{
	arc_buf_hdr_t *hdr = vbuf;

	hdr_full_dest(vbuf, unused);
	arc_space_return(sizeof (hdr->b_crypt_hdr), ARC_SPACE_HDRS);
}

/* ARGSUSED */
static void
hdr_l2only_dest(void *vbuf, void *unused)
{
	arc_buf_hdr_t *hdr __maybe_unused = vbuf;

	ASSERT(HDR_EMPTY(hdr));
	arc_space_return(HDR_L2ONLY_SIZE, ARC_SPACE_L2HDRS);
}

/* ARGSUSED */
static void
buf_dest(void *vbuf, void *unused)
{
	arc_buf_t *buf = vbuf;

	mutex_destroy(&buf->b_evict_lock);
	arc_space_return(sizeof (arc_buf_t), ARC_SPACE_HDRS);
}

static void
buf_init(void)
{
	uint64_t *ct = NULL;
	uint64_t hsize = 1ULL << 12;
	int i, j;

	/*
	 * The hash table is big enough to fill all of physical memory
	 * with an average block size of zfs_arc_average_blocksize (default 8K).
	 * By default, the table will take up
	 * totalmem * sizeof(void*) / 8K (1MB per GB with 8-byte pointers).
	 */
	while (hsize * zfs_arc_average_blocksize < arc_all_memory())
		hsize <<= 1;
retry:
	buf_hash_table.ht_mask = hsize - 1;
#if defined(_KERNEL)
	/*
	 * Large allocations which do not require contiguous pages
	 * should be using vmem_alloc() in the linux kernel
	 */
	buf_hash_table.ht_table =
	    vmem_zalloc(hsize * sizeof (void*), KM_SLEEP);
#else
	buf_hash_table.ht_table =
	    kmem_zalloc(hsize * sizeof (void*), KM_NOSLEEP);
#endif
	if (buf_hash_table.ht_table == NULL) {
		ASSERT(hsize > (1ULL << 8));
		hsize >>= 1;
		goto retry;
	}

	hdr_full_cache = kmem_cache_create("arc_buf_hdr_t_full", HDR_FULL_SIZE,
	    0, hdr_full_cons, hdr_full_dest, NULL, NULL, NULL, 0);
	hdr_full_crypt_cache = kmem_cache_create("arc_buf_hdr_t_full_crypt",
	    HDR_FULL_CRYPT_SIZE, 0, hdr_full_crypt_cons, hdr_full_crypt_dest,
	    NULL, NULL, NULL, 0);
	hdr_l2only_cache = kmem_cache_create("arc_buf_hdr_t_l2only",
	    HDR_L2ONLY_SIZE, 0, hdr_l2only_cons, hdr_l2only_dest, NULL,
	    NULL, NULL, 0);
	buf_cache = kmem_cache_create("arc_buf_t", sizeof (arc_buf_t),
	    0, buf_cons, buf_dest, NULL, NULL, NULL, 0);

	for (i = 0; i < 256; i++)
		for (ct = zfs_crc64_table + i, *ct = i, j = 8; j > 0; j--)
			*ct = (*ct >> 1) ^ (-(*ct & 1) & ZFS_CRC64_POLY);

	for (i = 0; i < BUF_LOCKS; i++) {
		mutex_init(&buf_hash_table.ht_locks[i].ht_lock,
		    NULL, MUTEX_DEFAULT, NULL);
	}
}

#define	ARC_MINTIME	(hz>>4) /* 62 ms */

/*
 * This is the size that the buf occupies in memory. If the buf is compressed,
 * it will correspond to the compressed size. You should use this method of
 * getting the buf size unless you explicitly need the logical size.
 */
uint64_t
arc_buf_size(arc_buf_t *buf)
{
	return (ARC_BUF_COMPRESSED(buf) ?
	    HDR_GET_PSIZE(buf->b_hdr) : HDR_GET_LSIZE(buf->b_hdr));
}

uint64_t
arc_buf_lsize(arc_buf_t *buf)
{
	return (HDR_GET_LSIZE(buf->b_hdr));
}

/*
 * This function will return B_TRUE if the buffer is encrypted in memory.
 * This buffer can be decrypted by calling arc_untransform().
 */
boolean_t
arc_is_encrypted(arc_buf_t *buf)
{
	return (ARC_BUF_ENCRYPTED(buf) != 0);
}

/*
 * Returns B_TRUE if the buffer represents data that has not had its MAC
 * verified yet.
 */
boolean_t
arc_is_unauthenticated(arc_buf_t *buf)
{
	return (HDR_NOAUTH(buf->b_hdr) != 0);
}

void
arc_get_raw_params(arc_buf_t *buf, boolean_t *byteorder, uint8_t *salt,
    uint8_t *iv, uint8_t *mac)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(HDR_PROTECTED(hdr));

	bcopy(hdr->b_crypt_hdr.b_salt, salt, ZIO_DATA_SALT_LEN);
	bcopy(hdr->b_crypt_hdr.b_iv, iv, ZIO_DATA_IV_LEN);
	bcopy(hdr->b_crypt_hdr.b_mac, mac, ZIO_DATA_MAC_LEN);
	*byteorder = (hdr->b_l1hdr.b_byteswap == DMU_BSWAP_NUMFUNCS) ?
	    ZFS_HOST_BYTEORDER : !ZFS_HOST_BYTEORDER;
}

/*
 * Indicates how this buffer is compressed in memory. If it is not compressed
 * the value will be ZIO_COMPRESS_OFF. It can be made normally readable with
 * arc_untransform() as long as it is also unencrypted.
 */
enum zio_compress
arc_get_compression(arc_buf_t *buf)
{
	return (ARC_BUF_COMPRESSED(buf) ?
	    HDR_GET_COMPRESS(buf->b_hdr) : ZIO_COMPRESS_OFF);
}

/*
 * Return the compression algorithm used to store this data in the ARC. If ARC
 * compression is enabled or this is an encrypted block, this will be the same
 * as what's used to store it on-disk. Otherwise, this will be ZIO_COMPRESS_OFF.
 */
static inline enum zio_compress
arc_hdr_get_compress(arc_buf_hdr_t *hdr)
{
	return (HDR_COMPRESSION_ENABLED(hdr) ?
	    HDR_GET_COMPRESS(hdr) : ZIO_COMPRESS_OFF);
}

uint8_t
arc_get_complevel(arc_buf_t *buf)
{
	return (buf->b_hdr->b_complevel);
}

static inline boolean_t
arc_buf_is_shared(arc_buf_t *buf)
{
	boolean_t shared = (buf->b_data != NULL &&
	    buf->b_hdr->b_l1hdr.b_pabd != NULL &&
	    abd_is_linear(buf->b_hdr->b_l1hdr.b_pabd) &&
	    buf->b_data == abd_to_buf(buf->b_hdr->b_l1hdr.b_pabd));
	IMPLY(shared, HDR_SHARED_DATA(buf->b_hdr));
	IMPLY(shared, ARC_BUF_SHARED(buf));
	IMPLY(shared, ARC_BUF_COMPRESSED(buf) || ARC_BUF_LAST(buf));

	/*
	 * It would be nice to assert arc_can_share() too, but the "hdr isn't
	 * already being shared" requirement prevents us from doing that.
	 */

	return (shared);
}

/*
 * Free the checksum associated with this header. If there is no checksum, this
 * is a no-op.
 */
static inline void
arc_cksum_free(arc_buf_hdr_t *hdr)
{
	ASSERT(HDR_HAS_L1HDR(hdr));

	mutex_enter(&hdr->b_l1hdr.b_freeze_lock);
	if (hdr->b_l1hdr.b_freeze_cksum != NULL) {
		kmem_free(hdr->b_l1hdr.b_freeze_cksum, sizeof (zio_cksum_t));
		hdr->b_l1hdr.b_freeze_cksum = NULL;
	}
	mutex_exit(&hdr->b_l1hdr.b_freeze_lock);
}

/*
 * Return true iff at least one of the bufs on hdr is not compressed.
 * Encrypted buffers count as compressed.
 */
static boolean_t
arc_hdr_has_uncompressed_buf(arc_buf_hdr_t *hdr)
{
	ASSERT(hdr->b_l1hdr.b_state == arc_anon || HDR_EMPTY_OR_LOCKED(hdr));

	for (arc_buf_t *b = hdr->b_l1hdr.b_buf; b != NULL; b = b->b_next) {
		if (!ARC_BUF_COMPRESSED(b)) {
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}


/*
 * If we've turned on the ZFS_DEBUG_MODIFY flag, verify that the buf's data
 * matches the checksum that is stored in the hdr. If there is no checksum,
 * or if the buf is compressed, this is a no-op.
 */
static void
arc_cksum_verify(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;
	zio_cksum_t zc;

	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		return;

	if (ARC_BUF_COMPRESSED(buf))
		return;

	ASSERT(HDR_HAS_L1HDR(hdr));

	mutex_enter(&hdr->b_l1hdr.b_freeze_lock);

	if (hdr->b_l1hdr.b_freeze_cksum == NULL || HDR_IO_ERROR(hdr)) {
		mutex_exit(&hdr->b_l1hdr.b_freeze_lock);
		return;
	}

	fletcher_2_native(buf->b_data, arc_buf_size(buf), NULL, &zc);
	if (!ZIO_CHECKSUM_EQUAL(*hdr->b_l1hdr.b_freeze_cksum, zc))
		panic("buffer modified while frozen!");
	mutex_exit(&hdr->b_l1hdr.b_freeze_lock);
}

/*
 * This function makes the assumption that data stored in the L2ARC
 * will be transformed exactly as it is in the main pool. Because of
 * this we can verify the checksum against the reading process's bp.
 */
static boolean_t
arc_cksum_is_equal(arc_buf_hdr_t *hdr, zio_t *zio)
{
	ASSERT(!BP_IS_EMBEDDED(zio->io_bp));
	VERIFY3U(BP_GET_PSIZE(zio->io_bp), ==, HDR_GET_PSIZE(hdr));

	/*
	 * Block pointers always store the checksum for the logical data.
	 * If the block pointer has the gang bit set, then the checksum
	 * it represents is for the reconstituted data and not for an
	 * individual gang member. The zio pipeline, however, must be able to
	 * determine the checksum of each of the gang constituents so it
	 * treats the checksum comparison differently than what we need
	 * for l2arc blocks. This prevents us from using the
	 * zio_checksum_error() interface directly. Instead we must call the
	 * zio_checksum_error_impl() so that we can ensure the checksum is
	 * generated using the correct checksum algorithm and accounts for the
	 * logical I/O size and not just a gang fragment.
	 */
	return (zio_checksum_error_impl(zio->io_spa, zio->io_bp,
	    BP_GET_CHECKSUM(zio->io_bp), zio->io_abd, zio->io_size,
	    zio->io_offset, NULL) == 0);
}

/*
 * Given a buf full of data, if ZFS_DEBUG_MODIFY is enabled this computes a
 * checksum and attaches it to the buf's hdr so that we can ensure that the buf
 * isn't modified later on. If buf is compressed or there is already a checksum
 * on the hdr, this is a no-op (we only checksum uncompressed bufs).
 */
static void
arc_cksum_compute(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		return;

	ASSERT(HDR_HAS_L1HDR(hdr));

	mutex_enter(&buf->b_hdr->b_l1hdr.b_freeze_lock);
	if (hdr->b_l1hdr.b_freeze_cksum != NULL || ARC_BUF_COMPRESSED(buf)) {
		mutex_exit(&hdr->b_l1hdr.b_freeze_lock);
		return;
	}

	ASSERT(!ARC_BUF_ENCRYPTED(buf));
	ASSERT(!ARC_BUF_COMPRESSED(buf));
	hdr->b_l1hdr.b_freeze_cksum = kmem_alloc(sizeof (zio_cksum_t),
	    KM_SLEEP);
	fletcher_2_native(buf->b_data, arc_buf_size(buf), NULL,
	    hdr->b_l1hdr.b_freeze_cksum);
	mutex_exit(&hdr->b_l1hdr.b_freeze_lock);
	arc_buf_watch(buf);
}

#ifndef _KERNEL
void
arc_buf_sigsegv(int sig, siginfo_t *si, void *unused)
{
	panic("Got SIGSEGV at address: 0x%lx\n", (long)si->si_addr);
}
#endif

/* ARGSUSED */
static void
arc_buf_unwatch(arc_buf_t *buf)
{
#ifndef _KERNEL
	if (arc_watch) {
		ASSERT0(mprotect(buf->b_data, arc_buf_size(buf),
		    PROT_READ | PROT_WRITE));
	}
#endif
}

/* ARGSUSED */
static void
arc_buf_watch(arc_buf_t *buf)
{
#ifndef _KERNEL
	if (arc_watch)
		ASSERT0(mprotect(buf->b_data, arc_buf_size(buf),
		    PROT_READ));
#endif
}

static arc_buf_contents_t
arc_buf_type(arc_buf_hdr_t *hdr)
{
	arc_buf_contents_t type;
	if (HDR_ISTYPE_METADATA(hdr)) {
		type = ARC_BUFC_METADATA;
	} else {
		type = ARC_BUFC_DATA;
	}
	VERIFY3U(hdr->b_type, ==, type);
	return (type);
}

boolean_t
arc_is_metadata(arc_buf_t *buf)
{
	return (HDR_ISTYPE_METADATA(buf->b_hdr) != 0);
}

static uint32_t
arc_bufc_to_flags(arc_buf_contents_t type)
{
	switch (type) {
	case ARC_BUFC_DATA:
		/* metadata field is 0 if buffer contains normal data */
		return (0);
	case ARC_BUFC_METADATA:
		return (ARC_FLAG_BUFC_METADATA);
	default:
		break;
	}
	panic("undefined ARC buffer type!");
	return ((uint32_t)-1);
}

void
arc_buf_thaw(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);
	ASSERT(!HDR_IO_IN_PROGRESS(hdr));

	arc_cksum_verify(buf);

	/*
	 * Compressed buffers do not manipulate the b_freeze_cksum.
	 */
	if (ARC_BUF_COMPRESSED(buf))
		return;

	ASSERT(HDR_HAS_L1HDR(hdr));
	arc_cksum_free(hdr);
	arc_buf_unwatch(buf);
}

void
arc_buf_freeze(arc_buf_t *buf)
{
	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		return;

	if (ARC_BUF_COMPRESSED(buf))
		return;

	ASSERT(HDR_HAS_L1HDR(buf->b_hdr));
	arc_cksum_compute(buf);
}

/*
 * The arc_buf_hdr_t's b_flags should never be modified directly. Instead,
 * the following functions should be used to ensure that the flags are
 * updated in a thread-safe way. When manipulating the flags either
 * the hash_lock must be held or the hdr must be undiscoverable. This
 * ensures that we're not racing with any other threads when updating
 * the flags.
 */
static inline void
arc_hdr_set_flags(arc_buf_hdr_t *hdr, arc_flags_t flags)
{
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));
	hdr->b_flags |= flags;
}

static inline void
arc_hdr_clear_flags(arc_buf_hdr_t *hdr, arc_flags_t flags)
{
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));
	hdr->b_flags &= ~flags;
}

/*
 * Setting the compression bits in the arc_buf_hdr_t's b_flags is
 * done in a special way since we have to clear and set bits
 * at the same time. Consumers that wish to set the compression bits
 * must use this function to ensure that the flags are updated in
 * thread-safe manner.
 */
static void
arc_hdr_set_compress(arc_buf_hdr_t *hdr, enum zio_compress cmp)
{
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));

	/*
	 * Holes and embedded blocks will always have a psize = 0 so
	 * we ignore the compression of the blkptr and set the
	 * want to uncompress them. Mark them as uncompressed.
	 */
	if (!zfs_compressed_arc_enabled || HDR_GET_PSIZE(hdr) == 0) {
		arc_hdr_clear_flags(hdr, ARC_FLAG_COMPRESSED_ARC);
		ASSERT(!HDR_COMPRESSION_ENABLED(hdr));
	} else {
		arc_hdr_set_flags(hdr, ARC_FLAG_COMPRESSED_ARC);
		ASSERT(HDR_COMPRESSION_ENABLED(hdr));
	}

	HDR_SET_COMPRESS(hdr, cmp);
	ASSERT3U(HDR_GET_COMPRESS(hdr), ==, cmp);
}

/*
 * Looks for another buf on the same hdr which has the data decompressed, copies
 * from it, and returns true. If no such buf exists, returns false.
 */
static boolean_t
arc_buf_try_copy_decompressed_data(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;
	boolean_t copied = B_FALSE;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT3P(buf->b_data, !=, NULL);
	ASSERT(!ARC_BUF_COMPRESSED(buf));

	for (arc_buf_t *from = hdr->b_l1hdr.b_buf; from != NULL;
	    from = from->b_next) {
		/* can't use our own data buffer */
		if (from == buf) {
			continue;
		}

		if (!ARC_BUF_COMPRESSED(from)) {
			bcopy(from->b_data, buf->b_data, arc_buf_size(buf));
			copied = B_TRUE;
			break;
		}
	}

	/*
	 * There were no decompressed bufs, so there should not be a
	 * checksum on the hdr either.
	 */
	if (zfs_flags & ZFS_DEBUG_MODIFY)
		EQUIV(!copied, hdr->b_l1hdr.b_freeze_cksum == NULL);

	return (copied);
}

/*
 * Allocates an ARC buf header that's in an evicted & L2-cached state.
 * This is used during l2arc reconstruction to make empty ARC buffers
 * which circumvent the regular disk->arc->l2arc path and instead come
 * into being in the reverse order, i.e. l2arc->arc.
 */
static arc_buf_hdr_t *
arc_buf_alloc_l2only(size_t size, arc_buf_contents_t type, l2arc_dev_t *dev,
    dva_t dva, uint64_t daddr, int32_t psize, uint64_t birth,
    enum zio_compress compress, uint8_t complevel, boolean_t protected,
    boolean_t prefetch, arc_state_type_t arcs_state)
{
	arc_buf_hdr_t	*hdr;

	ASSERT(size != 0);
	hdr = kmem_cache_alloc(hdr_l2only_cache, KM_SLEEP);
	hdr->b_birth = birth;
	hdr->b_type = type;
	hdr->b_flags = 0;
	arc_hdr_set_flags(hdr, arc_bufc_to_flags(type) | ARC_FLAG_HAS_L2HDR);
	HDR_SET_LSIZE(hdr, size);
	HDR_SET_PSIZE(hdr, psize);
	arc_hdr_set_compress(hdr, compress);
	hdr->b_complevel = complevel;
	if (protected)
		arc_hdr_set_flags(hdr, ARC_FLAG_PROTECTED);
	if (prefetch)
		arc_hdr_set_flags(hdr, ARC_FLAG_PREFETCH);
	hdr->b_spa = spa_load_guid(dev->l2ad_vdev->vdev_spa);

	hdr->b_dva = dva;

	hdr->b_l2hdr.b_dev = dev;
	hdr->b_l2hdr.b_daddr = daddr;
	hdr->b_l2hdr.b_arcs_state = arcs_state;

	return (hdr);
}

/*
 * Return the size of the block, b_pabd, that is stored in the arc_buf_hdr_t.
 */
static uint64_t
arc_hdr_size(arc_buf_hdr_t *hdr)
{
	uint64_t size;

	if (arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF &&
	    HDR_GET_PSIZE(hdr) > 0) {
		size = HDR_GET_PSIZE(hdr);
	} else {
		ASSERT3U(HDR_GET_LSIZE(hdr), !=, 0);
		size = HDR_GET_LSIZE(hdr);
	}
	return (size);
}

static int
arc_hdr_authenticate(arc_buf_hdr_t *hdr, spa_t *spa, uint64_t dsobj)
{
	int ret;
	uint64_t csize;
	uint64_t lsize = HDR_GET_LSIZE(hdr);
	uint64_t psize = HDR_GET_PSIZE(hdr);
	void *tmpbuf = NULL;
	abd_t *abd = hdr->b_l1hdr.b_pabd;

	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));
	ASSERT(HDR_AUTHENTICATED(hdr));
	ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);

	/*
	 * The MAC is calculated on the compressed data that is stored on disk.
	 * However, if compressed arc is disabled we will only have the
	 * decompressed data available to us now. Compress it into a temporary
	 * abd so we can verify the MAC. The performance overhead of this will
	 * be relatively low, since most objects in an encrypted objset will
	 * be encrypted (instead of authenticated) anyway.
	 */
	if (HDR_GET_COMPRESS(hdr) != ZIO_COMPRESS_OFF &&
	    !HDR_COMPRESSION_ENABLED(hdr)) {
		tmpbuf = zio_buf_alloc(lsize);
		abd = abd_get_from_buf(tmpbuf, lsize);
		abd_take_ownership_of_buf(abd, B_TRUE);
		csize = zio_compress_data(HDR_GET_COMPRESS(hdr),
		    hdr->b_l1hdr.b_pabd, tmpbuf, lsize, hdr->b_complevel);
		ASSERT3U(csize, <=, psize);
		abd_zero_off(abd, csize, psize - csize);
	}

	/*
	 * Authentication is best effort. We authenticate whenever the key is
	 * available. If we succeed we clear ARC_FLAG_NOAUTH.
	 */
	if (hdr->b_crypt_hdr.b_ot == DMU_OT_OBJSET) {
		ASSERT3U(HDR_GET_COMPRESS(hdr), ==, ZIO_COMPRESS_OFF);
		ASSERT3U(lsize, ==, psize);
		ret = spa_do_crypt_objset_mac_abd(B_FALSE, spa, dsobj, abd,
		    psize, hdr->b_l1hdr.b_byteswap != DMU_BSWAP_NUMFUNCS);
	} else {
		ret = spa_do_crypt_mac_abd(B_FALSE, spa, dsobj, abd, psize,
		    hdr->b_crypt_hdr.b_mac);
	}

	if (ret == 0)
		arc_hdr_clear_flags(hdr, ARC_FLAG_NOAUTH);
	else if (ret != ENOENT)
		goto error;

	if (tmpbuf != NULL)
		abd_free(abd);

	return (0);

error:
	if (tmpbuf != NULL)
		abd_free(abd);

	return (ret);
}

/*
 * This function will take a header that only has raw encrypted data in
 * b_crypt_hdr.b_rabd and decrypt it into a new buffer which is stored in
 * b_l1hdr.b_pabd. If designated in the header flags, this function will
 * also decompress the data.
 */
static int
arc_hdr_decrypt(arc_buf_hdr_t *hdr, spa_t *spa, const zbookmark_phys_t *zb)
{
	int ret;
	abd_t *cabd = NULL;
	void *tmp = NULL;
	boolean_t no_crypt = B_FALSE;
	boolean_t bswap = (hdr->b_l1hdr.b_byteswap != DMU_BSWAP_NUMFUNCS);

	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));
	ASSERT(HDR_ENCRYPTED(hdr));

	arc_hdr_alloc_abd(hdr, ARC_HDR_DO_ADAPT);

	ret = spa_do_crypt_abd(B_FALSE, spa, zb, hdr->b_crypt_hdr.b_ot,
	    B_FALSE, bswap, hdr->b_crypt_hdr.b_salt, hdr->b_crypt_hdr.b_iv,
	    hdr->b_crypt_hdr.b_mac, HDR_GET_PSIZE(hdr), hdr->b_l1hdr.b_pabd,
	    hdr->b_crypt_hdr.b_rabd, &no_crypt);
	if (ret != 0)
		goto error;

	if (no_crypt) {
		abd_copy(hdr->b_l1hdr.b_pabd, hdr->b_crypt_hdr.b_rabd,
		    HDR_GET_PSIZE(hdr));
	}

	/*
	 * If this header has disabled arc compression but the b_pabd is
	 * compressed after decrypting it, we need to decompress the newly
	 * decrypted data.
	 */
	if (HDR_GET_COMPRESS(hdr) != ZIO_COMPRESS_OFF &&
	    !HDR_COMPRESSION_ENABLED(hdr)) {
		/*
		 * We want to make sure that we are correctly honoring the
		 * zfs_abd_scatter_enabled setting, so we allocate an abd here
		 * and then loan a buffer from it, rather than allocating a
		 * linear buffer and wrapping it in an abd later.
		 */
		cabd = arc_get_data_abd(hdr, arc_hdr_size(hdr), hdr, B_TRUE);
		tmp = abd_borrow_buf(cabd, arc_hdr_size(hdr));

		ret = zio_decompress_data(HDR_GET_COMPRESS(hdr),
		    hdr->b_l1hdr.b_pabd, tmp, HDR_GET_PSIZE(hdr),
		    HDR_GET_LSIZE(hdr), &hdr->b_complevel);
		if (ret != 0) {
			abd_return_buf(cabd, tmp, arc_hdr_size(hdr));
			goto error;
		}

		abd_return_buf_copy(cabd, tmp, arc_hdr_size(hdr));
		arc_free_data_abd(hdr, hdr->b_l1hdr.b_pabd,
		    arc_hdr_size(hdr), hdr);
		hdr->b_l1hdr.b_pabd = cabd;
	}

	return (0);

error:
	arc_hdr_free_abd(hdr, B_FALSE);
	if (cabd != NULL)
		arc_free_data_buf(hdr, cabd, arc_hdr_size(hdr), hdr);

	return (ret);
}

/*
 * This function is called during arc_buf_fill() to prepare the header's
 * abd plaintext pointer for use. This involves authenticated protected
 * data and decrypting encrypted data into the plaintext abd.
 */
static int
arc_fill_hdr_crypt(arc_buf_hdr_t *hdr, kmutex_t *hash_lock, spa_t *spa,
    const zbookmark_phys_t *zb, boolean_t noauth)
{
	int ret;

	ASSERT(HDR_PROTECTED(hdr));

	if (hash_lock != NULL)
		mutex_enter(hash_lock);

	if (HDR_NOAUTH(hdr) && !noauth) {
		/*
		 * The caller requested authenticated data but our data has
		 * not been authenticated yet. Verify the MAC now if we can.
		 */
		ret = arc_hdr_authenticate(hdr, spa, zb->zb_objset);
		if (ret != 0)
			goto error;
	} else if (HDR_HAS_RABD(hdr) && hdr->b_l1hdr.b_pabd == NULL) {
		/*
		 * If we only have the encrypted version of the data, but the
		 * unencrypted version was requested we take this opportunity
		 * to store the decrypted version in the header for future use.
		 */
		ret = arc_hdr_decrypt(hdr, spa, zb);
		if (ret != 0)
			goto error;
	}

	ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);

	if (hash_lock != NULL)
		mutex_exit(hash_lock);

	return (0);

error:
	if (hash_lock != NULL)
		mutex_exit(hash_lock);

	return (ret);
}

/*
 * This function is used by the dbuf code to decrypt bonus buffers in place.
 * The dbuf code itself doesn't have any locking for decrypting a shared dnode
 * block, so we use the hash lock here to protect against concurrent calls to
 * arc_buf_fill().
 */
static void
arc_buf_untransform_in_place(arc_buf_t *buf, kmutex_t *hash_lock)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(HDR_ENCRYPTED(hdr));
	ASSERT3U(hdr->b_crypt_hdr.b_ot, ==, DMU_OT_DNODE);
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));
	ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);

	zio_crypt_copy_dnode_bonus(hdr->b_l1hdr.b_pabd, buf->b_data,
	    arc_buf_size(buf));
	buf->b_flags &= ~ARC_BUF_FLAG_ENCRYPTED;
	buf->b_flags &= ~ARC_BUF_FLAG_COMPRESSED;
	hdr->b_crypt_hdr.b_ebufcnt -= 1;
}

/*
 * Given a buf that has a data buffer attached to it, this function will
 * efficiently fill the buf with data of the specified compression setting from
 * the hdr and update the hdr's b_freeze_cksum if necessary. If the buf and hdr
 * are already sharing a data buf, no copy is performed.
 *
 * If the buf is marked as compressed but uncompressed data was requested, this
 * will allocate a new data buffer for the buf, remove that flag, and fill the
 * buf with uncompressed data. You can't request a compressed buf on a hdr with
 * uncompressed data, and (since we haven't added support for it yet) if you
 * want compressed data your buf must already be marked as compressed and have
 * the correct-sized data buffer.
 */
static int
arc_buf_fill(arc_buf_t *buf, spa_t *spa, const zbookmark_phys_t *zb,
    arc_fill_flags_t flags)
{
	int error = 0;
	arc_buf_hdr_t *hdr = buf->b_hdr;
	boolean_t hdr_compressed =
	    (arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF);
	boolean_t compressed = (flags & ARC_FILL_COMPRESSED) != 0;
	boolean_t encrypted = (flags & ARC_FILL_ENCRYPTED) != 0;
	dmu_object_byteswap_t bswap = hdr->b_l1hdr.b_byteswap;
	kmutex_t *hash_lock = (flags & ARC_FILL_LOCKED) ? NULL : HDR_LOCK(hdr);

	ASSERT3P(buf->b_data, !=, NULL);
	IMPLY(compressed, hdr_compressed || ARC_BUF_ENCRYPTED(buf));
	IMPLY(compressed, ARC_BUF_COMPRESSED(buf));
	IMPLY(encrypted, HDR_ENCRYPTED(hdr));
	IMPLY(encrypted, ARC_BUF_ENCRYPTED(buf));
	IMPLY(encrypted, ARC_BUF_COMPRESSED(buf));
	IMPLY(encrypted, !ARC_BUF_SHARED(buf));

	/*
	 * If the caller wanted encrypted data we just need to copy it from
	 * b_rabd and potentially byteswap it. We won't be able to do any
	 * further transforms on it.
	 */
	if (encrypted) {
		ASSERT(HDR_HAS_RABD(hdr));
		abd_copy_to_buf(buf->b_data, hdr->b_crypt_hdr.b_rabd,
		    HDR_GET_PSIZE(hdr));
		goto byteswap;
	}

	/*
	 * Adjust encrypted and authenticated headers to accommodate
	 * the request if needed. Dnode blocks (ARC_FILL_IN_PLACE) are
	 * allowed to fail decryption due to keys not being loaded
	 * without being marked as an IO error.
	 */
	if (HDR_PROTECTED(hdr)) {
		error = arc_fill_hdr_crypt(hdr, hash_lock, spa,
		    zb, !!(flags & ARC_FILL_NOAUTH));
		if (error == EACCES && (flags & ARC_FILL_IN_PLACE) != 0) {
			return (error);
		} else if (error != 0) {
			if (hash_lock != NULL)
				mutex_enter(hash_lock);
			arc_hdr_set_flags(hdr, ARC_FLAG_IO_ERROR);
			if (hash_lock != NULL)
				mutex_exit(hash_lock);
			return (error);
		}
	}

	/*
	 * There is a special case here for dnode blocks which are
	 * decrypting their bonus buffers. These blocks may request to
	 * be decrypted in-place. This is necessary because there may
	 * be many dnodes pointing into this buffer and there is
	 * currently no method to synchronize replacing the backing
	 * b_data buffer and updating all of the pointers. Here we use
	 * the hash lock to ensure there are no races. If the need
	 * arises for other types to be decrypted in-place, they must
	 * add handling here as well.
	 */
	if ((flags & ARC_FILL_IN_PLACE) != 0) {
		ASSERT(!hdr_compressed);
		ASSERT(!compressed);
		ASSERT(!encrypted);

		if (HDR_ENCRYPTED(hdr) && ARC_BUF_ENCRYPTED(buf)) {
			ASSERT3U(hdr->b_crypt_hdr.b_ot, ==, DMU_OT_DNODE);

			if (hash_lock != NULL)
				mutex_enter(hash_lock);
			arc_buf_untransform_in_place(buf, hash_lock);
			if (hash_lock != NULL)
				mutex_exit(hash_lock);

			/* Compute the hdr's checksum if necessary */
			arc_cksum_compute(buf);
		}

		return (0);
	}

	if (hdr_compressed == compressed) {
		if (!arc_buf_is_shared(buf)) {
			abd_copy_to_buf(buf->b_data, hdr->b_l1hdr.b_pabd,
			    arc_buf_size(buf));
		}
	} else {
		ASSERT(hdr_compressed);
		ASSERT(!compressed);
		ASSERT3U(HDR_GET_LSIZE(hdr), !=, HDR_GET_PSIZE(hdr));

		/*
		 * If the buf is sharing its data with the hdr, unlink it and
		 * allocate a new data buffer for the buf.
		 */
		if (arc_buf_is_shared(buf)) {
			ASSERT(ARC_BUF_COMPRESSED(buf));

			/* We need to give the buf its own b_data */
			buf->b_flags &= ~ARC_BUF_FLAG_SHARED;
			buf->b_data =
			    arc_get_data_buf(hdr, HDR_GET_LSIZE(hdr), buf);
			arc_hdr_clear_flags(hdr, ARC_FLAG_SHARED_DATA);

			/* Previously overhead was 0; just add new overhead */
			ARCSTAT_INCR(arcstat_overhead_size, HDR_GET_LSIZE(hdr));
		} else if (ARC_BUF_COMPRESSED(buf)) {
			/* We need to reallocate the buf's b_data */
			arc_free_data_buf(hdr, buf->b_data, HDR_GET_PSIZE(hdr),
			    buf);
			buf->b_data =
			    arc_get_data_buf(hdr, HDR_GET_LSIZE(hdr), buf);

			/* We increased the size of b_data; update overhead */
			ARCSTAT_INCR(arcstat_overhead_size,
			    HDR_GET_LSIZE(hdr) - HDR_GET_PSIZE(hdr));
		}

		/*
		 * Regardless of the buf's previous compression settings, it
		 * should not be compressed at the end of this function.
		 */
		buf->b_flags &= ~ARC_BUF_FLAG_COMPRESSED;

		/*
		 * Try copying the data from another buf which already has a
		 * decompressed version. If that's not possible, it's time to
		 * bite the bullet and decompress the data from the hdr.
		 */
		if (arc_buf_try_copy_decompressed_data(buf)) {
			/* Skip byteswapping and checksumming (already done) */
			return (0);
		} else {
			error = zio_decompress_data(HDR_GET_COMPRESS(hdr),
			    hdr->b_l1hdr.b_pabd, buf->b_data,
			    HDR_GET_PSIZE(hdr), HDR_GET_LSIZE(hdr),
			    &hdr->b_complevel);

			/*
			 * Absent hardware errors or software bugs, this should
			 * be impossible, but log it anyway so we can debug it.
			 */
			if (error != 0) {
				zfs_dbgmsg(
				    "hdr %px, compress %d, psize %d, lsize %d",
				    hdr, arc_hdr_get_compress(hdr),
				    HDR_GET_PSIZE(hdr), HDR_GET_LSIZE(hdr));
				if (hash_lock != NULL)
					mutex_enter(hash_lock);
				arc_hdr_set_flags(hdr, ARC_FLAG_IO_ERROR);
				if (hash_lock != NULL)
					mutex_exit(hash_lock);
				return (SET_ERROR(EIO));
			}
		}
	}

byteswap:
	/* Byteswap the buf's data if necessary */
	if (bswap != DMU_BSWAP_NUMFUNCS) {
		ASSERT(!HDR_SHARED_DATA(hdr));
		ASSERT3U(bswap, <, DMU_BSWAP_NUMFUNCS);
		dmu_ot_byteswap[bswap].ob_func(buf->b_data, HDR_GET_LSIZE(hdr));
	}

	/* Compute the hdr's checksum if necessary */
	arc_cksum_compute(buf);

	return (0);
}

/*
 * If this function is being called to decrypt an encrypted buffer or verify an
 * authenticated one, the key must be loaded and a mapping must be made
 * available in the keystore via spa_keystore_create_mapping() or one of its
 * callers.
 */
int
arc_untransform(arc_buf_t *buf, spa_t *spa, const zbookmark_phys_t *zb,
    boolean_t in_place)
{
	int ret;
	arc_fill_flags_t flags = 0;

	if (in_place)
		flags |= ARC_FILL_IN_PLACE;

	ret = arc_buf_fill(buf, spa, zb, flags);
	if (ret == ECKSUM) {
		/*
		 * Convert authentication and decryption errors to EIO
		 * (and generate an ereport) before leaving the ARC.
		 */
		ret = SET_ERROR(EIO);
		spa_log_error(spa, zb);
		(void) zfs_ereport_post(FM_EREPORT_ZFS_AUTHENTICATION,
		    spa, NULL, zb, NULL, 0);
	}

	return (ret);
}

/*
 * Increment the amount of evictable space in the arc_state_t's refcount.
 * We account for the space used by the hdr and the arc buf individually
 * so that we can add and remove them from the refcount individually.
 */
static void
arc_evictable_space_increment(arc_buf_hdr_t *hdr, arc_state_t *state)
{
	arc_buf_contents_t type = arc_buf_type(hdr);

	ASSERT(HDR_HAS_L1HDR(hdr));

	if (GHOST_STATE(state)) {
		ASSERT0(hdr->b_l1hdr.b_bufcnt);
		ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
		ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
		ASSERT(!HDR_HAS_RABD(hdr));
		(void) zfs_refcount_add_many(&state->arcs_esize[type],
		    HDR_GET_LSIZE(hdr), hdr);
		return;
	}

	ASSERT(!GHOST_STATE(state));
	if (hdr->b_l1hdr.b_pabd != NULL) {
		(void) zfs_refcount_add_many(&state->arcs_esize[type],
		    arc_hdr_size(hdr), hdr);
	}
	if (HDR_HAS_RABD(hdr)) {
		(void) zfs_refcount_add_many(&state->arcs_esize[type],
		    HDR_GET_PSIZE(hdr), hdr);
	}

	for (arc_buf_t *buf = hdr->b_l1hdr.b_buf; buf != NULL;
	    buf = buf->b_next) {
		if (arc_buf_is_shared(buf))
			continue;
		(void) zfs_refcount_add_many(&state->arcs_esize[type],
		    arc_buf_size(buf), buf);
	}
}

/*
 * Decrement the amount of evictable space in the arc_state_t's refcount.
 * We account for the space used by the hdr and the arc buf individually
 * so that we can add and remove them from the refcount individually.
 */
static void
arc_evictable_space_decrement(arc_buf_hdr_t *hdr, arc_state_t *state)
{
	arc_buf_contents_t type = arc_buf_type(hdr);

	ASSERT(HDR_HAS_L1HDR(hdr));

	if (GHOST_STATE(state)) {
		ASSERT0(hdr->b_l1hdr.b_bufcnt);
		ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
		ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
		ASSERT(!HDR_HAS_RABD(hdr));
		(void) zfs_refcount_remove_many(&state->arcs_esize[type],
		    HDR_GET_LSIZE(hdr), hdr);
		return;
	}

	ASSERT(!GHOST_STATE(state));
	if (hdr->b_l1hdr.b_pabd != NULL) {
		(void) zfs_refcount_remove_many(&state->arcs_esize[type],
		    arc_hdr_size(hdr), hdr);
	}
	if (HDR_HAS_RABD(hdr)) {
		(void) zfs_refcount_remove_many(&state->arcs_esize[type],
		    HDR_GET_PSIZE(hdr), hdr);
	}

	for (arc_buf_t *buf = hdr->b_l1hdr.b_buf; buf != NULL;
	    buf = buf->b_next) {
		if (arc_buf_is_shared(buf))
			continue;
		(void) zfs_refcount_remove_many(&state->arcs_esize[type],
		    arc_buf_size(buf), buf);
	}
}

/*
 * Add a reference to this hdr indicating that someone is actively
 * referencing that memory. When the refcount transitions from 0 to 1,
 * we remove it from the respective arc_state_t list to indicate that
 * it is not evictable.
 */
static void
add_reference(arc_buf_hdr_t *hdr, void *tag)
{
	arc_state_t *state;

	ASSERT(HDR_HAS_L1HDR(hdr));
	if (!HDR_EMPTY(hdr) && !MUTEX_HELD(HDR_LOCK(hdr))) {
		ASSERT(hdr->b_l1hdr.b_state == arc_anon);
		ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
		ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
	}

	state = hdr->b_l1hdr.b_state;

	if ((zfs_refcount_add(&hdr->b_l1hdr.b_refcnt, tag) == 1) &&
	    (state != arc_anon)) {
		/* We don't use the L2-only state list. */
		if (state != arc_l2c_only) {
			multilist_remove(state->arcs_list[arc_buf_type(hdr)],
			    hdr);
			arc_evictable_space_decrement(hdr, state);
		}
		/* remove the prefetch flag if we get a reference */
		if (HDR_HAS_L2HDR(hdr))
			l2arc_hdr_arcstats_decrement_state(hdr);
		arc_hdr_clear_flags(hdr, ARC_FLAG_PREFETCH);
		if (HDR_HAS_L2HDR(hdr))
			l2arc_hdr_arcstats_increment_state(hdr);
	}
}

/*
 * Remove a reference from this hdr. When the reference transitions from
 * 1 to 0 and we're not anonymous, then we add this hdr to the arc_state_t's
 * list making it eligible for eviction.
 */
static int
remove_reference(arc_buf_hdr_t *hdr, kmutex_t *hash_lock, void *tag)
{
	int cnt;
	arc_state_t *state = hdr->b_l1hdr.b_state;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(state == arc_anon || MUTEX_HELD(hash_lock));
	ASSERT(!GHOST_STATE(state));

	/*
	 * arc_l2c_only counts as a ghost state so we don't need to explicitly
	 * check to prevent usage of the arc_l2c_only list.
	 */
	if (((cnt = zfs_refcount_remove(&hdr->b_l1hdr.b_refcnt, tag)) == 0) &&
	    (state != arc_anon)) {
		multilist_insert(state->arcs_list[arc_buf_type(hdr)], hdr);
		ASSERT3U(hdr->b_l1hdr.b_bufcnt, >, 0);
		arc_evictable_space_increment(hdr, state);
	}
	return (cnt);
}

/*
 * Returns detailed information about a specific arc buffer.  When the
 * state_index argument is set the function will calculate the arc header
 * list position for its arc state.  Since this requires a linear traversal
 * callers are strongly encourage not to do this.  However, it can be helpful
 * for targeted analysis so the functionality is provided.
 */
void
arc_buf_info(arc_buf_t *ab, arc_buf_info_t *abi, int state_index)
{
	arc_buf_hdr_t *hdr = ab->b_hdr;
	l1arc_buf_hdr_t *l1hdr = NULL;
	l2arc_buf_hdr_t *l2hdr = NULL;
	arc_state_t *state = NULL;

	memset(abi, 0, sizeof (arc_buf_info_t));

	if (hdr == NULL)
		return;

	abi->abi_flags = hdr->b_flags;

	if (HDR_HAS_L1HDR(hdr)) {
		l1hdr = &hdr->b_l1hdr;
		state = l1hdr->b_state;
	}
	if (HDR_HAS_L2HDR(hdr))
		l2hdr = &hdr->b_l2hdr;

	if (l1hdr) {
		abi->abi_bufcnt = l1hdr->b_bufcnt;
		abi->abi_access = l1hdr->b_arc_access;
		abi->abi_mru_hits = l1hdr->b_mru_hits;
		abi->abi_mru_ghost_hits = l1hdr->b_mru_ghost_hits;
		abi->abi_mfu_hits = l1hdr->b_mfu_hits;
		abi->abi_mfu_ghost_hits = l1hdr->b_mfu_ghost_hits;
		abi->abi_holds = zfs_refcount_count(&l1hdr->b_refcnt);
	}

	if (l2hdr) {
		abi->abi_l2arc_dattr = l2hdr->b_daddr;
		abi->abi_l2arc_hits = l2hdr->b_hits;
	}

	abi->abi_state_type = state ? state->arcs_state : ARC_STATE_ANON;
	abi->abi_state_contents = arc_buf_type(hdr);
	abi->abi_size = arc_hdr_size(hdr);
}

/*
 * Move the supplied buffer to the indicated state. The hash lock
 * for the buffer must be held by the caller.
 */
static void
arc_change_state(arc_state_t *new_state, arc_buf_hdr_t *hdr,
    kmutex_t *hash_lock)
{
	arc_state_t *old_state;
	int64_t refcnt;
	uint32_t bufcnt;
	boolean_t update_old, update_new;
	arc_buf_contents_t buftype = arc_buf_type(hdr);

	/*
	 * We almost always have an L1 hdr here, since we call arc_hdr_realloc()
	 * in arc_read() when bringing a buffer out of the L2ARC.  However, the
	 * L1 hdr doesn't always exist when we change state to arc_anon before
	 * destroying a header, in which case reallocating to add the L1 hdr is
	 * pointless.
	 */
	if (HDR_HAS_L1HDR(hdr)) {
		old_state = hdr->b_l1hdr.b_state;
		refcnt = zfs_refcount_count(&hdr->b_l1hdr.b_refcnt);
		bufcnt = hdr->b_l1hdr.b_bufcnt;
		update_old = (bufcnt > 0 || hdr->b_l1hdr.b_pabd != NULL ||
		    HDR_HAS_RABD(hdr));
	} else {
		old_state = arc_l2c_only;
		refcnt = 0;
		bufcnt = 0;
		update_old = B_FALSE;
	}
	update_new = update_old;

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT3P(new_state, !=, old_state);
	ASSERT(!GHOST_STATE(new_state) || bufcnt == 0);
	ASSERT(old_state != arc_anon || bufcnt <= 1);

	/*
	 * If this buffer is evictable, transfer it from the
	 * old state list to the new state list.
	 */
	if (refcnt == 0) {
		if (old_state != arc_anon && old_state != arc_l2c_only) {
			ASSERT(HDR_HAS_L1HDR(hdr));
			multilist_remove(old_state->arcs_list[buftype], hdr);

			if (GHOST_STATE(old_state)) {
				ASSERT0(bufcnt);
				ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
				update_old = B_TRUE;
			}
			arc_evictable_space_decrement(hdr, old_state);
		}
		if (new_state != arc_anon && new_state != arc_l2c_only) {
			/*
			 * An L1 header always exists here, since if we're
			 * moving to some L1-cached state (i.e. not l2c_only or
			 * anonymous), we realloc the header to add an L1hdr
			 * beforehand.
			 */
			ASSERT(HDR_HAS_L1HDR(hdr));
			multilist_insert(new_state->arcs_list[buftype], hdr);

			if (GHOST_STATE(new_state)) {
				ASSERT0(bufcnt);
				ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
				update_new = B_TRUE;
			}
			arc_evictable_space_increment(hdr, new_state);
		}
	}

	ASSERT(!HDR_EMPTY(hdr));
	if (new_state == arc_anon && HDR_IN_HASH_TABLE(hdr))
		buf_hash_remove(hdr);

	/* adjust state sizes (ignore arc_l2c_only) */

	if (update_new && new_state != arc_l2c_only) {
		ASSERT(HDR_HAS_L1HDR(hdr));
		if (GHOST_STATE(new_state)) {
			ASSERT0(bufcnt);

			/*
			 * When moving a header to a ghost state, we first
			 * remove all arc buffers. Thus, we'll have a
			 * bufcnt of zero, and no arc buffer to use for
			 * the reference. As a result, we use the arc
			 * header pointer for the reference.
			 */
			(void) zfs_refcount_add_many(&new_state->arcs_size,
			    HDR_GET_LSIZE(hdr), hdr);
			ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
			ASSERT(!HDR_HAS_RABD(hdr));
		} else {
			uint32_t buffers = 0;

			/*
			 * Each individual buffer holds a unique reference,
			 * thus we must remove each of these references one
			 * at a time.
			 */
			for (arc_buf_t *buf = hdr->b_l1hdr.b_buf; buf != NULL;
			    buf = buf->b_next) {
				ASSERT3U(bufcnt, !=, 0);
				buffers++;

				/*
				 * When the arc_buf_t is sharing the data
				 * block with the hdr, the owner of the
				 * reference belongs to the hdr. Only
				 * add to the refcount if the arc_buf_t is
				 * not shared.
				 */
				if (arc_buf_is_shared(buf))
					continue;

				(void) zfs_refcount_add_many(
				    &new_state->arcs_size,
				    arc_buf_size(buf), buf);
			}
			ASSERT3U(bufcnt, ==, buffers);

			if (hdr->b_l1hdr.b_pabd != NULL) {
				(void) zfs_refcount_add_many(
				    &new_state->arcs_size,
				    arc_hdr_size(hdr), hdr);
			}

			if (HDR_HAS_RABD(hdr)) {
				(void) zfs_refcount_add_many(
				    &new_state->arcs_size,
				    HDR_GET_PSIZE(hdr), hdr);
			}
		}
	}

	if (update_old && old_state != arc_l2c_only) {
		ASSERT(HDR_HAS_L1HDR(hdr));
		if (GHOST_STATE(old_state)) {
			ASSERT0(bufcnt);
			ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
			ASSERT(!HDR_HAS_RABD(hdr));

			/*
			 * When moving a header off of a ghost state,
			 * the header will not contain any arc buffers.
			 * We use the arc header pointer for the reference
			 * which is exactly what we did when we put the
			 * header on the ghost state.
			 */

			(void) zfs_refcount_remove_many(&old_state->arcs_size,
			    HDR_GET_LSIZE(hdr), hdr);
		} else {
			uint32_t buffers = 0;

			/*
			 * Each individual buffer holds a unique reference,
			 * thus we must remove each of these references one
			 * at a time.
			 */
			for (arc_buf_t *buf = hdr->b_l1hdr.b_buf; buf != NULL;
			    buf = buf->b_next) {
				ASSERT3U(bufcnt, !=, 0);
				buffers++;

				/*
				 * When the arc_buf_t is sharing the data
				 * block with the hdr, the owner of the
				 * reference belongs to the hdr. Only
				 * add to the refcount if the arc_buf_t is
				 * not shared.
				 */
				if (arc_buf_is_shared(buf))
					continue;

				(void) zfs_refcount_remove_many(
				    &old_state->arcs_size, arc_buf_size(buf),
				    buf);
			}
			ASSERT3U(bufcnt, ==, buffers);
			ASSERT(hdr->b_l1hdr.b_pabd != NULL ||
			    HDR_HAS_RABD(hdr));

			if (hdr->b_l1hdr.b_pabd != NULL) {
				(void) zfs_refcount_remove_many(
				    &old_state->arcs_size, arc_hdr_size(hdr),
				    hdr);
			}

			if (HDR_HAS_RABD(hdr)) {
				(void) zfs_refcount_remove_many(
				    &old_state->arcs_size, HDR_GET_PSIZE(hdr),
				    hdr);
			}
		}
	}

	if (HDR_HAS_L1HDR(hdr)) {
		hdr->b_l1hdr.b_state = new_state;

		if (HDR_HAS_L2HDR(hdr) && new_state != arc_l2c_only) {
			l2arc_hdr_arcstats_decrement_state(hdr);
			hdr->b_l2hdr.b_arcs_state = new_state->arcs_state;
			l2arc_hdr_arcstats_increment_state(hdr);
		}
	}

	/*
	 * L2 headers should never be on the L2 state list since they don't
	 * have L1 headers allocated.
	 */
	ASSERT(multilist_is_empty(arc_l2c_only->arcs_list[ARC_BUFC_DATA]) &&
	    multilist_is_empty(arc_l2c_only->arcs_list[ARC_BUFC_METADATA]));
}

void
arc_space_consume(uint64_t space, arc_space_type_t type)
{
	ASSERT(type >= 0 && type < ARC_SPACE_NUMTYPES);

	switch (type) {
	default:
		break;
	case ARC_SPACE_DATA:
		aggsum_add(&astat_data_size, space);
		break;
	case ARC_SPACE_META:
		aggsum_add(&astat_metadata_size, space);
		break;
	case ARC_SPACE_BONUS:
		aggsum_add(&astat_bonus_size, space);
		break;
	case ARC_SPACE_DNODE:
		aggsum_add(&astat_dnode_size, space);
		break;
	case ARC_SPACE_DBUF:
		aggsum_add(&astat_dbuf_size, space);
		break;
	case ARC_SPACE_HDRS:
		aggsum_add(&astat_hdr_size, space);
		break;
	case ARC_SPACE_L2HDRS:
		aggsum_add(&astat_l2_hdr_size, space);
		break;
	case ARC_SPACE_ABD_CHUNK_WASTE:
		/*
		 * Note: this includes space wasted by all scatter ABD's, not
		 * just those allocated by the ARC.  But the vast majority of
		 * scatter ABD's come from the ARC, because other users are
		 * very short-lived.
		 */
		aggsum_add(&astat_abd_chunk_waste_size, space);
		break;
	}

	if (type != ARC_SPACE_DATA && type != ARC_SPACE_ABD_CHUNK_WASTE)
		aggsum_add(&arc_meta_used, space);

	aggsum_add(&arc_size, space);
}

void
arc_space_return(uint64_t space, arc_space_type_t type)
{
	ASSERT(type >= 0 && type < ARC_SPACE_NUMTYPES);

	switch (type) {
	default:
		break;
	case ARC_SPACE_DATA:
		aggsum_add(&astat_data_size, -space);
		break;
	case ARC_SPACE_META:
		aggsum_add(&astat_metadata_size, -space);
		break;
	case ARC_SPACE_BONUS:
		aggsum_add(&astat_bonus_size, -space);
		break;
	case ARC_SPACE_DNODE:
		aggsum_add(&astat_dnode_size, -space);
		break;
	case ARC_SPACE_DBUF:
		aggsum_add(&astat_dbuf_size, -space);
		break;
	case ARC_SPACE_HDRS:
		aggsum_add(&astat_hdr_size, -space);
		break;
	case ARC_SPACE_L2HDRS:
		aggsum_add(&astat_l2_hdr_size, -space);
		break;
	case ARC_SPACE_ABD_CHUNK_WASTE:
		aggsum_add(&astat_abd_chunk_waste_size, -space);
		break;
	}

	if (type != ARC_SPACE_DATA && type != ARC_SPACE_ABD_CHUNK_WASTE) {
		ASSERT(aggsum_compare(&arc_meta_used, space) >= 0);
		/*
		 * We use the upper bound here rather than the precise value
		 * because the arc_meta_max value doesn't need to be
		 * precise. It's only consumed by humans via arcstats.
		 */
		if (arc_meta_max < aggsum_upper_bound(&arc_meta_used))
			arc_meta_max = aggsum_upper_bound(&arc_meta_used);
		aggsum_add(&arc_meta_used, -space);
	}

	ASSERT(aggsum_compare(&arc_size, space) >= 0);
	aggsum_add(&arc_size, -space);
}

/*
 * Given a hdr and a buf, returns whether that buf can share its b_data buffer
 * with the hdr's b_pabd.
 */
static boolean_t
arc_can_share(arc_buf_hdr_t *hdr, arc_buf_t *buf)
{
	/*
	 * The criteria for sharing a hdr's data are:
	 * 1. the buffer is not encrypted
	 * 2. the hdr's compression matches the buf's compression
	 * 3. the hdr doesn't need to be byteswapped
	 * 4. the hdr isn't already being shared
	 * 5. the buf is either compressed or it is the last buf in the hdr list
	 *
	 * Criterion #5 maintains the invariant that shared uncompressed
	 * bufs must be the final buf in the hdr's b_buf list. Reading this, you
	 * might ask, "if a compressed buf is allocated first, won't that be the
	 * last thing in the list?", but in that case it's impossible to create
	 * a shared uncompressed buf anyway (because the hdr must be compressed
	 * to have the compressed buf). You might also think that #3 is
	 * sufficient to make this guarantee, however it's possible
	 * (specifically in the rare L2ARC write race mentioned in
	 * arc_buf_alloc_impl()) there will be an existing uncompressed buf that
	 * is shareable, but wasn't at the time of its allocation. Rather than
	 * allow a new shared uncompressed buf to be created and then shuffle
	 * the list around to make it the last element, this simply disallows
	 * sharing if the new buf isn't the first to be added.
	 */
	ASSERT3P(buf->b_hdr, ==, hdr);
	boolean_t hdr_compressed =
	    arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF;
	boolean_t buf_compressed = ARC_BUF_COMPRESSED(buf) != 0;
	return (!ARC_BUF_ENCRYPTED(buf) &&
	    buf_compressed == hdr_compressed &&
	    hdr->b_l1hdr.b_byteswap == DMU_BSWAP_NUMFUNCS &&
	    !HDR_SHARED_DATA(hdr) &&
	    (ARC_BUF_LAST(buf) || ARC_BUF_COMPRESSED(buf)));
}

/*
 * Allocate a buf for this hdr. If you care about the data that's in the hdr,
 * or if you want a compressed buffer, pass those flags in. Returns 0 if the
 * copy was made successfully, or an error code otherwise.
 */
static int
arc_buf_alloc_impl(arc_buf_hdr_t *hdr, spa_t *spa, const zbookmark_phys_t *zb,
    void *tag, boolean_t encrypted, boolean_t compressed, boolean_t noauth,
    boolean_t fill, arc_buf_t **ret)
{
	arc_buf_t *buf;
	arc_fill_flags_t flags = ARC_FILL_LOCKED;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT3U(HDR_GET_LSIZE(hdr), >, 0);
	VERIFY(hdr->b_type == ARC_BUFC_DATA ||
	    hdr->b_type == ARC_BUFC_METADATA);
	ASSERT3P(ret, !=, NULL);
	ASSERT3P(*ret, ==, NULL);
	IMPLY(encrypted, compressed);

	hdr->b_l1hdr.b_mru_hits = 0;
	hdr->b_l1hdr.b_mru_ghost_hits = 0;
	hdr->b_l1hdr.b_mfu_hits = 0;
	hdr->b_l1hdr.b_mfu_ghost_hits = 0;
	hdr->b_l1hdr.b_l2_hits = 0;

	buf = *ret = kmem_cache_alloc(buf_cache, KM_PUSHPAGE);
	buf->b_hdr = hdr;
	buf->b_data = NULL;
	buf->b_next = hdr->b_l1hdr.b_buf;
	buf->b_flags = 0;

	add_reference(hdr, tag);

	/*
	 * We're about to change the hdr's b_flags. We must either
	 * hold the hash_lock or be undiscoverable.
	 */
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));

	/*
	 * Only honor requests for compressed bufs if the hdr is actually
	 * compressed. This must be overridden if the buffer is encrypted since
	 * encrypted buffers cannot be decompressed.
	 */
	if (encrypted) {
		buf->b_flags |= ARC_BUF_FLAG_COMPRESSED;
		buf->b_flags |= ARC_BUF_FLAG_ENCRYPTED;
		flags |= ARC_FILL_COMPRESSED | ARC_FILL_ENCRYPTED;
	} else if (compressed &&
	    arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF) {
		buf->b_flags |= ARC_BUF_FLAG_COMPRESSED;
		flags |= ARC_FILL_COMPRESSED;
	}

	if (noauth) {
		ASSERT0(encrypted);
		flags |= ARC_FILL_NOAUTH;
	}

	/*
	 * If the hdr's data can be shared then we share the data buffer and
	 * set the appropriate bit in the hdr's b_flags to indicate the hdr is
	 * sharing it's b_pabd with the arc_buf_t. Otherwise, we allocate a new
	 * buffer to store the buf's data.
	 *
	 * There are two additional restrictions here because we're sharing
	 * hdr -> buf instead of the usual buf -> hdr. First, the hdr can't be
	 * actively involved in an L2ARC write, because if this buf is used by
	 * an arc_write() then the hdr's data buffer will be released when the
	 * write completes, even though the L2ARC write might still be using it.
	 * Second, the hdr's ABD must be linear so that the buf's user doesn't
	 * need to be ABD-aware.  It must be allocated via
	 * zio_[data_]buf_alloc(), not as a page, because we need to be able
	 * to abd_release_ownership_of_buf(), which isn't allowed on "linear
	 * page" buffers because the ABD code needs to handle freeing them
	 * specially.
	 */
	boolean_t can_share = arc_can_share(hdr, buf) &&
	    !HDR_L2_WRITING(hdr) &&
	    hdr->b_l1hdr.b_pabd != NULL &&
	    abd_is_linear(hdr->b_l1hdr.b_pabd) &&
	    !abd_is_linear_page(hdr->b_l1hdr.b_pabd);

	/* Set up b_data and sharing */
	if (can_share) {
		buf->b_data = abd_to_buf(hdr->b_l1hdr.b_pabd);
		buf->b_flags |= ARC_BUF_FLAG_SHARED;
		arc_hdr_set_flags(hdr, ARC_FLAG_SHARED_DATA);
	} else {
		buf->b_data =
		    arc_get_data_buf(hdr, arc_buf_size(buf), buf);
		ARCSTAT_INCR(arcstat_overhead_size, arc_buf_size(buf));
	}
	VERIFY3P(buf->b_data, !=, NULL);

	hdr->b_l1hdr.b_buf = buf;
	hdr->b_l1hdr.b_bufcnt += 1;
	if (encrypted)
		hdr->b_crypt_hdr.b_ebufcnt += 1;

	/*
	 * If the user wants the data from the hdr, we need to either copy or
	 * decompress the data.
	 */
	if (fill) {
		ASSERT3P(zb, !=, NULL);
		return (arc_buf_fill(buf, spa, zb, flags));
	}

	return (0);
}

static char *arc_onloan_tag = "onloan";

static inline void
arc_loaned_bytes_update(int64_t delta)
{
	atomic_add_64(&arc_loaned_bytes, delta);

	/* assert that it did not wrap around */
	ASSERT3S(atomic_add_64_nv(&arc_loaned_bytes, 0), >=, 0);
}

/*
 * Loan out an anonymous arc buffer. Loaned buffers are not counted as in
 * flight data by arc_tempreserve_space() until they are "returned". Loaned
 * buffers must be returned to the arc before they can be used by the DMU or
 * freed.
 */
arc_buf_t *
arc_loan_buf(spa_t *spa, boolean_t is_metadata, int size)
{
	arc_buf_t *buf = arc_alloc_buf(spa, arc_onloan_tag,
	    is_metadata ? ARC_BUFC_METADATA : ARC_BUFC_DATA, size);

	arc_loaned_bytes_update(arc_buf_size(buf));

	return (buf);
}

arc_buf_t *
arc_loan_compressed_buf(spa_t *spa, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel)
{
	arc_buf_t *buf = arc_alloc_compressed_buf(spa, arc_onloan_tag,
	    psize, lsize, compression_type, complevel);

	arc_loaned_bytes_update(arc_buf_size(buf));

	return (buf);
}

arc_buf_t *
arc_loan_raw_buf(spa_t *spa, uint64_t dsobj, boolean_t byteorder,
    const uint8_t *salt, const uint8_t *iv, const uint8_t *mac,
    dmu_object_type_t ot, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel)
{
	arc_buf_t *buf = arc_alloc_raw_buf(spa, arc_onloan_tag, dsobj,
	    byteorder, salt, iv, mac, ot, psize, lsize, compression_type,
	    complevel);

	atomic_add_64(&arc_loaned_bytes, psize);
	return (buf);
}


/*
 * Return a loaned arc buffer to the arc.
 */
void
arc_return_buf(arc_buf_t *buf, void *tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT3P(buf->b_data, !=, NULL);
	ASSERT(HDR_HAS_L1HDR(hdr));
	(void) zfs_refcount_add(&hdr->b_l1hdr.b_refcnt, tag);
	(void) zfs_refcount_remove(&hdr->b_l1hdr.b_refcnt, arc_onloan_tag);

	arc_loaned_bytes_update(-arc_buf_size(buf));
}

/* Detach an arc_buf from a dbuf (tag) */
void
arc_loan_inuse_buf(arc_buf_t *buf, void *tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT3P(buf->b_data, !=, NULL);
	ASSERT(HDR_HAS_L1HDR(hdr));
	(void) zfs_refcount_add(&hdr->b_l1hdr.b_refcnt, arc_onloan_tag);
	(void) zfs_refcount_remove(&hdr->b_l1hdr.b_refcnt, tag);

	arc_loaned_bytes_update(arc_buf_size(buf));
}

static void
l2arc_free_abd_on_write(abd_t *abd, size_t size, arc_buf_contents_t type)
{
	l2arc_data_free_t *df = kmem_alloc(sizeof (*df), KM_SLEEP);

	df->l2df_abd = abd;
	df->l2df_size = size;
	df->l2df_type = type;
	mutex_enter(&l2arc_free_on_write_mtx);
	list_insert_head(l2arc_free_on_write, df);
	mutex_exit(&l2arc_free_on_write_mtx);
}

static void
arc_hdr_free_on_write(arc_buf_hdr_t *hdr, boolean_t free_rdata)
{
	arc_state_t *state = hdr->b_l1hdr.b_state;
	arc_buf_contents_t type = arc_buf_type(hdr);
	uint64_t size = (free_rdata) ? HDR_GET_PSIZE(hdr) : arc_hdr_size(hdr);

	/* protected by hash lock, if in the hash table */
	if (multilist_link_active(&hdr->b_l1hdr.b_arc_node)) {
		ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
		ASSERT(state != arc_anon && state != arc_l2c_only);

		(void) zfs_refcount_remove_many(&state->arcs_esize[type],
		    size, hdr);
	}
	(void) zfs_refcount_remove_many(&state->arcs_size, size, hdr);
	if (type == ARC_BUFC_METADATA) {
		arc_space_return(size, ARC_SPACE_META);
	} else {
		ASSERT(type == ARC_BUFC_DATA);
		arc_space_return(size, ARC_SPACE_DATA);
	}

	if (free_rdata) {
		l2arc_free_abd_on_write(hdr->b_crypt_hdr.b_rabd, size, type);
	} else {
		l2arc_free_abd_on_write(hdr->b_l1hdr.b_pabd, size, type);
	}
}

/*
 * Share the arc_buf_t's data with the hdr. Whenever we are sharing the
 * data buffer, we transfer the refcount ownership to the hdr and update
 * the appropriate kstats.
 */
static void
arc_share_buf(arc_buf_hdr_t *hdr, arc_buf_t *buf)
{
	ASSERT(arc_can_share(hdr, buf));
	ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
	ASSERT(!ARC_BUF_ENCRYPTED(buf));
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));

	/*
	 * Start sharing the data buffer. We transfer the
	 * refcount ownership to the hdr since it always owns
	 * the refcount whenever an arc_buf_t is shared.
	 */
	zfs_refcount_transfer_ownership_many(&hdr->b_l1hdr.b_state->arcs_size,
	    arc_hdr_size(hdr), buf, hdr);
	hdr->b_l1hdr.b_pabd = abd_get_from_buf(buf->b_data, arc_buf_size(buf));
	abd_take_ownership_of_buf(hdr->b_l1hdr.b_pabd,
	    HDR_ISTYPE_METADATA(hdr));
	arc_hdr_set_flags(hdr, ARC_FLAG_SHARED_DATA);
	buf->b_flags |= ARC_BUF_FLAG_SHARED;

	/*
	 * Since we've transferred ownership to the hdr we need
	 * to increment its compressed and uncompressed kstats and
	 * decrement the overhead size.
	 */
	ARCSTAT_INCR(arcstat_compressed_size, arc_hdr_size(hdr));
	ARCSTAT_INCR(arcstat_uncompressed_size, HDR_GET_LSIZE(hdr));
	ARCSTAT_INCR(arcstat_overhead_size, -arc_buf_size(buf));
}

static void
arc_unshare_buf(arc_buf_hdr_t *hdr, arc_buf_t *buf)
{
	ASSERT(arc_buf_is_shared(buf));
	ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));

	/*
	 * We are no longer sharing this buffer so we need
	 * to transfer its ownership to the rightful owner.
	 */
	zfs_refcount_transfer_ownership_many(&hdr->b_l1hdr.b_state->arcs_size,
	    arc_hdr_size(hdr), hdr, buf);
	arc_hdr_clear_flags(hdr, ARC_FLAG_SHARED_DATA);
	abd_release_ownership_of_buf(hdr->b_l1hdr.b_pabd);
	abd_put(hdr->b_l1hdr.b_pabd);
	hdr->b_l1hdr.b_pabd = NULL;
	buf->b_flags &= ~ARC_BUF_FLAG_SHARED;

	/*
	 * Since the buffer is no longer shared between
	 * the arc buf and the hdr, count it as overhead.
	 */
	ARCSTAT_INCR(arcstat_compressed_size, -arc_hdr_size(hdr));
	ARCSTAT_INCR(arcstat_uncompressed_size, -HDR_GET_LSIZE(hdr));
	ARCSTAT_INCR(arcstat_overhead_size, arc_buf_size(buf));
}

/*
 * Remove an arc_buf_t from the hdr's buf list and return the last
 * arc_buf_t on the list. If no buffers remain on the list then return
 * NULL.
 */
static arc_buf_t *
arc_buf_remove(arc_buf_hdr_t *hdr, arc_buf_t *buf)
{
	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(HDR_EMPTY_OR_LOCKED(hdr));

	arc_buf_t **bufp = &hdr->b_l1hdr.b_buf;
	arc_buf_t *lastbuf = NULL;

	/*
	 * Remove the buf from the hdr list and locate the last
	 * remaining buffer on the list.
	 */
	while (*bufp != NULL) {
		if (*bufp == buf)
			*bufp = buf->b_next;

		/*
		 * If we've removed a buffer in the middle of
		 * the list then update the lastbuf and update
		 * bufp.
		 */
		if (*bufp != NULL) {
			lastbuf = *bufp;
			bufp = &(*bufp)->b_next;
		}
	}
	buf->b_next = NULL;
	ASSERT3P(lastbuf, !=, buf);
	IMPLY(hdr->b_l1hdr.b_bufcnt > 0, lastbuf != NULL);
	IMPLY(hdr->b_l1hdr.b_bufcnt > 0, hdr->b_l1hdr.b_buf != NULL);
	IMPLY(lastbuf != NULL, ARC_BUF_LAST(lastbuf));

	return (lastbuf);
}

/*
 * Free up buf->b_data and pull the arc_buf_t off of the arc_buf_hdr_t's
 * list and free it.
 */
static void
arc_buf_destroy_impl(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	/*
	 * Free up the data associated with the buf but only if we're not
	 * sharing this with the hdr. If we are sharing it with the hdr, the
	 * hdr is responsible for doing the free.
	 */
	if (buf->b_data != NULL) {
		/*
		 * We're about to change the hdr's b_flags. We must either
		 * hold the hash_lock or be undiscoverable.
		 */
		ASSERT(HDR_EMPTY_OR_LOCKED(hdr));

		arc_cksum_verify(buf);
		arc_buf_unwatch(buf);

		if (arc_buf_is_shared(buf)) {
			arc_hdr_clear_flags(hdr, ARC_FLAG_SHARED_DATA);
		} else {
			uint64_t size = arc_buf_size(buf);
			arc_free_data_buf(hdr, buf->b_data, size, buf);
			ARCSTAT_INCR(arcstat_overhead_size, -size);
		}
		buf->b_data = NULL;

		ASSERT(hdr->b_l1hdr.b_bufcnt > 0);
		hdr->b_l1hdr.b_bufcnt -= 1;

		if (ARC_BUF_ENCRYPTED(buf)) {
			hdr->b_crypt_hdr.b_ebufcnt -= 1;

			/*
			 * If we have no more encrypted buffers and we've
			 * already gotten a copy of the decrypted data we can
			 * free b_rabd to save some space.
			 */
			if (hdr->b_crypt_hdr.b_ebufcnt == 0 &&
			    HDR_HAS_RABD(hdr) && hdr->b_l1hdr.b_pabd != NULL &&
			    !HDR_IO_IN_PROGRESS(hdr)) {
				arc_hdr_free_abd(hdr, B_TRUE);
			}
		}
	}

	arc_buf_t *lastbuf = arc_buf_remove(hdr, buf);

	if (ARC_BUF_SHARED(buf) && !ARC_BUF_COMPRESSED(buf)) {
		/*
		 * If the current arc_buf_t is sharing its data buffer with the
		 * hdr, then reassign the hdr's b_pabd to share it with the new
		 * buffer at the end of the list. The shared buffer is always
		 * the last one on the hdr's buffer list.
		 *
		 * There is an equivalent case for compressed bufs, but since
		 * they aren't guaranteed to be the last buf in the list and
		 * that is an exceedingly rare case, we just allow that space be
		 * wasted temporarily. We must also be careful not to share
		 * encrypted buffers, since they cannot be shared.
		 */
		if (lastbuf != NULL && !ARC_BUF_ENCRYPTED(lastbuf)) {
			/* Only one buf can be shared at once */
			VERIFY(!arc_buf_is_shared(lastbuf));
			/* hdr is uncompressed so can't have compressed buf */
			VERIFY(!ARC_BUF_COMPRESSED(lastbuf));

			ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);
			arc_hdr_free_abd(hdr, B_FALSE);

			/*
			 * We must setup a new shared block between the
			 * last buffer and the hdr. The data would have
			 * been allocated by the arc buf so we need to transfer
			 * ownership to the hdr since it's now being shared.
			 */
			arc_share_buf(hdr, lastbuf);
		}
	} else if (HDR_SHARED_DATA(hdr)) {
		/*
		 * Uncompressed shared buffers are always at the end
		 * of the list. Compressed buffers don't have the
		 * same requirements. This makes it hard to
		 * simply assert that the lastbuf is shared so
		 * we rely on the hdr's compression flags to determine
		 * if we have a compressed, shared buffer.
		 */
		ASSERT3P(lastbuf, !=, NULL);
		ASSERT(arc_buf_is_shared(lastbuf) ||
		    arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF);
	}

	/*
	 * Free the checksum if we're removing the last uncompressed buf from
	 * this hdr.
	 */
	if (!arc_hdr_has_uncompressed_buf(hdr)) {
		arc_cksum_free(hdr);
	}

	/* clean up the buf */
	buf->b_hdr = NULL;
	kmem_cache_free(buf_cache, buf);
}

static void
arc_hdr_alloc_abd(arc_buf_hdr_t *hdr, int alloc_flags)
{
	uint64_t size;
	boolean_t alloc_rdata = ((alloc_flags & ARC_HDR_ALLOC_RDATA) != 0);
	boolean_t do_adapt = ((alloc_flags & ARC_HDR_DO_ADAPT) != 0);

	ASSERT3U(HDR_GET_LSIZE(hdr), >, 0);
	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(!HDR_SHARED_DATA(hdr) || alloc_rdata);
	IMPLY(alloc_rdata, HDR_PROTECTED(hdr));

	if (alloc_rdata) {
		size = HDR_GET_PSIZE(hdr);
		ASSERT3P(hdr->b_crypt_hdr.b_rabd, ==, NULL);
		hdr->b_crypt_hdr.b_rabd = arc_get_data_abd(hdr, size, hdr,
		    do_adapt);
		ASSERT3P(hdr->b_crypt_hdr.b_rabd, !=, NULL);
		ARCSTAT_INCR(arcstat_raw_size, size);
	} else {
		size = arc_hdr_size(hdr);
		ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
		hdr->b_l1hdr.b_pabd = arc_get_data_abd(hdr, size, hdr,
		    do_adapt);
		ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);
	}

	ARCSTAT_INCR(arcstat_compressed_size, size);
	ARCSTAT_INCR(arcstat_uncompressed_size, HDR_GET_LSIZE(hdr));
}

static void
arc_hdr_free_abd(arc_buf_hdr_t *hdr, boolean_t free_rdata)
{
	uint64_t size = (free_rdata) ? HDR_GET_PSIZE(hdr) : arc_hdr_size(hdr);

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(hdr->b_l1hdr.b_pabd != NULL || HDR_HAS_RABD(hdr));
	IMPLY(free_rdata, HDR_HAS_RABD(hdr));

	/*
	 * If the hdr is currently being written to the l2arc then
	 * we defer freeing the data by adding it to the l2arc_free_on_write
	 * list. The l2arc will free the data once it's finished
	 * writing it to the l2arc device.
	 */
	if (HDR_L2_WRITING(hdr)) {
		arc_hdr_free_on_write(hdr, free_rdata);
		ARCSTAT_BUMP(arcstat_l2_free_on_write);
	} else if (free_rdata) {
		arc_free_data_abd(hdr, hdr->b_crypt_hdr.b_rabd, size, hdr);
	} else {
		arc_free_data_abd(hdr, hdr->b_l1hdr.b_pabd, size, hdr);
	}

	if (free_rdata) {
		hdr->b_crypt_hdr.b_rabd = NULL;
		ARCSTAT_INCR(arcstat_raw_size, -size);
	} else {
		hdr->b_l1hdr.b_pabd = NULL;
	}

	if (hdr->b_l1hdr.b_pabd == NULL && !HDR_HAS_RABD(hdr))
		hdr->b_l1hdr.b_byteswap = DMU_BSWAP_NUMFUNCS;

	ARCSTAT_INCR(arcstat_compressed_size, -size);
	ARCSTAT_INCR(arcstat_uncompressed_size, -HDR_GET_LSIZE(hdr));
}

static arc_buf_hdr_t *
arc_hdr_alloc(uint64_t spa, int32_t psize, int32_t lsize,
    boolean_t protected, enum zio_compress compression_type, uint8_t complevel,
    arc_buf_contents_t type, boolean_t alloc_rdata)
{
	arc_buf_hdr_t *hdr;
	int flags = ARC_HDR_DO_ADAPT;

	VERIFY(type == ARC_BUFC_DATA || type == ARC_BUFC_METADATA);
	if (protected) {
		hdr = kmem_cache_alloc(hdr_full_crypt_cache, KM_PUSHPAGE);
	} else {
		hdr = kmem_cache_alloc(hdr_full_cache, KM_PUSHPAGE);
	}
	flags |= alloc_rdata ? ARC_HDR_ALLOC_RDATA : 0;

	ASSERT(HDR_EMPTY(hdr));
	ASSERT3P(hdr->b_l1hdr.b_freeze_cksum, ==, NULL);
	HDR_SET_PSIZE(hdr, psize);
	HDR_SET_LSIZE(hdr, lsize);
	hdr->b_spa = spa;
	hdr->b_type = type;
	hdr->b_flags = 0;
	arc_hdr_set_flags(hdr, arc_bufc_to_flags(type) | ARC_FLAG_HAS_L1HDR);
	arc_hdr_set_compress(hdr, compression_type);
	hdr->b_complevel = complevel;
	if (protected)
		arc_hdr_set_flags(hdr, ARC_FLAG_PROTECTED);

	hdr->b_l1hdr.b_state = arc_anon;
	hdr->b_l1hdr.b_arc_access = 0;
	hdr->b_l1hdr.b_bufcnt = 0;
	hdr->b_l1hdr.b_buf = NULL;

	/*
	 * Allocate the hdr's buffer. This will contain either
	 * the compressed or uncompressed data depending on the block
	 * it references and compressed arc enablement.
	 */
	arc_hdr_alloc_abd(hdr, flags);
	ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));

	return (hdr);
}

/*
 * Transition between the two allocation states for the arc_buf_hdr struct.
 * The arc_buf_hdr struct can be allocated with (hdr_full_cache) or without
 * (hdr_l2only_cache) the fields necessary for the L1 cache - the smaller
 * version is used when a cache buffer is only in the L2ARC in order to reduce
 * memory usage.
 */
static arc_buf_hdr_t *
arc_hdr_realloc(arc_buf_hdr_t *hdr, kmem_cache_t *old, kmem_cache_t *new)
{
	ASSERT(HDR_HAS_L2HDR(hdr));

	arc_buf_hdr_t *nhdr;
	l2arc_dev_t *dev = hdr->b_l2hdr.b_dev;

	ASSERT((old == hdr_full_cache && new == hdr_l2only_cache) ||
	    (old == hdr_l2only_cache && new == hdr_full_cache));

	/*
	 * if the caller wanted a new full header and the header is to be
	 * encrypted we will actually allocate the header from the full crypt
	 * cache instead. The same applies to freeing from the old cache.
	 */
	if (HDR_PROTECTED(hdr) && new == hdr_full_cache)
		new = hdr_full_crypt_cache;
	if (HDR_PROTECTED(hdr) && old == hdr_full_cache)
		old = hdr_full_crypt_cache;

	nhdr = kmem_cache_alloc(new, KM_PUSHPAGE);

	ASSERT(MUTEX_HELD(HDR_LOCK(hdr)));
	buf_hash_remove(hdr);

	bcopy(hdr, nhdr, HDR_L2ONLY_SIZE);

	if (new == hdr_full_cache || new == hdr_full_crypt_cache) {
		arc_hdr_set_flags(nhdr, ARC_FLAG_HAS_L1HDR);
		/*
		 * arc_access and arc_change_state need to be aware that a
		 * header has just come out of L2ARC, so we set its state to
		 * l2c_only even though it's about to change.
		 */
		nhdr->b_l1hdr.b_state = arc_l2c_only;

		/* Verify previous threads set to NULL before freeing */
		ASSERT3P(nhdr->b_l1hdr.b_pabd, ==, NULL);
		ASSERT(!HDR_HAS_RABD(hdr));
	} else {
		ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
		ASSERT0(hdr->b_l1hdr.b_bufcnt);
		ASSERT3P(hdr->b_l1hdr.b_freeze_cksum, ==, NULL);

		/*
		 * If we've reached here, We must have been called from
		 * arc_evict_hdr(), as such we should have already been
		 * removed from any ghost list we were previously on
		 * (which protects us from racing with arc_evict_state),
		 * thus no locking is needed during this check.
		 */
		ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));

		/*
		 * A buffer must not be moved into the arc_l2c_only
		 * state if it's not finished being written out to the
		 * l2arc device. Otherwise, the b_l1hdr.b_pabd field
		 * might try to be accessed, even though it was removed.
		 */
		VERIFY(!HDR_L2_WRITING(hdr));
		VERIFY3P(hdr->b_l1hdr.b_pabd, ==, NULL);
		ASSERT(!HDR_HAS_RABD(hdr));

		arc_hdr_clear_flags(nhdr, ARC_FLAG_HAS_L1HDR);
	}
	/*
	 * The header has been reallocated so we need to re-insert it into any
	 * lists it was on.
	 */
	(void) buf_hash_insert(nhdr, NULL);

	ASSERT(list_link_active(&hdr->b_l2hdr.b_l2node));

	mutex_enter(&dev->l2ad_mtx);

	/*
	 * We must place the realloc'ed header back into the list at
	 * the same spot. Otherwise, if it's placed earlier in the list,
	 * l2arc_write_buffers() could find it during the function's
	 * write phase, and try to write it out to the l2arc.
	 */
	list_insert_after(&dev->l2ad_buflist, hdr, nhdr);
	list_remove(&dev->l2ad_buflist, hdr);

	mutex_exit(&dev->l2ad_mtx);

	/*
	 * Since we're using the pointer address as the tag when
	 * incrementing and decrementing the l2ad_alloc refcount, we
	 * must remove the old pointer (that we're about to destroy) and
	 * add the new pointer to the refcount. Otherwise we'd remove
	 * the wrong pointer address when calling arc_hdr_destroy() later.
	 */

	(void) zfs_refcount_remove_many(&dev->l2ad_alloc,
	    arc_hdr_size(hdr), hdr);
	(void) zfs_refcount_add_many(&dev->l2ad_alloc,
	    arc_hdr_size(nhdr), nhdr);

	buf_discard_identity(hdr);
	kmem_cache_free(old, hdr);

	return (nhdr);
}

/*
 * This function allows an L1 header to be reallocated as a crypt
 * header and vice versa. If we are going to a crypt header, the
 * new fields will be zeroed out.
 */
static arc_buf_hdr_t *
arc_hdr_realloc_crypt(arc_buf_hdr_t *hdr, boolean_t need_crypt)
{
	arc_buf_hdr_t *nhdr;
	arc_buf_t *buf;
	kmem_cache_t *ncache, *ocache;
	unsigned nsize, osize;

	/*
	 * This function requires that hdr is in the arc_anon state.
	 * Therefore it won't have any L2ARC data for us to worry
	 * about copying.
	 */
	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(!HDR_HAS_L2HDR(hdr));
	ASSERT3U(!!HDR_PROTECTED(hdr), !=, need_crypt);
	ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);
	ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
	ASSERT(!list_link_active(&hdr->b_l2hdr.b_l2node));
	ASSERT3P(hdr->b_hash_next, ==, NULL);

	if (need_crypt) {
		ncache = hdr_full_crypt_cache;
		nsize = sizeof (hdr->b_crypt_hdr);
		ocache = hdr_full_cache;
		osize = HDR_FULL_SIZE;
	} else {
		ncache = hdr_full_cache;
		nsize = HDR_FULL_SIZE;
		ocache = hdr_full_crypt_cache;
		osize = sizeof (hdr->b_crypt_hdr);
	}

	nhdr = kmem_cache_alloc(ncache, KM_PUSHPAGE);

	/*
	 * Copy all members that aren't locks or condvars to the new header.
	 * No lists are pointing to us (as we asserted above), so we don't
	 * need to worry about the list nodes.
	 */
	nhdr->b_dva = hdr->b_dva;
	nhdr->b_birth = hdr->b_birth;
	nhdr->b_type = hdr->b_type;
	nhdr->b_flags = hdr->b_flags;
	nhdr->b_psize = hdr->b_psize;
	nhdr->b_lsize = hdr->b_lsize;
	nhdr->b_spa = hdr->b_spa;
	nhdr->b_l1hdr.b_freeze_cksum = hdr->b_l1hdr.b_freeze_cksum;
	nhdr->b_l1hdr.b_bufcnt = hdr->b_l1hdr.b_bufcnt;
	nhdr->b_l1hdr.b_byteswap = hdr->b_l1hdr.b_byteswap;
	nhdr->b_l1hdr.b_state = hdr->b_l1hdr.b_state;
	nhdr->b_l1hdr.b_arc_access = hdr->b_l1hdr.b_arc_access;
	nhdr->b_l1hdr.b_mru_hits = hdr->b_l1hdr.b_mru_hits;
	nhdr->b_l1hdr.b_mru_ghost_hits = hdr->b_l1hdr.b_mru_ghost_hits;
	nhdr->b_l1hdr.b_mfu_hits = hdr->b_l1hdr.b_mfu_hits;
	nhdr->b_l1hdr.b_mfu_ghost_hits = hdr->b_l1hdr.b_mfu_ghost_hits;
	nhdr->b_l1hdr.b_l2_hits = hdr->b_l1hdr.b_l2_hits;
	nhdr->b_l1hdr.b_acb = hdr->b_l1hdr.b_acb;
	nhdr->b_l1hdr.b_pabd = hdr->b_l1hdr.b_pabd;

	/*
	 * This zfs_refcount_add() exists only to ensure that the individual
	 * arc buffers always point to a header that is referenced, avoiding
	 * a small race condition that could trigger ASSERTs.
	 */
	(void) zfs_refcount_add(&nhdr->b_l1hdr.b_refcnt, FTAG);
	nhdr->b_l1hdr.b_buf = hdr->b_l1hdr.b_buf;
	for (buf = nhdr->b_l1hdr.b_buf; buf != NULL; buf = buf->b_next) {
		mutex_enter(&buf->b_evict_lock);
		buf->b_hdr = nhdr;
		mutex_exit(&buf->b_evict_lock);
	}

	zfs_refcount_transfer(&nhdr->b_l1hdr.b_refcnt, &hdr->b_l1hdr.b_refcnt);
	(void) zfs_refcount_remove(&nhdr->b_l1hdr.b_refcnt, FTAG);
	ASSERT0(zfs_refcount_count(&hdr->b_l1hdr.b_refcnt));

	if (need_crypt) {
		arc_hdr_set_flags(nhdr, ARC_FLAG_PROTECTED);
	} else {
		arc_hdr_clear_flags(nhdr, ARC_FLAG_PROTECTED);
	}

	/* unset all members of the original hdr */
	bzero(&hdr->b_dva, sizeof (dva_t));
	hdr->b_birth = 0;
	hdr->b_type = ARC_BUFC_INVALID;
	hdr->b_flags = 0;
	hdr->b_psize = 0;
	hdr->b_lsize = 0;
	hdr->b_spa = 0;
	hdr->b_l1hdr.b_freeze_cksum = NULL;
	hdr->b_l1hdr.b_buf = NULL;
	hdr->b_l1hdr.b_bufcnt = 0;
	hdr->b_l1hdr.b_byteswap = 0;
	hdr->b_l1hdr.b_state = NULL;
	hdr->b_l1hdr.b_arc_access = 0;
	hdr->b_l1hdr.b_mru_hits = 0;
	hdr->b_l1hdr.b_mru_ghost_hits = 0;
	hdr->b_l1hdr.b_mfu_hits = 0;
	hdr->b_l1hdr.b_mfu_ghost_hits = 0;
	hdr->b_l1hdr.b_l2_hits = 0;
	hdr->b_l1hdr.b_acb = NULL;
	hdr->b_l1hdr.b_pabd = NULL;

	if (ocache == hdr_full_crypt_cache) {
		ASSERT(!HDR_HAS_RABD(hdr));
		hdr->b_crypt_hdr.b_ot = DMU_OT_NONE;
		hdr->b_crypt_hdr.b_ebufcnt = 0;
		hdr->b_crypt_hdr.b_dsobj = 0;
		bzero(hdr->b_crypt_hdr.b_salt, ZIO_DATA_SALT_LEN);
		bzero(hdr->b_crypt_hdr.b_iv, ZIO_DATA_IV_LEN);
		bzero(hdr->b_crypt_hdr.b_mac, ZIO_DATA_MAC_LEN);
	}

	buf_discard_identity(hdr);
	kmem_cache_free(ocache, hdr);

	return (nhdr);
}

/*
 * This function is used by the send / receive code to convert a newly
 * allocated arc_buf_t to one that is suitable for a raw encrypted write. It
 * is also used to allow the root objset block to be updated without altering
 * its embedded MACs. Both block types will always be uncompressed so we do not
 * have to worry about compression type or psize.
 */
void
arc_convert_to_raw(arc_buf_t *buf, uint64_t dsobj, boolean_t byteorder,
    dmu_object_type_t ot, const uint8_t *salt, const uint8_t *iv,
    const uint8_t *mac)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(ot == DMU_OT_DNODE || ot == DMU_OT_OBJSET);
	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);

	buf->b_flags |= (ARC_BUF_FLAG_COMPRESSED | ARC_BUF_FLAG_ENCRYPTED);
	if (!HDR_PROTECTED(hdr))
		hdr = arc_hdr_realloc_crypt(hdr, B_TRUE);
	hdr->b_crypt_hdr.b_dsobj = dsobj;
	hdr->b_crypt_hdr.b_ot = ot;
	hdr->b_l1hdr.b_byteswap = (byteorder == ZFS_HOST_BYTEORDER) ?
	    DMU_BSWAP_NUMFUNCS : DMU_OT_BYTESWAP(ot);
	if (!arc_hdr_has_uncompressed_buf(hdr))
		arc_cksum_free(hdr);

	if (salt != NULL)
		bcopy(salt, hdr->b_crypt_hdr.b_salt, ZIO_DATA_SALT_LEN);
	if (iv != NULL)
		bcopy(iv, hdr->b_crypt_hdr.b_iv, ZIO_DATA_IV_LEN);
	if (mac != NULL)
		bcopy(mac, hdr->b_crypt_hdr.b_mac, ZIO_DATA_MAC_LEN);
}

/*
 * Allocate a new arc_buf_hdr_t and arc_buf_t and return the buf to the caller.
 * The buf is returned thawed since we expect the consumer to modify it.
 */
arc_buf_t *
arc_alloc_buf(spa_t *spa, void *tag, arc_buf_contents_t type, int32_t size)
{
	arc_buf_hdr_t *hdr = arc_hdr_alloc(spa_load_guid(spa), size, size,
	    B_FALSE, ZIO_COMPRESS_OFF, 0, type, B_FALSE);

	arc_buf_t *buf = NULL;
	VERIFY0(arc_buf_alloc_impl(hdr, spa, NULL, tag, B_FALSE, B_FALSE,
	    B_FALSE, B_FALSE, &buf));
	arc_buf_thaw(buf);

	return (buf);
}

/*
 * Allocate a compressed buf in the same manner as arc_alloc_buf. Don't use this
 * for bufs containing metadata.
 */
arc_buf_t *
arc_alloc_compressed_buf(spa_t *spa, void *tag, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel)
{
	ASSERT3U(lsize, >, 0);
	ASSERT3U(lsize, >=, psize);
	ASSERT3U(compression_type, >, ZIO_COMPRESS_OFF);
	ASSERT3U(compression_type, <, ZIO_COMPRESS_FUNCTIONS);

	arc_buf_hdr_t *hdr = arc_hdr_alloc(spa_load_guid(spa), psize, lsize,
	    B_FALSE, compression_type, complevel, ARC_BUFC_DATA, B_FALSE);

	arc_buf_t *buf = NULL;
	VERIFY0(arc_buf_alloc_impl(hdr, spa, NULL, tag, B_FALSE,
	    B_TRUE, B_FALSE, B_FALSE, &buf));
	arc_buf_thaw(buf);
	ASSERT3P(hdr->b_l1hdr.b_freeze_cksum, ==, NULL);

	if (!arc_buf_is_shared(buf)) {
		/*
		 * To ensure that the hdr has the correct data in it if we call
		 * arc_untransform() on this buf before it's been written to
		 * disk, it's easiest if we just set up sharing between the
		 * buf and the hdr.
		 */
		arc_hdr_free_abd(hdr, B_FALSE);
		arc_share_buf(hdr, buf);
	}

	return (buf);
}

arc_buf_t *
arc_alloc_raw_buf(spa_t *spa, void *tag, uint64_t dsobj, boolean_t byteorder,
    const uint8_t *salt, const uint8_t *iv, const uint8_t *mac,
    dmu_object_type_t ot, uint64_t psize, uint64_t lsize,
    enum zio_compress compression_type, uint8_t complevel)
{
	arc_buf_hdr_t *hdr;
	arc_buf_t *buf;
	arc_buf_contents_t type = DMU_OT_IS_METADATA(ot) ?
	    ARC_BUFC_METADATA : ARC_BUFC_DATA;

	ASSERT3U(lsize, >, 0);
	ASSERT3U(lsize, >=, psize);
	ASSERT3U(compression_type, >=, ZIO_COMPRESS_OFF);
	ASSERT3U(compression_type, <, ZIO_COMPRESS_FUNCTIONS);

	hdr = arc_hdr_alloc(spa_load_guid(spa), psize, lsize, B_TRUE,
	    compression_type, complevel, type, B_TRUE);

	hdr->b_crypt_hdr.b_dsobj = dsobj;
	hdr->b_crypt_hdr.b_ot = ot;
	hdr->b_l1hdr.b_byteswap = (byteorder == ZFS_HOST_BYTEORDER) ?
	    DMU_BSWAP_NUMFUNCS : DMU_OT_BYTESWAP(ot);
	bcopy(salt, hdr->b_crypt_hdr.b_salt, ZIO_DATA_SALT_LEN);
	bcopy(iv, hdr->b_crypt_hdr.b_iv, ZIO_DATA_IV_LEN);
	bcopy(mac, hdr->b_crypt_hdr.b_mac, ZIO_DATA_MAC_LEN);

	/*
	 * This buffer will be considered encrypted even if the ot is not an
	 * encrypted type. It will become authenticated instead in
	 * arc_write_ready().
	 */
	buf = NULL;
	VERIFY0(arc_buf_alloc_impl(hdr, spa, NULL, tag, B_TRUE, B_TRUE,
	    B_FALSE, B_FALSE, &buf));
	arc_buf_thaw(buf);
	ASSERT3P(hdr->b_l1hdr.b_freeze_cksum, ==, NULL);

	return (buf);
}

static void
l2arc_hdr_arcstats_update(arc_buf_hdr_t *hdr, boolean_t incr,
    boolean_t state_only)
{
	l2arc_buf_hdr_t *l2hdr = &hdr->b_l2hdr;
	l2arc_dev_t *dev = l2hdr->b_dev;
	uint64_t lsize = HDR_GET_LSIZE(hdr);
	uint64_t psize = HDR_GET_PSIZE(hdr);
	uint64_t asize = vdev_psize_to_asize(dev->l2ad_vdev, psize);
	arc_buf_contents_t type = hdr->b_type;
	int64_t lsize_s;
	int64_t psize_s;
	int64_t asize_s;

	if (incr) {
		lsize_s = lsize;
		psize_s = psize;
		asize_s = asize;
	} else {
		lsize_s = -lsize;
		psize_s = -psize;
		asize_s = -asize;
	}

	/* If the buffer is a prefetch, count it as such. */
	if (HDR_PREFETCH(hdr)) {
		ARCSTAT_INCR(arcstat_l2_prefetch_asize, asize_s);
	} else {
		/*
		 * We use the value stored in the L2 header upon initial
		 * caching in L2ARC. This value will be updated in case
		 * an MRU/MRU_ghost buffer transitions to MFU but the L2ARC
		 * metadata (log entry) cannot currently be updated. Having
		 * the ARC state in the L2 header solves the problem of a
		 * possibly absent L1 header (apparent in buffers restored
		 * from persistent L2ARC).
		 */
		switch (hdr->b_l2hdr.b_arcs_state) {
			case ARC_STATE_MRU_GHOST:
			case ARC_STATE_MRU:
				ARCSTAT_INCR(arcstat_l2_mru_asize, asize_s);
				break;
			case ARC_STATE_MFU_GHOST:
			case ARC_STATE_MFU:
				ARCSTAT_INCR(arcstat_l2_mfu_asize, asize_s);
				break;
			default:
				break;
		}
	}

	if (state_only)
		return;

	ARCSTAT_INCR(arcstat_l2_psize, psize_s);
	ARCSTAT_INCR(arcstat_l2_lsize, lsize_s);

	switch (type) {
		case ARC_BUFC_DATA:
			ARCSTAT_INCR(arcstat_l2_bufc_data_asize, asize_s);
			break;
		case ARC_BUFC_METADATA:
			ARCSTAT_INCR(arcstat_l2_bufc_metadata_asize, asize_s);
			break;
		default:
			break;
	}
}


static void
arc_hdr_l2hdr_destroy(arc_buf_hdr_t *hdr)
{
	l2arc_buf_hdr_t *l2hdr = &hdr->b_l2hdr;
	l2arc_dev_t *dev = l2hdr->b_dev;
	uint64_t psize = HDR_GET_PSIZE(hdr);
	uint64_t asize = vdev_psize_to_asize(dev->l2ad_vdev, psize);

	ASSERT(MUTEX_HELD(&dev->l2ad_mtx));
	ASSERT(HDR_HAS_L2HDR(hdr));

	list_remove(&dev->l2ad_buflist, hdr);

	l2arc_hdr_arcstats_decrement(hdr);
	vdev_space_update(dev->l2ad_vdev, -asize, 0, 0);

	(void) zfs_refcount_remove_many(&dev->l2ad_alloc, arc_hdr_size(hdr),
	    hdr);
	arc_hdr_clear_flags(hdr, ARC_FLAG_HAS_L2HDR);
}

static void
arc_hdr_destroy(arc_buf_hdr_t *hdr)
{
	if (HDR_HAS_L1HDR(hdr)) {
		ASSERT(hdr->b_l1hdr.b_buf == NULL ||
		    hdr->b_l1hdr.b_bufcnt > 0);
		ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
		ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);
	}
	ASSERT(!HDR_IO_IN_PROGRESS(hdr));
	ASSERT(!HDR_IN_HASH_TABLE(hdr));

	if (HDR_HAS_L2HDR(hdr)) {
		l2arc_dev_t *dev = hdr->b_l2hdr.b_dev;
		boolean_t buflist_held = MUTEX_HELD(&dev->l2ad_mtx);

		if (!buflist_held)
			mutex_enter(&dev->l2ad_mtx);

		/*
		 * Even though we checked this conditional above, we
		 * need to check this again now that we have the
		 * l2ad_mtx. This is because we could be racing with
		 * another thread calling l2arc_evict() which might have
		 * destroyed this header's L2 portion as we were waiting
		 * to acquire the l2ad_mtx. If that happens, we don't
		 * want to re-destroy the header's L2 portion.
		 */
		if (HDR_HAS_L2HDR(hdr))
			arc_hdr_l2hdr_destroy(hdr);

		if (!buflist_held)
			mutex_exit(&dev->l2ad_mtx);
	}

	/*
	 * The header's identify can only be safely discarded once it is no
	 * longer discoverable.  This requires removing it from the hash table
	 * and the l2arc header list.  After this point the hash lock can not
	 * be used to protect the header.
	 */
	if (!HDR_EMPTY(hdr))
		buf_discard_identity(hdr);

	if (HDR_HAS_L1HDR(hdr)) {
		arc_cksum_free(hdr);

		while (hdr->b_l1hdr.b_buf != NULL)
			arc_buf_destroy_impl(hdr->b_l1hdr.b_buf);

		if (hdr->b_l1hdr.b_pabd != NULL)
			arc_hdr_free_abd(hdr, B_FALSE);

		if (HDR_HAS_RABD(hdr))
			arc_hdr_free_abd(hdr, B_TRUE);
	}

	ASSERT3P(hdr->b_hash_next, ==, NULL);
	if (HDR_HAS_L1HDR(hdr)) {
		ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
		ASSERT3P(hdr->b_l1hdr.b_acb, ==, NULL);

		if (!HDR_PROTECTED(hdr)) {
			kmem_cache_free(hdr_full_cache, hdr);
		} else {
			kmem_cache_free(hdr_full_crypt_cache, hdr);
		}
	} else {
		kmem_cache_free(hdr_l2only_cache, hdr);
	}
}

void
arc_buf_destroy(arc_buf_t *buf, void* tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	if (hdr->b_l1hdr.b_state == arc_anon) {
		ASSERT3U(hdr->b_l1hdr.b_bufcnt, ==, 1);
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		VERIFY0(remove_reference(hdr, NULL, tag));
		arc_hdr_destroy(hdr);
		return;
	}

	kmutex_t *hash_lock = HDR_LOCK(hdr);
	mutex_enter(hash_lock);

	ASSERT3P(hdr, ==, buf->b_hdr);
	ASSERT(hdr->b_l1hdr.b_bufcnt > 0);
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));
	ASSERT3P(hdr->b_l1hdr.b_state, !=, arc_anon);
	ASSERT3P(buf->b_data, !=, NULL);

	(void) remove_reference(hdr, hash_lock, tag);
	arc_buf_destroy_impl(buf);
	mutex_exit(hash_lock);
}

/*
 * Evict the arc_buf_hdr that is provided as a parameter. The resultant
 * state of the header is dependent on its state prior to entering this
 * function. The following transitions are possible:
 *
 *    - arc_mru -> arc_mru_ghost
 *    - arc_mfu -> arc_mfu_ghost
 *    - arc_mru_ghost -> arc_l2c_only
 *    - arc_mru_ghost -> deleted
 *    - arc_mfu_ghost -> arc_l2c_only
 *    - arc_mfu_ghost -> deleted
 */
static int64_t
arc_evict_hdr(arc_buf_hdr_t *hdr, kmutex_t *hash_lock)
{
	arc_state_t *evicted_state, *state;
	int64_t bytes_evicted = 0;
	int min_lifetime = HDR_PRESCIENT_PREFETCH(hdr) ?
	    arc_min_prescient_prefetch_ms : arc_min_prefetch_ms;

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT(HDR_HAS_L1HDR(hdr));

	state = hdr->b_l1hdr.b_state;
	if (GHOST_STATE(state)) {
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);

		/*
		 * l2arc_write_buffers() relies on a header's L1 portion
		 * (i.e. its b_pabd field) during it's write phase.
		 * Thus, we cannot push a header onto the arc_l2c_only
		 * state (removing its L1 piece) until the header is
		 * done being written to the l2arc.
		 */
		if (HDR_HAS_L2HDR(hdr) && HDR_L2_WRITING(hdr)) {
			ARCSTAT_BUMP(arcstat_evict_l2_skip);
			return (bytes_evicted);
		}

		ARCSTAT_BUMP(arcstat_deleted);
		bytes_evicted += HDR_GET_LSIZE(hdr);

		DTRACE_PROBE1(arc__delete, arc_buf_hdr_t *, hdr);

		if (HDR_HAS_L2HDR(hdr)) {
			ASSERT(hdr->b_l1hdr.b_pabd == NULL);
			ASSERT(!HDR_HAS_RABD(hdr));
			/*
			 * This buffer is cached on the 2nd Level ARC;
			 * don't destroy the header.
			 */
			arc_change_state(arc_l2c_only, hdr, hash_lock);
			/*
			 * dropping from L1+L2 cached to L2-only,
			 * realloc to remove the L1 header.
			 */
			hdr = arc_hdr_realloc(hdr, hdr_full_cache,
			    hdr_l2only_cache);
		} else {
			arc_change_state(arc_anon, hdr, hash_lock);
			arc_hdr_destroy(hdr);
		}
		return (bytes_evicted);
	}

	ASSERT(state == arc_mru || state == arc_mfu);
	evicted_state = (state == arc_mru) ? arc_mru_ghost : arc_mfu_ghost;

	/* prefetch buffers have a minimum lifespan */
	if (HDR_IO_IN_PROGRESS(hdr) ||
	    ((hdr->b_flags & (ARC_FLAG_PREFETCH | ARC_FLAG_INDIRECT)) &&
	    ddi_get_lbolt() - hdr->b_l1hdr.b_arc_access <
	    MSEC_TO_TICK(min_lifetime))) {
		ARCSTAT_BUMP(arcstat_evict_skip);
		return (bytes_evicted);
	}

	ASSERT0(zfs_refcount_count(&hdr->b_l1hdr.b_refcnt));
	while (hdr->b_l1hdr.b_buf) {
		arc_buf_t *buf = hdr->b_l1hdr.b_buf;
		if (!mutex_tryenter(&buf->b_evict_lock)) {
			ARCSTAT_BUMP(arcstat_mutex_miss);
			break;
		}
		if (buf->b_data != NULL)
			bytes_evicted += HDR_GET_LSIZE(hdr);
		mutex_exit(&buf->b_evict_lock);
		arc_buf_destroy_impl(buf);
	}

	if (HDR_HAS_L2HDR(hdr)) {
		ARCSTAT_INCR(arcstat_evict_l2_cached, HDR_GET_LSIZE(hdr));
	} else {
		if (l2arc_write_eligible(hdr->b_spa, hdr)) {
			ARCSTAT_INCR(arcstat_evict_l2_eligible,
			    HDR_GET_LSIZE(hdr));

			switch (state->arcs_state) {
				case ARC_STATE_MRU:
					ARCSTAT_INCR(
					    arcstat_evict_l2_eligible_mru,
					    HDR_GET_LSIZE(hdr));
					break;
				case ARC_STATE_MFU:
					ARCSTAT_INCR(
					    arcstat_evict_l2_eligible_mfu,
					    HDR_GET_LSIZE(hdr));
					break;
				default:
					break;
			}
		} else {
			ARCSTAT_INCR(arcstat_evict_l2_ineligible,
			    HDR_GET_LSIZE(hdr));
		}
	}

	if (hdr->b_l1hdr.b_bufcnt == 0) {
		arc_cksum_free(hdr);

		bytes_evicted += arc_hdr_size(hdr);

		/*
		 * If this hdr is being evicted and has a compressed
		 * buffer then we discard it here before we change states.
		 * This ensures that the accounting is updated correctly
		 * in arc_free_data_impl().
		 */
		if (hdr->b_l1hdr.b_pabd != NULL)
			arc_hdr_free_abd(hdr, B_FALSE);

		if (HDR_HAS_RABD(hdr))
			arc_hdr_free_abd(hdr, B_TRUE);

		arc_change_state(evicted_state, hdr, hash_lock);
		ASSERT(HDR_IN_HASH_TABLE(hdr));
		arc_hdr_set_flags(hdr, ARC_FLAG_IN_HASH_TABLE);
		DTRACE_PROBE1(arc__evict, arc_buf_hdr_t *, hdr);
	}

	return (bytes_evicted);
}

static void
arc_set_need_free(void)
{
	ASSERT(MUTEX_HELD(&arc_evict_lock));
	int64_t remaining = arc_free_memory() - arc_sys_free / 2;
	arc_evict_waiter_t *aw = list_tail(&arc_evict_waiters);
	if (aw == NULL) {
		arc_need_free = MAX(-remaining, 0);
	} else {
		arc_need_free =
		    MAX(-remaining, (int64_t)(aw->aew_count - arc_evict_count));
	}
}

static uint64_t
arc_evict_state_impl(multilist_t *ml, int idx, arc_buf_hdr_t *marker,
    uint64_t spa, int64_t bytes)
{
	multilist_sublist_t *mls;
	uint64_t bytes_evicted = 0;
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;
	int evict_count = 0;

	ASSERT3P(marker, !=, NULL);
	IMPLY(bytes < 0, bytes == ARC_EVICT_ALL);

	mls = multilist_sublist_lock(ml, idx);

	for (hdr = multilist_sublist_prev(mls, marker); hdr != NULL;
	    hdr = multilist_sublist_prev(mls, marker)) {
		if ((bytes != ARC_EVICT_ALL && bytes_evicted >= bytes) ||
		    (evict_count >= zfs_arc_evict_batch_limit))
			break;

		/*
		 * To keep our iteration location, move the marker
		 * forward. Since we're not holding hdr's hash lock, we
		 * must be very careful and not remove 'hdr' from the
		 * sublist. Otherwise, other consumers might mistake the
		 * 'hdr' as not being on a sublist when they call the
		 * multilist_link_active() function (they all rely on
		 * the hash lock protecting concurrent insertions and
		 * removals). multilist_sublist_move_forward() was
		 * specifically implemented to ensure this is the case
		 * (only 'marker' will be removed and re-inserted).
		 */
		multilist_sublist_move_forward(mls, marker);

		/*
		 * The only case where the b_spa field should ever be
		 * zero, is the marker headers inserted by
		 * arc_evict_state(). It's possible for multiple threads
		 * to be calling arc_evict_state() concurrently (e.g.
		 * dsl_pool_close() and zio_inject_fault()), so we must
		 * skip any markers we see from these other threads.
		 */
		if (hdr->b_spa == 0)
			continue;

		/* we're only interested in evicting buffers of a certain spa */
		if (spa != 0 && hdr->b_spa != spa) {
			ARCSTAT_BUMP(arcstat_evict_skip);
			continue;
		}

		hash_lock = HDR_LOCK(hdr);

		/*
		 * We aren't calling this function from any code path
		 * that would already be holding a hash lock, so we're
		 * asserting on this assumption to be defensive in case
		 * this ever changes. Without this check, it would be
		 * possible to incorrectly increment arcstat_mutex_miss
		 * below (e.g. if the code changed such that we called
		 * this function with a hash lock held).
		 */
		ASSERT(!MUTEX_HELD(hash_lock));

		if (mutex_tryenter(hash_lock)) {
			uint64_t evicted = arc_evict_hdr(hdr, hash_lock);
			mutex_exit(hash_lock);

			bytes_evicted += evicted;

			/*
			 * If evicted is zero, arc_evict_hdr() must have
			 * decided to skip this header, don't increment
			 * evict_count in this case.
			 */
			if (evicted != 0)
				evict_count++;

		} else {
			ARCSTAT_BUMP(arcstat_mutex_miss);
		}
	}

	multilist_sublist_unlock(mls);

	/*
	 * Increment the count of evicted bytes, and wake up any threads that
	 * are waiting for the count to reach this value.  Since the list is
	 * ordered by ascending aew_count, we pop off the beginning of the
	 * list until we reach the end, or a waiter that's past the current
	 * "count".  Doing this outside the loop reduces the number of times
	 * we need to acquire the global arc_evict_lock.
	 *
	 * Only wake when there's sufficient free memory in the system
	 * (specifically, arc_sys_free/2, which by default is a bit more than
	 * 1/64th of RAM).  See the comments in arc_wait_for_eviction().
	 */
	mutex_enter(&arc_evict_lock);
	arc_evict_count += bytes_evicted;

	if ((int64_t)(arc_free_memory() - arc_sys_free / 2) > 0) {
		arc_evict_waiter_t *aw;
		while ((aw = list_head(&arc_evict_waiters)) != NULL &&
		    aw->aew_count <= arc_evict_count) {
			list_remove(&arc_evict_waiters, aw);
			cv_broadcast(&aw->aew_cv);
		}
	}
	arc_set_need_free();
	mutex_exit(&arc_evict_lock);

	/*
	 * If the ARC size is reduced from arc_c_max to arc_c_min (especially
	 * if the average cached block is small), eviction can be on-CPU for
	 * many seconds.  To ensure that other threads that may be bound to
	 * this CPU are able to make progress, make a voluntary preemption
	 * call here.
	 */
	cond_resched();

	return (bytes_evicted);
}

/*
 * Evict buffers from the given arc state, until we've removed the
 * specified number of bytes. Move the removed buffers to the
 * appropriate evict state.
 *
 * This function makes a "best effort". It skips over any buffers
 * it can't get a hash_lock on, and so, may not catch all candidates.
 * It may also return without evicting as much space as requested.
 *
 * If bytes is specified using the special value ARC_EVICT_ALL, this
 * will evict all available (i.e. unlocked and evictable) buffers from
 * the given arc state; which is used by arc_flush().
 */
static uint64_t
arc_evict_state(arc_state_t *state, uint64_t spa, int64_t bytes,
    arc_buf_contents_t type)
{
	uint64_t total_evicted = 0;
	multilist_t *ml = state->arcs_list[type];
	int num_sublists;
	arc_buf_hdr_t **markers;

	IMPLY(bytes < 0, bytes == ARC_EVICT_ALL);

	num_sublists = multilist_get_num_sublists(ml);

	/*
	 * If we've tried to evict from each sublist, made some
	 * progress, but still have not hit the target number of bytes
	 * to evict, we want to keep trying. The markers allow us to
	 * pick up where we left off for each individual sublist, rather
	 * than starting from the tail each time.
	 */
	markers = kmem_zalloc(sizeof (*markers) * num_sublists, KM_SLEEP);
	for (int i = 0; i < num_sublists; i++) {
		multilist_sublist_t *mls;

		markers[i] = kmem_cache_alloc(hdr_full_cache, KM_SLEEP);

		/*
		 * A b_spa of 0 is used to indicate that this header is
		 * a marker. This fact is used in arc_evict_type() and
		 * arc_evict_state_impl().
		 */
		markers[i]->b_spa = 0;

		mls = multilist_sublist_lock(ml, i);
		multilist_sublist_insert_tail(mls, markers[i]);
		multilist_sublist_unlock(mls);
	}

	/*
	 * While we haven't hit our target number of bytes to evict, or
	 * we're evicting all available buffers.
	 */
	while (total_evicted < bytes || bytes == ARC_EVICT_ALL) {
		int sublist_idx = multilist_get_random_index(ml);
		uint64_t scan_evicted = 0;

		/*
		 * Try to reduce pinned dnodes with a floor of arc_dnode_limit.
		 * Request that 10% of the LRUs be scanned by the superblock
		 * shrinker.
		 */
		if (type == ARC_BUFC_DATA && aggsum_compare(&astat_dnode_size,
		    arc_dnode_size_limit) > 0) {
			arc_prune_async((aggsum_upper_bound(&astat_dnode_size) -
			    arc_dnode_size_limit) / sizeof (dnode_t) /
			    zfs_arc_dnode_reduce_percent);
		}

		/*
		 * Start eviction using a randomly selected sublist,
		 * this is to try and evenly balance eviction across all
		 * sublists. Always starting at the same sublist
		 * (e.g. index 0) would cause evictions to favor certain
		 * sublists over others.
		 */
		for (int i = 0; i < num_sublists; i++) {
			uint64_t bytes_remaining;
			uint64_t bytes_evicted;

			if (bytes == ARC_EVICT_ALL)
				bytes_remaining = ARC_EVICT_ALL;
			else if (total_evicted < bytes)
				bytes_remaining = bytes - total_evicted;
			else
				break;

			bytes_evicted = arc_evict_state_impl(ml, sublist_idx,
			    markers[sublist_idx], spa, bytes_remaining);

			scan_evicted += bytes_evicted;
			total_evicted += bytes_evicted;

			/* we've reached the end, wrap to the beginning */
			if (++sublist_idx >= num_sublists)
				sublist_idx = 0;
		}

		/*
		 * If we didn't evict anything during this scan, we have
		 * no reason to believe we'll evict more during another
		 * scan, so break the loop.
		 */
		if (scan_evicted == 0) {
			/* This isn't possible, let's make that obvious */
			ASSERT3S(bytes, !=, 0);

			/*
			 * When bytes is ARC_EVICT_ALL, the only way to
			 * break the loop is when scan_evicted is zero.
			 * In that case, we actually have evicted enough,
			 * so we don't want to increment the kstat.
			 */
			if (bytes != ARC_EVICT_ALL) {
				ASSERT3S(total_evicted, <, bytes);
				ARCSTAT_BUMP(arcstat_evict_not_enough);
			}

			break;
		}
	}

	for (int i = 0; i < num_sublists; i++) {
		multilist_sublist_t *mls = multilist_sublist_lock(ml, i);
		multilist_sublist_remove(mls, markers[i]);
		multilist_sublist_unlock(mls);

		kmem_cache_free(hdr_full_cache, markers[i]);
	}
	kmem_free(markers, sizeof (*markers) * num_sublists);

	return (total_evicted);
}

/*
 * Flush all "evictable" data of the given type from the arc state
 * specified. This will not evict any "active" buffers (i.e. referenced).
 *
 * When 'retry' is set to B_FALSE, the function will make a single pass
 * over the state and evict any buffers that it can. Since it doesn't
 * continually retry the eviction, it might end up leaving some buffers
 * in the ARC due to lock misses.
 *
 * When 'retry' is set to B_TRUE, the function will continually retry the
 * eviction until *all* evictable buffers have been removed from the
 * state. As a result, if concurrent insertions into the state are
 * allowed (e.g. if the ARC isn't shutting down), this function might
 * wind up in an infinite loop, continually trying to evict buffers.
 */
static uint64_t
arc_flush_state(arc_state_t *state, uint64_t spa, arc_buf_contents_t type,
    boolean_t retry)
{
	uint64_t evicted = 0;

	while (zfs_refcount_count(&state->arcs_esize[type]) != 0) {
		evicted += arc_evict_state(state, spa, ARC_EVICT_ALL, type);

		if (!retry)
			break;
	}

	return (evicted);
}

/*
 * Evict the specified number of bytes from the state specified,
 * restricting eviction to the spa and type given. This function
 * prevents us from trying to evict more from a state's list than
 * is "evictable", and to skip evicting altogether when passed a
 * negative value for "bytes". In contrast, arc_evict_state() will
 * evict everything it can, when passed a negative value for "bytes".
 */
static uint64_t
arc_evict_impl(arc_state_t *state, uint64_t spa, int64_t bytes,
    arc_buf_contents_t type)
{
	int64_t delta;

	if (bytes > 0 && zfs_refcount_count(&state->arcs_esize[type]) > 0) {
		delta = MIN(zfs_refcount_count(&state->arcs_esize[type]),
		    bytes);
		return (arc_evict_state(state, spa, delta, type));
	}

	return (0);
}

/*
 * The goal of this function is to evict enough meta data buffers from the
 * ARC in order to enforce the arc_meta_limit.  Achieving this is slightly
 * more complicated than it appears because it is common for data buffers
 * to have holds on meta data buffers.  In addition, dnode meta data buffers
 * will be held by the dnodes in the block preventing them from being freed.
 * This means we can't simply traverse the ARC and expect to always find
 * enough unheld meta data buffer to release.
 *
 * Therefore, this function has been updated to make alternating passes
 * over the ARC releasing data buffers and then newly unheld meta data
 * buffers.  This ensures forward progress is maintained and meta_used
 * will decrease.  Normally this is sufficient, but if required the ARC
 * will call the registered prune callbacks causing dentry and inodes to
 * be dropped from the VFS cache.  This will make dnode meta data buffers
 * available for reclaim.
 */
static uint64_t
arc_evict_meta_balanced(uint64_t meta_used)
{
	int64_t delta, prune = 0, adjustmnt;
	uint64_t total_evicted = 0;
	arc_buf_contents_t type = ARC_BUFC_DATA;
	int restarts = MAX(zfs_arc_meta_adjust_restarts, 0);

restart:
	/*
	 * This slightly differs than the way we evict from the mru in
	 * arc_evict because we don't have a "target" value (i.e. no
	 * "meta" arc_p). As a result, I think we can completely
	 * cannibalize the metadata in the MRU before we evict the
	 * metadata from the MFU. I think we probably need to implement a
	 * "metadata arc_p" value to do this properly.
	 */
	adjustmnt = meta_used - arc_meta_limit;

	if (adjustmnt > 0 &&
	    zfs_refcount_count(&arc_mru->arcs_esize[type]) > 0) {
		delta = MIN(zfs_refcount_count(&arc_mru->arcs_esize[type]),
		    adjustmnt);
		total_evicted += arc_evict_impl(arc_mru, 0, delta, type);
		adjustmnt -= delta;
	}

	/*
	 * We can't afford to recalculate adjustmnt here. If we do,
	 * new metadata buffers can sneak into the MRU or ANON lists,
	 * thus penalize the MFU metadata. Although the fudge factor is
	 * small, it has been empirically shown to be significant for
	 * certain workloads (e.g. creating many empty directories). As
	 * such, we use the original calculation for adjustmnt, and
	 * simply decrement the amount of data evicted from the MRU.
	 */

	if (adjustmnt > 0 &&
	    zfs_refcount_count(&arc_mfu->arcs_esize[type]) > 0) {
		delta = MIN(zfs_refcount_count(&arc_mfu->arcs_esize[type]),
		    adjustmnt);
		total_evicted += arc_evict_impl(arc_mfu, 0, delta, type);
	}

	adjustmnt = meta_used - arc_meta_limit;

	if (adjustmnt > 0 &&
	    zfs_refcount_count(&arc_mru_ghost->arcs_esize[type]) > 0) {
		delta = MIN(adjustmnt,
		    zfs_refcount_count(&arc_mru_ghost->arcs_esize[type]));
		total_evicted += arc_evict_impl(arc_mru_ghost, 0, delta, type);
		adjustmnt -= delta;
	}

	if (adjustmnt > 0 &&
	    zfs_refcount_count(&arc_mfu_ghost->arcs_esize[type]) > 0) {
		delta = MIN(adjustmnt,
		    zfs_refcount_count(&arc_mfu_ghost->arcs_esize[type]));
		total_evicted += arc_evict_impl(arc_mfu_ghost, 0, delta, type);
	}

	/*
	 * If after attempting to make the requested adjustment to the ARC
	 * the meta limit is still being exceeded then request that the
	 * higher layers drop some cached objects which have holds on ARC
	 * meta buffers.  Requests to the upper layers will be made with
	 * increasingly large scan sizes until the ARC is below the limit.
	 */
	if (meta_used > arc_meta_limit) {
		if (type == ARC_BUFC_DATA) {
			type = ARC_BUFC_METADATA;
		} else {
			type = ARC_BUFC_DATA;

			if (zfs_arc_meta_prune) {
				prune += zfs_arc_meta_prune;
				arc_prune_async(prune);
			}
		}

		if (restarts > 0) {
			restarts--;
			goto restart;
		}
	}
	return (total_evicted);
}

/*
 * Evict metadata buffers from the cache, such that arc_meta_used is
 * capped by the arc_meta_limit tunable.
 */
static uint64_t
arc_evict_meta_only(uint64_t meta_used)
{
	uint64_t total_evicted = 0;
	int64_t target;

	/*
	 * If we're over the meta limit, we want to evict enough
	 * metadata to get back under the meta limit. We don't want to
	 * evict so much that we drop the MRU below arc_p, though. If
	 * we're over the meta limit more than we're over arc_p, we
	 * evict some from the MRU here, and some from the MFU below.
	 */
	target = MIN((int64_t)(meta_used - arc_meta_limit),
	    (int64_t)(zfs_refcount_count(&arc_anon->arcs_size) +
	    zfs_refcount_count(&arc_mru->arcs_size) - arc_p));

	total_evicted += arc_evict_impl(arc_mru, 0, target, ARC_BUFC_METADATA);

	/*
	 * Similar to the above, we want to evict enough bytes to get us
	 * below the meta limit, but not so much as to drop us below the
	 * space allotted to the MFU (which is defined as arc_c - arc_p).
	 */
	target = MIN((int64_t)(meta_used - arc_meta_limit),
	    (int64_t)(zfs_refcount_count(&arc_mfu->arcs_size) -
	    (arc_c - arc_p)));

	total_evicted += arc_evict_impl(arc_mfu, 0, target, ARC_BUFC_METADATA);

	return (total_evicted);
}

static uint64_t
arc_evict_meta(uint64_t meta_used)
{
	if (zfs_arc_meta_strategy == ARC_STRATEGY_META_ONLY)
		return (arc_evict_meta_only(meta_used));
	else
		return (arc_evict_meta_balanced(meta_used));
}

/*
 * Return the type of the oldest buffer in the given arc state
 *
 * This function will select a random sublist of type ARC_BUFC_DATA and
 * a random sublist of type ARC_BUFC_METADATA. The tail of each sublist
 * is compared, and the type which contains the "older" buffer will be
 * returned.
 */
static arc_buf_contents_t
arc_evict_type(arc_state_t *state)
{
	multilist_t *data_ml = state->arcs_list[ARC_BUFC_DATA];
	multilist_t *meta_ml = state->arcs_list[ARC_BUFC_METADATA];
	int data_idx = multilist_get_random_index(data_ml);
	int meta_idx = multilist_get_random_index(meta_ml);
	multilist_sublist_t *data_mls;
	multilist_sublist_t *meta_mls;
	arc_buf_contents_t type;
	arc_buf_hdr_t *data_hdr;
	arc_buf_hdr_t *meta_hdr;

	/*
	 * We keep the sublist lock until we're finished, to prevent
	 * the headers from being destroyed via arc_evict_state().
	 */
	data_mls = multilist_sublist_lock(data_ml, data_idx);
	meta_mls = multilist_sublist_lock(meta_ml, meta_idx);

	/*
	 * These two loops are to ensure we skip any markers that
	 * might be at the tail of the lists due to arc_evict_state().
	 */

	for (data_hdr = multilist_sublist_tail(data_mls); data_hdr != NULL;
	    data_hdr = multilist_sublist_prev(data_mls, data_hdr)) {
		if (data_hdr->b_spa != 0)
			break;
	}

	for (meta_hdr = multilist_sublist_tail(meta_mls); meta_hdr != NULL;
	    meta_hdr = multilist_sublist_prev(meta_mls, meta_hdr)) {
		if (meta_hdr->b_spa != 0)
			break;
	}

	if (data_hdr == NULL && meta_hdr == NULL) {
		type = ARC_BUFC_DATA;
	} else if (data_hdr == NULL) {
		ASSERT3P(meta_hdr, !=, NULL);
		type = ARC_BUFC_METADATA;
	} else if (meta_hdr == NULL) {
		ASSERT3P(data_hdr, !=, NULL);
		type = ARC_BUFC_DATA;
	} else {
		ASSERT3P(data_hdr, !=, NULL);
		ASSERT3P(meta_hdr, !=, NULL);

		/* The headers can't be on the sublist without an L1 header */
		ASSERT(HDR_HAS_L1HDR(data_hdr));
		ASSERT(HDR_HAS_L1HDR(meta_hdr));

		if (data_hdr->b_l1hdr.b_arc_access <
		    meta_hdr->b_l1hdr.b_arc_access) {
			type = ARC_BUFC_DATA;
		} else {
			type = ARC_BUFC_METADATA;
		}
	}

	multilist_sublist_unlock(meta_mls);
	multilist_sublist_unlock(data_mls);

	return (type);
}

/*
 * Evict buffers from the cache, such that arc_size is capped by arc_c.
 */
static uint64_t
arc_evict(void)
{
	uint64_t total_evicted = 0;
	uint64_t bytes;
	int64_t target;
	uint64_t asize = aggsum_value(&arc_size);
	uint64_t ameta = aggsum_value(&arc_meta_used);

	/*
	 * If we're over arc_meta_limit, we want to correct that before
	 * potentially evicting data buffers below.
	 */
	total_evicted += arc_evict_meta(ameta);

	/*
	 * Adjust MRU size
	 *
	 * If we're over the target cache size, we want to evict enough
	 * from the list to get back to our target size. We don't want
	 * to evict too much from the MRU, such that it drops below
	 * arc_p. So, if we're over our target cache size more than
	 * the MRU is over arc_p, we'll evict enough to get back to
	 * arc_p here, and then evict more from the MFU below.
	 */
	target = MIN((int64_t)(asize - arc_c),
	    (int64_t)(zfs_refcount_count(&arc_anon->arcs_size) +
	    zfs_refcount_count(&arc_mru->arcs_size) + ameta - arc_p));

	/*
	 * If we're below arc_meta_min, always prefer to evict data.
	 * Otherwise, try to satisfy the requested number of bytes to
	 * evict from the type which contains older buffers; in an
	 * effort to keep newer buffers in the cache regardless of their
	 * type. If we cannot satisfy the number of bytes from this
	 * type, spill over into the next type.
	 */
	if (arc_evict_type(arc_mru) == ARC_BUFC_METADATA &&
	    ameta > arc_meta_min) {
		bytes = arc_evict_impl(arc_mru, 0, target, ARC_BUFC_METADATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * metadata, we try to get the rest from data.
		 */
		target -= bytes;

		total_evicted +=
		    arc_evict_impl(arc_mru, 0, target, ARC_BUFC_DATA);
	} else {
		bytes = arc_evict_impl(arc_mru, 0, target, ARC_BUFC_DATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * data, we try to get the rest from metadata.
		 */
		target -= bytes;

		total_evicted +=
		    arc_evict_impl(arc_mru, 0, target, ARC_BUFC_METADATA);
	}

	/*
	 * Re-sum ARC stats after the first round of evictions.
	 */
	asize = aggsum_value(&arc_size);
	ameta = aggsum_value(&arc_meta_used);


	/*
	 * Adjust MFU size
	 *
	 * Now that we've tried to evict enough from the MRU to get its
	 * size back to arc_p, if we're still above the target cache
	 * size, we evict the rest from the MFU.
	 */
	target = asize - arc_c;

	if (arc_evict_type(arc_mfu) == ARC_BUFC_METADATA &&
	    ameta > arc_meta_min) {
		bytes = arc_evict_impl(arc_mfu, 0, target, ARC_BUFC_METADATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * metadata, we try to get the rest from data.
		 */
		target -= bytes;

		total_evicted +=
		    arc_evict_impl(arc_mfu, 0, target, ARC_BUFC_DATA);
	} else {
		bytes = arc_evict_impl(arc_mfu, 0, target, ARC_BUFC_DATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * data, we try to get the rest from data.
		 */
		target -= bytes;

		total_evicted +=
		    arc_evict_impl(arc_mfu, 0, target, ARC_BUFC_METADATA);
	}

	/*
	 * Adjust ghost lists
	 *
	 * In addition to the above, the ARC also defines target values
	 * for the ghost lists. The sum of the mru list and mru ghost
	 * list should never exceed the target size of the cache, and
	 * the sum of the mru list, mfu list, mru ghost list, and mfu
	 * ghost list should never exceed twice the target size of the
	 * cache. The following logic enforces these limits on the ghost
	 * caches, and evicts from them as needed.
	 */
	target = zfs_refcount_count(&arc_mru->arcs_size) +
	    zfs_refcount_count(&arc_mru_ghost->arcs_size) - arc_c;

	bytes = arc_evict_impl(arc_mru_ghost, 0, target, ARC_BUFC_DATA);
	total_evicted += bytes;

	target -= bytes;

	total_evicted +=
	    arc_evict_impl(arc_mru_ghost, 0, target, ARC_BUFC_METADATA);

	/*
	 * We assume the sum of the mru list and mfu list is less than
	 * or equal to arc_c (we enforced this above), which means we
	 * can use the simpler of the two equations below:
	 *
	 *	mru + mfu + mru ghost + mfu ghost <= 2 * arc_c
	 *		    mru ghost + mfu ghost <= arc_c
	 */
	target = zfs_refcount_count(&arc_mru_ghost->arcs_size) +
	    zfs_refcount_count(&arc_mfu_ghost->arcs_size) - arc_c;

	bytes = arc_evict_impl(arc_mfu_ghost, 0, target, ARC_BUFC_DATA);
	total_evicted += bytes;

	target -= bytes;

	total_evicted +=
	    arc_evict_impl(arc_mfu_ghost, 0, target, ARC_BUFC_METADATA);

	return (total_evicted);
}

void
arc_flush(spa_t *spa, boolean_t retry)
{
	uint64_t guid = 0;

	/*
	 * If retry is B_TRUE, a spa must not be specified since we have
	 * no good way to determine if all of a spa's buffers have been
	 * evicted from an arc state.
	 */
	ASSERT(!retry || spa == 0);

	if (spa != NULL)
		guid = spa_load_guid(spa);

	(void) arc_flush_state(arc_mru, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mru, guid, ARC_BUFC_METADATA, retry);

	(void) arc_flush_state(arc_mfu, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mfu, guid, ARC_BUFC_METADATA, retry);

	(void) arc_flush_state(arc_mru_ghost, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mru_ghost, guid, ARC_BUFC_METADATA, retry);

	(void) arc_flush_state(arc_mfu_ghost, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mfu_ghost, guid, ARC_BUFC_METADATA, retry);
}

void
arc_reduce_target_size(int64_t to_free)
{
	uint64_t asize = aggsum_value(&arc_size);

	/*
	 * All callers want the ARC to actually evict (at least) this much
	 * memory.  Therefore we reduce from the lower of the current size and
	 * the target size.  This way, even if arc_c is much higher than
	 * arc_size (as can be the case after many calls to arc_freed(), we will
	 * immediately have arc_c < arc_size and therefore the arc_evict_zthr
	 * will evict.
	 */
	uint64_t c = MIN(arc_c, asize);

	if (c > to_free && c - to_free > arc_c_min) {
		arc_c = c - to_free;
		atomic_add_64(&arc_p, -(arc_p >> arc_shrink_shift));
		if (arc_p > arc_c)
			arc_p = (arc_c >> 1);
		ASSERT(arc_c >= arc_c_min);
		ASSERT((int64_t)arc_p >= 0);
	} else {
		arc_c = arc_c_min;
	}

	if (asize > arc_c) {
		/* See comment in arc_evict_cb_check() on why lock+flag */
		mutex_enter(&arc_evict_lock);
		arc_evict_needed = B_TRUE;
		mutex_exit(&arc_evict_lock);
		zthr_wakeup(arc_evict_zthr);
	}
}

/*
 * Determine if the system is under memory pressure and is asking
 * to reclaim memory. A return value of B_TRUE indicates that the system
 * is under memory pressure and that the arc should adjust accordingly.
 */
boolean_t
arc_reclaim_needed(void)
{
	return (arc_available_memory() < 0);
}

void
arc_kmem_reap_soon(void)
{
	size_t			i;
	kmem_cache_t		*prev_cache = NULL;
	kmem_cache_t		*prev_data_cache = NULL;
	extern kmem_cache_t	*zio_buf_cache[];
	extern kmem_cache_t	*zio_data_buf_cache[];

#ifdef _KERNEL
	if ((aggsum_compare(&arc_meta_used, arc_meta_limit) >= 0) &&
	    zfs_arc_meta_prune) {
		/*
		 * We are exceeding our meta-data cache limit.
		 * Prune some entries to release holds on meta-data.
		 */
		arc_prune_async(zfs_arc_meta_prune);
	}
#if defined(_ILP32)
	/*
	 * Reclaim unused memory from all kmem caches.
	 */
	kmem_reap();
#endif
#endif

	for (i = 0; i < SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT; i++) {
#if defined(_ILP32)
		/* reach upper limit of cache size on 32-bit */
		if (zio_buf_cache[i] == NULL)
			break;
#endif
		if (zio_buf_cache[i] != prev_cache) {
			prev_cache = zio_buf_cache[i];
			kmem_cache_reap_now(zio_buf_cache[i]);
		}
		if (zio_data_buf_cache[i] != prev_data_cache) {
			prev_data_cache = zio_data_buf_cache[i];
			kmem_cache_reap_now(zio_data_buf_cache[i]);
		}
	}
	kmem_cache_reap_now(buf_cache);
	kmem_cache_reap_now(hdr_full_cache);
	kmem_cache_reap_now(hdr_l2only_cache);
	kmem_cache_reap_now(zfs_btree_leaf_cache);
	abd_cache_reap_now();
}

/* ARGSUSED */
static boolean_t
arc_evict_cb_check(void *arg, zthr_t *zthr)
{
#ifdef ZFS_DEBUG
	/*
	 * This is necessary in order to keep the kstat information
	 * up to date for tools that display kstat data such as the
	 * mdb ::arc dcmd and the Linux crash utility.  These tools
	 * typically do not call kstat's update function, but simply
	 * dump out stats from the most recent update.  Without
	 * this call, these commands may show stale stats for the
	 * anon, mru, mru_ghost, mfu, and mfu_ghost lists.  Even
	 * with this call, the data might be out of date if the
	 * evict thread hasn't been woken recently; but that should
	 * suffice.  The arc_state_t structures can be queried
	 * directly if more accurate information is needed.
	 */
	if (arc_ksp != NULL)
		arc_ksp->ks_update(arc_ksp, KSTAT_READ);
#endif

	/*
	 * We have to rely on arc_wait_for_eviction() to tell us when to
	 * evict, rather than checking if we are overflowing here, so that we
	 * are sure to not leave arc_wait_for_eviction() waiting on aew_cv.
	 * If we have become "not overflowing" since arc_wait_for_eviction()
	 * checked, we need to wake it up.  We could broadcast the CV here,
	 * but arc_wait_for_eviction() may have not yet gone to sleep.  We
	 * would need to use a mutex to ensure that this function doesn't
	 * broadcast until arc_wait_for_eviction() has gone to sleep (e.g.
	 * the arc_evict_lock).  However, the lock ordering of such a lock
	 * would necessarily be incorrect with respect to the zthr_lock,
	 * which is held before this function is called, and is held by
	 * arc_wait_for_eviction() when it calls zthr_wakeup().
	 */
	return (arc_evict_needed);
}

/*
 * Keep arc_size under arc_c by running arc_evict which evicts data
 * from the ARC.
 */
/* ARGSUSED */
static void
arc_evict_cb(void *arg, zthr_t *zthr)
{
	uint64_t evicted = 0;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	/* Evict from cache */
	evicted = arc_evict();

	/*
	 * If evicted is zero, we couldn't evict anything
	 * via arc_evict(). This could be due to hash lock
	 * collisions, but more likely due to the majority of
	 * arc buffers being unevictable. Therefore, even if
	 * arc_size is above arc_c, another pass is unlikely to
	 * be helpful and could potentially cause us to enter an
	 * infinite loop.  Additionally, zthr_iscancelled() is
	 * checked here so that if the arc is shutting down, the
	 * broadcast will wake any remaining arc evict waiters.
	 */
	mutex_enter(&arc_evict_lock);
	arc_evict_needed = !zthr_iscancelled(arc_evict_zthr) &&
	    evicted > 0 && aggsum_compare(&arc_size, arc_c) > 0;
	if (!arc_evict_needed) {
		/*
		 * We're either no longer overflowing, or we
		 * can't evict anything more, so we should wake
		 * arc_get_data_impl() sooner.
		 */
		arc_evict_waiter_t *aw;
		while ((aw = list_remove_head(&arc_evict_waiters)) != NULL) {
			cv_broadcast(&aw->aew_cv);
		}
		arc_set_need_free();
	}
	mutex_exit(&arc_evict_lock);
	spl_fstrans_unmark(cookie);
}

/* ARGSUSED */
static boolean_t
arc_reap_cb_check(void *arg, zthr_t *zthr)
{
	int64_t free_memory = arc_available_memory();
	static int reap_cb_check_counter = 0;

	/*
	 * If a kmem reap is already active, don't schedule more.  We must
	 * check for this because kmem_cache_reap_soon() won't actually
	 * block on the cache being reaped (this is to prevent callers from
	 * becoming implicitly blocked by a system-wide kmem reap -- which,
	 * on a system with many, many full magazines, can take minutes).
	 */
	if (!kmem_cache_reap_active() && free_memory < 0) {

		arc_no_grow = B_TRUE;
		arc_warm = B_TRUE;
		/*
		 * Wait at least zfs_grow_retry (default 5) seconds
		 * before considering growing.
		 */
		arc_growtime = gethrtime() + SEC2NSEC(arc_grow_retry);
		return (B_TRUE);
	} else if (free_memory < arc_c >> arc_no_grow_shift) {
		arc_no_grow = B_TRUE;
	} else if (gethrtime() >= arc_growtime) {
		arc_no_grow = B_FALSE;
	}

	/*
	 * Called unconditionally every 60 seconds to reclaim unused
	 * zstd compression and decompression context. This is done
	 * here to avoid the need for an independent thread.
	 */
	if (!((reap_cb_check_counter++) % 60))
		zfs_zstd_cache_reap_now();

	return (B_FALSE);
}

/*
 * Keep enough free memory in the system by reaping the ARC's kmem
 * caches.  To cause more slabs to be reapable, we may reduce the
 * target size of the cache (arc_c), causing the arc_evict_cb()
 * to free more buffers.
 */
/* ARGSUSED */
static void
arc_reap_cb(void *arg, zthr_t *zthr)
{
	int64_t free_memory;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	/*
	 * Kick off asynchronous kmem_reap()'s of all our caches.
	 */
	arc_kmem_reap_soon();

	/*
	 * Wait at least arc_kmem_cache_reap_retry_ms between
	 * arc_kmem_reap_soon() calls. Without this check it is possible to
	 * end up in a situation where we spend lots of time reaping
	 * caches, while we're near arc_c_min.  Waiting here also gives the
	 * subsequent free memory check a chance of finding that the
	 * asynchronous reap has already freed enough memory, and we don't
	 * need to call arc_reduce_target_size().
	 */
	delay((hz * arc_kmem_cache_reap_retry_ms + 999) / 1000);

	/*
	 * Reduce the target size as needed to maintain the amount of free
	 * memory in the system at a fraction of the arc_size (1/128th by
	 * default).  If oversubscribed (free_memory < 0) then reduce the
	 * target arc_size by the deficit amount plus the fractional
	 * amount.  If free memory is positive but less then the fractional
	 * amount, reduce by what is needed to hit the fractional amount.
	 */
	free_memory = arc_available_memory();

	int64_t to_free =
	    (arc_c >> arc_shrink_shift) - free_memory;
	if (to_free > 0) {
		arc_reduce_target_size(to_free);
	}
	spl_fstrans_unmark(cookie);
}

#ifdef _KERNEL
/*
 * Determine the amount of memory eligible for eviction contained in the
 * ARC. All clean data reported by the ghost lists can always be safely
 * evicted. Due to arc_c_min, the same does not hold for all clean data
 * contained by the regular mru and mfu lists.
 *
 * In the case of the regular mru and mfu lists, we need to report as
 * much clean data as possible, such that evicting that same reported
 * data will not bring arc_size below arc_c_min. Thus, in certain
 * circumstances, the total amount of clean data in the mru and mfu
 * lists might not actually be evictable.
 *
 * The following two distinct cases are accounted for:
 *
 * 1. The sum of the amount of dirty data contained by both the mru and
 *    mfu lists, plus the ARC's other accounting (e.g. the anon list),
 *    is greater than or equal to arc_c_min.
 *    (i.e. amount of dirty data >= arc_c_min)
 *
 *    This is the easy case; all clean data contained by the mru and mfu
 *    lists is evictable. Evicting all clean data can only drop arc_size
 *    to the amount of dirty data, which is greater than arc_c_min.
 *
 * 2. The sum of the amount of dirty data contained by both the mru and
 *    mfu lists, plus the ARC's other accounting (e.g. the anon list),
 *    is less than arc_c_min.
 *    (i.e. arc_c_min > amount of dirty data)
 *
 *    2.1. arc_size is greater than or equal arc_c_min.
 *         (i.e. arc_size >= arc_c_min > amount of dirty data)
 *
 *         In this case, not all clean data from the regular mru and mfu
 *         lists is actually evictable; we must leave enough clean data
 *         to keep arc_size above arc_c_min. Thus, the maximum amount of
 *         evictable data from the two lists combined, is exactly the
 *         difference between arc_size and arc_c_min.
 *
 *    2.2. arc_size is less than arc_c_min
 *         (i.e. arc_c_min > arc_size > amount of dirty data)
 *
 *         In this case, none of the data contained in the mru and mfu
 *         lists is evictable, even if it's clean. Since arc_size is
 *         already below arc_c_min, evicting any more would only
 *         increase this negative difference.
 */

#endif /* _KERNEL */

/*
 * Adapt arc info given the number of bytes we are trying to add and
 * the state that we are coming from.  This function is only called
 * when we are adding new content to the cache.
 */
static void
arc_adapt(int bytes, arc_state_t *state)
{
	int mult;
	uint64_t arc_p_min = (arc_c >> arc_p_min_shift);
	int64_t mrug_size = zfs_refcount_count(&arc_mru_ghost->arcs_size);
	int64_t mfug_size = zfs_refcount_count(&arc_mfu_ghost->arcs_size);

	ASSERT(bytes > 0);
	/*
	 * Adapt the target size of the MRU list:
	 *	- if we just hit in the MRU ghost list, then increase
	 *	  the target size of the MRU list.
	 *	- if we just hit in the MFU ghost list, then increase
	 *	  the target size of the MFU list by decreasing the
	 *	  target size of the MRU list.
	 */
	if (state == arc_mru_ghost) {
		mult = (mrug_size >= mfug_size) ? 1 : (mfug_size / mrug_size);
		if (!zfs_arc_p_dampener_disable)
			mult = MIN(mult, 10); /* avoid wild arc_p adjustment */

		arc_p = MIN(arc_c - arc_p_min, arc_p + bytes * mult);
	} else if (state == arc_mfu_ghost) {
		uint64_t delta;

		mult = (mfug_size >= mrug_size) ? 1 : (mrug_size / mfug_size);
		if (!zfs_arc_p_dampener_disable)
			mult = MIN(mult, 10);

		delta = MIN(bytes * mult, arc_p);
		arc_p = MAX(arc_p_min, arc_p - delta);
	}
	ASSERT((int64_t)arc_p >= 0);

	/*
	 * Wake reap thread if we do not have any available memory
	 */
	if (arc_reclaim_needed()) {
		zthr_wakeup(arc_reap_zthr);
		return;
	}

	if (arc_no_grow)
		return;

	if (arc_c >= arc_c_max)
		return;

	/*
	 * If we're within (2 * maxblocksize) bytes of the target
	 * cache size, increment the target cache size
	 */
	ASSERT3U(arc_c, >=, 2ULL << SPA_MAXBLOCKSHIFT);
	if (aggsum_upper_bound(&arc_size) >=
	    arc_c - (2ULL << SPA_MAXBLOCKSHIFT)) {
		atomic_add_64(&arc_c, (int64_t)bytes);
		if (arc_c > arc_c_max)
			arc_c = arc_c_max;
		else if (state == arc_anon)
			atomic_add_64(&arc_p, (int64_t)bytes);
		if (arc_p > arc_c)
			arc_p = arc_c;
	}
	ASSERT((int64_t)arc_p >= 0);
}

/*
 * Check if arc_size has grown past our upper threshold, determined by
 * zfs_arc_overflow_shift.
 */
boolean_t
arc_is_overflowing(void)
{
	/* Always allow at least one block of overflow */
	int64_t overflow = MAX(SPA_MAXBLOCKSIZE,
	    arc_c >> zfs_arc_overflow_shift);

	/*
	 * We just compare the lower bound here for performance reasons. Our
	 * primary goals are to make sure that the arc never grows without
	 * bound, and that it can reach its maximum size. This check
	 * accomplishes both goals. The maximum amount we could run over by is
	 * 2 * aggsum_borrow_multiplier * NUM_CPUS * the average size of a block
	 * in the ARC. In practice, that's in the tens of MB, which is low
	 * enough to be safe.
	 */
	return (aggsum_lower_bound(&arc_size) >= (int64_t)arc_c + overflow);
}

static abd_t *
arc_get_data_abd(arc_buf_hdr_t *hdr, uint64_t size, void *tag,
    boolean_t do_adapt)
{
	arc_buf_contents_t type = arc_buf_type(hdr);

	arc_get_data_impl(hdr, size, tag, do_adapt);
	if (type == ARC_BUFC_METADATA) {
		return (abd_alloc(size, B_TRUE));
	} else {
		ASSERT(type == ARC_BUFC_DATA);
		return (abd_alloc(size, B_FALSE));
	}
}

static void *
arc_get_data_buf(arc_buf_hdr_t *hdr, uint64_t size, void *tag)
{
	arc_buf_contents_t type = arc_buf_type(hdr);

	arc_get_data_impl(hdr, size, tag, B_TRUE);
	if (type == ARC_BUFC_METADATA) {
		return (zio_buf_alloc(size));
	} else {
		ASSERT(type == ARC_BUFC_DATA);
		return (zio_data_buf_alloc(size));
	}
}

/*
 * Wait for the specified amount of data (in bytes) to be evicted from the
 * ARC, and for there to be sufficient free memory in the system.  Waiting for
 * eviction ensures that the memory used by the ARC decreases.  Waiting for
 * free memory ensures that the system won't run out of free pages, regardless
 * of ARC behavior and settings.  See arc_lowmem_init().
 */
void
arc_wait_for_eviction(uint64_t amount)
{
	mutex_enter(&arc_evict_lock);
	if (arc_is_overflowing()) {
		arc_evict_needed = B_TRUE;
		zthr_wakeup(arc_evict_zthr);

		if (amount != 0) {
			arc_evict_waiter_t aw;
			list_link_init(&aw.aew_node);
			cv_init(&aw.aew_cv, NULL, CV_DEFAULT, NULL);

			arc_evict_waiter_t *last =
			    list_tail(&arc_evict_waiters);
			if (last != NULL) {
				ASSERT3U(last->aew_count, >, arc_evict_count);
				aw.aew_count = last->aew_count + amount;
			} else {
				aw.aew_count = arc_evict_count + amount;
			}

			list_insert_tail(&arc_evict_waiters, &aw);

			arc_set_need_free();

			DTRACE_PROBE3(arc__wait__for__eviction,
			    uint64_t, amount,
			    uint64_t, arc_evict_count,
			    uint64_t, aw.aew_count);

			/*
			 * We will be woken up either when arc_evict_count
			 * reaches aew_count, or when the ARC is no longer
			 * overflowing and eviction completes.
			 */
			cv_wait(&aw.aew_cv, &arc_evict_lock);

			/*
			 * In case of "false" wakeup, we will still be on the
			 * list.
			 */
			if (list_link_active(&aw.aew_node))
				list_remove(&arc_evict_waiters, &aw);

			cv_destroy(&aw.aew_cv);
		}
	}
	mutex_exit(&arc_evict_lock);
}

/*
 * Allocate a block and return it to the caller. If we are hitting the
 * hard limit for the cache size, we must sleep, waiting for the eviction
 * thread to catch up. If we're past the target size but below the hard
 * limit, we'll only signal the reclaim thread and continue on.
 */
static void
arc_get_data_impl(arc_buf_hdr_t *hdr, uint64_t size, void *tag,
    boolean_t do_adapt)
{
	arc_state_t *state = hdr->b_l1hdr.b_state;
	arc_buf_contents_t type = arc_buf_type(hdr);

	if (do_adapt)
		arc_adapt(size, state);

	/*
	 * If arc_size is currently overflowing, we must be adding data
	 * faster than we are evicting.  To ensure we don't compound the
	 * problem by adding more data and forcing arc_size to grow even
	 * further past it's target size, we wait for the eviction thread to
	 * make some progress.  We also wait for there to be sufficient free
	 * memory in the system, as measured by arc_free_memory().
	 *
	 * Specifically, we wait for zfs_arc_eviction_pct percent of the
	 * requested size to be evicted.  This should be more than 100%, to
	 * ensure that that progress is also made towards getting arc_size
	 * under arc_c.  See the comment above zfs_arc_eviction_pct.
	 *
	 * We do the overflowing check without holding the arc_evict_lock to
	 * reduce lock contention in this hot path.  Note that
	 * arc_wait_for_eviction() will acquire the lock and check again to
	 * ensure we are truly overflowing before blocking.
	 */
	if (arc_is_overflowing()) {
		arc_wait_for_eviction(size *
		    zfs_arc_eviction_pct / 100);
	}

	VERIFY3U(hdr->b_type, ==, type);
	if (type == ARC_BUFC_METADATA) {
		arc_space_consume(size, ARC_SPACE_META);
	} else {
		arc_space_consume(size, ARC_SPACE_DATA);
	}

	/*
	 * Update the state size.  Note that ghost states have a
	 * "ghost size" and so don't need to be updated.
	 */
	if (!GHOST_STATE(state)) {

		(void) zfs_refcount_add_many(&state->arcs_size, size, tag);

		/*
		 * If this is reached via arc_read, the link is
		 * protected by the hash lock. If reached via
		 * arc_buf_alloc, the header should not be accessed by
		 * any other thread. And, if reached via arc_read_done,
		 * the hash lock will protect it if it's found in the
		 * hash table; otherwise no other thread should be
		 * trying to [add|remove]_reference it.
		 */
		if (multilist_link_active(&hdr->b_l1hdr.b_arc_node)) {
			ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
			(void) zfs_refcount_add_many(&state->arcs_esize[type],
			    size, tag);
		}

		/*
		 * If we are growing the cache, and we are adding anonymous
		 * data, and we have outgrown arc_p, update arc_p
		 */
		if (aggsum_upper_bound(&arc_size) < arc_c &&
		    hdr->b_l1hdr.b_state == arc_anon &&
		    (zfs_refcount_count(&arc_anon->arcs_size) +
		    zfs_refcount_count(&arc_mru->arcs_size) > arc_p))
			arc_p = MIN(arc_c, arc_p + size);
	}
}

static void
arc_free_data_abd(arc_buf_hdr_t *hdr, abd_t *abd, uint64_t size, void *tag)
{
	arc_free_data_impl(hdr, size, tag);
	abd_free(abd);
}

static void
arc_free_data_buf(arc_buf_hdr_t *hdr, void *buf, uint64_t size, void *tag)
{
	arc_buf_contents_t type = arc_buf_type(hdr);

	arc_free_data_impl(hdr, size, tag);
	if (type == ARC_BUFC_METADATA) {
		zio_buf_free(buf, size);
	} else {
		ASSERT(type == ARC_BUFC_DATA);
		zio_data_buf_free(buf, size);
	}
}

/*
 * Free the arc data buffer.
 */
static void
arc_free_data_impl(arc_buf_hdr_t *hdr, uint64_t size, void *tag)
{
	arc_state_t *state = hdr->b_l1hdr.b_state;
	arc_buf_contents_t type = arc_buf_type(hdr);

	/* protected by hash lock, if in the hash table */
	if (multilist_link_active(&hdr->b_l1hdr.b_arc_node)) {
		ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
		ASSERT(state != arc_anon && state != arc_l2c_only);

		(void) zfs_refcount_remove_many(&state->arcs_esize[type],
		    size, tag);
	}
	(void) zfs_refcount_remove_many(&state->arcs_size, size, tag);

	VERIFY3U(hdr->b_type, ==, type);
	if (type == ARC_BUFC_METADATA) {
		arc_space_return(size, ARC_SPACE_META);
	} else {
		ASSERT(type == ARC_BUFC_DATA);
		arc_space_return(size, ARC_SPACE_DATA);
	}
}

/*
 * This routine is called whenever a buffer is accessed.
 * NOTE: the hash lock is dropped in this function.
 */
static void
arc_access(arc_buf_hdr_t *hdr, kmutex_t *hash_lock)
{
	clock_t now;

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT(HDR_HAS_L1HDR(hdr));

	if (hdr->b_l1hdr.b_state == arc_anon) {
		/*
		 * This buffer is not in the cache, and does not
		 * appear in our "ghost" list.  Add the new buffer
		 * to the MRU state.
		 */

		ASSERT0(hdr->b_l1hdr.b_arc_access);
		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		DTRACE_PROBE1(new_state__mru, arc_buf_hdr_t *, hdr);
		arc_change_state(arc_mru, hdr, hash_lock);

	} else if (hdr->b_l1hdr.b_state == arc_mru) {
		now = ddi_get_lbolt();

		/*
		 * If this buffer is here because of a prefetch, then either:
		 * - clear the flag if this is a "referencing" read
		 *   (any subsequent access will bump this into the MFU state).
		 * or
		 * - move the buffer to the head of the list if this is
		 *   another prefetch (to make it less likely to be evicted).
		 */
		if (HDR_PREFETCH(hdr) || HDR_PRESCIENT_PREFETCH(hdr)) {
			if (zfs_refcount_count(&hdr->b_l1hdr.b_refcnt) == 0) {
				/* link protected by hash lock */
				ASSERT(multilist_link_active(
				    &hdr->b_l1hdr.b_arc_node));
			} else {
				if (HDR_HAS_L2HDR(hdr))
					l2arc_hdr_arcstats_decrement_state(hdr);
				arc_hdr_clear_flags(hdr,
				    ARC_FLAG_PREFETCH |
				    ARC_FLAG_PRESCIENT_PREFETCH);
				atomic_inc_32(&hdr->b_l1hdr.b_mru_hits);
				ARCSTAT_BUMP(arcstat_mru_hits);
				if (HDR_HAS_L2HDR(hdr))
					l2arc_hdr_arcstats_increment_state(hdr);
			}
			hdr->b_l1hdr.b_arc_access = now;
			return;
		}

		/*
		 * This buffer has been "accessed" only once so far,
		 * but it is still in the cache. Move it to the MFU
		 * state.
		 */
		if (ddi_time_after(now, hdr->b_l1hdr.b_arc_access +
		    ARC_MINTIME)) {
			/*
			 * More than 125ms have passed since we
			 * instantiated this buffer.  Move it to the
			 * most frequently used state.
			 */
			hdr->b_l1hdr.b_arc_access = now;
			DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
			arc_change_state(arc_mfu, hdr, hash_lock);
		}
		atomic_inc_32(&hdr->b_l1hdr.b_mru_hits);
		ARCSTAT_BUMP(arcstat_mru_hits);
	} else if (hdr->b_l1hdr.b_state == arc_mru_ghost) {
		arc_state_t	*new_state;
		/*
		 * This buffer has been "accessed" recently, but
		 * was evicted from the cache.  Move it to the
		 * MFU state.
		 */
		if (HDR_PREFETCH(hdr) || HDR_PRESCIENT_PREFETCH(hdr)) {
			new_state = arc_mru;
			if (zfs_refcount_count(&hdr->b_l1hdr.b_refcnt) > 0) {
				if (HDR_HAS_L2HDR(hdr))
					l2arc_hdr_arcstats_decrement_state(hdr);
				arc_hdr_clear_flags(hdr,
				    ARC_FLAG_PREFETCH |
				    ARC_FLAG_PRESCIENT_PREFETCH);
				if (HDR_HAS_L2HDR(hdr))
					l2arc_hdr_arcstats_increment_state(hdr);
			}
			DTRACE_PROBE1(new_state__mru, arc_buf_hdr_t *, hdr);
		} else {
			new_state = arc_mfu;
			DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
		}

		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		arc_change_state(new_state, hdr, hash_lock);

		atomic_inc_32(&hdr->b_l1hdr.b_mru_ghost_hits);
		ARCSTAT_BUMP(arcstat_mru_ghost_hits);
	} else if (hdr->b_l1hdr.b_state == arc_mfu) {
		/*
		 * This buffer has been accessed more than once and is
		 * still in the cache.  Keep it in the MFU state.
		 *
		 * NOTE: an add_reference() that occurred when we did
		 * the arc_read() will have kicked this off the list.
		 * If it was a prefetch, we will explicitly move it to
		 * the head of the list now.
		 */

		atomic_inc_32(&hdr->b_l1hdr.b_mfu_hits);
		ARCSTAT_BUMP(arcstat_mfu_hits);
		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
	} else if (hdr->b_l1hdr.b_state == arc_mfu_ghost) {
		arc_state_t	*new_state = arc_mfu;
		/*
		 * This buffer has been accessed more than once but has
		 * been evicted from the cache.  Move it back to the
		 * MFU state.
		 */

		if (HDR_PREFETCH(hdr) || HDR_PRESCIENT_PREFETCH(hdr)) {
			/*
			 * This is a prefetch access...
			 * move this block back to the MRU state.
			 */
			new_state = arc_mru;
		}

		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
		arc_change_state(new_state, hdr, hash_lock);

		atomic_inc_32(&hdr->b_l1hdr.b_mfu_ghost_hits);
		ARCSTAT_BUMP(arcstat_mfu_ghost_hits);
	} else if (hdr->b_l1hdr.b_state == arc_l2c_only) {
		/*
		 * This buffer is on the 2nd Level ARC.
		 */

		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
		arc_change_state(arc_mfu, hdr, hash_lock);
	} else {
		cmn_err(CE_PANIC, "invalid arc state 0x%p",
		    hdr->b_l1hdr.b_state);
	}
}

/*
 * This routine is called by dbuf_hold() to update the arc_access() state
 * which otherwise would be skipped for entries in the dbuf cache.
 */
void
arc_buf_access(arc_buf_t *buf)
{
	mutex_enter(&buf->b_evict_lock);
	arc_buf_hdr_t *hdr = buf->b_hdr;

	/*
	 * Avoid taking the hash_lock when possible as an optimization.
	 * The header must be checked again under the hash_lock in order
	 * to handle the case where it is concurrently being released.
	 */
	if (hdr->b_l1hdr.b_state == arc_anon || HDR_EMPTY(hdr)) {
		mutex_exit(&buf->b_evict_lock);
		return;
	}

	kmutex_t *hash_lock = HDR_LOCK(hdr);
	mutex_enter(hash_lock);

	if (hdr->b_l1hdr.b_state == arc_anon || HDR_EMPTY(hdr)) {
		mutex_exit(hash_lock);
		mutex_exit(&buf->b_evict_lock);
		ARCSTAT_BUMP(arcstat_access_skip);
		return;
	}

	mutex_exit(&buf->b_evict_lock);

	ASSERT(hdr->b_l1hdr.b_state == arc_mru ||
	    hdr->b_l1hdr.b_state == arc_mfu);

	DTRACE_PROBE1(arc__hit, arc_buf_hdr_t *, hdr);
	arc_access(hdr, hash_lock);
	mutex_exit(hash_lock);

	ARCSTAT_BUMP(arcstat_hits);
	ARCSTAT_CONDSTAT(!HDR_PREFETCH(hdr) && !HDR_PRESCIENT_PREFETCH(hdr),
	    demand, prefetch, !HDR_ISTYPE_METADATA(hdr), data, metadata, hits);
}

/* a generic arc_read_done_func_t which you can use */
/* ARGSUSED */
void
arc_bcopy_func(zio_t *zio, const zbookmark_phys_t *zb, const blkptr_t *bp,
    arc_buf_t *buf, void *arg)
{
	if (buf == NULL)
		return;

	bcopy(buf->b_data, arg, arc_buf_size(buf));
	arc_buf_destroy(buf, arg);
}

/* a generic arc_read_done_func_t */
/* ARGSUSED */
void
arc_getbuf_func(zio_t *zio, const zbookmark_phys_t *zb, const blkptr_t *bp,
    arc_buf_t *buf, void *arg)
{
	arc_buf_t **bufp = arg;

	if (buf == NULL) {
		ASSERT(zio == NULL || zio->io_error != 0);
		*bufp = NULL;
	} else {
		ASSERT(zio == NULL || zio->io_error == 0);
		*bufp = buf;
		ASSERT(buf->b_data != NULL);
	}
}

static void
arc_hdr_verify(arc_buf_hdr_t *hdr, blkptr_t *bp)
{
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp)) {
		ASSERT3U(HDR_GET_PSIZE(hdr), ==, 0);
		ASSERT3U(arc_hdr_get_compress(hdr), ==, ZIO_COMPRESS_OFF);
	} else {
		if (HDR_COMPRESSION_ENABLED(hdr)) {
			ASSERT3U(arc_hdr_get_compress(hdr), ==,
			    BP_GET_COMPRESS(bp));
		}
		ASSERT3U(HDR_GET_LSIZE(hdr), ==, BP_GET_LSIZE(bp));
		ASSERT3U(HDR_GET_PSIZE(hdr), ==, BP_GET_PSIZE(bp));
		ASSERT3U(!!HDR_PROTECTED(hdr), ==, BP_IS_PROTECTED(bp));
	}
}

static void
arc_read_done(zio_t *zio)
{
	blkptr_t 	*bp = zio->io_bp;
	arc_buf_hdr_t	*hdr = zio->io_private;
	kmutex_t	*hash_lock = NULL;
	arc_callback_t	*callback_list;
	arc_callback_t	*acb;
	boolean_t	freeable = B_FALSE;

	/*
	 * The hdr was inserted into hash-table and removed from lists
	 * prior to starting I/O.  We should find this header, since
	 * it's in the hash table, and it should be legit since it's
	 * not possible to evict it during the I/O.  The only possible
	 * reason for it not to be found is if we were freed during the
	 * read.
	 */
	if (HDR_IN_HASH_TABLE(hdr)) {
		arc_buf_hdr_t *found;

		ASSERT3U(hdr->b_birth, ==, BP_PHYSICAL_BIRTH(zio->io_bp));
		ASSERT3U(hdr->b_dva.dva_word[0], ==,
		    BP_IDENTITY(zio->io_bp)->dva_word[0]);
		ASSERT3U(hdr->b_dva.dva_word[1], ==,
		    BP_IDENTITY(zio->io_bp)->dva_word[1]);

		found = buf_hash_find(hdr->b_spa, zio->io_bp, &hash_lock);

		ASSERT((found == hdr &&
		    DVA_EQUAL(&hdr->b_dva, BP_IDENTITY(zio->io_bp))) ||
		    (found == hdr && HDR_L2_READING(hdr)));
		ASSERT3P(hash_lock, !=, NULL);
	}

	if (BP_IS_PROTECTED(bp)) {
		hdr->b_crypt_hdr.b_ot = BP_GET_TYPE(bp);
		hdr->b_crypt_hdr.b_dsobj = zio->io_bookmark.zb_objset;
		zio_crypt_decode_params_bp(bp, hdr->b_crypt_hdr.b_salt,
		    hdr->b_crypt_hdr.b_iv);

		if (BP_GET_TYPE(bp) == DMU_OT_INTENT_LOG) {
			void *tmpbuf;

			tmpbuf = abd_borrow_buf_copy(zio->io_abd,
			    sizeof (zil_chain_t));
			zio_crypt_decode_mac_zil(tmpbuf,
			    hdr->b_crypt_hdr.b_mac);
			abd_return_buf(zio->io_abd, tmpbuf,
			    sizeof (zil_chain_t));
		} else {
			zio_crypt_decode_mac_bp(bp, hdr->b_crypt_hdr.b_mac);
		}
	}

	if (zio->io_error == 0) {
		/* byteswap if necessary */
		if (BP_SHOULD_BYTESWAP(zio->io_bp)) {
			if (BP_GET_LEVEL(zio->io_bp) > 0) {
				hdr->b_l1hdr.b_byteswap = DMU_BSWAP_UINT64;
			} else {
				hdr->b_l1hdr.b_byteswap =
				    DMU_OT_BYTESWAP(BP_GET_TYPE(zio->io_bp));
			}
		} else {
			hdr->b_l1hdr.b_byteswap = DMU_BSWAP_NUMFUNCS;
		}
		if (!HDR_L2_READING(hdr)) {
			hdr->b_complevel = zio->io_prop.zp_complevel;
		}
	}

	arc_hdr_clear_flags(hdr, ARC_FLAG_L2_EVICTED);
	if (l2arc_noprefetch && HDR_PREFETCH(hdr))
		arc_hdr_clear_flags(hdr, ARC_FLAG_L2CACHE);

	callback_list = hdr->b_l1hdr.b_acb;
	ASSERT3P(callback_list, !=, NULL);

	if (hash_lock && zio->io_error == 0 &&
	    hdr->b_l1hdr.b_state == arc_anon) {
		/*
		 * Only call arc_access on anonymous buffers.  This is because
		 * if we've issued an I/O for an evicted buffer, we've already
		 * called arc_access (to prevent any simultaneous readers from
		 * getting confused).
		 */
		arc_access(hdr, hash_lock);
	}

	/*
	 * If a read request has a callback (i.e. acb_done is not NULL), then we
	 * make a buf containing the data according to the parameters which were
	 * passed in. The implementation of arc_buf_alloc_impl() ensures that we
	 * aren't needlessly decompressing the data multiple times.
	 */
	int callback_cnt = 0;
	for (acb = callback_list; acb != NULL; acb = acb->acb_next) {
		if (!acb->acb_done || acb->acb_nobuf)
			continue;

		callback_cnt++;

		if (zio->io_error != 0)
			continue;

		int error = arc_buf_alloc_impl(hdr, zio->io_spa,
		    &acb->acb_zb, acb->acb_private, acb->acb_encrypted,
		    acb->acb_compressed, acb->acb_noauth, B_TRUE,
		    &acb->acb_buf);

		/*
		 * Assert non-speculative zios didn't fail because an
		 * encryption key wasn't loaded
		 */
		ASSERT((zio->io_flags & ZIO_FLAG_SPECULATIVE) ||
		    error != EACCES);

		/*
		 * If we failed to decrypt, report an error now (as the zio
		 * layer would have done if it had done the transforms).
		 */
		if (error == ECKSUM) {
			ASSERT(BP_IS_PROTECTED(bp));
			error = SET_ERROR(EIO);
			if ((zio->io_flags & ZIO_FLAG_SPECULATIVE) == 0) {
				spa_log_error(zio->io_spa, &acb->acb_zb);
				(void) zfs_ereport_post(
				    FM_EREPORT_ZFS_AUTHENTICATION,
				    zio->io_spa, NULL, &acb->acb_zb, zio, 0);
			}
		}

		if (error != 0) {
			/*
			 * Decompression or decryption failed.  Set
			 * io_error so that when we call acb_done
			 * (below), we will indicate that the read
			 * failed. Note that in the unusual case
			 * where one callback is compressed and another
			 * uncompressed, we will mark all of them
			 * as failed, even though the uncompressed
			 * one can't actually fail.  In this case,
			 * the hdr will not be anonymous, because
			 * if there are multiple callbacks, it's
			 * because multiple threads found the same
			 * arc buf in the hash table.
			 */
			zio->io_error = error;
		}
	}

	/*
	 * If there are multiple callbacks, we must have the hash lock,
	 * because the only way for multiple threads to find this hdr is
	 * in the hash table.  This ensures that if there are multiple
	 * callbacks, the hdr is not anonymous.  If it were anonymous,
	 * we couldn't use arc_buf_destroy() in the error case below.
	 */
	ASSERT(callback_cnt < 2 || hash_lock != NULL);

	hdr->b_l1hdr.b_acb = NULL;
	arc_hdr_clear_flags(hdr, ARC_FLAG_IO_IN_PROGRESS);
	if (callback_cnt == 0)
		ASSERT(hdr->b_l1hdr.b_pabd != NULL || HDR_HAS_RABD(hdr));

	ASSERT(zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt) ||
	    callback_list != NULL);

	if (zio->io_error == 0) {
		arc_hdr_verify(hdr, zio->io_bp);
	} else {
		arc_hdr_set_flags(hdr, ARC_FLAG_IO_ERROR);
		if (hdr->b_l1hdr.b_state != arc_anon)
			arc_change_state(arc_anon, hdr, hash_lock);
		if (HDR_IN_HASH_TABLE(hdr))
			buf_hash_remove(hdr);
		freeable = zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt);
	}

	/*
	 * Broadcast before we drop the hash_lock to avoid the possibility
	 * that the hdr (and hence the cv) might be freed before we get to
	 * the cv_broadcast().
	 */
	cv_broadcast(&hdr->b_l1hdr.b_cv);

	if (hash_lock != NULL) {
		mutex_exit(hash_lock);
	} else {
		/*
		 * This block was freed while we waited for the read to
		 * complete.  It has been removed from the hash table and
		 * moved to the anonymous state (so that it won't show up
		 * in the cache).
		 */
		ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);
		freeable = zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt);
	}

	/* execute each callback and free its structure */
	while ((acb = callback_list) != NULL) {
		if (acb->acb_done != NULL) {
			if (zio->io_error != 0 && acb->acb_buf != NULL) {
				/*
				 * If arc_buf_alloc_impl() fails during
				 * decompression, the buf will still be
				 * allocated, and needs to be freed here.
				 */
				arc_buf_destroy(acb->acb_buf,
				    acb->acb_private);
				acb->acb_buf = NULL;
			}
			acb->acb_done(zio, &zio->io_bookmark, zio->io_bp,
			    acb->acb_buf, acb->acb_private);
		}

		if (acb->acb_zio_dummy != NULL) {
			acb->acb_zio_dummy->io_error = zio->io_error;
			zio_nowait(acb->acb_zio_dummy);
		}

		callback_list = acb->acb_next;
		kmem_free(acb, sizeof (arc_callback_t));
	}

	if (freeable)
		arc_hdr_destroy(hdr);
}

/*
 * "Read" the block at the specified DVA (in bp) via the
 * cache.  If the block is found in the cache, invoke the provided
 * callback immediately and return.  Note that the `zio' parameter
 * in the callback will be NULL in this case, since no IO was
 * required.  If the block is not in the cache pass the read request
 * on to the spa with a substitute callback function, so that the
 * requested block will be added to the cache.
 *
 * If a read request arrives for a block that has a read in-progress,
 * either wait for the in-progress read to complete (and return the
 * results); or, if this is a read with a "done" func, add a record
 * to the read to invoke the "done" func when the read completes,
 * and return; or just return.
 *
 * arc_read_done() will invoke all the requested "done" functions
 * for readers of this block.
 */
int
arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    arc_read_done_func_t *done, void *private, zio_priority_t priority,
    int zio_flags, arc_flags_t *arc_flags, const zbookmark_phys_t *zb)
{
	arc_buf_hdr_t *hdr = NULL;
	kmutex_t *hash_lock = NULL;
	zio_t *rzio;
	uint64_t guid = spa_load_guid(spa);
	boolean_t compressed_read = (zio_flags & ZIO_FLAG_RAW_COMPRESS) != 0;
	boolean_t encrypted_read = BP_IS_ENCRYPTED(bp) &&
	    (zio_flags & ZIO_FLAG_RAW_ENCRYPT) != 0;
	boolean_t noauth_read = BP_IS_AUTHENTICATED(bp) &&
	    (zio_flags & ZIO_FLAG_RAW_ENCRYPT) != 0;
	boolean_t embedded_bp = !!BP_IS_EMBEDDED(bp);
	boolean_t no_buf = *arc_flags & ARC_FLAG_NO_BUF;
	int rc = 0;

	ASSERT(!embedded_bp ||
	    BPE_GET_ETYPE(bp) == BP_EMBEDDED_TYPE_DATA);
	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!BP_IS_REDACTED(bp));

	/*
	 * Normally SPL_FSTRANS will already be set since kernel threads which
	 * expect to call the DMU interfaces will set it when created.  System
	 * calls are similarly handled by setting/cleaning the bit in the
	 * registered callback (module/os/.../zfs/zpl_*).
	 *
	 * External consumers such as Lustre which call the exported DMU
	 * interfaces may not have set SPL_FSTRANS.  To avoid a deadlock
	 * on the hash_lock always set and clear the bit.
	 */
	fstrans_cookie_t cookie = spl_fstrans_mark();
top:
	if (!embedded_bp) {
		/*
		 * Embedded BP's have no DVA and require no I/O to "read".
		 * Create an anonymous arc buf to back it.
		 */
		hdr = buf_hash_find(guid, bp, &hash_lock);
	}

	/*
	 * Determine if we have an L1 cache hit or a cache miss. For simplicity
	 * we maintain encrypted data separately from compressed / uncompressed
	 * data. If the user is requesting raw encrypted data and we don't have
	 * that in the header we will read from disk to guarantee that we can
	 * get it even if the encryption keys aren't loaded.
	 */
	if (hdr != NULL && HDR_HAS_L1HDR(hdr) && (HDR_HAS_RABD(hdr) ||
	    (hdr->b_l1hdr.b_pabd != NULL && !encrypted_read))) {
		arc_buf_t *buf = NULL;
		*arc_flags |= ARC_FLAG_CACHED;

		if (HDR_IO_IN_PROGRESS(hdr)) {
			zio_t *head_zio = hdr->b_l1hdr.b_acb->acb_zio_head;

			if (*arc_flags & ARC_FLAG_CACHED_ONLY) {
				mutex_exit(hash_lock);
				ARCSTAT_BUMP(arcstat_cached_only_in_progress);
				rc = SET_ERROR(ENOENT);
				goto out;
			}

			ASSERT3P(head_zio, !=, NULL);
			if ((hdr->b_flags & ARC_FLAG_PRIO_ASYNC_READ) &&
			    priority == ZIO_PRIORITY_SYNC_READ) {
				/*
				 * This is a sync read that needs to wait for
				 * an in-flight async read. Request that the
				 * zio have its priority upgraded.
				 */
				zio_change_priority(head_zio, priority);
				DTRACE_PROBE1(arc__async__upgrade__sync,
				    arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(arcstat_async_upgrade_sync);
			}
			if (hdr->b_flags & ARC_FLAG_PREDICTIVE_PREFETCH) {
				arc_hdr_clear_flags(hdr,
				    ARC_FLAG_PREDICTIVE_PREFETCH);
			}

			if (*arc_flags & ARC_FLAG_WAIT) {
				cv_wait(&hdr->b_l1hdr.b_cv, hash_lock);
				mutex_exit(hash_lock);
				goto top;
			}
			ASSERT(*arc_flags & ARC_FLAG_NOWAIT);

			if (done) {
				arc_callback_t *acb = NULL;

				acb = kmem_zalloc(sizeof (arc_callback_t),
				    KM_SLEEP);
				acb->acb_done = done;
				acb->acb_private = private;
				acb->acb_compressed = compressed_read;
				acb->acb_encrypted = encrypted_read;
				acb->acb_noauth = noauth_read;
				acb->acb_nobuf = no_buf;
				acb->acb_zb = *zb;
				if (pio != NULL)
					acb->acb_zio_dummy = zio_null(pio,
					    spa, NULL, NULL, NULL, zio_flags);

				ASSERT3P(acb->acb_done, !=, NULL);
				acb->acb_zio_head = head_zio;
				acb->acb_next = hdr->b_l1hdr.b_acb;
				hdr->b_l1hdr.b_acb = acb;
			}
			mutex_exit(hash_lock);
			goto out;
		}

		ASSERT(hdr->b_l1hdr.b_state == arc_mru ||
		    hdr->b_l1hdr.b_state == arc_mfu);

		if (done && !no_buf) {
			if (hdr->b_flags & ARC_FLAG_PREDICTIVE_PREFETCH) {
				/*
				 * This is a demand read which does not have to
				 * wait for i/o because we did a predictive
				 * prefetch i/o for it, which has completed.
				 */
				DTRACE_PROBE1(
				    arc__demand__hit__predictive__prefetch,
				    arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(
				    arcstat_demand_hit_predictive_prefetch);
				arc_hdr_clear_flags(hdr,
				    ARC_FLAG_PREDICTIVE_PREFETCH);
			}

			if (hdr->b_flags & ARC_FLAG_PRESCIENT_PREFETCH) {
				ARCSTAT_BUMP(
				    arcstat_demand_hit_prescient_prefetch);
				arc_hdr_clear_flags(hdr,
				    ARC_FLAG_PRESCIENT_PREFETCH);
			}

			ASSERT(!embedded_bp || !BP_IS_HOLE(bp));

			/* Get a buf with the desired data in it. */
			rc = arc_buf_alloc_impl(hdr, spa, zb, private,
			    encrypted_read, compressed_read, noauth_read,
			    B_TRUE, &buf);
			if (rc == ECKSUM) {
				/*
				 * Convert authentication and decryption errors
				 * to EIO (and generate an ereport if needed)
				 * before leaving the ARC.
				 */
				rc = SET_ERROR(EIO);
				if ((zio_flags & ZIO_FLAG_SPECULATIVE) == 0) {
					spa_log_error(spa, zb);
					(void) zfs_ereport_post(
					    FM_EREPORT_ZFS_AUTHENTICATION,
					    spa, NULL, zb, NULL, 0);
				}
			}
			if (rc != 0) {
				(void) remove_reference(hdr, hash_lock,
				    private);
				arc_buf_destroy_impl(buf);
				buf = NULL;
			}

			/* assert any errors weren't due to unloaded keys */
			ASSERT((zio_flags & ZIO_FLAG_SPECULATIVE) ||
			    rc != EACCES);
		} else if (*arc_flags & ARC_FLAG_PREFETCH &&
		    zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt)) {
			if (HDR_HAS_L2HDR(hdr))
				l2arc_hdr_arcstats_decrement_state(hdr);
			arc_hdr_set_flags(hdr, ARC_FLAG_PREFETCH);
			if (HDR_HAS_L2HDR(hdr))
				l2arc_hdr_arcstats_increment_state(hdr);
		}
		DTRACE_PROBE1(arc__hit, arc_buf_hdr_t *, hdr);
		arc_access(hdr, hash_lock);
		if (*arc_flags & ARC_FLAG_PRESCIENT_PREFETCH)
			arc_hdr_set_flags(hdr, ARC_FLAG_PRESCIENT_PREFETCH);
		if (*arc_flags & ARC_FLAG_L2CACHE)
			arc_hdr_set_flags(hdr, ARC_FLAG_L2CACHE);
		mutex_exit(hash_lock);
		ARCSTAT_BUMP(arcstat_hits);
		ARCSTAT_CONDSTAT(!HDR_PREFETCH(hdr),
		    demand, prefetch, !HDR_ISTYPE_METADATA(hdr),
		    data, metadata, hits);

		if (done)
			done(NULL, zb, bp, buf, private);
	} else {
		uint64_t lsize = BP_GET_LSIZE(bp);
		uint64_t psize = BP_GET_PSIZE(bp);
		arc_callback_t *acb;
		vdev_t *vd = NULL;
		uint64_t addr = 0;
		boolean_t devw = B_FALSE;
		uint64_t size;
		abd_t *hdr_abd;
		int alloc_flags = encrypted_read ? ARC_HDR_ALLOC_RDATA : 0;

		if (*arc_flags & ARC_FLAG_CACHED_ONLY) {
			rc = SET_ERROR(ENOENT);
			if (hash_lock != NULL)
				mutex_exit(hash_lock);
			goto out;
		}

		/*
		 * Gracefully handle a damaged logical block size as a
		 * checksum error.
		 */
		if (lsize > spa_maxblocksize(spa)) {
			rc = SET_ERROR(ECKSUM);
			if (hash_lock != NULL)
				mutex_exit(hash_lock);
			goto out;
		}

		if (hdr == NULL) {
			/*
			 * This block is not in the cache or it has
			 * embedded data.
			 */
			arc_buf_hdr_t *exists = NULL;
			arc_buf_contents_t type = BP_GET_BUFC_TYPE(bp);
			hdr = arc_hdr_alloc(spa_load_guid(spa), psize, lsize,
			    BP_IS_PROTECTED(bp), BP_GET_COMPRESS(bp), 0, type,
			    encrypted_read);

			if (!embedded_bp) {
				hdr->b_dva = *BP_IDENTITY(bp);
				hdr->b_birth = BP_PHYSICAL_BIRTH(bp);
				exists = buf_hash_insert(hdr, &hash_lock);
			}
			if (exists != NULL) {
				/* somebody beat us to the hash insert */
				mutex_exit(hash_lock);
				buf_discard_identity(hdr);
				arc_hdr_destroy(hdr);
				goto top; /* restart the IO request */
			}
		} else {
			/*
			 * This block is in the ghost cache or encrypted data
			 * was requested and we didn't have it. If it was
			 * L2-only (and thus didn't have an L1 hdr),
			 * we realloc the header to add an L1 hdr.
			 */
			if (!HDR_HAS_L1HDR(hdr)) {
				hdr = arc_hdr_realloc(hdr, hdr_l2only_cache,
				    hdr_full_cache);
			}

			if (GHOST_STATE(hdr->b_l1hdr.b_state)) {
				ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
				ASSERT(!HDR_HAS_RABD(hdr));
				ASSERT(!HDR_IO_IN_PROGRESS(hdr));
				ASSERT0(zfs_refcount_count(
				    &hdr->b_l1hdr.b_refcnt));
				ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
				ASSERT3P(hdr->b_l1hdr.b_freeze_cksum, ==, NULL);
			} else if (HDR_IO_IN_PROGRESS(hdr)) {
				/*
				 * If this header already had an IO in progress
				 * and we are performing another IO to fetch
				 * encrypted data we must wait until the first
				 * IO completes so as not to confuse
				 * arc_read_done(). This should be very rare
				 * and so the performance impact shouldn't
				 * matter.
				 */
				cv_wait(&hdr->b_l1hdr.b_cv, hash_lock);
				mutex_exit(hash_lock);
				goto top;
			}

			/*
			 * This is a delicate dance that we play here.
			 * This hdr might be in the ghost list so we access
			 * it to move it out of the ghost list before we
			 * initiate the read. If it's a prefetch then
			 * it won't have a callback so we'll remove the
			 * reference that arc_buf_alloc_impl() created. We
			 * do this after we've called arc_access() to
			 * avoid hitting an assert in remove_reference().
			 */
			arc_adapt(arc_hdr_size(hdr), hdr->b_l1hdr.b_state);
			arc_access(hdr, hash_lock);
			arc_hdr_alloc_abd(hdr, alloc_flags);
		}

		if (encrypted_read) {
			ASSERT(HDR_HAS_RABD(hdr));
			size = HDR_GET_PSIZE(hdr);
			hdr_abd = hdr->b_crypt_hdr.b_rabd;
			zio_flags |= ZIO_FLAG_RAW;
		} else {
			ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);
			size = arc_hdr_size(hdr);
			hdr_abd = hdr->b_l1hdr.b_pabd;

			if (arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF) {
				zio_flags |= ZIO_FLAG_RAW_COMPRESS;
			}

			/*
			 * For authenticated bp's, we do not ask the ZIO layer
			 * to authenticate them since this will cause the entire
			 * IO to fail if the key isn't loaded. Instead, we
			 * defer authentication until arc_buf_fill(), which will
			 * verify the data when the key is available.
			 */
			if (BP_IS_AUTHENTICATED(bp))
				zio_flags |= ZIO_FLAG_RAW_ENCRYPT;
		}

		if (*arc_flags & ARC_FLAG_PREFETCH &&
		    zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt)) {
			if (HDR_HAS_L2HDR(hdr))
				l2arc_hdr_arcstats_decrement_state(hdr);
			arc_hdr_set_flags(hdr, ARC_FLAG_PREFETCH);
			if (HDR_HAS_L2HDR(hdr))
				l2arc_hdr_arcstats_increment_state(hdr);
		}
		if (*arc_flags & ARC_FLAG_PRESCIENT_PREFETCH)
			arc_hdr_set_flags(hdr, ARC_FLAG_PRESCIENT_PREFETCH);
		if (*arc_flags & ARC_FLAG_L2CACHE)
			arc_hdr_set_flags(hdr, ARC_FLAG_L2CACHE);
		if (BP_IS_AUTHENTICATED(bp))
			arc_hdr_set_flags(hdr, ARC_FLAG_NOAUTH);
		if (BP_GET_LEVEL(bp) > 0)
			arc_hdr_set_flags(hdr, ARC_FLAG_INDIRECT);
		if (*arc_flags & ARC_FLAG_PREDICTIVE_PREFETCH)
			arc_hdr_set_flags(hdr, ARC_FLAG_PREDICTIVE_PREFETCH);
		ASSERT(!GHOST_STATE(hdr->b_l1hdr.b_state));

		acb = kmem_zalloc(sizeof (arc_callback_t), KM_SLEEP);
		acb->acb_done = done;
		acb->acb_private = private;
		acb->acb_compressed = compressed_read;
		acb->acb_encrypted = encrypted_read;
		acb->acb_noauth = noauth_read;
		acb->acb_zb = *zb;

		ASSERT3P(hdr->b_l1hdr.b_acb, ==, NULL);
		hdr->b_l1hdr.b_acb = acb;
		arc_hdr_set_flags(hdr, ARC_FLAG_IO_IN_PROGRESS);

		if (HDR_HAS_L2HDR(hdr) &&
		    (vd = hdr->b_l2hdr.b_dev->l2ad_vdev) != NULL) {
			devw = hdr->b_l2hdr.b_dev->l2ad_writing;
			addr = hdr->b_l2hdr.b_daddr;
			/*
			 * Lock out L2ARC device removal.
			 */
			if (vdev_is_dead(vd) ||
			    !spa_config_tryenter(spa, SCL_L2ARC, vd, RW_READER))
				vd = NULL;
		}

		/*
		 * We count both async reads and scrub IOs as asynchronous so
		 * that both can be upgraded in the event of a cache hit while
		 * the read IO is still in-flight.
		 */
		if (priority == ZIO_PRIORITY_ASYNC_READ ||
		    priority == ZIO_PRIORITY_SCRUB)
			arc_hdr_set_flags(hdr, ARC_FLAG_PRIO_ASYNC_READ);
		else
			arc_hdr_clear_flags(hdr, ARC_FLAG_PRIO_ASYNC_READ);

		/*
		 * At this point, we have a level 1 cache miss or a blkptr
		 * with embedded data.  Try again in L2ARC if possible.
		 */
		ASSERT3U(HDR_GET_LSIZE(hdr), ==, lsize);

		/*
		 * Skip ARC stat bump for block pointers with embedded
		 * data. The data are read from the blkptr itself via
		 * decode_embedded_bp_compressed().
		 */
		if (!embedded_bp) {
			DTRACE_PROBE4(arc__miss, arc_buf_hdr_t *, hdr,
			    blkptr_t *, bp, uint64_t, lsize,
			    zbookmark_phys_t *, zb);
			ARCSTAT_BUMP(arcstat_misses);
			ARCSTAT_CONDSTAT(!HDR_PREFETCH(hdr),
			    demand, prefetch, !HDR_ISTYPE_METADATA(hdr), data,
			    metadata, misses);
		}

		/* Check if the spa even has l2 configured */
		const boolean_t spa_has_l2 = l2arc_ndev != 0 &&
		    spa->spa_l2cache.sav_count > 0;

		if (vd != NULL && spa_has_l2 && !(l2arc_norw && devw)) {
			/*
			 * Read from the L2ARC if the following are true:
			 * 1. The L2ARC vdev was previously cached.
			 * 2. This buffer still has L2ARC metadata.
			 * 3. This buffer isn't currently writing to the L2ARC.
			 * 4. The L2ARC entry wasn't evicted, which may
			 *    also have invalidated the vdev.
			 * 5. This isn't prefetch or l2arc_noprefetch is 0.
			 */
			if (HDR_HAS_L2HDR(hdr) &&
			    !HDR_L2_WRITING(hdr) && !HDR_L2_EVICTED(hdr) &&
			    !(l2arc_noprefetch && HDR_PREFETCH(hdr))) {
				l2arc_read_callback_t *cb;
				abd_t *abd;
				uint64_t asize;

				DTRACE_PROBE1(l2arc__hit, arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(arcstat_l2_hits);
				atomic_inc_32(&hdr->b_l2hdr.b_hits);

				cb = kmem_zalloc(sizeof (l2arc_read_callback_t),
				    KM_SLEEP);
				cb->l2rcb_hdr = hdr;
				cb->l2rcb_bp = *bp;
				cb->l2rcb_zb = *zb;
				cb->l2rcb_flags = zio_flags;

				/*
				 * When Compressed ARC is disabled, but the
				 * L2ARC block is compressed, arc_hdr_size()
				 * will have returned LSIZE rather than PSIZE.
				 */
				if (HDR_GET_COMPRESS(hdr) != ZIO_COMPRESS_OFF &&
				    !HDR_COMPRESSION_ENABLED(hdr) &&
				    HDR_GET_PSIZE(hdr) != 0) {
					size = HDR_GET_PSIZE(hdr);
				}

				asize = vdev_psize_to_asize(vd, size);
				if (asize != size) {
					abd = abd_alloc_for_io(asize,
					    HDR_ISTYPE_METADATA(hdr));
					cb->l2rcb_abd = abd;
				} else {
					abd = hdr_abd;
				}

				ASSERT(addr >= VDEV_LABEL_START_SIZE &&
				    addr + asize <= vd->vdev_psize -
				    VDEV_LABEL_END_SIZE);

				/*
				 * l2arc read.  The SCL_L2ARC lock will be
				 * released by l2arc_read_done().
				 * Issue a null zio if the underlying buffer
				 * was squashed to zero size by compression.
				 */
				ASSERT3U(arc_hdr_get_compress(hdr), !=,
				    ZIO_COMPRESS_EMPTY);
				rzio = zio_read_phys(pio, vd, addr,
				    asize, abd,
				    ZIO_CHECKSUM_OFF,
				    l2arc_read_done, cb, priority,
				    zio_flags | ZIO_FLAG_DONT_CACHE |
				    ZIO_FLAG_CANFAIL |
				    ZIO_FLAG_DONT_PROPAGATE |
				    ZIO_FLAG_DONT_RETRY, B_FALSE);
				acb->acb_zio_head = rzio;

				if (hash_lock != NULL)
					mutex_exit(hash_lock);

				DTRACE_PROBE2(l2arc__read, vdev_t *, vd,
				    zio_t *, rzio);
				ARCSTAT_INCR(arcstat_l2_read_bytes,
				    HDR_GET_PSIZE(hdr));

				if (*arc_flags & ARC_FLAG_NOWAIT) {
					zio_nowait(rzio);
					goto out;
				}

				ASSERT(*arc_flags & ARC_FLAG_WAIT);
				if (zio_wait(rzio) == 0)
					goto out;

				/* l2arc read error; goto zio_read() */
				if (hash_lock != NULL)
					mutex_enter(hash_lock);
			} else {
				DTRACE_PROBE1(l2arc__miss,
				    arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(arcstat_l2_misses);
				if (HDR_L2_WRITING(hdr))
					ARCSTAT_BUMP(arcstat_l2_rw_clash);
				spa_config_exit(spa, SCL_L2ARC, vd);
			}
		} else {
			if (vd != NULL)
				spa_config_exit(spa, SCL_L2ARC, vd);

			/*
			 * Only a spa with l2 should contribute to l2
			 * miss stats.  (Including the case of having a
			 * faulted cache device - that's also a miss.)
			 */
			if (spa_has_l2) {
				/*
				 * Skip ARC stat bump for block pointers with
				 * embedded data. The data are read from the
				 * blkptr itself via
				 * decode_embedded_bp_compressed().
				 */
				if (!embedded_bp) {
					DTRACE_PROBE1(l2arc__miss,
					    arc_buf_hdr_t *, hdr);
					ARCSTAT_BUMP(arcstat_l2_misses);
				}
			}
		}

		rzio = zio_read(pio, spa, bp, hdr_abd, size,
		    arc_read_done, hdr, priority, zio_flags, zb);
		acb->acb_zio_head = rzio;

		if (hash_lock != NULL)
			mutex_exit(hash_lock);

		if (*arc_flags & ARC_FLAG_WAIT) {
			rc = zio_wait(rzio);
			goto out;
		}

		ASSERT(*arc_flags & ARC_FLAG_NOWAIT);
		zio_nowait(rzio);
	}

out:
	/* embedded bps don't actually go to disk */
	if (!embedded_bp)
		spa_read_history_add(spa, zb, *arc_flags);
	spl_fstrans_unmark(cookie);
	return (rc);
}

arc_prune_t *
arc_add_prune_callback(arc_prune_func_t *func, void *private)
{
	arc_prune_t *p;

	p = kmem_alloc(sizeof (*p), KM_SLEEP);
	p->p_pfunc = func;
	p->p_private = private;
	list_link_init(&p->p_node);
	zfs_refcount_create(&p->p_refcnt);

	mutex_enter(&arc_prune_mtx);
	zfs_refcount_add(&p->p_refcnt, &arc_prune_list);
	list_insert_head(&arc_prune_list, p);
	mutex_exit(&arc_prune_mtx);

	return (p);
}

void
arc_remove_prune_callback(arc_prune_t *p)
{
	boolean_t wait = B_FALSE;
	mutex_enter(&arc_prune_mtx);
	list_remove(&arc_prune_list, p);
	if (zfs_refcount_remove(&p->p_refcnt, &arc_prune_list) > 0)
		wait = B_TRUE;
	mutex_exit(&arc_prune_mtx);

	/* wait for arc_prune_task to finish */
	if (wait)
		taskq_wait_outstanding(arc_prune_taskq, 0);
	ASSERT0(zfs_refcount_count(&p->p_refcnt));
	zfs_refcount_destroy(&p->p_refcnt);
	kmem_free(p, sizeof (*p));
}

/*
 * Notify the arc that a block was freed, and thus will never be used again.
 */
void
arc_freed(spa_t *spa, const blkptr_t *bp)
{
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;
	uint64_t guid = spa_load_guid(spa);

	ASSERT(!BP_IS_EMBEDDED(bp));

	hdr = buf_hash_find(guid, bp, &hash_lock);
	if (hdr == NULL)
		return;

	/*
	 * We might be trying to free a block that is still doing I/O
	 * (i.e. prefetch) or has a reference (i.e. a dedup-ed,
	 * dmu_sync-ed block). If this block is being prefetched, then it
	 * would still have the ARC_FLAG_IO_IN_PROGRESS flag set on the hdr
	 * until the I/O completes. A block may also have a reference if it is
	 * part of a dedup-ed, dmu_synced write. The dmu_sync() function would
	 * have written the new block to its final resting place on disk but
	 * without the dedup flag set. This would have left the hdr in the MRU
	 * state and discoverable. When the txg finally syncs it detects that
	 * the block was overridden in open context and issues an override I/O.
	 * Since this is a dedup block, the override I/O will determine if the
	 * block is already in the DDT. If so, then it will replace the io_bp
	 * with the bp from the DDT and allow the I/O to finish. When the I/O
	 * reaches the done callback, dbuf_write_override_done, it will
	 * check to see if the io_bp and io_bp_override are identical.
	 * If they are not, then it indicates that the bp was replaced with
	 * the bp in the DDT and the override bp is freed. This allows
	 * us to arrive here with a reference on a block that is being
	 * freed. So if we have an I/O in progress, or a reference to
	 * this hdr, then we don't destroy the hdr.
	 */
	if (!HDR_HAS_L1HDR(hdr) || (!HDR_IO_IN_PROGRESS(hdr) &&
	    zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt))) {
		arc_change_state(arc_anon, hdr, hash_lock);
		arc_hdr_destroy(hdr);
		mutex_exit(hash_lock);
	} else {
		mutex_exit(hash_lock);
	}

}

/*
 * Release this buffer from the cache, making it an anonymous buffer.  This
 * must be done after a read and prior to modifying the buffer contents.
 * If the buffer has more than one reference, we must make
 * a new hdr for the buffer.
 */
void
arc_release(arc_buf_t *buf, void *tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	/*
	 * It would be nice to assert that if its DMU metadata (level >
	 * 0 || it's the dnode file), then it must be syncing context.
	 * But we don't know that information at this level.
	 */

	mutex_enter(&buf->b_evict_lock);

	ASSERT(HDR_HAS_L1HDR(hdr));

	/*
	 * We don't grab the hash lock prior to this check, because if
	 * the buffer's header is in the arc_anon state, it won't be
	 * linked into the hash table.
	 */
	if (hdr->b_l1hdr.b_state == arc_anon) {
		mutex_exit(&buf->b_evict_lock);
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		ASSERT(!HDR_IN_HASH_TABLE(hdr));
		ASSERT(!HDR_HAS_L2HDR(hdr));
		ASSERT(HDR_EMPTY(hdr));

		ASSERT3U(hdr->b_l1hdr.b_bufcnt, ==, 1);
		ASSERT3S(zfs_refcount_count(&hdr->b_l1hdr.b_refcnt), ==, 1);
		ASSERT(!list_link_active(&hdr->b_l1hdr.b_arc_node));

		hdr->b_l1hdr.b_arc_access = 0;

		/*
		 * If the buf is being overridden then it may already
		 * have a hdr that is not empty.
		 */
		buf_discard_identity(hdr);
		arc_buf_thaw(buf);

		return;
	}

	kmutex_t *hash_lock = HDR_LOCK(hdr);
	mutex_enter(hash_lock);

	/*
	 * This assignment is only valid as long as the hash_lock is
	 * held, we must be careful not to reference state or the
	 * b_state field after dropping the lock.
	 */
	arc_state_t *state = hdr->b_l1hdr.b_state;
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));
	ASSERT3P(state, !=, arc_anon);

	/* this buffer is not on any list */
	ASSERT3S(zfs_refcount_count(&hdr->b_l1hdr.b_refcnt), >, 0);

	if (HDR_HAS_L2HDR(hdr)) {
		mutex_enter(&hdr->b_l2hdr.b_dev->l2ad_mtx);

		/*
		 * We have to recheck this conditional again now that
		 * we're holding the l2ad_mtx to prevent a race with
		 * another thread which might be concurrently calling
		 * l2arc_evict(). In that case, l2arc_evict() might have
		 * destroyed the header's L2 portion as we were waiting
		 * to acquire the l2ad_mtx.
		 */
		if (HDR_HAS_L2HDR(hdr))
			arc_hdr_l2hdr_destroy(hdr);

		mutex_exit(&hdr->b_l2hdr.b_dev->l2ad_mtx);
	}

	/*
	 * Do we have more than one buf?
	 */
	if (hdr->b_l1hdr.b_bufcnt > 1) {
		arc_buf_hdr_t *nhdr;
		uint64_t spa = hdr->b_spa;
		uint64_t psize = HDR_GET_PSIZE(hdr);
		uint64_t lsize = HDR_GET_LSIZE(hdr);
		boolean_t protected = HDR_PROTECTED(hdr);
		enum zio_compress compress = arc_hdr_get_compress(hdr);
		arc_buf_contents_t type = arc_buf_type(hdr);
		VERIFY3U(hdr->b_type, ==, type);

		ASSERT(hdr->b_l1hdr.b_buf != buf || buf->b_next != NULL);
		(void) remove_reference(hdr, hash_lock, tag);

		if (arc_buf_is_shared(buf) && !ARC_BUF_COMPRESSED(buf)) {
			ASSERT3P(hdr->b_l1hdr.b_buf, !=, buf);
			ASSERT(ARC_BUF_LAST(buf));
		}

		/*
		 * Pull the data off of this hdr and attach it to
		 * a new anonymous hdr. Also find the last buffer
		 * in the hdr's buffer list.
		 */
		arc_buf_t *lastbuf = arc_buf_remove(hdr, buf);
		ASSERT3P(lastbuf, !=, NULL);

		/*
		 * If the current arc_buf_t and the hdr are sharing their data
		 * buffer, then we must stop sharing that block.
		 */
		if (arc_buf_is_shared(buf)) {
			ASSERT3P(hdr->b_l1hdr.b_buf, !=, buf);
			VERIFY(!arc_buf_is_shared(lastbuf));

			/*
			 * First, sever the block sharing relationship between
			 * buf and the arc_buf_hdr_t.
			 */
			arc_unshare_buf(hdr, buf);

			/*
			 * Now we need to recreate the hdr's b_pabd. Since we
			 * have lastbuf handy, we try to share with it, but if
			 * we can't then we allocate a new b_pabd and copy the
			 * data from buf into it.
			 */
			if (arc_can_share(hdr, lastbuf)) {
				arc_share_buf(hdr, lastbuf);
			} else {
				arc_hdr_alloc_abd(hdr, ARC_HDR_DO_ADAPT);
				abd_copy_from_buf(hdr->b_l1hdr.b_pabd,
				    buf->b_data, psize);
			}
			VERIFY3P(lastbuf->b_data, !=, NULL);
		} else if (HDR_SHARED_DATA(hdr)) {
			/*
			 * Uncompressed shared buffers are always at the end
			 * of the list. Compressed buffers don't have the
			 * same requirements. This makes it hard to
			 * simply assert that the lastbuf is shared so
			 * we rely on the hdr's compression flags to determine
			 * if we have a compressed, shared buffer.
			 */
			ASSERT(arc_buf_is_shared(lastbuf) ||
			    arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF);
			ASSERT(!ARC_BUF_SHARED(buf));
		}

		ASSERT(hdr->b_l1hdr.b_pabd != NULL || HDR_HAS_RABD(hdr));
		ASSERT3P(state, !=, arc_l2c_only);

		(void) zfs_refcount_remove_many(&state->arcs_size,
		    arc_buf_size(buf), buf);

		if (zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt)) {
			ASSERT3P(state, !=, arc_l2c_only);
			(void) zfs_refcount_remove_many(
			    &state->arcs_esize[type],
			    arc_buf_size(buf), buf);
		}

		hdr->b_l1hdr.b_bufcnt -= 1;
		if (ARC_BUF_ENCRYPTED(buf))
			hdr->b_crypt_hdr.b_ebufcnt -= 1;

		arc_cksum_verify(buf);
		arc_buf_unwatch(buf);

		/* if this is the last uncompressed buf free the checksum */
		if (!arc_hdr_has_uncompressed_buf(hdr))
			arc_cksum_free(hdr);

		mutex_exit(hash_lock);

		/*
		 * Allocate a new hdr. The new hdr will contain a b_pabd
		 * buffer which will be freed in arc_write().
		 */
		nhdr = arc_hdr_alloc(spa, psize, lsize, protected,
		    compress, hdr->b_complevel, type, HDR_HAS_RABD(hdr));
		ASSERT3P(nhdr->b_l1hdr.b_buf, ==, NULL);
		ASSERT0(nhdr->b_l1hdr.b_bufcnt);
		ASSERT0(zfs_refcount_count(&nhdr->b_l1hdr.b_refcnt));
		VERIFY3U(nhdr->b_type, ==, type);
		ASSERT(!HDR_SHARED_DATA(nhdr));

		nhdr->b_l1hdr.b_buf = buf;
		nhdr->b_l1hdr.b_bufcnt = 1;
		if (ARC_BUF_ENCRYPTED(buf))
			nhdr->b_crypt_hdr.b_ebufcnt = 1;
		nhdr->b_l1hdr.b_mru_hits = 0;
		nhdr->b_l1hdr.b_mru_ghost_hits = 0;
		nhdr->b_l1hdr.b_mfu_hits = 0;
		nhdr->b_l1hdr.b_mfu_ghost_hits = 0;
		nhdr->b_l1hdr.b_l2_hits = 0;
		(void) zfs_refcount_add(&nhdr->b_l1hdr.b_refcnt, tag);
		buf->b_hdr = nhdr;

		mutex_exit(&buf->b_evict_lock);
		(void) zfs_refcount_add_many(&arc_anon->arcs_size,
		    arc_buf_size(buf), buf);
	} else {
		mutex_exit(&buf->b_evict_lock);
		ASSERT(zfs_refcount_count(&hdr->b_l1hdr.b_refcnt) == 1);
		/* protected by hash lock, or hdr is on arc_anon */
		ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		hdr->b_l1hdr.b_mru_hits = 0;
		hdr->b_l1hdr.b_mru_ghost_hits = 0;
		hdr->b_l1hdr.b_mfu_hits = 0;
		hdr->b_l1hdr.b_mfu_ghost_hits = 0;
		hdr->b_l1hdr.b_l2_hits = 0;
		arc_change_state(arc_anon, hdr, hash_lock);
		hdr->b_l1hdr.b_arc_access = 0;

		mutex_exit(hash_lock);
		buf_discard_identity(hdr);
		arc_buf_thaw(buf);
	}
}

int
arc_released(arc_buf_t *buf)
{
	int released;

	mutex_enter(&buf->b_evict_lock);
	released = (buf->b_data != NULL &&
	    buf->b_hdr->b_l1hdr.b_state == arc_anon);
	mutex_exit(&buf->b_evict_lock);
	return (released);
}

#ifdef ZFS_DEBUG
int
arc_referenced(arc_buf_t *buf)
{
	int referenced;

	mutex_enter(&buf->b_evict_lock);
	referenced = (zfs_refcount_count(&buf->b_hdr->b_l1hdr.b_refcnt));
	mutex_exit(&buf->b_evict_lock);
	return (referenced);
}
#endif

static void
arc_write_ready(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;
	arc_buf_t *buf = callback->awcb_buf;
	arc_buf_hdr_t *hdr = buf->b_hdr;
	blkptr_t *bp = zio->io_bp;
	uint64_t psize = BP_IS_HOLE(bp) ? 0 : BP_GET_PSIZE(bp);
	fstrans_cookie_t cookie = spl_fstrans_mark();

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(!zfs_refcount_is_zero(&buf->b_hdr->b_l1hdr.b_refcnt));
	ASSERT(hdr->b_l1hdr.b_bufcnt > 0);

	/*
	 * If we're reexecuting this zio because the pool suspended, then
	 * cleanup any state that was previously set the first time the
	 * callback was invoked.
	 */
	if (zio->io_flags & ZIO_FLAG_REEXECUTED) {
		arc_cksum_free(hdr);
		arc_buf_unwatch(buf);
		if (hdr->b_l1hdr.b_pabd != NULL) {
			if (arc_buf_is_shared(buf)) {
				arc_unshare_buf(hdr, buf);
			} else {
				arc_hdr_free_abd(hdr, B_FALSE);
			}
		}

		if (HDR_HAS_RABD(hdr))
			arc_hdr_free_abd(hdr, B_TRUE);
	}
	ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);
	ASSERT(!HDR_HAS_RABD(hdr));
	ASSERT(!HDR_SHARED_DATA(hdr));
	ASSERT(!arc_buf_is_shared(buf));

	callback->awcb_ready(zio, buf, callback->awcb_private);

	if (HDR_IO_IN_PROGRESS(hdr))
		ASSERT(zio->io_flags & ZIO_FLAG_REEXECUTED);

	arc_hdr_set_flags(hdr, ARC_FLAG_IO_IN_PROGRESS);

	if (BP_IS_PROTECTED(bp) != !!HDR_PROTECTED(hdr))
		hdr = arc_hdr_realloc_crypt(hdr, BP_IS_PROTECTED(bp));

	if (BP_IS_PROTECTED(bp)) {
		/* ZIL blocks are written through zio_rewrite */
		ASSERT3U(BP_GET_TYPE(bp), !=, DMU_OT_INTENT_LOG);
		ASSERT(HDR_PROTECTED(hdr));

		if (BP_SHOULD_BYTESWAP(bp)) {
			if (BP_GET_LEVEL(bp) > 0) {
				hdr->b_l1hdr.b_byteswap = DMU_BSWAP_UINT64;
			} else {
				hdr->b_l1hdr.b_byteswap =
				    DMU_OT_BYTESWAP(BP_GET_TYPE(bp));
			}
		} else {
			hdr->b_l1hdr.b_byteswap = DMU_BSWAP_NUMFUNCS;
		}

		hdr->b_crypt_hdr.b_ot = BP_GET_TYPE(bp);
		hdr->b_crypt_hdr.b_dsobj = zio->io_bookmark.zb_objset;
		zio_crypt_decode_params_bp(bp, hdr->b_crypt_hdr.b_salt,
		    hdr->b_crypt_hdr.b_iv);
		zio_crypt_decode_mac_bp(bp, hdr->b_crypt_hdr.b_mac);
	}

	/*
	 * If this block was written for raw encryption but the zio layer
	 * ended up only authenticating it, adjust the buffer flags now.
	 */
	if (BP_IS_AUTHENTICATED(bp) && ARC_BUF_ENCRYPTED(buf)) {
		arc_hdr_set_flags(hdr, ARC_FLAG_NOAUTH);
		buf->b_flags &= ~ARC_BUF_FLAG_ENCRYPTED;
		if (BP_GET_COMPRESS(bp) == ZIO_COMPRESS_OFF)
			buf->b_flags &= ~ARC_BUF_FLAG_COMPRESSED;
	} else if (BP_IS_HOLE(bp) && ARC_BUF_ENCRYPTED(buf)) {
		buf->b_flags &= ~ARC_BUF_FLAG_ENCRYPTED;
		buf->b_flags &= ~ARC_BUF_FLAG_COMPRESSED;
	}

	/* this must be done after the buffer flags are adjusted */
	arc_cksum_compute(buf);

	enum zio_compress compress;
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp)) {
		compress = ZIO_COMPRESS_OFF;
	} else {
		ASSERT3U(HDR_GET_LSIZE(hdr), ==, BP_GET_LSIZE(bp));
		compress = BP_GET_COMPRESS(bp);
	}
	HDR_SET_PSIZE(hdr, psize);
	arc_hdr_set_compress(hdr, compress);
	hdr->b_complevel = zio->io_prop.zp_complevel;

	if (zio->io_error != 0 || psize == 0)
		goto out;

	/*
	 * Fill the hdr with data. If the buffer is encrypted we have no choice
	 * but to copy the data into b_radb. If the hdr is compressed, the data
	 * we want is available from the zio, otherwise we can take it from
	 * the buf.
	 *
	 * We might be able to share the buf's data with the hdr here. However,
	 * doing so would cause the ARC to be full of linear ABDs if we write a
	 * lot of shareable data. As a compromise, we check whether scattered
	 * ABDs are allowed, and assume that if they are then the user wants
	 * the ARC to be primarily filled with them regardless of the data being
	 * written. Therefore, if they're allowed then we allocate one and copy
	 * the data into it; otherwise, we share the data directly if we can.
	 */
	if (ARC_BUF_ENCRYPTED(buf)) {
		ASSERT3U(psize, >, 0);
		ASSERT(ARC_BUF_COMPRESSED(buf));
		arc_hdr_alloc_abd(hdr, ARC_HDR_DO_ADAPT|ARC_HDR_ALLOC_RDATA);
		abd_copy(hdr->b_crypt_hdr.b_rabd, zio->io_abd, psize);
	} else if (zfs_abd_scatter_enabled || !arc_can_share(hdr, buf)) {
		/*
		 * Ideally, we would always copy the io_abd into b_pabd, but the
		 * user may have disabled compressed ARC, thus we must check the
		 * hdr's compression setting rather than the io_bp's.
		 */
		if (BP_IS_ENCRYPTED(bp)) {
			ASSERT3U(psize, >, 0);
			arc_hdr_alloc_abd(hdr,
			    ARC_HDR_DO_ADAPT|ARC_HDR_ALLOC_RDATA);
			abd_copy(hdr->b_crypt_hdr.b_rabd, zio->io_abd, psize);
		} else if (arc_hdr_get_compress(hdr) != ZIO_COMPRESS_OFF &&
		    !ARC_BUF_COMPRESSED(buf)) {
			ASSERT3U(psize, >, 0);
			arc_hdr_alloc_abd(hdr, ARC_HDR_DO_ADAPT);
			abd_copy(hdr->b_l1hdr.b_pabd, zio->io_abd, psize);
		} else {
			ASSERT3U(zio->io_orig_size, ==, arc_hdr_size(hdr));
			arc_hdr_alloc_abd(hdr, ARC_HDR_DO_ADAPT);
			abd_copy_from_buf(hdr->b_l1hdr.b_pabd, buf->b_data,
			    arc_buf_size(buf));
		}
	} else {
		ASSERT3P(buf->b_data, ==, abd_to_buf(zio->io_orig_abd));
		ASSERT3U(zio->io_orig_size, ==, arc_buf_size(buf));
		ASSERT3U(hdr->b_l1hdr.b_bufcnt, ==, 1);

		arc_share_buf(hdr, buf);
	}

out:
	arc_hdr_verify(hdr, bp);
	spl_fstrans_unmark(cookie);
}

static void
arc_write_children_ready(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;
	arc_buf_t *buf = callback->awcb_buf;

	callback->awcb_children_ready(zio, buf, callback->awcb_private);
}

/*
 * The SPA calls this callback for each physical write that happens on behalf
 * of a logical write.  See the comment in dbuf_write_physdone() for details.
 */
static void
arc_write_physdone(zio_t *zio)
{
	arc_write_callback_t *cb = zio->io_private;
	if (cb->awcb_physdone != NULL)
		cb->awcb_physdone(zio, cb->awcb_buf, cb->awcb_private);
}

static void
arc_write_done(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;
	arc_buf_t *buf = callback->awcb_buf;
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT3P(hdr->b_l1hdr.b_acb, ==, NULL);

	if (zio->io_error == 0) {
		arc_hdr_verify(hdr, zio->io_bp);

		if (BP_IS_HOLE(zio->io_bp) || BP_IS_EMBEDDED(zio->io_bp)) {
			buf_discard_identity(hdr);
		} else {
			hdr->b_dva = *BP_IDENTITY(zio->io_bp);
			hdr->b_birth = BP_PHYSICAL_BIRTH(zio->io_bp);
		}
	} else {
		ASSERT(HDR_EMPTY(hdr));
	}

	/*
	 * If the block to be written was all-zero or compressed enough to be
	 * embedded in the BP, no write was performed so there will be no
	 * dva/birth/checksum.  The buffer must therefore remain anonymous
	 * (and uncached).
	 */
	if (!HDR_EMPTY(hdr)) {
		arc_buf_hdr_t *exists;
		kmutex_t *hash_lock;

		ASSERT3U(zio->io_error, ==, 0);

		arc_cksum_verify(buf);

		exists = buf_hash_insert(hdr, &hash_lock);
		if (exists != NULL) {
			/*
			 * This can only happen if we overwrite for
			 * sync-to-convergence, because we remove
			 * buffers from the hash table when we arc_free().
			 */
			if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
				if (!BP_EQUAL(&zio->io_bp_orig, zio->io_bp))
					panic("bad overwrite, hdr=%p exists=%p",
					    (void *)hdr, (void *)exists);
				ASSERT(zfs_refcount_is_zero(
				    &exists->b_l1hdr.b_refcnt));
				arc_change_state(arc_anon, exists, hash_lock);
				arc_hdr_destroy(exists);
				mutex_exit(hash_lock);
				exists = buf_hash_insert(hdr, &hash_lock);
				ASSERT3P(exists, ==, NULL);
			} else if (zio->io_flags & ZIO_FLAG_NOPWRITE) {
				/* nopwrite */
				ASSERT(zio->io_prop.zp_nopwrite);
				if (!BP_EQUAL(&zio->io_bp_orig, zio->io_bp))
					panic("bad nopwrite, hdr=%p exists=%p",
					    (void *)hdr, (void *)exists);
			} else {
				/* Dedup */
				ASSERT(hdr->b_l1hdr.b_bufcnt == 1);
				ASSERT(hdr->b_l1hdr.b_state == arc_anon);
				ASSERT(BP_GET_DEDUP(zio->io_bp));
				ASSERT(BP_GET_LEVEL(zio->io_bp) == 0);
			}
		}
		arc_hdr_clear_flags(hdr, ARC_FLAG_IO_IN_PROGRESS);
		/* if it's not anon, we are doing a scrub */
		if (exists == NULL && hdr->b_l1hdr.b_state == arc_anon)
			arc_access(hdr, hash_lock);
		mutex_exit(hash_lock);
	} else {
		arc_hdr_clear_flags(hdr, ARC_FLAG_IO_IN_PROGRESS);
	}

	ASSERT(!zfs_refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
	callback->awcb_done(zio, buf, callback->awcb_private);

	abd_put(zio->io_abd);
	kmem_free(callback, sizeof (arc_write_callback_t));
}

zio_t *
arc_write(zio_t *pio, spa_t *spa, uint64_t txg,
    blkptr_t *bp, arc_buf_t *buf, boolean_t l2arc,
    const zio_prop_t *zp, arc_write_done_func_t *ready,
    arc_write_done_func_t *children_ready, arc_write_done_func_t *physdone,
    arc_write_done_func_t *done, void *private, zio_priority_t priority,
    int zio_flags, const zbookmark_phys_t *zb)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;
	arc_write_callback_t *callback;
	zio_t *zio;
	zio_prop_t localprop = *zp;

	ASSERT3P(ready, !=, NULL);
	ASSERT3P(done, !=, NULL);
	ASSERT(!HDR_IO_ERROR(hdr));
	ASSERT(!HDR_IO_IN_PROGRESS(hdr));
	ASSERT3P(hdr->b_l1hdr.b_acb, ==, NULL);
	ASSERT3U(hdr->b_l1hdr.b_bufcnt, >, 0);
	if (l2arc)
		arc_hdr_set_flags(hdr, ARC_FLAG_L2CACHE);

	if (ARC_BUF_ENCRYPTED(buf)) {
		ASSERT(ARC_BUF_COMPRESSED(buf));
		localprop.zp_encrypt = B_TRUE;
		localprop.zp_compress = HDR_GET_COMPRESS(hdr);
		localprop.zp_complevel = hdr->b_complevel;
		localprop.zp_byteorder =
		    (hdr->b_l1hdr.b_byteswap == DMU_BSWAP_NUMFUNCS) ?
		    ZFS_HOST_BYTEORDER : !ZFS_HOST_BYTEORDER;
		bcopy(hdr->b_crypt_hdr.b_salt, localprop.zp_salt,
		    ZIO_DATA_SALT_LEN);
		bcopy(hdr->b_crypt_hdr.b_iv, localprop.zp_iv,
		    ZIO_DATA_IV_LEN);
		bcopy(hdr->b_crypt_hdr.b_mac, localprop.zp_mac,
		    ZIO_DATA_MAC_LEN);
		if (DMU_OT_IS_ENCRYPTED(localprop.zp_type)) {
			localprop.zp_nopwrite = B_FALSE;
			localprop.zp_copies =
			    MIN(localprop.zp_copies, SPA_DVAS_PER_BP - 1);
		}
		zio_flags |= ZIO_FLAG_RAW;
	} else if (ARC_BUF_COMPRESSED(buf)) {
		ASSERT3U(HDR_GET_LSIZE(hdr), !=, arc_buf_size(buf));
		localprop.zp_compress = HDR_GET_COMPRESS(hdr);
		localprop.zp_complevel = hdr->b_complevel;
		zio_flags |= ZIO_FLAG_RAW_COMPRESS;
	}
	callback = kmem_zalloc(sizeof (arc_write_callback_t), KM_SLEEP);
	callback->awcb_ready = ready;
	callback->awcb_children_ready = children_ready;
	callback->awcb_physdone = physdone;
	callback->awcb_done = done;
	callback->awcb_private = private;
	callback->awcb_buf = buf;

	/*
	 * The hdr's b_pabd is now stale, free it now. A new data block
	 * will be allocated when the zio pipeline calls arc_write_ready().
	 */
	if (hdr->b_l1hdr.b_pabd != NULL) {
		/*
		 * If the buf is currently sharing the data block with
		 * the hdr then we need to break that relationship here.
		 * The hdr will remain with a NULL data pointer and the
		 * buf will take sole ownership of the block.
		 */
		if (arc_buf_is_shared(buf)) {
			arc_unshare_buf(hdr, buf);
		} else {
			arc_hdr_free_abd(hdr, B_FALSE);
		}
		VERIFY3P(buf->b_data, !=, NULL);
	}

	if (HDR_HAS_RABD(hdr))
		arc_hdr_free_abd(hdr, B_TRUE);

	if (!(zio_flags & ZIO_FLAG_RAW))
		arc_hdr_set_compress(hdr, ZIO_COMPRESS_OFF);

	ASSERT(!arc_buf_is_shared(buf));
	ASSERT3P(hdr->b_l1hdr.b_pabd, ==, NULL);

	zio = zio_write(pio, spa, txg, bp,
	    abd_get_from_buf(buf->b_data, HDR_GET_LSIZE(hdr)),
	    HDR_GET_LSIZE(hdr), arc_buf_size(buf), &localprop, arc_write_ready,
	    (children_ready != NULL) ? arc_write_children_ready : NULL,
	    arc_write_physdone, arc_write_done, callback,
	    priority, zio_flags, zb);

	return (zio);
}

void
arc_tempreserve_clear(uint64_t reserve)
{
	atomic_add_64(&arc_tempreserve, -reserve);
	ASSERT((int64_t)arc_tempreserve >= 0);
}

int
arc_tempreserve_space(spa_t *spa, uint64_t reserve, uint64_t txg)
{
	int error;
	uint64_t anon_size;

	if (!arc_no_grow &&
	    reserve > arc_c/4 &&
	    reserve * 4 > (2ULL << SPA_MAXBLOCKSHIFT))
		arc_c = MIN(arc_c_max, reserve * 4);

	/*
	 * Throttle when the calculated memory footprint for the TXG
	 * exceeds the target ARC size.
	 */
	if (reserve > arc_c) {
		DMU_TX_STAT_BUMP(dmu_tx_memory_reserve);
		return (SET_ERROR(ERESTART));
	}

	/*
	 * Don't count loaned bufs as in flight dirty data to prevent long
	 * network delays from blocking transactions that are ready to be
	 * assigned to a txg.
	 */

	/* assert that it has not wrapped around */
	ASSERT3S(atomic_add_64_nv(&arc_loaned_bytes, 0), >=, 0);

	anon_size = MAX((int64_t)(zfs_refcount_count(&arc_anon->arcs_size) -
	    arc_loaned_bytes), 0);

	/*
	 * Writes will, almost always, require additional memory allocations
	 * in order to compress/encrypt/etc the data.  We therefore need to
	 * make sure that there is sufficient available memory for this.
	 */
	error = arc_memory_throttle(spa, reserve, txg);
	if (error != 0)
		return (error);

	/*
	 * Throttle writes when the amount of dirty data in the cache
	 * gets too large.  We try to keep the cache less than half full
	 * of dirty blocks so that our sync times don't grow too large.
	 *
	 * In the case of one pool being built on another pool, we want
	 * to make sure we don't end up throttling the lower (backing)
	 * pool when the upper pool is the majority contributor to dirty
	 * data. To insure we make forward progress during throttling, we
	 * also check the current pool's net dirty data and only throttle
	 * if it exceeds zfs_arc_pool_dirty_percent of the anonymous dirty
	 * data in the cache.
	 *
	 * Note: if two requests come in concurrently, we might let them
	 * both succeed, when one of them should fail.  Not a huge deal.
	 */
	uint64_t total_dirty = reserve + arc_tempreserve + anon_size;
	uint64_t spa_dirty_anon = spa_dirty_data(spa);
	uint64_t rarc_c = arc_warm ? arc_c : arc_c_max;
	if (total_dirty > rarc_c * zfs_arc_dirty_limit_percent / 100 &&
	    anon_size > rarc_c * zfs_arc_anon_limit_percent / 100 &&
	    spa_dirty_anon > anon_size * zfs_arc_pool_dirty_percent / 100) {
#ifdef ZFS_DEBUG
		uint64_t meta_esize = zfs_refcount_count(
		    &arc_anon->arcs_esize[ARC_BUFC_METADATA]);
		uint64_t data_esize =
		    zfs_refcount_count(&arc_anon->arcs_esize[ARC_BUFC_DATA]);
		dprintf("failing, arc_tempreserve=%lluK anon_meta=%lluK "
		    "anon_data=%lluK tempreserve=%lluK rarc_c=%lluK\n",
		    arc_tempreserve >> 10, meta_esize >> 10,
		    data_esize >> 10, reserve >> 10, rarc_c >> 10);
#endif
		DMU_TX_STAT_BUMP(dmu_tx_dirty_throttle);
		return (SET_ERROR(ERESTART));
	}
	atomic_add_64(&arc_tempreserve, reserve);
	return (0);
}

static void
arc_kstat_update_state(arc_state_t *state, kstat_named_t *size,
    kstat_named_t *evict_data, kstat_named_t *evict_metadata)
{
	size->value.ui64 = zfs_refcount_count(&state->arcs_size);
	evict_data->value.ui64 =
	    zfs_refcount_count(&state->arcs_esize[ARC_BUFC_DATA]);
	evict_metadata->value.ui64 =
	    zfs_refcount_count(&state->arcs_esize[ARC_BUFC_METADATA]);
}

static int
arc_kstat_update(kstat_t *ksp, int rw)
{
	arc_stats_t *as = ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		return (SET_ERROR(EACCES));
	} else {
		arc_kstat_update_state(arc_anon,
		    &as->arcstat_anon_size,
		    &as->arcstat_anon_evictable_data,
		    &as->arcstat_anon_evictable_metadata);
		arc_kstat_update_state(arc_mru,
		    &as->arcstat_mru_size,
		    &as->arcstat_mru_evictable_data,
		    &as->arcstat_mru_evictable_metadata);
		arc_kstat_update_state(arc_mru_ghost,
		    &as->arcstat_mru_ghost_size,
		    &as->arcstat_mru_ghost_evictable_data,
		    &as->arcstat_mru_ghost_evictable_metadata);
		arc_kstat_update_state(arc_mfu,
		    &as->arcstat_mfu_size,
		    &as->arcstat_mfu_evictable_data,
		    &as->arcstat_mfu_evictable_metadata);
		arc_kstat_update_state(arc_mfu_ghost,
		    &as->arcstat_mfu_ghost_size,
		    &as->arcstat_mfu_ghost_evictable_data,
		    &as->arcstat_mfu_ghost_evictable_metadata);

		ARCSTAT(arcstat_size) = aggsum_value(&arc_size);
		ARCSTAT(arcstat_meta_used) = aggsum_value(&arc_meta_used);
		ARCSTAT(arcstat_data_size) = aggsum_value(&astat_data_size);
		ARCSTAT(arcstat_metadata_size) =
		    aggsum_value(&astat_metadata_size);
		ARCSTAT(arcstat_hdr_size) = aggsum_value(&astat_hdr_size);
		ARCSTAT(arcstat_l2_hdr_size) = aggsum_value(&astat_l2_hdr_size);
		ARCSTAT(arcstat_dbuf_size) = aggsum_value(&astat_dbuf_size);
#if defined(COMPAT_FREEBSD11)
		ARCSTAT(arcstat_other_size) = aggsum_value(&astat_bonus_size) +
		    aggsum_value(&astat_dnode_size) +
		    aggsum_value(&astat_dbuf_size);
#endif
		ARCSTAT(arcstat_dnode_size) = aggsum_value(&astat_dnode_size);
		ARCSTAT(arcstat_bonus_size) = aggsum_value(&astat_bonus_size);
		ARCSTAT(arcstat_abd_chunk_waste_size) =
		    aggsum_value(&astat_abd_chunk_waste_size);

		as->arcstat_memory_all_bytes.value.ui64 =
		    arc_all_memory();
		as->arcstat_memory_free_bytes.value.ui64 =
		    arc_free_memory();
		as->arcstat_memory_available_bytes.value.i64 =
		    arc_available_memory();
	}

	return (0);
}

/*
 * This function *must* return indices evenly distributed between all
 * sublists of the multilist. This is needed due to how the ARC eviction
 * code is laid out; arc_evict_state() assumes ARC buffers are evenly
 * distributed between all sublists and uses this assumption when
 * deciding which sublist to evict from and how much to evict from it.
 */
static unsigned int
arc_state_multilist_index_func(multilist_t *ml, void *obj)
{
	arc_buf_hdr_t *hdr = obj;

	/*
	 * We rely on b_dva to generate evenly distributed index
	 * numbers using buf_hash below. So, as an added precaution,
	 * let's make sure we never add empty buffers to the arc lists.
	 */
	ASSERT(!HDR_EMPTY(hdr));

	/*
	 * The assumption here, is the hash value for a given
	 * arc_buf_hdr_t will remain constant throughout its lifetime
	 * (i.e. its b_spa, b_dva, and b_birth fields don't change).
	 * Thus, we don't need to store the header's sublist index
	 * on insertion, as this index can be recalculated on removal.
	 *
	 * Also, the low order bits of the hash value are thought to be
	 * distributed evenly. Otherwise, in the case that the multilist
	 * has a power of two number of sublists, each sublists' usage
	 * would not be evenly distributed.
	 */
	return (buf_hash(hdr->b_spa, &hdr->b_dva, hdr->b_birth) %
	    multilist_get_num_sublists(ml));
}

#define	WARN_IF_TUNING_IGNORED(tuning, value, do_warn) do {	\
	if ((do_warn) && (tuning) && ((tuning) != (value))) {	\
		cmn_err(CE_WARN,				\
		    "ignoring tunable %s (using %llu instead)",	\
		    (#tuning), (value));			\
	}							\
} while (0)

/*
 * Called during module initialization and periodically thereafter to
 * apply reasonable changes to the exposed performance tunings.  Can also be
 * called explicitly by param_set_arc_*() functions when ARC tunables are
 * updated manually.  Non-zero zfs_* values which differ from the currently set
 * values will be applied.
 */
void
arc_tuning_update(boolean_t verbose)
{
	uint64_t allmem = arc_all_memory();
	unsigned long limit;

	/* Valid range: 32M - <arc_c_max> */
	if ((zfs_arc_min) && (zfs_arc_min != arc_c_min) &&
	    (zfs_arc_min >= 2ULL << SPA_MAXBLOCKSHIFT) &&
	    (zfs_arc_min <= arc_c_max)) {
		arc_c_min = zfs_arc_min;
		arc_c = MAX(arc_c, arc_c_min);
	}
	WARN_IF_TUNING_IGNORED(zfs_arc_min, arc_c_min, verbose);

	/* Valid range: 64M - <all physical memory> */
	if ((zfs_arc_max) && (zfs_arc_max != arc_c_max) &&
	    (zfs_arc_max >= 64 << 20) && (zfs_arc_max < allmem) &&
	    (zfs_arc_max > arc_c_min)) {
		arc_c_max = zfs_arc_max;
		arc_c = MIN(arc_c, arc_c_max);
		arc_p = (arc_c >> 1);
		if (arc_meta_limit > arc_c_max)
			arc_meta_limit = arc_c_max;
		if (arc_dnode_size_limit > arc_meta_limit)
			arc_dnode_size_limit = arc_meta_limit;
	}
	WARN_IF_TUNING_IGNORED(zfs_arc_max, arc_c_max, verbose);

	/* Valid range: 16M - <arc_c_max> */
	if ((zfs_arc_meta_min) && (zfs_arc_meta_min != arc_meta_min) &&
	    (zfs_arc_meta_min >= 1ULL << SPA_MAXBLOCKSHIFT) &&
	    (zfs_arc_meta_min <= arc_c_max)) {
		arc_meta_min = zfs_arc_meta_min;
		if (arc_meta_limit < arc_meta_min)
			arc_meta_limit = arc_meta_min;
		if (arc_dnode_size_limit < arc_meta_min)
			arc_dnode_size_limit = arc_meta_min;
	}
	WARN_IF_TUNING_IGNORED(zfs_arc_meta_min, arc_meta_min, verbose);

	/* Valid range: <arc_meta_min> - <arc_c_max> */
	limit = zfs_arc_meta_limit ? zfs_arc_meta_limit :
	    MIN(zfs_arc_meta_limit_percent, 100) * arc_c_max / 100;
	if ((limit != arc_meta_limit) &&
	    (limit >= arc_meta_min) &&
	    (limit <= arc_c_max))
		arc_meta_limit = limit;
	WARN_IF_TUNING_IGNORED(zfs_arc_meta_limit, arc_meta_limit, verbose);

	/* Valid range: <arc_meta_min> - <arc_meta_limit> */
	limit = zfs_arc_dnode_limit ? zfs_arc_dnode_limit :
	    MIN(zfs_arc_dnode_limit_percent, 100) * arc_meta_limit / 100;
	if ((limit != arc_dnode_size_limit) &&
	    (limit >= arc_meta_min) &&
	    (limit <= arc_meta_limit))
		arc_dnode_size_limit = limit;
	WARN_IF_TUNING_IGNORED(zfs_arc_dnode_limit, arc_dnode_size_limit,
	    verbose);

	/* Valid range: 1 - N */
	if (zfs_arc_grow_retry)
		arc_grow_retry = zfs_arc_grow_retry;

	/* Valid range: 1 - N */
	if (zfs_arc_shrink_shift) {
		arc_shrink_shift = zfs_arc_shrink_shift;
		arc_no_grow_shift = MIN(arc_no_grow_shift, arc_shrink_shift -1);
	}

	/* Valid range: 1 - N */
	if (zfs_arc_p_min_shift)
		arc_p_min_shift = zfs_arc_p_min_shift;

	/* Valid range: 1 - N ms */
	if (zfs_arc_min_prefetch_ms)
		arc_min_prefetch_ms = zfs_arc_min_prefetch_ms;

	/* Valid range: 1 - N ms */
	if (zfs_arc_min_prescient_prefetch_ms) {
		arc_min_prescient_prefetch_ms =
		    zfs_arc_min_prescient_prefetch_ms;
	}

	/* Valid range: 0 - 100 */
	if ((zfs_arc_lotsfree_percent >= 0) &&
	    (zfs_arc_lotsfree_percent <= 100))
		arc_lotsfree_percent = zfs_arc_lotsfree_percent;
	WARN_IF_TUNING_IGNORED(zfs_arc_lotsfree_percent, arc_lotsfree_percent,
	    verbose);

	/* Valid range: 0 - <all physical memory> */
	if ((zfs_arc_sys_free) && (zfs_arc_sys_free != arc_sys_free))
		arc_sys_free = MIN(MAX(zfs_arc_sys_free, 0), allmem);
	WARN_IF_TUNING_IGNORED(zfs_arc_sys_free, arc_sys_free, verbose);
}

static void
arc_state_init(void)
{
	arc_anon = &ARC_anon;
	arc_mru = &ARC_mru;
	arc_mru_ghost = &ARC_mru_ghost;
	arc_mfu = &ARC_mfu;
	arc_mfu_ghost = &ARC_mfu_ghost;
	arc_l2c_only = &ARC_l2c_only;

	arc_mru->arcs_list[ARC_BUFC_METADATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mru->arcs_list[ARC_BUFC_DATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mru_ghost->arcs_list[ARC_BUFC_METADATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mru_ghost->arcs_list[ARC_BUFC_DATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mfu->arcs_list[ARC_BUFC_METADATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mfu->arcs_list[ARC_BUFC_DATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mfu_ghost->arcs_list[ARC_BUFC_METADATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_mfu_ghost->arcs_list[ARC_BUFC_DATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_l2c_only->arcs_list[ARC_BUFC_METADATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);
	arc_l2c_only->arcs_list[ARC_BUFC_DATA] =
	    multilist_create(sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    arc_state_multilist_index_func);

	zfs_refcount_create(&arc_anon->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_create(&arc_anon->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_create(&arc_mru->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_create(&arc_mru->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_create(&arc_mru_ghost->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_create(&arc_mru_ghost->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_create(&arc_mfu->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_create(&arc_mfu->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_create(&arc_mfu_ghost->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_create(&arc_mfu_ghost->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_create(&arc_l2c_only->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_create(&arc_l2c_only->arcs_esize[ARC_BUFC_DATA]);

	zfs_refcount_create(&arc_anon->arcs_size);
	zfs_refcount_create(&arc_mru->arcs_size);
	zfs_refcount_create(&arc_mru_ghost->arcs_size);
	zfs_refcount_create(&arc_mfu->arcs_size);
	zfs_refcount_create(&arc_mfu_ghost->arcs_size);
	zfs_refcount_create(&arc_l2c_only->arcs_size);

	aggsum_init(&arc_meta_used, 0);
	aggsum_init(&arc_size, 0);
	aggsum_init(&astat_data_size, 0);
	aggsum_init(&astat_metadata_size, 0);
	aggsum_init(&astat_hdr_size, 0);
	aggsum_init(&astat_l2_hdr_size, 0);
	aggsum_init(&astat_bonus_size, 0);
	aggsum_init(&astat_dnode_size, 0);
	aggsum_init(&astat_dbuf_size, 0);
	aggsum_init(&astat_abd_chunk_waste_size, 0);

	arc_anon->arcs_state = ARC_STATE_ANON;
	arc_mru->arcs_state = ARC_STATE_MRU;
	arc_mru_ghost->arcs_state = ARC_STATE_MRU_GHOST;
	arc_mfu->arcs_state = ARC_STATE_MFU;
	arc_mfu_ghost->arcs_state = ARC_STATE_MFU_GHOST;
	arc_l2c_only->arcs_state = ARC_STATE_L2C_ONLY;
}

static void
arc_state_fini(void)
{
	zfs_refcount_destroy(&arc_anon->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_destroy(&arc_anon->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_destroy(&arc_mru->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_destroy(&arc_mru->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_destroy(&arc_mru_ghost->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_destroy(&arc_mru_ghost->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_destroy(&arc_mfu->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_destroy(&arc_mfu->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_destroy(&arc_mfu_ghost->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_destroy(&arc_mfu_ghost->arcs_esize[ARC_BUFC_DATA]);
	zfs_refcount_destroy(&arc_l2c_only->arcs_esize[ARC_BUFC_METADATA]);
	zfs_refcount_destroy(&arc_l2c_only->arcs_esize[ARC_BUFC_DATA]);

	zfs_refcount_destroy(&arc_anon->arcs_size);
	zfs_refcount_destroy(&arc_mru->arcs_size);
	zfs_refcount_destroy(&arc_mru_ghost->arcs_size);
	zfs_refcount_destroy(&arc_mfu->arcs_size);
	zfs_refcount_destroy(&arc_mfu_ghost->arcs_size);
	zfs_refcount_destroy(&arc_l2c_only->arcs_size);

	multilist_destroy(arc_mru->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(arc_mru_ghost->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(arc_mfu->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(arc_mfu_ghost->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(arc_mru->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(arc_mru_ghost->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(arc_mfu->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(arc_mfu_ghost->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(arc_l2c_only->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(arc_l2c_only->arcs_list[ARC_BUFC_DATA]);

	aggsum_fini(&arc_meta_used);
	aggsum_fini(&arc_size);
	aggsum_fini(&astat_data_size);
	aggsum_fini(&astat_metadata_size);
	aggsum_fini(&astat_hdr_size);
	aggsum_fini(&astat_l2_hdr_size);
	aggsum_fini(&astat_bonus_size);
	aggsum_fini(&astat_dnode_size);
	aggsum_fini(&astat_dbuf_size);
	aggsum_fini(&astat_abd_chunk_waste_size);
}

uint64_t
arc_target_bytes(void)
{
	return (arc_c);
}

void
arc_set_limits(uint64_t allmem)
{
	/* Set min cache to 1/32 of all memory, or 32MB, whichever is more. */
	arc_c_min = MAX(allmem / 32, 2ULL << SPA_MAXBLOCKSHIFT);

	/* How to set default max varies by platform. */
	arc_c_max = arc_default_max(arc_c_min, allmem);
}
void
arc_init(void)
{
	uint64_t percent, allmem = arc_all_memory();
	mutex_init(&arc_evict_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&arc_evict_waiters, sizeof (arc_evict_waiter_t),
	    offsetof(arc_evict_waiter_t, aew_node));

	arc_min_prefetch_ms = 1000;
	arc_min_prescient_prefetch_ms = 6000;

#if defined(_KERNEL)
	arc_lowmem_init();
#endif

	arc_set_limits(allmem);

#ifndef _KERNEL
	/*
	 * In userland, there's only the memory pressure that we artificially
	 * create (see arc_available_memory()).  Don't let arc_c get too
	 * small, because it can cause transactions to be larger than
	 * arc_c, causing arc_tempreserve_space() to fail.
	 */
	arc_c_min = MAX(arc_c_max / 2, 2ULL << SPA_MAXBLOCKSHIFT);
#endif

	arc_c = arc_c_min;
	arc_p = (arc_c >> 1);

	/* Set min to 1/2 of arc_c_min */
	arc_meta_min = 1ULL << SPA_MAXBLOCKSHIFT;
	/* Initialize maximum observed usage to zero */
	arc_meta_max = 0;
	/*
	 * Set arc_meta_limit to a percent of arc_c_max with a floor of
	 * arc_meta_min, and a ceiling of arc_c_max.
	 */
	percent = MIN(zfs_arc_meta_limit_percent, 100);
	arc_meta_limit = MAX(arc_meta_min, (percent * arc_c_max) / 100);
	percent = MIN(zfs_arc_dnode_limit_percent, 100);
	arc_dnode_size_limit = (percent * arc_meta_limit) / 100;

	/* Apply user specified tunings */
	arc_tuning_update(B_TRUE);

	/* if kmem_flags are set, lets try to use less memory */
	if (kmem_debugging())
		arc_c = arc_c / 2;
	if (arc_c < arc_c_min)
		arc_c = arc_c_min;

	arc_register_hotplug();

	arc_state_init();

	buf_init();

	list_create(&arc_prune_list, sizeof (arc_prune_t),
	    offsetof(arc_prune_t, p_node));
	mutex_init(&arc_prune_mtx, NULL, MUTEX_DEFAULT, NULL);

	arc_prune_taskq = taskq_create("arc_prune", 100, defclsyspri,
	    boot_ncpus, INT_MAX, TASKQ_PREPOPULATE | TASKQ_DYNAMIC |
	    TASKQ_THREADS_CPU_PCT);

	arc_ksp = kstat_create("zfs", 0, "arcstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (arc_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);

	if (arc_ksp != NULL) {
		arc_ksp->ks_data = &arc_stats;
		arc_ksp->ks_update = arc_kstat_update;
		kstat_install(arc_ksp);
	}

	arc_evict_zthr = zthr_create("arc_evict",
	    arc_evict_cb_check, arc_evict_cb, NULL);
	arc_reap_zthr = zthr_create_timer("arc_reap",
	    arc_reap_cb_check, arc_reap_cb, NULL, SEC2NSEC(1));

	arc_warm = B_FALSE;

	/*
	 * Calculate maximum amount of dirty data per pool.
	 *
	 * If it has been set by a module parameter, take that.
	 * Otherwise, use a percentage of physical memory defined by
	 * zfs_dirty_data_max_percent (default 10%) with a cap at
	 * zfs_dirty_data_max_max (default 4G or 25% of physical memory).
	 */
#ifdef __LP64__
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = MIN(4ULL * 1024 * 1024 * 1024,
		    allmem * zfs_dirty_data_max_max_percent / 100);
#else
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = MIN(1ULL * 1024 * 1024 * 1024,
		    allmem * zfs_dirty_data_max_max_percent / 100);
#endif

	if (zfs_dirty_data_max == 0) {
		zfs_dirty_data_max = allmem *
		    zfs_dirty_data_max_percent / 100;
		zfs_dirty_data_max = MIN(zfs_dirty_data_max,
		    zfs_dirty_data_max_max);
	}
}

void
arc_fini(void)
{
	arc_prune_t *p;

#ifdef _KERNEL
	arc_lowmem_fini();
#endif /* _KERNEL */

	/* Use B_TRUE to ensure *all* buffers are evicted */
	arc_flush(NULL, B_TRUE);

	if (arc_ksp != NULL) {
		kstat_delete(arc_ksp);
		arc_ksp = NULL;
	}

	taskq_wait(arc_prune_taskq);
	taskq_destroy(arc_prune_taskq);

	mutex_enter(&arc_prune_mtx);
	while ((p = list_head(&arc_prune_list)) != NULL) {
		list_remove(&arc_prune_list, p);
		zfs_refcount_remove(&p->p_refcnt, &arc_prune_list);
		zfs_refcount_destroy(&p->p_refcnt);
		kmem_free(p, sizeof (*p));
	}
	mutex_exit(&arc_prune_mtx);

	list_destroy(&arc_prune_list);
	mutex_destroy(&arc_prune_mtx);

	(void) zthr_cancel(arc_evict_zthr);
	(void) zthr_cancel(arc_reap_zthr);

	mutex_destroy(&arc_evict_lock);
	list_destroy(&arc_evict_waiters);

	/*
	 * Free any buffers that were tagged for destruction.  This needs
	 * to occur before arc_state_fini() runs and destroys the aggsum
	 * values which are updated when freeing scatter ABDs.
	 */
	l2arc_do_free_on_write();

	/*
	 * buf_fini() must proceed arc_state_fini() because buf_fin() may
	 * trigger the release of kmem magazines, which can callback to
	 * arc_space_return() which accesses aggsums freed in act_state_fini().
	 */
	buf_fini();
	arc_state_fini();

	arc_unregister_hotplug();

	/*
	 * We destroy the zthrs after all the ARC state has been
	 * torn down to avoid the case of them receiving any
	 * wakeup() signals after they are destroyed.
	 */
	zthr_destroy(arc_evict_zthr);
	zthr_destroy(arc_reap_zthr);

	ASSERT0(arc_loaned_bytes);
}

/*
 * Level 2 ARC
 *
 * The level 2 ARC (L2ARC) is a cache layer in-between main memory and disk.
 * It uses dedicated storage devices to hold cached data, which are populated
 * using large infrequent writes.  The main role of this cache is to boost
 * the performance of random read workloads.  The intended L2ARC devices
 * include short-stroked disks, solid state disks, and other media with
 * substantially faster read latency than disk.
 *
 *                 +-----------------------+
 *                 |         ARC           |
 *                 +-----------------------+
 *                    |         ^     ^
 *                    |         |     |
 *      l2arc_feed_thread()    arc_read()
 *                    |         |     |
 *                    |  l2arc read   |
 *                    V         |     |
 *               +---------------+    |
 *               |     L2ARC     |    |
 *               +---------------+    |
 *                   |    ^           |
 *          l2arc_write() |           |
 *                   |    |           |
 *                   V    |           |
 *                 +-------+      +-------+
 *                 | vdev  |      | vdev  |
 *                 | cache |      | cache |
 *                 +-------+      +-------+
 *                 +=========+     .-----.
 *                 :  L2ARC  :    |-_____-|
 *                 : devices :    | Disks |
 *                 +=========+    `-_____-'
 *
 * Read requests are satisfied from the following sources, in order:
 *
 *	1) ARC
 *	2) vdev cache of L2ARC devices
 *	3) L2ARC devices
 *	4) vdev cache of disks
 *	5) disks
 *
 * Some L2ARC device types exhibit extremely slow write performance.
 * To accommodate for this there are some significant differences between
 * the L2ARC and traditional cache design:
 *
 * 1. There is no eviction path from the ARC to the L2ARC.  Evictions from
 * the ARC behave as usual, freeing buffers and placing headers on ghost
 * lists.  The ARC does not send buffers to the L2ARC during eviction as
 * this would add inflated write latencies for all ARC memory pressure.
 *
 * 2. The L2ARC attempts to cache data from the ARC before it is evicted.
 * It does this by periodically scanning buffers from the eviction-end of
 * the MFU and MRU ARC lists, copying them to the L2ARC devices if they are
 * not already there. It scans until a headroom of buffers is satisfied,
 * which itself is a buffer for ARC eviction. If a compressible buffer is
 * found during scanning and selected for writing to an L2ARC device, we
 * temporarily boost scanning headroom during the next scan cycle to make
 * sure we adapt to compression effects (which might significantly reduce
 * the data volume we write to L2ARC). The thread that does this is
 * l2arc_feed_thread(), illustrated below; example sizes are included to
 * provide a better sense of ratio than this diagram:
 *
 *	       head -->                        tail
 *	        +---------------------+----------+
 *	ARC_mfu |:::::#:::::::::::::::|o#o###o###|-->.   # already on L2ARC
 *	        +---------------------+----------+   |   o L2ARC eligible
 *	ARC_mru |:#:::::::::::::::::::|#o#ooo####|-->|   : ARC buffer
 *	        +---------------------+----------+   |
 *	             15.9 Gbytes      ^ 32 Mbytes    |
 *	                           headroom          |
 *	                                      l2arc_feed_thread()
 *	                                             |
 *	                 l2arc write hand <--[oooo]--'
 *	                         |           8 Mbyte
 *	                         |          write max
 *	                         V
 *		  +==============================+
 *	L2ARC dev |####|#|###|###|    |####| ... |
 *	          +==============================+
 *	                     32 Gbytes
 *
 * 3. If an ARC buffer is copied to the L2ARC but then hit instead of
 * evicted, then the L2ARC has cached a buffer much sooner than it probably
 * needed to, potentially wasting L2ARC device bandwidth and storage.  It is
 * safe to say that this is an uncommon case, since buffers at the end of
 * the ARC lists have moved there due to inactivity.
 *
 * 4. If the ARC evicts faster than the L2ARC can maintain a headroom,
 * then the L2ARC simply misses copying some buffers.  This serves as a
 * pressure valve to prevent heavy read workloads from both stalling the ARC
 * with waits and clogging the L2ARC with writes.  This also helps prevent
 * the potential for the L2ARC to churn if it attempts to cache content too
 * quickly, such as during backups of the entire pool.
 *
 * 5. After system boot and before the ARC has filled main memory, there are
 * no evictions from the ARC and so the tails of the ARC_mfu and ARC_mru
 * lists can remain mostly static.  Instead of searching from tail of these
 * lists as pictured, the l2arc_feed_thread() will search from the list heads
 * for eligible buffers, greatly increasing its chance of finding them.
 *
 * The L2ARC device write speed is also boosted during this time so that
 * the L2ARC warms up faster.  Since there have been no ARC evictions yet,
 * there are no L2ARC reads, and no fear of degrading read performance
 * through increased writes.
 *
 * 6. Writes to the L2ARC devices are grouped and sent in-sequence, so that
 * the vdev queue can aggregate them into larger and fewer writes.  Each
 * device is written to in a rotor fashion, sweeping writes through
 * available space then repeating.
 *
 * 7. The L2ARC does not store dirty content.  It never needs to flush
 * write buffers back to disk based storage.
 *
 * 8. If an ARC buffer is written (and dirtied) which also exists in the
 * L2ARC, the now stale L2ARC buffer is immediately dropped.
 *
 * The performance of the L2ARC can be tweaked by a number of tunables, which
 * may be necessary for different workloads:
 *
 *	l2arc_write_max		max write bytes per interval
 *	l2arc_write_boost	extra write bytes during device warmup
 *	l2arc_noprefetch	skip caching prefetched buffers
 *	l2arc_headroom		number of max device writes to precache
 *	l2arc_headroom_boost	when we find compressed buffers during ARC
 *				scanning, we multiply headroom by this
 *				percentage factor for the next scan cycle,
 *				since more compressed buffers are likely to
 *				be present
 *	l2arc_feed_secs		seconds between L2ARC writing
 *
 * Tunables may be removed or added as future performance improvements are
 * integrated, and also may become zpool properties.
 *
 * There are three key functions that control how the L2ARC warms up:
 *
 *	l2arc_write_eligible()	check if a buffer is eligible to cache
 *	l2arc_write_size()	calculate how much to write
 *	l2arc_write_interval()	calculate sleep delay between writes
 *
 * These three functions determine what to write, how much, and how quickly
 * to send writes.
 *
 * L2ARC persistence:
 *
 * When writing buffers to L2ARC, we periodically add some metadata to
 * make sure we can pick them up after reboot, thus dramatically reducing
 * the impact that any downtime has on the performance of storage systems
 * with large caches.
 *
 * The implementation works fairly simply by integrating the following two
 * modifications:
 *
 * *) When writing to the L2ARC, we occasionally write a "l2arc log block",
 *    which is an additional piece of metadata which describes what's been
 *    written. This allows us to rebuild the arc_buf_hdr_t structures of the
 *    main ARC buffers. There are 2 linked-lists of log blocks headed by
 *    dh_start_lbps[2]. We alternate which chain we append to, so they are
 *    time-wise and offset-wise interleaved, but that is an optimization rather
 *    than for correctness. The log block also includes a pointer to the
 *    previous block in its chain.
 *
 * *) We reserve SPA_MINBLOCKSIZE of space at the start of each L2ARC device
 *    for our header bookkeeping purposes. This contains a device header,
 *    which contains our top-level reference structures. We update it each
 *    time we write a new log block, so that we're able to locate it in the
 *    L2ARC device. If this write results in an inconsistent device header
 *    (e.g. due to power failure), we detect this by verifying the header's
 *    checksum and simply fail to reconstruct the L2ARC after reboot.
 *
 * Implementation diagram:
 *
 * +=== L2ARC device (not to scale) ======================================+
 * |       ___two newest log block pointers__.__________                  |
 * |      /                                   \dh_start_lbps[1]           |
 * |	 /				       \         \dh_start_lbps[0]|
 * |.___/__.                                    V         V               |
 * ||L2 dev|....|lb |bufs |lb |bufs |lb |bufs |lb |bufs |lb |---(empty)---|
 * ||   hdr|      ^         /^       /^        /         /                |
 * |+------+  ...--\-------/  \-----/--\------/         /                 |
 * |                \--------------/    \--------------/                  |
 * +======================================================================+
 *
 * As can be seen on the diagram, rather than using a simple linked list,
 * we use a pair of linked lists with alternating elements. This is a
 * performance enhancement due to the fact that we only find out the
 * address of the next log block access once the current block has been
 * completely read in. Obviously, this hurts performance, because we'd be
 * keeping the device's I/O queue at only a 1 operation deep, thus
 * incurring a large amount of I/O round-trip latency. Having two lists
 * allows us to fetch two log blocks ahead of where we are currently
 * rebuilding L2ARC buffers.
 *
 * On-device data structures:
 *
 * L2ARC device header:	l2arc_dev_hdr_phys_t
 * L2ARC log block:	l2arc_log_blk_phys_t
 *
 * L2ARC reconstruction:
 *
 * When writing data, we simply write in the standard rotary fashion,
 * evicting buffers as we go and simply writing new data over them (writing
 * a new log block every now and then). This obviously means that once we
 * loop around the end of the device, we will start cutting into an already
 * committed log block (and its referenced data buffers), like so:
 *
 *    current write head__       __old tail
 *                        \     /
 *                        V    V
 * <--|bufs |lb |bufs |lb |    |bufs |lb |bufs |lb |-->
 *                         ^    ^^^^^^^^^___________________________________
 *                         |                                                \
 *                   <<nextwrite>> may overwrite this blk and/or its bufs --'
 *
 * When importing the pool, we detect this situation and use it to stop
 * our scanning process (see l2arc_rebuild).
 *
 * There is one significant caveat to consider when rebuilding ARC contents
 * from an L2ARC device: what about invalidated buffers? Given the above
 * construction, we cannot update blocks which we've already written to amend
 * them to remove buffers which were invalidated. Thus, during reconstruction,
 * we might be populating the cache with buffers for data that's not on the
 * main pool anymore, or may have been overwritten!
 *
 * As it turns out, this isn't a problem. Every arc_read request includes
 * both the DVA and, crucially, the birth TXG of the BP the caller is
 * looking for. So even if the cache were populated by completely rotten
 * blocks for data that had been long deleted and/or overwritten, we'll
 * never actually return bad data from the cache, since the DVA with the
 * birth TXG uniquely identify a block in space and time - once created,
 * a block is immutable on disk. The worst thing we have done is wasted
 * some time and memory at l2arc rebuild to reconstruct outdated ARC
 * entries that will get dropped from the l2arc as it is being updated
 * with new blocks.
 *
 * L2ARC buffers that have been evicted by l2arc_evict() ahead of the write
 * hand are not restored. This is done by saving the offset (in bytes)
 * l2arc_evict() has evicted to in the L2ARC device header and taking it
 * into account when restoring buffers.
 */

static boolean_t
l2arc_write_eligible(uint64_t spa_guid, arc_buf_hdr_t *hdr)
{
	/*
	 * A buffer is *not* eligible for the L2ARC if it:
	 * 1. belongs to a different spa.
	 * 2. is already cached on the L2ARC.
	 * 3. has an I/O in progress (it may be an incomplete read).
	 * 4. is flagged not eligible (zfs property).
	 */
	if (hdr->b_spa != spa_guid || HDR_HAS_L2HDR(hdr) ||
	    HDR_IO_IN_PROGRESS(hdr) || !HDR_L2CACHE(hdr))
		return (B_FALSE);

	return (B_TRUE);
}

static uint64_t
l2arc_write_size(l2arc_dev_t *dev)
{
	uint64_t size, dev_size, tsize;

	/*
	 * Make sure our globals have meaningful values in case the user
	 * altered them.
	 */
	size = l2arc_write_max;
	if (size == 0) {
		cmn_err(CE_NOTE, "Bad value for l2arc_write_max, value must "
		    "be greater than zero, resetting it to the default (%d)",
		    L2ARC_WRITE_SIZE);
		size = l2arc_write_max = L2ARC_WRITE_SIZE;
	}

	if (arc_warm == B_FALSE)
		size += l2arc_write_boost;

	/*
	 * Make sure the write size does not exceed the size of the cache
	 * device. This is important in l2arc_evict(), otherwise infinite
	 * iteration can occur.
	 */
	dev_size = dev->l2ad_end - dev->l2ad_start;
	tsize = size + l2arc_log_blk_overhead(size, dev);
	if (dev->l2ad_vdev->vdev_has_trim && l2arc_trim_ahead > 0)
		tsize += MAX(64 * 1024 * 1024,
		    (tsize * l2arc_trim_ahead) / 100);

	if (tsize >= dev_size) {
		cmn_err(CE_NOTE, "l2arc_write_max or l2arc_write_boost "
		    "plus the overhead of log blocks (persistent L2ARC, "
		    "%llu bytes) exceeds the size of the cache device "
		    "(guid %llu), resetting them to the default (%d)",
		    l2arc_log_blk_overhead(size, dev),
		    dev->l2ad_vdev->vdev_guid, L2ARC_WRITE_SIZE);
		size = l2arc_write_max = l2arc_write_boost = L2ARC_WRITE_SIZE;

		if (arc_warm == B_FALSE)
			size += l2arc_write_boost;
	}

	return (size);

}

static clock_t
l2arc_write_interval(clock_t began, uint64_t wanted, uint64_t wrote)
{
	clock_t interval, next, now;

	/*
	 * If the ARC lists are busy, increase our write rate; if the
	 * lists are stale, idle back.  This is achieved by checking
	 * how much we previously wrote - if it was more than half of
	 * what we wanted, schedule the next write much sooner.
	 */
	if (l2arc_feed_again && wrote > (wanted / 2))
		interval = (hz * l2arc_feed_min_ms) / 1000;
	else
		interval = hz * l2arc_feed_secs;

	now = ddi_get_lbolt();
	next = MAX(now, MIN(now + interval, began + interval));

	return (next);
}

/*
 * Cycle through L2ARC devices.  This is how L2ARC load balances.
 * If a device is returned, this also returns holding the spa config lock.
 */
static l2arc_dev_t *
l2arc_dev_get_next(void)
{
	l2arc_dev_t *first, *next = NULL;

	/*
	 * Lock out the removal of spas (spa_namespace_lock), then removal
	 * of cache devices (l2arc_dev_mtx).  Once a device has been selected,
	 * both locks will be dropped and a spa config lock held instead.
	 */
	mutex_enter(&spa_namespace_lock);
	mutex_enter(&l2arc_dev_mtx);

	/* if there are no vdevs, there is nothing to do */
	if (l2arc_ndev == 0)
		goto out;

	first = NULL;
	next = l2arc_dev_last;
	do {
		/* loop around the list looking for a non-faulted vdev */
		if (next == NULL) {
			next = list_head(l2arc_dev_list);
		} else {
			next = list_next(l2arc_dev_list, next);
			if (next == NULL)
				next = list_head(l2arc_dev_list);
		}

		/* if we have come back to the start, bail out */
		if (first == NULL)
			first = next;
		else if (next == first)
			break;

	} while (vdev_is_dead(next->l2ad_vdev) || next->l2ad_rebuild ||
	    next->l2ad_trim_all);

	/* if we were unable to find any usable vdevs, return NULL */
	if (vdev_is_dead(next->l2ad_vdev) || next->l2ad_rebuild ||
	    next->l2ad_trim_all)
		next = NULL;

	l2arc_dev_last = next;

out:
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Grab the config lock to prevent the 'next' device from being
	 * removed while we are writing to it.
	 */
	if (next != NULL)
		spa_config_enter(next->l2ad_spa, SCL_L2ARC, next, RW_READER);
	mutex_exit(&spa_namespace_lock);

	return (next);
}

/*
 * Free buffers that were tagged for destruction.
 */
static void
l2arc_do_free_on_write(void)
{
	list_t *buflist;
	l2arc_data_free_t *df, *df_prev;

	mutex_enter(&l2arc_free_on_write_mtx);
	buflist = l2arc_free_on_write;

	for (df = list_tail(buflist); df; df = df_prev) {
		df_prev = list_prev(buflist, df);
		ASSERT3P(df->l2df_abd, !=, NULL);
		abd_free(df->l2df_abd);
		list_remove(buflist, df);
		kmem_free(df, sizeof (l2arc_data_free_t));
	}

	mutex_exit(&l2arc_free_on_write_mtx);
}

/*
 * A write to a cache device has completed.  Update all headers to allow
 * reads from these buffers to begin.
 */
static void
l2arc_write_done(zio_t *zio)
{
	l2arc_write_callback_t	*cb;
	l2arc_lb_abd_buf_t	*abd_buf;
	l2arc_lb_ptr_buf_t	*lb_ptr_buf;
	l2arc_dev_t		*dev;
	l2arc_dev_hdr_phys_t	*l2dhdr;
	list_t			*buflist;
	arc_buf_hdr_t		*head, *hdr, *hdr_prev;
	kmutex_t		*hash_lock;
	int64_t			bytes_dropped = 0;

	cb = zio->io_private;
	ASSERT3P(cb, !=, NULL);
	dev = cb->l2wcb_dev;
	l2dhdr = dev->l2ad_dev_hdr;
	ASSERT3P(dev, !=, NULL);
	head = cb->l2wcb_head;
	ASSERT3P(head, !=, NULL);
	buflist = &dev->l2ad_buflist;
	ASSERT3P(buflist, !=, NULL);
	DTRACE_PROBE2(l2arc__iodone, zio_t *, zio,
	    l2arc_write_callback_t *, cb);

	/*
	 * All writes completed, or an error was hit.
	 */
top:
	mutex_enter(&dev->l2ad_mtx);
	for (hdr = list_prev(buflist, head); hdr; hdr = hdr_prev) {
		hdr_prev = list_prev(buflist, hdr);

		hash_lock = HDR_LOCK(hdr);

		/*
		 * We cannot use mutex_enter or else we can deadlock
		 * with l2arc_write_buffers (due to swapping the order
		 * the hash lock and l2ad_mtx are taken).
		 */
		if (!mutex_tryenter(hash_lock)) {
			/*
			 * Missed the hash lock. We must retry so we
			 * don't leave the ARC_FLAG_L2_WRITING bit set.
			 */
			ARCSTAT_BUMP(arcstat_l2_writes_lock_retry);

			/*
			 * We don't want to rescan the headers we've
			 * already marked as having been written out, so
			 * we reinsert the head node so we can pick up
			 * where we left off.
			 */
			list_remove(buflist, head);
			list_insert_after(buflist, hdr, head);

			mutex_exit(&dev->l2ad_mtx);

			/*
			 * We wait for the hash lock to become available
			 * to try and prevent busy waiting, and increase
			 * the chance we'll be able to acquire the lock
			 * the next time around.
			 */
			mutex_enter(hash_lock);
			mutex_exit(hash_lock);
			goto top;
		}

		/*
		 * We could not have been moved into the arc_l2c_only
		 * state while in-flight due to our ARC_FLAG_L2_WRITING
		 * bit being set. Let's just ensure that's being enforced.
		 */
		ASSERT(HDR_HAS_L1HDR(hdr));

		/*
		 * Skipped - drop L2ARC entry and mark the header as no
		 * longer L2 eligibile.
		 */
		if (zio->io_error != 0) {
			/*
			 * Error - drop L2ARC entry.
			 */
			list_remove(buflist, hdr);
			arc_hdr_clear_flags(hdr, ARC_FLAG_HAS_L2HDR);

			uint64_t psize = HDR_GET_PSIZE(hdr);
			l2arc_hdr_arcstats_decrement(hdr);

			bytes_dropped +=
			    vdev_psize_to_asize(dev->l2ad_vdev, psize);
			(void) zfs_refcount_remove_many(&dev->l2ad_alloc,
			    arc_hdr_size(hdr), hdr);
		}

		/*
		 * Allow ARC to begin reads and ghost list evictions to
		 * this L2ARC entry.
		 */
		arc_hdr_clear_flags(hdr, ARC_FLAG_L2_WRITING);

		mutex_exit(hash_lock);
	}

	/*
	 * Free the allocated abd buffers for writing the log blocks.
	 * If the zio failed reclaim the allocated space and remove the
	 * pointers to these log blocks from the log block pointer list
	 * of the L2ARC device.
	 */
	while ((abd_buf = list_remove_tail(&cb->l2wcb_abd_list)) != NULL) {
		abd_free(abd_buf->abd);
		zio_buf_free(abd_buf, sizeof (*abd_buf));
		if (zio->io_error != 0) {
			lb_ptr_buf = list_remove_head(&dev->l2ad_lbptr_list);
			/*
			 * L2BLK_GET_PSIZE returns aligned size for log
			 * blocks.
			 */
			uint64_t asize =
			    L2BLK_GET_PSIZE((lb_ptr_buf->lb_ptr)->lbp_prop);
			bytes_dropped += asize;
			ARCSTAT_INCR(arcstat_l2_log_blk_asize, -asize);
			ARCSTAT_BUMPDOWN(arcstat_l2_log_blk_count);
			zfs_refcount_remove_many(&dev->l2ad_lb_asize, asize,
			    lb_ptr_buf);
			zfs_refcount_remove(&dev->l2ad_lb_count, lb_ptr_buf);
			kmem_free(lb_ptr_buf->lb_ptr,
			    sizeof (l2arc_log_blkptr_t));
			kmem_free(lb_ptr_buf, sizeof (l2arc_lb_ptr_buf_t));
		}
	}
	list_destroy(&cb->l2wcb_abd_list);

	if (zio->io_error != 0) {
		ARCSTAT_BUMP(arcstat_l2_writes_error);

		/*
		 * Restore the lbps array in the header to its previous state.
		 * If the list of log block pointers is empty, zero out the
		 * log block pointers in the device header.
		 */
		lb_ptr_buf = list_head(&dev->l2ad_lbptr_list);
		for (int i = 0; i < 2; i++) {
			if (lb_ptr_buf == NULL) {
				/*
				 * If the list is empty zero out the device
				 * header. Otherwise zero out the second log
				 * block pointer in the header.
				 */
				if (i == 0) {
					bzero(l2dhdr, dev->l2ad_dev_hdr_asize);
				} else {
					bzero(&l2dhdr->dh_start_lbps[i],
					    sizeof (l2arc_log_blkptr_t));
				}
				break;
			}
			bcopy(lb_ptr_buf->lb_ptr, &l2dhdr->dh_start_lbps[i],
			    sizeof (l2arc_log_blkptr_t));
			lb_ptr_buf = list_next(&dev->l2ad_lbptr_list,
			    lb_ptr_buf);
		}
	}

	atomic_inc_64(&l2arc_writes_done);
	list_remove(buflist, head);
	ASSERT(!HDR_HAS_L1HDR(head));
	kmem_cache_free(hdr_l2only_cache, head);
	mutex_exit(&dev->l2ad_mtx);

	ASSERT(dev->l2ad_vdev != NULL);
	vdev_space_update(dev->l2ad_vdev, -bytes_dropped, 0, 0);

	l2arc_do_free_on_write();

	kmem_free(cb, sizeof (l2arc_write_callback_t));
}

static int
l2arc_untransform(zio_t *zio, l2arc_read_callback_t *cb)
{
	int ret;
	spa_t *spa = zio->io_spa;
	arc_buf_hdr_t *hdr = cb->l2rcb_hdr;
	blkptr_t *bp = zio->io_bp;
	uint8_t salt[ZIO_DATA_SALT_LEN];
	uint8_t iv[ZIO_DATA_IV_LEN];
	uint8_t mac[ZIO_DATA_MAC_LEN];
	boolean_t no_crypt = B_FALSE;

	/*
	 * ZIL data is never be written to the L2ARC, so we don't need
	 * special handling for its unique MAC storage.
	 */
	ASSERT3U(BP_GET_TYPE(bp), !=, DMU_OT_INTENT_LOG);
	ASSERT(MUTEX_HELD(HDR_LOCK(hdr)));
	ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);

	/*
	 * If the data was encrypted, decrypt it now. Note that
	 * we must check the bp here and not the hdr, since the
	 * hdr does not have its encryption parameters updated
	 * until arc_read_done().
	 */
	if (BP_IS_ENCRYPTED(bp)) {
		abd_t *eabd = arc_get_data_abd(hdr, arc_hdr_size(hdr), hdr,
		    B_TRUE);

		zio_crypt_decode_params_bp(bp, salt, iv);
		zio_crypt_decode_mac_bp(bp, mac);

		ret = spa_do_crypt_abd(B_FALSE, spa, &cb->l2rcb_zb,
		    BP_GET_TYPE(bp), BP_GET_DEDUP(bp), BP_SHOULD_BYTESWAP(bp),
		    salt, iv, mac, HDR_GET_PSIZE(hdr), eabd,
		    hdr->b_l1hdr.b_pabd, &no_crypt);
		if (ret != 0) {
			arc_free_data_abd(hdr, eabd, arc_hdr_size(hdr), hdr);
			goto error;
		}

		/*
		 * If we actually performed decryption, replace b_pabd
		 * with the decrypted data. Otherwise we can just throw
		 * our decryption buffer away.
		 */
		if (!no_crypt) {
			arc_free_data_abd(hdr, hdr->b_l1hdr.b_pabd,
			    arc_hdr_size(hdr), hdr);
			hdr->b_l1hdr.b_pabd = eabd;
			zio->io_abd = eabd;
		} else {
			arc_free_data_abd(hdr, eabd, arc_hdr_size(hdr), hdr);
		}
	}

	/*
	 * If the L2ARC block was compressed, but ARC compression
	 * is disabled we decompress the data into a new buffer and
	 * replace the existing data.
	 */
	if (HDR_GET_COMPRESS(hdr) != ZIO_COMPRESS_OFF &&
	    !HDR_COMPRESSION_ENABLED(hdr)) {
		abd_t *cabd = arc_get_data_abd(hdr, arc_hdr_size(hdr), hdr,
		    B_TRUE);
		void *tmp = abd_borrow_buf(cabd, arc_hdr_size(hdr));

		ret = zio_decompress_data(HDR_GET_COMPRESS(hdr),
		    hdr->b_l1hdr.b_pabd, tmp, HDR_GET_PSIZE(hdr),
		    HDR_GET_LSIZE(hdr), &hdr->b_complevel);
		if (ret != 0) {
			abd_return_buf_copy(cabd, tmp, arc_hdr_size(hdr));
			arc_free_data_abd(hdr, cabd, arc_hdr_size(hdr), hdr);
			goto error;
		}

		abd_return_buf_copy(cabd, tmp, arc_hdr_size(hdr));
		arc_free_data_abd(hdr, hdr->b_l1hdr.b_pabd,
		    arc_hdr_size(hdr), hdr);
		hdr->b_l1hdr.b_pabd = cabd;
		zio->io_abd = cabd;
		zio->io_size = HDR_GET_LSIZE(hdr);
	}

	return (0);

error:
	return (ret);
}


/*
 * A read to a cache device completed.  Validate buffer contents before
 * handing over to the regular ARC routines.
 */
static void
l2arc_read_done(zio_t *zio)
{
	int tfm_error = 0;
	l2arc_read_callback_t *cb = zio->io_private;
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;
	boolean_t valid_cksum;
	boolean_t using_rdata = (BP_IS_ENCRYPTED(&cb->l2rcb_bp) &&
	    (cb->l2rcb_flags & ZIO_FLAG_RAW_ENCRYPT));

	ASSERT3P(zio->io_vd, !=, NULL);
	ASSERT(zio->io_flags & ZIO_FLAG_DONT_PROPAGATE);

	spa_config_exit(zio->io_spa, SCL_L2ARC, zio->io_vd);

	ASSERT3P(cb, !=, NULL);
	hdr = cb->l2rcb_hdr;
	ASSERT3P(hdr, !=, NULL);

	hash_lock = HDR_LOCK(hdr);
	mutex_enter(hash_lock);
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));

	/*
	 * If the data was read into a temporary buffer,
	 * move it and free the buffer.
	 */
	if (cb->l2rcb_abd != NULL) {
		ASSERT3U(arc_hdr_size(hdr), <, zio->io_size);
		if (zio->io_error == 0) {
			if (using_rdata) {
				abd_copy(hdr->b_crypt_hdr.b_rabd,
				    cb->l2rcb_abd, arc_hdr_size(hdr));
			} else {
				abd_copy(hdr->b_l1hdr.b_pabd,
				    cb->l2rcb_abd, arc_hdr_size(hdr));
			}
		}

		/*
		 * The following must be done regardless of whether
		 * there was an error:
		 * - free the temporary buffer
		 * - point zio to the real ARC buffer
		 * - set zio size accordingly
		 * These are required because zio is either re-used for
		 * an I/O of the block in the case of the error
		 * or the zio is passed to arc_read_done() and it
		 * needs real data.
		 */
		abd_free(cb->l2rcb_abd);
		zio->io_size = zio->io_orig_size = arc_hdr_size(hdr);

		if (using_rdata) {
			ASSERT(HDR_HAS_RABD(hdr));
			zio->io_abd = zio->io_orig_abd =
			    hdr->b_crypt_hdr.b_rabd;
		} else {
			ASSERT3P(hdr->b_l1hdr.b_pabd, !=, NULL);
			zio->io_abd = zio->io_orig_abd = hdr->b_l1hdr.b_pabd;
		}
	}

	ASSERT3P(zio->io_abd, !=, NULL);

	/*
	 * Check this survived the L2ARC journey.
	 */
	ASSERT(zio->io_abd == hdr->b_l1hdr.b_pabd ||
	    (HDR_HAS_RABD(hdr) && zio->io_abd == hdr->b_crypt_hdr.b_rabd));
	zio->io_bp_copy = cb->l2rcb_bp;	/* XXX fix in L2ARC 2.0	*/
	zio->io_bp = &zio->io_bp_copy;	/* XXX fix in L2ARC 2.0	*/
	zio->io_prop.zp_complevel = hdr->b_complevel;

	valid_cksum = arc_cksum_is_equal(hdr, zio);

	/*
	 * b_rabd will always match the data as it exists on disk if it is
	 * being used. Therefore if we are reading into b_rabd we do not
	 * attempt to untransform the data.
	 */
	if (valid_cksum && !using_rdata)
		tfm_error = l2arc_untransform(zio, cb);

	if (valid_cksum && tfm_error == 0 && zio->io_error == 0 &&
	    !HDR_L2_EVICTED(hdr)) {
		mutex_exit(hash_lock);
		zio->io_private = hdr;
		arc_read_done(zio);
	} else {
		/*
		 * Buffer didn't survive caching.  Increment stats and
		 * reissue to the original storage device.
		 */
		if (zio->io_error != 0) {
			ARCSTAT_BUMP(arcstat_l2_io_error);
		} else {
			zio->io_error = SET_ERROR(EIO);
		}
		if (!valid_cksum || tfm_error != 0)
			ARCSTAT_BUMP(arcstat_l2_cksum_bad);

		/*
		 * If there's no waiter, issue an async i/o to the primary
		 * storage now.  If there *is* a waiter, the caller must
		 * issue the i/o in a context where it's OK to block.
		 */
		if (zio->io_waiter == NULL) {
			zio_t *pio = zio_unique_parent(zio);
			void *abd = (using_rdata) ?
			    hdr->b_crypt_hdr.b_rabd : hdr->b_l1hdr.b_pabd;

			ASSERT(!pio || pio->io_child_type == ZIO_CHILD_LOGICAL);

			zio = zio_read(pio, zio->io_spa, zio->io_bp,
			    abd, zio->io_size, arc_read_done,
			    hdr, zio->io_priority, cb->l2rcb_flags,
			    &cb->l2rcb_zb);

			/*
			 * Original ZIO will be freed, so we need to update
			 * ARC header with the new ZIO pointer to be used
			 * by zio_change_priority() in arc_read().
			 */
			for (struct arc_callback *acb = hdr->b_l1hdr.b_acb;
			    acb != NULL; acb = acb->acb_next)
				acb->acb_zio_head = zio;

			mutex_exit(hash_lock);
			zio_nowait(zio);
		} else {
			mutex_exit(hash_lock);
		}
	}

	kmem_free(cb, sizeof (l2arc_read_callback_t));
}

/*
 * This is the list priority from which the L2ARC will search for pages to
 * cache.  This is used within loops (0..3) to cycle through lists in the
 * desired order.  This order can have a significant effect on cache
 * performance.
 *
 * Currently the metadata lists are hit first, MFU then MRU, followed by
 * the data lists.  This function returns a locked list, and also returns
 * the lock pointer.
 */
static multilist_sublist_t *
l2arc_sublist_lock(int list_num)
{
	multilist_t *ml = NULL;
	unsigned int idx;

	ASSERT(list_num >= 0 && list_num < L2ARC_FEED_TYPES);

	switch (list_num) {
	case 0:
		ml = arc_mfu->arcs_list[ARC_BUFC_METADATA];
		break;
	case 1:
		ml = arc_mru->arcs_list[ARC_BUFC_METADATA];
		break;
	case 2:
		ml = arc_mfu->arcs_list[ARC_BUFC_DATA];
		break;
	case 3:
		ml = arc_mru->arcs_list[ARC_BUFC_DATA];
		break;
	default:
		return (NULL);
	}

	/*
	 * Return a randomly-selected sublist. This is acceptable
	 * because the caller feeds only a little bit of data for each
	 * call (8MB). Subsequent calls will result in different
	 * sublists being selected.
	 */
	idx = multilist_get_random_index(ml);
	return (multilist_sublist_lock(ml, idx));
}

/*
 * Calculates the maximum overhead of L2ARC metadata log blocks for a given
 * L2ARC write size. l2arc_evict and l2arc_write_size need to include this
 * overhead in processing to make sure there is enough headroom available
 * when writing buffers.
 */
static inline uint64_t
l2arc_log_blk_overhead(uint64_t write_sz, l2arc_dev_t *dev)
{
	if (dev->l2ad_log_entries == 0) {
		return (0);
	} else {
		uint64_t log_entries = write_sz >> SPA_MINBLOCKSHIFT;

		uint64_t log_blocks = (log_entries +
		    dev->l2ad_log_entries - 1) /
		    dev->l2ad_log_entries;

		return (vdev_psize_to_asize(dev->l2ad_vdev,
		    sizeof (l2arc_log_blk_phys_t)) * log_blocks);
	}
}

/*
 * Evict buffers from the device write hand to the distance specified in
 * bytes. This distance may span populated buffers, it may span nothing.
 * This is clearing a region on the L2ARC device ready for writing.
 * If the 'all' boolean is set, every buffer is evicted.
 */
static void
l2arc_evict(l2arc_dev_t *dev, uint64_t distance, boolean_t all)
{
	list_t *buflist;
	arc_buf_hdr_t *hdr, *hdr_prev;
	kmutex_t *hash_lock;
	uint64_t taddr;
	l2arc_lb_ptr_buf_t *lb_ptr_buf, *lb_ptr_buf_prev;
	vdev_t *vd = dev->l2ad_vdev;
	boolean_t rerun;

	buflist = &dev->l2ad_buflist;

	/*
	 * We need to add in the worst case scenario of log block overhead.
	 */
	distance += l2arc_log_blk_overhead(distance, dev);
	if (vd->vdev_has_trim && l2arc_trim_ahead > 0) {
		/*
		 * Trim ahead of the write size 64MB or (l2arc_trim_ahead/100)
		 * times the write size, whichever is greater.
		 */
		distance += MAX(64 * 1024 * 1024,
		    (distance * l2arc_trim_ahead) / 100);
	}

top:
	rerun = B_FALSE;
	if (dev->l2ad_hand >= (dev->l2ad_end - distance)) {
		/*
		 * When there is no space to accommodate upcoming writes,
		 * evict to the end. Then bump the write and evict hands
		 * to the start and iterate. This iteration does not
		 * happen indefinitely as we make sure in
		 * l2arc_write_size() that when the write hand is reset,
		 * the write size does not exceed the end of the device.
		 */
		rerun = B_TRUE;
		taddr = dev->l2ad_end;
	} else {
		taddr = dev->l2ad_hand + distance;
	}
	DTRACE_PROBE4(l2arc__evict, l2arc_dev_t *, dev, list_t *, buflist,
	    uint64_t, taddr, boolean_t, all);

	if (!all) {
		/*
		 * This check has to be placed after deciding whether to
		 * iterate (rerun).
		 */
		if (dev->l2ad_first) {
			/*
			 * This is the first sweep through the device. There is
			 * nothing to evict. We have already trimmmed the
			 * whole device.
			 */
			goto out;
		} else {
			/*
			 * Trim the space to be evicted.
			 */
			if (vd->vdev_has_trim && dev->l2ad_evict < taddr &&
			    l2arc_trim_ahead > 0) {
				/*
				 * We have to drop the spa_config lock because
				 * vdev_trim_range() will acquire it.
				 * l2ad_evict already accounts for the label
				 * size. To prevent vdev_trim_ranges() from
				 * adding it again, we subtract it from
				 * l2ad_evict.
				 */
				spa_config_exit(dev->l2ad_spa, SCL_L2ARC, dev);
				vdev_trim_simple(vd,
				    dev->l2ad_evict - VDEV_LABEL_START_SIZE,
				    taddr - dev->l2ad_evict);
				spa_config_enter(dev->l2ad_spa, SCL_L2ARC, dev,
				    RW_READER);
			}

			/*
			 * When rebuilding L2ARC we retrieve the evict hand
			 * from the header of the device. Of note, l2arc_evict()
			 * does not actually delete buffers from the cache
			 * device, but trimming may do so depending on the
			 * hardware implementation. Thus keeping track of the
			 * evict hand is useful.
			 */
			dev->l2ad_evict = MAX(dev->l2ad_evict, taddr);
		}
	}

retry:
	mutex_enter(&dev->l2ad_mtx);
	/*
	 * We have to account for evicted log blocks. Run vdev_space_update()
	 * on log blocks whose offset (in bytes) is before the evicted offset
	 * (in bytes) by searching in the list of pointers to log blocks
	 * present in the L2ARC device.
	 */
	for (lb_ptr_buf = list_tail(&dev->l2ad_lbptr_list); lb_ptr_buf;
	    lb_ptr_buf = lb_ptr_buf_prev) {

		lb_ptr_buf_prev = list_prev(&dev->l2ad_lbptr_list, lb_ptr_buf);

		/* L2BLK_GET_PSIZE returns aligned size for log blocks */
		uint64_t asize = L2BLK_GET_PSIZE(
		    (lb_ptr_buf->lb_ptr)->lbp_prop);

		/*
		 * We don't worry about log blocks left behind (ie
		 * lbp_payload_start < l2ad_hand) because l2arc_write_buffers()
		 * will never write more than l2arc_evict() evicts.
		 */
		if (!all && l2arc_log_blkptr_valid(dev, lb_ptr_buf->lb_ptr)) {
			break;
		} else {
			vdev_space_update(vd, -asize, 0, 0);
			ARCSTAT_INCR(arcstat_l2_log_blk_asize, -asize);
			ARCSTAT_BUMPDOWN(arcstat_l2_log_blk_count);
			zfs_refcount_remove_many(&dev->l2ad_lb_asize, asize,
			    lb_ptr_buf);
			zfs_refcount_remove(&dev->l2ad_lb_count, lb_ptr_buf);
			list_remove(&dev->l2ad_lbptr_list, lb_ptr_buf);
			kmem_free(lb_ptr_buf->lb_ptr,
			    sizeof (l2arc_log_blkptr_t));
			kmem_free(lb_ptr_buf, sizeof (l2arc_lb_ptr_buf_t));
		}
	}

	for (hdr = list_tail(buflist); hdr; hdr = hdr_prev) {
		hdr_prev = list_prev(buflist, hdr);

		ASSERT(!HDR_EMPTY(hdr));
		hash_lock = HDR_LOCK(hdr);

		/*
		 * We cannot use mutex_enter or else we can deadlock
		 * with l2arc_write_buffers (due to swapping the order
		 * the hash lock and l2ad_mtx are taken).
		 */
		if (!mutex_tryenter(hash_lock)) {
			/*
			 * Missed the hash lock.  Retry.
			 */
			ARCSTAT_BUMP(arcstat_l2_evict_lock_retry);
			mutex_exit(&dev->l2ad_mtx);
			mutex_enter(hash_lock);
			mutex_exit(hash_lock);
			goto retry;
		}

		/*
		 * A header can't be on this list if it doesn't have L2 header.
		 */
		ASSERT(HDR_HAS_L2HDR(hdr));

		/* Ensure this header has finished being written. */
		ASSERT(!HDR_L2_WRITING(hdr));
		ASSERT(!HDR_L2_WRITE_HEAD(hdr));

		if (!all && (hdr->b_l2hdr.b_daddr >= dev->l2ad_evict ||
		    hdr->b_l2hdr.b_daddr < dev->l2ad_hand)) {
			/*
			 * We've evicted to the target address,
			 * or the end of the device.
			 */
			mutex_exit(hash_lock);
			break;
		}

		if (!HDR_HAS_L1HDR(hdr)) {
			ASSERT(!HDR_L2_READING(hdr));
			/*
			 * This doesn't exist in the ARC.  Destroy.
			 * arc_hdr_destroy() will call list_remove()
			 * and decrement arcstat_l2_lsize.
			 */
			arc_change_state(arc_anon, hdr, hash_lock);
			arc_hdr_destroy(hdr);
		} else {
			ASSERT(hdr->b_l1hdr.b_state != arc_l2c_only);
			ARCSTAT_BUMP(arcstat_l2_evict_l1cached);
			/*
			 * Invalidate issued or about to be issued
			 * reads, since we may be about to write
			 * over this location.
			 */
			if (HDR_L2_READING(hdr)) {
				ARCSTAT_BUMP(arcstat_l2_evict_reading);
				arc_hdr_set_flags(hdr, ARC_FLAG_L2_EVICTED);
			}

			arc_hdr_l2hdr_destroy(hdr);
		}
		mutex_exit(hash_lock);
	}
	mutex_exit(&dev->l2ad_mtx);

out:
	/*
	 * We need to check if we evict all buffers, otherwise we may iterate
	 * unnecessarily.
	 */
	if (!all && rerun) {
		/*
		 * Bump device hand to the device start if it is approaching the
		 * end. l2arc_evict() has already evicted ahead for this case.
		 */
		dev->l2ad_hand = dev->l2ad_start;
		dev->l2ad_evict = dev->l2ad_start;
		dev->l2ad_first = B_FALSE;
		goto top;
	}

	if (!all) {
		/*
		 * In case of cache device removal (all) the following
		 * assertions may be violated without functional consequences
		 * as the device is about to be removed.
		 */
		ASSERT3U(dev->l2ad_hand + distance, <, dev->l2ad_end);
		if (!dev->l2ad_first)
			ASSERT3U(dev->l2ad_hand, <, dev->l2ad_evict);
	}
}

/*
 * Handle any abd transforms that might be required for writing to the L2ARC.
 * If successful, this function will always return an abd with the data
 * transformed as it is on disk in a new abd of asize bytes.
 */
static int
l2arc_apply_transforms(spa_t *spa, arc_buf_hdr_t *hdr, uint64_t asize,
    abd_t **abd_out)
{
	int ret;
	void *tmp = NULL;
	abd_t *cabd = NULL, *eabd = NULL, *to_write = hdr->b_l1hdr.b_pabd;
	enum zio_compress compress = HDR_GET_COMPRESS(hdr);
	uint64_t psize = HDR_GET_PSIZE(hdr);
	uint64_t size = arc_hdr_size(hdr);
	boolean_t ismd = HDR_ISTYPE_METADATA(hdr);
	boolean_t bswap = (hdr->b_l1hdr.b_byteswap != DMU_BSWAP_NUMFUNCS);
	dsl_crypto_key_t *dck = NULL;
	uint8_t mac[ZIO_DATA_MAC_LEN] = { 0 };
	boolean_t no_crypt = B_FALSE;

	ASSERT((HDR_GET_COMPRESS(hdr) != ZIO_COMPRESS_OFF &&
	    !HDR_COMPRESSION_ENABLED(hdr)) ||
	    HDR_ENCRYPTED(hdr) || HDR_SHARED_DATA(hdr) || psize != asize);
	ASSERT3U(psize, <=, asize);

	/*
	 * If this data simply needs its own buffer, we simply allocate it
	 * and copy the data. This may be done to eliminate a dependency on a
	 * shared buffer or to reallocate the buffer to match asize.
	 */
	if (HDR_HAS_RABD(hdr) && asize != psize) {
		ASSERT3U(asize, >=, psize);
		to_write = abd_alloc_for_io(asize, ismd);
		abd_copy(to_write, hdr->b_crypt_hdr.b_rabd, psize);
		if (psize != asize)
			abd_zero_off(to_write, psize, asize - psize);
		goto out;
	}

	if ((compress == ZIO_COMPRESS_OFF || HDR_COMPRESSION_ENABLED(hdr)) &&
	    !HDR_ENCRYPTED(hdr)) {
		ASSERT3U(size, ==, psize);
		to_write = abd_alloc_for_io(asize, ismd);
		abd_copy(to_write, hdr->b_l1hdr.b_pabd, size);
		if (size != asize)
			abd_zero_off(to_write, size, asize - size);
		goto out;
	}

	if (compress != ZIO_COMPRESS_OFF && !HDR_COMPRESSION_ENABLED(hdr)) {
		cabd = abd_alloc_for_io(asize, ismd);
		tmp = abd_borrow_buf(cabd, asize);

		psize = zio_compress_data(compress, to_write, tmp, size,
		    hdr->b_complevel);

		if (psize >= size) {
			abd_return_buf(cabd, tmp, asize);
			HDR_SET_COMPRESS(hdr, ZIO_COMPRESS_OFF);
			to_write = cabd;
			abd_copy(to_write, hdr->b_l1hdr.b_pabd, size);
			if (size != asize)
				abd_zero_off(to_write, size, asize - size);
			goto encrypt;
		}
		ASSERT3U(psize, <=, HDR_GET_PSIZE(hdr));
		if (psize < asize)
			bzero((char *)tmp + psize, asize - psize);
		psize = HDR_GET_PSIZE(hdr);
		abd_return_buf_copy(cabd, tmp, asize);
		to_write = cabd;
	}

encrypt:
	if (HDR_ENCRYPTED(hdr)) {
		eabd = abd_alloc_for_io(asize, ismd);

		/*
		 * If the dataset was disowned before the buffer
		 * made it to this point, the key to re-encrypt
		 * it won't be available. In this case we simply
		 * won't write the buffer to the L2ARC.
		 */
		ret = spa_keystore_lookup_key(spa, hdr->b_crypt_hdr.b_dsobj,
		    FTAG, &dck);
		if (ret != 0)
			goto error;

		ret = zio_do_crypt_abd(B_TRUE, &dck->dck_key,
		    hdr->b_crypt_hdr.b_ot, bswap, hdr->b_crypt_hdr.b_salt,
		    hdr->b_crypt_hdr.b_iv, mac, psize, to_write, eabd,
		    &no_crypt);
		if (ret != 0)
			goto error;

		if (no_crypt)
			abd_copy(eabd, to_write, psize);

		if (psize != asize)
			abd_zero_off(eabd, psize, asize - psize);

		/* assert that the MAC we got here matches the one we saved */
		ASSERT0(bcmp(mac, hdr->b_crypt_hdr.b_mac, ZIO_DATA_MAC_LEN));
		spa_keystore_dsl_key_rele(spa, dck, FTAG);

		if (to_write == cabd)
			abd_free(cabd);

		to_write = eabd;
	}

out:
	ASSERT3P(to_write, !=, hdr->b_l1hdr.b_pabd);
	*abd_out = to_write;
	return (0);

error:
	if (dck != NULL)
		spa_keystore_dsl_key_rele(spa, dck, FTAG);
	if (cabd != NULL)
		abd_free(cabd);
	if (eabd != NULL)
		abd_free(eabd);

	*abd_out = NULL;
	return (ret);
}

static void
l2arc_blk_fetch_done(zio_t *zio)
{
	l2arc_read_callback_t *cb;

	cb = zio->io_private;
	if (cb->l2rcb_abd != NULL)
		abd_put(cb->l2rcb_abd);
	kmem_free(cb, sizeof (l2arc_read_callback_t));
}

/*
 * Find and write ARC buffers to the L2ARC device.
 *
 * An ARC_FLAG_L2_WRITING flag is set so that the L2ARC buffers are not valid
 * for reading until they have completed writing.
 * The headroom_boost is an in-out parameter used to maintain headroom boost
 * state between calls to this function.
 *
 * Returns the number of bytes actually written (which may be smaller than
 * the delta by which the device hand has changed due to alignment and the
 * writing of log blocks).
 */
static uint64_t
l2arc_write_buffers(spa_t *spa, l2arc_dev_t *dev, uint64_t target_sz)
{
	arc_buf_hdr_t 		*hdr, *hdr_prev, *head;
	uint64_t 		write_asize, write_psize, write_lsize, headroom;
	boolean_t		full;
	l2arc_write_callback_t	*cb = NULL;
	zio_t 			*pio, *wzio;
	uint64_t 		guid = spa_load_guid(spa);

	ASSERT3P(dev->l2ad_vdev, !=, NULL);

	pio = NULL;
	write_lsize = write_asize = write_psize = 0;
	full = B_FALSE;
	head = kmem_cache_alloc(hdr_l2only_cache, KM_PUSHPAGE);
	arc_hdr_set_flags(head, ARC_FLAG_L2_WRITE_HEAD | ARC_FLAG_HAS_L2HDR);

	/*
	 * Copy buffers for L2ARC writing.
	 */
	for (int try = 0; try < L2ARC_FEED_TYPES; try++) {
		/*
		 * If try == 1 or 3, we cache MRU metadata and data
		 * respectively.
		 */
		if (l2arc_mfuonly) {
			if (try == 1 || try == 3)
				continue;
		}

		multilist_sublist_t *mls = l2arc_sublist_lock(try);
		uint64_t passed_sz = 0;

		VERIFY3P(mls, !=, NULL);

		/*
		 * L2ARC fast warmup.
		 *
		 * Until the ARC is warm and starts to evict, read from the
		 * head of the ARC lists rather than the tail.
		 */
		if (arc_warm == B_FALSE)
			hdr = multilist_sublist_head(mls);
		else
			hdr = multilist_sublist_tail(mls);

		headroom = target_sz * l2arc_headroom;
		if (zfs_compressed_arc_enabled)
			headroom = (headroom * l2arc_headroom_boost) / 100;

		for (; hdr; hdr = hdr_prev) {
			kmutex_t *hash_lock;
			abd_t *to_write = NULL;

			if (arc_warm == B_FALSE)
				hdr_prev = multilist_sublist_next(mls, hdr);
			else
				hdr_prev = multilist_sublist_prev(mls, hdr);

			hash_lock = HDR_LOCK(hdr);
			if (!mutex_tryenter(hash_lock)) {
				/*
				 * Skip this buffer rather than waiting.
				 */
				continue;
			}

			passed_sz += HDR_GET_LSIZE(hdr);
			if (l2arc_headroom != 0 && passed_sz > headroom) {
				/*
				 * Searched too far.
				 */
				mutex_exit(hash_lock);
				break;
			}

			if (!l2arc_write_eligible(guid, hdr)) {
				mutex_exit(hash_lock);
				continue;
			}

			/*
			 * We rely on the L1 portion of the header below, so
			 * it's invalid for this header to have been evicted out
			 * of the ghost cache, prior to being written out. The
			 * ARC_FLAG_L2_WRITING bit ensures this won't happen.
			 */
			ASSERT(HDR_HAS_L1HDR(hdr));

			ASSERT3U(HDR_GET_PSIZE(hdr), >, 0);
			ASSERT3U(arc_hdr_size(hdr), >, 0);
			ASSERT(hdr->b_l1hdr.b_pabd != NULL ||
			    HDR_HAS_RABD(hdr));
			uint64_t psize = HDR_GET_PSIZE(hdr);
			uint64_t asize = vdev_psize_to_asize(dev->l2ad_vdev,
			    psize);

			if ((write_asize + asize) > target_sz) {
				full = B_TRUE;
				mutex_exit(hash_lock);
				break;
			}

			/*
			 * We rely on the L1 portion of the header below, so
			 * it's invalid for this header to have been evicted out
			 * of the ghost cache, prior to being written out. The
			 * ARC_FLAG_L2_WRITING bit ensures this won't happen.
			 */
			arc_hdr_set_flags(hdr, ARC_FLAG_L2_WRITING);
			ASSERT(HDR_HAS_L1HDR(hdr));

			ASSERT3U(HDR_GET_PSIZE(hdr), >, 0);
			ASSERT(hdr->b_l1hdr.b_pabd != NULL ||
			    HDR_HAS_RABD(hdr));
			ASSERT3U(arc_hdr_size(hdr), >, 0);

			/*
			 * If this header has b_rabd, we can use this since it
			 * must always match the data exactly as it exists on
			 * disk. Otherwise, the L2ARC can normally use the
			 * hdr's data, but if we're sharing data between the
			 * hdr and one of its bufs, L2ARC needs its own copy of
			 * the data so that the ZIO below can't race with the
			 * buf consumer. To ensure that this copy will be
			 * available for the lifetime of the ZIO and be cleaned
			 * up afterwards, we add it to the l2arc_free_on_write
			 * queue. If we need to apply any transforms to the
			 * data (compression, encryption) we will also need the
			 * extra buffer.
			 */
			if (HDR_HAS_RABD(hdr) && psize == asize) {
				to_write = hdr->b_crypt_hdr.b_rabd;
			} else if ((HDR_COMPRESSION_ENABLED(hdr) ||
			    HDR_GET_COMPRESS(hdr) == ZIO_COMPRESS_OFF) &&
			    !HDR_ENCRYPTED(hdr) && !HDR_SHARED_DATA(hdr) &&
			    psize == asize) {
				to_write = hdr->b_l1hdr.b_pabd;
			} else {
				int ret;
				arc_buf_contents_t type = arc_buf_type(hdr);

				ret = l2arc_apply_transforms(spa, hdr, asize,
				    &to_write);
				if (ret != 0) {
					arc_hdr_clear_flags(hdr,
					    ARC_FLAG_L2_WRITING);
					mutex_exit(hash_lock);
					continue;
				}

				l2arc_free_abd_on_write(to_write, asize, type);
			}

			if (pio == NULL) {
				/*
				 * Insert a dummy header on the buflist so
				 * l2arc_write_done() can find where the
				 * write buffers begin without searching.
				 */
				mutex_enter(&dev->l2ad_mtx);
				list_insert_head(&dev->l2ad_buflist, head);
				mutex_exit(&dev->l2ad_mtx);

				cb = kmem_alloc(
				    sizeof (l2arc_write_callback_t), KM_SLEEP);
				cb->l2wcb_dev = dev;
				cb->l2wcb_head = head;
				/*
				 * Create a list to save allocated abd buffers
				 * for l2arc_log_blk_commit().
				 */
				list_create(&cb->l2wcb_abd_list,
				    sizeof (l2arc_lb_abd_buf_t),
				    offsetof(l2arc_lb_abd_buf_t, node));
				pio = zio_root(spa, l2arc_write_done, cb,
				    ZIO_FLAG_CANFAIL);
			}

			hdr->b_l2hdr.b_dev = dev;
			hdr->b_l2hdr.b_hits = 0;

			hdr->b_l2hdr.b_daddr = dev->l2ad_hand;
			hdr->b_l2hdr.b_arcs_state =
			    hdr->b_l1hdr.b_state->arcs_state;
			arc_hdr_set_flags(hdr, ARC_FLAG_HAS_L2HDR);

			mutex_enter(&dev->l2ad_mtx);
			list_insert_head(&dev->l2ad_buflist, hdr);
			mutex_exit(&dev->l2ad_mtx);

			(void) zfs_refcount_add_many(&dev->l2ad_alloc,
			    arc_hdr_size(hdr), hdr);

			wzio = zio_write_phys(pio, dev->l2ad_vdev,
			    hdr->b_l2hdr.b_daddr, asize, to_write,
			    ZIO_CHECKSUM_OFF, NULL, hdr,
			    ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_CANFAIL, B_FALSE);

			write_lsize += HDR_GET_LSIZE(hdr);
			DTRACE_PROBE2(l2arc__write, vdev_t *, dev->l2ad_vdev,
			    zio_t *, wzio);

			write_psize += psize;
			write_asize += asize;
			dev->l2ad_hand += asize;
			l2arc_hdr_arcstats_increment(hdr);
			vdev_space_update(dev->l2ad_vdev, asize, 0, 0);

			mutex_exit(hash_lock);

			/*
			 * Append buf info to current log and commit if full.
			 * arcstat_l2_{size,asize} kstats are updated
			 * internally.
			 */
			if (l2arc_log_blk_insert(dev, hdr))
				l2arc_log_blk_commit(dev, pio, cb);

			zio_nowait(wzio);
		}

		multilist_sublist_unlock(mls);

		if (full == B_TRUE)
			break;
	}

	/* No buffers selected for writing? */
	if (pio == NULL) {
		ASSERT0(write_lsize);
		ASSERT(!HDR_HAS_L1HDR(head));
		kmem_cache_free(hdr_l2only_cache, head);

		/*
		 * Although we did not write any buffers l2ad_evict may
		 * have advanced.
		 */
		l2arc_dev_hdr_update(dev);

		return (0);
	}

	if (!dev->l2ad_first)
		ASSERT3U(dev->l2ad_hand, <=, dev->l2ad_evict);

	ASSERT3U(write_asize, <=, target_sz);
	ARCSTAT_BUMP(arcstat_l2_writes_sent);
	ARCSTAT_INCR(arcstat_l2_write_bytes, write_psize);

	dev->l2ad_writing = B_TRUE;
	(void) zio_wait(pio);
	dev->l2ad_writing = B_FALSE;

	/*
	 * Update the device header after the zio completes as
	 * l2arc_write_done() may have updated the memory holding the log block
	 * pointers in the device header.
	 */
	l2arc_dev_hdr_update(dev);

	return (write_asize);
}

static boolean_t
l2arc_hdr_limit_reached(void)
{
	int64_t s = aggsum_upper_bound(&astat_l2_hdr_size);

	return (arc_reclaim_needed() || (s > arc_meta_limit * 3 / 4) ||
	    (s > (arc_warm ? arc_c : arc_c_max) * l2arc_meta_percent / 100));
}

/*
 * This thread feeds the L2ARC at regular intervals.  This is the beating
 * heart of the L2ARC.
 */
/* ARGSUSED */
static void
l2arc_feed_thread(void *unused)
{
	callb_cpr_t cpr;
	l2arc_dev_t *dev;
	spa_t *spa;
	uint64_t size, wrote;
	clock_t begin, next = ddi_get_lbolt();
	fstrans_cookie_t cookie;

	CALLB_CPR_INIT(&cpr, &l2arc_feed_thr_lock, callb_generic_cpr, FTAG);

	mutex_enter(&l2arc_feed_thr_lock);

	cookie = spl_fstrans_mark();
	while (l2arc_thread_exit == 0) {
		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait_idle(&l2arc_feed_thr_cv,
		    &l2arc_feed_thr_lock, next);
		CALLB_CPR_SAFE_END(&cpr, &l2arc_feed_thr_lock);
		next = ddi_get_lbolt() + hz;

		/*
		 * Quick check for L2ARC devices.
		 */
		mutex_enter(&l2arc_dev_mtx);
		if (l2arc_ndev == 0) {
			mutex_exit(&l2arc_dev_mtx);
			continue;
		}
		mutex_exit(&l2arc_dev_mtx);
		begin = ddi_get_lbolt();

		/*
		 * This selects the next l2arc device to write to, and in
		 * doing so the next spa to feed from: dev->l2ad_spa.   This
		 * will return NULL if there are now no l2arc devices or if
		 * they are all faulted.
		 *
		 * If a device is returned, its spa's config lock is also
		 * held to prevent device removal.  l2arc_dev_get_next()
		 * will grab and release l2arc_dev_mtx.
		 */
		if ((dev = l2arc_dev_get_next()) == NULL)
			continue;

		spa = dev->l2ad_spa;
		ASSERT3P(spa, !=, NULL);

		/*
		 * If the pool is read-only then force the feed thread to
		 * sleep a little longer.
		 */
		if (!spa_writeable(spa)) {
			next = ddi_get_lbolt() + 5 * l2arc_feed_secs * hz;
			spa_config_exit(spa, SCL_L2ARC, dev);
			continue;
		}

		/*
		 * Avoid contributing to memory pressure.
		 */
		if (l2arc_hdr_limit_reached()) {
			ARCSTAT_BUMP(arcstat_l2_abort_lowmem);
			spa_config_exit(spa, SCL_L2ARC, dev);
			continue;
		}

		ARCSTAT_BUMP(arcstat_l2_feeds);

		size = l2arc_write_size(dev);

		/*
		 * Evict L2ARC buffers that will be overwritten.
		 */
		l2arc_evict(dev, size, B_FALSE);

		/*
		 * Write ARC buffers.
		 */
		wrote = l2arc_write_buffers(spa, dev, size);

		/*
		 * Calculate interval between writes.
		 */
		next = l2arc_write_interval(begin, size, wrote);
		spa_config_exit(spa, SCL_L2ARC, dev);
	}
	spl_fstrans_unmark(cookie);

	l2arc_thread_exit = 0;
	cv_broadcast(&l2arc_feed_thr_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops l2arc_feed_thr_lock */
	thread_exit();
}

boolean_t
l2arc_vdev_present(vdev_t *vd)
{
	return (l2arc_vdev_get(vd) != NULL);
}

/*
 * Returns the l2arc_dev_t associated with a particular vdev_t or NULL if
 * the vdev_t isn't an L2ARC device.
 */
l2arc_dev_t *
l2arc_vdev_get(vdev_t *vd)
{
	l2arc_dev_t	*dev;

	mutex_enter(&l2arc_dev_mtx);
	for (dev = list_head(l2arc_dev_list); dev != NULL;
	    dev = list_next(l2arc_dev_list, dev)) {
		if (dev->l2ad_vdev == vd)
			break;
	}
	mutex_exit(&l2arc_dev_mtx);

	return (dev);
}

/*
 * Add a vdev for use by the L2ARC.  By this point the spa has already
 * validated the vdev and opened it.
 */
void
l2arc_add_vdev(spa_t *spa, vdev_t *vd)
{
	l2arc_dev_t		*adddev;
	uint64_t		l2dhdr_asize;

	ASSERT(!l2arc_vdev_present(vd));

	/*
	 * Create a new l2arc device entry.
	 */
	adddev = vmem_zalloc(sizeof (l2arc_dev_t), KM_SLEEP);
	adddev->l2ad_spa = spa;
	adddev->l2ad_vdev = vd;
	/* leave extra size for an l2arc device header */
	l2dhdr_asize = adddev->l2ad_dev_hdr_asize =
	    MAX(sizeof (*adddev->l2ad_dev_hdr), 1 << vd->vdev_ashift);
	adddev->l2ad_start = VDEV_LABEL_START_SIZE + l2dhdr_asize;
	adddev->l2ad_end = VDEV_LABEL_START_SIZE + vdev_get_min_asize(vd);
	ASSERT3U(adddev->l2ad_start, <, adddev->l2ad_end);
	adddev->l2ad_hand = adddev->l2ad_start;
	adddev->l2ad_evict = adddev->l2ad_start;
	adddev->l2ad_first = B_TRUE;
	adddev->l2ad_writing = B_FALSE;
	adddev->l2ad_trim_all = B_FALSE;
	list_link_init(&adddev->l2ad_node);
	adddev->l2ad_dev_hdr = kmem_zalloc(l2dhdr_asize, KM_SLEEP);

	mutex_init(&adddev->l2ad_mtx, NULL, MUTEX_DEFAULT, NULL);
	/*
	 * This is a list of all ARC buffers that are still valid on the
	 * device.
	 */
	list_create(&adddev->l2ad_buflist, sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l2hdr.b_l2node));

	/*
	 * This is a list of pointers to log blocks that are still present
	 * on the device.
	 */
	list_create(&adddev->l2ad_lbptr_list, sizeof (l2arc_lb_ptr_buf_t),
	    offsetof(l2arc_lb_ptr_buf_t, node));

	vdev_space_update(vd, 0, 0, adddev->l2ad_end - adddev->l2ad_hand);
	zfs_refcount_create(&adddev->l2ad_alloc);
	zfs_refcount_create(&adddev->l2ad_lb_asize);
	zfs_refcount_create(&adddev->l2ad_lb_count);

	/*
	 * Add device to global list
	 */
	mutex_enter(&l2arc_dev_mtx);
	list_insert_head(l2arc_dev_list, adddev);
	atomic_inc_64(&l2arc_ndev);
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Decide if vdev is eligible for L2ARC rebuild
	 */
	l2arc_rebuild_vdev(adddev->l2ad_vdev, B_FALSE);
}

void
l2arc_rebuild_vdev(vdev_t *vd, boolean_t reopen)
{
	l2arc_dev_t		*dev = NULL;
	l2arc_dev_hdr_phys_t	*l2dhdr;
	uint64_t		l2dhdr_asize;
	spa_t			*spa;

	dev = l2arc_vdev_get(vd);
	ASSERT3P(dev, !=, NULL);
	spa = dev->l2ad_spa;
	l2dhdr = dev->l2ad_dev_hdr;
	l2dhdr_asize = dev->l2ad_dev_hdr_asize;

	/*
	 * The L2ARC has to hold at least the payload of one log block for
	 * them to be restored (persistent L2ARC). The payload of a log block
	 * depends on the amount of its log entries. We always write log blocks
	 * with 1022 entries. How many of them are committed or restored depends
	 * on the size of the L2ARC device. Thus the maximum payload of
	 * one log block is 1022 * SPA_MAXBLOCKSIZE = 16GB. If the L2ARC device
	 * is less than that, we reduce the amount of committed and restored
	 * log entries per block so as to enable persistence.
	 */
	if (dev->l2ad_end < l2arc_rebuild_blocks_min_l2size) {
		dev->l2ad_log_entries = 0;
	} else {
		dev->l2ad_log_entries = MIN((dev->l2ad_end -
		    dev->l2ad_start) >> SPA_MAXBLOCKSHIFT,
		    L2ARC_LOG_BLK_MAX_ENTRIES);
	}

	/*
	 * Read the device header, if an error is returned do not rebuild L2ARC.
	 */
	if (l2arc_dev_hdr_read(dev) == 0 && dev->l2ad_log_entries > 0) {
		/*
		 * If we are onlining a cache device (vdev_reopen) that was
		 * still present (l2arc_vdev_present()) and rebuild is enabled,
		 * we should evict all ARC buffers and pointers to log blocks
		 * and reclaim their space before restoring its contents to
		 * L2ARC.
		 */
		if (reopen) {
			if (!l2arc_rebuild_enabled) {
				return;
			} else {
				l2arc_evict(dev, 0, B_TRUE);
				/* start a new log block */
				dev->l2ad_log_ent_idx = 0;
				dev->l2ad_log_blk_payload_asize = 0;
				dev->l2ad_log_blk_payload_start = 0;
			}
		}
		/*
		 * Just mark the device as pending for a rebuild. We won't
		 * be starting a rebuild in line here as it would block pool
		 * import. Instead spa_load_impl will hand that off to an
		 * async task which will call l2arc_spa_rebuild_start.
		 */
		dev->l2ad_rebuild = B_TRUE;
	} else if (spa_writeable(spa)) {
		/*
		 * In this case TRIM the whole device if l2arc_trim_ahead > 0,
		 * otherwise create a new header. We zero out the memory holding
		 * the header to reset dh_start_lbps. If we TRIM the whole
		 * device the new header will be written by
		 * vdev_trim_l2arc_thread() at the end of the TRIM to update the
		 * trim_state in the header too. When reading the header, if
		 * trim_state is not VDEV_TRIM_COMPLETE and l2arc_trim_ahead > 0
		 * we opt to TRIM the whole device again.
		 */
		if (l2arc_trim_ahead > 0) {
			dev->l2ad_trim_all = B_TRUE;
		} else {
			bzero(l2dhdr, l2dhdr_asize);
			l2arc_dev_hdr_update(dev);
		}
	}
}

/*
 * Remove a vdev from the L2ARC.
 */
void
l2arc_remove_vdev(vdev_t *vd)
{
	l2arc_dev_t *remdev = NULL;

	/*
	 * Find the device by vdev
	 */
	remdev = l2arc_vdev_get(vd);
	ASSERT3P(remdev, !=, NULL);

	/*
	 * Cancel any ongoing or scheduled rebuild.
	 */
	mutex_enter(&l2arc_rebuild_thr_lock);
	if (remdev->l2ad_rebuild_began == B_TRUE) {
		remdev->l2ad_rebuild_cancel = B_TRUE;
		while (remdev->l2ad_rebuild == B_TRUE)
			cv_wait(&l2arc_rebuild_thr_cv, &l2arc_rebuild_thr_lock);
	}
	mutex_exit(&l2arc_rebuild_thr_lock);

	/*
	 * Remove device from global list
	 */
	mutex_enter(&l2arc_dev_mtx);
	list_remove(l2arc_dev_list, remdev);
	l2arc_dev_last = NULL;		/* may have been invalidated */
	atomic_dec_64(&l2arc_ndev);
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Clear all buflists and ARC references.  L2ARC device flush.
	 */
	l2arc_evict(remdev, 0, B_TRUE);
	list_destroy(&remdev->l2ad_buflist);
	ASSERT(list_is_empty(&remdev->l2ad_lbptr_list));
	list_destroy(&remdev->l2ad_lbptr_list);
	mutex_destroy(&remdev->l2ad_mtx);
	zfs_refcount_destroy(&remdev->l2ad_alloc);
	zfs_refcount_destroy(&remdev->l2ad_lb_asize);
	zfs_refcount_destroy(&remdev->l2ad_lb_count);
	kmem_free(remdev->l2ad_dev_hdr, remdev->l2ad_dev_hdr_asize);
	vmem_free(remdev, sizeof (l2arc_dev_t));
}

void
l2arc_init(void)
{
	l2arc_thread_exit = 0;
	l2arc_ndev = 0;
	l2arc_writes_sent = 0;
	l2arc_writes_done = 0;

	mutex_init(&l2arc_feed_thr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&l2arc_feed_thr_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&l2arc_rebuild_thr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&l2arc_rebuild_thr_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&l2arc_dev_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&l2arc_free_on_write_mtx, NULL, MUTEX_DEFAULT, NULL);

	l2arc_dev_list = &L2ARC_dev_list;
	l2arc_free_on_write = &L2ARC_free_on_write;
	list_create(l2arc_dev_list, sizeof (l2arc_dev_t),
	    offsetof(l2arc_dev_t, l2ad_node));
	list_create(l2arc_free_on_write, sizeof (l2arc_data_free_t),
	    offsetof(l2arc_data_free_t, l2df_list_node));
}

void
l2arc_fini(void)
{
	mutex_destroy(&l2arc_feed_thr_lock);
	cv_destroy(&l2arc_feed_thr_cv);
	mutex_destroy(&l2arc_rebuild_thr_lock);
	cv_destroy(&l2arc_rebuild_thr_cv);
	mutex_destroy(&l2arc_dev_mtx);
	mutex_destroy(&l2arc_free_on_write_mtx);

	list_destroy(l2arc_dev_list);
	list_destroy(l2arc_free_on_write);
}

void
l2arc_start(void)
{
	if (!(spa_mode_global & SPA_MODE_WRITE))
		return;

	(void) thread_create(NULL, 0, l2arc_feed_thread, NULL, 0, &p0,
	    TS_RUN, defclsyspri);
}

void
l2arc_stop(void)
{
	if (!(spa_mode_global & SPA_MODE_WRITE))
		return;

	mutex_enter(&l2arc_feed_thr_lock);
	cv_signal(&l2arc_feed_thr_cv);	/* kick thread out of startup */
	l2arc_thread_exit = 1;
	while (l2arc_thread_exit != 0)
		cv_wait(&l2arc_feed_thr_cv, &l2arc_feed_thr_lock);
	mutex_exit(&l2arc_feed_thr_lock);
}

/*
 * Punches out rebuild threads for the L2ARC devices in a spa. This should
 * be called after pool import from the spa async thread, since starting
 * these threads directly from spa_import() will make them part of the
 * "zpool import" context and delay process exit (and thus pool import).
 */
void
l2arc_spa_rebuild_start(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	/*
	 * Locate the spa's l2arc devices and kick off rebuild threads.
	 */
	for (int i = 0; i < spa->spa_l2cache.sav_count; i++) {
		l2arc_dev_t *dev =
		    l2arc_vdev_get(spa->spa_l2cache.sav_vdevs[i]);
		if (dev == NULL) {
			/* Don't attempt a rebuild if the vdev is UNAVAIL */
			continue;
		}
		mutex_enter(&l2arc_rebuild_thr_lock);
		if (dev->l2ad_rebuild && !dev->l2ad_rebuild_cancel) {
			dev->l2ad_rebuild_began = B_TRUE;
			(void) thread_create(NULL, 0, l2arc_dev_rebuild_thread,
			    dev, 0, &p0, TS_RUN, minclsyspri);
		}
		mutex_exit(&l2arc_rebuild_thr_lock);
	}
}

/*
 * Main entry point for L2ARC rebuilding.
 */
static void
l2arc_dev_rebuild_thread(void *arg)
{
	l2arc_dev_t *dev = arg;

	VERIFY(!dev->l2ad_rebuild_cancel);
	VERIFY(dev->l2ad_rebuild);
	(void) l2arc_rebuild(dev);
	mutex_enter(&l2arc_rebuild_thr_lock);
	dev->l2ad_rebuild_began = B_FALSE;
	dev->l2ad_rebuild = B_FALSE;
	mutex_exit(&l2arc_rebuild_thr_lock);

	thread_exit();
}

/*
 * This function implements the actual L2ARC metadata rebuild. It:
 * starts reading the log block chain and restores each block's contents
 * to memory (reconstructing arc_buf_hdr_t's).
 *
 * Operation stops under any of the following conditions:
 *
 * 1) We reach the end of the log block chain.
 * 2) We encounter *any* error condition (cksum errors, io errors)
 */
static int
l2arc_rebuild(l2arc_dev_t *dev)
{
	vdev_t			*vd = dev->l2ad_vdev;
	spa_t			*spa = vd->vdev_spa;
	int			err = 0;
	l2arc_dev_hdr_phys_t	*l2dhdr = dev->l2ad_dev_hdr;
	l2arc_log_blk_phys_t	*this_lb, *next_lb;
	zio_t			*this_io = NULL, *next_io = NULL;
	l2arc_log_blkptr_t	lbps[2];
	l2arc_lb_ptr_buf_t	*lb_ptr_buf;
	boolean_t		lock_held;

	this_lb = vmem_zalloc(sizeof (*this_lb), KM_SLEEP);
	next_lb = vmem_zalloc(sizeof (*next_lb), KM_SLEEP);

	/*
	 * We prevent device removal while issuing reads to the device,
	 * then during the rebuilding phases we drop this lock again so
	 * that a spa_unload or device remove can be initiated - this is
	 * safe, because the spa will signal us to stop before removing
	 * our device and wait for us to stop.
	 */
	spa_config_enter(spa, SCL_L2ARC, vd, RW_READER);
	lock_held = B_TRUE;

	/*
	 * Retrieve the persistent L2ARC device state.
	 * L2BLK_GET_PSIZE returns aligned size for log blocks.
	 */
	dev->l2ad_evict = MAX(l2dhdr->dh_evict, dev->l2ad_start);
	dev->l2ad_hand = MAX(l2dhdr->dh_start_lbps[0].lbp_daddr +
	    L2BLK_GET_PSIZE((&l2dhdr->dh_start_lbps[0])->lbp_prop),
	    dev->l2ad_start);
	dev->l2ad_first = !!(l2dhdr->dh_flags & L2ARC_DEV_HDR_EVICT_FIRST);

	vd->vdev_trim_action_time = l2dhdr->dh_trim_action_time;
	vd->vdev_trim_state = l2dhdr->dh_trim_state;

	/*
	 * In case the zfs module parameter l2arc_rebuild_enabled is false
	 * we do not start the rebuild process.
	 */
	if (!l2arc_rebuild_enabled)
		goto out;

	/* Prepare the rebuild process */
	bcopy(l2dhdr->dh_start_lbps, lbps, sizeof (lbps));

	/* Start the rebuild process */
	for (;;) {
		if (!l2arc_log_blkptr_valid(dev, &lbps[0]))
			break;

		if ((err = l2arc_log_blk_read(dev, &lbps[0], &lbps[1],
		    this_lb, next_lb, this_io, &next_io)) != 0)
			goto out;

		/*
		 * Our memory pressure valve. If the system is running low
		 * on memory, rather than swamping memory with new ARC buf
		 * hdrs, we opt not to rebuild the L2ARC. At this point,
		 * however, we have already set up our L2ARC dev to chain in
		 * new metadata log blocks, so the user may choose to offline/
		 * online the L2ARC dev at a later time (or re-import the pool)
		 * to reconstruct it (when there's less memory pressure).
		 */
		if (l2arc_hdr_limit_reached()) {
			ARCSTAT_BUMP(arcstat_l2_rebuild_abort_lowmem);
			cmn_err(CE_NOTE, "System running low on memory, "
			    "aborting L2ARC rebuild.");
			err = SET_ERROR(ENOMEM);
			goto out;
		}

		spa_config_exit(spa, SCL_L2ARC, vd);
		lock_held = B_FALSE;

		/*
		 * Now that we know that the next_lb checks out alright, we
		 * can start reconstruction from this log block.
		 * L2BLK_GET_PSIZE returns aligned size for log blocks.
		 */
		uint64_t asize = L2BLK_GET_PSIZE((&lbps[0])->lbp_prop);
		l2arc_log_blk_restore(dev, this_lb, asize);

		/*
		 * log block restored, include its pointer in the list of
		 * pointers to log blocks present in the L2ARC device.
		 */
		lb_ptr_buf = kmem_zalloc(sizeof (l2arc_lb_ptr_buf_t), KM_SLEEP);
		lb_ptr_buf->lb_ptr = kmem_zalloc(sizeof (l2arc_log_blkptr_t),
		    KM_SLEEP);
		bcopy(&lbps[0], lb_ptr_buf->lb_ptr,
		    sizeof (l2arc_log_blkptr_t));
		mutex_enter(&dev->l2ad_mtx);
		list_insert_tail(&dev->l2ad_lbptr_list, lb_ptr_buf);
		ARCSTAT_INCR(arcstat_l2_log_blk_asize, asize);
		ARCSTAT_BUMP(arcstat_l2_log_blk_count);
		zfs_refcount_add_many(&dev->l2ad_lb_asize, asize, lb_ptr_buf);
		zfs_refcount_add(&dev->l2ad_lb_count, lb_ptr_buf);
		mutex_exit(&dev->l2ad_mtx);
		vdev_space_update(vd, asize, 0, 0);

		/*
		 * Protection against loops of log blocks:
		 *
		 *				       l2ad_hand  l2ad_evict
		 *                                         V	      V
		 * l2ad_start |=======================================| l2ad_end
		 *             -----|||----|||---|||----|||
		 *                  (3)    (2)   (1)    (0)
		 *             ---|||---|||----|||---|||
		 *		  (7)   (6)    (5)   (4)
		 *
		 * In this situation the pointer of log block (4) passes
		 * l2arc_log_blkptr_valid() but the log block should not be
		 * restored as it is overwritten by the payload of log block
		 * (0). Only log blocks (0)-(3) should be restored. We check
		 * whether l2ad_evict lies in between the payload starting
		 * offset of the next log block (lbps[1].lbp_payload_start)
		 * and the payload starting offset of the present log block
		 * (lbps[0].lbp_payload_start). If true and this isn't the
		 * first pass, we are looping from the beginning and we should
		 * stop.
		 */
		if (l2arc_range_check_overlap(lbps[1].lbp_payload_start,
		    lbps[0].lbp_payload_start, dev->l2ad_evict) &&
		    !dev->l2ad_first)
			goto out;

		cond_resched();
		for (;;) {
			mutex_enter(&l2arc_rebuild_thr_lock);
			if (dev->l2ad_rebuild_cancel) {
				dev->l2ad_rebuild = B_FALSE;
				cv_signal(&l2arc_rebuild_thr_cv);
				mutex_exit(&l2arc_rebuild_thr_lock);
				err = SET_ERROR(ECANCELED);
				goto out;
			}
			mutex_exit(&l2arc_rebuild_thr_lock);
			if (spa_config_tryenter(spa, SCL_L2ARC, vd,
			    RW_READER)) {
				lock_held = B_TRUE;
				break;
			}
			/*
			 * L2ARC config lock held by somebody in writer,
			 * possibly due to them trying to remove us. They'll
			 * likely to want us to shut down, so after a little
			 * delay, we check l2ad_rebuild_cancel and retry
			 * the lock again.
			 */
			delay(1);
		}

		/*
		 * Continue with the next log block.
		 */
		lbps[0] = lbps[1];
		lbps[1] = this_lb->lb_prev_lbp;
		PTR_SWAP(this_lb, next_lb);
		this_io = next_io;
		next_io = NULL;
	}

	if (this_io != NULL)
		l2arc_log_blk_fetch_abort(this_io);
out:
	if (next_io != NULL)
		l2arc_log_blk_fetch_abort(next_io);
	vmem_free(this_lb, sizeof (*this_lb));
	vmem_free(next_lb, sizeof (*next_lb));

	if (!l2arc_rebuild_enabled) {
		spa_history_log_internal(spa, "L2ARC rebuild", NULL,
		    "disabled");
	} else if (err == 0 && zfs_refcount_count(&dev->l2ad_lb_count) > 0) {
		ARCSTAT_BUMP(arcstat_l2_rebuild_success);
		spa_history_log_internal(spa, "L2ARC rebuild", NULL,
		    "successful, restored %llu blocks",
		    (u_longlong_t)zfs_refcount_count(&dev->l2ad_lb_count));
	} else if (err == 0 && zfs_refcount_count(&dev->l2ad_lb_count) == 0) {
		/*
		 * No error but also nothing restored, meaning the lbps array
		 * in the device header points to invalid/non-present log
		 * blocks. Reset the header.
		 */
		spa_history_log_internal(spa, "L2ARC rebuild", NULL,
		    "no valid log blocks");
		bzero(l2dhdr, dev->l2ad_dev_hdr_asize);
		l2arc_dev_hdr_update(dev);
	} else if (err == ECANCELED) {
		/*
		 * In case the rebuild was canceled do not log to spa history
		 * log as the pool may be in the process of being removed.
		 */
		zfs_dbgmsg("L2ARC rebuild aborted, restored %llu blocks",
		    zfs_refcount_count(&dev->l2ad_lb_count));
	} else if (err != 0) {
		spa_history_log_internal(spa, "L2ARC rebuild", NULL,
		    "aborted, restored %llu blocks",
		    (u_longlong_t)zfs_refcount_count(&dev->l2ad_lb_count));
	}

	if (lock_held)
		spa_config_exit(spa, SCL_L2ARC, vd);

	return (err);
}

/*
 * Attempts to read the device header on the provided L2ARC device and writes
 * it to `hdr'. On success, this function returns 0, otherwise the appropriate
 * error code is returned.
 */
static int
l2arc_dev_hdr_read(l2arc_dev_t *dev)
{
	int			err;
	uint64_t		guid;
	l2arc_dev_hdr_phys_t	*l2dhdr = dev->l2ad_dev_hdr;
	const uint64_t		l2dhdr_asize = dev->l2ad_dev_hdr_asize;
	abd_t 			*abd;

	guid = spa_guid(dev->l2ad_vdev->vdev_spa);

	abd = abd_get_from_buf(l2dhdr, l2dhdr_asize);

	err = zio_wait(zio_read_phys(NULL, dev->l2ad_vdev,
	    VDEV_LABEL_START_SIZE, l2dhdr_asize, abd,
	    ZIO_CHECKSUM_LABEL, NULL, NULL, ZIO_PRIORITY_SYNC_READ,
	    ZIO_FLAG_DONT_CACHE | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY |
	    ZIO_FLAG_SPECULATIVE, B_FALSE));

	abd_put(abd);

	if (err != 0) {
		ARCSTAT_BUMP(arcstat_l2_rebuild_abort_dh_errors);
		zfs_dbgmsg("L2ARC IO error (%d) while reading device header, "
		    "vdev guid: %llu", err, dev->l2ad_vdev->vdev_guid);
		return (err);
	}

	if (l2dhdr->dh_magic == BSWAP_64(L2ARC_DEV_HDR_MAGIC))
		byteswap_uint64_array(l2dhdr, sizeof (*l2dhdr));

	if (l2dhdr->dh_magic != L2ARC_DEV_HDR_MAGIC ||
	    l2dhdr->dh_spa_guid != guid ||
	    l2dhdr->dh_vdev_guid != dev->l2ad_vdev->vdev_guid ||
	    l2dhdr->dh_version != L2ARC_PERSISTENT_VERSION ||
	    l2dhdr->dh_log_entries != dev->l2ad_log_entries ||
	    l2dhdr->dh_end != dev->l2ad_end ||
	    !l2arc_range_check_overlap(dev->l2ad_start, dev->l2ad_end,
	    l2dhdr->dh_evict) ||
	    (l2dhdr->dh_trim_state != VDEV_TRIM_COMPLETE &&
	    l2arc_trim_ahead > 0)) {
		/*
		 * Attempt to rebuild a device containing no actual dev hdr
		 * or containing a header from some other pool or from another
		 * version of persistent L2ARC.
		 */
		ARCSTAT_BUMP(arcstat_l2_rebuild_abort_unsupported);
		return (SET_ERROR(ENOTSUP));
	}

	return (0);
}

/*
 * Reads L2ARC log blocks from storage and validates their contents.
 *
 * This function implements a simple fetcher to make sure that while
 * we're processing one buffer the L2ARC is already fetching the next
 * one in the chain.
 *
 * The arguments this_lp and next_lp point to the current and next log block
 * address in the block chain. Similarly, this_lb and next_lb hold the
 * l2arc_log_blk_phys_t's of the current and next L2ARC blk.
 *
 * The `this_io' and `next_io' arguments are used for block fetching.
 * When issuing the first blk IO during rebuild, you should pass NULL for
 * `this_io'. This function will then issue a sync IO to read the block and
 * also issue an async IO to fetch the next block in the block chain. The
 * fetched IO is returned in `next_io'. On subsequent calls to this
 * function, pass the value returned in `next_io' from the previous call
 * as `this_io' and a fresh `next_io' pointer to hold the next fetch IO.
 * Prior to the call, you should initialize your `next_io' pointer to be
 * NULL. If no fetch IO was issued, the pointer is left set at NULL.
 *
 * On success, this function returns 0, otherwise it returns an appropriate
 * error code. On error the fetching IO is aborted and cleared before
 * returning from this function. Therefore, if we return `success', the
 * caller can assume that we have taken care of cleanup of fetch IOs.
 */
static int
l2arc_log_blk_read(l2arc_dev_t *dev,
    const l2arc_log_blkptr_t *this_lbp, const l2arc_log_blkptr_t *next_lbp,
    l2arc_log_blk_phys_t *this_lb, l2arc_log_blk_phys_t *next_lb,
    zio_t *this_io, zio_t **next_io)
{
	int		err = 0;
	zio_cksum_t	cksum;
	abd_t		*abd = NULL;
	uint64_t	asize;

	ASSERT(this_lbp != NULL && next_lbp != NULL);
	ASSERT(this_lb != NULL && next_lb != NULL);
	ASSERT(next_io != NULL && *next_io == NULL);
	ASSERT(l2arc_log_blkptr_valid(dev, this_lbp));

	/*
	 * Check to see if we have issued the IO for this log block in a
	 * previous run. If not, this is the first call, so issue it now.
	 */
	if (this_io == NULL) {
		this_io = l2arc_log_blk_fetch(dev->l2ad_vdev, this_lbp,
		    this_lb);
	}

	/*
	 * Peek to see if we can start issuing the next IO immediately.
	 */
	if (l2arc_log_blkptr_valid(dev, next_lbp)) {
		/*
		 * Start issuing IO for the next log block early - this
		 * should help keep the L2ARC device busy while we
		 * decompress and restore this log block.
		 */
		*next_io = l2arc_log_blk_fetch(dev->l2ad_vdev, next_lbp,
		    next_lb);
	}

	/* Wait for the IO to read this log block to complete */
	if ((err = zio_wait(this_io)) != 0) {
		ARCSTAT_BUMP(arcstat_l2_rebuild_abort_io_errors);
		zfs_dbgmsg("L2ARC IO error (%d) while reading log block, "
		    "offset: %llu, vdev guid: %llu", err, this_lbp->lbp_daddr,
		    dev->l2ad_vdev->vdev_guid);
		goto cleanup;
	}

	/*
	 * Make sure the buffer checks out.
	 * L2BLK_GET_PSIZE returns aligned size for log blocks.
	 */
	asize = L2BLK_GET_PSIZE((this_lbp)->lbp_prop);
	fletcher_4_native(this_lb, asize, NULL, &cksum);
	if (!ZIO_CHECKSUM_EQUAL(cksum, this_lbp->lbp_cksum)) {
		ARCSTAT_BUMP(arcstat_l2_rebuild_abort_cksum_lb_errors);
		zfs_dbgmsg("L2ARC log block cksum failed, offset: %llu, "
		    "vdev guid: %llu, l2ad_hand: %llu, l2ad_evict: %llu",
		    this_lbp->lbp_daddr, dev->l2ad_vdev->vdev_guid,
		    dev->l2ad_hand, dev->l2ad_evict);
		err = SET_ERROR(ECKSUM);
		goto cleanup;
	}

	/* Now we can take our time decoding this buffer */
	switch (L2BLK_GET_COMPRESS((this_lbp)->lbp_prop)) {
	case ZIO_COMPRESS_OFF:
		break;
	case ZIO_COMPRESS_LZ4:
		abd = abd_alloc_for_io(asize, B_TRUE);
		abd_copy_from_buf_off(abd, this_lb, 0, asize);
		if ((err = zio_decompress_data(
		    L2BLK_GET_COMPRESS((this_lbp)->lbp_prop),
		    abd, this_lb, asize, sizeof (*this_lb), NULL)) != 0) {
			err = SET_ERROR(EINVAL);
			goto cleanup;
		}
		break;
	default:
		err = SET_ERROR(EINVAL);
		goto cleanup;
	}
	if (this_lb->lb_magic == BSWAP_64(L2ARC_LOG_BLK_MAGIC))
		byteswap_uint64_array(this_lb, sizeof (*this_lb));
	if (this_lb->lb_magic != L2ARC_LOG_BLK_MAGIC) {
		err = SET_ERROR(EINVAL);
		goto cleanup;
	}
cleanup:
	/* Abort an in-flight fetch I/O in case of error */
	if (err != 0 && *next_io != NULL) {
		l2arc_log_blk_fetch_abort(*next_io);
		*next_io = NULL;
	}
	if (abd != NULL)
		abd_free(abd);
	return (err);
}

/*
 * Restores the payload of a log block to ARC. This creates empty ARC hdr
 * entries which only contain an l2arc hdr, essentially restoring the
 * buffers to their L2ARC evicted state. This function also updates space
 * usage on the L2ARC vdev to make sure it tracks restored buffers.
 */
static void
l2arc_log_blk_restore(l2arc_dev_t *dev, const l2arc_log_blk_phys_t *lb,
    uint64_t lb_asize)
{
	uint64_t	size = 0, asize = 0;
	uint64_t	log_entries = dev->l2ad_log_entries;

	/*
	 * Usually arc_adapt() is called only for data, not headers, but
	 * since we may allocate significant amount of memory here, let ARC
	 * grow its arc_c.
	 */
	arc_adapt(log_entries * HDR_L2ONLY_SIZE, arc_l2c_only);

	for (int i = log_entries - 1; i >= 0; i--) {
		/*
		 * Restore goes in the reverse temporal direction to preserve
		 * correct temporal ordering of buffers in the l2ad_buflist.
		 * l2arc_hdr_restore also does a list_insert_tail instead of
		 * list_insert_head on the l2ad_buflist:
		 *
		 *		LIST	l2ad_buflist		LIST
		 *		HEAD  <------ (time) ------	TAIL
		 * direction	+-----+-----+-----+-----+-----+    direction
		 * of l2arc <== | buf | buf | buf | buf | buf | ===> of rebuild
		 * fill		+-----+-----+-----+-----+-----+
		 *		^				^
		 *		|				|
		 *		|				|
		 *	l2arc_feed_thread		l2arc_rebuild
		 *	will place new bufs here	restores bufs here
		 *
		 * During l2arc_rebuild() the device is not used by
		 * l2arc_feed_thread() as dev->l2ad_rebuild is set to true.
		 */
		size += L2BLK_GET_LSIZE((&lb->lb_entries[i])->le_prop);
		asize += vdev_psize_to_asize(dev->l2ad_vdev,
		    L2BLK_GET_PSIZE((&lb->lb_entries[i])->le_prop));
		l2arc_hdr_restore(&lb->lb_entries[i], dev);
	}

	/*
	 * Record rebuild stats:
	 *	size		Logical size of restored buffers in the L2ARC
	 *	asize		Aligned size of restored buffers in the L2ARC
	 */
	ARCSTAT_INCR(arcstat_l2_rebuild_size, size);
	ARCSTAT_INCR(arcstat_l2_rebuild_asize, asize);
	ARCSTAT_INCR(arcstat_l2_rebuild_bufs, log_entries);
	ARCSTAT_F_AVG(arcstat_l2_log_blk_avg_asize, lb_asize);
	ARCSTAT_F_AVG(arcstat_l2_data_to_meta_ratio, asize / lb_asize);
	ARCSTAT_BUMP(arcstat_l2_rebuild_log_blks);
}

/*
 * Restores a single ARC buf hdr from a log entry. The ARC buffer is put
 * into a state indicating that it has been evicted to L2ARC.
 */
static void
l2arc_hdr_restore(const l2arc_log_ent_phys_t *le, l2arc_dev_t *dev)
{
	arc_buf_hdr_t		*hdr, *exists;
	kmutex_t		*hash_lock;
	arc_buf_contents_t	type = L2BLK_GET_TYPE((le)->le_prop);
	uint64_t		asize;

	/*
	 * Do all the allocation before grabbing any locks, this lets us
	 * sleep if memory is full and we don't have to deal with failed
	 * allocations.
	 */
	hdr = arc_buf_alloc_l2only(L2BLK_GET_LSIZE((le)->le_prop), type,
	    dev, le->le_dva, le->le_daddr,
	    L2BLK_GET_PSIZE((le)->le_prop), le->le_birth,
	    L2BLK_GET_COMPRESS((le)->le_prop), le->le_complevel,
	    L2BLK_GET_PROTECTED((le)->le_prop),
	    L2BLK_GET_PREFETCH((le)->le_prop),
	    L2BLK_GET_STATE((le)->le_prop));
	asize = vdev_psize_to_asize(dev->l2ad_vdev,
	    L2BLK_GET_PSIZE((le)->le_prop));

	/*
	 * vdev_space_update() has to be called before arc_hdr_destroy() to
	 * avoid underflow since the latter also calls vdev_space_update().
	 */
	l2arc_hdr_arcstats_increment(hdr);
	vdev_space_update(dev->l2ad_vdev, asize, 0, 0);

	mutex_enter(&dev->l2ad_mtx);
	list_insert_tail(&dev->l2ad_buflist, hdr);
	(void) zfs_refcount_add_many(&dev->l2ad_alloc, arc_hdr_size(hdr), hdr);
	mutex_exit(&dev->l2ad_mtx);

	exists = buf_hash_insert(hdr, &hash_lock);
	if (exists) {
		/* Buffer was already cached, no need to restore it. */
		arc_hdr_destroy(hdr);
		/*
		 * If the buffer is already cached, check whether it has
		 * L2ARC metadata. If not, enter them and update the flag.
		 * This is important is case of onlining a cache device, since
		 * we previously evicted all L2ARC metadata from ARC.
		 */
		if (!HDR_HAS_L2HDR(exists)) {
			arc_hdr_set_flags(exists, ARC_FLAG_HAS_L2HDR);
			exists->b_l2hdr.b_dev = dev;
			exists->b_l2hdr.b_daddr = le->le_daddr;
			exists->b_l2hdr.b_arcs_state =
			    L2BLK_GET_STATE((le)->le_prop);
			mutex_enter(&dev->l2ad_mtx);
			list_insert_tail(&dev->l2ad_buflist, exists);
			(void) zfs_refcount_add_many(&dev->l2ad_alloc,
			    arc_hdr_size(exists), exists);
			mutex_exit(&dev->l2ad_mtx);
			l2arc_hdr_arcstats_increment(exists);
			vdev_space_update(dev->l2ad_vdev, asize, 0, 0);
		}
		ARCSTAT_BUMP(arcstat_l2_rebuild_bufs_precached);
	}

	mutex_exit(hash_lock);
}

/*
 * Starts an asynchronous read IO to read a log block. This is used in log
 * block reconstruction to start reading the next block before we are done
 * decoding and reconstructing the current block, to keep the l2arc device
 * nice and hot with read IO to process.
 * The returned zio will contain a newly allocated memory buffers for the IO
 * data which should then be freed by the caller once the zio is no longer
 * needed (i.e. due to it having completed). If you wish to abort this
 * zio, you should do so using l2arc_log_blk_fetch_abort, which takes
 * care of disposing of the allocated buffers correctly.
 */
static zio_t *
l2arc_log_blk_fetch(vdev_t *vd, const l2arc_log_blkptr_t *lbp,
    l2arc_log_blk_phys_t *lb)
{
	uint32_t		asize;
	zio_t			*pio;
	l2arc_read_callback_t	*cb;

	/* L2BLK_GET_PSIZE returns aligned size for log blocks */
	asize = L2BLK_GET_PSIZE((lbp)->lbp_prop);
	ASSERT(asize <= sizeof (l2arc_log_blk_phys_t));

	cb = kmem_zalloc(sizeof (l2arc_read_callback_t), KM_SLEEP);
	cb->l2rcb_abd = abd_get_from_buf(lb, asize);
	pio = zio_root(vd->vdev_spa, l2arc_blk_fetch_done, cb,
	    ZIO_FLAG_DONT_CACHE | ZIO_FLAG_CANFAIL | ZIO_FLAG_DONT_PROPAGATE |
	    ZIO_FLAG_DONT_RETRY);
	(void) zio_nowait(zio_read_phys(pio, vd, lbp->lbp_daddr, asize,
	    cb->l2rcb_abd, ZIO_CHECKSUM_OFF, NULL, NULL,
	    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_DONT_CACHE | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY, B_FALSE));

	return (pio);
}

/*
 * Aborts a zio returned from l2arc_log_blk_fetch and frees the data
 * buffers allocated for it.
 */
static void
l2arc_log_blk_fetch_abort(zio_t *zio)
{
	(void) zio_wait(zio);
}

/*
 * Creates a zio to update the device header on an l2arc device.
 */
void
l2arc_dev_hdr_update(l2arc_dev_t *dev)
{
	l2arc_dev_hdr_phys_t	*l2dhdr = dev->l2ad_dev_hdr;
	const uint64_t		l2dhdr_asize = dev->l2ad_dev_hdr_asize;
	abd_t			*abd;
	int			err;

	VERIFY(spa_config_held(dev->l2ad_spa, SCL_STATE_ALL, RW_READER));

	l2dhdr->dh_magic = L2ARC_DEV_HDR_MAGIC;
	l2dhdr->dh_version = L2ARC_PERSISTENT_VERSION;
	l2dhdr->dh_spa_guid = spa_guid(dev->l2ad_vdev->vdev_spa);
	l2dhdr->dh_vdev_guid = dev->l2ad_vdev->vdev_guid;
	l2dhdr->dh_log_entries = dev->l2ad_log_entries;
	l2dhdr->dh_evict = dev->l2ad_evict;
	l2dhdr->dh_start = dev->l2ad_start;
	l2dhdr->dh_end = dev->l2ad_end;
	l2dhdr->dh_lb_asize = zfs_refcount_count(&dev->l2ad_lb_asize);
	l2dhdr->dh_lb_count = zfs_refcount_count(&dev->l2ad_lb_count);
	l2dhdr->dh_flags = 0;
	l2dhdr->dh_trim_action_time = dev->l2ad_vdev->vdev_trim_action_time;
	l2dhdr->dh_trim_state = dev->l2ad_vdev->vdev_trim_state;
	if (dev->l2ad_first)
		l2dhdr->dh_flags |= L2ARC_DEV_HDR_EVICT_FIRST;

	abd = abd_get_from_buf(l2dhdr, l2dhdr_asize);

	err = zio_wait(zio_write_phys(NULL, dev->l2ad_vdev,
	    VDEV_LABEL_START_SIZE, l2dhdr_asize, abd, ZIO_CHECKSUM_LABEL, NULL,
	    NULL, ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_CANFAIL, B_FALSE));

	abd_put(abd);

	if (err != 0) {
		zfs_dbgmsg("L2ARC IO error (%d) while writing device header, "
		    "vdev guid: %llu", err, dev->l2ad_vdev->vdev_guid);
	}
}

/*
 * Commits a log block to the L2ARC device. This routine is invoked from
 * l2arc_write_buffers when the log block fills up.
 * This function allocates some memory to temporarily hold the serialized
 * buffer to be written. This is then released in l2arc_write_done.
 */
static void
l2arc_log_blk_commit(l2arc_dev_t *dev, zio_t *pio, l2arc_write_callback_t *cb)
{
	l2arc_log_blk_phys_t	*lb = &dev->l2ad_log_blk;
	l2arc_dev_hdr_phys_t	*l2dhdr = dev->l2ad_dev_hdr;
	uint64_t		psize, asize;
	zio_t			*wzio;
	l2arc_lb_abd_buf_t	*abd_buf;
	uint8_t			*tmpbuf;
	l2arc_lb_ptr_buf_t	*lb_ptr_buf;

	VERIFY3S(dev->l2ad_log_ent_idx, ==, dev->l2ad_log_entries);

	tmpbuf = zio_buf_alloc(sizeof (*lb));
	abd_buf = zio_buf_alloc(sizeof (*abd_buf));
	abd_buf->abd = abd_get_from_buf(lb, sizeof (*lb));
	lb_ptr_buf = kmem_zalloc(sizeof (l2arc_lb_ptr_buf_t), KM_SLEEP);
	lb_ptr_buf->lb_ptr = kmem_zalloc(sizeof (l2arc_log_blkptr_t), KM_SLEEP);

	/* link the buffer into the block chain */
	lb->lb_prev_lbp = l2dhdr->dh_start_lbps[1];
	lb->lb_magic = L2ARC_LOG_BLK_MAGIC;

	/*
	 * l2arc_log_blk_commit() may be called multiple times during a single
	 * l2arc_write_buffers() call. Save the allocated abd buffers in a list
	 * so we can free them in l2arc_write_done() later on.
	 */
	list_insert_tail(&cb->l2wcb_abd_list, abd_buf);

	/* try to compress the buffer */
	psize = zio_compress_data(ZIO_COMPRESS_LZ4,
	    abd_buf->abd, tmpbuf, sizeof (*lb), 0);

	/* a log block is never entirely zero */
	ASSERT(psize != 0);
	asize = vdev_psize_to_asize(dev->l2ad_vdev, psize);
	ASSERT(asize <= sizeof (*lb));

	/*
	 * Update the start log block pointer in the device header to point
	 * to the log block we're about to write.
	 */
	l2dhdr->dh_start_lbps[1] = l2dhdr->dh_start_lbps[0];
	l2dhdr->dh_start_lbps[0].lbp_daddr = dev->l2ad_hand;
	l2dhdr->dh_start_lbps[0].lbp_payload_asize =
	    dev->l2ad_log_blk_payload_asize;
	l2dhdr->dh_start_lbps[0].lbp_payload_start =
	    dev->l2ad_log_blk_payload_start;
	_NOTE(CONSTCOND)
	L2BLK_SET_LSIZE(
	    (&l2dhdr->dh_start_lbps[0])->lbp_prop, sizeof (*lb));
	L2BLK_SET_PSIZE(
	    (&l2dhdr->dh_start_lbps[0])->lbp_prop, asize);
	L2BLK_SET_CHECKSUM(
	    (&l2dhdr->dh_start_lbps[0])->lbp_prop,
	    ZIO_CHECKSUM_FLETCHER_4);
	if (asize < sizeof (*lb)) {
		/* compression succeeded */
		bzero(tmpbuf + psize, asize - psize);
		L2BLK_SET_COMPRESS(
		    (&l2dhdr->dh_start_lbps[0])->lbp_prop,
		    ZIO_COMPRESS_LZ4);
	} else {
		/* compression failed */
		bcopy(lb, tmpbuf, sizeof (*lb));
		L2BLK_SET_COMPRESS(
		    (&l2dhdr->dh_start_lbps[0])->lbp_prop,
		    ZIO_COMPRESS_OFF);
	}

	/* checksum what we're about to write */
	fletcher_4_native(tmpbuf, asize, NULL,
	    &l2dhdr->dh_start_lbps[0].lbp_cksum);

	abd_put(abd_buf->abd);

	/* perform the write itself */
	abd_buf->abd = abd_get_from_buf(tmpbuf, sizeof (*lb));
	abd_take_ownership_of_buf(abd_buf->abd, B_TRUE);
	wzio = zio_write_phys(pio, dev->l2ad_vdev, dev->l2ad_hand,
	    asize, abd_buf->abd, ZIO_CHECKSUM_OFF, NULL, NULL,
	    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_CANFAIL, B_FALSE);
	DTRACE_PROBE2(l2arc__write, vdev_t *, dev->l2ad_vdev, zio_t *, wzio);
	(void) zio_nowait(wzio);

	dev->l2ad_hand += asize;
	/*
	 * Include the committed log block's pointer  in the list of pointers
	 * to log blocks present in the L2ARC device.
	 */
	bcopy(&l2dhdr->dh_start_lbps[0], lb_ptr_buf->lb_ptr,
	    sizeof (l2arc_log_blkptr_t));
	mutex_enter(&dev->l2ad_mtx);
	list_insert_head(&dev->l2ad_lbptr_list, lb_ptr_buf);
	ARCSTAT_INCR(arcstat_l2_log_blk_asize, asize);
	ARCSTAT_BUMP(arcstat_l2_log_blk_count);
	zfs_refcount_add_many(&dev->l2ad_lb_asize, asize, lb_ptr_buf);
	zfs_refcount_add(&dev->l2ad_lb_count, lb_ptr_buf);
	mutex_exit(&dev->l2ad_mtx);
	vdev_space_update(dev->l2ad_vdev, asize, 0, 0);

	/* bump the kstats */
	ARCSTAT_INCR(arcstat_l2_write_bytes, asize);
	ARCSTAT_BUMP(arcstat_l2_log_blk_writes);
	ARCSTAT_F_AVG(arcstat_l2_log_blk_avg_asize, asize);
	ARCSTAT_F_AVG(arcstat_l2_data_to_meta_ratio,
	    dev->l2ad_log_blk_payload_asize / asize);

	/* start a new log block */
	dev->l2ad_log_ent_idx = 0;
	dev->l2ad_log_blk_payload_asize = 0;
	dev->l2ad_log_blk_payload_start = 0;
}

/*
 * Validates an L2ARC log block address to make sure that it can be read
 * from the provided L2ARC device.
 */
boolean_t
l2arc_log_blkptr_valid(l2arc_dev_t *dev, const l2arc_log_blkptr_t *lbp)
{
	/* L2BLK_GET_PSIZE returns aligned size for log blocks */
	uint64_t asize = L2BLK_GET_PSIZE((lbp)->lbp_prop);
	uint64_t end = lbp->lbp_daddr + asize - 1;
	uint64_t start = lbp->lbp_payload_start;
	boolean_t evicted = B_FALSE;

	/*
	 * A log block is valid if all of the following conditions are true:
	 * - it fits entirely (including its payload) between l2ad_start and
	 *   l2ad_end
	 * - it has a valid size
	 * - neither the log block itself nor part of its payload was evicted
	 *   by l2arc_evict():
	 *
	 *		l2ad_hand          l2ad_evict
	 *		|			 |	lbp_daddr
	 *		|     start		 |	|  end
	 *		|     |			 |	|  |
	 *		V     V		         V	V  V
	 *   l2ad_start ============================================ l2ad_end
	 *                    --------------------------||||
	 *				^		 ^
	 *				|		log block
	 *				payload
	 */

	evicted =
	    l2arc_range_check_overlap(start, end, dev->l2ad_hand) ||
	    l2arc_range_check_overlap(start, end, dev->l2ad_evict) ||
	    l2arc_range_check_overlap(dev->l2ad_hand, dev->l2ad_evict, start) ||
	    l2arc_range_check_overlap(dev->l2ad_hand, dev->l2ad_evict, end);

	return (start >= dev->l2ad_start && end <= dev->l2ad_end &&
	    asize > 0 && asize <= sizeof (l2arc_log_blk_phys_t) &&
	    (!evicted || dev->l2ad_first));
}

/*
 * Inserts ARC buffer header `hdr' into the current L2ARC log block on
 * the device. The buffer being inserted must be present in L2ARC.
 * Returns B_TRUE if the L2ARC log block is full and needs to be committed
 * to L2ARC, or B_FALSE if it still has room for more ARC buffers.
 */
static boolean_t
l2arc_log_blk_insert(l2arc_dev_t *dev, const arc_buf_hdr_t *hdr)
{
	l2arc_log_blk_phys_t	*lb = &dev->l2ad_log_blk;
	l2arc_log_ent_phys_t	*le;

	if (dev->l2ad_log_entries == 0)
		return (B_FALSE);

	int index = dev->l2ad_log_ent_idx++;

	ASSERT3S(index, <, dev->l2ad_log_entries);
	ASSERT(HDR_HAS_L2HDR(hdr));

	le = &lb->lb_entries[index];
	bzero(le, sizeof (*le));
	le->le_dva = hdr->b_dva;
	le->le_birth = hdr->b_birth;
	le->le_daddr = hdr->b_l2hdr.b_daddr;
	if (index == 0)
		dev->l2ad_log_blk_payload_start = le->le_daddr;
	L2BLK_SET_LSIZE((le)->le_prop, HDR_GET_LSIZE(hdr));
	L2BLK_SET_PSIZE((le)->le_prop, HDR_GET_PSIZE(hdr));
	L2BLK_SET_COMPRESS((le)->le_prop, HDR_GET_COMPRESS(hdr));
	le->le_complevel = hdr->b_complevel;
	L2BLK_SET_TYPE((le)->le_prop, hdr->b_type);
	L2BLK_SET_PROTECTED((le)->le_prop, !!(HDR_PROTECTED(hdr)));
	L2BLK_SET_PREFETCH((le)->le_prop, !!(HDR_PREFETCH(hdr)));
	L2BLK_SET_STATE((le)->le_prop, hdr->b_l1hdr.b_state->arcs_state);

	dev->l2ad_log_blk_payload_asize += vdev_psize_to_asize(dev->l2ad_vdev,
	    HDR_GET_PSIZE(hdr));

	return (dev->l2ad_log_ent_idx == dev->l2ad_log_entries);
}

/*
 * Checks whether a given L2ARC device address sits in a time-sequential
 * range. The trick here is that the L2ARC is a rotary buffer, so we can't
 * just do a range comparison, we need to handle the situation in which the
 * range wraps around the end of the L2ARC device. Arguments:
 *	bottom -- Lower end of the range to check (written to earlier).
 *	top    -- Upper end of the range to check (written to later).
 *	check  -- The address for which we want to determine if it sits in
 *		  between the top and bottom.
 *
 * The 3-way conditional below represents the following cases:
 *
 *	bottom < top : Sequentially ordered case:
 *	  <check>--------+-------------------+
 *	                 |  (overlap here?)  |
 *	 L2ARC dev       V                   V
 *	 |---------------<bottom>============<top>--------------|
 *
 *	bottom > top: Looped-around case:
 *	                      <check>--------+------------------+
 *	                                     |  (overlap here?) |
 *	 L2ARC dev                           V                  V
 *	 |===============<top>---------------<bottom>===========|
 *	 ^               ^
 *	 |  (or here?)   |
 *	 +---------------+---------<check>
 *
 *	top == bottom : Just a single address comparison.
 */
boolean_t
l2arc_range_check_overlap(uint64_t bottom, uint64_t top, uint64_t check)
{
	if (bottom < top)
		return (bottom <= check && check <= top);
	else if (bottom > top)
		return (check <= top || bottom <= check);
	else
		return (check == top);
}

EXPORT_SYMBOL(arc_buf_size);
EXPORT_SYMBOL(arc_write);
EXPORT_SYMBOL(arc_read);
EXPORT_SYMBOL(arc_buf_info);
EXPORT_SYMBOL(arc_getbuf_func);
EXPORT_SYMBOL(arc_add_prune_callback);
EXPORT_SYMBOL(arc_remove_prune_callback);

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, min, param_set_arc_long,
	param_get_long, ZMOD_RW, "Min arc size");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, max, param_set_arc_long,
	param_get_long, ZMOD_RW, "Max arc size");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, meta_limit, param_set_arc_long,
	param_get_long, ZMOD_RW, "Metadata limit for arc size");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, meta_limit_percent,
	param_set_arc_long, param_get_long, ZMOD_RW,
	"Percent of arc size for arc meta limit");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, meta_min, param_set_arc_long,
	param_get_long, ZMOD_RW, "Min arc metadata");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, meta_prune, INT, ZMOD_RW,
	"Meta objects to scan for prune");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, meta_adjust_restarts, INT, ZMOD_RW,
	"Limit number of restarts in arc_evict_meta");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, meta_strategy, INT, ZMOD_RW,
	"Meta reclaim strategy");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, grow_retry, param_set_arc_int,
	param_get_int, ZMOD_RW, "Seconds before growing arc size");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, p_dampener_disable, INT, ZMOD_RW,
	"Disable arc_p adapt dampener");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, shrink_shift, param_set_arc_int,
	param_get_int, ZMOD_RW, "log2(fraction of arc to reclaim)");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, pc_percent, UINT, ZMOD_RW,
	"Percent of pagecache to reclaim arc to");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, p_min_shift, param_set_arc_int,
	param_get_int, ZMOD_RW, "arc_c shift to calc min/max arc_p");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, average_blocksize, INT, ZMOD_RD,
	"Target average block size");

ZFS_MODULE_PARAM(zfs, zfs_, compressed_arc_enabled, INT, ZMOD_RW,
	"Disable compressed arc buffers");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, min_prefetch_ms, param_set_arc_int,
	param_get_int, ZMOD_RW, "Min life of prefetch block in ms");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, min_prescient_prefetch_ms,
	param_set_arc_int, param_get_int, ZMOD_RW,
	"Min life of prescient prefetched block in ms");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, write_max, ULONG, ZMOD_RW,
	"Max write bytes per interval");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, write_boost, ULONG, ZMOD_RW,
	"Extra write bytes during device warmup");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, headroom, ULONG, ZMOD_RW,
	"Number of max device writes to precache");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, headroom_boost, ULONG, ZMOD_RW,
	"Compressed l2arc_headroom multiplier");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, trim_ahead, ULONG, ZMOD_RW,
	"TRIM ahead L2ARC write size multiplier");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, feed_secs, ULONG, ZMOD_RW,
	"Seconds between L2ARC writing");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, feed_min_ms, ULONG, ZMOD_RW,
	"Min feed interval in milliseconds");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, noprefetch, INT, ZMOD_RW,
	"Skip caching prefetched buffers");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, feed_again, INT, ZMOD_RW,
	"Turbo L2ARC warmup");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, norw, INT, ZMOD_RW,
	"No reads during writes");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, meta_percent, INT, ZMOD_RW,
	"Percent of ARC size allowed for L2ARC-only headers");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, rebuild_enabled, INT, ZMOD_RW,
	"Rebuild the L2ARC when importing a pool");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, rebuild_blocks_min_l2size, ULONG, ZMOD_RW,
	"Min size in bytes to write rebuild log blocks in L2ARC");

ZFS_MODULE_PARAM(zfs_l2arc, l2arc_, mfuonly, INT, ZMOD_RW,
	"Cache only MFU data from ARC into L2ARC");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, lotsfree_percent, param_set_arc_int,
	param_get_int, ZMOD_RW, "System free memory I/O throttle in bytes");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, sys_free, param_set_arc_long,
	param_get_long, ZMOD_RW, "System free memory target size in bytes");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, dnode_limit, param_set_arc_long,
	param_get_long, ZMOD_RW, "Minimum bytes of dnodes in arc");

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, dnode_limit_percent,
	param_set_arc_long, param_get_long, ZMOD_RW,
	"Percent of ARC meta buffers for dnodes");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, dnode_reduce_percent, ULONG, ZMOD_RW,
	"Percentage of excess dnodes to try to unpin");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, eviction_pct, INT, ZMOD_RW,
	"When full, ARC allocation waits for eviction of this % of alloc size");

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, evict_batch_limit, INT, ZMOD_RW,
	"The number of headers to evict per sublist before moving to the next");
/* END CSTYLED */
