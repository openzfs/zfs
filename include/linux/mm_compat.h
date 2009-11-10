#ifndef _SPL_MM_COMPAT_H
#define _SPL_MM_COMPAT_H

#include <linux/mm.h>

/*
 * Linux 2.6.31 API Change.
 * Individual pages_{min,low,high} moved in to watermark array.
 */
#ifndef min_wmark_pages
#define min_wmark_pages(z)	(z->pages_min)
#endif

#ifndef low_wmark_pages
#define low_wmark_pages(z)	(z->pages_low)
#endif

#ifndef high_wmark_pages
#define high_wmark_pages(z)	(z->pages_high)
#endif

#endif /* SPL_MM_COMPAT_H */
