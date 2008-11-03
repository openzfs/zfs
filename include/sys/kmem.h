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

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#ifdef	__cplusplus
extern "C" {
#endif

#undef DEBUG_KMEM_UNIMPLEMENTED
#undef DEBUG_KMEM_TRACKING /* Per-allocation memory tracking */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/hash.h>
#include <linux/ctype.h>
#include <asm/atomic_compat.h>
#include <sys/types.h>
#include <sys/debug.h>

/*
 * Memory allocation interfaces
 */
#define KM_SLEEP                        GFP_KERNEL
#define KM_NOSLEEP                      GFP_ATOMIC
#undef  KM_PANIC                        /* No linux analog */
#define KM_PUSHPAGE			(KM_SLEEP | __GFP_HIGH)
#define KM_VMFLAGS                      GFP_LEVEL_MASK
#define KM_FLAGS                        __GFP_BITS_MASK

/*
 * Used internally, the kernel does not need to support this flag
 */
#ifndef __GFP_ZERO
#define __GFP_ZERO			0x8000
#endif

#ifdef DEBUG_KMEM
extern atomic64_t kmem_alloc_used;
extern unsigned long kmem_alloc_max;
extern atomic64_t vmem_alloc_used;
extern unsigned long vmem_alloc_max;
extern int kmem_warning_flag;

#ifdef DEBUG_KMEM_TRACKING
/* XXX - Not to surprisingly with debugging enabled the xmem_locks are very
 * highly contended particularly on xfree().  If we want to run with this
 * detailed debugging enabled for anything other than debugging  we need to
 * minimize the contention by moving to a lock per xmem_table entry model.
 */
#define KMEM_HASH_BITS          10
#define KMEM_TABLE_SIZE         (1 << KMEM_HASH_BITS)

extern struct hlist_head kmem_table[KMEM_TABLE_SIZE];
extern struct list_head kmem_list;
extern spinlock_t kmem_lock;

#define VMEM_HASH_BITS          10
#define VMEM_TABLE_SIZE         (1 << VMEM_HASH_BITS)

extern struct hlist_head vmem_table[VMEM_TABLE_SIZE];
extern struct list_head vmem_list;
extern spinlock_t vmem_lock;

typedef struct kmem_debug {
        struct hlist_node kd_hlist;     /* Hash node linkage */
        struct list_head kd_list;       /* List of all allocations */
        void *kd_addr;                  /* Allocation pointer */
        size_t kd_size;                 /* Allocation size */
        const char *kd_func;            /* Allocation function */
        int kd_line;                    /* Allocation line */
} kmem_debug_t;

static __inline__ kmem_debug_t *
__kmem_del_init(spinlock_t *lock,struct hlist_head *table,int bits,void *addr)
{
        struct hlist_head *head;
        struct hlist_node *node;
        struct kmem_debug *p;
        unsigned long flags;

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
        return NULL;
}

#define __kmem_alloc(size, flags, allocator, args...)                         \
({      void *_ptr_ = NULL;                                                   \
        kmem_debug_t *_dptr_;                                                 \
        unsigned long _flags_;                                                \
                                                                              \
        _dptr_ = (kmem_debug_t *)kmalloc(sizeof(kmem_debug_t), (flags));      \
        if (_dptr_ == NULL) {                                                 \
                __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "                  \
			       "kmem_alloc(%d, 0x%x) debug failed\n",         \
			       sizeof(kmem_debug_t), (int)(flags));           \
        } else {                                                              \
		/* Marked unlikely because we should never be doing this, */  \
		/* we tolerate to up 2 pages but a single page is best.   */  \
                if (unlikely((size) > (PAGE_SIZE * 2)) && kmem_warning_flag)  \
                        __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning large "    \
				       "kmem_alloc(%d, 0x%x) (%ld/%ld)\n",    \
				       (int)(size), (int)(flags),             \
			               atomic64_read(&kmem_alloc_used),       \
				       kmem_alloc_max);                       \
                                                                              \
                _ptr_ = (void *)allocator((size), (flags), ## args);          \
                if (_ptr_ == NULL) {                                          \
                        kfree(_dptr_);                                        \
                        __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "          \
				       "kmem_alloc(%d, 0x%x) failed (%ld/"    \
                                       "%ld)\n", (int)(size), (int)(flags),   \
			               atomic64_read(&kmem_alloc_used),       \
				       kmem_alloc_max);                       \
                } else {                                                      \
                        atomic64_add((size), &kmem_alloc_used);               \
                        if (unlikely(atomic64_read(&kmem_alloc_used) >        \
                            kmem_alloc_max))                                  \
                                kmem_alloc_max =                              \
                                        atomic64_read(&kmem_alloc_used);      \
				                                              \
                        INIT_HLIST_NODE(&_dptr_->kd_hlist);                   \
                        INIT_LIST_HEAD(&_dptr_->kd_list);                     \
                        _dptr_->kd_addr = _ptr_;                              \
                        _dptr_->kd_size = (size);                             \
                        _dptr_->kd_func = __FUNCTION__;                       \
                        _dptr_->kd_line = __LINE__;                           \
                        spin_lock_irqsave(&kmem_lock, _flags_);               \
                        hlist_add_head_rcu(&_dptr_->kd_hlist,                 \
                                &kmem_table[hash_ptr(_ptr_, KMEM_HASH_BITS)]);\
                        list_add_tail(&_dptr_->kd_list, &kmem_list);          \
                        spin_unlock_irqrestore(&kmem_lock, _flags_);          \
                                                                              \
                        __CDEBUG_LIMIT(S_KMEM, D_INFO, "kmem_alloc("          \
                                       "%d, 0x%x) = %p (%ld/%ld)\n",          \
                                       (int)(size), (int)(flags), _ptr_,      \
                                       atomic64_read(&kmem_alloc_used),       \
				       kmem_alloc_max);                       \
                }                                                             \
        }                                                                     \
                                                                              \
        _ptr_;                                                                \
})

#define kmem_free(ptr, size)                                                  \
({                                                                            \
        kmem_debug_t *_dptr_;                                                 \
        ASSERT((ptr) || (size > 0));                                          \
                                                                              \
        _dptr_ = __kmem_del_init(&kmem_lock, kmem_table, KMEM_HASH_BITS, ptr);\
        ASSERT(_dptr_); /* Must exist in hash due to kmem_alloc() */          \
        ASSERTF(_dptr_->kd_size == (size), "kd_size (%d) != size (%d), "      \
                "kd_func = %s, kd_line = %d\n", _dptr_->kd_size, (size),      \
                _dptr_->kd_func, _dptr_->kd_line); /* Size must match */      \
        atomic64_sub((size), &kmem_alloc_used);                               \
        __CDEBUG_LIMIT(S_KMEM, D_INFO, "kmem_free(%p, %d) (%ld/%ld)\n",       \
		       (ptr), (int)(size), atomic64_read(&kmem_alloc_used),   \
		       kmem_alloc_max);                                       \
                                                                              \
        memset(_dptr_, 0x5a, sizeof(kmem_debug_t));                           \
        kfree(_dptr_);                                                        \
                                                                              \
        memset(ptr, 0x5a, (size));                                            \
        kfree(ptr);                                                           \
})

#define __vmem_alloc(size, flags)                                             \
({      void *_ptr_ = NULL;                                                   \
        kmem_debug_t *_dptr_;                                                 \
        unsigned long _flags_;                                                \
                                                                              \
	ASSERT((flags) & KM_SLEEP);                                           \
                                                                              \
        _dptr_ = (kmem_debug_t *)kmalloc(sizeof(kmem_debug_t), (flags));      \
        if (_dptr_ == NULL) {                                                 \
                __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "                  \
                               "vmem_alloc(%d, 0x%x) debug failed\n",         \
                               sizeof(kmem_debug_t), (int)(flags));           \
        } else {                                                              \
                _ptr_ = (void *)__vmalloc((size), (((flags) |                 \
                                          __GFP_HIGHMEM) & ~__GFP_ZERO),      \
					  PAGE_KERNEL);                       \
                if (_ptr_ == NULL) {                                          \
                        kfree(_dptr_);                                        \
                        __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "          \
				       "vmem_alloc(%d, 0x%x) failed (%ld/"    \
                                       "%ld)\n", (int)(size), (int)(flags),   \
			              atomic64_read(&vmem_alloc_used),        \
				      vmem_alloc_max);                        \
                } else {                                                      \
                        if (flags & __GFP_ZERO)                               \
                                memset(_ptr_, 0, (size));                     \
                                                                              \
                        atomic64_add((size), &vmem_alloc_used);               \
                        if (unlikely(atomic64_read(&vmem_alloc_used) >        \
                            vmem_alloc_max))                                  \
                                vmem_alloc_max =                              \
                                        atomic64_read(&vmem_alloc_used);      \
				                                              \
                        INIT_HLIST_NODE(&_dptr_->kd_hlist);                   \
                        INIT_LIST_HEAD(&_dptr_->kd_list);                     \
                        _dptr_->kd_addr = _ptr_;                              \
                        _dptr_->kd_size = (size);                             \
                        _dptr_->kd_func = __FUNCTION__;                       \
                        _dptr_->kd_line = __LINE__;                           \
                        spin_lock_irqsave(&vmem_lock, _flags_);               \
                        hlist_add_head_rcu(&_dptr_->kd_hlist,                 \
                                &vmem_table[hash_ptr(_ptr_, VMEM_HASH_BITS)]);\
                        list_add_tail(&_dptr_->kd_list, &vmem_list);          \
                        spin_unlock_irqrestore(&vmem_lock, _flags_);          \
                                                                              \
                        __CDEBUG_LIMIT(S_KMEM, D_INFO, "vmem_alloc("          \
                                       "%d, 0x%x) = %p (%ld/%ld)\n",          \
                                       (int)(size), (int)(flags), _ptr_,      \
                                       atomic64_read(&vmem_alloc_used),       \
				       vmem_alloc_max);                       \
                }                                                             \
        }                                                                     \
                                                                              \
        _ptr_;                                                                \
})

#define vmem_free(ptr, size)                                                  \
({                                                                            \
        kmem_debug_t *_dptr_;                                                 \
        ASSERT((ptr) || (size > 0));                                          \
                                                                              \
        _dptr_ = __kmem_del_init(&vmem_lock, vmem_table, VMEM_HASH_BITS, ptr);\
        ASSERT(_dptr_); /* Must exist in hash due to vmem_alloc() */          \
        ASSERTF(_dptr_->kd_size == (size), "kd_size (%d) != size (%d), "      \
                "kd_func = %s, kd_line = %d\n", _dptr_->kd_size, (size),      \
                _dptr_->kd_func, _dptr_->kd_line); /* Size must match */      \
        atomic64_sub((size), &vmem_alloc_used);                               \
        __CDEBUG_LIMIT(S_KMEM, D_INFO, "vmem_free(%p, %d) (%ld/%ld)\n",       \
		       (ptr), (int)(size), atomic64_read(&vmem_alloc_used),   \
		       vmem_alloc_max);                                       \
                                                                              \
        memset(_dptr_, 0x5a, sizeof(kmem_debug_t));                           \
        kfree(_dptr_);                                                        \
                                                                              \
        memset(ptr, 0x5a, (size));                                            \
        vfree(ptr);                                                           \
})

#else /* DEBUG_KMEM_TRACKING */

#define __kmem_alloc(size, flags, allocator, args...)                         \
({      void *_ptr_ = NULL;                                                   \
                                                                              \
	/* Marked unlikely because we should never be doing this, */          \
	/* we tolerate to up 2 pages but a single page is best.   */          \
        if (unlikely((size) > (PAGE_SIZE * 2)) && kmem_warning_flag)          \
                __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning large "            \
			       "kmem_alloc(%d, 0x%x) (%ld/%ld)\n",            \
			       (int)(size), (int)(flags),                     \
		               atomic64_read(&kmem_alloc_used),               \
			       kmem_alloc_max);                               \
                                                                              \
        _ptr_ = (void *)allocator((size), (flags), ## args);                  \
        if (_ptr_ == NULL) {                                                  \
                __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "                  \
			       "kmem_alloc(%d, 0x%x) failed (%ld/"            \
                               "%ld)\n", (int)(size), (int)(flags),           \
		               atomic64_read(&kmem_alloc_used),               \
			       kmem_alloc_max);                               \
        } else {                                                              \
                atomic64_add((size), &kmem_alloc_used);                       \
                if (unlikely(atomic64_read(&kmem_alloc_used) >                \
                    kmem_alloc_max))                                          \
                        kmem_alloc_max =                                      \
                                atomic64_read(&kmem_alloc_used);              \
			                                                      \
                __CDEBUG_LIMIT(S_KMEM, D_INFO, "kmem_alloc(%d, 0x%x) = %p "   \
			       "(%ld/%ld)\n", (int)(size), (int)(flags),      \
			       _ptr_, atomic64_read(&kmem_alloc_used),        \
			       kmem_alloc_max);                               \
        }                                                                     \
                                                                              \
        _ptr_;                                                                \
})

#define kmem_free(ptr, size)                                                  \
({                                                                            \
        ASSERT((ptr) || (size > 0));                                          \
                                                                              \
        atomic64_sub((size), &kmem_alloc_used);                               \
        __CDEBUG_LIMIT(S_KMEM, D_INFO, "kmem_free(%p, %d) (%ld/%ld)\n",       \
		       (ptr), (int)(size), atomic64_read(&kmem_alloc_used),   \
		       kmem_alloc_max);                                       \
        memset(ptr, 0x5a, (size));                                            \
        kfree(ptr);                                                           \
})

#define __vmem_alloc(size, flags)                                             \
({      void *_ptr_ = NULL;                                                   \
                                                                              \
	ASSERT((flags) & KM_SLEEP);                                           \
                                                                              \
        _ptr_ = (void *)__vmalloc((size), (((flags) |                         \
                                  __GFP_HIGHMEM) & ~__GFP_ZERO), PAGE_KERNEL);\
        if (_ptr_ == NULL) {                                                  \
                __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "                  \
			       "vmem_alloc(%d, 0x%x) failed (%ld/"            \
                               "%ld)\n", (int)(size), (int)(flags),           \
		              atomic64_read(&vmem_alloc_used),                \
			      vmem_alloc_max);                                \
        } else {                                                              \
                if (flags & __GFP_ZERO)                                       \
                        memset(_ptr_, 0, (size));                             \
                                                                              \
                atomic64_add((size), &vmem_alloc_used);                       \
                if (unlikely(atomic64_read(&vmem_alloc_used) >                \
                    vmem_alloc_max))                                          \
                        vmem_alloc_max =                                      \
                                atomic64_read(&vmem_alloc_used);              \
			                                                      \
                __CDEBUG_LIMIT(S_KMEM, D_INFO, "vmem_alloc("                  \
                               "%d, 0x%x) = %p (%ld/%ld)\n",                  \
                               (int)(size), (int)(flags), _ptr_,              \
                               atomic64_read(&vmem_alloc_used),               \
			       vmem_alloc_max);                               \
        }                                                                     \
                                                                              \
	_ptr_;                                                                \
})

#define vmem_free(ptr, size)                                                  \
({                                                                            \
        ASSERT((ptr) || (size > 0));                                          \
                                                                              \
        atomic64_sub((size), &vmem_alloc_used);                               \
        __CDEBUG_LIMIT(S_KMEM, D_INFO, "vmem_free(%p, %d) (%ld/%ld)\n",       \
		       (ptr), (int)(size), atomic64_read(&vmem_alloc_used),   \
		       vmem_alloc_max);                                       \
        memset(ptr, 0x5a, (size));                                            \
        vfree(ptr);                                                           \
})

#endif /* DEBUG_KMEM_TRACKING */

#define kmem_alloc(size, flags)         __kmem_alloc((size), (flags), kmalloc)
#define kmem_zalloc(size, flags)        __kmem_alloc((size), (flags), kzalloc)

#ifdef HAVE_KMALLOC_NODE
#define kmem_alloc_node(size, flags, node)                                    \
	__kmem_alloc((size), (flags), kmalloc_node, node)
#else
#define kmem_alloc_node(size, flags, node)                                    \
	__kmem_alloc((size), (flags), kmalloc)
#endif

#define vmem_alloc(size, flags)         __vmem_alloc((size), (flags))
#define vmem_zalloc(size, flags)        __vmem_alloc((size), ((flags) | __GFP_ZERO))

#else /* DEBUG_KMEM */

#define kmem_alloc(size, flags)         kmalloc((size), (flags))
#define kmem_zalloc(size, flags)        kzalloc((size), (flags))
#define kmem_free(ptr, size)            kfree(ptr)

#ifdef HAVE_KMALLOC_NODE
#define kmem_alloc_node(size, flags, node)                                    \
	kmalloc_node((size), (flags), (node))
#else
#define kmem_alloc_node(size, flags, node)                                    \
	kmalloc((size), (flags))
#endif

#define vmem_alloc(size, flags)         __vmalloc((size), ((flags) |          \
					__GFP_HIGHMEM), PAGE_KERNEL)
#define vmem_zalloc(size, flags)                                              \
({                                                                            \
        void *_ptr_ = __vmalloc((size),((flags)|__GFP_HIGHMEM),PAGE_KERNEL);  \
        if (_ptr_)                                                            \
                memset(_ptr_, 0, (size));                                     \
        _ptr_;                                                                \
})
#define vmem_free(ptr, size)            vfree(ptr)

#endif /* DEBUG_KMEM */

#ifdef DEBUG_KMEM_UNIMPLEMENTED
static __inline__ void *
kmem_alloc_tryhard(size_t size, size_t *alloc_size, int kmflags)
{
#error "kmem_alloc_tryhard() not implemented"
}
#endif /* DEBUG_KMEM_UNIMPLEMENTED */

/*
 * Slab allocation interfaces
 */
#define KMC_NOTOUCH                     0x00000001
#define KMC_NODEBUG                     0x00000002 /* Default behavior */
#define KMC_NOMAGAZINE                  0x00000004 /* XXX: No disable support available */
#define KMC_NOHASH                      0x00000008 /* XXX: No hash available */
#define KMC_QCACHE                      0x00000010 /* XXX: Unsupported */
#define KMC_KMEM			0x00000100 /* Use kmem cache */
#define KMC_VMEM			0x00000200 /* Use vmem cache */
#define KMC_OFFSLAB			0x00000400 /* Objects not on slab */

#define KMC_REAP_CHUNK                  256
#define KMC_DEFAULT_SEEKS               DEFAULT_SEEKS

#ifdef DEBUG_KMEM_UNIMPLEMENTED
static __inline__ void kmem_init(void) {
#error "kmem_init() not implemented"
}

static __inline__ void kmem_thread_init(void) {
#error "kmem_thread_init() not implemented"
}

static __inline__ void kmem_mp_init(void) {
#error "kmem_mp_init() not implemented"
}

static __inline__ void kmem_reap_idspace(void) {
#error "kmem_reap_idspace() not implemented"
}

static __inline__ size_t kmem_avail(void) {
#error "kmem_avail() not implemented"
}

static __inline__ size_t kmem_maxavail(void) {
#error "kmem_maxavail() not implemented"
}

static __inline__ uint64_t kmem_cache_stat(spl_kmem_cache_t *cache) {
#error "kmem_cache_stat() not implemented"
}
#endif /* DEBUG_KMEM_UNIMPLEMENTED */

/* XXX - Used by arc.c to adjust its memory footprint. We may want
 *       to use this hook in the future to adjust behavior based on
 *       debug levels.  For now it's safe to always return 0.
 */
static __inline__ int
kmem_debugging(void)
{
        return 0;
}

extern int kmem_set_warning(int flag);

extern struct list_head spl_kmem_cache_list;
extern struct rw_semaphore spl_kmem_cache_sem;

#define SKM_MAGIC			0x2e2e2e2e
#define SKO_MAGIC			0x20202020
#define SKS_MAGIC			0x22222222
#define SKC_MAGIC			0x2c2c2c2c

#define SPL_KMEM_CACHE_DELAY		5
#define SPL_KMEM_CACHE_OBJ_PER_SLAB	32

typedef int (*spl_kmem_ctor_t)(void *, void *, int);
typedef void (*spl_kmem_dtor_t)(void *, void *);
typedef void (*spl_kmem_reclaim_t)(void *);

typedef struct spl_kmem_magazine {
        uint32_t		skm_magic;	/* Sanity magic */
	uint32_t		skm_avail;	/* Available objects */
	uint32_t		skm_size;	/* Magazine size */
	uint32_t		skm_refill;	/* Batch refill size */
	unsigned long		skm_age;	/* Last cache access */
	void			*skm_objs[0];	/* Object pointers */
} spl_kmem_magazine_t;

typedef struct spl_kmem_obj {
        uint32_t		sko_magic;	/* Sanity magic */
	void			*sko_addr;	/* Buffer address */
	struct spl_kmem_slab	*sko_slab;	/* Owned by slab */
	struct list_head	sko_list;	/* Free object list linkage */
} spl_kmem_obj_t;

typedef struct spl_kmem_slab {
        uint32_t		sks_magic;	/* Sanity magic */
	uint32_t		sks_objs;	/* Objects per slab */
	struct spl_kmem_cache	*sks_cache;	/* Owned by cache */
	struct list_head	sks_list;	/* Slab list linkage */
	struct list_head	sks_free_list;	/* Free object list */
	unsigned long		sks_age;	/* Last modify jiffie */
	uint32_t		sks_ref;	/* Ref count used objects */
} spl_kmem_slab_t;

typedef struct spl_kmem_cache {
        uint32_t		skc_magic;	/* Sanity magic */
        uint32_t		skc_name_size;	/* Name length */
        char			*skc_name;	/* Name string */
	spl_kmem_magazine_t	*skc_mag[NR_CPUS]; /* Per-CPU warm cache */
	uint32_t		skc_mag_size;	/* Magazine size */
	uint32_t		skc_mag_refill;	/* Magazine refill count */
        spl_kmem_ctor_t		skc_ctor;	/* Constructor */
        spl_kmem_dtor_t		skc_dtor;	/* Destructor */
        spl_kmem_reclaim_t      skc_reclaim;	/* Reclaimator */
        void			*skc_private;	/* Private data */
        void			*skc_vmp;	/* Unused */
	uint32_t		skc_flags;	/* Flags */
	uint32_t		skc_obj_size;	/* Object size */
	uint32_t		skc_slab_objs;	/* Objects per slab */
	uint32_t		skc_slab_size;  /* Slab size */
	uint32_t		skc_delay;	/* slab reclaim interval */
        struct list_head	skc_list;	/* List of caches linkage */
	struct list_head	skc_complete_list;/* Completely alloc'ed */
	struct list_head	skc_partial_list; /* Partially alloc'ed */
	spinlock_t		skc_lock;	/* Cache lock */
	uint64_t		skc_slab_fail;	/* Slab alloc failures */
	uint64_t		skc_slab_create;/* Slab creates */
	uint64_t		skc_slab_destroy;/* Slab destroys */
	uint64_t		skc_slab_total;	/* Slab total current */
	uint64_t		skc_slab_alloc; /* Slab alloc current */
	uint64_t		skc_slab_max;	/* Slab max historic  */
	uint64_t		skc_obj_total;	/* Obj total current */
	uint64_t		skc_obj_alloc;	/* Obj alloc current */
	uint64_t		skc_obj_max;	/* Obj max historic */
} spl_kmem_cache_t;
#define kmem_cache_t		spl_kmem_cache_t

extern spl_kmem_cache_t *
spl_kmem_cache_create(char *name, size_t size, size_t align,
        spl_kmem_ctor_t ctor, spl_kmem_dtor_t dtor, spl_kmem_reclaim_t reclaim,
        void *priv, void *vmp, int flags);

extern void spl_kmem_cache_destroy(spl_kmem_cache_t *skc);
extern void *spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags);
extern void spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj);
extern void spl_kmem_cache_reap_now(spl_kmem_cache_t *skc);
extern void spl_kmem_reap(void);

int spl_kmem_init(void);
void spl_kmem_fini(void);

#define kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags) \
        spl_kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags)
#define kmem_cache_destroy(skc)		spl_kmem_cache_destroy(skc)
#define kmem_cache_alloc(skc, flags)	spl_kmem_cache_alloc(skc, flags)
#define kmem_cache_free(skc, obj)	spl_kmem_cache_free(skc, obj)
#define kmem_cache_reap_now(skc)	spl_kmem_cache_reap_now(skc)
#define kmem_reap()			spl_kmem_reap()
#define kmem_virt(ptr)			(((ptr) >= (void *)VMALLOC_START) && \
					 ((ptr) <  (void *)VMALLOC_END))

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_KMEM_H */
