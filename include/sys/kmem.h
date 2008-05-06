#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#ifdef	__cplusplus
extern "C" {
#endif

#define DEBUG_KMEM
#undef DEBUG_KMEM_UNIMPLEMENTED

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/hash.h>
#include <linux/ctype.h>
#include <sys/debug.h>
/*
 * Memory allocation interfaces
 */
#define KM_SLEEP                        GFP_KERNEL
#define KM_NOSLEEP                      GFP_ATOMIC
#undef  KM_PANIC                        /* No linux analog */
#define KM_PUSHPAGE			(GFP_KERNEL | GFP_HIGH)
#define KM_VMFLAGS                      GFP_LEVEL_MASK
#define KM_FLAGS                        __GFP_BITS_MASK

#ifdef DEBUG_KMEM
extern atomic64_t kmem_alloc_used;
extern unsigned long kmem_alloc_max;
extern atomic64_t vmem_alloc_used;
extern unsigned long vmem_alloc_max;
extern int kmem_warning_flag;

#define KMEM_HASH_BITS          10
#define KMEM_TABLE_SIZE         (1 << KMEM_HASH_BITS)

extern struct hlist_head kmem_table[KMEM_TABLE_SIZE];
extern struct list_head kmem_list;
extern spinlock_t kmem_lock;

typedef struct kmem_debug {
        struct hlist_node kd_hlist;     /* Hash node linkage */
        struct list_head kd_list;       /* List of all allocations */
        void *kd_addr;                  /* Allocation pointer */
        size_t kd_size;                 /* Allocation size */
        const char *kd_func;            /* Allocation function */
        int kd_line;                    /* Allocation line */
} kmem_debug_t;

static __inline__ kmem_debug_t *
__kmem_del_init(void *addr)
{
        struct hlist_head *head;
        struct hlist_node *node;
        struct kmem_debug *p;
        unsigned long flags;

        spin_lock_irqsave(&kmem_lock, flags);
        head = &kmem_table[hash_ptr(addr, KMEM_HASH_BITS)];
        hlist_for_each_entry_rcu(p, node, head, kd_hlist) {
                if (p->kd_addr == addr) {
                        hlist_del_init(&p->kd_hlist);
                        list_del_init(&p->kd_list);
                        spin_unlock_irqrestore(&kmem_lock, flags);
                        return p;
                }
        }

        spin_unlock_irqrestore(&kmem_lock, flags);
        return NULL;
}

#define __kmem_alloc(size, flags, allocator)                                  \
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
		/* Marked unlikely because we should never be doing this */   \
                if (unlikely((size) > (PAGE_SIZE * 4)) && kmem_warning_flag)  \
                        __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning large "    \
				       "kmem_alloc(%d, 0x%x) (%ld/%ld)\n",    \
				       (int)(size), (int)(flags),             \
			               atomic64_read(&kmem_alloc_used),       \
				       kmem_alloc_max);                       \
                                                                              \
                _ptr_ = (void *)allocator((size), (flags));                   \
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

#define kmem_alloc(size, flags)         __kmem_alloc((size), (flags), kmalloc)
#define kmem_zalloc(size, flags)        __kmem_alloc((size), (flags), kzalloc)

#define kmem_free(ptr, size)                                                  \
({                                                                            \
        kmem_debug_t *_dptr_;                                                 \
        ASSERT((ptr) || (size > 0));                                          \
                                                                              \
        _dptr_ = __kmem_del_init(ptr);                                        \
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
({      void *_ptr_;                                                          \
                                                                              \
	ASSERT(flags & KM_SLEEP);                                             \
                                                                              \
        _ptr_ = (void *)__vmalloc((size),                                     \
				  (((flags) | __GFP_HIGHMEM) & ~__GFP_ZERO),  \
				  PAGE_KERNEL);                               \
        if (_ptr_ == NULL) {                                                  \
                __CDEBUG_LIMIT(S_KMEM, D_WARNING, "Warning "                  \
			       "vmem_alloc(%d, 0x%x) failed (%ld/%ld)\n",     \
			       (int)(size), (int)(flags),                     \
		              atomic64_read(&vmem_alloc_used),                \
			      vmem_alloc_max);                                \
        } else {                                                              \
                if (flags & __GFP_ZERO)                                       \
                        memset(_ptr_, 0, (size));                             \
                                                                              \
                atomic64_add((size), &vmem_alloc_used);                       \
                if (unlikely(atomic64_read(&vmem_alloc_used)>vmem_alloc_max)) \
                        vmem_alloc_max = atomic64_read(&vmem_alloc_used);     \
                                                                              \
                __CDEBUG_LIMIT(S_KMEM, D_INFO, "vmem_alloc(%d, 0x%x) = %p "   \
			       "(%ld/%ld)\n", (int)(size), (int)(flags),      \
		               _ptr_, atomic64_read(&vmem_alloc_used),        \
			       vmem_alloc_max);                               \
        }                                                                     \
                                                                              \
        _ptr_;                                                                \
})

#define vmem_alloc(size, flags)         __vmem_alloc((size), (flags))
#define vmem_zalloc(size, flags)        __vmem_alloc((size), ((flags) |       \
                                                     __GFP_ZERO))

#define vmem_free(ptr, size)                                                  \
({                                                                            \
        ASSERT((ptr) || (size > 0));                                          \
        atomic64_sub((size), &vmem_alloc_used);                               \
        __CDEBUG_LIMIT(S_KMEM, D_INFO, "vmem_free(%p, %d) (%ld/%ld)\n",       \
		       (ptr), (int)(size), atomic64_read(&vmem_alloc_used),   \
		       vmem_alloc_max);                                       \
        memset(ptr, 0x5a, (size)); /* Poison */                               \
        vfree(ptr);                                                           \
})

#else

#define kmem_alloc(size, flags)         kmalloc((size), (flags))
#define kmem_zalloc(size, flags)        kzalloc((size), (flags))
#define kmem_free(ptr, size)                                                  \
({                                                                            \
        ASSERT((ptr) || (size > 0));                                          \
	kfree(ptr);                                                           \
})

#define vmem_alloc(size, flags)         __vmalloc((size), ((flags) |          \
						  __GFP_HIGHMEM), PAGE_KERNEL)
#define vmem_zalloc(size, flags)        __vmalloc((size), ((flags) |          \
						  __GFP_HIGHMEM | __GFP_ZERO) \
						  PAGE_KERNEL)
#define vmem_free(ptr, size)                                                  \
({                                                                            \
        ASSERT((ptr) || (size > 0));                                          \
	vfree(ptr);                                                           \
})

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
#undef  KMC_NOTOUCH                     /* No linux analog */
#define KMC_NODEBUG                     0x00000000 /* Default behavior */
#define KMC_NOMAGAZINE                  /* No linux analog */
#define KMC_NOHASH                      /* No linux analog */
#define KMC_QCACHE                      /* No linux analog */

#define KMC_REAP_CHUNK                  256
#define KMC_DEFAULT_SEEKS               DEFAULT_SEEKS

/* Defined by linux slab.h
 * typedef struct kmem_cache_s kmem_cache_t;
 */

/* No linux analog
 * extern int kmem_ready;
 * extern pgcnt_t kmem_reapahead;
 */

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

static __inline__ uint64_t kmem_cache_stat(kmem_cache_t *cache) {
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

typedef int (*kmem_constructor_t)(void *, void *, int);
typedef void (*kmem_destructor_t)(void *, void *);
typedef void (*kmem_reclaim_t)(void *);

extern int kmem_set_warning(int flag);

extern kmem_cache_t *
__kmem_cache_create(char *name, size_t size, size_t align,
        kmem_constructor_t constructor,
        kmem_destructor_t destructor,
        kmem_reclaim_t reclaim,
        void *priv, void *vmp, int flags);

int
extern __kmem_cache_destroy(kmem_cache_t *cache);

void
extern __kmem_reap(void);

int kmem_init(void);
void kmem_fini(void);

#define kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags) \
        __kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags)
#define kmem_cache_destroy(cache)       __kmem_cache_destroy(cache)
#define kmem_cache_alloc(cache, flags)  kmem_cache_alloc(cache, flags)
#define kmem_cache_free(cache, ptr)     kmem_cache_free(cache, ptr)
#define kmem_cache_reap_now(cache)      kmem_cache_shrink(cache)
#define kmem_reap()                     __kmem_reap()

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_KMEM_H */
