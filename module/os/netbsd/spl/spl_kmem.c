// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS kmem / kmem_cache for NetBSD: kmem(9) + pool_cache(9).
 *
 * OpenZFS kmem_free's size is a Solaris leftover.  Linux kfree and
 * FreeBSD free ignore it.  NetBSD kmem_free selects the pool from
 * that size: alloc(N)/free(M) that cross a size class, or
 * kmem_strfree of a truncated string, puts the object on the wrong
 * cache.  The pagedaemon then panics in pool_do_put ("page header
 * missing") long after the bad free — kmem-00008 is the 8-byte
 * class, i.e. sizeof (void *) on amd64.
 *
 * Record the real size in a 16-byte header so free hits the same
 * pool as alloc, and panic at the free site if the caller size
 * does not match.  Same idea as zcalloc in spl_zlib.c and NetBSD
 * option KMEM_SIZE.  ZFS_KMEM_STRICT=0 drops the header.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/pool.h>
#include <sys/cpu.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/stdarg.h>
#include <sys/debug.h>
#include <uvm/uvm_extern.h>

/* Use native NetBSD kmem(9), not the OpenZFS wrappers. */
#undef kmem_alloc
#undef kmem_zalloc
#undef kmem_free

#define	SPL_KMEM_MAGIC		0x5a4b4d48u	/* 'ZKMH' */

/*
 * Always 16 bytes so the payload is 8-byte aligned on ILP32 as well
 * as LP64.  skh_total is the size passed to native kmem_free,
 * including this header.  skh_user is the size the caller passed
 * to solaris_kmem_alloc.
 */
struct spl_kmem_hdr {
	uint64_t	skh_total;
	uint32_t	skh_magic;
	uint32_t	skh_user;
};
_Static_assert(sizeof (struct spl_kmem_hdr) == 16,
	"spl_kmem_hdr must be 16 bytes");

struct kmem_cache {
	pool_cache_t	km_pool;
	char		km_name[32];
	void		*km_private;
	size_t		km_size;
	int		(*km_constructor)(void *, void *, int);
	void		(*km_destructor)(void *, void *);
	void		(*km_reclaim)(void *);
	struct kmem_cache *km_next;
	unsigned	km_busy;	/* reapers holding this cache */
	unsigned	km_dying;	/* destroy waiting for km_busy */
	unsigned	km_reap_gen;
};

static kmem_cache_t *kmem_cache_head;
static kmutex_t kmem_cache_lock;
static kcondvar_t kmem_cache_cv;
static int kmem_cache_lock_ready;
static unsigned kmem_reap_gen;
static int kmem_reap_live;
static int kmem_reap_stop;

/*
 * Snapshot buffer for kmem_reap.  Dedicated pool_cache, not on
 * kmem_cache_head, so kmem_reap never reclaims the pool it is
 * allocated from.
 *
 * Why a primed slab and not kmem_alloc(KM_NOSLEEP): reap runs
 * because the system is short on memory.  A KM_NOSLEEP kmem_alloc
 * then fails and we skip the reap.  pool_cache_prime at init
 * puts items on the free list so PR_NOWAIT succeeds without
 * touching the page allocator.
 *
 * Why this cannot deadlock on its own objects:
 *   - The only getter is kmem_reap, and it is always PR_NOWAIT
 *     (KM_NOSLEEP).  kmem_reap never PR_WAITOK / KM_SLEEP: if the
 *     primed objects are all in use, this pass is skipped.  Nobody
 *     waits for a snap object, so "all objects held, waiter blocked
 *     for another" cannot happen.
 *   - Extra concurrent/nested reaps that miss just return.  Nested
 *     kmem_reap from a drain hook therefore cannot self-wait.
 *   - pool_reclaim uses mutex_tryenter and calls drain hooks with
 *     PR_NOWAIT.  It does not wait on this cache.
 *   - Held objects are returned before kmem_reap returns; we never
 *     block on kmem_cache_lock while waiting for a snap.
 */
#define	KMEM_REAP_SNAP_MAX	256
#define	KMEM_REAP_SNAP_N	4
static pool_cache_t kmem_reap_snap_cache;

static void
kmem_caches_lock_init(void)
{
	if (kmem_cache_lock_ready == 0) {
		mutex_init(&kmem_cache_lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&kmem_cache_cv, "kmemc", CV_DEFAULT, NULL);
		kmem_cache_lock_ready = 1;
	}
}

void
spl_kmem_init(void)
{
	void *objs[KMEM_REAP_SNAP_N];
	int i, got;

	kmem_caches_lock_init();
	kmem_reap_snap_cache = pool_cache_init(
	    KMEM_REAP_SNAP_MAX * sizeof (kmem_cache_t *),
	    sizeof (void *), 0, 0, "kmem_reap_snap",
	    NULL, IPL_NONE, NULL, NULL, NULL);
	if (kmem_reap_snap_cache == NULL)
		return;
	pool_cache_prime(kmem_reap_snap_cache, KMEM_REAP_SNAP_N);
	got = 0;
	for (i = 0; i < KMEM_REAP_SNAP_N; i++) {
		objs[i] = pool_cache_get(kmem_reap_snap_cache, PR_WAITOK);
		if (objs[i] == NULL)
			break;
		got++;
	}
	for (i = 0; i < got; i++)
		pool_cache_put(kmem_reap_snap_cache, objs[i]);
}

void
spl_kmem_fini(void)
{
	if (kmem_cache_lock_ready != 0) {
		mutex_enter(&kmem_cache_lock);
		kmem_reap_stop = 1;
		while (kmem_reap_live != 0)
			cv_wait(&kmem_cache_cv, &kmem_cache_lock);
		mutex_exit(&kmem_cache_lock);
	}
	if (kmem_reap_snap_cache != NULL) {
		pool_cache_destroy(kmem_reap_snap_cache);
		kmem_reap_snap_cache = NULL;
	}
}

static int
solaris_constructor(void *private, void *object, int flag)
{
	kmem_cache_t *km = private;

	if (km->km_constructor != NULL)
		return ((*km->km_constructor)(object, km->km_private, flag));
	return (0);
}

static void
solaris_destructor(void *private, void *object)
{
	kmem_cache_t *km = private;

	if (km->km_destructor != NULL)
		(*km->km_destructor)(object, km->km_private);
}

static void
solaris_reclaim(void *private, int flag)
{
	kmem_cache_t *km = private;

	(void) flag;
	if (km->km_reclaim != NULL)
		(*km->km_reclaim)(km->km_private);
}

/*
 * NetBSD kmem(9) requires exactly one of KM_SLEEP / KM_NOSLEEP.
 * OpenZFS KM_PUSHPAGE and a bare 0 both mean a sleeping alloc.
 */
static int
spl_kmflags(int kmflags)
{
	return ((kmflags & KM_NOSLEEP) ? KM_NOSLEEP : KM_SLEEP);
}

void *
solaris_kmem_alloc(size_t size, int kmflags)
{
#if ZFS_KMEM_STRICT
	struct spl_kmem_hdr *h;
	size_t total;

	if (size == 0)
		return (NULL);
	if (size > SIZE_MAX - sizeof (*h))
		return (NULL);
	total = sizeof (*h) + size;
	/*
	 * kmem_alloc() asserts !cpu_intr_p and then calls
	 * kmem_intr_alloc.  The intr pair is legal in any context
	 * and is what kmem_intr_free releases, so strict and
	 * non-strict builds both use it.  KM_SLEEP must panic, not
	 * return NULL: callers do not check.  panic, not KASSERT:
	 * a KASSERT disappears on a non-DIAGNOSTIC kernel, and there
	 * the native allocator would try to sleep in interrupt
	 * context.
	 */
	if ((cpu_intr_p() || cpu_softintr_p()) &&
	    (kmflags & KM_NOSLEEP) == 0) {
		panic("solaris_kmem_alloc: KM_SLEEP from interrupt, "
		    "size %zu", size);
	}
	h = kmem_intr_alloc(total, spl_kmflags(kmflags));
	if (h == NULL)
		return (NULL);
	h->skh_total = total;
	h->skh_magic = SPL_KMEM_MAGIC;
	h->skh_user = (uint32_t)size;
	return (h + 1);
#else
	if (size == 0)
		return (NULL);
	/*
	 * No header, so the free path cannot tell the pools apart.
	 * kmem_intr_alloc and kmem_intr_free are the same pools as
	 * kmem_alloc/kmem_free and are legal in any context.  Using
	 * them for every alloc keeps a KM_NOSLEEP from interrupt
	 * working in production the same way it does in a strict
	 * build, and a free from interrupt does not hit kmem_free's
	 * !cpu_intr_p assert.  KM_SLEEP from interrupt still panics.
	 */
	if ((cpu_intr_p() || cpu_softintr_p()) &&
	    (kmflags & KM_NOSLEEP) == 0) {
		panic("solaris_kmem_alloc: KM_SLEEP from interrupt, "
		    "size %zu", size);
	}
	return (kmem_intr_alloc(size, spl_kmflags(kmflags)));
#endif
}

void *
solaris_kmem_zalloc(size_t size, int kmflags)
{
	void *p;

	p = solaris_kmem_alloc(size, kmflags);
	if (p != NULL)
		memset(p, 0, size);
	return (p);
}

void
solaris_kmem_free(void *buf, size_t size)
{
#if ZFS_KMEM_STRICT
	struct spl_kmem_hdr *h;
	size_t payload;

	if (buf == NULL)
		return;
	h = (struct spl_kmem_hdr *)buf - 1;
	if (h->skh_magic != SPL_KMEM_MAGIC) {
		panic("solaris_kmem_free: %p size %zu: bad header magic %x",
		    buf, size, h->skh_magic);
	}
	payload = h->skh_total - sizeof (*h);
	if (payload != size) {
		panic("solaris_kmem_free: %p size %zu != allocated %zu",
		    buf, size, payload);
	}
	h->skh_magic = 0;
	kmem_intr_free(h, h->skh_total);
#else
	if (buf == NULL)
		return;
	kmem_intr_free(buf, size);
#endif
}

void *
spl_xdr_mem_alloc(size_t size)
{
	if (size == 0)
		size = 1;
	return (solaris_kmem_alloc(size, KM_SLEEP));
}

void
spl_xdr_mem_free(void *ptr, size_t size)
{
	if (ptr == NULL)
		return;
	if (size == 0)
		size = 1;
	solaris_kmem_free(ptr, size);
}

uint64_t
spl_kmem_size(void)
{
	return ((uint64_t)physmem * PAGE_SIZE);
}

kmem_cache_t *
kmem_cache_create(const char *name, size_t bufsize, size_t align,
    int (*constructor)(void *, void *, int), void (*destructor)(void *, void *),
    void (*reclaim)(void *), void *private, vmem_t *vmp, int cflags)
{
	kmem_cache_t *km;

	(void) vmp;
	(void) cflags;
	kmem_caches_lock_init();
	km = kmem_zalloc(sizeof (*km), KM_SLEEP);
	strlcpy(km->km_name, name, sizeof (km->km_name));
	km->km_private = private;
	km->km_size = bufsize;
	km->km_constructor = constructor;
	km->km_destructor = destructor;
	km->km_reclaim = reclaim;
	if (align == 0)
		align = 8;
	km->km_pool = pool_cache_init(bufsize, align, 0, 0, km->km_name,
	    NULL, IPL_NONE, solaris_constructor, solaris_destructor, km);
	if (km->km_pool == NULL) {
		kmem_free(km, sizeof (*km));
		return (NULL);
	}
	if (reclaim != NULL)
		pool_cache_set_drain_hook(km->km_pool, solaris_reclaim, km);
	mutex_enter(&kmem_cache_lock);
	km->km_next = kmem_cache_head;
	kmem_cache_head = km;
	mutex_exit(&kmem_cache_lock);
	return (km);
}

void
kmem_cache_destroy(kmem_cache_t *km)
{
	kmem_cache_t **pp;

	mutex_enter(&kmem_cache_lock);
	km->km_dying = 1;
	for (pp = &kmem_cache_head; *pp != NULL; pp = &(*pp)->km_next) {
		if (*pp == km) {
			*pp = km->km_next;
			break;
		}
	}
	while (km->km_busy != 0)
		cv_wait(&kmem_cache_cv, &kmem_cache_lock);
	mutex_exit(&kmem_cache_lock);
	pool_cache_destroy(km->km_pool);
	kmem_free(km, sizeof (*km));
}

void *
kmem_cache_alloc(kmem_cache_t *km, int flags)
{
	int pr;

	/*
	 * osnet passes flags through to pool_cache_get.  KM_SLEEP is
	 * PR_WAITOK on NetBSD; map explicitly so a stray bit cannot
	 * turn a sleeping alloc into PR_NOWAIT.
	 */
	pr = (flags & KM_NOSLEEP) ? PR_NOWAIT : PR_WAITOK;
	return (pool_cache_get(km->km_pool, pr));
}

void
kmem_cache_free(kmem_cache_t *km, void *object)
{
	pool_cache_put(km->km_pool, object);
}

void
kmem_cache_reap_now(kmem_cache_t *km)
{
	if (km == NULL || kmem_cache_lock_ready == 0)
		return;
	mutex_enter(&kmem_cache_lock);
	if (km->km_dying) {
		mutex_exit(&kmem_cache_lock);
		return;
	}
	km->km_busy++;
	mutex_exit(&kmem_cache_lock);
	pool_cache_reclaim(km->km_pool);
	mutex_enter(&kmem_cache_lock);
	km->km_busy--;
	if (km->km_dying && km->km_busy == 0)
		cv_broadcast(&kmem_cache_cv);
	mutex_exit(&kmem_cache_lock);
}

void
kmem_cache_reap_soon(kmem_cache_t *km)
{
	kmem_cache_reap_now(km);
}

boolean_t
kmem_cache_reap_active(void)
{
	return (B_FALSE);
}

void
kmem_reap(void)
{
	kmem_cache_t **snap;
	unsigned n, i, gen;
	kmem_cache_t *km;

	if (kmem_cache_lock_ready == 0)
		return;
	if (cpu_intr_p() || cpu_softintr_p())
		return;

	/*
	 * Bump kmem_reap_live before the snap get, and never call
	 * pool_cache_get while holding kmem_cache_lock: a miss can
	 * drain other pools and re-enter this lock.
	 *
	 * fini sets kmem_reap_stop and waits for live == 0 before
	 * destroying the snap cache, so a get/put cannot land on a
	 * freed pool.  Put the snap object back before dropping
	 * live — live is the fence; a put after live-- races
	 * pool_cache_destroy on unload.
	 */
	mutex_enter(&kmem_cache_lock);
	if (kmem_reap_stop || kmem_reap_snap_cache == NULL) {
		mutex_exit(&kmem_cache_lock);
		return;
	}
	kmem_reap_live++;
	mutex_exit(&kmem_cache_lock);

	snap = pool_cache_get(kmem_reap_snap_cache, PR_NOWAIT);
	if (snap == NULL) {
		mutex_enter(&kmem_cache_lock);
		ASSERT(kmem_reap_live > 0);
		kmem_reap_live--;
		if (kmem_reap_stop && kmem_reap_live == 0)
			cv_broadcast(&kmem_cache_cv);
		mutex_exit(&kmem_cache_lock);
		return;
	}

	mutex_enter(&kmem_cache_lock);
	if (kmem_reap_stop) {
		mutex_exit(&kmem_cache_lock);
		goto put_snap;
	}
	gen = ++kmem_reap_gen;
	mutex_exit(&kmem_cache_lock);

	/*
	 * Snapshot under the list lock, then reclaim with it dropped
	 * so drain hooks can allocate.  km_busy keeps destroy from
	 * freeing a cache still in snap[].  Multiple passes if more
	 * than KMEM_REAP_SNAP_MAX caches exist.
	 */
	for (;;) {
		n = 0;
		mutex_enter(&kmem_cache_lock);
		for (km = kmem_cache_head; km != NULL &&
		    n < KMEM_REAP_SNAP_MAX; km = km->km_next) {
			if (km->km_dying || km->km_reap_gen == gen)
				continue;
			km->km_reap_gen = gen;
			km->km_busy++;
			snap[n++] = km;
		}
		mutex_exit(&kmem_cache_lock);
		if (n == 0)
			break;
		for (i = 0; i < n; i++)
			pool_cache_reclaim(snap[i]->km_pool);
		mutex_enter(&kmem_cache_lock);
		for (i = 0; i < n; i++) {
			snap[i]->km_busy--;
			if (snap[i]->km_dying && snap[i]->km_busy == 0)
				cv_broadcast(&kmem_cache_cv);
		}
		mutex_exit(&kmem_cache_lock);
		if (n < KMEM_REAP_SNAP_MAX)
			break;
	}

put_snap:
	pool_cache_put(kmem_reap_snap_cache, snap);
	mutex_enter(&kmem_cache_lock);
	ASSERT(kmem_reap_live > 0);
	kmem_reap_live--;
	if (kmem_reap_stop && kmem_reap_live == 0)
		cv_broadcast(&kmem_cache_cv);
	mutex_exit(&kmem_cache_lock);
}

int
kmem_debugging(void)
{
	return (0);
}

uint64_t
spl_kmem_cache_inuse(kmem_cache_t *cache)
{
	(void) cache;
	return (0);
}

uint64_t
spl_kmem_cache_entry_size(kmem_cache_t *cache)
{
	return (cache->km_size);
}

void *
spl_calloc(size_t n, size_t s)
{
	size_t bytes;
	int kmflag;

	if (n == 0 || s == 0) {
		kmflag = (cpu_intr_p() || cpu_softintr_p()) ?
		    KM_NOSLEEP : KM_SLEEP;
		return (zfs_kmem_zalloc(1, kmflag));
	}
	if (n > SIZE_MAX / s)
		return (NULL);
	bytes = n * s;
	kmflag = (cpu_intr_p() || cpu_softintr_p()) ? KM_NOSLEEP : KM_SLEEP;
	return (zfs_kmem_zalloc(bytes, kmflag));
}

/*
 * GENERIC does not export calloc(9).  lz4 and zfeature_common call
 * calloc by name; this is the kmod-local symbol, not an interpose.
 */
void *
calloc(size_t n, size_t s)
{
	return (spl_calloc(n, s));
}

char *
kmem_vasprintf(const char *fmt, va_list ap)
{
	char *buf;
	va_list aq;
	int n;

	va_copy(aq, ap);
	n = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	if (n < 0)
		return (NULL);
	buf = zfs_kmem_alloc((size_t)n + 1, KM_SLEEP);
	(void) vsnprintf(buf, (size_t)n + 1, fmt, ap);
	return (buf);
}

char *
kmem_asprintf(const char *fmt, ...)
{
	va_list ap;
	char *buf;

	va_start(ap, fmt);
	buf = kmem_vasprintf(fmt, ap);
	va_end(ap);
	return (buf);
}

int
kmem_scnprintf(char *restrict str, size_t size, const char *restrict fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(str, size, fmt, ap);
	va_end(ap);
	if (n < 0)
		return (n);
	if (size == 0)
		return (0);
	if ((size_t)n >= size)
		return ((int)size - 1);
	return (n);
}
