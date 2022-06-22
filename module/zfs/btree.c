/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2019 by Delphix. All rights reserved.
 */

#include	<sys/btree.h>
#include	<sys/bitops.h>
#include	<sys/zfs_context.h>

kmem_cache_t *zfs_btree_leaf_cache;

/*
 * Control the extent of the verification that occurs when zfs_btree_verify is
 * called. Primarily used for debugging when extending the btree logic and
 * functionality. As the intensity is increased, new verification steps are
 * added. These steps are cumulative; intensity = 3 includes the intensity = 1
 * and intensity = 2 steps as well.
 *
 * Intensity 1: Verify that the tree's height is consistent throughout.
 * Intensity 2: Verify that a core node's children's parent pointers point
 * to the core node.
 * Intensity 3: Verify that the total number of elements in the tree matches the
 * sum of the number of elements in each node. Also verifies that each node's
 * count obeys the invariants (less than or equal to maximum value, greater than
 * or equal to half the maximum minus one).
 * Intensity 4: Verify that each element compares less than the element
 * immediately after it and greater than the one immediately before it using the
 * comparator function. For core nodes, also checks that each element is greater
 * than the last element in the first of the two nodes it separates, and less
 * than the first element in the second of the two nodes.
 * Intensity 5: Verifies, if ZFS_DEBUG is defined, that all unused memory inside
 * of each node is poisoned appropriately. Note that poisoning always occurs if
 * ZFS_DEBUG is set, so it is safe to set the intensity to 5 during normal
 * operation.
 *
 * Intensity 4 and 5 are particularly expensive to perform; the previous levels
 * are a few memory operations per node, while these levels require multiple
 * operations per element. In addition, when creating large btrees, these
 * operations are called at every step, resulting in extremely slow operation
 * (while the asymptotic complexity of the other steps is the same, the
 * importance of the constant factors cannot be denied).
 */
int zfs_btree_verify_intensity = 0;

/*
 * Convenience functions to silence warnings from memcpy/memmove's
 * return values and change argument order to src, dest.
 */
static void
bcpy(const void *src, void *dest, size_t size)
{
	(void) memcpy(dest, src, size);
}

static void
bmov(const void *src, void *dest, size_t size)
{
	(void) memmove(dest, src, size);
}

static boolean_t
zfs_btree_is_core(struct zfs_btree_hdr *hdr)
{
	return (hdr->bth_first == -1);
}

#ifdef _ILP32
#define	BTREE_POISON 0xabadb10c
#else
#define	BTREE_POISON 0xabadb10cdeadbeef
#endif

static void
zfs_btree_poison_node(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
#ifdef ZFS_DEBUG
	size_t size = tree->bt_elem_size;
	if (zfs_btree_is_core(hdr)) {
		zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
		for (uint32_t i = hdr->bth_count + 1; i <= BTREE_CORE_ELEMS;
		    i++) {
			node->btc_children[i] =
			    (zfs_btree_hdr_t *)BTREE_POISON;
		}
		(void) memset(node->btc_elems + hdr->bth_count * size, 0x0f,
		    (BTREE_CORE_ELEMS - hdr->bth_count) * size);
	} else {
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)hdr;
		(void) memset(leaf->btl_elems, 0x0f, hdr->bth_first * size);
		(void) memset(leaf->btl_elems +
		    (hdr->bth_first + hdr->bth_count) * size, 0x0f,
		    BTREE_LEAF_ESIZE -
		    (hdr->bth_first + hdr->bth_count) * size);
	}
#endif
}

static inline void
zfs_btree_poison_node_at(zfs_btree_t *tree, zfs_btree_hdr_t *hdr,
    uint32_t idx, uint32_t count)
{
#ifdef ZFS_DEBUG
	size_t size = tree->bt_elem_size;
	if (zfs_btree_is_core(hdr)) {
		ASSERT3U(idx, >=, hdr->bth_count);
		ASSERT3U(idx, <=, BTREE_CORE_ELEMS);
		ASSERT3U(idx + count, <=, BTREE_CORE_ELEMS);
		zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
		for (uint32_t i = 1; i <= count; i++) {
			node->btc_children[idx + i] =
			    (zfs_btree_hdr_t *)BTREE_POISON;
		}
		(void) memset(node->btc_elems + idx * size, 0x0f, count * size);
	} else {
		ASSERT3U(idx, <=, tree->bt_leaf_cap);
		ASSERT3U(idx + count, <=, tree->bt_leaf_cap);
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)hdr;
		(void) memset(leaf->btl_elems +
		    (hdr->bth_first + idx) * size, 0x0f, count * size);
	}
#endif
}

static inline void
zfs_btree_verify_poison_at(zfs_btree_t *tree, zfs_btree_hdr_t *hdr,
    uint32_t idx)
{
#ifdef ZFS_DEBUG
	size_t size = tree->bt_elem_size;
	if (zfs_btree_is_core(hdr)) {
		ASSERT3U(idx, <, BTREE_CORE_ELEMS);
		zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
		zfs_btree_hdr_t *cval = (zfs_btree_hdr_t *)BTREE_POISON;
		VERIFY3P(node->btc_children[idx + 1], ==, cval);
		for (size_t i = 0; i < size; i++)
			VERIFY3U(node->btc_elems[idx * size + i], ==, 0x0f);
	} else  {
		ASSERT3U(idx, <, tree->bt_leaf_cap);
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)hdr;
		if (idx >= tree->bt_leaf_cap - hdr->bth_first)
			return;
		for (size_t i = 0; i < size; i++) {
			VERIFY3U(leaf->btl_elems[(hdr->bth_first + idx)
			    * size + i], ==, 0x0f);
		}
	}
#endif
}

void
zfs_btree_init(void)
{
	zfs_btree_leaf_cache = kmem_cache_create("zfs_btree_leaf_cache",
	    BTREE_LEAF_SIZE, 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
zfs_btree_fini(void)
{
	kmem_cache_destroy(zfs_btree_leaf_cache);
}

void
zfs_btree_create(zfs_btree_t *tree, int (*compar) (const void *, const void *),
    size_t size)
{
	ASSERT3U(size, <=, BTREE_LEAF_ESIZE / 2);

	memset(tree, 0, sizeof (*tree));
	tree->bt_compar = compar;
	tree->bt_elem_size = size;
	tree->bt_leaf_cap = P2ALIGN(BTREE_LEAF_ESIZE / size, 2);
	tree->bt_height = -1;
	tree->bt_bulk = NULL;
}

/*
 * Find value in the array of elements provided. Uses a simple binary search.
 */
static void *
zfs_btree_find_in_buf(zfs_btree_t *tree, uint8_t *buf, uint32_t nelems,
    const void *value, zfs_btree_index_t *where)
{
	uint32_t max = nelems;
	uint32_t min = 0;
	while (max > min) {
		uint32_t idx = (min + max) / 2;
		uint8_t *cur = buf + idx * tree->bt_elem_size;
		int comp = tree->bt_compar(cur, value);
		if (comp < 0) {
			min = idx + 1;
		} else if (comp > 0) {
			max = idx;
		} else {
			where->bti_offset = idx;
			where->bti_before = B_FALSE;
			return (cur);
		}
	}

	where->bti_offset = max;
	where->bti_before = B_TRUE;
	return (NULL);
}

/*
 * Find the given value in the tree. where may be passed as null to use as a
 * membership test or if the btree is being used as a map.
 */
void *
zfs_btree_find(zfs_btree_t *tree, const void *value, zfs_btree_index_t *where)
{
	if (tree->bt_height == -1) {
		if (where != NULL) {
			where->bti_node = NULL;
			where->bti_offset = 0;
		}
		ASSERT0(tree->bt_num_elems);
		return (NULL);
	}

	/*
	 * If we're in bulk-insert mode, we check the last spot in the tree
	 * and the last leaf in the tree before doing the normal search,
	 * because for most workloads the vast majority of finds in
	 * bulk-insert mode are to insert new elements.
	 */
	zfs_btree_index_t idx;
	size_t size = tree->bt_elem_size;
	if (tree->bt_bulk != NULL) {
		zfs_btree_leaf_t *last_leaf = tree->bt_bulk;
		int comp = tree->bt_compar(last_leaf->btl_elems +
		    (last_leaf->btl_hdr.bth_first +
		    last_leaf->btl_hdr.bth_count - 1) * size, value);
		if (comp < 0) {
			/*
			 * If what they're looking for is after the last
			 * element, it's not in the tree.
			 */
			if (where != NULL) {
				where->bti_node = (zfs_btree_hdr_t *)last_leaf;
				where->bti_offset =
				    last_leaf->btl_hdr.bth_count;
				where->bti_before = B_TRUE;
			}
			return (NULL);
		} else if (comp == 0) {
			if (where != NULL) {
				where->bti_node = (zfs_btree_hdr_t *)last_leaf;
				where->bti_offset =
				    last_leaf->btl_hdr.bth_count - 1;
				where->bti_before = B_FALSE;
			}
			return (last_leaf->btl_elems +
			    (last_leaf->btl_hdr.bth_first +
			    last_leaf->btl_hdr.bth_count - 1) * size);
		}
		if (tree->bt_compar(last_leaf->btl_elems +
		    last_leaf->btl_hdr.bth_first * size, value) <= 0) {
			/*
			 * If what they're looking for is after the first
			 * element in the last leaf, it's in the last leaf or
			 * it's not in the tree.
			 */
			void *d = zfs_btree_find_in_buf(tree,
			    last_leaf->btl_elems +
			    last_leaf->btl_hdr.bth_first * size,
			    last_leaf->btl_hdr.bth_count, value, &idx);

			if (where != NULL) {
				idx.bti_node = (zfs_btree_hdr_t *)last_leaf;
				*where = idx;
			}
			return (d);
		}
	}

	zfs_btree_core_t *node = NULL;
	uint32_t child = 0;
	uint64_t depth = 0;

	/*
	 * Iterate down the tree, finding which child the value should be in
	 * by comparing with the separators.
	 */
	for (node = (zfs_btree_core_t *)tree->bt_root; depth < tree->bt_height;
	    node = (zfs_btree_core_t *)node->btc_children[child], depth++) {
		ASSERT3P(node, !=, NULL);
		void *d = zfs_btree_find_in_buf(tree, node->btc_elems,
		    node->btc_hdr.bth_count, value, &idx);
		EQUIV(d != NULL, !idx.bti_before);
		if (d != NULL) {
			if (where != NULL) {
				idx.bti_node = (zfs_btree_hdr_t *)node;
				*where = idx;
			}
			return (d);
		}
		ASSERT(idx.bti_before);
		child = idx.bti_offset;
	}

	/*
	 * The value is in this leaf, or it would be if it were in the
	 * tree. Find its proper location and return it.
	 */
	zfs_btree_leaf_t *leaf = (depth == 0 ?
	    (zfs_btree_leaf_t *)tree->bt_root : (zfs_btree_leaf_t *)node);
	void *d = zfs_btree_find_in_buf(tree, leaf->btl_elems +
	    leaf->btl_hdr.bth_first * size,
	    leaf->btl_hdr.bth_count, value, &idx);

	if (where != NULL) {
		idx.bti_node = (zfs_btree_hdr_t *)leaf;
		*where = idx;
	}

	return (d);
}

/*
 * To explain the following functions, it is useful to understand the four
 * kinds of shifts used in btree operation. First, a shift is a movement of
 * elements within a node. It is used to create gaps for inserting new
 * elements and children, or cover gaps created when things are removed. A
 * shift has two fundamental properties, each of which can be one of two
 * values, making four types of shifts.  There is the direction of the shift
 * (left or right) and the shape of the shift (parallelogram or isoceles
 * trapezoid (shortened to trapezoid hereafter)). The shape distinction only
 * applies to shifts of core nodes.
 *
 * The names derive from the following imagining of the layout of a node:
 *
 *  Elements:       *   *   *   *   *   *   *   ...   *   *   *
 *  Children:     *   *   *   *   *   *   *   *   ...   *   *   *
 *
 * This layout follows from the fact that the elements act as separators
 * between pairs of children, and that children root subtrees "below" the
 * current node. A left and right shift are fairly self-explanatory; a left
 * shift moves things to the left, while a right shift moves things to the
 * right. A parallelogram shift is a shift with the same number of elements
 * and children being moved, while a trapezoid shift is a shift that moves one
 * more children than elements. An example follows:
 *
 * A parallelogram shift could contain the following:
 *      _______________
 *      \*   *   *   * \ *   *   *   ...   *   *   *
 *     * \ *   *   *   *\  *   *   *   ...   *   *   *
 *        ---------------
 * A trapezoid shift could contain the following:
 *          ___________
 *       * / *   *   * \ *   *   *   ...   *   *   *
 *     *  / *  *   *   *\  *   *   *   ...   *   *   *
 *        ---------------
 *
 * Note that a parallelogram shift is always shaped like a "left-leaning"
 * parallelogram, where the starting index of the children being moved is
 * always one higher than the starting index of the elements being moved. No
 * "right-leaning" parallelogram shifts are needed (shifts where the starting
 * element index and starting child index being moved are the same) to achieve
 * any btree operations, so we ignore them.
 */

enum bt_shift_shape {
	BSS_TRAPEZOID,
	BSS_PARALLELOGRAM
};

enum bt_shift_direction {
	BSD_LEFT,
	BSD_RIGHT
};

/*
 * Shift elements and children in the provided core node by off spots.  The
 * first element moved is idx, and count elements are moved. The shape of the
 * shift is determined by shape. The direction is determined by dir.
 */
static inline void
bt_shift_core(zfs_btree_t *tree, zfs_btree_core_t *node, uint32_t idx,
    uint32_t count, uint32_t off, enum bt_shift_shape shape,
    enum bt_shift_direction dir)
{
	size_t size = tree->bt_elem_size;
	ASSERT(zfs_btree_is_core(&node->btc_hdr));

	uint8_t *e_start = node->btc_elems + idx * size;
	uint8_t *e_out = (dir == BSD_LEFT ? e_start - off * size :
	    e_start + off * size);
	bmov(e_start, e_out, count * size);

	zfs_btree_hdr_t **c_start = node->btc_children + idx +
	    (shape == BSS_TRAPEZOID ? 0 : 1);
	zfs_btree_hdr_t **c_out = (dir == BSD_LEFT ? c_start - off :
	    c_start + off);
	uint32_t c_count = count + (shape == BSS_TRAPEZOID ? 1 : 0);
	bmov(c_start, c_out, c_count * sizeof (*c_start));
}

/*
 * Shift elements and children in the provided core node left by one spot.
 * The first element moved is idx, and count elements are moved. The
 * shape of the shift is determined by trap; true if the shift is a trapezoid,
 * false if it is a parallelogram.
 */
static inline void
bt_shift_core_left(zfs_btree_t *tree, zfs_btree_core_t *node, uint32_t idx,
    uint32_t count, enum bt_shift_shape shape)
{
	bt_shift_core(tree, node, idx, count, 1, shape, BSD_LEFT);
}

/*
 * Shift elements and children in the provided core node right by one spot.
 * Starts with elements[idx] and children[idx] and one more child than element.
 */
static inline void
bt_shift_core_right(zfs_btree_t *tree, zfs_btree_core_t *node, uint32_t idx,
    uint32_t count, enum bt_shift_shape shape)
{
	bt_shift_core(tree, node, idx, count, 1, shape, BSD_RIGHT);
}

/*
 * Shift elements and children in the provided leaf node by off spots.
 * The first element moved is idx, and count elements are moved. The direction
 * is determined by left.
 */
static inline void
bt_shift_leaf(zfs_btree_t *tree, zfs_btree_leaf_t *node, uint32_t idx,
    uint32_t count, uint32_t off, enum bt_shift_direction dir)
{
	size_t size = tree->bt_elem_size;
	zfs_btree_hdr_t *hdr = &node->btl_hdr;
	ASSERT(!zfs_btree_is_core(hdr));

	if (count == 0)
		return;
	uint8_t *start = node->btl_elems + (hdr->bth_first + idx) * size;
	uint8_t *out = (dir == BSD_LEFT ? start - off * size :
	    start + off * size);
	bmov(start, out, count * size);
}

/*
 * Grow leaf for n new elements before idx.
 */
static void
bt_grow_leaf(zfs_btree_t *tree, zfs_btree_leaf_t *leaf, uint32_t idx,
    uint32_t n)
{
	zfs_btree_hdr_t *hdr = &leaf->btl_hdr;
	ASSERT(!zfs_btree_is_core(hdr));
	ASSERT3U(idx, <=, hdr->bth_count);
	uint32_t capacity = tree->bt_leaf_cap;
	ASSERT3U(hdr->bth_count + n, <=, capacity);
	boolean_t cl = (hdr->bth_first >= n);
	boolean_t cr = (hdr->bth_first + hdr->bth_count + n <= capacity);

	if (cl && (!cr || idx <= hdr->bth_count / 2)) {
		/* Grow left. */
		hdr->bth_first -= n;
		bt_shift_leaf(tree, leaf, n, idx, n, BSD_LEFT);
	} else if (cr) {
		/* Grow right. */
		bt_shift_leaf(tree, leaf, idx, hdr->bth_count - idx, n,
		    BSD_RIGHT);
	} else {
		/* Grow both ways. */
		uint32_t fn = hdr->bth_first -
		    (capacity - (hdr->bth_count + n)) / 2;
		hdr->bth_first -= fn;
		bt_shift_leaf(tree, leaf, fn, idx, fn, BSD_LEFT);
		bt_shift_leaf(tree, leaf, fn + idx, hdr->bth_count - idx,
		    n - fn, BSD_RIGHT);
	}
	hdr->bth_count += n;
}

/*
 * Shrink leaf for count elements starting from idx.
 */
static void
bt_shrink_leaf(zfs_btree_t *tree, zfs_btree_leaf_t *leaf, uint32_t idx,
    uint32_t n)
{
	zfs_btree_hdr_t *hdr = &leaf->btl_hdr;
	ASSERT(!zfs_btree_is_core(hdr));
	ASSERT3U(idx, <=, hdr->bth_count);
	ASSERT3U(idx + n, <=, hdr->bth_count);

	if (idx <= (hdr->bth_count - n) / 2) {
		bt_shift_leaf(tree, leaf, 0, idx, n, BSD_RIGHT);
		zfs_btree_poison_node_at(tree, hdr, 0, n);
		hdr->bth_first += n;
	} else {
		bt_shift_leaf(tree, leaf, idx + n, hdr->bth_count - idx - n, n,
		    BSD_LEFT);
		zfs_btree_poison_node_at(tree, hdr, hdr->bth_count - n, n);
	}
	hdr->bth_count -= n;
}

/*
 * Move children and elements from one core node to another. The shape
 * parameter behaves the same as it does in the shift logic.
 */
static inline void
bt_transfer_core(zfs_btree_t *tree, zfs_btree_core_t *source, uint32_t sidx,
    uint32_t count, zfs_btree_core_t *dest, uint32_t didx,
    enum bt_shift_shape shape)
{
	size_t size = tree->bt_elem_size;
	ASSERT(zfs_btree_is_core(&source->btc_hdr));
	ASSERT(zfs_btree_is_core(&dest->btc_hdr));

	bcpy(source->btc_elems + sidx * size, dest->btc_elems + didx * size,
	    count * size);

	uint32_t c_count = count + (shape == BSS_TRAPEZOID ? 1 : 0);
	bcpy(source->btc_children + sidx + (shape == BSS_TRAPEZOID ? 0 : 1),
	    dest->btc_children + didx + (shape == BSS_TRAPEZOID ? 0 : 1),
	    c_count * sizeof (*source->btc_children));
}

static inline void
bt_transfer_leaf(zfs_btree_t *tree, zfs_btree_leaf_t *source, uint32_t sidx,
    uint32_t count, zfs_btree_leaf_t *dest, uint32_t didx)
{
	size_t size = tree->bt_elem_size;
	ASSERT(!zfs_btree_is_core(&source->btl_hdr));
	ASSERT(!zfs_btree_is_core(&dest->btl_hdr));

	bcpy(source->btl_elems + (source->btl_hdr.bth_first + sidx) * size,
	    dest->btl_elems + (dest->btl_hdr.bth_first + didx) * size,
	    count * size);
}

/*
 * Find the first element in the subtree rooted at hdr, return its value and
 * put its location in where if non-null.
 */
static void *
zfs_btree_first_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr,
    zfs_btree_index_t *where)
{
	zfs_btree_hdr_t *node;

	for (node = hdr; zfs_btree_is_core(node);
	    node = ((zfs_btree_core_t *)node)->btc_children[0])
		;

	ASSERT(!zfs_btree_is_core(node));
	zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)node;
	if (where != NULL) {
		where->bti_node = node;
		where->bti_offset = 0;
		where->bti_before = B_FALSE;
	}
	return (&leaf->btl_elems[node->bth_first * tree->bt_elem_size]);
}

/* Insert an element and a child into a core node at the given offset. */
static void
zfs_btree_insert_core_impl(zfs_btree_t *tree, zfs_btree_core_t *parent,
    uint32_t offset, zfs_btree_hdr_t *new_node, void *buf)
{
	size_t size = tree->bt_elem_size;
	zfs_btree_hdr_t *par_hdr = &parent->btc_hdr;
	ASSERT3P(par_hdr, ==, new_node->bth_parent);
	ASSERT3U(par_hdr->bth_count, <, BTREE_CORE_ELEMS);

	if (zfs_btree_verify_intensity >= 5) {
		zfs_btree_verify_poison_at(tree, par_hdr,
		    par_hdr->bth_count);
	}
	/* Shift existing elements and children */
	uint32_t count = par_hdr->bth_count - offset;
	bt_shift_core_right(tree, parent, offset, count,
	    BSS_PARALLELOGRAM);

	/* Insert new values */
	parent->btc_children[offset + 1] = new_node;
	bcpy(buf, parent->btc_elems + offset * size, size);
	par_hdr->bth_count++;
}

/*
 * Insert new_node into the parent of old_node directly after old_node, with
 * buf as the dividing element between the two.
 */
static void
zfs_btree_insert_into_parent(zfs_btree_t *tree, zfs_btree_hdr_t *old_node,
    zfs_btree_hdr_t *new_node, void *buf)
{
	ASSERT3P(old_node->bth_parent, ==, new_node->bth_parent);
	size_t size = tree->bt_elem_size;
	zfs_btree_core_t *parent = old_node->bth_parent;

	/*
	 * If this is the root node we were splitting, we create a new root
	 * and increase the height of the tree.
	 */
	if (parent == NULL) {
		ASSERT3P(old_node, ==, tree->bt_root);
		tree->bt_num_nodes++;
		zfs_btree_core_t *new_root =
		    kmem_alloc(sizeof (zfs_btree_core_t) + BTREE_CORE_ELEMS *
		    size, KM_SLEEP);
		zfs_btree_hdr_t *new_root_hdr = &new_root->btc_hdr;
		new_root_hdr->bth_parent = NULL;
		new_root_hdr->bth_first = -1;
		new_root_hdr->bth_count = 1;

		old_node->bth_parent = new_node->bth_parent = new_root;
		new_root->btc_children[0] = old_node;
		new_root->btc_children[1] = new_node;
		bcpy(buf, new_root->btc_elems, size);

		tree->bt_height++;
		tree->bt_root = new_root_hdr;
		zfs_btree_poison_node(tree, new_root_hdr);
		return;
	}

	/*
	 * Since we have the new separator, binary search for where to put
	 * new_node.
	 */
	zfs_btree_hdr_t *par_hdr = &parent->btc_hdr;
	zfs_btree_index_t idx;
	ASSERT(zfs_btree_is_core(par_hdr));
	VERIFY3P(zfs_btree_find_in_buf(tree, parent->btc_elems,
	    par_hdr->bth_count, buf, &idx), ==, NULL);
	ASSERT(idx.bti_before);
	uint32_t offset = idx.bti_offset;
	ASSERT3U(offset, <=, par_hdr->bth_count);
	ASSERT3P(parent->btc_children[offset], ==, old_node);

	/*
	 * If the parent isn't full, shift things to accommodate our insertions
	 * and return.
	 */
	if (par_hdr->bth_count != BTREE_CORE_ELEMS) {
		zfs_btree_insert_core_impl(tree, parent, offset, new_node, buf);
		return;
	}

	/*
	 * We need to split this core node into two. Currently there are
	 * BTREE_CORE_ELEMS + 1 child nodes, and we are adding one for
	 * BTREE_CORE_ELEMS + 2. Some of the children will be part of the
	 * current node, and the others will be moved to the new core node.
	 * There are BTREE_CORE_ELEMS + 1 elements including the new one. One
	 * will be used as the new separator in our parent, and the others
	 * will be split among the two core nodes.
	 *
	 * Usually we will split the node in half evenly, with
	 * BTREE_CORE_ELEMS/2 elements in each node. If we're bulk loading, we
	 * instead move only about a quarter of the elements (and children) to
	 * the new node. Since the average state after a long time is a 3/4
	 * full node, shortcutting directly to that state improves efficiency.
	 *
	 * We do this in two stages: first we split into two nodes, and then we
	 * reuse our existing logic to insert the new element and child.
	 */
	uint32_t move_count = MAX((BTREE_CORE_ELEMS / (tree->bt_bulk == NULL ?
	    2 : 4)) - 1, 2);
	uint32_t keep_count = BTREE_CORE_ELEMS - move_count - 1;
	ASSERT3U(BTREE_CORE_ELEMS - move_count, >=, 2);
	tree->bt_num_nodes++;
	zfs_btree_core_t *new_parent = kmem_alloc(sizeof (zfs_btree_core_t) +
	    BTREE_CORE_ELEMS * size, KM_SLEEP);
	zfs_btree_hdr_t *new_par_hdr = &new_parent->btc_hdr;
	new_par_hdr->bth_parent = par_hdr->bth_parent;
	new_par_hdr->bth_first = -1;
	new_par_hdr->bth_count = move_count;
	zfs_btree_poison_node(tree, new_par_hdr);

	par_hdr->bth_count = keep_count;

	bt_transfer_core(tree, parent, keep_count + 1, move_count, new_parent,
	    0, BSS_TRAPEZOID);

	/* Store the new separator in a buffer. */
	uint8_t *tmp_buf = kmem_alloc(size, KM_SLEEP);
	bcpy(parent->btc_elems + keep_count * size, tmp_buf,
	    size);
	zfs_btree_poison_node(tree, par_hdr);

	if (offset < keep_count) {
		/* Insert the new node into the left half */
		zfs_btree_insert_core_impl(tree, parent, offset, new_node,
		    buf);

		/*
		 * Move the new separator to the existing buffer.
		 */
		bcpy(tmp_buf, buf, size);
	} else if (offset > keep_count) {
		/* Insert the new node into the right half */
		new_node->bth_parent = new_parent;
		zfs_btree_insert_core_impl(tree, new_parent,
		    offset - keep_count - 1, new_node, buf);

		/*
		 * Move the new separator to the existing buffer.
		 */
		bcpy(tmp_buf, buf, size);
	} else {
		/*
		 * Move the new separator into the right half, and replace it
		 * with buf. We also need to shift back the elements in the
		 * right half to accommodate new_node.
		 */
		bt_shift_core_right(tree, new_parent, 0, move_count,
		    BSS_TRAPEZOID);
		new_parent->btc_children[0] = new_node;
		bcpy(tmp_buf, new_parent->btc_elems, size);
		new_par_hdr->bth_count++;
	}
	kmem_free(tmp_buf, size);
	zfs_btree_poison_node(tree, par_hdr);

	for (uint32_t i = 0; i <= new_parent->btc_hdr.bth_count; i++)
		new_parent->btc_children[i]->bth_parent = new_parent;

	for (uint32_t i = 0; i <= parent->btc_hdr.bth_count; i++)
		ASSERT3P(parent->btc_children[i]->bth_parent, ==, parent);

	/*
	 * Now that the node is split, we need to insert the new node into its
	 * parent. This may cause further splitting.
	 */
	zfs_btree_insert_into_parent(tree, &parent->btc_hdr,
	    &new_parent->btc_hdr, buf);
}

/* Insert an element into a leaf node at the given offset. */
static void
zfs_btree_insert_leaf_impl(zfs_btree_t *tree, zfs_btree_leaf_t *leaf,
    uint32_t idx, const void *value)
{
	size_t size = tree->bt_elem_size;
	zfs_btree_hdr_t *hdr = &leaf->btl_hdr;
	ASSERT3U(leaf->btl_hdr.bth_count, <, tree->bt_leaf_cap);

	if (zfs_btree_verify_intensity >= 5) {
		zfs_btree_verify_poison_at(tree, &leaf->btl_hdr,
		    leaf->btl_hdr.bth_count);
	}

	bt_grow_leaf(tree, leaf, idx, 1);
	uint8_t *start = leaf->btl_elems + (hdr->bth_first + idx) * size;
	bcpy(value, start, size);
}

static void
zfs_btree_verify_order_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr);

/* Helper function for inserting a new value into leaf at the given index. */
static void
zfs_btree_insert_into_leaf(zfs_btree_t *tree, zfs_btree_leaf_t *leaf,
    const void *value, uint32_t idx)
{
	size_t size = tree->bt_elem_size;
	uint32_t capacity = tree->bt_leaf_cap;

	/*
	 * If the leaf isn't full, shift the elements after idx and insert
	 * value.
	 */
	if (leaf->btl_hdr.bth_count != capacity) {
		zfs_btree_insert_leaf_impl(tree, leaf, idx, value);
		return;
	}

	/*
	 * Otherwise, we split the leaf node into two nodes. If we're not bulk
	 * inserting, each is of size (capacity / 2).  If we are bulk
	 * inserting, we move a quarter of the elements to the new node so
	 * inserts into the old node don't cause immediate splitting but the
	 * tree stays relatively dense. Since the average state after a long
	 * time is a 3/4 full node, shortcutting directly to that state
	 * improves efficiency.  At the end of the bulk insertion process
	 * we'll need to go through and fix up any nodes (the last leaf and
	 * its ancestors, potentially) that are below the minimum.
	 *
	 * In either case, we're left with one extra element. The leftover
	 * element will become the new dividing element between the two nodes.
	 */
	uint32_t move_count = MAX(capacity / (tree->bt_bulk ? 4 : 2), 1) - 1;
	uint32_t keep_count = capacity - move_count - 1;
	ASSERT3U(keep_count, >=, 1);
	/* If we insert on left. move one more to keep leaves balanced.  */
	if (idx < keep_count) {
		keep_count--;
		move_count++;
	}
	tree->bt_num_nodes++;
	zfs_btree_leaf_t *new_leaf = kmem_cache_alloc(zfs_btree_leaf_cache,
	    KM_SLEEP);
	zfs_btree_hdr_t *new_hdr = &new_leaf->btl_hdr;
	new_hdr->bth_parent = leaf->btl_hdr.bth_parent;
	new_hdr->bth_first = (tree->bt_bulk ? 0 : capacity / 4) +
	    (idx >= keep_count && idx <= keep_count + move_count / 2);
	new_hdr->bth_count = move_count;
	zfs_btree_poison_node(tree, new_hdr);

	if (tree->bt_bulk != NULL && leaf == tree->bt_bulk)
		tree->bt_bulk = new_leaf;

	/* Copy the back part to the new leaf. */
	bt_transfer_leaf(tree, leaf, keep_count + 1, move_count, new_leaf, 0);

	/* We store the new separator in a buffer we control for simplicity. */
	uint8_t *buf = kmem_alloc(size, KM_SLEEP);
	bcpy(leaf->btl_elems + (leaf->btl_hdr.bth_first + keep_count) * size,
	    buf, size);

	bt_shrink_leaf(tree, leaf, keep_count, 1 + move_count);

	if (idx < keep_count) {
		/* Insert into the existing leaf. */
		zfs_btree_insert_leaf_impl(tree, leaf, idx, value);
	} else if (idx > keep_count) {
		/* Insert into the new leaf. */
		zfs_btree_insert_leaf_impl(tree, new_leaf, idx - keep_count -
		    1, value);
	} else {
		/*
		 * Insert planned separator into the new leaf, and use
		 * the new value as the new separator.
		 */
		zfs_btree_insert_leaf_impl(tree, new_leaf, 0, buf);
		bcpy(value, buf, size);
	}

	/*
	 * Now that the node is split, we need to insert the new node into its
	 * parent. This may cause further splitting, bur only of core nodes.
	 */
	zfs_btree_insert_into_parent(tree, &leaf->btl_hdr, &new_leaf->btl_hdr,
	    buf);
	kmem_free(buf, size);
}

static uint32_t
zfs_btree_find_parent_idx(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
	void *buf;
	if (zfs_btree_is_core(hdr)) {
		buf = ((zfs_btree_core_t *)hdr)->btc_elems;
	} else {
		buf = ((zfs_btree_leaf_t *)hdr)->btl_elems +
		    hdr->bth_first * tree->bt_elem_size;
	}
	zfs_btree_index_t idx;
	zfs_btree_core_t *parent = hdr->bth_parent;
	VERIFY3P(zfs_btree_find_in_buf(tree, parent->btc_elems,
	    parent->btc_hdr.bth_count, buf, &idx), ==, NULL);
	ASSERT(idx.bti_before);
	ASSERT3U(idx.bti_offset, <=, parent->btc_hdr.bth_count);
	ASSERT3P(parent->btc_children[idx.bti_offset], ==, hdr);
	return (idx.bti_offset);
}

/*
 * Take the b-tree out of bulk insert mode. During bulk-insert mode, some
 * nodes may violate the invariant that non-root nodes must be at least half
 * full. All nodes violating this invariant should be the last node in their
 * particular level. To correct the invariant, we take values from their left
 * neighbor until they are half full. They must have a left neighbor at their
 * level because the last node at a level is not the first node unless it's
 * the root.
 */
static void
zfs_btree_bulk_finish(zfs_btree_t *tree)
{
	ASSERT3P(tree->bt_bulk, !=, NULL);
	ASSERT3P(tree->bt_root, !=, NULL);
	zfs_btree_leaf_t *leaf = tree->bt_bulk;
	zfs_btree_hdr_t *hdr = &leaf->btl_hdr;
	zfs_btree_core_t *parent = hdr->bth_parent;
	size_t size = tree->bt_elem_size;
	uint32_t capacity = tree->bt_leaf_cap;

	/*
	 * The invariant doesn't apply to the root node, if that's the only
	 * node in the tree we're done.
	 */
	if (parent == NULL) {
		tree->bt_bulk = NULL;
		return;
	}

	/* First, take elements to rebalance the leaf node. */
	if (hdr->bth_count < capacity / 2) {
		/*
		 * First, find the left neighbor. The simplest way to do this
		 * is to call zfs_btree_prev twice; the first time finds some
		 * ancestor of this node, and the second time finds the left
		 * neighbor. The ancestor found is the lowest common ancestor
		 * of leaf and the neighbor.
		 */
		zfs_btree_index_t idx = {
			.bti_node = hdr,
			.bti_offset = 0
		};
		VERIFY3P(zfs_btree_prev(tree, &idx, &idx), !=, NULL);
		ASSERT(zfs_btree_is_core(idx.bti_node));
		zfs_btree_core_t *common = (zfs_btree_core_t *)idx.bti_node;
		uint32_t common_idx = idx.bti_offset;

		VERIFY3P(zfs_btree_prev(tree, &idx, &idx), !=, NULL);
		ASSERT(!zfs_btree_is_core(idx.bti_node));
		zfs_btree_leaf_t *l_neighbor = (zfs_btree_leaf_t *)idx.bti_node;
		zfs_btree_hdr_t *l_hdr = idx.bti_node;
		uint32_t move_count = (capacity / 2) - hdr->bth_count;
		ASSERT3U(l_neighbor->btl_hdr.bth_count - move_count, >=,
		    capacity / 2);

		if (zfs_btree_verify_intensity >= 5) {
			for (uint32_t i = 0; i < move_count; i++) {
				zfs_btree_verify_poison_at(tree, hdr,
				    leaf->btl_hdr.bth_count + i);
			}
		}

		/* First, shift elements in leaf back. */
		bt_grow_leaf(tree, leaf, 0, move_count);

		/* Next, move the separator from the common ancestor to leaf. */
		uint8_t *separator = common->btc_elems + common_idx * size;
		uint8_t *out = leaf->btl_elems +
		    (hdr->bth_first + move_count - 1) * size;
		bcpy(separator, out, size);

		/*
		 * Now we move elements from the tail of the left neighbor to
		 * fill the remaining spots in leaf.
		 */
		bt_transfer_leaf(tree, l_neighbor, l_hdr->bth_count -
		    (move_count - 1), move_count - 1, leaf, 0);

		/*
		 * Finally, move the new last element in the left neighbor to
		 * the separator.
		 */
		bcpy(l_neighbor->btl_elems + (l_hdr->bth_first +
		    l_hdr->bth_count - move_count) * size, separator, size);

		/* Adjust the node's counts, and we're done. */
		bt_shrink_leaf(tree, l_neighbor, l_hdr->bth_count - move_count,
		    move_count);

		ASSERT3U(l_hdr->bth_count, >=, capacity / 2);
		ASSERT3U(hdr->bth_count, >=, capacity / 2);
	}

	/*
	 * Now we have to rebalance any ancestors of leaf that may also
	 * violate the invariant.
	 */
	capacity = BTREE_CORE_ELEMS;
	while (parent->btc_hdr.bth_parent != NULL) {
		zfs_btree_core_t *cur = parent;
		zfs_btree_hdr_t *hdr = &cur->btc_hdr;
		parent = hdr->bth_parent;
		/*
		 * If the invariant isn't violated, move on to the next
		 * ancestor.
		 */
		if (hdr->bth_count >= capacity / 2)
			continue;

		/*
		 * Because the smallest number of nodes we can move when
		 * splitting is 2, we never need to worry about not having a
		 * left sibling (a sibling is a neighbor with the same parent).
		 */
		uint32_t parent_idx = zfs_btree_find_parent_idx(tree, hdr);
		ASSERT3U(parent_idx, >, 0);
		zfs_btree_core_t *l_neighbor =
		    (zfs_btree_core_t *)parent->btc_children[parent_idx - 1];
		uint32_t move_count = (capacity / 2) - hdr->bth_count;
		ASSERT3U(l_neighbor->btc_hdr.bth_count - move_count, >=,
		    capacity / 2);

		if (zfs_btree_verify_intensity >= 5) {
			for (uint32_t i = 0; i < move_count; i++) {
				zfs_btree_verify_poison_at(tree, hdr,
				    hdr->bth_count + i);
			}
		}
		/* First, shift things in the right node back. */
		bt_shift_core(tree, cur, 0, hdr->bth_count, move_count,
		    BSS_TRAPEZOID, BSD_RIGHT);

		/* Next, move the separator to the right node. */
		uint8_t *separator = parent->btc_elems + ((parent_idx - 1) *
		    size);
		uint8_t *e_out = cur->btc_elems + ((move_count - 1) * size);
		bcpy(separator, e_out, size);

		/*
		 * Now, move elements and children from the left node to the
		 * right.  We move one more child than elements.
		 */
		move_count--;
		uint32_t move_idx = l_neighbor->btc_hdr.bth_count - move_count;
		bt_transfer_core(tree, l_neighbor, move_idx, move_count, cur, 0,
		    BSS_TRAPEZOID);

		/*
		 * Finally, move the last element in the left node to the
		 * separator's position.
		 */
		move_idx--;
		bcpy(l_neighbor->btc_elems + move_idx * size, separator, size);

		l_neighbor->btc_hdr.bth_count -= move_count + 1;
		hdr->bth_count += move_count + 1;

		ASSERT3U(l_neighbor->btc_hdr.bth_count, >=, capacity / 2);
		ASSERT3U(hdr->bth_count, >=, capacity / 2);

		zfs_btree_poison_node(tree, &l_neighbor->btc_hdr);

		for (uint32_t i = 0; i <= hdr->bth_count; i++)
			cur->btc_children[i]->bth_parent = cur;
	}

	tree->bt_bulk = NULL;
	zfs_btree_verify(tree);
}

/*
 * Insert value into tree at the location specified by where.
 */
void
zfs_btree_add_idx(zfs_btree_t *tree, const void *value,
    const zfs_btree_index_t *where)
{
	zfs_btree_index_t idx = {0};

	/* If we're not inserting in the last leaf, end bulk insert mode. */
	if (tree->bt_bulk != NULL) {
		if (where->bti_node != &tree->bt_bulk->btl_hdr) {
			zfs_btree_bulk_finish(tree);
			VERIFY3P(zfs_btree_find(tree, value, &idx), ==, NULL);
			where = &idx;
		}
	}

	tree->bt_num_elems++;
	/*
	 * If this is the first element in the tree, create a leaf root node
	 * and add the value to it.
	 */
	if (where->bti_node == NULL) {
		ASSERT3U(tree->bt_num_elems, ==, 1);
		ASSERT3S(tree->bt_height, ==, -1);
		ASSERT3P(tree->bt_root, ==, NULL);
		ASSERT0(where->bti_offset);

		tree->bt_num_nodes++;
		zfs_btree_leaf_t *leaf = kmem_cache_alloc(zfs_btree_leaf_cache,
		    KM_SLEEP);
		tree->bt_root = &leaf->btl_hdr;
		tree->bt_height++;

		zfs_btree_hdr_t *hdr = &leaf->btl_hdr;
		hdr->bth_parent = NULL;
		hdr->bth_first = 0;
		hdr->bth_count = 0;
		zfs_btree_poison_node(tree, hdr);

		zfs_btree_insert_into_leaf(tree, leaf, value, 0);
		tree->bt_bulk = leaf;
	} else if (!zfs_btree_is_core(where->bti_node)) {
		/*
		 * If we're inserting into a leaf, go directly to the helper
		 * function.
		 */
		zfs_btree_insert_into_leaf(tree,
		    (zfs_btree_leaf_t *)where->bti_node, value,
		    where->bti_offset);
	} else {
		/*
		 * If we're inserting into a core node, we can't just shift
		 * the existing element in that slot in the same node without
		 * breaking our ordering invariants. Instead we place the new
		 * value in the node at that spot and then insert the old
		 * separator into the first slot in the subtree to the right.
		 */
		zfs_btree_core_t *node = (zfs_btree_core_t *)where->bti_node;

		/*
		 * We can ignore bti_before, because either way the value
		 * should end up in bti_offset.
		 */
		uint32_t off = where->bti_offset;
		zfs_btree_hdr_t *subtree = node->btc_children[off + 1];
		size_t size = tree->bt_elem_size;
		uint8_t *buf = kmem_alloc(size, KM_SLEEP);
		bcpy(node->btc_elems + off * size, buf, size);
		bcpy(value, node->btc_elems + off * size, size);

		/*
		 * Find the first slot in the subtree to the right, insert
		 * there.
		 */
		zfs_btree_index_t new_idx;
		VERIFY3P(zfs_btree_first_helper(tree, subtree, &new_idx), !=,
		    NULL);
		ASSERT0(new_idx.bti_offset);
		ASSERT(!zfs_btree_is_core(new_idx.bti_node));
		zfs_btree_insert_into_leaf(tree,
		    (zfs_btree_leaf_t *)new_idx.bti_node, buf, 0);
		kmem_free(buf, size);
	}
	zfs_btree_verify(tree);
}

/*
 * Return the first element in the tree, and put its location in where if
 * non-null.
 */
void *
zfs_btree_first(zfs_btree_t *tree, zfs_btree_index_t *where)
{
	if (tree->bt_height == -1) {
		ASSERT0(tree->bt_num_elems);
		return (NULL);
	}
	return (zfs_btree_first_helper(tree, tree->bt_root, where));
}

/*
 * Find the last element in the subtree rooted at hdr, return its value and
 * put its location in where if non-null.
 */
static void *
zfs_btree_last_helper(zfs_btree_t *btree, zfs_btree_hdr_t *hdr,
    zfs_btree_index_t *where)
{
	zfs_btree_hdr_t *node;

	for (node = hdr; zfs_btree_is_core(node); node =
	    ((zfs_btree_core_t *)node)->btc_children[node->bth_count])
		;

	zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)node;
	if (where != NULL) {
		where->bti_node = node;
		where->bti_offset = node->bth_count - 1;
		where->bti_before = B_FALSE;
	}
	return (leaf->btl_elems + (node->bth_first + node->bth_count - 1) *
	    btree->bt_elem_size);
}

/*
 * Return the last element in the tree, and put its location in where if
 * non-null.
 */
void *
zfs_btree_last(zfs_btree_t *tree, zfs_btree_index_t *where)
{
	if (tree->bt_height == -1) {
		ASSERT0(tree->bt_num_elems);
		return (NULL);
	}
	return (zfs_btree_last_helper(tree, tree->bt_root, where));
}

/*
 * This function contains the logic to find the next node in the tree. A
 * helper function is used because there are multiple internal consumemrs of
 * this logic. The done_func is used by zfs_btree_destroy_nodes to clean up each
 * node after we've finished with it.
 */
static void *
zfs_btree_next_helper(zfs_btree_t *tree, const zfs_btree_index_t *idx,
    zfs_btree_index_t *out_idx,
    void (*done_func)(zfs_btree_t *, zfs_btree_hdr_t *))
{
	if (idx->bti_node == NULL) {
		ASSERT3S(tree->bt_height, ==, -1);
		return (NULL);
	}

	uint32_t offset = idx->bti_offset;
	if (!zfs_btree_is_core(idx->bti_node)) {
		/*
		 * When finding the next element of an element in a leaf,
		 * there are two cases. If the element isn't the last one in
		 * the leaf, in which case we just return the next element in
		 * the leaf. Otherwise, we need to traverse up our parents
		 * until we find one where our ancestor isn't the last child
		 * of its parent. Once we do, the next element is the
		 * separator after our ancestor in its parent.
		 */
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)idx->bti_node;
		uint32_t new_off = offset + (idx->bti_before ? 0 : 1);
		if (leaf->btl_hdr.bth_count > new_off) {
			out_idx->bti_node = &leaf->btl_hdr;
			out_idx->bti_offset = new_off;
			out_idx->bti_before = B_FALSE;
			return (leaf->btl_elems + (leaf->btl_hdr.bth_first +
			    new_off) * tree->bt_elem_size);
		}

		zfs_btree_hdr_t *prev = &leaf->btl_hdr;
		for (zfs_btree_core_t *node = leaf->btl_hdr.bth_parent;
		    node != NULL; node = node->btc_hdr.bth_parent) {
			zfs_btree_hdr_t *hdr = &node->btc_hdr;
			ASSERT(zfs_btree_is_core(hdr));
			uint32_t i = zfs_btree_find_parent_idx(tree, prev);
			if (done_func != NULL)
				done_func(tree, prev);
			if (i == hdr->bth_count) {
				prev = hdr;
				continue;
			}
			out_idx->bti_node = hdr;
			out_idx->bti_offset = i;
			out_idx->bti_before = B_FALSE;
			return (node->btc_elems + i * tree->bt_elem_size);
		}
		if (done_func != NULL)
			done_func(tree, prev);
		/*
		 * We've traversed all the way up and been at the end of the
		 * node every time, so this was the last element in the tree.
		 */
		return (NULL);
	}

	/* If we were before an element in a core node, return that element. */
	ASSERT(zfs_btree_is_core(idx->bti_node));
	zfs_btree_core_t *node = (zfs_btree_core_t *)idx->bti_node;
	if (idx->bti_before) {
		out_idx->bti_before = B_FALSE;
		return (node->btc_elems + offset * tree->bt_elem_size);
	}

	/*
	 * The next element from one in a core node is the first element in
	 * the subtree just to the right of the separator.
	 */
	zfs_btree_hdr_t *child = node->btc_children[offset + 1];
	return (zfs_btree_first_helper(tree, child, out_idx));
}

/*
 * Return the next valued node in the tree.  The same address can be safely
 * passed for idx and out_idx.
 */
void *
zfs_btree_next(zfs_btree_t *tree, const zfs_btree_index_t *idx,
    zfs_btree_index_t *out_idx)
{
	return (zfs_btree_next_helper(tree, idx, out_idx, NULL));
}

/*
 * Return the previous valued node in the tree.  The same value can be safely
 * passed for idx and out_idx.
 */
void *
zfs_btree_prev(zfs_btree_t *tree, const zfs_btree_index_t *idx,
    zfs_btree_index_t *out_idx)
{
	if (idx->bti_node == NULL) {
		ASSERT3S(tree->bt_height, ==, -1);
		return (NULL);
	}

	uint32_t offset = idx->bti_offset;
	if (!zfs_btree_is_core(idx->bti_node)) {
		/*
		 * When finding the previous element of an element in a leaf,
		 * there are two cases. If the element isn't the first one in
		 * the leaf, in which case we just return the previous element
		 * in the leaf. Otherwise, we need to traverse up our parents
		 * until we find one where our previous ancestor isn't the
		 * first child. Once we do, the previous element is the
		 * separator after our previous ancestor.
		 */
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)idx->bti_node;
		if (offset != 0) {
			out_idx->bti_node = &leaf->btl_hdr;
			out_idx->bti_offset = offset - 1;
			out_idx->bti_before = B_FALSE;
			return (leaf->btl_elems + (leaf->btl_hdr.bth_first +
			    offset - 1) * tree->bt_elem_size);
		}
		zfs_btree_hdr_t *prev = &leaf->btl_hdr;
		for (zfs_btree_core_t *node = leaf->btl_hdr.bth_parent;
		    node != NULL; node = node->btc_hdr.bth_parent) {
			zfs_btree_hdr_t *hdr = &node->btc_hdr;
			ASSERT(zfs_btree_is_core(hdr));
			uint32_t i = zfs_btree_find_parent_idx(tree, prev);
			if (i == 0) {
				prev = hdr;
				continue;
			}
			out_idx->bti_node = hdr;
			out_idx->bti_offset = i - 1;
			out_idx->bti_before = B_FALSE;
			return (node->btc_elems + (i - 1) * tree->bt_elem_size);
		}
		/*
		 * We've traversed all the way up and been at the start of the
		 * node every time, so this was the first node in the tree.
		 */
		return (NULL);
	}

	/*
	 * The previous element from one in a core node is the last element in
	 * the subtree just to the left of the separator.
	 */
	ASSERT(zfs_btree_is_core(idx->bti_node));
	zfs_btree_core_t *node = (zfs_btree_core_t *)idx->bti_node;
	zfs_btree_hdr_t *child = node->btc_children[offset];
	return (zfs_btree_last_helper(tree, child, out_idx));
}

/*
 * Get the value at the provided index in the tree.
 *
 * Note that the value returned from this function can be mutated, but only
 * if it will not change the ordering of the element with respect to any other
 * elements that could be in the tree.
 */
void *
zfs_btree_get(zfs_btree_t *tree, zfs_btree_index_t *idx)
{
	ASSERT(!idx->bti_before);
	size_t size = tree->bt_elem_size;
	if (!zfs_btree_is_core(idx->bti_node)) {
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)idx->bti_node;
		return (leaf->btl_elems + (leaf->btl_hdr.bth_first +
		    idx->bti_offset) * size);
	}
	zfs_btree_core_t *node = (zfs_btree_core_t *)idx->bti_node;
	return (node->btc_elems + idx->bti_offset * size);
}

/* Add the given value to the tree. Must not already be in the tree. */
void
zfs_btree_add(zfs_btree_t *tree, const void *node)
{
	zfs_btree_index_t where = {0};
	VERIFY3P(zfs_btree_find(tree, node, &where), ==, NULL);
	zfs_btree_add_idx(tree, node, &where);
}

/* Helper function to free a tree node. */
static void
zfs_btree_node_destroy(zfs_btree_t *tree, zfs_btree_hdr_t *node)
{
	tree->bt_num_nodes--;
	if (!zfs_btree_is_core(node)) {
		kmem_cache_free(zfs_btree_leaf_cache, node);
	} else {
		kmem_free(node, sizeof (zfs_btree_core_t) +
		    BTREE_CORE_ELEMS * tree->bt_elem_size);
	}
}

/*
 * Remove the rm_hdr and the separator to its left from the parent node. The
 * buffer that rm_hdr was stored in may already be freed, so its contents
 * cannot be accessed.
 */
static void
zfs_btree_remove_from_node(zfs_btree_t *tree, zfs_btree_core_t *node,
    zfs_btree_hdr_t *rm_hdr)
{
	size_t size = tree->bt_elem_size;
	uint32_t min_count = (BTREE_CORE_ELEMS / 2) - 1;
	zfs_btree_hdr_t *hdr = &node->btc_hdr;
	/*
	 * If the node is the root node and rm_hdr is one of two children,
	 * promote the other child to the root.
	 */
	if (hdr->bth_parent == NULL && hdr->bth_count <= 1) {
		ASSERT3U(hdr->bth_count, ==, 1);
		ASSERT3P(tree->bt_root, ==, node);
		ASSERT3P(node->btc_children[1], ==, rm_hdr);
		tree->bt_root = node->btc_children[0];
		node->btc_children[0]->bth_parent = NULL;
		zfs_btree_node_destroy(tree, hdr);
		tree->bt_height--;
		return;
	}

	uint32_t idx;
	for (idx = 0; idx <= hdr->bth_count; idx++) {
		if (node->btc_children[idx] == rm_hdr)
			break;
	}
	ASSERT3U(idx, <=, hdr->bth_count);

	/*
	 * If the node is the root or it has more than the minimum number of
	 * children, just remove the child and separator, and return.
	 */
	if (hdr->bth_parent == NULL ||
	    hdr->bth_count > min_count) {
		/*
		 * Shift the element and children to the right of rm_hdr to
		 * the left by one spot.
		 */
		bt_shift_core_left(tree, node, idx, hdr->bth_count - idx,
		    BSS_PARALLELOGRAM);
		hdr->bth_count--;
		zfs_btree_poison_node_at(tree, hdr, hdr->bth_count, 1);
		return;
	}

	ASSERT3U(hdr->bth_count, ==, min_count);

	/*
	 * Now we try to take a node from a neighbor. We check left, then
	 * right. If the neighbor exists and has more than the minimum number
	 * of elements, we move the separator between us and them to our
	 * node, move their closest element (last for left, first for right)
	 * to the separator, and move their closest child to our node. Along
	 * the way we need to collapse the gap made by idx, and (for our right
	 * neighbor) the gap made by removing their first element and child.
	 *
	 * Note: this logic currently doesn't support taking from a neighbor
	 * that isn't a sibling (i.e. a neighbor with a different
	 * parent). This isn't critical functionality, but may be worth
	 * implementing in the future for completeness' sake.
	 */
	zfs_btree_core_t *parent = hdr->bth_parent;
	uint32_t parent_idx = zfs_btree_find_parent_idx(tree, hdr);

	zfs_btree_hdr_t *l_hdr = (parent_idx == 0 ? NULL :
	    parent->btc_children[parent_idx - 1]);
	if (l_hdr != NULL && l_hdr->bth_count > min_count) {
		/* We can take a node from the left neighbor. */
		ASSERT(zfs_btree_is_core(l_hdr));
		zfs_btree_core_t *neighbor = (zfs_btree_core_t *)l_hdr;

		/*
		 * Start by shifting the elements and children in the current
		 * node to the right by one spot.
		 */
		bt_shift_core_right(tree, node, 0, idx - 1, BSS_TRAPEZOID);

		/*
		 * Move the separator between node and neighbor to the first
		 * element slot in the current node.
		 */
		uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
		    size;
		bcpy(separator, node->btc_elems, size);

		/* Move the last child of neighbor to our first child slot. */
		node->btc_children[0] =
		    neighbor->btc_children[l_hdr->bth_count];
		node->btc_children[0]->bth_parent = node;

		/* Move the last element of neighbor to the separator spot. */
		uint8_t *take_elem = neighbor->btc_elems +
		    (l_hdr->bth_count - 1) * size;
		bcpy(take_elem, separator, size);
		l_hdr->bth_count--;
		zfs_btree_poison_node_at(tree, l_hdr, l_hdr->bth_count, 1);
		return;
	}

	zfs_btree_hdr_t *r_hdr = (parent_idx == parent->btc_hdr.bth_count ?
	    NULL : parent->btc_children[parent_idx + 1]);
	if (r_hdr != NULL && r_hdr->bth_count > min_count) {
		/* We can take a node from the right neighbor. */
		ASSERT(zfs_btree_is_core(r_hdr));
		zfs_btree_core_t *neighbor = (zfs_btree_core_t *)r_hdr;

		/*
		 * Shift elements in node left by one spot to overwrite rm_hdr
		 * and the separator before it.
		 */
		bt_shift_core_left(tree, node, idx, hdr->bth_count - idx,
		    BSS_PARALLELOGRAM);

		/*
		 * Move the separator between node and neighbor to the last
		 * element spot in node.
		 */
		uint8_t *separator = parent->btc_elems + parent_idx * size;
		bcpy(separator, node->btc_elems + (hdr->bth_count - 1) * size,
		    size);

		/*
		 * Move the first child of neighbor to the last child spot in
		 * node.
		 */
		node->btc_children[hdr->bth_count] = neighbor->btc_children[0];
		node->btc_children[hdr->bth_count]->bth_parent = node;

		/* Move the first element of neighbor to the separator spot. */
		uint8_t *take_elem = neighbor->btc_elems;
		bcpy(take_elem, separator, size);
		r_hdr->bth_count--;

		/*
		 * Shift the elements and children of neighbor to cover the
		 * stolen elements.
		 */
		bt_shift_core_left(tree, neighbor, 1, r_hdr->bth_count,
		    BSS_TRAPEZOID);
		zfs_btree_poison_node_at(tree, r_hdr, r_hdr->bth_count, 1);
		return;
	}

	/*
	 * In this case, neither of our neighbors can spare an element, so we
	 * need to merge with one of them. We prefer the left one,
	 * arbitrarily. Move the separator into the leftmost merging node
	 * (which may be us or the left neighbor), and then move the right
	 * merging node's elements. Once that's done, we go back and delete
	 * the element we're removing. Finally, go into the parent and delete
	 * the right merging node and the separator. This may cause further
	 * merging.
	 */
	zfs_btree_hdr_t *new_rm_hdr, *keep_hdr;
	uint32_t new_idx = idx;
	if (l_hdr != NULL) {
		keep_hdr = l_hdr;
		new_rm_hdr = hdr;
		new_idx += keep_hdr->bth_count + 1;
	} else {
		ASSERT3P(r_hdr, !=, NULL);
		keep_hdr = hdr;
		new_rm_hdr = r_hdr;
		parent_idx++;
	}

	ASSERT(zfs_btree_is_core(keep_hdr));
	ASSERT(zfs_btree_is_core(new_rm_hdr));

	zfs_btree_core_t *keep = (zfs_btree_core_t *)keep_hdr;
	zfs_btree_core_t *rm = (zfs_btree_core_t *)new_rm_hdr;

	if (zfs_btree_verify_intensity >= 5) {
		for (uint32_t i = 0; i < new_rm_hdr->bth_count + 1; i++) {
			zfs_btree_verify_poison_at(tree, keep_hdr,
			    keep_hdr->bth_count + i);
		}
	}

	/* Move the separator into the left node. */
	uint8_t *e_out = keep->btc_elems + keep_hdr->bth_count * size;
	uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
	    size;
	bcpy(separator, e_out, size);
	keep_hdr->bth_count++;

	/* Move all our elements and children into the left node. */
	bt_transfer_core(tree, rm, 0, new_rm_hdr->bth_count, keep,
	    keep_hdr->bth_count, BSS_TRAPEZOID);

	uint32_t old_count = keep_hdr->bth_count;

	/* Update bookkeeping */
	keep_hdr->bth_count += new_rm_hdr->bth_count;
	ASSERT3U(keep_hdr->bth_count, ==, (min_count * 2) + 1);

	/*
	 * Shift the element and children to the right of rm_hdr to
	 * the left by one spot.
	 */
	ASSERT3P(keep->btc_children[new_idx], ==, rm_hdr);
	bt_shift_core_left(tree, keep, new_idx, keep_hdr->bth_count - new_idx,
	    BSS_PARALLELOGRAM);
	keep_hdr->bth_count--;

	/* Reparent all our children to point to the left node. */
	zfs_btree_hdr_t **new_start = keep->btc_children +
	    old_count - 1;
	for (uint32_t i = 0; i < new_rm_hdr->bth_count + 1; i++)
		new_start[i]->bth_parent = keep;
	for (uint32_t i = 0; i <= keep_hdr->bth_count; i++) {
		ASSERT3P(keep->btc_children[i]->bth_parent, ==, keep);
		ASSERT3P(keep->btc_children[i], !=, rm_hdr);
	}
	zfs_btree_poison_node_at(tree, keep_hdr, keep_hdr->bth_count, 1);

	new_rm_hdr->bth_count = 0;
	zfs_btree_node_destroy(tree, new_rm_hdr);
	zfs_btree_remove_from_node(tree, parent, new_rm_hdr);
}

/* Remove the element at the specific location. */
void
zfs_btree_remove_idx(zfs_btree_t *tree, zfs_btree_index_t *where)
{
	size_t size = tree->bt_elem_size;
	zfs_btree_hdr_t *hdr = where->bti_node;
	uint32_t idx = where->bti_offset;

	ASSERT(!where->bti_before);
	if (tree->bt_bulk != NULL) {
		/*
		 * Leave bulk insert mode. Note that our index would be
		 * invalid after we correct the tree, so we copy the value
		 * we're planning to remove and find it again after
		 * bulk_finish.
		 */
		uint8_t *value = zfs_btree_get(tree, where);
		uint8_t *tmp = kmem_alloc(size, KM_SLEEP);
		bcpy(value, tmp, size);
		zfs_btree_bulk_finish(tree);
		VERIFY3P(zfs_btree_find(tree, tmp, where), !=, NULL);
		kmem_free(tmp, size);
		hdr = where->bti_node;
		idx = where->bti_offset;
	}

	tree->bt_num_elems--;
	/*
	 * If the element happens to be in a core node, we move a leaf node's
	 * element into its place and then remove the leaf node element. This
	 * makes the rebalance logic not need to be recursive both upwards and
	 * downwards.
	 */
	if (zfs_btree_is_core(hdr)) {
		zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
		zfs_btree_hdr_t *left_subtree = node->btc_children[idx];
		void *new_value = zfs_btree_last_helper(tree, left_subtree,
		    where);
		ASSERT3P(new_value, !=, NULL);

		bcpy(new_value, node->btc_elems + idx * size, size);

		hdr = where->bti_node;
		idx = where->bti_offset;
		ASSERT(!where->bti_before);
	}

	/*
	 * First, we'll update the leaf's metadata. Then, we shift any
	 * elements after the idx to the left. After that, we rebalance if
	 * needed.
	 */
	ASSERT(!zfs_btree_is_core(hdr));
	zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)hdr;
	ASSERT3U(hdr->bth_count, >, 0);

	uint32_t min_count = (tree->bt_leaf_cap / 2) - 1;

	/*
	 * If we're over the minimum size or this is the root, just overwrite
	 * the value and return.
	 */
	if (hdr->bth_count > min_count || hdr->bth_parent == NULL) {
		bt_shrink_leaf(tree, leaf, idx, 1);
		if (hdr->bth_parent == NULL) {
			ASSERT0(tree->bt_height);
			if (hdr->bth_count == 0) {
				tree->bt_root = NULL;
				tree->bt_height--;
				zfs_btree_node_destroy(tree, &leaf->btl_hdr);
			}
		}
		zfs_btree_verify(tree);
		return;
	}
	ASSERT3U(hdr->bth_count, ==, min_count);

	/*
	 * Now we try to take a node from a sibling. We check left, then
	 * right. If they exist and have more than the minimum number of
	 * elements, we move the separator between us and them to our node
	 * and move their closest element (last for left, first for right) to
	 * the separator. Along the way we need to collapse the gap made by
	 * idx, and (for our right neighbor) the gap made by removing their
	 * first element.
	 *
	 * Note: this logic currently doesn't support taking from a neighbor
	 * that isn't a sibling. This isn't critical functionality, but may be
	 * worth implementing in the future for completeness' sake.
	 */
	zfs_btree_core_t *parent = hdr->bth_parent;
	uint32_t parent_idx = zfs_btree_find_parent_idx(tree, hdr);

	zfs_btree_hdr_t *l_hdr = (parent_idx == 0 ? NULL :
	    parent->btc_children[parent_idx - 1]);
	if (l_hdr != NULL && l_hdr->bth_count > min_count) {
		/* We can take a node from the left neighbor. */
		ASSERT(!zfs_btree_is_core(l_hdr));
		zfs_btree_leaf_t *neighbor = (zfs_btree_leaf_t *)l_hdr;

		/*
		 * Move our elements back by one spot to make room for the
		 * stolen element and overwrite the element being removed.
		 */
		bt_shift_leaf(tree, leaf, 0, idx, 1, BSD_RIGHT);

		/* Move the separator to our first spot. */
		uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
		    size;
		bcpy(separator, leaf->btl_elems + hdr->bth_first * size, size);

		/* Move our neighbor's last element to the separator. */
		uint8_t *take_elem = neighbor->btl_elems +
		    (l_hdr->bth_first + l_hdr->bth_count - 1) * size;
		bcpy(take_elem, separator, size);

		/* Delete our neighbor's last element. */
		bt_shrink_leaf(tree, neighbor, l_hdr->bth_count - 1, 1);
		zfs_btree_verify(tree);
		return;
	}

	zfs_btree_hdr_t *r_hdr = (parent_idx == parent->btc_hdr.bth_count ?
	    NULL : parent->btc_children[parent_idx + 1]);
	if (r_hdr != NULL && r_hdr->bth_count > min_count) {
		/* We can take a node from the right neighbor. */
		ASSERT(!zfs_btree_is_core(r_hdr));
		zfs_btree_leaf_t *neighbor = (zfs_btree_leaf_t *)r_hdr;

		/*
		 * Move our elements after the element being removed forwards
		 * by one spot to make room for the stolen element and
		 * overwrite the element being removed.
		 */
		bt_shift_leaf(tree, leaf, idx + 1, hdr->bth_count - idx - 1,
		    1, BSD_LEFT);

		/* Move the separator between us to our last spot. */
		uint8_t *separator = parent->btc_elems + parent_idx * size;
		bcpy(separator, leaf->btl_elems + (hdr->bth_first +
		    hdr->bth_count - 1) * size, size);

		/* Move our neighbor's first element to the separator. */
		uint8_t *take_elem = neighbor->btl_elems +
		    r_hdr->bth_first * size;
		bcpy(take_elem, separator, size);

		/* Delete our neighbor's first element. */
		bt_shrink_leaf(tree, neighbor, 0, 1);
		zfs_btree_verify(tree);
		return;
	}

	/*
	 * In this case, neither of our neighbors can spare an element, so we
	 * need to merge with one of them. We prefer the left one, arbitrarily.
	 * After remove we move the separator into the leftmost merging node
	 * (which may be us or the left neighbor), and then move the right
	 * merging node's elements. Once that's done, we go back and delete
	 * the element we're removing. Finally, go into the parent and delete
	 * the right merging node and the separator. This may cause further
	 * merging.
	 */
	zfs_btree_hdr_t *rm_hdr, *k_hdr;
	if (l_hdr != NULL) {
		k_hdr = l_hdr;
		rm_hdr = hdr;
	} else {
		ASSERT3P(r_hdr, !=, NULL);
		k_hdr = hdr;
		rm_hdr = r_hdr;
		parent_idx++;
	}
	ASSERT(!zfs_btree_is_core(k_hdr));
	ASSERT(!zfs_btree_is_core(rm_hdr));
	ASSERT3U(k_hdr->bth_count, ==, min_count);
	ASSERT3U(rm_hdr->bth_count, ==, min_count);
	zfs_btree_leaf_t *keep = (zfs_btree_leaf_t *)k_hdr;
	zfs_btree_leaf_t *rm = (zfs_btree_leaf_t *)rm_hdr;

	if (zfs_btree_verify_intensity >= 5) {
		for (uint32_t i = 0; i < rm_hdr->bth_count + 1; i++) {
			zfs_btree_verify_poison_at(tree, k_hdr,
			    k_hdr->bth_count + i);
		}
	}

	/*
	 * Remove the value from the node.  It will go below the minimum,
	 * but we'll fix it in no time.
	 */
	bt_shrink_leaf(tree, leaf, idx, 1);

	/* Prepare space for elements to be moved from the right. */
	uint32_t k_count = k_hdr->bth_count;
	bt_grow_leaf(tree, keep, k_count, 1 + rm_hdr->bth_count);
	ASSERT3U(k_hdr->bth_count, ==, min_count * 2);

	/* Move the separator into the first open spot. */
	uint8_t *out = keep->btl_elems + (k_hdr->bth_first + k_count) * size;
	uint8_t *separator = parent->btc_elems + (parent_idx - 1) * size;
	bcpy(separator, out, size);

	/* Move our elements to the left neighbor. */
	bt_transfer_leaf(tree, rm, 0, rm_hdr->bth_count, keep, k_count + 1);
	zfs_btree_node_destroy(tree, rm_hdr);

	/* Remove the emptied node from the parent. */
	zfs_btree_remove_from_node(tree, parent, rm_hdr);
	zfs_btree_verify(tree);
}

/* Remove the given value from the tree. */
void
zfs_btree_remove(zfs_btree_t *tree, const void *value)
{
	zfs_btree_index_t where = {0};
	VERIFY3P(zfs_btree_find(tree, value, &where), !=, NULL);
	zfs_btree_remove_idx(tree, &where);
}

/* Return the number of elements in the tree. */
ulong_t
zfs_btree_numnodes(zfs_btree_t *tree)
{
	return (tree->bt_num_elems);
}

/*
 * This function is used to visit all the elements in the tree before
 * destroying the tree. This allows the calling code to perform any cleanup it
 * needs to do. This is more efficient than just removing the first element
 * over and over, because it removes all rebalancing. Once the destroy_nodes()
 * function has been called, no other btree operations are valid until it
 * returns NULL, which point the only valid operation is zfs_btree_destroy().
 *
 * example:
 *
 *      zfs_btree_index_t *cookie = NULL;
 *      my_data_t *node;
 *
 *      while ((node = zfs_btree_destroy_nodes(tree, &cookie)) != NULL)
 *              free(node->ptr);
 *      zfs_btree_destroy(tree);
 *
 */
void *
zfs_btree_destroy_nodes(zfs_btree_t *tree, zfs_btree_index_t **cookie)
{
	if (*cookie == NULL) {
		if (tree->bt_height == -1)
			return (NULL);
		*cookie = kmem_alloc(sizeof (**cookie), KM_SLEEP);
		return (zfs_btree_first(tree, *cookie));
	}

	void *rval = zfs_btree_next_helper(tree, *cookie, *cookie,
	    zfs_btree_node_destroy);
	if (rval == NULL)   {
		tree->bt_root = NULL;
		tree->bt_height = -1;
		tree->bt_num_elems = 0;
		kmem_free(*cookie, sizeof (**cookie));
		tree->bt_bulk = NULL;
	}
	return (rval);
}

static void
zfs_btree_clear_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
	if (zfs_btree_is_core(hdr)) {
		zfs_btree_core_t *btc = (zfs_btree_core_t *)hdr;
		for (uint32_t i = 0; i <= hdr->bth_count; i++)
			zfs_btree_clear_helper(tree, btc->btc_children[i]);
	}

	zfs_btree_node_destroy(tree, hdr);
}

void
zfs_btree_clear(zfs_btree_t *tree)
{
	if (tree->bt_root == NULL) {
		ASSERT0(tree->bt_num_elems);
		return;
	}

	zfs_btree_clear_helper(tree, tree->bt_root);
	tree->bt_num_elems = 0;
	tree->bt_root = NULL;
	tree->bt_num_nodes = 0;
	tree->bt_height = -1;
	tree->bt_bulk = NULL;
}

void
zfs_btree_destroy(zfs_btree_t *tree)
{
	ASSERT0(tree->bt_num_elems);
	ASSERT3P(tree->bt_root, ==, NULL);
}

/* Verify that every child of this node has the correct parent pointer. */
static void
zfs_btree_verify_pointers_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
	if (!zfs_btree_is_core(hdr))
		return;

	zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
	for (uint32_t i = 0; i <= hdr->bth_count; i++) {
		VERIFY3P(node->btc_children[i]->bth_parent, ==, hdr);
		zfs_btree_verify_pointers_helper(tree, node->btc_children[i]);
	}
}

/* Verify that every node has the correct parent pointer. */
static void
zfs_btree_verify_pointers(zfs_btree_t *tree)
{
	if (tree->bt_height == -1) {
		VERIFY3P(tree->bt_root, ==, NULL);
		return;
	}
	VERIFY3P(tree->bt_root->bth_parent, ==, NULL);
	zfs_btree_verify_pointers_helper(tree, tree->bt_root);
}

/*
 * Verify that all the current node and its children satisfy the count
 * invariants, and return the total count in the subtree rooted in this node.
 */
static uint64_t
zfs_btree_verify_counts_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
	if (!zfs_btree_is_core(hdr)) {
		if (tree->bt_root != hdr && tree->bt_bulk &&
		    hdr != &tree->bt_bulk->btl_hdr) {
			VERIFY3U(hdr->bth_count, >=, tree->bt_leaf_cap / 2 - 1);
		}

		return (hdr->bth_count);
	} else {

		zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
		uint64_t ret = hdr->bth_count;
		if (tree->bt_root != hdr && tree->bt_bulk == NULL)
			VERIFY3P(hdr->bth_count, >=, BTREE_CORE_ELEMS / 2 - 1);
		for (uint32_t i = 0; i <= hdr->bth_count; i++) {
			ret += zfs_btree_verify_counts_helper(tree,
			    node->btc_children[i]);
		}

		return (ret);
	}
}

/*
 * Verify that all nodes satisfy the invariants and that the total number of
 * elements is correct.
 */
static void
zfs_btree_verify_counts(zfs_btree_t *tree)
{
	EQUIV(tree->bt_num_elems == 0, tree->bt_height == -1);
	if (tree->bt_height == -1) {
		return;
	}
	VERIFY3P(zfs_btree_verify_counts_helper(tree, tree->bt_root), ==,
	    tree->bt_num_elems);
}

/*
 * Check that the subtree rooted at this node has a uniform height. Returns
 * the number of nodes under this node, to help verify bt_num_nodes.
 */
static uint64_t
zfs_btree_verify_height_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr,
    int64_t height)
{
	if (!zfs_btree_is_core(hdr)) {
		VERIFY0(height);
		return (1);
	}

	zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
	uint64_t ret = 1;
	for (uint32_t i = 0; i <= hdr->bth_count; i++) {
		ret += zfs_btree_verify_height_helper(tree,
		    node->btc_children[i], height - 1);
	}
	return (ret);
}

/*
 * Check that the tree rooted at this node has a uniform height, and that the
 * bt_height in the tree is correct.
 */
static void
zfs_btree_verify_height(zfs_btree_t *tree)
{
	EQUIV(tree->bt_height == -1, tree->bt_root == NULL);
	if (tree->bt_height == -1) {
		return;
	}

	VERIFY3U(zfs_btree_verify_height_helper(tree, tree->bt_root,
	    tree->bt_height), ==, tree->bt_num_nodes);
}

/*
 * Check that the elements in this node are sorted, and that if this is a core
 * node, the separators are properly between the subtrees they separaate and
 * that the children also satisfy this requirement.
 */
static void
zfs_btree_verify_order_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
	size_t size = tree->bt_elem_size;
	if (!zfs_btree_is_core(hdr)) {
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)hdr;
		for (uint32_t i = 1; i < hdr->bth_count; i++) {
			VERIFY3S(tree->bt_compar(leaf->btl_elems +
			    (hdr->bth_first + i - 1) * size,
			    leaf->btl_elems +
			    (hdr->bth_first + i) * size), ==, -1);
		}
		return;
	}

	zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
	for (uint32_t i = 1; i < hdr->bth_count; i++) {
		VERIFY3S(tree->bt_compar(node->btc_elems + (i - 1) * size,
		    node->btc_elems + i * size), ==, -1);
	}
	for (uint32_t i = 0; i < hdr->bth_count; i++) {
		uint8_t *left_child_last = NULL;
		zfs_btree_hdr_t *left_child_hdr = node->btc_children[i];
		if (zfs_btree_is_core(left_child_hdr)) {
			zfs_btree_core_t *left_child =
			    (zfs_btree_core_t *)left_child_hdr;
			left_child_last = left_child->btc_elems +
			    (left_child_hdr->bth_count - 1) * size;
		} else {
			zfs_btree_leaf_t *left_child =
			    (zfs_btree_leaf_t *)left_child_hdr;
			left_child_last = left_child->btl_elems +
			    (left_child_hdr->bth_first +
			    left_child_hdr->bth_count - 1) * size;
		}
		int comp = tree->bt_compar(node->btc_elems + i * size,
		    left_child_last);
		if (comp <= 0) {
			panic("btree: compar returned %d (expected 1) at "
			    "%px %d: compar(%px,  %px)", comp, node, i,
			    node->btc_elems + i * size, left_child_last);
		}

		uint8_t *right_child_first = NULL;
		zfs_btree_hdr_t *right_child_hdr = node->btc_children[i + 1];
		if (zfs_btree_is_core(right_child_hdr)) {
			zfs_btree_core_t *right_child =
			    (zfs_btree_core_t *)right_child_hdr;
			right_child_first = right_child->btc_elems;
		} else {
			zfs_btree_leaf_t *right_child =
			    (zfs_btree_leaf_t *)right_child_hdr;
			right_child_first = right_child->btl_elems +
			    right_child_hdr->bth_first * size;
		}
		comp = tree->bt_compar(node->btc_elems + i * size,
		    right_child_first);
		if (comp >= 0) {
			panic("btree: compar returned %d (expected -1) at "
			    "%px %d: compar(%px,  %px)", comp, node, i,
			    node->btc_elems + i * size, right_child_first);
		}
	}
	for (uint32_t i = 0; i <= hdr->bth_count; i++)
		zfs_btree_verify_order_helper(tree, node->btc_children[i]);
}

/* Check that all elements in the tree are in sorted order. */
static void
zfs_btree_verify_order(zfs_btree_t *tree)
{
	EQUIV(tree->bt_height == -1, tree->bt_root == NULL);
	if (tree->bt_height == -1) {
		return;
	}

	zfs_btree_verify_order_helper(tree, tree->bt_root);
}

#ifdef ZFS_DEBUG
/* Check that all unused memory is poisoned correctly. */
static void
zfs_btree_verify_poison_helper(zfs_btree_t *tree, zfs_btree_hdr_t *hdr)
{
	size_t size = tree->bt_elem_size;
	if (!zfs_btree_is_core(hdr)) {
		zfs_btree_leaf_t *leaf = (zfs_btree_leaf_t *)hdr;
		for (size_t i = 0; i < hdr->bth_first * size; i++)
			VERIFY3U(leaf->btl_elems[i], ==, 0x0f);
		for (size_t i = (hdr->bth_first + hdr->bth_count) * size;
		    i < BTREE_LEAF_ESIZE; i++)
			VERIFY3U(leaf->btl_elems[i], ==, 0x0f);
	} else {
		zfs_btree_core_t *node = (zfs_btree_core_t *)hdr;
		for (size_t i = hdr->bth_count * size;
		    i < BTREE_CORE_ELEMS * size; i++)
			VERIFY3U(node->btc_elems[i], ==, 0x0f);

		for (uint32_t i = hdr->bth_count + 1; i <= BTREE_CORE_ELEMS;
		    i++) {
			VERIFY3P(node->btc_children[i], ==,
			    (zfs_btree_hdr_t *)BTREE_POISON);
		}

		for (uint32_t i = 0; i <= hdr->bth_count; i++) {
			zfs_btree_verify_poison_helper(tree,
			    node->btc_children[i]);
		}
	}
}
#endif

/* Check that unused memory in the tree is still poisoned. */
static void
zfs_btree_verify_poison(zfs_btree_t *tree)
{
#ifdef ZFS_DEBUG
	if (tree->bt_height == -1)
		return;
	zfs_btree_verify_poison_helper(tree, tree->bt_root);
#endif
}

void
zfs_btree_verify(zfs_btree_t *tree)
{
	if (zfs_btree_verify_intensity == 0)
		return;
	zfs_btree_verify_height(tree);
	if (zfs_btree_verify_intensity == 1)
		return;
	zfs_btree_verify_pointers(tree);
	if (zfs_btree_verify_intensity == 2)
		return;
	zfs_btree_verify_counts(tree);
	if (zfs_btree_verify_intensity == 3)
		return;
	zfs_btree_verify_order(tree);

	if (zfs_btree_verify_intensity == 4)
		return;
	zfs_btree_verify_poison(tree);
}
