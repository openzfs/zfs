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
#include <linux/kmap_compat.h>
#include <linux/mm_compat.h>
#include <linux/scatterlist.h>
#include <linux/version.h>

#if defined(MAX_ORDER)
#define	ABD_MAX_ORDER	(MAX_ORDER)
#elif defined(MAX_PAGE_ORDER)
#define	ABD_MAX_ORDER	(MAX_PAGE_ORDER)
#endif

typedef struct abd_stats {
	kstat_named_t abdstat_struct_size;
	kstat_named_t abdstat_linear_cnt;
	kstat_named_t abdstat_linear_data_size;
	kstat_named_t abdstat_scatter_cnt;
	kstat_named_t abdstat_scatter_data_size;
	kstat_named_t abdstat_scatter_chunk_waste;
	kstat_named_t abdstat_scatter_orders[ABD_MAX_ORDER];
	kstat_named_t abdstat_scatter_page_multi_chunk;
	kstat_named_t abdstat_scatter_page_multi_zone;
	kstat_named_t abdstat_scatter_page_alloc_retry;
	kstat_named_t abdstat_scatter_sg_table_retry;
} abd_stats_t;

static abd_stats_t abd_stats = {
	/* Amount of memory occupied by all of the abd_t struct allocations */
	{ "struct_size",			KSTAT_DATA_UINT64 },
	/*
	 * The number of linear ABDs which are currently allocated, excluding
	 * ABDs which don't own their data (for instance the ones which were
	 * allocated through abd_get_offset() and abd_get_from_buf()). If an
	 * ABD takes ownership of its buf then it will become tracked.
	 */
	{ "linear_cnt",				KSTAT_DATA_UINT64 },
	/* Amount of data stored in all linear ABDs tracked by linear_cnt */
	{ "linear_data_size",			KSTAT_DATA_UINT64 },
	/*
	 * The number of scatter ABDs which are currently allocated, excluding
	 * ABDs which don't own their data (for instance the ones which were
	 * allocated through abd_get_offset()).
	 */
	{ "scatter_cnt",			KSTAT_DATA_UINT64 },
	/* Amount of data stored in all scatter ABDs tracked by scatter_cnt */
	{ "scatter_data_size",			KSTAT_DATA_UINT64 },
	/*
	 * The amount of space wasted at the end of the last chunk across all
	 * scatter ABDs tracked by scatter_cnt.
	 */
	{ "scatter_chunk_waste",		KSTAT_DATA_UINT64 },
	/*
	 * The number of compound allocations of a given order.  These
	 * allocations are spread over all currently allocated ABDs, and
	 * act as a measure of memory fragmentation.
	 */
	{ { "scatter_order_N",			KSTAT_DATA_UINT64 } },
	/*
	 * The number of scatter ABDs which contain multiple chunks.
	 * ABDs are preferentially allocated from the minimum number of
	 * contiguous multi-page chunks, a single chunk is optimal.
	 */
	{ "scatter_page_multi_chunk",		KSTAT_DATA_UINT64 },
	/*
	 * The number of scatter ABDs which are split across memory zones.
	 * ABDs are preferentially allocated using pages from a single zone.
	 */
	{ "scatter_page_multi_zone",		KSTAT_DATA_UINT64 },
	/*
	 *  The total number of retries encountered when attempting to
	 *  allocate the pages to populate the scatter ABD.
	 */
	{ "scatter_page_alloc_retry",		KSTAT_DATA_UINT64 },
	/*
	 *  The total number of retries encountered when attempting to
	 *  allocate the sg table for an ABD.
	 */
	{ "scatter_sg_table_retry",		KSTAT_DATA_UINT64 },
};

static struct {
	wmsum_t abdstat_struct_size;
	wmsum_t abdstat_linear_cnt;
	wmsum_t abdstat_linear_data_size;
	wmsum_t abdstat_scatter_cnt;
	wmsum_t abdstat_scatter_data_size;
	wmsum_t abdstat_scatter_chunk_waste;
	wmsum_t abdstat_scatter_orders[ABD_MAX_ORDER];
	wmsum_t abdstat_scatter_page_multi_chunk;
	wmsum_t abdstat_scatter_page_multi_zone;
	wmsum_t abdstat_scatter_page_alloc_retry;
	wmsum_t abdstat_scatter_sg_table_retry;
} abd_sums;

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
 * abd_zero_page is assigned to each of the pages of abd_zero_scatter. It will
 * point to ZERO_PAGE if it is available or it will be an allocated zero'd
 * PAGESIZE buffer.
 */
static struct page *abd_zero_page = NULL;

static kmem_cache_t *abd_cache = NULL;
static kstat_t *abd_ksp;

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
	ABDSTAT_INCR(abdstat_struct_size, sizeof (abd_t));

	return (abd);
}

void
abd_free_struct_impl(abd_t *abd)
{
	kmem_cache_free(abd_cache, abd);
	ABDSTAT_INCR(abdstat_struct_size, -(int)sizeof (abd_t));
}

static unsigned zfs_abd_scatter_max_order = ABD_MAX_ORDER - 1;

/*
 * Mark zfs data pages so they can be excluded from kernel crash dumps
 */
#ifdef _LP64
#define	ABD_FILE_CACHE_PAGE	0x2F5ABDF11ECAC4E

static inline void
abd_mark_zfs_page(struct page *page)
{
	get_page(page);
	SetPagePrivate(page);
	set_page_private(page, ABD_FILE_CACHE_PAGE);
}

static inline void
abd_unmark_zfs_page(struct page *page)
{
	set_page_private(page, 0UL);
	ClearPagePrivate(page);
	put_page(page);
}
#else
#define	abd_mark_zfs_page(page)
#define	abd_unmark_zfs_page(page)
#endif /* _LP64 */

#ifndef CONFIG_HIGHMEM

#ifndef __GFP_RECLAIM
#define	__GFP_RECLAIM		__GFP_WAIT
#endif

/*
 * The goal is to minimize fragmentation by preferentially populating ABDs
 * with higher order compound pages from a single zone.  Allocation size is
 * progressively decreased until it can be satisfied without performing
 * reclaim or compaction.  When necessary this function will degenerate to
 * allocating individual pages and allowing reclaim to satisfy allocations.
 */
void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	struct list_head pages;
	struct sg_table table;
	struct scatterlist *sg;
	struct page *page, *tmp_page = NULL;
	gfp_t gfp = __GFP_RECLAIMABLE | __GFP_NOWARN | GFP_NOIO;
	gfp_t gfp_comp = (gfp | __GFP_NORETRY | __GFP_COMP) & ~__GFP_RECLAIM;
	unsigned int max_order = MIN(zfs_abd_scatter_max_order,
	    ABD_MAX_ORDER - 1);
	unsigned int nr_pages = abd_chunkcnt_for_bytes(size);
	unsigned int chunks = 0, zones = 0;
	size_t remaining_size;
	int nid = NUMA_NO_NODE;
	unsigned int alloc_pages = 0;

	INIT_LIST_HEAD(&pages);

	ASSERT3U(alloc_pages, <, nr_pages);

	while (alloc_pages < nr_pages) {
		unsigned int chunk_pages;
		unsigned int order;

		order = MIN(highbit64(nr_pages - alloc_pages) - 1, max_order);
		chunk_pages = (1U << order);

		page = alloc_pages_node(nid, order ? gfp_comp : gfp, order);
		if (page == NULL) {
			if (order == 0) {
				ABDSTAT_BUMP(abdstat_scatter_page_alloc_retry);
				schedule_timeout_interruptible(1);
			} else {
				max_order = MAX(0, order - 1);
			}
			continue;
		}

		list_add_tail(&page->lru, &pages);

		if ((nid != NUMA_NO_NODE) && (page_to_nid(page) != nid))
			zones++;

		nid = page_to_nid(page);
		ABDSTAT_BUMP(abdstat_scatter_orders[order]);
		chunks++;
		alloc_pages += chunk_pages;
	}

	ASSERT3S(alloc_pages, ==, nr_pages);

	while (sg_alloc_table(&table, chunks, gfp)) {
		ABDSTAT_BUMP(abdstat_scatter_sg_table_retry);
		schedule_timeout_interruptible(1);
	}

	sg = table.sgl;
	remaining_size = size;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		size_t sg_size = MIN(PAGESIZE << compound_order(page),
		    remaining_size);
		sg_set_page(sg, page, sg_size, 0);
		abd_mark_zfs_page(page);
		remaining_size -= sg_size;

		sg = sg_next(sg);
		list_del(&page->lru);
	}

	/*
	 * These conditions ensure that a possible transformation to a linear
	 * ABD would be valid.
	 */
	ASSERT(!PageHighMem(sg_page(table.sgl)));
	ASSERT0(ABD_SCATTER(abd).abd_offset);

	if (table.nents == 1) {
		/*
		 * Since there is only one entry, this ABD can be represented
		 * as a linear buffer.  All single-page (4K) ABD's can be
		 * represented this way.  Some multi-page ABD's can also be
		 * represented this way, if we were able to allocate a single
		 * "chunk" (higher-order "page" which represents a power-of-2
		 * series of physically-contiguous pages).  This is often the
		 * case for 2-page (8K) ABD's.
		 *
		 * Representing a single-entry scatter ABD as a linear ABD
		 * has the performance advantage of avoiding the copy (and
		 * allocation) in abd_borrow_buf_copy / abd_return_buf_copy.
		 * A performance increase of around 5% has been observed for
		 * ARC-cached reads (of small blocks which can take advantage
		 * of this).
		 *
		 * Note that this optimization is only possible because the
		 * pages are always mapped into the kernel's address space.
		 * This is not the case for highmem pages, so the
		 * optimization can not be made there.
		 */
		abd->abd_flags |= ABD_FLAG_LINEAR;
		abd->abd_flags |= ABD_FLAG_LINEAR_PAGE;
		abd->abd_u.abd_linear.abd_sgl = table.sgl;
		ABD_LINEAR_BUF(abd) = page_address(sg_page(table.sgl));
	} else if (table.nents > 1) {
		ABDSTAT_BUMP(abdstat_scatter_page_multi_chunk);
		abd->abd_flags |= ABD_FLAG_MULTI_CHUNK;

		if (zones) {
			ABDSTAT_BUMP(abdstat_scatter_page_multi_zone);
			abd->abd_flags |= ABD_FLAG_MULTI_ZONE;
		}

		ABD_SCATTER(abd).abd_sgl = table.sgl;
		ABD_SCATTER(abd).abd_nents = table.nents;
	}
}
#else

/*
 * Allocate N individual pages to construct a scatter ABD.  This function
 * makes no attempt to request contiguous pages and requires the minimal
 * number of kernel interfaces.  It's designed for maximum compatibility.
 */
void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	struct scatterlist *sg = NULL;
	struct sg_table table;
	struct page *page;
	gfp_t gfp = __GFP_RECLAIMABLE | __GFP_NOWARN | GFP_NOIO;
	int nr_pages = abd_chunkcnt_for_bytes(size);
	int i = 0;

	while (sg_alloc_table(&table, nr_pages, gfp)) {
		ABDSTAT_BUMP(abdstat_scatter_sg_table_retry);
		schedule_timeout_interruptible(1);
	}

	ASSERT3U(table.nents, ==, nr_pages);
	ABD_SCATTER(abd).abd_sgl = table.sgl;
	ABD_SCATTER(abd).abd_nents = nr_pages;

	abd_for_each_sg(abd, sg, nr_pages, i) {
		while ((page = __page_cache_alloc(gfp)) == NULL) {
			ABDSTAT_BUMP(abdstat_scatter_page_alloc_retry);
			schedule_timeout_interruptible(1);
		}

		ABDSTAT_BUMP(abdstat_scatter_orders[0]);
		sg_set_page(sg, page, PAGESIZE, 0);
		abd_mark_zfs_page(page);
	}

	if (nr_pages > 1) {
		ABDSTAT_BUMP(abdstat_scatter_page_multi_chunk);
		abd->abd_flags |= ABD_FLAG_MULTI_CHUNK;
	}
}
#endif /* !CONFIG_HIGHMEM */

/*
 * This must be called if any of the sg_table allocation functions
 * are called.
 */
static void
abd_free_sg_table(abd_t *abd)
{
	struct sg_table table;

	table.sgl = ABD_SCATTER(abd).abd_sgl;
	table.nents = table.orig_nents = ABD_SCATTER(abd).abd_nents;
	sg_free_table(&table);
}

void
abd_free_chunks(abd_t *abd)
{
	struct scatterlist *sg = NULL;
	struct page *page;
	int nr_pages = ABD_SCATTER(abd).abd_nents;
	int order, i = 0;

	if (abd->abd_flags & ABD_FLAG_MULTI_ZONE)
		ABDSTAT_BUMPDOWN(abdstat_scatter_page_multi_zone);

	if (abd->abd_flags & ABD_FLAG_MULTI_CHUNK)
		ABDSTAT_BUMPDOWN(abdstat_scatter_page_multi_chunk);

	/*
	 * Scatter ABDs may be constructed by abd_alloc_from_pages() from
	 * an array of pages. In which case they should not be freed.
	 */
	if (!abd_is_from_pages(abd)) {
		abd_for_each_sg(abd, sg, nr_pages, i) {
			page = sg_page(sg);
			abd_unmark_zfs_page(page);
			order = compound_order(page);
			__free_pages(page, order);
			ASSERT3U(sg->length, <=, PAGE_SIZE << order);
			ABDSTAT_BUMPDOWN(abdstat_scatter_orders[order]);
		}
	}

	abd_free_sg_table(abd);
}

/*
 * Allocate scatter ABD of size SPA_MAXBLOCKSIZE, where each page in
 * the scatterlist will be set to the zero'd out buffer abd_zero_page.
 */
static void
abd_alloc_zero_scatter(void)
{
	struct scatterlist *sg = NULL;
	struct sg_table table;
	gfp_t gfp = __GFP_NOWARN | GFP_NOIO;
	int nr_pages = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	int i = 0;

#if defined(HAVE_ZERO_PAGE_GPL_ONLY)
	gfp_t gfp_zero_page = gfp | __GFP_ZERO;
	while ((abd_zero_page = __page_cache_alloc(gfp_zero_page)) == NULL) {
		ABDSTAT_BUMP(abdstat_scatter_page_alloc_retry);
		schedule_timeout_interruptible(1);
	}
	abd_mark_zfs_page(abd_zero_page);
#else
	abd_zero_page = ZERO_PAGE(0);
#endif /* HAVE_ZERO_PAGE_GPL_ONLY */

	while (sg_alloc_table(&table, nr_pages, gfp)) {
		ABDSTAT_BUMP(abdstat_scatter_sg_table_retry);
		schedule_timeout_interruptible(1);
	}
	ASSERT3U(table.nents, ==, nr_pages);

	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);
	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER;
	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;
	ABD_SCATTER(abd_zero_scatter).abd_sgl = table.sgl;
	ABD_SCATTER(abd_zero_scatter).abd_nents = nr_pages;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;
	abd_zero_scatter->abd_flags |= ABD_FLAG_MULTI_CHUNK;

	abd_for_each_sg(abd_zero_scatter, sg, nr_pages, i) {
		sg_set_page(sg, abd_zero_page, PAGESIZE, 0);
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, PAGESIZE);
	ABDSTAT_BUMP(abdstat_scatter_page_multi_chunk);
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
		ABDSTAT_BUMP(abdstat_scatter_cnt);
		ABDSTAT_INCR(abdstat_scatter_data_size, abd->abd_size);
		ABDSTAT_INCR(abdstat_scatter_chunk_waste, waste);
		arc_space_consume(waste, ARC_SPACE_ABD_CHUNK_WASTE);
	} else {
		ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
		ABDSTAT_INCR(abdstat_scatter_data_size, -(int)abd->abd_size);
		ABDSTAT_INCR(abdstat_scatter_chunk_waste, -waste);
		arc_space_return(waste, ARC_SPACE_ABD_CHUNK_WASTE);
	}
}

void
abd_update_linear_stats(abd_t *abd, abd_stats_op_t op)
{
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	if (op == ABDSTAT_INCR) {
		ABDSTAT_BUMP(abdstat_linear_cnt);
		ABDSTAT_INCR(abdstat_linear_data_size, abd->abd_size);
	} else {
		ABDSTAT_BUMPDOWN(abdstat_linear_cnt);
		ABDSTAT_INCR(abdstat_linear_data_size, -(int)abd->abd_size);
	}
}

void
abd_verify_scatter(abd_t *abd)
{
	ASSERT3U(ABD_SCATTER(abd).abd_nents, >, 0);
	ASSERT3U(ABD_SCATTER(abd).abd_offset, <,
	    ABD_SCATTER(abd).abd_sgl->length);

#ifdef ZFS_DEBUG
	struct scatterlist *sg = NULL;
	size_t n = ABD_SCATTER(abd).abd_nents;
	int i = 0;

	abd_for_each_sg(abd, sg, n, i) {
		ASSERT3P(sg_page(sg), !=, NULL);
	}
#endif
}

static void
abd_free_zero_scatter(void)
{
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)PAGESIZE);
	ABDSTAT_BUMPDOWN(abdstat_scatter_page_multi_chunk);

	abd_free_sg_table(abd_zero_scatter);
	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;
	ASSERT3P(abd_zero_page, !=, NULL);
#if defined(HAVE_ZERO_PAGE_GPL_ONLY)
	abd_unmark_zfs_page(abd_zero_page);
	__free_page(abd_zero_page);
#endif /* HAVE_ZERO_PAGE_GPL_ONLY */
}

static int
abd_kstats_update(kstat_t *ksp, int rw)
{
	abd_stats_t *as = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	as->abdstat_struct_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_struct_size);
	as->abdstat_linear_cnt.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_linear_cnt);
	as->abdstat_linear_data_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_linear_data_size);
	as->abdstat_scatter_cnt.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_cnt);
	as->abdstat_scatter_data_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_data_size);
	as->abdstat_scatter_chunk_waste.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_chunk_waste);
	for (int i = 0; i < ABD_MAX_ORDER; i++) {
		as->abdstat_scatter_orders[i].value.ui64 =
		    wmsum_value(&abd_sums.abdstat_scatter_orders[i]);
	}
	as->abdstat_scatter_page_multi_chunk.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_page_multi_chunk);
	as->abdstat_scatter_page_multi_zone.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_page_multi_zone);
	as->abdstat_scatter_page_alloc_retry.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_page_alloc_retry);
	as->abdstat_scatter_sg_table_retry.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_sg_table_retry);
	return (0);
}

void
abd_init(void)
{
	int i;

	abd_cache = kmem_cache_create("abd_t", sizeof (abd_t),
	    0, NULL, NULL, NULL, NULL, NULL, KMC_RECLAIMABLE);

	wmsum_init(&abd_sums.abdstat_struct_size, 0);
	wmsum_init(&abd_sums.abdstat_linear_cnt, 0);
	wmsum_init(&abd_sums.abdstat_linear_data_size, 0);
	wmsum_init(&abd_sums.abdstat_scatter_cnt, 0);
	wmsum_init(&abd_sums.abdstat_scatter_data_size, 0);
	wmsum_init(&abd_sums.abdstat_scatter_chunk_waste, 0);
	for (i = 0; i < ABD_MAX_ORDER; i++)
		wmsum_init(&abd_sums.abdstat_scatter_orders[i], 0);
	wmsum_init(&abd_sums.abdstat_scatter_page_multi_chunk, 0);
	wmsum_init(&abd_sums.abdstat_scatter_page_multi_zone, 0);
	wmsum_init(&abd_sums.abdstat_scatter_page_alloc_retry, 0);
	wmsum_init(&abd_sums.abdstat_scatter_sg_table_retry, 0);

	abd_ksp = kstat_create("zfs", 0, "abdstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (abd_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (abd_ksp != NULL) {
		for (i = 0; i < ABD_MAX_ORDER; i++) {
			snprintf(abd_stats.abdstat_scatter_orders[i].name,
			    KSTAT_STRLEN, "scatter_order_%d", i);
			abd_stats.abdstat_scatter_orders[i].data_type =
			    KSTAT_DATA_UINT64;
		}
		abd_ksp->ks_data = &abd_stats;
		abd_ksp->ks_update = abd_kstats_update;
		kstat_install(abd_ksp);
	}

	abd_alloc_zero_scatter();
}

void
abd_fini(void)
{
	abd_free_zero_scatter();

	if (abd_ksp != NULL) {
		kstat_delete(abd_ksp);
		abd_ksp = NULL;
	}

	wmsum_fini(&abd_sums.abdstat_struct_size);
	wmsum_fini(&abd_sums.abdstat_linear_cnt);
	wmsum_fini(&abd_sums.abdstat_linear_data_size);
	wmsum_fini(&abd_sums.abdstat_scatter_cnt);
	wmsum_fini(&abd_sums.abdstat_scatter_data_size);
	wmsum_fini(&abd_sums.abdstat_scatter_chunk_waste);
	for (int i = 0; i < ABD_MAX_ORDER; i++)
		wmsum_fini(&abd_sums.abdstat_scatter_orders[i]);
	wmsum_fini(&abd_sums.abdstat_scatter_page_multi_chunk);
	wmsum_fini(&abd_sums.abdstat_scatter_page_multi_zone);
	wmsum_fini(&abd_sums.abdstat_scatter_page_alloc_retry);
	wmsum_fini(&abd_sums.abdstat_scatter_sg_table_retry);

	if (abd_cache) {
		kmem_cache_destroy(abd_cache);
		abd_cache = NULL;
	}
}

void
abd_free_linear_page(abd_t *abd)
{
	/* Transform it back into a scatter ABD for freeing */
	struct scatterlist *sg = abd->abd_u.abd_linear.abd_sgl;

	/* When backed by user page unmap it */
	if (abd_is_from_pages(abd))
		zfs_kunmap(sg_page(sg));

	abd->abd_flags &= ~ABD_FLAG_LINEAR;
	abd->abd_flags &= ~ABD_FLAG_LINEAR_PAGE;
	ABD_SCATTER(abd).abd_nents = 1;
	ABD_SCATTER(abd).abd_offset = 0;
	ABD_SCATTER(abd).abd_sgl = sg;
	abd_free_chunks(abd);
}

/*
 * Allocate a scatter ABD structure from user pages. The pages must be
 * pinned with get_user_pages, or similiar, but need not be mapped via
 * the kmap interfaces.
 */
abd_t *
abd_alloc_from_pages(struct page **pages, unsigned long offset, uint64_t size)
{
	uint_t npages = DIV_ROUND_UP(size, PAGE_SIZE);
	struct sg_table table;

	VERIFY3U(size, <=, DMU_MAX_ACCESS);
	ASSERT3U(offset, <, PAGE_SIZE);
	ASSERT3P(pages, !=, NULL);

	/*
	 * Even if this buf is filesystem metadata, we only track that we
	 * own the underlying data buffer, which is not true in this case.
	 * Therefore, we don't ever use ABD_FLAG_META here.
	 */
	abd_t *abd = abd_alloc_struct(0);
	abd->abd_flags |= ABD_FLAG_FROM_PAGES | ABD_FLAG_OWNER;
	abd->abd_size = size;

	while (sg_alloc_table_from_pages(&table, pages, npages, offset,
	    size, __GFP_NOWARN | GFP_NOIO) != 0) {
		ABDSTAT_BUMP(abdstat_scatter_sg_table_retry);
		schedule_timeout_interruptible(1);
	}

	if ((offset + size) <= PAGE_SIZE) {
		/*
		 * Since there is only one entry, this ABD can be represented
		 * as a linear buffer. All single-page (4K) ABD's constructed
		 * from a user page can be represented this way as long as the
		 * page is mapped to a virtual address. This allows us to
		 * apply an offset in to the mapped page.
		 *
		 * Note that kmap() must be used, not kmap_atomic(), because
		 * the mapping needs to bet set up on all CPUs. Using kmap()
		 * also enables the user of highmem pages when required.
		 */
		abd->abd_flags |= ABD_FLAG_LINEAR | ABD_FLAG_LINEAR_PAGE;
		abd->abd_u.abd_linear.abd_sgl = table.sgl;
		zfs_kmap(sg_page(table.sgl));
		ABD_LINEAR_BUF(abd) = sg_virt(table.sgl);
	} else {
		ABDSTAT_BUMP(abdstat_scatter_page_multi_chunk);
		abd->abd_flags |= ABD_FLAG_MULTI_CHUNK;

		ABD_SCATTER(abd).abd_offset = offset;
		ABD_SCATTER(abd).abd_sgl = table.sgl;
		ABD_SCATTER(abd).abd_nents = table.nents;

		ASSERT0(ABD_SCATTER(abd).abd_offset);
	}

	return (abd);
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

	if (abd_is_from_pages(sabd))
		abd->abd_flags |= ABD_FLAG_FROM_PAGES;

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

		paddr = zfs_kmap_local(sg_page(aiter->iter_sg));
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

	if (!abd_is_linear(aiter->iter_abd)) {
		/* LINTED E_FUNC_SET_NOT_USED */
		zfs_kunmap_local(aiter->iter_mapaddr - aiter->iter_offset);
	}

	ASSERT3P(aiter->iter_mapaddr, !=, NULL);
	ASSERT3U(aiter->iter_mapsize, >, 0);

	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

void
abd_cache_reap_now(void)
{
}

/*
 * Borrow a raw buffer from an ABD without copying the contents of the ABD
 * into the buffer. If the ABD is scattered, this will allocate a raw buffer
 * whose contents are undefined. To copy over the existing data in the ABD, use
 * abd_borrow_buf_copy() instead.
 */
void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	void *buf;
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, 0);
	/*
	 * In the event the ABD is composed of a single user page from Direct
	 * I/O we can not direclty return the raw buffer. This is a consequence
	 * of not being able to write protect the page and the contents of the
	 * page can be changed at any time by the user.
	 */
	if (abd_is_from_pages(abd)) {
		buf = zio_buf_alloc(n);
	} else if (abd_is_linear(abd)) {
		buf = abd_to_buf(abd);
	} else {
		buf = zio_buf_alloc(n);
	}

#ifdef ZFS_DEBUG
	(void) zfs_refcount_add_many(&abd->abd_children, n, buf);
#endif
	return (buf);
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t n)
{
	void *buf = abd_borrow_buf(abd, n);

	/*
	 * In the event the ABD is composed of a single user page from Direct
	 * I/O we must make sure copy the data over into the newly allocated
	 * buffer. This is a consequence of the fact that we can not write
	 * protect the user page and there is a risk the contents of the page
	 * could be changed by the user at any moment.
	 */
	if (!abd_is_linear(abd) || abd_is_from_pages(abd)) {
		abd_copy_to_buf(buf, abd, n);
	}
	return (buf);
}

/*
 * Return a borrowed raw buffer to an ABD. If the ABD is scatterd, this will
 * not change the contents of the ABD. If you want any changes you made to
 * buf to be copied back to abd, use abd_return_buf_copy() instead. If the
 * ABD is not constructed from user pages for Direct I/O then an ASSERT
 * checks to make sure the contents of buffer have not changed since it was
 * borrowed. We can not ASSERT that the contents of the buffer have not changed
 * if it is composed of user pages because the pages can not be placed under
 * write protection and the user could have possibly changed the contents in
 * the pages at any time.
 */
void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, n);
#ifdef ZFS_DEBUG
	(void) zfs_refcount_remove_many(&abd->abd_children, n, buf);
#endif
	if (abd_is_from_pages(abd)) {
		zio_buf_free(buf, n);
	} else if (abd_is_linear(abd)) {
		ASSERT3P(buf, ==, abd_to_buf(abd));
	} else if (abd_is_gang(abd)) {
#ifdef ZFS_DEBUG
		/*
		 * We have to be careful with gang ABD's that we do not ASSERT0
		 * for any ABD's that contain user pages from Direct I/O. In
		 * order to handle this, we just iterate through the gang ABD
		 * and only verify ABDs that are not from user pages.
		 */
		void *cmp_buf = buf;

		for (abd_t *cabd = list_head(&ABD_GANG(abd).abd_gang_chain);
		    cabd != NULL;
		    cabd = list_next(&ABD_GANG(abd).abd_gang_chain, cabd)) {
			if (!abd_is_from_pages(cabd)) {
				ASSERT0(abd_cmp_buf(cabd, cmp_buf,
				    cabd->abd_size));
			}
			cmp_buf = (char *)cmp_buf + cabd->abd_size;
		}
#endif
		zio_buf_free(buf, n);
	} else {
		ASSERT0(abd_cmp_buf(abd, buf, n));
		zio_buf_free(buf, n);
	}
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	if (!abd_is_linear(abd) || abd_is_from_pages(abd)) {
		abd_copy_from_buf(abd, buf, n);
	}
	abd_return_buf(abd, buf, n);
}

/*
 * This is abd_iter_page(), the function underneath abd_iterate_page_func().
 * It yields the next page struct and data offset and size within it, without
 * mapping it into the address space.
 */

/*
 * "Compound pages" are a group of pages that can be referenced from a single
 * struct page *. Its organised as a "head" page, followed by a series of
 * "tail" pages.
 *
 * In OpenZFS, compound pages are allocated using the __GFP_COMP flag, which we
 * get from scatter ABDs and SPL vmalloc slabs (ie >16K allocations). So a
 * great many of the IO buffers we get are going to be of this type.
 *
 * The tail pages are just regular PAGESIZE pages, and can be safely used
 * as-is. However, the head page has length covering itself and all the tail
 * pages. If the ABD chunk spans multiple pages, then we can use the head page
 * and a >PAGESIZE length, which is far more efficient.
 *
 * Before kernel 4.5 however, compound page heads were refcounted separately
 * from tail pages, such that moving back to the head page would require us to
 * take a reference to it and releasing it once we're completely finished with
 * it. In practice, that meant when our caller is done with the ABD, which we
 * have no insight into from here. Rather than contort this API to track head
 * page references on such ancient kernels, we disabled this special compound
 * page handling on kernels before 4.5, instead just using treating each page
 * within it as a regular PAGESIZE page (which it is). This is slightly less
 * efficient, but makes everything far simpler.
 *
 * We no longer support kernels before 4.5, so in theory none of this is
 * necessary. However, this code is still relatively new in the grand scheme of
 * things, so I'm leaving the ability to compile this out for the moment.
 *
 * Setting/clearing ABD_ITER_COMPOUND_PAGES below enables/disables the special
 * handling, by defining the ABD_ITER_PAGE_SIZE(page) macro to understand
 * compound pages, or not, and compiling in/out the support to detect compound
 * tail pages and move back to the start.
 */

/* On by default */
#define	ABD_ITER_COMPOUND_PAGES

#ifdef ABD_ITER_COMPOUND_PAGES
#define	ABD_ITER_PAGE_SIZE(page)	\
	(PageCompound(page) ? page_size(page) : PAGESIZE)
#else
#define	ABD_ITER_PAGE_SIZE(page)	(PAGESIZE)
#endif

void
abd_iter_page(struct abd_iter *aiter)
{
	if (abd_iter_at_end(aiter)) {
		aiter->iter_page = NULL;
		aiter->iter_page_doff = 0;
		aiter->iter_page_dsize = 0;
		return;
	}

	struct page *page;
	size_t doff, dsize;

	/*
	 * Find the page, and the start of the data within it. This is computed
	 * differently for linear and scatter ABDs; linear is referenced by
	 * virtual memory location, while scatter is referenced by page
	 * pointer.
	 */
	if (abd_is_linear(aiter->iter_abd)) {
		ASSERT3U(aiter->iter_pos, ==, aiter->iter_offset);

		/* memory address at iter_pos */
		void *paddr = ABD_LINEAR_BUF(aiter->iter_abd) + aiter->iter_pos;

		/* struct page for address */
		page = is_vmalloc_addr(paddr) ?
		    vmalloc_to_page(paddr) : virt_to_page(paddr);

		/* offset of address within the page */
		doff = offset_in_page(paddr);
	} else {
		ASSERT(!abd_is_gang(aiter->iter_abd));

		/* current scatter page */
		page = nth_page(sg_page(aiter->iter_sg),
		    aiter->iter_offset >> PAGE_SHIFT);

		/* position within page */
		doff = aiter->iter_offset & (PAGESIZE - 1);
	}

#ifdef ABD_ITER_COMPOUND_PAGES
	if (PageTail(page)) {
		/*
		 * If this is a compound tail page, move back to the head, and
		 * adjust the offset to match. This may let us yield a much
		 * larger amount of data from a single logical page, and so
		 * leave our caller with fewer pages to process.
		 */
		struct page *head = compound_head(page);
		doff += ((page - head) * PAGESIZE);
		page = head;
	}
#endif

	ASSERT(page);

	/*
	 * Compute the maximum amount of data we can take from this page. This
	 * is the smaller of:
	 * - the remaining space in the page
	 * - the remaining space in this scatterlist entry (which may not cover
	 *   the entire page)
	 * - the remaining space in the abd (which may not cover the entire
	 *   scatterlist entry)
	 */
	dsize = MIN(ABD_ITER_PAGE_SIZE(page) - doff,
	    aiter->iter_abd->abd_size - aiter->iter_pos);
	if (!abd_is_linear(aiter->iter_abd))
		dsize = MIN(dsize, aiter->iter_sg->length - aiter->iter_offset);
	ASSERT3U(dsize, >, 0);

	/* final iterator outputs */
	aiter->iter_page = page;
	aiter->iter_page_doff = doff;
	aiter->iter_page_dsize = dsize;
}

/*
 * Note: ABD BIO functions only needed to support vdev_classic. See comments in
 * vdev_disk.c.
 */

/*
 * bio_nr_pages for ABD.
 * @off is the offset in @abd
 */
unsigned long
abd_nr_pages_off(abd_t *abd, unsigned int size, size_t off)
{
	unsigned long pos;

	if (abd_is_gang(abd)) {
		unsigned long count = 0;

		for (abd_t *cabd = abd_gang_get_offset(abd, &off);
		    cabd != NULL && size != 0;
		    cabd = list_next(&ABD_GANG(abd).abd_gang_chain, cabd)) {
			ASSERT3U(off, <, cabd->abd_size);
			int mysize = MIN(size, cabd->abd_size - off);
			count += abd_nr_pages_off(cabd, mysize, off);
			size -= mysize;
			off = 0;
		}
		return (count);
	}

	if (abd_is_linear(abd))
		pos = (unsigned long)abd_to_buf(abd) + off;
	else
		pos = ABD_SCATTER(abd).abd_offset + off;

	return (((pos + size + PAGESIZE - 1) >> PAGE_SHIFT) -
	    (pos >> PAGE_SHIFT));
}

static unsigned int
bio_map(struct bio *bio, void *buf_ptr, unsigned int bio_size)
{
	unsigned int offset, size, i;
	struct page *page;

	offset = offset_in_page(buf_ptr);
	for (i = 0; i < bio->bi_max_vecs; i++) {
		size = PAGE_SIZE - offset;

		if (bio_size <= 0)
			break;

		if (size > bio_size)
			size = bio_size;

		if (is_vmalloc_addr(buf_ptr))
			page = vmalloc_to_page(buf_ptr);
		else
			page = virt_to_page(buf_ptr);

		/*
		 * Some network related block device uses tcp_sendpage, which
		 * doesn't behave well when using 0-count page, this is a
		 * safety net to catch them.
		 */
		ASSERT3S(page_count(page), >, 0);

		if (bio_add_page(bio, page, size, offset) != size)
			break;

		buf_ptr += size;
		bio_size -= size;
		offset = 0;
	}

	return (bio_size);
}

/*
 * bio_map for gang ABD.
 */
static unsigned int
abd_gang_bio_map_off(struct bio *bio, abd_t *abd,
    unsigned int io_size, size_t off)
{
	ASSERT(abd_is_gang(abd));

	for (abd_t *cabd = abd_gang_get_offset(abd, &off);
	    cabd != NULL;
	    cabd = list_next(&ABD_GANG(abd).abd_gang_chain, cabd)) {
		ASSERT3U(off, <, cabd->abd_size);
		int size = MIN(io_size, cabd->abd_size - off);
		int remainder = abd_bio_map_off(bio, cabd, size, off);
		io_size -= (size - remainder);
		if (io_size == 0 || remainder > 0)
			return (io_size);
		off = 0;
	}
	ASSERT0(io_size);
	return (io_size);
}

/*
 * bio_map for ABD.
 * @off is the offset in @abd
 * Remaining IO size is returned
 */
unsigned int
abd_bio_map_off(struct bio *bio, abd_t *abd,
    unsigned int io_size, size_t off)
{
	struct abd_iter aiter;

	ASSERT3U(io_size, <=, abd->abd_size - off);
	if (abd_is_linear(abd))
		return (bio_map(bio, ((char *)abd_to_buf(abd)) + off, io_size));

	ASSERT(!abd_is_linear(abd));
	if (abd_is_gang(abd))
		return (abd_gang_bio_map_off(bio, abd, io_size, off));

	abd_iter_init(&aiter, abd);
	abd_iter_advance(&aiter, off);

	for (int i = 0; i < bio->bi_max_vecs; i++) {
		struct page *pg;
		size_t len, sgoff, pgoff;
		struct scatterlist *sg;

		if (io_size <= 0)
			break;

		sg = aiter.iter_sg;
		sgoff = aiter.iter_offset;
		pgoff = sgoff & (PAGESIZE - 1);
		len = MIN(io_size, PAGESIZE - pgoff);
		ASSERT(len > 0);

		pg = nth_page(sg_page(sg), sgoff >> PAGE_SHIFT);
		if (bio_add_page(bio, pg, len, pgoff) != len)
			break;

		io_size -= len;
		abd_iter_advance(&aiter, len);
	}

	return (io_size);
}

/* Tunable Parameters */
module_param(zfs_abd_scatter_enabled, int, 0644);
MODULE_PARM_DESC(zfs_abd_scatter_enabled,
	"Toggle whether ABD allocations must be linear.");
module_param(zfs_abd_scatter_min_size, int, 0644);
MODULE_PARM_DESC(zfs_abd_scatter_min_size,
	"Minimum size of scatter allocations.");
/* CSTYLED */
module_param(zfs_abd_scatter_max_order, uint, 0644);
MODULE_PARM_DESC(zfs_abd_scatter_max_order,
	"Maximum order allocation used for a scatter ABD.");
