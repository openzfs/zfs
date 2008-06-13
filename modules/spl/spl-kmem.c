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
atomic64_t kmem_cache_alloc_failed;

spinlock_t kmem_lock;
struct hlist_head kmem_table[KMEM_TABLE_SIZE];
struct list_head kmem_list;

spinlock_t vmem_lock;
struct hlist_head vmem_table[VMEM_TABLE_SIZE];
struct list_head vmem_list;

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);
EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);
EXPORT_SYMBOL(kmem_warning_flag);

EXPORT_SYMBOL(kmem_lock);
EXPORT_SYMBOL(kmem_table);
EXPORT_SYMBOL(kmem_list);

EXPORT_SYMBOL(vmem_lock);
EXPORT_SYMBOL(vmem_table);
EXPORT_SYMBOL(vmem_list);

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
 * XXX: Refactor the below code in to smaller functions.  This works
 *      for a first pass but each function is doing to much.
 *
 * XXX: Implement SPL proc interface to export full per cache stats.
 *
 * XXX: Implement work requests to keep an eye on each cache and
 *      shrink them via slab_reclaim() when they are wasting lots
 *      of space.  Currently this process is driven by the reapers.
 *
 * XXX: Implement proper small cache object support by embedding
 *      the spl_kmem_slab_t, spl_kmem_obj_t's, and objects in the
 *      allocated for a particular slab.
 *
 * XXX: Implement a resizable used object hash.  Currently the hash
 *      is statically sized for thousands of objects but it should
 *      grow based on observed worst case slab depth.
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

/* Ensure the __kmem_cache_create/__kmem_cache_destroy macros are
 * removed here to prevent a recursive substitution, we want to call
 * the native linux version.
 */
#undef kmem_cache_t
#undef kmem_cache_create
#undef kmem_cache_destroy
#undef kmem_cache_alloc
#undef kmem_cache_free

static struct list_head spl_kmem_cache_list;	/* List of caches */
static struct rw_semaphore spl_kmem_cache_sem;	/* Cache list lock */
static kmem_cache_t *spl_slab_cache;		/* Cache for slab structs */
static kmem_cache_t *spl_obj_cache;		/* Cache for obj structs */

#ifdef HAVE_SET_SHRINKER
static struct shrinker *spl_kmem_cache_shrinker;
#else
static int kmem_cache_generic_shrinker(int nr_to_scan, unsigned int gfp_mask);
static struct shrinker spl_kmem_cache_shrinker = {
	.shrink = kmem_cache_generic_shrinker,
	.seeks = KMC_DEFAULT_SEEKS,
};
#endif

static spl_kmem_slab_t *
slab_alloc(spl_kmem_cache_t *skc, int flags) {
	spl_kmem_slab_t *sks;
	spl_kmem_obj_t *sko, *n;
	int i;
	ENTRY;

	sks = kmem_cache_alloc(spl_slab_cache, flags);
	if (sks == NULL)
		RETURN(sks);

	sks->sks_magic = SKS_MAGIC;
	sks->sks_objs = SPL_KMEM_CACHE_OBJ_PER_SLAB;
	sks->sks_age = jiffies;
	sks->sks_cache = skc;
	INIT_LIST_HEAD(&sks->sks_list);
	INIT_LIST_HEAD(&sks->sks_free_list);
	atomic_set(&sks->sks_ref, 0);

	for (i = 0; i < sks->sks_objs; i++) {
		sko = kmem_cache_alloc(spl_obj_cache, flags);
		if (sko == NULL) {
out_alloc:
			/* Unable to fully construct slab, objects,
			 * and object data buffers unwind everything.
			 */
			list_for_each_entry_safe(sko, n, &sks->sks_free_list,
						 sko_list) {
				ASSERT(sko->sko_magic == SKO_MAGIC);
				vmem_free(sko->sko_addr, skc->skc_obj_size);
				list_del(&sko->sko_list);
				kmem_cache_free(spl_obj_cache, sko);
			}

			kmem_cache_free(spl_slab_cache, sks);
			GOTO(out, sks = NULL);
		}

		sko->sko_addr = vmem_alloc(skc->skc_obj_size, flags);
		if (sko->sko_addr == NULL) {
			kmem_cache_free(spl_obj_cache, sko);
			GOTO(out_alloc, sks = NULL);
		}

		sko->sko_magic = SKO_MAGIC;
		sko->sko_flags = 0;
		sko->sko_slab = sks;
		INIT_LIST_HEAD(&sko->sko_list);
	        INIT_HLIST_NODE(&sko->sko_hlist);
		list_add(&sko->sko_list, &sks->sks_free_list);
	}
out:
	RETURN(sks);
}

/* Removes slab from complete or partial list, so it must
 * be called with the 'skc->skc_sem' semaphore held.
 *                         */
static void
slab_free(spl_kmem_slab_t *sks) {
	spl_kmem_cache_t *skc;
	spl_kmem_obj_t *sko, *n;
	int i = 0;
	ENTRY;

	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(atomic_read(&sks->sks_ref) == 0);
	skc = sks->sks_cache;
	skc->skc_obj_total -= sks->sks_objs;
	skc->skc_slab_total--;

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	ASSERT(rwsem_is_locked(&skc->skc_sem));
#endif

	list_for_each_entry_safe(sko, n, &sks->sks_free_list, sko_list) {
		ASSERT(sko->sko_magic == SKO_MAGIC);

		/* Run destructors for being freed */
		if (skc->skc_dtor)
			skc->skc_dtor(sko->sko_addr, skc->skc_private);

		vmem_free(sko->sko_addr, skc->skc_obj_size);
		list_del(&sko->sko_list);
		kmem_cache_free(spl_obj_cache, sko);
		i++;
	}

	ASSERT(sks->sks_objs == i);
	list_del(&sks->sks_list);
	kmem_cache_free(spl_slab_cache, sks);

	EXIT;
}

static int
__slab_reclaim(spl_kmem_cache_t *skc)
{
	spl_kmem_slab_t *sks, *m;
	int rc = 0;
	ENTRY;

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
	ASSERT(rwsem_is_locked(&skc->skc_sem));
#endif
	/*
	 * Free empty slabs which have not been touched in skc_delay
	 * seconds.  This delay time is important to avoid thrashing.
	 * Empty slabs will be at the end of the skc_partial_list.
	 */
        list_for_each_entry_safe_reverse(sks, m, &skc->skc_partial_list,
					 sks_list) {
		if (atomic_read(&sks->sks_ref) > 0)
		       break;

		if (time_after(jiffies, sks->sks_age + skc->skc_delay * HZ)) {
			slab_free(sks);
			rc++;
		}
	}

	/* Returns number of slabs reclaimed */
	RETURN(rc);
}

static int
slab_reclaim(spl_kmem_cache_t *skc)
{
	int rc;
	ENTRY;

	down_write(&skc->skc_sem);
	rc = __slab_reclaim(skc);
	up_write(&skc->skc_sem);

	RETURN(rc);
}

spl_kmem_cache_t *
spl_kmem_cache_create(char *name, size_t size, size_t align,
                      spl_kmem_ctor_t ctor,
                      spl_kmem_dtor_t dtor,
                      spl_kmem_reclaim_t reclaim,
                      void *priv, void *vmp, int flags)
{
        spl_kmem_cache_t *skc;
	int i, kmem_flags = KM_SLEEP;
	ENTRY;

        /* We may be called when there is a non-zero preempt_count or
         * interrupts are disabled is which case we must not sleep.
	 */
        if (current_thread_info()->preempt_count || irqs_disabled())
		kmem_flags = KM_NOSLEEP;

	/* Allocate new cache memory and initialize. */
        skc = (spl_kmem_cache_t *)kmem_alloc(sizeof(*skc), kmem_flags);
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
	skc->skc_chunk_size = 0; /* XXX: Needed only when implementing   */
	skc->skc_slab_size = 0;  /*      small slab object optimizations */
	skc->skc_max_chunks = 0; /*      which are yet supported. */
	skc->skc_delay = SPL_KMEM_CACHE_DELAY;

	skc->skc_hash_bits = SPL_KMEM_CACHE_HASH_BITS;
	skc->skc_hash_size = SPL_KMEM_CACHE_HASH_SIZE;
	skc->skc_hash_elts = SPL_KMEM_CACHE_HASH_ELTS;
	skc->skc_hash = (struct hlist_head *)
		        kmem_alloc(skc->skc_hash_size, kmem_flags);
	if (skc->skc_hash == NULL) {
		kmem_free(skc->skc_name, skc->skc_name_size);
		kmem_free(skc, sizeof(*skc));
	}

	for (i = 0; i < skc->skc_hash_elts; i++)
		INIT_HLIST_HEAD(&skc->skc_hash[i]);

	INIT_LIST_HEAD(&skc->skc_list);
	INIT_LIST_HEAD(&skc->skc_complete_list);
	INIT_LIST_HEAD(&skc->skc_partial_list);
	init_rwsem(&skc->skc_sem);
        skc->skc_slab_fail = 0;
        skc->skc_slab_create = 0;
        skc->skc_slab_destroy = 0;
	skc->skc_slab_total = 0;
	skc->skc_slab_alloc = 0;
	skc->skc_slab_max = 0;
	skc->skc_obj_total = 0;
	skc->skc_obj_alloc = 0;
	skc->skc_obj_max = 0;
	skc->skc_hash_depth = 0;
	skc->skc_hash_max = 0;

	down_write(&spl_kmem_cache_sem);
        list_add_tail(&skc->skc_list, &spl_kmem_cache_list);
	up_write(&spl_kmem_cache_sem);

        RETURN(skc);
}
EXPORT_SYMBOL(spl_kmem_cache_create);

/* The caller must ensure there are no racing calls to
 * spl_kmem_cache_alloc() for this spl_kmem_cache_t when
 * it is being destroyed.
 */
void
spl_kmem_cache_destroy(spl_kmem_cache_t *skc)
{
        spl_kmem_slab_t *sks, *m;
	ENTRY;

        down_write(&spl_kmem_cache_sem);
        list_del_init(&skc->skc_list);
        up_write(&spl_kmem_cache_sem);

	down_write(&skc->skc_sem);

	/* Validate there are no objects in use and free all the
	 * spl_kmem_slab_t, spl_kmem_obj_t, and object buffers.
	 */
	ASSERT(list_empty(&skc->skc_complete_list));

        list_for_each_entry_safe(sks, m, &skc->skc_partial_list, sks_list)
		slab_free(sks);

	kmem_free(skc->skc_hash, skc->skc_hash_size);
	kmem_free(skc->skc_name, skc->skc_name_size);
	kmem_free(skc, sizeof(*skc));
	up_write(&skc->skc_sem);

	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_destroy);

/* The kernel provided hash_ptr() function behaves exceptionally badly
 * when all the addresses are page aligned which is likely the case
 * here.  To avoid this issue shift off the low order non-random bits.
 */
static unsigned long
spl_hash_ptr(void *ptr, unsigned int bits)
{
	return hash_long((unsigned long)ptr >> PAGE_SHIFT, bits);
}

#ifndef list_first_entry
#define list_first_entry(ptr, type, member) \
	        list_entry((ptr)->next, type, member)
#endif

void *
spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags)
{
        spl_kmem_slab_t *sks;
	spl_kmem_obj_t *sko;
	void *obj;
	unsigned long key;
	ENTRY;

	down_write(&skc->skc_sem);
restart:
	/* Check for available objects from the partial slabs */
	if (!list_empty(&skc->skc_partial_list)) {
		sks = list_first_entry(&skc->skc_partial_list,
		                       spl_kmem_slab_t, sks_list);
		ASSERT(sks->sks_magic == SKS_MAGIC);
		ASSERT(atomic_read(&sks->sks_ref) < sks->sks_objs);
		ASSERT(!list_empty(&sks->sks_free_list));

		sko = list_first_entry(&sks->sks_free_list,
		                       spl_kmem_obj_t, sko_list);
		ASSERT(sko->sko_magic == SKO_MAGIC);
		ASSERT(sko->sko_addr != NULL);

		/* Remove from sks_free_list, add to used hash */
		list_del_init(&sko->sko_list);
		key = spl_hash_ptr(sko->sko_addr, skc->skc_hash_bits);
		hlist_add_head_rcu(&sko->sko_hlist, &skc->skc_hash[key]);

		sks->sks_age = jiffies;
		atomic_inc(&sks->sks_ref);
		skc->skc_obj_alloc++;

		if (skc->skc_obj_alloc > skc->skc_obj_max)
			skc->skc_obj_max = skc->skc_obj_alloc;

		if (atomic_read(&sks->sks_ref) == 1) {
			skc->skc_slab_alloc++;

			if (skc->skc_slab_alloc > skc->skc_slab_max)
				skc->skc_slab_max = skc->skc_slab_alloc;
		}

		/* Move slab to skc_complete_list when full */
		if (atomic_read(&sks->sks_ref) == sks->sks_objs) {
			list_del(&sks->sks_list);
			list_add(&sks->sks_list, &skc->skc_complete_list);
		}

		GOTO(out_lock, obj = sko->sko_addr);
	}

	up_write(&skc->skc_sem);

	/* No available objects create a new slab.  Since this is an
	 * expensive operation we do it without holding the semaphore
	 * and only briefly aquire it when we link in the fully
	 * allocated and constructed slab.
	 */

	/* Under Solaris if the KM_SLEEP flag is passed we may never
	 * fail, so sleep as long as needed.  Additionally, since we are
	 * using vmem_alloc() KM_NOSLEEP is not an option and we must
	 * fail.  Shifting to allocating our own pages and mapping the
	 * virtual address space may allow us to bypass this issue.
	 */
	if (!flags)
		flags |= KM_SLEEP;

	if (flags & KM_SLEEP)
		flags |= __GFP_NOFAIL;
	else
		GOTO(out, obj = NULL);

	sks = slab_alloc(skc, flags);
	if (sks == NULL)
		GOTO(out, obj = NULL);

	/* Run all the constructors now that the slab is fully allocated */
	list_for_each_entry(sko, &sks->sks_free_list, sko_list) {
		ASSERT(sko->sko_magic == SKO_MAGIC);

		if (skc->skc_ctor)
			skc->skc_ctor(sko->sko_addr, skc->skc_private, flags);
	}

	/* Link the newly created slab in to the skc_partial_list,
	 * and retry the allocation which will now succeed.
	 */
	down_write(&skc->skc_sem);
	skc->skc_slab_total++;
	skc->skc_obj_total += sks->sks_objs;
	list_add_tail(&sks->sks_list, &skc->skc_partial_list);
	GOTO(restart, obj = NULL);

out_lock:
	up_write(&skc->skc_sem);
out:
	RETURN(obj);
}
EXPORT_SYMBOL(spl_kmem_cache_alloc);

void
spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj)
{
        struct hlist_head *head;
        struct hlist_node *node;
        spl_kmem_slab_t *sks = NULL;
	spl_kmem_obj_t *sko = NULL;
	ENTRY;

	down_write(&skc->skc_sem);

        head = &skc->skc_hash[spl_hash_ptr(obj, skc->skc_hash_bits)];
        hlist_for_each_entry_rcu(sko, node, head, sko_hlist) {
                if (sko->sko_addr == obj) {
			ASSERT(sko->sko_magic == SKO_MAGIC);
			sks = sko->sko_slab;
			break;
		}
	}

	ASSERT(sko != NULL); /* Obj must be in hash */
	ASSERT(sks != NULL); /* Obj must reference slab */
	ASSERT(sks->sks_cache == skc);
	hlist_del_init(&sko->sko_hlist);
	list_add(&sko->sko_list, &sks->sks_free_list);

	sks->sks_age = jiffies;
	atomic_dec(&sks->sks_ref);
	skc->skc_obj_alloc--;

	/* Move slab to skc_partial_list when no longer full.  Slabs
	 * are added to the kead to keep the partial list is quasi
	 * full sorted order.  Fuller at the head, emptier at the tail.
	 */
	if (atomic_read(&sks->sks_ref) == (sks->sks_objs - 1)) {
		list_del(&sks->sks_list);
		list_add(&sks->sks_list, &skc->skc_partial_list);
	}

	/* Move emply slabs to the end of the partial list so
	 * they can be easily found and freed during reclamation.
	 */
	if (atomic_read(&sks->sks_ref) == 0) {
		list_del(&sks->sks_list);
		list_add_tail(&sks->sks_list, &skc->skc_partial_list);
		skc->skc_slab_alloc--;
	}

	__slab_reclaim(skc);
	up_write(&skc->skc_sem);
}
EXPORT_SYMBOL(spl_kmem_cache_free);

static int
kmem_cache_generic_shrinker(int nr_to_scan, unsigned int gfp_mask)
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
	ENTRY;
        ASSERT(skc && skc->skc_magic == SKC_MAGIC);

	if (skc->skc_reclaim)
		skc->skc_reclaim(skc->skc_private);

	slab_reclaim(skc);
	EXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_reap_now);

void
spl_kmem_reap(void)
{
	kmem_cache_generic_shrinker(KMC_REAP_CHUNK, GFP_KERNEL);
}
EXPORT_SYMBOL(spl_kmem_reap);

int
spl_kmem_init(void)
{
        int rc = 0;
        ENTRY;

	init_rwsem(&spl_kmem_cache_sem);
	INIT_LIST_HEAD(&spl_kmem_cache_list);

	spl_slab_cache = NULL;
	spl_obj_cache = NULL;

	spl_slab_cache = __kmem_cache_create("spl_slab_cache",
					     sizeof(spl_kmem_slab_t),
					     0, 0, NULL, NULL);
	if (spl_slab_cache == NULL)
		GOTO(out_cache, rc = -ENOMEM);

	spl_obj_cache = __kmem_cache_create("spl_obj_cache",
					    sizeof(spl_kmem_obj_t),
					    0, 0, NULL, NULL);
	if (spl_obj_cache == NULL)
		GOTO(out_cache, rc = -ENOMEM);

#ifdef HAVE_SET_SHRINKER
	spl_kmem_cache_shrinker = set_shrinker(KMC_DEFAULT_SEEKS,
					       kmem_cache_generic_shrinker);
	if (spl_kmem_cache_shrinker == NULL)
		GOTO(out_cache, rc = -ENOMEM);
#else
	register_shrinker(&spl_kmem_cache_shrinker);
#endif

#ifdef DEBUG_KMEM
	{ int i;
	atomic64_set(&kmem_alloc_used, 0);
	atomic64_set(&vmem_alloc_used, 0);
	atomic64_set(&kmem_cache_alloc_failed, 0);

	spin_lock_init(&kmem_lock);
	INIT_LIST_HEAD(&kmem_list);

	for (i = 0; i < KMEM_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&kmem_table[i]);

	spin_lock_init(&vmem_lock);
	INIT_LIST_HEAD(&vmem_list);

	for (i = 0; i < VMEM_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&vmem_table[i]);
	}
#endif
	RETURN(rc);

out_cache:
	if (spl_obj_cache)
	        (void)kmem_cache_destroy(spl_obj_cache);

	if (spl_slab_cache)
	        (void)kmem_cache_destroy(spl_slab_cache);

	RETURN(rc);
}

#ifdef DEBUG_KMEM
static char *
sprintf_addr(kmem_debug_t *kd, char *str, int len, int min)
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
#endif /* DEBUG_KMEM */

void
spl_kmem_fini(void)
{
#ifdef DEBUG_KMEM
	unsigned long flags;
	kmem_debug_t *kd;
	char str[17];

	/* Display all unreclaimed memory addresses, including the
	 * allocation size and the first few bytes of what's located
	 * at that address to aid in debugging.  Performance is not
	 * a serious concern here since it is module unload time. */
	if (atomic64_read(&kmem_alloc_used) != 0)
		CWARN("kmem leaked %ld/%ld bytes\n",
		      atomic_read(&kmem_alloc_used), kmem_alloc_max);

	spin_lock_irqsave(&kmem_lock, flags);
	if (!list_empty(&kmem_list))
		CDEBUG(D_WARNING, "%-16s %-5s %-16s %s:%s\n",
		       "address", "size", "data", "func", "line");

	list_for_each_entry(kd, &kmem_list, kd_list)
		CDEBUG(D_WARNING, "%p %-5d %-16s %s:%d\n",
		       kd->kd_addr, kd->kd_size,
		       sprintf_addr(kd, str, 17, 8),
		       kd->kd_func, kd->kd_line);

	spin_unlock_irqrestore(&kmem_lock, flags);

	if (atomic64_read(&vmem_alloc_used) != 0)
		CWARN("vmem leaked %ld/%ld bytes\n",
		      atomic_read(&vmem_alloc_used), vmem_alloc_max);

	spin_lock_irqsave(&vmem_lock, flags);
	if (!list_empty(&vmem_list))
		CDEBUG(D_WARNING, "%-16s %-5s %-16s %s:%s\n",
		       "address", "size", "data", "func", "line");

	list_for_each_entry(kd, &vmem_list, kd_list)
		CDEBUG(D_WARNING, "%p %-5d %-16s %s:%d\n",
		       kd->kd_addr, kd->kd_size,
		       sprintf_addr(kd, str, 17, 8),
		       kd->kd_func, kd->kd_line);

	spin_unlock_irqrestore(&vmem_lock, flags);
#endif
	ENTRY;

#ifdef HAVE_SET_SHRINKER
	remove_shrinker(spl_kmem_cache_shrinker);
#else
	unregister_shrinker(&spl_kmem_cache_shrinker);
#endif

        (void)kmem_cache_destroy(spl_obj_cache);
        (void)kmem_cache_destroy(spl_slab_cache);

	EXIT;
}
