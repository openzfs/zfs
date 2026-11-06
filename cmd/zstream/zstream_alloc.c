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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include "zstream_alloc.h"
#include "zstream_alloc_impl.h"
#include "zstream_heap.h"
#include "zstream_util.h"

/*
 * This implementation exploits two features common to most UNIX-like systems.
 *
 * - Filesystem write holes: on an allocator that uses both memory and disk
 *   storage, the memory-resident portion of the data is treated as an overlay
 *   of the first part of the backing file. Memory and disk share the same
 *   addressing scheme for records: record 100 is always at the same offset
 *   whether it's in memory or on disk.
 *
 *   The first part of the disk file hides underneath the memory overlay and
 *   is never written to unless the allocator is forced to yield memory.
 *   Ergo, it occupies no storage space on filesystems that support holes.
 *   Since memory and disk have common addressing, clusters can be moved from
 *   memory to disk by writing them to their same offsets in the file.
 *
 *   If the backing file's filesystem does not support holes (unlikely but
 *   possible), the code is still correct. However, actual disk space
 *   consumption will be higher.
 *
 * - PROT_NONE virtual pages: These pages do not consume physical memory,
 *   TLB entries, or swap space, so they are essentially free. Allocator
 *   groups request a monolithic allocation of address space up front.
 *   (The pages do still count as address space, so the whole budget must
 *   fit within RLIMIT_AS.)
 *
 *   The group makes clusters writable one at a time, in address order, by
 *   changing their protection from PROT_NONE to PROT_READ | PROT_WRITE.
 *   Clusters never revert to PROT_NONE. Once the whole region is writable,
 *   memory moves between allocators only through transfers.
 */

typedef struct {
	off_t		offset;
	uint64_t	cluster;
	off_t		offset_in_cluster;
	boolean_t	on_disk;
} record_location_t;

static inline boolean_t
try_make_writable(allocator_t *alloc, uint64_t cluster);

/*
 * Each page cluster holds a_records_per_cluster records, stored back to
 * back with no gaps. The cluster size isn't necessarily divisible by
 * a_record_size, so the trailing bytes of a cluster may be waste space.
 * Since records are at most MAX_RECORD_SIZE bytes, the waste is always less
 * than 1% of the cluster at the default page cluster size of 1MB.
 */
static inline record_location_t
locate_record(allocator_t *alloc, record_ix_t rec)
{
	ASSERT(alloc != NULL);
	uint64_t cluster = rec / alloc->a_records_per_cluster;
	uint64_t record_in_cluster = rec % alloc->a_records_per_cluster;
	off_t cluster_offset = cluster * alloc->a_group->ag_cluster_size;
	off_t rec_offset = record_in_cluster * alloc->a_record_size;

	record_location_t loc = {
		.offset = cluster_offset + rec_offset,
		.cluster = cluster,
		.offset_in_cluster = rec_offset,
		.on_disk = cluster >= alloc->a_first_disk_cluster
	};
	return (loc);
}

allocator_t **
allocator_group_init(const size_t *record_sizes, unsigned num_allocators,
    size_t cluster_size, size_t mem_limit, const char *dir_path)
{
	if (mem_limit == 0 && dir_path == NULL)
		errx(1, "allocator group needs either disk or memory backing");
	if (num_allocators == 0)
		errx(1, "invalid number of allocators: %u", num_allocators);
	size_t max_record_size = 0;
	for (unsigned i = 0; i < num_allocators; i++) {
		if (record_sizes[i] == 0 || record_sizes[i] > MAX_RECORD_SIZE)
			errx(1, "requested allocator_t record size is invalid "
			    "(%llu bytes)", (u_longlong_t)record_sizes[i]);
		max_record_size = MAX(max_record_size, record_sizes[i]);
	}

	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize <= 0)
		errx(1, "unable to get system page size");
	/* Headroom so that roundup() and the memory check can't overflow */
	if (cluster_size > SIZE_MAX / 4 / num_allocators)
		errx(1, "requested allocator cluster size %llu is too large",
		    (u_longlong_t)cluster_size);
	if (cluster_size == 0)
		cluster_size = MAX(DEFAULT_CLUSTER_SIZE, pagesize);
	else
		cluster_size = roundup(MAX(MAX(cluster_size, max_record_size),
		    pagesize), pagesize);
	if (mem_limit > SIZE_MAX / 2 - num_allocators * cluster_size)
		errx(1, "requested allocator memory size %llu is too large",
		    (u_longlong_t)mem_limit);

	uint8_t *base_address = NULL;
	if (mem_limit > 0) {
		mem_limit = MAX(mem_limit, num_allocators * cluster_size);
		mem_limit = roundup(mem_limit, cluster_size);
		base_address = mmap(NULL, mem_limit, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (base_address == MAP_FAILED)
			err(1, "unable to reserve %llu bytes of address space",
			    (u_longlong_t)mem_limit);
	}

	allocator_group_t *group = safe_malloc(sizeof (allocator_group_t));
	*group = (allocator_group_t) {
		.ag_clusters_max = mem_limit / cluster_size,
		.ag_cluster_size = cluster_size,
		/*
		 * strdup() can fail, but ag_dir is only used as error
		 * message text; the error value (NULL) is already OK.
		 */
		.ag_dir = (dir_path != NULL) ? strdup(dir_path) : NULL,
		.ag_num_allocators = num_allocators,
		.ag_allocators =
		    safe_malloc(num_allocators * sizeof (allocator_t *)),
		.ag_base_addr = base_address
	};

	/* heap_init() needs 0 < initial size <= limit (0 = none) */
	uint64_t heap_size = MAX(1, MIN(INITIAL_HEAP_SIZE,
	    group->ag_clusters_max));
	for (unsigned i = 0; i < num_allocators; i++) {
		uint8_t **cluster_pointers = NULL;
		if (group->ag_clusters_max > 0)
			cluster_pointers = safe_calloc(sizeof (uint8_t *) *
			    group->ag_clusters_max);
		group->ag_allocators[i] = safe_malloc(sizeof (allocator_t));
		*group->ag_allocators[i] = (allocator_t) {
			.a_fd = (dir_path != NULL) ?
			    safe_create_temp_file(dir_path) : -1,
			.a_record_size = record_sizes[i],
			.a_records_per_cluster = cluster_size / record_sizes[i],
			.a_first_disk_cluster = group->ag_clusters_max,
			.a_clusters = cluster_pointers,
			.a_cluster_heap = heap_init(HEAP_MAX, heap_size,
			    group->ag_clusters_max),
			.a_group = group
		};
	}
	return (group->ag_allocators);
}

void
allocator_store(allocator_t *alloc, record_ix_t record, const void *buff)
{
	VERIFY(alloc != NULL && buff != NULL);
	record_location_t loc;

retry:	loc = locate_record(alloc, record);
	if (loc.on_disk) {
		if (alloc->a_fd < 0)
			errx(1, "allocator memory limit exceeded, record %llu "
			    "(write)", (u_longlong_t)record);
		else
			safe_pwrite(alloc->a_fd, buff, alloc->a_record_size,
			    loc.offset, alloc->a_group->ag_dir);
	} else {
		if (alloc->a_clusters[loc.cluster] == NULL &&
		    try_make_writable(alloc, loc.cluster) == B_FALSE) {
			/*
			 * try_make_writable() moves the split point on
			 * failure, so locate_record() will now return an
			 * on-disk location.
			 */
			goto retry;
		}
		memcpy(alloc->a_clusters[loc.cluster] + loc.offset_in_cluster,
		    buff, alloc->a_record_size);
	}
	alloc->a_record_count = MAX(alloc->a_record_count, record + 1);
}

void
allocator_retrieve(allocator_t *alloc, record_ix_t record, void *buff)
{
	VERIFY(alloc != NULL && buff != NULL);
	record_location_t loc = locate_record(alloc, record);

	if (loc.on_disk) {
		if (alloc->a_fd >= 0)
			safe_pread_zero(alloc->a_fd, buff,
			    alloc->a_record_size, loc.offset);
		else
			/* Fine as a read, would fail as a write */
			memset(buff, 0, alloc->a_record_size);
	} else if (alloc->a_clusters[loc.cluster] == NULL) {
		/* Never written, so we can safely "read" back as zeros */
		memset(buff, 0, alloc->a_record_size);
	} else {
		memcpy(buff, alloc->a_clusters[loc.cluster] +
		    loc.offset_in_cluster, alloc->a_record_size);
	}
}

record_ix_t
allocator_append(allocator_t *alloc, const void *data)
{
	VERIFY(alloc != NULL);
	record_ix_t loc = alloc->a_record_count;
	allocator_store(alloc, loc, data);
	return (loc);
}

record_ix_t
allocator_skip(allocator_t *alloc)
{
	ASSERT(alloc != NULL);
	return (alloc->a_record_count++);
}

/*
 * Force an allocator to flush its highest-numbered memory page cluster to
 * disk and return the address of that memory for reuse. Returns NULL if the
 * allocator has no clusters to evict.
 */
static uint8_t *
evict_last_cluster(allocator_t *alloc)
{
	VERIFY(alloc != NULL);
	VERIFY(alloc->a_fd >= 0);

	uint64_t last_cluster;
	if (!heap_pop(&alloc->a_cluster_heap, &last_cluster))
		return (NULL);
	VERIFY(alloc->a_clusters[last_cluster] != NULL);
	VERIFY(last_cluster < alloc->a_first_disk_cluster);

	allocator_group_t *group = alloc->a_group;
	off_t offset = last_cluster * group->ag_cluster_size;
	uint8_t *buff = alloc->a_clusters[last_cluster];
	safe_pwrite(alloc->a_fd, buff, group->ag_cluster_size, offset,
	    group->ag_dir);
	alloc->a_clusters[last_cluster] = NULL;
	alloc->a_first_disk_cluster = last_cluster;
	memset(buff, 0, group->ag_cluster_size);
	return (buff);
}

/*
 * Try to poach a page cluster from an allocator of lower priority, or from
 * the allocator itself if there's a higher-numbered page cluster available.
 *
 * Self-poaching is allowed because we want to keep as much data in memory
 * as possible. Recycling a page cluster forces the memory/disk boundary to
 * that cluster, so we want to do that on the highest numbered cluster.
 */
static boolean_t
poach_cluster(allocator_t *alloc, uint64_t cluster)
{
	VERIFY(alloc != NULL && alloc->a_clusters[cluster] == NULL);
	allocator_t **allocators = alloc->a_group->ag_allocators;
	unsigned int i = 0;

	do {
		if (allocators[i]->a_fd < 0 || !clusters_owned(allocators[i]))
			continue;
		if (allocators[i] == alloc && heap_compare_root(
		    &allocators[i]->a_cluster_heap, cluster) != 1)
			continue;
		uint8_t *buff = evict_last_cluster(allocators[i]);
		if (buff == NULL)
			return (B_FALSE);
		alloc->a_clusters[cluster] = buff;
		heap_push(&alloc->a_cluster_heap, cluster);
		return (B_TRUE);
	} while (allocators[i++] != alloc);
	return (B_FALSE);
}

/*
 * Attempt to obtain a newly-allocated page cluster from the allocator
 * group. Returns B_TRUE if successful.
 */
static boolean_t
allocate_cluster(allocator_t *alloc, uint64_t cluster)
{
	VERIFY(cluster < alloc->a_first_disk_cluster);
	VERIFY(alloc->a_clusters[cluster] == NULL);

	allocator_group_t *group = alloc->a_group;
	if (group->ag_clusters_current >= group->ag_clusters_max) {
		return (B_FALSE);
	}

	uint8_t *buff = group->ag_base_addr +
	    group->ag_clusters_current * group->ag_cluster_size;
	int rc = mprotect(buff, group->ag_cluster_size,
	    PROT_READ | PROT_WRITE);
	if (rc != 0 && errno == ENOMEM && alloc->a_fd >= 0) {
		if (!group->ag_enomem_warned) {
			warnx("out of memory: spilling to %s...",
			    (group->ag_dir != NULL) ? group->ag_dir : "disk");
			group->ag_enomem_warned = B_TRUE;
		}
		return (B_FALSE);
	} else if (rc != 0) {
		err(1, "mprotect() failed");
	}

	group->ag_clusters_current++;
	alloc->a_clusters[cluster] = buff;
	heap_push(&alloc->a_cluster_heap, cluster);
	return (B_TRUE);
}

/*
 * Attempt to convert one of an allocator's page cluster slots into writable
 * memory. If the allocator group has sufficient unused memory, this can be
 * done directly. If not, we call poach_cluster() to try and recover memory
 * from a lower priority allocator (or from a higher-numbered cluster of the
 * same allocator.)
 *
 * If the attempt to obtain memory from another allocator fails, we lower
 * the allocator's a_first_disk_cluster and return B_FALSE. Retrying the
 * transaction should then succeed as a disk operation.
 */
static inline boolean_t
try_make_writable(allocator_t *alloc, uint64_t cluster)
{
	VERIFY(cluster < alloc->a_first_disk_cluster);
	VERIFY(alloc->a_clusters[cluster] == NULL);
	if (allocate_cluster(alloc, cluster) || poach_cluster(alloc, cluster))
		return (B_TRUE);
	alloc->a_first_disk_cluster = cluster;
	return (B_FALSE);
}

allocator_stats_t
allocator_get_stats(allocator_t *alloc)
{
	VERIFY(alloc != NULL);
	allocator_group_t *group = alloc->a_group;
	allocator_stats_t stats = {
		.as_memory_used =
		    clusters_owned(alloc) * group->ag_cluster_size,
		.as_num_records = alloc->a_record_count,
		.as_record_size = alloc->a_record_size
	};
	return (stats);
}

static inline void
allocator_fini(allocator_t *alloc)
{
	if (alloc->a_fd >= 0 && close(alloc->a_fd) != 0)
		warn("unable to close allocator backing file");
	heap_fini(&alloc->a_cluster_heap);
	free(alloc->a_clusters);
	free(alloc);
}

/*
 * May be called with any allocator in the group. Frees the whole group.
 */
void
allocator_group_fini(allocator_t *alloc)
{
	VERIFY(alloc != NULL);
	allocator_group_t *group = alloc->a_group;
	for (unsigned i = 0; i < group->ag_num_allocators; i++)
		allocator_fini(group->ag_allocators[i]);
	if (group->ag_dir != NULL)
		free(group->ag_dir);
	free(group->ag_allocators);
	if (group->ag_base_addr != NULL) {
		int rc = munmap(group->ag_base_addr,
		    group->ag_clusters_max * group->ag_cluster_size);
		if (rc != 0)
			warn("munmap() failed, ignoring...");
	}
	free(group);
}
