#ifndef _SPL_BITOPS_COMPAT_H
#define _SPL_BITOPS_COMPAT_H

#include <linux/bitops.h>

#ifndef HAVE_FLS64

static inline int fls64(__u64 x)
{
       __u32 h = x >> 32;
       if (h)
               return fls(h) + 32;
       return fls(x);
}

#endif /* HAVE_FLS64 */

#endif /* _SPL_BITOPS_COMPAT_H */

