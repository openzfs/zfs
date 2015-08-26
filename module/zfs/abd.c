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
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
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
#ifndef PAGE_SHIFT
#define	PAGE_SHIFT (highbit64(PAGESIZE)-1)
#endif

struct page;

#define	alloc_page(gfp) \
	((struct page *)umem_alloc_aligned(PAGESIZE, PAGESIZE, UMEM_DEFAULT))

#define	__free_page(page) \
	umem_free(page, PAGESIZE)

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

#define	for_each_sg(sgl, sg, nr, i)	\
	for ((i) = 0, (sg) = (sgl); (i) < (nr); (i)++, (sg) = sg_next(sg))

static inline void
sg_set_page(struct scatterlist *sg, struct page *page, unsigned int len,
    unsigned int offset)
{
	/* currently we don't use offset */
	ASSERT(offset == 0);
	sg->page = page;
	sg->length = len;
}

static inline struct page *
sg_page(struct scatterlist *sg)
{
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
		if (atomic)
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
do {							\
	ASSERT((abd)->abd_magic == ARC_BUF_DATA_MAGIC);	\
	ASSERT((abd)->abd_size > 0);			\
	if (ABD_IS_LINEAR(abd)) {			\
		ASSERT((abd)->abd_offset == 0);		\
		ASSERT((abd)->abd_nents == 1);		\
	} else {					\
		ASSERT((abd)->abd_offset < PAGESIZE);	\
		ASSERT((abd)->abd_nents > 0);		\
	}						\
} while (0)

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
 * Copy from @buf to the ABD indicated by @aiter
 * @aiter will advance @size byte when done
 */
static void
abd_miter_copy_from_buf(struct abd_miter *aiter, const void *buf, size_t size)
{
	size_t len;
	while (size > 0) {
		len = MIN(aiter->length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(aiter);

		memcpy(aiter->addr, buf, len);

		abd_miter_unmap_atomic(aiter);

		size -= len;
		buf += len;
		abd_miter_advance(aiter, len);
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
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	abd_miter_copy_from_buf(&aiter, buf, size);
}

/*
 * Copy from the ABD indicated by @aiter to @buf
 * @aiter will advance @size byte when done
 */
static void
abd_miter_copy_to_buf(void *buf, struct abd_miter *aiter, size_t size)
{
	size_t len;
	while (size > 0) {
		len = MIN(aiter->length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(aiter);

		memcpy(buf, aiter->addr, len);

		abd_miter_unmap_atomic(aiter);

		size -= len;
		buf += len;
		abd_miter_advance(aiter, len);
	}
}

/*
 * Copy from @abd to @buf
 * @off is the offset in @abd
 */
void
abd_copy_to_buf_off(void *buf, abd_t *abd, size_t size, size_t off)
{
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_R);
	abd_miter_advance(&aiter, off);

	abd_miter_copy_to_buf(buf, &aiter, size);
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
static int
abd_miter_copy_to_user(void __user *buf, struct abd_miter *aiter, size_t size)
{
	int ret = 0;
	size_t len;
	while (size > 0) {
		len = MIN(aiter->length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(aiter);

		ret = __copy_to_user_inatomic(buf, aiter->addr, len);

		abd_miter_unmap_atomic(aiter);
		if (ret) {
			abd_miter_map(aiter);
			ret = copy_to_user(buf, aiter->addr, len);
			abd_miter_unmap(aiter);
			if (ret)
				break;
		}

		size -= len;
		buf += len;
		abd_miter_advance(aiter, len);
	}
	return (ret ? EFAULT : 0);
}
/*
 * Copy from @abd to user buffer @buf.
 * @off is the offset in @abd
 */
int
abd_copy_to_user_off(void __user *buf, abd_t *abd, size_t size,
    size_t off)
{
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_R);
	abd_miter_advance(&aiter, off);

	return (abd_miter_copy_to_user(buf, &aiter, size));
}

static int
abd_miter_copy_from_user(struct abd_miter *aiter, const void __user *buf,
    size_t size)
{
	int ret = 0;
	size_t len;
	while (size > 0) {
		len = MIN(aiter->length, size);
		ASSERT(len > 0);

		abd_miter_map_atomic(aiter);

		ret = __copy_from_user_inatomic(aiter->addr, buf, len);

		abd_miter_unmap_atomic(aiter);
		if (ret) {
			abd_miter_map(aiter);
			ret = copy_from_user(aiter->addr, buf, len);
			abd_miter_unmap(aiter);
			if (ret)
				break;
		}

		size -= len;
		buf += len;
		abd_miter_advance(aiter, len);
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
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(size <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	return (abd_miter_copy_from_user(&aiter, buf, size));
}

static int
abd_uiomove_iov_off(abd_t *abd, size_t n, enum uio_rw rw, uio_t *uio,
    size_t off)
{
	const struct iovec *iov = uio->uio_iov;
	size_t skip = uio->uio_skip;
	ulong_t cnt;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(n <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, rw == UIO_READ ? ABD_MITER_R : ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	while (n && uio->uio_resid) {
		void *caddr = iov->iov_base + skip;
		cnt = MIN(iov->iov_len - skip, n);
		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
		case UIO_USERISPACE:
			if (rw == UIO_READ) {
				if (abd_miter_copy_to_user(caddr, &aiter, cnt))
					return (EFAULT);
			} else {
				if (abd_miter_copy_from_user(&aiter, caddr,
				    cnt))
					return (EFAULT);
			}
			break;
		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				abd_miter_copy_to_buf(caddr, &aiter, cnt);
			else
				abd_miter_copy_from_buf(&aiter, caddr, cnt);
			break;
		default:
			ASSERT(0);
		}
		skip += cnt;
		if (skip == iov->iov_len) {
			skip = 0;
			uio->uio_iov = (++iov);
			uio->uio_iovcnt--;
		}
		uio->uio_skip = skip;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		n -= cnt;
	}
	return (0);
}

static int
abd_uiomove_bvec_off(abd_t *abd, size_t n, enum uio_rw rw, uio_t *uio,
    size_t off)
{
	const struct bio_vec *bv = uio->uio_bvec;
	size_t skip = uio->uio_skip;
	ulong_t cnt;
	struct abd_miter aiter;

	ABD_CHECK(abd);
	ASSERT(n <= abd->abd_size - off);

	abd_miter_init(&aiter, abd, rw == UIO_READ ? ABD_MITER_R : ABD_MITER_W);
	abd_miter_advance(&aiter, off);

	while (n && uio->uio_resid) {
		void *paddr, *caddr;
		cnt = MIN(bv->bv_len - skip, n);

		/* miter will use KM_USER0, so here we use KM_USER1 */
		paddr = zfs_kmap_atomic(bv->bv_page, KM_USER1);
		caddr = paddr + bv->bv_offset + skip;
		if (rw == UIO_READ)
			abd_miter_copy_to_buf(caddr, &aiter, cnt);
		else
			abd_miter_copy_from_buf(&aiter, caddr, cnt);
		zfs_kunmap_atomic(paddr, KM_USER1);

		skip += cnt;
		if (skip == bv->bv_len) {
			skip = 0;
			uio->uio_bvec = (++bv);
			uio->uio_iovcnt--;
		}
		uio->uio_skip = skip;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		n -= cnt;
	}
	return (0);
}

/*
 * uiomove for ABD.
 * @off is the offset in @abd
 */
int
abd_uiomove_off(abd_t *abd, size_t n, enum uio_rw rw, uio_t *uio, size_t off)
{
	if (uio->uio_segflg != UIO_BVEC)
		return (abd_uiomove_iov_off(abd, n, rw, uio, off));
	else
		return (abd_uiomove_bvec_off(abd, n, rw, uio, off));
}

/*
 * uiocopy for ABD.
 * @off is the offset in @abd
 */
int
abd_uiocopy_off(abd_t *abd, size_t n, enum uio_rw rw, uio_t *uio,
    size_t *cbytes, size_t off)
{
	struct uio uio_copy;
	int ret;

	bcopy(uio, &uio_copy, sizeof (struct uio));
	ret = abd_uiomove_off(abd, n, rw, &uio_copy, off);
	*cbytes = uio->uio_resid - uio_copy.uio_resid;
	return (ret);
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
	return ((pos + bio_size + PAGESIZE-1)>>PAGE_SHIFT)-(pos>>PAGE_SHIFT);
}
#endif	/* _KERNEL */

static kmem_cache_t *abd_struct_cache = NULL;

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

	abd = kmem_cache_alloc(abd_struct_cache, KM_PUSHPAGE);

	abd->abd_magic = ARC_BUF_DATA_MAGIC;
	abd->abd_size = sabd->abd_size - off;
	abd->abd_flags = sabd->abd_flags & ~ABD_F_OWNER;

	if (ABD_IS_LINEAR(sabd)) {
		abd->abd_offset = 0;
		abd->abd_nents = 1;
		abd->abd_buf = sabd->abd_buf + off;
	} else if (!(sabd->abd_flags & ABD_F_SG_CHAIN)) {
		/* scatterlist is not chained, treat it as an array. */
		offset = sabd->abd_offset + off;
		abd->abd_offset = offset & (PAGESIZE - 1);
		/* make sure the new abd start as sgl[0] */
		abd->abd_sgl = &sabd->abd_sgl[offset >> PAGE_SHIFT];
		abd->abd_nents = sabd->abd_nents - (offset >> PAGE_SHIFT);
	} else {
		/* Chained scatterlist, need to walk through it. */
		abd->abd_sgl = sabd->abd_sgl;
		abd->abd_nents = sabd->abd_nents;

		offset = sabd->abd_offset + off;
		while (offset >= PAGESIZE) {
			abd->abd_sgl = sg_next(abd->abd_sgl);
			abd->abd_nents--;
			offset -= PAGESIZE;
		}
		abd->abd_offset = offset;
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

	abd = kmem_cache_alloc(abd_struct_cache, KM_PUSHPAGE);

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
	kmem_cache_free(abd_struct_cache, abd);
}

static void
abd_sg_alloc_table(abd_t *abd)
{
	int n = abd->abd_nents;
#if defined(_KERNEL) && \
	(defined(CONFIG_ARCH_HAS_SG_CHAIN) || defined(ARCH_HAS_SG_CHAIN))
	struct sg_table table;
	while (sg_alloc_table(&table, n, GFP_NOIO))
		schedule_timeout(1);

	ASSERT3U(table.nents, ==, n);
	abd->abd_sgl = table.sgl;
	/* scatterlist is chained (see sg_alloc_table) */
	if (n > SG_MAX_SINGLE_ALLOC)
		abd->abd_flags |= ABD_F_SG_CHAIN;
#else
	/*
	 * Unfortunately, some arch don't support chained scatterlist. For
	 * them and user space, we use contiguous scatterlist. For a 16MB
	 * buffer size with 4KB page, this would mean around 128KB of
	 * scatterlist.
	 */
	abd->abd_sgl = vmem_alloc(n * sizeof (struct scatterlist), KM_PUSHPAGE);
	ASSERT(abd->abd_sgl);
	sg_init_table(abd->abd_sgl, n);
#endif
}

static void
abd_sg_free_table(abd_t *abd)
{
#if defined(_KERNEL) && \
	(defined(CONFIG_ARCH_HAS_SG_CHAIN) || defined(ARCH_HAS_SG_CHAIN))
	struct sg_table table;
	table.sgl = abd->abd_sgl;
	table.nents = table.orig_nents = abd->abd_nents;
	sg_free_table(&table);
#else
	vmem_free(abd->abd_sgl, abd->abd_nents * sizeof (struct scatterlist));
#endif
}

/*
 * Allocate a scatter ABD
 */
abd_t *
abd_alloc_scatter(size_t size)
{
	abd_t *abd;
	struct page *page;
	struct scatterlist *sg;
	int i, n = DIV_ROUND_UP(size, PAGESIZE);
	size_t last_size = size - ((n-1) << PAGE_SHIFT);

	abd = kmem_cache_alloc(abd_struct_cache, KM_PUSHPAGE);

	abd->abd_magic = ARC_BUF_DATA_MAGIC;
	abd->abd_flags = ABD_F_SCATTER|ABD_F_OWNER|ABD_F_HIGHMEM;
	abd->abd_size = size;
	abd->abd_offset = 0;
	abd->abd_nents = n;

	abd_sg_alloc_table(abd);

	for_each_sg(abd->abd_sgl, sg, n, i) {
retry:
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (unlikely(page == NULL)) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(1);
			goto retry;
		}
		sg_set_page(sg, page, (i == n-1 ? last_size : PAGESIZE), 0);
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

	abd = kmem_cache_alloc(abd_struct_cache, KM_PUSHPAGE);

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
	struct scatterlist *sg;
	struct page *page;

	ASSERT(abd->abd_nents == DIV_ROUND_UP(abd->abd_size, PAGESIZE));

	n = abd->abd_nents;
	abd->abd_magic = 0;
	for_each_sg(abd->abd_sgl, sg, n, i) {
		page = sg_page(sg);
		if (page)
			__free_page(page);
	}

	abd_sg_free_table(abd);
	kmem_cache_free(abd_struct_cache, abd);
}

static void
abd_free_linear(abd_t *abd, size_t size)
{
	abd->abd_magic = 0;
	zio_buf_free(abd->abd_buf, size);
	kmem_cache_free(abd_struct_cache, abd);
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
	ASSERT(abd->abd_size == size);
	if (ABD_IS_LINEAR(abd))
		abd_free_linear(abd, size);
	else
		abd_free_scatter(abd, size);
}

void
abd_init(void)
{
	abd_struct_cache = kmem_cache_create("abd_struct", sizeof (abd_t), 0,
	    NULL, NULL, NULL, NULL, NULL, 0);
}

void
abd_fini(void)
{
	kmem_cache_destroy(abd_struct_cache);
}
