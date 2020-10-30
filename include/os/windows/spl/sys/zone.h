
#ifndef _SPL_ZONE_H
#define _SPL_ZONE_H

#include <sys/byteorder.h>

#define	GLOBAL_ZONEID 0

#define zone_dataset_visible(x, y)                      (1)
#define INGLOBALZONE(z)                                 (1)

#define crgetzoneid(x)                  (GLOBAL_ZONEID)

#endif /* SPL_ZONE_H */
