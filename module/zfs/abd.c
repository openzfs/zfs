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
 * Copyright (c) 2019 by Delphix. All rights reserved.
 */

/*
 * ARC buffer data (ABD).
 *
 * ABDs are an abstract data structure for the ARC which can use two
 * different ways of storing the underlying data:
 *
 * (a) Linear buffer. In this case, all the data in the ABD is stored in one
 *     contiguous buffer in memory (from a zio_[data_]buf_* kmem cache).
 *
 *         +-------------------+
 *         | ABD (linear)      |
 *         |   abd_flags = ... |
 *         |   abd_size = ...  |     +--------------------------------+
 *         |   abd_buf ------------->| raw buffer of size abd_size    |
 *         +-------------------+     +--------------------------------+
 *              no abd_chunks
 *
 * (b) Scattered buffer. In this case, the data in the ABD is split into
 *     equal-sized chunks (from the abd_chunk_cache kmem_cache), with pointers
 *     to the chunks recorded in an array at the end of the ABD structure.
 *
 *         +-------------------+
 *         | ABD (scattered)   |
 *         |   abd_flags = ... |
 *         |   abd_size = ...  |
 *         |   abd_offset = 0  |                           +-----------+
 *         |   abd_chunks[0] ----------------------------->| chunk 0   |
 *         |   abd_chunks[1] ---------------------+        +-----------+
 *         |   ...             |                  |        +-----------+
 *         |   abd_chunks[N-1] ---------+         +------->| chunk 1   |
 *         +-------------------+        |                  +-----------+
 *                                      |                      ...
 *                                      |                  +-----------+
 *                                      +----------------->| chunk N-1 |
 *                                                         +-----------+
 *
 * In addition to directly allocating a linear or scattered ABD, it is also
 * possible to create an ABD by requesting the "sub-ABD" starting at an offset
 * within an existing ABD. In linear buffers this is simple (set abd_buf of
 * the new ABD to the starting point within the original raw buffer), but
 * scattered ABDs are a little more complex. The new ABD makes a copy of the
 * relevant abd_chunks pointers (but not the underlying data). However, to
 * provide arbitrary rather than only chunk-aligned starting offsets, it also
 * tracks an abd_offset field which represents the starting point of the data
 * within the first chunk in abd_chunks. For both linear and scattered ABDs,
 * creating an offset ABD marks the original ABD as the offset's parent, and the
 * original ABD's abd_children refcount is incremented. This data allows us to
 * ensure the root ABD isn't deleted before its children.
 *
 * Most consumers should never need to know what type of ABD they're using --
 * the ABD public API ensures that it's possible to transparently switch from
 * using a linear ABD to a scattered one when doing so would be beneficial.
 *
 * If you need to use the data within an ABD directly, if you know it's linear
 * (because you allocated it) you can use abd_to_buf() to access the underlying
 * raw buffer. Otherwise, you should use one of the abd_borrow_buf* functions
 * which will allocate a raw buffer if necessary. Use the abd_return_buf*
 * functions to return any raw buffers that are no longer necessary when you're
 * done using them.
 *
 * There are a variety of ABD APIs that implement basic buffer operations:
 * compare, copy, read, write, and fill with zeroes. If you need a custom
 * function which progressively accesses the whole ABD, use the abd_iterate_*
 * functions.
 *
 * As an additional feature, linear and scatter ABD's can be stitched together
 * by using the gang ABD type (abd_alloc_gang_abd()). This allows for
 * multiple ABDs to be viewed as a singular ABD.
 *
 * It is possible to make all ABDs linear by setting zfs_abd_scatter_enabled to
 * B_FALSE.
 */

#include <sys/abd_impl.h>
#include <sys/param.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>

/* see block comment above for description */
int zfs_abd_scatter_enabled = B_TRUE;

boolean_t
abd_is_linear(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_LINEAR) != 0 ? B_TRUE : B_FALSE);
}

boolean_t
abd_is_linear_page(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_LINEAR_PAGE) != 0 ?
	    B_TRUE : B_FALSE);
}

boolean_t
abd_is_gang(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_GANG) != 0 ? B_TRUE :
	    B_FALSE);
}

void
abd_verify(abd_t *abd)
{
	ASSERT3U(abd->abd_size, >, 0);
	ASSERT3U(abd->abd_size, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(abd->abd_flags, ==, abd->abd_flags & (ABD_FLAG_LINEAR |
	    ABD_FLAG_OWNER | ABD_FLAG_META | ABD_FLAG_MULTI_ZONE |
	    ABD_FLAG_MULTI_CHUNK | ABD_FLAG_LINEAR_PAGE | ABD_FLAG_GANG |
	    ABD_FLAG_GANG_FREE | ABD_FLAG_ZEROS));
	IMPLY(abd->abd_parent != NULL, !(abd->abd_flags & ABD_FLAG_OWNER));
	IMPLY(abd->abd_flags & ABD_FLAG_META, abd->abd_flags & ABD_FLAG_OWNER);
	if (abd_is_linear(abd)) {
		ASSERT3P(ABD_LINEAR_BUF(abd), !=, NULL);
	} else if (abd_is_gang(abd)) {
		uint_t child_sizes = 0;
		for (abd_t *cabd = list_head(&ABD_GANG(abd).abd_gang_chain);
		    cabd != NULL;
		    cabd = list_next(&ABD_GANG(abd).abd_gang_chain, cabd)) {
			ASSERT(list_link_active(&cabd->abd_gang_link));
			child_sizes += cabd->abd_size;
			abd_verify(cabd);
		}
		ASSERT3U(abd->abd_size, ==, child_sizes);
	} else {
		abd_verify_scatter(abd);
	}
}

uint_t
abd_get_size(abd_t *abd)
{
	abd_verify(abd);
	return (abd->abd_size);
}

/*
 * Allocate an ABD, along with its own underlying data buffers. Use this if you
 * don't care whether the ABD is linear or not.
 */
abd_t *
abd_alloc(size_t size, boolean_t is_metadata)
{
	if (!zfs_abd_scatter_enabled || abd_size_alloc_linear(size))
		return (abd_alloc_linear(size, is_metadata));

	VERIFY3U(size, <=, SPA_MAXBLOCKSIZE);

	abd_t *abd = abd_alloc_struct(size);
	abd->abd_flags = ABD_FLAG_OWNER;
	abd->abd_u.abd_scatter.abd_offset = 0;
	abd_alloc_chunks(abd, size);

	if (is_metadata) {
		abd->abd_flags |= ABD_FLAG_META;
	}
	abd->abd_size = size;
	abd->abd_parent = NULL;
	zfs_refcount_create(&abd->abd_children);

	abd_update_scatter_stats(abd, ABDSTAT_INCR);

	return (abd);
}

static void
abd_free_scatter(abd_t *abd)
{
	abd_free_chunks(abd);

	zfs_refcount_destroy(&abd->abd_children);
	abd_update_scatter_stats(abd, ABDSTAT_DECR);
	abd_free_struct(abd);
}

static void
abd_put_gang_abd(abd_t *abd)
{
	ASSERT(abd_is_gang(abd));
	abd_t *cabd;

	while ((cabd = list_remove_head(&ABD_GANG(abd).abd_gang_chain))
	    != NULL) {
		ASSERT0(cabd->abd_flags & ABD_FLAG_GANG_FREE);
		abd->abd_size -= cabd->abd_size;
		abd_put(cabd);
	}
	ASSERT0(abd->abd_size);
	list_destroy(&ABD_GANG(abd).abd_gang_chain);
}

/*
 * Free an ABD allocated from abd_get_offset() or abd_get_from_buf(). Will not
 * free the underlying scatterlist or buffer.
 */
void
abd_put(abd_t *abd)
{
	if (abd == NULL)
		return;

	abd_verify(abd);
	ASSERT(!(abd->abd_flags & ABD_FLAG_OWNER));

	if (abd->abd_parent != NULL) {
		(void) zfs_refcount_remove_many(&abd->abd_parent->abd_children,
		    abd->abd_size, abd);
	}

	if (abd_is_gang(abd))
		abd_put_gang_abd(abd);

	zfs_refcount_destroy(&abd->abd_children);
	abd_free_struct(abd);
}

/*
 * Allocate an ABD that must be linear, along with its own underlying data
 * buffer. Only use this when it would be very annoying to write your ABD
 * consumer with a scattered ABD.
 */
abd_t *
abd_alloc_linear(size_t size, boolean_t is_metadata)
{
	abd_t *abd = abd_alloc_struct(0);

	VERIFY3U(size, <=, SPA_MAXBLOCKSIZE);

	abd->abd_flags = ABD_FLAG_LINEAR | ABD_FLAG_OWNER;
	if (is_metadata) {
		abd->abd_flags |= ABD_FLAG_META;
	}
	abd->abd_size = size;
	abd->abd_parent = NULL;
	zfs_refcount_create(&abd->abd_children);

	if (is_metadata) {
		ABD_LINEAR_BUF(abd) = zio_buf_alloc(size);
	} else {
		ABD_LINEAR_BUF(abd) = zio_data_buf_alloc(size);
	}

	abd_update_linear_stats(abd, ABDSTAT_INCR);

	return (abd);
}

static void
abd_free_linear(abd_t *abd)
{
	if (abd_is_linear_page(abd)) {
		abd_free_linear_page(abd);
		return;
	}
	if (abd->abd_flags & ABD_FLAG_META) {
		zio_buf_free(ABD_LINEAR_BUF(abd), abd->abd_size);
	} else {
		zio_data_buf_free(ABD_LINEAR_BUF(abd), abd->abd_size);
	}

	zfs_refcount_destroy(&abd->abd_children);
	abd_update_linear_stats(abd, ABDSTAT_DECR);

	abd_free_struct(abd);
}

static void
abd_free_gang_abd(abd_t *abd)
{
	ASSERT(abd_is_gang(abd));
	abd_t *cabd = list_head(&ABD_GANG(abd).abd_gang_chain);

	while (cabd != NULL) {
		/*
		 * We must acquire the child ABDs mutex to ensure that if it
		 * is being added to another gang ABD we will set the link
		 * as inactive when removing it from this gang ABD and before
		 * adding it to the other gang ABD.
		 */
		mutex_enter(&cabd->abd_mtx);
		ASSERT(list_link_active(&cabd->abd_gang_link));
		list_remove(&ABD_GANG(abd).abd_gang_chain, cabd);
		mutex_exit(&cabd->abd_mtx);
		abd->abd_size -= cabd->abd_size;
		if (cabd->abd_flags & ABD_FLAG_GANG_FREE) {
			if (cabd->abd_flags & ABD_FLAG_OWNER)
				abd_free(cabd);
			else
				abd_put(cabd);
		}
		cabd = list_head(&ABD_GANG(abd).abd_gang_chain);
	}
	ASSERT0(abd->abd_size);
	list_destroy(&ABD_GANG(abd).abd_gang_chain);
	zfs_refcount_destroy(&abd->abd_children);
	abd_free_struct(abd);
}

/*
 * Free an ABD. Only use this on ABDs allocated with abd_alloc(),
 * abd_alloc_linear(), or abd_alloc_gang_abd().
 */
void
abd_free(abd_t *abd)
{
	if (abd == NULL)
		return;

	abd_verify(abd);
	ASSERT3P(abd->abd_parent, ==, NULL);
	ASSERT(abd->abd_flags & ABD_FLAG_OWNER);
	if (abd_is_linear(abd))
		abd_free_linear(abd);
	else if (abd_is_gang(abd))
		abd_free_gang_abd(abd);
	else
		abd_free_scatter(abd);
}

/*
 * Allocate an ABD of the same format (same metadata flag, same scatterize
 * setting) as another ABD.
 */
abd_t *
abd_alloc_sametype(abd_t *sabd, size_t size)
{
	boolean_t is_metadata = (sabd->abd_flags & ABD_FLAG_META) != 0;
	if (abd_is_linear(sabd) &&
	    !abd_is_linear_page(sabd)) {
		return (abd_alloc_linear(size, is_metadata));
	} else {
		return (abd_alloc(size, is_metadata));
	}
}


/*
 * Create gang ABD that will be the head of a list of ABD's. This is used
 * to "chain" scatter/gather lists together when constructing aggregated
 * IO's. To free this abd, abd_free() must be called.
 */
abd_t *
abd_alloc_gang_abd(void)
{
	abd_t *abd;

	abd = abd_alloc_struct(0);
	abd->abd_flags = ABD_FLAG_GANG | ABD_FLAG_OWNER;
	abd->abd_size = 0;
	abd->abd_parent = NULL;
	list_create(&ABD_GANG(abd).abd_gang_chain,
	    sizeof (abd_t), offsetof(abd_t, abd_gang_link));
	zfs_refcount_create(&abd->abd_children);
	return (abd);
}

/*
 * Add a child gang ABD to a parent gang ABDs chained list.
 */
static void
abd_gang_add_gang(abd_t *pabd, abd_t *cabd, boolean_t free_on_free)
{
	ASSERT(abd_is_gang(pabd));
	ASSERT(abd_is_gang(cabd));

	if (free_on_free) {
		/*
		 * If the parent is responsible for freeing the child gang
		 * ABD we will just splice the childs children ABD list to
		 * the parents list and immediately free the child gang ABD
		 * struct. The parent gang ABDs children from the child gang
		 * will retain all the free_on_free settings after being
		 * added to the parents list.
		 */
		pabd->abd_size += cabd->abd_size;
		list_move_tail(&ABD_GANG(pabd).abd_gang_chain,
		    &ABD_GANG(cabd).abd_gang_chain);
		ASSERT(list_is_empty(&ABD_GANG(cabd).abd_gang_chain));
		abd_verify(pabd);
		abd_free_struct(cabd);
	} else {
		for (abd_t *child = list_head(&ABD_GANG(cabd).abd_gang_chain);
		    child != NULL;
		    child = list_next(&ABD_GANG(cabd).abd_gang_chain, child)) {
			/*
			 * We always pass B_FALSE for free_on_free as it is the
			 * original child gang ABDs responsibilty to determine
			 * if any of its child ABDs should be free'd on the call
			 * to abd_free().
			 */
			abd_gang_add(pabd, child, B_FALSE);
		}
		abd_verify(pabd);
	}
}

/*
 * Add a child ABD to a gang ABD's chained list.
 */
void
abd_gang_add(abd_t *pabd, abd_t *cabd, boolean_t free_on_free)
{
	ASSERT(abd_is_gang(pabd));
	abd_t *child_abd = NULL;

	/*
	 * If the child being added is a gang ABD, we will add the
	 * childs ABDs to the parent gang ABD. This alllows us to account
	 * for the offset correctly in the parent gang ABD.
	 */
	if (abd_is_gang(cabd)) {
		ASSERT(!list_link_active(&cabd->abd_gang_link));
		ASSERT(!list_is_empty(&ABD_GANG(cabd).abd_gang_chain));
		return (abd_gang_add_gang(pabd, cabd, free_on_free));
	}
	ASSERT(!abd_is_gang(cabd));

	/*
	 * In order to verify that an ABD is not already part of
	 * another gang ABD, we must lock the child ABD's abd_mtx
	 * to check its abd_gang_link status. We unlock the abd_mtx
	 * only after it is has been added to a gang ABD, which
	 * will update the abd_gang_link's status. See comment below
	 * for how an ABD can be in multiple gang ABD's simultaneously.
	 */
	mutex_enter(&cabd->abd_mtx);
	if (list_link_active(&cabd->abd_gang_link)) {
		/*
		 * If the child ABD is already part of another
		 * gang ABD then we must allocate a new
		 * ABD to use a separate link. We mark the newly
		 * allocated ABD with ABD_FLAG_GANG_FREE, before
		 * adding it to the gang ABD's list, to make the
		 * gang ABD aware that it is responsible to call
		 * abd_put(). We use abd_get_offset() in order
		 * to just allocate a new ABD but avoid copying the
		 * data over into the newly allocated ABD.
		 *
		 * An ABD may become part of multiple gang ABD's. For
		 * example, when writing ditto bocks, the same ABD
		 * is used to write 2 or 3 locations with 2 or 3
		 * zio_t's. Each of the zio's may be aggregated with
		 * different adjacent zio's. zio aggregation uses gang
		 * zio's, so the single ABD can become part of multiple
		 * gang zio's.
		 *
		 * The ASSERT below is to make sure that if
		 * free_on_free is passed as B_TRUE, the ABD can
		 * not be in multiple gang ABD's. The gang ABD
		 * can not be responsible for cleaning up the child
		 * ABD memory allocation if the ABD can be in
		 * multiple gang ABD's at one time.
		 */
		ASSERT3B(free_on_free, ==, B_FALSE);
		child_abd = abd_get_offset(cabd, 0);
		child_abd->abd_flags |= ABD_FLAG_GANG_FREE;
	} else {
		child_abd = cabd;
		if (free_on_free)
			child_abd->abd_flags |= ABD_FLAG_GANG_FREE;
	}
	ASSERT3P(child_abd, !=, NULL);

	list_insert_tail(&ABD_GANG(pabd).abd_gang_chain, child_abd);
	mutex_exit(&cabd->abd_mtx);
	pabd->abd_size += child_abd->abd_size;
}

/*
 * Locate the ABD for the supplied offset in the gang ABD.
 * Return a new offset relative to the returned ABD.
 */
abd_t *
abd_gang_get_offset(abd_t *abd, size_t *off)
{
	abd_t *cabd;

	ASSERT(abd_is_gang(abd));
	ASSERT3U(*off, <, abd->abd_size);
	for (cabd = list_head(&ABD_GANG(abd).abd_gang_chain); cabd != NULL;
	    cabd = list_next(&ABD_GANG(abd).abd_gang_chain, cabd)) {
		if (*off >= cabd->abd_size)
			*off -= cabd->abd_size;
		else
			return (cabd);
	}
	VERIFY3P(cabd, !=, NULL);
	return (cabd);
}

/*
 * Allocate a new ABD to point to offset off of sabd. It shares the underlying
 * buffer data with sabd. Use abd_put() to free. sabd must not be freed while
 * any derived ABDs exist.
 */
static abd_t *
abd_get_offset_impl(abd_t *sabd, size_t off, size_t size)
{
	abd_t *abd = NULL;

	abd_verify(sabd);
	ASSERT3U(off, <=, sabd->abd_size);

	if (abd_is_linear(sabd)) {
		abd = abd_alloc_struct(0);

		/*
		 * Even if this buf is filesystem metadata, we only track that
		 * if we own the underlying data buffer, which is not true in
		 * this case. Therefore, we don't ever use ABD_FLAG_META here.
		 */
		abd->abd_flags = ABD_FLAG_LINEAR;

		ABD_LINEAR_BUF(abd) = (char *)ABD_LINEAR_BUF(sabd) + off;
	} else if (abd_is_gang(sabd)) {
		size_t left = size;
		abd = abd_alloc_gang_abd();
		abd->abd_flags &= ~ABD_FLAG_OWNER;
		for (abd_t *cabd = abd_gang_get_offset(sabd, &off);
		    cabd != NULL && left > 0;
		    cabd = list_next(&ABD_GANG(sabd).abd_gang_chain, cabd)) {
			int csize = MIN(left, cabd->abd_size - off);

			abd_t *nabd = abd_get_offset_impl(cabd, off, csize);
			abd_gang_add(abd, nabd, B_FALSE);
			left -= csize;
			off = 0;
		}
		ASSERT3U(left, ==, 0);
	} else {
		abd = abd_get_offset_scatter(sabd, off);
	}

	abd->abd_size = size;
	abd->abd_parent = sabd;
	zfs_refcount_create(&abd->abd_children);
	(void) zfs_refcount_add_many(&sabd->abd_children, abd->abd_size, abd);
	return (abd);
}

abd_t *
abd_get_offset(abd_t *sabd, size_t off)
{
	size_t size = sabd->abd_size > off ? sabd->abd_size - off : 0;
	VERIFY3U(size, >, 0);
	return (abd_get_offset_impl(sabd, off, size));
}

abd_t *
abd_get_offset_size(abd_t *sabd, size_t off, size_t size)
{
	ASSERT3U(off + size, <=, sabd->abd_size);
	return (abd_get_offset_impl(sabd, off, size));
}

/*
 * Return a size scatter ABD. In order to free the returned
 * ABD abd_put() must be called.
 */
abd_t *
abd_get_zeros(size_t size)
{
	ASSERT3P(abd_zero_scatter, !=, NULL);
	ASSERT3U(size, <=, SPA_MAXBLOCKSIZE);
	return (abd_get_offset_size(abd_zero_scatter, 0, size));
}

/*
 * Allocate a linear ABD structure for buf. You must free this with abd_put()
 * since the resulting ABD doesn't own its own buffer.
 */
abd_t *
abd_get_from_buf(void *buf, size_t size)
{
	abd_t *abd = abd_alloc_struct(0);

	VERIFY3U(size, <=, SPA_MAXBLOCKSIZE);

	/*
	 * Even if this buf is filesystem metadata, we only track that if we
	 * own the underlying data buffer, which is not true in this case.
	 * Therefore, we don't ever use ABD_FLAG_META here.
	 */
	abd->abd_flags = ABD_FLAG_LINEAR;
	abd->abd_size = size;
	abd->abd_parent = NULL;
	zfs_refcount_create(&abd->abd_children);

	ABD_LINEAR_BUF(abd) = buf;

	return (abd);
}

/*
 * Get the raw buffer associated with a linear ABD.
 */
void *
abd_to_buf(abd_t *abd)
{
	ASSERT(abd_is_linear(abd));
	abd_verify(abd);
	return (ABD_LINEAR_BUF(abd));
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
	ASSERT3U(abd->abd_size, >=, n);
	if (abd_is_linear(abd)) {
		buf = abd_to_buf(abd);
	} else {
		buf = zio_buf_alloc(n);
	}
	(void) zfs_refcount_add_many(&abd->abd_children, n, buf);
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
 * not change the contents of the ABD and will ASSERT that you didn't modify
 * the buffer since it was borrowed. If you want any changes you made to buf to
 * be copied back to abd, use abd_return_buf_copy() instead.
 */
void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, n);
	if (abd_is_linear(abd)) {
		ASSERT3P(buf, ==, abd_to_buf(abd));
	} else {
		ASSERT0(abd_cmp_buf(abd, buf, n));
		zio_buf_free(buf, n);
	}
	(void) zfs_refcount_remove_many(&abd->abd_children, n, buf);
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	if (!abd_is_linear(abd)) {
		abd_copy_from_buf(abd, buf, n);
	}
	abd_return_buf(abd, buf, n);
}

void
abd_release_ownership_of_buf(abd_t *abd)
{
	ASSERT(abd_is_linear(abd));
	ASSERT(abd->abd_flags & ABD_FLAG_OWNER);

	/*
	 * abd_free() needs to handle LINEAR_PAGE ABD's specially.
	 * Since that flag does not survive the
	 * abd_release_ownership_of_buf() -> abd_get_from_buf() ->
	 * abd_take_ownership_of_buf() sequence, we don't allow releasing
	 * these "linear but not zio_[data_]buf_alloc()'ed" ABD's.
	 */
	ASSERT(!abd_is_linear_page(abd));

	abd_verify(abd);

	abd->abd_flags &= ~ABD_FLAG_OWNER;
	/* Disable this flag since we no longer own the data buffer */
	abd->abd_flags &= ~ABD_FLAG_META;

	abd_update_linear_stats(abd, ABDSTAT_DECR);
}


/*
 * Give this ABD ownership of the buffer that it's storing. Can only be used on
 * linear ABDs which were allocated via abd_get_from_buf(), or ones allocated
 * with abd_alloc_linear() which subsequently released ownership of their buf
 * with abd_release_ownership_of_buf().
 */
void
abd_take_ownership_of_buf(abd_t *abd, boolean_t is_metadata)
{
	ASSERT(abd_is_linear(abd));
	ASSERT(!(abd->abd_flags & ABD_FLAG_OWNER));
	abd_verify(abd);

	abd->abd_flags |= ABD_FLAG_OWNER;
	if (is_metadata) {
		abd->abd_flags |= ABD_FLAG_META;
	}

	abd_update_linear_stats(abd, ABDSTAT_INCR);
}

/*
 * Initializes an abd_iter based on whether the abd is a gang ABD
 * or just a single ABD.
 */
static inline abd_t *
abd_init_abd_iter(abd_t *abd, struct abd_iter *aiter, size_t off)
{
	abd_t *cabd = NULL;

	if (abd_is_gang(abd)) {
		cabd = abd_gang_get_offset(abd, &off);
		if (cabd) {
			abd_iter_init(aiter, cabd);
			abd_iter_advance(aiter, off);
		}
	} else {
		abd_iter_init(aiter, abd);
		abd_iter_advance(aiter, off);
	}
	return (cabd);
}

/*
 * Advances an abd_iter. We have to be careful with gang ABD as
 * advancing could mean that we are at the end of a particular ABD and
 * must grab the ABD in the gang ABD's list.
 */
static inline abd_t *
abd_advance_abd_iter(abd_t *abd, abd_t *cabd, struct abd_iter *aiter,
    size_t len)
{
	abd_iter_advance(aiter, len);
	if (abd_is_gang(abd) && abd_iter_at_end(aiter)) {
		ASSERT3P(cabd, !=, NULL);
		cabd = list_next(&ABD_GANG(abd).abd_gang_chain, cabd);
		if (cabd) {
			abd_iter_init(aiter, cabd);
			abd_iter_advance(aiter, 0);
		}
	}
	return (cabd);
}

int
abd_iterate_func(abd_t *abd, size_t off, size_t size,
    abd_iter_func_t *func, void *private)
{
	struct abd_iter aiter;
	int ret = 0;

	if (size == 0)
		return (0);

	abd_verify(abd);
	ASSERT3U(off + size, <=, abd->abd_size);

	boolean_t abd_multi = abd_is_gang(abd);
	abd_t *c_abd = abd_init_abd_iter(abd, &aiter, off);

	while (size > 0) {
		/* If we are at the end of the gang ABD we are done */
		if (abd_multi && !c_abd)
			break;

		abd_iter_map(&aiter);

		size_t len = MIN(aiter.iter_mapsize, size);
		ASSERT3U(len, >, 0);

		ret = func(aiter.iter_mapaddr, len, private);

		abd_iter_unmap(&aiter);

		if (ret != 0)
			break;

		size -= len;
		c_abd = abd_advance_abd_iter(abd, c_abd, &aiter, len);
	}

	return (ret);
}

struct buf_arg {
	void *arg_buf;
};

static int
abd_copy_to_buf_off_cb(void *buf, size_t size, void *private)
{
	struct buf_arg *ba_ptr = private;

	(void) memcpy(ba_ptr->arg_buf, buf, size);
	ba_ptr->arg_buf = (char *)ba_ptr->arg_buf + size;

	return (0);
}

/*
 * Copy abd to buf. (off is the offset in abd.)
 */
void
abd_copy_to_buf_off(void *buf, abd_t *abd, size_t off, size_t size)
{
	struct buf_arg ba_ptr = { buf };

	(void) abd_iterate_func(abd, off, size, abd_copy_to_buf_off_cb,
	    &ba_ptr);
}

static int
abd_cmp_buf_off_cb(void *buf, size_t size, void *private)
{
	int ret;
	struct buf_arg *ba_ptr = private;

	ret = memcmp(buf, ba_ptr->arg_buf, size);
	ba_ptr->arg_buf = (char *)ba_ptr->arg_buf + size;

	return (ret);
}

/*
 * Compare the contents of abd to buf. (off is the offset in abd.)
 */
int
abd_cmp_buf_off(abd_t *abd, const void *buf, size_t off, size_t size)
{
	struct buf_arg ba_ptr = { (void *) buf };

	return (abd_iterate_func(abd, off, size, abd_cmp_buf_off_cb, &ba_ptr));
}

static int
abd_copy_from_buf_off_cb(void *buf, size_t size, void *private)
{
	struct buf_arg *ba_ptr = private;

	(void) memcpy(buf, ba_ptr->arg_buf, size);
	ba_ptr->arg_buf = (char *)ba_ptr->arg_buf + size;

	return (0);
}

/*
 * Copy from buf to abd. (off is the offset in abd.)
 */
void
abd_copy_from_buf_off(abd_t *abd, const void *buf, size_t off, size_t size)
{
	struct buf_arg ba_ptr = { (void *) buf };

	(void) abd_iterate_func(abd, off, size, abd_copy_from_buf_off_cb,
	    &ba_ptr);
}

/*ARGSUSED*/
static int
abd_zero_off_cb(void *buf, size_t size, void *private)
{
	(void) memset(buf, 0, size);
	return (0);
}

/*
 * Zero out the abd from a particular offset to the end.
 */
void
abd_zero_off(abd_t *abd, size_t off, size_t size)
{
	(void) abd_iterate_func(abd, off, size, abd_zero_off_cb, NULL);
}

/*
 * Iterate over two ABDs and call func incrementally on the two ABDs' data in
 * equal-sized chunks (passed to func as raw buffers). func could be called many
 * times during this iteration.
 */
int
abd_iterate_func2(abd_t *dabd, abd_t *sabd, size_t doff, size_t soff,
    size_t size, abd_iter_func2_t *func, void *private)
{
	int ret = 0;
	struct abd_iter daiter, saiter;
	boolean_t dabd_is_gang_abd, sabd_is_gang_abd;
	abd_t *c_dabd, *c_sabd;

	if (size == 0)
		return (0);

	abd_verify(dabd);
	abd_verify(sabd);

	ASSERT3U(doff + size, <=, dabd->abd_size);
	ASSERT3U(soff + size, <=, sabd->abd_size);

	dabd_is_gang_abd = abd_is_gang(dabd);
	sabd_is_gang_abd = abd_is_gang(sabd);
	c_dabd = abd_init_abd_iter(dabd, &daiter, doff);
	c_sabd = abd_init_abd_iter(sabd, &saiter, soff);

	while (size > 0) {
		/* if we are at the end of the gang ABD we are done */
		if ((dabd_is_gang_abd && !c_dabd) ||
		    (sabd_is_gang_abd && !c_sabd))
			break;

		abd_iter_map(&daiter);
		abd_iter_map(&saiter);

		size_t dlen = MIN(daiter.iter_mapsize, size);
		size_t slen = MIN(saiter.iter_mapsize, size);
		size_t len = MIN(dlen, slen);
		ASSERT(dlen > 0 || slen > 0);

		ret = func(daiter.iter_mapaddr, saiter.iter_mapaddr, len,
		    private);

		abd_iter_unmap(&saiter);
		abd_iter_unmap(&daiter);

		if (ret != 0)
			break;

		size -= len;
		c_dabd =
		    abd_advance_abd_iter(dabd, c_dabd, &daiter, len);
		c_sabd =
		    abd_advance_abd_iter(sabd, c_sabd, &saiter, len);
	}

	return (ret);
}

/*ARGSUSED*/
static int
abd_copy_off_cb(void *dbuf, void *sbuf, size_t size, void *private)
{
	(void) memcpy(dbuf, sbuf, size);
	return (0);
}

/*
 * Copy from sabd to dabd starting from soff and doff.
 */
void
abd_copy_off(abd_t *dabd, abd_t *sabd, size_t doff, size_t soff, size_t size)
{
	(void) abd_iterate_func2(dabd, sabd, doff, soff, size,
	    abd_copy_off_cb, NULL);
}

/*ARGSUSED*/
static int
abd_cmp_cb(void *bufa, void *bufb, size_t size, void *private)
{
	return (memcmp(bufa, bufb, size));
}

/*
 * Compares the contents of two ABDs.
 */
int
abd_cmp(abd_t *dabd, abd_t *sabd)
{
	ASSERT3U(dabd->abd_size, ==, sabd->abd_size);
	return (abd_iterate_func2(dabd, sabd, 0, 0, dabd->abd_size,
	    abd_cmp_cb, NULL));
}

/*
 * Iterate over code ABDs and a data ABD and call @func_raidz_gen.
 *
 * @cabds          parity ABDs, must have equal size
 * @dabd           data ABD. Can be NULL (in this case @dsize = 0)
 * @func_raidz_gen should be implemented so that its behaviour
 *                 is the same when taking linear and when taking scatter
 */
void
abd_raidz_gen_iterate(abd_t **cabds, abd_t *dabd,
    ssize_t csize, ssize_t dsize, const unsigned parity,
    void (*func_raidz_gen)(void **, const void *, size_t, size_t))
{
	int i;
	ssize_t len, dlen;
	struct abd_iter caiters[3];
	struct abd_iter daiter = {0};
	void *caddrs[3];
	unsigned long flags __maybe_unused = 0;
	abd_t *c_cabds[3];
	abd_t *c_dabd = NULL;
	boolean_t cabds_is_gang_abd[3];
	boolean_t dabd_is_gang_abd = B_FALSE;

	ASSERT3U(parity, <=, 3);

	for (i = 0; i < parity; i++) {
		cabds_is_gang_abd[i] = abd_is_gang(cabds[i]);
		c_cabds[i] = abd_init_abd_iter(cabds[i], &caiters[i], 0);
	}

	if (dabd) {
		dabd_is_gang_abd = abd_is_gang(dabd);
		c_dabd = abd_init_abd_iter(dabd, &daiter, 0);
	}

	ASSERT3S(dsize, >=, 0);

	abd_enter_critical(flags);
	while (csize > 0) {
		/* if we are at the end of the gang ABD we are done */
		if (dabd_is_gang_abd && !c_dabd)
			break;

		for (i = 0; i < parity; i++) {
			/*
			 * If we are at the end of the gang ABD we are
			 * done.
			 */
			if (cabds_is_gang_abd[i] && !c_cabds[i])
				break;
			abd_iter_map(&caiters[i]);
			caddrs[i] = caiters[i].iter_mapaddr;
		}

		len = csize;

		if (dabd && dsize > 0)
			abd_iter_map(&daiter);

		switch (parity) {
			case 3:
				len = MIN(caiters[2].iter_mapsize, len);
				/* falls through */
			case 2:
				len = MIN(caiters[1].iter_mapsize, len);
				/* falls through */
			case 1:
				len = MIN(caiters[0].iter_mapsize, len);
		}

		/* must be progressive */
		ASSERT3S(len, >, 0);

		if (dabd && dsize > 0) {
			/* this needs precise iter.length */
			len = MIN(daiter.iter_mapsize, len);
			dlen = len;
		} else
			dlen = 0;

		/* must be progressive */
		ASSERT3S(len, >, 0);
		/*
		 * The iterated function likely will not do well if each
		 * segment except the last one is not multiple of 512 (raidz).
		 */
		ASSERT3U(((uint64_t)len & 511ULL), ==, 0);

		func_raidz_gen(caddrs, daiter.iter_mapaddr, len, dlen);

		for (i = parity-1; i >= 0; i--) {
			abd_iter_unmap(&caiters[i]);
			c_cabds[i] =
			    abd_advance_abd_iter(cabds[i], c_cabds[i],
			    &caiters[i], len);
		}

		if (dabd && dsize > 0) {
			abd_iter_unmap(&daiter);
			c_dabd =
			    abd_advance_abd_iter(dabd, c_dabd, &daiter,
			    dlen);
			dsize -= dlen;
		}

		csize -= len;

		ASSERT3S(dsize, >=, 0);
		ASSERT3S(csize, >=, 0);
	}
	abd_exit_critical(flags);
}

/*
 * Iterate over code ABDs and data reconstruction target ABDs and call
 * @func_raidz_rec. Function maps at most 6 pages atomically.
 *
 * @cabds           parity ABDs, must have equal size
 * @tabds           rec target ABDs, at most 3
 * @tsize           size of data target columns
 * @func_raidz_rec  expects syndrome data in target columns. Function
 *                  reconstructs data and overwrites target columns.
 */
void
abd_raidz_rec_iterate(abd_t **cabds, abd_t **tabds,
    ssize_t tsize, const unsigned parity,
    void (*func_raidz_rec)(void **t, const size_t tsize, void **c,
    const unsigned *mul),
    const unsigned *mul)
{
	int i;
	ssize_t len;
	struct abd_iter citers[3];
	struct abd_iter xiters[3];
	void *caddrs[3], *xaddrs[3];
	unsigned long flags __maybe_unused = 0;
	boolean_t cabds_is_gang_abd[3];
	boolean_t tabds_is_gang_abd[3];
	abd_t *c_cabds[3];
	abd_t *c_tabds[3];

	ASSERT3U(parity, <=, 3);

	for (i = 0; i < parity; i++) {
		cabds_is_gang_abd[i] = abd_is_gang(cabds[i]);
		tabds_is_gang_abd[i] = abd_is_gang(tabds[i]);
		c_cabds[i] =
		    abd_init_abd_iter(cabds[i], &citers[i], 0);
		c_tabds[i] =
		    abd_init_abd_iter(tabds[i], &xiters[i], 0);
	}

	abd_enter_critical(flags);
	while (tsize > 0) {

		for (i = 0; i < parity; i++) {
			/*
			 * If we are at the end of the gang ABD we
			 * are done.
			 */
			if (cabds_is_gang_abd[i] && !c_cabds[i])
				break;
			if (tabds_is_gang_abd[i] && !c_tabds[i])
				break;
			abd_iter_map(&citers[i]);
			abd_iter_map(&xiters[i]);
			caddrs[i] = citers[i].iter_mapaddr;
			xaddrs[i] = xiters[i].iter_mapaddr;
		}

		len = tsize;
		switch (parity) {
			case 3:
				len = MIN(xiters[2].iter_mapsize, len);
				len = MIN(citers[2].iter_mapsize, len);
				/* falls through */
			case 2:
				len = MIN(xiters[1].iter_mapsize, len);
				len = MIN(citers[1].iter_mapsize, len);
				/* falls through */
			case 1:
				len = MIN(xiters[0].iter_mapsize, len);
				len = MIN(citers[0].iter_mapsize, len);
		}
		/* must be progressive */
		ASSERT3S(len, >, 0);
		/*
		 * The iterated function likely will not do well if each
		 * segment except the last one is not multiple of 512 (raidz).
		 */
		ASSERT3U(((uint64_t)len & 511ULL), ==, 0);

		func_raidz_rec(xaddrs, len, caddrs, mul);

		for (i = parity-1; i >= 0; i--) {
			abd_iter_unmap(&xiters[i]);
			abd_iter_unmap(&citers[i]);
			c_tabds[i] =
			    abd_advance_abd_iter(tabds[i], c_tabds[i],
			    &xiters[i], len);
			c_cabds[i] =
			    abd_advance_abd_iter(cabds[i], c_cabds[i],
			    &citers[i], len);
		}

		tsize -= len;
		ASSERT3S(tsize, >=, 0);
	}
	abd_exit_critical(flags);
}
