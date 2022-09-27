/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/percpu_compat.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/taskq.h>
#include <sys/timer.h>
#include <sys/vmem.h>
#include <sys/wait.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/prefetch.h>

/*
 * Within the scope of spl-kmem.c file the kmem_cache_* definitions
 * are removed to allow access to the real Linux slab allocator.
 */
#undef kmem_cache_destroy
#undef kmem_cache_create
#undef kmem_cache_alloc
#undef kmem_cache_free


/*
 * Linux 3.16 replaced smp_mb__{before,after}_{atomic,clear}_{dec,inc,bit}()
 * with smp_mb__{before,after}_atomic() because they were redundant. This is
 * only used inside our SLAB allocator, so we implement an internal wrapper
 * here to give us smp_mb__{before,after}_atomic() on older kernels.
 */
#ifndef smp_mb__before_atomic
#define	smp_mb__before_atomic(x) smp_mb__before_clear_bit(x)
#endif

#ifndef smp_mb__after_atomic
#define	smp_mb__after_atomic(x) smp_mb__after_clear_bit(x)
#endif

/* BEGIN CSTYLED */
/*
 * Cache magazines are an optimization designed to minimize the cost of
 * allocating memory.  They do this by keeping a per-cpu cache of recently
 * freed objects, which can then be reallocated without taking a lock. This
 * can improve performance on highly contended caches.  However, because
 * objects in magazines will prevent otherwise empty slabs from being
 * immediately released this may not be ideal for low memory machines.
 *
 * For this reason spl_kmem_cache_magazine_size can be used to set a maximum
 * magazine size.  When this value is set to 0 the magazine size will be
 * automatically determined based on the object size.  Otherwise magazines
 * will be limited to 2-256 objects per magazine (i.e per cpu).  Magazines
 * may never be entirely disabled in this implementation.
 */
static unsigned int spl_kmem_cache_magazine_size = 0;
module_param(spl_kmem_cache_magazine_size, uint, 0444);
MODULE_PARM_DESC(spl_kmem_cache_magazine_size,
	"Default magazine size (2-256), set automatically (0)");

/*
 * The default behavior is to report the number of objects remaining in the
 * cache.  This allows the Linux VM to repeatedly reclaim objects from the
 * cache when memory is low satisfy other memory allocations.  Alternately,
 * setting this value to KMC_RECLAIM_ONCE limits how aggressively the cache
 * is reclaimed.  This may increase the likelihood of out of memory events.
 */
static unsigned int spl_kmem_cache_reclaim = 0 /* KMC_RECLAIM_ONCE */;
module_param(spl_kmem_cache_reclaim, uint, 0644);
MODULE_PARM_DESC(spl_kmem_cache_reclaim, "Single reclaim pass (0x1)");

static unsigned int spl_kmem_cache_obj_per_slab = SPL_KMEM_CACHE_OBJ_PER_SLAB;
module_param(spl_kmem_cache_obj_per_slab, uint, 0644);
MODULE_PARM_DESC(spl_kmem_cache_obj_per_slab, "Number of objects per slab");

static unsigned int spl_kmem_cache_max_size = SPL_KMEM_CACHE_MAX_SIZE;
module_param(spl_kmem_cache_max_size, uint, 0644);
MODULE_PARM_DESC(spl_kmem_cache_max_size, "Maximum size of slab in MB");

/*
 * For small objects the Linux slab allocator should be used to make the most
 * efficient use of the memory.  However, large objects are not supported by
 * the Linux slab and therefore the SPL implementation is preferred.  A cutoff
 * of 16K was determined to be optimal for architectures using 4K pages and
 * to also work well on architecutres using larger 64K page sizes.
 */
static unsigned int spl_kmem_cache_slab_limit = 16384;
module_param(spl_kmem_cache_slab_limit, uint, 0644);
MODULE_PARM_DESC(spl_kmem_cache_slab_limit,
	"Objects less than N bytes use the Linux slab");

/*
 * The number of threads available to allocate new slabs for caches.  This
 * should not need to be tuned but it is available for performance analysis.
 */
static unsigned int spl_kmem_cache_kmem_threads = 4;
module_param(spl_kmem_cache_kmem_threads, uint, 0444);
MODULE_PARM_DESC(spl_kmem_cache_kmem_threads,
	"Number of spl_kmem_cache threads");
/* END CSTYLED */

/*
 * Slab allocation interfaces
 *
 * While the Linux slab implementation was inspired by the Solaris
 * implementation I cannot use it to emulate the Solaris APIs.  I
 * require two features which are not provided by the Linux slab.
 *
 * 1) Constructors AND destructors.  Recent versions of the Linux
 *    kernel have removed support for destructors.  This is a deal
 *    breaker for the SPL which contains particularly expensive
 *    initializers for mutex's, condition variables, etc.  We also
 *    require a minimal level of cleanup for these data types unlike
 *    many Linux data types which do need to be explicitly destroyed.
 *
 * 2) Virtual address space backed slab.  Callers of the Solaris slab
 *    expect it to work well for both small are very large allocations.
 *    Because of memory fragmentation the Linux slab which is backed
 *    by kmalloc'ed memory performs very badly when confronted with
 *    large numbers of large allocations.  Basing the slab on the
 *    virtual address space removes the need for contiguous pages
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
 */

struct list_head spl_kmem_cache_list;   /* List of caches */
struct rw_semaphore spl_kmem_cache_sem; /* Cache list lock */
taskq_t *spl_kmem_cache_taskq;		/* Task queue for aging / reclaim */

static void spl_cache_shrink(spl_kmem_cache_t *skc, void *obj);

static void *
kv_alloc(spl_kmem_cache_t *skc, int size, int flags)
{
	gfp_t lflags = kmem_flags_convert(flags);
	void *ptr;

	ptr = spl_vmalloc(size, lflags | __GFP_HIGHMEM);

	/* Resulting allocated memory will be page aligned */
	ASSERT(IS_P2ALIGNED(ptr, PAGE_SIZE));

	return (ptr);
}

static void
kv_free(spl_kmem_cache_t *skc, void *ptr, int size)
{
	ASSERT(IS_P2ALIGNED(ptr, PAGE_SIZE));

	/*
	 * The Linux direct reclaim path uses this out of band value to
	 * determine if forward progress is being made.  Normally this is
	 * incremented by kmem_freepages() which is part of the various
	 * Linux slab implementations.  However, since we are using none
	 * of that infrastructure we are responsible for incrementing it.
	 */
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += size >> PAGE_SHIFT;

	vfree(ptr);
}

/*
 * Required space for each aligned sks.
 */
static inline uint32_t
spl_sks_size(spl_kmem_cache_t *skc)
{
	return (P2ROUNDUP_TYPED(sizeof (spl_kmem_slab_t),
	    skc->skc_obj_align, uint32_t));
}

/*
 * Required space for each aligned object.
 */
static inline uint32_t
spl_obj_size(spl_kmem_cache_t *skc)
{
	uint32_t align = skc->skc_obj_align;

	return (P2ROUNDUP_TYPED(skc->skc_obj_size, align, uint32_t) +
	    P2ROUNDUP_TYPED(sizeof (spl_kmem_obj_t), align, uint32_t));
}

uint64_t
spl_kmem_cache_inuse(kmem_cache_t *cache)
{
	return (cache->skc_obj_total);
}
EXPORT_SYMBOL(spl_kmem_cache_inuse);

uint64_t
spl_kmem_cache_entry_size(kmem_cache_t *cache)
{
	return (cache->skc_obj_size);
}
EXPORT_SYMBOL(spl_kmem_cache_entry_size);

/*
 * Lookup the spl_kmem_object_t for an object given that object.
 */
static inline spl_kmem_obj_t *
spl_sko_from_obj(spl_kmem_cache_t *skc, void *obj)
{
	return (obj + P2ROUNDUP_TYPED(skc->skc_obj_size,
	    skc->skc_obj_align, uint32_t));
}

/*
 * It's important that we pack the spl_kmem_obj_t structure and the
 * actual objects in to one large address space to minimize the number
 * of calls to the allocator.  It is far better to do a few large
 * allocations and then subdivide it ourselves.  Now which allocator
 * we use requires balancing a few trade offs.
 *
 * For small objects we use kmem_alloc() because as long as you are
 * only requesting a small number of pages (ideally just one) its cheap.
 * However, when you start requesting multiple pages with kmem_alloc()
 * it gets increasingly expensive since it requires contiguous pages.
 * For this reason we shift to vmem_alloc() for slabs of large objects
 * which removes the need for contiguous pages.  We do not use
 * vmem_alloc() in all cases because there is significant locking
 * overhead in __get_vm_area_node().  This function takes a single
 * global lock when acquiring an available virtual address range which
 * serializes all vmem_alloc()'s for all slab caches.  Using slightly
 * different allocation functions for small and large objects should
 * give us the best of both worlds.
 *
 * +------------------------+
 * | spl_kmem_slab_t --+-+  |
 * | skc_obj_size    <-+ |  |
 * | spl_kmem_obj_t      |  |
 * | skc_obj_size    <---+  |
 * | spl_kmem_obj_t      |  |
 * | ...                 v  |
 * +------------------------+
 */
static spl_kmem_slab_t *
spl_slab_alloc(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_slab_t *sks;
	void *base;
	uint32_t obj_size;

	base = kv_alloc(skc, skc->skc_slab_size, flags);
	if (base == NULL)
		return (NULL);

	sks = (spl_kmem_slab_t *)base;
	sks->sks_magic = SKS_MAGIC;
	sks->sks_objs = skc->skc_slab_objs;
	sks->sks_age = jiffies;
	sks->sks_cache = skc;
	INIT_LIST_HEAD(&sks->sks_list);
	INIT_LIST_HEAD(&sks->sks_free_list);
	sks->sks_ref = 0;
	obj_size = spl_obj_size(skc);

	for (int i = 0; i < sks->sks_objs; i++) {
		void *obj = base + spl_sks_size(skc) + (i * obj_size);

		ASSERT(IS_P2ALIGNED(obj, skc->skc_obj_align));
		spl_kmem_obj_t *sko = spl_sko_from_obj(skc, obj);
		sko->sko_addr = obj;
		sko->sko_magic = SKO_MAGIC;
		sko->sko_slab = sks;
		INIT_LIST_HEAD(&sko->sko_list);
		list_add_tail(&sko->sko_list, &sks->sks_free_list);
	}

	return (sks);
}

/*
 * Remove a slab from complete or partial list, it must be called with
 * the 'skc->skc_lock' held but the actual free must be performed
 * outside the lock to prevent deadlocking on vmem addresses.
 */
static void
spl_slab_free(spl_kmem_slab_t *sks,
    struct list_head *sks_list, struct list_head *sko_list)
{
	spl_kmem_cache_t *skc;

	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(sks->sks_ref == 0);

	skc = sks->sks_cache;
	ASSERT(skc->skc_magic == SKC_MAGIC);

	/*
	 * Update slab/objects counters in the cache, then remove the
	 * slab from the skc->skc_partial_list.  Finally add the slab
	 * and all its objects in to the private work lists where the
	 * destructors will be called and the memory freed to the system.
	 */
	skc->skc_obj_total -= sks->sks_objs;
	skc->skc_slab_total--;
	list_del(&sks->sks_list);
	list_add(&sks->sks_list, sks_list);
	list_splice_init(&sks->sks_free_list, sko_list);
}

/*
 * Reclaim empty slabs at the end of the partial list.
 */
static void
spl_slab_reclaim(spl_kmem_cache_t *skc)
{
	spl_kmem_slab_t *sks = NULL, *m = NULL;
	spl_kmem_obj_t *sko = NULL, *n = NULL;
	LIST_HEAD(sks_list);
	LIST_HEAD(sko_list);

	/*
	 * Empty slabs and objects must be moved to a private list so they
	 * can be safely freed outside the spin lock.  All empty slabs are
	 * at the end of skc->skc_partial_list, therefore once a non-empty
	 * slab is found we can stop scanning.
	 */
	spin_lock(&skc->skc_lock);
	list_for_each_entry_safe_reverse(sks, m,
	    &skc->skc_partial_list, sks_list) {

		if (sks->sks_ref > 0)
			break;

		spl_slab_free(sks, &sks_list, &sko_list);
	}
	spin_unlock(&skc->skc_lock);

	/*
	 * The following two loops ensure all the object destructors are run,
	 * and the slabs themselves are freed.  This is all done outside the
	 * skc->skc_lock since this allows the destructor to sleep, and
	 * allows us to perform a conditional reschedule when a freeing a
	 * large number of objects and slabs back to the system.
	 */

	list_for_each_entry_safe(sko, n, &sko_list, sko_list) {
		ASSERT(sko->sko_magic == SKO_MAGIC);
	}

	list_for_each_entry_safe(sks, m, &sks_list, sks_list) {
		ASSERT(sks->sks_magic == SKS_MAGIC);
		kv_free(skc, sks, skc->skc_slab_size);
	}
}

static spl_kmem_emergency_t *
spl_emergency_search(struct rb_root *root, void *obj)
{
	struct rb_node *node = root->rb_node;
	spl_kmem_emergency_t *ske;
	unsigned long address = (unsigned long)obj;

	while (node) {
		ske = container_of(node, spl_kmem_emergency_t, ske_node);

		if (address < ske->ske_obj)
			node = node->rb_left;
		else if (address > ske->ske_obj)
			node = node->rb_right;
		else
			return (ske);
	}

	return (NULL);
}

static int
spl_emergency_insert(struct rb_root *root, spl_kmem_emergency_t *ske)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	spl_kmem_emergency_t *ske_tmp;
	unsigned long address = ske->ske_obj;

	while (*new) {
		ske_tmp = container_of(*new, spl_kmem_emergency_t, ske_node);

		parent = *new;
		if (address < ske_tmp->ske_obj)
			new = &((*new)->rb_left);
		else if (address > ske_tmp->ske_obj)
			new = &((*new)->rb_right);
		else
			return (0);
	}

	rb_link_node(&ske->ske_node, parent, new);
	rb_insert_color(&ske->ske_node, root);

	return (1);
}

/*
 * Allocate a single emergency object and track it in a red black tree.
 */
static int
spl_emergency_alloc(spl_kmem_cache_t *skc, int flags, void **obj)
{
	gfp_t lflags = kmem_flags_convert(flags);
	spl_kmem_emergency_t *ske;
	int order = get_order(skc->skc_obj_size);
	int empty;

	/* Last chance use a partial slab if one now exists */
	spin_lock(&skc->skc_lock);
	empty = list_empty(&skc->skc_partial_list);
	spin_unlock(&skc->skc_lock);
	if (!empty)
		return (-EEXIST);

	ske = kmalloc(sizeof (*ske), lflags);
	if (ske == NULL)
		return (-ENOMEM);

	ske->ske_obj = __get_free_pages(lflags, order);
	if (ske->ske_obj == 0) {
		kfree(ske);
		return (-ENOMEM);
	}

	spin_lock(&skc->skc_lock);
	empty = spl_emergency_insert(&skc->skc_emergency_tree, ske);
	if (likely(empty)) {
		skc->skc_obj_total++;
		skc->skc_obj_emergency++;
		if (skc->skc_obj_emergency > skc->skc_obj_emergency_max)
			skc->skc_obj_emergency_max = skc->skc_obj_emergency;
	}
	spin_unlock(&skc->skc_lock);

	if (unlikely(!empty)) {
		free_pages(ske->ske_obj, order);
		kfree(ske);
		return (-EINVAL);
	}

	*obj = (void *)ske->ske_obj;

	return (0);
}

/*
 * Locate the passed object in the red black tree and free it.
 */
static int
spl_emergency_free(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_emergency_t *ske;
	int order = get_order(skc->skc_obj_size);

	spin_lock(&skc->skc_lock);
	ske = spl_emergency_search(&skc->skc_emergency_tree, obj);
	if (ske) {
		rb_erase(&ske->ske_node, &skc->skc_emergency_tree);
		skc->skc_obj_emergency--;
		skc->skc_obj_total--;
	}
	spin_unlock(&skc->skc_lock);

	if (ske == NULL)
		return (-ENOENT);

	free_pages(ske->ske_obj, order);
	kfree(ske);

	return (0);
}

/*
 * Release objects from the per-cpu magazine back to their slab.  The flush
 * argument contains the max number of entries to remove from the magazine.
 */
static void
spl_cache_flush(spl_kmem_cache_t *skc, spl_kmem_magazine_t *skm, int flush)
{
	spin_lock(&skc->skc_lock);

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skm->skm_magic == SKM_MAGIC);

	int count = MIN(flush, skm->skm_avail);
	for (int i = 0; i < count; i++)
		spl_cache_shrink(skc, skm->skm_objs[i]);

	skm->skm_avail -= count;
	memmove(skm->skm_objs, &(skm->skm_objs[count]),
	    sizeof (void *) * skm->skm_avail);

	spin_unlock(&skc->skc_lock);
}

/*
 * Size a slab based on the size of each aligned object plus spl_kmem_obj_t.
 * When on-slab we want to target spl_kmem_cache_obj_per_slab.  However,
 * for very small objects we may end up with more than this so as not
 * to waste space in the minimal allocation of a single page.
 */
static int
spl_slab_size(spl_kmem_cache_t *skc, uint32_t *objs, uint32_t *size)
{
	uint32_t sks_size, obj_size, max_size, tgt_size, tgt_objs;

	sks_size = spl_sks_size(skc);
	obj_size = spl_obj_size(skc);
	max_size = (spl_kmem_cache_max_size * 1024 * 1024);
	tgt_size = (spl_kmem_cache_obj_per_slab * obj_size + sks_size);

	if (tgt_size <= max_size) {
		tgt_objs = (tgt_size - sks_size) / obj_size;
	} else {
		tgt_objs = (max_size - sks_size) / obj_size;
		tgt_size = (tgt_objs * obj_size) + sks_size;
	}

	if (tgt_objs == 0)
		return (-ENOSPC);

	*objs = tgt_objs;
	*size = tgt_size;

	return (0);
}

/*
 * Make a guess at reasonable per-cpu magazine size based on the size of
 * each object and the cost of caching N of them in each magazine.  Long
 * term this should really adapt based on an observed usage heuristic.
 */
static int
spl_magazine_size(spl_kmem_cache_t *skc)
{
	uint32_t obj_size = spl_obj_size(skc);
	int size;

	if (spl_kmem_cache_magazine_size > 0)
		return (MAX(MIN(spl_kmem_cache_magazine_size, 256), 2));

	/* Per-magazine sizes below assume a 4Kib page size */
	if (obj_size > (PAGE_SIZE * 256))
		size = 4;  /* Minimum 4Mib per-magazine */
	else if (obj_size > (PAGE_SIZE * 32))
		size = 16; /* Minimum 2Mib per-magazine */
	else if (obj_size > (PAGE_SIZE))
		size = 64; /* Minimum 256Kib per-magazine */
	else if (obj_size > (PAGE_SIZE / 4))
		size = 128; /* Minimum 128Kib per-magazine */
	else
		size = 256;

	return (size);
}

/*
 * Allocate a per-cpu magazine to associate with a specific core.
 */
static spl_kmem_magazine_t *
spl_magazine_alloc(spl_kmem_cache_t *skc, int cpu)
{
	spl_kmem_magazine_t *skm;
	int size = sizeof (spl_kmem_magazine_t) +
	    sizeof (void *) * skc->skc_mag_size;

	skm = kmalloc_node(size, GFP_KERNEL, cpu_to_node(cpu));
	if (skm) {
		skm->skm_magic = SKM_MAGIC;
		skm->skm_avail = 0;
		skm->skm_size = skc->skc_mag_size;
		skm->skm_refill = skc->skc_mag_refill;
		skm->skm_cache = skc;
		skm->skm_cpu = cpu;
	}

	return (skm);
}

/*
 * Free a per-cpu magazine associated with a specific core.
 */
static void
spl_magazine_free(spl_kmem_magazine_t *skm)
{
	ASSERT(skm->skm_magic == SKM_MAGIC);
	ASSERT(skm->skm_avail == 0);
	kfree(skm);
}

/*
 * Create all pre-cpu magazines of reasonable sizes.
 */
static int
spl_magazine_create(spl_kmem_cache_t *skc)
{
	int i = 0;

	ASSERT((skc->skc_flags & KMC_SLAB) == 0);

	skc->skc_mag = kzalloc(sizeof (spl_kmem_magazine_t *) *
	    num_possible_cpus(), kmem_flags_convert(KM_SLEEP));
	skc->skc_mag_size = spl_magazine_size(skc);
	skc->skc_mag_refill = (skc->skc_mag_size + 1) / 2;

	for_each_possible_cpu(i) {
		skc->skc_mag[i] = spl_magazine_alloc(skc, i);
		if (!skc->skc_mag[i]) {
			for (i--; i >= 0; i--)
				spl_magazine_free(skc->skc_mag[i]);

			kfree(skc->skc_mag);
			return (-ENOMEM);
		}
	}

	return (0);
}

/*
 * Destroy all pre-cpu magazines.
 */
static void
spl_magazine_destroy(spl_kmem_cache_t *skc)
{
	spl_kmem_magazine_t *skm;
	int i = 0;

	ASSERT((skc->skc_flags & KMC_SLAB) == 0);

	for_each_possible_cpu(i) {
		skm = skc->skc_mag[i];
		spl_cache_flush(skc, skm, skm->skm_avail);
		spl_magazine_free(skm);
	}

	kfree(skc->skc_mag);
}

/*
 * Create a object cache based on the following arguments:
 * name		cache name
 * size		cache object size
 * align	cache object alignment
 * ctor		cache object constructor
 * dtor		cache object destructor
 * reclaim	cache object reclaim
 * priv		cache private data for ctor/dtor/reclaim
 * vmp		unused must be NULL
 * flags
 *	KMC_KVMEM       Force kvmem backed SPL cache
 *	KMC_SLAB        Force Linux slab backed cache
 *	KMC_NODEBUG	Disable debugging (unsupported)
 */
spl_kmem_cache_t *
spl_kmem_cache_create(const char *name, size_t size, size_t align,
    spl_kmem_ctor_t ctor, spl_kmem_dtor_t dtor, void *reclaim,
    void *priv, void *vmp, int flags)
{
	gfp_t lflags = kmem_flags_convert(KM_SLEEP);
	spl_kmem_cache_t *skc;
	int rc;

	/*
	 * Unsupported flags
	 */
	ASSERT(vmp == NULL);
	ASSERT(reclaim == NULL);

	might_sleep();

	skc = kzalloc(sizeof (*skc), lflags);
	if (skc == NULL)
		return (NULL);

	skc->skc_magic = SKC_MAGIC;
	skc->skc_name_size = strlen(name) + 1;
	skc->skc_name = (char *)kmalloc(skc->skc_name_size, lflags);
	if (skc->skc_name == NULL) {
		kfree(skc);
		return (NULL);
	}
	strlcpy(skc->skc_name, name, skc->skc_name_size);

	skc->skc_ctor = ctor;
	skc->skc_dtor = dtor;
	skc->skc_private = priv;
	skc->skc_vmp = vmp;
	skc->skc_linux_cache = NULL;
	skc->skc_flags = flags;
	skc->skc_obj_size = size;
	skc->skc_obj_align = SPL_KMEM_CACHE_ALIGN;
	atomic_set(&skc->skc_ref, 0);

	INIT_LIST_HEAD(&skc->skc_list);
	INIT_LIST_HEAD(&skc->skc_complete_list);
	INIT_LIST_HEAD(&skc->skc_partial_list);
	skc->skc_emergency_tree = RB_ROOT;
	spin_lock_init(&skc->skc_lock);
	init_waitqueue_head(&skc->skc_waitq);
	skc->skc_slab_fail = 0;
	skc->skc_slab_create = 0;
	skc->skc_slab_destroy = 0;
	skc->skc_slab_total = 0;
	skc->skc_slab_alloc = 0;
	skc->skc_slab_max = 0;
	skc->skc_obj_total = 0;
	skc->skc_obj_alloc = 0;
	skc->skc_obj_max = 0;
	skc->skc_obj_deadlock = 0;
	skc->skc_obj_emergency = 0;
	skc->skc_obj_emergency_max = 0;

	rc = percpu_counter_init_common(&skc->skc_linux_alloc, 0,
	    GFP_KERNEL);
	if (rc != 0) {
		kfree(skc);
		return (NULL);
	}

	/*
	 * Verify the requested alignment restriction is sane.
	 */
	if (align) {
		VERIFY(ISP2(align));
		VERIFY3U(align, >=, SPL_KMEM_CACHE_ALIGN);
		VERIFY3U(align, <=, PAGE_SIZE);
		skc->skc_obj_align = align;
	}

	/*
	 * When no specific type of slab is requested (kmem, vmem, or
	 * linuxslab) then select a cache type based on the object size
	 * and default tunables.
	 */
	if (!(skc->skc_flags & (KMC_SLAB | KMC_KVMEM))) {
		if (spl_kmem_cache_slab_limit &&
		    size <= (size_t)spl_kmem_cache_slab_limit) {
			/*
			 * Objects smaller than spl_kmem_cache_slab_limit can
			 * use the Linux slab for better space-efficiency.
			 */
			skc->skc_flags |= KMC_SLAB;
		} else {
			/*
			 * All other objects are considered large and are
			 * placed on kvmem backed slabs.
			 */
			skc->skc_flags |= KMC_KVMEM;
		}
	}

	/*
	 * Given the type of slab allocate the required resources.
	 */
	if (skc->skc_flags & KMC_KVMEM) {
		rc = spl_slab_size(skc,
		    &skc->skc_slab_objs, &skc->skc_slab_size);
		if (rc)
			goto out;

		rc = spl_magazine_create(skc);
		if (rc)
			goto out;
	} else {
		unsigned long slabflags = 0;

		if (size > (SPL_MAX_KMEM_ORDER_NR_PAGES * PAGE_SIZE)) {
			rc = EINVAL;
			goto out;
		}

#if defined(SLAB_USERCOPY)
		/*
		 * Required for PAX-enabled kernels if the slab is to be
		 * used for copying between user and kernel space.
		 */
		slabflags |= SLAB_USERCOPY;
#endif

#if defined(HAVE_KMEM_CACHE_CREATE_USERCOPY)
		/*
		 * Newer grsec patchset uses kmem_cache_create_usercopy()
		 * instead of SLAB_USERCOPY flag
		 */
		skc->skc_linux_cache = kmem_cache_create_usercopy(
		    skc->skc_name, size, align, slabflags, 0, size, NULL);
#else
		skc->skc_linux_cache = kmem_cache_create(
		    skc->skc_name, size, align, slabflags, NULL);
#endif
		if (skc->skc_linux_cache == NULL) {
			rc = ENOMEM;
			goto out;
		}
	}

	down_write(&spl_kmem_cache_sem);
	list_add_tail(&skc->skc_list, &spl_kmem_cache_list);
	up_write(&spl_kmem_cache_sem);

	return (skc);
out:
	kfree(skc->skc_name);
	percpu_counter_destroy(&skc->skc_linux_alloc);
	kfree(skc);
	return (NULL);
}
EXPORT_SYMBOL(spl_kmem_cache_create);

/*
 * Register a move callback for cache defragmentation.
 * XXX: Unimplemented but harmless to stub out for now.
 */
void
spl_kmem_cache_set_move(spl_kmem_cache_t *skc,
    kmem_cbrc_t (move)(void *, void *, size_t, void *))
{
	ASSERT(move != NULL);
}
EXPORT_SYMBOL(spl_kmem_cache_set_move);

/*
 * Destroy a cache and all objects associated with the cache.
 */
void
spl_kmem_cache_destroy(spl_kmem_cache_t *skc)
{
	DECLARE_WAIT_QUEUE_HEAD(wq);
	taskqid_t id;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skc->skc_flags & (KMC_KVMEM | KMC_SLAB));

	down_write(&spl_kmem_cache_sem);
	list_del_init(&skc->skc_list);
	up_write(&spl_kmem_cache_sem);

	/* Cancel any and wait for any pending delayed tasks */
	VERIFY(!test_and_set_bit(KMC_BIT_DESTROY, &skc->skc_flags));

	spin_lock(&skc->skc_lock);
	id = skc->skc_taskqid;
	spin_unlock(&skc->skc_lock);

	taskq_cancel_id(spl_kmem_cache_taskq, id);

	/*
	 * Wait until all current callers complete, this is mainly
	 * to catch the case where a low memory situation triggers a
	 * cache reaping action which races with this destroy.
	 */
	wait_event(wq, atomic_read(&skc->skc_ref) == 0);

	if (skc->skc_flags & KMC_KVMEM) {
		spl_magazine_destroy(skc);
		spl_slab_reclaim(skc);
	} else {
		ASSERT(skc->skc_flags & KMC_SLAB);
		kmem_cache_destroy(skc->skc_linux_cache);
	}

	spin_lock(&skc->skc_lock);

	/*
	 * Validate there are no objects in use and free all the
	 * spl_kmem_slab_t, spl_kmem_obj_t, and object buffers.
	 */
	ASSERT3U(skc->skc_slab_alloc, ==, 0);
	ASSERT3U(skc->skc_obj_alloc, ==, 0);
	ASSERT3U(skc->skc_slab_total, ==, 0);
	ASSERT3U(skc->skc_obj_total, ==, 0);
	ASSERT3U(skc->skc_obj_emergency, ==, 0);
	ASSERT(list_empty(&skc->skc_complete_list));

	ASSERT3U(percpu_counter_sum(&skc->skc_linux_alloc), ==, 0);
	percpu_counter_destroy(&skc->skc_linux_alloc);

	spin_unlock(&skc->skc_lock);

	kfree(skc->skc_name);
	kfree(skc);
}
EXPORT_SYMBOL(spl_kmem_cache_destroy);

/*
 * Allocate an object from a slab attached to the cache.  This is used to
 * repopulate the per-cpu magazine caches in batches when they run low.
 */
static void *
spl_cache_obj(spl_kmem_cache_t *skc, spl_kmem_slab_t *sks)
{
	spl_kmem_obj_t *sko;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(sks->sks_magic == SKS_MAGIC);

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

	return (sko->sko_addr);
}

/*
 * Generic slab allocation function to run by the global work queues.
 * It is responsible for allocating a new slab, linking it in to the list
 * of partial slabs, and then waking any waiters.
 */
static int
__spl_cache_grow(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_slab_t *sks;

	fstrans_cookie_t cookie = spl_fstrans_mark();
	sks = spl_slab_alloc(skc, flags);
	spl_fstrans_unmark(cookie);

	spin_lock(&skc->skc_lock);
	if (sks) {
		skc->skc_slab_total++;
		skc->skc_obj_total += sks->sks_objs;
		list_add_tail(&sks->sks_list, &skc->skc_partial_list);

		smp_mb__before_atomic();
		clear_bit(KMC_BIT_DEADLOCKED, &skc->skc_flags);
		smp_mb__after_atomic();
	}
	spin_unlock(&skc->skc_lock);

	return (sks == NULL ? -ENOMEM : 0);
}

static void
spl_cache_grow_work(void *data)
{
	spl_kmem_alloc_t *ska = (spl_kmem_alloc_t *)data;
	spl_kmem_cache_t *skc = ska->ska_cache;

	int error = __spl_cache_grow(skc, ska->ska_flags);

	atomic_dec(&skc->skc_ref);
	smp_mb__before_atomic();
	clear_bit(KMC_BIT_GROWING, &skc->skc_flags);
	smp_mb__after_atomic();
	if (error == 0)
		wake_up_all(&skc->skc_waitq);

	kfree(ska);
}

/*
 * Returns non-zero when a new slab should be available.
 */
static int
spl_cache_grow_wait(spl_kmem_cache_t *skc)
{
	return (!test_bit(KMC_BIT_GROWING, &skc->skc_flags));
}

/*
 * No available objects on any slabs, create a new slab.  Note that this
 * functionality is disabled for KMC_SLAB caches which are backed by the
 * Linux slab.
 */
static int
spl_cache_grow(spl_kmem_cache_t *skc, int flags, void **obj)
{
	int remaining, rc = 0;

	ASSERT0(flags & ~KM_PUBLIC_MASK);
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT((skc->skc_flags & KMC_SLAB) == 0);
	might_sleep();
	*obj = NULL;

	/*
	 * Before allocating a new slab wait for any reaping to complete and
	 * then return so the local magazine can be rechecked for new objects.
	 */
	if (test_bit(KMC_BIT_REAPING, &skc->skc_flags)) {
		rc = spl_wait_on_bit(&skc->skc_flags, KMC_BIT_REAPING,
		    TASK_UNINTERRUPTIBLE);
		return (rc ? rc : -EAGAIN);
	}

	/*
	 * Note: It would be nice to reduce the overhead of context switch
	 * and improve NUMA locality, by trying to allocate a new slab in the
	 * current process context with KM_NOSLEEP flag.
	 *
	 * However, this can't be applied to vmem/kvmem due to a bug that
	 * spl_vmalloc() doesn't honor gfp flags in page table allocation.
	 */

	/*
	 * This is handled by dispatching a work request to the global work
	 * queue.  This allows us to asynchronously allocate a new slab while
	 * retaining the ability to safely fall back to a smaller synchronous
	 * allocations to ensure forward progress is always maintained.
	 */
	if (test_and_set_bit(KMC_BIT_GROWING, &skc->skc_flags) == 0) {
		spl_kmem_alloc_t *ska;

		ska = kmalloc(sizeof (*ska), kmem_flags_convert(flags));
		if (ska == NULL) {
			clear_bit_unlock(KMC_BIT_GROWING, &skc->skc_flags);
			smp_mb__after_atomic();
			wake_up_all(&skc->skc_waitq);
			return (-ENOMEM);
		}

		atomic_inc(&skc->skc_ref);
		ska->ska_cache = skc;
		ska->ska_flags = flags;
		taskq_init_ent(&ska->ska_tqe);
		taskq_dispatch_ent(spl_kmem_cache_taskq,
		    spl_cache_grow_work, ska, 0, &ska->ska_tqe);
	}

	/*
	 * The goal here is to only detect the rare case where a virtual slab
	 * allocation has deadlocked.  We must be careful to minimize the use
	 * of emergency objects which are more expensive to track.  Therefore,
	 * we set a very long timeout for the asynchronous allocation and if
	 * the timeout is reached the cache is flagged as deadlocked.  From
	 * this point only new emergency objects will be allocated until the
	 * asynchronous allocation completes and clears the deadlocked flag.
	 */
	if (test_bit(KMC_BIT_DEADLOCKED, &skc->skc_flags)) {
		rc = spl_emergency_alloc(skc, flags, obj);
	} else {
		remaining = wait_event_timeout(skc->skc_waitq,
		    spl_cache_grow_wait(skc), HZ / 10);

		if (!remaining) {
			spin_lock(&skc->skc_lock);
			if (test_bit(KMC_BIT_GROWING, &skc->skc_flags)) {
				set_bit(KMC_BIT_DEADLOCKED, &skc->skc_flags);
				skc->skc_obj_deadlock++;
			}
			spin_unlock(&skc->skc_lock);
		}

		rc = -ENOMEM;
	}

	return (rc);
}

/*
 * Refill a per-cpu magazine with objects from the slabs for this cache.
 * Ideally the magazine can be repopulated using existing objects which have
 * been released, however if we are unable to locate enough free objects new
 * slabs of objects will be created.  On success NULL is returned, otherwise
 * the address of a single emergency object is returned for use by the caller.
 */
static void *
spl_cache_refill(spl_kmem_cache_t *skc, spl_kmem_magazine_t *skm, int flags)
{
	spl_kmem_slab_t *sks;
	int count = 0, rc, refill;
	void *obj = NULL;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skm->skm_magic == SKM_MAGIC);

	refill = MIN(skm->skm_refill, skm->skm_size - skm->skm_avail);
	spin_lock(&skc->skc_lock);

	while (refill > 0) {
		/* No slabs available we may need to grow the cache */
		if (list_empty(&skc->skc_partial_list)) {
			spin_unlock(&skc->skc_lock);

			local_irq_enable();
			rc = spl_cache_grow(skc, flags, &obj);
			local_irq_disable();

			/* Emergency object for immediate use by caller */
			if (rc == 0 && obj != NULL)
				return (obj);

			if (rc)
				goto out;

			/* Rescheduled to different CPU skm is not local */
			if (skm != skc->skc_mag[smp_processor_id()])
				goto out;

			/*
			 * Potentially rescheduled to the same CPU but
			 * allocations may have occurred from this CPU while
			 * we were sleeping so recalculate max refill.
			 */
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

		/*
		 * Consume as many objects as needed to refill the requested
		 * cache.  We must also be careful not to overfill it.
		 */
		while (sks->sks_ref < sks->sks_objs && refill-- > 0 &&
		    ++count) {
			ASSERT(skm->skm_avail < skm->skm_size);
			ASSERT(count < skm->skm_size);
			skm->skm_objs[skm->skm_avail++] =
			    spl_cache_obj(skc, sks);
		}

		/* Move slab to skc_complete_list when full */
		if (sks->sks_ref == sks->sks_objs) {
			list_del(&sks->sks_list);
			list_add(&sks->sks_list, &skc->skc_complete_list);
		}
	}

	spin_unlock(&skc->skc_lock);
out:
	return (NULL);
}

/*
 * Release an object back to the slab from which it came.
 */
static void
spl_cache_shrink(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_slab_t *sks = NULL;
	spl_kmem_obj_t *sko = NULL;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	sko = spl_sko_from_obj(skc, obj);
	ASSERT(sko->sko_magic == SKO_MAGIC);
	sks = sko->sko_slab;
	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(sks->sks_cache == skc);
	list_add(&sko->sko_list, &sks->sks_free_list);

	sks->sks_age = jiffies;
	sks->sks_ref--;
	skc->skc_obj_alloc--;

	/*
	 * Move slab to skc_partial_list when no longer full.  Slabs
	 * are added to the head to keep the partial list is quasi-full
	 * sorted order.  Fuller at the head, emptier at the tail.
	 */
	if (sks->sks_ref == (sks->sks_objs - 1)) {
		list_del(&sks->sks_list);
		list_add(&sks->sks_list, &skc->skc_partial_list);
	}

	/*
	 * Move empty slabs to the end of the partial list so
	 * they can be easily found and freed during reclamation.
	 */
	if (sks->sks_ref == 0) {
		list_del(&sks->sks_list);
		list_add_tail(&sks->sks_list, &skc->skc_partial_list);
		skc->skc_slab_alloc--;
	}
}

/*
 * Allocate an object from the per-cpu magazine, or if the magazine
 * is empty directly allocate from a slab and repopulate the magazine.
 */
void *
spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_magazine_t *skm;
	void *obj = NULL;

	ASSERT0(flags & ~KM_PUBLIC_MASK);
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));

	/*
	 * Allocate directly from a Linux slab.  All optimizations are left
	 * to the underlying cache we only need to guarantee that KM_SLEEP
	 * callers will never fail.
	 */
	if (skc->skc_flags & KMC_SLAB) {
		struct kmem_cache *slc = skc->skc_linux_cache;
		do {
			obj = kmem_cache_alloc(slc, kmem_flags_convert(flags));
		} while ((obj == NULL) && !(flags & KM_NOSLEEP));

		if (obj != NULL) {
			/*
			 * Even though we leave everything up to the
			 * underlying cache we still keep track of
			 * how many objects we've allocated in it for
			 * better debuggability.
			 */
			percpu_counter_inc(&skc->skc_linux_alloc);
		}
		goto ret;
	}

	local_irq_disable();

restart:
	/*
	 * Safe to update per-cpu structure without lock, but
	 * in the restart case we must be careful to reacquire
	 * the local magazine since this may have changed
	 * when we need to grow the cache.
	 */
	skm = skc->skc_mag[smp_processor_id()];
	ASSERT(skm->skm_magic == SKM_MAGIC);

	if (likely(skm->skm_avail)) {
		/* Object available in CPU cache, use it */
		obj = skm->skm_objs[--skm->skm_avail];
	} else {
		obj = spl_cache_refill(skc, skm, flags);
		if ((obj == NULL) && !(flags & KM_NOSLEEP))
			goto restart;

		local_irq_enable();
		goto ret;
	}

	local_irq_enable();
	ASSERT(obj);
	ASSERT(IS_P2ALIGNED(obj, skc->skc_obj_align));

ret:
	/* Pre-emptively migrate object to CPU L1 cache */
	if (obj) {
		if (obj && skc->skc_ctor)
			skc->skc_ctor(obj, skc->skc_private, flags);
		else
			prefetchw(obj);
	}

	return (obj);
}
EXPORT_SYMBOL(spl_kmem_cache_alloc);

/*
 * Free an object back to the local per-cpu magazine, there is no
 * guarantee that this is the same magazine the object was originally
 * allocated from.  We may need to flush entire from the magazine
 * back to the slabs to make space.
 */
void
spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_magazine_t *skm;
	unsigned long flags;
	int do_reclaim = 0;
	int do_emergency = 0;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));

	/*
	 * Run the destructor
	 */
	if (skc->skc_dtor)
		skc->skc_dtor(obj, skc->skc_private);

	/*
	 * Free the object from the Linux underlying Linux slab.
	 */
	if (skc->skc_flags & KMC_SLAB) {
		kmem_cache_free(skc->skc_linux_cache, obj);
		percpu_counter_dec(&skc->skc_linux_alloc);
		return;
	}

	/*
	 * While a cache has outstanding emergency objects all freed objects
	 * must be checked.  However, since emergency objects will never use
	 * a virtual address these objects can be safely excluded as an
	 * optimization.
	 */
	if (!is_vmalloc_addr(obj)) {
		spin_lock(&skc->skc_lock);
		do_emergency = (skc->skc_obj_emergency > 0);
		spin_unlock(&skc->skc_lock);

		if (do_emergency && (spl_emergency_free(skc, obj) == 0))
			return;
	}

	local_irq_save(flags);

	/*
	 * Safe to update per-cpu structure without lock, but
	 * no remote memory allocation tracking is being performed
	 * it is entirely possible to allocate an object from one
	 * CPU cache and return it to another.
	 */
	skm = skc->skc_mag[smp_processor_id()];
	ASSERT(skm->skm_magic == SKM_MAGIC);

	/*
	 * Per-CPU cache full, flush it to make space for this object,
	 * this may result in an empty slab which can be reclaimed once
	 * interrupts are re-enabled.
	 */
	if (unlikely(skm->skm_avail >= skm->skm_size)) {
		spl_cache_flush(skc, skm, skm->skm_refill);
		do_reclaim = 1;
	}

	/* Available space in cache, use it */
	skm->skm_objs[skm->skm_avail++] = obj;

	local_irq_restore(flags);

	if (do_reclaim)
		spl_slab_reclaim(skc);
}
EXPORT_SYMBOL(spl_kmem_cache_free);

/*
 * Depending on how many and which objects are released it may simply
 * repopulate the local magazine which will then need to age-out.  Objects
 * which cannot fit in the magazine will be released back to their slabs
 * which will also need to age out before being released.  This is all just
 * best effort and we do not want to thrash creating and destroying slabs.
 */
void
spl_kmem_cache_reap_now(spl_kmem_cache_t *skc)
{
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));

	if (skc->skc_flags & KMC_SLAB)
		return;

	atomic_inc(&skc->skc_ref);

	/*
	 * Prevent concurrent cache reaping when contended.
	 */
	if (test_and_set_bit(KMC_BIT_REAPING, &skc->skc_flags))
		goto out;

	/* Reclaim from the magazine and free all now empty slabs. */
	unsigned long irq_flags;
	local_irq_save(irq_flags);
	spl_kmem_magazine_t *skm = skc->skc_mag[smp_processor_id()];
	spl_cache_flush(skc, skm, skm->skm_avail);
	local_irq_restore(irq_flags);

	spl_slab_reclaim(skc);
	clear_bit_unlock(KMC_BIT_REAPING, &skc->skc_flags);
	smp_mb__after_atomic();
	wake_up_bit(&skc->skc_flags, KMC_BIT_REAPING);
out:
	atomic_dec(&skc->skc_ref);
}
EXPORT_SYMBOL(spl_kmem_cache_reap_now);

/*
 * This is stubbed out for code consistency with other platforms.  There
 * is existing logic to prevent concurrent reaping so while this is ugly
 * it should do no harm.
 */
int
spl_kmem_cache_reap_active(void)
{
	return (0);
}
EXPORT_SYMBOL(spl_kmem_cache_reap_active);

/*
 * Reap all free slabs from all registered caches.
 */
void
spl_kmem_reap(void)
{
	spl_kmem_cache_t *skc = NULL;

	down_read(&spl_kmem_cache_sem);
	list_for_each_entry(skc, &spl_kmem_cache_list, skc_list) {
		spl_kmem_cache_reap_now(skc);
	}
	up_read(&spl_kmem_cache_sem);
}
EXPORT_SYMBOL(spl_kmem_reap);

int
spl_kmem_cache_init(void)
{
	init_rwsem(&spl_kmem_cache_sem);
	INIT_LIST_HEAD(&spl_kmem_cache_list);
	spl_kmem_cache_taskq = taskq_create("spl_kmem_cache",
	    spl_kmem_cache_kmem_threads, maxclsyspri,
	    spl_kmem_cache_kmem_threads * 8, INT_MAX,
	    TASKQ_PREPOPULATE | TASKQ_DYNAMIC);

	if (spl_kmem_cache_taskq == NULL)
		return (-ENOMEM);

	return (0);
}

void
spl_kmem_cache_fini(void)
{
	taskq_destroy(spl_kmem_cache_taskq);
}
