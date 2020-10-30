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
 * Copyright (c) 2011, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2014 by Saso Kiselkov. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/spa_impl.h>
#include <sys/zio_compress.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/arc_impl.h>
#include <sys/zfs_refcount.h>
#include <sys/vdev.h>
#include <sys/vdev_trim.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_pool.h>
#include <sys/zio_checksum.h>
#include <sys/multilist.h>
#include <sys/abd.h>
#include <sys/zil.h>
#include <sys/fm/fs/zfs.h>
#ifdef _KERNEL
#include <sys/vmsystm.h>
#include <sys/zpl.h>
#endif
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/zthr.h>
#include <zfs_fletcher.h>
#include <sys/arc_impl.h>
#include <sys/trace_zfs.h>
#include <sys/aggsum.h>
#include <sys/kstat_windows.h>

extern arc_stats_t arc_stats;

static kmutex_t			arc_reclaim_lock;
static kcondvar_t		arc_reclaim_thread_cv;
static boolean_t		arc_reclaim_thread_exit;
static kcondvar_t		arc_reclaim_waiters_cv;

/*
 * log2(fraction of ARC which must be free to allow growing).
 * I.e. If there is less than arc_c >> arc_no_grow_shift free memory,
 * when reading a new block into the ARC, we will evict an equal-sized block
 * from the ARC.
 *
 * This must be less than arc_shrink_shift, so that when we shrink the ARC,
 * we will still not allow it to grow.
 */
extern int	arc_no_grow_shift;


/*
 * Return a default max arc size based on the amount of physical memory.
 */
uint64_t
arc_default_max(uint64_t min, uint64_t allmem)
{
	/* Default to 1/3 of all memory. */
	return (MAX(allmem / 3, min));
}

#ifdef _KERNEL

/* Remove these uses of _Atomic */
static _Atomic boolean_t arc_reclaim_in_loop = B_FALSE;
static _Atomic int64_t reclaim_shrink_target = 0;

/*
 * Return maximum amount of memory that we could possibly use.  Reduced
 * to half of all memory in user space which is primarily used for testing.
 */
uint64_t
arc_all_memory(void)
{
	return (kmem_size());
}

/*
 * Return the amount of memory that is considered free.  In user space
 * which is primarily used for testing we pretend that free memory ranges
 * from 0-20% of all memory.
 */
uint64_t
arc_free_memory(void)
{
	int64_t avail;

	avail = spl_free_wrapper();
	return (avail >= 0LL ? avail : 0LL);
}

/*
 * Return the amount of memory that can be consumed before reclaim will be
 * needed.  Positive if there is sufficient free memory, negative indicates
 * the amount of memory that needs to be freed up.
 */
int64_t
arc_available_memory(void)
{
	return (arc_free_memory() - arc_sys_free);
}

int
arc_memory_throttle(spa_t *spa, uint64_t reserve, uint64_t txg)
{
	int64_t available_memory = spl_free_wrapper();
	int64_t freemem = available_memory / PAGESIZE;
	static uint64_t page_load = 0;
	static uint64_t last_txg = 0;

#if defined(__i386)
	available_memory =
	    MIN(available_memory, vmem_size(heap_arena, VMEM_FREE));
#endif

	if (txg > last_txg) {
		last_txg = txg;
		page_load = 0;
	}

	if (freemem > physmem * arc_lotsfree_percent / 100) {
		page_load = 0;
		return (0);
	}

	/*
	 * If we are in pageout, we know that memory is already tight,
	 * the arc is already going to be evicting, so we just want to
	 * continue to let page writes occur as quickly as possible.
	 */

	if (spl_free_manual_pressure_wrapper() != 0 &&
	    arc_reclaim_in_loop == B_FALSE) {
		cv_signal(&arc_reclaim_thread_cv);
		kpreempt(KPREEMPT_SYNC);
		page_load = 0;
	}

	if (!spl_minimal_physmem_p() && page_load > 0) {
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		dprintf("ZFS: %s: !spl_minimal_physmem_p(), available_memory "
		    "== %lld, page_load = %llu, txg = %llu, reserve = %llu\n",
		    __func__, available_memory, page_load, txg, reserve);
		if (arc_reclaim_in_loop == B_FALSE)
			cv_signal(&arc_reclaim_thread_cv);
		kpreempt(KPREEMPT_SYNC);
		page_load = 0;
		return (SET_ERROR(EAGAIN));
	}

	if (arc_reclaim_needed() && page_load > 0) {
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		dprintf("ZFS: %s: arc_reclaim_needed(), available_memory "
		    "== %lld, page_load = %llu, txg = %llu, reserve = %lld\n",
		    __func__, available_memory, page_load, txg, reserve);
		if (arc_reclaim_in_loop == B_FALSE)
			cv_signal(&arc_reclaim_thread_cv);
		kpreempt(KPREEMPT_SYNC);
		page_load = 0;
		return (SET_ERROR(EAGAIN));
	}

	/* as with sun, assume we are reclaiming */
	if (available_memory <= 0 || page_load > available_memory / 4) {
		return (SET_ERROR(ERESTART));
	}

	if (!spl_minimal_physmem_p()) {
		page_load += reserve/8;
		return (0);
	}

	page_load = 0;

	return (0);
}

int64_t
arc_shrink(int64_t to_free)
{
	int64_t shrank = 0;
	int64_t arc_c_before = arc_c;
	int64_t arc_adjust_evicted = 0;

	uint64_t asize = aggsum_value(&arc_size);
	if (arc_c > arc_c_min) {

		if (arc_c > arc_c_min + to_free)
			atomic_add_64(&arc_c, -to_free);
		else
			arc_c = arc_c_min;

		atomic_add_64(&arc_p, -(arc_p >> arc_shrink_shift));
		if (asize < arc_c)
			arc_c = MAX(asize, arc_c_min);
		if (arc_p > arc_c)
			arc_p = (arc_c >> 1);
		ASSERT(arc_c >= arc_c_min);
		ASSERT((int64_t)arc_p >= 0);
	}

	shrank = arc_c_before - arc_c;

	return (shrank + arc_adjust_evicted);
}


/*
 * arc.c has a arc_reap_zthr we should probably use, instead of
 * having our own legacy arc_reclaim_thread().
 */
static void arc_kmem_reap_now(void)
{
	arc_wait_for_eviction(0);

	/* arc.c will do the heavy lifting */
	arc_kmem_reap_soon();

	/* Now some OsX additionals */
	extern kmem_cache_t *abd_chunk_cache;
	extern kmem_cache_t *znode_cache;

	kmem_cache_reap_now(abd_chunk_cache);
	if (znode_cache) kmem_cache_reap_now(znode_cache);

	if (zio_arena_parent != NULL) {
		/*
		 * Ask the vmem arena to reclaim unused memory from its
		 * quantum caches.
		 */
		vmem_qcache_reap(zio_arena_parent);
	}
}



/*
 * Threads can block in arc_get_data_impl() waiting for this thread to evict
 * enough data and signal them to proceed. When this happens, the threads in
 * arc_get_data_impl() are sleeping while holding the hash lock for their
 * particular arc header. Thus, we must be careful to never sleep on a
 * hash lock in this thread. This is to prevent the following deadlock:
 *
 *  - Thread A sleeps on CV in arc_get_data_impl() holding hash lock "L",
 *    waiting for the reclaim thread to signal it.
 *
 *  - arc_reclaim_thread() tries to acquire hash lock "L" using mutex_enter,
 *    fails, and goes to sleep forever.
 *
 * This possible deadlock is avoided by always acquiring a hash lock
 * using mutex_tryenter() from arc_reclaim_thread().
 */
static void
arc_reclaim_thread(void *unused)
{
	hrtime_t growtime = 0;
	callb_cpr_t cpr;

	CALLB_CPR_INIT(&cpr, &arc_reclaim_lock, callb_generic_cpr, FTAG);

	mutex_enter(&arc_reclaim_lock);
	while (!arc_reclaim_thread_exit) {
		arc_reclaim_in_loop = B_TRUE;
		uint64_t evicted = 0;

		mutex_exit(&arc_reclaim_lock);

		if (reclaim_shrink_target > 0) {
			int64_t t = reclaim_shrink_target;
			reclaim_shrink_target = 0;
			evicted = arc_shrink(t);
			extern kmem_cache_t *abd_chunk_cache;
			kmem_cache_reap_now(abd_chunk_cache);
			IOSleep(1);
			goto lock_and_sleep;
		}

		int64_t pre_adjust_free_memory = MIN(spl_free_wrapper(),
		    arc_available_memory());

		int64_t manual_pressure = spl_free_manual_pressure_wrapper();
		spl_free_set_pressure(0); // clears both spl pressure variables

		/*
		 * We call arc_adjust() before (possibly) calling
		 * arc_kmem_reap_now(), so that we can wake up
		 * arc_get_data_impl() sooner.
		 */
		arc_wait_for_eviction(0);

		int64_t free_memory = arc_available_memory();

		int64_t post_adjust_manual_pressure =
		    spl_free_manual_pressure_wrapper();
		manual_pressure = MAX(manual_pressure,
		    post_adjust_manual_pressure);
		spl_free_set_pressure(0);

		int64_t post_adjust_free_memory =
		    MIN(spl_free_wrapper(), arc_available_memory());

		// if arc_adjust() evicted, we expect post_adjust_free_memory
		// to be larger than pre_adjust_free_memory (as there should
		// be more free memory).
		int64_t d_adj = post_adjust_free_memory -
		    pre_adjust_free_memory;

		if (manual_pressure > 0 && post_adjust_manual_pressure == 0) {
			// pressure did not get re-signalled during arc_adjust()
			if (d_adj >= 0) {
				manual_pressure -= MIN(evicted, d_adj);
			} else {
				manual_pressure -= evicted;
			}
		} else if (evicted > 0 && manual_pressure > 0 &&
		    post_adjust_manual_pressure > 0) {
			// otherwise use the most recent pressure value
			manual_pressure = post_adjust_manual_pressure;
		}

		free_memory = post_adjust_free_memory;

		if (free_memory >= 0 && manual_pressure <= 0 && evicted > 0) {
			extern kmem_cache_t *abd_chunk_cache;
			kmem_cache_reap_now(abd_chunk_cache);
		}

		if (free_memory < 0 || manual_pressure > 0) {

			if (free_memory <=
			    (arc_c >> arc_no_grow_shift) + SPA_MAXBLOCKSIZE) {
				arc_no_grow = B_TRUE;

		/*
		 * Absorb occasional low memory conditions, as they
		 * may be caused by a single sequentially writing thread
		 * pushing a lot of dirty data into the ARC.
		 *
		 * In particular, we want to quickly
		 * begin re-growing the ARC if we are
		 * not in chronic high pressure.
		 * However, if we're in chronic high
		 * pressure, we want to reduce reclaim
		 * thread work by keeping arc_no_grow set.
		 *
		 * If growtime is in the past, then set it to last
		 * half a second (which is the length of the
		 * cv_timedwait_hires() call below; if this works,
		 * that value should be a parameter, #defined or constified.
		 *
		 * If growtime is in the future, then make sure that it
		 * is no further than 60 seconds into the future.
		 * If it's in the nearer future, then grow growtime by
		 * an exponentially increasing value starting with 500msec.
		 *
		 */
				const hrtime_t curtime = gethrtime();
				const hrtime_t agr = SEC2NSEC(arc_grow_retry);
				static int grow_pass = 0;

				if (growtime == 0) {
					growtime = curtime + MSEC2NSEC(500);
					grow_pass = 0;
				} else {
					// check for 500ms not being enough
					ASSERT3U(growtime, >, curtime);
					if (growtime <= curtime)
						growtime = curtime +
						    MSEC2NSEC(500);

					// growtime is in the future!
					const hrtime_t difference =
					    growtime - curtime;

					if (difference >= agr) {
						// cap arc_grow_retry secs now
						growtime = curtime + agr - 1LL;
						grow_pass = 0;
					} else {
						hrtime_t grow_by =
						    MSEC2NSEC(500) *
						    (1LL << grow_pass);

						if (grow_by > (agr >> 1))
							grow_by = agr >> 1;

						growtime += grow_by;

						// add 512 seconds maximum
						if (grow_pass < 10)
							grow_pass++;
					}
				}
			}

			arc_warm = B_TRUE;

			arc_kmem_reap_now();

			/*
			 * If we are still low on memory, shrink the ARC
			 * so that we have arc_shrink_min free space.
			 */
			free_memory = arc_available_memory();

			static int64_t old_to_free = 0;

			int64_t to_free =
			    (arc_c >> arc_shrink_shift) - free_memory;

			if (to_free > 0 || manual_pressure != 0) {
				// 2 * SPA_MAXBLOCKSIZE
				const int64_t large_amount =
				    32LL * 1024LL * 1024LL;
				const int64_t huge_amount =
				    128LL * 1024LL * 1024LL;

				if (to_free > large_amount ||
				    evicted > huge_amount)
					dprintf("SPL: %s: post-reap %lld "
					    "post-evict %lld adjusted %lld "
					    "pre-adjust %lld to-free %lld"
					    " pressure %lld\n",
					    __func__, free_memory, d_adj,
					    evicted, pre_adjust_free_memory,
					    to_free, manual_pressure);
				to_free = MAX(to_free, manual_pressure);

				int64_t old_arc_size =
				    (int64_t)aggsum_value(&arc_size);
				(void) arc_shrink(to_free);
				int64_t new_arc_size =
				    (int64_t)aggsum_value(&arc_size);
				int64_t arc_shrink_freed =
				    old_arc_size - new_arc_size;
				int64_t left_to_free =
				    to_free - arc_shrink_freed;
				if (left_to_free <= 0) {
					if (arc_shrink_freed > large_amount) {
						dprintf("ZFS: %s, arc_shrink "
						    "freed %lld, zeroing "
						    "old_to_free from %lld\n",
						    __func__, arc_shrink_freed,
						    old_to_free);
					}
					old_to_free = 0;
				} else if (arc_shrink_freed > 2LL *
				    (int64_t)SPA_MAXBLOCKSIZE) {
					dprintf("ZFS: %s, arc_shrink freed "
					    "%lld, setting old_to_free to "
					    "%lld from %lld\n",
					    __func__, arc_shrink_freed,
					    left_to_free, old_to_free);
					old_to_free = left_to_free;
				} else {
					old_to_free = left_to_free;
				}

				// If we have reduced ARC by a lot before
				// this point, try to give memory back to
				// lower arenas (and possibly xnu).

				int64_t total_freed =
				    arc_shrink_freed + evicted;
				if (total_freed >= huge_amount) {
					if (zio_arena_parent != NULL)
						vmem_qcache_reap(
						    zio_arena_parent);
				}
				if (arc_shrink_freed > 0)
					evicted += arc_shrink_freed;
			} else if (old_to_free > 0) {
				dprintf("ZFS: %s, (old_)to_free has "
				    "returned to zero from %lld\n",
				    __func__, old_to_free);
				old_to_free = 0;
			}

		} else if (free_memory < (arc_c >> arc_no_grow_shift) &&
		    aggsum_value(&arc_size) >
		    arc_c_min + SPA_MAXBLOCKSIZE) {
			// relatively low memory and arc is above arc_c_min
			arc_no_grow = B_TRUE;
			growtime = gethrtime() + SEC2NSEC(1);
		}

		if (growtime > 0 && gethrtime() >= growtime) {
			if (arc_no_grow == B_TRUE)
				dprintf("ZFS: arc growtime expired\n");
			growtime = 0;
			arc_no_grow = B_FALSE;
		}

lock_and_sleep:

		mutex_enter(&arc_reclaim_lock);

		/*
		 * If evicted is zero, we couldn't evict anything via
		 * arc_adjust(). This could be due to hash lock
		 * collisions, but more likely due to the majority of
		 * arc buffers being unevictable. Therefore, even if
		 * arc_size is above arc_c, another pass is unlikely to
		 * be helpful and could potentially cause us to enter an
		 * infinite loop.
		 */
		if (aggsum_compare(&arc_size, arc_c) <= 0 || evicted == 0) {
			/*
			 * We're either no longer overflowing, or we
			 * can't evict anything more, so we should wake
			 * up any threads before we go to sleep.
			 */
			cv_broadcast(&arc_reclaim_waiters_cv);

			arc_reclaim_in_loop = B_FALSE;
			/*
			 * Block until signaled, or after one second (we
			 * might need to perform arc_kmem_reap_now()
			 * even if we aren't being signalled)
			 */
			CALLB_CPR_SAFE_BEGIN(&cpr);
			(void) cv_timedwait_hires(&arc_reclaim_thread_cv,
			    &arc_reclaim_lock, MSEC2NSEC(500), MSEC2NSEC(1), 0);
			CALLB_CPR_SAFE_END(&cpr, &arc_reclaim_lock);

		} else if (evicted >= SPA_MAXBLOCKSIZE * 3) {
			// we evicted plenty of buffers, so let's wake up
			// all the waiters rather than having them stall
			cv_broadcast(&arc_reclaim_waiters_cv);
		} else {
			// we evicted some buffers but are still overflowing,
			// so wake up only one waiter
			cv_signal(&arc_reclaim_waiters_cv);
		}
	}

	arc_reclaim_thread_exit = B_FALSE;
	cv_broadcast(&arc_reclaim_thread_cv);
	CALLB_CPR_EXIT(&cpr); /* drops arc_reclaim_lock */
	thread_exit();
}

uint64_t
isqrt(uint64_t n)
{
	int i;
	uint64_t r, tmp;
	r = 0;
	for (i = 64/2-1; i >= 0; i--) {
		tmp = r | (1 << i);
		if (tmp*tmp <= n)
			r = tmp;
	}
	return (r);
}

/* This is called before arc is initialized, and threads are not running */
void
arc_lowmem_init(void)
{
	/*
	 * The ARC tries to keep at least this much memory available for the
	 * system.  This gives the ARC time to shrink in response to memory
	 * pressure, before running completely out of memory and invoking the
	 * direct-reclaim ARC shrinker.
	 *
	 * This should be more than twice high_wmark_pages(), so that
	 * arc_wait_for_eviction() will wait until at least the
	 * high_wmark_pages() are free (see arc_evict_state_impl()).
	 *
	 * Note: Even when the system is very low on memory, the kernel's
	 * shrinker code may only ask for one "batch" of pages (512KB) to be
	 * evicted.  If concurrent allocations consume these pages, there may
	 * still be insufficient free pages, and the OOM killer takes action.
	 *
	 * By setting arc_sys_free large enough, and having
	 * arc_wait_for_eviction() wait until there is at least arc_sys_free/2
	 * free memory, it is much less likely that concurrent allocations can
	 * consume all the memory that was evicted before checking for
	 * OOM.
	 *
	 * It's hard to iterate the zones from a linux kernel module, which
	 * makes it difficult to determine the watermark dynamically. Instead
	 * we compute the maximum high watermark for this system, based
	 * on the amount of memory, assuming default parameters on Linux kernel
	 * 5.3.
	 */
	/*
	 * Base wmark_low is 4 * the square root of Kbytes of RAM.
	 */
	uint64_t allmem = kmem_size();
	long wmark = 4 * (long)isqrt(allmem/1024) * 1024;

	/*
	 * Clamp to between 128K and 64MB.
	 */
	wmark = MAX(wmark, 128 * 1024);
	wmark = MIN(wmark, 64 * 1024 * 1024);

	/*
	 * watermark_boost can increase the wmark by up to 150%.
	 */
	wmark += wmark * 150 / 100;

	/*
	 * arc_sys_free needs to be more than 2x the watermark, because
	 * arc_wait_for_eviction() waits for half of arc_sys_free.  Bump this up
	 * to 3x to ensure we're above it.
	 */
	arc_sys_free = wmark * 3 + allmem / 32;

}

/* This is called after arc is initialized, and thread are running */
void
arc_os_init(void)
{
	mutex_init(&arc_reclaim_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&arc_reclaim_thread_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&arc_reclaim_waiters_cv, NULL, CV_DEFAULT, NULL);

	arc_reclaim_thread_exit = B_FALSE;

	(void) thread_create(NULL, 0, arc_reclaim_thread, NULL, 0, &p0,
	    TS_RUN, minclsyspri);

	arc_warm = B_FALSE;

}

void
arc_lowmem_fini(void)
{
}

void
arc_os_fini(void)
{
	mutex_enter(&arc_reclaim_lock);
	arc_reclaim_thread_exit = B_TRUE;
	/*
	 * The reclaim thread will set arc_reclaim_thread_exit back to
	 * B_FALSE when it is finished exiting; we're waiting for that.
	 */
	while (arc_reclaim_thread_exit) {
		cv_signal(&arc_reclaim_thread_cv);
		cv_wait(&arc_reclaim_thread_cv, &arc_reclaim_lock);
	}
	mutex_exit(&arc_reclaim_lock);

	mutex_destroy(&arc_reclaim_lock);
	cv_destroy(&arc_reclaim_thread_cv);
	cv_destroy(&arc_reclaim_waiters_cv);
}

/*
 * Uses ARC static variables in logic.
 */
#define	arc_meta_limit	ARCSTAT(arcstat_meta_limit) /* max size for metadata */
/* max size for dnodes */
#define	arc_dnode_size_limit	ARCSTAT(arcstat_dnode_limit)
#define	arc_meta_min	ARCSTAT(arcstat_meta_min) /* min size for metadata */
#define	arc_meta_max	ARCSTAT(arcstat_meta_max) /* max size of metadata */

/* So close, they made arc_min_prefetch_ms be static, but no others */

int
arc_kstat_update_windows(kstat_t *ksp, int rw)
{
	windows_kstat_t *ks = ksp->ks_data;

	if (rw == KSTAT_WRITE) {

		/* Did we change the value ? */
		if (ks->arc_zfs_arc_max.value.ui64 != zfs_arc_max) {

			/* Assign new value */
			zfs_arc_max = ks->arc_zfs_arc_max.value.ui64;

			/* Update ARC with new value */
			if (zfs_arc_max > 64<<20 && zfs_arc_max <
			    physmem * PAGESIZE)
				arc_c_max = zfs_arc_max;

			arc_c = arc_c_max;
			arc_p = (arc_c >> 1);

			/* If meta_limit is not set, adjust it automatically */
			if (!zfs_arc_meta_limit)
				arc_meta_limit = arc_c_max / 4;
		}

		if (ks->arc_zfs_arc_min.value.ui64 != zfs_arc_min) {
			zfs_arc_min = ks->arc_zfs_arc_min.value.ui64;
			if (zfs_arc_min > 64<<20 && zfs_arc_min <= arc_c_max) {
				arc_c_min = zfs_arc_min;
				dprintf("ZFS: set arc_c_min %llu, arc_meta_min "
				    "%llu, zfs_arc_meta_min %llu\n",
				    arc_c_min, arc_meta_min, zfs_arc_meta_min);
				if (arc_c < arc_c_min) {
					dprintf("ZFS: raise arc_c %llu to "
					    "arc_c_min %llu\n", arc_c,
					    arc_c_min);
					arc_c = arc_c_min;
					if (arc_p < (arc_c >> 1)) {
						dprintf("ZFS: raise arc_p %llu "
						    "to %llu\n",
						    arc_p, (arc_c >> 1));
						arc_p = (arc_c >> 1);
					}
				}
			}
		}

		if (ks->arc_zfs_arc_meta_limit.value.ui64 !=
		    zfs_arc_meta_limit) {
			zfs_arc_meta_limit =
			    ks->arc_zfs_arc_meta_limit.value.ui64;

			/* Allow the tunable to override if it is reasonable */
			if (zfs_arc_meta_limit > 0 &&
			    zfs_arc_meta_limit <= arc_c_max)
				arc_meta_limit = zfs_arc_meta_limit;

			if (arc_c_min < arc_meta_limit / 2 &&
			    zfs_arc_min == 0)
				arc_c_min = arc_meta_limit / 2;

			dprintf("ZFS: set arc_meta_limit %lu, arc_c_min %lu,"
			    "zfs_arc_meta_limit %lu\n",
			    arc_meta_limit, arc_c_min, zfs_arc_meta_limit);
		}

		if (ks->arc_zfs_arc_meta_min.value.ui64 != zfs_arc_meta_min) {
			zfs_arc_meta_min  = ks->arc_zfs_arc_meta_min.value.ui64;
			if (zfs_arc_meta_min >= arc_c_min) {
				dprintf("ZFS: probable error, zfs_arc_meta_min "
				    "%llu >= arc_c_min %llu\n",
				    zfs_arc_meta_min, arc_c_min);
			}
			if (zfs_arc_meta_min > 0 &&
			    zfs_arc_meta_min <= arc_meta_limit)
				arc_meta_min = zfs_arc_meta_min;
			dprintf("ZFS: set arc_meta_min %llu\n", arc_meta_min);
		}

		zfs_arc_grow_retry = ks->arc_zfs_arc_grow_retry.value.ui64;
		arc_grow_retry = zfs_arc_grow_retry;
		zfs_arc_shrink_shift = ks->arc_zfs_arc_shrink_shift.value.ui64;
		zfs_arc_p_min_shift = ks->arc_zfs_arc_p_min_shift.value.ui64;
		zfs_arc_average_blocksize =
		    ks->arc_zfs_arc_average_blocksize.value.ui64;

	} else {

		ks->arc_zfs_arc_max.value.ui64 = zfs_arc_max;
		ks->arc_zfs_arc_min.value.ui64 = zfs_arc_min;

		ks->arc_zfs_arc_meta_limit.value.ui64 = zfs_arc_meta_limit;
		ks->arc_zfs_arc_meta_min.value.ui64 = zfs_arc_meta_min;

		ks->arc_zfs_arc_grow_retry.value.ui64 =
		    zfs_arc_grow_retry ? zfs_arc_grow_retry : arc_grow_retry;
		ks->arc_zfs_arc_shrink_shift.value.ui64 = zfs_arc_shrink_shift;
		ks->arc_zfs_arc_p_min_shift.value.ui64 = zfs_arc_p_min_shift;
		ks->arc_zfs_arc_average_blocksize.value.ui64 =
		    zfs_arc_average_blocksize;
	}
	return (0);
}

/*
 * Helper function for arc_prune_async() it is responsible for safely
 * handling the execution of a registered arc_prune_func_t.
 */
static void
arc_prune_task(void *ptr)
{
	arc_prune_t *ap = (arc_prune_t *)ptr;
	arc_prune_func_t *func = ap->p_pfunc;

	if (func != NULL)
		func(ap->p_adjust, ap->p_private);

	zfs_refcount_remove(&ap->p_refcnt, func);
}

/*
 * Notify registered consumers they must drop holds on a portion of the ARC
 * buffered they reference.  This provides a mechanism to ensure the ARC can
 * honor the arc_meta_limit and reclaim otherwise pinned ARC buffers.  This
 * is analogous to dnlc_reduce_cache() but more generic.
 *
 * This operation is performed asynchronously so it may be safely called
 * in the context of the arc_reclaim_thread().  A reference is taken here
 * for each registered arc_prune_t and the arc_prune_task() is responsible
 * for releasing it once the registered arc_prune_func_t has completed.
 */
void
arc_prune_async(int64_t adjust)
{
	arc_prune_t *ap;

	mutex_enter(&arc_prune_mtx);
	for (ap = list_head(&arc_prune_list); ap != NULL;
	    ap = list_next(&arc_prune_list, ap)) {

		if (zfs_refcount_count(&ap->p_refcnt) >= 2)
			continue;

		zfs_refcount_add(&ap->p_refcnt, ap->p_pfunc);
		ap->p_adjust = adjust;
		if (taskq_dispatch(arc_prune_taskq, arc_prune_task,
		    ap, TQ_SLEEP) == TASKQID_INVALID) {
			zfs_refcount_remove(&ap->p_refcnt, ap->p_pfunc);
			continue;
		}
		ARCSTAT_BUMP(arcstat_prune);
	}
	mutex_exit(&arc_prune_mtx);
}

#else /* KERNEL */

int64_t
arc_available_memory(void)
{
	int64_t lowest = INT64_MAX;

	/* Every 100 calls, free a small amount */
	if (spa_get_random(100) == 0)
		lowest = -1024;

	return (lowest);
}

int
arc_memory_throttle(spa_t *spa, uint64_t reserve, uint64_t txg)
{
	return (0);
}

uint64_t
arc_all_memory(void)
{
	return (ptob(physmem) / 2);
}

uint64_t
arc_free_memory(void)
{
	return (spa_get_random(arc_all_memory() * 20 / 100));
}

#endif /* KERNEL */
