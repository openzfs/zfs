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

#define __kmem_alloc(size, flags, allocator)                                  \
({      void *_ptr_;                                                          \
                                                                              \
	/* Marked unlikely because we should never be doing this */           \
        if (unlikely((size) > (PAGE_SIZE * 2)) && kmem_warning_flag)          \
                printk("spl: Warning kmem_alloc(%d, 0x%x) large alloc at %s:%d "  \
                       "(%ld/%ld)\n", (int)(size), (int)(flags),              \
		       __FILE__, __LINE__,                                    \
		       atomic64_read(&kmem_alloc_used), kmem_alloc_max);      \
                                                                              \
        _ptr_ = (void *)allocator((size), (flags));                           \
        if (_ptr_ == NULL) {                                                  \
                printk("spl: Warning kmem_alloc(%d, 0x%x) failed at %s:%d "       \
		       "(%ld/%ld)\n", (int)(size), (int)(flags),              \
		       __FILE__, __LINE__,                                    \
		       atomic64_read(&kmem_alloc_used), kmem_alloc_max);      \
        } else {                                                              \
                atomic64_add((size), &kmem_alloc_used);                       \
                if (unlikely(atomic64_read(&kmem_alloc_used)>kmem_alloc_max)) \
                        kmem_alloc_max = atomic64_read(&kmem_alloc_used);     \
        }                                                                     \
                                                                              \
        _ptr_;                                                                \
})

#define kmem_alloc(size, flags)         __kmem_alloc(size, flags, kmalloc)
#define kmem_zalloc(size, flags)        __kmem_alloc(size, flags, kzalloc)

#define kmem_free(ptr, size)                                                  \
({                                                                            \
        BUG_ON(!(ptr) || (size) < 0);                                         \
        atomic64_sub((size), &kmem_alloc_used);                               \
        memset(ptr, 0x5a, (size)); /* Poison */                               \
        kfree(ptr);                                                           \
})

#define __vmem_alloc(size, flags)                                             \
({      void *_ptr_;                                                          \
                                                                              \
	BUG_ON(flags != KM_SLEEP);                                            \
                                                                              \
        _ptr_ = (void *)vmalloc((size));                                      \
        if (_ptr_ == NULL) {                                                  \
                printk("spl: Warning vmem_alloc(%d, 0x%x) failed at %s:%d "       \
		       "(%ld/%ld)\n", (int)(size), (int)(flags),              \
		       __FILE__, __LINE__,                                    \
		       atomic64_read(&vmem_alloc_used), vmem_alloc_max);      \
        } else {                                                              \
                atomic64_add((size), &vmem_alloc_used);                       \
                if (unlikely(atomic64_read(&vmem_alloc_used)>vmem_alloc_max)) \
                        vmem_alloc_max = atomic64_read(&vmem_alloc_used);     \
        }                                                                     \
                                                                              \
        _ptr_;                                                                \
})

#define vmem_alloc(size, flags)         __vmem_alloc(size, flags)

#define vmem_free(ptr, size)                                                  \
({                                                                            \
        BUG_ON(!(ptr) || (size) < 0);                                         \
        atomic64_sub((size), &vmem_alloc_used);                               \
        memset(ptr, 0x5a, (size)); /* Poison */                               \
        vfree(ptr);                                                           \
})

#else

#define kmem_alloc(size, flags)         kmalloc(size, flags)
#define kmem_zalloc(size, flags)        kzalloc(size, flags)
#define kmem_free(ptr, size)                                                  \
({                                                                            \
	BUG_ON(!(ptr) || (size) < 0);                                         \
	kfree(ptr);                                                           \
})

#define vmem_alloc(size, flags)         vmalloc(size)
#define vmem_free(ptr, size)                                                  \
({                                                                            \
	BUG_ON(!(ptr) || (size) < 0);                                         \
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
