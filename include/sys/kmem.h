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
#define KM_SLEEP                        (GFP_KERNEL | __GFP_NOFAIL)
#define KM_NOSLEEP                      GFP_ATOMIC
#undef  KM_PANIC                        /* No linux analog */
#define KM_PUSHPAGE                     (KM_SLEEP | __GFP_HIGH)
#define KM_VMFLAGS                      GFP_LEVEL_MASK
#define KM_FLAGS                        __GFP_BITS_MASK

/*
 * Used internally, the kernel does not need to support this flag
 */
#ifndef __GFP_ZERO
# define __GFP_ZERO                     0x8000
#endif

#ifdef DEBUG_KMEM

extern atomic64_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;
extern atomic64_t vmem_alloc_used;
extern unsigned long long vmem_alloc_max;

# define kmem_alloc(size, flags)             __kmem_alloc((size), (flags), 0, 0)
# define kmem_zalloc(size, flags)            __kmem_alloc((size), ((flags) |  \
                                                 __GFP_ZERO), 0, 0)

/* The node alloc functions are only used by the SPL code itself */
# ifdef HAVE_KMALLOC_NODE
#  define kmem_alloc_node(size, flags, node) __kmem_alloc((size), (flags), 1, \
                                                 node)
# else
#  define kmem_alloc_node(size, flags, node) __kmem_alloc((size), (flags), 0, 0)
# endif

# define vmem_zalloc(size, flags)            vmem_alloc((size), ((flags) |    \
                                                 __GFP_ZERO))

# ifdef DEBUG_KMEM_TRACKING

extern void *kmem_alloc_track(size_t size, int flags, const char *func,
    int line, int node_alloc, int node);
extern void kmem_free_track(void *ptr, size_t size);
extern void *vmem_alloc_track(size_t size, int flags, const char *func,
    int line);
extern void vmem_free_track(void *ptr, size_t size);

#  define __kmem_alloc(size, flags, na, node) kmem_alloc_track((size),        \
                                                  (flags), __FUNCTION__,      \
                                                  __LINE__, (na), (node))
#  define kmem_free(ptr, size)                kmem_free_track((ptr), (size))
#  define vmem_alloc(size, flags)             vmem_alloc_track((size),        \
                                                  (flags),__FUNCTION__,       \
                                                  __LINE__)
#  define vmem_free(ptr, size)                vmem_free_track((ptr), (size))

# else /* DEBUG_KMEM_TRACKING */

extern void *kmem_alloc_debug(size_t size, int flags, const char *func,
    int line, int node_alloc, int node);
extern void kmem_free_debug(void *ptr, size_t size);
extern void *vmem_alloc_debug(size_t size, int flags, const char *func,
    int line);
extern void vmem_free_debug(void *ptr, size_t size);

#  define __kmem_alloc(size, flags, na, node) kmem_alloc_debug((size),        \
                                                  (flags), __FUNCTION__,      \
                                                  __LINE__, (na), (node))
#  define kmem_free(ptr, size)                kmem_free_debug((ptr), (size))
#  define vmem_alloc(size, flags)             vmem_alloc_debug((size),        \
                                                  (flags), __FUNCTION__,      \
                                                  __LINE__)
#  define vmem_free(ptr, size)                vmem_free_debug((ptr), (size))

# endif /* DEBUG_KMEM_TRACKING */

#else /* DEBUG_KMEM */

# define kmem_alloc(size, flags)        kmalloc((size), (flags))
# define kmem_zalloc(size, flags)       kzalloc((size), (flags))
# define kmem_free(ptr, size)           (kfree(ptr), (void)(size))

# ifdef HAVE_KMALLOC_NODE
#  define kmem_alloc_node(size, flags, node)                                  \
          kmalloc_node((size), (flags), (node))
# else
#  define kmem_alloc_node(size, flags, node)                                  \
          kmalloc((size), (flags))
# endif

# define vmem_alloc(size, flags)        __vmalloc((size), ((flags) |          \
                                            __GFP_HIGHMEM), PAGE_KERNEL)
# define vmem_zalloc(size, flags)                                             \
({                                                                            \
        void *_ptr_ = __vmalloc((size),((flags)|__GFP_HIGHMEM),PAGE_KERNEL);  \
        if (_ptr_)                                                            \
                memset(_ptr_, 0, (size));                                     \
        _ptr_;                                                                \
})
# define vmem_free(ptr, size)           (vfree(ptr), (void)(size))

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
#define SPL_KMEM_CACHE_ALIGN		8

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
	uint32_t		skc_obj_align;	/* Object alignment */
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
