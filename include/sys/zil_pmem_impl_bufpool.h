#ifndef _ZIL_PMEM_IMPL_BUFPOOL_H_
#define _ZIL_PMEM_IMPL_BUFPOOL_H_

#include <sys/zfs_context.h>

typedef struct zfs_bufpool
{
	kmutex_t mtx;
	kcondvar_t cv;
	size_t size;
	size_t nbufs;
	boolean_t *taken;
	void **bufs;
} zfs_bufpool_t;

typedef struct zfs_bufpool_buf_ref
{
	zfs_bufpool_t *pool;
	int idx;
	void *buf;
	size_t size;
} zfs_bufpool_buf_ref_t;

static void inline zfs_bufpool_ctor(zfs_bufpool_t *lb, size_t size)
{
	VERIFY3P(lb->bufs, ==, NULL);
	mutex_init(&lb->mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&lb->cv, NULL, CV_DEFAULT, NULL);
	lb->size = size;
	lb->nbufs = max_ncpus;

	lb->taken = kmem_zalloc(lb->nbufs * sizeof(void *), KM_SLEEP);
	lb->bufs = kmem_zalloc(lb->nbufs * sizeof(void *), KM_SLEEP);
	for (size_t i = 0; i < lb->nbufs; i++)
	{
		lb->bufs[i] = vmem_alloc(lb->size, KM_SLEEP);
#ifdef KERNEL
		pr_debug("zfs_bufpool_t 0x%px : allocating buffer n=%ld  addr=0x%px\n", lb, i, lb->bufs[i]);
#endif
	}
}

static void inline zfs_bufpool_dtor(zfs_bufpool_t *lb)
{
	VERIFY(lb->bufs);
	mutex_destroy(&lb->mtx); // destroy ealry so that we crash if still held
	cv_destroy(&lb->cv);
	for (size_t i = 0; i < lb->nbufs; i++)
	{
		VERIFY(!lb->taken[i]);
#ifdef KERNEL
		pr_debug("zfs_bufpool_t 0x%px : freeing buffer n=%ld  addr=0x%px\n", lb, i, lb->bufs[i]);
#endif
		VERIFY(lb->bufs[i]);
		vmem_free(lb->bufs[i], lb->size);
	}
	kmem_free(lb->bufs, lb->nbufs * sizeof(void *));
	kmem_free(lb->taken, lb->nbufs * sizeof(void *));
	lb->bufs = NULL;
}

static inline void
zfs_bufpool_get_ref(zfs_bufpool_t *lb, zfs_bufpool_buf_ref_t *out)
{
	ASSERT(lb);
	ASSERT(lb->bufs);
	ASSERT(out);

	mutex_enter(&lb->mtx);

retry:
	(void)0;
	size_t find_idx = CPU_SEQID_UNSTABLE % lb->nbufs;
	const size_t start_idx = find_idx;
	size_t found = lb->nbufs;
	do
	{
		if (!lb->taken[find_idx])
		{
			found = find_idx;
			break;
		}
		find_idx = (find_idx + 1) % lb->nbufs;
	} while (find_idx != start_idx);

	/* what to do if full */
	if (found == lb->nbufs)
	{
		cv_wait(&lb->cv, &lb->mtx);
		goto retry;
	}

	ASSERT(!lb->taken[found]);
	lb->taken[found] = B_TRUE;

	out->pool = lb;
	out->buf = lb->bufs[found];
	out->size = lb->size;
	out->idx = found;

	mutex_exit(&lb->mtx);
}

static inline void zfs_bufpool_put(zfs_bufpool_buf_ref_t *ref)
{
	ASSERT(ref);
	ASSERT(ref->pool);
	zfs_bufpool_t *lb = ref->pool;

	mutex_enter(&lb->mtx);

	ASSERT3U(ref->idx, <, lb->nbufs);
	ASSERT(lb->taken[ref->idx]);
	ASSERT3P(lb->bufs[ref->idx], ==, ref->buf);

	lb->taken[ref->idx] = B_FALSE;
	cv_broadcast(&lb->cv);

	mutex_exit(&lb->mtx);

	memset(ref, 0, sizeof(*ref));
	ref->pool = (void *)0x1;
	ref->buf = (void *)0x1;
}

#endif /* _ZIL_PMEM_IMPL_BUFPOOL_H_ */
