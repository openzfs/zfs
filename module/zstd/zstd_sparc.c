#ifdef __sparc__
#include <stdint.h>
#include <sys/byteorder.h>
#include "include/sparc_compat.h"
uint64_t __bswapdi2(uint64_t in) {
	return (BSWAP_64(in));
}
uint32_t __bswapsi2(uint32_t in) {
	return (BSWAP_32(in));
}
#endif
