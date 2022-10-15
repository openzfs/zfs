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

typedef enum {
	B_FALSE = 0,
	B_TRUE = 1
} boolean_t;

typedef unsigned int uint_t;

int condition0, condition1;

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	(void) flags;
	__coverity_negative_sink__(len);
	__coverity_tainted_data_argument__(from);
	__coverity_tainted_data_argument__(to);
	__coverity_writeall__(to);
}

void *
memset(void *dst, int c, size_t len)
{
	__coverity_negative_sink__(len);
	if (c == 0)
		__coverity_writeall0__(dst);
	else
		__coverity_writeall__(dst);
	return (dst);
}

void *
memmove(void *dst, void *src, size_t len)
{
	int first = ((char *)src)[0];
	int last = ((char *)src)[len-1];

	__coverity_negative_sink__(len);
	__coverity_writeall__(dst);
	return (dst);
}

void *
memcpy(void *dst, void *src, size_t len)
{
	int first = ((char *)src)[0];
	int last = ((char *)src)[len-1];

	__coverity_negative_sink__(len);
	__coverity_writeall__(dst);
	return (dst);
}

void *
umem_alloc_aligned(size_t size, size_t align, int kmflags)
{
	__coverity_negative_sink__(size);
	__coverity_negative_sink__(align);

	if (((UMEM_NOFAIL & kmflags) == UMEM_NOFAIL) || condition0) {
		void *buf = __coverity_alloc__(size);
		__coverity_mark_as_uninitialized_buffer__(buf);
		__coverity_mark_as_afm_allocated__(buf, "umem_free");
		return (buf);
	}

	return (NULL);
}

void *
umem_alloc(size_t size, int kmflags)
{
	__coverity_negative_sink__(size);

	if (((UMEM_NOFAIL & kmflags) == UMEM_NOFAIL) || condition0) {
		void *buf = __coverity_alloc__(size);
		__coverity_mark_as_uninitialized_buffer__(buf);
		__coverity_mark_as_afm_allocated__(buf, "umem_free");
		return (buf);
	}

	return (NULL);
}

void *
umem_zalloc(size_t size, int kmflags)
{
	__coverity_negative_sink__(size);

	if (((UMEM_NOFAIL & kmflags) == UMEM_NOFAIL) || condition0) {
		void *buf = __coverity_alloc__(size);
		__coverity_writeall0__(buf);
		__coverity_mark_as_afm_allocated__(buf, "umem_free");
		return (buf);
	}

	return (NULL);
}

void
umem_free(void *buf, size_t size)
{
	__coverity_negative_sink__(size);
	__coverity_free__(buf);
}

typedef struct {} umem_cache_t;

void *
umem_cache_alloc(umem_cache_t *skc, int flags)
{
	(void) skc;

	if (condition1)
		__coverity_sleep__();

	if (((UMEM_NOFAIL & flags) == UMEM_NOFAIL) || condition0) {
		void *buf = __coverity_alloc_nosize__();
		__coverity_mark_as_uninitialized_buffer__(buf);
		__coverity_mark_as_afm_allocated__(buf, "umem_cache_free");
		return (buf);
	}

	return (NULL);
}

void
umem_cache_free(umem_cache_t *skc, void *obj)
{
	(void) skc;

	__coverity_free__(obj);
}

void *
spl_kmem_alloc(size_t sz, int fl, const char *func, int line)
{
	(void) func;
	(void) line;

	__coverity_negative_sink__(sz);

	if (condition1)
		__coverity_sleep__();

	if ((fl == 0) || condition0) {
		void *buf = __coverity_alloc__(sz);
		__coverity_mark_as_uninitialized_buffer__(buf);
		__coverity_mark_as_afm_allocated__(buf, "spl_kmem_free");
		return (buf);
	}

	return (NULL);
}

void *
spl_kmem_zalloc(size_t sz, int fl, const char *func, int line)
{
	(void) func;
	(void) line;

	__coverity_negative_sink__(sz);

	if (condition1)
		__coverity_sleep__();

	if ((fl == 0) || condition0) {
		void *buf = __coverity_alloc__(sz);
		__coverity_writeall0__(buf);
		__coverity_mark_as_afm_allocated__(buf, "spl_kmem_free");
		return (buf);
	}

	return (NULL);
}

void
spl_kmem_free(const void *ptr, size_t sz)
{
	__coverity_negative_sink__(sz);
	__coverity_free__(ptr);
}

char *
kmem_vasprintf(const char *fmt, va_list ap)
{
	char *buf = __coverity_alloc_nosize__();
	(void) ap;

	__coverity_string_null_sink__(fmt);
	__coverity_string_size_sink__(fmt);

	__coverity_writeall__(buf);

	__coverity_mark_as_afm_allocated__(buf, "kmem_strfree");

	return (buf);
}

char *
kmem_asprintf(const char *fmt, ...)
{
	char *buf = __coverity_alloc_nosize__();

	__coverity_string_null_sink__(fmt);
	__coverity_string_size_sink__(fmt);

	__coverity_writeall__(buf);

	__coverity_mark_as_afm_allocated__(buf, "kmem_strfree");

	return (buf);
}

char *
kmem_strdup(const char *str)
{
	char *buf = __coverity_alloc_nosize__();

	__coverity_string_null_sink__(str);
	__coverity_string_size_sink__(str);

	__coverity_writeall__(buf);

	__coverity_mark_as_afm_allocated__(buf, "kmem_strfree");

	return (buf);


}

void
kmem_strfree(char *str)
{
	__coverity_free__(str);
}

void *
spl_vmem_alloc(size_t sz, int fl, const char *func, int line)
{
	(void) func;
	(void) line;

	__coverity_negative_sink__(sz);

	if (condition1)
		__coverity_sleep__();

	if ((fl == 0) || condition0) {
		void *buf = __coverity_alloc__(sz);
		__coverity_mark_as_uninitialized_buffer__(buf);
		__coverity_mark_as_afm_allocated__(buf, "spl_vmem_free");
		return (buf);
	}

	return (NULL);
}

void *
spl_vmem_zalloc(size_t sz, int fl, const char *func, int line)
{
	(void) func;
	(void) line;

	if (condition1)
		__coverity_sleep__();

	if ((fl == 0) || condition0) {
		void *buf = __coverity_alloc__(sz);
		__coverity_writeall0__(buf);
		__coverity_mark_as_afm_allocated__(buf, "spl_vmem_free");
		return (buf);
	}

	return (NULL);
}

void
spl_vmem_free(const void *ptr, size_t sz)
{
	__coverity_negative_sink__(sz);
	__coverity_free__(ptr);
}

typedef struct {} spl_kmem_cache_t;

void *
spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags)
{
	(void) skc;

	if (condition1)
		__coverity_sleep__();

	if ((flags == 0) || condition0) {
		void *buf = __coverity_alloc_nosize__();
		__coverity_mark_as_uninitialized_buffer__(buf);
		__coverity_mark_as_afm_allocated__(buf, "spl_kmem_cache_free");
		return (buf);
	}
}

void
spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj)
{
	(void) skc;

	__coverity_free__(obj);
}

typedef struct {} zfsvfs_t;

int
zfsvfs_create(const char *osname, boolean_t readonly, zfsvfs_t **zfvp)
{
	(void) osname;
	(void) readonly;

	if (condition1)
		__coverity_sleep__();

	if (condition0) {
		*zfvp = __coverity_alloc_nosize__();
		__coverity_writeall__(*zfvp);
		return (0);
	}

	return (1);
}

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	__coverity_free__(zfsvfs);
}

typedef struct {} nvlist_t;

int
nvlist_alloc(nvlist_t **nvlp, uint_t nvflag, int kmflag)
{
	(void) nvflag;

	if (condition1)
		__coverity_sleep__();

	if ((kmflag == 0) || condition0) {
		*nvlp = __coverity_alloc_nosize__();
		__coverity_mark_as_afm_allocated__(*nvlp, "nvlist_free");
		__coverity_writeall__(*nvlp);
		return (0);
	}

	return (-1);

}

int
nvlist_dup(const nvlist_t *nvl, nvlist_t **nvlp, int kmflag)
{
	nvlist_t read = *nvl;

	if (condition1)
		__coverity_sleep__();

	if ((kmflag == 0) || condition0) {
		nvlist_t *nvl = __coverity_alloc_nosize__();
		__coverity_mark_as_afm_allocated__(nvl, "nvlist_free");
		__coverity_writeall__(nvl);
		*nvlp = nvl;
		return (0);
	}

	return (-1);
}

void
nvlist_free(nvlist_t *nvl)
{
	__coverity_free__(nvl);
}

int
nvlist_pack(nvlist_t *nvl, char **bufp, size_t *buflen, int encoding,
    int kmflag)
{
	(void) nvl;
	(void) encoding;

	if (*bufp == NULL) {
		if (condition1)
			__coverity_sleep__();

		if ((kmflag == 0) || condition0) {
			char *buf = __coverity_alloc_nosize__();
			__coverity_writeall__(buf);
			/*
			 * We cannot use __coverity_mark_as_afm_allocated__()
			 * because the free function varies between the kernel
			 * and userspace.
			 */
			*bufp = buf;
			return (0);
		}

		return (-1);
	}

	/*
	 * Unfortunately, errors from the buffer being too small are not
	 * possible to model, so we assume success.
	 */
	__coverity_negative_sink__(*buflen);
	__coverity_writeall__(*bufp);
	return (0);
}


int
nvlist_unpack(char *buf, size_t buflen, nvlist_t **nvlp, int kmflag)
{
	__coverity_negative_sink__(buflen);

	if (condition1)
		__coverity_sleep__();

	if ((kmflag == 0) || condition0) {
		nvlist_t *nvl = __coverity_alloc_nosize__();
		__coverity_mark_as_afm_allocated__(nvl, "nvlist_free");
		__coverity_writeall__(nvl);
		*nvlp = nvl;
		int first = buf[0];
		int last = buf[buflen-1];
		return (0);
	}

	return (-1);

}

void *
malloc(size_t size)
{
	void *buf = __coverity_alloc__(size);

	if (condition1)
		__coverity_sleep__();

	__coverity_negative_sink__(size);
	__coverity_mark_as_uninitialized_buffer__(buf);
	__coverity_mark_as_afm_allocated__(buf, "free");

	return (buf);
}

void *
calloc(size_t nmemb, size_t size)
{
	void *buf = __coverity_alloc__(size * nmemb);

	if (condition1)
		__coverity_sleep__();

	__coverity_negative_sink__(size);
	__coverity_writeall0__(buf);
	__coverity_mark_as_afm_allocated__(buf, "free");
	return (buf);
}
void
free(void *buf)
{
	__coverity_free__(buf);
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
