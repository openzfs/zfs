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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Iterate over all children of the current object.  This includes the normal
 * dataset hierarchy, but also arbitrary hierarchies due to clones.  We want to
 * walk all datasets in the pool, and construct a directed graph of the form:
 *
 * 			home
 *                        |
 *                   +----+----+
 *                   |         |
 *                   v         v             ws
 *                  bar       baz             |
 *                             |              |
 *                             v              v
 *                          @yesterday ----> foo
 *
 * In order to construct this graph, we have to walk every dataset in the pool,
 * because the clone parent is stored as a property of the child, not the
 * parent.  The parent only keeps track of the number of clones.
 *
 * In the normal case (without clones) this would be rather expensive.  To avoid
 * unnecessary computation, we first try a walk of the subtree hierarchy
 * starting from the initial node.  At each dataset, we construct a node in the
 * graph and an edge leading from its parent.  If we don't see any snapshots
 * with a non-zero clone count, then we are finished.
 *
 * If we do find a cloned snapshot, then we finish the walk of the current
 * subtree, but indicate that we need to do a complete walk.  We then perform a
 * global walk of all datasets, avoiding the subtree we already processed.
 *
 * At the end of this, we'll end up with a directed graph of all relevant (and
 * possible some irrelevant) datasets in the system.  We need to both find our
 * limiting subgraph and determine a safe ordering in which to destroy the
 * datasets.  We do a topological ordering of our graph starting at our target
 * dataset, and then walk the results in reverse.
 *
 * It's possible for the graph to have cycles if, for example, the user renames
 * a clone to be the parent of its origin snapshot.  The user can request to
 * generate an error in this case, or ignore the cycle and continue.
 *
 * When removing datasets, we want to destroy the snapshots in chronological
 * order (because this is the most efficient method).  In order to accomplish
 * this, we store the creation transaction group with each vertex and keep each
 * vertex's edges sorted according to this value.  The topological sort will
 * automatically walk the snapshots in the correct order.
 */

#include <assert.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <libzfs.h>

#include "libzfs_impl.h"
#include "zfs_namecheck.h"

#define	MIN_EDGECOUNT	4

/*
 * Vertex structure.  Indexed by dataset name, this structure maintains a list
 * of edges to other vertices.
 */
struct zfs_edge;
typedef struct zfs_vertex {
	char			zv_dataset[ZFS_MAXNAMELEN];
	struct zfs_vertex	*zv_next;
	int			zv_visited;
	uint64_t		zv_txg;
	struct zfs_edge		**zv_edges;
	int			zv_edgecount;
	int			zv_edgealloc;
} zfs_vertex_t;

enum {
	VISIT_SEEN = 1,
	VISIT_SORT_PRE,
	VISIT_SORT_POST
};

/*
 * Edge structure.  Simply maintains a pointer to the destination vertex.  There
 * is no need to store the source vertex, since we only use edges in the context
 * of the source vertex.
 */
typedef struct zfs_edge {
	zfs_vertex_t		*ze_dest;
	struct zfs_edge		*ze_next;
} zfs_edge_t;

#define	ZFS_GRAPH_SIZE		1027	/* this could be dynamic some day */

/*
 * Graph structure.  Vertices are maintained in a hash indexed by dataset name.
 */
typedef struct zfs_graph {
	zfs_vertex_t		**zg_hash;
	size_t			zg_size;
	size_t			zg_nvertex;
	const char		*zg_root;
	int			zg_clone_count;
} zfs_graph_t;

/*
 * Allocate a new edge pointing to the target vertex.
 */
static zfs_edge_t *
zfs_edge_create(libzfs_handle_t *hdl, zfs_vertex_t *dest)
{
	zfs_edge_t *zep = zfs_alloc(hdl, sizeof (zfs_edge_t));

	if (zep == NULL)
		return (NULL);

	zep->ze_dest = dest;

	return (zep);
}

/*
 * Destroy an edge.
 */
static void
zfs_edge_destroy(zfs_edge_t *zep)
{
	free(zep);
}

/*
 * Allocate a new vertex with the given name.
 */
static zfs_vertex_t *
zfs_vertex_create(libzfs_handle_t *hdl, const char *dataset)
{
	zfs_vertex_t *zvp = zfs_alloc(hdl, sizeof (zfs_vertex_t));

	if (zvp == NULL)
		return (NULL);

	assert(strlen(dataset) < ZFS_MAXNAMELEN);

	(void) strlcpy(zvp->zv_dataset, dataset, sizeof (zvp->zv_dataset));

	if ((zvp->zv_edges = zfs_alloc(hdl,
	    MIN_EDGECOUNT * sizeof (void *))) == NULL) {
		free(zvp);
		return (NULL);
	}

	zvp->zv_edgealloc = MIN_EDGECOUNT;

	return (zvp);
}

/*
 * Destroy a vertex.  Frees up any associated edges.
 */
static void
zfs_vertex_destroy(zfs_vertex_t *zvp)
{
	int i;

	for (i = 0; i < zvp->zv_edgecount; i++)
		zfs_edge_destroy(zvp->zv_edges[i]);

	free(zvp->zv_edges);
	free(zvp);
}

/*
 * Given a vertex, add an edge to the destination vertex.
 */
static int
zfs_vertex_add_edge(libzfs_handle_t *hdl, zfs_vertex_t *zvp,
    zfs_vertex_t *dest)
{
	zfs_edge_t *zep = zfs_edge_create(hdl, dest);

	if (zep == NULL)
		return (-1);

	if (zvp->zv_edgecount == zvp->zv_edgealloc) {
		void *ptr;

		if ((ptr = zfs_realloc(hdl, zvp->zv_edges,
		    zvp->zv_edgealloc * sizeof (void *),
		    zvp->zv_edgealloc * 2 * sizeof (void *))) == NULL)
			return (-1);

		zvp->zv_edges = ptr;
		zvp->zv_edgealloc *= 2;
	}

	zvp->zv_edges[zvp->zv_edgecount++] = zep;

	return (0);
}

static int
zfs_edge_compare(const void *a, const void *b)
{
	const zfs_edge_t *ea = *((zfs_edge_t **)a);
	const zfs_edge_t *eb = *((zfs_edge_t **)b);

	if (ea->ze_dest->zv_txg < eb->ze_dest->zv_txg)
		return (-1);
	if (ea->ze_dest->zv_txg > eb->ze_dest->zv_txg)
		return (1);
	return (0);
}

/*
 * Sort the given vertex edges according to the creation txg of each vertex.
 */
static void
zfs_vertex_sort_edges(zfs_vertex_t *zvp)
{
	if (zvp->zv_edgecount == 0)
		return;

	qsort(zvp->zv_edges, zvp->zv_edgecount, sizeof (void *),
	    zfs_edge_compare);
}

/*
 * Construct a new graph object.  We allow the size to be specified as a
 * parameter so in the future we can size the hash according to the number of
 * datasets in the pool.
 */
static zfs_graph_t *
zfs_graph_create(libzfs_handle_t *hdl, const char *dataset, size_t size)
{
	zfs_graph_t *zgp = zfs_alloc(hdl, sizeof (zfs_graph_t));

	if (zgp == NULL)
		return (NULL);

	zgp->zg_size = size;
	if ((zgp->zg_hash = zfs_alloc(hdl,
	    size * sizeof (zfs_vertex_t *))) == NULL) {
		free(zgp);
		return (NULL);
	}

	zgp->zg_root = dataset;
	zgp->zg_clone_count = 0;

	return (zgp);
}

/*
 * Destroy a graph object.  We have to iterate over all the hash chains,
 * destroying each vertex in the process.
 */
static void
zfs_graph_destroy(zfs_graph_t *zgp)
{
	int i;
	zfs_vertex_t *current, *next;

	for (i = 0; i < zgp->zg_size; i++) {
		current = zgp->zg_hash[i];
		while (current != NULL) {
			next = current->zv_next;
			zfs_vertex_destroy(current);
			current = next;
		}
	}

	free(zgp->zg_hash);
	free(zgp);
}

/*
 * Graph hash function.  Classic bernstein k=33 hash function, taken from
 * usr/src/cmd/sgs/tools/common/strhash.c
 */
static size_t
zfs_graph_hash(zfs_graph_t *zgp, const char *str)
{
	size_t hash = 5381;
	int c;

	while ((c = *str++) != 0)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return (hash % zgp->zg_size);
}

/*
 * Given a dataset name, finds the associated vertex, creating it if necessary.
 */
static zfs_vertex_t *
zfs_graph_lookup(libzfs_handle_t *hdl, zfs_graph_t *zgp, const char *dataset,
    uint64_t txg)
{
	size_t idx = zfs_graph_hash(zgp, dataset);
	zfs_vertex_t *zvp;

	for (zvp = zgp->zg_hash[idx]; zvp != NULL; zvp = zvp->zv_next) {
		if (strcmp(zvp->zv_dataset, dataset) == 0) {
			if (zvp->zv_txg == 0)
				zvp->zv_txg = txg;
			return (zvp);
		}
	}

	if ((zvp = zfs_vertex_create(hdl, dataset)) == NULL)
		return (NULL);

	zvp->zv_next = zgp->zg_hash[idx];
	zvp->zv_txg = txg;
	zgp->zg_hash[idx] = zvp;
	zgp->zg_nvertex++;

	return (zvp);
}

/*
 * Given two dataset names, create an edge between them.  For the source vertex,
 * mark 'zv_visited' to indicate that we have seen this vertex, and not simply
 * created it as a destination of another edge.  If 'dest' is NULL, then this
 * is an individual vertex (i.e. the starting vertex), so don't add an edge.
 */
static int
zfs_graph_add(libzfs_handle_t *hdl, zfs_graph_t *zgp, const char *source,
    const char *dest, uint64_t txg)
{
	zfs_vertex_t *svp, *dvp;

	if ((svp = zfs_graph_lookup(hdl, zgp, source, 0)) == NULL)
		return (-1);
	svp->zv_visited = VISIT_SEEN;
	if (dest != NULL) {
		dvp = zfs_graph_lookup(hdl, zgp, dest, txg);
		if (dvp == NULL)
			return (-1);
		if (zfs_vertex_add_edge(hdl, svp, dvp) != 0)
			return (-1);
	}

	return (0);
}

/*
 * Iterate over all children of the given dataset, adding any vertices
 * as necessary.  Returns -1 if there was an error, or 0 otherwise.
 * This is a simple recursive algorithm - the ZFS namespace typically
 * is very flat.  We manually invoke the necessary ioctl() calls to
 * avoid the overhead and additional semantics of zfs_open().
 */
static int
iterate_children(libzfs_handle_t *hdl, zfs_graph_t *zgp, const char *dataset)
{
	zfs_cmd_t zc = { 0 };
	zfs_vertex_t *zvp;

	/*
	 * Look up the source vertex, and avoid it if we've seen it before.
	 */
	zvp = zfs_graph_lookup(hdl, zgp, dataset, 0);
	if (zvp == NULL)
		return (-1);
	if (zvp->zv_visited == VISIT_SEEN)
		return (0);

	/*
	 * Iterate over all children
	 */
	for ((void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	    ioctl(hdl->libzfs_fd, ZFS_IOC_DATASET_LIST_NEXT, &zc) == 0;
	    (void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name))) {
		/*
		 * Get statistics for this dataset, to determine the type of the
		 * dataset and clone statistics.  If this fails, the dataset has
		 * since been removed, and we're pretty much screwed anyway.
		 */
		zc.zc_objset_stats.dds_origin[0] = '\0';
		if (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0)
			continue;

		if (zc.zc_objset_stats.dds_origin[0] != '\0') {
			if (zfs_graph_add(hdl, zgp,
			    zc.zc_objset_stats.dds_origin, zc.zc_name,
			    zc.zc_objset_stats.dds_creation_txg) != 0)
				return (-1);
			/*
			 * Count origins only if they are contained in the graph
			 */
			if (isa_child_of(zc.zc_objset_stats.dds_origin,
			    zgp->zg_root))
				zgp->zg_clone_count--;
		}

		/*
		 * Add an edge between the parent and the child.
		 */
		if (zfs_graph_add(hdl, zgp, dataset, zc.zc_name,
		    zc.zc_objset_stats.dds_creation_txg) != 0)
			return (-1);

		/*
		 * Recursively visit child
		 */
		if (iterate_children(hdl, zgp, zc.zc_name))
			return (-1);
	}

	/*
	 * Now iterate over all snapshots.
	 */
	bzero(&zc, sizeof (zc));

	for ((void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	    ioctl(hdl->libzfs_fd, ZFS_IOC_SNAPSHOT_LIST_NEXT, &zc) == 0;
	    (void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name))) {

		/*
		 * Get statistics for this dataset, to determine the type of the
		 * dataset and clone statistics.  If this fails, the dataset has
		 * since been removed, and we're pretty much screwed anyway.
		 */
		if (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0)
			continue;

		/*
		 * Add an edge between the parent and the child.
		 */
		if (zfs_graph_add(hdl, zgp, dataset, zc.zc_name,
		    zc.zc_objset_stats.dds_creation_txg) != 0)
			return (-1);

		zgp->zg_clone_count += zc.zc_objset_stats.dds_num_clones;
	}

	zvp->zv_visited = VISIT_SEEN;

	return (0);
}

/*
 * Returns false if there are no snapshots with dependent clones in this
 * subtree or if all of those clones are also in this subtree.  Returns
 * true if there is an error or there are external dependents.
 */
static boolean_t
external_dependents(libzfs_handle_t *hdl, zfs_graph_t *zgp, const char *dataset)
{
	zfs_cmd_t zc = { 0 };

	/*
	 * Check whether this dataset is a clone or has clones since
	 * iterate_children() only checks the children.
	 */
	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	if (ioctl(hdl->libzfs_fd, ZFS_IOC_OBJSET_STATS, &zc) != 0)
		return (B_TRUE);

	if (zc.zc_objset_stats.dds_origin[0] != '\0') {
		if (zfs_graph_add(hdl, zgp,
		    zc.zc_objset_stats.dds_origin, zc.zc_name,
		    zc.zc_objset_stats.dds_creation_txg) != 0)
			return (B_TRUE);
		if (isa_child_of(zc.zc_objset_stats.dds_origin, dataset))
			zgp->zg_clone_count--;
	}

	if ((zc.zc_objset_stats.dds_num_clones) ||
	    iterate_children(hdl, zgp, dataset))
		return (B_TRUE);

	return (zgp->zg_clone_count != 0);
}

/*
 * Construct a complete graph of all necessary vertices.  First, iterate over
 * only our object's children.  If no cloned snapshots are found, or all of
 * the cloned snapshots are in this subtree then return a graph of the subtree.
 * Otherwise, start at the root of the pool and iterate over all datasets.
 */
static zfs_graph_t *
construct_graph(libzfs_handle_t *hdl, const char *dataset)
{
	zfs_graph_t *zgp = zfs_graph_create(hdl, dataset, ZFS_GRAPH_SIZE);
	int ret = 0;

	if (zgp == NULL)
		return (zgp);

	if ((strchr(dataset, '/') == NULL) ||
	    (external_dependents(hdl, zgp, dataset))) {
		/*
		 * Determine pool name and try again.
		 */
		int len = strcspn(dataset, "/@") + 1;
		char *pool = zfs_alloc(hdl, len);

		if (pool == NULL) {
			zfs_graph_destroy(zgp);
			return (NULL);
		}
		(void) strlcpy(pool, dataset, len);

		if (iterate_children(hdl, zgp, pool) == -1 ||
		    zfs_graph_add(hdl, zgp, pool, NULL, 0) != 0) {
			free(pool);
			zfs_graph_destroy(zgp);
			return (NULL);
		}
		free(pool);
	}

	if (ret == -1 || zfs_graph_add(hdl, zgp, dataset, NULL, 0) != 0) {
		zfs_graph_destroy(zgp);
		return (NULL);
	}

	return (zgp);
}

/*
 * Given a graph, do a recursive topological sort into the given array.  This is
 * really just a depth first search, so that the deepest nodes appear first.
 * hijack the 'zv_visited' marker to avoid visiting the same vertex twice.
 */
static int
topo_sort(libzfs_handle_t *hdl, boolean_t allowrecursion, char **result,
    size_t *idx, zfs_vertex_t *zgv)
{
	int i;

	if (zgv->zv_visited == VISIT_SORT_PRE && !allowrecursion) {
		/*
		 * If we've already seen this vertex as part of our depth-first
		 * search, then we have a cyclic dependency, and we must return
		 * an error.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
		    "recursive dependency at '%s'"),
		    zgv->zv_dataset);
		return (zfs_error(hdl, EZFS_RECURSIVE,
		    dgettext(TEXT_DOMAIN,
		    "cannot determine dependent datasets")));
	} else if (zgv->zv_visited >= VISIT_SORT_PRE) {
		/*
		 * If we've already processed this as part of the topological
		 * sort, then don't bother doing so again.
		 */
		return (0);
	}

	zgv->zv_visited = VISIT_SORT_PRE;

	/* avoid doing a search if we don't have to */
	zfs_vertex_sort_edges(zgv);
	for (i = 0; i < zgv->zv_edgecount; i++) {
		if (topo_sort(hdl, allowrecursion, result, idx,
		    zgv->zv_edges[i]->ze_dest) != 0)
			return (-1);
	}

	/* we may have visited this in the course of the above */
	if (zgv->zv_visited == VISIT_SORT_POST)
		return (0);

	if ((result[*idx] = zfs_alloc(hdl,
	    strlen(zgv->zv_dataset) + 1)) == NULL)
		return (-1);

	(void) strcpy(result[*idx], zgv->zv_dataset);
	*idx += 1;
	zgv->zv_visited = VISIT_SORT_POST;
	return (0);
}

/*
 * The only public interface for this file.  Do the dirty work of constructing a
 * child list for the given object.  Construct the graph, do the toplogical
 * sort, and then return the array of strings to the caller.
 *
 * The 'allowrecursion' parameter controls behavior when cycles are found.  If
 * it is set, the the cycle is ignored and the results returned as if the cycle
 * did not exist.  If it is not set, then the routine will generate an error if
 * a cycle is found.
 */
int
get_dependents(libzfs_handle_t *hdl, boolean_t allowrecursion,
    const char *dataset, char ***result, size_t *count)
{
	zfs_graph_t *zgp;
	zfs_vertex_t *zvp;

	if ((zgp = construct_graph(hdl, dataset)) == NULL)
		return (-1);

	if ((*result = zfs_alloc(hdl,
	    zgp->zg_nvertex * sizeof (char *))) == NULL) {
		zfs_graph_destroy(zgp);
		return (-1);
	}

	if ((zvp = zfs_graph_lookup(hdl, zgp, dataset, 0)) == NULL) {
		free(*result);
		zfs_graph_destroy(zgp);
		return (-1);
	}

	*count = 0;
	if (topo_sort(hdl, allowrecursion, *result, count, zvp) != 0) {
		free(*result);
		zfs_graph_destroy(zgp);
		return (-1);
	}

	/*
	 * Get rid of the last entry, which is our starting vertex and not
	 * strictly a dependent.
	 */
	assert(*count > 0);
	free((*result)[*count - 1]);
	(*count)--;

	zfs_graph_destroy(zgp);

	return (0);
}
