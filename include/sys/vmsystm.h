#ifndef _SPL_VMSYSTM_H
#define _SPL_VMSYSTM_H

#include <linux/mm.h>

#define physmem				num_physpages
#define ptob(pages)			(pages * PAGE_SIZE)
#define membar_producer()		smp_wmb()

#define copyin(from, to, size)		copy_from_user(to, from, size)
#define copyout(from, to, size)		copy_to_user(to, from, size)

#if 0
/* The approximate total number of free pages */
#define freemem				0

/* The average number of free pages over the last 5 seconds */
#define avefree				0

/* The average number of free pages over the last 30 seconds */
#define avefree30			0

/* A guess as to how much memory has been promised to
 * processes but not yet allocated */
#define deficit				0

/* A guess as to how many page are needed to satisfy
 * stalled page creation requests */
#define needfree			0

/* A bootlean the controls the setting of deficit */
#define desperate

/* When free memory is above this limit, no paging or swapping is done */
#define lotsfree			0

/* When free memory is above this limit, swapping is not performed */
#define desfree				0

/* Threshold for many low memory tests, e.g. swapping is
 * more active below this limit */
#define minfree				0
#endif

#endif /* SPL_VMSYSTM_H */
