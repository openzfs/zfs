/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/kmem.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_KMEM

/*
 * Memory allocation interfaces and debugging for basic kmem_*
 * and vmem_* style memory allocation.  When DEBUG_KMEM is enable
 * all allocations will be tracked when they are allocated and
 * freed.  When the SPL module is unload a list of all leaked
 * addresses and where they were allocated will be dumped to the
 * console.  Enabling this feature has a significant impant on
 * performance but it makes finding memory leaks staight forward.
 */
#ifdef DEBUG_KMEM
/* Shim layer memory accounting */
atomic64_t kmem_alloc_used;
unsigned long kmem_alloc_max = 0;
atomic64_t vmem_alloc_used;
unsigned long vmem_alloc_max = 0;
int kmem_warning_flag = 1;

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);
EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);
EXPORT_SYMBOL(kmem_warning_flag);

#ifdef DEBUG_KMEM_TRACKING
spinlock_t kmem_lock;
struct hlist_head kmem_table[KMEM_TABLE_SIZE];
struct list_head kmem_list;

spinlock_t vmem_lock;
struct hlist_head vmem_table[VMEM_TABLE_SIZE];
struct list_head vmem_list;

EXPORT_SYMBOL(kmem_lock);
EXPORT_SYMBOL(kmem_table);
EXPORT_SYMBOL(kmem_list);

EXPORT_SYMBOL(vmem_lock);
EXPORT_SYMBOL(vmem_table);
EXPORT_SYMBOL(vmem_list);
#endif

int kmem_set_warning(int flag) { return (kmem_warning_flag = !!flag); }
#else
int kmem_set_warning(int flag) { return 0; }
#endif
EXPORT_SYMBOL(kmem_set_warning);

/*
 * Slab allocation interfaces
 *
 * While the Linux slab implementation was inspired by the Solaris
 * implemenation I cannot use it to emulate the Solaris APIs.  I
 * require two features which are not provided by the Linux slab.
 *
 * 1) Constructors AND destructors.  Recent versions of the Linux
 *    kernel have removed support for destructors.  This is a deal
 *    breaker for the SPL which contains particularly expensive
 *    initializers for mutex's, condition variables, etc.  We also
 *    require a minimal level of cleaner for these data types unlike
 *    may Linux data type which do need to be explicitly destroyed.
 *
 * 2) Virtual address backed slab.  Callers of the Solaris slab
 *    expect it to work well for both small are very large allocations.
 *    Because of memory fragmentation the Linux slab which is backed
 *    by kmalloc'ed memory performs very badly when confronted with
 *    large numbers of large allocations.  Basing the slab on the
 *    virtual address space removes the need for contigeous pages
 *    and greatly improve performance for large allocations.
 *
 * For these reasons, the SPL has its own slab implementation with
 * the needed features.  It is not as highly optimized as either the
 * Solaris or Linux slabs, but it should get me most of what is
 * needed until it can be optimized or obsoleted by another approach.
 *
 * One serious concern I do have about this method is the relatively
 * small virtual address space on 32bit arches.  This will seriously
 * constrain the size of the slab caches and their performance.
 *
 * XXX: Implement work requests to keep an eye on each cache and
 *      shrink them via spl_slab_reclaim() when they are wasting lots
 *      of space.  Currently this process is driven by the reapers.
 *
 * XXX: Improve the partial slab list by carefully maintaining a
 *      strict ordering of fullest to emptiest slabs based on
 *      the slab reference count.  This gaurentees the when freeing
 *      slabs back to the system we need only linearly traverse the
 *      last N slabs in the list to discover all the freeable slabs.
 *
 * XXX: NUMA awareness for optionally allocating memory close to a
 *      particular core.  This can be adventageous if you know the slab
 *      object will be short lived and primarily accessed from one core.
 *
 * XXX: Slab coloring may also yield performance improvements and would
 *      be desirable to implement.
 *
 * XXX: Proper hardware cache alignment would be good too.
 */

struct list_head spl_kmem_cache_list;	/* List of caches */
struct rw_semaphore spl_kmem_cache_sem;	/* Cache list lock */

static int spl_cache_flush(spl_kmem_cache_t *skc,
			   spl_kmem_magazine_t *skm, int flush);

#ifdef HAVE_SET_SHRINKER
static struct shrinker *spl_kmem_cache_shrinker;
#else
static int spl_kmem_cache_generic_shrinker(int nr_to_scan,
					   unsigned int gfp_mask);
static struct shrinker spl_kmem_cache_shrinker = {
	.shrink = spl_kmem_cache_generic_shrinker,
	.seeks = KMC_DEFAULT_SEEKS,
};
#endif

static void *
kv_alloc(spl_kmem_cache_t *skc, int size, int flags)
{
	void *ptr;

	if (skc->skc_flags & KMC_KMEM) {
		if (size > (2 * PAGE_SIZE)) {
			ptr = (void *)__get_free_pages(flags, get_order(size));
		} else
			ptr = kmem_alloc(size, flags);
	} else {
		ptr = vmem_alloc(size, flags);
	}

	return ptr;
}

static void
kv_free(spl_kmem_cache_t *skc, void *ptr, int size)
{
	if (skc->skc_flags & KMC_KMEM) {
		if (size > (2 * PAGE_SIZE))
			free_pages((unsigned long)ptr, get_order(size));
		else
			kmem_free(ptr, size);
	} else {
		vmem_free(ptr, size);
	}
}

static spl_kmem_slab_t *
spl_slab_alloc(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_slab_t *sks;
	spl_kmem_obj_t *sko, *n;
	void *base, *obj;
	int i, size, rc = 0;

	/* It's important that we pack the spl_kmem_obj_t structure
	 * and the actual objects in to one large address space
	 * to minimize the number of calls to the allocator.  It
	 * is far better to do a few large allocations and then
	 * subdivide it ourselves.  Now which allocator we use
	 * requires balancling a few trade offs.
	 *
	 * For small objects we use kmem_alloc() because as long
	 * as you are only requesting a small number of pages
	 * (ideally just one) its cheap.  However, when you start
	 * requesting multiple pages kmem_alloc() get increasingly
	 * expensive since it requires contigeous pages.  For this
	 * reason we shift to vmem_alloc() for slabs of large
	 * objects which removes the need for contigeous pages.
	 * We do not use vmem_alloc() in all cases because there
	 * is significant locking overhead in __get_vm_area_node().
	 * This function takes a single global lock when aquiring
	 * an available virtual address range which serialize all
	 * vmem_alloc()'s for all slab caches.  Using slightly
	 * different allocation functions for small and large
	 * objects should give us the best of both worlds.
	 *
	 * sks struct:  sizeof(spl_kmem_slab_t)
	 * obj data:    skc->skc_obj_size
	 * obj struct:  sizeof(spl_kmem_obj_t)
	 * <N obj data + obj structs>
	 *
	 * XXX: It would probably be a good idea to more carefully
	 *      align these data structures in memory.
	 */
	base = kv_alloc(skc, skc->skc_slab_size, flags);
	if (base == NULL)
		RETURN(NULL);

	sks = (spl_kmem_slab_t *)base;
	sks->sks_magic = SKS_MAGIC;
	sks->sks_objs = skc->skc_slab_objs;
	sks->sks_age = jiffies;
	sks->sks_cache = skc;
	INIT_LIST_HEAD(&sks->sks_list);
	INIT_LIST_HEAD(&sks->sks_free_list);
	sks->sks_ref = 0;
	size = sizeof(spl_kmem_obj_t) + skc->skc_obj_size;

	for (i = 0; i < sks->sks_objs; i++) {
		if (skc->skc_flags & KMC_OFFSLAB) {
			obj = kv_alloc(skc, size, flags);
			if (!obj)
				GOTO(out, rc = -ENOMEM);
		} else {
			obj = base + sizeof(spl_kmem_slab_t) + i * size;
		}

		sko = obj + skc->skc_obj_size;
		sko->sko_addr = obj;
		sko->sko_magic = SKO_MAGIC;
		sko->sko_slab = sks;
		INIT_LIST_HEAD(&sko->sko_list);
		list_add_tail(&sko->sko_list, &sks->sks_free_list);
	}

	list_for_each_entry(sko, &sks->sks_free_list, sko_list)
		if (skc->skc_ctor)
			skc->skc_ctor(sko->sko_addr, skc->skc_private, flags);
out:
	if (rc) {
		if (skc->skc_flags & KMC_OFFSLAB)
			list_for_each_entry_safe(sko,n,&sks->sks_free_list,sko_list)
				kv_free(skc, sko->sko_addr, size);

		kv_free(skc, base, skc->skc_slab_size);
		sks = NULL;
	}

	RETURN(sks);
}

/* Removes slab from complete or partial list, so it must
 * be called with the 'skc->skc_lock' held.
 */
static void
spl_slab_free(spl_kmem_slab_t *sks) {
	spl_kmem_cache_t *skc;
	spl_kmem_obj_t *sko, *n;
	int size;
	ENTRY;

	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(sks->sks_ref == 0);

	skc = sks->sks_cache;
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

	skc->skc_obj_total -= sks->sks_objs;
	skc->skc_slab_total--;
	list_del(&sks->sks_list);
	size = sizeof(spl_kmem_obj_t) + skc->skc_obj_size;

	/* Run destructors slab is being released */
	list_for_each_entry_safe(sko, n, &sks->sks_free_list, sko_list) {
		ASSERT(sko->sko_magic == SKO_MAGIC);

		if (skc->skc_dtor)
			skc->skc_dtor(sko->sko_addr, skc->skc_private);

		if (skc->skc_flags & KMC_OFFSLAB)
			kv_free(skc, sko->sko_addr, size);
	}

	kv_free(skc, sks, skc->skc_slab_size);
	EXIT;
}

static int
__spl_slab_reclaim(spl_kmem_cache_t *skc)
{
	spl_kmem_slab_t *sks, *m;
	int rc = 0;
	ENTRY;

	ASSERT(spin_is_locked(&skc->skc_lock));
	/*
	 * Free empty slabs which have not been touched in skc_delay
	 * seconds.  This delay time is important to avoid thrashing.
	 * Empty slabs will be at the end of the skc_partial_list.
	 */
        list_for_each_entry_safe_reverse(sks, m, &skc->skc_partial_list,
					 sks_list) {
		if (sks->sks_ref > 0)
		       break;

		if (time_after(jiffies, sks->sks_age + skc->skc_delay * HZ)) {
			spl_slab_free(sks);
			rc++;
		}
	}

	/* Returns number of slabs reclaimed */
	RETURN(rc);
}

static int
spl_slab_reclaim(spl_kmem_cache_t *skc)
{
	int rc;
	ENTRY;

	spin_lock(&skc->skc_lock);
	rc = __spl_slab_reclaim(skc);
	spin_unlock(&skc->skc_lock);

	RETURN(rc);
}

static int
spl_magazine_size(spl_kmem_cache_t *skc)
{
	int size;
	ENTRY;

	/* Guesses for reasonable magazine sizes, they
	 * should really adapt based on observed usage. */
	if (skc->skc_obj_size > (PAGE_SIZE * 256))
		size = 4;
	else if (skc->skc_obj_size > (PAGE_SIZE * 32))
		size = 16;
	else if (skc->skc_obj_size > (PAGE_SIZE))
		size = 64;
	else if (skc->skc_obj_size > (PAGE_SIZE / 4))
		size = 128;
	else
		size = 512;

	RETURN(size);
}

static spl_kmem_magazine_t *
spl_magazine_alloc(spl_kmem_cache_t *skc, int node)
{
	spl_kmem_magazine_t *skm;
	int size = sizeof(spl_kmem_magazine_t) +
	           sizeof(void *) * skc->skc_mag_size;
	ENTRY;

	skm = kmalloc_node(size, GFP_KERNEL, node);
	if (skm) {
		skm->skm_magic = SKM_MAGIC;
		skm->skm_avail = 0;
		skm->skm_size = skc->skc_mag_size;
		skm->skm_refill = skc->skc_mag_refill;
		if (!(skc->skc_flags & KMC_NOTOUCH))
			skm->skm_age = jiffies;
	}

	RETURN(skm);
}

static void
spl_magazine_free(spl_kmem_magazine_t *skm)
{
	ENTRY;
	ASSERT(skm->skm_magic == SKM_MAGIC);
	ASSERT(skm->skm_avail == 0);
	kfree(skm);
	EXIT;
}

static int
spl_magazine_create(spl_kmem_cache_t *skc)
{
	int i;
	ENTRY;

	skc->skc_mag_size = spl_magazine_size(skc);
	skc->skc_mag_refill = (skc->skc_mag_size + 1)  / 2;

	for_each_online_cpu(i) {
		skc->skc_mag[i] = spl_magazine_alloc(skc, cpu_to_node(i));
		if (!skc->skc_mag[i]) {
			for (i--; i >= 0; i--)
				spl_magazine_free(skc->skc_mag[i]);

			RETURN(-ENOMEM);
		}
	}

	RETURN(0);
}

static void
spl_magazine_destroy(spl_kmem_cache_t *skc)
{
        spl_kmem_magazine_t *skm;
	int i;
	ENTRY;

	for_each_online_cpu(i) {
		skm = skc->skc_mag[i];
		(void)spl_cache_flush(skc, skm, skm->skm_avail);
		spl_magazine_free(skm);
	}

	EXIT;
}

spl_kmem_cache_t *
spl_kmem_cache_create(char *name, size_t size, size_t align,
                      spl_kmem_ctor_t ctor,
                      spl_kmem_dtor_t dtor,
                      spl_kmem_reclaim_t reclaim,
                      void *priv, void *vmp, int flags)
{
        spl_kmem_cache_t *skc;
	uint32_t slab_max, slab_size, slab_objs;
	int rc, kmem_flags = KM_SLEEP;
	ENTRY;

	ASSERTF(!(flags & KMC_NOMAGAZINE), "Bad KMC_NOMAGAZINE (%x)\n", flags);
	ASSERTF(!(flags & KMC_NOHASH), "Bad KMC_NOHASH (%x)\n", flags);
	ASSERTF(!(flags & KMC_QCACHE), "Bad KMC_QCACHE (%x)\n", flags);

        /* We may be called when there is a non-zero preempt_count or
         * interrupts are disabled is which case we must not sleep.
	 */
	if (current_thread_info()->preempt_count || irqs_disabled())
		kmem_flags = KM_NOSLEEP;

	/* Allocate new cache memory and initialize. */
	skc = (spl_kmem_cache_t *)kmem_zalloc(sizeof(*skc), kmem_flags);
	if (skc == NULL)
		RETURN(NULL);

	skc->skc_magic = SKC_MAGIC;
	skc->skc_name_size = strlen(name) + 1;
	skc->skc_name = (char *)kmem_alloc(skc->skc_name_size, kmem_flags);
	if (skc->skc_name == NULL) {
		kmem_free(skc, sizeof(*skc));
		RETURN(NULL);
	}
	strncpy(skc->skc_name, name, skc->skc_name_size);

	skc->skc_ctor = ctor;
	skc->skc_dtor = dtor;
	skc->skc_reclaim = reclaim;
	skc->skc_private = priv;
	skc->skc_vmp = vmp;
	skc->skc_flags = flags;
	skc->skc_obj_size = size;
	skc->skc_delay = SPL_KMEM_CACHE_DELAY;

	INIT_LIST_HEAD(&skc->skc_list);
	INIT_LIST_HEAD(&skc->skc_complete_list);
	INIT_LIST_HEAD(&skc->skc_partial_list);
	spin_lock_init(&skc->skc_lock);
	skc->skc_slab_fail = 0;
	skc->skc_slab_create = 0;
	skc->skc_slab_destroy = 0;
	skc->skc_slab_total = 0;
	skc->skc_slab_alloc = 0;
	skc->skc_slab_max = 0;
	skc->skc_obj_total = 0;
	skc->skc_obj_alloc = 0;
	skc->skc_obj_max = 0;

	/* If none passed select a cache type based on object size */
	if (!(skc->skc_flags & (KMC_KMEM | KMC_VMEM))) {
		if (skc->skc_obj_size < (PAGE_SIZE / 8)) {
			skc->skc_flags |= KMC_KMEM;
		} else {
			skc->skc_flags |= KMC_VMEM;
		}
	}

	/* Size slabs properly so ensure they are not too large */
	slab_max = ((uint64_t)1 << (MAX_ORDER - 1)) * PAGE_SIZE;
	if (skc->skc_flags & KMC_OFFSLAB) {
		skc->skc_slab_objs = SPL_KMEM_CACHE_OBJ_PER_SLAB;
		skc->skc_slab_size = sizeof(spl_kmem_slab_t);
		ASSERT(skc->skc_obj_size < slab_max);
	} else {
		slab_objs = SPL_KMEM_CACHE_OBJ_PER_SLAB + 1;

		do {
			slab_objs--;
			slab_size = sizeof(spl_kmem_slab_t) + slab_objs *
			            (skc->skc_obj_size+sizeof(spl_kmem_obj_t));
		} while (slab_size > slab_max);

		skc->skc_slab_objs = slab_objs;
		skc->skc_slab_size = slab_size;
	}

	rc = spl_magazine_create(skc);
	if (rc) {
		kmem_free(skc->skc_name, skc->skc_name_size);
		kmem_free(skc, sizeof(*skc));
		RETURN(NULL);
	}

	down_write(&spl_kmem_cache_sem);
	list_add_tail(&skc->skc_list, &spl_kmem_cache_list);
	up_write(&spl_kmem_cache_sem);

	RETURN(skc);
}
EXPORT_SYMBOL(spl_kmem_cache_create);

void
spl_kmem_cache_destroy(spl_kmem_cache_t *skc)
{
        spl_kmem_slab_t *sks, *m;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	down_write(&spl_kmem_cache_sem);
	list_del_init(&skc->skc_list);
	up_write(&spl_kmem_cache_sem);

	spl_magazine_destroy(skc);
	spin_lock(&skc->skc_lock);

	/* Validate there are no objects in use and free all the
	 * spl_kmem_slab_t, spl_kmem_obj_t, and object buffers. */
	ASSERT(list_empty(&skc->skc_complete_list));
	ASSERT(skc->skc_slab_alloc == 0);
	ASSERT(skc->skc_obj_alloc == 0);

	list_for_each_entry_safe(sks, m, &skc->skc_partial_list, sks_list)
		spl_slab_free(sks);

	ASSERT(skc->skc_slab_total == 0);
	ASSERT(skc->skc_obj_total == 0);

	kmem_free(skc->skc_name, skc->skc_name_size);
	spin_unlock(&skc->skc_lock);

	kmem_free(skc, sizeof(*skc));

	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_destroy);

static void *
spl_cache_obj(spl_kmem_cache_t *skc, spl_kmem_slab_t *sks)
{
	spl_kmem_obj_t *sko;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

	sko = list_entry(sks->sks_free_list.next, spl_kmem_obj_t, sko_list);
	ASSERT(sko->sko_magic == SKO_MAGIC);
	ASSERT(sko->sko_addr != NULL);

	/* Remove from sks_free_list */
	list_del_init(&sko->sko_list);

	sks->sks_age = jiffies;
	sks->sks_ref++;
	skc->skc_obj_alloc++;

	/* Track max obj usage statistics */
	if (skc->skc_obj_alloc > skc->skc_obj_max)
		skc->skc_obj_max = skc->skc_obj_alloc;

	/* Track max slab usage statistics */
	if (sks->sks_ref == 1) {
		skc->skc_slab_alloc++;

		if (skc->skc_slab_alloc > skc->skc_slab_max)
			skc->skc_slab_max = skc->skc_slab_alloc;
	}

	return sko->sko_addr;
}

/* No available objects create a new slab.  Since this is an
 * expensive operation we do it without holding the spinlock
 * and only briefly aquire it when we link in the fully
 * allocated and constructed slab.
 */
static spl_kmem_slab_t *
spl_cache_grow(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_slab_t *sks;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	if (flags & __GFP_WAIT) {
		flags |= __GFP_NOFAIL;
		local_irq_enable();
		might_sleep();
	}

	sks = spl_slab_alloc(skc, flags);
	if (sks == NULL) {
	        if (flags & __GFP_WAIT)
			local_irq_disable();

		RETURN(NULL);
	}

	if (flags & __GFP_WAIT)
		local_irq_disable();

	/* Link the new empty slab in to the end of skc_partial_list */
	spin_lock(&skc->skc_lock);
	skc->skc_slab_total++;
	skc->skc_obj_total += sks->sks_objs;
	list_add_tail(&sks->sks_list, &skc->skc_partial_list);
	spin_unlock(&skc->skc_lock);

	RETURN(sks);
}

static int
spl_cache_refill(spl_kmem_cache_t *skc, spl_kmem_magazine_t *skm, int flags)
{
	spl_kmem_slab_t *sks;
	int rc = 0, refill;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skm->skm_magic == SKM_MAGIC);

	/* XXX: Check for refill bouncing by age perhaps */
	refill = MIN(skm->skm_refill, skm->skm_size - skm->skm_avail);

	spin_lock(&skc->skc_lock);

	while (refill > 0) {
		/* No slabs available we must grow the cache */
		if (list_empty(&skc->skc_partial_list)) {
			spin_unlock(&skc->skc_lock);

			sks = spl_cache_grow(skc, flags);
			if (!sks)
				GOTO(out, rc);

			/* Rescheduled to different CPU skm is not local */
			if (skm != skc->skc_mag[smp_processor_id()])
				GOTO(out, rc);

			/* Potentially rescheduled to the same CPU but
			 * allocations may have occured from this CPU while
			 * we were sleeping so recalculate max refill. */
			refill = MIN(refill, skm->skm_size - skm->skm_avail);

			spin_lock(&skc->skc_lock);
			continue;
		}

		/* Grab the next available slab */
		sks = list_entry((&skc->skc_partial_list)->next,
		                 spl_kmem_slab_t, sks_list);
		ASSERT(sks->sks_magic == SKS_MAGIC);
		ASSERT(sks->sks_ref < sks->sks_objs);
		ASSERT(!list_empty(&sks->sks_free_list));

		/* Consume as many objects as needed to refill the requested
		 * cache.  We must also be careful not to overfill it. */
		while (sks->sks_ref < sks->sks_objs && refill-- > 0 && ++rc) {
			ASSERT(skm->skm_avail < skm->skm_size);
			ASSERT(rc < skm->skm_size);
			skm->skm_objs[skm->skm_avail++]=spl_cache_obj(skc,sks);
		}

		/* Move slab to skc_complete_list when full */
		if (sks->sks_ref == sks->sks_objs) {
			list_del(&sks->sks_list);
			list_add(&sks->sks_list, &skc->skc_complete_list);
		}
	}

	spin_unlock(&skc->skc_lock);
out:
	/* Returns the number of entries added to cache */
	RETURN(rc);
}

static void
spl_cache_shrink(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_slab_t *sks = NULL;
	spl_kmem_obj_t *sko = NULL;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

	sko = obj + skc->skc_obj_size;
	ASSERT(sko->sko_magic == SKO_MAGIC);

	sks = sko->sko_slab;
	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(sks->sks_cache == skc);
	list_add(&sko->sko_list, &sks->sks_free_list);

	sks->sks_age = jiffies;
	sks->sks_ref--;
	skc->skc_obj_alloc--;

	/* Move slab to skc_partial_list when no longer full.  Slabs
	 * are added to the head to keep the partial list is quasi-full
	 * sorted order.  Fuller at the head, emptier at the tail. */
	if (sks->sks_ref == (sks->sks_objs - 1)) {
		list_del(&sks->sks_list);
		list_add(&sks->sks_list, &skc->skc_partial_list);
	}

	/* Move emply slabs to the end of the partial list so
	 * they can be easily found and freed during reclamation. */
	if (sks->sks_ref == 0) {
		list_del(&sks->sks_list);
		list_add_tail(&sks->sks_list, &skc->skc_partial_list);
		skc->skc_slab_alloc--;
	}

	EXIT;
}

static int
spl_cache_flush(spl_kmem_cache_t *skc, spl_kmem_magazine_t *skm, int flush)
{
	int i, count = MIN(flush, skm->skm_avail);
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skm->skm_magic == SKM_MAGIC);

	spin_lock(&skc->skc_lock);

	for (i = 0; i < count; i++)
		spl_cache_shrink(skc, skm->skm_objs[i]);

//	__spl_slab_reclaim(skc);
	skm->skm_avail -= count;
	memmove(skm->skm_objs, &(skm->skm_objs[count]),
	        sizeof(void *) * skm->skm_avail);

	spin_unlock(&skc->skc_lock);

	RETURN(count);
}

void *
spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_magazine_t *skm;
	unsigned long irq_flags;
	void *obj = NULL;
	int id;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(flags & KM_SLEEP); /* XXX: KM_NOSLEEP not yet supported */
	local_irq_save(irq_flags);

restart:
	/* Safe to update per-cpu structure without lock, but
	 * in the restart case we must be careful to reaquire
	 * the local magazine since this may have changed
	 * when we need to grow the cache. */
	id = smp_processor_id();
	ASSERTF(id < 4, "cache=%p smp_processor_id=%d\n", skc, id);
	skm = skc->skc_mag[smp_processor_id()];
	ASSERTF(skm->skm_magic == SKM_MAGIC, "%x != %x: %s/%p/%p %x/%x/%x\n",
		skm->skm_magic, SKM_MAGIC, skc->skc_name, skc, skm,
		skm->skm_size, skm->skm_refill, skm->skm_avail);

	if (likely(skm->skm_avail)) {
		/* Object available in CPU cache, use it */
		obj = skm->skm_objs[--skm->skm_avail];
		if (!(skc->skc_flags & KMC_NOTOUCH))
			skm->skm_age = jiffies;
	} else {
		/* Per-CPU cache empty, directly allocate from
		 * the slab and refill the per-CPU cache. */
		(void)spl_cache_refill(skc, skm, flags);
		GOTO(restart, obj = NULL);
	}

	local_irq_restore(irq_flags);
	ASSERT(obj);

	/* Pre-emptively migrate object to CPU L1 cache */
	prefetchw(obj);

	RETURN(obj);
}
EXPORT_SYMBOL(spl_kmem_cache_alloc);

void
spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_magazine_t *skm;
	unsigned long flags;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	local_irq_save(flags);

	/* Safe to update per-cpu structure without lock, but
	 * no remote memory allocation tracking is being performed
	 * it is entirely possible to allocate an object from one
	 * CPU cache and return it to another. */
	skm = skc->skc_mag[smp_processor_id()];
	ASSERT(skm->skm_magic == SKM_MAGIC);

	/* Per-CPU cache full, flush it to make space */
	if (unlikely(skm->skm_avail >= skm->skm_size))
		(void)spl_cache_flush(skc, skm, skm->skm_refill);

	/* Available space in cache, use it */
	skm->skm_objs[skm->skm_avail++] = obj;

	local_irq_restore(flags);

	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_free);

static int
spl_kmem_cache_generic_shrinker(int nr_to_scan, unsigned int gfp_mask)
{
	spl_kmem_cache_t *skc;

	/* Under linux a shrinker is not tightly coupled with a slab
	 * cache.  In fact linux always systematically trys calling all
	 * registered shrinker callbacks until its target reclamation level
	 * is reached.  Because of this we only register one shrinker
	 * function in the shim layer for all slab caches.  And we always
	 * attempt to shrink all caches when this generic shrinker is called.
	 */
	down_read(&spl_kmem_cache_sem);

	list_for_each_entry(skc, &spl_kmem_cache_list, skc_list)
		spl_kmem_cache_reap_now(skc);

	up_read(&spl_kmem_cache_sem);

	/* XXX: Under linux we should return the remaining number of
	 * entries in the cache.  We should do this as well.
	 */
	return 1;
}

void
spl_kmem_cache_reap_now(spl_kmem_cache_t *skc)
{
	spl_kmem_magazine_t *skm;
	int i;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	if (skc->skc_reclaim)
		skc->skc_reclaim(skc->skc_private);

	/* Ensure per-CPU caches which are idle gradually flush */
	for_each_online_cpu(i) {
		skm = skc->skc_mag[i];

		if (time_after(jiffies, skm->skm_age + skc->skc_delay * HZ))
			(void)spl_cache_flush(skc, skm, skm->skm_refill);
	}

	spl_slab_reclaim(skc);

	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_reap_now);

void
spl_kmem_reap(void)
{
	spl_kmem_cache_generic_shrinker(KMC_REAP_CHUNK, GFP_KERNEL);
}
EXPORT_SYMBOL(spl_kmem_reap);

#if defined(DEBUG_KMEM) && defined(DEBUG_KMEM_TRACKING)
static char *
spl_sprintf_addr(kmem_debug_t *kd, char *str, int len, int min)
{
	int size = ((len - 1) < kd->kd_size) ? (len - 1) : kd->kd_size;
	int i, flag = 1;

	ASSERT(str != NULL && len >= 17);
	memset(str, 0, len);

	/* Check for a fully printable string, and while we are at
         * it place the printable characters in the passed buffer. */
	for (i = 0; i < size; i++) {
		str[i] = ((char *)(kd->kd_addr))[i];
		if (isprint(str[i])) {
			continue;
		} else {
			/* Minimum number of printable characters found
			 * to make it worthwhile to print this as ascii. */
			if (i > min)
				break;

			flag = 0;
			break;
		}
	}

	if (!flag) {
		sprintf(str, "%02x%02x%02x%02x%02x%02x%02x%02x",
		        *((uint8_t *)kd->kd_addr),
		        *((uint8_t *)kd->kd_addr + 2),
		        *((uint8_t *)kd->kd_addr + 4),
		        *((uint8_t *)kd->kd_addr + 6),
		        *((uint8_t *)kd->kd_addr + 8),
		        *((uint8_t *)kd->kd_addr + 10),
		        *((uint8_t *)kd->kd_addr + 12),
		        *((uint8_t *)kd->kd_addr + 14));
	}

	return str;
}

static int
spl_kmem_init_tracking(struct list_head *list, spinlock_t *lock, int size)
{
	int i;
	ENTRY;

	spin_lock_init(lock);
	INIT_LIST_HEAD(list);

	for (i = 0; i < size; i++)
		INIT_HLIST_HEAD(&kmem_table[i]);

	RETURN(0);
}

static void
spl_kmem_fini_tracking(struct list_head *list, spinlock_t *lock)
{
	unsigned long flags;
	kmem_debug_t *kd;
	char str[17];
	ENTRY;

	spin_lock_irqsave(lock, flags);
	if (!list_empty(list))
		CDEBUG(D_WARNING, "%-16s %-5s %-16s %s:%s\n",
		       "address", "size", "data", "func", "line");

	list_for_each_entry(kd, list, kd_list)
		CDEBUG(D_WARNING, "%p %-5d %-16s %s:%d\n",
		       kd->kd_addr, kd->kd_size,
		       spl_sprintf_addr(kd, str, 17, 8),
		       kd->kd_func, kd->kd_line);

	spin_unlock_irqrestore(lock, flags);
	EXIT;
}
#else /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */
#define spl_kmem_init_tracking(list, lock, size)
#define spl_kmem_fini_tracking(list, lock)
#endif /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */

int
spl_kmem_init(void)
{
	int rc = 0;
	ENTRY;

	init_rwsem(&spl_kmem_cache_sem);
	INIT_LIST_HEAD(&spl_kmem_cache_list);

#ifdef HAVE_SET_SHRINKER
	spl_kmem_cache_shrinker = set_shrinker(KMC_DEFAULT_SEEKS,
					       spl_kmem_cache_generic_shrinker);
	if (spl_kmem_cache_shrinker == NULL)
		RETURN(rc = -ENOMEM);
#else
	register_shrinker(&spl_kmem_cache_shrinker);
#endif

#ifdef DEBUG_KMEM
	atomic64_set(&kmem_alloc_used, 0);
	atomic64_set(&vmem_alloc_used, 0);

	spl_kmem_init_tracking(&kmem_list, &kmem_lock, KMEM_TABLE_SIZE);
	spl_kmem_init_tracking(&vmem_list, &vmem_lock, VMEM_TABLE_SIZE);
#endif
	RETURN(rc);
}

void
spl_kmem_fini(void)
{
#ifdef DEBUG_KMEM
	/* Display all unreclaimed memory addresses, including the
	 * allocation size and the first few bytes of what's located
	 * at that address to aid in debugging.  Performance is not
	 * a serious concern here since it is module unload time. */
	if (atomic64_read(&kmem_alloc_used) != 0)
		CWARN("kmem leaked %ld/%ld bytes\n",
		      atomic_read(&kmem_alloc_used), kmem_alloc_max);


	if (atomic64_read(&vmem_alloc_used) != 0)
		CWARN("vmem leaked %ld/%ld bytes\n",
		      atomic_read(&vmem_alloc_used), vmem_alloc_max);

	spl_kmem_fini_tracking(&kmem_list, &kmem_lock);
	spl_kmem_fini_tracking(&vmem_list, &vmem_lock);
#endif /* DEBUG_KMEM */
	ENTRY;

#ifdef HAVE_SET_SHRINKER
	remove_shrinker(spl_kmem_cache_shrinker);
#else
	unregister_shrinker(&spl_kmem_cache_shrinker);
#endif

	EXIT;
}
