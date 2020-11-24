/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

/*
 * See abd.c for a general overview of the arc buffered data (ABD).
 *
 * Using a large proportion of scattered ABDs decreases ARC fragmentation since
 * when we are at the limit of allocatable space, using equal-size chunks will
 * allow us to quickly reclaim enough space for a new large allocation (assuming
 * it is also scattered).
 *
 * ABDs are allocated scattered by default unless the caller uses
 * abd_alloc_linear() or zfs_abd_scatter_enabled is disabled.
 */

#include <sys/abd_impl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>

#ifdef _KERNEL
#include <sys/vm.h>
#include <vm/vm_page.h>
#endif

typedef struct abd_stats {
	kstat_named_t abdstat_struct_size;
	kstat_named_t abdstat_scatter_cnt;
	kstat_named_t abdstat_scatter_data_size;
	kstat_named_t abdstat_scatter_chunk_waste;
	kstat_named_t abdstat_linear_cnt;
	kstat_named_t abdstat_linear_data_size;
} abd_stats_t;

static abd_stats_t abd_stats = {
	/* Amount of memory occupied by all of the abd_t struct allocations */
	{ "struct_size",			KSTAT_DATA_UINT64 },
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
	 * The number of linear ABDs which are currently allocated, excluding
	 * ABDs which don't own their data (for instance the ones which were
	 * allocated through abd_get_offset() and abd_get_from_buf()). If an
	 * ABD takes ownership of its buf then it will become tracked.
	 */
	{ "linear_cnt",				KSTAT_DATA_UINT64 },
	/* Amount of data stored in all linear ABDs tracked by linear_cnt */
	{ "linear_data_size",			KSTAT_DATA_UINT64 },
};

struct {
	wmsum_t abdstat_struct_size;
	wmsum_t abdstat_scatter_cnt;
	wmsum_t abdstat_scatter_data_size;
	wmsum_t abdstat_scatter_chunk_waste;
	wmsum_t abdstat_linear_cnt;
	wmsum_t abdstat_linear_data_size;
} abd_sums;

/*
 * zfs_abd_scatter_min_size is the minimum allocation size to use scatter
 * ABD's for.  Smaller allocations will use linear ABD's which use
 * zio_[data_]buf_alloc().
 *
 * Scatter ABD's use at least one page each, so sub-page allocations waste
 * some space when allocated as scatter (e.g. 2KB scatter allocation wastes
 * half of each page).  Using linear ABD's for small allocations means that
 * they will be put on slabs which contain many allocations.
 *
 * Linear ABDs for multi-page allocations are easier to use, and in some cases
 * it allows to avoid buffer copying.  But allocation and especially free
 * of multi-page linear ABDs are expensive operations due to KVA mapping and
 * unmapping, and with time they cause KVA fragmentations.
 */
static size_t zfs_abd_scatter_min_size = PAGE_SIZE + 1;

#if defined(_KERNEL)
SYSCTL_DECL(_vfs_zfs);

SYSCTL_INT(_vfs_zfs, OID_AUTO, abd_scatter_enabled, CTLFLAG_RWTUN,
	&zfs_abd_scatter_enabled, 0, "Enable scattered ARC data buffers");
SYSCTL_ULONG(_vfs_zfs, OID_AUTO, abd_scatter_min_size, CTLFLAG_RWTUN,
	&zfs_abd_scatter_min_size, 0, "Minimum size of scatter allocations.");
#endif

kmem_cache_t *abd_chunk_cache;
static kstat_t *abd_ksp;

/*
 * We use a scattered SPA_MAXBLOCKSIZE sized ABD whose chunks are
 * just a single zero'd page-sized buffer. This allows us to conserve
 * memory by only using a single zero buffer for the scatter chunks.
 */
abd_t *abd_zero_scatter = NULL;

static uint_t
abd_chunkcnt_for_bytes(size_t size)
{
	return ((size + PAGE_MASK) >> PAGE_SHIFT);
}

static inline uint_t
abd_scatter_chunkcnt(abd_t *abd)
{
	ASSERT(!abd_is_linear(abd));
	return (abd_chunkcnt_for_bytes(
	    ABD_SCATTER(abd).abd_offset + abd->abd_size));
}

boolean_t
abd_size_alloc_linear(size_t size)
{
	return (!zfs_abd_scatter_enabled || size < zfs_abd_scatter_min_size);
}

void
abd_update_scatter_stats(abd_t *abd, abd_stats_op_t op)
{
	uint_t n;

	if (abd_is_from_pages(abd))
		n = abd_chunkcnt_for_bytes(abd->abd_size);
	else
		n = abd_scatter_chunkcnt(abd);
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	int waste = (n << PAGE_SHIFT) - abd->abd_size;
	ASSERT3U(n, >, 0);
	ASSERT3S(waste, >=, 0);
	IMPLY(abd_is_linear_page(abd), waste < PAGE_SIZE);
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
	uint_t i, n;

	/*
	 * There is no scatter linear pages in FreeBSD so there is
	 * an error if the ABD has been marked as a linear page.
	 */
	ASSERT(!abd_is_linear_page(abd));
	ASSERT3U(ABD_SCATTER(abd).abd_offset, <, PAGE_SIZE);
	n = abd_scatter_chunkcnt(abd);
	for (i = 0; i < n; i++) {
		ASSERT3P(ABD_SCATTER(abd).abd_chunks[i], !=, NULL);
	}
}

void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	uint_t i, n;

	n = abd_chunkcnt_for_bytes(size);
	for (i = 0; i < n; i++) {
		ABD_SCATTER(abd).abd_chunks[i] =
		    kmem_cache_alloc(abd_chunk_cache, KM_PUSHPAGE);
	}
}

void
abd_free_chunks(abd_t *abd)
{
	uint_t i, n;

	/*
	 * Scatter ABDs may be constructed by abd_alloc_from_pages() from
	 * an array of pages. In which case they should not be freed.
	 */
	if (!abd_is_from_pages(abd)) {
		n = abd_scatter_chunkcnt(abd);
		for (i = 0; i < n; i++) {
			kmem_cache_free(abd_chunk_cache,
			    ABD_SCATTER(abd).abd_chunks[i]);
		}
	}
}

abd_t *
abd_alloc_struct_impl(size_t size)
{
	uint_t chunkcnt = abd_chunkcnt_for_bytes(size);
	/*
	 * In the event we are allocating a gang ABD, the size passed in
	 * will be 0. We must make sure to set abd_size to the size of an
	 * ABD struct as opposed to an ABD scatter with 0 chunks. The gang
	 * ABD struct allocation accounts for an additional 24 bytes over
	 * a scatter ABD with 0 chunks.
	 */
	size_t abd_size = MAX(sizeof (abd_t),
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]));
	abd_t *abd = kmem_alloc(abd_size, KM_PUSHPAGE);
	ASSERT3P(abd, !=, NULL);
	ABDSTAT_INCR(abdstat_struct_size, abd_size);

	return (abd);
}

void
abd_free_struct_impl(abd_t *abd)
{
	uint_t chunkcnt = abd_is_linear(abd) || abd_is_gang(abd) ? 0 :
	    abd_scatter_chunkcnt(abd);
	ssize_t size = MAX(sizeof (abd_t),
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]));
	kmem_free(abd, size);
	ABDSTAT_INCR(abdstat_struct_size, -size);
}

/*
 * Allocate scatter ABD of size SPA_MAXBLOCKSIZE, where
 * each chunk in the scatterlist will be set to the same area.
 */
_Static_assert(ZERO_REGION_SIZE >= PAGE_SIZE, "zero_region too small");
static void
abd_alloc_zero_scatter(void)
{
	uint_t i, n;

	n = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);
	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER | ABD_FLAG_ZEROS;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;

	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;

	for (i = 0; i < n; i++) {
		ABD_SCATTER(abd_zero_scatter).abd_chunks[i] =
		    __DECONST(void *, zero_region);
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, PAGE_SIZE);
}

static void
abd_free_zero_scatter(void)
{
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)PAGE_SIZE);

	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;
}

static int
abd_kstats_update(kstat_t *ksp, int rw)
{
	abd_stats_t *as = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	as->abdstat_struct_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_struct_size);
	as->abdstat_scatter_cnt.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_cnt);
	as->abdstat_scatter_data_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_data_size);
	as->abdstat_scatter_chunk_waste.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_chunk_waste);
	as->abdstat_linear_cnt.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_linear_cnt);
	as->abdstat_linear_data_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_linear_data_size);
	return (0);
}

void
abd_init(void)
{
	abd_chunk_cache = kmem_cache_create("abd_chunk", PAGE_SIZE, 0,
	    NULL, NULL, NULL, NULL, 0, KMC_NODEBUG);

	wmsum_init(&abd_sums.abdstat_struct_size, 0);
	wmsum_init(&abd_sums.abdstat_scatter_cnt, 0);
	wmsum_init(&abd_sums.abdstat_scatter_data_size, 0);
	wmsum_init(&abd_sums.abdstat_scatter_chunk_waste, 0);
	wmsum_init(&abd_sums.abdstat_linear_cnt, 0);
	wmsum_init(&abd_sums.abdstat_linear_data_size, 0);

	abd_ksp = kstat_create("zfs", 0, "abdstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (abd_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (abd_ksp != NULL) {
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
	wmsum_fini(&abd_sums.abdstat_scatter_cnt);
	wmsum_fini(&abd_sums.abdstat_scatter_data_size);
	wmsum_fini(&abd_sums.abdstat_scatter_chunk_waste);
	wmsum_fini(&abd_sums.abdstat_linear_cnt);
	wmsum_fini(&abd_sums.abdstat_linear_data_size);

	kmem_cache_destroy(abd_chunk_cache);
	abd_chunk_cache = NULL;
}

void
abd_free_linear_page(abd_t *abd)
{
#if defined(_KERNEL)
	ASSERT3P(abd->abd_u.abd_linear.sf, !=, NULL);
	zfs_unmap_page(abd->abd_u.abd_linear.sf);

	abd_update_scatter_stats(abd, ABDSTAT_DECR);
#else
	/*
	 * The ABD flag ABD_FLAG_LINEAR_PAGE should only be set in
	 * abd_alloc_from_pages(), which is strictly in kernel space.
	 * So if we have gotten here outside of kernel space we have
	 * an issue.
	 */
	VERIFY(0);
#endif
}

/*
 * If we're going to use this ABD for doing I/O using the block layer, the
 * consumer of the ABD data doesn't care if it's scattered or not, and we don't
 * plan to store this ABD in memory for a long period of time, we should
 * allocate the ABD type that requires the least data copying to do the I/O.
 *
 * Currently this is linear ABDs, however if ldi_strategy() can ever issue I/Os
 * using a scatter/gather list we should switch to that and replace this call
 * with vanilla abd_alloc().
 */
abd_t *
abd_alloc_for_io(size_t size, boolean_t is_metadata)
{
	return (abd_alloc_linear(size, is_metadata));
}

static abd_t *
abd_get_offset_from_pages(abd_t *abd, abd_t *sabd, size_t chunkcnt,
    size_t new_offset)
{
	ASSERT(abd_is_from_pages(sabd));

	/*
	 * Set the child child chunks to point at the parent chunks as
	 * the chunks are just pages and we don't want to copy them.
	 */
	size_t parent_offset = new_offset / PAGE_SIZE;
	ASSERT3U(parent_offset, <, abd_scatter_chunkcnt(sabd));
	for (int i = 0; i < chunkcnt; i++)
		ABD_SCATTER(abd).abd_chunks[i] =
		    ABD_SCATTER(sabd).abd_chunks[parent_offset + i];

	abd->abd_flags |= ABD_FLAG_FROM_PAGES;
	return (abd);
}

abd_t *
abd_get_offset_scatter(abd_t *abd, abd_t *sabd, size_t off,
    size_t size)
{
	abd_verify(sabd);
	ASSERT3U(off, <=, sabd->abd_size);

	size_t new_offset = ABD_SCATTER(sabd).abd_offset + off;
	size_t chunkcnt = abd_chunkcnt_for_bytes(
	    (new_offset & PAGE_MASK) + size);

	ASSERT3U(chunkcnt, <=, abd_scatter_chunkcnt(sabd));

	/*
	 * If an abd struct is provided, it is only the minimum size.  If we
	 * need additional chunks, we need to allocate a new struct.
	 */
	if (abd != NULL &&
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]) >
	    sizeof (abd_t)) {
		abd = NULL;
	}

	if (abd == NULL)
		abd = abd_alloc_struct(chunkcnt << PAGE_SHIFT);

	/*
	 * Even if this buf is filesystem metadata, we only track that
	 * if we own the underlying data buffer, which is not true in
	 * this case. Therefore, we don't ever use ABD_FLAG_META here.
	 */

	ABD_SCATTER(abd).abd_offset = new_offset & PAGE_MASK;

	if (abd_is_from_pages(sabd)) {
		return (abd_get_offset_from_pages(abd, sabd, chunkcnt,
		    new_offset));
	}

	/* Copy the scatterlist starting at the correct offset */
	(void) memcpy(&ABD_SCATTER(abd).abd_chunks,
	    &ABD_SCATTER(sabd).abd_chunks[new_offset >> PAGE_SHIFT],
	    chunkcnt * sizeof (void *));

	return (abd);
}

#ifdef _KERNEL
/*
 * Allocate a scatter ABD structure from user pages.
 */
abd_t *
abd_alloc_from_pages(vm_page_t *pages, unsigned long offset, uint64_t size)
{
	VERIFY3U(size, <=, DMU_MAX_ACCESS);
	ASSERT3U(offset, <, PAGE_SIZE);
	ASSERT3P(pages, !=, NULL);

	abd_t *abd = abd_alloc_struct(size);
	abd->abd_flags |= ABD_FLAG_OWNER | ABD_FLAG_FROM_PAGES;
	abd->abd_size = size;

	if (size < PAGE_SIZE) {
		/*
		 * We do not have a full page so we will just use  a linear ABD.
		 * We have to make sure to take into account the offset though.
		 * In all other cases our offset will be 0 as we are always
		 * PAGE_SIZE aligned.
		 */
		ASSERT3U(offset + size, <=, PAGE_SIZE);
		abd->abd_flags |= ABD_FLAG_LINEAR | ABD_FLAG_LINEAR_PAGE;
		ABD_LINEAR_BUF(abd) = (char *)zfs_map_page(pages[0],
		    &abd->abd_u.abd_linear.sf) + offset;
	} else {
		ABD_SCATTER(abd).abd_offset = offset;
		ASSERT0(ABD_SCATTER(abd).abd_offset);

		/*
		 * Setting the ABD's abd_chunks to point to the user pages.
		 */
		for (int i = 0; i < abd_chunkcnt_for_bytes(size); i++)
			ABD_SCATTER(abd).abd_chunks[i] = pages[i];
	}

	abd_update_scatter_stats(abd, ABDSTAT_INCR);

	return (abd);
}

#endif /* _KERNEL */

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
}

/*
 * This is just a helper function to see if we have exhausted the
 * abd_iter and reached the end.
 */
boolean_t
abd_iter_at_end(struct abd_iter *aiter)
{
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
	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	/* There's nothing left to advance to, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	aiter->iter_pos += amount;
}

/*
 * Map the current chunk into aiter. This can be safely called when the aiter
 * has already exhausted, in which case this does nothing.
 */
void
abd_iter_map(struct abd_iter *aiter)
{
	void *paddr;

	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	/* There's nothing left to iterate over, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	abd_t *abd = aiter->iter_abd;
	size_t offset = aiter->iter_pos;
	if (abd_is_linear(abd)) {
		aiter->iter_mapsize = abd->abd_size - offset;
		paddr = ABD_LINEAR_BUF(abd);
#if defined(_KERNEL)
	} else if (abd_is_from_pages(abd)) {
		aiter->sf = NULL;
		offset += ABD_SCATTER(abd).abd_offset;
		size_t index = offset / PAGE_SIZE;
		offset &= PAGE_MASK;
		aiter->iter_mapsize = MIN(PAGE_SIZE - offset,
		    abd->abd_size - aiter->iter_pos);
		paddr = zfs_map_page(
		    ABD_SCATTER(aiter->iter_abd).abd_chunks[index],
		    &aiter->sf);
#endif
	} else {
		offset += ABD_SCATTER(abd).abd_offset;
		paddr = ABD_SCATTER(abd).abd_chunks[offset >> PAGE_SHIFT];
		offset &= PAGE_MASK;
		aiter->iter_mapsize = MIN(PAGE_SIZE - offset,
		    abd->abd_size - aiter->iter_pos);
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
	if (!abd_iter_at_end(aiter)) {
		ASSERT3P(aiter->iter_mapaddr, !=, NULL);
		ASSERT3U(aiter->iter_mapsize, >, 0);
	}

#if defined(_KERNEL)
	if (abd_is_from_pages(aiter->iter_abd) &&
	    !abd_is_linear_page(aiter->iter_abd)) {
		ASSERT3P(aiter->sf, !=, NULL);
		zfs_unmap_page(aiter->sf);
	}
#endif

	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

void
abd_cache_reap_now(void)
{
	kmem_cache_reap_soon(abd_chunk_cache);
}

/*
 * Borrow a raw buffer from an ABD without copying the contents of the ABD
 * into the buffer. If the ABD is scattered, this will alloate a raw buffer
 * whose contents are undefined. To copy over the existing data in the ABD, use
 * abd_borrow_buf_copy() instead.
 */
void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	void *buf;
	abd_verify(abd);

	if (abd_is_linear(abd)) {
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
	if (!abd_is_linear(abd)) {
		abd_copy_to_buf(buf, abd, n);
	}
	return (buf);
}

/*
 * Return a borrowed raw buffer to an ABD. If the ABD is scattered, this will
 * no change the contents of the ABD and will ASSERT that you didn't modify
 * the buffer since it was borrowed. If you want any changes you made to buf to
 * be copied back to abd, use abd_return_buf_copy() instead.
 */
void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, n);
#ifdef ZFS_DEBUG
	(void) zfs_refcount_remove_many(&abd->abd_children, n, buf);
#endif
	if (abd_is_linear(abd)) {
		ASSERT3P(buf, ==, abd_to_buf(abd));
	} else {
		ASSERT0(abd_cmp_buf(abd, buf, n));
		zio_buf_free(buf, n);
	}
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	if (!abd_is_linear(abd)) {
		abd_copy_from_buf(abd, buf, n);
	}
	abd_return_buf(abd, buf, n);
}
