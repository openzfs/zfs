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
\*****************************************************************************/

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm_compat.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/hash.h>
#include <linux/ctype.h>
#include <asm/atomic.h>
#include <sys/types.h>
#include <sys/vmsystm.h>
#include <sys/kstat.h>

/*
 * Memory allocation interfaces
 */
#define KM_SLEEP	GFP_KERNEL	/* Can sleep, never fails */
#define KM_NOSLEEP	GFP_ATOMIC	/* Can not sleep, may fail */
#define KM_PUSHPAGE	(GFP_NOIO | __GFP_HIGH)	/* Use reserved memory */
#define KM_NODEBUG	__GFP_NOWARN	/* Suppress warnings */
#define KM_FLAGS	__GFP_BITS_MASK
#define KM_VMFLAGS	GFP_LEVEL_MASK

/*
 * Used internally, the kernel does not need to support this flag
 */
#ifndef __GFP_ZERO
# define __GFP_ZERO                     0x8000
#endif

/*
 * PF_NOFS is a per-process debug flag which is set in current->flags to
 * detect when a process is performing an unsafe allocation.  All tasks
 * with PF_NOFS set must strictly use KM_PUSHPAGE for allocations because
 * if they enter direct reclaim and initiate I/O the may deadlock.
 *
 * When debugging is disabled, any incorrect usage will be detected and
 * a call stack with warning will be printed to the console.  The flags
 * will then be automatically corrected to allow for safe execution.  If
 * debugging is enabled this will be treated as a fatal condition.
 *
 * To avoid any risk of conflicting with the existing PF_ flags.  The
 * PF_NOFS bit shadows the rarely used PF_MUTEX_TESTER bit.  Only when
 * CONFIG_RT_MUTEX_TESTER is not set, and we know this bit is unused,
 * will the PF_NOFS bit be valid.  Happily, most existing distributions
 * ship a kernel with CONFIG_RT_MUTEX_TESTER disabled.
 */
#if !defined(CONFIG_RT_MUTEX_TESTER) && defined(PF_MUTEX_TESTER)
# define PF_NOFS			PF_MUTEX_TESTER

static inline void
sanitize_flags(struct task_struct *p, gfp_t *flags)
{
	if (unlikely((p->flags & PF_NOFS) && (*flags & (__GFP_IO|__GFP_FS)))) {
# ifdef NDEBUG
		SDEBUG_LIMIT(SD_CONSOLE | SD_WARNING, "Fixing allocation for "
		   "task %s (%d) which used GFP flags 0x%x with PF_NOFS set\n",
		    p->comm, p->pid, flags);
		spl_debug_dumpstack(p);
		*flags &= ~(__GFP_IO|__GFP_FS);
# else
		PANIC("FATAL allocation for task %s (%d) which used GFP "
		    "flags 0x%x with PF_NOFS set\n", p->comm, p->pid, flags);
# endif /* NDEBUG */
	}
}
#else
# define PF_NOFS			0x00000000
# define sanitize_flags(p, fl)		((void)0)
#endif /* !defined(CONFIG_RT_MUTEX_TESTER) && defined(PF_MUTEX_TESTER) */

/*
 * __GFP_NOFAIL looks like it will be removed from the kernel perhaps as
 * early as 2.6.32.  To avoid this issue when it occurs in upstream kernels
 * we retry the allocation here as long as it is not __GFP_WAIT (GFP_ATOMIC).
 * I would prefer the caller handle the failure case cleanly but we are
 * trying to emulate Solaris and those are not the Solaris semantics.
 */
static inline void *
kmalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	sanitize_flags(current, &flags);

	do {
		ptr = kmalloc(size, flags);
	} while (ptr == NULL && (flags & __GFP_WAIT));

	return ptr;
}

static inline void *
kzalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	sanitize_flags(current, &flags);

	do {
		ptr = kzalloc(size, flags);
	} while (ptr == NULL && (flags & __GFP_WAIT));

	return ptr;
}

static inline void *
kmalloc_node_nofail(size_t size, gfp_t flags, int node)
{
#ifdef HAVE_KMALLOC_NODE
	void *ptr;

	sanitize_flags(current, &flags);

	do {
		ptr = kmalloc_node(size, flags, node);
	} while (ptr == NULL && (flags & __GFP_WAIT));

	return ptr;
#else
	return kmalloc_nofail(size, flags);
#endif /* HAVE_KMALLOC_NODE */
}

static inline void *
vmalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	sanitize_flags(current, &flags);

	/*
	 * Retry failed __vmalloc() allocations once every second.  The
	 * rational for the delay is that the likely failure modes are:
	 *
	 * 1) The system has completely exhausted memory, in which case
	 *    delaying 1 second for the memory reclaim to run is reasonable
	 *    to avoid thrashing the system.
	 * 2) The system has memory but has exhausted the small virtual
	 *    address space available on 32-bit systems.  Retrying the
	 *    allocation immediately will only result in spinning on the
	 *    virtual address space lock.  It is better delay a second and
	 *    hope that another process will free some of the address space.
	 *    But the bottom line is there is not much we can actually do
	 *    since we can never safely return a failure and honor the
	 *    Solaris semantics.
	 */
	while (1) {
		ptr = __vmalloc(size, flags | __GFP_HIGHMEM, PAGE_KERNEL);
		if (unlikely((ptr == NULL) && (flags & __GFP_WAIT))) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
		} else {
			break;
		}
	}

	return ptr;
}

static inline void *
vzalloc_nofail(size_t size, gfp_t flags)
{
	void *ptr;

	ptr = vmalloc_nofail(size, flags);
	if (ptr)
		memset(ptr, 0, (size));

	return ptr;
}

#ifdef DEBUG_KMEM

/*
 * Memory accounting functions to be used only when DEBUG_KMEM is set.
 */
# ifdef HAVE_ATOMIC64_T

# define kmem_alloc_used_add(size)      atomic64_add(size, &kmem_alloc_used)
# define kmem_alloc_used_sub(size)      atomic64_sub(size, &kmem_alloc_used)
# define kmem_alloc_used_read()         atomic64_read(&kmem_alloc_used)
# define kmem_alloc_used_set(size)      atomic64_set(&kmem_alloc_used, size)
# define vmem_alloc_used_add(size)      atomic64_add(size, &vmem_alloc_used)
# define vmem_alloc_used_sub(size)      atomic64_sub(size, &vmem_alloc_used)
# define vmem_alloc_used_read()         atomic64_read(&vmem_alloc_used)
# define vmem_alloc_used_set(size)      atomic64_set(&vmem_alloc_used, size)

extern atomic64_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;
extern atomic64_t vmem_alloc_used;
extern unsigned long long vmem_alloc_max;

# else  /* HAVE_ATOMIC64_T */

# define kmem_alloc_used_add(size)      atomic_add(size, &kmem_alloc_used)
# define kmem_alloc_used_sub(size)      atomic_sub(size, &kmem_alloc_used)
# define kmem_alloc_used_read()         atomic_read(&kmem_alloc_used)
# define kmem_alloc_used_set(size)      atomic_set(&kmem_alloc_used, size)
# define vmem_alloc_used_add(size)      atomic_add(size, &vmem_alloc_used)
# define vmem_alloc_used_sub(size)      atomic_sub(size, &vmem_alloc_used)
# define vmem_alloc_used_read()         atomic_read(&vmem_alloc_used)
# define vmem_alloc_used_set(size)      atomic_set(&vmem_alloc_used, size)

extern atomic_t kmem_alloc_used;
extern unsigned long long kmem_alloc_max;
extern atomic_t vmem_alloc_used;
extern unsigned long long vmem_alloc_max;

# endif /* HAVE_ATOMIC64_T */

# ifdef DEBUG_KMEM_TRACKING
/*
 * DEBUG_KMEM && DEBUG_KMEM_TRACKING
 *
 * The maximum level of memory debugging.  All memory will be accounted
 * for and each allocation will be explicitly tracked.  Any allocation
 * which is leaked will be reported on module unload and the exact location
 * where that memory was allocation will be reported.  This level of memory
 * tracking will have a significant impact on performance and should only
 * be enabled for debugging.  This feature may be enabled by passing
 * --enable-debug-kmem-tracking to configure.
 */
#  define kmem_alloc(sz, fl)            kmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_zalloc(sz, fl)           kmem_alloc_track((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_alloc_node(sz, fl, nd)   kmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 1, nd)
#  define kmem_free(ptr, sz)            kmem_free_track((ptr), (sz))

#  define vmem_alloc(sz, fl)            vmem_alloc_track((sz), (fl),           \
                                             __FUNCTION__, __LINE__)
#  define vmem_zalloc(sz, fl)           vmem_alloc_track((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__)
#  define vmem_free(ptr, sz)            vmem_free_track((ptr), (sz))

extern void *kmem_alloc_track(size_t, int, const char *, int, int, int);
extern void kmem_free_track(const void *, size_t);
extern void *vmem_alloc_track(size_t, int, const char *, int);
extern void vmem_free_track(const void *, size_t);

# else /* DEBUG_KMEM_TRACKING */
/*
 * DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * The default build will set DEBUG_KEM.  This provides basic memory
 * accounting with little to no impact on performance.  When the module
 * is unloaded in any memory was leaked the total number of leaked bytes
 * will be reported on the console.  To disable this basic accounting
 * pass the --disable-debug-kmem option to configure.
 */
#  define kmem_alloc(sz, fl)            kmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_zalloc(sz, fl)           kmem_alloc_debug((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__, 0, 0)
#  define kmem_alloc_node(sz, fl, nd)   kmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__, 1, nd)
#  define kmem_free(ptr, sz)            kmem_free_debug((ptr), (sz))

#  define vmem_alloc(sz, fl)            vmem_alloc_debug((sz), (fl),           \
                                             __FUNCTION__, __LINE__)
#  define vmem_zalloc(sz, fl)           vmem_alloc_debug((sz), (fl)|__GFP_ZERO,\
                                             __FUNCTION__, __LINE__)
#  define vmem_free(ptr, sz)            vmem_free_debug((ptr), (sz))

extern void *kmem_alloc_debug(size_t, int, const char *, int, int, int);
extern void kmem_free_debug(const void *, size_t);
extern void *vmem_alloc_debug(size_t, int, const char *, int);
extern void vmem_free_debug(const void *, size_t);

# endif /* DEBUG_KMEM_TRACKING */
#else /* DEBUG_KMEM */
/*
 * !DEBUG_KMEM && !DEBUG_KMEM_TRACKING
 *
 * All debugging is disabled.  There will be no overhead even for
 * minimal memory accounting.  To enable basic accounting pass the
 * --enable-debug-kmem option to configure.
 */
# define kmem_alloc(sz, fl)             kmalloc_nofail((sz), (fl))
# define kmem_zalloc(sz, fl)            kzalloc_nofail((sz), (fl))
# define kmem_alloc_node(sz, fl, nd)    kmalloc_node_nofail((sz), (fl), (nd))
# define kmem_free(ptr, sz)             ((void)(sz), kfree(ptr))

# define vmem_alloc(sz, fl)             vmalloc_nofail((sz), (fl))
# define vmem_zalloc(sz, fl)            vzalloc_nofail((sz), (fl))
# define vmem_free(ptr, sz)             ((void)(sz), vfree(ptr))

#endif /* DEBUG_KMEM */

extern int kmem_debugging(void);
extern char *kmem_vasprintf(const char *fmt, va_list ap);
extern char *kmem_asprintf(const char *fmt, ...);
extern char *strdup(const char *str);
extern void strfree(char *str);


/*
 * Slab allocation interfaces.  The SPL slab differs from the standard
 * Linux SLAB or SLUB primarily in that each cache may be backed by slabs
 * allocated from the physical or virtal memory address space.  The virtual
 * slabs allow for good behavior when allocation large objects of identical
 * size.  This slab implementation also supports both constructors and
 * destructions which the Linux slab does not.
 */
enum {
	KMC_BIT_NOTOUCH		= 0,	/* Don't update ages */
	KMC_BIT_NODEBUG		= 1,	/* Default behavior */
	KMC_BIT_NOMAGAZINE	= 2,	/* XXX: Unsupported */
	KMC_BIT_NOHASH		= 3,	/* XXX: Unsupported */
	KMC_BIT_QCACHE		= 4,	/* XXX: Unsupported */
	KMC_BIT_KMEM		= 5,	/* Use kmem cache */
	KMC_BIT_VMEM		= 6,	/* Use vmem cache */
	KMC_BIT_OFFSLAB		= 7,	/* Objects not on slab */
	KMC_BIT_GROWING         = 15,   /* Growing in progress */
	KMC_BIT_REAPING		= 16,	/* Reaping in progress */
	KMC_BIT_DESTROY		= 17,	/* Destroy in progress */
	KMC_BIT_TOTAL		= 18,	/* Proc handler helper bit */
	KMC_BIT_ALLOC		= 19,	/* Proc handler helper bit */
	KMC_BIT_MAX		= 20,	/* Proc handler helper bit */
};

/* kmem move callback return values */
typedef enum kmem_cbrc {
	KMEM_CBRC_YES		= 0,	/* Object moved */
	KMEM_CBRC_NO		= 1,	/* Object not moved */
	KMEM_CBRC_LATER		= 2,	/* Object not moved, try again later */
	KMEM_CBRC_DONT_NEED	= 3,	/* Neither object is needed */
	KMEM_CBRC_DONT_KNOW	= 4,	/* Object unknown */
} kmem_cbrc_t;

#define KMC_NOTOUCH		(1 << KMC_BIT_NOTOUCH)
#define KMC_NODEBUG		(1 << KMC_BIT_NODEBUG)
#define KMC_NOMAGAZINE		(1 << KMC_BIT_NOMAGAZINE)
#define KMC_NOHASH		(1 << KMC_BIT_NOHASH)
#define KMC_QCACHE		(1 << KMC_BIT_QCACHE)
#define KMC_KMEM		(1 << KMC_BIT_KMEM)
#define KMC_VMEM		(1 << KMC_BIT_VMEM)
#define KMC_OFFSLAB		(1 << KMC_BIT_OFFSLAB)
#define KMC_GROWING		(1 << KMC_BIT_GROWING)
#define KMC_REAPING		(1 << KMC_BIT_REAPING)
#define KMC_DESTROY		(1 << KMC_BIT_DESTROY)
#define KMC_TOTAL		(1 << KMC_BIT_TOTAL)
#define KMC_ALLOC		(1 << KMC_BIT_ALLOC)
#define KMC_MAX			(1 << KMC_BIT_MAX)

#define KMC_REAP_CHUNK			INT_MAX
#define KMC_DEFAULT_SEEKS		1

extern struct list_head spl_kmem_cache_list;
extern struct rw_semaphore spl_kmem_cache_sem;

#define SKM_MAGIC			0x2e2e2e2e
#define SKO_MAGIC			0x20202020
#define SKS_MAGIC			0x22222222
#define SKC_MAGIC			0x2c2c2c2c

#define SPL_KMEM_CACHE_DELAY		15	/* Minimum slab release age */
#define SPL_KMEM_CACHE_REAP		0	/* Default reap everything */
#define SPL_KMEM_CACHE_OBJ_PER_SLAB	16	/* Target objects per slab */
#define SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN	8	/* Minimum objects per slab */
#define SPL_KMEM_CACHE_ALIGN		8	/* Default object alignment */

#define POINTER_IS_VALID(p)		0	/* Unimplemented */
#define POINTER_INVALIDATE(pp)			/* Unimplemented */

typedef int (*spl_kmem_ctor_t)(void *, void *, int);
typedef void (*spl_kmem_dtor_t)(void *, void *);
typedef void (*spl_kmem_reclaim_t)(void *);

typedef struct spl_kmem_magazine {
	uint32_t		skm_magic;	/* Sanity magic */
	uint32_t		skm_avail;	/* Available objects */
	uint32_t		skm_size;	/* Magazine size */
	uint32_t		skm_refill;	/* Batch refill size */
	struct spl_kmem_cache	*skm_cache;	/* Owned by cache */
	struct delayed_work	skm_work;	/* Magazine reclaim work */
	unsigned long		skm_age;	/* Last cache access */
	unsigned int		skm_cpu;	/* Owned by cpu */
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

typedef struct spl_kmem_alloc {
	struct spl_kmem_cache	*ska_cache;	/* Owned by cache */
	int			ska_flags;	/* Allocation flags */
	struct delayed_work	ska_work;	/* Allocation work */
} spl_kmem_alloc_t;

typedef struct spl_kmem_emergency {
	void			*ske_obj;	/* Buffer address */
	struct list_head	ske_list;	/* Emergency list linkage */
} spl_kmem_emergency_t;

typedef struct spl_kmem_cache {
	uint32_t		skc_magic;	/* Sanity magic */
	uint32_t		skc_name_size;	/* Name length */
	char			*skc_name;	/* Name string */
	spl_kmem_magazine_t	*skc_mag[NR_CPUS]; /* Per-CPU warm cache */
	uint32_t		skc_mag_size;	/* Magazine size */
	uint32_t		skc_mag_refill;	/* Magazine refill count */
	spl_kmem_ctor_t		skc_ctor;	/* Constructor */
	spl_kmem_dtor_t		skc_dtor;	/* Destructor */
	spl_kmem_reclaim_t	skc_reclaim;	/* Reclaimator */
	void			*skc_private;	/* Private data */
	void			*skc_vmp;	/* Unused */
	unsigned long		skc_flags;	/* Flags */
	uint32_t		skc_obj_size;	/* Object size */
	uint32_t		skc_obj_align;	/* Object alignment */
	uint32_t		skc_slab_objs;	/* Objects per slab */
	uint32_t		skc_slab_size;	/* Slab size */
	uint32_t		skc_delay;	/* Slab reclaim interval */
	uint32_t		skc_reap;	/* Slab reclaim count */
	atomic_t		skc_ref;	/* Ref count callers */
	struct delayed_work	skc_work;	/* Slab reclaim work */
	struct list_head	skc_list;	/* List of caches linkage */
	struct list_head	skc_complete_list;/* Completely alloc'ed */
	struct list_head	skc_partial_list; /* Partially alloc'ed */
	struct list_head	skc_emergency_list; /* Min sized objects */
	spinlock_t		skc_lock;	/* Cache lock */
	wait_queue_head_t	skc_waitq;	/* Allocation waiters */
	uint64_t		skc_slab_fail;	/* Slab alloc failures */
	uint64_t		skc_slab_create;/* Slab creates */
	uint64_t		skc_slab_destroy;/* Slab destroys */
	uint64_t		skc_slab_total;	/* Slab total current */
	uint64_t		skc_slab_alloc;	/* Slab alloc current */
	uint64_t		skc_slab_max;	/* Slab max historic  */
	uint64_t		skc_obj_total;	/* Obj total current */
	uint64_t		skc_obj_alloc;	/* Obj alloc current */
	uint64_t		skc_obj_max;	/* Obj max historic */
	uint64_t		skc_obj_emergency; /* Obj emergency current */
	uint64_t		skc_obj_emergency_max; /* Obj emergency max */
} spl_kmem_cache_t;
#define kmem_cache_t		spl_kmem_cache_t

extern spl_kmem_cache_t *spl_kmem_cache_create(char *name, size_t size,
	size_t align, spl_kmem_ctor_t ctor, spl_kmem_dtor_t dtor,
	spl_kmem_reclaim_t reclaim, void *priv, void *vmp, int flags);
extern void spl_kmem_cache_set_move(spl_kmem_cache_t *,
	kmem_cbrc_t (*)(void *, void *, size_t, void *));
extern void spl_kmem_cache_destroy(spl_kmem_cache_t *skc);
extern void *spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags);
extern void spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj);
extern void spl_kmem_cache_reap_now(spl_kmem_cache_t *skc, int count);
extern void spl_kmem_reap(void);

int spl_kmem_init_kallsyms_lookup(void);
int spl_kmem_init(void);
void spl_kmem_fini(void);

#define kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags) \
        spl_kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags)
#define kmem_cache_set_move(skc, move)	spl_kmem_cache_set_move(skc, move)
#define kmem_cache_destroy(skc)		spl_kmem_cache_destroy(skc)
#define kmem_cache_alloc(skc, flags)	spl_kmem_cache_alloc(skc, flags)
#define kmem_cache_free(skc, obj)	spl_kmem_cache_free(skc, obj)
#define kmem_cache_reap_now(skc)	\
        spl_kmem_cache_reap_now(skc, skc->skc_reap)
#define kmem_reap()			spl_kmem_reap()
#define kmem_virt(ptr)			(((ptr) >= (void *)VMALLOC_START) && \
					 ((ptr) <  (void *)VMALLOC_END))

#endif	/* _SPL_KMEM_H */
