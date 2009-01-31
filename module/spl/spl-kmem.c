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
# undef DEBUG_SUBSYSTEM
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
atomic64_t kmem_alloc_used = ATOMIC64_INIT(0);
unsigned long long kmem_alloc_max = 0;
atomic64_t vmem_alloc_used = ATOMIC64_INIT(0);
unsigned long long vmem_alloc_max = 0;
int kmem_warning_flag = 1;

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);
EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);
EXPORT_SYMBOL(kmem_warning_flag);

# ifdef DEBUG_KMEM_TRACKING

/* XXX - Not to surprisingly with debugging enabled the xmem_locks are very
 * highly contended particularly on xfree().  If we want to run with this
 * detailed debugging enabled for anything other than debugging  we need to
 * minimize the contention by moving to a lock per xmem_table entry model.
 */

#  define KMEM_HASH_BITS          10
#  define KMEM_TABLE_SIZE         (1 << KMEM_HASH_BITS)

#  define VMEM_HASH_BITS          10
#  define VMEM_TABLE_SIZE         (1 << VMEM_HASH_BITS)

typedef struct kmem_debug {
	struct hlist_node kd_hlist;     /* Hash node linkage */
	struct list_head kd_list;       /* List of all allocations */
	void *kd_addr;                  /* Allocation pointer */
	size_t kd_size;                 /* Allocation size */
	const char *kd_func;            /* Allocation function */
	int kd_line;                    /* Allocation line */
} kmem_debug_t;

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
# endif

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
 *    require a minimal level of cleanup for these data types unlike
 *    many Linux data type which do need to be explicitly destroyed.
 *
 * 2) Virtual address space backed slab.  Callers of the Solaris slab
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
 */

struct list_head spl_kmem_cache_list;   /* List of caches */
struct rw_semaphore spl_kmem_cache_sem; /* Cache list lock */

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

#ifdef DEBUG_KMEM
# ifdef DEBUG_KMEM_TRACKING

static kmem_debug_t *
kmem_del_init(spinlock_t *lock, struct hlist_head *table, int bits,
                void *addr)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kmem_debug *p;
	unsigned long flags;
	ENTRY;

	spin_lock_irqsave(lock, flags);

	head = &table[hash_ptr(addr, bits)];
	hlist_for_each_entry_rcu(p, node, head, kd_hlist) {
		if (p->kd_addr == addr) {
			hlist_del_init(&p->kd_hlist);
			list_del_init(&p->kd_list);
			spin_unlock_irqrestore(lock, flags);
			return p;
		}
	}

	spin_unlock_irqrestore(lock, flags);

	RETURN(NULL);
}

void *
kmem_alloc_track(size_t size, int flags, const char *func, int line,
    int node_alloc, int node)
{
	void *ptr = NULL;
	kmem_debug_t *dptr;
	unsigned long irq_flags;
	ENTRY;

	dptr = (kmem_debug_t *) kmalloc(sizeof(kmem_debug_t),
	    flags & ~__GFP_ZERO);

	if (dptr == NULL) {
		CWARN("kmem_alloc(%ld, 0x%x) debug failed\n",
		    sizeof(kmem_debug_t), flags);
	} else {
		/* Marked unlikely because we should never be doing this,
		 * we tolerate to up 2 pages but a single page is best.   */
		if (unlikely((size) > (PAGE_SIZE * 2)) && kmem_warning_flag)
			CWARN("Large kmem_alloc(%llu, 0x%x) (%lld/%llu)\n",
			    (unsigned long long) size, flags,
			    atomic64_read(&kmem_alloc_used), kmem_alloc_max);

		/* We use kstrdup() below because the string pointed to by
		 * __FUNCTION__ might not be available by the time we want
		 * to print it since the module might have been unloaded. */
		dptr->kd_func = kstrdup(func, flags & ~__GFP_ZERO);
		if (unlikely(dptr->kd_func == NULL)) {
			kfree(dptr);
			CWARN("kstrdup() failed in kmem_alloc(%llu, 0x%x) "
			    "(%lld/%llu)\n", (unsigned long long) size, flags,
			    atomic64_read(&kmem_alloc_used), kmem_alloc_max);
			goto out;
		}

		/* Use the correct allocator */
		if (node_alloc) {
			ASSERT(!(flags & __GFP_ZERO));
			ptr = kmalloc_node(size, flags, node);
		} else if (flags & __GFP_ZERO) {
			ptr = kzalloc(size, flags & ~__GFP_ZERO);
		} else {
			ptr = kmalloc(size, flags);
		}

		if (unlikely(ptr == NULL)) {
			kfree(dptr->kd_func);
			kfree(dptr);
			CWARN("kmem_alloc(%llu, 0x%x) failed (%lld/%llu)\n",
			    (unsigned long long) size, flags,
			    atomic64_read(&kmem_alloc_used), kmem_alloc_max);
			goto out;
		}

		atomic64_add(size, &kmem_alloc_used);
		if (unlikely(atomic64_read(&kmem_alloc_used) >
		    kmem_alloc_max))
			kmem_alloc_max =
			    atomic64_read(&kmem_alloc_used);

		INIT_HLIST_NODE(&dptr->kd_hlist);
		INIT_LIST_HEAD(&dptr->kd_list);

		dptr->kd_addr = ptr;
		dptr->kd_size = size;
		dptr->kd_line = line;

		spin_lock_irqsave(&kmem_lock, irq_flags);
		hlist_add_head_rcu(&dptr->kd_hlist,
		    &kmem_table[hash_ptr(ptr, KMEM_HASH_BITS)]);
		list_add_tail(&dptr->kd_list, &kmem_list);
		spin_unlock_irqrestore(&kmem_lock, irq_flags);

		CDEBUG_LIMIT(D_INFO, "kmem_alloc(%llu, 0x%x) = %p "
		    "(%lld/%llu)\n", (unsigned long long) size, flags,
		    ptr, atomic64_read(&kmem_alloc_used),
		    kmem_alloc_max);
	}
out:
	RETURN(ptr);
}
EXPORT_SYMBOL(kmem_alloc_track);

void
kmem_free_track(void *ptr, size_t size)
{
	kmem_debug_t *dptr;
	ENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	dptr = kmem_del_init(&kmem_lock, kmem_table, KMEM_HASH_BITS, ptr);

	ASSERT(dptr); /* Must exist in hash due to kmem_alloc() */

	/* Size must match */
	ASSERTF(dptr->kd_size == size, "kd_size (%llu) != size (%llu), "
	    "kd_func = %s, kd_line = %d\n", (unsigned long long) dptr->kd_size,
	    (unsigned long long) size, dptr->kd_func, dptr->kd_line);

	atomic64_sub(size, &kmem_alloc_used);

	CDEBUG_LIMIT(D_INFO, "kmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, atomic64_read(&kmem_alloc_used),
	    kmem_alloc_max);

	kfree(dptr->kd_func);

	memset(dptr, 0x5a, sizeof(kmem_debug_t));
	kfree(dptr);

	memset(ptr, 0x5a, size);
	kfree(ptr);

	EXIT;
}
EXPORT_SYMBOL(kmem_free_track);

void *
vmem_alloc_track(size_t size, int flags, const char *func, int line)
{
	void *ptr = NULL;
	kmem_debug_t *dptr;
	unsigned long irq_flags;
	ENTRY;

	ASSERT(flags & KM_SLEEP);

	dptr = (kmem_debug_t *) kmalloc(sizeof(kmem_debug_t), flags);
	if (dptr == NULL) {
		CWARN("vmem_alloc(%ld, 0x%x) debug failed\n",
		    sizeof(kmem_debug_t), flags);
	} else {
		/* We use kstrdup() below because the string pointed to by
		 * __FUNCTION__ might not be available by the time we want
		 * to print it, since the module might have been unloaded. */
		dptr->kd_func = kstrdup(func, flags & ~__GFP_ZERO);
		if (unlikely(dptr->kd_func == NULL)) {
			kfree(dptr);
			CWARN("kstrdup() failed in vmem_alloc(%llu, 0x%x) "
			    "(%lld/%llu)\n", (unsigned long long) size, flags,
			    atomic64_read(&vmem_alloc_used), vmem_alloc_max);
			goto out;
		}

		ptr = __vmalloc(size, (flags | __GFP_HIGHMEM) & ~__GFP_ZERO,
		    PAGE_KERNEL);

		if (unlikely(ptr == NULL)) {
			kfree(dptr->kd_func);
			kfree(dptr);
			CWARN("vmem_alloc(%llu, 0x%x) failed (%lld/%llu)\n",
			    (unsigned long long) size, flags,
			    atomic64_read(&vmem_alloc_used), vmem_alloc_max);
			goto out;
		}

		if (flags & __GFP_ZERO)
			memset(ptr, 0, size);

		atomic64_add(size, &vmem_alloc_used);
		if (unlikely(atomic64_read(&vmem_alloc_used) >
		    vmem_alloc_max))
			vmem_alloc_max =
			    atomic64_read(&vmem_alloc_used);

		INIT_HLIST_NODE(&dptr->kd_hlist);
		INIT_LIST_HEAD(&dptr->kd_list);

		dptr->kd_addr = ptr;
		dptr->kd_size = size;
		dptr->kd_line = line;

		spin_lock_irqsave(&vmem_lock, irq_flags);
		hlist_add_head_rcu(&dptr->kd_hlist,
		    &vmem_table[hash_ptr(ptr, VMEM_HASH_BITS)]);
		list_add_tail(&dptr->kd_list, &vmem_list);
		spin_unlock_irqrestore(&vmem_lock, irq_flags);

		CDEBUG_LIMIT(D_INFO, "vmem_alloc(%llu, 0x%x) = %p "
		    "(%lld/%llu)\n", (unsigned long long) size, flags,
		    ptr, atomic64_read(&vmem_alloc_used),
		    vmem_alloc_max);
	}
out:
	RETURN(ptr);
}
EXPORT_SYMBOL(vmem_alloc_track);

void
vmem_free_track(void *ptr, size_t size)
{
	kmem_debug_t *dptr;
	ENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	dptr = kmem_del_init(&vmem_lock, vmem_table, VMEM_HASH_BITS, ptr);
	ASSERT(dptr); /* Must exist in hash due to vmem_alloc() */

	/* Size must match */
	ASSERTF(dptr->kd_size == size, "kd_size (%llu) != size (%llu), "
	    "kd_func = %s, kd_line = %d\n", (unsigned long long) dptr->kd_size,
	    (unsigned long long) size, dptr->kd_func, dptr->kd_line);

	atomic64_sub(size, &vmem_alloc_used);
	CDEBUG_LIMIT(D_INFO, "vmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, atomic64_read(&vmem_alloc_used),
	    vmem_alloc_max);

	kfree(dptr->kd_func);

	memset(dptr, 0x5a, sizeof(kmem_debug_t));
	kfree(dptr);

	memset(ptr, 0x5a, size);
	vfree(ptr);

	EXIT;
}
EXPORT_SYMBOL(vmem_free_track);

# else /* DEBUG_KMEM_TRACKING */

void *
kmem_alloc_debug(size_t size, int flags, const char *func, int line,
    int node_alloc, int node)
{
	void *ptr;
	ENTRY;

	/* Marked unlikely because we should never be doing this,
	 * we tolerate to up 2 pages but a single page is best.   */
	if (unlikely(size > (PAGE_SIZE * 2)) && kmem_warning_flag)
		CWARN("Large kmem_alloc(%llu, 0x%x) (%lld/%llu)\n",
		    (unsigned long long) size, flags,
		    atomic64_read(&kmem_alloc_used), kmem_alloc_max);

	/* Use the correct allocator */
	if (node_alloc) {
		ASSERT(!(flags & __GFP_ZERO));
		ptr = kmalloc_node(size, flags, node);
	} else if (flags & __GFP_ZERO) {
		ptr = kzalloc(size, flags & (~__GFP_ZERO));
	} else {
		ptr = kmalloc(size, flags);
	}

	if (ptr == NULL) {
		CWARN("kmem_alloc(%llu, 0x%x) failed (%lld/%llu)\n",
		    (unsigned long long) size, flags,
		    atomic64_read(&kmem_alloc_used), kmem_alloc_max);
	} else {
		atomic64_add(size, &kmem_alloc_used);
		if (unlikely(atomic64_read(&kmem_alloc_used) > kmem_alloc_max))
			kmem_alloc_max = atomic64_read(&kmem_alloc_used);

		CDEBUG_LIMIT(D_INFO, "kmem_alloc(%llu, 0x%x) = %p "
		       "(%lld/%llu)\n", (unsigned long long) size, flags, ptr,
		       atomic64_read(&kmem_alloc_used), kmem_alloc_max);
	}
	RETURN(ptr);
}
EXPORT_SYMBOL(kmem_alloc_debug);

void
kmem_free_debug(void *ptr, size_t size)
{
	ENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	atomic64_sub(size, &kmem_alloc_used);

	CDEBUG_LIMIT(D_INFO, "kmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, atomic64_read(&kmem_alloc_used),
	    kmem_alloc_max);

	memset(ptr, 0x5a, size);
	kfree(ptr);

	EXIT;
}
EXPORT_SYMBOL(kmem_free_debug);

void *
vmem_alloc_debug(size_t size, int flags, const char *func, int line)
{
	void *ptr;
	ENTRY;

	ASSERT(flags & KM_SLEEP);

	ptr = __vmalloc(size, (flags | __GFP_HIGHMEM) & ~__GFP_ZERO,
	    PAGE_KERNEL);
	if (ptr == NULL) {
		CWARN("vmem_alloc(%llu, 0x%x) failed (%lld/%llu)\n",
		    (unsigned long long) size, flags,
		    atomic64_read(&vmem_alloc_used), vmem_alloc_max);
	} else {
		if (flags & __GFP_ZERO)
			memset(ptr, 0, size);

		atomic64_add(size, &vmem_alloc_used);

		if (unlikely(atomic64_read(&vmem_alloc_used) > vmem_alloc_max))
			vmem_alloc_max = atomic64_read(&vmem_alloc_used);

		CDEBUG_LIMIT(D_INFO, "vmem_alloc(%llu, 0x%x) = %p "
		    "(%lld/%llu)\n", (unsigned long long) size, flags, ptr,
		    atomic64_read(&vmem_alloc_used), vmem_alloc_max);
	}

	RETURN(ptr);
}
EXPORT_SYMBOL(vmem_alloc_debug);

void
vmem_free_debug(void *ptr, size_t size)
{
	ENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	atomic64_sub(size, &vmem_alloc_used);

	CDEBUG_LIMIT(D_INFO, "vmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, atomic64_read(&vmem_alloc_used),
	    vmem_alloc_max);

	memset(ptr, 0x5a, size);
	vfree(ptr);

	EXIT;
}
EXPORT_SYMBOL(vmem_free_debug);

# endif /* DEBUG_KMEM_TRACKING */
#endif /* DEBUG_KMEM */

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
 * it gets increasingly expensive since it requires contigeous pages.
 * For this reason we shift to vmem_alloc() for slabs of large objects
 * which removes the need for contigeous pages.  We do not use
 * vmem_alloc() in all cases because there is significant locking
 * overhead in __get_vm_area_node().  This function takes a single
 * global lock when aquiring an available virtual address range which
 * serializes all vmem_alloc()'s for all slab caches.  Using slightly
 * different allocation functions for small and large objects should
 * give us the best of both worlds.
 *
 * KMC_ONSLAB                       KMC_OFFSLAB
 *
 * +------------------------+       +-----------------+
 * | spl_kmem_slab_t --+-+  |       | spl_kmem_slab_t |---+-+
 * | skc_obj_size    <-+ |  |       +-----------------+   | |
 * | spl_kmem_obj_t      |  |                             | |
 * | skc_obj_size    <---+  |       +-----------------+   | |
 * | spl_kmem_obj_t      |  |       | skc_obj_size    | <-+ |
 * | ...                 v  |       | spl_kmem_obj_t  |     |
 * +------------------------+       +-----------------+     v
 */
static spl_kmem_slab_t *
spl_slab_alloc(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_slab_t *sks;
	spl_kmem_obj_t *sko, *n;
	void *base, *obj;
	int i, align, size, rc = 0;

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

	align = skc->skc_obj_align;
	size = P2ROUNDUP(skc->skc_obj_size, align) +
	       P2ROUNDUP(sizeof(spl_kmem_obj_t), align);

	for (i = 0; i < sks->sks_objs; i++) {
		if (skc->skc_flags & KMC_OFFSLAB) {
			obj = kv_alloc(skc, size, flags);
			if (!obj)
				GOTO(out, rc = -ENOMEM);
		} else {
			obj = base +
			      P2ROUNDUP(sizeof(spl_kmem_slab_t), align) +
			      (i * size);
		}

		sko = obj + P2ROUNDUP(skc->skc_obj_size, align);
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
			list_for_each_entry_safe(sko, n, &sks->sks_free_list,
						 sko_list)
				kv_free(skc, sko->sko_addr, size);

		kv_free(skc, base, skc->skc_slab_size);
		sks = NULL;
	}

	RETURN(sks);
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
	spl_kmem_obj_t *sko, *n;
	ENTRY;

	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(sks->sks_ref == 0);

	skc = sks->sks_cache;
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

	skc->skc_obj_total -= sks->sks_objs;
	skc->skc_slab_total--;
	list_del(&sks->sks_list);

	/* Run destructors slab is being released */
	list_for_each_entry_safe(sko, n, &sks->sks_free_list, sko_list) {
		ASSERT(sko->sko_magic == SKO_MAGIC);
		list_del(&sko->sko_list);

		if (skc->skc_dtor)
			skc->skc_dtor(sko->sko_addr, skc->skc_private);

		if (skc->skc_flags & KMC_OFFSLAB)
			list_add(&sko->sko_list, sko_list);
	}

	list_add(&sks->sks_list, sks_list);
	EXIT;
}

/*
 * Traverses all the partial slabs attached to a cache and free those
 * which which are currently empty, and have not been touched for
 * skc_delay seconds.  This is to avoid thrashing.
 */
static void
spl_slab_reclaim(spl_kmem_cache_t *skc, int flag)
{
	spl_kmem_slab_t *sks, *m;
	spl_kmem_obj_t *sko, *n;
	LIST_HEAD(sks_list);
	LIST_HEAD(sko_list);
	int size;
	ENTRY;

	/*
	 * Move empty slabs and objects which have not been touched in
	 * skc_delay seconds on to private lists to be freed outside
	 * the spin lock.  This delay time is important to avoid
	 * thrashing however when flag is set the delay will not be
	 * used.  Empty slabs will be at the end of the skc_partial_list.
	 */
	spin_lock(&skc->skc_lock);
        list_for_each_entry_safe_reverse(sks, m, &skc->skc_partial_list,
					 sks_list) {
		if (sks->sks_ref > 0)
		       break;

		if (flag || time_after(jiffies,sks->sks_age+skc->skc_delay*HZ))
			spl_slab_free(sks, &sks_list, &sko_list);
	}
	spin_unlock(&skc->skc_lock);

	/*
	 * We only have list of spl_kmem_obj_t's if they are located off
	 * the slab, otherwise they get feed with the spl_kmem_slab_t.
	 */
	if (!list_empty(&sko_list)) {
		ASSERT(skc->skc_flags & KMC_OFFSLAB);

		size = P2ROUNDUP(skc->skc_obj_size, skc->skc_obj_align) +
		       P2ROUNDUP(sizeof(spl_kmem_obj_t), skc->skc_obj_align);

		list_for_each_entry_safe(sko, n, &sko_list, sko_list)
			kv_free(skc, sko->sko_addr, size);
	}

	list_for_each_entry_safe(sks, m, &sks_list, sks_list)
		kv_free(skc, sks, skc->skc_slab_size);

	EXIT;
}

/*
 * Called regularly on all caches to age objects out of the magazines
 * which have not been access in skc->skc_delay seconds.  This prevents
 * idle magazines from holding memory which might be better used by
 * other caches or parts of the system.  The delay is present to
 * prevent thrashing the magazine.
 */
static void
spl_magazine_age(void *data)
{
	spl_kmem_cache_t *skc = data;
	spl_kmem_magazine_t *skm = skc->skc_mag[smp_processor_id()];

	if (skm->skm_avail > 0 &&
	    time_after(jiffies, skm->skm_age + skc->skc_delay * HZ))
		(void)spl_cache_flush(skc, skm, skm->skm_refill);
}

/*
 * Called regularly to keep a downward pressure on the size of idle
 * magazines and to release free slabs from the cache.  This function
 * never calls the registered reclaim function, that only occures
 * under memory pressure or with a direct call to spl_kmem_reap().
 */
static void
spl_cache_age(void *data)
{
        spl_kmem_cache_t *skc =
		spl_get_work_data(data, spl_kmem_cache_t, skc_work.work);

	ASSERT(skc->skc_magic == SKC_MAGIC);
	on_each_cpu(spl_magazine_age, skc, 0, 1);
	spl_slab_reclaim(skc, 0);

	if (!test_bit(KMC_BIT_DESTROY, &skc->skc_flags))
		schedule_delayed_work(&skc->skc_work, 2 * skc->skc_delay * HZ);
}

/*
 * Size a slab based on the size of each aliged object plus spl_kmem_obj_t.
 * When on-slab we want to target SPL_KMEM_CACHE_OBJ_PER_SLAB.  However,
 * for very small objects we may end up with more than this so as not
 * to waste space in the minimal allocation of a single page.  Also for
 * very large objects we may use as few as SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN,
 * lower than this and we will fail.
 */
static int
spl_slab_size(spl_kmem_cache_t *skc, uint32_t *objs, uint32_t *size)
{
	int sks_size, obj_size, max_size, align;

	if (skc->skc_flags & KMC_OFFSLAB) {
		*objs = SPL_KMEM_CACHE_OBJ_PER_SLAB;
		*size = sizeof(spl_kmem_slab_t);
	} else {
		align = skc->skc_obj_align;
		sks_size = P2ROUNDUP(sizeof(spl_kmem_slab_t), align);
		obj_size = P2ROUNDUP(skc->skc_obj_size, align) +
                           P2ROUNDUP(sizeof(spl_kmem_obj_t), align);

		if (skc->skc_flags & KMC_KMEM)
			max_size = ((uint64_t)1 << (MAX_ORDER-1)) * PAGE_SIZE;
		else
			max_size = (32 * 1024 * 1024);

		for (*size = PAGE_SIZE; *size <= max_size; *size += PAGE_SIZE) {
			*objs = (*size - sks_size) / obj_size;
			if (*objs >= SPL_KMEM_CACHE_OBJ_PER_SLAB)
				RETURN(0);
		}

		/*
		 * Unable to satisfy target objets per slab, fallback to
		 * allocating a maximally sized slab and assuming it can
		 * contain the minimum objects count use it.  If not fail.
		 */
		*size = max_size;
		*objs = (*size - sks_size) / obj_size;
		if (*objs >= SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN)
			RETURN(0);
	}

	RETURN(-ENOSPC);
}

/*
 * Make a guess at reasonable per-cpu magazine size based on the size of
 * each object and the cost of caching N of them in each magazine.  Long
 * term this should really adapt based on an observed usage heuristic.
 */
static int
spl_magazine_size(spl_kmem_cache_t *skc)
{
	int size, align = skc->skc_obj_align;
	ENTRY;

	/* Per-magazine sizes below assume a 4Kib page size */
	if (P2ROUNDUP(skc->skc_obj_size, align) > (PAGE_SIZE * 256))
		size = 4;  /* Minimum 4Mib per-magazine */
	else if (P2ROUNDUP(skc->skc_obj_size, align) > (PAGE_SIZE * 32))
		size = 16; /* Minimum 2Mib per-magazine */
	else if (P2ROUNDUP(skc->skc_obj_size, align) > (PAGE_SIZE))
		size = 64; /* Minimum 256Kib per-magazine */
	else if (P2ROUNDUP(skc->skc_obj_size, align) > (PAGE_SIZE / 4))
		size = 128; /* Minimum 128Kib per-magazine */
	else
		size = 256;

	RETURN(size);
}

/*
 * Allocate a per-cpu magazine to assoicate with a specific core.
 */
static spl_kmem_magazine_t *
spl_magazine_alloc(spl_kmem_cache_t *skc, int node)
{
	spl_kmem_magazine_t *skm;
	int size = sizeof(spl_kmem_magazine_t) +
	           sizeof(void *) * skc->skc_mag_size;
	ENTRY;

	skm = kmem_alloc_node(size, GFP_KERNEL | __GFP_NOFAIL, node);
	if (skm) {
		skm->skm_magic = SKM_MAGIC;
		skm->skm_avail = 0;
		skm->skm_size = skc->skc_mag_size;
		skm->skm_refill = skc->skc_mag_refill;
		skm->skm_age = jiffies;
	}

	RETURN(skm);
}

/*
 * Free a per-cpu magazine assoicated with a specific core.
 */
static void
spl_magazine_free(spl_kmem_magazine_t *skm)
{
	int size = sizeof(spl_kmem_magazine_t) +
	           sizeof(void *) * skm->skm_size;

	ENTRY;
	ASSERT(skm->skm_magic == SKM_MAGIC);
	ASSERT(skm->skm_avail == 0);

	kmem_free(skm, size);
	EXIT;
}

static void
__spl_magazine_create(void *data)
{
        spl_kmem_cache_t *skc = data;
	int id = smp_processor_id();

	skc->skc_mag[id] = spl_magazine_alloc(skc, cpu_to_node(id));
	ASSERT(skc->skc_mag[id]);
}

/*
 * Create all pre-cpu magazines of reasonable sizes.
 */
static int
spl_magazine_create(spl_kmem_cache_t *skc)
{
	ENTRY;

	skc->skc_mag_size = spl_magazine_size(skc);
	skc->skc_mag_refill = (skc->skc_mag_size + 1) / 2;
	on_each_cpu(__spl_magazine_create, skc, 0, 1);

	RETURN(0);
}

static void
__spl_magazine_destroy(void *data)
{
        spl_kmem_cache_t *skc = data;
	spl_kmem_magazine_t *skm = skc->skc_mag[smp_processor_id()];

	(void)spl_cache_flush(skc, skm, skm->skm_avail);
	spl_magazine_free(skm);
}

/*
 * Destroy all pre-cpu magazines.
 */
static void
spl_magazine_destroy(spl_kmem_cache_t *skc)
{
	ENTRY;
	on_each_cpu(__spl_magazine_destroy, skc, 0, 1);
	EXIT;
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
 *	KMC_NOTOUCH	Disable cache object aging (unsupported)
 *	KMC_NODEBUG	Disable debugging (unsupported)
 *	KMC_NOMAGAZINE	Disable magazine (unsupported)
 *	KMC_NOHASH      Disable hashing (unsupported)
 *	KMC_QCACHE	Disable qcache (unsupported)
 *	KMC_KMEM	Force kmem backed cache
 *	KMC_VMEM        Force vmem backed cache
 *	KMC_OFFSLAB	Locate objects off the slab
 */
spl_kmem_cache_t *
spl_kmem_cache_create(char *name, size_t size, size_t align,
                      spl_kmem_ctor_t ctor,
                      spl_kmem_dtor_t dtor,
                      spl_kmem_reclaim_t reclaim,
                      void *priv, void *vmp, int flags)
{
        spl_kmem_cache_t *skc;
	int rc, kmem_flags = KM_SLEEP;
	ENTRY;

	ASSERTF(!(flags & KMC_NOMAGAZINE), "Bad KMC_NOMAGAZINE (%x)\n", flags);
	ASSERTF(!(flags & KMC_NOHASH), "Bad KMC_NOHASH (%x)\n", flags);
	ASSERTF(!(flags & KMC_QCACHE), "Bad KMC_QCACHE (%x)\n", flags);
	ASSERT(vmp == NULL);

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
	skc->skc_obj_align = SPL_KMEM_CACHE_ALIGN;
	skc->skc_delay = SPL_KMEM_CACHE_DELAY;
	atomic_set(&skc->skc_ref, 0);

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

	if (align) {
		ASSERT((align & (align - 1)) == 0);    /* Power of two */
		ASSERT(align >= SPL_KMEM_CACHE_ALIGN); /* Minimum size */
		skc->skc_obj_align = align;
	}

	/* If none passed select a cache type based on object size */
	if (!(skc->skc_flags & (KMC_KMEM | KMC_VMEM))) {
		if (P2ROUNDUP(skc->skc_obj_size, skc->skc_obj_align) <
		    (PAGE_SIZE / 8)) {
			skc->skc_flags |= KMC_KMEM;
		} else {
			skc->skc_flags |= KMC_VMEM;
		}
	}

	rc = spl_slab_size(skc, &skc->skc_slab_objs, &skc->skc_slab_size);
	if (rc)
		GOTO(out, rc);

	rc = spl_magazine_create(skc);
	if (rc)
		GOTO(out, rc);

	spl_init_delayed_work(&skc->skc_work, spl_cache_age, skc);
	schedule_delayed_work(&skc->skc_work, 2 * skc->skc_delay * HZ);

	down_write(&spl_kmem_cache_sem);
	list_add_tail(&skc->skc_list, &spl_kmem_cache_list);
	up_write(&spl_kmem_cache_sem);

	RETURN(skc);
out:
	kmem_free(skc->skc_name, skc->skc_name_size);
	kmem_free(skc, sizeof(*skc));
	RETURN(NULL);
}
EXPORT_SYMBOL(spl_kmem_cache_create);

/*
 * Destroy a cache and all objects assoicated with the cache.
 */
void
spl_kmem_cache_destroy(spl_kmem_cache_t *skc)
{
	DECLARE_WAIT_QUEUE_HEAD(wq);
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	down_write(&spl_kmem_cache_sem);
	list_del_init(&skc->skc_list);
	up_write(&spl_kmem_cache_sem);

	/* Cancel any and wait for any pending delayed work */
	ASSERT(!test_and_set_bit(KMC_BIT_DESTROY, &skc->skc_flags));
	cancel_delayed_work(&skc->skc_work);
	flush_scheduled_work();

	/* Wait until all current callers complete, this is mainly
	 * to catch the case where a low memory situation triggers a
	 * cache reaping action which races with this destroy. */
	wait_event(wq, atomic_read(&skc->skc_ref) == 0);

	spl_magazine_destroy(skc);
	spl_slab_reclaim(skc, 1);
	spin_lock(&skc->skc_lock);

	/* Validate there are no objects in use and free all the
	 * spl_kmem_slab_t, spl_kmem_obj_t, and object buffers. */
	ASSERT3U(skc->skc_slab_alloc, ==, 0);
	ASSERT3U(skc->skc_obj_alloc, ==, 0);
	ASSERT3U(skc->skc_slab_total, ==, 0);
	ASSERT3U(skc->skc_obj_total, ==, 0);
	ASSERT(list_empty(&skc->skc_complete_list));

	kmem_free(skc->skc_name, skc->skc_name_size);
	spin_unlock(&skc->skc_lock);

	kmem_free(skc, sizeof(*skc));

	EXIT;
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

/*
 * No available objects on any slabsi, create a new slab.  Since this
 * is an expensive operation we do it without holding the spinlock and
 * only briefly aquire it when we link in the fully allocated and
 * constructed slab.
 */
static spl_kmem_slab_t *
spl_cache_grow(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_slab_t *sks;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	local_irq_enable();
	might_sleep();

	/*
	 * Before allocating a new slab check if the slab is being reaped.
	 * If it is there is a good chance we can wait until it finishes
	 * and then use one of the newly freed but not aged-out slabs.
	 */
	if (test_bit(KMC_BIT_REAPING, &skc->skc_flags)) {
		schedule();
		GOTO(out, sks= NULL);
	}

	/* Allocate a new slab for the cache */
	sks = spl_slab_alloc(skc, flags | __GFP_NORETRY | __GFP_NOWARN);
	if (sks == NULL)
		GOTO(out, sks = NULL);

	/* Link the new empty slab in to the end of skc_partial_list. */
	spin_lock(&skc->skc_lock);
	skc->skc_slab_total++;
	skc->skc_obj_total += sks->sks_objs;
	list_add_tail(&sks->sks_list, &skc->skc_partial_list);
	spin_unlock(&skc->skc_lock);
out:
	local_irq_disable();

	RETURN(sks);
}

/*
 * Refill a per-cpu magazine with objects from the slabs for this
 * cache.  Ideally the magazine can be repopulated using existing
 * objects which have been released, however if we are unable to
 * locate enough free objects new slabs of objects will be created.
 */
static int
spl_cache_refill(spl_kmem_cache_t *skc, spl_kmem_magazine_t *skm, int flags)
{
	spl_kmem_slab_t *sks;
	int rc = 0, refill;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skm->skm_magic == SKM_MAGIC);

	refill = MIN(skm->skm_refill, skm->skm_size - skm->skm_avail);
	spin_lock(&skc->skc_lock);

	while (refill > 0) {
		/* No slabs available we may need to grow the cache */
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

/*
 * Release an object back to the slab from which it came.
 */
static void
spl_cache_shrink(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_slab_t *sks = NULL;
	spl_kmem_obj_t *sko = NULL;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

	sko = obj + P2ROUNDUP(skc->skc_obj_size, skc->skc_obj_align);
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

/*
 * Release a batch of objects from a per-cpu magazine back to their
 * respective slabs.  This occurs when we exceed the magazine size,
 * are under memory pressure, when the cache is idle, or during
 * cache cleanup.  The flush argument contains the number of entries
 * to remove from the magazine.
 */
static int
spl_cache_flush(spl_kmem_cache_t *skc, spl_kmem_magazine_t *skm, int flush)
{
	int i, count = MIN(flush, skm->skm_avail);
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skm->skm_magic == SKM_MAGIC);

	/*
	 * XXX: Currently we simply return objects from the magazine to
	 * the slabs in fifo order.  The ideal thing to do from a memory
	 * fragmentation standpoint is to cheaply determine the set of
	 * objects in the magazine which will result in the largest
	 * number of free slabs if released from the magazine.
	 */
	spin_lock(&skc->skc_lock);
	for (i = 0; i < count; i++)
		spl_cache_shrink(skc, skm->skm_objs[i]);

	skm->skm_avail -= count;
	memmove(skm->skm_objs, &(skm->skm_objs[count]),
	        sizeof(void *) * skm->skm_avail);

	spin_unlock(&skc->skc_lock);

	RETURN(count);
}

/*
 * Allocate an object from the per-cpu magazine, or if the magazine
 * is empty directly allocate from a slab and repopulate the magazine.
 */
void *
spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags)
{
	spl_kmem_magazine_t *skm;
	unsigned long irq_flags;
	void *obj = NULL;
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));
	ASSERT(flags & KM_SLEEP);
	atomic_inc(&skc->skc_ref);
	local_irq_save(irq_flags);

restart:
	/* Safe to update per-cpu structure without lock, but
	 * in the restart case we must be careful to reaquire
	 * the local magazine since this may have changed
	 * when we need to grow the cache. */
	skm = skc->skc_mag[smp_processor_id()];
	ASSERTF(skm->skm_magic == SKM_MAGIC, "%x != %x: %s/%p/%p %x/%x/%x\n",
		skm->skm_magic, SKM_MAGIC, skc->skc_name, skc, skm,
		skm->skm_size, skm->skm_refill, skm->skm_avail);

	if (likely(skm->skm_avail)) {
		/* Object available in CPU cache, use it */
		obj = skm->skm_objs[--skm->skm_avail];
		skm->skm_age = jiffies;
	} else {
		/* Per-CPU cache empty, directly allocate from
		 * the slab and refill the per-CPU cache. */
		(void)spl_cache_refill(skc, skm, flags);
		GOTO(restart, obj = NULL);
	}

	local_irq_restore(irq_flags);
	ASSERT(obj);
	ASSERT(((unsigned long)(obj) % skc->skc_obj_align) == 0);

	/* Pre-emptively migrate object to CPU L1 cache */
	prefetchw(obj);
	atomic_dec(&skc->skc_ref);

	RETURN(obj);
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
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));
	atomic_inc(&skc->skc_ref);
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
	atomic_dec(&skc->skc_ref);

	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_free);

/*
 * The generic shrinker function for all caches.  Under linux a shrinker
 * may not be tightly coupled with a slab cache.  In fact linux always
 * systematically trys calling all registered shrinker callbacks which
 * report that they contain unused objects.  Because of this we only
 * register one shrinker function in the shim layer for all slab caches.
 * We always attempt to shrink all caches when this generic shrinker
 * is called.  The shrinker should return the number of free objects
 * in the cache when called with nr_to_scan == 0 but not attempt to
 * free any objects.  When nr_to_scan > 0 it is a request that nr_to_scan
 * objects should be freed, because Solaris semantics are to free
 * all available objects we may free more objects than requested.
 */
static int
spl_kmem_cache_generic_shrinker(int nr_to_scan, unsigned int gfp_mask)
{
	spl_kmem_cache_t *skc;
	int unused = 0;

	down_read(&spl_kmem_cache_sem);
	list_for_each_entry(skc, &spl_kmem_cache_list, skc_list) {
		if (nr_to_scan)
			spl_kmem_cache_reap_now(skc);

		/*
		 * Presume everything alloc'ed in reclaimable, this ensures
		 * we are called again with nr_to_scan > 0 so can try and
		 * reclaim.  The exact number is not important either so
		 * we forgo taking this already highly contented lock.
		 */
		unused += skc->skc_obj_alloc;
	}
	up_read(&spl_kmem_cache_sem);

	return (unused * sysctl_vfs_cache_pressure) / 100;
}

/*
 * Call the registered reclaim function for a cache.  Depending on how
 * many and which objects are released it may simply repopulate the
 * local magazine which will then need to age-out.  Objects which cannot
 * fit in the magazine we will be released back to their slabs which will
 * also need to age out before being release.  This is all just best
 * effort and we do not want to thrash creating and destroying slabs.
 */
void
spl_kmem_cache_reap_now(spl_kmem_cache_t *skc)
{
	ENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));

	/* Prevent concurrent cache reaping when contended */
	if (test_and_set_bit(KMC_BIT_REAPING, &skc->skc_flags)) {
		EXIT;
		return;
	}

	atomic_inc(&skc->skc_ref);

	if (skc->skc_reclaim)
		skc->skc_reclaim(skc->skc_private);

	spl_slab_reclaim(skc, 0);
	clear_bit(KMC_BIT_REAPING, &skc->skc_flags);
	atomic_dec(&skc->skc_ref);

	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_reap_now);

/*
 * Reap all free slabs from all registered caches.
 */
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
		printk(KERN_WARNING "%-16s %-5s %-16s %s:%s\n", "address",
		       "size", "data", "func", "line");

	list_for_each_entry(kd, list, kd_list)
		printk(KERN_WARNING "%p %-5d %-16s %s:%d\n", kd->kd_addr,
		       (int)kd->kd_size, spl_sprintf_addr(kd, str, 17, 8),
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
		      atomic64_read(&kmem_alloc_used), kmem_alloc_max);


	if (atomic64_read(&vmem_alloc_used) != 0)
		CWARN("vmem leaked %ld/%ld bytes\n",
		      atomic64_read(&vmem_alloc_used), vmem_alloc_max);

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
