/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2019 by Delphix. All rights reserved.
 * Copyright (c) 2023, 2024, Klara Inc.
 */

/*
 * See abd.c for a general overview of the arc buffered data (ABD).
 *
 * Linear buffers act exactly like normal buffers and are always mapped into the
 * kernel's virtual memory space, while scattered ABD data chunks are allocated
 * as physical pages and then mapped in only while they are actually being
 * accessed through one of the abd_* library functions. Using scattered ABDs
 * provides several benefits:
 *
 *  (1) They avoid use of kmem_*, preventing performance problems where running
 *      kmem_reap on very large memory systems never finishes and causes
 *      constant TLB shootdowns.
 *
 *  (2) Fragmentation is less of an issue since when we are at the limit of
 *      allocatable space, we won't have to search around for a long free
 *      hole in the VA space for large ARC allocations. Each chunk is mapped in
 *      individually, so even if we are using HIGHMEM (see next point) we
 *      wouldn't need to worry about finding a contiguous address range.
 *
 *  (3) If we are not using HIGHMEM, then all physical memory is always
 *      mapped into the kernel's address space, so we also avoid the map /
 *      unmap costs on each ABD access.
 *
 * If we are not using HIGHMEM, scattered buffers which have only one chunk
 * can be treated as linear buffers, because they are contiguous in the
 * kernel's virtual address space.  See abd_alloc_chunks() for details.
 */

#include <sys/abd_impl.h>
#include <sys/param.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>


#define	abd_for_each_sg(abd, sg, n, i)	\
	for_each_sg(ABD_SCATTER(abd).abd_sgl, sg, n, i)

/*
 * zfs_abd_scatter_min_size is the minimum allocation size to use scatter
 * ABD's.  Smaller allocations will use linear ABD's which uses
 * zio_[data_]buf_alloc().
 *
 * Scatter ABD's use at least one page each, so sub-page allocations waste
 * some space when allocated as scatter (e.g. 2KB scatter allocation wastes
 * half of each page).  Using linear ABD's for small allocations means that
 * they will be put on slabs which contain many allocations.  This can
 * improve memory efficiency, but it also makes it much harder for ARC
 * evictions to actually free pages, because all the buffers on one slab need
 * to be freed in order for the slab (and underlying pages) to be freed.
 * Typically, 512B and 1KB kmem caches have 16 buffers per slab, so it's
 * possible for them to actually waste more memory than scatter (one page per
 * buf = wasting 3/4 or 7/8th; one buf per slab = wasting 15/16th).
 *
 * Spill blocks are typically 512B and are heavily used on systems running
 * selinux with the default dnode size and the `xattr=sa` property set.
 *
 * By default we use linear allocations for 512B and 1KB, and scatter
 * allocations for larger (1.5KB and up).
 */
static int zfs_abd_scatter_min_size = 512 * 3;

/*
 * We use a scattered SPA_MAXBLOCKSIZE sized ABD whose pages are
 * just a single zero'd page. This allows us to conserve memory by
 * only using a single zero page for the scatterlist.
 */
abd_t *abd_zero_scatter = NULL;

struct page;
/*
 * abd_zero_page will be allocated with a zero'ed PAGESIZE buffer, which is
 * assigned to each of the pages of abd_zero_scatter.
 */
static struct page *abd_zero_page = NULL;

static kmem_cache_t *abd_cache = NULL;

static uint_t
abd_chunkcnt_for_bytes(size_t size)
{
	return (P2ROUNDUP(size, PAGESIZE) / PAGESIZE);
}

abd_t *
abd_alloc_struct_impl(size_t size)
{
	/*
	 * In Linux we do not use the size passed in during ABD
	 * allocation, so we just ignore it.
	 */
	(void) size;
	abd_t *abd = kmem_cache_alloc(abd_cache, KM_PUSHPAGE);
	ASSERT3P(abd, !=, NULL);

	return (abd);
}

void
abd_free_struct_impl(abd_t *abd)
{
	kmem_cache_free(abd_cache, abd);
}

#define	nth_page(pg, i) \
	((struct page *)((void *)(pg) + (i) * PAGESIZE))

struct scatterlist {
	struct page *page;
	int length;
	int end;
};

static void
sg_init_table(struct scatterlist *sg, int nr)
{
	memset(sg, 0, nr * sizeof (struct scatterlist));
	sg[nr - 1].end = 1;
}

/*
 * This must be called if any of the sg_table allocation functions
 * are called.
 */
static void
abd_free_sg_table(abd_t *abd)
{
	int nents = ABD_SCATTER(abd).abd_nents;
	vmem_free(ABD_SCATTER(abd).abd_sgl,
	    nents * sizeof (struct scatterlist));
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

void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	unsigned nr_pages = abd_chunkcnt_for_bytes(size);
	struct scatterlist *sg;
	int i;

	ABD_SCATTER(abd).abd_sgl = vmem_alloc(nr_pages *
	    sizeof (struct scatterlist), KM_SLEEP);
	sg_init_table(ABD_SCATTER(abd).abd_sgl, nr_pages);

	abd_for_each_sg(abd, sg, nr_pages, i) {
		struct page *p = umem_alloc_aligned(PAGESIZE, 64, KM_SLEEP);
		sg_set_page(sg, p, PAGESIZE, 0);
	}
	ABD_SCATTER(abd).abd_nents = nr_pages;
}

void
abd_free_chunks(abd_t *abd)
{
	int i, n = ABD_SCATTER(abd).abd_nents;
	struct scatterlist *sg;

	abd_for_each_sg(abd, sg, n, i) {
		struct page *p = nth_page(sg_page(sg), 0);
		umem_free_aligned(p, PAGESIZE);
	}
	abd_free_sg_table(abd);
}

static void
abd_alloc_zero_scatter(void)
{
	unsigned nr_pages = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	struct scatterlist *sg;
	int i;

	abd_zero_page = umem_alloc_aligned(PAGESIZE, 64, KM_SLEEP);
	memset(abd_zero_page, 0, PAGESIZE);
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);
	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER;
	abd_zero_scatter->abd_flags |= ABD_FLAG_MULTI_CHUNK;
	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;
	ABD_SCATTER(abd_zero_scatter).abd_nents = nr_pages;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;
	ABD_SCATTER(abd_zero_scatter).abd_sgl = vmem_alloc(nr_pages *
	    sizeof (struct scatterlist), KM_SLEEP);

	sg_init_table(ABD_SCATTER(abd_zero_scatter).abd_sgl, nr_pages);

	abd_for_each_sg(abd_zero_scatter, sg, nr_pages, i) {
		sg_set_page(sg, abd_zero_page, PAGESIZE, 0);
	}
}

boolean_t
abd_size_alloc_linear(size_t size)
{
	return (!zfs_abd_scatter_enabled || size < zfs_abd_scatter_min_size);
}

void
abd_update_scatter_stats(abd_t *abd, abd_stats_op_t op)
{
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	int waste = P2ROUNDUP(abd->abd_size, PAGESIZE) - abd->abd_size;
	if (op == ABDSTAT_INCR) {
		arc_space_consume(waste, ARC_SPACE_ABD_CHUNK_WASTE);
	} else {
		arc_space_return(waste, ARC_SPACE_ABD_CHUNK_WASTE);
	}
}

void
abd_update_linear_stats(abd_t *abd, abd_stats_op_t op)
{
	(void) abd;
	(void) op;
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
}

void
abd_verify_scatter(abd_t *abd)
{
	size_t n;
	int i = 0;
	struct scatterlist *sg = NULL;

	ASSERT3U(ABD_SCATTER(abd).abd_nents, >, 0);
	ASSERT3U(ABD_SCATTER(abd).abd_offset, <,
	    ABD_SCATTER(abd).abd_sgl->length);
	n = ABD_SCATTER(abd).abd_nents;
	abd_for_each_sg(abd, sg, n, i) {
		ASSERT3P(sg_page(sg), !=, NULL);
	}
}

static void
abd_free_zero_scatter(void)
{
	abd_free_sg_table(abd_zero_scatter);
	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;
	ASSERT3P(abd_zero_page, !=, NULL);
	umem_free_aligned(abd_zero_page, PAGESIZE);
}

void
abd_init(void)
{
	abd_cache = kmem_cache_create("abd_t", sizeof (abd_t),
	    0, NULL, NULL, NULL, NULL, NULL, 0);

	abd_alloc_zero_scatter();
}

void
abd_fini(void)
{
	abd_free_zero_scatter();

	if (abd_cache) {
		kmem_cache_destroy(abd_cache);
		abd_cache = NULL;
	}
}

void
abd_free_linear_page(abd_t *abd)
{
	(void) abd;
	__builtin_unreachable();
}

/*
 * If we're going to use this ABD for doing I/O using the block layer, the
 * consumer of the ABD data doesn't care if it's scattered or not, and we don't
 * plan to store this ABD in memory for a long period of time, we should
 * allocate the ABD type that requires the least data copying to do the I/O.
 *
 * On Linux the optimal thing to do would be to use abd_get_offset() and
 * construct a new ABD which shares the original pages thereby eliminating
 * the copy.  But for the moment a new linear ABD is allocated until this
 * performance optimization can be implemented.
 */
abd_t *
abd_alloc_for_io(size_t size, boolean_t is_metadata)
{
	return (abd_alloc(size, is_metadata));
}

abd_t *
abd_get_offset_scatter(abd_t *abd, abd_t *sabd, size_t off,
    size_t size)
{
	(void) size;
	int i = 0;
	struct scatterlist *sg = NULL;

	abd_verify(sabd);
	ASSERT3U(off, <=, sabd->abd_size);

	size_t new_offset = ABD_SCATTER(sabd).abd_offset + off;

	if (abd == NULL)
		abd = abd_alloc_struct(0);

	/*
	 * Even if this buf is filesystem metadata, we only track that
	 * if we own the underlying data buffer, which is not true in
	 * this case. Therefore, we don't ever use ABD_FLAG_META here.
	 */

	abd_for_each_sg(sabd, sg, ABD_SCATTER(sabd).abd_nents, i) {
		if (new_offset < sg->length)
			break;
		new_offset -= sg->length;
	}

	ABD_SCATTER(abd).abd_sgl = sg;
	ABD_SCATTER(abd).abd_offset = new_offset;
	ABD_SCATTER(abd).abd_nents = ABD_SCATTER(sabd).abd_nents - i;

	return (abd);
}

/*
 * Initialize the abd_iter.
 */
void
abd_iter_init(struct abd_iter *aiter, abd_t *abd)
{
	ASSERT(!abd_is_gang(abd));
	abd_verify(abd);
	memset(aiter, 0, sizeof (struct abd_iter));
	aiter->iter_abd = abd;
	if (!abd_is_linear(abd)) {
		aiter->iter_offset = ABD_SCATTER(abd).abd_offset;
		aiter->iter_sg = ABD_SCATTER(abd).abd_sgl;
	}
}

/*
 * This is just a helper function to see if we have exhausted the
 * abd_iter and reached the end.
 */
boolean_t
abd_iter_at_end(struct abd_iter *aiter)
{
	ASSERT3U(aiter->iter_pos, <=, aiter->iter_abd->abd_size);
	return (aiter->iter_pos == aiter->iter_abd->abd_size);
}

/*
 * Advance the iterator by a certain amount. Cannot be called when a chunk is
 * in use. This can be safely called when the aiter has already exhausted, in
 * which case this does nothing.
 */
void
abd_iter_advance(struct abd_iter *aiter, size_t amount)
{
	/*
	 * Ensure that last chunk is not in use. abd_iterate_*() must clear
	 * this state (directly or abd_iter_unmap()) before advancing.
	 */
	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);
	ASSERT3P(aiter->iter_page, ==, NULL);
	ASSERT0(aiter->iter_page_doff);
	ASSERT0(aiter->iter_page_dsize);

	/* There's nothing left to advance to, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	aiter->iter_pos += amount;
	aiter->iter_offset += amount;
	if (!abd_is_linear(aiter->iter_abd)) {
		while (aiter->iter_offset >= aiter->iter_sg->length) {
			aiter->iter_offset -= aiter->iter_sg->length;
			aiter->iter_sg = sg_next(aiter->iter_sg);
			if (aiter->iter_sg == NULL) {
				ASSERT0(aiter->iter_offset);
				break;
			}
		}
	}
}

/*
 * Map the current chunk into aiter. This can be safely called when the aiter
 * has already exhausted, in which case this does nothing.
 */
void
abd_iter_map(struct abd_iter *aiter)
{
	void *paddr;
	size_t offset = 0;

	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	/* There's nothing left to iterate over, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	if (abd_is_linear(aiter->iter_abd)) {
		ASSERT3U(aiter->iter_pos, ==, aiter->iter_offset);
		offset = aiter->iter_offset;
		aiter->iter_mapsize = aiter->iter_abd->abd_size - offset;
		paddr = ABD_LINEAR_BUF(aiter->iter_abd);
	} else {
		offset = aiter->iter_offset;
		aiter->iter_mapsize = MIN(aiter->iter_sg->length - offset,
		    aiter->iter_abd->abd_size - aiter->iter_pos);

		paddr = sg_page(aiter->iter_sg);
	}

	aiter->iter_mapaddr = (char *)paddr + offset;
}

/*
 * Unmap the current chunk from aiter. This can be safely called when the aiter
 * has already exhausted, in which case this does nothing.
 */
void
abd_iter_unmap(struct abd_iter *aiter)
{
	/* There's nothing left to unmap, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	ASSERT3P(aiter->iter_mapaddr, !=, NULL);
	ASSERT3U(aiter->iter_mapsize, >, 0);

	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

void
abd_cache_reap_now(void)
{
}
