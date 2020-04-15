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
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>

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

kmem_cache_t *abd_subpage_cache[ABD_PGSIZE >> SPA_MINBLOCKSHIFT] = { NULL };

/*
 * We use a scattered SPA_MAXBLOCKSIZE sized ABD whose chunks are
 * just a single zero'd sized zfs_abd_chunk_size buffer. This
 * allows us to conserve memory by only using a single zero buffer
 * for the scatter chunks.
 */
abd_t *abd_zero_scatter = NULL;
static char *abd_zero_buf = NULL;

static void
abd_free_chunk(void *c)
{
	kmem_cache_free(abd_chunk_cache, c);
}

static inline size_t
abd_chunkcnt_for_bytes(size_t size)
{
	return (P2ROUNDUP(size, zfs_abd_chunk_size) / zfs_abd_chunk_size);
}

static size_t
abd_scatter_chunkcnt(abd_t *abd)
{
	VERIFY(!abd_is_linear(abd));
	return (abd_chunkcnt_for_bytes(
	    ABD_SCATTER(abd).abd_offset + abd->abd_size));
}

boolean_t
abd_size_alloc_linear(size_t size)
{
	return (B_FALSE);
}

void
abd_update_scatter_stats(abd_t *abd, abd_stats_op_t op)
{
	size_t n = abd_scatter_chunkcnt(abd);
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	if (op == ABDSTAT_INCR) {
		ABDSTAT_BUMP(abdstat_scatter_cnt);
		ABDSTAT_INCR(abdstat_scatter_data_size, abd->abd_size);
		ABDSTAT_INCR(abdstat_scatter_chunk_waste,
		    n * ABD_SCATTER(abd).abd_chunk_size - abd->abd_size);
	} else {
		ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
		ABDSTAT_INCR(abdstat_scatter_data_size, -(int)abd->abd_size);
		ABDSTAT_INCR(abdstat_scatter_chunk_waste,
		    abd->abd_size - n * ABD_SCATTER(abd).abd_chunk_size);
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
	/*
	 * There is no scatter linear pages in FreeBSD so there is an
	 * if an error if the ABD has been marked as a linear page.
	 */
	VERIFY(!abd_is_linear_page(abd));
	VERIFY3U(ABD_SCATTER(abd).abd_offset, <,
	    zfs_abd_chunk_size);
	VERIFY3U(ABD_SCATTER(abd).abd_offset, <,
	    ABD_SCATTER(abd).abd_chunk_size);
	VERIFY3U(ABD_SCATTER(abd).abd_chunk_size, >=,
	    SPA_MINBLOCKSIZE);

	size_t n = abd_scatter_chunkcnt(abd);

	if (ABD_SCATTER(abd).abd_chunk_size != ABD_PGSIZE) {
		VERIFY3U(n, ==, 1);
		VERIFY3U(ABD_SCATTER(abd).abd_chunk_size, <, ABD_PGSIZE);
		VERIFY3U(abd->abd_size, <=, ABD_SCATTER(abd).abd_chunk_size);
	}

	for (int i = 0; i < n; i++) {
		VERIFY3P(
		    ABD_SCATTER(abd).abd_chunks[i], !=, NULL);
	}
}

static inline int
abd_subpage_cache_index(const size_t size)
{
	const int idx = size >> SPA_MINBLOCKSHIFT;

	if ((size % SPA_MINBLOCKSIZE) == 0)
		return (idx - 1);
	else
		return (idx);
}

static inline uint_t
abd_subpage_enclosing_size(const int i)
{
	return (SPA_MINBLOCKSIZE * (i + 1));
}

void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	VERIFY3U(size, >, 0);
	if (size <= (zfs_abd_chunk_size - SPA_MINBLOCKSIZE)) {
		const int i = abd_subpage_cache_index(size);
		const uint_t s = abd_subpage_enclosing_size(i);
		VERIFY3U(s, >=, size);
		VERIFY3U(s, <, zfs_abd_chunk_size);
		void *c = kmem_cache_alloc(abd_subpage_cache[i], KM_SLEEP);
		ABD_SCATTER(abd).abd_chunks[0] = c;
		ABD_SCATTER(abd).abd_chunk_size = s;
	} else {
		const size_t n = abd_chunkcnt_for_bytes(size);

		for (int i = 0; i < n; i++) {
			void *c = kmem_cache_alloc(abd_chunk_cache, KM_SLEEP);
			ABD_SCATTER(abd).abd_chunks[i] = c;
		}
		ABD_SCATTER(abd).abd_chunk_size = zfs_abd_chunk_size;
	}
}

void
abd_free_chunks(abd_t *abd)
{
	const uint_t abd_cs = ABD_SCATTER(abd).abd_chunk_size;

	if (abd_cs < zfs_abd_chunk_size) {
		VERIFY3U(abd->abd_size, <, zfs_abd_chunk_size);
		VERIFY0(abd_cs % SPA_MINBLOCKSIZE);

		const int idx = abd_subpage_cache_index(abd_cs);

		kmem_cache_free(abd_subpage_cache[idx],
		    ABD_SCATTER(abd).abd_chunks[0]);
	} else {
		const size_t n = abd_scatter_chunkcnt(abd);
		for (int i = 0; i < n; i++) {
			abd_free_chunk(ABD_SCATTER(abd).abd_chunks[i]);
		}
	}
}

abd_t *
abd_alloc_struct_impl(size_t size)
{
	size_t chunkcnt = abd_chunkcnt_for_bytes(size);
	/*
	 * In the event we are allocating a gang ABD, the size passed in
	 * will be 0. We must make sure to set abd_size to the size of an
	 * ABD struct as opposed to an ABD scatter with 0 chunks. The gang
	 * ABD struct allocation accounts for an additional 24 bytes over
	 * a scatter ABD with 0 chunks.
	 */
	size_t abd_size = MAX(sizeof (abd_t),
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]));
	abd_t *abd = kmem_zalloc(abd_size, KM_PUSHPAGE);
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
 * each chunk in the scatterlist will be set to abd_zero_buf.
 */
static void
abd_alloc_zero_scatter(void)
{
	size_t n = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	abd_zero_buf = kmem_cache_alloc(abd_chunk_cache, KM_SLEEP);
	memset(abd_zero_buf, 0, zfs_abd_chunk_size);
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);

	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER | ABD_FLAG_ZEROS;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;

	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;
	ABD_SCATTER(abd_zero_scatter).abd_chunk_size =
	    zfs_abd_chunk_size;

	for (int i = 0; i < n; i++) {
		ABD_SCATTER(abd_zero_scatter).abd_chunks[i] =
		    abd_zero_buf;
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, zfs_abd_chunk_size);
}

static void
abd_free_zero_scatter(void)
{
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)zfs_abd_chunk_size);

	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;
	kmem_cache_free(abd_chunk_cache, abd_zero_buf);
}

void
abd_init(void)
{
	/* check if we guessed ABD_PGSIZE correctly */
	ASSERT3U(ABD_PGSIZE, ==, PAGE_SIZE);

#ifdef DEBUG
	/*
	 * KMF_BUFTAG | KMF_LITE on the abd kmem_caches causes them to waste
	 * up to 50% of their memory for redzone.  Even in DEBUG builds this
	 * therefore should be KMC_NOTOUCH unless there are concerns about
	 * overruns, UAFs, etc involving abd chunks or subpage chunks.
	 *
	 * Additionally these KMF_
	 * flags require the definitions from <sys/kmem_impl.h>
	 */
	// const int cflags = KMF_BUFTAG | KMF_LITE;
	const int cflags = KMC_NOTOUCH;
#else
	const int cflags = KMC_NOTOUCH;
#endif

	abd_chunk_cache = kmem_cache_create("abd_chunk", zfs_abd_chunk_size,
	    ABD_PGSIZE,
	    NULL, NULL, NULL, NULL, abd_arena, cflags);

	abd_ksp = kstat_create("zfs", 0, "abdstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (abd_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (abd_ksp != NULL) {
		abd_ksp->ks_data = &abd_stats;
		kstat_install(abd_ksp);
	}

	abd_alloc_zero_scatter();

	/*
	 * Check at compile time that SPA_MINBLOCKSIZE is 512, because we want
	 * to build sub-page-size linear ABD kmem caches at multiples of
	 * SPA_MINBLOCKSIZE.  If SPA_MINBLOCKSIZE ever changes, a different
	 * layout should be calculated at runtime.
	 *
	 * See also the assertions above the definition of abd_subpbage_cache.
	 */

	_Static_assert(SPA_MINBLOCKSIZE == 512,
	    "unexpected SPA_MINBLOCKSIZE != 512");

	const int step_size = SPA_MINBLOCKSIZE;
	for (int bytes = step_size; bytes < ABD_PGSIZE; bytes += step_size) {
		char name[36];

		(void) snprintf(name, sizeof (name),
		    "abd_subpage_%lu", (ulong_t)bytes);

		const int index = (bytes >> SPA_MINBLOCKSHIFT) - 1;
		VERIFY3U(index, >=, 0);
		VERIFY3U(index, <, ABD_PGSIZE >> SPA_MINBLOCKSHIFT);

		abd_subpage_cache[index] =
		    kmem_cache_create(name, bytes, 512,
		    NULL, NULL, NULL, NULL, abd_subpage_arena, cflags);

		VERIFY3P(abd_subpage_cache[index], !=, NULL);
	}
}

void
abd_fini(void)
{
	const int step_size = SPA_MINBLOCKSIZE;
	for (int bytes = step_size; bytes < ABD_PGSIZE; bytes += step_size) {
		const int index = (bytes >> SPA_MINBLOCKSHIFT) - 1;
		kmem_cache_destroy(abd_subpage_cache[index]);
		abd_subpage_cache[index] = NULL;
	}

	abd_free_zero_scatter();

	if (abd_ksp != NULL) {
		kstat_delete(abd_ksp);
		abd_ksp = NULL;
	}

	kmem_cache_destroy(abd_chunk_cache);
	abd_chunk_cache = NULL;
}

void
abd_free_linear_page(abd_t *abd)
{
	/*
	 * FreeBSD does not have have scatter linear pages
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


/*
 * return an ABD structure that peers into source ABD sabd.  The returned ABD
 * may be new, or the one supplied as abd.  abd and sabd must point to one or
 * more zfs_abd_chunk_size (ABD_PGSIZE) chunks, or point to one and exactly one
 * smaller chunk.
 *
 * The [off, off+size] range must be found within (and thus
 * fit within) the source ABD.
 */

abd_t *
abd_get_offset_scatter(abd_t *abd, abd_t *sabd, size_t off, size_t size)
{
	abd_verify(sabd);
	VERIFY3U(off, <=, sabd->abd_size);

	const uint_t sabd_chunksz = ABD_SCATTER(sabd).abd_chunk_size;

	const size_t new_offset = ABD_SCATTER(sabd).abd_offset + off;

	/* subpage ABD range checking */
	if (sabd_chunksz != zfs_abd_chunk_size) {
		/*  off+size must fit in 1 chunk */
		VERIFY3U(off + size, <=, sabd_chunksz);
		/* new_offset must be in bounds of 1 chunk */
		VERIFY3U(new_offset, <=, sabd_chunksz);
		/* new_offset + size must be in bounds of 1 chunk */
		VERIFY3U(new_offset + size, <=, sabd_chunksz);
	}

	/*
	 * chunkcnt is abd_chunkcnt_for_bytes(size), which rounds
	 * up to the nearest chunk, but we also must take care
	 * of the offset *in the leading chunk*
	 */
	const size_t chunkcnt = (sabd_chunksz != zfs_abd_chunk_size)
	    ? 1
	    : abd_chunkcnt_for_bytes((new_offset % sabd_chunksz) + size);

	/* sanity checks on chunkcnt */
	VERIFY3U(chunkcnt, <=, abd_scatter_chunkcnt(sabd));
	VERIFY3U(chunkcnt, >, 0);

	/* non-subpage sanity checking */
	if (chunkcnt > 1) {
		/* compare with legacy calculation of chunkcnt */
		VERIFY3U(chunkcnt, ==, abd_chunkcnt_for_bytes(
		    (new_offset % zfs_abd_chunk_size) + size));
		/* EITHER subpage chunk (singular) or std chunks */
		VERIFY3U(sabd_chunksz, ==, zfs_abd_chunk_size);
	}

	/*
	 * If an abd struct is provided, it is only the minimum size (and
	 * almost certainly provided as an abd_t embedded in a larger
	 * structure). If we need additional chunks, we need to allocate a
	 * new struct.
	 */
	if (abd != NULL &&
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]) >
	    sizeof (abd_t)) {
		abd = NULL;
	}

	if (abd == NULL)
		abd = abd_alloc_struct(chunkcnt * sabd_chunksz);

	/*
	 * Even if this buf is filesystem metadata, we only track that
	 * if we own the underlying data buffer, which is not true in
	 * this case. Therefore, we don't ever use ABD_FLAG_META here.
	 */

	/* update offset, and sanity check it */
	ABD_SCATTER(abd).abd_offset = new_offset % sabd_chunksz;

	VERIFY3U(ABD_SCATTER(abd).abd_offset, <, sabd_chunksz);
	VERIFY3U(ABD_SCATTER(abd).abd_offset + size, <=,
	    chunkcnt * sabd_chunksz);

	ABD_SCATTER(abd).abd_chunk_size = sabd_chunksz;

	if (chunkcnt > 1) {
		VERIFY3U(ABD_SCATTER(sabd).abd_chunk_size, ==,
		    zfs_abd_chunk_size);
	}

	/* Copy the scatterlist starting at the correct offset */
	(void) memcpy(&ABD_SCATTER(abd).abd_chunks,
	    &ABD_SCATTER(sabd).abd_chunks[new_offset /
	    sabd_chunksz],
	    chunkcnt * sizeof (void *));

	return (abd);
}

static inline size_t
abd_iter_scatter_chunk_offset(struct abd_iter *aiter)
{
	ASSERT(!abd_is_linear(aiter->iter_abd));
	return ((ABD_SCATTER(aiter->iter_abd).abd_offset +
	    aiter->iter_pos) %
	    ABD_SCATTER(aiter->iter_abd).abd_chunk_size);
}

static inline size_t
abd_iter_scatter_chunk_index(struct abd_iter *aiter)
{
	ASSERT(!abd_is_linear(aiter->iter_abd));
	return ((ABD_SCATTER(aiter->iter_abd).abd_offset + aiter->iter_pos)
	    / ABD_SCATTER(aiter->iter_abd).abd_chunk_size);
}

/*
 * Initialize the abd_iter.
 */
void
abd_iter_init(struct abd_iter *aiter, abd_t *abd)
{
	ASSERT(!abd_is_gang(abd));
	abd_verify(abd);
	aiter->iter_abd = abd;
	aiter->iter_pos = 0;
	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
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
	size_t offset = 0;

	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

#if 0
	/* Panic if someone has changed zfs_abd_chunk_size */

	IMPLY(!abd_is_linear(aiter->iter_abd), zfs_abd_chunk_size ==
	    ABD_SCATTER(aiter->iter_abd).abd_chunk_size);
#else
	/*
	 * If scattered, VERIFY that we are using ABD_PGSIZE chunks, or we have
	 * one and only one chunk of less than ABD_PGSIZE.
	 */

	if (!abd_is_linear(aiter->iter_abd)) {
		if (ABD_SCATTER(aiter->iter_abd).abd_chunk_size !=
		    zfs_abd_chunk_size) {
			VERIFY3U(
			    ABD_SCATTER(aiter->iter_abd).abd_chunk_size,
			    <, zfs_abd_chunk_size);
			VERIFY3U(aiter->iter_abd->abd_size,
			    <, zfs_abd_chunk_size);
			VERIFY3U(aiter->iter_abd->abd_size,
			    <=, ABD_SCATTER(aiter->iter_abd).abd_chunk_size);
		}
	}
#endif

	/* There's nothing left to iterate over, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	if (abd_is_linear(aiter->iter_abd)) {
		offset = aiter->iter_pos;
		aiter->iter_mapsize = aiter->iter_abd->abd_size - offset;
		paddr = ABD_LINEAR_BUF(aiter->iter_abd);
	} else {
		size_t index = abd_iter_scatter_chunk_index(aiter);
		IMPLY(ABD_SCATTER(aiter->iter_abd).abd_chunk_size != ABD_PGSIZE,
		    index == 0);
		offset = abd_iter_scatter_chunk_offset(aiter);
		aiter->iter_mapsize = MIN(
		    ABD_SCATTER(aiter->iter_abd).abd_chunk_size
		    - offset,
		    aiter->iter_abd->abd_size - aiter->iter_pos);
		paddr = ABD_SCATTER(aiter->iter_abd).abd_chunks[index];
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
	kmem_cache_reap_now(abd_chunk_cache);

	const int step_size = SPA_MINBLOCKSIZE;
	for (int bytes = step_size; bytes < ABD_PGSIZE; bytes += step_size) {
		const int index = (bytes >> SPA_MINBLOCKSHIFT) - 1;
		kmem_cache_reap_now(abd_subpage_cache[index]);
	}

}
