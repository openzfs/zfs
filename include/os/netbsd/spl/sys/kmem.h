// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS kmem / kmem_cache on top of NetBSD kmem(9) and pool_cache(9).
 */

#ifndef _OPENSOLARIS_SYS_KMEM_H_
#define	_OPENSOLARIS_SYS_KMEM_H_

/*
 * ZFS_KMEM_STRICT=1 (default) keeps the size-check header and panics
 * on a mismatched free.  ZFS_KMEM_STRICT=0 is a thin kmem(9) wrapper
 * (no header).  Select it with -DZFS_KMEM_STRICT=0.
 */
#ifndef ZFS_KMEM_STRICT
#define	ZFS_KMEM_STRICT	1
#endif

#ifdef _KERNEL

#include <sys/param.h>
#include_next <sys/kmem.h>
#ifdef kmem_strdup
#undef kmem_strdup
#endif
#include <sys/pool.h>
#include <sys/stdarg.h>

/*
 * KM_PUSHPAGE: NetBSD kmem(9) has no reserve / no-recurse flag.
 * Same mapping as FreeBSD (M_WAITOK).  Linux KM_PUSHPAGE dips
 * into PF_MEMALLOC; we cannot.
 *
 * KMC_RECLAIMABLE: pool_cache(9) is always drainable by the
 * pagedaemon.  Linux SLAB_RECLAIM_ACCOUNT has no equivalent;
 * unused objects are reclaimed regardless of this bit.
 * KMC_NODEBUG / KMC_NOTOUCH: no pool_cache analogue; ignored.
 */
#define	KM_PUSHPAGE	KM_SLEEP
#define	KM_NORMALPRI	0
#define	KM_NODEBUG	0
#define	KMC_NOTOUCH	0x00010000
#define	KMC_NODEBUG	0x00020000
#define	KMC_RECLAIMABLE	0

#define	POINTER_IS_VALID(p)	(!((uintptr_t)(p) & 0x3))
#define	POINTER_INVALIDATE(pp)	(*(pp) = (void *)((uintptr_t)(*(pp)) | 0x1))

typedef struct vmem vmem_t;
typedef struct kmem_cache kmem_cache_t;

/* CSTYLED */
char *kmem_asprintf(const char *, ...)
    __attribute__((format(__printf__, 1, 2)));
/* CSTYLED */
char *kmem_vasprintf(const char *fmt, va_list ap)
    __attribute__((format(__printf__, 1, 0)));
int	kmem_scnprintf(char *restrict str, size_t size,
    const char *restrict fmt, ...);

kmem_cache_t *kmem_cache_create(const char *, size_t, size_t,
    int (*)(void *, void *, int), void (*)(void *, void *),
    void (*)(void *), void *, vmem_t *, int);
void	kmem_cache_destroy(kmem_cache_t *);
void	*kmem_cache_alloc(kmem_cache_t *, int);
void	kmem_cache_free(kmem_cache_t *, void *);
void	kmem_cache_reap_now(kmem_cache_t *);
void	kmem_cache_reap_soon(kmem_cache_t *);
boolean_t kmem_cache_reap_active(void);
void	kmem_reap(void);
int	kmem_debugging(void);
uint64_t spl_kmem_size(void);
uint64_t spl_kmem_cache_inuse(kmem_cache_t *);
uint64_t spl_kmem_cache_entry_size(kmem_cache_t *);
void	spl_kmem_init(void);
void	spl_kmem_fini(void);
void	*spl_calloc(size_t, size_t);
void	*calloc(size_t, size_t);

void	*solaris_kmem_alloc(size_t, int);
void	*solaris_kmem_zalloc(size_t, int);
void	solaris_kmem_free(void *, size_t);

/* Compat names used inside this port before the solaris_* remap. */
#define	zfs_kmem_alloc	solaris_kmem_alloc
#define	zfs_kmem_zalloc	solaris_kmem_zalloc
#define	zfs_kmem_free	solaris_kmem_free

/*
 * OpenZFS remaps kmem_* to solaris_kmem_* so one wrapper serves
 * both this module and (historically) dtrace.  dtrace now gets
 * its own copies from solaris.kmod; openzfs localizes these
 * names so the two kmods can load together.
 */
#define	kmem_alloc(size, flags)		solaris_kmem_alloc((size), (flags))
#define	kmem_zalloc(size, flags)	solaris_kmem_zalloc((size), (flags))
#define	kmem_free(buf, size)		solaris_kmem_free((buf), (size))


#define	kmem_size()			spl_kmem_size()
#define	freemem				uvmexp.free
#define	minfree				uvmexp.freemin

#endif /* _KERNEL */

#ifdef _STANDALONE
typedef int kmem_cache_t;
#endif

#endif /* _OPENSOLARIS_SYS_KMEM_H_ */
