#ifndef	_SPL_PMEM_H_
#define	_SPL_PMEM_H_

#include <sys/types.h>

struct spl_dax_device;
struct block_device;

int spl_open_dax_device(struct block_device *bdev, uint64_t expect_capacity,
    struct spl_dax_device **out);
void spl_close_dax_device(struct spl_dax_device *dev);

void spl_dax_device_base_len(struct spl_dax_device *dev, void **base,
    uint64_t *len);

int spl_memcpy_mc(void *dst, const void *src_checked, size_t size);
void spl_memcpy_flushcache(void *dst, const void *src, size_t size);

#endif /* _SPL_PMEM_H_ */
