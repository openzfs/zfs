// SPDX-License-Identifier: CDDL-1.0
/*
 * ARC OS hooks for NetBSD -- UVM memory accounting, write throttle,
 * and a low-memory shrinker analogue.
 *
 * NetBSD 11 has no vm_lowmem event or uvm_reclaim_hook_add.  The
 * eviction zthr already polls arc_available_memory(); the callout
 * below asks it to shrink when free pages drop below freemin, the
 * level that wakes the pagedaemon, instead of waiting for the next
 * poll.  freetarg is only where that scan stops.  Free pages sit
 * there during ordinary I/O, so it is not low memory.
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/spa_impl.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/arc_os.h>
#include <sys/arc_impl.h>
#include <sys/kstat.h>
#include <sys/dmu_tx.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/workqueue.h>
#include <sys/callout.h>
#include <sys/atomic.h>
#include <uvm/uvm.h>
#include <uvm/uvm_extern.h>

uint_t zfs_arc_free_target = 0;

ZFS_MODULE_PARAM_CALL(zfs_arc, zfs_arc_, free_target,
    param_set_arc_free_target, 0, CTLFLAG_RW,
	"Desired number of free pages below which ARC triggers reclaim");

static struct callout arc_lowmem_ch;
static struct workqueue *arc_lowmem_wq;
static struct work arc_lowmem_work;
static unsigned int arc_lowmem_queued;

static uint64_t
arc_free_pages(void)
{
	int fp;

	fp = uvm_availmem(true);
	if (fp < 0)
		fp = 0;
	return ((uint64_t)fp);
}

uint64_t
arc_free_memory(void)
{
	return (ptoa(arc_free_pages()));
}

int64_t
arc_available_memory(void)
{
	int64_t lowest, n;

	/*
	 * Convert both sides to bytes before subtracting so a
	 * deficit is a negative byte count, not a left-shift of a
	 * negative (undefined in C).
	 */
	n = (int64_t)arc_free_memory() -
	    (int64_t)ptoa((uint64_t)zfs_arc_free_target);
	lowest = n;

#ifndef _LP64
	/*
	 * 32-bit KVA can exhaust while physical pages remain.
	 * Reclaim when less than 1/4 of kmem_arena is free.
	 */
	if (kmem_arena != NULL) {
		vmem_size_t kfree, ktot;

		kfree = vmem_size(kmem_arena, VMEM_FREE);
		ktot = vmem_size(kmem_arena, VMEM_FREE | VMEM_ALLOC);
		n = (int64_t)kfree - (int64_t)(ktot / 4);
		if (n < lowest)
			lowest = n;
	}
#endif
	if (lowest > 0 && uvm_km_va_starved_p())
		lowest = -1;

	return (lowest);
}

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

uint64_t
arc_all_memory(void)
{
	return (ptoa((uint64_t)physmem));
}

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
	 * Pageout already has the machine tight.  Keep writing so
	 * eviction can make progress, but cap in-flight reserve.
	 */
	if (uvm.pagedaemon_lwp != NULL && curlwp == uvm.pagedaemon_lwp) {
		if (spa->spa_lowmem_page_load >
		    MAX(ptoa((uint64_t)uvmexp.freemin), free_memory) / 4) {
			DMU_TX_STAT_BUMP(dmu_tx_memory_reclaim);
			return (SET_ERROR(ERESTART));
		}
		atomic_add_64(&spa->spa_lowmem_page_load, reserve / 8);
		return (0);
	} else if (spa->spa_lowmem_page_load > 0 && arc_reclaim_needed()) {
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		DMU_TX_STAT_BUMP(dmu_tx_memory_reclaim);
		return (SET_ERROR(EAGAIN));
	}
	spa->spa_lowmem_page_load = 0;
	return (0);
}

static void
arc_lowmem(void)
{
	int64_t can_free, free_memory, to_free;

	arc_no_grow = B_TRUE;
	arc_warm = B_TRUE;
	arc_growtime = gethrtime() + SEC2NSEC(arc_grow_retry);

	free_memory = arc_available_memory();
	can_free = (int64_t)arc_c - (int64_t)arc_c_min;
	to_free = (MAX(can_free, 0) >> arc_shrink_shift) - MIN(free_memory, 0);
	to_free = (int64_t)arc_reduce_target_size((uint64_t)MAX(to_free, 0));
	ARCSTAT_BUMP(arcstat_memory_direct_count);
	(void) to_free;
}

static void
arc_lowmem_work_cb(struct work *wk, void *cookie)
{
	(void) wk;
	(void) cookie;
	arc_lowmem();
	(void) atomic_swap_uint(&arc_lowmem_queued, 0);
}

static void
arc_lowmem_tick(void *arg)
{
	int64_t n;

	(void) arg;
	n = (int64_t)arc_free_memory() -
	    (int64_t)ptoa((uint64_t)zfs_arc_free_target);
	if (n < 0 && arc_lowmem_wq != NULL &&
	    atomic_cas_uint(&arc_lowmem_queued, 0, 1) == 0)
		workqueue_enqueue(arc_lowmem_wq, &arc_lowmem_work, NULL);
	callout_schedule(&arc_lowmem_ch, hz);
}

void
arc_lowmem_init(void)
{
	int error;

	/*
	 * freemin wakes the pagedaemon.  freetarg is where it stops,
	 * and free pages sit there during ordinary file I/O.  Illumos
	 * compares against lotsfree, the pageout-start level.  A
	 * freetarg default makes arc_reclaim_needed() true across that
	 * whole band, so the L2ARC writer skips its passes.
	 */
	if (zfs_arc_free_target == 0) {
		if (uvmexp.freemin != 0)
			zfs_arc_free_target = uvmexp.freemin;
		else
			zfs_arc_free_target = uvmexp.freetarg;
	}

	error = workqueue_create(&arc_lowmem_wq, "zarclow",
	    arc_lowmem_work_cb, NULL, PRI_NONE, IPL_SOFTCLOCK, WQ_MPSAFE);
	if (error != 0) {
		arc_lowmem_wq = NULL;
		return;
	}

	callout_init(&arc_lowmem_ch, CALLOUT_MPSAFE);
	callout_reset(&arc_lowmem_ch, hz, arc_lowmem_tick, NULL);
}

void
arc_lowmem_fini(void)
{
	if (arc_lowmem_wq == NULL)
		return;
	callout_halt(&arc_lowmem_ch, NULL);
	callout_destroy(&arc_lowmem_ch);
	workqueue_wait(arc_lowmem_wq, &arc_lowmem_work);
	workqueue_destroy(arc_lowmem_wq);
	arc_lowmem_wq = NULL;
}

void
arc_register_hotplug(void)
{
}

void
arc_unregister_hotplug(void)
{
}
