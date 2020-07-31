
#ifndef _SPL_ZONE_H
#define _SPL_ZONE_H

#include <sys/byteorder.h>

#define zone_dataset_visible(x, y)                      (1)
#define INGLOBALZONE(z)                                 (1)

static inline unsigned long long getzoneid(void) { return 0; }
#endif /* SPL_ZONE_H */
