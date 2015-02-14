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
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 */

#include <sys/abd.h>
#include <sys/zio.h>
#ifdef _KERNEL
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/kmap_compat.h>

#else	/* _KERNEL */

/*
 * Userspace compatibility layer
 */

/*
 * page
 */
#ifndef PAGE_SIZE
#define	PAGE_SIZE 4096
#endif

struct page;

#define	alloc_page(gfp) \
	((struct page *)umem_alloc_aligned(PAGE_SIZE, PAGE_SIZE, UMEM_DEFAULT))

#define	__free_page(page) \
	umem_free(page, PAGE_SIZE)

/*
 * scatterlist
 */
struct scatterlist {
	struct page *page;
	int length;
	int end;
};

static void
sg_init_table(struct scatterlist *sg, int nr) {
	memset(sg, 0, nr * sizeof (struct scatterlist));
	sg[nr - 1].end = 1;
}

static inline void
sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len,
    unsigned int offset) {
	/* currently we don't use offset */
	ASSERT(offset == 0);
	sg->page = page;
	sg->length = len;
}

static inline struct page *
sg_page(struct scatterlist *sg) {
	return (sg->page);
}

static inline struct scatterlist *
sg_next(struct scatterlist *sg)
{
	if (sg->end)
		return (NULL);
	return (sg + 1);
}

/*
 * misc
 */
#ifndef DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)		(((n) + (d) - 1) / (d))
#endif

#ifndef unlikely
#define	unlikely(x)			(x)
#endif

#define	kmap(page)			((void *)page)
#define	kunmap(page)			do { } while (0)
#define	zfs_kmap_atomic(page, type)	((void *)page)
#define	zfs_kunmap_atomic(addr, type)	do { } while (0)
#define	pagefault_disable()		do { } while (0)
#define	pagefault_enable()		do { } while (0)
#define	flush_kernel_dcache_page(page)	do { } while (0)
#define	set_current_state(state)	do { } while (0)
static inline long
schedule_timeout(long timeout)
{
	sleep(timeout);
	return (0);
}

#endif	/* _KERNEL */


struct abd_miter {
	void *addr;		/* mapped addr, adjusted by offset */
	int length;		/* current segment length, adjusted by offset */
	int offset;		/* offset in current segment */
	int is_linear;		/* the type of the abd */
	union {
		struct scatterlist *sg;
		void *buf;
	};
	int nents;		/* num of sg entries */
	int rw;			/* r/w access, whether to flush cache */
#ifndef HAVE_1ARG_KMAP_ATOMIC
	int km_type;		/* KM_USER0 or KM_USER1 */
#endif
};

#define	ABD_MITER_W	(1)
#define	ABD_MITER_R	(0)

/*
 * Initialize the abd_miter.
 * Pass ABD_MITER_W to rw if you will write to the abd buffer.
 * Please use abd_miter_init or abd_miter_init2 for one or two iterators
 * respectively, they will setup KM_USERx accordingly.
 */
static void
abd_miter_init_km(struct abd_miter *aiter, abd_t *abd, int rw, int km)
{
	ASSERT(abd->abd_nents != 0);
	aiter->addr = NULL;
	if (ABD_IS_LINEAR(abd)) {
		ASSERT(abd->abd_nents == 1);
		aiter->is_linear = 1;
		aiter->buf = abd->abd_buf;
		aiter->length = abd->abd_size;
	} else {
		aiter->is_linear = 0;
		aiter->sg = abd->abd_sgl;
		aiter->length = aiter->sg->length - abd->abd_offset;
	}
	aiter->offset = abd->abd_offset;
	aiter->nents = abd->abd_nents;
	aiter->rw = rw;
#ifndef HAVE_1ARG_KMAP_ATOMIC
	aiter->km_type = km;
#endif
}


#define	abd_miter_init(a, abd, rw)	abd_miter_init_km(a, abd, rw, 0)
#define	abd_miter_init2(a, aabd, arw, b, babd, brw)	\
do {							\
	abd_miter_init_km(a, aabd, arw, 0);		\
	abd_miter_init_km(b, babd, brw, 1);		\
} while (0);

/*
 * Map the current page in abd_miter.
 * Pass 1 to atmoic if you want to use kmap_atomic.
 * This can be safely called when the aiter has already exhausted, in which
 * case this does nothing.
 * The mapped address and length will be aiter->addr and aiter->length.
 */
static void
abd_miter_map_x(struct abd_miter *aiter, int atomic)
{
	void *paddr;

	ASSERT(!aiter->addr);

	if (!aiter->nents)
		return;

	if (aiter->is_linear) {
		paddr = aiter->buf;
		/*
		 * Turn of pagefault to keep the context the same as
		 * kmap_atomic.
		 */
		if (atomic)
			pagefault_disable();
	} else {
		ASSERT(aiter->length == aiter->sg->length - aiter->offset);

		if (atomic)
			paddr = zfs_kmap_atomic(sg_page(aiter->sg),
			    (aiter->km_type ? KM_USER1 : KM_USER0));
		else
			paddr = kmap(sg_page(aiter->sg));
	}
	aiter->addr = paddr + aiter->offset;
}

/*
 * Unmap the current page in abd_miter.
 * Pass 1 to atmoic if you want to use kmap_atomic.
 * This can be safely called when the aiter has already exhausted, in which
 * case this does nothing.
 */
static void
abd_miter_unmap_x(struct abd_miter *aiter, int atomic)
{
	void *paddr;

	if (!aiter->nents)
		return;

	ASSERT(aiter->addr);

	if (aiter->is_linear) {
		pagefault_enable();
	} else {
		paddr = aiter->addr - aiter->offset;
		if (atomic) {
			if (aiter->rw == ABD_MITER_W)
				flush_kernel_dcache_page(sg_page(aiter->sg));
			zfs_kunmap_atomic(paddr,
			    (aiter->km_type ? KM_USER1 : KM_USER0));
		} else {
			kunmap(sg_page(aiter->sg));
		}
	}
	aiter->addr = NULL;
}

#define	abd_miter_map_atomic(a)		abd_miter_map_x(a, 1)
#define	abd_miter_map(a)		abd_miter_map_x(a, 0)
#define	abd_miter_unmap_atomic(a)	abd_miter_unmap_x(a, 1)
#define	abd_miter_unmap(a)		abd_miter_unmap_x(a, 0)

/*
 * Use abd_miter_{,un}map_atomic2 if you want to map 2 abd_miters.
 * You need to pass the arguments in the same order for these two.
 */
#define	abd_miter_map_atomic2(a, b)	\
do {					\
	abd_miter_map_atomic(a);	\
	abd_miter_map_atomic(b);	\
} while (0)

#define	abd_miter_unmap_atomic2(a, b)	\
do {					\
	abd_miter_unmap_atomic(b);	\
	abd_miter_unmap_atomic(a);	\
} while (0)

/*
 * Advance the iterator by offset.
 * Cannot be called when a page is mapped.
 * Returns 0 if exhausted.
 * This can be safely called when the aiter has already exhausted, in which
 * case this does nothing.
 */
static int
abd_miter_advance(struct abd_miter *aiter, int offset)
{
	ASSERT(!aiter->addr);

	if (!aiter->nents)
		return (0);

	aiter->offset += offset;
	if (aiter->is_linear) {
		aiter->length -= offset;
		if (aiter->length <= 0) {
			aiter->nents--;
			aiter->length = 0;
			return (0);
		}
	} else {
		while (aiter->offset >= aiter->sg->length) {
			aiter->offset -= aiter->sg->length;
			aiter->nents--;
			aiter->sg = sg_next(aiter->sg);
			if (!aiter->nents) {
				aiter->length = 0;
				return (0);
			}
		}
		aiter->length = aiter->sg->length - aiter->offset;
	}
	return (1);
}

#define	ABD_CHECK(abd)					\
(							\
{							\
	ASSERT((abd)->abd_magic == ARC_BUF_DATA_MAGIC);	\
	ASSERT((abd)->abd_size > 0);			\
	if (ABD_IS_LINEAR(abd)) {			\
		ASSERT((abd)->abd_offset == 0);		\
		ASSERT((abd)->abd_nents == 1);		\
	} else {					\
		ASSERT((abd)->abd_offset < PAGE_SIZE);	\
		ASSERT((abd)->abd_nents > 0);		\
	}						\
}							\
)

static void
abd_iterate_func(abd_t *abd, size_t size,
    int (*func)(void *, uint64_t, void *), void *private, int rw)
{
	size_t len;
	int stop;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size);

	abd_miter_init(&aiter, abd, rw);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);
		/*
		 * The iterated function likely will not do well if each
		 * segment except the last one is not multiple of 16.
		 */
		ASSERT(size == len || (len & 15) == 0);

		abd_miter_map_atomic(&aiter);

		stop = func(aiter.addr, len, private);

		abd_miter_unmap_atomic(&aiter);

		if (stop)
			break;
		size -= len;
		abd_miter_advance(&aiter, len);
	}
}

/*
 * Iterate over ABD and call a read function @func.
 * @func should be implemented so that its behaviour is the same when taking
 * linear and when taking scatter
 */
void
abd_iterate_rfunc(abd_t *abd, size_t size,
    int (*func)(const void *, uint64_t, void *), void *private)
{
	/* skip type checking on func */
	abd_iterate_func(abd, size, (void *)func, private, ABD_MITER_R);
}

/*
 * Iterate over ABD and call a write function @func.
 * @func should be implemented so that its behaviour is the same when taking
 * linear and when taking scatter
 */
void
abd_iterate_wfunc(abd_t *abd, size_t size,
    int (*func)(void *, uint64_t, void *), void *private)
{
	abd_iterate_func(abd, size, func, private, ABD_MITER_W);
}

/*
 * Iterate over two ABD and call @func2.
 * @func2 should be implemented so that its behaviour is the same when taking
 * linear and when taking scatter
 */
void
abd_iterate_func2(abd_t *dabd, abd_t *sabd, size_t dsize, size_t ssize,
    int (*func2)(void *, void *, uint64_t, uint64_t, void *), void *private)
{
	size_t dlen, slen;
	int stop;
	struct abd_miter daiter, saiter;

	ABD_CHECK(dabd);
	ABD_CHECK(sabd);

	ASSERT(dsize <= dabd->abd_size);
	ASSERT(ssize <= sabd->abd_size);

	abd_miter_init2(&daiter, dabd, ABD_MITER_W,
			&saiter, sabd, ABD_MITER_W);

	while (dsize > 0 || ssize > 0) {
		dlen = MIN(daiter.length, dsize);
		slen = MIN(saiter.length, ssize);

		/* there are remainings after this run, use equal len */
		if (dsize > dlen || ssize > slen) {
			if (MIN(dlen, slen) > 0)
				slen = dlen = MIN(dlen, slen);
		}

		/* must be progressive */
		ASSERT(dlen > 0 || slen > 0);
		/*
		 * The iterated function likely will not do well if each
		 * segment except the last one is not multiple of 16.
		 */
		ASSERT(dsize == dlen || (dlen & 15) == 0);
		ASSERT(ssize == slen || (slen & 15) == 0);

		abd_miter_map_atomic2(&daiter, &saiter);

		stop = func2(daiter.addr, saiter.addr, dlen, slen, private);

		abd_miter_unmap_atomic2(&daiter, &saiter);

		if (stop)
			break;

		dsize -= dlen;
		ssize -= slen;
		abd_miter_advance(&daiter, dlen);
		abd_miter_advance(&saiter, slen);
	}
}

/*
 * Copy from @sabd to @dabd
 * @doff is offset in dabd
 * @soff is offset in sabd
 */
void
abd_copy_off(abd_t *dabd, abd_t *sabd, size_t size, size_t doff,
    size_t soff)
{
	size_t len;
	struct abd_miter daiter, saiter;

	ABD_CHECK(dabd);
	ABD_CHECK(sabd);

	ASSERT(size <= dabd->abd_size);
	ASSERT(size <= sabd->abd_size);

	abd_miter_init2(&daiter, dabd, ABD_MITER_W,
			&saiter, sabd, ABD_MITER_R);
	abd_miter_advance(&daiter, doff);
	abd_miter_advance(&saiter, soff);

	while (size > 0) {
		len = MIN(daiter.length, size);
		len = MIN(len, saiter.length);
		ASSERT(len > 0);

		abd_miter_map_atomic2(&daiter, &saiter);

		memcpy(daiter.addr, saiter.addr, len);

		abd_miter_unmap_atomic2(&daiter, &saiter);

		size -= len;
		abd_miter_advance(&daiter, len);
		abd_miter_advance(&saiter, len);
	}
}

/*
 * Copy from @buf to @abd
 * @off is the offset in @abd
 */
void
abd_copy_from_buf_off(abd_t *abd, const void *buf, size_t size,
    size_t off)
{
	size_t len;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(&aiter);

		memcpy(aiter.addr, buf, len);

		abd_miter_unmap_atomic(&aiter);

		size -= len;
		buf += len;
		abd_miter_advance(&aiter, len);
	}
}

/*
 * Copy from @abd to @buf
 * @off is the offset in @abd
 */
void
abd_copy_to_buf_off(void *buf, abd_t *abd, size_t size, size_t off)
{
	size_t len;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_R);
	abd_miter_advance(&aiter, off);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(&aiter);

		memcpy(buf, aiter.addr, len);

		abd_miter_unmap_atomic(&aiter);

		size -= len;
		buf += len;
		abd_miter_advance(&aiter, len);
	}
}

/*
 * Compare between @dabd and @sabd.
 */
int
abd_cmp(abd_t *dabd, abd_t *sabd, size_t size)
{
	size_t len;
	int ret = 0;
	struct abd_miter daiter, saiter;

	ABD_CHECK(dabd);
	ABD_CHECK(sabd);
	ASSERT(size <= dabd->abd_size);
	ASSERT(size <= sabd->abd_size);

	abd_miter_init2(&daiter, dabd, ABD_MITER_R,
			&saiter, sabd, ABD_MITER_R);

	while (size > 0) {
		len = MIN(daiter.length, size);
		len = MIN(len, saiter.length);
		ASSERT(len > 0);

		abd_miter_map_atomic2(&daiter, &saiter);

		ret = memcmp(daiter.addr, saiter.addr, len);

		abd_miter_unmap_atomic2(&daiter, &saiter);

		if (ret)
			break;

		size -= len;
		abd_miter_advance(&daiter, len);
		abd_miter_advance(&saiter, len);
	}
	return (ret);
}

/*
 * Compare between @abd and @buf.
 * @off is the offset in @abd
 */
int
abd_cmp_buf_off(abd_t *abd, const void *buf, size_t size, size_t off)
{
	size_t len;
	int ret = 0;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_R);
	abd_miter_advance(&aiter, off);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(&aiter);

		ret = memcmp(aiter.addr, buf, len);

		abd_miter_unmap_atomic(&aiter);

		if (ret)
			break;

		size -= len;
		buf += len;
		abd_miter_advance(&aiter, len);
	}
	return (ret);
}

/*
 * Zero out @abd.
 * @off is the offset in @abd
 */
void
abd_zero_off(abd_t *abd, size_t size, size_t off)
{
	size_t len;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(&aiter);

		memset(aiter.addr, 0, len);

		abd_miter_unmap_atomic(&aiter);

		size -= len;
		abd_miter_advance(&aiter, len);
	}
}

#ifdef _KERNEL
/*
 * Copy from @abd to user buffer @buf.
 * @off is the offset in @abd
 */
int
abd_copy_to_user_off(void __user *buf, abd_t *abd, size_t size,
    size_t off)
{
	int ret = 0;
	size_t len;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_R);
	abd_miter_advance(&aiter, off);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(&aiter);

		ret = __copy_to_user_inatomic(buf, aiter.addr, len);

		abd_miter_unmap_atomic(&aiter);
		if (ret) {
			abd_miter_map(&aiter);
			ret = copy_to_user(buf, aiter.addr, len);
			abd_miter_unmap(&aiter);
			if (ret)
				break;
		}

		size -= len;
		buf += len;
		abd_miter_advance(&aiter, len);
	}
	return (ret ? EFAULT : 0);
}

/*
 * Copy from user buffer @buf to @abd.
 * @off is the offset in @abd
 */
int
abd_copy_from_user_off(abd_t *abd, const void __user *buf, size_t size,
    size_t off)
{
	int ret = 0;
	size_t len;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	while (size > 0) {
		len = MIN(aiter.length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(&aiter);

		ret = __copy_from_user_inatomic(aiter.addr, buf, len);

		abd_miter_unmap_atomic(&aiter);
		if (ret) {
			abd_miter_map(&aiter);
			ret = copy_from_user(aiter.addr, buf, len);
			abd_miter_unmap(&aiter);
			if (ret)
				break;
		}

		size -= len;
		buf += len;
		abd_miter_advance(&aiter, len);
	}
	return (ret ? EFAULT : 0);
}

/*
 * uiomove for ABD.
 * @off is the offset in @abd
 */
int
abd_uiomove_off(abd_t *abd, size_t n, enum uio_rw rw, uio_t *uio,
    size_t off)
{
	struct iovec *iov;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = MIN(iov->iov_len, n);
		if (cnt == 0l) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
		case UIO_USERISPACE:
			/*
			 * p = kernel data pointer
			 * iov->iov_base = user data pointer
			 */
			if (rw == UIO_READ) {
				if (abd_copy_to_user_off(iov->iov_base,
				    abd, cnt, off))
					return (EFAULT);
			} else {
				if (abd_copy_from_user_off(abd,
				    iov->iov_base, cnt, off))
					return (EFAULT);
			}
			break;
		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				abd_copy_to_buf_off(iov->iov_base, abd,
				    cnt, off);
			else
				abd_copy_from_buf_off(abd, iov->iov_base,
				    cnt, off);
			break;
		}
		iov->iov_base += cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		off += cnt;
		n -= cnt;
	}
	return (0);
}

/*
 * uiocopy for ABD.
 * @off is the offset in @abd
 */
int
abd_uiocopy_off(abd_t *abd, size_t n, enum uio_rw rw, uio_t *uio,
    size_t *cbytes, size_t off)
{
	struct iovec *iov;
	ulong_t cnt;
	int iovcnt;

	iovcnt = uio->uio_iovcnt;
	*cbytes = 0;

	for (iov = uio->uio_iov; n && iovcnt; iov++, iovcnt--) {
		cnt = MIN(iov->iov_len, n);
		if (cnt == 0)
			continue;

		switch (uio->uio_segflg) {

		case UIO_USERSPACE:
		case UIO_USERISPACE:
			/*
			 * p = kernel data pointer
			 * iov->iov_base = user data pointer
			 */
			if (rw == UIO_READ) {
				/* UIO_READ = copy data from kernel to user */
				if (abd_copy_to_user_off(iov->iov_base,
				    abd, cnt, off))
					return (EFAULT);
			} else {
				/* UIO_WRITE = copy data from user to kernel */
				if (abd_copy_from_user_off(abd,
				    iov->iov_base, cnt, off))
					return (EFAULT);
			}
			break;

		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				abd_copy_to_buf_off(iov->iov_base, abd,
				    cnt, off);
			else
				abd_copy_from_buf_off(abd, iov->iov_base,
				    cnt, off);
			break;
		}
		off += cnt;
		n -= cnt;
		*cbytes += cnt;
	}
	return (0);
}

/*
 * bio_map for scatter ABD.
 * @off is the offset in @abd
 * You should use abd_bio_map_off, it will choose the right function according
 * to the ABD type.
 */
unsigned int
abd_scatter_bio_map_off(struct bio *bio, abd_t *abd, unsigned int bio_size,
    size_t off)
{
	int i;
	size_t len;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT_ABD_SCATTER(abd);
	ASSERT(bio_size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_R);
	abd_miter_advance(&aiter, off);

	for (i = 0; i < bio->bi_max_vecs; i++) {
		if (bio_size <= 0)
			break;

		len = MIN(bio_size, aiter.length);
		ASSERT(len > 0);

		if (bio_add_page(bio, sg_page(aiter.sg), len,
		    aiter.offset) != len)
			break;

		bio_size -= len;
		abd_miter_advance(&aiter, len);
	}
	return (bio_size);
}

/*
 * bio_nr_pages for ABD.
 * @off is the offset in @abd
 */
unsigned long
abd_bio_nr_pages_off(abd_t *abd, unsigned int bio_size, size_t off)
{
	unsigned long pos = 0;
	ABD_CHECK(abd);

	if (ABD_IS_LINEAR(abd))
		pos = (unsigned long)abd->abd_buf + off;
	else
		pos = abd->abd_offset + off;
	return ((pos + bio_size + PAGE_SIZE-1)>>PAGE_SHIFT)-(pos>>PAGE_SHIFT);
}
#endif	/* _KERNEL */

static inline abd_t *
abd_alloc_struct(int nr_pages)
{
	abd_t *abd;
	size_t asize = sizeof (abd_t) + nr_pages*sizeof (struct scatterlist);
	/*
	 * If the maximum block size increases, inline sgl might not fit into
	 * a single page. We might want to consider using chained sgl if
	 * that's the case.
	 */
	ASSERT(nr_pages * sizeof (struct scatterlist) <= PAGE_SIZE);
#ifndef DEBUG_ABD
	abd = kmem_alloc(asize, KM_PUSHPAGE);
#else
	abd = umem_alloc_aligned(asize, PAGE_SIZE, UMEM_DEFAULT);
	/* deny access to padding */
	if (mprotect(abd, PAGE_SIZE, PROT_NONE) != 0) {
		perror("mprotect failed");
		ASSERT(0);
	}
#endif
	ASSERT(abd);

	return (abd);
}

static inline void
abd_free_struct(abd_t *abd, int nr_pages)
{
#ifndef DEBUG_ABD
	kmem_free(abd, sizeof (abd_t) + nr_pages*sizeof (struct scatterlist));
#else
	if (mprotect(abd, PAGE_SIZE, PROT_READ|PROT_WRITE) != 0) {
		perror("mprotect failed");
		ASSERT(0);
	}
	umem_free(abd, sizeof (abd_t) + nr_pages*sizeof (struct scatterlist));
#endif
}

/*
 * Allocate a new ABD to point to offset @off of the original ABD.
 * It shares the underlying buffer with the original ABD.
 * Use abd_put to free. The original ABD(allocated from abd_alloc) must
 * not be freed before any of its derived ABD.
 */
abd_t *
abd_get_offset(abd_t *sabd, size_t off)
{
	size_t offset;
	abd_t *abd;

	ABD_CHECK(sabd);
	ASSERT(off <= sabd->abd_size);

	abd = abd_alloc_struct(0);

	abd->abd_magic = ARC_BUF_DATA_MAGIC;
	abd->abd_size = sabd->abd_size - off;

	if (ABD_IS_LINEAR(sabd)) {
		abd->abd_flags = ABD_F_LINEAR;
		abd->abd_offset = 0;
		abd->abd_nents = 1;
		abd->abd_buf = sabd->abd_buf + off;
	} else {
		abd->abd_flags = ABD_F_SCATTER;
		offset = sabd->abd_offset + off;
		abd->abd_offset = offset & (PAGE_SIZE - 1);
		/* make sure the new abd start as sgl[0] */
		abd->abd_sgl = &sabd->abd_sgl[offset >> PAGE_SHIFT];
		abd->abd_nents = sabd->abd_nents - (offset >> PAGE_SHIFT);
	}

	return (abd);
}

/*
 * Allocate a linear ABD structure for @buf
 * Use abd_put to free.
 */
abd_t *
abd_get_from_buf(void *buf, size_t size)
{
	abd_t *abd;

	abd = abd_alloc_struct(0);

	abd->abd_magic = ARC_BUF_DATA_MAGIC;
	abd->abd_flags = ABD_F_LINEAR;
	abd->abd_size = size;
	abd->abd_offset = 0;
	abd->abd_nents = 1;
	abd->abd_buf = buf;

	return (abd);
}

/*
 * Free an ABD allocated from abd_get_{offset,from_buf}.
 * Must not be used on ABD from elsewhere.
 * Will not free the underlying scatterlist or buffer.
 */
void
abd_put(abd_t *abd)
{
	ABD_CHECK(abd);
	ASSERT(!(abd->abd_flags & ABD_F_OWNER));

	abd->abd_magic = 0;
	abd_free_struct(abd, 0);
}

/*
 * Allocate a scatter ABD
 */
abd_t *
abd_alloc_scatter(size_t size)
{
	abd_t *abd;
	struct page *page;
	int i, n = DIV_ROUND_UP(size, PAGE_SIZE);
	size_t last_size = size - ((n-1) << PAGE_SHIFT);

	abd = abd_alloc_struct(n);

	abd->abd_magic = ARC_BUF_DATA_MAGIC;
	abd->abd_flags = ABD_F_SCATTER|ABD_F_OWNER;
	abd->abd_size = size;
	abd->abd_offset = 0;
	abd->abd_nents = n;
	abd->abd_sgl = (struct scatterlist *)&abd->__abd_sgl[0];
	sg_init_table(abd->abd_sgl, n);

	for (i = 0; i < n; i++) {
retry:
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (unlikely(page == NULL)) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(1);
			goto retry;
		}
		sg_set_page(&abd->abd_sgl[i], page,
		    (i == n-1 ? last_size : PAGE_SIZE), 0);
	}

	return (abd);
}

/*
 * Allocate a linear ABD
 */
abd_t *
abd_alloc_linear(size_t size)
{
	abd_t *abd;

	abd = abd_alloc_struct(0);

	abd->abd_magic = ARC_BUF_DATA_MAGIC;
	abd->abd_flags = ABD_F_LINEAR|ABD_F_OWNER;
	abd->abd_size = size;
	abd->abd_offset = 0;
	abd->abd_nents = 1;

	abd->abd_buf = zio_buf_alloc(size);

	return (abd);
}

static void
abd_free_scatter(abd_t *abd, size_t size)
{
	int i, n;
	struct page *page;

	ASSERT(abd->abd_sgl == (struct scatterlist *)&abd->__abd_sgl[0]);
	ASSERT(abd->abd_size == size);
	ASSERT(abd->abd_nents == DIV_ROUND_UP(abd->abd_size, PAGE_SIZE));

	n = abd->abd_nents;
	abd->abd_magic = 0;
	for (i = 0; i < n; i++) {
		page = sg_page(&abd->abd_sgl[i]);
		if (page)
			__free_page(page);
	}
	abd_free_struct(abd, n);
}

static void
abd_free_linear(abd_t *abd, size_t size)
{
	abd->abd_magic = 0;
	zio_buf_free(abd->abd_buf, size);
	abd_free_struct(abd, 0);
}

/*
 * Free a ABD.
 * Only use this on ABD allocated with abd_alloc_{scatter,linear}.
 */
void
abd_free(abd_t *abd, size_t size)
{
	ABD_CHECK(abd);
	ASSERT(abd->abd_flags & ABD_F_OWNER);
	if (ABD_IS_LINEAR(abd))
		abd_free_linear(abd, size);
	else
		abd_free_scatter(abd, size);
}
