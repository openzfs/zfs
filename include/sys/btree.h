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

#ifndef	_BTREE_H
#define	_BTREE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include	<sys/zfs_context.h>

/*
 * This file defines the interface for a B-Tree implementation for ZFS. The
 * tree can be used to store arbitrary sortable data types with low overhead
 * and good operation performance. In addition the tree intelligently
 * optimizes bulk in-order insertions to improve memory use and performance.
 *
 * Note that for all B-Tree functions, the values returned are pointers to the
 * internal copies of the data in the tree. The internal data can only be
 * safely mutated if the changes cannot change the ordering of the element
 * with respect to any other elements in the tree.
 *
 * The major drawback of the B-Tree is that any returned elements or indexes
 * are only valid until a side-effectful operation occurs, since these can
 * result in reallocation or relocation of data. Side effectful operations are
 * defined as insertion, removal, and zfs_btree_destroy_nodes.
 *
 * The B-Tree has two types of nodes: core nodes, and leaf nodes. Core
 * nodes have an array of children pointing to other nodes, and an array of
 * elements that act as separators between the elements of the subtrees rooted
 * at its children. Leaf nodes only contain data elements, and form the bottom
 * layer of the tree. Unlike B+ Trees, in this B-Tree implementation the
 * elements in the core nodes are not copies of or references to leaf node
 * elements.  Each element occurs only once in the tree, no matter what kind
 * of node it is in.
 *
 * The tree's height is the same throughout, unlike many other forms of search
 * tree. Each node (except for the root) must be between half minus one and
 * completely full of elements (and children) at all times. Any operation that
 * would put the node outside of that range results in a rebalancing operation
 * (taking, merging, or splitting).
 *
 * This tree was implemented using descriptions from Wikipedia's articles on
 * B-Trees and B+ Trees.
 */

/*
 * Decreasing these values results in smaller memmove operations, but more of
 * them, and increased memory overhead. Increasing these values results in
 * higher variance in operation time, and reduces memory overhead.
 */
#define	BTREE_CORE_ELEMS	128
#define	BTREE_LEAF_SIZE		4096

extern kmem_cache_t *zfs_btree_leaf_cache;

typedef struct zfs_btree_hdr {
	struct zfs_btree_core	*bth_parent;
	boolean_t		bth_core;
	/*
	 * For both leaf and core nodes, represents the number of elements in
	 * the node. For core nodes, they will have bth_count + 1 children.
	 */
	uint32_t		bth_count;
} zfs_btree_hdr_t;

typedef struct zfs_btree_core {
	zfs_btree_hdr_t	btc_hdr;
	zfs_btree_hdr_t	*btc_children[BTREE_CORE_ELEMS + 1];
	uint8_t		btc_elems[];
} zfs_btree_core_t;

typedef struct zfs_btree_leaf {
	zfs_btree_hdr_t	btl_hdr;
	uint8_t		btl_elems[];
} zfs_btree_leaf_t;

typedef struct zfs_btree_index {
	zfs_btree_hdr_t	*bti_node;
	uint64_t	bti_offset;
	/*
	 * True if the location is before the list offset, false if it's at
	 * the listed offset.
	 */
	boolean_t	bti_before;
} zfs_btree_index_t;

typedef struct btree {
	zfs_btree_hdr_t		*bt_root;
	int64_t			bt_height;
	size_t			bt_elem_size;
	uint64_t		bt_num_elems;
	uint64_t		bt_num_nodes;
	zfs_btree_leaf_t	*bt_bulk; // non-null if bulk loading
	int (*bt_compar) (const void *, const void *);
} zfs_btree_t;

/*
 * Allocate and deallocate caches for btree nodes.
 */
void zfs_btree_init(void);
void zfs_btree_fini(void);

/*
 * Initialize an B-Tree. Arguments are:
 *
 * tree   - the tree to be initialized
 * compar - function to compare two nodes, it must return exactly: -1, 0, or +1
 *          -1 for <, 0 for ==, and +1 for >
 * size   - the value of sizeof(struct my_type)
 */
void zfs_btree_create(zfs_btree_t *, int (*) (const void *, const void *),
    size_t);

/*
 * Find a node with a matching value in the tree. Returns the matching node
 * found. If not found, it returns NULL and then if "where" is not NULL it sets
 * "where" for use with zfs_btree_add_idx() or zfs_btree_nearest().
 *
 * node   - node that has the value being looked for
 * where  - position for use with zfs_btree_nearest() or zfs_btree_add_idx(),
 *          may be NULL
 */
void *zfs_btree_find(zfs_btree_t *, const void *, zfs_btree_index_t *);

/*
 * Insert a node into the tree.
 *
 * node   - the node to insert
 * where  - position as returned from zfs_btree_find()
 */
void zfs_btree_add_idx(zfs_btree_t *, const void *, const zfs_btree_index_t *);

/*
 * Return the first or last valued node in the tree. Will return NULL if the
 * tree is empty. The index can be NULL if the location of the first or last
 * element isn't required.
 */
void *zfs_btree_first(zfs_btree_t *, zfs_btree_index_t *);
void *zfs_btree_last(zfs_btree_t *, zfs_btree_index_t *);

/*
 * Return the next or previous valued node in the tree. The second index can
 * safely be NULL, if the location of the next or previous value isn't
 * required.
 */
void *zfs_btree_next(zfs_btree_t *, const zfs_btree_index_t *,
    zfs_btree_index_t *);
void *zfs_btree_prev(zfs_btree_t *, const zfs_btree_index_t *,
    zfs_btree_index_t *);

/*
 * Get a value from a tree and an index.
 */
void *zfs_btree_get(zfs_btree_t *, zfs_btree_index_t *);

/*
 * Add a single value to the tree. The value must not compare equal to any
 * other node already in the tree. Note that the value will be copied out, not
 * inserted directly. It is safe to free or destroy the value once this
 * function returns.
 */
void zfs_btree_add(zfs_btree_t *, const void *);

/*
 * Remove a single value from the tree.  The value must be in the tree. The
 * pointer passed in may be a pointer into a tree-controlled buffer, but it
 * need not be.
 */
void zfs_btree_remove(zfs_btree_t *, const void *);

/*
 * Remove the value at the given location from the tree.
 */
void zfs_btree_remove_idx(zfs_btree_t *, zfs_btree_index_t *);

/*
 * Return the number of nodes in the tree
 */
ulong_t zfs_btree_numnodes(zfs_btree_t *);

/*
 * Used to destroy any remaining nodes in a tree. The cookie argument should
 * be initialized to NULL before the first call. Returns a node that has been
 * removed from the tree and may be free()'d. Returns NULL when the tree is
 * empty.
 *
 * Once you call zfs_btree_destroy_nodes(), you can only continuing calling it
 * and finally zfs_btree_destroy(). No other B-Tree routines will be valid.
 *
 * cookie - an index used to save state between calls to
 * zfs_btree_destroy_nodes()
 *
 * EXAMPLE:
 *	zfs_btree_t *tree;
 *	struct my_data *node;
 *	zfs_btree_index_t *cookie;
 *
 *	cookie = NULL;
 *	while ((node = zfs_btree_destroy_nodes(tree, &cookie)) != NULL)
 *		data_destroy(node);
 *	zfs_btree_destroy(tree);
 */
void *zfs_btree_destroy_nodes(zfs_btree_t *, zfs_btree_index_t **);

/*
 * Destroys all nodes in the tree quickly. This doesn't give the caller an
 * opportunity to iterate over each node and do its own cleanup; for that, use
 * zfs_btree_destroy_nodes().
 */
void zfs_btree_clear(zfs_btree_t *);

/*
 * Final destroy of an B-Tree. Arguments are:
 *
 * tree   - the empty tree to destroy
 */
void zfs_btree_destroy(zfs_btree_t *tree);

/* Runs a variety of self-checks on the btree to verify integrity. */
void zfs_btree_verify(zfs_btree_t *tree);

#ifdef	__cplusplus
}
#endif

#endif	/* _BTREE_H */
