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

/*
 * The size of the chunks ABD allocates. Because the sizes allocated from the
 * kmem_cache can't change, this tunable can only be modified at boot. Changing
 * it at runtime would cause ABD iteration to work incorrectly for ABDs which
 * were allocated with the old size, so a safeguard has been put in place which
 * will cause the machine to panic if you change it and try to access the data
 * within a scattered ABD.
 */
size_t zfs_abd_chunk_size = 4096;

#if defined(_KERNEL)
SYSCTL_DECL(_vfs_zfs);

SYSCTL_INT(_vfs_zfs, OID_AUTO, abd_scatter_enabled, CTLFLAG_RWTUN,
	&zfs_abd_scatter_enabled, 0, "Enable scattered ARC data buffers");
SYSCTL_ULONG(_vfs_zfs, OID_AUTO, abd_chunk_size, CTLFLAG_RDTUN,
	&zfs_abd_chunk_size, 0, "The size of the chunks ABD allocates");
#endif

kmem_cache_t *abd_chunk_cache;
static kstat_t *abd_ksp;

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

static uint_t
abd_chunkcnt_for_bytes(size_t size)
{
	return (P2ROUNDUP(size, zfs_abd_chunk_size) / zfs_abd_chunk_size);
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
	return (size <= zfs_abd_chunk_size ? B_TRUE : B_FALSE);
}

void
abd_update_scatter_stats(abd_t *abd, abd_stats_op_t op)
{
	uint_t n = abd_scatter_chunkcnt(abd);
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	int waste = n * zfs_abd_chunk_size - abd->abd_size;
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
	 * There is no scatter linear pages in FreeBSD so there is an
	 * if an error if the ABD has been marked as a linear page.
	 */
	ASSERT(!abd_is_linear_page(abd));
	ASSERT3U(ABD_SCATTER(abd).abd_offset, <,
	    zfs_abd_chunk_size);
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
		void *c = kmem_cache_alloc(abd_chunk_cache, KM_PUSHPAGE);
		ASSERT3P(c, !=, NULL);
		ABD_SCATTER(abd).abd_chunks[i] = c;
	}
	ABD_SCATTER(abd).abd_chunk_size = zfs_abd_chunk_size;
}

void
abd_free_chunks(abd_t *abd)
{
	uint_t i, n;

	n = abd_scatter_chunkcnt(abd);
	for (i = 0; i < n; i++) {
		abd_free_chunk(ABD_SCATTER(abd).abd_chunks[i]);
	}
}

abd_t *
abd_alloc_struct(size_t size)
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
	list_link_init(&abd->abd_gang_link);
	mutex_init(&abd->abd_mtx, NULL, MUTEX_DEFAULT, NULL);
	ABDSTAT_INCR(abdstat_struct_size, abd_size);

	return (abd);
}

void
abd_free_struct(abd_t *abd)
{
	uint_t chunkcnt = abd_is_linear(abd) || abd_is_gang(abd) ? 0 :
	    abd_scatter_chunkcnt(abd);
	ssize_t size = MAX(sizeof (abd_t),
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]));
	mutex_destroy(&abd->abd_mtx);
	ASSERT(!list_link_active(&abd->abd_gang_link));
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
	uint_t i, n;

	n = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	abd_zero_buf = kmem_zalloc(zfs_abd_chunk_size, KM_SLEEP);
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);

	abd_zero_scatter->abd_flags = ABD_FLAG_OWNER | ABD_FLAG_ZEROS;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;
	abd_zero_scatter->abd_parent = NULL;
	zfs_refcount_create(&abd_zero_scatter->abd_children);

	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;
	ABD_SCATTER(abd_zero_scatter).abd_chunk_size =
	    zfs_abd_chunk_size;

	for (i = 0; i < n; i++) {
		ABD_SCATTER(abd_zero_scatter).abd_chunks[i] =
		    abd_zero_buf;
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, zfs_abd_chunk_size);
}

static void
abd_free_zero_scatter(void)
{
	zfs_refcount_destroy(&abd_zero_scatter->abd_children);
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)zfs_abd_chunk_size);

	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;
	kmem_free(abd_zero_buf, zfs_abd_chunk_size);
}

void
abd_init(void)
{
	abd_chunk_cache = kmem_cache_create("abd_chunk", zfs_abd_chunk_size, 0,
	    NULL, NULL, NULL, NULL, 0, KMC_NODEBUG);

	abd_ksp = kstat_create("zfs", 0, "abdstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (abd_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (abd_ksp != NULL) {
		abd_ksp->ks_data = &abd_stats;
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
 * This is just a helper function to abd_get_offset_scatter() to alloc a
 * scatter ABD using the calculated chunkcnt based on the offset within the
 * parent ABD.
 */
static abd_t *
abd_alloc_scatter_offset_chunkcnt(size_t chunkcnt)
{
	size_t abd_size = offsetof(abd_t,
	    abd_u.abd_scatter.abd_chunks[chunkcnt]);
	abd_t *abd = kmem_alloc(abd_size, KM_PUSHPAGE);
	ASSERT3P(abd, !=, NULL);
	list_link_init(&abd->abd_gang_link);
	mutex_init(&abd->abd_mtx, NULL, MUTEX_DEFAULT, NULL);
	ABDSTAT_INCR(abdstat_struct_size, abd_size);

	return (abd);
}

abd_t *
abd_get_offset_scatter(abd_t *sabd, size_t off)
{
	abd_t *abd = NULL;

	abd_verify(sabd);
	ASSERT3U(off, <=, sabd->abd_size);

	size_t new_offset = ABD_SCATTER(sabd).abd_offset + off;
	uint_t chunkcnt = abd_scatter_chunkcnt(sabd) -
	    (new_offset / zfs_abd_chunk_size);

	abd = abd_alloc_scatter_offset_chunkcnt(chunkcnt);

	/*
	 * Even if this buf is filesystem metadata, we only track that
	 * if we own the underlying data buffer, which is not true in
	 * this case. Therefore, we don't ever use ABD_FLAG_META here.
	 */
	abd->abd_flags = 0;

	ABD_SCATTER(abd).abd_offset = new_offset % zfs_abd_chunk_size;
	ABD_SCATTER(abd).abd_chunk_size = zfs_abd_chunk_size;

	/* Copy the scatterlist starting at the correct offset */
	(void) memcpy(&ABD_SCATTER(abd).abd_chunks,
	    &ABD_SCATTER(sabd).abd_chunks[new_offset /
	    zfs_abd_chunk_size],
	    chunkcnt * sizeof (void *));

	return (abd);
}

static inline size_t
abd_iter_scatter_chunk_offset(struct abd_iter *aiter)
{
	ASSERT(!abd_is_linear(aiter->iter_abd));
	return ((ABD_SCATTER(aiter->iter_abd).abd_offset +
	    aiter->iter_pos) % zfs_abd_chunk_size);
}

static inline size_t
abd_iter_scatter_chunk_index(struct abd_iter *aiter)
{
	ASSERT(!abd_is_linear(aiter->iter_abd));
	return ((ABD_SCATTER(aiter->iter_abd).abd_offset +
	    aiter->iter_pos) / zfs_abd_chunk_size);
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

	/* Panic if someone has changed zfs_abd_chunk_size */
	IMPLY(!abd_is_linear(aiter->iter_abd), zfs_abd_chunk_size ==
	    ABD_SCATTER(aiter->iter_abd).abd_chunk_size);

	/* There's nothing left to iterate over, so do nothing */
	if (abd_iter_at_end(aiter))
		return;

	if (abd_is_linear(aiter->iter_abd)) {
		offset = aiter->iter_pos;
		aiter->iter_mapsize = aiter->iter_abd->abd_size - offset;
		paddr = ABD_LINEAR_BUF(aiter->iter_abd);
	} else {
		size_t index = abd_iter_scatter_chunk_index(aiter);
		offset = abd_iter_scatter_chunk_offset(aiter);
		aiter->iter_mapsize = MIN(zfs_abd_chunk_size - offset,
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
	kmem_cache_reap_soon(abd_chunk_cache);
}
