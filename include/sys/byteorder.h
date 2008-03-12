#ifndef _SPL_BYTEORDER_H
#define _SPL_BYTEORDER_H

#include <asm/byteorder.h>

#define LE_16(x)	cpu_to_le16(x)
#define LE_32(x)	cpu_to_le32(x)
#define LE_64(x)	cpu_to_le64(x)
#define BE_16(x)	cpu_to_be16(x)
#define BE_32(x)	cpu_to_be32(x)
#define BE_64(x)	cpu_to_be64(x)

#endif /* SPL_BYTEORDER_H */
