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
 * Copyright (c) 2020 by Jorgen Lundman. All rights reserved.
 * Copyright (c) 2021 by Sean Doran. All rights reserved.
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
#ifdef DEBUG
#include <sys/kmem_impl.h>
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
 * The size of the chunks ABD allocates. Because the sizes allocated from the
 * kmem_cache can't change, this tunable can only be modified at boot. Changing
 * it at runtime would cause ABD iteration to work incorrectly for ABDs which
 * were allocated with the old size, so a safeguard has been put in place which
 * will cause the machine to panic if you change it and try to access the data
 * within a scattered ABD.
 */

#if defined(__arm64__)
/*
 * On ARM macOS, PAGE_SIZE is not a runtime constant!  So here we have to
 * guess at compile time.  There a balance between fewer kmem_caches, more
 * memory use by "tails" of medium-sized ABDs, and more memory use by
 * accounting structures if we use 4k versus 16k.
 *
 * Since the original *subpage* design expected PAGE_SIZE to be constant and
 * the pre-subpage ABDs used PAGE_SIZE without requiring it to be a
 * compile-time constant, let's use 16k initially and adjust downwards based
 * on feedback.
 */
#define	ABD_PGSIZE	16384
#else
#define	ABD_PGSIZE	PAGE_SIZE
#endif

const static size_t zfs_abd_chunk_size = ABD_PGSIZE;

kmem_cache_t *abd_chunk_cache;
static kstat_t *abd_ksp;

/*
 * Sub-ABD_PGSIZE allocations are segregated into kmem caches.  This may be
 * inefficient or counterproductive if in future the following conditions are
 * not met.
 */
_Static_assert(SPA_MINBLOCKSHIFT == 9, "unexpected SPA_MINSBLOCKSHIFT != 9");
_Static_assert(ISP2(ABD_PGSIZE), "ABD_PGSIZE unexpectedly non power of 2");
_Static_assert(ABD_PGSIZE >= 4096, "ABD_PGSIZE unexpectedly smaller than 4096");
_Static_assert(ABD_PGSIZE <= 16384,
	"ABD_PGSIZE unexpectedly larger than 16384");

#define	SUBPAGE_CACHE_INDICES (ABD_PGSIZE >> SPA_MINBLOCKSHIFT)
kmem_cache_t *abd_subpage_cache[SUBPAGE_CACHE_INDICES] = { NULL };

/*
 * We use a scattered SPA_MAXBLOCKSIZE sized ABD whose chunks are
 * just a single zero'd sized zfs_abd_chunk_size buffer. This
 * allows us to conserve memory by only using a single zero buffer
 * for the scatter chunks.
 */
abd_t *abd_zero_scatter = NULL;
static char *abd_zero_buf = NULL;

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
static size_t zfs_abd_scatter_min_size = ABD_PGSIZE + 1;

kmem_cache_t *abd_chunk_cache;
static kstat_t *abd_ksp;

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
	uint_t n = abd_scatter_chunkcnt(abd);
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	int waste = (n << PAGE_SHIFT) - abd->abd_size;
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
	ASSERT3U(ABD_SCATTER(abd).abd_offset, <, ABD_PGSIZE);
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
		    kmem_cache_alloc(abd_chunk_cache, KM_SLEEP);
	}
}

void
abd_free_chunks(abd_t *abd)
{
	uint_t i, n;

	n = abd_scatter_chunkcnt(abd);
	for (i = 0; i < n; i++) {
		kmem_cache_free(abd_chunk_cache,
		    ABD_SCATTER(abd).abd_chunks[i]);
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
	abd_t *abd = kmem_alloc(abd_size, KM_SLEEP);

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
static void
abd_alloc_zero_scatter(void)
{
	uint_t i, n;

	abd_zero_buf = kmem_alloc(ABD_PGSIZE, KM_SLEEP);
	memset(abd_zero_buf, 0, ABD_PGSIZE);

	n = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);
	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;

	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;

	for (i = 0; i < n; i++) {
		ABD_SCATTER(abd_zero_scatter).abd_chunks[i] =
		    __DECONST(void *, abd_zero_buf);
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, ABD_PGSIZE);
}

static void
abd_free_zero_scatter(void)
{
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)ABD_PGSIZE);

	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;

	kmem_free(abd_zero_buf, ABD_PGSIZE);
	abd_zero_buf = NULL;
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
	abd_chunk_cache = kmem_cache_create("abd_chunk", ABD_PGSIZE, 0,
	    NULL, NULL, NULL, NULL, 0, KMC_NODEBUG | KMC_RECLAIMABLE);

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
	/*
	 * FreeBSD does not have scatter linear pages
	 * so there is an error.
	 */
	VERIFY(0);
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

	/* Copy the scatterlist starting at the correct offset */
	(void) memcpy(&ABD_SCATTER(abd).abd_chunks,
	    &ABD_SCATTER(sabd).abd_chunks[new_offset >> PAGE_SHIFT],
	    chunkcnt * sizeof (void *));

	return (abd);
}

/*
 * Allocate a scatter ABD structure from user pages.
 */
abd_t *
abd_alloc_from_pages(vm_page_t *pages, unsigned long offset, uint64_t size)
{
	VERIFY3U(size, <=, DMU_MAX_ACCESS);
	ASSERT3U(offset, <, PAGE_SIZE);
	ASSERT3P(pages, !=, NULL);

	/* Until we do DIRECTIO */
	VERIFY3U(0, !=, 0);
	abd_t *abd = NULL;
#if 0
	abd_t *abd = abd_alloc_struct(size);
	abd->abd_flags |= ABD_FLAG_OWNER | ABD_FLAG_FROM_PAGES;
	abd->abd_size = size;

	if ((offset + size) <= PAGE_SIZE) {
		/*
		 * There is only a single page worth of data, so we will just
		 * use  a linear ABD. We have to make sure to take into account
		 * the offset though. In all other cases our offset will be 0
		 * as we are always PAGE_SIZE aligned.
		 */
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
#endif
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
	} else {
		offset += ABD_SCATTER(abd).abd_offset;
		paddr = ABD_SCATTER(abd).abd_chunks[offset >> PAGE_SHIFT];
		offset &= PAGE_MASK;
		aiter->iter_mapsize = MIN(ABD_PGSIZE - offset,
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

	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

void
abd_cache_reap_now(void)
{
	/*
	 * This function is called by arc_kmem_reap_soon(), which also invokes
	 * kmem_cache_reap_now() on several other kmem caches.
	 *
	 * kmem_cache_reap_now() now operates on all kmem caches at each
	 * invocation (ignoring its kmem_cache_t argument except for an ASSERT
	 * in DEBUG builds) by invoking kmem_reap().  Previously
	 * kmem_cache_reap_now() would clearing the caches magazine working
	 * set and starting a reap immediately and without regard to the
	 * kmem_reaping compare-and-swap flag.
	 *
	 * Previously in this function we would call kmem_cache_reap_now() for
	 * each of the abd_chunk and subpage kmem caches.  Now, since this
	 * function is called after several kmem_cache_reap_now(), it
	 * can be a noop.
	 */
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
	ASSERT3U(abd->abd_size, >=, 0);
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
 * not change the contents of the ABD. If you want any changes you made to
 * buf to be copied back to abd, use abd_return_buf_copy() instead. If the
 * ABD is not constructed from user pages from Direct I/O then an ASSERT
 * checks to make sure the contents of the buffer have not changed since it was
 * borrowed. We can not ASSERT the contents of the buffer have not changed if
 * it is composed of user pages. While Direct I/O write pages are placed under
 * write protection and can not be changed, this is not the case for Direct I/O
 * reads. The pages of a Direct I/O read could be manipulated at any time.
 * Checksum verifications in the ZIO pipeline check for this issue and handle
 * it by returning an error on checksum verification failure.
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
		if (!abd_is_linear_page(abd))
			zio_buf_free(buf, n);
	} else if (abd_is_linear(abd)) {
		ASSERT3P(buf, ==, abd_to_buf(abd));
	} else if (abd_is_gang(abd)) {
#ifdef ZFS_DEBUG
		/*
		 * We have to be careful with gang ABD's that we do not ASSERT
		 * for any ABD's that contain user pages from Direct I/O. See
		 * the comment above about Direct I/O read buffers possibly
		 * being manipulated. In order to handle this, we jsut iterate
		 * through the gang ABD and only verify ABD's that are not from
		 * user pages.
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
	if (!abd_is_linear(abd)) {
		abd_copy_from_buf(abd, buf, n);
	}
	abd_return_buf(abd, buf, n);
}

/* Tunable Parameters */
module_param(zfs_abd_scatter_enabled, int, 0644);
MODULE_PARM_DESC(zfs_abd_scatter_enabled,
	"Toggle whether ABD allocations must be linear.");
module_param(zfs_abd_scatter_min_size, int, 0644);
MODULE_PARM_DESC(zfs_abd_scatter_min_size,
	"Minimum size of scatter allocations.");
