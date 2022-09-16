/*
 * Coverity Scan model
 * https://scan.coverity.com/models
 *
 * This is a modeling file for Coverity Scan.
 * Modeling helps to avoid false positives.
 *
 * - Modeling doesn't need full structs and typedefs. Rudimentary structs
 *   and similar types are sufficient.
 * - An uninitialized local pointer is not an error. It signifies that the
 *   variable could be either NULL or have some data.
 *
 * Coverity Scan doesn't pick up modifications automatically. The model file
 * must be uploaded by an admin in the analysis settings.
 *
 * Some of this initially cribbed from:
 *
 * https://github.com/kees/coverity-linux/blob/trunk/model.c
 *
 * The below model was based on the original model by Brian Behlendorf for the
 * original zfsonlinux/zfs repository. Some inspiration was taken from
 * kees/coverity-linux, specifically involving memory copies.
 */

#include <stdarg.h>

#define	UMEM_DEFAULT		0x0000  /* normal -- may fail */
#define	UMEM_NOFAIL		0x0100  /* Never fails */

#define	NULL	(0)

int condition0, condition1;

void
abort()
{
	__coverity_panic__();
}

void
exit(int status)
{
	(void) status;

	__coverity_panic__();
}

void
_exit(int status)
{
	(void) status;

	__coverity_panic__();
}

void
zed_log_die(const char *fmt, ...)
{
	__coverity_format_string_sink__(fmt);
	__coverity_panic__();
}

void
panic(const char *fmt, ...)
{
	__coverity_format_string_sink__(fmt);
	__coverity_panic__();
}

void
vpanic(const char *fmt, va_list adx)
{
	(void) adx;

	__coverity_format_string_sink__(fmt);
	__coverity_panic__();
}

void
uu_panic(const char *format, ...)
{
	__coverity_format_string_sink__(format);
	__coverity_panic__();
}

int
libspl_assertf(const char *file, const char *func, int line,
    const char *format, ...)
{
	__coverity_format_string_sink__(format);
	__coverity_panic__();
}

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	__coverity_tainted_data_argument__(from);
	__coverity_tainted_data_argument__(to);
	__coverity_writeall__(to);
}

void *
memset(void *dst, int c, size_t len)
{
	__coverity_writeall__(dst);
	return (dst);
}

void *
memmove(void *dst, void *src, size_t len)
{
	__coverity_writeall__(dst);
	return (dst);
}

void *
memcpy(void *dst, void *src, size_t len)
{
	__coverity_writeall__(dst);
	return (dst);
}

void *
umem_alloc_aligned(size_t size, size_t align, int kmflags)
{
	(void) align;

	if (UMEM_NOFAIL & kmflags == UMEM_NOFAIL)
		return (__coverity_alloc__(size));
	else if (condition0)
		return (__coverity_alloc__(size));
	else
		return (NULL);
}

void *
umem_alloc(size_t size, int kmflags)
{
	if (UMEM_NOFAIL & kmflags == UMEM_NOFAIL)
		return (__coverity_alloc__(size));
	else if (condition0)
		return (__coverity_alloc__(size));
	else
		return (NULL);
}

void *
umem_zalloc(size_t size, int kmflags)
{
	if (UMEM_NOFAIL & kmflags == UMEM_NOFAIL)
		return (__coverity_alloc__(size));
	else if (condition0)
		return (__coverity_alloc__(size));
	else
		return (NULL);
}

void
umem_free(void *buf, size_t size)
{
	(void) size;

	__coverity_free__(buf);
}

void *
spl_kmem_alloc(size_t sz, int fl, const char *func, int line)
{
	(void) func;
	(void) line;

	if (condition1)
		__coverity_sleep__();

	if (fl == 0) {
		return (__coverity_alloc__(sz));
	} else if (condition0)
		return (__coverity_alloc__(sz));
	else
		return (NULL);
}

void *
spl_kmem_zalloc(size_t sz, int fl, const char *func, int line)
{
	(void) func;
	(void) line;

	if (condition1)
		__coverity_sleep__();

	if (fl == 0) {
		return (__coverity_alloc__(sz));
	} else if (condition0)
		return (__coverity_alloc__(sz));
	else
		return (NULL);
}

void
spl_kmem_free(const void *ptr, size_t sz)
{
	(void) sz;

	__coverity_free__(ptr);
}

typedef struct {} spl_kmem_cache_t;

void *
spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags)
{
	(void) skc;

	if (condition1)
		__coverity_sleep__();

	if (flags == 0) {
		return (__coverity_alloc_nosize__());
	} else if (condition0)
		return (__coverity_alloc_nosize__());
	else
		return (NULL);
}

void
spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj)
{
	(void) skc;

	__coverity_free__(obj);
}

void
malloc(size_t size)
{
	__coverity_alloc__(size);
}

void
free(void *buf)
{
	__coverity_free__(buf);
}

int
spl_panic(const char *file, const char *func, int line, const char *fmt, ...)
{
	__coverity_format_string_sink__(fmt);
	__coverity_panic__();
}

int
sched_yield(void)
{
	__coverity_sleep__();
}

typedef struct {} kmutex_t;
typedef struct {} krwlock_t;
typedef int krw_t;

/*
 * Coverty reportedly does not support macros, so this only works for
 * userspace.
 */

void
mutex_enter(kmutex_t *mp)
{
	if (condition0)
		__coverity_sleep__();

	__coverity_exclusive_lock_acquire__(mp);
}

int
mutex_tryenter(kmutex_t *mp)
{
	if (condition0) {
		__coverity_exclusive_lock_acquire__(mp);
		return (1);
	}

	return (0);
}

void
mutex_exit(kmutex_t *mp)
{
	__coverity_exclusive_lock_release__(mp);
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	(void) rw;

	if (condition0)
		__coverity_sleep__();

	__coverity_recursive_lock_acquire__(rwlp);
}

void
rw_exit(krwlock_t *rwlp)
{
	__coverity_recursive_lock_release__(rwlp);

}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	if (condition0) {
		__coverity_recursive_lock_acquire__(rwlp);
		return (1);
	}

	return (0);
}

/* Thus, we fallback to the Linux kernel locks */
struct {} mutex;
struct {} rw_semaphore;

void
mutex_lock(struct mutex *lock)
{
	if (condition0) {
		__coverity_sleep__();
	}
	__coverity_exclusive_lock_acquire__(lock);
}

void
mutex_unlock(struct mutex *lock)
{
	__coverity_exclusive_lock_release__(lock);
}

void
down_read(struct rw_semaphore *sem)
{
	if (condition0) {
		__coverity_sleep__();
	}
	__coverity_recursive_lock_acquire__(sem);
}

void
down_write(struct rw_semaphore *sem)
{
	if (condition0) {
		__coverity_sleep__();
	}
	__coverity_recursive_lock_acquire__(sem);
}

int
down_read_trylock(struct rw_semaphore *sem)
{
	if (condition0) {
		__coverity_recursive_lock_acquire__(sem);
		return (1);
	}

	return (0);
}

int
down_write_trylock(struct rw_semaphore *sem)
{
	if (condition0) {
		__coverity_recursive_lock_acquire__(sem);
		return (1);
	}

	return (0);
}

void
up_read(struct rw_semaphore *sem)
{
	__coverity_recursive_lock_release__(sem);
}

void
up_write(struct rw_semaphore *sem)
{
	__coverity_recursive_lock_release__(sem);
}

int
__cond_resched(void)
{
	if (condition0) {
		__coverity_sleep__();
	}
}

/*
 * An endian-independent filesystem must support doing byte swaps on data. We
 * attempt to suppress taint warnings, which are false positives for us.
 */
void
byteswap_uint64_array(void *vbuf, size_t size)
{
	__coverity_tainted_data_sanitize__(vbuf);
}

void
byteswap_uint32_array(void *vbuf, size_t size)
{
	__coverity_tainted_data_sanitize__(vbuf);
}

void
byteswap_uint16_array(void *vbuf, size_t size)
{
	__coverity_tainted_data_sanitize__(vbuf);
}
