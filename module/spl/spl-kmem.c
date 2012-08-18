/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Kmem Implementation.
\*****************************************************************************/

#include <sys/kmem.h>
#include <spl-debug.h>

#ifdef SS_DEBUG_SUBSYS
#undef SS_DEBUG_SUBSYS
#endif

#define SS_DEBUG_SUBSYS SS_KMEM

/*
 * The minimum amount of memory measured in pages to be free at all
 * times on the system.  This is similar to Linux's zone->pages_min
 * multiplied by the number of zones and is sized based on that.
 */
pgcnt_t minfree = 0;
EXPORT_SYMBOL(minfree);

/*
 * The desired amount of memory measured in pages to be free at all
 * times on the system.  This is similar to Linux's zone->pages_low
 * multiplied by the number of zones and is sized based on that.
 * Assuming all zones are being used roughly equally, when we drop
 * below this threshold asynchronous page reclamation is triggered.
 */
pgcnt_t desfree = 0;
EXPORT_SYMBOL(desfree);

/*
 * When above this amount of memory measures in pages the system is
 * determined to have enough free memory.  This is similar to Linux's
 * zone->pages_high multiplied by the number of zones and is sized based
 * on that.  Assuming all zones are being used roughly equally, when
 * asynchronous page reclamation reaches this threshold it stops.
 */
pgcnt_t lotsfree = 0;
EXPORT_SYMBOL(lotsfree);

/* Unused always 0 in this implementation */
pgcnt_t needfree = 0;
EXPORT_SYMBOL(needfree);

pgcnt_t swapfs_minfree = 0;
EXPORT_SYMBOL(swapfs_minfree);

pgcnt_t swapfs_reserve = 0;
EXPORT_SYMBOL(swapfs_reserve);

vmem_t *heap_arena = NULL;
EXPORT_SYMBOL(heap_arena);

vmem_t *zio_alloc_arena = NULL;
EXPORT_SYMBOL(zio_alloc_arena);

vmem_t *zio_arena = NULL;
EXPORT_SYMBOL(zio_arena);

#ifndef HAVE_GET_VMALLOC_INFO
get_vmalloc_info_t get_vmalloc_info_fn = SYMBOL_POISON;
EXPORT_SYMBOL(get_vmalloc_info_fn);
#endif /* HAVE_GET_VMALLOC_INFO */

#ifdef HAVE_PGDAT_HELPERS
# ifndef HAVE_FIRST_ONLINE_PGDAT
first_online_pgdat_t first_online_pgdat_fn = SYMBOL_POISON;
EXPORT_SYMBOL(first_online_pgdat_fn);
# endif /* HAVE_FIRST_ONLINE_PGDAT */

# ifndef HAVE_NEXT_ONLINE_PGDAT
next_online_pgdat_t next_online_pgdat_fn = SYMBOL_POISON;
EXPORT_SYMBOL(next_online_pgdat_fn);
# endif /* HAVE_NEXT_ONLINE_PGDAT */

# ifndef HAVE_NEXT_ZONE
next_zone_t next_zone_fn = SYMBOL_POISON;
EXPORT_SYMBOL(next_zone_fn);
# endif /* HAVE_NEXT_ZONE */

#else /* HAVE_PGDAT_HELPERS */

# ifndef HAVE_PGDAT_LIST
struct pglist_data *pgdat_list_addr = SYMBOL_POISON;
EXPORT_SYMBOL(pgdat_list_addr);
# endif /* HAVE_PGDAT_LIST */

#endif /* HAVE_PGDAT_HELPERS */

#ifdef NEED_GET_ZONE_COUNTS
# ifndef HAVE_GET_ZONE_COUNTS
get_zone_counts_t get_zone_counts_fn = SYMBOL_POISON;
EXPORT_SYMBOL(get_zone_counts_fn);
# endif /* HAVE_GET_ZONE_COUNTS */

unsigned long
spl_global_page_state(spl_zone_stat_item_t item)
{
	unsigned long active;
	unsigned long inactive;
	unsigned long free;

	get_zone_counts(&active, &inactive, &free);
	switch (item) {
	case SPL_NR_FREE_PAGES: return free;
	case SPL_NR_INACTIVE:   return inactive;
	case SPL_NR_ACTIVE:     return active;
	default:                ASSERT(0); /* Unsupported */
	}

	return 0;
}
#else
# ifdef HAVE_GLOBAL_PAGE_STATE
unsigned long
spl_global_page_state(spl_zone_stat_item_t item)
{
	unsigned long pages = 0;

	switch (item) {
	case SPL_NR_FREE_PAGES:
#  ifdef HAVE_ZONE_STAT_ITEM_NR_FREE_PAGES
		pages += global_page_state(NR_FREE_PAGES);
#  endif
		break;
	case SPL_NR_INACTIVE:
#  ifdef HAVE_ZONE_STAT_ITEM_NR_INACTIVE
		pages += global_page_state(NR_INACTIVE);
#  endif
#  ifdef HAVE_ZONE_STAT_ITEM_NR_INACTIVE_ANON
		pages += global_page_state(NR_INACTIVE_ANON);
#  endif
#  ifdef HAVE_ZONE_STAT_ITEM_NR_INACTIVE_FILE
		pages += global_page_state(NR_INACTIVE_FILE);
#  endif
		break;
	case SPL_NR_ACTIVE:
#  ifdef HAVE_ZONE_STAT_ITEM_NR_ACTIVE
		pages += global_page_state(NR_ACTIVE);
#  endif
#  ifdef HAVE_ZONE_STAT_ITEM_NR_ACTIVE_ANON
		pages += global_page_state(NR_ACTIVE_ANON);
#  endif
#  ifdef HAVE_ZONE_STAT_ITEM_NR_ACTIVE_FILE
		pages += global_page_state(NR_ACTIVE_FILE);
#  endif
		break;
	default:
		ASSERT(0); /* Unsupported */
	}

	return pages;
}
# else
#  error "Both global_page_state() and get_zone_counts() unavailable"
# endif /* HAVE_GLOBAL_PAGE_STATE */
#endif /* NEED_GET_ZONE_COUNTS */
EXPORT_SYMBOL(spl_global_page_state);

#if !defined(HAVE_INVALIDATE_INODES) && !defined(HAVE_INVALIDATE_INODES_CHECK)
invalidate_inodes_t invalidate_inodes_fn = SYMBOL_POISON;
EXPORT_SYMBOL(invalidate_inodes_fn);
#endif /* !HAVE_INVALIDATE_INODES && !HAVE_INVALIDATE_INODES_CHECK */

#ifndef HAVE_SHRINK_DCACHE_MEMORY
shrink_dcache_memory_t shrink_dcache_memory_fn = SYMBOL_POISON;
EXPORT_SYMBOL(shrink_dcache_memory_fn);
#endif /* HAVE_SHRINK_DCACHE_MEMORY */

#ifndef HAVE_SHRINK_ICACHE_MEMORY
shrink_icache_memory_t shrink_icache_memory_fn = SYMBOL_POISON;
EXPORT_SYMBOL(shrink_icache_memory_fn);
#endif /* HAVE_SHRINK_ICACHE_MEMORY */

pgcnt_t
spl_kmem_availrmem(void)
{
	/* The amount of easily available memory */
	return (spl_global_page_state(SPL_NR_FREE_PAGES) +
	        spl_global_page_state(SPL_NR_INACTIVE));
}
EXPORT_SYMBOL(spl_kmem_availrmem);

size_t
vmem_size(vmem_t *vmp, int typemask)
{
        struct vmalloc_info vmi;
	size_t size = 0;

	ASSERT(vmp == NULL);
	ASSERT(typemask & (VMEM_ALLOC | VMEM_FREE));

	get_vmalloc_info(&vmi);
	if (typemask & VMEM_ALLOC)
		size += (size_t)vmi.used;

	if (typemask & VMEM_FREE)
		size += (size_t)(VMALLOC_TOTAL - vmi.used);

	return size;
}
EXPORT_SYMBOL(vmem_size);

int
kmem_debugging(void)
{
	return 0;
}
EXPORT_SYMBOL(kmem_debugging);

#ifndef HAVE_KVASPRINTF
/* Simplified asprintf. */
char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	unsigned int len;
	char *p;
	va_list aq;

	va_copy(aq, ap);
	len = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);

	p = kmalloc(len+1, gfp);
	if (!p)
		return NULL;

	vsnprintf(p, len+1, fmt, ap);

	return p;
}
EXPORT_SYMBOL(kvasprintf);
#endif /* HAVE_KVASPRINTF */

char *
kmem_vasprintf(const char *fmt, va_list ap)
{
	va_list aq;
	char *ptr;

	do {
		va_copy(aq, ap);
		ptr = kvasprintf(GFP_KERNEL, fmt, aq);
		va_end(aq);
	} while (ptr == NULL);

	return ptr;
}
EXPORT_SYMBOL(kmem_vasprintf);

char *
kmem_asprintf(const char *fmt, ...)
{
	va_list ap;
	char *ptr;

	do {
		va_start(ap, fmt);
		ptr = kvasprintf(GFP_KERNEL, fmt, ap);
		va_end(ap);
	} while (ptr == NULL);

	return ptr;
}
EXPORT_SYMBOL(kmem_asprintf);

static char *
__strdup(const char *str, int flags)
{
	char *ptr;
	int n;

	n = strlen(str);
	ptr = kmalloc_nofail(n + 1, flags);
	if (ptr)
		memcpy(ptr, str, n + 1);

	return ptr;
}

char *
strdup(const char *str)
{
	return __strdup(str, KM_SLEEP);
}
EXPORT_SYMBOL(strdup);

void
strfree(char *str)
{
	kfree(str);
}
EXPORT_SYMBOL(strfree);

/*
 * Memory allocation interfaces and debugging for basic kmem_*
 * and vmem_* style memory allocation.  When DEBUG_KMEM is enabled
 * the SPL will keep track of the total memory allocated, and
 * report any memory leaked when the module is unloaded.
 */
#ifdef DEBUG_KMEM

/* Shim layer memory accounting */
# ifdef HAVE_ATOMIC64_T
atomic64_t kmem_alloc_used = ATOMIC64_INIT(0);
unsigned long long kmem_alloc_max = 0;
atomic64_t vmem_alloc_used = ATOMIC64_INIT(0);
unsigned long long vmem_alloc_max = 0;
# else  /* HAVE_ATOMIC64_T */
atomic_t kmem_alloc_used = ATOMIC_INIT(0);
unsigned long long kmem_alloc_max = 0;
atomic_t vmem_alloc_used = ATOMIC_INIT(0);
unsigned long long vmem_alloc_max = 0;
# endif /* HAVE_ATOMIC64_T */

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);
EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);

/* When DEBUG_KMEM_TRACKING is enabled not only will total bytes be tracked
 * but also the location of every alloc and free.  When the SPL module is
 * unloaded a list of all leaked addresses and where they were allocated
 * will be dumped to the console.  Enabling this feature has a significant
 * impact on performance but it makes finding memory leaks straight forward.
 *
 * Not surprisingly with debugging enabled the xmem_locks are very highly
 * contended particularly on xfree().  If we want to run with this detailed
 * debugging enabled for anything other than debugging  we need to minimize
 * the contention by moving to a lock per xmem_table entry model.
 */
# ifdef DEBUG_KMEM_TRACKING

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

static kmem_debug_t *
kmem_del_init(spinlock_t *lock, struct hlist_head *table, int bits, const void *addr)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct kmem_debug *p;
	unsigned long flags;
	SENTRY;

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

	SRETURN(NULL);
}

void *
kmem_alloc_track(size_t size, int flags, const char *func, int line,
    int node_alloc, int node)
{
	void *ptr = NULL;
	kmem_debug_t *dptr;
	unsigned long irq_flags;
	SENTRY;

	/* Function may be called with KM_NOSLEEP so failure is possible */
	dptr = (kmem_debug_t *) kmalloc_nofail(sizeof(kmem_debug_t),
	    flags & ~__GFP_ZERO);

	if (unlikely(dptr == NULL)) {
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING, "debug "
		    "kmem_alloc(%ld, 0x%x) at %s:%d failed (%lld/%llu)\n",
		    sizeof(kmem_debug_t), flags, func, line,
		    kmem_alloc_used_read(), kmem_alloc_max);
	} else {
		/*
		 * Marked unlikely because we should never be doing this,
		 * we tolerate to up 2 pages but a single page is best.
		 */
		if (unlikely((size > PAGE_SIZE*2) && !(flags & KM_NODEBUG))) {
			SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING, "large "
			    "kmem_alloc(%llu, 0x%x) at %s:%d (%lld/%llu)\n",
			    (unsigned long long) size, flags, func, line,
			    kmem_alloc_used_read(), kmem_alloc_max);
			spl_debug_dumpstack(NULL);
		}

		/*
		 *  We use __strdup() below because the string pointed to by
		 * __FUNCTION__ might not be available by the time we want
		 * to print it since the module might have been unloaded.
		 * This can only fail in the KM_NOSLEEP case.
		 */
		dptr->kd_func = __strdup(func, flags & ~__GFP_ZERO);
		if (unlikely(dptr->kd_func == NULL)) {
			kfree(dptr);
			SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING,
			    "debug __strdup() at %s:%d failed (%lld/%llu)\n",
			    func, line, kmem_alloc_used_read(), kmem_alloc_max);
			goto out;
		}

		/* Use the correct allocator */
		if (node_alloc) {
			ASSERT(!(flags & __GFP_ZERO));
			ptr = kmalloc_node_nofail(size, flags, node);
		} else if (flags & __GFP_ZERO) {
			ptr = kzalloc_nofail(size, flags & ~__GFP_ZERO);
		} else {
			ptr = kmalloc_nofail(size, flags);
		}

		if (unlikely(ptr == NULL)) {
			kfree(dptr->kd_func);
			kfree(dptr);
			SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING, "kmem_alloc"
			    "(%llu, 0x%x) at %s:%d failed (%lld/%llu)\n",
			    (unsigned long long) size, flags, func, line,
			    kmem_alloc_used_read(), kmem_alloc_max);
			goto out;
		}

		kmem_alloc_used_add(size);
		if (unlikely(kmem_alloc_used_read() > kmem_alloc_max))
			kmem_alloc_max = kmem_alloc_used_read();

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

		SDEBUG_LIMIT(SD_INFO,
		    "kmem_alloc(%llu, 0x%x) at %s:%d = %p (%lld/%llu)\n",
		    (unsigned long long) size, flags, func, line, ptr,
		    kmem_alloc_used_read(), kmem_alloc_max);
	}
out:
	SRETURN(ptr);
}
EXPORT_SYMBOL(kmem_alloc_track);

void
kmem_free_track(const void *ptr, size_t size)
{
	kmem_debug_t *dptr;
	SENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	dptr = kmem_del_init(&kmem_lock, kmem_table, KMEM_HASH_BITS, ptr);

	/* Must exist in hash due to kmem_alloc() */
	ASSERT(dptr);

	/* Size must match */
	ASSERTF(dptr->kd_size == size, "kd_size (%llu) != size (%llu), "
	    "kd_func = %s, kd_line = %d\n", (unsigned long long) dptr->kd_size,
	    (unsigned long long) size, dptr->kd_func, dptr->kd_line);

	kmem_alloc_used_sub(size);
	SDEBUG_LIMIT(SD_INFO, "kmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, kmem_alloc_used_read(),
	    kmem_alloc_max);

	kfree(dptr->kd_func);

	memset(dptr, 0x5a, sizeof(kmem_debug_t));
	kfree(dptr);

	memset(ptr, 0x5a, size);
	kfree(ptr);

	SEXIT;
}
EXPORT_SYMBOL(kmem_free_track);

void *
vmem_alloc_track(size_t size, int flags, const char *func, int line)
{
	void *ptr = NULL;
	kmem_debug_t *dptr;
	unsigned long irq_flags;
	SENTRY;

	ASSERT(flags & KM_SLEEP);

	/* Function may be called with KM_NOSLEEP so failure is possible */
	dptr = (kmem_debug_t *) kmalloc_nofail(sizeof(kmem_debug_t),
	    flags & ~__GFP_ZERO);
	if (unlikely(dptr == NULL)) {
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING, "debug "
		    "vmem_alloc(%ld, 0x%x) at %s:%d failed (%lld/%llu)\n",
		    sizeof(kmem_debug_t), flags, func, line,
		    vmem_alloc_used_read(), vmem_alloc_max);
	} else {
		/*
		 * We use __strdup() below because the string pointed to by
		 * __FUNCTION__ might not be available by the time we want
		 * to print it, since the module might have been unloaded.
		 * This can never fail because we have already asserted
		 * that flags is KM_SLEEP.
		 */
		dptr->kd_func = __strdup(func, flags & ~__GFP_ZERO);
		if (unlikely(dptr->kd_func == NULL)) {
			kfree(dptr);
			SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING,
			    "debug __strdup() at %s:%d failed (%lld/%llu)\n",
			    func, line, vmem_alloc_used_read(), vmem_alloc_max);
			goto out;
		}

		/* Use the correct allocator */
		if (flags & __GFP_ZERO) {
			ptr = vzalloc_nofail(size, flags & ~__GFP_ZERO);
		} else {
			ptr = vmalloc_nofail(size, flags);
		}

		if (unlikely(ptr == NULL)) {
			kfree(dptr->kd_func);
			kfree(dptr);
			SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING, "vmem_alloc"
			    "(%llu, 0x%x) at %s:%d failed (%lld/%llu)\n",
			    (unsigned long long) size, flags, func, line,
			    vmem_alloc_used_read(), vmem_alloc_max);
			goto out;
		}

		vmem_alloc_used_add(size);
		if (unlikely(vmem_alloc_used_read() > vmem_alloc_max))
			vmem_alloc_max = vmem_alloc_used_read();

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

		SDEBUG_LIMIT(SD_INFO,
		    "vmem_alloc(%llu, 0x%x) at %s:%d = %p (%lld/%llu)\n",
		    (unsigned long long) size, flags, func, line,
		    ptr, vmem_alloc_used_read(), vmem_alloc_max);
	}
out:
	SRETURN(ptr);
}
EXPORT_SYMBOL(vmem_alloc_track);

void
vmem_free_track(const void *ptr, size_t size)
{
	kmem_debug_t *dptr;
	SENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	dptr = kmem_del_init(&vmem_lock, vmem_table, VMEM_HASH_BITS, ptr);

	/* Must exist in hash due to vmem_alloc() */
	ASSERT(dptr);

	/* Size must match */
	ASSERTF(dptr->kd_size == size, "kd_size (%llu) != size (%llu), "
	    "kd_func = %s, kd_line = %d\n", (unsigned long long) dptr->kd_size,
	    (unsigned long long) size, dptr->kd_func, dptr->kd_line);

	vmem_alloc_used_sub(size);
	SDEBUG_LIMIT(SD_INFO, "vmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, vmem_alloc_used_read(),
	    vmem_alloc_max);

	kfree(dptr->kd_func);

	memset(dptr, 0x5a, sizeof(kmem_debug_t));
	kfree(dptr);

	memset(ptr, 0x5a, size);
	vfree(ptr);

	SEXIT;
}
EXPORT_SYMBOL(vmem_free_track);

# else /* DEBUG_KMEM_TRACKING */

void *
kmem_alloc_debug(size_t size, int flags, const char *func, int line,
    int node_alloc, int node)
{
	void *ptr;
	SENTRY;

	/*
	 * Marked unlikely because we should never be doing this,
	 * we tolerate to up 2 pages but a single page is best.
	 */
	if (unlikely((size > PAGE_SIZE * 2) && !(flags & KM_NODEBUG))) {
		SDEBUG(SD_CONSOLE | SD_WARNING,
		    "large kmem_alloc(%llu, 0x%x) at %s:%d (%lld/%llu)\n",
		    (unsigned long long) size, flags, func, line,
		    kmem_alloc_used_read(), kmem_alloc_max);
		dump_stack();
	}

	/* Use the correct allocator */
	if (node_alloc) {
		ASSERT(!(flags & __GFP_ZERO));
		ptr = kmalloc_node_nofail(size, flags, node);
	} else if (flags & __GFP_ZERO) {
		ptr = kzalloc_nofail(size, flags & (~__GFP_ZERO));
	} else {
		ptr = kmalloc_nofail(size, flags);
	}

	if (unlikely(ptr == NULL)) {
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING,
		    "kmem_alloc(%llu, 0x%x) at %s:%d failed (%lld/%llu)\n",
		    (unsigned long long) size, flags, func, line,
		    kmem_alloc_used_read(), kmem_alloc_max);
	} else {
		kmem_alloc_used_add(size);
		if (unlikely(kmem_alloc_used_read() > kmem_alloc_max))
			kmem_alloc_max = kmem_alloc_used_read();

		SDEBUG_LIMIT(SD_INFO,
		    "kmem_alloc(%llu, 0x%x) at %s:%d = %p (%lld/%llu)\n",
		    (unsigned long long) size, flags, func, line, ptr,
		    kmem_alloc_used_read(), kmem_alloc_max);
	}

	SRETURN(ptr);
}
EXPORT_SYMBOL(kmem_alloc_debug);

void
kmem_free_debug(const void *ptr, size_t size)
{
	SENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	kmem_alloc_used_sub(size);
	SDEBUG_LIMIT(SD_INFO, "kmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, kmem_alloc_used_read(),
	    kmem_alloc_max);
	kfree(ptr);

	SEXIT;
}
EXPORT_SYMBOL(kmem_free_debug);

void *
vmem_alloc_debug(size_t size, int flags, const char *func, int line)
{
	void *ptr;
	SENTRY;

	ASSERT(flags & KM_SLEEP);

	/* Use the correct allocator */
	if (flags & __GFP_ZERO) {
		ptr = vzalloc_nofail(size, flags & (~__GFP_ZERO));
	} else {
		ptr = vmalloc_nofail(size, flags);
	}

	if (unlikely(ptr == NULL)) {
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING,
		    "vmem_alloc(%llu, 0x%x) at %s:%d failed (%lld/%llu)\n",
		    (unsigned long long) size, flags, func, line,
		    vmem_alloc_used_read(), vmem_alloc_max);
	} else {
		vmem_alloc_used_add(size);
		if (unlikely(vmem_alloc_used_read() > vmem_alloc_max))
			vmem_alloc_max = vmem_alloc_used_read();

		SDEBUG_LIMIT(SD_INFO, "vmem_alloc(%llu, 0x%x) = %p "
		    "(%lld/%llu)\n", (unsigned long long) size, flags, ptr,
		    vmem_alloc_used_read(), vmem_alloc_max);
	}

	SRETURN(ptr);
}
EXPORT_SYMBOL(vmem_alloc_debug);

void
vmem_free_debug(const void *ptr, size_t size)
{
	SENTRY;

	ASSERTF(ptr || size > 0, "ptr: %p, size: %llu", ptr,
	    (unsigned long long) size);

	vmem_alloc_used_sub(size);
	SDEBUG_LIMIT(SD_INFO, "vmem_free(%p, %llu) (%lld/%llu)\n", ptr,
	    (unsigned long long) size, vmem_alloc_used_read(),
	    vmem_alloc_max);
	vfree(ptr);

	SEXIT;
}
EXPORT_SYMBOL(vmem_free_debug);

# endif /* DEBUG_KMEM_TRACKING */
#endif /* DEBUG_KMEM */

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
 *    many Linux data type which do need to be explicitly destroyed.
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
 *
 * XXX: Improve the partial slab list by carefully maintaining a
 *      strict ordering of fullest to emptiest slabs based on
 *      the slab reference count.  This guarantees the when freeing
 *      slabs back to the system we need only linearly traverse the
 *      last N slabs in the list to discover all the freeable slabs.
 *
 * XXX: NUMA awareness for optionally allocating memory close to a
 *      particular core.  This can be advantageous if you know the slab
 *      object will be short lived and primarily accessed from one core.
 *
 * XXX: Slab coloring may also yield performance improvements and would
 *      be desirable to implement.
 */

struct list_head spl_kmem_cache_list;   /* List of caches */
struct rw_semaphore spl_kmem_cache_sem; /* Cache list lock */

static int spl_cache_flush(spl_kmem_cache_t *skc,
                           spl_kmem_magazine_t *skm, int flush);

SPL_SHRINKER_CALLBACK_FWD_DECLARE(spl_kmem_cache_generic_shrinker);
SPL_SHRINKER_DECLARE(spl_kmem_cache_shrinker,
	spl_kmem_cache_generic_shrinker, KMC_DEFAULT_SEEKS);

static void *
kv_alloc(spl_kmem_cache_t *skc, int size, int flags)
{
	void *ptr;

	ASSERT(ISP2(size));

	if (skc->skc_flags & KMC_KMEM)
		ptr = (void *)__get_free_pages(flags, get_order(size));
	else
		ptr = __vmalloc(size, flags | __GFP_HIGHMEM, PAGE_KERNEL);

	/* Resulting allocated memory will be page aligned */
	ASSERT(IS_P2ALIGNED(ptr, PAGE_SIZE));

	return ptr;
}

static void
kv_free(spl_kmem_cache_t *skc, void *ptr, int size)
{
	ASSERT(IS_P2ALIGNED(ptr, PAGE_SIZE));
	ASSERT(ISP2(size));

	/*
	 * The Linux direct reclaim path uses this out of band value to
	 * determine if forward progress is being made.  Normally this is
	 * incremented by kmem_freepages() which is part of the various
	 * Linux slab implementations.  However, since we are using none
	 * of that infrastructure we are responsible for incrementing it.
	 */
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += size >> PAGE_SHIFT;

	if (skc->skc_flags & KMC_KMEM)
		free_pages((unsigned long)ptr, get_order(size));
	else
		vfree(ptr);
}

/*
 * Required space for each aligned sks.
 */
static inline uint32_t
spl_sks_size(spl_kmem_cache_t *skc)
{
	return P2ROUNDUP_TYPED(sizeof(spl_kmem_slab_t),
	       skc->skc_obj_align, uint32_t);
}

/*
 * Required space for each aligned object.
 */
static inline uint32_t
spl_obj_size(spl_kmem_cache_t *skc)
{
	uint32_t align = skc->skc_obj_align;

	return P2ROUNDUP_TYPED(skc->skc_obj_size, align, uint32_t) +
	       P2ROUNDUP_TYPED(sizeof(spl_kmem_obj_t), align, uint32_t);
}

/*
 * Lookup the spl_kmem_object_t for an object given that object.
 */
static inline spl_kmem_obj_t *
spl_sko_from_obj(spl_kmem_cache_t *skc, void *obj)
{
	return obj + P2ROUNDUP_TYPED(skc->skc_obj_size,
	       skc->skc_obj_align, uint32_t);
}

/*
 * Required space for each offslab object taking in to account alignment
 * restrictions and the power-of-two requirement of kv_alloc().
 */
static inline uint32_t
spl_offslab_size(spl_kmem_cache_t *skc)
{
	return 1UL << (highbit(spl_obj_size(skc)) + 1);
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
	uint32_t obj_size, offslab_size = 0;
	int i,  rc = 0;

	base = kv_alloc(skc, skc->skc_slab_size, flags);
	if (base == NULL)
		SRETURN(NULL);

	sks = (spl_kmem_slab_t *)base;
	sks->sks_magic = SKS_MAGIC;
	sks->sks_objs = skc->skc_slab_objs;
	sks->sks_age = jiffies;
	sks->sks_cache = skc;
	INIT_LIST_HEAD(&sks->sks_list);
	INIT_LIST_HEAD(&sks->sks_free_list);
	sks->sks_ref = 0;
	obj_size = spl_obj_size(skc);

	if (skc->skc_flags & KMC_OFFSLAB)
		offslab_size = spl_offslab_size(skc);

	for (i = 0; i < sks->sks_objs; i++) {
		if (skc->skc_flags & KMC_OFFSLAB) {
			obj = kv_alloc(skc, offslab_size, flags);
			if (!obj)
				SGOTO(out, rc = -ENOMEM);
		} else {
			obj = base + spl_sks_size(skc) + (i * obj_size);
		}

		ASSERT(IS_P2ALIGNED(obj, skc->skc_obj_align));
		sko = spl_sko_from_obj(skc, obj);
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
				kv_free(skc, sko->sko_addr, offslab_size);

		kv_free(skc, base, skc->skc_slab_size);
		sks = NULL;
	}

	SRETURN(sks);
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
	SENTRY;

	ASSERT(sks->sks_magic == SKS_MAGIC);
	ASSERT(sks->sks_ref == 0);

	skc = sks->sks_cache;
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

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

	SEXIT;
}

/*
 * Traverses all the partial slabs attached to a cache and free those
 * which which are currently empty, and have not been touched for
 * skc_delay seconds to  avoid thrashing.  The count argument is
 * passed to optionally cap the number of slabs reclaimed, a count
 * of zero means try and reclaim everything.  When flag is set we
 * always free an available slab regardless of age.
 */
static void
spl_slab_reclaim(spl_kmem_cache_t *skc, int count, int flag)
{
	spl_kmem_slab_t *sks, *m;
	spl_kmem_obj_t *sko, *n;
	LIST_HEAD(sks_list);
	LIST_HEAD(sko_list);
	uint32_t size = 0;
	int i = 0;
	SENTRY;

	/*
	 * Move empty slabs and objects which have not been touched in
	 * skc_delay seconds on to private lists to be freed outside
	 * the spin lock.  This delay time is important to avoid thrashing
	 * however when flag is set the delay will not be used.
	 */
	spin_lock(&skc->skc_lock);
	list_for_each_entry_safe_reverse(sks,m,&skc->skc_partial_list,sks_list){
		/*
		 * All empty slabs are at the end of skc->skc_partial_list,
		 * therefore once a non-empty slab is found we can stop
		 * scanning.  Additionally, stop when reaching the target
		 * reclaim 'count' if a non-zero threshold is given.
		 */
		if ((sks->sks_ref > 0) || (count && i >= count))
			break;

		if (time_after(jiffies,sks->sks_age+skc->skc_delay*HZ)||flag) {
			spl_slab_free(sks, &sks_list, &sko_list);
			i++;
		}
	}
	spin_unlock(&skc->skc_lock);

	/*
	 * The following two loops ensure all the object destructors are
	 * run, any offslab objects are freed, and the slabs themselves
	 * are freed.  This is all done outside the skc->skc_lock since
	 * this allows the destructor to sleep, and allows us to perform
	 * a conditional reschedule when a freeing a large number of
	 * objects and slabs back to the system.
	 */
	if (skc->skc_flags & KMC_OFFSLAB)
		size = spl_offslab_size(skc);

	list_for_each_entry_safe(sko, n, &sko_list, sko_list) {
		ASSERT(sko->sko_magic == SKO_MAGIC);

		if (skc->skc_dtor)
			skc->skc_dtor(sko->sko_addr, skc->skc_private);

		if (skc->skc_flags & KMC_OFFSLAB)
			kv_free(skc, sko->sko_addr, size);

		cond_resched();
	}

	list_for_each_entry_safe(sks, m, &sks_list, sks_list) {
		ASSERT(sks->sks_magic == SKS_MAGIC);
		kv_free(skc, sks, skc->skc_slab_size);
		cond_resched();
	}

	SEXIT;
}

/*
 * Allocate a single emergency object for use by the caller.
 */
static int
spl_emergency_alloc(spl_kmem_cache_t *skc, int flags, void **obj)
{
	spl_kmem_emergency_t *ske;
	int empty;
	SENTRY;

	/* Last chance use a partial slab if one now exists */
	spin_lock(&skc->skc_lock);
	empty = list_empty(&skc->skc_partial_list);
	spin_unlock(&skc->skc_lock);
	if (!empty)
		SRETURN(-EEXIST);

	ske = kmalloc(sizeof(*ske), flags);
	if (ske == NULL)
		SRETURN(-ENOMEM);

	ske->ske_obj = kmalloc(skc->skc_obj_size, flags);
	if (ske->ske_obj == NULL) {
		kfree(ske);
		SRETURN(-ENOMEM);
	}

	if (skc->skc_ctor)
		skc->skc_ctor(ske->ske_obj, skc->skc_private, flags);

	spin_lock(&skc->skc_lock);
	skc->skc_obj_total++;
	skc->skc_obj_emergency++;
	if (skc->skc_obj_emergency > skc->skc_obj_emergency_max)
		skc->skc_obj_emergency_max = skc->skc_obj_emergency;

	list_add(&ske->ske_list, &skc->skc_emergency_list);
	spin_unlock(&skc->skc_lock);

	*obj = ske->ske_obj;

	SRETURN(0);
}

/*
 * Free the passed object if it is an emergency object or a normal slab
 * object.  Currently this is done by walking what should be a short list of
 * emergency objects.  If this proves to be too inefficient we can replace
 * the simple list with a hash.
 */
static int
spl_emergency_free(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_emergency_t *m, *n, *ske = NULL;
	SENTRY;

	spin_lock(&skc->skc_lock);
	list_for_each_entry_safe(m, n, &skc->skc_emergency_list, ske_list) {
		if (m->ske_obj == obj) {
			list_del(&m->ske_list);
			skc->skc_obj_emergency--;
			skc->skc_obj_total--;
			ske = m;
			break;
		}
	}
	spin_unlock(&skc->skc_lock);

	if (ske == NULL)
		SRETURN(-ENOENT);

	if (skc->skc_dtor)
		skc->skc_dtor(ske->ske_obj, skc->skc_private);

	kfree(ske->ske_obj);
	kfree(ske);

	SRETURN(0);
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
	spl_kmem_magazine_t *skm =
		spl_get_work_data(data, spl_kmem_magazine_t, skm_work.work);
	spl_kmem_cache_t *skc = skm->skm_cache;

	ASSERT(skm->skm_magic == SKM_MAGIC);
	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(skc->skc_mag[skm->skm_cpu] == skm);

	if (skm->skm_avail > 0 &&
	    time_after(jiffies, skm->skm_age + skc->skc_delay * HZ))
		(void)spl_cache_flush(skc, skm, skm->skm_refill);

	if (!test_bit(KMC_BIT_DESTROY, &skc->skc_flags))
		schedule_delayed_work_on(skm->skm_cpu, &skm->skm_work,
					 skc->skc_delay / 3 * HZ);
}

/*
 * Called regularly to keep a downward pressure on the size of idle
 * magazines and to release free slabs from the cache.  This function
 * never calls the registered reclaim function, that only occurs
 * under memory pressure or with a direct call to spl_kmem_reap().
 */
static void
spl_cache_age(void *data)
{
	spl_kmem_cache_t *skc =
		spl_get_work_data(data, spl_kmem_cache_t, skc_work.work);

	ASSERT(skc->skc_magic == SKC_MAGIC);
	spl_slab_reclaim(skc, skc->skc_reap, 0);

	if (!test_bit(KMC_BIT_DESTROY, &skc->skc_flags))
		schedule_delayed_work(&skc->skc_work, skc->skc_delay / 3 * HZ);
}

/*
 * Size a slab based on the size of each aligned object plus spl_kmem_obj_t.
 * When on-slab we want to target SPL_KMEM_CACHE_OBJ_PER_SLAB.  However,
 * for very small objects we may end up with more than this so as not
 * to waste space in the minimal allocation of a single page.  Also for
 * very large objects we may use as few as SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN,
 * lower than this and we will fail.
 */
static int
spl_slab_size(spl_kmem_cache_t *skc, uint32_t *objs, uint32_t *size)
{
	uint32_t sks_size, obj_size, max_size;

	if (skc->skc_flags & KMC_OFFSLAB) {
		*objs = SPL_KMEM_CACHE_OBJ_PER_SLAB;
		*size = sizeof(spl_kmem_slab_t);
	} else {
		sks_size = spl_sks_size(skc);
		obj_size = spl_obj_size(skc);

		if (skc->skc_flags & KMC_KMEM)
			max_size = ((uint32_t)1 << (MAX_ORDER-3)) * PAGE_SIZE;
		else
			max_size = (32 * 1024 * 1024);

		/* Power of two sized slab */
		for (*size = PAGE_SIZE; *size <= max_size; *size *= 2) {
			*objs = (*size - sks_size) / obj_size;
			if (*objs >= SPL_KMEM_CACHE_OBJ_PER_SLAB)
				SRETURN(0);
		}

		/*
		 * Unable to satisfy target objects per slab, fall back to
		 * allocating a maximally sized slab and assuming it can
		 * contain the minimum objects count use it.  If not fail.
		 */
		*size = max_size;
		*objs = (*size - sks_size) / obj_size;
		if (*objs >= SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN)
			SRETURN(0);
	}

	SRETURN(-ENOSPC);
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
	SENTRY;

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

	SRETURN(size);
}

/*
 * Allocate a per-cpu magazine to associate with a specific core.
 */
static spl_kmem_magazine_t *
spl_magazine_alloc(spl_kmem_cache_t *skc, int cpu)
{
	spl_kmem_magazine_t *skm;
	int size = sizeof(spl_kmem_magazine_t) +
	           sizeof(void *) * skc->skc_mag_size;
	SENTRY;

	skm = kmem_alloc_node(size, KM_SLEEP, cpu_to_node(cpu));
	if (skm) {
		skm->skm_magic = SKM_MAGIC;
		skm->skm_avail = 0;
		skm->skm_size = skc->skc_mag_size;
		skm->skm_refill = skc->skc_mag_refill;
		skm->skm_cache = skc;
		spl_init_delayed_work(&skm->skm_work, spl_magazine_age, skm);
		skm->skm_age = jiffies;
		skm->skm_cpu = cpu;
	}

	SRETURN(skm);
}

/*
 * Free a per-cpu magazine associated with a specific core.
 */
static void
spl_magazine_free(spl_kmem_magazine_t *skm)
{
	int size = sizeof(spl_kmem_magazine_t) +
	           sizeof(void *) * skm->skm_size;

	SENTRY;
	ASSERT(skm->skm_magic == SKM_MAGIC);
	ASSERT(skm->skm_avail == 0);

	kmem_free(skm, size);
	SEXIT;
}

/*
 * Create all pre-cpu magazines of reasonable sizes.
 */
static int
spl_magazine_create(spl_kmem_cache_t *skc)
{
	int i;
	SENTRY;

	skc->skc_mag_size = spl_magazine_size(skc);
	skc->skc_mag_refill = (skc->skc_mag_size + 1) / 2;

	for_each_online_cpu(i) {
		skc->skc_mag[i] = spl_magazine_alloc(skc, i);
		if (!skc->skc_mag[i]) {
			for (i--; i >= 0; i--)
				spl_magazine_free(skc->skc_mag[i]);

			SRETURN(-ENOMEM);
		}
	}

	/* Only after everything is allocated schedule magazine work */
	for_each_online_cpu(i)
		schedule_delayed_work_on(i, &skc->skc_mag[i]->skm_work,
				         skc->skc_delay / 3 * HZ);

	SRETURN(0);
}

/*
 * Destroy all pre-cpu magazines.
 */
static void
spl_magazine_destroy(spl_kmem_cache_t *skc)
{
	spl_kmem_magazine_t *skm;
	int i;
	SENTRY;

        for_each_online_cpu(i) {
		skm = skc->skc_mag[i];
		(void)spl_cache_flush(skc, skm, skm->skm_avail);
		spl_magazine_free(skm);
        }

	SEXIT;
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
	SENTRY;

	ASSERTF(!(flags & KMC_NOMAGAZINE), "Bad KMC_NOMAGAZINE (%x)\n", flags);
	ASSERTF(!(flags & KMC_NOHASH), "Bad KMC_NOHASH (%x)\n", flags);
	ASSERTF(!(flags & KMC_QCACHE), "Bad KMC_QCACHE (%x)\n", flags);
	ASSERT(vmp == NULL);

        /* We may be called when there is a non-zero preempt_count or
         * interrupts are disabled is which case we must not sleep.
	 */
	if (current_thread_info()->preempt_count || irqs_disabled())
		kmem_flags = KM_NOSLEEP;

	/* Allocate memory for a new cache an initialize it.  Unfortunately,
	 * this usually ends up being a large allocation of ~32k because
	 * we need to allocate enough memory for the worst case number of
	 * cpus in the magazine, skc_mag[NR_CPUS].  Because of this we
	 * explicitly pass KM_NODEBUG to suppress the kmem warning */
	skc = (spl_kmem_cache_t *)kmem_zalloc(sizeof(*skc),
	                                      kmem_flags | KM_NODEBUG);
	if (skc == NULL)
		SRETURN(NULL);

	skc->skc_magic = SKC_MAGIC;
	skc->skc_name_size = strlen(name) + 1;
	skc->skc_name = (char *)kmem_alloc(skc->skc_name_size, kmem_flags);
	if (skc->skc_name == NULL) {
		kmem_free(skc, sizeof(*skc));
		SRETURN(NULL);
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
	skc->skc_reap = SPL_KMEM_CACHE_REAP;
	atomic_set(&skc->skc_ref, 0);

	INIT_LIST_HEAD(&skc->skc_list);
	INIT_LIST_HEAD(&skc->skc_complete_list);
	INIT_LIST_HEAD(&skc->skc_partial_list);
	INIT_LIST_HEAD(&skc->skc_emergency_list);
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
	skc->skc_obj_emergency = 0;
	skc->skc_obj_emergency_max = 0;

	if (align) {
		VERIFY(ISP2(align));
		VERIFY3U(align, >=, SPL_KMEM_CACHE_ALIGN); /* Min alignment */
		VERIFY3U(align, <=, PAGE_SIZE);            /* Max alignment */
		skc->skc_obj_align = align;
	}

	/* If none passed select a cache type based on object size */
	if (!(skc->skc_flags & (KMC_KMEM | KMC_VMEM))) {
		if (spl_obj_size(skc) < (PAGE_SIZE / 8))
			skc->skc_flags |= KMC_KMEM;
		else
			skc->skc_flags |= KMC_VMEM;
	}

	rc = spl_slab_size(skc, &skc->skc_slab_objs, &skc->skc_slab_size);
	if (rc)
		SGOTO(out, rc);

	rc = spl_magazine_create(skc);
	if (rc)
		SGOTO(out, rc);

	spl_init_delayed_work(&skc->skc_work, spl_cache_age, skc);
	schedule_delayed_work(&skc->skc_work, skc->skc_delay / 3 * HZ);

	down_write(&spl_kmem_cache_sem);
	list_add_tail(&skc->skc_list, &spl_kmem_cache_list);
	up_write(&spl_kmem_cache_sem);

	SRETURN(skc);
out:
	kmem_free(skc->skc_name, skc->skc_name_size);
	kmem_free(skc, sizeof(*skc));
	SRETURN(NULL);
}
EXPORT_SYMBOL(spl_kmem_cache_create);

/*
 * Register a move callback to for cache defragmentation.
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
	int i;
	SENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	down_write(&spl_kmem_cache_sem);
	list_del_init(&skc->skc_list);
	up_write(&spl_kmem_cache_sem);

	/* Cancel any and wait for any pending delayed work */
	VERIFY(!test_and_set_bit(KMC_BIT_DESTROY, &skc->skc_flags));
	cancel_delayed_work_sync(&skc->skc_work);
	for_each_online_cpu(i)
		cancel_delayed_work_sync(&skc->skc_mag[i]->skm_work);

	flush_scheduled_work();

	/* Wait until all current callers complete, this is mainly
	 * to catch the case where a low memory situation triggers a
	 * cache reaping action which races with this destroy. */
	wait_event(wq, atomic_read(&skc->skc_ref) == 0);

	spl_magazine_destroy(skc);
	spl_slab_reclaim(skc, 0, 1);
	spin_lock(&skc->skc_lock);

	/* Validate there are no objects in use and free all the
	 * spl_kmem_slab_t, spl_kmem_obj_t, and object buffers. */
	ASSERT3U(skc->skc_slab_alloc, ==, 0);
	ASSERT3U(skc->skc_obj_alloc, ==, 0);
	ASSERT3U(skc->skc_slab_total, ==, 0);
	ASSERT3U(skc->skc_obj_total, ==, 0);
	ASSERT3U(skc->skc_obj_emergency, ==, 0);
	ASSERT(list_empty(&skc->skc_complete_list));
	ASSERT(list_empty(&skc->skc_emergency_list));

	kmem_free(skc->skc_name, skc->skc_name_size);
	spin_unlock(&skc->skc_lock);

	kmem_free(skc, sizeof(*skc));

	SEXIT;
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
 * Generic slab allocation function to run by the global work queues.
 * It is responsible for allocating a new slab, linking it in to the list
 * of partial slabs, and then waking any waiters.
 */
static void
spl_cache_grow_work(void *data)
{
	spl_kmem_alloc_t *ska =
		spl_get_work_data(data, spl_kmem_alloc_t, ska_work.work);
	spl_kmem_cache_t *skc = ska->ska_cache;
	spl_kmem_slab_t *sks;

	sks = spl_slab_alloc(skc, ska->ska_flags | __GFP_NORETRY | KM_NODEBUG);
	spin_lock(&skc->skc_lock);
	if (sks) {
		skc->skc_slab_total++;
		skc->skc_obj_total += sks->sks_objs;
		list_add_tail(&sks->sks_list, &skc->skc_partial_list);
	}

	atomic_dec(&skc->skc_ref);
	clear_bit(KMC_BIT_GROWING, &skc->skc_flags);
	wake_up_all(&skc->skc_waitq);
	spin_unlock(&skc->skc_lock);

	kfree(ska);
}

/*
 * Returns non-zero when a new slab should be available.
 */
static int
spl_cache_grow_wait(spl_kmem_cache_t *skc)
{
	return !test_bit(KMC_BIT_GROWING, &skc->skc_flags);
}

/*
 * No available objects on any slabs, create a new slab.
 */
static int
spl_cache_grow(spl_kmem_cache_t *skc, int flags, void **obj)
{
	int remaining, rc = 0;
	SENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	might_sleep();
	*obj = NULL;

	/*
	 * Before allocating a new slab check if the slab is being reaped.
	 * If it is there is a good chance we can wait until it finishes
	 * and then use one of the newly freed but not aged-out slabs.
	 */
	if (test_bit(KMC_BIT_REAPING, &skc->skc_flags))
		SRETURN(-EAGAIN);

	/*
	 * This is handled by dispatching a work request to the global work
	 * queue.  This allows us to asynchronously allocate a new slab while
	 * retaining the ability to safely fall back to a smaller synchronous
	 * allocations to ensure forward progress is always maintained.
	 */
	if (test_and_set_bit(KMC_BIT_GROWING, &skc->skc_flags) == 0) {
		spl_kmem_alloc_t *ska;

		ska = kmalloc(sizeof(*ska), flags);
		if (ska == NULL) {
			clear_bit(KMC_BIT_GROWING, &skc->skc_flags);
			wake_up_all(&skc->skc_waitq);
			SRETURN(-ENOMEM);
		}

		atomic_inc(&skc->skc_ref);
		ska->ska_cache = skc;
		ska->ska_flags = flags;
		spl_init_delayed_work(&ska->ska_work, spl_cache_grow_work, ska);
		schedule_delayed_work(&ska->ska_work, 0);
	}

	/*
	 * Allow a single timer tick before falling back to synchronously
	 * allocating the minimum about of memory required by the caller.
	 */
	remaining = wait_event_timeout(skc->skc_waitq,
				       spl_cache_grow_wait(skc), 1);
	if (remaining == 0)
		rc = spl_emergency_alloc(skc, flags, obj);

	SRETURN(rc);
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
	SENTRY;

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
				SRETURN(obj);

			if (rc)
				SGOTO(out, rc);

			/* Rescheduled to different CPU skm is not local */
			if (skm != skc->skc_mag[smp_processor_id()])
				SGOTO(out, rc);

			/* Potentially rescheduled to the same CPU but
			 * allocations may have occurred from this CPU while
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
		while (sks->sks_ref < sks->sks_objs && refill-- > 0 && ++count) {
			ASSERT(skm->skm_avail < skm->skm_size);
			ASSERT(count < skm->skm_size);
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
	SRETURN(NULL);
}

/*
 * Release an object back to the slab from which it came.
 */
static void
spl_cache_shrink(spl_kmem_cache_t *skc, void *obj)
{
	spl_kmem_slab_t *sks = NULL;
	spl_kmem_obj_t *sko = NULL;
	SENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(spin_is_locked(&skc->skc_lock));

	sko = spl_sko_from_obj(skc, obj);
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

	/* Move empty slabs to the end of the partial list so
	 * they can be easily found and freed during reclamation. */
	if (sks->sks_ref == 0) {
		list_del(&sks->sks_list);
		list_add_tail(&sks->sks_list, &skc->skc_partial_list);
		skc->skc_slab_alloc--;
	}

	SEXIT;
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
	SENTRY;

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

	SRETURN(count);
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
	SENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));
	ASSERT(flags & KM_SLEEP);
	atomic_inc(&skc->skc_ref);
	local_irq_save(irq_flags);

restart:
	/* Safe to update per-cpu structure without lock, but
	 * in the restart case we must be careful to reacquire
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
		obj = spl_cache_refill(skc, skm, flags);
		if (obj == NULL)
			SGOTO(restart, obj = NULL);
	}

	local_irq_restore(irq_flags);
	ASSERT(obj);
	ASSERT(IS_P2ALIGNED(obj, skc->skc_obj_align));

	/* Pre-emptively migrate object to CPU L1 cache */
	prefetchw(obj);
	atomic_dec(&skc->skc_ref);

	SRETURN(obj);
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
	SENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));
	atomic_inc(&skc->skc_ref);

	/*
	 * Emergency objects are never part of the virtual address space
	 * so if we get a virtual address we can optimize this check out.
	 */
	if (!kmem_virt(obj) && !spl_emergency_free(skc, obj))
		SGOTO(out, 0);

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
out:
	atomic_dec(&skc->skc_ref);

	SEXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_free);

/*
 * The generic shrinker function for all caches.  Under Linux a shrinker
 * may not be tightly coupled with a slab cache.  In fact Linux always
 * systematically tries calling all registered shrinker callbacks which
 * report that they contain unused objects.  Because of this we only
 * register one shrinker function in the shim layer for all slab caches.
 * We always attempt to shrink all caches when this generic shrinker
 * is called.  The shrinker should return the number of free objects
 * in the cache when called with nr_to_scan == 0 but not attempt to
 * free any objects.  When nr_to_scan > 0 it is a request that nr_to_scan
 * objects should be freed, which differs from Solaris semantics.
 * Solaris semantics are to free all available objects which may (and
 * probably will) be more objects than the requested nr_to_scan.
 */
static int
__spl_kmem_cache_generic_shrinker(struct shrinker *shrink,
    struct shrink_control *sc)
{
	spl_kmem_cache_t *skc;
	int unused = 0;

	down_read(&spl_kmem_cache_sem);
	list_for_each_entry(skc, &spl_kmem_cache_list, skc_list) {
		if (sc->nr_to_scan)
			spl_kmem_cache_reap_now(skc,
			   MAX(sc->nr_to_scan >> fls64(skc->skc_slab_objs), 1));

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

SPL_SHRINKER_CALLBACK_WRAPPER(spl_kmem_cache_generic_shrinker);

/*
 * Call the registered reclaim function for a cache.  Depending on how
 * many and which objects are released it may simply repopulate the
 * local magazine which will then need to age-out.  Objects which cannot
 * fit in the magazine we will be released back to their slabs which will
 * also need to age out before being release.  This is all just best
 * effort and we do not want to thrash creating and destroying slabs.
 */
void
spl_kmem_cache_reap_now(spl_kmem_cache_t *skc, int count)
{
	SENTRY;

	ASSERT(skc->skc_magic == SKC_MAGIC);
	ASSERT(!test_bit(KMC_BIT_DESTROY, &skc->skc_flags));

	/* Prevent concurrent cache reaping when contended */
	if (test_and_set_bit(KMC_BIT_REAPING, &skc->skc_flags)) {
		SEXIT;
		return;
	}

	atomic_inc(&skc->skc_ref);

	/*
	 * When a reclaim function is available it may be invoked repeatedly
	 * until at least a single slab can be freed.  This ensures that we
	 * do free memory back to the system.  This helps minimize the chance
	 * of an OOM event when the bulk of memory is used by the slab.
	 *
	 * When free slabs are already available the reclaim callback will be
	 * skipped.  Additionally, if no forward progress is detected despite
	 * a reclaim function the cache will be skipped to avoid deadlock.
	 *
	 * Longer term this would be the correct place to add the code which
	 * repacks the slabs in order minimize fragmentation.
	 */
	if (skc->skc_reclaim) {
		uint64_t objects = UINT64_MAX;
		int do_reclaim;

		do {
			spin_lock(&skc->skc_lock);
			do_reclaim =
			    (skc->skc_slab_total > 0) &&
			    ((skc->skc_slab_total - skc->skc_slab_alloc) == 0) &&
			    (skc->skc_obj_alloc < objects);

			objects = skc->skc_obj_alloc;
			spin_unlock(&skc->skc_lock);

			if (do_reclaim)
				skc->skc_reclaim(skc->skc_private);

		} while (do_reclaim);
	}

	/* Reclaim from the cache, ignoring it's age and delay. */
	spl_slab_reclaim(skc, count, 1);
	clear_bit(KMC_BIT_REAPING, &skc->skc_flags);
	atomic_dec(&skc->skc_ref);

	SEXIT;
}
EXPORT_SYMBOL(spl_kmem_cache_reap_now);

/*
 * Reap all free slabs from all registered caches.
 */
void
spl_kmem_reap(void)
{
	struct shrink_control sc;

	sc.nr_to_scan = KMC_REAP_CHUNK;
	sc.gfp_mask = GFP_KERNEL;

	__spl_kmem_cache_generic_shrinker(NULL, &sc);
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
	SENTRY;

	spin_lock_init(lock);
	INIT_LIST_HEAD(list);

	for (i = 0; i < size; i++)
		INIT_HLIST_HEAD(&kmem_table[i]);

	SRETURN(0);
}

static void
spl_kmem_fini_tracking(struct list_head *list, spinlock_t *lock)
{
	unsigned long flags;
	kmem_debug_t *kd;
	char str[17];
	SENTRY;

	spin_lock_irqsave(lock, flags);
	if (!list_empty(list))
		printk(KERN_WARNING "%-16s %-5s %-16s %s:%s\n", "address",
		       "size", "data", "func", "line");

	list_for_each_entry(kd, list, kd_list)
		printk(KERN_WARNING "%p %-5d %-16s %s:%d\n", kd->kd_addr,
		       (int)kd->kd_size, spl_sprintf_addr(kd, str, 17, 8),
		       kd->kd_func, kd->kd_line);

	spin_unlock_irqrestore(lock, flags);
	SEXIT;
}
#else /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */
#define spl_kmem_init_tracking(list, lock, size)
#define spl_kmem_fini_tracking(list, lock)
#endif /* DEBUG_KMEM && DEBUG_KMEM_TRACKING */

static void
spl_kmem_init_globals(void)
{
	struct zone *zone;

	/* For now all zones are includes, it may be wise to restrict
	 * this to normal and highmem zones if we see problems. */
        for_each_zone(zone) {

                if (!populated_zone(zone))
                        continue;

		minfree += min_wmark_pages(zone);
		desfree += low_wmark_pages(zone);
		lotsfree += high_wmark_pages(zone);
	}

	/* Solaris default values */
	swapfs_minfree = MAX(2*1024*1024 >> PAGE_SHIFT, physmem >> 3);
	swapfs_reserve = MIN(4*1024*1024 >> PAGE_SHIFT, physmem >> 4);
}

/*
 * Called at module init when it is safe to use spl_kallsyms_lookup_name()
 */
int
spl_kmem_init_kallsyms_lookup(void)
{
#ifndef HAVE_GET_VMALLOC_INFO
	get_vmalloc_info_fn = (get_vmalloc_info_t)
		spl_kallsyms_lookup_name("get_vmalloc_info");
	if (!get_vmalloc_info_fn) {
		printk(KERN_ERR "Error: Unknown symbol get_vmalloc_info\n");
		return -EFAULT;
	}
#endif /* HAVE_GET_VMALLOC_INFO */

#ifdef HAVE_PGDAT_HELPERS
# ifndef HAVE_FIRST_ONLINE_PGDAT
	first_online_pgdat_fn = (first_online_pgdat_t)
		spl_kallsyms_lookup_name("first_online_pgdat");
	if (!first_online_pgdat_fn) {
		printk(KERN_ERR "Error: Unknown symbol first_online_pgdat\n");
		return -EFAULT;
	}
# endif /* HAVE_FIRST_ONLINE_PGDAT */

# ifndef HAVE_NEXT_ONLINE_PGDAT
	next_online_pgdat_fn = (next_online_pgdat_t)
		spl_kallsyms_lookup_name("next_online_pgdat");
	if (!next_online_pgdat_fn) {
		printk(KERN_ERR "Error: Unknown symbol next_online_pgdat\n");
		return -EFAULT;
	}
# endif /* HAVE_NEXT_ONLINE_PGDAT */

# ifndef HAVE_NEXT_ZONE
	next_zone_fn = (next_zone_t)
		spl_kallsyms_lookup_name("next_zone");
	if (!next_zone_fn) {
		printk(KERN_ERR "Error: Unknown symbol next_zone\n");
		return -EFAULT;
	}
# endif /* HAVE_NEXT_ZONE */

#else /* HAVE_PGDAT_HELPERS */

# ifndef HAVE_PGDAT_LIST
	pgdat_list_addr = *(struct pglist_data **)
		spl_kallsyms_lookup_name("pgdat_list");
	if (!pgdat_list_addr) {
		printk(KERN_ERR "Error: Unknown symbol pgdat_list\n");
		return -EFAULT;
	}
# endif /* HAVE_PGDAT_LIST */
#endif /* HAVE_PGDAT_HELPERS */

#if defined(NEED_GET_ZONE_COUNTS) && !defined(HAVE_GET_ZONE_COUNTS)
	get_zone_counts_fn = (get_zone_counts_t)
		spl_kallsyms_lookup_name("get_zone_counts");
	if (!get_zone_counts_fn) {
		printk(KERN_ERR "Error: Unknown symbol get_zone_counts\n");
		return -EFAULT;
	}
#endif  /* NEED_GET_ZONE_COUNTS && !HAVE_GET_ZONE_COUNTS */

	/*
	 * It is now safe to initialize the global tunings which rely on
	 * the use of the for_each_zone() macro.  This macro in turns
	 * depends on the *_pgdat symbols which are now available.
	 */
	spl_kmem_init_globals();

#if !defined(HAVE_INVALIDATE_INODES) && !defined(HAVE_INVALIDATE_INODES_CHECK)
	invalidate_inodes_fn = (invalidate_inodes_t)
		spl_kallsyms_lookup_name("invalidate_inodes");
	if (!invalidate_inodes_fn) {
		printk(KERN_ERR "Error: Unknown symbol invalidate_inodes\n");
		return -EFAULT;
	}
#endif /* !HAVE_INVALIDATE_INODES && !HAVE_INVALIDATE_INODES_CHECK */

#ifndef HAVE_SHRINK_DCACHE_MEMORY
	/* When shrink_dcache_memory_fn == NULL support is disabled */
	shrink_dcache_memory_fn = (shrink_dcache_memory_t)
		spl_kallsyms_lookup_name("shrink_dcache_memory");
#endif /* HAVE_SHRINK_DCACHE_MEMORY */

#ifndef HAVE_SHRINK_ICACHE_MEMORY
	/* When shrink_icache_memory_fn == NULL support is disabled */
	shrink_icache_memory_fn = (shrink_icache_memory_t)
		spl_kallsyms_lookup_name("shrink_icache_memory");
#endif /* HAVE_SHRINK_ICACHE_MEMORY */

	return 0;
}

int
spl_kmem_init(void)
{
	int rc = 0;
	SENTRY;

	init_rwsem(&spl_kmem_cache_sem);
	INIT_LIST_HEAD(&spl_kmem_cache_list);

	spl_register_shrinker(&spl_kmem_cache_shrinker);

#ifdef DEBUG_KMEM
	kmem_alloc_used_set(0);
	vmem_alloc_used_set(0);

	spl_kmem_init_tracking(&kmem_list, &kmem_lock, KMEM_TABLE_SIZE);
	spl_kmem_init_tracking(&vmem_list, &vmem_lock, VMEM_TABLE_SIZE);
#endif
	SRETURN(rc);
}

void
spl_kmem_fini(void)
{
#ifdef DEBUG_KMEM
	/* Display all unreclaimed memory addresses, including the
	 * allocation size and the first few bytes of what's located
	 * at that address to aid in debugging.  Performance is not
	 * a serious concern here since it is module unload time. */
	if (kmem_alloc_used_read() != 0)
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING,
		    "kmem leaked %ld/%ld bytes\n",
		    kmem_alloc_used_read(), kmem_alloc_max);


	if (vmem_alloc_used_read() != 0)
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING,
		    "vmem leaked %ld/%ld bytes\n",
		    vmem_alloc_used_read(), vmem_alloc_max);

	spl_kmem_fini_tracking(&kmem_list, &kmem_lock);
	spl_kmem_fini_tracking(&vmem_list, &vmem_lock);
#endif /* DEBUG_KMEM */
	SENTRY;

	spl_unregister_shrinker(&spl_kmem_cache_shrinker);

	SEXIT;
}
