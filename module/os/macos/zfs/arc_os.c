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

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#include <IOKit/IOLib.h>
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
extern uint_t	arc_no_grow_shift;


/*
 * Return a default max arc size based on the amount of physical memory.
 */
uint64_t
arc_default_max(uint64_t min, uint64_t allmem)
{
	/* Default to 1/3 of all memory. */
	return (MAX(allmem, min));
}

#ifdef _KERNEL

static _Atomic boolean_t arc_reclaim_in_loop = B_FALSE;

/*
 * Return maximum amount of memory that ARC may use.
 *
 * kmem_size() returns half of the system memory.
 * Keep 2^(-4) of that half away from ARC for various overheads,
 * and other kmem cache users.
 * On a 8 GiB Mac that means 256 MiB, arc_max max under 4 GiB
 * On a 128 GiB Mac that means 4 GiB, arc_max max 60 GiB
 *
 * Greater memory typically implies more threads and more potential I/O
 * throughput, so a large reduction is prudent on a large-memory machine.
 *
 * Since ARC is the primary driver of memory allocation activity, this reduces
 * the chances of waiting in the lowest memory allocation layers.
 *
 */
uint64_t
arc_all_memory(void)
{
	const uint64_t ks = kmem_size();
	const uint64_t overhead_safety_shift = 4;
	const uint64_t leave_this_much_free = ks >> overhead_safety_shift;
	const uint64_t ks_minus_overhead = ks - leave_this_much_free;

	return (ks_minus_overhead);
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

	/* possibly wake up arc reclaim thread */

	if (arc_reclaim_in_loop == B_FALSE) {
		if (spl_free_manual_pressure_wrapper() != 0 ||
		    !spl_minimal_physmem_p() ||
		    arc_reclaim_needed()) {
			cv_signal(&arc_reclaim_thread_cv);
			kpreempt(KPREEMPT_SYNC);
			ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		}
	}

	return (0);
}

/*
 * arc.c has a arc_reap_zthr we should probably use, instead of
 * having our own legacy arc_reclaim_thread().
 */
static void arc_kmem_reap_now(void)
{
	arc_wait_for_eviction(0, B_FALSE);

	/* arc.c will do the heavy lifting */
	arc_kmem_reap_soon();
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

		mutex_exit(&arc_reclaim_lock);

		const int64_t pre_adjust_free_memory = MIN(spl_free_wrapper(),
		    arc_available_memory());

		int64_t manual_pressure = spl_free_manual_pressure_wrapper();
		spl_free_set_pressure(0); // clears both spl pressure variables

		/*
		 * We call arc_adjust() before (possibly) calling
		 * arc_kmem_reap_now(), so that we can wake up
		 * arc_get_data_impl() sooner.
		 */

		if (manual_pressure > 0) {
			arc_reduce_target_size(MIN(manual_pressure,
			    (arc_c >> arc_shrink_shift)));
		}

		arc_wait_for_eviction(0, B_FALSE);

		int64_t free_memory = arc_available_memory();

		const int64_t post_adjust_manual_pressure =
		    spl_free_manual_pressure_wrapper();

		/* maybe we are getting lots of pressure from spl */
		manual_pressure = MAX(manual_pressure,
		    post_adjust_manual_pressure);

		spl_free_set_pressure(0);

		const int64_t post_adjust_free_memory =
		    MIN(spl_free_wrapper(), arc_available_memory());

		// if arc_adjust() evicted, we expect post_adjust_free_memory
		// to be larger than pre_adjust_free_memory (as there should
		// be more free memory).

		/*
		 * d_adj tracks the change of memory across the call
		 * to arc_wait_for_eviction(), and will count the number
		 * of bytes the spl_free_thread calculates has been
		 * made free (signed)
		 */

		const int64_t d_adj = post_adjust_free_memory -
		    pre_adjust_free_memory;

		if (manual_pressure > 0 && post_adjust_manual_pressure == 0) {
			// pressure did not get re-signalled during arc_adjust()
			if (d_adj > 0)
				manual_pressure -= d_adj;
		} else if (manual_pressure > 0 &&
		    post_adjust_manual_pressure > 0) {
			// otherwise use the most recent pressure value
			manual_pressure = post_adjust_manual_pressure;
		}

		/*
		 * If we have successfully freed a bunch of memory,
		 * it is worth reaping the abd_chunk_cache
		 */
		if (d_adj >= 64LL*1024LL*1024LL) {
			extern kmem_cache_t *abd_chunk_cache;
			kmem_cache_reap_now(abd_chunk_cache);
		}

		free_memory = post_adjust_free_memory;

		const hrtime_t curtime = gethrtime();

		if (free_memory < 0 || manual_pressure > 0) {

			if (manual_pressure > 0 || free_memory <=
			    (arc_c >> arc_no_grow_shift) + SPA_MAXBLOCKSIZE) {

				arc_no_grow = B_TRUE;

				/*
				 * Absorb occasional low memory conditions, as
				 * they may be caused by a single sequentially
				 * writing thread pushing a lot of dirty data
				 * into the ARC.
				 *
				 * In particular, we want to quickly begin
				 * re-growing the ARC if we are not in chronic
				 * high pressure.  However, if we're in
				 * chronic high pressure, we want to reduce
				 * reclaim thread work by keeping arc_no_grow
				 * set.
				 *
				 * If growtime is in the past, then set it to
				 * last half a second (which is the length of
				 * the cv_timedwait_hires() call below).
				 *
				 * If growtime is in the future, then make
				 * sure that it is no further than 60 seconds
				 * into the future.
				 *
				 * If growtime is less than 60 seconds in the
				 * future, then grow growtime by an
				 * exponentially increasing value starting
				 * with 500msec.
				 *
				 */
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
						/*
						 * with each pass, push
						 * turning off arc_no_grow
						 * by longer
						 */
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

			int64_t to_free =
			    (arc_c >> arc_shrink_shift) - free_memory;

			if (to_free > 0 || manual_pressure != 0) {

				to_free = MAX(to_free, manual_pressure);

				arc_reduce_target_size(to_free);

				goto lock_and_sleep;
			}
		} else if (free_memory < (arc_c >> arc_no_grow_shift) &&
		    aggsum_value(&arc_sums.arcstat_size) >
		    arc_c_min + SPA_MAXBLOCKSIZE) {
			// relatively low memory and arc is above arc_c_min
			arc_no_grow = B_TRUE;
			growtime = curtime + SEC2NSEC(1);
			goto lock_and_sleep;
		}

		/*
		 * The abd vmem layer can see a large number of
		 * frees from the abd kmem cache layer, and unfortunately
		 * the abd vmem layer might end up fragmented as a result.
		 *
		 * Watch for this fragmentation and if it arises
		 * suppress ARC growth for ten minutes in hopes that
		 * abd activity driven by ARC replacement or further ARC
		 * shrinking lets the abd vmem layer defragment.
		 */

		if (arc_no_grow != B_TRUE) {
			/*
			 * The gap is between imported and inuse
			 * in the abd vmem layer
			 */

			static hrtime_t when_gap_grew = 0;
			static int64_t previous_gap = 0;
			static int64_t previous_abd_size = 0;

			int64_t gap = abd_arena_empty_space();
			int64_t abd_size = abd_arena_total_size();

			if (gap == 0) {
				/*
				 * no abd vmem layer fragmentation
				 * so don't adjust arc_no_grow
				 */
				previous_gap = 0;
				previous_abd_size = abd_size;
			} else if (gap > 0 && gap == previous_gap &&
			    abd_size == previous_abd_size) {
				if (curtime < when_gap_grew + SEC2NSEC(600)) {
					/*
					 * our abd arena is unchanged
					 * try up to ten minutes for kmem layer
					 * to free slabs to abd vmem layer
					 */
					arc_no_grow = B_TRUE;
					growtime = curtime +
					    SEC2NSEC(arc_grow_retry);
					previous_abd_size = abd_size;
				} else {
					/*
					 * ten minutes have expired with no
					 * good result, shrink the arc a little,
					 * no more than once every
					 * arc_grow_retry (5) seconds
					 */
					arc_no_grow = B_TRUE;
					growtime = curtime +
					    SEC2NSEC(arc_grow_retry);
					previous_abd_size = abd_size;

					const int64_t sb =
					    arc_c >> arc_shrink_shift;
					if (arc_c_min + sb > arc_c) {
						arc_reduce_target_size(sb);
						goto lock_and_sleep;
					}
				}
			} else if (gap > 0 && gap > previous_gap) {
				/*
				 * kmem layer must have freed slabs
				 * but vmem layer is holding on because
				 * of fragmentation.   Don't grow ARC
				 * for a minute.
				 */
				arc_no_grow = B_TRUE;
				growtime = curtime + SEC2NSEC(arc_grow_retry);
				previous_gap = gap;
				when_gap_grew = curtime;
				/*
				 * but if we're growing the abd
				 * as well as its gap, shrink
				 */
				if (abd_size > previous_abd_size) {
					const int64_t sb =
					    arc_c >> arc_shrink_shift;
					if (arc_c_min + sb > arc_c)
						arc_reduce_target_size(sb);
				}
				previous_abd_size = abd_size;
			} else if (gap > 0 && gap < previous_gap) {
				/*
				 * vmem layer successfully freeing.
				 */
				if (curtime < when_gap_grew + SEC2NSEC(600)) {
					arc_no_grow = B_TRUE;
					growtime = curtime +
					    SEC2NSEC(arc_grow_retry);
				}
				previous_gap = gap;
				previous_abd_size = abd_size;
			} else {
				previous_abd_size = abd_size;
			}
		}

		if (growtime > 0 && curtime >= growtime) {
			if (arc_no_grow == B_TRUE)
				dprintf("ZFS: arc growtime expired\n");
			growtime = 0;
			arc_no_grow = B_FALSE;
		}

lock_and_sleep:

		arc_reclaim_in_loop = B_FALSE;

		mutex_enter(&arc_reclaim_lock);

		/*
		 * If d_adj is non-positive, we didn't evict anything,
		 * perhaps because nothing was evictable.  Immediately
		 * running another pass is unlikely to be helpful.
		 */

		if (aggsum_compare(&arc_sums.arcstat_size, arc_c) <= 0 ||
		    d_adj <= 0) {
			/*
			 * We're either no longer overflowing, or we
			 * can't evict anything more, so we should wake
			 * up any threads before we go to sleep.
			 */
			cv_broadcast(&arc_reclaim_waiters_cv);

			/*
			 * Block until signaled, or after one second (we
			 * might need to perform arc_kmem_reap_now()
			 * even if we aren't being signalled)
			 */
			CALLB_CPR_SAFE_BEGIN(&cpr);
			(void) cv_timedwait_hires(&arc_reclaim_thread_cv,
			    &arc_reclaim_lock, MSEC2NSEC(500), MSEC2NSEC(1), 0);
			CALLB_CPR_SAFE_END(&cpr, &arc_reclaim_lock);

		} else if (d_adj >= SPA_MAXBLOCKSIZE * 3) {
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
	 * arc_wait_for_eviction() waits for half of arc_sys_free.  Bump this up
	 * to 3x to ensure we're above it.
	 */
	VERIFY3U(arc_all_memory(), >, 0);
	arc_sys_free = arc_all_memory() / 128LL;

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
#if 0
int
arc_kstat_update_osx(kstat_t *ksp, int rw)
{
	osx_kstat_t *ks = ksp->ks_data;
	boolean_t do_update = B_FALSE;

	if (rw == KSTAT_WRITE) {

		/* Did we change the value ? */
		if (ks->arc_zfs_arc_max.value.ui64 != zfs_arc_max) {
			/* Assign new value */
			zfs_arc_max = ks->arc_zfs_arc_max.value.ui64;
			do_update = B_TRUE;
		}

		if (ks->arc_zfs_arc_min.value.ui64 != zfs_arc_min) {
			zfs_arc_min = ks->arc_zfs_arc_min.value.ui64;
			do_update = B_TRUE;
		}

		if (ks->arc_zfs_arc_meta_limit.value.ui64 !=
		    zfs_arc_meta_limit) {
			zfs_arc_meta_limit =
			    ks->arc_zfs_arc_meta_limit.value.ui64;
			do_update = B_TRUE;
		}

		if (ks->arc_zfs_arc_meta_min.value.ui64 != zfs_arc_meta_min) {
			zfs_arc_meta_min  = ks->arc_zfs_arc_meta_min.value.ui64;
			do_update = B_TRUE;
		}

		if (zfs_arc_grow_retry !=
		    ks->arc_zfs_arc_grow_retry.value.ui64) {
			zfs_arc_grow_retry =
			    ks->arc_zfs_arc_grow_retry.value.ui64;
			do_update = B_TRUE;
		}
		if (zfs_arc_shrink_shift !=
		    ks->arc_zfs_arc_shrink_shift.value.ui64) {
			zfs_arc_shrink_shift =
			    ks->arc_zfs_arc_shrink_shift.value.ui64;
			do_update = B_TRUE;
		}
		if (zfs_arc_p_min_shift !=
		    ks->arc_zfs_arc_p_min_shift.value.ui64) {
			zfs_arc_p_min_shift =
			    ks->arc_zfs_arc_p_min_shift.value.ui64;
			do_update = B_TRUE;
		}
		if (zfs_arc_average_blocksize !=
		    ks->arc_zfs_arc_average_blocksize.value.ui64) {
			zfs_arc_average_blocksize =
			    ks->arc_zfs_arc_average_blocksize.value.ui64;
			do_update = B_TRUE;
		}
		if (zfs_arc_lotsfree_percent !=
		    ks->zfs_arc_lotsfree_percent.value.i64) {
			zfs_arc_lotsfree_percent =
			    ks->zfs_arc_lotsfree_percent.value.i64;
			do_update = B_TRUE;
		}
		if (zfs_arc_sys_free !=
		    ks->zfs_arc_sys_free.value.i64) {
			zfs_arc_sys_free =
			    ks->zfs_arc_sys_free.value.i64;
			do_update = B_TRUE;
		}

		if (do_update)
			arc_tuning_update(B_TRUE);
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
		ks->zfs_arc_lotsfree_percent.value.i64 =
		    zfs_arc_lotsfree_percent;
		ks->zfs_arc_sys_free.value.i64 =
		    zfs_arc_sys_free;
	}
	return (0);
}
#endif

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
arc_prune_async(uint64_t adjust)
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

#else /* from above ifdef _KERNEL */

int64_t
arc_available_memory(void)
{
	return (arc_free_memory() - arc_sys_free);
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
	int64_t avail;

	avail = spl_free_wrapper();
	return (avail >= 0LL ? avail : 0LL);
}
#endif /* KERNEL */

void
arc_register_hotplug(void)
{
}

void
arc_unregister_hotplug(void)
{
}

void
spl_set_arc_no_grow(int i)
{
	arc_no_grow = i;
	if (i == B_TRUE)
		membar_producer(); /* make it visible to other threads */
}
