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

#include <sys/abd_impl.h>
#include <sys/param.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>

/*
 * We're simulating scatter/gather with 4K allocations, since that's more like
 * what a typical kernel does.
 */
#define	ABD_PAGESIZE	(4096)
#define	ABD_PAGESHIFT	(12)
#define	ABD_PAGEMASK	(ABD_PAGESIZE-1)

/*
 * See rationale in module/os/linux/zfs/abd_os.c, but in userspace this is
 * mostly useful to get a mix of linear and scatter ABDs for testing.
 */
#define	ABD_SCATTER_MIN_SIZE	(512 * 3)

abd_t *abd_zero_scatter = NULL;

static uint_t
abd_iovcnt_for_bytes(size_t size)
{
	/*
	 * Each iovec points to a 4K page. There's no real reason to do this
	 * in userspace, but our whole point here is to make it feel a bit
	 * more like a real paged memory model.
	 */
	return (P2ROUNDUP(size, ABD_PAGESIZE) / ABD_PAGESIZE);
}

abd_t *
abd_alloc_struct_impl(size_t size)
{
	/*
	 * Zero-sized means it will be used for a linear or gang abd, so just
	 * allocate the abd itself and return.
	 */
	if (size == 0)
		return (umem_alloc(sizeof (abd_t), UMEM_NOFAIL));

	/*
	 * Allocating for a scatter abd, so compute how many ABD_PAGESIZE
	 * iovecs we will need to hold this size. Append that allocation to the
	 * end. Note that struct abd_scatter has includes abd_iov[1], so we
	 * allocate one less iovec than we need.
	 *
	 * Note we're not allocating the pages proper, just the iovec pointers.
	 * That's down in abd_alloc_chunks. We _could_ do it here in a single
	 * allocation, but it's fiddly and harder to read for no real gain.
	 */
	uint_t n = abd_iovcnt_for_bytes(size);
	abd_t *abd = umem_alloc(sizeof (abd_t) + (n-1) * sizeof (struct iovec),
	    UMEM_NOFAIL);
	ABD_SCATTER(abd).abd_offset = 0;
	ABD_SCATTER(abd).abd_iovcnt = n;
	return (abd);
}

void
abd_free_struct_impl(abd_t *abd)
{
	/* For scatter, compute the extra amount we need to free */
	uint_t iovcnt =
	    abd_is_linear(abd) || abd_is_gang(abd) ?
	    0 : (ABD_SCATTER(abd).abd_iovcnt - 1);
	umem_free(abd, sizeof (abd_t) + iovcnt * sizeof (struct iovec));
}

void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	/*
	 * We've already allocated the iovec array; ensure that the wanted size
	 * actually matches, otherwise the caller has made a mistake somewhere.
	 */
	uint_t n = ABD_SCATTER(abd).abd_iovcnt;
	ASSERT3U(n, ==, abd_iovcnt_for_bytes(size));

	/*
	 * Allocate a ABD_PAGESIZE region for each iovec.
	 */
	struct iovec *iov = ABD_SCATTER(abd).abd_iov;
	for (int i = 0; i < n; i++) {
		iov[i].iov_base =
		    umem_alloc_aligned(ABD_PAGESIZE, ABD_PAGESIZE, UMEM_NOFAIL);
		iov[i].iov_len = ABD_PAGESIZE;
	}
}

void
abd_free_chunks(abd_t *abd)
{
	uint_t n = ABD_SCATTER(abd).abd_iovcnt;
	struct iovec *iov = ABD_SCATTER(abd).abd_iov;
	for (int i = 0; i < n; i++)
		umem_free_aligned(iov[i].iov_base, ABD_PAGESIZE);
}

boolean_t
abd_size_alloc_linear(size_t size)
{
	return (size < ABD_SCATTER_MIN_SIZE);
}

void
abd_update_scatter_stats(abd_t *abd, abd_stats_op_t op)
{
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	int waste = P2ROUNDUP(abd->abd_size, ABD_PAGESIZE) - abd->abd_size;
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
#ifdef ZFS_DEBUG
	/*
	 * scatter abds shall have:
	 * - at least one iovec
	 * - all iov_base point somewhere
	 * - all iov_len are ABD_PAGESIZE
	 * - offset set within the abd pages somewhere
	 */
	uint_t n = ABD_SCATTER(abd).abd_iovcnt;
	ASSERT3U(n, >, 0);

	uint_t len = 0;
	for (int i = 0; i < n; i++) {
		ASSERT3P(ABD_SCATTER(abd).abd_iov[i].iov_base, !=, NULL);
		ASSERT3U(ABD_SCATTER(abd).abd_iov[i].iov_len, ==, ABD_PAGESIZE);
		len += ABD_PAGESIZE;
	}

	ASSERT3U(ABD_SCATTER(abd).abd_offset, <, len);
#endif
}

void
abd_init(void)
{
	/*
	 * Create the "zero" scatter abd. This is always the size of the
	 * largest possible block, but only actually has a single allocated
	 * page, which all iovecs in the abd point to.
	 */
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);
	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;

	void *zero =
	    umem_alloc_aligned(ABD_PAGESIZE, ABD_PAGESIZE, UMEM_NOFAIL);
	memset(zero, 0, ABD_PAGESIZE);

	uint_t n = abd_iovcnt_for_bytes(SPA_MAXBLOCKSIZE);
	struct iovec *iov = ABD_SCATTER(abd_zero_scatter).abd_iov;
	for (int i = 0; i < n; i++) {
		iov[i].iov_base = zero;
		iov[i].iov_len = ABD_PAGESIZE;
	}
}

void
abd_fini(void)
{
	umem_free_aligned(
	    ABD_SCATTER(abd_zero_scatter).abd_iov[0].iov_base, ABD_PAGESIZE);
	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;
}

void
abd_free_linear_page(abd_t *abd)
{
	/*
	 * LINEAR_PAGE is specific to the Linux kernel; we never set this
	 * flag, so this will never be called.
	 */
	(void) abd;
	PANIC("unreachable");
}

abd_t *
abd_alloc_for_io(size_t size, boolean_t is_metadata)
{
	return (abd_alloc(size, is_metadata));
}

abd_t *
abd_get_offset_scatter(abd_t *dabd, abd_t *sabd, size_t off, size_t size)
{

	/*
	 * Create a new scatter dabd by borrowing data pages from sabd to cover
	 * off+size.
	 *
	 * sabd is an existing scatter abd with a set of iovecs, each covering
	 * an ABD_PAGESIZE (4K) allocation. It's "zero" is at abd_offset.
	 *
	 *   [........][........][........][........]
	 *      ^- sabd_offset
	 *
	 * We want to produce a new abd, referencing those allocations at the
	 * given offset.
	 *
	 *   [........][........][........][........]
	 *                    ^- dabd_offset = sabd_offset + off
	 *                                        ^- dabd_offset + size
	 *
	 * In this example, dabd needs three iovecs. The first iovec is offset
	 * 0, so the final dabd_offset is masked back into the first iovec.
	 *
	 *             [........][........][........]
	 *                    ^- dabd_offset
	 */
	size_t soff = ABD_SCATTER(sabd).abd_offset + off;
	size_t doff = soff & ABD_PAGEMASK;
	size_t iovcnt = abd_iovcnt_for_bytes(doff + size);

	/*
	 * If the passed-in abd has enough allocated iovecs already, reuse it.
	 * Otherwise, make a new one. The caller will free the original if the
	 * one it gets back is not the same.
	 *
	 * Note that it's ok if we reuse an abd with more iovecs than we need.
	 * abd_size has the usable amount of data, and the abd does not own the
	 * pages referenced by the iovecs. At worst, they're holding dangling
	 * pointers that we'll never use anyway.
	 */
	if (dabd == NULL || ABD_SCATTER(dabd).abd_iovcnt < iovcnt)
		dabd = abd_alloc_struct(iovcnt << ABD_PAGESHIFT);

	/* Set offset into first page in view */
	ABD_SCATTER(dabd).abd_offset = doff;

	/* Copy the wanted iovecs from the source to the dest */
	memcpy(&ABD_SCATTER(dabd).abd_iov[0],
	    &ABD_SCATTER(sabd).abd_iov[soff >> ABD_PAGESHIFT],
	    iovcnt * sizeof (struct iovec));

	return (dabd);
}

void
abd_iter_init(struct abd_iter *aiter, abd_t *abd)
{
	ASSERT(!abd_is_gang(abd));
	abd_verify(abd);
	memset(aiter, 0, sizeof (struct abd_iter));
	aiter->iter_abd = abd;
}

boolean_t
abd_iter_at_end(struct abd_iter *aiter)
{
	ASSERT3U(aiter->iter_pos, <=, aiter->iter_abd->abd_size);
	return (aiter->iter_pos == aiter->iter_abd->abd_size);
}

void
abd_iter_advance(struct abd_iter *aiter, size_t amount)
{
	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	if (abd_iter_at_end(aiter))
		return;

	aiter->iter_pos += amount;
	ASSERT3U(aiter->iter_pos, <=, aiter->iter_abd->abd_size);
}

void
abd_iter_map(struct abd_iter *aiter)
{
	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	if (abd_iter_at_end(aiter))
		return;

	if (abd_is_linear(aiter->iter_abd)) {
		aiter->iter_mapaddr =
		    ABD_LINEAR_BUF(aiter->iter_abd) + aiter->iter_pos;
		aiter->iter_mapsize =
		    aiter->iter_abd->abd_size - aiter->iter_pos;
		return;
	}

	/*
	 * For scatter, we index into the appropriate iovec, and return the
	 * smaller of the amount requested, or up to the end of the page.
	 */
	size_t poff = aiter->iter_pos + ABD_SCATTER(aiter->iter_abd).abd_offset;

	ASSERT3U(poff >> ABD_PAGESHIFT, <=,
	    ABD_SCATTER(aiter->iter_abd).abd_iovcnt);
	struct iovec *iov = &ABD_SCATTER(aiter->iter_abd).
	    abd_iov[poff >> ABD_PAGESHIFT];

	aiter->iter_mapsize = MIN(ABD_PAGESIZE - (poff & ABD_PAGEMASK),
	    aiter->iter_abd->abd_size - aiter->iter_pos);
	ASSERT3U(aiter->iter_mapsize, <=, ABD_PAGESIZE);

	aiter->iter_mapaddr = iov->iov_base + (poff & ABD_PAGEMASK);
}

void
abd_iter_unmap(struct abd_iter *aiter)
{
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
