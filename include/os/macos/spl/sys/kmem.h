/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 *
 * Copyright (C) 2008 MacZFS Project
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 * Copyright (C) 2017 Sean Doran <smd@use.net>
 *
 */

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <sys/atomic.h>
#include <sys/types.h>
#include <sys/vmsystm.h>
#include <sys/kstat.h>
#include <sys/malloc.h>
#include <sys/list.h>
#include <sys/vmem.h>

#ifdef	__cplusplus
extern "C" {
#endif

// XNU total amount of memory
extern uint64_t physmem;

#define	KM_SLEEP	0x0000	/* can block for memory; success guaranteed */
#define	KM_NOSLEEP	0x0001	/* cannot block for memory; may fail */
#define	KM_PANIC	0x0002	/* if memory cannot be allocated, panic */
#define	KM_PUSHPAGE	0x0004	/* can block for memory; may use reserve */
#define	KM_NORMALPRI	0x0008  /* with KM_NOSLEEP, lower priority allocation */
#define	KM_NODEBUG	0x0010  /* NOT IMPLEMENTED ON OSX */
#define	KM_NO_VBA	0x0020  /* OSX: don't descend to the bucket layer */
#define	KM_VMFLAGS	0x00ff	/* flags that must match VM_* flags */

#define	KM_FLAGS	0xffff	/* all settable kmem flags */

/*
 * Kernel memory allocator: DDI interfaces.
 * See kmem_alloc(9F) for details.
 */

// Work around symbol collisions in XNU
#define	kmem_alloc(size, kmflags)	zfs_kmem_alloc((size), (kmflags))
#define	kmem_zalloc(size, kmflags)	zfs_kmem_zalloc((size), (kmflags))
#define	kmem_free(buf, size)		zfs_kmem_free((buf), (size))

void *zfs_kmem_alloc(size_t size, int kmflags);
void *zfs_kmem_zalloc(size_t size, int kmflags);
void zfs_kmem_free(void *buf, size_t size);

void spl_kmem_init(uint64_t);
void spl_kmem_thread_init(void);
void spl_kmem_mp_init(void);
void spl_kmem_thread_fini(void);
void spl_kmem_fini(void);

size_t kmem_size(void);
size_t kmem_used(void);
int64_t kmem_avail(void);
size_t kmem_num_pages_wanted(void);
int	spl_vm_pool_low(void);
int32_t spl_minimal_physmem_p(void);
int64_t spl_adjust_pressure(int64_t);
int64_t spl_free_wrapper(void);
int64_t spl_free_manual_pressure_wrapper(void);
boolean_t spl_free_fast_pressure_wrapper(void);
void spl_free_set_pressure(int64_t);
void spl_free_set_fast_pressure(boolean_t);
uint64_t spl_free_last_pressure_wrapper(void);

#define	KMC_NOTOUCH	0x00010000
#define	KMC_NODEBUG	0x00020000
#define	KMC_NOMAGAZINE	0x00040000
#define	KMC_NOHASH	0x00080000
#define	KMC_QCACHE	0x00100000
#define	KMC_KMEM_ALLOC	0x00200000	/* internal use only */
#define	KMC_IDENTIFIER	0x00400000	/* internal use only */
#define	KMC_PREFILL	0x00800000
#define	KMC_ARENA_SLAB	0x01000000	/* use a bigger kmem cache */

struct kmem_cache;

typedef struct kmem_cache kmem_cache_t;

/* Client response to kmem move callback */
typedef enum kmem_cbrc {
	KMEM_CBRC_YES,
	KMEM_CBRC_NO,
	KMEM_CBRC_LATER,
	KMEM_CBRC_DONT_NEED,
	KMEM_CBRC_DONT_KNOW
} kmem_cbrc_t;

#define	POINTER_IS_VALID(p)	(!((uintptr_t)(p) & 0x3))
#define	POINTER_INVALIDATE(pp)	(*(pp) = (void *)((uintptr_t)(*(pp)) | 0x1))

kmem_cache_t *kmem_cache_create(char *name, size_t bufsize, size_t align,
    int (*constructor)(void *, void *, int),
    void (*destructor)(void *, void *),
    void (*reclaim)(void *),
    void *_private, vmem_t *vmp, int cflags);
void kmem_cache_destroy(kmem_cache_t *cache);
void *kmem_cache_alloc(kmem_cache_t *cache, int flags);
void kmem_cache_free(kmem_cache_t *cache, void *buf);
void kmem_cache_free_to_slab(kmem_cache_t *cache, void *buf);
extern boolean_t kmem_cache_reap_active(void);
void kmem_cache_reap_now(kmem_cache_t *cache);
void kmem_depot_ws_zero(kmem_cache_t *cache);
void kmem_reap(void);
void kmem_reap_idspace(void);
kmem_cache_t *kmem_cache_buf_in_cache(kmem_cache_t *, void *);

int kmem_debugging(void);
void kmem_cache_set_move(kmem_cache_t *,
    kmem_cbrc_t (*)(void *, void *, size_t, void *));

char *kmem_asprintf(const char *fmt, ...);
extern char *kmem_strdup(const char *str);
extern void kmem_strfree(char *str);
char *kmem_vasprintf(const char *fmt, va_list ap);
char *kmem_strstr(const char *in, const char *str);
void strident_canon(char *s, size_t n);
extern int kmem_scnprintf(char *str, size_t size,
	const char *fmt, ...);

boolean_t spl_arc_no_grow(size_t, boolean_t, kmem_cache_t **);

extern uint64_t spl_kmem_cache_inuse(kmem_cache_t *cache);
extern uint64_t spl_kmem_cache_entry_size(kmem_cache_t *cache);

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_KMEM_H */
