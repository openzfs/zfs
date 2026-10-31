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
#include "zstream_util.h"

/*
 * This implementation exploits two features common to most UNIX-like systems.
 *
 * - Filesystem write holes: For a dual-backed allocator, the
 *   memory-resident portion of the data is treated as an overlay of the
 *   first part of the backing file. Memory and disk share the same
 *   addressing scheme for records: record 100 is always at the same offset
 *   whether it's in memory or on disk.
 *
 *   The first part of the disk file hides underneath the memory overlay and
 *   is never written to. Ergo, it occupies no actual storage space. Since
 *   memory and disk have common addressing, they can be rebalanced with a
 *   single write when the memory budget changes.
 *
 *   If the backing file's filesystem does not support holes (unlikely but
 *   possible), the code is still correct. However, actual disk space
 *   consumption will be higher.
 *
 * - PROT_NONE virtual pages: These pages do not consume physical memory,
 *   TLB entries, or swap space, so they are essentially free. Because of
 *   that, allocators can request a large, contiguous VM allocation up front
 *   and need never change their addressing scheme, even as memory use evolves.
 *
 *   When an allocator needs more writable pages, it incrementally changes
 *   pages' protection from PROT_NONE to PROT_READ | PROT_WRITE.
 */

#define	OFFSET_TO_ADDR(alloc, off)	((char *)(alloc)->a_base_addr + (off))
#define	OFFSET_IS_ON_DISK(alloc, off)	((off) >= (alloc)->a_first_on_disk)

static boolean_t
try_make_readwrite(allocator_t *alloc, off_t end_offset);

/*
 * Each page cluster holds a_records_per_cluster records, stored back to
 * back with no gaps. The cluster size isn't necessarily divisible by
 * a_record_size, so the trailing bytes of a cluster may be waste space.
 * Since records are at most MAX_RECORD_SIZE bytes, the waste is always less
 * than 1% of the cluster.
 */
static inline off_t
record_offset(allocator_t *alloc, record_ix_t rec)
{
	ASSERT(alloc != NULL);
	uint64_t cluster = rec / alloc->a_records_per_cluster;
	uint64_t record_in_cluster = rec % alloc->a_records_per_cluster;
	off_t cluster_offset = cluster * alloc->a_group->ag_cluster_size;
	off_t record_offset = record_in_cluster * alloc->a_record_size;
	return (cluster_offset + record_offset);
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
	for (int i = 0; i < num_allocators; i++) {
		if (record_sizes[i] == 0 || record_sizes[i] > MAX_RECORD_SIZE)
			errx(1, "requested allocator_t record size is invalid "
			    "(%llu bytes)", (u_longlong_t)record_sizes[i]);
		max_record_size = MAX(max_record_size, record_sizes[i]);
	}

	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize <= 0)
		errx(1, "unable to get system page size");
	if (cluster_size == 0)
		cluster_size = MAX(DEFAULT_CLUSTER_SIZE, pagesize);
	else
		cluster_size = roundup(MAX(MAX(cluster_size, max_record_size),
		    pagesize), pagesize);
	if (mem_limit != 0)
		mem_limit = MAX(mem_limit, num_allocators * cluster_size);
	if (mem_limit > SIZE_MAX / 2 - num_allocators * cluster_size)
		errx(1, "requested allocator size %llu is too large",
		    (u_longlong_t)mem_limit);

	allocator_group_t *group = safe_malloc(sizeof (allocator_group_t));
	*group = (allocator_group_t) {
		.ag_mem_limit = roundup(mem_limit, cluster_size),
		.ag_mem_current = 0,
		.ag_cluster_size = cluster_size,
		/*
		 * strdup() can fail, but ag_dir is only used as error
		 * message text; the error value (NULL) is already OK.
		 */
		.ag_dir = (dir_path != NULL) ? strdup(dir_path) : NULL,
		.ag_num_allocators = num_allocators,
		.ag_allocators =
		    safe_malloc(num_allocators * sizeof (allocator_t *))
	};

	for (int i = 0; i < num_allocators; i++) {
		void *base = NULL;
		if (group->ag_mem_limit > 0) {
			/*
			 * Each allocator reserves address space for its
			 * maximum possible memory use. This is not yet
			 * "real" memory, but it can still fall afoul of
			 * RLIMIT_AS if that rlimit has been set.
			 */
			base = mmap(NULL, group->ag_mem_limit,
			    PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (base == MAP_FAILED)
				err(1, "mmap failed in %s", __func__);
		}
		group->ag_allocators[i] = safe_malloc(sizeof (allocator_t));
		*group->ag_allocators[i] = (allocator_t) {
			.a_fd = (dir_path != NULL) ?
			    safe_create_temp_file(dir_path) : -1,
			.a_record_size = record_sizes[i],
			.a_records_per_cluster = cluster_size / record_sizes[i],
			.a_first_on_disk = group->ag_mem_limit,
			.a_group = group,
			.a_base_addr = base,
			.a_writable_frontier = 0
		};
	}
	return (group->ag_allocators);
}

void
allocator_store(allocator_t *alloc, record_ix_t record, const void *buff)
{
	VERIFY(alloc != NULL && buff != NULL);
	off_t off = record_offset(alloc, record);
	off_t next_off = off + alloc->a_record_size;
retry:	if (OFFSET_IS_ON_DISK(alloc, off)) {
		if (alloc->a_fd < 0)
			errx(1, "allocator memory limit exceeded, record %llu "
			    "(write)", (u_longlong_t)record);
		safe_pwrite(alloc->a_fd, buff, alloc->a_record_size, off,
		    alloc->a_group->ag_dir);
	} else if (try_make_readwrite(alloc, next_off)) {
		memcpy(OFFSET_TO_ADDR(alloc, off), buff, alloc->a_record_size);
	} else {
		/* If try_make_readwrite() failed, this is now a disk op */
		goto retry;
	}
	alloc->a_count = MAX(alloc->a_count, record + 1);
}

void
allocator_retrieve(allocator_t *alloc, record_ix_t record, void *buff)
{
	VERIFY(alloc != NULL && buff != NULL);
	off_t off = record_offset(alloc, record);
	if (OFFSET_IS_ON_DISK(alloc, off)) {
		/*
		 * If there's no disk backing, we know this record was
		 * never written.
		 */
		if (alloc->a_fd < 0)
			memset(buff, 0, alloc->a_record_size);
		else
			safe_pread_zero(alloc->a_fd, buff,
			    alloc->a_record_size, off);
	} else {
		/*
		 * Even though we are reading from below the memory/disk
		 * split point, we might still be trying to read from a
		 * region that is PROT_NONE. But in that case, the record
		 * has never actually been written, so we can safely "read"
		 * it back as zeros.
		 */
		if (off + alloc->a_record_size <= alloc->a_writable_frontier)
			memcpy(buff, OFFSET_TO_ADDR(alloc, off),
			    alloc->a_record_size);
		else
			memset(buff, 0, alloc->a_record_size);
	}
}

record_ix_t
allocator_append(allocator_t *alloc, const void *data)
{
	record_ix_t loc = alloc->a_count;
	allocator_store(alloc, loc, data);
	return (loc);
}

record_ix_t
allocator_skip(allocator_t *alloc)
{
	ASSERT(alloc != NULL);
	return (alloc->a_count++);
}

/*
 * Try to free at least delta_bytes of memory from a given allocator. This
 * may not be possible if the allocator has insufficient memory to yield.
 * The return value is the amount actually freed. This value will always be
 * a multiple of the cluster size.
 *
 * a_writable_frontier is the end of the memory actually in use, but we need
 * to also lower a_first_on_disk when yielding memory. Otherwise, the
 * allocator will just try to reallocate memory when later records are
 * written.
 *
 * Since memory and disk segments share offset addresses, we only need to
 * perform one copy from memory to disk to change the split point. Only
 * bytes below the writable frontier are written out. Bytes between the
 * frontier and the old memory budget were never written and are logically
 * zero. The corresponding file region has never been written, so it already
 * reads back as zeros.
 */
static size_t
free_memory_from(allocator_t *alloc, size_t delta_bytes)
{
	ASSERT(alloc != NULL && delta_bytes > 0);
	if (alloc->a_base_addr == NULL || alloc->a_first_on_disk == 0 ||
	    alloc->a_fd < 0)
		return (0);
	size_t delta = roundup(delta_bytes, alloc->a_group->ag_cluster_size);
	delta = MIN(delta, alloc->a_writable_frontier);
	off_t eject_start = alloc->a_writable_frontier - delta;
	off_t eject_end = alloc->a_writable_frontier;
	if (eject_end <= eject_start)
		return (0);
	size_t bytes_to_free = eject_end - eject_start;
	void *eject_start_addr = OFFSET_TO_ADDR(alloc, eject_start);
	safe_pwrite(alloc->a_fd, eject_start_addr, bytes_to_free, eject_start,
	    alloc->a_group->ag_dir);
	if (mmap(eject_start_addr, bytes_to_free, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
		err(1, "mmap (frontier shrink) failed");
	alloc->a_writable_frontier = eject_start;
	alloc->a_first_on_disk = eject_start;
	alloc->a_group->ag_mem_current -= bytes_to_free;
	return (bytes_to_free);
}

/*
 * Try to free at least delta_bytes of memory from some combination of
 * allocators of lower priority than the named allocator. Returns the amount
 * of memory actually freed, which may be less than delta_bytes (or 0).
 */
static inline size_t
free_memory_for(allocator_t *alloc, size_t delta_bytes)
{
	ASSERT(alloc != NULL && delta_bytes > 0);
	size_t freed = 0;
	allocator_group_t *group = alloc->a_group;
	for (int i = 0; i < group->ag_num_allocators; i++) {
		if (group->ag_allocators[i] == alloc)
			break;
		freed += free_memory_from(group->ag_allocators[i],
		    delta_bytes - freed);
		if (freed >= delta_bytes)
			break;
	}
	return (freed);
}

/*
 * Check whether an offset corresponds to writable memory, and if not,
 * attempt to expand the allocator's writable region to include it. If the
 * allocator group has sufficient unused memory, this can be done directly.
 * If not, we call free_memory_for() to try and recover memory from lower
 * priority allocators.
 *
 * If the attempt to obtain memory from another allocator fails, we lower
 * the allocator's a_first_on_disk offset to the current writable frontier
 * and return B_FALSE. Retrying the transaction should then succeed as a disk
 * operation.
 *
 * The end_offset parameter and the a_writable_frontier offset are both
 * "+1" markers. I.e., everything below a_writable_frontier is already
 * writable, and try_make_readwrite() makes memory writable up to but not
 * including the end_offset. a_writable_frontier is always the offset of
 * the first byte of an unwritable memory page.
 */
static inline boolean_t
try_make_readwrite(allocator_t *alloc, off_t end_offset)
{
	if (end_offset <= alloc->a_writable_frontier)
		return (B_TRUE);
	allocator_group_t *group = alloc->a_group;
	size_t needed = roundup(end_offset - alloc->a_writable_frontier,
	    group->ag_cluster_size);
	size_t avail = group->ag_mem_limit - group->ag_mem_current;
	if (needed > avail) {
		size_t deficit = needed - avail;
		if (free_memory_for(alloc, deficit) < deficit) {
			alloc->a_first_on_disk = alloc->a_writable_frontier;
			return (B_FALSE);
		}
	}
	/* Checked before, but we may have since done multiple frees */
	VERIFY3U(group->ag_mem_limit - group->ag_mem_current, >=, needed);
	if (mprotect(OFFSET_TO_ADDR(alloc, alloc->a_writable_frontier),
	    needed, PROT_READ | PROT_WRITE) != 0) {
		if (errno != ENOMEM)
			err(1, "mprotect failed");
		/*
		 * Some uncommon situations (e.g., RLIMIT_DATA or strict
		 * overcommit) can result in memory allocation failing
		 * despite the budget being carefully chosen. This is
		 * essentially the same situation as being unable to free
		 * sufficient memory. It's likely that other allocators will
		 * also fail allocations going forward.
		 */
		warn("memory allocation failed%s", (alloc->a_fd >= 0) ?
		    ", spilling to disk" : "");
		alloc->a_first_on_disk = alloc->a_writable_frontier;
		return (B_FALSE);
	}
	alloc->a_writable_frontier += needed;
	group->ag_mem_current += needed;
	return (B_TRUE);
}

allocator_stats_t
allocator_get_stats(allocator_t *alloc)
{
	VERIFY(alloc != NULL);
	allocator_stats_t stats = {
		.as_memory_used = alloc->a_writable_frontier,
		.as_num_records = alloc->a_count,
		.as_record_size = alloc->a_record_size
	};
	return (stats);
}

/*
 * May be called with any allocator in the group. Frees the entire group.
 */
void
allocator_group_fini(allocator_t *alloc)
{
	VERIFY(alloc != NULL);
	allocator_group_t *group = alloc->a_group;
	for (int i = 0; i < group->ag_num_allocators; i++) {
		allocator_t *group_alloc = group->ag_allocators[i];
		if (group_alloc->a_base_addr != NULL)
			munmap(group_alloc->a_base_addr, group->ag_mem_limit);
		if (group_alloc->a_fd >= 0) {
			if (close(group_alloc->a_fd) != 0)
				warn("unable to close allocator backing file");
		}
		free(group_alloc);
	}
	if (group->ag_dir != NULL)
		free(group->ag_dir);
	free(group->ag_allocators);
	free(group);
}
