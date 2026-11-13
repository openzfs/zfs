// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

/*
 * This is the internal header for allocator_t. It holds the struct
 * definitions that back the implementation but that shouldn't be part of
 * the public API in zstream_alloc.h. Conceptually it is part of
 * zstream_alloc.c; it exists only so that selftests can inspect group
 * state.
 */

#ifndef _ZSTREAM_ALLOC_IMPL_H
#define	_ZSTREAM_ALLOC_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "zstream_alloc.h"
#include "zstream_heap.h"

#define	INITIAL_HEAP_SIZE	64

#define	clusters_owned(alloc)	(heap_count(&(alloc)->a_cluster_heap))

/*
 * Here's the memory model for this system:
 *
 * A group's page cluster size is the granularity of memory management. It's
 * a whole number of pages: by default, 1MB worth if the system page size is
 * not larger. An allocator's memory to disk transition always falls at a
 * page cluster boundary.
 *
 * The group's ag_base_addr is a pointer to an mmapped region of address
 * space of size ag_clusters_max * ag_cluster_size (or NULL for a disk-only
 * group). This memory is initially PROT_NONE. It's converted to usable
 * memory page cluster by page cluster as storage needs increase.
 *
 * Each allocator within the group has an a_clusters array of pointers to
 * memory clusters. Those arrays are all sized to accommodate the group's
 * total memory limit. The pointers are all initially NULL and no allocator
 * owns any memory.
 *
 * When a write targets a page cluster that's within the memory region but
 * for which the allocator doesn't yet have backing, it needs to find a
 * cluster to insert into its a_clusters table. Clusters can come from two
 * sources: a new allocation from the group's PROT_NONE address space, or
 * stolen from a lower-priority allocator via poaching. Once a usable source
 * is found, it's entered into the allocator's memory map.
 *
 * If API clients request scattered writes, an allocator's a_clusters array
 * may be only sparsely populated. That's fine; additional memory can be
 * attached anywhere within the range.
 *
 * When an allocator receives a write request for an as-yet-unallocated
 * cluster but finds that no memory is available, it converts that request
 * to a disk operation by lowering a_first_disk_cluster. Unlike memory, disk
 * storage is monolithic and continuous, and a_first_disk_cluster is its
 * lower bound. If an allocator's memory is progressively preempted by
 * higher priority allocators, a_first_disk_cluster will gradually lower all
 * the way to 0, at which point that allocator will no longer be using any
 * memory.
 *
 * To keep a_first_disk_cluster as high as possible, allocators will poach
 * from their own supply of page clusters if they cannot poach from a
 * lower-priority allocator. For example, if a write request for unallocated
 * cluster 100 is received but the allocator's highest-numbered page cluster
 * is 200, cluster 200 will be written to disk, zeroed, and then reassigned
 * to alloc->a_clusters[100]. a_first_disk_cluster then becomes 200.
 *
 * Once allocated, memory is never freed until the allocator group is
 * dissolved. All page clusters remain in active use.
 */
typedef struct {
	uint64_t	ag_clusters_max;	/* Total allowed for group */
	uint64_t	ag_clusters_current;	/* Currently used by group */
	size_t		ag_cluster_size;	/* Allocation unit */
	char		*ag_dir;		/* Spill dir, for error msgs */
	unsigned int	ag_num_allocators;	/* # of allocators in group */
	allocator_t	**ag_allocators;	/* Pointers to allocator_t's */
	uint8_t		*ag_base_addr;		/* Start of address space */
	boolean_t	ag_enomem_warned;	/* Suppress subsequent warns */
} allocator_group_t;

/*
 * Invariants:
 *
 *   clusters_owned(alloc) <= a_first_disk_cluster <= a_group->ag_clusters_max
 *   a_group->ag_clusters_current <= a_group->ag_clusters_max
 *   a_group->ag_clusters_current = sum_over_group(clusters_owned())
 *   a_first_disk_cluster decreases monotonically (if at all)
 *   a_group->ag_clusters_current increases monotonically (if at all)
 *
 * Given that you can read and write anywhere in the index space,
 * a_record_count is a bit vaguely defined. It's maintained to give
 * allocator_append() and allocator_skip() some reasonable definition of
 * "the next index." Functionally, it's 1 plus the highest index that was
 * ever written to or returned to the client. Reads don't affect it.
 *
 * a_cluster_heap tracks the indexes in a_clusters that point to actual
 * memory. When converting memory clusters to disk, we always want to
 * purge the cluster at the highest possible index. That guarantees three
 * properties:
 *
 *   1) a_first_disk_cluster is moved only minimally.
 *   2) We only need to flush a single cluster to disk.
 *   3) The on-disk region remains monolithic.
 */
struct allocator {
	int		   a_fd;		  /* On-disk file descriptor */
	size_t		   a_record_size;	  /* Client-specified */
	uint64_t	   a_records_per_cluster; /* Constant (minimize divs) */
	uint64_t	   a_first_disk_cluster;  /* Memory/disk split point */
	uint64_t	   a_record_count;	  /* See comments above */
	uint8_t		   **a_clusters;	  /* Array of cluster ptrs */
	heap_t		   a_cluster_heap;	  /* Heap of memory clusters */
	allocator_group_t  *a_group;		  /* Back-pointer to group */
};

#ifdef __cplusplus
}
#endif

#endif /* _ZSTREAM_ALLOC_IMPL_H */
