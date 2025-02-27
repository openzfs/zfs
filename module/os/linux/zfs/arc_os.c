// SPDX-License-Identifier: CDDL-1.0
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
#include <sys/zfs_refcount.h>
#include <sys/vdev.h>
#include <sys/vdev_trim.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_pool.h>
#include <sys/multilist.h>
#include <sys/abd.h>
#include <sys/zil.h>
#include <sys/fm/fs/zfs.h>
#include <sys/shrinker.h>
#include <sys/vmsystm.h>
#include <sys/zpl.h>
#include <linux/page_compat.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/version.h>
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/zthr.h>
#include <zfs_fletcher.h>
#include <sys/arc_impl.h>
#include <sys/trace_zfs.h>
#include <sys/aggsum.h>

/*
 * This is a limit on how many pages the ARC shrinker makes available for
 * eviction in response to one page allocation attempt.  Note that in
 * practice, the kernel's shrinker can ask us to evict up to about 4x this
 * for one allocation attempt.
 *
 * For example a value of 10,000 (in practice, 160MB per allocation attempt
 * with 4K pages) limits the amount of time spent attempting to reclaim ARC
 * memory to less than 100ms per allocation attempt, even with a small
 * average compressed block size of ~8KB.
 *
 * See also the comment in arc_shrinker_count().
 * Set to 0 to disable limit.
 */
static int zfs_arc_shrinker_limit = 0;

/*
 * Relative cost of ARC eviction, AKA number of seeks needed to restore evicted
 * page.  Bigger values make ARC more precious and evictions smaller comparing
 * to other kernel subsystems.  Value of 4 means parity with page cache,
 * according to my reading of kernel's do_shrink_slab() and other code.
 */
static int zfs_arc_shrinker_seeks = DEFAULT_SEEKS;

#ifdef CONFIG_MEMORY_HOTPLUG
static struct notifier_block arc_hotplug_callback_mem_nb;
#endif

/*
 * Return a default max arc size based on the amount of physical memory.
 * This may be overridden by tuning the zfs_arc_max module parameter.
 */
uint64_t
arc_default_max(uint64_t min, uint64_t allmem)
{
	uint64_t size;

	if (allmem >= 1 << 30)
		size = allmem - (1 << 30);
	else
		size = min;
	return (MAX(allmem * 5 / 8, size));
}

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
	    nr_inactive_file_pages()));
#endif /* CONFIG_HIGHMEM */
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

static uint64_t
arc_evictable_memory(void)
{
	int64_t asize = aggsum_value(&arc_sums.arcstat_size);
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
 * The _count() function returns the number of free-able objects.
 * The _scan() function returns the number of objects that were freed.
 */
static unsigned long
arc_shrinker_count(struct shrinker *shrink, struct shrink_control *sc)
{
	/*
	 * The kernel's shrinker code may not understand how many pages the
	 * ARC's callback actually frees, so it may ask the ARC to shrink a
	 * lot for one page allocation. This is problematic because it may
	 * take a long time, thus delaying the page allocation, and because
	 * it may force the ARC to unnecessarily shrink very small.
	 *
	 * Therefore, we limit the amount of data that we say is evictable,
	 * which limits the amount that the shrinker will ask us to evict for
	 * one page allocation attempt.
	 *
	 * In practice, we may be asked to shrink 4x the limit to satisfy one
	 * page allocation, before the kernel's shrinker code gives up on us.
	 * When that happens, we rely on the kernel code to find the pages
	 * that we freed before invoking the OOM killer.  This happens in
	 * __alloc_pages_slowpath(), which retries and finds the pages we
	 * freed when it calls get_page_from_freelist().
	 *
	 * See also the comment above zfs_arc_shrinker_limit.
	 */
	int64_t can_free = btop(arc_evictable_memory());
	if (current_is_kswapd() && zfs_arc_shrinker_limit)
		can_free = MIN(can_free, zfs_arc_shrinker_limit);
	return (can_free);
}

static unsigned long
arc_shrinker_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	/* The arc is considered warm once reclaim has occurred */
	if (unlikely(arc_warm == B_FALSE))
		arc_warm = B_TRUE;

	/*
	 * We are experiencing memory pressure which the arc_evict_zthr was
	 * unable to keep up with.  Set arc_no_grow to briefly pause ARC
	 * growth to avoid compounding the memory pressure.
	 */
	arc_no_grow = B_TRUE;

	/*
	 * Evict the requested number of pages by reducing arc_c and waiting
	 * for the requested amount of data to be evicted.  To avoid deadlock
	 * do not wait for eviction if we may be called from ZFS itself (see
	 * kmem_flags_convert() removing __GFP_FS).  It may cause excessive
	 * eviction later if many evictions are accumulated, but just skipping
	 * the eviction is not good either if most of memory is used by ARC.
	 */
	uint64_t to_free = arc_reduce_target_size(ptob(sc->nr_to_scan));
	if (sc->gfp_mask & __GFP_FS)
		arc_wait_for_eviction(to_free, B_FALSE, B_FALSE);
	if (current->reclaim_state != NULL)
#ifdef	HAVE_RECLAIM_STATE_RECLAIMED
		current->reclaim_state->reclaimed += btop(to_free);
#else
		current->reclaim_state->reclaimed_slab += btop(to_free);
#endif

	/*
	 * When direct reclaim is observed it usually indicates a rapid
	 * increase in memory pressure.  This occurs because the kswapd
	 * threads were unable to asynchronously keep enough free memory
	 * available.
	 */
	if (current_is_kswapd()) {
		ARCSTAT_BUMP(arcstat_memory_indirect_count);
	} else {
		ARCSTAT_BUMP(arcstat_memory_direct_count);
	}

	return (btop(to_free));
}

static struct shrinker *arc_shrinker = NULL;

int
arc_memory_throttle(spa_t *spa, uint64_t reserve, uint64_t txg)
{
	uint64_t free_memory = arc_free_memory();

	if (free_memory > arc_all_memory() * arc_lotsfree_percent / 100)
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
		    MAX(arc_sys_free / 4, free_memory) / 4) {
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

static void
arc_set_sys_free(uint64_t allmem)
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
	 * Note: If concurrent allocations consume these pages, there may
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
	 * on the amount of memory, using the same method as the kernel uses
	 * to calculate its internal `min_free_kbytes` variable.  See
	 * torvalds/linux@ee8eb9a5fe86 for the change in the upper clamp value
	 * from 64M to 256M.
	 */

	/*
	 * Base wmark_low is 4 * the square root of Kbytes of RAM.
	 */
	long wmark = int_sqrt(allmem / 1024 * 16) * 1024;

	/*
	 * Clamp to between 128K and 256/64MB.
	 */
	wmark = MAX(wmark, 128 * 1024);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
	wmark = MIN(wmark, 256 * 1024 * 1024);
#else
	wmark = MIN(wmark, 64 * 1024 * 1024);
#endif

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

void
arc_lowmem_init(void)
{
	uint64_t allmem = arc_all_memory();

	/*
	 * Register a shrinker to support synchronous (direct) memory
	 * reclaim from the arc.  This is done to prevent kswapd from
	 * swapping out pages when it is preferable to shrink the arc.
	 */
	arc_shrinker = spl_register_shrinker("zfs-arc-shrinker",
	    arc_shrinker_count, arc_shrinker_scan, zfs_arc_shrinker_seeks);
	VERIFY(arc_shrinker);

	arc_set_sys_free(allmem);
}

void
arc_lowmem_fini(void)
{
	spl_unregister_shrinker(arc_shrinker);
	arc_shrinker = NULL;
}

int
param_set_arc_u64(const char *buf, zfs_kernel_param_t *kp)
{
	int error;

	error = spl_param_set_u64(buf, kp);
	if (error < 0)
		return (SET_ERROR(error));

	arc_tuning_update(B_TRUE);

	return (0);
}

int
param_set_arc_min(const char *buf, zfs_kernel_param_t *kp)
{
	return (param_set_arc_u64(buf, kp));
}

int
param_set_arc_max(const char *buf, zfs_kernel_param_t *kp)
{
	return (param_set_arc_u64(buf, kp));
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

#ifdef CONFIG_MEMORY_HOTPLUG
static int
arc_hotplug_callback(struct notifier_block *self, unsigned long action,
    void *arg)
{
	(void) self, (void) arg;
	uint64_t allmem = arc_all_memory();
	if (action != MEM_ONLINE)
		return (NOTIFY_OK);

	arc_set_limits(allmem);

#ifdef __LP64__
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = MIN(4ULL * 1024 * 1024 * 1024,
		    allmem * zfs_dirty_data_max_max_percent / 100);
#else
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = MIN(1ULL * 1024 * 1024 * 1024,
		    allmem * zfs_dirty_data_max_max_percent / 100);
#endif

	arc_set_sys_free(allmem);
	return (NOTIFY_OK);
}
#endif

void
arc_register_hotplug(void)
{
#ifdef CONFIG_MEMORY_HOTPLUG
	arc_hotplug_callback_mem_nb.notifier_call = arc_hotplug_callback;
	/* There is no significance to the value 100 */
	arc_hotplug_callback_mem_nb.priority = 100;
	register_memory_notifier(&arc_hotplug_callback_mem_nb);
#endif
}

void
arc_unregister_hotplug(void)
{
#ifdef CONFIG_MEMORY_HOTPLUG
	unregister_memory_notifier(&arc_hotplug_callback_mem_nb);
#endif
}

ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, shrinker_limit, INT, ZMOD_RW,
	"Limit on number of pages that ARC shrinker can reclaim at once");
ZFS_MODULE_PARAM(zfs_arc, zfs_arc_, shrinker_seeks, INT, ZMOD_RD,
	"Relative cost of ARC eviction vs other kernel subsystems");
