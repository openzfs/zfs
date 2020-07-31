
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
 * Copyright 2014 Brendon Humphrey (brendon.humphrey@mac.com)
 *
 * CDDL HEADER END
 */

#ifndef BMALLOC_H
#define BMALLOC_H

#include <sys/list.h>

/*
 * Knobs and controls
 */

/*
 * Place the allocator in thread-safe mode. If you have an application where the
 * allocator does not have to be thread safe, then removing the mutexes will
 * improve the allocator performance by about 30%.
 */
#define	THREAD_SAFE 1

/*
 * Provide extra locking around the slice lists, as under some conditions,
 * memory handling errors in the application can interfere with the locking
 * strategy used.
 */
// #define	SLICE_SPINLOCK 1

/*
 * Turn on counting of the number of allocations made to each allocator. Major
 * performance killer. Keep turned off.
 */
// #define	COUNT_ALLOCATIONS 1

/*
 * Borrow an idea from the Linux kernel SLUB allocator - namely, have the Slice
 * Allocator simply forget about full slices. They are "found" again when a free
 * occurs from the full slice, and added to the partial list again. This saves a
 * small amount of list processing overhead and storage space. (The performance
 * difference is probably purely academic.)
 *
 * You will want to enable this if hunting memory leaks.
 */
// #define	SLICE_ALLOCATOR_TRACK_FULL_SLABS 1

// #define	DEBUG 1

#ifdef DEBUG

/* Select a logging mechanism. */
// #define	REPORT_PANIC 1
#define	REPORT_LOG 1

/*
 * Check whether an application is writing beyond the number of bytes allocated
 * in a call to bmalloc(). Implemented using buffer poisoning.
 */
#define	SLICE_CHECK_BOUNDS_WRITE 1

/*
 * Check for writes to memory after free. Works in part by poisoning the user
 * memory on free. The idea is that if a buffer is not fully poisoned on
 * allocate, there is evidence of use after free. This may have the side effect
 * of causing other failures - if an application relies on valid data in the
 * memory after free, bad things can happen.
 */
#define	SLICE_CHECK_WRITE_AFTER_FREE 1

/* Check integrity of slice row headers. */
#define	SLICE_CHECK_ROW_HEADERS 1

/*
 * Check that the number of bytes passed to bmalloc to release matches the
 * number of bytes allocated.
 */
#define	SLICE_CHECK_FREE_SIZE 1

/*
 * Instrument the Slice object to detect concurrent threads accessing the data
 * structures - indicative of a serious programming error.
 */
#define	SLICE_CHECK_THREADS 1

/*
 * Have the SA check that any operations performed on a slice are performed on a
 * slice that the the SA actually owns.
 */
#define	SA_CHECK_SLICE_SIZE 1

/* Select correct dependencies based on debug flags. */

#ifdef SLICE_CHECK_WRITE_AFTER_FREE
/* Poison user allocatable portions of slice rows on free. */
#define	SLICE_POISON_USER_SPACE 1
#endif /* SLICE_CHECK_WRITE_AFTER_FREE */

#endif /* DEBUG */

/*
 * Data Types 
 */
typedef uint64_t sa_size_t;
typedef uint8_t sa_byte_t;
typedef uint8_t sa_bool_t;
typedef uint64_t sa_hrtime_t;
typedef uint32_t large_offset_t;

typedef struct slice_allocator {
	
	/*
	 * Statistics
	 */
	uint64_t                        slices_created;    /* slices added to sa */
	uint64_t                        slices_destroyed;  /* empty slices freed */
	uint64_t                        slice_alloc;       /* allocation count */
	uint64_t                        slice_free;        /* free count */
	uint64_t                        slice_alloc_fail;  /* num failed allocs */
	uint64_t			free_slices;	   /* number of empty slices cached */
	
	/*
	 * State
	 */
	
	uint64_t						flags;
	sa_size_t						slice_size;
	list_t							free;
	list_t							partial;
#ifdef SLICE_ALLOCATOR_TRACK_FULL_SLABS
	list_t							full;
#endif /* SLICE_ALLOCATOR_TRACK_FULL_SLABS */
	/* Max alloc size for slice */
	sa_size_t						max_alloc_size;
	/* Number of rows to be allocated in the Slices */
	sa_size_t						num_allocs_per_slice;
	lck_spin_t						*spinlock;
} slice_allocator_t;

// Convenient way to access kernel_memory_allocate and kmem_free
void * osif_malloc(sa_size_t size);
void osif_free(void* buf, sa_size_t size);

//
// Initialises the allocator, must be called before any other function.
//
void bmalloc_init();

//
// Allocate <size> bytes of memory for the application
//
void* bmalloc(uint64_t size, int flags);
void* bzmalloc(uint64_t size, int flags);

//
// Release memory from the application
//
void bfree(void* buf, uint64_t size);

//
// Attempt to release <num_pages> pages of
// memory from the free memory block collection.
// Returns number of pages released.
uint64_t bmalloc_release_pages(uint64_t num_pages);

//
// Manages from free memory within the allocator.
// Should be called periodically (say at least
// every 10 seconds).
// Returns the number of pages released as a result
uint64_t bmalloc_garbage_collect();

//
// Release all remaining memory and allocator resources
//
void bmalloc_fini();

/*
 * Slice allocator interfaces for kmem to use as "slabs" for its caches
 */

void
slice_allocator_init(slice_allocator_t *sa, sa_size_t max_alloc_size);

void *
slice_allocator_alloc(slice_allocator_t *sa, sa_size_t size);

void
slice_allocator_free(slice_allocator_t *sa, void *buf, sa_size_t size);

void
slice_allocator_garbage_collect(slice_allocator_t *sa);

uint64_t
slice_allocator_release_pages(slice_allocator_t *sa, uint64_t num_pages);

void
slice_allocator_fini(slice_allocator_t *sa);


#endif
