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

kmem_cache_t *btree_leaf_cache;

/*
 * Control the extent of the verification that occurs when btree_verify is
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
 * or equal to half the maximum).
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
 * operations are called at every step, resulting in extremely sloww operation
 * (while the asymptotic complexity of the other steps is the same, the
 * importance of the constant factors cannot be denied).
 */
#ifdef ZFS_DEBUG
int btree_verify_intensity = 5;
#else
int btree_verify_intensity = 0;
#endif

#ifdef _ILP32
#define BTREE_POISON 0xabadb10c
#else
#define BTREE_POISON 0xabadb10cdeadbeef
#endif

#ifdef ZFS_DEBUG
static void
btree_poison_node(btree_t *tree, btree_hdr_t *hdr)
{
	size_t size = tree->bt_elem_size;
	if (!hdr->bth_core) {
		btree_leaf_t *leaf = (btree_leaf_t *)hdr;
		(void) memset(leaf->btl_elems + hdr->bth_count * size, 0x0f,
		    BTREE_LEAF_SIZE - sizeof (btree_hdr_t) - hdr->bth_count *
		    size);
	} else {
		btree_core_t *node = (btree_core_t *)hdr;
		for (int i = hdr->bth_count + 1; i <= BTREE_CORE_ELEMS; i++) {
			node->btc_children[i] =
			    (btree_hdr_t *)BTREE_POISON;
		}
		(void) memset(node->btc_elems + hdr->bth_count * size, 0x0f,
		    (BTREE_CORE_ELEMS - hdr->bth_count) * size);
	}
}

static inline void
btree_poison_node_at(btree_t *tree, btree_hdr_t *hdr, uint64_t offset)
{
	size_t size = tree->bt_elem_size;
	ASSERT3U(offset, >=, hdr->bth_count);
	if (!hdr->bth_core) {
		btree_leaf_t *leaf = (btree_leaf_t *)hdr;
		(void) memset(leaf->btl_elems + offset * size, 0x0f, size);
	} else {
		btree_core_t *node = (btree_core_t *)hdr;
		node->btc_children[offset + 1] =
		    (btree_hdr_t *)BTREE_POISON;
		(void) memset(node->btc_elems + offset * size, 0x0f, size);
	}
}
#endif

static inline void
btree_verify_poison_at(btree_t *tree, btree_hdr_t *hdr, uint64_t offset)
{
#ifdef ZFS_DEBUG
	size_t size = tree->bt_elem_size;
	uint8_t eval = 0x0f;
	if (hdr->bth_core) {
		btree_core_t *node = (btree_core_t *)hdr;
		btree_hdr_t *cval = (btree_hdr_t *)BTREE_POISON;
		VERIFY3P(node->btc_children[offset + 1], ==, cval);
		for (int i = 0; i < size; i++)
			VERIFY3U(node->btc_elems[offset * size + i], ==, eval);
	} else  {
		btree_leaf_t *leaf = (btree_leaf_t *)hdr;
		for (int i = 0; i < size; i++)
			VERIFY3U(leaf->btl_elems[offset * size + i], ==, eval);
	}
#endif
}

void
btree_init(void)
{
	btree_leaf_cache = kmem_cache_create("btree_leaf_cache",
	    BTREE_LEAF_SIZE, 0, NULL, NULL, NULL, NULL,
	    NULL, 0);
}

void
btree_fini(void)
{
	kmem_cache_destroy(btree_leaf_cache);
}

void
btree_create(btree_t *tree, int (*compar) (const void *, const void *),
    size_t size)
{
	/*
	 * We need a minimmum of 4 elements so that when we split a node we
	 * always have at least two elements in each node. This simplifies the
	 * logic in btree_bulk_finish, since it means the last leaf will
	 * always have a left sibling to share with (unless it's the root).
	 */
	ASSERT3U(size, <=, (BTREE_LEAF_SIZE - sizeof (btree_hdr_t)) / 4);

	bzero(tree, sizeof (*tree));
	tree->bt_compar = compar;
	tree->bt_elem_size = size;
	tree->bt_height = -1;
	tree->bt_bulk = NULL;
}

/*
 * Find value in the array of elements provided. Uses a simple binary search.
 */
static void *
btree_find_in_buf(btree_t *tree, uint8_t *buf, uint64_t nelems,
    const void *value, btree_index_t *where)
{
	uint64_t max = nelems;
	uint64_t min = 0;
	while (max > min) {
		uint64_t idx = (min + max) / 2;
		uint8_t *cur = buf + idx * tree->bt_elem_size;
		int comp = tree->bt_compar(cur, value);
		if (comp == -1) {
			min = idx + 1;
		} else if (comp == 1) {
			max = idx;
		} else {
			ASSERT0(comp);
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
btree_find(btree_t *tree, const void *value, btree_index_t *where)
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
	btree_index_t idx;
	if (tree->bt_bulk != NULL) {
		btree_leaf_t *last_leaf = tree->bt_bulk;
		int compar = tree->bt_compar(last_leaf->btl_elems +
		    ((last_leaf->btl_hdr.bth_count - 1) * tree->bt_elem_size),
		    value);
		if (compar < 0) {
			/*
			 * If what they're looking for is after the last
			 * element, it's not in the tree.
			 */
			if (where != NULL) {
				where->bti_node = (btree_hdr_t *)last_leaf;
				where->bti_offset =
				    last_leaf->btl_hdr.bth_count;
				where->bti_before = B_TRUE;
			}
			return (NULL);
		} else if (compar == 0) {
			if (where != NULL) {
				where->bti_node = (btree_hdr_t *)last_leaf;
				where->bti_offset =
				    last_leaf->btl_hdr.bth_count - 1;
				where->bti_before = B_FALSE;
			}
			return (last_leaf->btl_elems +
			    ((last_leaf->btl_hdr.bth_count - 1) *
			    tree->bt_elem_size));
		}
		if (tree->bt_compar(last_leaf->btl_elems, value) <= 0) {
			/*
			 * If what they're looking for is after the first
			 * element in the last leaf, it's in the last leaf or
			 * it's not in the tree.
			 */
			void *d = btree_find_in_buf(tree, last_leaf->btl_elems,
			    last_leaf->btl_hdr.bth_count, value, &idx);

			if (where != NULL) {
				idx.bti_node = (btree_hdr_t *)last_leaf;
				*where = idx;
			}
			return (d);
		}
	}

	btree_core_t *node = NULL;
	uint64_t child = 0;
	uint64_t depth = 0;

	/*
	 * Iterate down the tree, finding which child the value should be in
	 * by comparing with the separators.
	 */
	for (node = (btree_core_t *)tree->bt_root; depth < tree->bt_height;
	    node = (btree_core_t *)node->btc_children[child], depth++) {
		ASSERT3P(node, !=, NULL);
		void *d = btree_find_in_buf(tree, node->btc_elems,
		    node->btc_hdr.bth_count, value, &idx);
		EQUIV(d != NULL, !idx.bti_before);
		if (d != NULL) {
			if (where != NULL) {
				idx.bti_node = (btree_hdr_t *)node;
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
	btree_leaf_t *leaf = (depth == 0 ? (btree_leaf_t *)tree->bt_root :
	    (btree_leaf_t *)node);
	void *d = btree_find_in_buf(tree, leaf->btl_elems,
	    leaf->btl_hdr.bth_count, value, &idx);

	if (where != NULL) {
		idx.bti_node = (btree_hdr_t *)leaf;
		*where = idx;
	}

	return (d);
}

/*
 * Find the first element in the subtree rooted at hdr, return its value and
 * put its location in where if non-null.
 */
static void *
btree_first_helper(btree_hdr_t *hdr, btree_index_t *where)
{
	btree_hdr_t *node;

	for (node = hdr; node->bth_core; node =
	    ((btree_core_t *)node)->btc_children[0])
		;

	ASSERT(!node->bth_core);
	btree_leaf_t *leaf = (btree_leaf_t *)node;
	if (where != NULL) {
		where->bti_node = node;
		where->bti_offset = 0;
		where->bti_before = B_FALSE;
	}
	return (&leaf->btl_elems[0]);
}

/*
 * Insert new_node into the parent of old_node directly after old_node, with
 * buf as the dividing element between the two.
 */
static void
btree_insert_into_parent(btree_t *tree, btree_hdr_t *old_node,
    btree_hdr_t *new_node, void *buf)
{
	ASSERT3P(old_node->bth_parent, ==, new_node->bth_parent);
	uint64_t size = tree->bt_elem_size;
	btree_core_t *parent = old_node->bth_parent;
	btree_hdr_t *par_hdr = &parent->btc_hdr;

	/*
	 * If this is the root node we were splitting, we create a new root
	 * and increase the height of the tree.
	 */
	if (parent == NULL) {
		ASSERT3P(old_node, ==, tree->bt_root);
		tree->bt_num_nodes++;
		btree_core_t *new_root = kmem_alloc(sizeof (btree_core_t) +
		    BTREE_CORE_ELEMS * size, KM_SLEEP);
		btree_hdr_t *new_root_hdr = &new_root->btc_hdr;
		new_root_hdr->bth_parent = NULL;
		new_root_hdr->bth_core = B_TRUE;
		new_root_hdr->bth_count = 1;

		old_node->bth_parent = new_node->bth_parent = new_root;
		new_root->btc_children[0] = old_node;
		new_root->btc_children[1] = new_node;
		bcopy(buf, new_root->btc_elems, size);

		tree->bt_height++;
		tree->bt_root = new_root_hdr;
#ifdef ZFS_DEBUG
		btree_poison_node(tree, new_root_hdr);
#endif
		return;
	}

	/*
	 * Since we have the new separator, binary search for where to put
	 * new_node.
	 */
	btree_index_t idx;
	ASSERT(par_hdr->bth_core);
	VERIFY3P(btree_find_in_buf(tree, parent->btc_elems, par_hdr->bth_count,
	    buf, &idx), ==, NULL);
	ASSERT(idx.bti_before);
	uint64_t offset = idx.bti_offset;
	ASSERT3U(offset, <=, par_hdr->bth_count);
	ASSERT3P(parent->btc_children[offset], ==, old_node);

	/*
	 * If the parent isn't full, shift things to accomodate our insertions
	 * and return.
	 */
	if (par_hdr->bth_count != BTREE_CORE_ELEMS) {
		if (btree_verify_intensity >= 5) {
			btree_verify_poison_at(tree, par_hdr,
			    par_hdr->bth_count);
		}
		/* Move the child pointers back one. */
		btree_hdr_t **c_start = parent->btc_children + offset + 1;
		uint64_t count = par_hdr->bth_count - offset;
		bcopy(c_start, c_start + 1, sizeof (*c_start) * count);
		*c_start = new_node;

		/* Move the elements back one. */
		uint8_t *e_start = parent->btc_elems + offset * size;
		bcopy(e_start, e_start + size, count * size);
		bcopy(buf, e_start, size);

		par_hdr->bth_count++;
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
	 */
	uint64_t move_count = MAX(BTREE_CORE_ELEMS / (tree->bt_bulk == NULL ?
	    2 : 4), 2);
	uint64_t keep_count = BTREE_CORE_ELEMS - move_count;
	ASSERT3U(BTREE_CORE_ELEMS - move_count, >=, 2);
	tree->bt_num_nodes++;
	btree_core_t *new_parent = kmem_alloc(sizeof (btree_core_t) +
	    BTREE_CORE_ELEMS * size, KM_SLEEP);
	btree_hdr_t *new_par_hdr = &new_parent->btc_hdr;
	new_par_hdr->bth_parent = par_hdr->bth_parent;
	new_par_hdr->bth_core = B_TRUE;
	new_par_hdr->bth_count = move_count;
#ifdef ZFS_DEBUG
	btree_poison_node(tree, new_par_hdr);
#endif
	par_hdr->bth_count = keep_count;

	/*
	 * The three cases to consider are that the element in buf should be
	 * in the existing node (with lower values), the new node (with higher
	 * values), or that it should separate the two nodes.
	 */
	if (offset < keep_count) {
		/*
		 * Copy the back part of the elements and children to the new
		 * leaf.
		 */
		uint64_t e_count = move_count;
		uint8_t *e_start = parent->btc_elems + keep_count * size;
		bcopy(e_start, new_parent->btc_elems, e_count * size);

		uint64_t c_count = move_count + 1;
		btree_hdr_t **c_start = parent->btc_children + keep_count;
		bcopy(c_start, new_parent->btc_children, c_count *
		    sizeof (*c_start));

		/* Store the new separator in a buffer. */
		uint8_t *tmp_buf = kmem_alloc(size, KM_SLEEP);
		bcopy(parent->btc_elems + (keep_count - 1) * size, tmp_buf,
		    size);

		/*
		 * Shift the remaining elements and children in the front half
		 * to handle the new value.
		 */
		e_start = parent->btc_elems + offset * size;
		e_count = keep_count - 1 - offset;
		bcopy(e_start, e_start + size, e_count * size);
		bcopy(buf, e_start, size);

		c_start = parent->btc_children + (offset + 1);
		c_count = keep_count - 1 - offset;
		bcopy(c_start, c_start + 1, c_count * sizeof (*c_start));
		*c_start = new_node;
		ASSERT3P(*(c_start - 1), ==, old_node);

		/*
		 * Move the new separator to the existing buffer.
		 */
		bcopy(tmp_buf, buf, size);
		kmem_free(tmp_buf, size);
	} else if (offset > keep_count) {
		/* Store the new separator in a buffer. */
		uint8_t *tmp_buf = kmem_alloc(size, KM_SLEEP);
		uint8_t *e_start = parent->btc_elems + keep_count * size;
		bcopy(e_start, tmp_buf, size);

		/*
		 * Of the elements and children in the back half, move those
		 * before offset to the new leaf.
		 */
		e_start += size;
		uint8_t *e_out = new_parent->btc_elems;
		uint64_t e_count = offset - keep_count - 1;
		bcopy(e_start, e_out, e_count * size);

		btree_hdr_t **c_start = parent->btc_children + (keep_count + 1);
		uint64_t c_count = offset - keep_count;
		btree_hdr_t **c_out = new_parent->btc_children;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		/* Add the new value to the new leaf. */
		e_out += e_count * size;
		bcopy(buf, e_out, size);

		c_out += c_count;
		*c_out = new_node;
		ASSERT3P(*(c_out - 1), ==, old_node);

		/*
		 * Move the new separator to the existing buffer.
		 */
		bcopy(tmp_buf, buf, size);
		kmem_free(tmp_buf, size);

		/* Move the rest of the back half to the new leaf. */
		e_out += size;
		e_start += e_count * size;
		e_count = BTREE_CORE_ELEMS - offset;
		bcopy(e_start, e_out, e_count * size);

		c_out++;
		c_start += c_count;
		c_count = BTREE_CORE_ELEMS - offset;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));
	} else {
		/*
		 * The new value is the new separator, no change.
		 *
		 * Copy the back part of the elements and children to the new
		 * leaf.
		 */
		uint64_t e_count = move_count;
		uint8_t *e_start = parent->btc_elems + keep_count * size;
		bcopy(e_start, new_parent->btc_elems, e_count * size);

		uint64_t c_count = move_count;
		btree_hdr_t **c_start = parent->btc_children + keep_count + 1;
		bcopy(c_start, new_parent->btc_children + 1, c_count *
		    sizeof (*c_start));
		new_parent->btc_children[0] = new_node;
	}
#ifdef ZFS_DEBUG
	btree_poison_node(tree, par_hdr);
#endif

	for (int i = 0; i <= new_parent->btc_hdr.bth_count; i++)
		new_parent->btc_children[i]->bth_parent = new_parent;

	for (int i = 0; i <= parent->btc_hdr.bth_count; i++)
		ASSERT3P(parent->btc_children[i]->bth_parent, ==, parent);

	/*
	 * Now that the node is split, we need to insert the new node into its
	 * parent. This may cause further splitting.
	 */
	btree_insert_into_parent(tree, &parent->btc_hdr, &new_parent->btc_hdr,
	    buf);

}

/* Helper function for inserting a new value into leaf at the given index. */
static void
btree_insert_into_leaf(btree_t *tree, btree_leaf_t *leaf, const void *value,
    uint64_t idx)
{
	uint64_t size = tree->bt_elem_size;
	uint8_t *start = leaf->btl_elems + (idx * size);
	uint64_t count = leaf->btl_hdr.bth_count - idx;
	uint64_t capacity = P2ALIGN((BTREE_LEAF_SIZE - sizeof (btree_hdr_t)) /
	    size, 2);

	/*
	 * If the leaf isn't full, shift the elements after idx and insert
	 * value.
	 */
	if (leaf->btl_hdr.bth_count != capacity) {
		if (btree_verify_intensity >= 5) {
			btree_verify_poison_at(tree, &leaf->btl_hdr,
			    leaf->btl_hdr.bth_count);
		}
		leaf->btl_hdr.bth_count++;
		bcopy(start, start + size, count * size);
		bcopy(value, start, size);
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
	uint64_t move_count = MAX(capacity / (tree->bt_bulk == NULL ? 2 : 4),
	    2);
	uint64_t keep_count = capacity - move_count;
	ASSERT3U(capacity - move_count, >=, 2);
	tree->bt_num_nodes++;
	btree_leaf_t *new_leaf = kmem_cache_alloc(btree_leaf_cache, KM_SLEEP);
	btree_hdr_t *new_hdr = &new_leaf->btl_hdr;
	new_hdr->bth_parent = leaf->btl_hdr.bth_parent;
	new_hdr->bth_core = B_FALSE;
	new_hdr->bth_count = move_count;
#ifdef ZFS_DEBUG
	btree_poison_node(tree, new_hdr);
#endif
	leaf->btl_hdr.bth_count = keep_count;

	if (tree->bt_bulk != NULL && leaf == tree->bt_bulk)
		tree->bt_bulk = new_leaf;

	/* We store the new separator in a buffer we control for simplicity. */
	uint8_t *buf = kmem_alloc(size, KM_SLEEP);

	/*
	 * The three cases to consider are that value should be in the new
	 * first node, the new second node, or that it should separate the two
	 * nodes.
	 */
	if (idx < keep_count) {
		/* Copy the back part to the new leaf. */
		start = leaf->btl_elems + keep_count * size;
		count = move_count;
		bcopy(start, new_leaf->btl_elems, count * size);

		/* Store the new separator in a buffer. */
		bcopy(leaf->btl_elems + (keep_count - 1) * size, buf,
		    size);

		/*
		 * Shift the remaining elements in the front part to handle
		 * the new value.
		 */
		start = leaf->btl_elems + idx * size;
		count = (keep_count) - 1 - idx;
		bcopy(start, start + size, count * size);
		bcopy(value, start, size);
	} else if (idx > keep_count) {
		/* Store the new separator in a buffer. */
		start = leaf->btl_elems + (keep_count) * size;
		bcopy(start, buf, size);

		/* Move the back part before idx to the new leaf. */
		start += size;
		uint8_t *out = new_leaf->btl_elems;
		count = idx - keep_count - 1;
		bcopy(start, out, count * size);

		/* Add the new value to the new leaf. */
		out += count * size;
		bcopy(value, out, size);

		/* Move the rest of the back part to the new leaf. */
		out += size;
		start += count * size;
		count = capacity - idx;
		bcopy(start, out, count * size);
	} else {
		/* The new value is the new separator. */
		bcopy(value, buf, size);

		/* Copy the back part to the new leaf. */
		start = leaf->btl_elems + keep_count * size;
		count = move_count;
		bcopy(start, new_leaf->btl_elems, count * size);
	}

#ifdef ZFS_DEBUG
	btree_poison_node(tree, &leaf->btl_hdr);
#endif
	/*
	 * Now that the node is split, we need to insert the new node into its
	 * parent. This may cause further splitting, bur only of core nodes.
	 */
	btree_insert_into_parent(tree, &leaf->btl_hdr, &new_leaf->btl_hdr, buf);
	kmem_free(buf, size);
}

static uint64_t
btree_find_parent_idx(btree_t *tree, btree_hdr_t *hdr)
{
	void *buf;
	if (hdr->bth_core) {
		buf = ((btree_core_t *)hdr)->btc_elems;
	} else {
		buf = ((btree_leaf_t *)hdr)->btl_elems;
	}
	btree_index_t idx;
	btree_core_t *parent = hdr->bth_parent;
	VERIFY3P(btree_find_in_buf(tree, parent->btc_elems,
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
 * particular level. To correct the invariant, we steal values from their left
 * neighbor until they are half full. They must have a left neighbor at their
 * level because the last node at a level is not the first node unless it's
 * the root.
 */
static void
btree_bulk_finish(btree_t *tree)
{
	ASSERT3P(tree->bt_bulk, !=, NULL);
	ASSERT3P(tree->bt_root, !=, NULL);
	btree_leaf_t *leaf = tree->bt_bulk;
	btree_hdr_t *hdr = &leaf->btl_hdr;
	btree_core_t *parent = hdr->bth_parent;
	uint64_t size = tree->bt_elem_size;
	uint64_t capacity = P2ALIGN((BTREE_LEAF_SIZE - sizeof (btree_hdr_t)) /
	    size, 2);

	/*
	 * The invariant doesn't apply to the root node, if that's the only
	 * node in the tree we're done.
	 */
	if (parent == NULL) {
		tree->bt_bulk = NULL;
		return;
	}

	/* First, steal elements to rebalance the leaf node. */
	if (hdr->bth_count < capacity / 2) {
		/*
		 * First, find the left neighbor. The simplest way to do this
		 * is to call btree_prev twice; the first time finds some
		 * ancestor of this node, and the second time finds the left
		 * neighbor. The ancestor found is the lowest common ancestor
		 * of leaf and the neighbor.
		 */
		btree_index_t idx = {
			.bti_node = hdr,
			.bti_offset = 0
		};
		VERIFY3P(btree_prev(tree, &idx, &idx), !=, NULL);
		ASSERT(idx.bti_node->bth_core);
		btree_core_t *common = (btree_core_t *)idx.bti_node;
		uint64_t common_idx = idx.bti_offset;

		VERIFY3P(btree_prev(tree, &idx, &idx), !=, NULL);
		ASSERT(!idx.bti_node->bth_core);
		btree_leaf_t *l_neighbor = (btree_leaf_t *)idx.bti_node;
		uint64_t move_count = (capacity / 2) - hdr->bth_count;
		ASSERT3U(l_neighbor->btl_hdr.bth_count - move_count, >=,
		    capacity / 2);

		if (btree_verify_intensity >= 5) {
			for (int i = 0; i < move_count; i++) {
				btree_verify_poison_at(tree, hdr,
				    leaf->btl_hdr.bth_count + i);
			}
		}

		/* First, shift elements in leaf back. */
		uint8_t *start = leaf->btl_elems;
		uint8_t *out = leaf->btl_elems + (move_count * size);
		uint64_t count = hdr->bth_count;
		bcopy(start, out, count * size);

		/* Next, move the separator from the common ancestor to leaf. */
		uint8_t *separator = common->btc_elems + (common_idx * size);
		out -= size;
		bcopy(separator, out, size);
		move_count--;

		/*
		 * Now we move elements from the tail of the left neighbor to
		 * fill the remaining spots in leaf.
		 */
		start = l_neighbor->btl_elems + (l_neighbor->btl_hdr.bth_count -
		    move_count) * size;
		out = leaf->btl_elems;
		bcopy(start, out, move_count * size);

		/*
		 * Finally, move the new last element in the left neighbor to
		 * the separator.
		 */
		start -= size;
		bcopy(start, separator, size);

		/* Adjust the node's counts, and we're done. */
		l_neighbor->btl_hdr.bth_count -= move_count + 1;
		hdr->bth_count += move_count + 1;

		ASSERT3U(l_neighbor->btl_hdr.bth_count, >=, capacity / 2);
		ASSERT3U(hdr->bth_count, >=, capacity / 2);
#ifdef ZFS_DEBUG
		btree_poison_node(tree, &l_neighbor->btl_hdr);
#endif
	}

	/*
	 * Now we have to rebalance any ancestors of leaf that may also
	 * violate the invariant.
	 */
	capacity = BTREE_CORE_ELEMS;
	while (parent->btc_hdr.bth_parent != NULL) {
		btree_core_t *cur = parent;
		btree_hdr_t *hdr = &cur->btc_hdr;
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
		 * left sibling.
		 */
		uint64_t parent_idx = btree_find_parent_idx(tree, hdr);
		ASSERT3U(parent_idx, >, 0);
		btree_core_t *l_neighbor = (btree_core_t *)parent->btc_children[
		    parent_idx - 1];
		uint64_t move_count = (capacity / 2) - hdr->bth_count;
		ASSERT3U(l_neighbor->btc_hdr.bth_count - move_count, >=,
		    capacity / 2);

		if (btree_verify_intensity >= 5) {
			for (int i = 0; i < move_count; i++) {
				btree_verify_poison_at(tree, hdr,
				    leaf->btl_hdr.bth_count + i);
			}
		}
		/* First, shift things in the right node back. */
		uint8_t *e_start = cur->btc_elems;
		uint8_t *e_out = cur->btc_elems + (move_count * size);
		uint64_t e_count = hdr->bth_count;
		bcopy(e_start, e_out, e_count * size);

		btree_hdr_t **c_start = cur->btc_children;
		btree_hdr_t **c_out = cur->btc_children + move_count;
		uint64_t c_count = hdr->bth_count + 1;
		bcopy(c_start, c_out, c_count * sizeof (btree_hdr_t *));

		/* Next, move the separator to the right node. */
		uint8_t *separator = parent->btc_elems + ((parent_idx - 1) *
		    size);
		e_out -= size;
		bcopy(separator, e_out, size);

		/*
		 * Now, move elements and children from the left node to the
		 * right.  We move one more child than elements.
		 */
		move_count--;
		e_start = l_neighbor->btc_elems +
		    (l_neighbor->btc_hdr.bth_count - move_count) * size;
		e_out = cur->btc_elems;
		e_count = move_count;
		bcopy(e_start, e_out, e_count * size);

		c_start = l_neighbor->btc_children +
		    (l_neighbor->btc_hdr.bth_count - move_count);
		c_out = cur->btc_children;
		c_count = move_count + 1;
		bcopy(c_start, c_out, c_count * sizeof (btree_hdr_t *));

		/*
		 * Finally, move the last element in the left node to the
		 * separator's position.
		 */
		e_start -= size;
		bcopy(e_start, separator, size);

		l_neighbor->btc_hdr.bth_count -= move_count + 1;
		hdr->bth_count += move_count + 1;

		ASSERT3U(l_neighbor->btc_hdr.bth_count, >=, capacity / 2);
		ASSERT3U(hdr->bth_count, >=, capacity / 2);

#ifdef ZFS_DEBUG
		btree_poison_node(tree, &l_neighbor->btc_hdr);
#endif
		for (int i = 0; i <= hdr->bth_count; i++)
			cur->btc_children[i]->bth_parent = cur;
	}

	tree->bt_bulk = NULL;
}

/*
 * Insert value into tree at the location specified by where.
 */
void
btree_insert(btree_t *tree, const void *value, const btree_index_t *where)
{
	btree_index_t idx = {0};

	/* If we're not inserting in the last leaf, end bulk insert mode. */
	if (tree->bt_bulk != NULL) {
		if (where->bti_node != &tree->bt_bulk->btl_hdr) {
			btree_bulk_finish(tree);
			VERIFY3P(btree_find(tree, value, &idx), ==, NULL);
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
		btree_leaf_t *leaf = kmem_cache_alloc(btree_leaf_cache,
		    KM_SLEEP);
		tree->bt_root = &leaf->btl_hdr;
		tree->bt_height++;

		btree_hdr_t *hdr = &leaf->btl_hdr;
		hdr->bth_parent = NULL;
		hdr->bth_core = B_FALSE;
		hdr->bth_count = 0;
#ifdef ZFS_DEBUG
		btree_poison_node(tree, hdr);
#endif
		btree_insert_into_leaf(tree, leaf, value, 0);
		tree->bt_bulk = leaf;
	} else if (!where->bti_node->bth_core) {
		/*
		 * If we're inserting into a leaf, go directly to the helper
		 * function.
		 */
		btree_insert_into_leaf(tree, (btree_leaf_t *)where->bti_node,
		    value, where->bti_offset);
	} else {
		/*
		 * If we're inserting into a core node, we can't just shift
		 * the existing element in that slot in the same node without
		 * breaking our ordering invariants. Instead we place the new
		 * value in the node at that spot and then insert the old
		 * separator into the first slot in the subtree to the right.
		 */
		ASSERT(where->bti_node->bth_core);
		btree_core_t *node = (btree_core_t *)where->bti_node;

		/*
		 * We can ignore bti_before, because either way the value
		 * should end up in bti_offset.
		 */
		uint64_t off = where->bti_offset;
		btree_hdr_t *subtree = node->btc_children[off + 1];
		size_t size = tree->bt_elem_size;
		uint8_t *buf = kmem_alloc(size, KM_SLEEP);
		bcopy(node->btc_elems + off * size, buf, size);
		bcopy(value, node->btc_elems + off * size, size);

		/*
		 * Find the first slot in the subtree to the right, insert
		 * there.
		 */
		btree_index_t new_idx;
		VERIFY3P(btree_first_helper(subtree, &new_idx), !=, NULL);
		ASSERT0(new_idx.bti_offset);
		ASSERT(!new_idx.bti_node->bth_core);
		btree_insert_into_leaf(tree, (btree_leaf_t *)new_idx.bti_node,
		    buf, 0);
		kmem_free(buf, size);
	}
	btree_verify(tree);
}

/*
 * Return the first element in the tree, and put its location in where if
 * non-null.
 */
void *
btree_first(btree_t *tree, btree_index_t *where)
{
	if (tree->bt_height == -1) {
		ASSERT0(tree->bt_num_elems);
		return (NULL);
	}
	return (btree_first_helper(tree->bt_root, where));

}

/*
 * Find the last element in the subtree rooted at hdr, return its value and
 * put its location in where if non-null.
 */
static void *
btree_last_helper(btree_t *btree, btree_hdr_t *hdr, btree_index_t *where)
{
	btree_hdr_t *node;

	for (node = hdr; node->bth_core; node =
	    ((btree_core_t *)node)->btc_children[node->bth_count])
		;

	btree_leaf_t *leaf = (btree_leaf_t *)node;
	if (where != NULL) {
		where->bti_node = node;
		where->bti_offset = node->bth_count - 1;
		where->bti_before = B_FALSE;
	}
	return (leaf->btl_elems + (node->bth_count - 1) * btree->bt_elem_size);
}

/*
 * Return the last element in the tree, and put its location in where if
 * non-null.
 */
void *
btree_last(btree_t *tree, btree_index_t *where)
{
	if (tree->bt_height == -1) {
		ASSERT0(tree->bt_num_elems);
		return (NULL);
	}
	return (btree_last_helper(tree, tree->bt_root, where));
}

/*
 * This function contains the logic to find the next node in the tree. A
 * helper function is used because there are multiple internal consumemrs of
 * this logic. The done_func is used by btree_destroy_nodes to clean up each
 * node after we've finished with it.
 */
static void *
btree_next_helper(btree_t *tree, const btree_index_t *idx,
    btree_index_t *out_idx,
    void (*done_func)(btree_t *, btree_hdr_t *))
{
	if (idx->bti_node == NULL) {
		ASSERT3S(tree->bt_height, ==, -1);
		return (NULL);
	}

	uint64_t offset = idx->bti_offset;
	if (!idx->bti_node->bth_core) {
		/*
		 * When finding the next element of an element in a leaf,
		 * there are two cases. If the element isn't the last one in
		 * the leaf, in which case we just return the next element in
		 * the leaf. Otherwise, we need to traverse up our parents
		 * until we find one where our ancestor isn't the last child
		 * of its parent. Once we do, the next element is the
		 * separator after our ancestor in its parent.
		 */
		btree_leaf_t *leaf = (btree_leaf_t *)idx->bti_node;
		uint64_t new_off = offset + (idx->bti_before ? 0 : 1);
		if (leaf->btl_hdr.bth_count > new_off) {
			out_idx->bti_node = &leaf->btl_hdr;
			out_idx->bti_offset = new_off;
			out_idx->bti_before = B_FALSE;
			return (leaf->btl_elems + new_off * tree->bt_elem_size);
		}

		btree_hdr_t *prev = &leaf->btl_hdr;
		for (btree_core_t *node = leaf->btl_hdr.bth_parent;
		    node != NULL; node = node->btc_hdr.bth_parent) {
			btree_hdr_t *hdr = &node->btc_hdr;
			ASSERT(hdr->bth_core);
			uint64_t i = btree_find_parent_idx(tree, prev);
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
	ASSERT(idx->bti_node->bth_core);
	btree_core_t *node = (btree_core_t *)idx->bti_node;
	if (idx->bti_before) {
		out_idx->bti_before = B_FALSE;
		return (node->btc_elems + offset * tree->bt_elem_size);
	}

	/*
	 * The next element from one in a core node is the first element in
	 * the subtree just to the right of the separator.
	 */
	btree_hdr_t *child = node->btc_children[offset + 1];
	return (btree_first_helper(child, out_idx));
}

/*
 * Return the next valued node in the tree.  The same address can be safely
 * passed for idx and out_idx.
 */
void *
btree_next(btree_t *tree, const btree_index_t *idx, btree_index_t *out_idx)
{
	return (btree_next_helper(tree, idx, out_idx, NULL));
}

/*
 * Return the previous valued node in the tree.  The same value can be safely
 * passed for idx and out_idx.
 */
void *
btree_prev(btree_t *tree, const btree_index_t *idx, btree_index_t *out_idx)
{
	if (idx->bti_node == NULL) {
		ASSERT3S(tree->bt_height, ==, -1);
		return (NULL);
	}

	uint64_t offset = idx->bti_offset;
	if (!idx->bti_node->bth_core) {
		/*
		 * When finding the previous element of an element in a leaf,
		 * there are two cases. If the element isn't the first one in
		 * the leaf, in which case we just return the next element in
		 * the leaf. Otherwise, we need to traverse up our parents
		 * until we find one where our previous ancestor isn't the
		 * first child. Once we do, the next element is the separator
		 * before our previous ancestor.
		 */
		btree_leaf_t *leaf = (btree_leaf_t *)idx->bti_node;
		if (offset != 0) {
			out_idx->bti_node = &leaf->btl_hdr;
			out_idx->bti_offset = offset - 1;
			out_idx->bti_before = B_FALSE;
			return (leaf->btl_elems + (offset - 1) *
			    tree->bt_elem_size);
		}
		btree_hdr_t *prev = &leaf->btl_hdr;
		for (btree_core_t *node = leaf->btl_hdr.bth_parent;
		    node != NULL; node = node->btc_hdr.bth_parent) {
			btree_hdr_t *hdr = &node->btc_hdr;
			ASSERT(hdr->bth_core);
			uint64_t i = btree_find_parent_idx(tree, prev);
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
	 * the subtree just to the lefet of the separator.
	 */
	ASSERT(idx->bti_node->bth_core);
	btree_core_t *node = (btree_core_t *)idx->bti_node;
	btree_hdr_t *child = node->btc_children[offset];
	return (btree_last_helper(tree, child, out_idx));
}

/*
 * Get the value at the provided index in the tree.
 *
 * Note that the value returned from this function can be mutataed, but only
 * if it will not change the ordering of the element with respect to any other
 * elements that could be in the tree.
 */
void *
btree_get(btree_t *tree, btree_index_t *idx)
{
	ASSERT(!idx->bti_before);
	if (!idx->bti_node->bth_core) {
		btree_leaf_t *leaf = (btree_leaf_t *)idx->bti_node;
		return (leaf->btl_elems + idx->bti_offset * tree->bt_elem_size);
	}
	ASSERT(idx->bti_node->bth_core);
	btree_core_t *node = (btree_core_t *)idx->bti_node;
	return (node->btc_elems + idx->bti_offset * tree->bt_elem_size);
}

/* Add the given value to the tree. Must not already be in the tree. */
void
btree_add(btree_t *tree, const void *node)
{
	btree_index_t where = {0};
	VERIFY3P(btree_find(tree, node, &where), ==, NULL);
	btree_insert(tree, node, &where);
}

/* Helper function to free a tree node. */
static void
btree_node_destroy(btree_t *tree, btree_hdr_t *node)
{
	tree->bt_num_nodes--;
	if (!node->bth_core) {
		kmem_cache_free(btree_leaf_cache, node);
	} else {
		kmem_free(node, sizeof (btree_core_t) +
		    BTREE_CORE_ELEMS * tree->bt_elem_size);
	}
}

/*
 * Remove the rm_hdr and the separator to its left from the parent node. The
 * buffer that rm_hdr was stored in may already be freed, so its contents
 * cannot be accessed.
 */
static void
btree_remove_from_node(btree_t *tree, btree_core_t *node, btree_hdr_t *rm_hdr)
{
	size_t size = tree->bt_elem_size;
	uint64_t min_count = BTREE_CORE_ELEMS / 2;
	btree_hdr_t *hdr = &node->btc_hdr;
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
		btree_node_destroy(tree, hdr);
		tree->bt_height--;
		return;
	}

	uint64_t idx;
	for (idx = 0; idx <= hdr->bth_count; idx++) {
		if (node->btc_children[idx] == rm_hdr)
			break;
	}
	ASSERT3U(idx, <=, hdr->bth_count);
	hdr->bth_count--;

	/*
	 * If the node is the root or it has more than the minimum number of
	 * children, just remove the child and separator, and return.
	 */
	if (hdr->bth_parent == NULL ||
	    hdr->bth_count >= min_count) {
		/*
		 * Shift the element and children to the right of rm_hdr to
		 * the left by one spot.
		 */
		uint8_t *e_start = node->btc_elems + idx * size;
		uint8_t *e_out = e_start - size;
		uint64_t e_count = hdr->bth_count - idx + 1;
		bcopy(e_start, e_out, e_count * size);

		btree_hdr_t **c_start = node->btc_children + idx + 1;
		btree_hdr_t **c_out = c_start - 1;
		uint64_t c_count = hdr->bth_count - idx + 1;
		bcopy(c_start, c_out, c_count * sizeof (*c_start));
#ifdef ZFS_DEBUG
		btree_poison_node_at(tree, hdr, hdr->bth_count);
#endif
		return;
	}

	ASSERT3U(hdr->bth_count, ==, min_count - 1);

	/*
	 * Now we try to steal a node from a neighbor. We check left, then
	 * right. If the neighbor exists and has more than the minimum number
	 * of elements, we move the separator betweeen us and them to our
	 * node, move their closest element (last for left, first for right)
	 * to the separator, and move their closest child to our node. Along
	 * the way we need to collapse the gap made by idx, and (for our right
	 * neighbor) the gap made by removing their first element and child.
	 *
	 * Note: this logic currently doesn't support stealing from a neighbor
	 * that isn't a sibling. This isn't critical functionality, but may be
	 * worth implementing in the future for completeness' sake.
	 */
	btree_core_t *parent = hdr->bth_parent;
	uint64_t parent_idx = btree_find_parent_idx(tree, hdr);

	btree_hdr_t *l_hdr = (parent_idx == 0 ? NULL :
	    parent->btc_children[parent_idx - 1]);
	if (l_hdr != NULL && l_hdr->bth_count > min_count) {
		/* We can steal a node from the left neighbor. */
		ASSERT(l_hdr->bth_core);
		btree_core_t *neighbor = (btree_core_t *)l_hdr;

		/*
		 * Start by shifting the elements and children in the current
		 * node to the right by one spot.
		 */
		uint8_t *e_start = node->btc_elems;
		uint8_t *e_out = node->btc_elems + size;
		uint64_t e_count = idx - 1;
		bcopy(e_start, e_out, e_count * size);

		btree_hdr_t **c_start = node->btc_children;
		btree_hdr_t **c_out = c_start + 1;
		uint64_t c_count = idx;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		/*
		 * Move the separator between node and neighbor to the first
		 * element slot in the current node.
		 */
		uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
		    size;
		bcopy(separator, node->btc_elems, size);

		/* Move the last child of neighbor to our first child slot. */
		btree_hdr_t **steal_child = neighbor->btc_children +
		    l_hdr->bth_count;
		bcopy(steal_child, node->btc_children, sizeof (*steal_child));
		node->btc_children[0]->bth_parent = node;

		/* Move the last element of neighbor to the separator spot. */
		uint8_t *steal_elem = neighbor->btc_elems +
		    (l_hdr->bth_count - 1) * size;
		bcopy(steal_elem, separator, size);
		l_hdr->bth_count--;
		hdr->bth_count++;
#ifdef ZFS_DEBUG
		btree_poison_node_at(tree, l_hdr, l_hdr->bth_count);
#endif
		return;
	}

	btree_hdr_t *r_hdr = (parent_idx == parent->btc_hdr.bth_count ?
	    NULL : parent->btc_children[parent_idx + 1]);
	if (r_hdr != NULL && r_hdr->bth_count > min_count) {
		/* We can steal a node from the right neighbor. */
		ASSERT(r_hdr->bth_core);
		btree_core_t *neighbor = (btree_core_t *)r_hdr;

		/*
		 * Shift elements in node left by one spot to overwrite rm_hdr
		 * and the separator before it.
		 */
		uint8_t *e_start = node->btc_elems + idx * size;
		uint8_t *e_out = e_start - size;
		uint64_t e_count = hdr->bth_count - idx + 1;
		bcopy(e_start, e_out, e_count * size);

		btree_hdr_t **c_start = node->btc_children + idx + 1;
		btree_hdr_t **c_out = c_start - 1;
		uint64_t c_count = hdr->bth_count - idx + 1;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		/*
		 * Move the separator between node and neighbor to the last
		 * element spot in node.
		 */
		uint8_t *separator = parent->btc_elems + parent_idx * size;
		bcopy(separator, node->btc_elems + hdr->bth_count * size, size);

		/*
		 * Move the first child of neighbor to the last child spot in
		 * node.
		 */
		btree_hdr_t **steal_child = neighbor->btc_children;
		bcopy(steal_child, node->btc_children + (hdr->bth_count + 1),
		    sizeof (*steal_child));
		node->btc_children[hdr->bth_count + 1]->bth_parent = node;

		/* Move the first element of neighbor to the separator spot. */
		uint8_t *steal_elem = neighbor->btc_elems;
		bcopy(steal_elem, separator, size);
		r_hdr->bth_count--;
		hdr->bth_count++;

		/*
		 * Shift the elements and children of neighbor to cover the
		 * stolen elements.
		 */
		bcopy(neighbor->btc_elems + size, neighbor->btc_elems,
		    r_hdr->bth_count * size);
		bcopy(neighbor->btc_children + 1, neighbor->btc_children,
		    (r_hdr->bth_count + 1) * sizeof (steal_child));
#ifdef ZFS_DEBUG
		btree_poison_node_at(tree, r_hdr, r_hdr->bth_count);
#endif
		return;
	}

	/*
	 * In this case, neither of our neighbors can spare an element, so we
	 * need to merge with one of them. We prefer the left one,
	 * arabitrarily. Move the separator into the leftmost merging node
	 * (which may be us or the left neighbor), and then move the right
	 * merging node's elements (skipping or overwriting idx, which we're
	 * deleting). Once that's done, go into the parent and delete the
	 * right merging node and the separator. This may cause further
	 * merging.
	 */
	btree_hdr_t *new_rm_hdr;

	if (l_hdr != NULL) {
		ASSERT(l_hdr->bth_core);
		btree_core_t *left = (btree_core_t *)l_hdr;

		if (btree_verify_intensity >= 5) {
			for (int i = 0; i < hdr->bth_count + 1; i++) {
				btree_verify_poison_at(tree, l_hdr,
				    l_hdr->bth_count + i);
			}
		}
		/* Move the separator into the left node. */
		uint8_t *e_out = left->btc_elems + l_hdr->bth_count * size;
		uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
		    size;
		bcopy(separator, e_out, size);

		/* Move all our elements into the left node. */
		e_out += size;
		uint8_t *e_start = node->btc_elems;
		uint64_t e_count = idx - 1;
		bcopy(e_start, e_out, e_count * size);

		e_out += e_count * size;
		e_start += (e_count + 1) * size;
		e_count = hdr->bth_count - idx + 1;
		bcopy(e_start, e_out, e_count * size);

		/* Move all our children into the left node. */
		btree_hdr_t **c_start = node->btc_children;
		btree_hdr_t **c_out = left->btc_children +
		    l_hdr->bth_count + 1;
		uint64_t c_count = idx;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		c_out += c_count;
		c_start += c_count + 1;
		c_count = hdr->bth_count - idx + 1;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		/* Reparent all our children to point to the left node. */
		btree_hdr_t **new_start = left->btc_children +
		    l_hdr->bth_count + 1;
		for (int i = 0; i < hdr->bth_count + 1; i++)
			new_start[i]->bth_parent = left;

		/* Update bookkeeping */
		l_hdr->bth_count += hdr->bth_count + 1;
		for (int i = 0; i <= l_hdr->bth_count; i++)
			ASSERT3P(left->btc_children[i]->bth_parent, ==, left);
		ASSERT3U(l_hdr->bth_count, ==, BTREE_CORE_ELEMS);
		new_rm_hdr = hdr;
	} else {
		ASSERT3P(r_hdr, !=, NULL);
		ASSERT(r_hdr->bth_core);
		btree_core_t *right = (btree_core_t *)r_hdr;

		if (btree_verify_intensity >= 5) {
			for (int i = 0; i < r_hdr->bth_count; i++) {
				btree_verify_poison_at(tree, hdr,
				    hdr->bth_count + i + 1);
			}
		}
		/*
		 * Overwrite rm_hdr and its separator by moving node's
		 * elements and children forward.
		 */
		uint8_t *e_start = node->btc_elems + idx * size;
		uint8_t *e_out = e_start - size;
		uint64_t e_count = hdr->bth_count - idx + 1;
		bcopy(e_start, e_out, e_count * size);

		btree_hdr_t **c_start = node->btc_children + idx + 1;
		btree_hdr_t **c_out = c_start - 1;
		uint64_t c_count = hdr->bth_count - idx + 1;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		/*
		 * Move the separator to the first open spot in node's
		 * elements.
		 */
		e_out += e_count * size;
		uint8_t *separator = parent->btc_elems + parent_idx * size;
		bcopy(separator, e_out, size);

		/* Move the right node's elements and children to node. */
		e_out += size;
		e_start = right->btc_elems;
		e_count = r_hdr->bth_count;
		bcopy(e_start, e_out, e_count * size);

		c_out += c_count;
		c_start = right->btc_children;
		c_count = r_hdr->bth_count + 1;
		bcopy(c_start, c_out, c_count * sizeof (*c_out));

		/* Reparent the right node's children to point to node. */
		for (int i = 0; i < c_count; i++)
			c_out[i]->bth_parent = node;

		/* Update bookkeeping. */
		hdr->bth_count += r_hdr->bth_count + 1;
		for (int i = 0; i <= hdr->bth_count; i++)
			ASSERT3P(node->btc_children[i]->bth_parent, ==, node);

		ASSERT3U(hdr->bth_count, ==, BTREE_CORE_ELEMS);
		new_rm_hdr = r_hdr;
	}

	new_rm_hdr->bth_count = 0;
	btree_node_destroy(tree, new_rm_hdr);
	btree_remove_from_node(tree, parent, new_rm_hdr);
}

/* Remove the element at the specific location. */
void
btree_remove_from(btree_t *tree, btree_index_t *where)
{
	size_t size = tree->bt_elem_size;
	btree_hdr_t *hdr = where->bti_node;
	uint64_t idx = where->bti_offset;
	uint64_t capacity = P2ALIGN((BTREE_LEAF_SIZE - sizeof (btree_hdr_t)) /
	    size, 2);

	ASSERT(!where->bti_before);
	if (tree->bt_bulk != NULL) {
		/*
		 * Leave bulk insert mode. Note that our index would be
		 * invalid after we correct the tree, so we copy the value
		 * we're planning to remove and find it again after
		 * bulk_finish.
		 */
		uint8_t *value = btree_get(tree, where);
		uint8_t *tmp = kmem_alloc(size, KM_SLEEP);
		bcopy(value, tmp, size);
		btree_bulk_finish(tree);
		VERIFY3P(btree_find(tree, tmp, where), !=, NULL);
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
	if (hdr->bth_core) {
		btree_core_t *node = (btree_core_t *)hdr;
		btree_hdr_t *left_subtree = node->btc_children[idx];
		void *new_value = btree_last_helper(tree, left_subtree, where);
		ASSERT3P(new_value, !=, NULL);

		bcopy(new_value, node->btc_elems + idx * size, size);

		hdr = where->bti_node;
		idx = where->bti_offset;
		ASSERT(!where->bti_before);
	}

	/*
	 * First, we'll update the leaf's metadata. Then, we shift any
	 * elements after the idx to the left. After that, we rebalance if
	 * needed.
	 */
	ASSERT(!hdr->bth_core);
	btree_leaf_t *leaf = (btree_leaf_t *)hdr;
	ASSERT3U(hdr->bth_count, >, 0);
	hdr->bth_count--;

	uint64_t min_count = capacity / 2;

	/*
	 * If we're over the minimum size or this is the root, just overwrite
	 * the value and return.
	 */
	if (hdr->bth_count >= min_count || hdr->bth_parent == NULL) {
		bcopy(leaf->btl_elems + (idx + 1) * size, leaf->btl_elems +
		    idx * size, (hdr->bth_count - idx) * size);
		if (hdr->bth_parent == NULL) {
			ASSERT0(tree->bt_height);
			if (hdr->bth_count == 0) {
				tree->bt_root = NULL;
				tree->bt_height--;
				btree_node_destroy(tree, &leaf->btl_hdr);
			}
		}
#ifdef ZFS_DEBUG
		if (tree->bt_root != NULL)
			btree_poison_node_at(tree, hdr, hdr->bth_count);
#endif
		btree_verify(tree);
		return;
	}
	ASSERT3U(hdr->bth_count, ==, min_count - 1);

	/*
	 * Now we try to steal a node from a sibling. We check left, then
	 * right. If they exist and have more than the minimum number of
	 * elements, we move the separator betweeen us and them to our node
	 * and move their closest element (last for left, first for right) to
	 * the separator. Along the way we need to collapse the gap made by
	 * idx, and (for our right neighbor) the gap made by removing their
	 * first element.
	 *
	 * Note: this logic currently doesn't support stealing from a neighbor
	 * that isn't a sibling. This isn't critical functionality, but may be
	 * worth implementing in the future for completeness' sake.
	 */
	btree_core_t *parent = hdr->bth_parent;
	uint64_t parent_idx = btree_find_parent_idx(tree, hdr);

	btree_hdr_t *l_hdr = (parent_idx == 0 ? NULL :
	    parent->btc_children[parent_idx - 1]);
	if (l_hdr != NULL && l_hdr->bth_count > min_count) {
		/* We can steal a node from the left neighbor. */
		ASSERT(!l_hdr->bth_core);

		/*
		 * Move our elements back by one spot to make room for the
		 * stolen element and overwrite the element being removed.
		 */
		bcopy(leaf->btl_elems, leaf->btl_elems + size, idx * size);
		uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
		    size;
		uint8_t *steal_elem = ((btree_leaf_t *)l_hdr)->btl_elems +
		    (l_hdr->bth_count - 1) * size;
		/* Move the separator to our first spot. */
		bcopy(separator, leaf->btl_elems, size);

		/* Move our neighbor's last element to the separator. */
		bcopy(steal_elem, separator, size);

		/* Update the bookkeeping. */
		l_hdr->bth_count--;
		hdr->bth_count++;
#ifdef ZFS_DEBUG
		btree_poison_node_at(tree, l_hdr, l_hdr->bth_count);
#endif
		btree_verify(tree);
		return;
	}

	btree_hdr_t *r_hdr = (parent_idx == parent->btc_hdr.bth_count ?
	    NULL : parent->btc_children[parent_idx + 1]);
	if (r_hdr != NULL && r_hdr->bth_count > min_count) {
		/* We can steal a node from the right neighbor. */
		ASSERT(!r_hdr->bth_core);
		btree_leaf_t *neighbor = (btree_leaf_t *)r_hdr;

		/*
		 * Move our elements after the element being removed forwards
		 * by one spot to make room for the stolen element and
		 * overwrite the element being removed.
		 */
		bcopy(leaf->btl_elems + (idx + 1) * size, leaf->btl_elems +
		    idx * size, (hdr->bth_count - idx) * size);

		uint8_t *separator = parent->btc_elems + parent_idx * size;
		uint8_t *steal_elem = ((btree_leaf_t *)r_hdr)->btl_elems;
		/* Move the separator between us to our last spot. */
		bcopy(separator, leaf->btl_elems + hdr->bth_count * size, size);

		/* Move our neighbor's first element to the separator. */
		bcopy(steal_elem, separator, size);

		/* Update the bookkeeping. */
		r_hdr->bth_count--;
		hdr->bth_count++;

		/*
		 * Move our neighbors elements forwards to overwrite the
		 * stolen element.
		 */
		bcopy(neighbor->btl_elems + size, neighbor->btl_elems,
		    r_hdr->bth_count * size);
#ifdef ZFS_DEBUG
		btree_poison_node_at(tree, r_hdr, r_hdr->bth_count);
#endif
		btree_verify(tree);
		return;
	}

	/*
	 * In this case, neither of our neighbors can spare an element, so we
	 * need to merge with one of them. We prefer the left one,
	 * arabitrarily. Move the separator into the leftmost merging node
	 * (which may be us or the left neighbor), and then move the right
	 * merging node's elements (skipping or overwriting idx, which we're
	 * deleting). Once that's done, go into the parent and delete the
	 * right merging node and the separator. This may cause further
	 * merging.
	 */
	btree_hdr_t *rm_hdr;

	if (l_hdr != NULL) {
		ASSERT(!l_hdr->bth_core);
		btree_leaf_t *left = (btree_leaf_t *)l_hdr;

		if (btree_verify_intensity >= 5) {
			for (int i = 0; i < hdr->bth_count + 1; i++) {
				btree_verify_poison_at(tree, l_hdr,
				    l_hdr->bth_count + i);
			}
		}
		/*
		 * Move the separator into the first open spot in the left
		 * neighbor.
		 */
		uint8_t *out = left->btl_elems + l_hdr->bth_count * size;
		uint8_t *separator = parent->btc_elems + (parent_idx - 1) *
		    size;
		bcopy(separator, out, size);

		/* Move our elements to the left neighbor. */
		out += size;
		uint8_t *start = leaf->btl_elems;
		uint64_t count = idx;
		bcopy(start, out, count * size);

		out += count * size;
		start += (count + 1) * size;
		count = hdr->bth_count - idx;
		bcopy(start, out, count * size);

		/* Update the bookkeeping. */
		l_hdr->bth_count += hdr->bth_count + 1;
		ASSERT3U(l_hdr->bth_count, ==, min_count * 2);
		rm_hdr = hdr;
	} else {
		ASSERT3P(r_hdr, !=, NULL);
		ASSERT(!r_hdr->bth_core);
		if (btree_verify_intensity >= 5) {
			for (int i = 0; i < r_hdr->bth_count; i++) {
				btree_verify_poison_at(tree, hdr,
				    hdr->bth_count + i + 1);
			}
		}
		btree_leaf_t *right = (btree_leaf_t *)r_hdr;

		/*
		 * Move our elements left to overwrite the element being
		 * removed.
		 */
		uint8_t *start = leaf->btl_elems + (idx + 1) * size;
		uint8_t *out = start - size;
		uint64_t count = hdr->bth_count - idx;
		bcopy(start, out, count * size);

		/* Move the separator to node's first open spot. */
		out += count * size;
		uint8_t *separator = parent->btc_elems + parent_idx * size;
		bcopy(separator, out, size);

		/* Move the right neighbor's elements to node. */
		out += size;
		start = right->btl_elems;
		count = r_hdr->bth_count;
		bcopy(start, out, count * size);

		/* Update the bookkeeping. */
		hdr->bth_count += r_hdr->bth_count + 1;
		ASSERT3U(hdr->bth_count, ==, min_count * 2);
		rm_hdr = r_hdr;
	}
	rm_hdr->bth_count = 0;
	btree_node_destroy(tree, rm_hdr);
	/* Remove the emptied node from the parent. */
	btree_remove_from_node(tree, parent, rm_hdr);
	btree_verify(tree);
}

/* Remove the given value from the tree. */
void
btree_remove(btree_t *tree, const void *value)
{
	btree_index_t where = {0};
	VERIFY3P(btree_find(tree, value, &where), !=, NULL);
	btree_remove_from(tree, &where);
}

/* Return the number of elements in the tree. */
ulong_t
btree_numnodes(btree_t *tree)
{
	return (tree->bt_num_elems);
}

/*
 * This function is used to visit all the elements in the tree before
 * destroying the tree. This allows the calling code to perform any cleanup it
 * needs to do. This is more efficient than just removing the first element
 * over and over, because it removes all rebalancing. Once the destroy_nodes()
 * function has been called, no other btree operations are valid until it
 * returns NULL, which point the only valid operation is btree_destroy().
 *
 * example:
 *
 *      btree_index_t *cookie = NULL;
 *      my_data_t *node;
 *
 *      while ((node = btree_destroy_nodes(tree, &cookie)) != NULL)
 *              free(node->ptr);
 *      btree_destroy(tree);
 *
 */
void *
btree_destroy_nodes(btree_t *tree, btree_index_t **cookie)
{
	if (*cookie == NULL) {
		if (tree->bt_height == -1)
			return (NULL);
		*cookie = kmem_alloc(sizeof (**cookie), KM_SLEEP);
		return (btree_first(tree, *cookie));
	}

	void *rval = btree_next_helper(tree, *cookie, *cookie,
	    btree_node_destroy);
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
btree_clear_helper(btree_t *tree, btree_hdr_t *hdr)
{
	if (hdr->bth_core) {
		btree_core_t *btc = (btree_core_t *)hdr;
		for (int i = 0; i <= hdr->bth_count; i++) {
			btree_clear_helper(tree, btc->btc_children[i]);
		}
	}

	btree_node_destroy(tree, hdr);
}

void
btree_clear(btree_t *tree)
{
	if (tree->bt_root == NULL) {
		ASSERT0(tree->bt_num_elems);
		return;
	}

	btree_clear_helper(tree, tree->bt_root);
	tree->bt_num_elems = 0;
	tree->bt_root = NULL;
	tree->bt_num_nodes = 0;
	tree->bt_height = -1;
	tree->bt_bulk = NULL;
}

void
btree_destroy(btree_t *tree)
{
	ASSERT0(tree->bt_num_elems);
	ASSERT3P(tree->bt_root, ==, NULL);
}

/* Verify that every child of this node has the correct parent pointer. */
static void
btree_verify_pointers_helper(btree_t *tree, btree_hdr_t *hdr)
{
	if (!hdr->bth_core)
		return;

	btree_core_t *node = (btree_core_t *)hdr;
	for (int i = 0; i <= hdr->bth_count; i++) {
		VERIFY3P(node->btc_children[i]->bth_parent, ==, hdr);
		btree_verify_pointers_helper(tree, node->btc_children[i]);
	}
}

/* Verify that every node has the correct parent pointer. */
static void
btree_verify_pointers(btree_t *tree)
{
	if (tree->bt_height == -1) {
		VERIFY3P(tree->bt_root, ==, NULL);
		return;
	}
	VERIFY3P(tree->bt_root->bth_parent, ==, NULL);
	btree_verify_pointers_helper(tree, tree->bt_root);
}

/*
 * Verify that all the current node and its children satisfy the count
 * invariants, and return the total count in the subtree rooted in this node.
 */
static uint64_t
btree_verify_counts_helper(btree_t *tree, btree_hdr_t *hdr)
{
	if (!hdr->bth_core) {
		if (tree->bt_root != hdr && hdr != &tree->bt_bulk->btl_hdr) {
			uint64_t capacity = P2ALIGN((BTREE_LEAF_SIZE -
			    sizeof (btree_hdr_t)) / tree->bt_elem_size, 2);
			VERIFY3U(hdr->bth_count, >=, capacity / 2);
		}

		return (hdr->bth_count);
	} else {

		btree_core_t *node = (btree_core_t *)hdr;
		uint64_t ret = hdr->bth_count;
		if (tree->bt_root != hdr && tree->bt_bulk == NULL)
			VERIFY3P(hdr->bth_count, >=, BTREE_CORE_ELEMS / 2);
		for (int i = 0; i <= hdr->bth_count; i++) {
			ret += btree_verify_counts_helper(tree,
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
btree_verify_counts(btree_t *tree)
{
	EQUIV(tree->bt_num_elems == 0, tree->bt_height == -1);
	if (tree->bt_height == -1) {
		return;
	}
	VERIFY3P(btree_verify_counts_helper(tree, tree->bt_root), ==,
	    tree->bt_num_elems);
}

/*
 * Check that the subtree rooted at this node has a uniform height. Returns
 * the number of nodes under this node, to help verify bt_num_nodes.
 */
static uint64_t
btree_verify_height_helper(btree_t *tree, btree_hdr_t *hdr, int64_t height)
{
	if (!hdr->bth_core) {
		VERIFY0(height);
		return (1);
	}

	VERIFY(hdr->bth_core);
	btree_core_t *node = (btree_core_t *)hdr;
	uint64_t ret = 1;
	for (int i = 0; i <= hdr->bth_count; i++) {
		ret += btree_verify_height_helper(tree, node->btc_children[i],
		    height - 1);
	}
	return (ret);
}

/*
 * Check that the tree rooted at this node has a uniform height, and that the
 * bt_height in the tree is correct.
 */
static void
btree_verify_height(btree_t *tree)
{
	EQUIV(tree->bt_height == -1, tree->bt_root == NULL);
	if (tree->bt_height == -1) {
		return;
	}

	VERIFY3U(btree_verify_height_helper(tree, tree->bt_root,
	    tree->bt_height), ==, tree->bt_num_nodes);
}

/*
 * Check that the elements in this node are sorted, and that if this is a core
 * node, the separators are properly between the subtrees they separaate and
 * that the children also satisfy this requirement.
 */
static void
btree_verify_order_helper(btree_t *tree, btree_hdr_t *hdr)
{
	size_t size = tree->bt_elem_size;
	if (!hdr->bth_core) {
		btree_leaf_t *leaf = (btree_leaf_t *)hdr;
		for (int i = 1; i < hdr->bth_count; i++) {
			VERIFY3S(tree->bt_compar(leaf->btl_elems + (i - 1) *
			    size, leaf->btl_elems + i * size), ==, -1);
		}
		return;
	}

	btree_core_t *node = (btree_core_t *)hdr;
	for (int i = 1; i < hdr->bth_count; i++) {
		VERIFY3S(tree->bt_compar(node->btc_elems + (i - 1) * size,
		    node->btc_elems + i * size), ==, -1);
	}
	for (int i = 0; i < hdr->bth_count; i++) {
		uint8_t *left_child_last = NULL;
		btree_hdr_t *left_child_hdr = node->btc_children[i];
		if (left_child_hdr->bth_core) {
			btree_core_t *left_child =
			    (btree_core_t *)left_child_hdr;
			left_child_last = left_child->btc_elems +
			    (left_child_hdr->bth_count - 1) * size;
		} else {
			btree_leaf_t *left_child =
			    (btree_leaf_t *)left_child_hdr;
			left_child_last = left_child->btl_elems +
			    (left_child_hdr->bth_count - 1) * size;
		}
		if (tree->bt_compar(node->btc_elems + i * size,
		    left_child_last) != 1) {
			panic("btree: compar returned %d (expected 1) at "
			    "%p %d: compar(%p,  %p)", tree->bt_compar(
			    node->btc_elems + i * size, left_child_last),
			    (void *)node, i, (void *)(node->btc_elems + i *
			    size), (void *)left_child_last);
		}

		uint8_t *right_child_first = NULL;
		btree_hdr_t *right_child_hdr = node->btc_children[i + 1];
		if (right_child_hdr->bth_core) {
			btree_core_t *right_child =
			    (btree_core_t *)right_child_hdr;
			right_child_first = right_child->btc_elems;
		} else {
			btree_leaf_t *right_child =
			    (btree_leaf_t *)right_child_hdr;
			right_child_first = right_child->btl_elems;
		}
		if (tree->bt_compar(node->btc_elems + i * size,
		    right_child_first) != -1) {
			panic("btree: compar returned %d (expected -1) at "
			    "%p %d: compar(%p,  %p)", tree->bt_compar(
			    node->btc_elems + i * size, right_child_first),
			    (void *)node, i, (void *)(node->btc_elems + i *
			    size), (void *)right_child_first);
		}
	}
	for (int i = 0; i <= hdr->bth_count; i++) {
		btree_verify_order_helper(tree, node->btc_children[i]);
	}
}

/* Check that all elements in the tree are in sorted order. */
static void
btree_verify_order(btree_t *tree)
{
	EQUIV(tree->bt_height == -1, tree->bt_root == NULL);
	if (tree->bt_height == -1) {
		return;
	}

	btree_verify_order_helper(tree, tree->bt_root);
}

#ifdef ZFS_DEBUG
/* Check that all unused memory is poisoned correctly. */
static void
btree_verify_poison_helper(btree_t *tree, btree_hdr_t *hdr)
{
	size_t size = tree->bt_elem_size;
	if (!hdr->bth_core) {
		btree_leaf_t *leaf = (btree_leaf_t *)hdr;
		uint8_t val = 0x0f;
		for (int i = hdr->bth_count * size; i < BTREE_LEAF_SIZE -
		    sizeof (btree_hdr_t); i++) {
			VERIFY3U(leaf->btl_elems[i], ==, val);
		}
	} else {
		btree_core_t *node = (btree_core_t *)hdr;
		uint8_t val = 0x0f;
		for (int i = hdr->bth_count * size; i < BTREE_CORE_ELEMS * size;
		    i++) {
			VERIFY3U(node->btc_elems[i], ==, val);
		}

		for (int i = hdr->bth_count + 1; i <= BTREE_CORE_ELEMS; i++) {
			VERIFY3P(node->btc_children[i], ==,
			    (btree_hdr_t *)BTREE_POISON);
		}

		for (int i = 0; i <= hdr->bth_count; i++) {
			btree_verify_poison_helper(tree, node->btc_children[i]);
		}
	}
}
#endif

/* Check that unused memory in the tree is still poisoned. */
static void
btree_verify_poison(btree_t *tree)
{
#ifdef ZFS_DEBUG
	if (tree->bt_height == -1)
		return;
	btree_verify_poison_helper(tree, tree->bt_root);
#endif
}

void
btree_verify(btree_t *tree)
{
	if (btree_verify_intensity == 0)
		return;
	btree_verify_height(tree);
	if (btree_verify_intensity == 1)
		return;
	btree_verify_pointers(tree);
	if (btree_verify_intensity == 2)
		return;
	btree_verify_counts(tree);
	if (btree_verify_intensity == 3)
		return;
	btree_verify_order(tree);

	if (btree_verify_intensity == 4)
		return;
	btree_verify_poison(tree);
}
