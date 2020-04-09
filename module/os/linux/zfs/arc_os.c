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
#include <sys/refcount.h>
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
#include <sys/shrinker.h>
#include <sys/vmsystm.h>
#include <sys/zpl.h>
#include <linux/page_compat.h>
#endif
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/zthr.h>
#include <zfs_fletcher.h>
#include <sys/arc_impl.h>
#include <sys/trace_zfs.h>
#include <sys/aggsum.h>

int64_t last_free_memory;
free_memory_reason_t last_free_reason;

/*
 * Return a default max arc size based on the amount of physical memory.
 */
uint64_t
arc_default_max(uint64_t min, uint64_t allmem)
{
	/* Default to 1/2 of all memory. */
	return (MAX(allmem / 2, min));
}

#ifdef _KERNEL
/*
 * Return maximum amount of memory that we could possibly use.  Reduced
 * to half of all memory in user space which is primarily used for testing.
 */
uint64_t
arc_all_memory(void)
{
#ifdef CONFIG_HIGHMEM
	return (ptob(zfs_totalram_pages - zfs_totalhigh_pages));
#else
	return (ptob(zfs_totalram_pages));
#endif /* CONFIG_HIGHMEM */
}

/*
 * Return the amount of memory that is considered free.  In user space
 * which is primarily used for testing we pretend that free memory ranges
 * from 0-20% of all memory.
 */
uint64_t
arc_free_memory(void)
{
#ifdef CONFIG_HIGHMEM
	struct sysinfo si;
	si_meminfo(&si);
	return (ptob(si.freeram - si.freehigh));
#else
	return (ptob(nr_free_pages() +
	    nr_inactive_file_pages() +
	    nr_inactive_anon_pages() +
	    nr_slab_reclaimable_pages()));
#endif /* CONFIG_HIGHMEM */
}

/*
 * Additional reserve of pages for pp_reserve.
 */
int64_t arc_pages_pp_reserve = 64;

/*
 * Additional reserve of pages for swapfs.
 */
int64_t arc_swapfs_reserve = 64;

/*
 * Return the amount of memory that can be consumed before reclaim will be
 * needed.  Positive if there is sufficient free memory, negative indicates
 * the amount of memory that needs to be freed up.
 */
int64_t
arc_available_memory(void)
{
	int64_t lowest = INT64_MAX;
	free_memory_reason_t r = FMR_UNKNOWN;
	int64_t n;
#ifdef freemem
#undef freemem
#endif
	pgcnt_t needfree = btop(arc_need_free);
	pgcnt_t lotsfree = btop(arc_sys_free);
	pgcnt_t desfree = 0;
	pgcnt_t freemem = btop(arc_free_memory());

	if (needfree > 0) {
		n = PAGESIZE * (-needfree);
		if (n < lowest) {
			lowest = n;
			r = FMR_NEEDFREE;
		}
	}

	/*
	 * check that we're out of range of the pageout scanner.  It starts to
	 * schedule paging if freemem is less than lotsfree and needfree.
	 * lotsfree is the high-water mark for pageout, and needfree is the
	 * number of needed free pages.  We add extra pages here to make sure
	 * the scanner doesn't start up while we're freeing memory.
	 */
	n = PAGESIZE * (freemem - lotsfree - needfree - desfree);
	if (n < lowest) {
		lowest = n;
		r = FMR_LOTSFREE;
	}

#if defined(_ILP32)
	/*
	 * If we're on a 32-bit platform, it's possible that we'll exhaust the
	 * kernel heap space before we ever run out of available physical
	 * memory.  Most checks of the size of the heap_area compare against
	 * tune.t_minarmem, which is the minimum available real memory that we
	 * can have in the system.  However, this is generally fixed at 25 pages
	 * which is so low that it's useless.  In this comparison, we seek to
	 * calculate the total heap-size, and reclaim if more than 3/4ths of the
	 * heap is allocated.  (Or, in the calculation, if less than 1/4th is
	 * free)
	 */
	n = vmem_size(heap_arena, VMEM_FREE) -
	    (vmem_size(heap_arena, VMEM_FREE | VMEM_ALLOC) >> 2);
	if (n < lowest) {
		lowest = n;
		r = FMR_HEAP_ARENA;
	}
#endif

	/*
	 * If zio data pages are being allocated out of a separate heap segment,
	 * then enforce that the size of available vmem for this arena remains
	 * above about 1/4th (1/(2^arc_zio_arena_free_shift)) free.
	 *
	 * Note that reducing the arc_zio_arena_free_shift keeps more virtual
	 * memory (in the zio_arena) free, which can avoid memory
	 * fragmentation issues.
	 */
	if (zio_arena != NULL) {
		n = (int64_t)vmem_size(zio_arena, VMEM_FREE) -
		    (vmem_size(zio_arena, VMEM_ALLOC) >>
		    arc_zio_arena_free_shift);
		if (n < lowest) {
			lowest = n;
			r = FMR_ZIO_ARENA;
		}
	}

	last_free_memory = lowest;
	last_free_reason = r;

	return (lowest);
}

static uint64_t
arc_evictable_memory(void)
{
	int64_t asize = aggsum_value(&arc_size);
	uint64_t arc_clean =
	    zfs_refcount_count(&arc_mru->arcs_esize[ARC_BUFC_DATA]) +
	    zfs_refcount_count(&arc_mru->arcs_esize[ARC_BUFC_METADATA]) +
	    zfs_refcount_count(&arc_mfu->arcs_esize[ARC_BUFC_DATA]) +
	    zfs_refcount_count(&arc_mfu->arcs_esize[ARC_BUFC_METADATA]);
	uint64_t arc_dirty = MAX((int64_t)asize - (int64_t)arc_clean, 0);

	/*
	 * Scale reported evictable memory in proportion to page cache, cap
	 * at specified min/max.
	 */
	uint64_t min = (ptob(nr_file_pages()) / 100) * zfs_arc_pc_percent;
	min = MAX(arc_c_min, MIN(arc_c_max, min));

	if (arc_dirty >= min)
		return (arc_clean);

	return (MAX((int64_t)asize - (int64_t)min, 0));
}

/*
 * If sc->nr_to_scan is zero, the caller is requesting a query of the
 * number of objects which can potentially be freed.  If it is nonzero,
 * the request is to free that many objects.
 *
 * Linux kernels >= 3.12 have the count_objects and scan_objects callbacks
 * in struct shrinker and also require the shrinker to return the number
 * of objects freed.
 *
 * Older kernels require the shrinker to return the number of freeable
 * objects following the freeing of nr_to_free.
 */
static spl_shrinker_t
__arc_shrinker_func(struct shrinker *shrink, struct shrink_control *sc)
{
	int64_t pages;

	/* The arc is considered warm once reclaim has occurred */
	if (unlikely(arc_warm == B_FALSE))
		arc_warm = B_TRUE;

	/* Return the potential number of reclaimable pages */
	pages = btop((int64_t)arc_evictable_memory());
	if (sc->nr_to_scan == 0)
		return (pages);

	/* Not allowed to perform filesystem reclaim */
	if (!(sc->gfp_mask & __GFP_FS))
		return (SHRINK_STOP);

	/* Reclaim in progress */
	if (mutex_tryenter(&arc_adjust_lock) == 0) {
		ARCSTAT_INCR(arcstat_need_free, ptob(sc->nr_to_scan));
		return (0);
	}

	mutex_exit(&arc_adjust_lock);

	/*
	 * Evict the requested number of pages by shrinking arc_c the
	 * requested amount.
	 */
	if (pages > 0) {
		arc_reduce_target_size(ptob(sc->nr_to_scan));
		if (current_is_kswapd())
			arc_kmem_reap_soon();
#ifdef HAVE_SPLIT_SHRINKER_CALLBACK
		pages = MAX((int64_t)pages -
		    (int64_t)btop(arc_evictable_memory()), 0);
#else
		pages = btop(arc_evictable_memory());
#endif
		/*
		 * We've shrunk what we can, wake up threads.
		 */
		cv_broadcast(&arc_adjust_waiters_cv);
	} else
		pages = SHRINK_STOP;

	/*
	 * When direct reclaim is observed it usually indicates a rapid
	 * increase in memory pressure.  This occurs because the kswapd
	 * threads were unable to asynchronously keep enough free memory
	 * available.  In this case set arc_no_grow to briefly pause arc
	 * growth to avoid compounding the memory pressure.
	 */
	if (current_is_kswapd()) {
		ARCSTAT_BUMP(arcstat_memory_indirect_count);
	} else {
		arc_no_grow = B_TRUE;
		arc_kmem_reap_soon();
		ARCSTAT_BUMP(arcstat_memory_direct_count);
	}

	return (pages);
}
SPL_SHRINKER_CALLBACK_WRAPPER(arc_shrinker_func);

SPL_SHRINKER_DECLARE(arc_shrinker, arc_shrinker_func, DEFAULT_SEEKS);

int
arc_memory_throttle(spa_t *spa, uint64_t reserve, uint64_t txg)
{
	uint64_t available_memory = arc_free_memory();

#if defined(_ILP32)
	available_memory =
	    MIN(available_memory, vmem_size(heap_arena, VMEM_FREE));
#endif

	if (available_memory > arc_all_memory() * arc_lotsfree_percent / 100)
		return (0);

	if (txg > spa->spa_lowmem_last_txg) {
		spa->spa_lowmem_last_txg = txg;
		spa->spa_lowmem_page_load = 0;
	}
	/*
	 * If we are in pageout, we know that memory is already tight,
	 * the arc is already going to be evicting, so we just want to
	 * continue to let page writes occur as quickly as possible.
	 */
	if (current_is_kswapd()) {
		if (spa->spa_lowmem_page_load >
		    MAX(arc_sys_free / 4, available_memory) / 4) {
			DMU_TX_STAT_BUMP(dmu_tx_memory_reclaim);
			return (SET_ERROR(ERESTART));
		}
		/* Note: reserve is inflated, so we deflate */
		atomic_add_64(&spa->spa_lowmem_page_load, reserve / 8);
		return (0);
	} else if (spa->spa_lowmem_page_load > 0 && arc_reclaim_needed()) {
		/* memory is low, delay before restarting */
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		DMU_TX_STAT_BUMP(dmu_tx_memory_reclaim);
		return (SET_ERROR(EAGAIN));
	}
	spa->spa_lowmem_page_load = 0;
	return (0);
}

void
arc_lowmem_init(void)
{
	uint64_t allmem = arc_all_memory();

	/*
	 * Register a shrinker to support synchronous (direct) memory
	 * reclaim from the arc.  This is done to prevent kswapd from
	 * swapping out pages when it is preferable to shrink the arc.
	 */
	spl_register_shrinker(&arc_shrinker);

	/* Set to 1/64 of all memory or a minimum of 512K */
	arc_sys_free = MAX(allmem / 64, (512 * 1024));
	arc_need_free = 0;
}

void
arc_lowmem_fini(void)
{
	spl_unregister_shrinker(&arc_shrinker);
}

int
param_set_arc_long(const char *buf, zfs_kernel_param_t *kp)
{
	int error;

	error = param_set_long(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	arc_tuning_update(B_TRUE);

	return (0);
}

int
param_set_arc_int(const char *buf, zfs_kernel_param_t *kp)
{
	int error;

	error = param_set_int(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	arc_tuning_update(B_TRUE);

	return (0);
}
#else /* _KERNEL */
int64_t
arc_available_memory(void)
{
	int64_t lowest = INT64_MAX;
	free_memory_reason_t r = FMR_UNKNOWN;

	/* Every 100 calls, free a small amount */
	if (spa_get_random(100) == 0)
		lowest = -1024;

	last_free_memory = lowest;
	last_free_reason = r;

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
#endif /* _KERNEL */

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
