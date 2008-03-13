#ifndef _SPL_QSORT_H
#define _SPL_QSORT_H

#include <linux/sort.h>

#define qsort(base, num, size, cmp)	sort(base, num, size, cmp, NULL)

#endif /* SPL_QSORT_H */
